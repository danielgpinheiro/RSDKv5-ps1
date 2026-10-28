// VRAM access for the engine side (see vram.hh).
#include "vram.hh"

#include <psyqo/gpu.hh>

#include "gpu_chain.hh"

extern psyqo::GPU *g_ps1GPU;

void PS1VramUpload(const uint16_t *data, int16_t x, int16_t y, int16_t w, int16_t h) {
    PS1GpuImmediate(); // a chain in flight may still be texturing from VRAM
    g_ps1GPU->uploadToVRAM(data, psyqo::Rect{{x, y}, {w, h}});
}

extern "C" {
volatile uint32_t g_ps1VramDumpReq = 0;
volatile int16_t g_ps1VramDumpX = 0, g_ps1VramDumpY = 0, g_ps1VramDumpW = 0, g_ps1VramDumpH = 0;
volatile uint32_t g_ps1VramDumps = 0;
__attribute__((noinline)) void PS1VramDumpHook(uint32_t halfwords) { __asm__ volatile("" ::"r"(halfwords)); }
}

#if PS1_DEBUG_BIGRAM || PS1_DEVMENU // (the DEVMENU debug build's pictures too)
#define PS1_VRAM_DUMP_HW (256 * 512) // 256 KB: four texture columns of both page rows, or a framebuffer
extern "C" {
uint16_t g_ps1VramDumpBuf[PS1_VRAM_DUMP_HW];
}

void PS1VramServiceDump() {
    if (g_ps1VramDumpReq != 1)
        return;
    uint32_t n = (uint32_t)g_ps1VramDumpW * (uint32_t)g_ps1VramDumpH;
    if (!n || n > PS1_VRAM_DUMP_HW) {
        g_ps1VramDumpReq = 3; // bad rectangle
        return;
    }
    PS1GpuImmediate();
    volatile uint32_t *gp0 = (volatile uint32_t *)0xBF801810, *stat = (volatile uint32_t *)0xBF801814;
    while ((*stat & ((1u << 26) | (1u << 28))) != ((1u << 26) | (1u << 28)));
    *gp0 = 0xC0000000;
    *gp0 = (uint32_t)(uint16_t)g_ps1VramDumpX | ((uint32_t)(uint16_t)g_ps1VramDumpY << 16);
    *gp0 = (uint32_t)(uint16_t)g_ps1VramDumpW | ((uint32_t)(uint16_t)g_ps1VramDumpH << 16);
    while (!(*stat & (1u << 27)));
    uint32_t *out = (uint32_t *)g_ps1VramDumpBuf;
    for (uint32_t i = 0; i < (n + 1) / 2; ++i) out[i] = *gp0; // GPUREAD shares GP0's address
    g_ps1VramDumps   = g_ps1VramDumps + 1;
    g_ps1VramDumpReq = 2;
    PS1VramDumpHook(n);
}
#else
void PS1VramServiceDump() {}
#endif
