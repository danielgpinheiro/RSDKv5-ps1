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
#if PS1_DEBUG_BIGRAM
// The frame's draw calls in order (8 MB link only; tools/gfx/layer_reference.gdb dumps them): type 0 = a tile layer
// (x = its PS1DrawLayerHScroll call number), 1 = an atlas rectangle (binding, size, position, flip, ink), 2 = a tile
// (attr = tile index, direction = flips), 3 = a rotated / scaled atlas rectangle (x, y = corner A) followed by 4
// (x, y = corner B, w, h = corner C) and 5 (x, y = corner D), 6 = a rotated tile (the same three entries, attr = tile)
struct PS1DrawLogEntry {
    uint8 type, direction, ink, alpha;
    uint8 u, v;
    uint16 attr;
    int16 x, y;
    uint16 w, h;
};
PS1DrawLogEntry g_ps1DrawLog[512];
volatile uint32 g_ps1DrawLogCount = 0;
#endif
}

namespace RSDK
{
// PS1LayersFrameBegin: this frame's counters (and the draw log) start over
void PS1SpritesFrameBegin()
{
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

// A frame's binding as a texture page, CLUT and top-left texel. False: not in the atlas (counted)
bool PS1FrameTex(const SpriteFrame *frame, uint32 &tpage, uint32 &clut, bool &half)
{
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
    if (w <= 0 || h <= 0 || !PS1FrameTex(frame, tpage, clut, half) || !PS1InkOf(inkEffect, alpha, ink))
        return;
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
    if (w <= 0 || h <= 0 || scaleX <= 0 || scaleY <= 0 || !PS1FrameTex(frame, tpage, clut, half) || !PS1InkOf(inkEffect, alpha, ink))
        return;
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
    if (!PS1TileTex(tile, u, v, tpage, clut) || !PS1InkOf(inkEffect, alpha, ink))
        return;
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
    if (scaleX <= 0 || scaleY <= 0 || !PS1TileTex(tile, u, v, tpage, clut) || !PS1InkOf(inkEffect, alpha, ink))
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
