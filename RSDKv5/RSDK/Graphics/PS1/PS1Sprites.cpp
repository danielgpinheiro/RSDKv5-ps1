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
volatile uint32 g_ps1InkTodoBy[8]    = {}; // those per ink (5 tint, 6 masked, 7 unmasked; total)
volatile uint32 g_ps1MissingSheet[8] = {}; // the first sheets with frames not bound (sheet + 1 | frames missing << 8)
volatile uint32 g_ps1MissingRect[8]  = {}; // their first missing frame's sheet position (x | y << 16)
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
volatile uint32 g_ps1SpriteWidePieces = 0; // 4.2b: frames drawn as pieces of several texture pages (GDB)
volatile uint32 g_ps1SpriteBigClips   = 0; // 7b.12: frames past the GPU primitive size drawn as their on-screen part (GDB)
volatile uint32 g_ps1SpriteOutside    = 0; // 4.2d: frames wholly outside their sheet (drawn as nothing; GDB)

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
    bool maskTest; // 4.1b: INK_MASKED, drawn with the GPU's mask test on
    bool forceMask; // 4.2b: a flat colour other than the mask colour while one is set: bit 15 forced (masked sprites skip it)
    bool tintInvert; // 4.2d: INK_TINT with a negative table (FXRuby): white, then the previous frame subtracted (approximate)
};

// A palette colour as a CLUT entry (PS1Layers.cpp's PS1Colour: the mask colour keeps STP clear, 4.1b)
inline uint16 PS1ClutColour(uint16 c)
{
    uint16 p = (uint16)(0x8000 | ((c >> 11) & 31) | (((c >> 6) & 31) << 5) | ((c & 31) << 10));
    if (maskColor && c == (uint16)maskColor && (p & 0x7FFF))
        p &= 0x7FFF;
    return p;
}

// DrawSpriteFlipped's ink rules (alpha: > 0xFF opaque, <= 0 nothing; add / sub: clamped to 0xFF), then the GPU's.
// False: nothing to draw (or an ink not done: counted)
bool PS1InkOf(int32 inkEffect, int32 alpha, PS1Ink &ink)
{
    ink.mode = -1, ink.passes = 1, ink.mod = 0x80, ink.maskTest = false, ink.forceMask = false, ink.tintInvert = false;
    switch (inkEffect) {
        case INK_NONE: return true;
        case INK_MASKED: // only where the framebuffer holds the mask colour (bit 15 clear: its CLUT entries' STP is 0)
            if (!maskColor)
                break; // a black mask colour can't be told from transparent: not done (counted below)
            ink.maskTest = true;
            return true;
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
        default: break;
    }
    g_ps1SpriteInkTodo            = g_ps1SpriteInkTodo + 1;
    g_ps1InkTodoBy[inkEffect & 7] = g_ps1InkTodoBy[inkEffect & 7] + 1;
    return false;
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
// (shift 1 / 2: the texels are the w x h picture halved / quartered). False: culled / nothing chained
bool PS1TexRect(int32 x, int32 y, int32 w, int32 h, int32 u, int32 v, uint32 tpage, uint32 clut, int32 direction, int32 shift,
                const PS1Ink &ink)
{
    if (PS1OffScreen(x, y, x + w, y + h))
        return false;
    uint32 semi = 0, colour = ink.mod | ink.mod << 8 | ink.mod << 16;
    if (ink.mode >= 0)
        tpage |= (uint32)ink.mode << 5, semi = 0x02000000;
    bool fx = direction & FLIP_X, fy = direction & FLIP_Y;

    if (!fx && !fy && !shift) {
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
    int32 m = (1 << shift) - 1, tw = (w + m) >> shift, th = (h + m) >> shift;
    int32 uL = fx ? u + tw - 1 : u, uR = fx ? u - 1 : u + tw;
    int32 vT = fy ? v + th - 1 : v, vB = fy ? v - 1 : v + th;
    // an edge one past the page (u / v -1, or 256) can't be addressed: that column / row becomes its own strip
    int32 qw = w, uEdge = -1, qh = h, vEdge = -1;
    if (uR < 0 || uR > 255) {
        if (shift) // halved: the texture edge is inside the last pair of pixels; clamp (the frame is approximate)
            uR = uR < 0 ? 0 : 255;
        else
            qw = w - 1, uEdge = uR < 0 ? 0 : 255, uR = uR < 0 ? 0 : 255;
    }
    if (vB < 0 || vB > 255) {
        if (shift)
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
                    int32 shift, int32 scaleX, int32 scaleY, int32 direction, int32 rotation, const PS1Ink &ink, int32 *cx, int32 *cy)
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
    int32 m = (1 << shift) - 1, tw = (w + m) >> shift, th = (h + m) >> shift;
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
// bank as it is now), or bank 0 when a palette write in this draw phase changed it (9f): the same palette indices from that bank, uploaded in the chain right before the draw (an 8-bit
// frame's 256 entries into the bank's own row 240 + bank; a 4-bit frame's 16 into a spare slot, alternating).
// 9f invert: INK_SUB's colours (each 5-bit channel c as 31 - c: the PC's subtract table takes alpha * (31 - c) >> 8 off;
// the GPU's B - F mode then takes texel * mod / 128 off, mod = alpha / 2); an 8-bit frame's row is put back after its
// draw (PS1SubClutRestore)
uint32 PS1BankClut(uint16 attr, int32 bank, bool invert = false)
{
    static uint16 s_row[256];
    static int32 s_flip = 0;
    const uint16 inv = invert ? 0x7FFF : 0; // STP stays set: an inverted white (0x8000) is drawn, not transparent
    uint32 slot = PS1_ATLAS_CLUT(attr);
    if (slot == PS1_ATLAS_BANK_CLUT) {
        s_row[0] = 0;
        for (int32 i = 1; i < 256; ++i) s_row[i] = (uint16)(PS1ClutColour(fullPalette[bank][i]) ^ inv) | 0x8000 * invert;
        PS1UploadClut(s_row, 256, 0, (int16)(240 + bank));
        if (!bank && !invert)
            PS1PaletteRow0Swapped(); // 9f: restored when the palette is (PS1PaletteWrite) or at the next frame start
        return (uint32)(240 + bank) << 6;
    }
    const PS1AtlasClut *ac = PS1AtlasCluts();
    if (!ac || slot >= PS1AtlasClutCount())
        return 0;
    s_row[0] = 0;
    for (int32 i = 1; i < 16; ++i) {
        uint16 c = ac[slot].map[i] ? fullPalette[bank][ac[slot].map[i]] : 0;
        s_row[i] = ac[slot].map[i] ? (uint16)((PS1ClutColour(c) ^ inv) | 0x8000 * invert) : 0;
    }
    uint32 scratch = PS1_SPRITE_CLUT_SCRATCH + (s_flip ^= 1);
    int16 x = (int16)((scratch % PS1_SPRITE_CLUTS_PER_ROW) * 16), y = (int16)(496 + scratch / PS1_SPRITE_CLUTS_PER_ROW);
    PS1UploadClut(s_row, 16, x, y);
    return (uint32)(x >> 4) | (uint32)y << 6;
}

// 9f: after an INK_SUB draw of an 8-bit frame, its bank's row back to the colours the other draws use
void PS1SubClutRestore(uint16 attr, int32 bank)
{
    if (PS1_ATLAS_CLUT(attr) != PS1_ATLAS_BANK_CLUT)
        return;
    if (bank)
        PS1BankClut(attr, bank);
    else
        PS1PaletteRow0Restore();
}

// The palette bank of screen line y (clamped to the screen)
inline int32 PS1LineBank(int32 y)
{
    int32 sh = currentScreen->size.y;
    return gfxLineBuffer[y < 0 ? 0 : y >= sh ? sh - 1 : y];
}

// A frame's binding as a texture page, CLUT and top-left texel. False: not in the atlas (counted)
#if PS1_COUNT_SHEETS // docs/32 9i (evidence build): frames asked for, per sprite sheet (GDB dumps it around a boss fight)
extern "C" {
volatile uint32 g_ps1SheetDraws[SURFACE_COUNT];
}
#endif
bool PS1FrameTex(const SpriteFrame *frame, uint32 &tpage, uint32 &clut, int32 &shift)
{
#if PS1_COUNT_SHEETS
    g_ps1SheetDraws[frame->sheetID] = g_ps1SheetDraws[frame->sheetID] + 1;
#endif
    uint16 attr = frame->ps1Attr;
    if (attr == PS1_ATLAS_MISSING) {
        if (frame->ps1U == PS1_ATLAS_OUTSIDE && frame->ps1V == PS1_ATLAS_OUTSIDE) { // 4.2d: no texels to draw (the PC
            g_ps1SpriteOutside = g_ps1SpriteOutside + 1;                           // reads past its sheet there)
            return false;
        }
        g_ps1SpriteMissing = g_ps1SpriteMissing + 1;
        for (int32 i = 0; i < 8; ++i) { // which sheets (GDB)
            uint32 e = g_ps1MissingSheet[i];
            if (!e || (e & 0xFF) == (uint32)(frame->sheetID + 1)) {
                if (!e)
                    g_ps1MissingRect[i] = (uint32)(uint16)frame->sprX | (uint32)(uint16)frame->sprY << 16;
                g_ps1MissingSheet[i] = (uint32)(frame->sheetID + 1) | ((e >> 8) + 1) << 8;
                break;
            }
        }
        return false;
    }
    tpage       = PS1_ATLAS_COLUMN(attr) | PS1_ATLAS_ROW(attr) << 4 | PS1_ATLAS_8BIT(attr) << 7;
    uint32 slot = PS1_ATLAS_CLUT(attr);
    PS1FadeRestoreSlot(slot); // a FillScreen / the pause tint earlier this frame changed the CLUTs: this one back (9b.3)
    clut = slot == PS1_ATLAS_BANK_CLUT ? (uint32)(240 << 6) : (slot % PS1_SPRITE_CLUTS_PER_ROW) | (496 + slot / PS1_SPRITE_CLUTS_PER_ROW) << 6;
    shift = PS1_ATLAS_SHIFT(attr);
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
// 9b.6: the next draw of one frame starts `du` texels into its stored strip (PS1SetFrameShift; the strip is the frame
// widened by build_atlas SHIFT_STRIPS, so the shifted reads stay inside stored texels)
static const void *s_shiftFrame = NULL;
static int32 s_shiftU           = 0;
extern "C" void PS1SetFrameShift(const void *frame, int du)
{
    s_shiftFrame = frame;
    s_shiftU     = du;
}

void RSDK::PS1DrawFrame(const SpriteFrame *frame, int32 x, int32 y, int32 direction, int32 inkEffect, int32 alpha)
{
    int32 w = frame->width, h = frame->height;
    uint32 tpage, clut;
    int32 shift, u = frame->ps1U, v = frame->ps1V;
    PS1Ink ink;
    if (w <= 0 || h <= 0 || !PS1PcDraws(inkEffect, alpha) || !PS1PcClip(x, y, w, h))
        return;
    if (!PS1FrameTex(frame, tpage, clut, shift) || !PS1InkOf(inkEffect, alpha, ink))
        return;
    if (frame == s_shiftFrame) {
        u += s_shiftU >> shift;
        s_shiftFrame = NULL;
    }
    int32 m = (1 << shift) - 1, tw = (w + m) >> shift;
    if ((w > 1023 || h > 511) && u + tw <= 256) {
        // 7b.12: past the GPU's primitive size (TMZ's 1024 x 1024 Monarch backdrop): only the screen's part, its edges on
        // whole texels (frame columns [a, b) / rows [c, d); a flipped axis counts from the frame's far edge)
        int32 sw = currentScreen->size.x, sh = currentScreen->size.y;
        int32 a = x < 0 ? -x : 0, b = x + w > sw ? sw - x : w, c = y < 0 ? -y : 0, d = y + h > sh ? sh - y : h;
        if (a >= b || c >= d)
            return;
        if (direction & FLIP_X) {
            int32 t = w - b;
            b = w - a, a = t;
        }
        if (direction & FLIP_Y) {
            int32 t = h - d;
            d = h - c, c = t;
        }
        a &= ~m, c &= ~m, b = (b + m) & ~m, d = (d + m) & ~m;
        b = b > w ? w : b, d = d > h ? h : d;
        x += (direction & FLIP_X) ? w - b : a;
        y += (direction & FLIP_Y) ? h - d : c;
        u += a >> shift, v += c >> shift;
        w = b - a, h = d - c, tw = (w + m) >> shift;
        g_ps1SpriteBigClips = g_ps1SpriteBigClips + 1;
    }
    // (9f: bank 0 too while a palette write in this draw phase changed it: CutsceneHBH's per-Heavy colours; INK_SUB
    // with its colours inverted)
    const bool sub = inkEffect == INK_SUB;
    const int32 bank = PS1LineBank(y);
    if (bank || g_ps1PalDiffs || sub) {
        clut = PS1BankClut(frame->ps1Attr, bank, sub);
        if (!clut)
            return;
    }
    if (ink.maskTest) // 4.1b: INK_MASKED: only over bit-15-clear pixels (the mask colour's)
        PS1ChainMaskControl(false, true);
    bool drawn;
    if (u + tw <= 256) {
        drawn = PS1TexRect(x, y, w, h, u, v, tpage, clut, direction, shift, ink);
    }
    else {
        // 4.2b: wider than a texture page (the menu diorama's 1024-pixel terrain): the atlas stored it from u 0 over
        // adjacent pages (tools/gfx/build_atlas.py pack): one piece of 256 texels per page, the next page 1 column
        // (4-bit) or 2 (8-bit) to the right
        int32 d = 1 << shift, step = (tpage & 0x80) ? 2 : 1;
        drawn = false;
        for (int32 k = 0, t0 = 0; t0 < tw; ++k, t0 += 256) {
            int32 sx0 = t0 * d, sw = (tw - t0 < 256 ? tw - t0 : 256) * d;
            if (sx0 + sw > w)
                sw = w - sx0;
            int32 sx = (direction & FLIP_X) ? x + w - sx0 - sw : x + sx0;
            drawn |= PS1TexRect(sx, y, sw, h, 0, v, tpage + (uint32)(k * step), clut, direction, shift, ink);
        }
        g_ps1SpriteWidePieces = g_ps1SpriteWidePieces + 1;
    }
    if (ink.maskTest)
        PS1ChainMaskControl(false, false);
    if (sub)
        PS1SubClutRestore(frame->ps1Attr, bank);
    if (!drawn)
        return;
    g_ps1SpriteDraws = g_ps1SpriteDraws + 1;
#if PS1_DEBUG_BIGRAM
    { // 7b.7: the top line's palette bank (bits 4-6: the bank the frame took) and bit 7 when its lines span banks
        int32 bt = PS1LineBank(y), bb = PS1LineBank(y + h - 1);
        PS1DrawLogAdd(1, (uint8)(direction | bt << 4 | (bt != bb) << 7), (uint8)inkEffect, PS1LogAlpha(alpha), (uint8)u, (uint8)v,
                      frame->ps1Attr, x, y, w, h);
    }
#endif
}

// DrawSprite's rotated / scaled frames: DrawSpriteRotozoom(x, y, pivotX, pivotY, ... scaleX, scaleY, direction, rotation)
void RSDK::PS1DrawFrameRotozoom(const SpriteFrame *frame, int32 x, int32 y, int32 scaleX, int32 scaleY, int32 direction, int32 rotation,
                                int32 inkEffect, int32 alpha)
{
    int32 w = frame->width, h = frame->height;
    uint32 tpage, clut;
    int32 shift;
    PS1Ink ink;
    int32 cx[4], cy[4];
    if (w <= 0 || h <= 0 || scaleX <= 0 || scaleY <= 0 || !PS1PcDraws(inkEffect, alpha)
        || !PS1PcClipRotozoom(x, y, frame->pivotX, frame->pivotY, w, h, scaleX, scaleY, direction, rotation))
        return;
    if (!PS1FrameTex(frame, tpage, clut, shift) || !PS1InkOf(inkEffect, alpha, ink))
        return;
    // (9f: bank 0 too while a palette write in this draw phase changed it; INK_SUB with its colours inverted)
    const bool sub   = inkEffect == INK_SUB;
    const int32 bank = PS1LineBank(y);
    if (bank || g_ps1PalDiffs || sub) {
        clut = PS1BankClut(frame->ps1Attr, bank, sub);
        if (!clut)
            return;
    }
    if (ink.maskTest)
        PS1ChainMaskControl(false, true);
    bool drawn = PS1TexRotozoom(x, y, frame->pivotX, frame->pivotY, w, h, frame->ps1U, frame->ps1V, tpage, clut, shift, scaleX, scaleY,
                                direction, rotation, ink, cx, cy);
    if (ink.maskTest)
        PS1ChainMaskControl(false, false);
    if (sub)
        PS1SubClutRestore(frame->ps1Attr, bank);
    if (!drawn)
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
    if (ink.maskTest) { // masked tiles: not done (counted)
        g_ps1SpriteInkTodo = g_ps1SpriteInkTodo + 1;
        return;
    }
    if (PS1LineBank(y))
        g_ps1BankTodo = g_ps1BankTodo + 1;
    int32 direction = (tile >> 10) & 3;
    if (!PS1TexRect(x, y, TILE_SIZE, TILE_SIZE, u, v, tpage, clut, direction, 0, ink))
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
    if (ink.maskTest) { // masked tiles: not done (counted)
        g_ps1SpriteInkTodo = g_ps1SpriteInkTodo + 1;
        return;
    }
    if (!PS1TexRotozoom(x, y, pivotX, pivotY, TILE_SIZE, TILE_SIZE, u, v, tpage, clut, 0, scaleX, scaleY, direction, rotation, ink, cx,
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
    ink.mode = -1, ink.passes = 1, ink.mod = 0x80, ink.maskTest = false, ink.tintInvert = false;
    // 4.2b: as lines (4.1b): with a mask colour set, a flat pixel keeps bit 15 clear only if the PC would store the mask
    // colour there (RGB565 as rgb32To16), so INK_MASKED sprites draw over exactly the PC's pixels
    ink.forceMask = maskColor && (uint16)(rgb32To16_R[r] | rgb32To16_G[g] | rgb32To16_B[b]) != (uint16)maskColor;
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
            if (inkEffect == INK_SUB) {
                // 9f: the PC's subtract table takes a * (31 - c) >> 8 off each 5-bit channel (subtractLookupTable,
                // Drawing.cpp: the colour's inverse, scaled), not the colour itself: GHZ's water pools (0xA0A0E0 at full
                // alpha) take 10 / 10 / 2 off, not 19 / 19 / 27. The GPU's B - F subtracts the flat colour's top 5 bits
                r = (a * (31 - (r >> 3)) >> 8) << 3, g = (a * (31 - (g >> 3)) >> 8) << 3, b = (a * (31 - (b >> 3)) >> 8) << 3;
                ink.mode = 2;
                break;
            }
            r = r * a >> 8, g = g * a >> 8, b = b * a >> 8;
            ink.mode = 1;
            break;
        }
        case INK_TINT:
            // 4.2d: the PC maps every pixel through a 64K table the PS1 has no room (nor per-pixel read-back) for. The
            // negative (FXRuby: the table's entry for black is white) is white minus the frame: the previous one,
            // read from the buffer on display (PS1TintInvertRect); other tints are not done yet (counted)
            if (tintLookupTable && tintLookupTable[0] == 0xFFFF) {
                ink.tintInvert = true;
                r = g = b = 0xFF;
                break;
            }
            g_ps1PrimTodo = g_ps1PrimTodo + 1;
            return false;
        case INK_MASKED: // 4.2b: only over the mask colour's pixels (bit 15 clear), as masked sprites (4.1b)
            if (!maskColor) {
                g_ps1PrimTodo = g_ps1PrimTodo + 1;
                return false;
            }
            ink.maskTest = true;
            break;
        default: g_ps1PrimTodo = g_ps1PrimTodo + 1; return false;
    }
    bgr = b << 16 | g << 8 | r;
    return true;
}

// 4.2d: INK_TINT's negative over a rectangle (clipped to the screen), from the frame as drawn so far: per row, the row is
// copied to a scratch row (VRAM row 510, x 0-319: sprite CLUT slots no atlas uses), the texture cache flushed, the row
// filled white, then the copy (a 15-bit texture, one page per 256 texels) subtracted: 31 - old per channel. Texels
// without STP (bit 15) are drawn as they are (approximate; the draw log marks the rectangle)
#define PS1_TINT_SCRATCH_Y 510
volatile uint32 g_ps1TintInvertRects = 0;
void PS1TintInvertRect(int32 x, int32 y, int32 w, int32 h)
{
    int32 x1 = x + w, y1 = y + h;
    int32 sw = currentScreen->size.x, sh = currentScreen->size.y;
    x  = x < 0 ? 0 : x, y = y < 0 ? 0 : y;
    x1 = x1 > sw ? sw : x1, y1 = y1 > sh ? sh : y1;
    if (x1 <= x || y1 <= y)
        return;
    w = x1 - x;
    uint32 by = (uint32)PS1ChainBufferY(); // the buffer this chain draws into (0 / 256)
    // 4.2e: the rows go into spans of up to 16 rows (a span per row cost ~70 hblanks a frame in the ruby's frames): per
    // row one 8-word fragment (copy, cache flush, white fill) and one 5-word fragment per texture page
    int32 pages = ((x1 - 1) >> 8) - (x >> 8) + 1;
    int32 perRow = 9 + pages * 6;
    for (int32 row0 = y; row0 < y1; row0 += 16) {
        int32 rows   = y1 - row0 < 16 ? y1 - row0 : 16;
        uint32_t *wr = PS1ChainSpanBegin(rows * perRow);
        if (!wr)
            return;
        uint32_t *hdr = NULL, *p = wr;
        int32 lastWords = 0;
#define PS1_TINT_FRAG(N)                                                                                               \
    do {                                                                                                               \
        if (hdr)                                                                                                       \
            *hdr = (uint32_t)lastWords << 24 | ((uintptr_t)p & 0xFFFFFF);                                              \
        hdr = p++, lastWords = (N);                                                                                    \
    } while (0)
        for (int32 row = row0; row < row0 + rows; ++row) {
            PS1_TINT_FRAG(8);
            *p++ = 0x80000000; // VRAM to VRAM: the row as drawn so far -> the scratch row
            *p++ = (by + (uint32)row) << 16 | (uint32)x;
            *p++ = (uint32)PS1_TINT_SCRATCH_Y << 16 | (uint32)x;
            *p++ = 1u << 16 | (uint32)w;
            *p++ = 0x01000000; // clear the texture cache (it may hold the scratch row's last copy)
            *p++ = 0x60FFFFFF; // white (the PC's 31 per channel)
            *p++ = (uint32)(uint16)row << 16 | (uint16)x;
            *p++ = 1u << 16 | (uint32)w;
            for (int32 px = x; px < x1;) {
                int32 page = px >> 8, pe = (page + 1) << 8, pw = (x1 < pe ? x1 : pe) - px;
                PS1_TINT_FRAG(5);
                // texpage: X base page * 256 halfwords, Y base 256 (the scratch row), semi mode 2 (B - F), 15-bit direct
                *p++ = 0xE1000000 | (uint32)(page * 4) | 1u << 4 | 2u << 5 | 2u << 7;
                *p++ = 0x67000000; // textured rectangle, raw texels, semi-transparent
                *p++ = (uint32)(uint16)row << 16 | (uint16)px;
                *p++ = (uint32)(PS1_TINT_SCRATCH_Y - 256) << 8 | (uint32)(px & 0xFF);
                *p++ = 1u << 16 | (uint32)pw;
                px += pw;
            }
        }
#undef PS1_TINT_FRAG
        PS1ChainSpanEnd(hdr, lastWords, p);
    }
    g_ps1TintInvertRects = g_ps1TintInvertRects + 1;
}

// One flat rectangle (already clipped)
void PS1FlatRect(int32 x, int32 y, int32 w, int32 h, uint32 bgr, const PS1Ink &ink)
{
    if (w <= 0 || h <= 0)
        return;
    if (ink.tintInvert) {
        PS1TintInvertRect(x, y, w, h);
#if PS1_DEBUG_BIGRAM
        PS1DrawLogAdd(7, 0, 1, 2, 0xFF, 0xFF, 0xFF, x, y, w, h); // flags 2: a tint (the host counts it approximate)
#endif
        return;
    }
    uint32 cmd = 0x60000000 | bgr | (ink.mode >= 0 ? 0x02000000 : 0);
    if (ink.forceMask || ink.maskTest)
        PS1ChainMaskControl(ink.forceMask, ink.maskTest);
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
    if (ink.forceMask || ink.maskTest)
        PS1ChainMaskControl(false, false);
    g_ps1FlatDraws = g_ps1FlatDraws + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(7, (uint8)(ink.mode + 1), (uint8)ink.passes, (uint8)ink.maskTest, (uint8)(bgr & 0xFF), (uint8)((bgr >> 8) & 0xFF), (uint16)(bgr >> 16), x, y, w,
                  h);
#endif
}

// Flat rectangles of one colour and ink, batched: one span, four to a fragment (after the texpage word when blended)
struct PS1FlatBatch {
    int32 n;
    int16 r[16][4]; // x, y, w, h
};

__attribute__((optimize("O2"))) void PS1FlatBatchFlush(PS1FlatBatch &b, uint32 bgr, const PS1Ink &ink)
{
    if (!b.n)
        return;
    if (ink.tintInvert) { // 4.2d: each rectangle as PS1TintInvertRect
        for (int32 k = 0; k < b.n; ++k) {
            PS1TintInvertRect(b.r[k][0], b.r[k][1], b.r[k][2], b.r[k][3]);
#if PS1_DEBUG_BIGRAM
            PS1DrawLogAdd(7, 0, 1, 2, 0xFF, 0xFF, 0xFF, b.r[k][0], b.r[k][1], b.r[k][2], b.r[k][3]);
#endif
        }
        b.n = 0;
        return;
    }
    bool semi  = ink.mode >= 0;
    uint32 cmd = 0x60000000 | bgr | (semi ? 0x02000000 : 0);
    int32 frags = (b.n + 3) >> 2;
    if (ink.forceMask || ink.maskTest)
        PS1ChainMaskControl(ink.forceMask, ink.maskTest);
    for (int32 p = 0; p < ink.passes; ++p) {
        uint32_t *w = PS1ChainSpanBegin(frags * 14);
        if (!w)
            break;
        uint32_t *first = w;
        uint32_t *hdr = NULL;
        int32 fw = 0, k = 0;
        while (k < b.n) {
            if (hdr)
                *hdr = (uint32_t)fw << 24 | ((uintptr_t)w & 0xFFFFFF);
            hdr = w++;
            fw  = 0;
            if (semi)
                *w++ = 0xE1000000 | (uint32)ink.mode << 5, ++fw;
            for (int32 j = 0; j < 4 && k < b.n; ++j, ++k) {
                w[0] = cmd;
                w[1] = (uint32)(uint16)b.r[k][1] << 16 | (uint16)b.r[k][0];
                w[2] = (uint32)(uint16)b.r[k][3] << 16 | (uint16)b.r[k][2];
                w += 3, fw += 3;
#if PS1_DEBUG_BIGRAM
                if (!p)
                    PS1DrawLogAdd(7, (uint8)(ink.mode + 1), (uint8)ink.passes, (uint8)ink.maskTest, (uint8)(bgr & 0xFF), (uint8)((bgr >> 8) & 0xFF),
                                  (uint16)(bgr >> 16), b.r[k][0], b.r[k][1], b.r[k][2], b.r[k][3]);
#endif
            }
        }
        PS1ChainSpanEnd(hdr, fw, w);
        PS1FadeRecordSpan(first, b.n, semi); // 3.6b: one record for the span's colours
    }
    if (ink.forceMask || ink.maskTest)
        PS1ChainMaskControl(false, false);
    g_ps1FlatDraws = g_ps1FlatDraws + b.n;
    b.n            = 0;
}

__attribute__((always_inline)) inline void PS1FlatBatchAdd(PS1FlatBatch &b, int32 x, int32 y, int32 w, int32 h, uint32 bgr, const PS1Ink &ink)
{
    if (w <= 0 || h <= 0)
        return;
    b.r[b.n][0] = (int16)x, b.r[b.n][1] = (int16)y, b.r[b.n][2] = (int16)w, b.r[b.n][3] = (int16)h;
    if (++b.n == 16)
        PS1FlatBatchFlush(b, bgr, ink);
}

// 4.2e: flat rectangles of one colour and ink written straight into the chain (no batch copy): spans of up to
// PS1_FLAT_SPAN_RECTS, four to a fragment as the batch's (after the texpage word when blended), the mask state set once
// around the whole draw (PS1RectOut below). Inks with several passes and the tint go through the batch
#define PS1_FLAT_SPAN_RECTS 32

__attribute__((noinline)) void PS1FlatSlowAdd(PS1FlatBatch *b, uint32 bgr, const PS1Ink *ink, int32 x, int32 y, int32 w, int32 h)
{
    PS1FlatBatchAdd(*b, x, y, w, h, bgr, *ink);
}

__attribute__((noinline)) void PS1FlatSpanClose(uint32_t *first, uint32_t *hdr, int32 n, bool semi, uint32_t *end)
{
    PS1ChainSpanEnd(hdr, 3 * (((n - 1) & 3) + 1) + (semi ? 1 : 0), end);
    PS1FadeRecordSpan(first, n, semi); // 3.6b: one record for the span's colours
    g_ps1FlatDraws = g_ps1FlatDraws + n;
}

// 4.2e: the writer (the ring's, the circle's and the edge spans' loops): its state is the caller's locals (every part
// inline; only a span's opening and closing are calls). PS1MirrorPut draws a run of rows above and below a centre row
// (rows y - d1..y - d0 and y + d0..y + d1, the centre row once), clipped to the rows [r0, r1). Inks the batch handles go
// there one rectangle at a time
struct PS1RectOut {
    uint32_t *p, *hdr, *first;
    int32 k, n, room; // rectangles in this fragment, in this span; room left in it (-1: the arena is full)
    uint32 cmd, tpage;
    int32 semi;
    bool direct, mask;
    int32 y, r0, r1;
    PS1FlatBatch *batch;
    uint32 bgr;
    const PS1Ink *ink;
};

// the last span closed (n rectangles), a new one opened (its texpage word written when blended); NULL: no room
__attribute__((noinline)) uint32_t *PS1RectSpanOpen(uint32_t *first, uint32_t *hdr, int32 n, int32 semi, uint32_t *p, uint32 tpage)
{
    if (n)
        PS1FlatSpanClose(first, hdr, n, semi, p);
    uint32_t *q = PS1ChainSpanBegin(PS1_FLAT_SPAN_RECTS / 4 * 14);
    if (q && semi)
        q[1] = tpage;
    return q;
}

__attribute__((always_inline)) inline void PS1RectBegin(PS1RectOut &o, PS1FlatBatch &batch, uint32 bgr, const PS1Ink &ink, int32 y, int32 r0, int32 r1)
{
    o.batch = &batch, o.bgr = bgr, o.ink = &ink, batch.n = 0;
    o.direct = ink.passes == 1 && !ink.tintInvert;
    o.semi   = ink.mode >= 0 ? 1 : 0;
    o.cmd    = 0x60000000 | bgr | (o.semi ? 0x02000000 : 0);
    o.tpage  = 0xE1000000 | (uint32)ink.mode << 5;
    o.mask   = o.direct && (ink.forceMask || ink.maskTest);
    o.p = o.hdr = o.first = NULL, o.k = o.n = o.room = 0;
    o.y = y, o.r0 = r0, o.r1 = r1;
    if (o.mask)
        PS1ChainMaskControl(ink.forceMask, ink.maskTest);
}

__attribute__((always_inline)) inline void PS1RectPut(PS1RectOut &o, int32 x, int32 y, int32 w, int32 h)
{
    if (w <= 0 || h <= 0)
        return;
    if (!o.direct) {
        PS1FlatSlowAdd(o.batch, o.bgr, o.ink, x, y, w, h);
        return;
    }
    if (o.room <= 0) { // a new span
        if (o.room < 0)
            return;
        uint32_t *q = PS1RectSpanOpen(o.first, o.hdr, o.n, o.semi, o.p, o.tpage);
        if (!q) {
            o.room = -1, o.n = 0;
            return;
        }
        o.first = o.hdr = q, o.p = q + 1 + o.semi, o.k = o.n = 0, o.room = PS1_FLAT_SPAN_RECTS;
    }
    else if (o.k == 4) { // a new fragment
        *o.hdr = (uint32_t)(12 + o.semi) << 24 | ((uintptr_t)o.p & 0xFFFFFF);
        o.hdr = o.p++;
        if (o.semi)
            *o.p++ = o.tpage;
        o.k = 0;
    }
    uint32_t *p = o.p;
    p[0] = o.cmd;
    p[1] = (uint32)(uint16)y << 16 | (uint16)x;
    p[2] = (uint32)(uint16)h << 16 | (uint16)w;
    o.p = p + 3, ++o.k, ++o.n, --o.room;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(7, (uint8)(o.ink->mode + 1), 1, (uint8)o.ink->maskTest, (uint8)(o.bgr & 0xFF), (uint8)((o.bgr >> 8) & 0xFF), (uint16)(o.bgr >> 16), x,
                  y, w, h);
#endif
}

// a run [s, e) over d0..d1
__attribute__((always_inline)) inline void PS1MirrorPut(PS1RectOut &o, int32 s, int32 e, int32 d0, int32 d1)
{
    int32 ua = o.y - d1, ub = o.y - d0 + 1, da = o.y + d0, db = o.y + d1 + 1;
    if (!d0) // one rectangle across the centre
        ub = db, da = db;
    ua = ua < o.r0 ? o.r0 : ua;
    ub = ub > o.r1 ? o.r1 : ub;
    da = da < o.r0 ? o.r0 : da;
    db = db > o.r1 ? o.r1 : db;
    PS1RectPut(o, s, ua, e - s, ub - ua);
    PS1RectPut(o, s, da, e - s, db - da);
}

__attribute__((always_inline)) inline void PS1RectEnd(PS1RectOut &o)
{
    if (!o.direct) {
        PS1FlatBatchFlush(*o.batch, o.bgr, *o.ink);
        return;
    }
    if (o.n)
        PS1FlatSpanClose(o.first, o.hdr, o.n, o.semi, o.p);
    if (o.mask)
        PS1ChainMaskControl(false, false);
}

// The edge buffer's rows [top, bottom) (clamped to the clip bounds as the PC does) as flat rectangles, identical rows merged
// (4.2e: through the register writer, one emission site)
__attribute__((optimize("O2"))) void PS1EdgeSpans(int32 top, int32 bottom, uint32 bgr, const PS1Ink &ink)
{
    PS1FlatBatch batch;
    PS1RectOut o;
    PS1RectBegin(o, batch, bgr, ink, 0, 0, 0);
    int32 runY = 0, runH = 0, runS = 0, runE = 0;
    const int32 cx1 = currentScreen->clipBound_X1, cx2 = currentScreen->clipBound_X2; // 3.6b: once, not per row
    for (int32 s = top; s <= bottom; ++s) { // s = bottom: no row (ends the last run)
        int32 start = 0, end = 0;
        if (s < bottom) {
            PS1Edge *edge = &PS1_EDGES[s];
            start = edge->start, end = edge->end;
            start = start < cx1 ? cx1 : start;
            start = start > cx2 ? cx2 : start;
            end   = end < cx1 ? cx1 : end;
            end   = end > cx2 ? cx2 : end;
        }
        if (end > start && runH && start == runS && end == runE) {
            ++runH;
            continue;
        }
        if (runH)
            PS1RectPut(o, runS, runY, runE - runS, runH);
        runY = s, runH = end > start ? 1 : 0, runS = start, runE = end;
    }
    PS1RectEnd(o);
}

// ProcessScanEdge (Scene3D.cpp) on the PS1 edge buffer
__attribute__((optimize("O2"))) void PS1ScanEdge(int32 x1, int32 y1, int32 x2, int32 y2)
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
        if (bottom > PS1_EDGE_ROWS)
            bottom = PS1_EDGE_ROWS;
        // 3.6b: scanX runs from ix1 to ix2 (within a pixel per row of rounding): inside int16 there is nothing to clamp
        if (ix1 > -0x7000 && ix1 < 0x7000 && ix2 > -0x7000 && ix2 < 0x7000) {
            for (int32 i = top; i < bottom; ++i, ++edge) {
                int32 scanX = scanPos >> 16;
                if (scanX < edge->start)
                    edge->start = (int16)scanX;
                if (scanX > edge->end)
                    edge->end = (int16)scanX;
                scanPos += delta;
            }
            return;
        }
        for (int32 i = top; i < bottom; ++i) {
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

// 4.2e: FillScreen at full alpha puts one colour on every pixel drawn so far (what the frame draws after it is not
// touched): an opaque flat rectangle over the screen instead of fading every CLUT and colour word (the menu's background
// does this every frame: ~70 hblanks). Bit 15 as any flat's (the PC's RGB565: G in bits 6-10 after the fill)
void RSDK::PS1FillOpaque(uint32 r5, uint32 g5, uint32 b5)
{
    PS1Ink ink;
    ink.mode = -1, ink.passes = 1, ink.mod = 0x80, ink.maskTest = false, ink.tintInvert = false;
    ink.forceMask = maskColor && (uint16)(r5 << 11 | g5 << 6 | b5) != (uint16)maskColor;
    PS1FlatRect(0, 0, currentScreen->size.x, currentScreen->size.y, b5 << 19 | g5 << 11 | r5 << 3, ink);
}

// DrawRectangle after its clip (the PC's validDraw is already set)
void RSDK::PS1DrawRect(int32 x, int32 y, int32 w, int32 h, uint32 color, int32 alpha, int32 inkEffect)
{
    // 4.3: the pause's full-screen grey tint (the PC maps every pixel through its table; alpha unused)
    if (inkEffect == INK_TINT && tintLookupTable && tintLookupTable[0] == PS1_TINT_GREY_MARK && x <= 0 && y <= 0
        && x + w >= currentScreen->size.x && y + h >= currentScreen->size.y) {
        PS1TintScreenGrey();
        return;
    }
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

// DrawCircle after its ink checks (x, y already on the screen): its rows from its edges (the PC's midpoint loop). 4.2e:
// the loop writes rows y + k and y - k alike (y +- the shrinking radius, y +- i), so one row per |k| is kept (no clip
// checks: the rows outside the clip are just not drawn), and each run of identical rows is drawn above and below the
// centre (PS1MirrorPut)
__attribute__((optimize("O2"))) void RSDK::PS1DrawCircle(int32 x, int32 y, int32 radius, uint32 color, int32 alpha, int32 inkEffect)
{
    int32 bottom = y + radius + 1, top = y - radius;
    top    = top < currentScreen->clipBound_Y1 ? currentScreen->clipBound_Y1 : top > currentScreen->clipBound_Y2 ? currentScreen->clipBound_Y2 : top;
    bottom = bottom < currentScreen->clipBound_Y1 ? currentScreen->clipBound_Y1
                                                   : bottom > currentScreen->clipBound_Y2 ? currentScreen->clipBound_Y2 : bottom;
    if (top == bottom || bottom >= PS1_EDGE_ROWS)
        return;
    uint32 bgr;
    PS1Ink ink;
    if (!PS1FlatInk(color, inkEffect, alpha, bgr, ink))
        return;
    // the rows [top, bottom) as |k| = |row - y| in [lo, hi] (hi - lo < PS1_EDGE_ROWS); h[k]: rows y - k and y + k
    int32 lo = top > y ? top - y : bottom - 1 < y ? y - (bottom - 1) : 0;
    int32 hi = y - top > bottom - 1 - y ? y - top : bottom - 1 - y;
    uint32 span = (uint32)(hi - lo);
    PS1Edge *h  = PS1_EDGES - lo;
    for (int32 k = lo; k <= hi; ++k) h[k].start = 0x7FFF, h[k].end = -1;
    int32 r = 3 - 2 * radius, rc = radius, xRad = x - radius;
    for (int32 i = 0; i <= radius; ++i) {
        // the PC: rows y +- rc take max(i + x) and min(x - i), rows y +- i max(x + rc) and min(xRad) (rows outside
        // [lo, hi] are not drawn)
        if ((uint32)(rc - lo) <= span) {
            PS1Edge *a = &h[rc];
            int32 v    = x + i;
            if (v > a->end)
                a->end = (int16)v;
            v = x - i;
            if (v < a->start)
                a->start = (int16)v;
        }
        if ((uint32)(i - lo) <= span) {
            PS1Edge *c = &h[i];
            int32 v    = x + rc;
            if (v > c->end)
                c->end = (int16)v;
            if (xRad < c->start)
                c->start = (int16)xRad;
        }
        if (r >= 0) {
            r += 4 * (i - rc) + 10;
            --rc;
            ++xRad;
        }
        else {
            r += 4 * i + 6;
        }
    }
    // the runs over k
    const int32 cx1 = currentScreen->clipBound_X1, cx2 = currentScreen->clipBound_X2;
    PS1FlatBatch batch;
    PS1RectOut o;
    PS1RectBegin(o, batch, bgr, ink, y, top, bottom);
    int32 runS = 0, runE = 0, runD = -1;
    for (int32 k = lo; k <= hi + 1; ++k) { // k = hi + 1: no row (ends the last run)
        int32 start = 0, end = 0;
        if (k <= hi) {
            start = h[k].start, end = h[k].end;
            start = start < cx1 ? cx1 : start;
            start = start > cx2 ? cx2 : start;
            end   = end < cx1 ? cx1 : end;
            end   = end > cx2 ? cx2 : end;
        }
        bool on = start < end;
        if (on && runD >= 0 && start == runS && end == runE)
            continue;
        if (runD >= 0) // one call site: the writer is inline
            PS1MirrorPut(o, runS, runE, runD, k - 1);
        runD = on ? k : -1, runS = start, runE = end;
    }
    PS1RectEnd(o);
}

// DrawCircleOutline after its clip (x, y on the screen; [left, right) x [top, bottom) as the PC computed them: its
// top / bottom clamps make them the clip bounds' rows): the PC's pixels are the x with inner^2 <= dx^2 + dy^2 < outer^2,
// per row one span (the row misses the inner circle) or two. 4.2e: rows y - d and y + d have the same spans: d walks
// once from the nearest visible row's to the farthest (integer square roots kept from step to step, no multiplies), the
// left and right spans kept as runs over d, each run drawn above and below the centre (host check: every pixel once,
// the PC's set)
namespace
{
// the largest b with b * b <= t, from the last step's b and its square bb: t only shrinks as d grows, and b starts at the
// radius (b * b > t), so b only walks down
__attribute__((always_inline)) inline int32 PS1ISqrtDown(int32 &b, int32 &bb, int32 t)
{
    while (bb > t)
        bb -= 2 * b - 1, --b;
    return b;
}

} // namespace

__attribute__((optimize("O2"))) void RSDK::PS1DrawCircleOutline(int32 x, int32 y, int32 innerRadius, int32 outerRadius, int32 left,
                                                                 int32 top, int32 right, int32 bottom, uint32 color, int32 alpha,
                                                                 int32 inkEffect)
{
    uint32 bgr;
    PS1Ink ink;
    if (!PS1FlatInk(color, inkEffect, alpha, bgr, ink))
        return;
    int32 rowEnd = bottom < PS1_EDGE_ROWS ? bottom : PS1_EDGE_ROWS;
    int32 r0 = y - (outerRadius - 1), r1 = y + outerRadius; // the rows with pixels, clipped
    r0 = r0 < top ? top : r0;
    r1 = r1 > rowEnd ? rowEnd : r1;
    if (r0 >= r1)
        return;
    // their |dy|: [lo, hi]
    int32 lo = r0 > y ? r0 - y : r1 - 1 < y ? y - (r1 - 1) : 0;
    int32 hi = y - r0 > r1 - 1 - y ? y - r0 : r1 - 1 - y;
    PS1FlatBatch batch;
    PS1RectOut o;
    PS1RectBegin(o, batch, bgr, ink, y, r0, r1);
    int32 ir2 = innerRadius * innerRadius, or2 = outerRadius * outerRadius;
    // 4.2e: three short loops (their values stay in registers): per d the spans' |dx| bounds into the edge buffer
    // (start = a, the inner bound (0: one span), end = b, the outer one (-1: no pixels)), then the left spans' runs, then the
    // right ones'
    PS1Edge *tab = PS1_EDGES - lo;
    {
        int32 d2 = lo * lo, bOut = outerRadius, bOut2 = or2, aIn = innerRadius < 0 ? -innerRadius : innerRadius, aIn2 = ir2;
        for (int32 d = lo; d <= hi; d2 += 2 * d + 1, ++d) {
            int32 a = 0, b = -1;
            int32 tOut = or2 - d2 - 1; // dx^2 <= or2 - d^2 - 1
            if (tOut >= 0) {
                b         = PS1ISqrtDown(bOut, bOut2, tOut);
                int32 tIn = ir2 - d2; // the smallest |dx| with dx^2 >= ir2 - d^2
                if (tIn > 0)
                    a = PS1ISqrtDown(aIn, aIn2, tIn - 1) + 1;
                if (a > b)
                    b = -1;
            }
            tab[d].start = (int16)a, tab[d].end = (int16)b;
        }
    }
    {
        int32 runS = 0, runE = 0, runD = -1;
        for (int32 d = lo; d <= hi + 1; ++d) { // d = hi + 1: no row (ends the last run)
            int32 s = 0, e = 0;
            if (d <= hi) {
                int32 a = tab[d].start, b = tab[d].end;
                if (b >= 0) {
                    s = x - b, e = a ? x - a + 1 : x + b + 1;
                    s = s < left ? left : s;
                    e = e > right ? right : e;
                }
            }
            bool on = s < e;
            if (on && runD >= 0 && s == runS && e == runE)
                continue;
            if (runD >= 0) // one call site: the writer is inline
                PS1MirrorPut(o, runS, runE, runD, d - 1);
            runD = on ? d : -1, runS = s, runE = e;
        }
    }
    if (ir2 > lo * lo) { // some row crosses the inner circle (a negative inner radius too: the PC squares it)
        int32 runS = 0, runE = 0, runD = -1;
        for (int32 d = lo; d <= hi + 1; ++d) {
            int32 s = 0, e = 0;
            if (d <= hi) {
                int32 a = tab[d].start, b = tab[d].end;
                if (a && b >= 0) { // else one span (the left pass drew it) or none
                    s = x + a, e = x + b + 1;
                    s = s < left ? left : s;
                    e = e > right ? right : e;
                }
            }
            bool on = s < e;
            if (on && runD >= 0 && s == runS && e == runE)
                continue;
            if (runD >= 0)
                PS1MirrorPut(o, runS, runE, runD, d - 1);
            runD = on ? d : -1, runS = s, runE = e;
        }
    }
    PS1RectEnd(o);
}

// DrawLine after its ink checks (screen coordinates): a GPU line (GP0 40h; the GPU's own pixels, approximate)
void RSDK::PS1DrawLine(int32 x1, int32 y1, int32 x2, int32 y2, uint32 color, int32 alpha, int32 inkEffect)
{
    uint32 bgr;
    PS1Ink ink;
    if (x1 < -1024 || x1 > 1023 || x2 < -1024 || x2 > 1023 || y1 < -1024 || y1 > 1023 || y2 < -1024 || y2 > 1023
        || !PS1FlatInk(color, inkEffect, alpha, bgr, ink))
        return;
    if (maskColor) // 4.1b: while a mask colour is set, every pixel but the mask colour's has bit 15 set
        PS1ChainMaskControl(true, false);
    for (int32 p = 0; p < ink.passes; ++p) {
        uint32_t *wr = PS1ChainSpanBegin(5);
        if (!wr)
            break;
        int32 n = 0;
        if (ink.mode >= 0)
            wr[1 + n++] = 0xE1000000 | (uint32)ink.mode << 5;
        wr[1 + n++] = 0x40000000 | bgr | (ink.mode >= 0 ? 0x02000000 : 0);
        wr[1 + n++] = (uint32)(uint16)y1 << 16 | (uint16)x1;
        wr[1 + n++] = (uint32)(uint16)y2 << 16 | (uint16)x2;
        PS1ChainSpanEnd(wr, n, wr + 1 + n);
    }
    if (maskColor)
        PS1ChainMaskControl(false, false);
    g_ps1PrimApprox = g_ps1PrimApprox + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(9, 0, 0, 0, 0, 0, 0, x1, y1, x2, y2); // the host marks the line's pixels approximate
#endif
}

// DrawBlendedFace after its ink checks: a gouraud fan (GP0 30h triangles; approximate)
// docs/32 7.3: Scene3D's faces (PS1Atlas.hpp): G3 / G4 (F3 / F4 for the flat modes) written straight into the chain
// arena, 32 at a time (PS1ChainBulkBegin); the ink as PS1FlatInk gives it (one pass; a mask colour: bit 15 forced)
namespace
{
PS1Ink s_ps1F3DInk;
uint32 s_ps1F3DSemi = 0;
uint32 *s_ps1F3DBase = NULL;
int32 s_ps1F3DN = 0, s_ps1F3DWpp = 0;
void PS1Face3DFlush()
{
    if (s_ps1F3DBase)
        PS1ChainBulkEnd(s_ps1F3DN);
    s_ps1F3DBase = NULL, s_ps1F3DN = 0;
}
} // namespace
extern "C" {
volatile uint32 g_ps1Face3DDrawn = 0; // Scene3D faces sent (total)
}
bool RSDK::PS1Face3DBegin(int32 inkEffect, int32 alpha, uint32 flatColor, uint32 *flatBgr, uint32 *semi)
{
    uint32 bgr;
    if (!PS1FlatInk(flatColor, inkEffect, alpha, bgr, s_ps1F3DInk))
        return false;
    s_ps1F3DSemi = s_ps1F3DInk.mode >= 0 ? 0x02000000 : 0;
    *flatBgr = bgr, *semi = s_ps1F3DSemi;
    s_ps1F3DBase = NULL, s_ps1F3DN = 0, s_ps1F3DWpp = 0;
    if (s_ps1F3DInk.mode >= 0)
        PS1ChainTPageRaw((uint16)(s_ps1F3DInk.mode << 5));
    if (maskColor)
        PS1ChainMaskControl(true, false);
    return true;
}
uint32 *RSDK::PS1Face3DSlot(int32 wpp)
{
    if (!s_ps1F3DBase || wpp != s_ps1F3DWpp || s_ps1F3DN == 32) {
        PS1Face3DFlush();
        s_ps1F3DBase = (uint32 *)PS1ChainBulkBegin(32, wpp);
        if (!s_ps1F3DBase)
            return NULL;
        s_ps1F3DWpp = wpp;
    }
    g_ps1Face3DDrawn = g_ps1Face3DDrawn + 1;
    return s_ps1F3DBase + s_ps1F3DN++ * (wpp + 1);
}
// 9f: PS1DrawBlendedFace's triangles for an opaque face (INK_NONE: one pass, no texpage word) into the open Face3D bulk
void RSDK::PS1BlendedFaceBulk(Vector2 *vertices, uint32 *colors, int32 vertCount)
{
    for (int32 v = 1; v + 1 < vertCount; ++v) {
        const int32 idx[3] = { 0, v, v + 1 };
        uint32 *w = PS1Face3DSlot(6);
        if (!w)
            break;
        for (int32 k = 0; k < 3; ++k) {
            uint32 c   = colors[idx[k]];
            uint32 bgr = (c & 0xFF) << 16 | (c & 0xFF00) | ((c >> 16) & 0xFF);
            w[2 * k]     = (k == 0 ? 0x30000000 : 0) | bgr;
            w[2 * k + 1] = (uint32)(uint16)FROM_FIXED(vertices[idx[k]].y) << 16 | (uint16)FROM_FIXED(vertices[idx[k]].x);
        }
    }
    g_ps1PrimApprox = g_ps1PrimApprox + 1;
#if PS1_DEBUG_BIGRAM
    { // the host marks the face's bounding box approximate (as PS1DrawBlendedFace)
        int32 x0 = 0x7FFF, y0 = 0x7FFF, x1 = -0x7FFF, y1 = -0x7FFF;
        for (int32 v = 0; v < vertCount; ++v) {
            int32 px = FROM_FIXED(vertices[v].x), py = FROM_FIXED(vertices[v].y);
            x0 = px < x0 ? px : x0, x1 = px > x1 ? px : x1, y0 = py < y0 ? py : y0, y1 = py > y1 ? py : y1;
        }
        PS1DrawLogAdd(10, 0, 0, 0, 0, 0, 0, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
#endif
}
// 9o: PS1DrawLine's words for an INK_NONE line (bgr = PS1FlatInk's for INK_NONE) into the open Face3D bulk (Scene3D's
// wireframe: the used giant ring); the same range check
void RSDK::PS1LineBulk(int32 x1, int32 y1, int32 x2, int32 y2, uint32 bgr)
{
    if (x1 < -1024 || x1 > 1023 || x2 < -1024 || x2 > 1023 || y1 < -1024 || y1 > 1023 || y2 < -1024 || y2 > 1023)
        return;
    uint32 *w = PS1Face3DSlot(3);
    if (!w)
        return;
    w[0] = 0x40000000 | bgr;
    w[1] = (uint32)(uint16)y1 << 16 | (uint16)x1;
    w[2] = (uint32)(uint16)y2 << 16 | (uint16)x2;
    g_ps1PrimApprox = g_ps1PrimApprox + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(9, 0, 0, 0, 0, 0, 0, x1, y1, x2, y2); // the host marks the line's pixels approximate
#endif
}
// 9o: a face's edges (corner v to v + 1, the last to the first) as PS1LineBulk would write them, with the bulk's slot
// bookkeeping inline: xs / ys the positions' screen coordinates, c the face's position indices
__attribute__((optimize("O2"))) void RSDK::PS1LineFace(const int32 *xs, const int32 *ys, const uint16 *c, int32 n, uint32 bgr)
{
    const uint32 cmd = 0x40000000 | bgr;
    for (int32 v = 0; v < n; ++v) {
        const int32 w = v + 1 < n ? v + 1 : 0;
        const int32 x1 = xs[c[v]], y1 = ys[c[v]], x2 = xs[c[w]], y2 = ys[c[w]];
        if (x1 < -1024 || x1 > 1023 || x2 < -1024 || x2 > 1023 || y1 < -1024 || y1 > 1023 || y2 < -1024 || y2 > 1023)
            continue;
        if (!s_ps1F3DBase || s_ps1F3DWpp != 3 || s_ps1F3DN == 32) {
            PS1Face3DFlush();
            s_ps1F3DBase = (uint32 *)PS1ChainBulkBegin(32, 3);
            if (!s_ps1F3DBase)
                return;
            s_ps1F3DWpp = 3;
        }
        uint32 *wr = s_ps1F3DBase + s_ps1F3DN++ * 4;
        wr[0] = cmd;
        wr[1] = (uint32)(uint16)y1 << 16 | (uint16)x1;
        wr[2] = (uint32)(uint16)y2 << 16 | (uint16)x2;
#if PS1_DEBUG_BIGRAM
        PS1DrawLogAdd(9, 0, 0, 0, 0, 0, 0, x1, y1, x2, y2);
#endif
    }
    g_ps1Face3DDrawn = g_ps1Face3DDrawn + n;
    g_ps1PrimApprox  = g_ps1PrimApprox + n;
}
void RSDK::PS1Face3DEnd()
{
    PS1Face3DFlush();
    if (maskColor)
        PS1ChainMaskControl(false, false);
    if (s_ps1F3DInk.mode >= 0)
        PS1ChainInvalidateTPage();
}

void RSDK::PS1DrawBlendedFace(Vector2 *vertices, uint32 *colors, int32 vertCount, int32 alpha, int32 inkEffect)
{
    PS1Ink ink;
    uint32 dummy;
    if (vertCount < 3 || !PS1FlatInk(0, inkEffect, alpha, dummy, ink))
        return;
    uint32 semi = ink.mode >= 0 ? 0x02000000 : 0;
    if (maskColor) // 4.1b: as lines
        PS1ChainMaskControl(true, false);
    for (int32 v = 1; v + 1 < vertCount; ++v) {
        const int32 idx[3] = { 0, v, v + 1 };
        for (int32 p = 0; p < ink.passes; ++p) {
            uint32_t *wr = PS1ChainSpanBegin(8);
            if (!wr)
                break;
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
    if (maskColor)
        PS1ChainMaskControl(false, false);
    g_ps1PrimApprox = g_ps1PrimApprox + 1;
#if PS1_DEBUG_BIGRAM
    { // the host marks the face's bounding box approximate
        int32 x0 = 0x7FFF, y0 = 0x7FFF, x1 = -0x7FFF, y1 = -0x7FFF;
        for (int32 v = 0; v < vertCount; ++v) {
            int32 px = FROM_FIXED(vertices[v].x), py = FROM_FIXED(vertices[v].y);
            x0 = px < x0 ? px : x0, x1 = px > x1 ? px : x1, y0 = py < y0 ? py : y0, y1 = py > y1 ? py : y1;
        }
        PS1DrawLogAdd(10, 0, 0, 0, 0, 0, 0, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
#endif
}
