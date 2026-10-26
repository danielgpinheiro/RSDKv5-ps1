// docs/ports/mania/32 9u: RSDK::SetPaletteFade's PS1 table version (RSDKv5/RSDK/Graphics/Palette.cpp) against stock's loop, every
// blend amount and range end on random palettes (the 257th entry included). c++ -O2 tools/host/fade_check.cpp
#include <cstdint>
#include <cstdio>
#include <cstring>
typedef uint8_t uint8; typedef uint16_t uint16; typedef int16_t int16; typedef int32_t int32; typedef uint32_t uint32;
#define RGB888_TO_RGB565(r, g, b) ((b) >> 3) | (((g) >> 2) << 5) | (((r) >> 3) << 11)
#define PACK_RGB888(r, g, b) RGB888_TO_RGB565(r, g, b)
#define CLAMP(v, a, b) ((v) < (a) ? (a) : (v) > (b) ? (b) : (v))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
static uint16 pal[9][256];
static uint32 GetPaletteEntry(uint8 bankID, uint8 index) {
    uint16 clr = pal[bankID & 7][index];
    return ((clr & 0xF800) << 8) | ((clr & 0x7E0) << 5) | ((clr & 0x1F) << 3);
}
static void stockFade(uint8 d, uint8 A, uint8 B, int16 blend, int32 s, int32 e) {
    blend = CLAMP(blend, 0, 0xFF); e = MIN(e, 0x100); if (s >= e) return;
    uint32 bA = 0xFF - blend; uint16 *pc = &pal[d][s];
    for (int32 i = s; i <= e; ++i) {
        uint32 cA = GetPaletteEntry(A, i), cB = GetPaletteEntry(B, i);
        int32 r = blend * ((cB >> 16) & 0xFF) + bA * ((cA >> 16) & 0xFF);
        int32 g = blend * ((cB >> 8) & 0xFF) + bA * ((cA >> 8) & 0xFF);
        int32 b = blend * (cB & 0xFF) + bA * (cA & 0xFF);
        *pc++ = PACK_RGB888((uint8)(r >> 8), (uint8)(g >> 8), (uint8)(b >> 8));
    }
}
static uint16 sp[512];
static void tableFade(uint8 d, uint8 A, uint8 B, int16 blendAmount, int32 s, int32 e) {
    blendAmount = CLAMP(blendAmount, 0, 0xFF); e = MIN(e, 0x100); if (s >= e) return;
    uint32 blendA = 0xFF - blendAmount; uint16 *paletteColor = &pal[d][s]; uint16 *t = sp;
    for (int32 k = 0; k < 32; ++k)
        t[k] = (uint16)(blendA * (uint32)(k << 3)), t[32 + k] = (uint16)(blendAmount * (k << 3)),
        t[192 + k] = (uint16)(blendA * (uint32)(k << 3)), t[224 + k] = (uint16)(blendAmount * (k << 3));
    for (int32 k = 0; k < 64; ++k)
        t[64 + k] = (uint16)(blendA * (uint32)(k << 2)), t[128 + k] = (uint16)(blendAmount * (k << 2));
    const uint16 *pa = pal[A], *pb = pal[B];
    for (int32 i = s; i <= e; ++i, ++paletteColor) {
        uint32 a = pa[(uint8)i], c = pb[(uint8)i];
        int32 r = t[32 + (c >> 11)] + t[a >> 11];
        int32 g = t[128 + ((c >> 5) & 63)] + t[64 + ((a >> 5) & 63)];
        int32 b = t[224 + (c & 31)] + t[192 + (a & 31)];
        *paletteColor = PACK_RGB888((uint8)(r >> 8), (uint8)(g >> 8), (uint8)(b >> 8));
    }
}
int main() {
    uint32 x = 1; long bad = 0, n = 0;
    for (int trial = 0; trial < 200; ++trial) {
        uint16 base[9][256];
        for (int b = 0; b < 9; ++b) for (int i = 0; i < 256; ++i) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; base[b][i] = (uint16)x; }
        for (int blend = -2; blend <= 258; blend += 1) {
            int s = (x >> 3) & 127, e = 200 + (x & 63) + (blend & 1 ? 56 : 0);
            uint16 a[9][256], b2[9][256];
            memcpy(pal, base, sizeof pal); stockFade(0, 1, 2, (int16)blend, s, e); memcpy(a, pal, sizeof a);
            memcpy(pal, base, sizeof pal); tableFade(0, 1, 2, (int16)blend, s, e); memcpy(b2, pal, sizeof b2);
            ++n; if (memcmp(a, b2, sizeof a)) ++bad;
        }
    }
    printf("%ld fades, %ld differ\n", n, bad);
    return bad != 0;
}
