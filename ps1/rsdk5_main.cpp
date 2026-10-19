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
#include <psyqo/kernel.hh>

#include "common/kernel/events.h"
#include "common/syscalls/syscalls.h"

#include "../ps1/irq_guard.hh"
#include "../ps1/loading.hh"
#include "../ps1/stack.hh" // MA-188: the stack reserve's overflow check
#include "../ps1/video.hh" // PS1Hblanks
#include "../ps1/vram.hh"  // PS1VramServiceDump (GDB VRAM dumps)
void PS1McPrepare(); // ps1/memcard.hh (phase 8: the user files on the card; its psyqo headers stay out of here)

#include "RSDK/Core/RetroEngine.hpp"

extern "C" void LinkGameLogicDLL(RSDK::EngineInfo *info); // SonicMania/Game.c (RETRO_STANDALONE 0)
void PS1SamplerInstall(); // ps1/pc_sampler.cpp (make SAMPLE=1)

using namespace RSDK;

volatile uint32_t g_ps1PerfFrames   = 0; // frames run (ProcessEngine calls)
volatile uint32_t g_ps1PerfVsyncs   = 0; // vblanks elapsed while running
volatile uint32_t g_ps1PerfLastHbl  = 0; // last frame's CPU time (hblanks; 262 = one NTSC frame)
volatile uint32_t g_ps1PerfMaxHbl   = 0;
volatile uint32_t g_ps1PerfOverHbl  = 0; // frames longer than a vblank
volatile uint32_t g_ps1PerfLastVsyncs = 0; // 9f: vblanks the last frame took (1 = 60 fps)
volatile uint32_t g_ps1PerfFlipHbl    = 0; // 9f: hblanks from the last frame's end to this one's start (psyqo's flip:
                                           // the GPU finishing the chain, then vblank)
volatile uint32_t g_ps1PerfFlipMax    = 0;
// 9f: sums a GDB script resets at a window's start and reads at its end (two stops, no per-frame breakpoint)
volatile uint32_t g_ps1PerfSumHbl = 0, g_ps1PerfSumFlip = 0, g_ps1PerfSlowFrames = 0, g_ps1PerfWorstVsyncs = 0;
static uint32_t s_frameEnd          = 0;
#if PS1_PROFILE
volatile uint32_t g_ps1ProfEngine = 0, g_ps1ProfHbl = 0; // hblanks of ProcessEngine / of the whole frame (PROFILE=1)
#endif
volatile uint32_t g_ps1InitDone     = 0; // engine initialised (1) / init failed (2)
volatile uint32_t g_ps1HeapTotal    = 0, g_ps1HeapHeadroom = 0;
static uint32_t s_lastCount         = 0;

psyqo::GPU *g_ps1GPU           = nullptr;
psyqo::AdvancedPad *g_ps1Pad   = nullptr;
psyqo::CDRomDevice *g_ps1CDRom = nullptr;

extern "C" char __heap_start, __stack_start;
#include "psyqo/alloc.h" // psyqo_heap_end (docs/32 9d)
extern "C" volatile uint32_t g_ps1ReserveEnd, g_ps1ReserveHigh; // RSDK/Storage/Storage.cpp, ps1/libc/stdlib.cpp
bool PS1FsFind(const char *path, uint32_t *lba, uint32_t *size);             // ps1/cdrom_fs.cpp
void PS1VersionLoad();                                                       // ps1/version_text.cpp (build stamp)
extern "C" volatile int g_ps1PlusChars;                                      // docs/32 9g: PS1ReadConfig below
volatile int g_ps1PlusChars = 0;

// docs/32 9b.5 (test harness): GDB breaks on ps1TestStop, called only at the frames it asked for (g_ps1TestStops, 0 =
// unused), instead of a breakpoint whose condition GDB evaluates every frame (that ran the emulator at a few fps)
volatile uint32_t g_ps1TestStops[8] = {};
__attribute__((noinline)) void ps1TestStop() { __asm__ volatile(""); }

extern volatile uint32_t g_ps1LoadingLoads; // ps1/loading.cpp (phase 13b: no frameskip across a load)

extern "C" {
// docs/32 phase 13b (user 2026-10-12): Options > Video > Frameskip (default Off). g_ps1LogicSteps is the scene's base (1,
// or 2 for the capped zones and the UFO stages); with Frameskip on a drawn frame runs as many logic steps as the vblanks
// the last frame took (at least the base, at most PS1_SKIP_MAX: 15 drawn fps at worst; the rest dropped, never caught
// up), so a slow stretch keeps the game's speed and draws fewer frames. A frame after a load runs the base. (At file
// scope, outside the anonymous namespace: GDB reads them by name.)
#if defined PS1_BOOT_CATEGORY && defined PS1_BOOT_FSKIP
volatile int32 g_ps1Frameskip    = 1; // `make BOOT=... FSKIP=1`
#else
volatile int32 g_ps1Frameskip    = 0; // Options > Video > Frameskip (from the next frame)
#endif
void PS1SetFrameskip(int on) { g_ps1Frameskip = on ? 1 : 0; }
volatile int32 g_ps1FrameSteps   = 1; // this drawn frame's logic steps (RSDK/Core/RetroEngine.cpp)
volatile uint32_t g_ps1StepsTotal = 0, g_ps1SkipFrames = 0; // logic steps run; frames that ran more than the base
}

namespace {

// Pad 1 -> g_ps1PadButtons (RSDK/Input/PS1): Up Down Left Right, A = Cross, B = Circle, C = L1, X = Square,
// Y = Triangle, Z = R1, Start, Select. docs/32 9b.5: the menus' prompts draw X as Square and Y as Triangle (Save Select's
// Delete, Game Options), as the PS4 version's layout; gameplay jumps on A / B / C / X, as there.
void readPad() {
    using Pad = psyqo::AdvancedPad;
    static const Pad::Button map[12] = { Pad::Up,   Pad::Down,   Pad::Left,     Pad::Right, Pad::Cross, Pad::Circle,
                                         Pad::L1,   Pad::Square, Pad::Triangle, Pad::R1,    Pad::Start, Pad::Select };
    uint16_t held = 0, held2 = 0;
    if (g_ps1Pad && g_ps1Pad->isPadConnected(Pad::Pad::Pad1a))
        for (int i = 0; i < 12; i++)
            if (g_ps1Pad->isButtonPressed(Pad::Pad::Pad1a, map[i]))
                held |= 1 << i;
    if (g_ps1Pad && g_ps1Pad->isPadConnected(Pad::Pad::Pad2a)) // 9f: pad 2 (Puyo Puyo's 2P)
        for (int i = 0; i < 12; i++)
            if (g_ps1Pad->isButtonPressed(Pad::Pad::Pad2a, map[i]))
                held2 |= 1 << i;
    g_ps1PadButtons  = held;
    g_ps1Pad2Buttons = held2;
}

// docs/32 9g: Mighty and Ray (SonicMania/Game.h PS1_PLUS_CHARS), off unless the disc holds Data/PS1Config.txt (the
// builder's option writes it, "plus_characters=1"). Its presence is the switch: a file-index lookup, no read (a read
// would take a CD window from the heap at boot).
static void PS1ReadConfig()
{
    uint32_t lba, size;
    if (PS1FsFind("Data/PS1Config.txt", &lba, &size))
        g_ps1PlusChars = 1;
}

// docs/32 9f (user 2026-10-08, the UFO stages only): "30 fps, 2 logic steps". UFO_Setup's StageLoad sets g_ps1LogicSteps
// to 2 (every scene load resets it to 1, RSDK/Scene/Object.cpp): the engine runs two logic steps a frame
// (RSDK/Core/RetroEngine.cpp) and each frame is shown at least that many vblanks, so the game keeps its full speed at a
// steady 30 fps. psyqo flips as soon as one vblank has passed since the last flip: a frame that finishes early waits
// for its second, counted by the port's own BIOS VBlank event.
// docs/32 choice 182 (user 2026-10-10): the zones (scene category 1) run the same way unless Options > Video >
// Uncapped FPS is Yes; g_ps1Uncapped is set from the options (SonicMania/Objects/Helpers/Options.c), 0 = capped.
// docs/32 9t (user 2026-10-19): the target is 60 fps at one logic step everywhere: Uncapped FPS defaults to Yes and the
// UFO stages follow it too; its No (30 fps, two steps) is a test aid / user option.
extern "C" {
volatile int32 g_ps1LogicSteps   = 1;
#if defined PS1_BOOT_CATEGORY && !defined PS1_BOOT_CAP
volatile int32 g_ps1Uncapped     = 1; // a BOOT= test build (no menu, no options): uncapped unless `make CAP=1`
#else
volatile int32 g_ps1Uncapped     = 1; // Options > Video > Uncapped FPS (applied at the next scene load; 9t: default Yes)
#endif
void PS1SetUncapped(int on) { g_ps1Uncapped = on ? 1 : 0; }
volatile int g_ps1FlickShift = 0, g_ps1LastStep = 1; // 9r: ps1/ps1_limits.h PS1_FLICK / PS1_LAST_STEP
volatile uint32_t g_ps1VblankCount = 0; // vblanks (the port's VBlank event)
volatile uint32_t g_ps1PaceWaitHbl = 0; // hblanks the last frame waited for its pace
}
static uint32_t s_frameVbl = 0; // the vblank count when the frame began (right after the last flip)
static uint32_t s_skipLoads = 0; // g_ps1LoadingLoads at the last frame's start (a load in between: no skip)
#define PS1_SKIP_MAX 4

__attribute__((noinline)) void perfTick(psyqo::GPU &gpu) {
    uint32_t now = gpu.getFrameCount();
    if (g_ps1PerfFrames) {
        g_ps1PerfVsyncs     = g_ps1PerfVsyncs + (now - s_lastCount);
        g_ps1PerfLastVsyncs = now - s_lastCount;
        if (now - s_lastCount > 1)
            g_ps1PerfSlowFrames = g_ps1PerfSlowFrames + 1;
        if (now - s_lastCount > g_ps1PerfWorstVsyncs)
            g_ps1PerfWorstVsyncs = now - s_lastCount;
    }
    s_lastCount     = now;
    g_ps1PerfFrames = g_ps1PerfFrames + 1;
    uintptr_t hs = (uintptr_t)&__heap_start, ss = (uintptr_t)&__stack_start;
    g_ps1HeapTotal    = ss - hs;
    // 2.4a: the part of the init reserve no allocation has reached (STG and the stack guard fill the rest of the heap)
    g_ps1HeapHeadroom = g_ps1ReserveEnd > g_ps1ReserveHigh ? g_ps1ReserveEnd - g_ps1ReserveHigh : 0;
    if (PS1StackGuardBroken()) // the main stack overflowed its 16 KB reserve (ps1/stack.hh)
        g_ps1StackOverflow = g_ps1StackOverflow + 1;
    for (int k = 0; k < 8; k++)
        if (g_ps1TestStops[k] == g_ps1PerfFrames) {
            ps1TestStop();
            break;
        }
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
        PS1McPrepare(); // phase 8: the memory card's file system (before any card call)
    }

    void start() override {
#if PS1_PROFILE
        *(volatile uint16_t *)0xBF801124 = 0x200; // root counter 2: system clock / 8, free-running (the profile's clock)
#endif
        m_cdrom.resetBlocking(gpu());
        g_ps1CDRom = &m_cdrom;
#if !PS1_NO_IRQ_GUARD // bisect switch (docs/32 2.2)
        PS1IrqGuardInstall(); // real-hardware IRQ fixes (docs/31)
#endif
#if PS1_SAMPLE
        PS1SamplerInstall(); // PC sampler (ps1/pc_sampler.cpp)
#endif
        m_pad.initialize(psyqo::AdvancedPad::PollingMode::Fast);
        { // 9f: the port's vblank count (the UFO stages' pace)
            uint32_t ev = psyqo::Kernel::openEvent(0xf2000003, 2, EVENT_MODE_CALLBACK, []() { g_ps1VblankCount = g_ps1VblankCount + 1; });
            syscall_enableEvent(ev);
        }
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
            PS1ReadConfig();   // 9g: the disc's settings (Mighty and Ray)
            SKU::curSKU.platform = SKU::userCore->GetUserPlatform();
            ok = SKU::userCore->CheckAPIInitialized();
        }
        if (ok) {
            InitEngine();
#if defined PS1_BOOT_CATEGORY
            // `make BOOT=cat,index` (docs/32 2.5): start at that scene of GameConfig's lists, as the dev menu's stage
            // select does (the globals keep GameConfig's defaults).
            if (PS1_BOOT_CATEGORY < sceneInfo.categoryCount) {
                sceneInfo.activeCategory = PS1_BOOT_CATEGORY;
                sceneInfo.listPos        = sceneInfo.listCategory[PS1_BOOT_CATEGORY].sceneOffsetStart + PS1_BOOT_INDEX;
                sceneInfo.state          = ENGINESTATE_LOAD;
            }
#endif
            ok = RenderDevice::Init(); // headless: the 320x240 screen, then AudioDevice::Init + InitInputDevices
            RenderDevice::isRunning = ok;
        }
        if (ok)
            RenderDevice::InitFPSCap();
        PS1LoadingEnd();
        // VERSION.TXT (the title's build stamp, ps1/version_text.cpp), after the boot load: the file index it needs is
        // loaded by then (read before InitStorage, the index went under STG and FBZ2 lost its room: STG exhausted)
        PS1VersionLoad();
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
#if PS1_PROFILE
                uint32_t pe0 = PS1Hblanks();
                ProcessEngine();
                g_ps1ProfEngine += PS1HblanksSince(pe0);
#else
                ProcessEngine();
#endif
            }
            if (videoSettings.windowState != WINDOWSTATE_ACTIVE)
                flip = false;
            else if (engine.inFocus == 1)
                RenderDevice::CopyFrameBuffer();
        }
        if (!flip)
            return;
        if ((engine.focusState & 1) || engine.inFocus == 1)
            for (int32 i = 0; i < g_ps1FrameSteps; ++i) RenderDevice::ProcessDimming(); // per logic step (9f, 13b)
        RenderDevice::FlipScreen();
    }

    void frame() override {
        PS1AtlasServiceRequests(); // a GDB-requested sprite group load (docs/32 3.1c)
        SKU::PS1CardTestService(); // a GDB-requested user file save / load (docs/32 phase 8)
        PS1VramServiceDump(); // a GDB-requested VRAM readback (8 MB measuring link), while the GPU is idle
        uint32_t t0 = PS1Hblanks();
        uint32_t vbl = g_ps1VblankCount;
        {
            int32 steps = g_ps1LogicSteps, base = steps; // phase 13b: the vblanks the last frame took (its pace wait too)
            if (g_ps1Frameskip && s_skipLoads == g_ps1LoadingLoads) {
                int32 took = (int32)(vbl - s_frameVbl);
                if (took > steps)
                    steps = took > PS1_SKIP_MAX ? PS1_SKIP_MAX : took;
            }
            s_skipLoads     = g_ps1LoadingLoads;
            g_ps1FrameSteps = steps;
            g_ps1StepsTotal = g_ps1StepsTotal + steps;
            if (steps > base)
                g_ps1SkipFrames = g_ps1SkipFrames + 1;
        }
        s_frameVbl  = vbl;
        if (s_frameEnd) {
            uint32_t gap     = (t0 - s_frameEnd) & 0xFFFF; // root counter 1 wraps at 16 bits
            g_ps1PerfFlipHbl = gap;
            g_ps1PerfSumFlip = g_ps1PerfSumFlip + gap;
            if (gap > g_ps1PerfFlipMax)
                g_ps1PerfFlipMax = gap;
        }

        if (g_ps1InitDone == 1 && RenderDevice::isRunning) {
            readPad();
            PS1RenderBeginFrame(); // this frame's chain (docs/32 3.2)
            engineFrame();
            PS1RenderEndFrame();
#if defined PS1_TEST_SLOW
            // phase 13b test switch (`make EXTRA_CPPFLAGS=-DPS1_TEST_SLOW=N`): N / 2, N, 3N / 2 hblanks of extra work on
            // successive frames, so Frameskip's step counts vary under GDB (tools/play/fskip_logic.gdb)
            for (uint32_t w = PS1Hblanks(), n = PS1_TEST_SLOW * (1 + g_ps1PerfFrames % 3) / 2; PS1HblanksSince(w) < n;) {}
#endif
            if (g_ps1PerfFrames % 60 == 0)
                PS1EntityCheckGhosts(); // empty entity slots must read as blank (docs/32 2.3)
        }
        uint32_t cpu     = PS1HblanksSince(t0);
#if PS1_PROFILE
        g_ps1ProfHbl += cpu;
#endif
        g_ps1PerfLastHbl = cpu;
        g_ps1PerfSumHbl  = g_ps1PerfSumHbl + cpu;
        if (cpu > g_ps1PerfMaxHbl)
            g_ps1PerfMaxHbl = cpu;
        if (cpu > 262)
            g_ps1PerfOverHbl = g_ps1PerfOverHbl + 1;
        if (g_ps1LogicSteps > 1) { // 9f: the UFO stages' pace: the frame shown until that many vblanks since it began
            uint32_t w0 = PS1Hblanks();
            while (g_ps1VblankCount - s_frameVbl < (uint32_t)g_ps1LogicSteps) gpu().pumpCallbacks();
            g_ps1PaceWaitHbl = PS1HblanksSince(w0);
        }
        perfTick(gpu());
        s_frameEnd = PS1Hblanks();
    }

    psyqo::CDRomDevice m_cdrom;
    psyqo::AdvancedPad m_pad;
};

ManiaPS1App s_app;

} // namespace

// The stack reserve painted first (ps1/stack.hh, MA-188): its guard words and low-water mark.
int main() {
    PS1StackPaint();
    return s_app.run();
}
