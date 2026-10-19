// PS1 port (docs/32 3.3): tile layers drawn by the GPU. Included by Scene.cpp.
//
// DrawLayerHScroll's software loop draws each screen line from scanlines[line].position (the layer pixel at the
// line's left edge, 16.16) with the line's palette bank (gfxLineBuffer). Here consecutive lines with the same X, a
// layer row one further per line, the same tile row and the same bank form a band, drawn as one row of tile pieces
// (the tiles' images from PS1Tiles: 4-bit on shared CLUTs or 8-bit on the bank CLUT): an unflipped piece is a sprite
// (a whole 16x16 tile the fast 16x16 kind), grouped by texture page (the pieces of a band never overlap) and written
// straight into the arena (3.3b); one line of a V-flipped tile is a sprite too (the mirrored row). Other flipped pieces
// are textured quads with backwards UVs (no rectangle flip bits on old GPUs, psx-spx), split where an edge would
// leave its page (u -1 or v -1: that column / row drawn as a constant strip). The GPU clips at the screen edges.
// The CLUTs are filled from the live palette at the frame start (PS1LayersFrameBegin); per-line banks are 3.5.
// Per-line parallax makes one band per line: the bands of one tile row share its tiles, so they are drawn from a
// column table (3.3b): the row's columns that any of them shows, looked up once (texture word, flips, page slot).
// Lines on which the whole layer is one colour (the .ptl's tables, 3.3b) don't depend on X: a run of them with one
// palette bank that covers at least the tile band starting on its first line is one flat rectangle (GP0 60h: not dithered, psx-spx), drawn with the mask bit forced (GP0 E6h) so its
// pixels read 0x8000 | colour like the tiles' CLUT entries.
#include "../../../../ps1/gpu_chain_raw.hh"
#include "../../../../ps1/gpu_detect.hh" // flip-bit rectangles (docs/43 G3)
#include "../../../../ps1/video.hh" // PS1Hblanks

namespace RSDK
{
extern "C" {
volatile uint32 g_ps1LayerBands  = 0; // bands drawn (this frame)
volatile uint32 g_ps1LayerPieces = 0; // tile pieces chained (this frame)
volatile uint32 g_ps1LayerMisses = 0; // tiles with no image in VRAM (must stay 0)
volatile uint32 g_ps1LayerNoTiles = 0; // 4.2d: layer draws in a scene with no tile images (.ptl not built yet; must stay 0)
volatile uint32 g_ps1LayerHbl    = 0; // hblanks spent drawing layers (this frame)
volatile uint32 g_ps1LayerSolidRuns = 0; // one-colour line runs drawn as rectangles (this frame)
volatile uint32 g_ps1LayerRowTables = 0; // tile rows drawn from a column table (this frame)
volatile uint32 g_ps1LayerUniform   = 0; // layers drawn from their uniform scroll, no scanlines (this frame)
volatile uint32 g_ps1LayerDirect    = 0; // layers drawn from lineScroll and their scroll infos, no scanlines (3.3h, total)
volatile uint32 g_ps1RotoLayers     = 0; // rotozoom layers drawn (4.1d, PS1Roto.cpp; total)
volatile uint32 g_ps1RotoQuads      = 0; // their tile quads (this frame)
volatile uint32 g_ps1RotoPageLayers = 0; // 9e: of them drawn from a page (PS1RotoPageDraw; total)
volatile uint32 g_ps1RotoPageCuts   = 0; // 9e: line spans past PS1_ROTO_SEGS, not drawn (total; must stay 0)
volatile uint32 g_ps1RotoFlat       = 0; // 7b.6: rotozoom layers drawn unscaled per line (horizontal-only scaling: total)
volatile uint32 g_ps1RotoTodo       = 0; // rotozoom draws not done: lines on several palette banks, a region too big (total)
volatile uint32 g_ps1RotoHbl        = 0; // hblanks spent in them (this frame)
volatile uint32 g_ps1LayerHblBy[8]  = {}; // hblanks per layer index (this frame)
volatile int32 g_ps1PalDiffs        = 0;  // 9f: bank-0 entries differing from the CLUTs uploaded this frame (PS1PaletteWrite)
__attribute__((noinline)) void PS1LayerDebugHook(uint32 index) { __asm__ volatile("" ::"r"(index)); } // GDB: each layer draw
volatile uint32 g_ps1FrameBufferY = 0;          // the buffer this frame draws into (0 / 256; evidence scripts)
volatile uint32 g_ps1LayerT[4] = { 0, 0, 0, 0 }; // PROFILE=1 (ticks of 8 cycles a frame): scan, gather, sprites, quads
}
} // namespace RSDK

void PS1ClutRow(const uint16 *row, int32 n, int16 x, int16 y); // (below)
extern "C" volatile uint32 g_ps1BankTodo;

namespace
{
#if PS1_PROFILE
inline uint16 PS1T() { return *(volatile uint16 *)0xBF801120; } // root counter 2, sysclock / 8 (ps1/rsdk5_main.cpp)
#define PS1_LT_ADD(i, v) (g_ps1LayerT[i] += (uint16)(v))
#else
inline uint16 PS1T() { return 0; }
#define PS1_LT_ADD(i, v) ((void)0)
#endif
#define PS1_SCREEN_PX   320
#define PS1_BAND_MAX    24 // pieces of one band: 320 / 16 + 1
#define PS1_COL_MAX     96 // columns of a tile row's table (320 px + the bands' spread)
// The column table of a tile row (DrawRowBands), in the scratchpad (1 KB of 1-cycle RAM at 1F800000h, psx-spx "Memory
// Map"; free outside the layer drawer's and the flat primitives' own calls): per column one record, the texture word
// (u | v << 8 | CLUT << 16), its texture page and flags (3.3f: one pointer walks it)
struct PS1Col {
    uint32_t tex;
    uint32_t pf; // texture page | flags << 16 (flags: 4 H-flip (no copy), 5 V-flip, 6 empty, 7 no image): one load (3.3j)
};
struct PS1ColTable {
    PS1Col col[PS1_COL_MAX];
};
static_assert(sizeof(PS1ColTable) <= 1024, "the column table must fit the scratchpad");
#define PS1_COL_HFLIP 0x10
#define PS1_COL_VFLIP 0x20
#define PS1_COL_EMPTY 0x40
#define PS1_COL_MISS  0x80
#define PS1_COL_TABLE ((PS1ColTable *)0x1F800000)

// RGB565 (fullPalette) -> PS1 BGR555, STP set on every visible entry (0x0000 is the GPU's transparent texel: an opaque
// black must read 0x8000; STP also makes semi-transparent primitives blend every texel, as Sonic CD's CLUTs)
// 4.1b: the mask colour (SetPaletteMask) keeps STP clear, so its pixels keep framebuffer bit 15 clear: INK_MASKED sprites,
// drawn with the GPU's mask test (GP0 E6h bit 1), land only on them (every other pixel has bit 15 set)
inline uint16 PS1Colour(uint16 c)
{
    uint16 p = (uint16)(0x8000 | ((c >> 11) & 31) | (((c >> 6) & 31) << 5) | ((c & 31) << 10));
    if (maskColor && c == (uint16)maskColor && (p & 0x7FFF))
        p &= 0x7FFF;
    return p;
}

// A band's primitives written straight into an arena span as they are gathered (3.3d): each primitive whole in a
// fragment of <= 14 words; a texpage word only where the page changes (a textured quad carries its own page, which
// then is the GPU's texpage: psx-spx "Texpage Attribute"); pieces of a band never overlap, so their order is free.
struct PS1Out {
    uint32_t *base, *end, *hdr, *w;
    int32 fw;    // words in the open fragment
    uint32 page; // the GPU's texpage after the last primitive written (0xFFFFFFFF: unknown)
};
#define PS1_OUT_RESERVE (PS1_BAND_MAX * 15) // a fragment per piece, texpage included; flipped pieces may split into more

inline bool PS1OutBegin(PS1Out &o)
{
    o.base = PS1ChainSpanBegin(PS1_OUT_RESERVE);
    if (!o.base)
        return false;
    o.end = o.base + PS1_OUT_RESERVE, o.hdr = o.base, o.w = o.base + 1, o.fw = 0;
    return true;
}

inline void PS1OutEnd(PS1Out &o)
{
    if (o.base && o.fw)
        PS1ChainSpanEnd(o.hdr, o.fw, o.w);
    o.base = NULL;
}

// Room for a primitive of n words: a new fragment when the open one would pass 14 words, a new span when the
// reservation would end (rare: flipped pieces split at page edges). False: the arena is full (counted by the chain)
__attribute__((always_inline)) inline bool PS1OutRoom(PS1Out &o, int32 n)
{
    if (!o.base)
        return false;
    bool close = o.fw + n > 14;
    if (o.w + n + close > o.end) {
        uint32 page = o.page;
        PS1OutEnd(o);
        if (!PS1OutBegin(o))
            return false;
        o.page = page;
        close  = false;
    }
    if (close) {
        *o.hdr = (uint32_t)o.fw << 24 | ((uintptr_t)o.w & 0xFFFFFF);
        o.hdr  = o.w++;
        o.fw   = 0;
    }
    return true;
}

// A textured quad (GP0 2Ch, colour 0x80: texel colours unchanged) over texels [uL, uR) x [vT, vB), which may run
// backwards; corners A B C D = top-left, top-right, bottom-left, bottom-right.
__attribute__((always_inline)) inline void Quad(PS1Out &o, int32 x, int32 y, int32 w, int32 h, uint32 clut, uint32 tpage, int32 uL,
                                                int32 uR, int32 vT, int32 vB)
{
    if (!PS1OutRoom(o, 9))
        return;
    uint32_t *q = o.w;
    uint32 xl = (uint16)x, xr = (uint16)(x + w);
    uint32 yt = (uint32)(uint16)y << 16, yb = (uint32)(uint16)(y + h) << 16;
    q[0] = 0x2C808080;
    q[1] = yt | xl;
    q[2] = clut << 16 | (uint32)(uint8)vT << 8 | (uint8)uL;
    q[3] = yt | xr;
    q[4] = tpage << 16 | (uint32)(uint8)vT << 8 | (uint8)uR;
    q[5] = yb | xl;
    q[6] = (uint32)(uint8)vB << 8 | (uint8)uL;
    q[7] = yb | xr;
    q[8] = (uint32)(uint8)vB << 8 | (uint8)uR;
    o.w += 9, o.fw += 9, o.page = tpage;
}

// Rows [ty0, ty0 + h) of a flipped tile (rows counted before flipping) at screen (sx, sy)
__attribute__((always_inline)) inline void FlippedPiece(PS1Out &o, int32 sx, int32 sy, int32 u, int32 v, int32 ty0, int32 h, bool fx,
                                                        bool fy, uint32 clut, uint32 tpage)
{
    int32 uL = fx ? u + 15 : u, uR = fx ? u - 1 : u + 16;
    int32 vT = fy ? v + 15 - ty0 : v + ty0, dv = fy ? -1 : 1;
    int32 vB = vT + h * dv;
    // an edge texel one past the page (u or v -1, or 256) can't be addressed: that column / row gets its own strip
    int32 w = TILE_SIZE, uEdge = -1;
    if (uR < 0 || uR > 255)
        w = TILE_SIZE - 1, uEdge = uR < 0 ? 0 : 255, uR = uR < 0 ? 0 : 255;
    int32 hh = h, vEdge = -1;
    if (vB < 0 || vB > 255)
        hh = h - 1, vEdge = vB < 0 ? 0 : 255, vB = vB < 0 ? 0 : 255;
    if (hh > 0) {
        Quad(o, sx, sy, w, hh, clut, tpage, uL, uR, vT, vB);
        if (uEdge >= 0)
            Quad(o, sx + w, sy, 1, hh, clut, tpage, uEdge, uEdge, vT, vB);
    }
    if (vEdge >= 0) {
        int32 vLast = vT + (h - 1) * dv;
        Quad(o, sx, sy + hh, w, 1, clut, tpage, uL, uR, vLast, vLast);
        if (uEdge >= 0)
            Quad(o, sx + w, sy + hh, 1, 1, clut, tpage, uEdge, uEdge, vLast, vLast);
    }
}

// A sprite piece (16x16: 3 words; a partial band's sized sprite: 4), after a texpage word if the page changed
__attribute__((always_inline)) inline void SpritePiece(PS1Out &o, uint32 cmd, uint32 xy, uint32 tex, uint32 wh, bool full,
                                                       uint32 tpage)
{
    bool newPage = tpage != o.page;
    int32 n      = (full ? 3 : 4) + newPage;
    if (!PS1OutRoom(o, n))
        return;
    uint32_t *w = o.w;
    if (newPage)
        *w++ = 0xE1000000 | tpage, o.page = tpage;
    w[0] = cmd, w[1] = xy, w[2] = tex;
    if (!full)
        w[3] = wh;
    o.w += n, o.fw += n;
}

// --- 3.3f: the span writer as scalars ----------------------------------------------------------------------------------
// The hot loops keep the writer in three registers: w (the next word), lim (the open fragment's header + 10: past it a
// piece of <= 5 words might not fit in 14, 3.3g), end (the span's reservation end); a fragment is opened only with 15
// words left before end, so a piece never checks end.
// The rare paths (a new span, a flipped quad) go through PS1Emit, out of line, so nothing in the loops is spilled.
// Arena full: the writer points at a sink, where every fragment asks for a new span again (each failure counted).
struct PS1Emit {
    uint32_t *w, *hdr, *end;
    uint32 page;
};
#define PS1_EMIT_RESERVE 512
uint32_t s_ps1EmitSink[16];

__attribute__((always_inline)) inline void EmitSink(PS1Emit &e) { e.hdr = s_ps1EmitSink, e.w = s_ps1EmitSink + 1, e.end = s_ps1EmitSink + 1; }

__attribute__((always_inline)) inline void EmitOpen(PS1Emit &e)
{
    uint32_t *b = PS1ChainSpanBegin(PS1_EMIT_RESERVE);
    if (!b) {
        EmitSink(e);
        return;
    }
    e.hdr = b, e.w = b + 1, e.end = b + PS1_EMIT_RESERVE;
}

__attribute__((always_inline)) inline void EmitClose(PS1Emit &e)
{
    if (e.hdr != s_ps1EmitSink && e.w > e.hdr + 1)
        PS1ChainSpanEnd(e.hdr, (int32)(e.w - e.hdr - 1), e.w);
    else
        PS1ChainSpanEnd(NULL, 0, NULL);
}

// The span is used up: link it, reserve another
__attribute__((noinline)) void EmitNewSpan(PS1Emit &e)
{
    EmitClose(e);
    EmitOpen(e);
}

// The open fragment is full (3.3g: the loops test w > lim, lim = header + 10, so a piece of <= 5 words always fits):
// close it and open the next, or a new span when fewer than 15 words are left
__attribute__((always_inline)) inline void EmitFrag(uint32_t *&w, uint32_t *&lim, uint32_t *&end, uint32 page)
{
    if (w + 15 > end) {
        PS1Emit e = { w, lim - 10, end, page };
        EmitNewSpan(e);
        w = e.w, lim = e.hdr + 10, end = e.end;
    }
    else {
        uint32_t *hdr = lim - 10;
        *hdr          = (uint32_t)(w - hdr - 1) << 24 | ((uintptr_t)w & 0xFFFFFF);
        lim           = w + 10;
        ++w;
    }
}

// A flipped piece as textured quads (FlippedPiece), out of line: H-flipped tiles without a copy, V-flipped bands of
// more than 4 lines
__attribute__((noinline)) void EmitFlipped(PS1Emit &e, int32 sx, int32 sy, uint32 tex, int32 v0, int32 h, uint32 f, uint32 tpage)
{
    if (e.hdr == s_ps1EmitSink) {
        EmitNewSpan(e);
        if (e.hdr == s_ps1EmitSink)
            return;
    }
    PS1Out o;
    o.base = e.hdr, o.end = e.end, o.hdr = e.hdr, o.w = e.w, o.fw = (int32)(e.w - e.hdr - 1), o.page = e.page;
    FlippedPiece(o, sx, sy, tex & 0xFF, (tex >> 8) & 0xFF, v0, h, f & PS1_COL_HFLIP, f & PS1_COL_VFLIP, tex >> 16, tpage);
    if (!o.base) { // PS1OutBegin found the arena full
        EmitSink(e);
        return;
    }
    e.w = o.w, e.hdr = o.hdr, e.end = o.end, e.page = o.page;
    if (e.hdr + 15 > e.end) // PS1OutRoom fills a reservation to its end: the loops need 15 words per fragment
        EmitNewSpan(e);
}

// One sprite piece: a texpage word first if the page changed; n = 3 (16x16, GP0 7Ch) or 4 (sized, GP0 64h). The
// size word is written either way (a 16x16 sprite's lands on the next piece's first word, inside the fragment's 14)
#define PS1_EMIT_PIECE(cmdW, xyW, texW, whW, nW, tpW)                                                                   \
    do {                                                                                                               \
        uint32 tp_ = (tpW);                                                                                            \
        if (__builtin_expect(w > lim, 0))                                                                              \
            EmitFrag(w, lim, end, page);                                                                               \
        if (tp_ != page)                                                                                               \
            *w++ = 0xE1000000 | tp_, page = tp_;                                                                       \
        w[0] = (cmdW), w[1] = (xyW), w[2] = (texW), w[3] = (whW);                                                      \
        w += (nW);                                                                                                     \
    } while (0)

// A V-flipped piece of h <= 4 lines: one sized sprite per line, each the mirrored row (the same pixels as the quad;
// cheaper for the CPU and the GPU). tex = the tile's u | v << 8 | CLUT << 16
#define PS1_EMIT_VFLIP_LINES(sxW, syW, texW, v0W, hW, tpW)                                                             \
    do {                                                                                                               \
        uint32 xyl_  = (uint32)(uint16)(syW) << 16 | (uint16)(sxW);                                                    \
        uint32 texl_ = (texW) + ((uint32)(15 - (v0W)) << 8);                                                           \
        for (int32 r_ = 0; r_ < (hW); ++r_, xyl_ += 0x10000, texl_ -= 0x100)                                           \
            PS1_EMIT_PIECE(0x64808080, xyl_, texl_, TILE_SIZE | 1u << 16, 4, (tpW));                                   \
    } while (0)

// A flipped piece (3.3i): one textured quad written inline (FlippedPiece's quad, the same words) unless an edge texel
// would leave the page (u / v -1 or 256), which FlippedPiece splits (EmitFlipped, out of line). A quad is 9 words: it
// fits when w <= header + 6 = lim - 4; it carries its own texpage. tex = u | v << 8 | CLUT << 16, f = PS1_COL_ flips
#define PS1_EMIT_FLIPPED(sxW, syW, texW, v0W, hW, fW, tpW)                                                             \
    do {                                                                                                               \
        int32 u_ = (texW) & 0xFF, v_ = ((texW) >> 8) & 0xFF;                                                           \
        int32 uL_ = ((fW) & PS1_COL_HFLIP) ? u_ + 15 : u_, uR_ = ((fW) & PS1_COL_HFLIP) ? u_ - 1 : u_ + TILE_SIZE;      \
        int32 vT_ = ((fW) & PS1_COL_VFLIP) ? v_ + 15 - (v0W) : v_ + (v0W);                                              \
        int32 vB_ = ((fW) & PS1_COL_VFLIP) ? vT_ - (hW) : vT_ + (hW);                                                   \
        if ((uint32)uR_ > 255 || (uint32)vB_ > 255) {                                                                  \
            PS1Emit q_ = { w, lim - 10, end, page };                                                                   \
            EmitFlipped(q_, (sxW), (syW), (texW), (v0W), (hW), (fW), (tpW));                                           \
            w = q_.w, lim = q_.hdr + 10, end = q_.end, page = q_.page;                                                 \
        }                                                                                                              \
        else {                                                                                                         \
            if (__builtin_expect(w > lim - 4, 0))                                                                      \
                EmitFrag(w, lim, end, page);                                                                           \
            uint32 xl_ = (uint16)(sxW), xr_ = (uint16)((sxW) + TILE_SIZE);                                             \
            uint32 yt_ = (uint32)(uint16)(syW) << 16, yb_ = (uint32)(uint16)((syW) + (hW)) << 16;                      \
            w[0] = 0x2C808080, w[1] = yt_ | xl_;                                                                       \
            w[2] = ((texW) & 0xFFFF0000) | (uint32)vT_ << 8 | (uint32)uL_;                                             \
            w[3] = yt_ | xr_, w[4] = (uint32)(tpW) << 16 | (uint32)vT_ << 8 | (uint32)uR_;                             \
            w[5] = yb_ | xl_, w[6] = (uint32)vB_ << 8 | (uint32)uL_;                                                    \
            w[7] = yb_ | xr_, w[8] = (uint32)vB_ << 8 | (uint32)uR_;                                                    \
            w += 9, page = (tpW);                                                                                      \
        }                                                                                                              \
    } while (0)

// Screen lines [sy, sy + h) showing layer rows [ly, ly + h) (inside one tile row), the line's left edge at layer x.
// Hot (hundreds of pieces a frame): -O2, the tiles fetched walking the 4x4 chunks (one chunk index per 4 tiles), the
// tile image read as three halfwords (u | v << 8, texpage, CLUT), every piece written straight into the arena.
// 7b.7: tiles on a palette bank other than 0 (water, flashes: SetActivePalette). A frame's one extra bank gets copies of
// the scene's 4-bit tile CLUTs in rows 242-247, X 0-255 (the 256-entry rows of banks 2-7, tools/gfx/vram_plan.py; its own
// row 240 + bank skipped) and its 256 entries at 240 + bank (8-bit tiles), uploaded into the chain the first time the
// frame draws a tile row on it (the water hook sets the banks after the frame's start); another bank in the same frame,
// or more CLUTs than the rows hold, stays bank 0 (counted: g_ps1BankTodo)
uint32 s_layerFrame = 1, s_extraFrame = 0;
int32 s_extraBank   = 0;
bool s_extraOk      = false;
uint8 s_attrSlot[16 * 20];  // a tile CLUT attribute (rows 240-255, X / 16 < 20) -> its index in PS1TileCluts()
uint16 s_copyAttr[96];      // index -> its copy's attribute
const void *s_attrSet = NULL;
bool PS1BankTiles(int32 bank)
{
    if (s_extraFrame == s_layerFrame)
        return s_extraOk && s_extraBank == bank;
    s_extraFrame = s_layerFrame, s_extraBank = bank, s_extraOk = false;
    const PS1TileClut *cl = PS1TileCluts();
    int32 n               = cl ? PS1TileClutCount() : 0;
    if (n > 80 || bank <= 0 || bank > 7)
        return false;
    if (cl != s_attrSet) {
        memset(s_attrSlot, 0xFF, sizeof(s_attrSlot));
        for (int32 c = 0; c < n; ++c)
            if (cl[c].y >= 240 && cl[c].y < 256 && cl[c].x < 320)
                s_attrSlot[(cl[c].y - 240) * 20 + (cl[c].x >> 4)] = (uint8)c;
        s_attrSet = cl;
    }
    static uint16 s_row[256];
    s_row[0] = 0;
    for (int32 c = 0, r = 242; c < n; ++c) {
        if (!(c & 15) && c) {
            if (++r == 240 + bank)
                ++r;
        }
        else if (!c && r == 240 + bank)
            ++r;
        for (int32 i = 1; i < 16; ++i) s_row[i] = cl[c].map[i] ? PS1Colour(fullPalette[bank][cl[c].map[i]]) : 0;
        int16 x       = (int16)((c & 15) << 4);
        s_copyAttr[c] = (uint16)((uint32)(x >> 4) | (uint32)r << 6);
        PS1ClutRow(s_row, 16, x, (int16)r);
    }
    for (int32 i = 1; i < 256; ++i) s_row[i] = PS1Colour(fullPalette[bank][i]);
    PS1ClutRow(s_row, 256, 0, (int16)(240 + bank));
    PS1ChainFlushCache();
    s_extraOk = true;
    return true;
}
// (the bands only ask: PS1BankTiles uploads, outside any open span, at the layer's start)
inline bool PS1BankReady(int32 bank) { return s_extraFrame == s_layerFrame && s_extraOk && s_extraBank == bank; }
inline uint32 PS1BankTileClut(uint32 attr)
{
    uint32 y = attr >> 6, k = (y - 240) * 20 + (attr & 63);
    uint8 c  = k < sizeof(s_attrSlot) ? s_attrSlot[k] : 0xFF;
    return c < 96 ? s_copyAttr[c] : attr;
}

__attribute__((optimize("O2"))) void DrawBandInto(PS1Emit &e, TileLayer *layer, int32 sy, int32 h, int32 x, int32 ly, uint8 bank)
{
    bool banked = bank && PS1BankReady(bank);
    if (bank && !banked)
        g_ps1BankTodo = g_ps1BankTodo + 1;
    uint16 tg              = PS1T();
    const uint16 *tileImage = TileTable();
    const bool flipAll      = PS1TileFlipCopiesAll();
    const uint8 *images     = s_ps1TileBlob + s_imgOff; // PS1TileImage: u8 u, v; u16 tpage, clut
    uint32 bankClut         = (uint32)(0 | (240 + bank) << 6); // GPU CLUT attribute of the bank's 256 entries
    int32 ty = ly >> 4, v0 = ly & 15;
    int32 tx = x >> 4, sx = -(x & 15);
    int32 xsize            = layer->xsize;
    const uint16 *chunkRow = PS1ChunkRow(layer, (uint32)ty >> 2); // (x wraps at xsize: its columns are all in the index)
    const uint16 *rowSel   = ps1ChunkPool + (ty & 3); // 7b.3: the chunks' row ty & 3
    const uint16 *chunk    = PS1ChunkTiles(rowSel, PS1ChunkIdx(chunkRow, tx >> 2));
    int32 sub              = tx & 3;
    int32 pieces = 0, misses = 0;
    bool full   = h == TILE_SIZE;
    uint32 cmd  = full ? 0x7C808080 : 0x64808080, wh = TILE_SIZE | (uint32)h << 16;
    int32 n     = full ? 3 : 4;
    uint32 yWord = (uint32)(uint16)sy << 16, vRow = (uint32)v0 << 8;
    uint32_t *w = e.w, *lim = e.hdr + 10, *end = e.end;
    uint32 page = e.page;
    // docs/43 G3: on a GPU that draws flip rectangles (PS1FlipRectsLive) a flipped tile is the band's own sprite under its
    // texpage with GP0(E1h) bit 12 (X) / 13 (Y) (tile bits 0x400 / 0x800 << 2), from its last texel column u + 15 (odd, as
    // X flip wants: u is a multiple of 16) / row v + 15 - v0 (vFlip1 added to v + v0); page then carries the flip bits,
    // so the next unflipped piece differs from it and sends its own texpage
    const bool flipLive = PS1FlipRectsLive();
    const uint32 vFlip1 = (uint32)(15 - 2 * v0) * 256u;
    int32 frects        = 0;
    for (; sx < PS1_SCREEN_PX; sx += TILE_SIZE) {
        uint16 t = chunk[sub];
        if (++tx >= xsize) { // wrap: the layer repeats
            tx = 0, sub = 0;
            chunk = PS1ChunkTiles(rowSel, PS1ChunkIdx(chunkRow, 0));
        }
        else if (++sub == 4) {
            sub   = 0;
            chunk = PS1ChunkTiles(rowSel, PS1ChunkIdx(chunkRow, tx >> 2));
        }
        if (t == 0xFFFF)
            continue;
        uint16 img = tileImage[t & 0x3FF];
        if ((t & 0xC00) && flipAll && img != PS1_TILE_NONE) { // 9f: the flipped copy: a sprite, not a 9-word quad
            img = PS1TileHFlipImage((t & 0x800) ? (t & 0xFFF) : (t & 0x3FF));
            t &= ~0xC00;
        }
        if (img == PS1_TILE_NONE) {
            ++misses;
            continue;
        }
        uint32 img2 = (uint32)img << 1, img4 = (uint32)img << 2;
        __asm__("" : "+r"(img2), "+r"(img4)); // 6-byte records: 2i + 4i (GCC folds shifts into a ~12-cycle mult)
        const uint8 *ti = images + img2 + img4;
        uint32 tex      = *(const uint16 *)ti;
        uint32 tpage    = *(const uint16 *)(ti + 2);
        uint32 clut     = *(const uint16 *)(ti + 4);
        if (clut == 0xFFFF)
            clut = bankClut;
        else if (banked)
            clut = PS1BankTileClut(clut);
        tex |= clut << 16;
        ++pieces;
        if (t & 0xC00) {
            if (flipLive) { // docs/43 G3: the band's own sprite as a flip rectangle (see vFlip1 above)
                tex += ((t & 0x400) ? 15u : 0u) + ((t & 0x800) ? vFlip1 : 0u);
                tpage |= (uint32)(t & 0xC00) << 2;
                ++frects;
            }
            else {
                if ((t & 0x400) || h > 4) {
                    PS1_EMIT_FLIPPED(sx, sy, tex, v0, h, (uint32)(t & 0xC00) >> 6, tpage);
                }
                else {
                    PS1_EMIT_VFLIP_LINES(sx, sy, tex, v0, h, tpage);
                }
                continue;
            }
        }
        PS1_EMIT_PIECE(cmd, yWord | (uint16)sx, tex + vRow, wh, n, tpage);
    }
    e.w = w, e.hdr = lim - 10, e.end = end, e.page = page;
    if (misses)
        g_ps1LayerMisses = g_ps1LayerMisses + misses;
    if (frects) {
        g_ps1TileFlipRects = g_ps1TileFlipRects + frects;
        g_ps1FlipFrameUsed = true;
    }
    PS1_LT_ADD(1, PS1T() - tg);
    g_ps1LayerBands  = g_ps1LayerBands + 1;
    g_ps1LayerPieces = g_ps1LayerPieces + pieces;
}
// One band in its own span (a lone band of a tile row, DrawRowBands' fallback)
void DrawBand(TileLayer *layer, int32 sy, int32 h, int32 x, int32 ly, uint8 bank)
{
    PS1Emit e;
    EmitOpen(e);
    e.page = 0xFFFFFFFF;
    DrawBandInto(e, layer, sy, h, x, ly, bank);
    EmitClose(e);
}

// One band as the scan found it: screen lines [sy, sy + h), layer rows [ly, ly + h), left edge at layer x
struct PS1Band {
    int16 sy;
    uint8 h, bank;
    int32 x, ly, d; // d: first column relative to the group's first band (DrawRowBands)
};

// The bands of one tile row (2..16, same palette bank), each at its own X: the columns any of them shows are looked
// up once into the scratchpad's column table, then each band's pieces come from it. The same pieces as DrawBand.
__attribute__((optimize("O2"))) void DrawRowBands(TileLayer *layer, PS1Band *bands, int32 count)
{
    uint16 tg           = PS1T();
    int32 xsize         = layer->xsize;
    int32 ly0           = bands[0].ly;
    int32 ty            = ly0 >> 4;
    int32 c0            = bands[0].x >> 4;
    int32 dmin = 0, dmax = 0, half = xsize >> 1;
    for (int32 i = 0; i < count; ++i) {
        int32 d = (bands[i].x >> 4) - c0; // the layer wraps: the nearest copy of the column
        if (d > half)
            d -= xsize;
        else if (d < -half)
            d += xsize;
        bands[i].d = d;
        if (d < dmin)
            dmin = d;
        if (d > dmax)
            dmax = d;
    }
    int32 cols = dmax - dmin + PS1_BAND_MAX - 3; // 21 columns a band
    if (cols > PS1_COL_MAX) {
        for (int32 i = 0; i < count; ++i) DrawBand(layer, bands[i].sy, bands[i].h, bands[i].x, bands[i].ly, bands[i].bank);
        return;
    }
    // the table: columns c0 + dmin .. (walking the 4x4 chunks as DrawBand)
    const uint16 *tileImage = TileTable();
    const bool flipAll      = PS1TileFlipCopiesAll(); // 9f
    const uint8 *images     = s_ps1TileBlob + s_imgOff;
    uint32 bankClut         = (uint32)(0 | (240 + bands[0].bank) << 6);
    bool banked             = bands[0].bank && PS1BankReady(bands[0].bank); // 7b.7
    if (bands[0].bank && !banked)
        g_ps1BankTodo = g_ps1BankTodo + 1;
    PS1ColTable *ct         = PS1_COL_TABLE;
    int32 tx                = c0 + dmin;
    if (tx < 0)
        tx += xsize;
    else if (tx >= xsize)
        tx -= xsize;
    const uint16 *chunkRow = PS1ChunkRow(layer, (uint32)ty >> 2); // (x wraps at xsize: its columns are all in the index)
    const uint16 *rowSel   = ps1ChunkPool + (ty & 3); // 7b.3: the chunks' row ty & 3
    const uint16 *chunk    = PS1ChunkTiles(rowSel, PS1ChunkIdx(chunkRow, tx >> 2));
    int32 sub              = tx & 3;
    for (int32 k = 0; k < cols; ++k) {
        uint16 t = chunk[sub];
        if (++tx >= xsize) {
            tx = 0, sub = 0;
            chunk = PS1ChunkTiles(rowSel, PS1ChunkIdx(chunkRow, 0));
        }
        else if (++sub == 4) {
            sub   = 0;
            chunk = PS1ChunkTiles(rowSel, PS1ChunkIdx(chunkRow, tx >> 2));
        }
        PS1Col *col = &ct->col[k];
        if (t == 0xFFFF) {
            col->pf = PS1_COL_EMPTY << 16;
            continue;
        }
        uint16 img = tileImage[t & 0x3FF];
        if (img == PS1_TILE_NONE) {
            col->pf = PS1_COL_MISS << 16;
            continue;
        }
        if (flipAll) { // 9f: every flipped tile has its copy (H, V, HV): a sprite
            if (t & 0xC00) {
                uint16 flipped = PS1TileHFlipImage((t & 0x800) ? (t & 0xFFF) : (t & 0x3FF));
                if (flipped != PS1_TILE_NONE)
                    img = flipped, t &= ~0xC00;
            }
        }
        else if (t & 0x400) { // an H-flipped copy (the .ptl's pairs, 3.3b) makes the piece a sprite
            uint16 flipped = PS1TileHFlipImage(t & 0x3FF);
            if (flipped != PS1_TILE_NONE)
                img = flipped, t &= ~0x400;
        }
        uint32 img2 = (uint32)img << 1, img4 = (uint32)img << 2;
        __asm__("" : "+r"(img2), "+r"(img4)); // 6-byte records: 2i + 4i (GCC folds shifts into a ~12-cycle mult)
        const uint8 *ti = images + img2 + img4;
        uint32 clut     = *(const uint16 *)(ti + 4);
        if (clut == 0xFFFF)
            clut = bankClut;
        else if (banked)
            clut = PS1BankTileClut(clut);
        col->tex   = *(const uint16 *)ti | clut << 16;
        col->pf    = *(const uint16 *)(ti + 2) | (uint32)(t & 0xC00) << 10; // 0x400 -> 0x10 (H), 0x800 -> 0x20 (V), << 16
    }
    PS1_LT_ADD(1, PS1T() - tg);
    // each band from the table, straight into the arena (one span for the row: 3.3f)
    int32 misses = 0, pieces = 0;
    uint16 tb = PS1T();
    PS1Emit e;
    EmitOpen(e);
    uint32_t *w = e.w, *lim = e.hdr + 10, *end = e.end;
    uint32 page = 0xFFFFFFFF;
    for (int32 b = 0; b < count; ++b) {
        const PS1Band *band = &bands[b];
        int32 h = band->h, v0 = band->ly & 15, sy = band->sy;
        int32 sx0          = -(band->x & 15);
        const PS1Col *c    = &ct->col[band->d - dmin];
        bool full          = h == TILE_SIZE;
        uint32 cmd         = full ? 0x7C808080 : 0x64808080, wh = TILE_SIZE | (uint32)h << 16;
        int32 n            = full ? 3 : 4;
        uint32 yWord       = (uint32)(uint16)sy << 16, vRow = (uint32)v0 << 8;
        uint32 xyEnd       = yWord + PS1_SCREEN_PX;
        uint32 vFlip1      = (uint32)(15 - 2 * v0) * 256u; // row v0 -> the mirrored row 15 - v0 (mod 2^32)
        // the first piece may start left of the screen (x -15..0); the others at x >= 1, so their position word is
        // yWord + x and steps by 16 (3.3j)
#define PS1_LAKE_PIECE(sxW, xyW)                                                                                       \
    do {                                                                                                               \
        uint32 pf_ = c->pf, tex_ = c->tex + vRow;                                                                      \
        if (__builtin_expect(pf_ >> 16, 0)) {                                                                          \
            uint32 f_ = pf_ >> 16;                                                                                     \
            if (f_ == PS1_COL_VFLIP && h == 1) { /* one line of a V-flipped tile: its mirrored row (3.3k) */           \
                tex_ += vFlip1;                                                                                        \
                pf_ &= 0xFFFF;                                                                                         \
            }                                                                                                          \
            else {                                                                                                     \
                if (f_ & (PS1_COL_EMPTY | PS1_COL_MISS))                                                               \
                    misses += f_ >> 7;                                                                                 \
                else {                                                                                                 \
                    ++pieces;                                                                                          \
                    if ((f_ & PS1_COL_HFLIP) || h > 4)                                                                 \
                        PS1_EMIT_FLIPPED((sxW), sy, c->tex, v0, h, f_, pf_ & 0xFFFF);                                  \
                    else                                                                                               \
                        PS1_EMIT_VFLIP_LINES((sxW), sy, c->tex, v0, h, pf_ & 0xFFFF);                                  \
                }                                                                                                      \
                break;                                                                                                 \
            }                                                                                                          \
        }                                                                                                              \
        ++pieces;                                                                                                      \
        PS1_EMIT_PIECE(cmd, (xyW), tex_, wh, n, pf_);                                                                  \
    } while (0)
        PS1_LAKE_PIECE(sx0, yWord | (uint16)sx0);
        ++c;
        for (uint32 xy = yWord + (uint32)(sx0 + TILE_SIZE); xy < xyEnd; xy += TILE_SIZE, ++c)
            PS1_LAKE_PIECE((int32)(xy - yWord), xy);
#undef PS1_LAKE_PIECE
    }
    e.w = w, e.hdr = lim - 10;
    EmitClose(e);
    PS1_LT_ADD(1, PS1T() - tb);
    if (misses)
        g_ps1LayerMisses = g_ps1LayerMisses + misses;
    g_ps1LayerBands     = g_ps1LayerBands + count;
    g_ps1LayerPieces    = g_ps1LayerPieces + pieces;
    g_ps1LayerRowTables = g_ps1LayerRowTables + 1;
}

// Screen lines [sy, sy + h), one colour (palette index c of bank `bank`): E6h mask on, the rectangle, E6h mask off
void DrawSolid(int32 sy, int32 h, uint8 c, uint8 bank)
{
    uint16 rgb    = fullPalette[bank][c];
    uint32 colour = (uint32)((rgb >> 11) & 31) << 3 | (uint32)((rgb >> 6) & 31) << 11 | (uint32)(rgb & 31) << 19;
    uint32_t *w   = PS1ChainSpanBegin(6);
    if (!w)
        return;
    // 9b.6: a run of the mask colour keeps bit 15 clear, as its tile texels do (CPZ2: INK_MASKED sprites land on it)
    w[1] = (maskColor && rgb == (uint16)maskColor) ? 0xE6000000 : 0xE6000001;
    w[2] = 0x60000000 | colour;
    PS1FadeRecordColour(&w[2]);
    w[3] = (uint32)(uint16)sy << 16;
    w[4] = PS1_SCREEN_PX | (uint32)h << 16;
    w[5] = 0xE6000000;
    PS1ChainSpanEnd(w, 5, w + 6);
    g_ps1LayerSolidRuns = g_ps1LayerSolidRuns + 1;
}
} // namespace

// The frame start of the draw lists: this frame's clear, the tile CLUTs and the bank CLUT from the palette
// --- 3.5: the frame's CLUT uploads, recorded for FillScreen -------------------------------------------------------------
#define PS1_CLUT_CHUNK_HW 22 // halfwords per CLUT upload fragment (3 + 11 words = 14), as PS1ChainReserveRow's
#define PS1_CLUT_RECS   320 // upload chunks a frame: tile CLUTs + the bank (12 chunks) + sprite CLUTs, and restores (9b.3: a full set
                            // is up to ~70 + 12 + 189 = 271 rows; 192 lost records, so a grey pause re-uploaded every CLUT each frame)
#define PS1_COLOUR_RECS 256 // direct colour words a frame (flat rectangles, one-colour runs)
struct PS1ClutRec {
    uint16_t *data;
    uint16 n;
};
PS1ClutRec s_clutRecs[PS1_CLUT_RECS];
int32 s_clutRecCount = 0;
struct PS1ColourRec {
    uint32_t *w; // a colour word, or (n > 0) the first fragment header of a span of n flat rectangles (3.6b)
    uint16 n;
    uint8 semi;
};
PS1ColourRec s_colourRecs[PS1_COLOUR_RECS];
int32 s_colourRecCount = 0;
bool s_ps1Faded        = false;
extern "C" {
volatile uint32 g_ps1FadeCalls    = 0; // FillScreens that blended something (total)
volatile uint32 g_ps1FadeOpaque   = 0; // 4.2e: FillScreens at full alpha drawn as one opaque rectangle (total)
volatile uint32 g_ps1FadeRestores = 0; // unfaded CLUT uploads after a fade (total)
volatile uint32 g_ps1FadeOverflow = 0; // FillScreens that met more CLUT / colour words than the tables hold (must stay 0)
volatile uint32 g_ps1TintGrey     = 0; // 4.3: pause grey tints (total)
}
bool s_fadeRecsLost = false; // this frame recorded fewer words than it drew (a FillScreen would miss some)

// A CLUT row through reserved upload chunks (PS1ChainReserveRow) whose data is recorded
void PS1ClutRow(const uint16 *row, int32 n, int16 x, int16 y)
{
    uint16_t *data[16];
    uint16_t len[16];
    int32 k = PS1ChainReserveRow(n, x, y, data, len, 16), off = 0;
    for (int32 i = 0; i < k; ++i) {
        memcpy(data[i], row + off, len[i] * 2);
        off += len[i];
        if (s_clutRecCount < PS1_CLUT_RECS)
            s_clutRecs[s_clutRecCount].data = data[i], s_clutRecs[s_clutRecCount++].n = len[i];
        else
            s_fadeRecsLost = true;
    }
}

// Every CLUT from the live palette: the tiles' 4-bit ones, bank 0's 256 entries (8-bit tiles / sprites), the atlas's
void PS1ClutUploadAll()
{
    const PS1TileClut *cl = PS1TileCluts();
    uint16 row[16];
    for (int32 c = 0; cl && c < PS1TileClutCount(); ++c) {
        row[0] = 0; // index 0: transparent
        for (int32 i = 1; i < 16; ++i) row[i] = cl[c].map[i] ? PS1Colour(fullPalette[0][cl[c].map[i]]) : 0; // padding: transparent
        PS1ClutRow(row, 16, (int16_t)cl[c].x, (int16_t)cl[c].y);
    }
    static uint16 s_bank[256];
    s_bank[0] = 0;
    for (int32 i = 1; i < 256; ++i) s_bank[i] = PS1Colour(fullPalette[0][i]);
    PS1ClutRow(s_bank, 256, 0, 240);
    const PS1AtlasClut *ac = PS1AtlasCluts(); // the sprite atlas's 4-bit CLUTs, the same way (3.4)
    for (int32 c = 0; ac && c < PS1AtlasClutCount(); ++c) {
        row[0] = 0;
        for (int32 i = 1; i < 16; ++i) row[i] = ac[c].map[i] ? PS1Colour(fullPalette[0][ac[c].map[i]]) : 0;
        PS1ClutRow(row, 16, (int16_t)ac[c].x, (int16_t)ac[c].y);
    }
    PS1ChainFlushCache(); // the GPU's CLUT cache may hold older colours
}

// --- 3.3e: CLUTs uploaded where the palette changed ---------------------------------------------------------------------
// The CLUTs in VRAM stay from frame to frame: a frame uploads only those whose palette entries changed since the last
// upload (bank 0 compared with the copy the uploads came from; GHZ changes 8 entries a frame). Everything is uploaded
// after VRAM changed outside the chains (g_ps1VramGen: loads, immediate uploads), after a new CLUT set, and after a frame
// that faded them. A FillScreen needs every CLUT at the frame's head, before the draws that used them (3.5a): a slot
// chained at the frame start takes the full set then, built from the same copy and faded with the rest.
#define PS1_CLUT_MASKS 128 // CLUTs whose palette buckets are known (more: always uploaded)
uint16 s_palRaw[256];      // bank 0 as the CLUTs in VRAM show it (RGB565)
bool s_palRow0Swapped = false; // 9f: row 240 holds a per-draw copy (PS1BankClut, bank 0) until restored
bool s_clutRow0Full    = false; // 9f: the next frame start uploads row 240 whole
uint16 s_pal16[256];       // the same as PS1 colours; [0] = 0 (transparent)
uint32 s_clutBuckets[PS1_CLUT_MASKS][2]; // per CLUT (tiles, then atlas): bit b of word w = an index in [128w + 4b, + 4) (3.3j)
const void *s_clutSetTiles = NULL, *s_clutSetAtlas = NULL;
int32 s_clutSetCounts   = -1;
uint32 s_clutGen        = 0;
int32 s_clutMask        = 0; // maskColor the CLUTs were built with (4.1b: a change uploads everything)
bool s_clutFullNext     = true; // the next frame uploads every CLUT
bool s_clutHeadFull     = false; // this frame's head uploads every CLUT
// 9b.3: the pause's grey tint. While frames keep tinting, VRAM keeps the grey CLUTs: a tinting frame greys only the CLUTs
// its head uploaded (the changed ones), not a full set spliced in every frame (SPZ2 overflowed the chain arena); the first
// frame that doesn't tint splices the full colour set at its head at the frame's end (PS1LayersFrameEnd)
bool s_greyHeld      = false; // VRAM's CLUTs are grey (the last frame tinted)
bool s_greyThisFrame = false;
// Draws after a fade / the tint take their colours back one CLUT at a time (PS1FadeRestoreSlot: the pause menu's sprites
// need a few rows, not every CLUT); the rows a tinting frame restored are uploaded again at the next frame's head, where
// that frame's tint greys them with the rest (s_regrey*)
#define PS1_RESTORE_WORDS 16 // 512 CLUTs (tiles + atlas)
uint32 s_restored[PS1_RESTORE_WORDS], s_regrey[PS1_RESTORE_WORDS];
bool s_restoredBank = false, s_regreyBank = false, s_restoredAll = false;
extern "C" {
volatile uint32 g_ps1ClutFullUploads = 0; // frames that uploaded every CLUT (total)
volatile uint32 g_ps1ClutRowUploads  = 0; // CLUT rows uploaded because their entries changed (total)
volatile uint32 g_ps1ClutRowsSkipped = 0; // 9.4c: rows whose buckets changed but not their entries (total)
volatile uint32 g_ps1ClutSplices     = 0; // full sets spliced in at the frame's head for a FillScreen (total)
volatile uint32 g_ps1ClutTestSplice  = 0; // GDB test: 1 = a fade doesn't make the next frame upload everything, so
                                          // every fading frame takes the splice (exact only while every frame fades)
}

inline void PS1ClutBucketsOf(const uint8 *map, uint32 *m)
{
    m[0] = m[1] = 0;
    for (int32 i = 1; i < 16; ++i)
        if (map[i])
            m[map[i] >> 7] |= 1u << ((map[i] >> 2) & 31);
}

// A 4-bit CLUT's 16 entries from the copy
inline void PS1ClutRowFrom(uint16 *row, const uint8 *map)
{
    row[0] = 0;
    for (int32 i = 1; i < 16; ++i) row[i] = map[i] ? s_pal16[map[i]] : 0;
}

// The frame start's CLUTs: every one (full) or those whose entries changed
void PS1ClutUploadChanged()
{
    g_ps1PalDiffs = 0; // s_palRaw follows bank 0 again below (index 0, always transparent, is not counted)
    if (s_palRow0Swapped) { // a per-draw copy left in row 240 (9f): all of it again, from the copy updated below
        s_palRow0Swapped = false;
        s_clutRow0Full   = true;
    }
    const PS1TileClut *cl  = PS1TileCluts();
    const PS1AtlasClut *ac = PS1AtlasCluts();
    int32 nt = cl ? PS1TileClutCount() : 0, na = ac ? PS1AtlasClutCount() : 0;
    uint16 row[16];
    if ((s_clutFullNext && !g_ps1ClutTestSplice) || g_ps1VramGen != s_clutGen || maskColor != s_clutMask || cl != s_clutSetTiles || ac != s_clutSetAtlas || (nt | na << 16) != s_clutSetCounts) {
        for (int32 i = 0; i < 256; ++i) s_palRaw[i] = fullPalette[0][i], s_pal16[i] = i ? PS1Colour(s_palRaw[i]) : 0;
        for (int32 c = 0; c < nt + na && c < PS1_CLUT_MASKS; ++c) PS1ClutBucketsOf(c < nt ? cl[c].map : ac[c - nt].map, s_clutBuckets[c]);
        s_clutSetTiles = cl, s_clutSetAtlas = ac, s_clutSetCounts = nt | na << 16, s_clutGen = g_ps1VramGen, s_clutMask = maskColor;
        s_clutFullNext = false;
        for (int32 c = 0; c < nt; ++c) PS1ClutRowFrom(row, cl[c].map), PS1ClutRow(row, 16, (int16_t)cl[c].x, (int16_t)cl[c].y);
        PS1ClutRow(s_pal16, 256, 0, 240);
        for (int32 c = 0; c < na; ++c) PS1ClutRowFrom(row, ac[c].map), PS1ClutRow(row, 16, (int16_t)ac[c].x, (int16_t)ac[c].y);
        s_clutHeadFull       = true;
        s_clutRow0Full       = false; // (row 240 just went up whole)
        g_ps1ClutFullUploads = g_ps1ClutFullUploads + 1;
        return;
    }
    s_clutHeadFull = false;
    if (s_greyHeld) { // 9b.3: the rows the last (tinting) frame took back to colour after its tint
        for (int32 c = 0; c < nt + na && c < PS1_RESTORE_WORDS * 32; ++c)
            if ((s_regrey[c >> 5] >> (c & 31)) & 1)
                PS1ClutRowFrom(row, c < nt ? cl[c].map : ac[c - nt].map),
                    PS1ClutRow(row, 16, (int16_t)(c < nt ? cl[c].x : ac[c - nt].x), (int16_t)(c < nt ? cl[c].y : ac[c - nt].y));
        if (s_regreyBank)
            PS1ClutRow(s_pal16, 256, 0, 240);
    }
    uint32 changed[2] = { 0, 0 };
    uint32 exact[8]   = { 0, 0, 0, 0, 0, 0, 0, 0 }; // 9.4c: the entries themselves (a bucket holds 4)
    int32 lo = 256, hi = -1;
    for (int32 i = 1; i < 256; ++i) {
        uint16 c = fullPalette[0][i];
        if (c != s_palRaw[i]) {
            s_palRaw[i] = c, s_pal16[i] = PS1Colour(c);
            changed[i >> 7] |= 1u << ((i >> 2) & 31);
            exact[i >> 5] |= 1u << (i & 31);
            if (i < lo)
                lo = i;
            hi = i;
        }
    }
    if (!(changed[0] | changed[1])) {
        if (s_clutRow0Full)
            PS1ClutRow(s_pal16, 256, 0, 240), s_clutRow0Full = false;
        return;
    }
    int32 rows = 1;
    for (int32 c = 0; c < nt + na; ++c) {
        if (c < PS1_CLUT_MASKS && !((s_clutBuckets[c][0] & changed[0]) | (s_clutBuckets[c][1] & changed[1])))
            continue;
        const PS1TileClut *t = c < nt ? &cl[c] : NULL;
        const PS1AtlasClut *a = c < nt ? NULL : &ac[c - nt];
        // 9.4c: only a CLUT one of whose own entries changed (SPZ1: ~129 rows a frame from the buckets' neighbours); an
        // unchanged one already holds these colours in VRAM
        const uint8 *map = t ? t->map : a->map;
        bool hit = false;
        for (int32 i = 1; i < 16 && !hit; ++i)
            hit = map[i] && ((exact[map[i] >> 5] >> (map[i] & 31)) & 1);
        if (!hit) {
            g_ps1ClutRowsSkipped = g_ps1ClutRowsSkipped + 1;
            continue;
        }
        PS1ClutRowFrom(row, map);
        PS1ClutRow(row, 16, (int16_t)(t ? t->x : a->x), (int16_t)(t ? t->y : a->y));
        ++rows;
    }
    lo &= ~1, hi |= 1; // an even count of halfwords (two per word)
    if (s_clutRow0Full)
        lo = 0, hi = 255, s_clutRow0Full = false;
    PS1ClutRow(s_pal16 + lo, hi - lo + 1, (int16_t)lo, 240);
    g_ps1ClutRowUploads = g_ps1ClutRowUploads + rows;
}

// 9f: SetPaletteEntry on bank 0 (RSDK/Graphics/Palette.hpp). The PC applies a palette write to the draws after it;
// here the CLUTs come from the palette at the draw phase's start (PS1ClutUploadChanged). While some entries differ from
// those (CutsceneHBH sets each Heavy's colours around its draw, then restores them), sprites take a per-draw copy
// (PS1BankClut with bank 0, uploaded in the chain right before them); when the last difference is undone, a copy left in
// row 240 (an 8-bit frame's) goes back to the uploaded colours, in the chain, before any later draw
void RSDK::PS1PaletteWrite(uint8 index, uint16 value)
{
    uint16 old = fullPalette[0][index];
    if (!index || old == value)
        return;
    int32 was = old != s_palRaw[index], is = value != s_palRaw[index];
    g_ps1PalDiffs = g_ps1PalDiffs + is - was;
    if (!g_ps1PalDiffs && s_palRow0Swapped) {
        s_palRow0Swapped = false;
        PS1ClutRow(s_pal16, 256, 0, 240);
    }
}
void RSDK::PS1PaletteRow0Swapped() { s_palRow0Swapped = true; }
void RSDK::PS1PaletteRow0Restore()
{
    if (g_ps1PalDiffs) { // the bank's own draws take copies now (PS1BankClut): put back when the palette is
        s_palRow0Swapped = true;
        return;
    }
    s_palRow0Swapped = false;
    PS1ClutRow(s_pal16, 256, 0, 240);
}

// A FillScreen in a frame whose head uploaded only the changed CLUTs: every CLUT, from the same copy, linked in at the
// frame's slot (before the draws) and recorded, so the fade rewrites them with the rest
void PS1ClutSpliceFull()
{
    const PS1TileClut *cl  = PS1TileCluts();
    const PS1AtlasClut *ac = PS1AtlasCluts();
    int32 nt = cl ? PS1TileClutCount() : 0, na = ac ? PS1AtlasClutCount() : 0;
    int32 words = (nt + na) * 12 + (256 / PS1_CLUT_CHUNK_HW + 1) * (4 + PS1_CLUT_CHUNK_HW / 2);
    uint32_t *first = PS1ChainHasSlot() ? PS1ChainDetachedBegin(words) : NULL;
    if (!first) {
        s_fadeRecsLost = true;
        return;
    }
    uint32_t *w = first, *hdr = NULL;
    int32 hdrWords = 0;
    for (int32 c = -1; c < nt + na; ++c) { // -1: the bank's 256 entries
        uint16 row[16];
        const uint16 *src = s_pal16;
        int32 n = 256, x = 0, y = 240;
        if (c >= 0) {
            const uint8 *map = c < nt ? cl[c].map : ac[c - nt].map;
            PS1ClutRowFrom(row, map);
            src = row, n = 16, x = c < nt ? cl[c].x : ac[c - nt].x, y = c < nt ? cl[c].y : ac[c - nt].y;
        }
        for (int32 off = 0; off < n; off += PS1_CLUT_CHUNK_HW) {
            int32 k = n - off < PS1_CLUT_CHUNK_HW ? n - off : PS1_CLUT_CHUNK_HW;
            if (hdr)
                *hdr = (uint32_t)hdrWords << 24 | ((uintptr_t)w & 0xFFFFFF);
            hdr      = w;
            w[1]     = 0xA0000000; // GP0 A0h: CPU to VRAM (psx-spx "GPU Memory Transfer Commands")
            w[2]     = (uint32_t)(uint16)(x + off) | (uint32_t)(uint16)y << 16;
            w[3]     = (uint32_t)k | 1u << 16;
            memcpy(&w[4], src + off, k * 2);
            if (s_clutRecCount < PS1_CLUT_RECS)
                s_clutRecs[s_clutRecCount].data = (uint16_t *)&w[4], s_clutRecs[s_clutRecCount++].n = (uint16)k;
            else
                s_fadeRecsLost = true;
            hdrWords = 3 + k / 2;
            w += 1 + hdrWords;
        }
    }
    if (!PS1ChainSplice(first, hdr, hdrWords, w)) {
        s_fadeRecsLost = true;
        return;
    }
    s_clutHeadFull   = true;
    g_ps1ClutSplices = g_ps1ClutSplices + 1;
}

// 9b.3: the end of the frame's draw lists: a frame that tinted leaves VRAM's CLUTs grey for the next one; the first frame
// that doesn't splices the colour set in at its head (before its draws), or the next frame uploads it (the splice failed)
void RSDK::PS1LayersFrameEnd()
{
    if (s_greyThisFrame) {
        s_greyHeld = !s_fadeRecsLost && !s_restoredAll;
        if (!s_greyHeld)
            s_clutFullNext = true; // some CLUT this frame stayed coloured: the next frame starts from a full set
        memcpy(s_regrey, s_restored, sizeof(s_regrey));
        s_regreyBank = s_restoredBank;
    }
    else if (s_greyHeld) {
        s_greyHeld = false;
        if (!s_clutHeadFull) {
            s_fadeRecsLost = false;
            PS1ClutSpliceFull();
            if (s_fadeRecsLost)
                s_clutFullNext = true;
        }
    }
    s_greyThisFrame = false;
}

// The frame start of the draw lists: this frame's clear, the CLUTs whose palette entries changed (3.3e)
void RSDK::PS1LayersFrameBegin()
{
    ++s_layerFrame; // 7b.7: this frame's extra bank is uploaded at its first use
    s_clutRecCount = s_colourRecCount = 0; // before the frame start: its clear may record a colour word (4.1b)
    s_ps1Faded = s_fadeRecsLost = false;
    memset(s_restored, 0, sizeof(s_restored));
    s_restoredBank = s_restoredAll = false;
    PS1FrameStart(true);
    g_ps1FrameBufferY = (uint32)PS1ChainBufferY();
    g_ps1LayerBands = g_ps1LayerPieces = g_ps1LayerHbl = g_ps1LayerSolidRuns = g_ps1LayerRowTables = g_ps1LayerUniform = 0;
    g_ps1RotoQuads = g_ps1RotoHbl = 0;
    for (int32 l = 0; l < 8; ++l) g_ps1LayerHblBy[l] = 0;
    PS1SpritesFrameBegin();
    g_ps1LayerT[0] = g_ps1LayerT[1] = g_ps1LayerT[2] = g_ps1LayerT[3] = 0;
    PS1ChainSlot();
    PS1ClutUploadChanged();
    PS1ChainFlushCache(); // the GPU's CLUT cache may hold older colours; also keeps the slot from being the chain's tail
}

void RSDK::PS1UploadClut(const uint16 *row, int32 n, int16 x, int16 y)
{
    PS1ClutRow(row, n, x, y);
    PS1ChainFlushCache();
}

// draws under a palette bank other than 0 not done yet (tiles, layers; total): global, declared in no header (GDB)
extern "C" {
volatile uint32 g_ps1BankTodo = 0;
}

#if PS1_DEBUG_BIGRAM
extern "C" {
volatile uint32 g_ps1FadeSpanChecked = 0, g_ps1FadeSpanBad = 0; // 3.6b evidence: spans walked as the fade would, wrong words
}
#endif
void RSDK::PS1FadeRecordSpan(uint32_t *firstHeader, int32 rects, bool semi)
{
#if PS1_DEBUG_BIGRAM // the fade's walk must meet exactly the rectangles' command words (GP0 60h / 62h)
    {
        uint32_t *f = firstHeader;
        for (int32 left = rects; left > 0;) {
            uint32_t *p = f + 1 + semi;
            int32 k     = left < 4 ? left : 4;
            for (int32 j = 0; j < k; ++j)
                if ((p[3 * j] >> 24) != (semi ? 0x62u : 0x60u))
                    g_ps1FadeSpanBad = g_ps1FadeSpanBad + 1;
            left -= k;
            f = p + 3 * k;
        }
        g_ps1FadeSpanChecked = g_ps1FadeSpanChecked + 1;
    }
#endif
    if (s_colourRecCount < PS1_COLOUR_RECS) {
        PS1ColourRec &r = s_colourRecs[s_colourRecCount++];
        r.w = firstHeader, r.n = (uint16)rects, r.semi = semi;
    }
    else
        s_fadeRecsLost = true;
}

void RSDK::PS1FadeRecordColour(uint32_t *word)
{
    if (s_colourRecCount < PS1_COLOUR_RECS) {
        PS1ColourRec &r = s_colourRecs[s_colourRecCount++];
        r.w = word, r.n = 0, r.semi = 0;
    }
    else
        s_fadeRecsLost = true;
}

void RSDK::PS1FadeRestore()
{
    if (s_ps1Faded) {
        s_ps1Faded = false;
        PS1ClutUploadAll();
        s_restoredAll = true;
        g_ps1FadeRestores = g_ps1FadeRestores + 1;
    }
}

// 9b.3: a sprite after a fade / the tint: its own CLUT back to colour (atlas slot, or the bank's 256 entries), once a frame
void RSDK::PS1FadeRestoreSlot(uint32 slot)
{
    if (!s_ps1Faded || s_restoredAll)
        return;
    if (slot == PS1_ATLAS_BANK_CLUT) {
        if (!s_restoredBank) {
            static uint16 s_bank[256];
            s_bank[0] = 0;
            for (int32 i = 1; i < 256; ++i) s_bank[i] = PS1Colour(fullPalette[0][i]);
            PS1ClutRow(s_bank, 256, 0, 240);
            PS1ChainFlushCache();
            s_restoredBank = true;
        }
        return;
    }
    const PS1AtlasClut *ac = PS1AtlasCluts();
    int32 nt = PS1TileCluts() ? PS1TileClutCount() : 0, c = nt + (int32)slot;
    if (!ac || slot >= PS1AtlasClutCount() || c >= PS1_RESTORE_WORDS * 32) {
        PS1FadeRestore(); // past the bitmap: every CLUT, as before
        return;
    }
    if ((s_restored[c >> 5] >> (c & 31)) & 1)
        return;
    uint16 row[16];
    row[0] = 0;
    for (int32 i = 1; i < 16; ++i) row[i] = ac[slot].map[i] ? PS1Colour(fullPalette[0][ac[slot].map[i]]) : 0;
    PS1ClutRow(row, 16, (int16_t)ac[slot].x, (int16_t)ac[slot].y);
    PS1ChainFlushCache();
    s_restored[c >> 5] |= 1u << (c & 31);
    g_ps1FadeRestores = g_ps1FadeRestores + 1;
}

// FillScreen's arithmetic per 5-bit channel (the PC: blendLookupTable[0x20 * a + c] = a * c >> 8; the fill colour's
// channels c8 >> 3; green is the 565 value's top 5 bits, as the PS1 colours are): c' = (c * (255 - a) >> 8) + (f * a >> 8)
extern "C" uint32_t g_ps1VersionFade[4]; // ps1/version_text.cpp: the title's build stamp fades with the frame
void RSDK::PS1FadeScreen(uint32 color, int32 alphaR, int32 alphaG, int32 alphaB)
{
    g_ps1VersionFade[0] = color, g_ps1VersionFade[1] = (uint32)alphaR, g_ps1VersionFade[2] = (uint32)alphaG,
    g_ps1VersionFade[3] = (uint32)alphaB;
    int32 fr = (int32)((color >> 16) & 0xFF) >> 3, fg = (int32)((color >> 8) & 0xFF) >> 3, fb = (int32)(color & 0xFF) >> 3;
    int32 cr = fr * alphaR >> 8, cg = fg * alphaG >> 8, cb = fb * alphaB >> 8;
    int32 kr = 0xFF - alphaR, kg = 0xFF - alphaG, kb = 0xFF - alphaB;
    if (!kr && !kg && !kb) { // 4.2e: every pixel so far becomes (cr, cg, cb): one opaque rectangle (PS1FillOpaque)
        PS1FillOpaque((uint32)cr, (uint32)cg, (uint32)cb);
        g_ps1FadeOpaque = g_ps1FadeOpaque + 1;
        return;
    }
    if (!s_clutHeadFull)
        PS1ClutSpliceFull(); // every CLUT at the frame's head, so all of them fade (3.3e)
    s_clutFullNext = true;   // VRAM keeps faded CLUTs after this frame
    for (int32 r = 0; r < s_clutRecCount; ++r) { // CLUT entries: BGR555 | STP (0: transparent, stays)
        uint16_t *e = s_clutRecs[r].data;
        for (int32 i = 0; i < s_clutRecs[r].n; ++i) {
            uint32 c = e[i];
            if (!c)
                continue;
            uint32 R = ((c & 31) * kr >> 8) + cr, G = (((c >> 5) & 31) * kg >> 8) + cg, B = (((c >> 10) & 31) * kb >> 8) + cb;
            e[i]     = (uint16_t)(0x8000 | R | G << 5 | B << 10);
        }
    }
    for (int32 r = 0; r < s_colourRecCount; ++r) { // direct colours: 8-bit fields, the GPU keeps their top 5 bits
        PS1ColourRec &rec = s_colourRecs[r];
        if (rec.n) { // a span of flat rectangles: each fragment's rectangles after its header (and texpage word)
            uint32_t *f = rec.w;
            for (int32 left = rec.n; left > 0;) {
                uint32_t *p = f + 1 + rec.semi;
                int32 k     = left < 4 ? left : 4;
                for (int32 j = 0; j < k; ++j) {
                    uint32 w = p[3 * j];
                    uint32 R = (((w & 0xFF) >> 3) * kr >> 8) + cr, G = ((((w >> 8) & 0xFF) >> 3) * kg >> 8) + cg;
                    uint32 B = ((((w >> 16) & 0xFF) >> 3) * kb >> 8) + cb;
                    p[3 * j] = (w & 0xFF000000) | R << 3 | G << 11 | B << 19;
                }
                left -= k;
                f = p + 3 * k;
            }
            continue;
        }
        uint32 w = *rec.w;
        uint32 R = (((w & 0xFF) >> 3) * kr >> 8) + cr, G = ((((w >> 8) & 0xFF) >> 3) * kg >> 8) + cg;
        uint32 B = ((((w >> 16) & 0xFF) >> 3) * kb >> 8) + cb;
        *rec.w = (w & 0xFF000000) | R << 3 | G << 11 | B << 19;
    }
    uint32 clear = PS1FrameClearColour();
    if (clear != 0xFFFFFFFF) {
        uint32 R = (((clear & 0xFF) >> 3) * kr >> 8) + cr, G = ((((clear >> 8) & 0xFF) >> 3) * kg >> 8) + cg;
        uint32 B = ((((clear >> 16) & 0xFF) >> 3) * kb >> 8) + cb;
        PS1FrameClearSet(R << 3 | G << 11 | B << 19);
    }
    s_ps1Faded     = true;
    g_ps1FadeCalls = g_ps1FadeCalls + 1;
    if (s_fadeRecsLost)
        g_ps1FadeOverflow = g_ps1FadeOverflow + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(8, 0, 0, 0, (uint8)((color >> 16) & 0xFF), (uint8)((color >> 8) & 0xFF), (uint16)(color & 0xFF), alphaR, alphaG, alphaB, 0);
#endif
}

// 4.3: the pause's tint (Plus: PauseMenu_SetupTintTable): for an RGB565 pixel, r = (0x20F * r5 + 23) >> 6, g = (0x103 * g6
// + 33) >> 6, b = (0x20F * b5 + 23) >> 6, grey = min(((r + g + b) << 8) / 680, 255), stored as RGB565 (grey >> 3, >> 2,
// >> 3): the PS1 keeps 5 bits a channel, so grey >> 3 each. Flat colours carry their 8-bit channels (the PC's 565 is
// exact from them); CLUT entries are BGR555 (green's low bit gone: taken as its top bit, approximate) and the host render
// counts the tint approximate
// the channels' 8-bit values as tables, and ((r + g + b) << 8) / 680 = (r + g + b) * 24673 >> 16 for every sum up to 765
// (checked on all of them): no divide (4.3)
const uint8 s_grey5To8[32] = { 0, 8, 16, 25, 33, 41, 49, 58, 66, 74, 82, 90, 99, 107, 115, 123, 132, 140, 148, 156, 165, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255 };
const uint8 s_grey6To8[64] = { 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101, 105, 109, 113, 117, 121, 125, 130, 134, 138, 142, 146, 150, 154, 158, 162, 166, 170, 174, 178, 182, 186, 190, 194, 198, 202, 206, 210, 215, 219, 223, 227, 231, 235, 239, 243, 247, 251, 255 };
__attribute__((always_inline)) inline uint32 PS1Grey5(uint32 r5, uint32 g6, uint32 b5)
{
    uint32 grey = (uint32)(s_grey5To8[r5] + s_grey6To8[g6] + s_grey5To8[b5]) * 24673 >> 16;
    return (grey > 255 ? 255 : grey) >> 3;
}
inline uint32 PS1GreyWord(uint32 w) // a GP0 colour word's 8-bit fields (0xBBGGRR) -> grey, the command byte kept
{
    uint32 v = PS1Grey5((w & 0xFF) >> 3, ((w >> 8) & 0xFF) >> 2, ((w >> 16) & 0xFF) >> 3) << 3;
    return (w & 0xFF000000) | v | v << 8 | v << 16;
}

void RSDK::PS1TintScreenGrey()
{
    if (!s_clutHeadFull && !s_greyHeld)
        PS1ClutSpliceFull(); // every CLUT at the frame's head, so all of them turn (3.3e); held: VRAM's are grey already
    s_greyThisFrame = true;
    for (int32 r = 0; r < s_clutRecCount; ++r) {
        uint16_t *e = s_clutRecs[r].data;
        for (int32 i = 0; i < s_clutRecs[r].n; ++i) {
            uint32 c = e[i];
            if (!c)
                continue;
            uint32 g5 = (c >> 5) & 31, v = PS1Grey5(c & 31, g5 << 1 | g5 >> 4, (c >> 10) & 31);
            e[i]      = (uint16_t)(0x8000 | v | v << 5 | v << 10);
        }
    }
    for (int32 r = 0; r < s_colourRecCount; ++r) {
        PS1ColourRec &rec = s_colourRecs[r];
        if (rec.n) {
            uint32_t *f = rec.w;
            for (int32 left = rec.n; left > 0;) {
                uint32_t *p = f + 1 + rec.semi;
                int32 k     = left < 4 ? left : 4;
                for (int32 j = 0; j < k; ++j) p[3 * j] = PS1GreyWord(p[3 * j]);
                left -= k;
                f = p + 3 * k;
            }
            continue;
        }
        *rec.w = PS1GreyWord(*rec.w);
    }
    uint32 clear = PS1FrameClearColour();
    if (clear != 0xFFFFFFFF)
        PS1FrameClearSet(PS1GreyWord(clear) & 0xFFFFFF);
    s_ps1Faded     = true;
    g_ps1TintGrey  = g_ps1TintGrey + 1;
    if (s_fadeRecsLost)
        g_ps1FadeOverflow = g_ps1FadeOverflow + 1;
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); // type 11: the grey tint (tools/gfx/layer_reference.py)
#endif
}

// The bands of a layer's lines [y1, y2) (3.3b), grouped per tile row: from the scanlines ProcessParallax filled, or
// (Direct, 3.3h) from the layer's lineScroll and each scroll info's X (xOf), the rows consecutive from scrollY on line 0
template <bool Direct>
__attribute__((always_inline)) inline void PS1ScanBands(TileLayer *layer, int32 y1, int32 y2, int32 widthPx, int32 heightPx,
                                                        const uint8 *solid, ScanlineInfo *sl, const int16 *xOf, int32 scrollY,
                                                        bool oneBank)
{
    PS1Band group[TILE_SIZE]; // the bands of the current tile row (one palette bank)
    int32 groupCount = 0;
#define PS1_FLUSH_GROUP()                                                                                              \
    do {                                                                                                               \
        if (groupCount == 1)                                                                                           \
            DrawBand(layer, group[0].sy, group[0].h, group[0].x, group[0].ly, group[0].bank);                          \
        else if (groupCount > 1)                                                                                       \
            DrawRowBands(layer, group, groupCount);                                                                    \
        groupCount = 0;                                                                                                \
    } while (0)
    for (int32 cy = y1; cy < y2;) {
        int32 x, ly;
        if constexpr (Direct) {
            ly = scrollY + cy; // ProcessParallax's rows: scrollY on line 0, then one further per line, wrapping
            while (ly >= heightPx)
                ly -= heightPx;
            x = xOf[layer->lineScroll ? layer->lineScroll[ly] : layer->ps1LineScrollOne]; // 7b: no table: one value
        }
        else {
            x = FROM_FIXED(sl->position.x);
            if (x >= widthPx)
                x -= widthPx;
            else if (x < 0)
                x += widthPx;
            ly = FROM_FIXED(sl->position.y);
            if (ly >= heightPx || ly < 0) { // as the software loop: the layout row wraps with the layer height
                ly %= heightPx;
                if (ly < 0)
                    ly += heightPx;
            }
        }
        uint8 bank   = gfxLineBuffer[cy];
        int32 h      = 1;
        int32 rowEnd = TILE_SIZE - (ly & 15);
        while (h < rowEnd && cy + h < y2) {
            if constexpr (Direct) { // rows ly + h stay in this tile row: no wrap, always consecutive
                if ((layer->lineScroll && xOf[layer->lineScroll[ly + h]] != x) || (!oneBank && gfxLineBuffer[cy + h] != bank))
                    break;
            }
            else {
                int32 nx = FROM_FIXED(sl[h].position.x);
                if (nx >= widthPx)
                    nx -= widthPx;
                else if (nx < 0)
                    nx += widthPx;
                if (nx != x || FROM_FIXED(sl[h].position.y) != ly + h || (!oneBank && gfxLineBuffer[cy + h] != bank))
                    break;
            }
            ++h;
        }
        if (solid && solid[ly]) {
            // one-colour lines from here (at any X, this bank): a rectangle when the run covers at least the band's
            // lines (so no band is split); inside a band they are drawn with its tiles
            uint8 c = solid[ly];
            int32 r = 1;
            while (cy + r < y2 && (oneBank || gfxLineBuffer[cy + r] == bank)) {
                int32 nly;
                if constexpr (Direct) {
                    nly = ly + r;
                    while (nly >= heightPx)
                        nly -= heightPx;
                }
                else {
                    nly = FROM_FIXED(sl[r].position.y);
                    if (nly >= heightPx || nly < 0) {
                        nly %= heightPx;
                        if (nly < 0)
                            nly += heightPx;
                    }
                }
                if (solid[nly] != c)
                    break;
                ++r;
            }
            if (r >= h) {
                PS1_FLUSH_GROUP();
                DrawSolid(cy, r, c, bank);
                cy += r;
                if constexpr (!Direct)
                    sl += r;
                continue;
            }
        }
        uint16 tb = PS1T();
        if (groupCount && ((group[0].ly >> 4) != (ly >> 4) || group[0].bank != bank || groupCount == TILE_SIZE))
            PS1_FLUSH_GROUP();
        PS1Band &g = group[groupCount++];
        g.sy = (int16)cy, g.h = (uint8)h, g.bank = bank, g.x = x, g.ly = ly;
#if PS1_PROFILE
        g_ps1LayerT[0] -= (uint16)(PS1T() - tb); // scan time = total - bands
#endif
        cy += h;
        if constexpr (!Direct)
            sl += h;
    }
    {
        uint16 tb = PS1T();
        PS1_FLUSH_GROUP();
#if PS1_PROFILE
        g_ps1LayerT[0] -= (uint16)(PS1T() - tb);
#endif
    }
#undef PS1_FLUSH_GROUP
}

// 4.1c: DrawLayerBasic: the clip rectangle's lines from the first line's scroll position (the rows consecutive from it),
// every line with palette bank 0 (the PC draws with fullPalette[0] whatever gfxLineBuffer holds)
__attribute__((optimize("O2"))) void RSDK::PS1DrawLayerBasic(TileLayer *layer)
{
    int32 y1 = currentScreen->clipBound_Y1, y2 = currentScreen->clipBound_Y2;
    if (!layer->xsize || !layer->ysize || currentScreen->clipBound_X1 >= currentScreen->clipBound_X2 || y1 >= y2)
        return;
    if (!TileTable()) { // 4.2d: no tile images loaded (a scene before its `make gfx`): nothing to draw from
        g_ps1LayerNoTiles = g_ps1LayerNoTiles + 1;
        return;
    }
    PS1LayerDebugHook((uint32)(layer - tileLayers));
    PS1FadeRestore();
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(0, 0, 0, 0, 0, 0, 0, (int32)(layer - tileLayers), 0, 0, 0);
#endif
    uint32 t0     = PS1Hblanks();
    int32 widthPx = layer->xsize << 4, heightPx = layer->ysize << 4;
    int32 x       = FROM_FIXED(scanlines[y1].position.x) % widthPx, y0 = FROM_FIXED(scanlines[y1].position.y);
    x             = x < 0 ? x + widthPx : x;
    PS1Emit e;
    EmitOpen(e);
    e.page = 0xFFFFFFFF;
    for (int32 cy = y1; cy < y2;) {
        int32 ly = (y0 + cy - y1) % heightPx;
        ly       = ly < 0 ? ly + heightPx : ly;
        int32 h  = TILE_SIZE - (ly & 15);
        if (h > y2 - cy)
            h = y2 - cy;
        DrawBandInto(e, layer, cy, h, x, ly, 0);
        cy += h;
    }
    EmitClose(e);
    uint32 dt     = PS1HblanksSince(t0);
    g_ps1LayerHbl = g_ps1LayerHbl + dt;
    g_ps1LayerHblBy[(layer - tileLayers) & 7] += dt;
}

__attribute__((optimize("O2"))) void RSDK::PS1DrawLayerHScroll(TileLayer *layer)
{
    if (!layer->xsize || !layer->ysize)
        return;
    if (!TileTable()) { // 4.2d: as PS1DrawLayerBasic
        g_ps1LayerNoTiles = g_ps1LayerNoTiles + 1;
        return;
    }
    PS1LayerDebugHook((uint32)(layer - tileLayers));
    PS1FadeRestore();
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(0, 0, 0, 0, 0, 0, 0, (int32)(layer - tileLayers), 0, 0, 0);
#endif
    uint32 t0     = PS1Hblanks();
    uint16 tl     = PS1T();
    int32 widthPx = layer->xsize << 4, heightPx = layer->ysize << 4;
    int32 y1 = currentScreen->clipBound_Y1, y2 = currentScreen->clipBound_Y2;
    ScanlineInfo *sl    = &scanlines[y1];
    for (int32 y = y1; y < y2; ++y) // 7b.7: the frame's extra bank's CLUTs, before any span opens
        if (gfxLineBuffer[y]) {
            PS1BankTiles(gfxLineBuffer[y]);
            break;
        }
    // 3.3i: every line of [y1, y2) on one palette bank (GHZ: no water): the bands skip the per-line bank checks. 3.3k:
    // the answer is kept until gfxLineBuffer is written again (SetActivePalette, the scene load: ps1LineBankGen)
    static uint32 s_oneBankGen = 0xFFFFFFFF;
    static int32 s_oneBankY1 = -1, s_oneBankY2 = -1;
    static bool s_oneBank = false;
    if (ps1LineBankGen != s_oneBankGen || y1 != s_oneBankY1 || y2 != s_oneBankY2) {
        s_oneBank = true;
        for (int32 y = y1 + 1; y < y2; ++y)
            if (gfxLineBuffer[y] != gfxLineBuffer[y1]) {
                s_oneBank = false;
                break;
            }
        s_oneBankGen = ps1LineBankGen, s_oneBankY1 = y1, s_oneBankY2 = y2;
    }
    bool oneBank = s_oneBank;
    if (layer->ps1Uniform) {
        // 3.3c: one X for every line, rows consecutive from ps1UniformY: bands are the tile rows (split where the
        // palette bank changes), as the scan below would find them
        int32 x = layer->ps1UniformX;
        x       = x >= widthPx ? x - widthPx : x < 0 ? x + widthPx : x;
        PS1Emit e; // one span for the layer (3.3j)
        EmitOpen(e);
        e.page = 0xFFFFFFFF;
        for (int32 cy = y1; cy < y2;) {
            int32 ly   = (layer->ps1UniformY + cy) % heightPx;
            uint8 bank = gfxLineBuffer[cy];
            int32 h = TILE_SIZE - (ly & 15);
            if (h > y2 - cy)
                h = y2 - cy;
            for (int32 k = 1; !oneBank && k < h; ++k)
                if (gfxLineBuffer[cy + k] != bank) {
                    h = k;
                    break;
                }
            DrawBandInto(e, layer, cy, h, x, ly, bank);
            cy += h;
        }
        EmitClose(e);
        PS1_LT_ADD(0, PS1T() - tl);
        uint32 dt         = PS1HblanksSince(t0);
        g_ps1LayerHbl     = g_ps1LayerHbl + dt;
        g_ps1LayerHblBy[(layer - tileLayers) & 7] += dt;
        g_ps1LayerUniform = g_ps1LayerUniform + 1;
        return;
    }
    const uint8 *solid = PS1TileSolidLines((int32)(layer - tileLayers), heightPx);
    if (layer->ps1Direct) {
        // 3.3h: every line's X is its scroll info's (no deformation): the same bands from lineScroll, no scanlines
        int16 xOf[0x80];
        for (int32 i = 0; i < layer->scrollInfoCount && i < 0x80; ++i) {
            int32 x = FROM_FIXED(layer->scrollInfo[i].tilePos);
            xOf[i]  = (int16)(x >= widthPx ? x - widthPx : x < 0 ? x + widthPx : x);
        }
        PS1ScanBands<true>(layer, y1, y2, widthPx, heightPx, solid, NULL, xOf, layer->ps1UniformY, oneBank);
        g_ps1LayerDirect = g_ps1LayerDirect + 1;
    }
    else {
        PS1ScanBands<false>(layer, y1, y2, widthPx, heightPx, solid, sl, NULL, 0, oneBank);
    }
    PS1_LT_ADD(0, PS1T() - tl);
    uint32 dt     = PS1HblanksSince(t0);
    g_ps1LayerHbl = g_ps1LayerHbl + dt;
    g_ps1LayerHblBy[(layer - tileLayers) & 7] += dt;
}
