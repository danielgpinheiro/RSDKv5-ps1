// PS1 port: headless RenderDevice (see PS1RenderDevice.hpp). Included by Drawing.cpp like the other backends.
#include "../../../../ps1/video.hh" // PS1Hblanks (root counter 1)

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
    if (!scanlines) {
        int32 size = videoSettings.pixWidth >= SCREEN_YSIZE ? videoSettings.pixWidth : SCREEN_YSIZE;
        scanlines  = (ScanlineInfo *)malloc(size * sizeof(ScanlineInfo));
        if (!scanlines)
            return false;
        memset(scanlines, 0, size * sizeof(ScanlineInfo));
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
void RenderDevice::FlipScreen() {}
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
