/*
 * RSDKv5 (Sonic Mania) on the PS1: the psyqo application (docs/32 phase 2).
 *
 * What RSDKv5/main.cpp + RunRetroEngine do on the PC, as psyqo's prepare / start / frame: the engine is
 * initialised once (storage, user core, the headless render device which also inits audio and input,
 * the game link, the first scene), then ProcessEngine runs once per vblank. The frame loop's PC-only
 * parts (window events, focus pause, dev menu touch, FPS cap) are left out; ProcessInputDevices and
 * ProcessDimming / FlipScreen keep their order. Counters for GDB as in the other ports.
 */
// psyqo first: the engine's headers bring the port's <assert.h> (a one-argument macro), psyqo has Kernel::assert.
#include <psyqo/advancedpad.hh>
#include <psyqo/application.hh>
#include <psyqo/cdrom-device.hh>
#include <psyqo/gpu.hh>

#include "../ps1/irq_guard.hh"
#include "../ps1/loading.hh"
#include "../ps1/video.hh" // PS1Hblanks

#include "RSDK/Core/RetroEngine.hpp"

extern "C" void LinkGameLogicDLL(RSDK::EngineInfo *info); // SonicMania/Game.c (RETRO_STANDALONE 0)

using namespace RSDK;

volatile uint32_t g_ps1PerfFrames   = 0; // frames run (ProcessEngine calls)
volatile uint32_t g_ps1PerfVsyncs   = 0; // vblanks elapsed while running
volatile uint32_t g_ps1PerfLastHbl  = 0; // last frame's CPU time (hblanks; 262 = one NTSC frame)
volatile uint32_t g_ps1PerfMaxHbl   = 0;
volatile uint32_t g_ps1PerfOverHbl  = 0; // frames longer than a vblank
volatile uint32_t g_ps1InitDone     = 0; // engine initialised (1) / init failed (2)
volatile uint32_t g_ps1HeapTotal    = 0, g_ps1HeapHeadroom = 0;
static uint32_t s_lastCount         = 0;

psyqo::GPU *g_ps1GPU           = nullptr;
psyqo::AdvancedPad *g_ps1Pad   = nullptr;
psyqo::CDRomDevice *g_ps1CDRom = nullptr;

extern "C" char __heap_start, __stack_start;
extern "C" void *psyqo_heap_end();

namespace {

// Pad 1 -> g_ps1PadButtons (RSDK/Input/PS1): Up Down Left Right, A = Cross, B = Circle, C = Square, X = Triangle,
// Y = L1, Z = R1, Start, Select (as the Sonic CD port's mapping).
void readPad() {
    using Pad = psyqo::AdvancedPad;
    static const Pad::Button map[12] = { Pad::Up,   Pad::Down,     Pad::Left, Pad::Right, Pad::Cross, Pad::Circle,
                                         Pad::Square, Pad::Triangle, Pad::L1,  Pad::R1,    Pad::Start, Pad::Select };
    uint16_t held = 0;
    if (g_ps1Pad && g_ps1Pad->isPadConnected(Pad::Pad::Pad1a))
        for (int i = 0; i < 12; i++)
            if (g_ps1Pad->isButtonPressed(Pad::Pad::Pad1a, map[i]))
                held |= 1 << i;
    g_ps1PadButtons = held;
}

__attribute__((noinline)) void perfTick(psyqo::GPU &gpu) {
    uint32_t now = gpu.getFrameCount();
    if (g_ps1PerfFrames)
        g_ps1PerfVsyncs = g_ps1PerfVsyncs + (now - s_lastCount);
    s_lastCount     = now;
    g_ps1PerfFrames = g_ps1PerfFrames + 1;
    uintptr_t hs = (uintptr_t)&__heap_start, ss = (uintptr_t)&__stack_start, top = (uintptr_t)psyqo_heap_end();
    if (top < hs)
        top = hs;
    g_ps1HeapTotal    = ss - hs;
    g_ps1HeapHeadroom = ss - top;
}

class ManiaPS1App final : public psyqo::Application {
    void prepare() override {
        gpu().initialize(psyqo::GPU::Configuration{}
                             .set(psyqo::GPU::Resolution::W320)
                             .set(psyqo::GPU::VideoMode::AUTO)
                             .set(psyqo::GPU::ColorMode::C15BITS)
                             .set(psyqo::GPU::Interlace::PROGRESSIVE)
                             .set(psyqo::GPU::MiscSetting::CLEAR_VRAM));
        g_ps1GPU = &gpu();
        m_cdrom.prepare();
    }

    void start() override {
        m_cdrom.resetBlocking(gpu());
        g_ps1CDRom = &m_cdrom;
#if !PS1_NO_IRQ_GUARD // bisect switch (docs/32 2.2)
        PS1IrqGuardInstall(); // real-hardware IRQ fixes (docs/31)
#endif
        m_pad.initialize(psyqo::AdvancedPad::PollingMode::Fast);
        g_ps1Pad = &m_pad;

        // RunRetroEngine's initialisation in its order (RSDKv5/RSDK/Core/RetroEngine.cpp, no mod loader), without the
        // PC parts (arguments, console). The game is linked in: main.cpp's linkGameLogic assignment.
        PS1LoadingBegin();
        linkGameLogic           = (LogicLinkHandle)LinkGameLogicDLL;
        RenderDevice::isRunning = false;
        bool ok                 = InitStorage();
        if (ok) {
            SKU::InitUserCore();
            LoadSettingsINI(); // PS1: the PC's no-Settings.ini defaults (UserCore.cpp)
            SKU::curSKU.platform = SKU::userCore->GetUserPlatform();
            ok = SKU::userCore->CheckAPIInitialized();
        }
        if (ok) {
            InitEngine();
            ok = RenderDevice::Init(); // headless: the 320x240 screen, then AudioDevice::Init + InitInputDevices
            RenderDevice::isRunning = ok;
        }
        if (ok)
            RenderDevice::InitFPSCap();
        PS1LoadingEnd();
        g_ps1InitDone = ok ? 1 : 2;
    }

    // One pass of RunRetroEngine's loop per vblank (psyqo is the FPS cap), in its order. PC-only parts left out:
    // window events, the touch dev-menu corner, mod reloading.
    void engineFrame() {
        AudioDevice::FrameInit();
        SKU::userCore->FrameInit();
        if (SKU::userCore->CheckEnginePause())
            return;

        if (customSettings.disableFocusPause)
            engine.focusState = 0;
        else if (SKU::userCore->CheckFocusLost()) {
            if (!(engine.focusState & 1)) {
                engine.focusState = 1;
                for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
                    engine.focusPausedChannel[c] = false;
                    if (!(channels[c].state & CHANNEL_PAUSED)) {
                        PauseChannel(c);
                        engine.focusPausedChannel[c] = true;
                    }
                }
            }
        }
        else if (engine.focusState) {
            engine.focusState = 0;
            for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
                if (engine.focusPausedChannel[c])
                    ResumeChannel(c);
                engine.focusPausedChannel[c] = false;
            }
        }

        bool flip = true;
        if (!engine.initialized || (engine.focusState & 1)) {
            if (videoSettings.windowState != WINDOWSTATE_ACTIVE)
                flip = false;
        }
        else {
            if (!engine.hardPause) {
                foreachStackPtr  = foreachStackList;
                debugHitboxCount = 0;
                ProcessInputDevices();
                if (engine.devMenu)
                    ProcessDebugCommands();
                ProcessEngine();
            }
            if (videoSettings.windowState != WINDOWSTATE_ACTIVE)
                flip = false;
            else if (engine.inFocus == 1)
                RenderDevice::CopyFrameBuffer();
        }
        if (!flip)
            return;
        if ((engine.focusState & 1) || engine.inFocus == 1)
            RenderDevice::ProcessDimming();
        RenderDevice::FlipScreen();
    }

    void frame() override {
        uint32_t t0 = PS1Hblanks();
        if (g_ps1InitDone == 1 && RenderDevice::isRunning) {
            readPad();
            engineFrame();
            if (g_ps1PerfFrames % 60 == 0)
                PS1EntityCheckGhosts(); // empty entity slots must read as blank (docs/32 2.3)
        }
        uint32_t cpu     = PS1HblanksSince(t0);
        g_ps1PerfLastHbl = cpu;
        if (cpu > g_ps1PerfMaxHbl)
            g_ps1PerfMaxHbl = cpu;
        if (cpu > 262)
            g_ps1PerfOverHbl = g_ps1PerfOverHbl + 1;
        perfTick(gpu());
    }

    psyqo::CDRomDevice m_cdrom;
    psyqo::AdvancedPad m_pad;
};

ManiaPS1App s_app;

} // namespace

int main() { return s_app.run(); }
