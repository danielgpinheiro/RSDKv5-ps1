#pragma once
// VRAM access for the engine side (docs/ports/mania/32 phase 3), without psyqo's headers (they clash with the engine's assert).
#include <stdint.h>

// Blocking upload of w x h halfwords at (x, y), after the in-flight chain (if any) has finished.
void PS1VramUpload(const uint16_t *data, int16_t x, int16_t y, int16_t w, int16_t h);

// Debug readback for evidence (GP0 C0h through GPUREAD, psx-spx "VRAM to CPU blitting"): GDB sets the rectangle and
// g_ps1VramDumpReq = 1; the next frame() start reads it into g_ps1VramDumpBuf (8 MB measuring link only) and calls
// PS1VramDumpHook, where the script dumps the buffer.
extern "C" {
extern volatile uint32_t g_ps1VramDumpReq;
extern volatile int16_t g_ps1VramDumpX, g_ps1VramDumpY, g_ps1VramDumpW, g_ps1VramDumpH;
extern volatile uint32_t g_ps1VramDumps;
void PS1VramDumpHook(uint32_t halfwords);
}
void PS1VramServiceDump(); // frame() start: the GPU is idle between frames
