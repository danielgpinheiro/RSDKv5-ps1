// PS1 port: the RenderDevice (see PS1RenderDevice.hpp). Included by Drawing.cpp like the other backends.
#include "../../../../ps1/video.hh"         // PS1Hblanks (root counter 1)
#include "../../../../ps1/gpu_chain_raw.hh" // the chained renderer (docs/32 3.2)

// 3.2: each frame is one DMA chain (ps1/gpu_chain, from Sonic CD): PS1RenderBeginFrame picks this parity's arena
// (nothing chained yet, so a load in this frame can still lend the arenas to the CD windows), the first draw opens the
// frame (a clear), PS1RenderEndFrame closes it; a frame that drew nothing (a load) copies the last one over, so both
// buffers show it. psyqo sends the chain after frame() returns.
extern "C" {
volatile uint32 g_ps1RenderFrames  = 0; // frames opened with a clear
volatile uint32 g_ps1RenderRepeats = 0; // frames that drew nothing (the last frame copied over)
volatile uint32 g_ps1ClearColour   = 0; // clear colour, r | g << 8 | b << 16 (GDB; 3.2's test)
}
namespace
{
#define PS1_SCREEN_W_CLEAR 320
bool s_ps1FrameOpen = false;
void *s_ps1FrameClear = NULL; // this frame's clear (FastFill in the arena) and its colour: a FillScreen blends it too (3.5)
uint32 s_ps1FrameClearColour = 0;
}
#include "../../../../ps1/video_ops.hh" // 6.2: the video player (an overlay's): a video opens / closes at a frame's start

void RSDK::PS1RenderBeginFrame()
{
    PS1ChainBeginFrame();
    if (g_ps1VideoOps.beginFrame)
        g_ps1VideoOps.beginFrame(); // (nothing chained yet: it swaps the arenas)
    s_ps1FrameOpen = false;
}

// docs/32 6.2: video frames (ps1/fmv.cpp, from Sonic CD's render.cpp): 0 = black, 1 = a new picture was just written
// into the other buffer (the one shown after this frame's flip): this frame copies it too, so both hold it (the bars
// filled black); 2 = no new picture: the displayed frame is copied over (the skip fade darkens it a step each frame).
// Rows the video frame leaves alone: the diagnostics strip (ps1/fmv.cpp), uploaded into both buffers, in two bands.
#define PS1_FMV_Y0 40  // 160 picture lines letterboxed in 240 (ps1/fmv.cpp FMV_Y)
#define PS1_FMV_Y1 200
static int s_ps1FmvKeep[2][2] = { { 0, 0 }, { 0, 0 } };
void PS1RenderFmvKeepRows(int y0, int y1, int y2, int y3)
{
    s_ps1FmvKeep[0][0] = y0, s_ps1FmvKeep[0][1] = y1;
    s_ps1FmvKeep[1][0] = y2, s_ps1FmvKeep[1][1] = y3;
}
// fill (copy = false) or copy from the last frame the lines [y0, y1) minus the kept bands
static void PS1FmvLines(int y0, int y1, bool copy, int band = 0)
{
    if (y0 >= y1)
        return;
    if (band == 2) {
        if (copy)
            PS1ChainCopyLastFrameLines(y0, y1);
        else
            PS1ChainFillLines(y0, y1, 0, 0, 0);
        return;
    }
    int k0 = s_ps1FmvKeep[band][0] > y0 ? s_ps1FmvKeep[band][0] : y0;
    int k1 = s_ps1FmvKeep[band][1] < y1 ? s_ps1FmvKeep[band][1] : y1;
    if (k0 >= k1) {
        PS1FmvLines(y0, y1, copy, band + 1);
        return;
    }
    PS1FmvLines(y0, k0, copy, band + 1);
    PS1FmvLines(k1, y1, copy, band + 1);
}
void PS1RenderFmvFrame(int mode)
{
    if (s_ps1FrameOpen)
        return;
    s_ps1FrameOpen = true;
    if (mode == 1) {
        PS1FmvLines(0, PS1_FMV_Y0, false);
        PS1FmvLines(PS1_FMV_Y0, PS1_FMV_Y1, true);
        PS1FmvLines(PS1_FMV_Y1, SCREEN_YSIZE, false);
    }
    else {
        PS1FmvLines(0, SCREEN_YSIZE, mode == 2);
    }
}
// the skip fade (upstream ProcessVideo: += 8 a frame): the frame, a copy of the last, darkened by step / 255 with
// subtractive blending (texpage semi-transparency mode 2, B - F), so 32 frames reach black
void PS1RenderFmvFade(int step)
{
    PS1ChainTPageRaw(2 << 5);
    uint32_t w[3] = { 0x62000000u | (uint32_t)step | (uint32_t)step << 8 | (uint32_t)step << 16, 0,
                      (uint32_t)PS1_SCREEN_W_CLEAR | (uint32_t)SCREEN_YSIZE << 16 };
    PS1ChainPrimsRawN(w, 3, 1);
    PS1ChainInvalidateTPage();
}
void RSDK::PS1FrameStart(bool clear)
{
    if (s_ps1FrameOpen)
        return;
    s_ps1FrameOpen = true;
    if (clear) {
        uint32 c              = g_ps1ClearColour;
        s_ps1FrameClear       = PS1ChainClearRGB(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
        s_ps1FrameClearColour = c;
        if (maskColor) {
            // 4.1b: the fill leaves bit 15 clear (psx-spx "Quick Rectangle Fill"), which INK_MASKED's mask test would
            // take for the mask colour: while a mask colour is set, the cleared screen is also a flat rectangle of the
            // clear colour drawn with the mask bit forced (its colour recorded for a FillScreen, like the clear's)
            uint32_t *w = PS1ChainSpanBegin(6);
            if (w) {
                w[1] = 0xE6000001;
                w[2] = 0x60000000 | (c & 0xFFFFFF);
                PS1FadeRecordColour(&w[2]);
                w[3] = 0;
                w[4] = (uint32)PS1_SCREEN_W_CLEAR | (uint32)SCREEN_YSIZE << 16;
                w[5] = 0xE6000000;
                PS1ChainSpanEnd(w, 5, w + 6);
            }
        }
        g_ps1RenderFrames = g_ps1RenderFrames + 1;
    }
    else {
        s_ps1FrameClear = NULL;
        PS1ChainRepeatLastFrame();
        g_ps1RenderRepeats = g_ps1RenderRepeats + 1;
    }
}
void RSDK::PS1RenderEndFrame()
{
    if (!s_ps1FrameOpen)
        PS1FrameStart(false);
    PS1ChainEnd();
}

#define PS1_SCREEN_W 320 // user decision: 320 px (PC 424)
#define PS1_SCREEN_H SCREEN_YSIZE

bool RenderDevice::Init()
{
    // What SetupRendering / InitGraphicsAPI set up on the PC: one 320 x 240 screen.
    videoSettings.windowed    = true;
    videoSettings.windowWidth = PS1_SCREEN_W;
    videoSettings.windowHeight = PS1_SCREEN_H;
    videoSettings.pixWidth    = PS1_SCREEN_W;
    videoSettings.pixHeight   = PS1_SCREEN_H;
    videoSettings.shaderSupport = false;
    viewSize.x = PS1_SCREEN_W;
    viewSize.y = PS1_SCREEN_H;
    pixelSize.x = PS1_SCREEN_W;
    pixelSize.y = PS1_SCREEN_H;
    textureSize.x = PS1_SCREEN_W;
    textureSize.y = PS1_SCREEN_H;
    for (int32 s = 0; s < SCREEN_COUNT; ++s) {
        SetScreenSize(s, PS1_SCREEN_W, PS1_SCREEN_H);
    }
    GetDisplays();
    if (!SetupRendering() || !AudioDevice::Init())
        return false;
    InitInputDevices();
    return true;
}

bool RenderDevice::SetupRendering()
{
    // Headless (phase 2): no software framebuffer; the Draw* functions return before touching pixels (Drawing.cpp,
    // RETRO_PS1_HEADLESS). Phase 3 draws through the GPU chain.
    for (int32 s = 0; s < SCREEN_COUNT; ++s)
        screens[s].frameBuffer = NULL;
    // The per-line scroll positions the layers' parallax (Scene.cpp ProcessParallax) and the game's scanline
    // callbacks write every frame: every upstream device allocates them here (DX9RenderDevice.cpp), and with it
    // missing they wrote through NULL into the BIOS kernel's RAM (docs/32 2.2: stale saved registers, then crashes).
    // 3.2: static (5 KB): allocated here it landed between the CD read-ahead windows' heap blocks and split the heap
    // the chain arenas take in play (ps1/gpu_chain) into 32 + 14 KB.
    static ScanlineInfo s_ps1Scanlines[PS1_SCREEN_W > SCREEN_YSIZE ? PS1_SCREEN_W : SCREEN_YSIZE];
    if (!scanlines) {
        scanlines = s_ps1Scanlines;
        memset(scanlines, 0, sizeof(s_ps1Scanlines));
    }
    videoSettings.screenCount = 1;
    engine.inFocus = 1;
    videoSettings.windowState = WINDOWSTATE_ACTIVE;
    videoSettings.dimMax      = 1.0; // as every upstream SetupRendering
    videoSettings.dimPercent  = 1.0;
    return true;
}

bool RenderDevice::InitGraphicsAPI() { return true; }
void RenderDevice::InitVertexBuffer() {}
void RenderDevice::GetDisplays()
{
    static WindowInfo::Display s_display = { PS1_SCREEN_W, PS1_SCREEN_H, 60 };
    displayInfo.displays = &s_display;
    displayCount = 1;
    displayWidth[0]  = PS1_SCREEN_W;
    displayHeight[0] = PS1_SCREEN_H;
}
void RenderDevice::CopyFrameBuffer() {}
void RenderDevice::FlipScreen()
{
    PS1FrameStart(true); // 3.2: nothing draws yet (3.3+), the frame is its clear
}
void RenderDevice::Release(bool32 isRefresh) {}
void RenderDevice::RefreshWindow() {}
void RenderDevice::GetWindowSize(int32 *width, int32 *height)
{
    if (width)
        *width = PS1_SCREEN_W;
    if (height)
        *height = PS1_SCREEN_H;
}
void RenderDevice::SetupImageTexture(int32 width, int32 height, uint8 *imagePixels) {}
void RenderDevice::SetupVideoTexture_YUV420(int32, int32, uint8 *, uint8 *, uint8 *, int32, int32, int32) {}
void RenderDevice::SetupVideoTexture_YUV422(int32, int32, uint8 *, uint8 *, uint8 *, int32, int32, int32) {}
void RenderDevice::SetupVideoTexture_YUV444(int32, int32, uint8 *, uint8 *, uint8 *, int32, int32, int32) {}
bool RenderDevice::ProcessEvents() { return true; }
// psyqo's frame() runs once per vblank: no cap needed.
void RenderDevice::InitFPSCap() {}
bool RenderDevice::CheckFPSCap() { return true; }
void RenderDevice::UpdateFPSCap() {}
bool RenderDevice::InitShaders() { return true; }
void RenderDevice::LoadShader(const char *fileName, bool32 linear) {}

// 3.5: the frame's clear, re-coloured by a FillScreen (0x00BBGGRR as g_ps1ClearColour)
uint32 RSDK::PS1FrameClearColour() { return s_ps1FrameClear ? s_ps1FrameClearColour : 0xFFFFFFFF; }
void RSDK::PS1FrameClearSet(uint32 bgr)
{
    if (s_ps1FrameClear) {
        s_ps1FrameClearColour = bgr;
        PS1ChainClearSetRGB(s_ps1FrameClear, bgr & 0xFF, (bgr >> 8) & 0xFF, (bgr >> 16) & 0xFF);
    }
}
