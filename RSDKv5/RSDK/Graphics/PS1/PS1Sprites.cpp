// PS1 port (docs/32 3.4): sprites drawn by the GPU from the atlas (PS1Atlas.hpp) and tiles (PS1Tiles). Included by
// Drawing.cpp.
//
// DrawSprite keeps the frame record, whose binding (SpriteFrame::ps1U / ps1V / ps1Attr) says where its texels are: the
// texture page (column, page row, depth), the top-left texel, half resolution, the CLUT slot. DrawTile's pieces come
// from the scene's tile images (PS1Tiles: the same as the layers'). An unflipped rectangle is a sprite (GP0 64h, after a
// texpage word); a flipped one a textured quad (GP0 2Ch: carries its texpage) with backwards UVs, the far edge one
// texel past the first (u + w - 1 .. u - 1, as the tile layers: exact on the GPU), split where that edge would leave
// the page; a half-resolution frame a quad over its halved texels (each drawn 2x). Rotated / scaled rectangles (3.4c)
// are quads on DrawSpriteRotozoom's own corners (integer, sin512 / cos512), their far texel edges one texel in (Sonic
// 2's rule: nothing from the neighbouring texels; the picture is then approximate). Off-screen rectangles are culled
// before anything is chained. Inks (3.4b) on the GPU's semi-transparency modes (psx-spx: 0 = B/2 + F/2, 1 = B + F,
// 2 = B - F; every CLUT colour has its STP bit, so every texel blends): blend = mode 0; alpha in the Sonic CD levels
// (user decision 2026-09-24: < 64 not drawn, < 160 one 50 % pass, < 224 two 50 % passes = B/4 + 3F/4, else opaque);
// add / sub = modes 1 / 2 with the texture modulated by alpha / 2 (0x80 = 1.0). Tint, masked and unmasked are counted,
// as are DrawSpriteFlipped / DrawSpriteRotozoom calls with sheet coordinates only (no frame, no tile).
#include "../../../../ps1/gpu_chain_raw.hh"

// Counters and the draw log at global scope, declared in no header: GDB then finds them by their plain names (an
// extern "C" variable declared inside namespace RSDK gets the debug name RSDK::x, which GDB can't match to the ELF).
extern "C" {
volatile uint32 g_ps1SpriteDraws    = 0; // rectangles chained (this frame): frames, tiles, rotated ones
volatile uint32 g_ps1SpriteMissing  = 0; // frames without a binding (not in the atlas, or their group not loaded): must stay 0
volatile uint32 g_ps1SpriteCulled   = 0; // rectangles off the screen (this frame)
volatile uint32 g_ps1SpriteInkTodo  = 0; // draws with an ink the GPU does not do yet: tint, masked, unmasked (total)
volatile uint32 g_ps1SpriteFxTodo   = 0; // rotated / scaled draws not done (too big for the GPU; total)
volatile uint32 g_ps1SpriteUnbound  = 0; // DrawSpriteFlipped / DrawSpriteRotozoom calls with sheet coordinates only (total)
volatile uint32 g_ps1SpriteClipTodo = 0; // draws while the screen's clip bounds are not the whole screen (total)
volatile uint32 g_ps1SpriteRotated  = 0; // rotated / scaled rectangles drawn (this frame)
volatile uint32 g_ps1TileDraws      = 0; // DrawTile pieces drawn (this frame)
volatile uint32 g_ps1TileMissing    = 0; // DrawTile pieces whose tile has no image (not in the scene's .ptl; total)
extern volatile uint32 g_ps1BankTodo; // PS1Layers.cpp: draws under another palette bank not done yet
#if PS1_DEBUG_BIGRAM
// The frame's draw calls in order (8 MB link only; tools/gfx/layer_reference.gdb dumps them): type 0 = a tile layer
// (x = its PS1DrawLayerHScroll call number), 1 = an atlas rectangle (binding, size, position, flip, ink), 2 = a tile
// (attr = tile index, direction = flips), 3 = a rotated / scaled atlas rectangle (x, y = corner A) followed by 4
// (x, y = corner B, w, h = corner C) and 5 (x, y = corner D), 6 = a rotated tile (the same three entries, attr = tile),
// 7 = a flat rectangle (u, v, attr = R, G, B as sent; direction = semi-transparency mode + 1, ink = passes),
// 8 = FillScreen (u, v, attr = R, G, B; x, y, w = the alphas)
struct PS1DrawLogEntry {
    uint8 type, direction, ink, alpha;
    uint8 u, v;
    uint16 attr;
    int16 x, y;
    uint16 w, h;
};
PS1DrawLogEntry g_ps1DrawLog[2048]; // 32 KB (8 MB link only): faces log a rectangle per run of rows
volatile uint32 g_ps1DrawLogCount = 0;
#endif
}

namespace RSDK
{
// PS1LayersFrameBegin: this frame's counters (and the draw log) start over
void PS1SpritesFrameBegin()
{
    // every counter read once: the linker keeps them (one only reachable from code the compiler folded away would go,
    // and the GDB scripts that print it would stop)
    (void)g_ps1SpriteMissing, (void)g_ps1SpriteInkTodo, (void)g_ps1SpriteFxTodo, (void)g_ps1SpriteUnbound;
    (void)g_ps1SpriteClipTodo, (void)g_ps1TileMissing;
    g_ps1SpriteDraws = g_ps1SpriteCulled = g_ps1SpriteRotated = g_ps1TileDraws = 0;
#if PS1_DEBUG_BIGRAM
    g_ps1DrawLogCount = 0;
#endif
}

#if PS1_DEBUG_BIGRAM
void PS1DrawLogAdd(uint8 type, uint8 direction, uint8 ink, uint8 alpha, uint8 u, uint8 v, uint16 attr, int32 x, int32 y, int32 w,
                   int32 h)
{
    if (g_ps1DrawLogCount >= sizeof(g_ps1DrawLog) / sizeof(g_ps1DrawLog[0]))
        return;
    PS1DrawLogEntry &e = g_ps1DrawLog[g_ps1DrawLogCount];
    e.type = type, e.direction = direction, e.ink = ink, e.alpha = alpha, e.u = u, e.v = v, e.attr = attr;
    e.x = (int16)x, e.y = (int16)y, e.w = (uint16)w, e.h = (uint16)h;
    g_ps1DrawLogCount = g_ps1DrawLogCount + 1;
}
#endif
} // namespace RSDK

namespace
{
using namespace RSDK;

#define PS1_SPRITE_CLUTS_PER_ROW 20 // tools/gfx/vram_plan.py SPRITE_CLUT_SLOTS: rows 496-511, x 0-319 in steps of 16

// How a rectangle blends: the GPU's semi-transparency mode (-1: opaque), passes, texture modulation
struct PS1Ink {
    int32 mode, passes;
    uint32 mod;
};

// DrawSpriteFlipped's ink rules (alpha: > 0xFF opaque, <= 0 nothing; add / sub: clamped to 0xFF), then the GPU's.
// False: nothing to draw (or an ink not done: counted)
bool PS1InkOf(int32 inkEffect, int32 alpha, PS1Ink &ink)
{
    ink.mode = -1, ink.passes = 1, ink.mod = 0x80;
    switch (inkEffect) {
        case INK_NONE: return true;
        case INK_BLEND: ink.mode = 0; return true;
        case INK_ALPHA: {
            if (alpha > 0xFF)
                return true;
            int32 level = alpha < 64 ? 0 : alpha < 160 ? 1 : alpha < 224 ? 2 : 3;
            if (!level)
                return false;
            if (level < 3)
                ink.mode = 0, ink.passes = level;
            return true;
        }
        case INK_ADD:
        case INK_SUB:
            if (alpha <= 0)
                return false;
            ink.mode = inkEffect == INK_ADD ? 1 : 2;
            ink.mod  = (uint32)((alpha > 0xFF ? 0xFF : alpha) + 1) >> 1;
            return true;
        default: g_ps1SpriteInkTodo = g_ps1SpriteInkTodo + 1; return false;
    }
}

inline uint8 PS1LogAlpha(int32 alpha) { return (uint8)(alpha > 0xFF ? 0xFF : alpha < 0 ? 0 : alpha); }

// The PC's own early returns (DrawSpriteFlipped / DrawSpriteRotozoom): nothing is drawn, validDraw stays as it is
inline bool PS1PcDraws(int32 inkEffect, int32 alpha)
{
    if ((inkEffect == INK_ALPHA || inkEffect == INK_ADD || inkEffect == INK_SUB) && alpha <= 0)
        return false;
    return inkEffect != INK_TINT || tintLookupTable;
}

// DrawSpriteFlipped's clip against the screen's clip bounds: a rectangle left non-empty sets validDraw (the draw pass
// ORs it into entity->onScreen, which game code reads: Splats, Spikes, Platform...). The PS1 then may still not draw
// it (no binding, an ink the GPU lacks), but the game logic sees what the PC's would.
inline bool PS1PcClip(int32 x, int32 y, int32 w, int32 h)
{
    int32 x0 = x > currentScreen->clipBound_X1 ? x : currentScreen->clipBound_X1;
    int32 x1 = x + w < currentScreen->clipBound_X2 ? x + w : currentScreen->clipBound_X2;
    int32 y0 = y > currentScreen->clipBound_Y1 ? y : currentScreen->clipBound_Y1;
    int32 y1 = y + h < currentScreen->clipBound_Y2 ? y + h : currentScreen->clipBound_Y2;
    if (x1 <= x0 || y1 <= y0)
        return false;
    validDraw = true;
    return true;
}

// DrawSpriteRotozoom's clip: its corners with a 2-pixel margin, the bounds starting from (pitch, 0, size.y, 0) as it
// does; a box of at least 1 x 1 sets validDraw
bool PS1PcClipRotozoom(int32 x, int32 y, int32 pivotX, int32 pivotY, int32 w, int32 h, int32 scaleX, int32 scaleY, int32 direction,
                       int32 rotation)
{
    int32 angle = 0x200 - (rotation & 0x1FF);
    if (!(rotation & 0x1FF))
        angle = rotation & 0x1FF;
    int32 sine = sin512LookupTable[angle], cosine = cos512LookupTable[angle];
    int32 fullScaleXS = scaleX * sine >> 9, fullScaleXC = scaleX * cosine >> 9;
    int32 fullScaleYS = scaleY * sine >> 9, fullScaleYC = scaleY * cosine >> 9;
    int32 posX[4], posY[4], xMax, scaledX1, scaledX2, scaledY1, scaledY2;
    if (direction == FLIP_X) {
        scaledX1 = fullScaleXS * (2 - pivotX), scaledX2 = fullScaleXC * (2 - pivotX);
        xMax     = -2 - pivotX - w;
    }
    else {
        scaledX1 = fullScaleXS * (pivotX - 2), scaledX2 = fullScaleXC * (pivotX - 2);
        xMax     = pivotX + 2 + w;
    }
    scaledY1 = fullScaleYS * (pivotY - 2), scaledY2 = fullScaleYC * (pivotY - 2);
    posX[0]  = x + ((scaledX2 + scaledY1) >> 9);
    posY[0]  = y + ((fullScaleYC * (pivotY - 2) - scaledX1) >> 9);
    int32 scaledXMaxS = fullScaleXS * xMax, scaledXMaxC = fullScaleXC * xMax;
    int32 scaledYMaxC = fullScaleYC * (pivotY + 2 + h), scaledYMaxS = fullScaleYS * (pivotY + 2 + h);
    posX[1] = x + ((scaledXMaxC + scaledY1) >> 9), posY[1] = y + ((scaledY2 - scaledXMaxS) >> 9);
    posX[2] = x + ((scaledYMaxS + scaledX2) >> 9), posY[2] = y + ((scaledYMaxC - scaledX1) >> 9);
    posX[3] = x + ((scaledXMaxC + scaledYMaxS) >> 9), posY[3] = y + ((scaledYMaxC - scaledXMaxS) >> 9);
    int32 left = currentScreen->pitch, right = 0, top = currentScreen->size.y, bottom = 0;
    for (int32 i = 0; i < 4; ++i) {
        left   = posX[i] < left ? posX[i] : left;
        right  = posX[i] > right ? posX[i] : right;
        top    = posY[i] < top ? posY[i] : top;
        bottom = posY[i] > bottom ? posY[i] : bottom;
    }
    left   = left < currentScreen->clipBound_X1 ? currentScreen->clipBound_X1 : left;
    right  = right > currentScreen->clipBound_X2 ? currentScreen->clipBound_X2 : right;
    top    = top < currentScreen->clipBound_Y1 ? currentScreen->clipBound_Y1 : top;
    bottom = bottom > currentScreen->clipBound_Y2 ? currentScreen->clipBound_Y2 : bottom;
    if (right - left < 1 || bottom - top < 1)
        return false;
    validDraw = true;
    return true;
}

// A textured quad (GP0 2Ch | semi-transparency | modulation) with its four corners (A B C D = top-left, top-right,
// bottom-left, bottom-right of the texture) and their texels
uint32_t *PS1QuadWords(uint32_t *w, uint32 cmd, const int32 *px, const int32 *py, const int32 *tu, const int32 *tv, uint32 clut,
                       uint32 tpage)
{
    w[0] = cmd;
    for (int32 k = 0; k < 4; ++k) {
        w[1 + 2 * k] = (uint32)(uint16)py[k] << 16 | (uint16)px[k];
        uint32 uv    = (uint32)(uint8)tv[k] << 8 | (uint8)tu[k];
        w[2 + 2 * k] = k == 0 ? clut << 16 | uv : k == 1 ? tpage << 16 | uv : uv;
    }
    return w + 9;
}

// A textured quad over the axis-aligned screen rectangle (x, y, qw, qh), texels [uL, uR) x [vT, vB) (may run backwards)
inline uint32_t *PS1SpriteQuad(uint32_t *w, uint32 cmd, int32 x, int32 y, int32 qw, int32 qh, uint32 clut, uint32 tpage, int32 uL,
                               int32 uR, int32 vT, int32 vB)
{
    int32 px[4] = { x, x + qw, x, x + qw }, py[4] = { y, y, y + qh, y + qh };
    int32 tu[4] = { uL, uR, uL, uR }, tv[4] = { vT, vT, vB, vB };
    return PS1QuadWords(w, cmd, px, py, tu, tv, clut, tpage);
}

bool PS1OffScreen(int32 x0, int32 y0, int32 x1, int32 y1) // [x0, x1) x [y0, y1)
{
    int32 sw = currentScreen->size.x, sh = currentScreen->size.y;
    if (x0 >= sw || y0 >= sh || x1 <= 0 || y1 <= 0) {
        g_ps1SpriteCulled = g_ps1SpriteCulled + 1;
        return true;
    }
    if (currentScreen->clipBound_X1 > 0 || currentScreen->clipBound_Y1 > 0 || currentScreen->clipBound_X2 < sw
        || currentScreen->clipBound_Y2 < sh)
        g_ps1SpriteClipTodo = g_ps1SpriteClipTodo + 1;
    return false;
}

// Texels [u, u + tw) x [v, v + th) of a page drawn as the w x h screen rectangle at (x, y), flipped as `direction`
// (half: the texels are the w x h picture halved). False: culled / nothing chained
bool PS1TexRect(int32 x, int32 y, int32 w, int32 h, int32 u, int32 v, uint32 tpage, uint32 clut, int32 direction, bool half,
                const PS1Ink &ink)
{
    if (PS1OffScreen(x, y, x + w, y + h))
        return false;
    uint32 semi = 0, colour = ink.mod | ink.mod << 8 | ink.mod << 16;
    if (ink.mode >= 0)
        tpage |= (uint32)ink.mode << 5, semi = 0x02000000;
    bool fx = direction & FLIP_X, fy = direction & FLIP_Y;

    if (!fx && !fy && !half) {
        for (int32 p = 0; p < ink.passes; ++p) {
            uint32_t *wr = PS1ChainSpanBegin(6);
            if (!wr)
                return false;
            wr[1] = 0xE1000000 | tpage;
            wr[2] = 0x64000000 | semi | colour;
            wr[3] = (uint32)(uint16)y << 16 | (uint16)x;
            wr[4] = clut << 16 | (uint32)v << 8 | (uint32)u;
            wr[5] = (uint32)h << 16 | (uint32)w;
            PS1ChainSpanEnd(wr, 5, wr + 6);
        }
        return true;
    }
    // texels per axis (halved frames: ceil(w / 2)); a flipped axis runs from its last texel to one before its first
    int32 tw = half ? (w + 1) >> 1 : w, th = half ? (h + 1) >> 1 : h;
    int32 uL = fx ? u + tw - 1 : u, uR = fx ? u - 1 : u + tw;
    int32 vT = fy ? v + th - 1 : v, vB = fy ? v - 1 : v + th;
    // an edge one past the page (u / v -1, or 256) can't be addressed: that column / row becomes its own strip
    int32 qw = w, uEdge = -1, qh = h, vEdge = -1;
    if (uR < 0 || uR > 255) {
        if (half) // halved: the texture edge is inside the last pair of pixels; clamp (the frame is approximate)
            uR = uR < 0 ? 0 : 255;
        else
            qw = w - 1, uEdge = uR < 0 ? 0 : 255, uR = uR < 0 ? 0 : 255;
    }
    if (vB < 0 || vB > 255) {
        if (half)
            vB = vB < 0 ? 0 : 255;
        else
            qh = h - 1, vEdge = vB < 0 ? 0 : 255, vB = vB < 0 ? 0 : 255;
    }
    uint32 qcmd = 0x2C000000 | semi | colour;
    for (int32 p = 0; p < ink.passes; ++p) {
        uint32_t *wr = PS1ChainSpanBegin(4 * 10);
        if (!wr)
            return false;
        uint32_t *hdr = wr, *q = wr + 1;
        int32 quads = 0;
#define PS1_SPRITE_QUAD(X, Y, QW, QH, UL, UR, VT, VB)                                                                   \
    do {                                                                                                               \
        if (quads)                                                                                                     \
            *hdr = 9u << 24 | ((uintptr_t)(q - 1) & 0xFFFFFF), hdr = q - 1;                                           \
        q = PS1SpriteQuad(q, qcmd, X, Y, QW, QH, clut, tpage, UL, UR, VT, VB) + 1;                                      \
        ++quads;                                                                                                       \
    } while (0)
        if (qw > 0 && qh > 0)
            PS1_SPRITE_QUAD(x, y, qw, qh, uL, uR, vT, vB);
        if (uEdge >= 0 && qh > 0)
            PS1_SPRITE_QUAD(x + qw, y, 1, qh, uEdge, uEdge, vT, vB);
        if (vEdge >= 0) {
            int32 vLast = fy ? v : v + th - 1;
            if (qw > 0)
                PS1_SPRITE_QUAD(x, y + qh, qw, 1, uL, uR, vLast, vLast);
            if (uEdge >= 0)
                PS1_SPRITE_QUAD(x + qw, y + qh, 1, 1, uEdge, uEdge, vLast, vLast);
        }
#undef PS1_SPRITE_QUAD
        if (!quads)
            return false;
        PS1ChainSpanEnd(hdr, 9, q - 1);
    }
    return true;
}

// A rectangle rotated / scaled on DrawSpriteRotozoom's corners: pivot space [pivotX, pivotX + w) x [pivotY, pivotY + h)
// (FLIP_X: [-pivotX - w, -pivotX), texels mirrored), scale 0x200 = 1, rotation 512 per turn. cx / cy: the corners
// A B C D (for the log). False: culled / too big for the GPU
bool PS1TexRotozoom(int32 x, int32 y, int32 pivotX, int32 pivotY, int32 w, int32 h, int32 u, int32 v, uint32 tpage, uint32 clut,
                    bool half, int32 scaleX, int32 scaleY, int32 direction, int32 rotation, const PS1Ink &ink, int32 *cx, int32 *cy)
{
    int32 angle = 0x200 - (rotation & 0x1FF);
    if (!(rotation & 0x1FF))
        angle = 0;
    angle &= 0x1FF;
    int32 s = sin512LookupTable[angle], c = cos512LookupTable[angle];
    int32 sxs = scaleX * s >> 9, sxc = scaleX * c >> 9, sys = scaleY * s >> 9, syc = scaleY * c >> 9;
    bool fx   = direction & FLIP_X;
    int32 X0 = fx ? -pivotX - w : pivotX, X1 = X0 + w, Y0 = pivotY, Y1 = pivotY + h;
    int32 xs[2] = { X0, X1 }, ys[2] = { Y0, Y1 };
    int32 minX = 0x7FFFFFFF, minY = 0x7FFFFFFF, maxX = -0x7FFFFFFF, maxY = -0x7FFFFFFF;
    for (int32 k = 0; k < 4; ++k) {
        int32 X = xs[k & 1], Y = ys[k >> 1];
        cx[k] = x + ((X * sxc + Y * sys) >> 9);
        cy[k] = y + ((Y * syc - X * sxs) >> 9);
        minX = cx[k] < minX ? cx[k] : minX, maxX = cx[k] > maxX ? cx[k] : maxX;
        minY = cy[k] < minY ? cy[k] : minY, maxY = cy[k] > maxY ? cy[k] : maxY;
    }
    if (PS1OffScreen(minX, minY, maxX + 1, maxY + 1))
        return false;
    if (maxX - minX > 1023 || maxY - minY > 511 || minX < -1024 || minY < -1024 || maxX > 1023 || maxY > 1023) {
        g_ps1SpriteFxTodo = g_ps1SpriteFxTodo + 1; // the GPU's vertex / size limits
        return false;
    }
    // texels: the far edges one texel in (nothing from the neighbours when the quad is not axis-aligned)
    int32 tw = half ? (w + 1) >> 1 : w, th = half ? (h + 1) >> 1 : h;
    int32 u0 = fx ? u + tw - 1 : u, u1 = fx ? u : u + tw - 1;
    int32 tu[4] = { u0, u1, u0, u1 }, tv[4] = { v, v, v + th - 1, v + th - 1 };
    uint32 semi = 0, colour = ink.mod | ink.mod << 8 | ink.mod << 16;
    if (ink.mode >= 0)
        tpage |= (uint32)ink.mode << 5, semi = 0x02000000;
    for (int32 p = 0; p < ink.passes; ++p) {
        uint32_t *wr = PS1ChainSpanBegin(10);
        if (!wr)
            return false;
        PS1QuadWords(wr + 1, 0x2C000000 | semi | colour, cx, cy, tu, tv, clut, tpage);
        PS1ChainSpanEnd(wr, 9, wr + 10);
    }
    return true;
}

#define PS1_SPRITE_CLUT_SCRATCH 318 // two spare sprite CLUT slots (318, 319: row 511, x 256 / 272) for other banks

// The frame's CLUT when its top line uses palette bank `bank` != 0 (SetActivePalette: the PC draws each line with its
// bank as it is now): the same palette indices from that bank, uploaded in the chain right before the draw (an 8-bit
// frame's 256 entries into the bank's own row 240 + bank; a 4-bit frame's 16 into a spare slot, alternating)
uint32 PS1BankClut(uint16 attr, int32 bank)
{
    static uint16 s_row[256];
    static int32 s_flip = 0;
    uint32 slot = PS1_ATLAS_CLUT(attr);
    if (slot == PS1_ATLAS_BANK_CLUT) {
        s_row[0] = 0;
        for (int32 i = 1; i < 256; ++i) s_row[i] = (uint16)(0x8000 | ((fullPalette[bank][i] >> 11) & 31) | (((fullPalette[bank][i] >> 6) & 31) << 5)
                                                        | ((fullPalette[bank][i] & 31) << 10));
        PS1UploadClut(s_row, 256, 0, (int16)(240 + bank));
        return (uint32)(240 + bank) << 6;
    }
    const PS1AtlasClut *ac = PS1AtlasCluts();
    if (!ac || slot >= PS1AtlasClutCount())
        return 0;
    s_row[0] = 0;
    for (int32 i = 1; i < 16; ++i) {
        uint16 c = ac[slot].map[i] ? fullPalette[bank][ac[slot].map[i]] : 0;
        s_row[i] = ac[slot].map[i] ? (uint16)(0x8000 | ((c >> 11) & 31) | (((c >> 6) & 31) << 5) | ((c & 31) << 10)) : 0;
    }
    uint32 scratch = PS1_SPRITE_CLUT_SCRATCH + (s_flip ^= 1);
    int16 x = (int16)((scratch % PS1_SPRITE_CLUTS_PER_ROW) * 16), y = (int16)(496 + scratch / PS1_SPRITE_CLUTS_PER_ROW);
    PS1UploadClut(s_row, 16, x, y);
    return (uint32)(x >> 4) | (uint32)y << 6;
}

// The palette bank of screen line y (clamped to the screen)
inline int32 PS1LineBank(int32 y)
{
    int32 sh = currentScreen->size.y;
    return gfxLineBuffer[y < 0 ? 0 : y >= sh ? sh - 1 : y];
}

// A frame's binding as a texture page, CLUT and top-left texel. False: not in the atlas (counted)
bool PS1FrameTex(const SpriteFrame *frame, uint32 &tpage, uint32 &clut, bool &half)
{
    PS1FadeRestore(); // a FillScreen earlier this frame faded the CLUTs
    uint16 attr = frame->ps1Attr;
    if (attr == PS1_ATLAS_MISSING) {
        g_ps1SpriteMissing = g_ps1SpriteMissing + 1;
        return false;
    }
    tpage       = PS1_ATLAS_COLUMN(attr) | PS1_ATLAS_ROW(attr) << 4 | PS1_ATLAS_8BIT(attr) << 7;
    uint32 slot = PS1_ATLAS_CLUT(attr);
    clut = slot == PS1_ATLAS_BANK_CLUT ? (uint32)(240 << 6) : (slot % PS1_SPRITE_CLUTS_PER_ROW) | (496 + slot / PS1_SPRITE_CLUTS_PER_ROW) << 6;
    half = PS1_ATLAS_HALF(attr);
    return true;
}

// A tile's image (the scene's .ptl, as the layers draw it). False: no image (counted)
bool PS1TileTex(uint16 tile, int32 &u, int32 &v, uint32 &tpage, uint32 &clut)
{
    PS1FadeRestore();
    uint16 img = PS1TileImageOf(tile & 0x3FF);
    if (img == PS1_TILE_NONE) {
        g_ps1TileMissing = g_ps1TileMissing + 1;
        return false;
    }
    const PS1TileImage *ti = &PS1TileImages()[img];
    u = ti->u, v = ti->v, tpage = ti->tpage;
    clut = ti->clut == 0xFFFF ? (uint32)(240 << 6) : ti->clut;
    return true;
}
} // namespace

// The frame at screen (x, y) (its top-left after the pivot, as DrawSpriteFlipped gets it), flipped as `direction`
void RSDK::PS1DrawFrame(const SpriteFrame *frame, int32 x, int32 y, int32 direction, int32 inkEffect, int32 alpha)
{
    int32 w = frame->width, h = frame->height;
    uint32 tpage, clut;
    bool half;
    PS1Ink ink;
    if (w <= 0 || h <= 0 || !PS1PcDraws(inkEffect, alpha) || !PS1PcClip(x, y, w, h))
        return;
    if (!PS1FrameTex(frame, tpage, clut, half) || !PS1InkOf(inkEffect, alpha, ink))
        return;
    if (int32 bank = PS1LineBank(y)) {
        clut = PS1BankClut(frame->ps1Attr, bank);
        if (!clut)
            return;
    }
    if (!PS1TexRect(x, y, w, h, frame->ps1U, frame->ps1V, tpage, clut, direction, half, ink))
        return;
    g_ps1SpriteDraws = g_ps1SpriteDraws + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(1, (uint8)direction, (uint8)inkEffect, PS1LogAlpha(alpha), frame->ps1U, frame->ps1V, frame->ps1Attr, x, y, w, h);
#endif
}

// DrawSprite's rotated / scaled frames: DrawSpriteRotozoom(x, y, pivotX, pivotY, ... scaleX, scaleY, direction, rotation)
void RSDK::PS1DrawFrameRotozoom(const SpriteFrame *frame, int32 x, int32 y, int32 scaleX, int32 scaleY, int32 direction, int32 rotation,
                                int32 inkEffect, int32 alpha)
{
    int32 w = frame->width, h = frame->height;
    uint32 tpage, clut;
    bool half;
    PS1Ink ink;
    int32 cx[4], cy[4];
    if (w <= 0 || h <= 0 || scaleX <= 0 || scaleY <= 0 || !PS1PcDraws(inkEffect, alpha)
        || !PS1PcClipRotozoom(x, y, frame->pivotX, frame->pivotY, w, h, scaleX, scaleY, direction, rotation))
        return;
    if (!PS1FrameTex(frame, tpage, clut, half) || !PS1InkOf(inkEffect, alpha, ink))
        return;
    if (int32 bank = PS1LineBank(y)) {
        clut = PS1BankClut(frame->ps1Attr, bank);
        if (!clut)
            return;
    }
    if (!PS1TexRotozoom(x, y, frame->pivotX, frame->pivotY, w, h, frame->ps1U, frame->ps1V, tpage, clut, half, scaleX, scaleY, direction,
                        rotation, ink, cx, cy))
        return;
    g_ps1SpriteDraws   = g_ps1SpriteDraws + 1;
    g_ps1SpriteRotated = g_ps1SpriteRotated + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(3, (uint8)direction, (uint8)inkEffect, PS1LogAlpha(alpha), frame->ps1U, frame->ps1V, frame->ps1Attr, cx[0], cy[0], w, h);
    PS1DrawLogAdd(4, 0, 0, 0, 0, 0, 0, cx[1], cy[1], (uint16)cx[2], (uint16)cy[2]);
    PS1DrawLogAdd(5, 0, 0, 0, 0, 0, 0, cx[3], cy[3], 0, 0);
#endif
}

// A DrawTile piece: the 16x16 tile (its flip bits) at screen (x, y)
void RSDK::PS1DrawTile(uint16 tile, int32 x, int32 y, int32 inkEffect, int32 alpha)
{
    int32 u, v;
    uint32 tpage, clut;
    PS1Ink ink;
    if (!PS1PcDraws(inkEffect, alpha) || !PS1PcClip(x, y, TILE_SIZE, TILE_SIZE))
        return;
    if (!PS1TileTex(tile, u, v, tpage, clut) || !PS1InkOf(inkEffect, alpha, ink))
        return;
    if (PS1LineBank(y))
        g_ps1BankTodo = g_ps1BankTodo + 1;
    int32 direction = (tile >> 10) & 3;
    if (!PS1TexRect(x, y, TILE_SIZE, TILE_SIZE, u, v, tpage, clut, direction, false, ink))
        return;
    g_ps1SpriteDraws = g_ps1SpriteDraws + 1;
    g_ps1TileDraws   = g_ps1TileDraws + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(2, (uint8)direction, (uint8)inkEffect, PS1LogAlpha(alpha), 0, 0, tile & 0x3FF, x, y, TILE_SIZE, TILE_SIZE);
#endif
}

// DrawTile's rotated / scaled pieces: DrawSpriteRotozoom(x, y, pivotX, pivotY, 16, 16, ..., scaleX, scaleY, direction,
// rotation) (DrawTile turns a tile's Y flip into rotation + 0x100 with X flipped)
void RSDK::PS1DrawTileRotozoom(uint16 tile, int32 x, int32 y, int32 pivotX, int32 pivotY, int32 scaleX, int32 scaleY, int32 direction,
                               int32 rotation, int32 inkEffect, int32 alpha)
{
    int32 u, v;
    uint32 tpage, clut;
    PS1Ink ink;
    int32 cx[4], cy[4];
    if (scaleX <= 0 || scaleY <= 0 || !PS1PcDraws(inkEffect, alpha)
        || !PS1PcClipRotozoom(x, y, pivotX, pivotY, TILE_SIZE, TILE_SIZE, scaleX, scaleY, direction, rotation))
        return;
    if (!PS1TileTex(tile, u, v, tpage, clut) || !PS1InkOf(inkEffect, alpha, ink))
        return;
    if (!PS1TexRotozoom(x, y, pivotX, pivotY, TILE_SIZE, TILE_SIZE, u, v, tpage, clut, false, scaleX, scaleY, direction, rotation, ink, cx,
                        cy))
        return;
    g_ps1SpriteDraws   = g_ps1SpriteDraws + 1;
    g_ps1SpriteRotated = g_ps1SpriteRotated + 1;
    g_ps1TileDraws     = g_ps1TileDraws + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(6, (uint8)direction, (uint8)inkEffect, PS1LogAlpha(alpha), 0, 0, tile & 0x3FF, cx[0], cy[0], TILE_SIZE, TILE_SIZE);
    PS1DrawLogAdd(4, 0, 0, 0, 0, 0, 0, cx[1], cy[1], (uint16)cx[2], (uint16)cy[2]);
    PS1DrawLogAdd(5, 0, 0, 0, 0, 0, 0, cx[3], cy[3], 0, 0);
#endif
}

// --- 3.6: flat primitives ------------------------------------------------------------------------------------------
// DrawRectangle, DrawFace and DrawCircle fill rows of the framebuffer on the PC; here each run of identical rows is one
// flat rectangle (GP0 60h: not dithered), from the same arithmetic (faces and circles through a port of their edge
// buffer, kept in the scratchpad as int16 pairs: the layer drawer only uses it inside its own calls), so the pixels are
// the PC's; the colour is the 24-bit one (the GPU keeps its top 5 bits per channel). Lines and gouraud faces are GPU
// lines / polygons (approximate). Flat inks: as sprites, add / sub with the colour scaled by alpha on the CPU (a flat
// colour can't be modulated).
extern "C" {
volatile uint32 g_ps1FlatDraws = 0;  // flat rectangles chained (this frame)
volatile uint32 g_ps1PrimTodo  = 0;  // primitives not drawn yet: circle outlines, tint / masked flat inks (total)
volatile uint32 g_ps1PrimApprox = 0; // lines and gouraud faces drawn by the GPU's own rules (total)
}

namespace
{
struct PS1Edge {
    int16 start, end;
};
#define PS1_EDGE_ROWS 241 // rows 0..240: the PC's loops touch the row at clipBound_Y2
#define PS1_EDGES     ((PS1Edge *)0x1F800000)
static_assert(PS1_EDGE_ROWS * sizeof(PS1Edge) <= 1024, "the edge buffer must fit the scratchpad");

// A flat colour (0xRRGGBB) and ink as GP0 colour bits (0xBBGGRR) + semi-transparency; false: nothing drawn / not done
bool PS1FlatInk(uint32 color, int32 inkEffect, int32 alpha, uint32 &bgr, PS1Ink &ink)
{
    uint32 r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    ink.mode = -1, ink.passes = 1, ink.mod = 0x80;
    switch (inkEffect) {
        case INK_NONE: break;
        case INK_BLEND: ink.mode = 0; break;
        case INK_ALPHA: {
            if (alpha > 0xFF)
                break;
            int32 level = alpha < 64 ? 0 : alpha < 160 ? 1 : alpha < 224 ? 2 : 3;
            if (!level)
                return false;
            if (level < 3)
                ink.mode = 0, ink.passes = level;
            break;
        }
        case INK_ADD:
        case INK_SUB: {
            if (alpha <= 0)
                return false;
            uint32 a = alpha > 0xFF ? 0xFF : (uint32)alpha;
            r = r * a >> 8, g = g * a >> 8, b = b * a >> 8;
            ink.mode = inkEffect == INK_ADD ? 1 : 2;
            break;
        }
        default: g_ps1PrimTodo = g_ps1PrimTodo + 1; return false;
    }
    bgr = b << 16 | g << 8 | r;
    return true;
}

// One flat rectangle (already clipped)
void PS1FlatRect(int32 x, int32 y, int32 w, int32 h, uint32 bgr, const PS1Ink &ink)
{
    if (w <= 0 || h <= 0)
        return;
    uint32 cmd = 0x60000000 | bgr | (ink.mode >= 0 ? 0x02000000 : 0);
    for (int32 p = 0; p < ink.passes; ++p) {
        uint32_t *wr = PS1ChainSpanBegin(ink.mode >= 0 ? 5 : 4);
        if (!wr)
            return;
        int32 n = 0;
        if (ink.mode >= 0)
            wr[1 + n++] = 0xE1000000 | (uint32)ink.mode << 5; // the semi-transparency mode lives in the texpage state
        PS1FadeRecordColour(&wr[1 + n]);
        wr[1 + n++] = cmd;
        wr[1 + n++] = (uint32)(uint16)y << 16 | (uint16)x;
        wr[1 + n++] = (uint32)h << 16 | (uint32)w;
        PS1ChainSpanEnd(wr, n, wr + 1 + n);
    }
    g_ps1FlatDraws = g_ps1FlatDraws + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(7, (uint8)(ink.mode + 1), (uint8)ink.passes, 0, (uint8)(bgr & 0xFF), (uint8)((bgr >> 8) & 0xFF), (uint16)(bgr >> 16), x, y, w,
                  h);
#endif
}

// The edge buffer's rows [top, bottom) (clamped to the clip bounds as the PC does) as flat rectangles, identical rows merged
void PS1EdgeSpans(int32 top, int32 bottom, uint32 bgr, const PS1Ink &ink)
{
    int32 runY = 0, runH = 0, runS = 0, runE = 0;
    for (int32 s = top; s < bottom; ++s) {
        PS1Edge *edge = &PS1_EDGES[s];
        int32 start = edge->start, end = edge->end;
        start = start < currentScreen->clipBound_X1 ? currentScreen->clipBound_X1 : start;
        start = start > currentScreen->clipBound_X2 ? currentScreen->clipBound_X2 : start;
        end   = end < currentScreen->clipBound_X1 ? currentScreen->clipBound_X1 : end;
        end   = end > currentScreen->clipBound_X2 ? currentScreen->clipBound_X2 : end;
        if (end <= start) {
            PS1FlatRect(runS, runY, runE - runS, runH, bgr, ink);
            runH = 0;
            continue;
        }
        if (runH && start == runS && end == runE && s == runY + runH) {
            ++runH;
            continue;
        }
        PS1FlatRect(runS, runY, runE - runS, runH, bgr, ink);
        runY = s, runH = 1, runS = start, runE = end;
    }
    PS1FlatRect(runS, runY, runE - runS, runH, bgr, ink);
}

// ProcessScanEdge (Scene3D.cpp) on the PS1 edge buffer
void PS1ScanEdge(int32 x1, int32 y1, int32 x2, int32 y2)
{
    int32 ix1 = FROM_FIXED(x1), iy1 = FROM_FIXED(y1), ix2 = FROM_FIXED(x2), iy2 = FROM_FIXED(y2);
    int32 top = FROM_FIXED(y1);
    if (iy1 == iy2)
        return;
    if (iy1 > iy2) {
        top = FROM_FIXED(y2);
        ix1 = FROM_FIXED(x2), ix2 = FROM_FIXED(x1);
        iy1 = FROM_FIXED(y2), iy2 = FROM_FIXED(y1);
    }
    int32 bottom = iy2 + 1;
    if (top < currentScreen->clipBound_Y2 && bottom >= currentScreen->clipBound_Y1) {
        if (bottom > currentScreen->clipBound_Y2)
            bottom = currentScreen->clipBound_Y2;
        int32 scanPos = TO_FIXED(ix1);
        int32 delta   = TO_FIXED(ix2 - ix1) / (iy2 - iy1);
        if (top < 0) {
            scanPos -= top * delta;
            top = 0;
        }
        PS1Edge *edge = &PS1_EDGES[top];
        for (int32 i = top; i < bottom && i < PS1_EDGE_ROWS; ++i) {
            int32 scanX = scanPos >> 16;
            if (scanX < edge->start)
                edge->start = (int16)(scanX < -0x7FFF ? -0x7FFF : scanX > 0x7FFF ? 0x7FFF : scanX);
            if (scanX > edge->end)
                edge->end = (int16)(scanX < -0x7FFF ? -0x7FFF : scanX > 0x7FFF ? 0x7FFF : scanX);
            scanPos += delta;
            ++edge;
        }
    }
}
} // namespace

// DrawRectangle after its clip (the PC's validDraw is already set)
void RSDK::PS1DrawRect(int32 x, int32 y, int32 w, int32 h, uint32 color, int32 alpha, int32 inkEffect)
{
    uint32 bgr;
    PS1Ink ink;
    if (PS1FlatInk(color, inkEffect, alpha, bgr, ink))
        PS1FlatRect(x, y, w, h, bgr, ink);
}

// DrawFace after its ink checks: its rows from its edges (vertices in 16.16 screen coordinates)
void RSDK::PS1DrawFace(Vector2 *vertices, int32 vertCount, int32 r, int32 g, int32 b, int32 alpha, int32 inkEffect)
{
    int32 top = 0x7FFFFFFF, bottom = -0x10000;
    for (int32 v = 0; v < vertCount; ++v) {
        top    = vertices[v].y < top ? vertices[v].y : top;
        bottom = vertices[v].y > bottom ? vertices[v].y : bottom;
    }
    int32 topScreen = FROM_FIXED(top), bottomScreen = FROM_FIXED(bottom);
    topScreen    = topScreen < currentScreen->clipBound_Y1 ? currentScreen->clipBound_Y1 : topScreen;
    topScreen    = topScreen > currentScreen->clipBound_Y2 ? currentScreen->clipBound_Y2 : topScreen;
    bottomScreen = bottomScreen < currentScreen->clipBound_Y1 ? currentScreen->clipBound_Y1 : bottomScreen;
    bottomScreen = bottomScreen > currentScreen->clipBound_Y2 ? currentScreen->clipBound_Y2 : bottomScreen;
    if (topScreen == bottomScreen || bottomScreen >= PS1_EDGE_ROWS)
        return;
    for (int32 s = topScreen; s <= bottomScreen; ++s) PS1_EDGES[s].start = 0x7FFF, PS1_EDGES[s].end = -1;
    for (int32 v = 0; v < vertCount - 1; ++v) PS1ScanEdge(vertices[v].x, vertices[v].y, vertices[v + 1].x, vertices[v + 1].y);
    PS1ScanEdge(vertices[0].x, vertices[0].y, vertices[vertCount - 1].x, vertices[vertCount - 1].y);
    uint32 bgr;
    PS1Ink ink;
    if (PS1FlatInk((uint32)(r & 0xFF) << 16 | (uint32)(g & 0xFF) << 8 | (uint32)(b & 0xFF), inkEffect, alpha, bgr, ink))
        PS1EdgeSpans(topScreen, bottomScreen + 1, bgr, ink); // the PC's rows run to bottomScreen inclusive
}

// DrawCircle after its ink checks (x, y already on the screen): its rows from its edges (the PC's midpoint loop)
void RSDK::PS1DrawCircle(int32 x, int32 y, int32 radius, uint32 color, int32 alpha, int32 inkEffect)
{
    int32 yRadiusBottom = y + radius, bottom = yRadiusBottom + 1, yRadiusTop = y - radius, top = yRadiusTop;
    top    = top < currentScreen->clipBound_Y1 ? currentScreen->clipBound_Y1 : top > currentScreen->clipBound_Y2 ? currentScreen->clipBound_Y2 : top;
    bottom = bottom < currentScreen->clipBound_Y1 ? currentScreen->clipBound_Y1
                                                   : bottom > currentScreen->clipBound_Y2 ? currentScreen->clipBound_Y2 : bottom;
    if (top == bottom || bottom >= PS1_EDGE_ROWS)
        return;
    for (int32 i = top; i < bottom; ++i) PS1_EDGES[i].start = 0x7FFF, PS1_EDGES[i].end = -1;
#define PS1_EDGE_MAX(ROW, V) if ((ROW) >= top && (ROW) <= bottom && (V) > PS1_EDGES[ROW].end) PS1_EDGES[ROW].end = (int16)(V)
#define PS1_EDGE_MIN(ROW, V) if ((ROW) >= top && (ROW) <= bottom && (V) < PS1_EDGES[ROW].start) PS1_EDGES[ROW].start = (int16)(V)
    int32 r = 3 - 2 * radius, xRad = x - radius, curY = y, curX = x, dist = x - y;
    for (int32 i = 0; i <= radius; ++i) {
        int32 scanX = i + curX;
        PS1_EDGE_MAX(yRadiusBottom, scanX);
        PS1_EDGE_MAX(yRadiusTop, scanX);
        int32 scanY = i + y;
        PS1_EDGE_MAX(scanY, yRadiusBottom + dist);
        PS1_EDGE_MAX(curY, yRadiusBottom + dist);
        PS1_EDGE_MIN(yRadiusBottom, x);
        PS1_EDGE_MIN(yRadiusTop, x);
        PS1_EDGE_MIN(scanY, xRad);
        PS1_EDGE_MIN(curY, xRad);
        if (r >= 0) {
            --yRadiusBottom;
            ++yRadiusTop;
            r += 4 * (i - radius) + 10;
            --radius;
            ++xRad;
        }
        else {
            r += 4 * i + 6;
        }
        --curY;
        --x;
    }
#undef PS1_EDGE_MAX
#undef PS1_EDGE_MIN
    uint32 bgr;
    PS1Ink ink;
    if (PS1FlatInk(color, inkEffect, alpha, bgr, ink))
        PS1EdgeSpans(top, bottom, bgr, ink);
}

// DrawLine after its ink checks (screen coordinates): a GPU line (GP0 40h; the GPU's own pixels, approximate)
void RSDK::PS1DrawLine(int32 x1, int32 y1, int32 x2, int32 y2, uint32 color, int32 alpha, int32 inkEffect)
{
    uint32 bgr;
    PS1Ink ink;
    if (x1 < -1024 || x1 > 1023 || x2 < -1024 || x2 > 1023 || y1 < -1024 || y1 > 1023 || y2 < -1024 || y2 > 1023
        || !PS1FlatInk(color, inkEffect, alpha, bgr, ink))
        return;
    for (int32 p = 0; p < ink.passes; ++p) {
        uint32_t *wr = PS1ChainSpanBegin(5);
        if (!wr)
            return;
        int32 n = 0;
        if (ink.mode >= 0)
            wr[1 + n++] = 0xE1000000 | (uint32)ink.mode << 5;
        wr[1 + n++] = 0x40000000 | bgr | (ink.mode >= 0 ? 0x02000000 : 0);
        wr[1 + n++] = (uint32)(uint16)y1 << 16 | (uint16)x1;
        wr[1 + n++] = (uint32)(uint16)y2 << 16 | (uint16)x2;
        PS1ChainSpanEnd(wr, n, wr + 1 + n);
    }
    g_ps1PrimApprox = g_ps1PrimApprox + 1;
}

// DrawBlendedFace after its ink checks: a gouraud fan (GP0 30h triangles; approximate)
void RSDK::PS1DrawBlendedFace(Vector2 *vertices, uint32 *colors, int32 vertCount, int32 alpha, int32 inkEffect)
{
    PS1Ink ink;
    uint32 dummy;
    if (vertCount < 3 || !PS1FlatInk(0, inkEffect, alpha, dummy, ink))
        return;
    uint32 semi = ink.mode >= 0 ? 0x02000000 : 0;
    for (int32 v = 1; v + 1 < vertCount; ++v) {
        const int32 idx[3] = { 0, v, v + 1 };
        for (int32 p = 0; p < ink.passes; ++p) {
            uint32_t *wr = PS1ChainSpanBegin(8);
            if (!wr)
                return;
            int32 n = 0;
            if (ink.mode >= 0)
                wr[1 + n++] = 0xE1000000 | (uint32)ink.mode << 5;
            for (int32 k = 0; k < 3; ++k) {
                uint32 c = colors[idx[k]];
                uint32 bgr = (c & 0xFF) << 16 | (c & 0xFF00) | ((c >> 16) & 0xFF);
                wr[1 + n++] = (k == 0 ? 0x30000000 | semi : 0) | bgr;
                int32 px = FROM_FIXED(vertices[idx[k]].x), py = FROM_FIXED(vertices[idx[k]].y);
                wr[1 + n++] = (uint32)(uint16)py << 16 | (uint16)px;
            }
            PS1ChainSpanEnd(wr, n, wr + 1 + n);
        }
    }
    g_ps1PrimApprox = g_ps1PrimApprox + 1;
}
