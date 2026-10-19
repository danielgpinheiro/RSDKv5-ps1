// GPU type detection + the flip-rectangle readback test (ps1/gpu_detect.hh). Same routine as RSDKv2-ps1
// ps1/gpu_detect.cpp and RSDKv3-ps1 / RSDKv4-ps1 ps1/render.cpp PS1DetectGpu.
#include "gpu_detect.hh"

#include "psyqo/hardware/gpu.hh"

#if PS1_FLIP_AB_OFF
volatile uint32_t g_ps1FlipRectsOn = 0;
#else
volatile uint32_t g_ps1FlipRectsOn = 1;
#endif
volatile uint32_t g_ps1GpuType = 0, g_ps1GpuQuery7 = 0, g_ps1FlipTest = 0, g_ps1FlipRectsForce = 0;
volatile uint32_t g_ps1TileFlipRects = 0, g_ps1SpriteFlipRects = 0, g_ps1SpriteFlipQuads = 0;
bool g_ps1FlipRectsGpu = false, g_ps1FlipFrameUsed = false;

void PS1DetectGpu()
{
    using namespace psyqo::Hardware;
    auto ready = []() { while (!(GPU::Ctrl & (1 << 26))) {} };
    auto query = [](uint32_t index) { GPU::Ctrl = 0x10000000 | index; return (uint32_t)GPU::Data; };
    ready();
    const uint32_t area0 = query(3), area1 = query(4), offset = query(5), stat = GPU::Ctrl;
    // psx-spx "GPU Detection" (Perfect Assassin's routine): index 4 is latched first, so an old GPU, which leaves the
    // latch alone on index 7, can't answer 2 with a stale value (the draw offset, latched last above, could be 2).
    GPU::Ctrl = 0x10000004;
    const uint32_t q7 = query(7);
    uint32_t type     = 2; // new 208-pin GPU (LATE-PU-8 and up, every PSone; DuckStation >= a9c01a6)
    if ((q7 & 0xFFFFFF) != 2) {
        ready();
        GPU::Data = 0xE6000000; // mask bits off: GPUSTAT bit 12 below can only come from the texpage write
        GPU::Data = 0xE1001000 | (GPU::Ctrl & 0x3FFF);
        (void)(uint32_t)GPU::Data; // the routine's dummy read
        type = (GPU::Ctrl >> 12) & 1; // 1: an unknown (arcade / prototype) GPU, 0: the old 160-pin one
    }
    g_ps1GpuQuery7 = q7;
    g_ps1GpuType   = type;
    if (type) {
        // the corner of VRAM (x 1016-1023, y 511): texels u 60-63 of the 15-bit page at x 960, y 256; free at boot.
        // Three runs of GP0 words, each sent once the GPU takes commands (a table: resident code is FBZ2's STG room)
        static const uint32_t words[18] = {
            0xA0000000, (511u << 16) | 1020u, (1u << 16) | 4u, 0x56781234u, 0x0F0F0F0Fu, // CPU -> VRAM: u 60 = 1234h, 61 = 5678h
            0xE3000000, 0xE4000000 | (511u << 10) | 1023u, 0xE5000000,                // drawing area: all of VRAM, no offset
            0xE1000000 | 0x1000 | 0x400 | 0x100 | 0x10 | 15,                          // X flip, draw to display, 15-bit, (960, 256)
            0x01000000, 0x65808080, (511u << 16) | 1016u, (255u << 8) | 61u, (1u << 16) | 2u, // a 2 x 1 raw rect from u 61
            0x01000000, 0xC0000000, (511u << 16) | 1016u, (1u << 16) | 2u,            // VRAM -> CPU: the two drawn pixels
        };
        for (int i = 0; i < 18; ++i) {
            if (i == 0 || i == 5 || i == 14)
                ready();
            GPU::Data = words[i];
        }
        while (!(GPU::Ctrl & (1 << 27))) {}
        g_ps1FlipTest     = (uint32_t)GPU::Data;
        g_ps1FlipRectsGpu = g_ps1FlipTest == 0x12345678u; // pixel 1016 = u 61, 1017 = u 60: mirrored
    }
    ready();
    GPU::Data = 0xE1000000 | (stat & 0x7FF); // the texpage as it was (GPUSTAT bits 0-10), flip bits clear
    GPU::Data = 0xE6000000 | ((stat >> 11) & 3); // the mask bits as they were (GPUSTAT bits 11-12)
    GPU::Data = 0xE3000000 | (area0 & 0xFFFFF);
    GPU::Data = 0xE4000000 | (area1 & 0xFFFFF);
    GPU::Data = 0xE5000000 | (offset & 0x3FFFFF);
}

#if PS1_FLIP_AB
#include <psyqo/gpu.hh>
extern psyqo::GPU *g_ps1GPU;
extern volatile uint32_t g_ps1PerfFrames;

static inline void ttyc(char c) { *(volatile char *)0xBF802080 = c; } // PCSX expansion region (DuckStation prints it)
static void tty(const char *s) { while (*s) ttyc(*s++); }
static void ttysx(uint32_t v)
{
    char b[9];
    int n = 0;
    do {
        b[n++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v);
    ttyc(' ');
    while (n) ttyc(b[--n]);
}
static uint32_t crc32w(uint32_t c, uint32_t w)
{
    for (int k = 0; k < 32; k++) {
        uint32_t b = (c ^ w) & 1;
        c          = (c >> 1) ^ (b ? 0xEDB88320u : 0);
        w >>= 1;
    }
    return c;
}
static uint32_t vramCrc(uint32_t y) // the 320 x 240 buffer at (0, y), read back by GP0(C0h) (as the v2-v4 test modes)
{
    using namespace psyqo::Hardware;
    while (!(GPU::Ctrl & (1 << 26))) {}
    GPU::Data = 0xC0000000u;
    GPU::Data = y << 16;
    GPU::Data = (240u << 16) | 320u;
    while (!(GPU::Ctrl & (1 << 27))) {}
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t n = 320 * 240 / 2; n; n--) c = crc32w(c, (uint32_t)GPU::Data);
    return ~c;
}

// Frames counted from boot (one logic step each in a BOOT= build: Frameskip off, uncapped): checkpoints every 50 frames
// from 300 to 1500; the end line then "E 0 <frame>" (the harness stops the emulator there).
void PS1FlipAbFrameStart()
{
    uint32_t f = g_ps1PerfFrames;
    if (f == 1) {
        tty("gpu");
        ttysx(g_ps1GpuType), ttysx(g_ps1GpuQuery7), ttysx(g_ps1FlipTest);
        ttyc('\n');
    }
    if (f < 300 || f > 1500 || f % 50)
        return;
    g_ps1GPU->waitChainIdle();
    tty("C");
    ttysx(f);
    tty(" V");
    ttysx(vramCrc(0)), ttysx(vramCrc(256));
    ttyc('\n');
    if (f == 1500) {
        tty("C");
        ttysx(f);
        tty(" W");
        ttysx(g_ps1GpuType), ttysx(g_ps1FlipTest), ttysx(g_ps1TileFlipRects), ttysx(g_ps1SpriteFlipRects),
            ttysx(g_ps1SpriteFlipQuads);
        tty("\nE 0");
        ttysx(f);
        ttyc('\n');
    }
}

// Right 400-700, nothing, Left 760-1000 (the player mirrored), nothing, Right 1100-1300, jumps (Cross) on the way
uint16_t PS1FlipAbPad()
{
    uint32_t f = g_ps1PerfFrames;
    uint16_t b = 0;
    if ((f >= 400 && f < 700) || (f >= 1100 && f < 1300))
        b |= 1 << 3;
    if (f >= 760 && f < 1000)
        b |= 1 << 2;
    if (f == 520 || f == 880 || f == 1200)
        b |= 1 << 4;
    return b;
}
#endif
