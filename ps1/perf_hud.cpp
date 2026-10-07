// docs/32 9f: `make PERF_HUD=1` (debug builds only; `make dist` refuses them): a line of numbers in the top-left corner
// for testing in an emulator with its own overlay (DuckStation) or on a console, drawn last in the frame's chain as flat
// rectangles (a 3x5 digit font, 2x): the last frame's
//   V  vblanks it took (1 = 60 fps) and, after it, the worst over the last 60 frames
//   C  CPU hblanks inside the frame (262 = one NTSC frame; includes G)
//   G  hblanks the frame's first draw waited for the previous frame's GPU work (the one chain arena)
//   F  hblanks between the frame's end and the next frame's start (psyqo's flip: the GPU finishing, then vblank)
//   S  sprites / tiles chained, K  chain words / 100
// A frame is GPU-bound when C stays under 262 but V is above 1 (F large); CPU-bound when C is above 262.
#include <stdint.h>

#include "gpu_chain_raw.hh"

extern volatile uint32_t g_ps1PerfLastVsyncs, g_ps1PerfLastHbl, g_ps1PerfFlipHbl;
extern "C" {
extern volatile uint32_t g_ps1SpriteDraws;
}
extern volatile uint32_t g_ps1ChainIdleWaitHbl, g_ps1ChainLastWords;

namespace {
// digits 0-9, 5 rows of 3 bits (bit 2 = left column)
const uint8_t kFont[10][5] = { { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
                               { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 },
                               { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 } };
// field colours (GP0 BGR): V white, C yellow, G red, F orange, S green, K cyan
const uint32_t kColours[6] = { 0xFFFFFF, 0x00FFFF, 0x4040FF, 0x0080FF, 0x40FF40, 0xFFFF40 };

uint32_t s_vsWorst[60];
uint32_t s_vsAt = 0;
uint32_t s_lastIdle = 0;

void rect(int x, int y, int w, int h, uint32_t bgr) {
    uint32_t *wr = PS1ChainSpanBegin(4);
    if (!wr)
        return;
    wr[1] = 0x60000000 | bgr;
    wr[2] = (uint32_t)(uint16_t)y << 16 | (uint16_t)x;
    wr[3] = (uint32_t)h << 16 | (uint32_t)w;
    PS1ChainSpanEnd(wr, 3, wr + 4);
}

// a number (up to `digits` digits, clamped) at x, y; returns the x after it
int number(int x, int y, uint32_t v, int digits, uint32_t bgr) {
    uint32_t lim = 1;
    for (int d = 0; d < digits; ++d) lim *= 10;
    if (v >= lim)
        v = lim - 1;
    char buf[6];
    for (int d = digits - 1; d >= 0; --d, v /= 10) buf[d] = (char)(v % 10);
    for (int d = 0; d < digits; ++d, x += 8) {
        const uint8_t *g = kFont[(int)buf[d]];
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 3;) {
                if (!(g[r] >> (2 - c) & 1)) {
                    ++c;
                    continue;
                }
                int c0 = c;
                while (c < 3 && (g[r] >> (2 - c) & 1)) ++c;
                rect(x + c0 * 2, y + r * 2, (c - c0) * 2, 2, bgr);
            }
    }
    return x;
}
} // namespace

void PS1PerfHudDraw() {
    s_vsWorst[s_vsAt] = g_ps1PerfLastVsyncs;
    s_vsAt            = (s_vsAt + 1) % 60;
    uint32_t worst = 0;
    for (int i = 0; i < 60; ++i) worst = s_vsWorst[i] > worst ? s_vsWorst[i] : worst;
    uint32_t idle = g_ps1ChainIdleWaitHbl, g = idle - s_lastIdle;
    s_lastIdle = idle;

    const uint32_t vals[6]   = { g_ps1PerfLastVsyncs * 10 + (worst > 9 ? 9 : worst), g_ps1PerfLastHbl, g,
                                 g_ps1PerfFlipHbl, g_ps1SpriteDraws, g_ps1ChainLastWords / 100 };
    const int widths[6]      = { 2, 3, 3, 3, 3, 3 };
    rect(4, 4, 6 * 6 + 2 * 8 + 5 * 3 * 8 + 4, 14, 0x000000); // the background
    int x = 6;
    for (int f = 0; f < 6; ++f) {
        rect(x, 6, 4, 10, kColours[f]); // the field's colour tag
        x = number(x + 6, 6, vals[f], widths[f], kColours[f]);
    }
}
