#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_PLATFORM == RETRO_PS1
// PS1 port (docs/ports/mania/32 6.2): no Ogg/Theora. A video plays as an STR sequence (tools/fmv/build_str.py: the pictures with the
// music stream the game starts alongside, as CD-XA inside) through ps1/fmv (Sonic CD's MDEC player, docs/ports/rsdk-common/31's fixes).
// LoadVideo records the video; the PlayStream of the same frame (before or after it) names the sound; the first
// ProcessVideo looks the pair (and the scene's id) up in Data/Video/PS1Video.bin and starts the sequence. A video
// inside an earlier sequence (an ending's second part) returns false, as on the PC when a video is missing.
#include "../../../ps1/video_ops.hh" // the player is linked into the overlays of the folders with videos
FileInfo VideoManager::file;
bool32 VideoManager::initializing = false;

extern "C" {
volatile uint32 g_ps1VideoMissing = 0; // a (video, stream, scene) with no sequence on the disc
volatile uint32 g_ps1VideoDone    = 0; // videos the engine returned from
}
namespace
{
char s_ps1VideoName[16];
bool s_ps1VideoStarted = false;

struct PS1VideoRow { // PS1Video.bin (tools/fmv/build_str.py)
    char video[16], audio[16], scene[8], sequence[16];
};
// the row for video (+ audio, when given) in this scene; false if none
bool PS1VideoFind(const char *video, const char *audio, PS1VideoRow *out)
{
    FileInfo info;
    InitFileInfo(&info);
    if (!LoadFile(&info, "Data/Video/PS1Video.bin", FMODE_RB))
        return false;
    uint32 head[2];
    ReadBytes(&info, head, sizeof(head));
    const char *scene = sceneInfo.listData ? sceneInfo.listData[sceneInfo.listPos].id : "";
    bool found        = false;
    for (uint32 n = head[1] & 0xFFFF; n && !found; --n) {
        ReadBytes(&info, out, sizeof(*out));
        out->video[15] = out->audio[15] = out->scene[7] = out->sequence[15] = 0;
        found = !strcmp(out->video, video) && (!audio || !out->audio[0] || !strcmp(out->audio, audio))
                && (!out->scene[0] || !strcmp(out->scene, scene));
    }
    CloseFile(&info);
    return found;
}
void PS1VideoFinish()
{
    PS1StreamVideoDone(); // the stream it played with ended with it (its sound was the STR's)
    sceneInfo.state        = engine.storedState;
    g_ps1VideoDone         = g_ps1VideoDone + 1;
}
} // namespace

bool32 RSDK::LoadVideo(const char *filename, double startDelay, bool32 (*skipCallback)())
{
    if (sceneInfo.state == ENGINESTATE_VIDEOPLAYBACK || !g_ps1VideoOps.request)
        return false;
    PS1VideoRow row;
    if (PS1VideoFind(filename, NULL, &row) && !row.sequence[0])
        return false; // inside an earlier sequence
    strncpy(s_ps1VideoName, filename, sizeof(s_ps1VideoName) - 1);
    s_ps1VideoStarted   = false;
    engine.skipCallback = skipCallback;
    engine.storedState  = sceneInfo.state;
    sceneInfo.state     = ENGINESTATE_VIDEOPLAYBACK;
    return true;
}

void RSDK::ProcessVideo()
{
    if (!s_ps1VideoStarted) { // the frame after LoadVideo: its PlayStream has been seen
        s_ps1VideoStarted = true;
        PS1VideoRow row;
        if (!PS1VideoFind(s_ps1VideoName, PS1StreamPendingName(), &row) || !row.sequence[0]) {
            g_ps1VideoMissing = g_ps1VideoMissing + 1;
            PS1VideoFinish();
            return;
        }
        g_ps1VideoOps.request(row.sequence, 0); // opens at the next frame's start (nothing chained yet)
        return;
    }
    bool skip = engine.skipCallback && engine.skipCallback();
    if (!g_ps1VideoOps.update || g_ps1VideoOps.update(skip))
        PS1VideoFinish();
}
#else
FileInfo VideoManager::file;

ogg_sync_state VideoManager::oy;
ogg_page VideoManager::og;
ogg_packet VideoManager::op;
ogg_stream_state VideoManager::vo;
ogg_stream_state VideoManager::to;
th_info VideoManager::ti;
th_comment VideoManager::tc;
th_dec_ctx *VideoManager::td    = NULL;
th_setup_info *VideoManager::ts = NULL;

th_pixel_fmt VideoManager::pixelFormat;
ogg_int64_t VideoManager::granulePos = 0;
bool32 VideoManager::initializing    = false;

bool32 RSDK::LoadVideo(const char *filename, double startDelay, bool32 (*skipCallback)())
{
    if (ENGINE_VERSION == 5 && sceneInfo.state == ENGINESTATE_VIDEOPLAYBACK)
        return false;
#if RETRO_REV0U
    if (ENGINE_VERSION == 3 && RSDK::Legacy::gameMode == RSDK::Legacy::v3::ENGINE_VIDEOWAIT)
        return false;
#endif

    char fullFilePath[0x80];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Video/%s", filename);

    InitFileInfo(&VideoManager::file);
    if (LoadFile(&VideoManager::file, fullFilePath, FMODE_RB)) {
        // Init
        ogg_sync_init(&VideoManager::oy);

        th_comment_init(&VideoManager::tc);
        th_info_init(&VideoManager::ti);

        int32 theora_p = 0;
        char *buffer   = NULL;

        // Parse header stuff
        bool32 finishedHeader = false;
        while (!finishedHeader) {
            buffer    = ogg_sync_buffer(&VideoManager::oy, 0x1000);
            int32 ret = (int32)ReadBytes(&VideoManager::file, buffer, 0x1000);
            ogg_sync_wrote(&VideoManager::oy, 0x1000);

            if (ret == 0)
                break;

            while (ogg_sync_pageout(&VideoManager::oy, &VideoManager::og) > 0) {
                ogg_stream_state test;

                /* is this a mandated initial header? If not, stop parsing */
                if (!ogg_page_bos(&VideoManager::og)) {
                    /* don't leak the page; get it into the appropriate stream */
                    ogg_stream_pagein(&VideoManager::to, &VideoManager::og);
                    finishedHeader = true;
                    break;
                }

                ogg_stream_init(&test, ogg_page_serialno(&VideoManager::og));
                ogg_stream_pagein(&test, &VideoManager::og);
                ogg_stream_packetout(&test, &VideoManager::op);

                // identify codec
                if (!theora_p && th_decode_headerin(&VideoManager::ti, &VideoManager::tc, &VideoManager::ts, &VideoManager::op) >= 0) {
                    // theora
                    memcpy(&VideoManager::to, &test, sizeof(test));
                    theora_p = 1;
                }
                else {
                    // we dont care (possibly vorbis)
                    ogg_stream_clear(&test);
                }
            }
        }

        if (theora_p) {
            VideoManager::ts = NULL;
            theora_p         = 1;
            while (theora_p && theora_p < 3) {
                int32 ret;

                while (theora_p && (theora_p < 3) && (ret = ogg_stream_packetout(&VideoManager::to, &VideoManager::op))) {
                    if (ret < 0) {
#if !RETRO_USE_ORIGINAL_CODE
                        PrintLog(PRINT_NORMAL, "ERROR: failed to parse theora stream headers. corrupted stream?");
#endif

                        theora_p = 0;
                        break;
                    }

                    if (!th_decode_headerin(&VideoManager::ti, &VideoManager::tc, &VideoManager::ts, &VideoManager::op)) {
#if !RETRO_USE_ORIGINAL_CODE
                        PrintLog(PRINT_NORMAL, "ERROR: failed to parse theora stream headers. corrupted stream?");
#endif

                        theora_p = 0;
                        break;
                    }

                    theora_p++;
                }

                if (!theora_p)
                    break;

                /* The header pages/packets will arrive before anything else we
                   care about, or the stream is not obeying spec */

                if (ogg_sync_pageout(&VideoManager::oy, &VideoManager::og) > 0) {
                    ogg_stream_pagein(&VideoManager::to, &VideoManager::og);
                }
                else {
                    buffer    = ogg_sync_buffer(&VideoManager::oy, 0x1000);
                    int32 ret = (int32)ReadBytes(&VideoManager::file, buffer, 0x1000);
                    ogg_sync_wrote(&VideoManager::oy, 0x1000);
                    if (ret == 0) {
#if !RETRO_USE_ORIGINAL_CODE
                        PrintLog(PRINT_NORMAL, "ERROR: Reached end of file while searching for codec headers.");
#endif

                        theora_p = 0;
                    }
                }
            }

            if (!theora_p) {
                th_info_clear(&VideoManager::ti);
                th_comment_clear(&VideoManager::tc);
                th_setup_free(VideoManager::ts);
            }
            else {
                VideoManager::td          = th_decode_alloc(&VideoManager::ti, VideoManager::ts);
                VideoManager::pixelFormat = VideoManager::ti.pixel_fmt;

                int32 ppLevelMax = 0;
                th_decode_ctl(VideoManager::td, TH_DECCTL_GET_PPLEVEL_MAX, &ppLevelMax, sizeof(int32));
                int32 ppLevel = 0;
                th_decode_ctl(VideoManager::td, TH_DECCTL_SET_PPLEVEL, &ppLevel, sizeof(int32));

                th_setup_free(VideoManager::ts);

                engine.storedShaderID     = videoSettings.shaderID;
                videoSettings.screenCount = 0;

                if (ENGINE_VERSION == 5)
                    engine.storedState = sceneInfo.state;
#if RETRO_REV0U
                else if (ENGINE_VERSION == 3)
                    engine.storedState = RSDK::Legacy::gameMode;
#endif
                engine.displayTime         = 0.0;
                VideoManager::initializing = true;
                VideoManager::granulePos   = 0;

                engine.displayTime     = 0.0;
                engine.videoStartDelay = 0.0;
                if (AudioDevice::audioState == 1)
                    engine.videoStartDelay = startDelay;

                switch (VideoManager::pixelFormat) {
                    default: break;
                    case TH_PF_420: videoSettings.shaderID = SHADER_YUV_420; break;
                    case TH_PF_422: videoSettings.shaderID = SHADER_YUV_422; break;
                    case TH_PF_444: videoSettings.shaderID = SHADER_YUV_444; break;
                }

                engine.skipCallback = NULL;
                ProcessVideo();
                engine.skipCallback = skipCallback;

                changedVideoSettings = false;
                if (ENGINE_VERSION == 5)
                    sceneInfo.state = ENGINESTATE_VIDEOPLAYBACK;
#if RETRO_REV0U
                else if (ENGINE_VERSION == 3)
                    RSDK::Legacy::gameMode = RSDK::Legacy::v3::ENGINE_VIDEOWAIT;
#endif

                return true;
            }
        }

        CloseFile(&VideoManager::file);
    }

    return false;
}

void RSDK::ProcessVideo()
{
    bool32 finished = false;
    double curTime  = 0;
    if (!VideoManager::initializing) {
        double streamPos = GetVideoStreamPos();

        if (streamPos <= -1.0)
            engine.displayTime += (1.0 / 60.0); // deltaTime frame-step
        else
            engine.displayTime = streamPos;

        curTime = th_granule_time(VideoManager::td, VideoManager::granulePos);

#if RETRO_USE_MOD_LOADER
        RunModCallbacks(MODCB_ONVIDEOSKIPCB, (void *)engine.skipCallback);
#endif
        if (engine.skipCallback && engine.skipCallback()) {
            finished = true;
        }
    }

    if (!finished && (VideoManager::initializing || engine.displayTime >= engine.videoStartDelay + curTime)) {
        while (ogg_stream_packetout(&VideoManager::to, &VideoManager::op) <= 0) {
            char *buffer = ogg_sync_buffer(&VideoManager::oy, 0x1000);
            // if we're playing and reached the end of file
            if (!ReadBytes(&VideoManager::file, buffer, 0x1000) && !VideoManager::initializing) {
                finished = true;
                break;
            }

            ogg_sync_wrote(&VideoManager::oy, 0x1000);

            while (ogg_sync_pageout(&VideoManager::oy, &VideoManager::og) > 0) ogg_stream_pagein(&VideoManager::to, &VideoManager::og);
        }

        if (!finished && !th_decode_packetin(VideoManager::td, &VideoManager::op, &VideoManager::granulePos)) {
            th_ycbcr_buffer yuv;
            th_decode_ycbcr_out(VideoManager::td, yuv);

            int32 dataPos = (VideoManager::ti.pic_x & 0xFFFFFFFE) + (VideoManager::ti.pic_y & 0xFFFFFFFE) * yuv[0].stride;
            switch (VideoManager::pixelFormat) {
                default: break;

                case TH_PF_444:
                    RenderDevice::SetupVideoTexture_YUV444(yuv[0].width, yuv[0].height, &yuv[0].data[dataPos], &yuv[1].data[dataPos],
                                                           &yuv[2].data[dataPos], yuv[0].stride, yuv[1].stride, yuv[2].stride);
                    break;

                case TH_PF_422:
                    RenderDevice::SetupVideoTexture_YUV422(yuv[0].width, yuv[0].height, &yuv[0].data[dataPos],
                                                           &yuv[1].data[yuv[1].stride * VideoManager::ti.pic_y + (VideoManager::ti.pic_x >> 1)],
                                                           &yuv[2].data[yuv[1].stride * VideoManager::ti.pic_y + (VideoManager::ti.pic_x >> 1)],
                                                           yuv[0].stride, yuv[1].stride, yuv[2].stride);
                    break;

                case TH_PF_420:
                    RenderDevice::SetupVideoTexture_YUV420(
                        yuv[0].width, yuv[0].height, &yuv[0].data[dataPos],
                        &yuv[1].data[yuv[1].stride * (VideoManager::ti.pic_y >> 1) + (VideoManager::ti.pic_x >> 1)],
                        &yuv[2].data[yuv[1].stride * (VideoManager::ti.pic_y >> 1) + (VideoManager::ti.pic_x >> 1)], yuv[0].stride, yuv[1].stride,
                        yuv[2].stride);
                    break;
            }
        }

        VideoManager::initializing = false;
    }

    if (finished) {
        CloseFile(&VideoManager::file);

        // Flush everything out
        while (ogg_sync_pageout(&VideoManager::oy, &VideoManager::og) > 0) ogg_stream_pagein(&VideoManager::to, &VideoManager::og);

        ogg_stream_clear(&VideoManager::to);
        th_decode_free(VideoManager::td);
        th_comment_clear(&VideoManager::tc);
        th_info_clear(&VideoManager::ti);
        ogg_sync_clear(&VideoManager::oy);

        videoSettings.shaderID    = engine.storedShaderID;
        videoSettings.screenCount = 1;
        if (ENGINE_VERSION == 5)
            sceneInfo.state = engine.storedState;
#if RETRO_REV0U
        else if (ENGINE_VERSION == 3)
            RSDK::Legacy::gameMode = engine.storedState;
#endif
    }
}
#endif // RETRO_PLATFORM == RETRO_PS1
