// PS1 port: the AudioDevice (see PS1AudioDevice.hpp). Included by Audio.cpp like the other backends.
#include "../../../../ps1/spu.hh"
#include "../../../../ps1/xa_music.hh"
#include "../../../../ps1/music_stream.hh" // 63 M5: a theme streamed into the SPU instead of XA

// docs/ports/mania/32 5.1: SFX on the SPU. Each scene's bank (tools/audio/build_sfx.py: Data/Stages/<Folder>/PS1Sfx<id>.bin, the
// samples the objects it runs ask for) is uploaded at the scene load; a registered SFX (LoadSfx: GameConfig's globals,
// the StageConfig's list) with a sample in the bank keeps its SPU address and rate in sfxList[].buffer and its length in
// 44.1 kHz frames in .length (the channel bookkeeping below ends and loops it as the PC mixer would). Channel c plays on
// SPU voice c + 1; FrameInit keys a voice when its channel starts a new play, keeps volume / pan / speed as the PC
// mixer computes them, holds it (pitch 0) while the channel is paused, re-keys it at a loop and keys it off when the
// channel stops.
extern "C" {
volatile uint32 g_ps1SfxBanks      = 0; // banks uploaded (total)
volatile uint32 g_ps1SfxBanksKept  = 0; // of them: equal to the one in SPU RAM, not uploaded (docs/ports/mania/32 9.3)
volatile uint32 g_ps1SfxBankBytes  = 0; // the last bank's sample bytes
volatile uint32 g_ps1SfxBankErrors = 0; // a bank missing / bad / over SPU RAM
volatile uint32 g_ps1SfxPlays      = 0; // voice key-ons (total)
volatile uint32 g_ps1SfxUnloaded   = 0; // plays of an SFX the scene's bank doesn't hold (a gap in sfx_sets.py's sets)
volatile int32 g_ps1SfxUnloadedLast = -1;
volatile uint32 g_ps1SfxVerify      = 0; // GDB sets it: each upload read back and compared (half-size chunks)
volatile uint32 g_ps1SfxVerifyBytes = 0;
volatile uint32 g_ps1SfxVerifyFail  = 0; // chunks that read back different
extern volatile int32 g_ps1FrameSteps; // ps1/rsdk5_main.cpp (9f / choice 182 / 13b)
// docs/ports/mania/32 5.2: music
volatile uint32 g_ps1MusicReady   = 0; // Music.xa + Music.bin found
volatile uint32 g_ps1MusicStarts  = 0; // PlayStream -> an XA track
volatile uint32 g_ps1MusicMissing = 0; // PlayStream of a track not on the disc (the channel keeps its timing only)
volatile uint32 g_ps1MusicResumes = 0; // a file read stopped the music (one CD action at a time): restarted where it was
volatile uint32 g_ps1MusicStreamReady = 0; // 63 M5: the streamed Super body was found on the disc at boot
volatile uint32 g_ps1MusicStreamRequests = 0; // PlayStream of Super.ogg (the hook was reached)
volatile uint32 g_ps1MusicStreamRefused  = 0; // ... and the stream declined it (no body / ring does not fit)
}

// 63 M5: the music channel is playing a streamed body (ps1/music_stream) instead of an XA track
static bool s_musicStream = false;
static const char *const PS1_STREAM_BODY = "Data/Music/Super.adp"; // the Super theme: 500,976 B, no bank can hold it
#define PS1_MUSIC_STREAM_RATE 22050

#define PS1_SFX_VOICE(c) ((c) + 1)
#define PS1_SFX_INFO(s)  ((uint32)(uintptr_t)sfxList[s].buffer) // rate << 16 | SPU address / 8 (0: no sample)

namespace
{
int16 s_voiceSound[CHANNEL_COUNT]; // the SFX the voice plays (-1: none)
uint32 s_voicePlay[CHANNEL_COUNT]; // its channel's playIndex
uint32 s_voiceFrom[CHANNEL_COUNT]; // its SPU start address (a loop re-keys from its loop point)
char s_bankPath[0x40];             // the bank in SPU RAM (a reload of the same scene keeps it: ~1.7 s of reading)
uint32 s_bankCrc;                  // its contents' CRC-32 (docs/ports/mania/32 9.3: another scene's equal bank keeps the samples)

// docs/ports/mania/32 5.2: the stream channel the CD-XA music follows (PlayStream -> LoadStream -> PS1StreamStart)
int8 s_xaChannel = -1, s_xaTrack = -1;
bool s_xaLoop, s_xaPaused;
uint16 s_xaLoopRow, s_xaPad;
uint16 s_cdVolume = 0xFFFF;
// docs/ports/mania/32 6.2: a PlayStream starts its XA at the next FrameInit, unless a video took it (the STR carries its sound)
int8 s_pendChannel = -1, s_videoChannel = -1;
char s_streamName[0x20]; // the last PlayStream's file ("IntroHP.ogg"): the video lookup's key

// a 44.1 kHz sample position of the track (the PC's startPos / loopPoint / GetChannelPos) -> its XA row (18.75 per
// second, 2,016 samples at 37.8 kHz = 6 / 7 of 44.1 kHz; the build pads the track so its loop point starts a row)
uint32 PS1XaRowOf(uint32 pos44) { return ((pos44 * 6 + 3) / 7 + s_xaPad) / 2016; }

struct PS1SfxEntry {
    char name[40];
    uint32 offset, size, rate, frames44;
};

void PS1SfxVoiceMix(int32 c, uint16 &l, uint16 &r, uint16 &pitch)
{
    const ChannelInfo *ch = &channels[c];
    float volL = ch->volume, volR = ch->volume; // AudioDeviceBase::ProcessAudioMixing
    if (ch->pan < 0.0f)
        volR = (1.0f + ch->pan) * ch->volume;
    else
        volL = (1.0f - ch->pan) * ch->volume;
    volL *= engine.soundFXVolume, volR *= engine.soundFXVolume;
    int32 il = (int32)(volL * 0x3FFF), ir = (int32)(volR * 0x3FFF);
    l = (uint16)(il < 0 ? 0 : il > 0x3FFF ? 0x3FFF : il);
    r = (uint16)(ir < 0 ? 0 : ir > 0x3FFF ? 0x3FFF : ir);
    uint32 rate = PS1_SFX_INFO(ch->soundID) >> 16; // speed 16.16: pitch <= 0x1000 x 4 fits 32 bits (no 64-bit helpers)
    uint32 p    = ((rate * PS1_SPU_PITCH_UNIT / 44100) * (uint32)ch->speed) >> 16;
    pitch       = (uint16)(p > 0x3FFF ? 0x3FFF : p);
    if (ch->state & CHANNEL_PAUSED)
        pitch = 0; // held: the SPU stops reading the sample
}

// the SPU address of `pos` (44.1 kHz frames) in the channel's sample: its ADPCM block (28 samples each, after the
// leading block psxavenc writes). Every bank rate is a multiple of 25 (build_sfx.RATES): 32-bit up to ~55 s.
uint32 PS1SfxAddrAt(int16 sound, uint32 pos)
{
    uint32 info = PS1_SFX_INFO(sound), rate = info >> 16, base = (info & 0xFFFF) * 8;
    return base + 16 + (pos * (rate / 25) / (44100 / 25) / 28) * 16;
}
} // namespace

void RSDK::PS1SfxLoadBank(const char *folder, const char *id)
{
    PS1SpuVoiceStop(0xFFFFu << 1); // voices 1-16
    for (int32 c = 0; c < CHANNEL_COUNT; ++c) s_voiceSound[c] = -1;

    char path[0x40];
    // 9g: Mighty / Ray (the Plus characters setting): the scene's bank with their own sounds, where the disc has one
    const char *chr = PS1CharSfxSuffix();
    sprintf_s(path, sizeof(path), "Data/Stages/%s/PS1Sfx%s%s.bin", folder, id, chr);
    if (*chr) { // (fopen: every index the loads use, the folder's local one too)
        FILE *probe = fopen(path, "rb");
        if (probe)
            fclose(probe);
        else
            sprintf_s(path, sizeof(path), "Data/Stages/%s/PS1Sfx%s.bin", folder, id);
    }
    // the same scene again (a restart): its folder's SFX stayed registered (LoadSceneFolder keeps a same-folder
    // StageConfig) with the samples already bound
    if (!strcmp(path, s_bankPath))
        return;
    s_bankPath[0] = 0;
    for (int32 s = 0; s < SFX_COUNT - 1; ++s) sfxList[s].buffer = NULL, sfxList[s].length = 0; // (the last: the stream's)
    FileInfo info;
    InitFileInfo(&info);
    if (!LoadFile(&info, path, FMODE_RB)) {
        g_ps1SfxBankErrors = g_ps1SfxBankErrors + 1;
        return;
    }
    uint32 head[3];
    ReadBytes(&info, head, sizeof(head));
    uint32 count = head[1] & 0xFFFF;
    // the table, then the samples through DATASET_TMP's memory (empty at LoadSceneAssets' start, after the tiles)
    DataStorage *tmp     = &dataStorage[DATASET_TMP];
    PS1SfxEntry *table   = (PS1SfxEntry *)tmp->memoryTable;
    uint32 tableBytes    = count * sizeof(PS1SfxEntry);
    if (head[0] != ('P' | 'S' << 8 | 'F' << 16 | (uint32)'6' << 24) || tableBytes + 4096 > tmp->storageLimit) {
        CloseFile(&info);
        g_ps1SfxBankErrors = g_ps1SfxBankErrors + 1;
        return;
    }
    ReadBytes(&info, table, tableBytes);
    uint32 dataBytes = 0;
    for (uint32 e = 0; e < count; ++e) {
        uint32 end = table[e].offset + table[e].size;
        dataBytes  = end > dataBytes ? end : dataBytes;
    }
    if (PS1_SPU_SAMPLE_START + dataBytes > PS1_SPU_SAMPLE_END) {
        CloseFile(&info);
        g_ps1SfxBankErrors = g_ps1SfxBankErrors + 1;
        return;
    }
    // docs/ports/mania/32 9.3: the same samples as the bank in SPU RAM (the Blue Spheres stages, a UFO's two acts, ...): bind only
    bool same = head[2] && head[2] == s_bankCrc;
    s_bankCrc = 0;
    uint8 *buf      = (uint8 *)tmp->memoryTable + ((tableBytes + 63) & ~63u);
    uint32 bufBytes = (tmp->storageLimit - ((tableBytes + 63) & ~63u)) & ~63u;
    bool verify = g_ps1SfxVerify != 0;
    if (verify)
        bufBytes = (bufBytes / 2) & ~63u;
    if (same)
        g_ps1SfxBanksKept = g_ps1SfxBanksKept + 1;
    for (uint32 done = same ? dataBytes : 0; done < dataBytes; done += bufBytes) {
        uint32 n = dataBytes - done < bufBytes ? dataBytes - done : bufBytes;
        ReadBytes(&info, buf, n);
        PS1SpuWrite(PS1_SPU_SAMPLE_START + done, buf, n);
        if (verify) {
            PS1SpuRead(PS1_SPU_SAMPLE_START + done, buf + bufBytes, n);
            if (memcmp(buf, buf + bufBytes, n))
                g_ps1SfxVerifyFail = g_ps1SfxVerifyFail + 1;
            g_ps1SfxVerifyBytes = g_ps1SfxVerifyBytes + n;
        }
    }
    CloseFile(&info);
    // the samples to the registered SFX with their names (the table is still in TMP: the uploads were after it)
    for (uint32 e = 0; e < count; ++e) {
        RETRO_HASH_MD5(hash);
        GEN_HASH_MD5(table[e].name, hash);
        for (int32 s = 0; s < SFX_COUNT - 1; ++s) {
            if (sfxList[s].scope != SCOPE_NONE && HASH_MATCH_MD5(sfxList[s].hash, hash)) {
                uint32 addr       = PS1_SPU_SAMPLE_START + table[e].offset;
                sfxList[s].buffer = (SAMPLE_FORMAT *)(uintptr_t)(table[e].rate << 16 | addr / 8);
                sfxList[s].length = table[e].frames44;
            }
        }
    }
    g_ps1SfxBanks     = g_ps1SfxBanks + 1;
    g_ps1SfxBankBytes = dataBytes;
    strcpy(s_bankPath, path);
    s_bankCrc = head[2];
}
#if PS1_DEBUG_OVERLAY
// ps1/debug_overlay.cpp: the samples in SPU RAM from 0x1000 (0 after a failed bank load: the counter keeps the last one's)
uint32 RSDK::PS1SfxBankBytes() { return s_bankPath[0] ? g_ps1SfxBankBytes : 0; }
#endif

void RSDK::PS1StreamQueue(int32 c)
{
    if (s_xaChannel == c) { // a new stream on the music's channel: the old track stops now (9f: without waiting)
        PS1XaStopAsync();
        s_xaChannel = -1;
    }
    s_pendChannel = (int8)c;
    const char *name = streamFilePath + (strncmp(streamFilePath, "Data/Music/", 11) ? 0 : 11);
    strncpy(s_streamName, name, sizeof(s_streamName) - 1);
}

const char *RSDK::PS1StreamPendingName() { return s_streamName; }

void RSDK::PS1StreamVideoDone()
{
    if (s_videoChannel >= 0 && (channels[s_videoChannel].state & ~CHANNEL_PAUSED) == CHANNEL_STREAM) {
        channels[s_videoChannel].state   = CHANNEL_IDLE;
        channels[s_videoChannel].soundID = -1;
    }
    s_videoChannel = -1;
    // The STR has released the CD input. Keep its hardware volume and the music cache together.
    PS1SpuSetCdVolume(0, 0), s_cdVolume = 0;
}

void RSDK::PS1StreamStart(int32 c)
{
    int32 track = PS1XaFindTrack(streamFilePath);
    if (track < 0) {
        g_ps1MusicMissing = g_ps1MusicMissing + 1;
        if (s_xaChannel == c)
            s_xaChannel = -1;
        return;
    }
    // 63 M5: the theme no bank can hold streams into the SPU from a 2 KiB ring on voice 17 (docs/ports/mania/63 M5).
    // The ring sits above the scene's SFX bank; if it does not fit (or the body is absent) the XA track below plays.
    const char *name = streamFilePath + (strncmp(streamFilePath, "Data/Music/", 11) ? 0 : 11);
    if (!strcmp(name, "Super.ogg"))
        g_ps1MusicStreamRequests = g_ps1MusicStreamRequests + 1;
    if (!strcmp(name, "Super.ogg") && !g_ps1MusicStreamReady)
        g_ps1MusicStreamRefused = g_ps1MusicStreamRefused + 1;
    if (g_ps1MusicStreamReady && !strcmp(name, "Super.ogg")
        && PS1MusicStreamBegin(streamLoopPoint, PS1_MUSIC_STREAM_RATE,
                               (PS1_SPU_SAMPLE_START + g_ps1SfxBankBytes + 7) & ~7u)) {
        s_musicStream = true, s_xaChannel = c, s_xaPaused = false;
        g_ps1MusicStarts = g_ps1MusicStarts + 1;
        return;
    }
    s_xaChannel = c, s_xaTrack = track, s_xaPaused = false;
    s_xaPad     = PS1XaTrackPad(track);
    s_xaLoop    = streamLoopPoint != 0;
    s_xaLoopRow = s_xaLoop ? PS1XaRowOf(streamLoopPoint) : 0;
    PS1XaPlayAt(track, s_xaLoop, PS1XaRowOf(streamStartPos), s_xaLoopRow);
    g_ps1MusicStarts = g_ps1MusicStarts + 1;
}

bool32 AudioDevice::Init()
{
    PS1SpuInit();
    g_ps1MusicReady = PS1XaInit();
    // 63 M5: find the streamed body now (a directory read), never at the transform
    g_ps1MusicStreamReady = PS1MusicStreamResolve(PS1_STREAM_BODY); // the ring itself is reserved at the first scene load
    InitAudioChannels();
    for (int32 c = 0; c < CHANNEL_COUNT; ++c) s_voiceSound[c] = -1;
    return true;
}

void AudioDevice::Release() {}

void AudioDevice::InitAudioChannels() { AudioDeviceBase::InitAudioChannels(); }

// The XA follows its stream channel: stopped / paused / resumed with it, the CD volume from its volume (the PC mixer
// halves stream samples), its position from the XA's timed position, the channel ending with a non-looping track;
// a track a file read stopped (one CD action at a time) goes on from where it was
static void PS1StreamVolume(ChannelInfo *channel)
{
    int32 v = channel->state == CHANNEL_STREAM
                  ? (int32)(channel->volume * engine.streamVolume * (0x7FFF / 2)) : 0;
    v = v < 0 ? 0 : v > 0x7FFF ? 0x7FFF : v;
    v = (int32)((uint32)v * PS1XaSeamGain() / 0x7FFF); // 63 M8: the loop seam's fade (xa_music)
    uint16 cd = (uint16)v;
    if (cd != s_cdVolume)
        PS1SpuSetCdVolume(cd, cd), s_cdVolume = cd;
}

static void PS1StreamFrame(ChannelInfo *channel)
{
    if ((channel->state & ~CHANNEL_PAUSED) != CHANNEL_STREAM) {
        if (s_musicStream) { // 63 M5: the SPU stream ends with its channel (the drive is released without waiting)
            PS1MusicStreamEnd();
            s_musicStream = false;
            s_xaChannel   = -1;
            return;
        }
        PS1XaStopAsync(); // (9f: without waiting: a real drive's Pause took ~7 vblanks)
        s_xaChannel = -1;
        PS1SpuSetCdVolume(0, 0), s_cdVolume = 0;
        return;
    }
    if (s_musicStream) { // 63 M5: volume and pause of the SPU music stream (FrameInit pumps it); the CD volume stays 0
        int32 v = (int32)(channel->volume * engine.streamVolume * (0x7FFF / 2));
        v       = v < 0 ? 0 : v > 0x7FFF ? 0x7FFF : v;
        PS1MusicStreamVolume((uint16)v, (uint16)v);
        PS1MusicStreamPause((channel->state & CHANNEL_PAUSED) != 0);
        channel->bufferPos += AUDIO_FREQUENCY / 60 * g_ps1FrameSteps; // the PC's timing for GetChannelPos
        return;
    }
    if (PS1XaEnded()) {
        channel->state   = CHANNEL_IDLE;
        channel->soundID = -1;
        s_xaChannel      = -1;
        return;
    }
    if (channel->state & CHANNEL_PAUSED) {
        if (!s_xaPaused)
            PS1XaPause(), s_xaPaused = true;
    }
    else if (s_xaPaused) {
        PS1XaResume(), s_xaPaused = false;
    }
    else if (!PS1XaPlaying()) {
        PS1XaPlayAt(s_xaTrack, s_xaLoop, PS1XaRowOf(channel->bufferPos), s_xaLoopRow);
        g_ps1MusicResumes = g_ps1MusicResumes + 1;
    }
    uint32 at        = PS1XaPositionRows() * 2016;
    channel->bufferPos = at > s_xaPad ? (at - s_xaPad) * 7 / 6 : 0;
    PS1StreamVolume(channel);
}

// Once per engine frame (RunRetroEngine order): the voices follow their channels (see above), then each playing
// channel advances by one frame of 44.1 kHz samples at its speed; an SFX past its length loops or ends exactly as in
// AudioDeviceBase::ProcessAudioMixing, so the game's IsSfxPlaying / GetChannelPos see the PC's state
// The stream a PlayStream queued: its sprite groups and its XA. 9b.2: also called between the object updates and the draw
// lists (RetroEngine.cpp), so what the update that started the music shows (ActClear's victory pose, from the end group)
// is drawn this frame, not missing one frame (the PC starts the stream in PlayStream itself)
void RSDK::PS1AudioStartPending()
{
    if (s_pendChannel < 0)
        return;
    // 63 M5: the streamed music owns the drive until its action ends; a track queued behind it starts after that
    if (s_musicStream || PS1MusicStreamActive()) {
        if (s_musicStream && channels[s_pendChannel].state == CHANNEL_STREAM) { // another track replaces the stream
            PS1MusicStreamEnd();
            s_musicStream = false;
            s_xaChannel   = -1;
        }
    }
    if (PS1MusicStreamClosing())
        return; // the drive is still releasing: the queued track starts on a later frame
    if (sceneInfo.state == ENGINESTATE_VIDEOPLAYBACK) // the video owns the CD (ps1/fmv)
        s_videoChannel = s_pendChannel; // its sound plays from the STR
    else if ((channels[s_pendChannel].state & ~CHANNEL_PAUSED) == CHANNEL_STREAM) {
        PS1AtlasMusicGroups(s_streamName); // 7b: the sprite groups this music brings (before its XA: the CD is free)
        PS1StreamStart(s_pendChannel);
    }
    s_pendChannel = -1;
}

void AudioDevice::FrameInit()
{
    const uint32 frameSamples = AUDIO_FREQUENCY / 60 * g_ps1FrameSteps; // one drawn frame = g_ps1FrameSteps logic frames
    bool video                = sceneInfo.state == ENGINESTATE_VIDEOPLAYBACK; // the video owns the CD (ps1/fmv)
    if (!video && !PS1MusicStreamActive() && !PS1MusicStreamClosing())
        PS1XaService(); // 9f: a track queued behind a stop that didn't wait (63 M5: never while the stream owns the drive)
    PS1MusicStreamPump(); // 63 M5: refill the ring, and finish a close even after its channel released the stream
    PS1AudioStartPending();
    // A video owns the drive, but its PlayStream still owns the music volume. Ending scenes stop
    // the previous track first (CD volume 0); skipping PS1StreamFrame must not leave the STR muted.
    if (video && s_videoChannel >= 0)
        PS1StreamVolume(&channels[s_videoChannel]);
    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        ChannelInfo *channel = &channels[c];
        bool sfx             = (channel->state & ~CHANNEL_PAUSED) == CHANNEL_SFX && channel->soundID >= 0;
        if (!sfx) {
            if (s_voiceSound[c] >= 0) {
                PS1SpuVoiceStop(1u << PS1_SFX_VOICE(c));
                s_voiceSound[c] = -1;
            }
            if (c == s_xaChannel && !video)
                PS1StreamFrame(channel);
            else if (channel->state == CHANNEL_STREAM)
                channel->bufferPos += frameSamples; // a track the disc lacks: timing only
            continue;
        }
        uint16 l, r, pitch;
        if (s_voiceSound[c] != channel->soundID || s_voicePlay[c] != channel->playIndex) {
            s_voiceSound[c] = channel->soundID, s_voicePlay[c] = channel->playIndex;
            if (!PS1_SFX_INFO(channel->soundID)) {
                g_ps1SfxUnloaded = g_ps1SfxUnloaded + 1, g_ps1SfxUnloadedLast = channel->soundID;
                s_voiceFrom[c] = 0;
            }
            else {
                PS1SfxVoiceMix(c, l, r, pitch);
                s_voiceFrom[c] = PS1SfxAddrAt(channel->soundID, 0) - 16;
                PS1SpuVoicePlay(PS1_SFX_VOICE(c), s_voiceFrom[c], 44100, l, r);
                PS1SpuVoicePitch(PS1_SFX_VOICE(c), pitch);
                g_ps1SfxPlays = g_ps1SfxPlays + 1;
            }
        }
        else if (s_voiceFrom[c]) {
            PS1SfxVoiceMix(c, l, r, pitch);
            PS1SpuVoiceVolume(PS1_SFX_VOICE(c), l, r);
            PS1SpuVoicePitch(PS1_SFX_VOICE(c), pitch);
        }
        if (channel->state & CHANNEL_PAUSED)
            continue;
        // the PC mixer's bookkeeping
        channel->bufferPos += (frameSamples * (uint32)channel->speed) >> 16;
        if (!channel->sampleLength) { // no sample here: one-shots end, loops keep playing
            channel->bufferPos = 0;
            if (channel->loop == (uint32)-1) {
                channel->state   = CHANNEL_IDLE;
                channel->soundID = -1;
            }
        }
        else if (channel->bufferPos >= channel->sampleLength) {
            if (channel->loop == (uint32)-1) {
                channel->state   = CHANNEL_IDLE;
                channel->soundID = -1;
            }
            else {
                channel->bufferPos = (channel->bufferPos - (uint32)channel->sampleLength) % (uint32)channel->sampleLength;
                channel->bufferPos += channel->loop;
                if (s_voiceFrom[c]) { // the voice starts over from the loop point
                    PS1SfxVoiceMix(c, l, r, pitch);
                    PS1SpuVoicePlay(PS1_SFX_VOICE(c), PS1SfxAddrAt(channel->soundID, channel->loop), 44100, l, r);
                    PS1SpuVoicePitch(PS1_SFX_VOICE(c), pitch);
                }
            }
        }
    }
}
