// Build stamp, the title's part (docs/32 "Build stamp"; resident part ps1/version_text.cpp): linked into the overlays of
// the folders whose objects need it (tools/gen_overlays.py EXTRAS 'stamp': TitleSetup), not the resident. PS1VersionLink
// plugs the draw into PS1RenderEndFrame (g_ps1VersionDraw).
//
// VRAM: the only spot no scene uses is sprite-CLUT row 511 (tools/gfx/vram_plan.py: atlases take slots 0-279 = rows
// 496-509; row 510 is the INK_TINT scratch, row 511 x 256-287 the bank-CLUT scratch), one row: the text's 7 pixel rows go
// side by side as 4-bit strips there, two per texture page (u 0 / 128 of the pages at x 0, 64, 128, 192; v 255), its
// CLUT at (288, 511). Each frame draws them as 1-pixel-tall sprites, a dark shadow one pixel down-right first, then the
// light text, after everything else (not over the attract video); their colours apply the frame's FillScreen fade
// exactly (one colour per sprite).
// The texture goes up on the first frame after each overlay load (the scene's load has run by then).
#include <stdint.h>

#include "gpu_chain_raw.hh"

#include <psyqo/gpu.hh>

extern psyqo::GPU *g_ps1GPU;
extern "C" {
extern char g_ps1VersionText[24];
extern void (*g_ps1VersionDraw)(uint32_t colour, int32_t aR, int32_t aG, int32_t aB);
}

namespace {

constexpr int16_t ROW_Y = 511, CLUT_X = 288; // texture row (x 0-255) and CLUT
constexpr int CELL = 6, GLYPH_H = 7, MAX_CHARS = 21; // 5 x 7 glyph + 1 column; a strip is 128 texels
constexpr int GREY = 22;   // text colour (5-bit grey, 176 in 8 bits)
constexpr int SHADOW = 16; // shadow colour (8 bits): the texels modulated down to dark grey

const char s_chars[] = "0123456789abcdefiknortuvwy-";
const uint8_t s_font[sizeof(s_chars) - 1][7] = {
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, // '0'
    {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}, // '1'
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}, // '2'
    {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e}, // '3'
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, // '4'
    {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}, // '5'
    {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}, // '6'
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // '7'
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, // '8'
    {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}, // '9'
    {0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f}, // 'a'
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1e}, // 'b'
    {0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e}, // 'c'
    {0x01, 0x01, 0x0d, 0x13, 0x11, 0x11, 0x0f}, // 'd'
    {0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e}, // 'e'
    {0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08}, // 'f'
    {0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e}, // 'i'
    {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}, // 'k'
    {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}, // 'n'
    {0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e}, // 'o'
    {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}, // 'r'
    {0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06}, // 't'
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d}, // 'u'
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04}, // 'v'
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a}, // 'w'
    {0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x0e}, // 'y'
    {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}, // '-'
};

const uint8_t *glyph(char c) {
    for (int i = 0; s_chars[i]; ++i)
        if (s_chars[i] == c)
            return s_font[i];
    return nullptr; // blank cell
}

bool s_uploaded = false;
int s_len       = 0;

void upload() {
    uint16_t tex[256] = {}; // row 511, x 0-255: strip r at page r / 2, u (r & 1) * 128
    s_len = 0;
    while (g_ps1VersionText[s_len] && s_len < MAX_CHARS) ++s_len;
    for (int i = 0; i < s_len; ++i) {
        const uint8_t *g = glyph(g_ps1VersionText[i]);
        for (int r = 0; g && r < GLYPH_H; ++r)
            for (int x = 0; x < 5; ++x)
                if (g[r] & (0x10 >> x)) {
                    int t = (r >> 1) * 256 + (r & 1) * 128 + i * CELL + x; // texel in the row
                    tex[t >> 2] |= (uint16_t)(1u << ((t & 3) * 4));        // CLUT index 1
                }
    }
    uint16_t clut[16] = {}; // 0 transparent; 1 the text colour, STP clear (a fade to black leaves 0x0000)
    clut[1] = GREY | GREY << 5 | GREY << 10;
    PS1GpuImmediate();
    g_ps1GPU->uploadToVRAM(tex, psyqo::Rect{ { 0, ROW_Y }, { 256, 1 } });
    g_ps1GPU->uploadToVRAM(clut, psyqo::Rect{ { CLUT_X, ROW_Y }, { 16, 1 } });
    s_uploaded = true;
}

uint32_t modulation(int want, uint32_t colour, int32_t aR, int32_t aG, int32_t aB) {
    // texel x colour / 128 per channel (psx-spx "Texture Color"): colour = 128 * blend / texel colour; the texel is 176
    // (>= 128), so the blend reaches white too
    constexpr int T = GREY * 8;
    const int32_t a[3] = { aR, aG, aB };
    const int32_t f[3] = { (int32_t)(colour >> 16 & 0xFF), (int32_t)(colour >> 8 & 0xFF), (int32_t)(colour & 0xFF) };
    uint32_t mod = 0;
    for (int ch = 0; ch < 3; ++ch) {
        int32_t m = ((want * (255 - a[ch]) + f[ch] * a[ch]) / 255) * 128 / T;
        mod |= (uint32_t)(m > 255 ? 255 : m) << (8 * ch);
    }
    return mod;
}

void draw(uint32_t colour, int32_t aR, int32_t aG, int32_t aB) {
    if (!s_uploaded)
        upload();
    const int x = 8, y = 240 - 8 - GLYPH_H;
    const uint32_t mod[2] = { modulation(SHADOW, colour, aR, aG, aB), modulation(GREY * 8, colour, aR, aG, aB) };
    const uint32_t wh = (uint32_t)(s_len * CELL - 1) | 1u << 16, clut = (uint32_t)((ROW_Y << 6) | (CLUT_X >> 4)) << 16;
    uint32_t *w = PS1ChainSpanBegin(4 * (10 + 9)); // 4 pages: [header, texpage, 2 shadow rows] [header, 2 text rows]
    if (!w)
        return;
    uint32_t *head = nullptr;
    int words      = 0;
    auto open = [&](int n) { // a fragment of n words after its header; the previous header links to it
        if (head)
            *head = (uint32_t)words << 24 | ((uint32_t)(uintptr_t)w & 0xFFFFFF);
        head  = w++;
        words = n;
    };
    for (int p = 0; p < 4; ++p) {
        int rows = p < 3 ? 2 : 1; // pixel rows 2p, 2p + 1 (row 7 doesn't exist)
        for (int k = 0; k < 2; ++k) { // k 0: shadow (one pixel down-right), 1: text
            open((k ? 0 : 1) + 4 * rows);
            if (!k)
                *w++ = 0xE1000000u | (uint32_t)p | 1u << 4; // texpage: x p * 64, y 256, 4-bit
            for (int j = 0; j < rows; ++j) {
                int r = 2 * p + j;
                w[0] = 0x64000000u | mod[k]; // textured rectangle, variable size, opaque, modulated
                w[1] = (uint32_t)(uint16_t)(x + 1 - k) | (uint32_t)(uint16_t)(y + r + 1 - k) << 16;
                w[2] = (uint32_t)((r & 1) * 128) | 255u << 8 | clut;
                w[3] = wh;
                w += 4;
            }
        }
    }
    PS1ChainSpanEnd(head, words, w);
    PS1ChainInvalidateTPage(); // the texpage went in raw
}

} // namespace

extern "C" void PS1VersionLink() {
    g_ps1VersionDraw = draw;
    s_uploaded       = false;
}
