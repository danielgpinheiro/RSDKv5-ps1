// PS1 port (docs/32): the RenderDevice on psyqo. Phase 2 = headless: the engine's software framebuffer
// (Drawing.cpp) is kept for the game logic that reads it, nothing is presented; phase 3 replaces the Draw*
// calls with the chained GPU renderer (RSDK/Scene/PS1, RSDK/Graphics/PS1; Sonic 2's ps1/render.cpp is the
// reference). Events, FPS cap and the window are psyqo's frame loop (ps1/rsdk5_main.cpp calls the engine once per
// vblank).
#pragma once

// Included inside namespace RSDK by Drawing.hpp, like the other backends.
using ShaderEntry = ShaderEntryBase;

// The chained frame (docs/32 3.2, PS1RenderDevice.cpp): begin (ps1/rsdk5_main.cpp frame()), open at the first draw
// (clear) or at the end (the last frame copied), end after FlipScreen.
void PS1RenderBeginFrame();
void PS1FrameStart(bool clear);
void PS1RenderEndFrame();

class RenderDevice : public RenderDeviceBase
{
public:
    struct WindowInfo {
        struct Display {
            int32 width, height, refresh_rate;
        } *displays; // one: the TV (GetDisplayInfo in Drawing.cpp)
    };
    static WindowInfo displayInfo;

    static bool Init();
    static void CopyFrameBuffer();
    static void FlipScreen();
    static void Release(bool32 isRefresh);
    static void RefreshWindow();
    static void GetWindowSize(int32 *width, int32 *height);
    static void SetupImageTexture(int32 width, int32 height, uint8 *imagePixels);
    static void SetupVideoTexture_YUV420(int32 width, int32 height, uint8 *yPlane, uint8 *uPlane, uint8 *vPlane, int32 strideY,
                                         int32 strideU, int32 strideV);
    static void SetupVideoTexture_YUV422(int32 width, int32 height, uint8 *yPlane, uint8 *uPlane, uint8 *vPlane, int32 strideY,
                                         int32 strideU, int32 strideV);
    static void SetupVideoTexture_YUV444(int32 width, int32 height, uint8 *yPlane, uint8 *uPlane, uint8 *vPlane, int32 strideY,
                                         int32 strideU, int32 strideV);
    static bool ProcessEvents();
    static void InitFPSCap();
    static bool CheckFPSCap();
    static void UpdateFPSCap();
    static bool InitShaders();
    static void LoadShader(const char *fileName, bool32 linear);
    static inline void ShowCursor(bool32 shown) {}
    static inline bool GetCursorPos(Vector2 *pos) { return false; }
    static inline void SetWindowTitle() {}

private:
    static bool SetupRendering();
    static void InitVertexBuffer();
    static bool InitGraphicsAPI();
    static void GetDisplays();
};
