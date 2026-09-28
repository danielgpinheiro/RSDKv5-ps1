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
#include "../../../../ps1/video.hh" // PS1Hblanks

namespace RSDK
{
extern "C" {
volatile uint32 g_ps1LayerBands  = 0; // bands drawn (this frame)
volatile uint32 g_ps1LayerPieces = 0; // tile pieces chained (this frame)
volatile uint32 g_ps1LayerMisses = 0; // tiles with no image in VRAM (must stay 0)
volatile uint32 g_ps1LayerHbl    = 0; // hblanks spent drawing layers (this frame)
volatile uint32 g_ps1LayerSolidRuns = 0; // one-colour line runs drawn as rectangles (this frame)
volatile uint32 g_ps1LayerRowTables = 0; // tile rows drawn from a column table (this frame)
__attribute__((noinline)) void PS1LayerDebugHook(uint32 index) { __asm__ volatile("" ::"r"(index)); } // GDB: each layer draw
volatile uint32 g_ps1FrameBufferY = 0;          // the buffer this frame draws into (0 / 256; evidence scripts)
volatile uint32 g_ps1LayerT[4] = { 0, 0, 0, 0 }; // PROFILE=1 (ticks of 8 cycles a frame): scan, gather, sprites, quads
}
} // namespace RSDK

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
#define PS1_LAYER_PAGES 8
#define PS1_BAND_MAX    24 // pieces of one band: 320 / 16 + 1
// One band's sprite pieces in screen order, staged in the scratchpad (1 KB of 1-cycle RAM at 1F800000h, psx-spx
// "Memory Map"; nothing else in Mania uses it): the sprite's xy and tex words and its texture page's slot. DrawBand then
// writes them per page into one arena span (a texpage word + up to 4 16x16 or 3 sized sprites per fragment).
#define PS1_COL_MAX     96 // columns of a tile row's table (320 px + the bands' spread)
struct PS1BandStage {
    uint32_t xy[PS1_BAND_MAX];
    uint32_t tex[PS1_BAND_MAX];
    uint8 page[PS1_BAND_MAX];
    uint16 tpage[PS1_LAYER_PAGES];
    // the column table of a tile row (DrawRowBands): texture word (u | v << 8 | CLUT << 16) and flags
    uint32_t colTex[PS1_COL_MAX];
    uint8 colFlags[PS1_COL_MAX]; // bits 0-2 page slot, 4 H-flip, 5 V-flip, 6 empty, 7 no image
};
static_assert(sizeof(PS1BandStage) <= 1024, "the band stage must fit the scratchpad");
#define PS1_COL_HFLIP 0x10
#define PS1_COL_VFLIP 0x20
#define PS1_COL_EMPTY 0x40
#define PS1_COL_MISS  0x80
#define PS1_BAND_STAGE ((PS1BandStage *)0x1F800000)
uint32_t s_quads[24 * 18];
int32 s_quadWords = 0;

// RGB565 (fullPalette) -> PS1 BGR555, STP set on every visible entry (0x0000 is the GPU's transparent texel: an opaque
// black must read 0x8000; STP also makes semi-transparent primitives blend every texel, as Sonic CD's CLUTs)
inline uint16 PS1Colour(uint16 c) { return (uint16)(0x8000 | ((c >> 11) & 31) | (((c >> 6) & 31) << 5) | ((c & 31) << 10)); }

// A textured quad (GP0 2Ch, colour 0x80: texel colours unchanged) over texels [uL, uR) x [vT, vB), which may run
// backwards; corners A B C D = top-left, top-right, bottom-left, bottom-right.
__attribute__((always_inline)) inline void Quad(int32 x, int32 y, int32 w, int32 h, uint32 clut, uint32 tpage, int32 uL, int32 uR,
                                                int32 vT, int32 vB)
{
    uint32_t *q = &s_quads[s_quadWords];
    s_quadWords += 9;
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
}

// Rows [ty0, ty0 + h) of a flipped tile (rows counted before flipping) at screen (sx, sy)
__attribute__((optimize("O2"))) void FlippedPiece(int32 sx, int32 sy, int32 u, int32 v, int32 ty0, int32 h, bool fx, bool fy, uint32 clut, uint32 tpage)
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
        Quad(sx, sy, w, hh, clut, tpage, uL, uR, vT, vB);
        if (uEdge >= 0)
            Quad(sx + w, sy, 1, hh, clut, tpage, uEdge, uEdge, vT, vB);
    }
    if (vEdge >= 0) {
        int32 vLast = vT + (h - 1) * dv;
        Quad(sx, sy + hh, w, 1, clut, tpage, uL, uR, vLast, vLast);
        if (uEdge >= 0)
            Quad(sx + w, sy + hh, 1, 1, clut, tpage, uEdge, uEdge, vLast, vLast);
    }
}

// The staged sprites of one band (PS1_BAND_STAGE: n of them, `pages` texture pages) and its quads (s_quads) into the
// arena: per page, fragments of a texpage word + up to 4 16x16 or 3 sized sprites, all in one span; the quads in another
__attribute__((optimize("O2"))) void FlushBand(int32 n, int32 pages, int32 h)
{
    PS1BandStage *st = PS1_BAND_STAGE;
    uint16 ts        = PS1T();
    if (n) {
        // per fragment: header, texpage, then `per` sprites of `wps` words (16x16: cmd, xy, tex; sized: + wh)
        bool full  = h == TILE_SIZE;
        int32 per  = full ? 4 : 3, wps = full ? 3 : 4;
        uint32 cmd = full ? 0x7C808080 : 0x64808080, wh = TILE_SIZE | (uint32)h << 16;
        int32 maxWords = (n / per + pages) * (2 + per * wps);
        uint32_t *w    = PS1ChainSpanBegin(maxWords);
        if (w) {
            uint32_t *hdr = NULL;
            int32 fw      = 0;
            for (int32 k = 0; k < pages; ++k) {
                int32 inFrag = per; // start a fragment at the page's first sprite
                for (int32 i = 0; i < n; ++i) {
                    if (st->page[i] != k)
                        continue;
                    if (inFrag == per) {
                        if (hdr)
                            *hdr = (uint32_t)fw << 24 | ((uintptr_t)w & 0xFFFFFF);
                        hdr  = w;
                        *++w = 0xE1000000 | st->tpage[k];
                        ++w;
                        fw     = 1;
                        inFrag = 0;
                    }
                    w[0] = cmd;
                    w[1] = st->xy[i];
                    w[2] = st->tex[i];
                    if (!full)
                        w[3] = wh;
                    w += wps;
                    fw += wps;
                    ++inFrag;
                }
            }
            PS1ChainSpanEnd(hdr, fw, w);
        }
    }
    uint16 tq = PS1T();
    PS1_LT_ADD(2, tq - ts);
    if (s_quadWords) {
        // the band's quads into one span: a fragment per quad (9 words), one chain call
        int32 nq    = s_quadWords / 9;
        uint32_t *w = PS1ChainSpanBegin(nq * 10);
        if (w) {
            const uint32_t *q = s_quads;
            uint32_t *hdr     = w;
            for (int32 i = 0; i < nq; ++i, q += 9) {
                if (i)
                    *hdr = 9u << 24 | ((uintptr_t)w & 0xFFFFFF);
                hdr  = w;
                w[1] = q[0], w[2] = q[1], w[3] = q[2], w[4] = q[3], w[5] = q[4];
                w[6] = q[5], w[7] = q[6], w[8] = q[7], w[9] = q[8];
                w += 10;
            }
            PS1ChainSpanEnd(hdr, 9, w);
        }
        s_quadWords = 0;
    }
    PS1_LT_ADD(3, PS1T() - tq);
}

// Screen lines [sy, sy + h) showing layer rows [ly, ly + h) (inside one tile row), the line's left edge at layer x.
// Hot (hundreds of pieces a frame): -O2, the tiles fetched walking the 4x4 chunks (one chunk index per 4 tiles), the
// tile image read as three halfwords (u | v << 8, texpage, CLUT), the last texture page's slot cached.
__attribute__((optimize("O2"))) void DrawBand(TileLayer *layer, int32 sy, int32 h, int32 x, int32 ly, uint8 bank)
{
    uint16 tg              = PS1T();
    const uint16 *tileImage = TileTable();
    const uint8 *images     = s_ps1TileBlob + s_imgOff; // PS1TileImage: u8 u, v; u16 tpage, clut
    uint32 bankClut         = (uint32)(0 | (240 + bank) << 6); // GPU CLUT attribute of the bank's 256 entries
    int32 ty = ly >> 4, v0 = ly & 15;
    int32 tx = x >> 4, sx = -(x & 15);
    int32 xsize            = layer->xsize;
    const uint16 *chunkRow = &layer->layout[(ty >> 2) << layer->ps1ChunkShift];
    const uint16 *pool     = ps1ChunkPool + ((ty & 3) << 2);
    const uint16 *chunk    = pool + ((uint32)chunkRow[tx >> 2] << 4);
    int32 sub              = tx & 3;
    PS1BandStage *st       = PS1_BAND_STAGE;
    s_quadWords            = 0;
    int32 n = 0, pages = 0, pieces = 0, misses = 0, slot = 0;
    uint32 lastTPage = 0xFFFFFFFF, yWord = (uint32)(uint16)sy << 16, vRow = (uint32)v0 << 8;
    uint32 vFlipRow = (uint32)(15 - v0) << 8; // one line of a V-flipped tile: its mirrored row
    for (; sx < PS1_SCREEN_PX; sx += TILE_SIZE) {
        uint16 t = chunk[sub];
        if (++tx >= xsize) { // wrap: the layer repeats
            tx = 0, sub = 0;
            chunk = pool + ((uint32)chunkRow[0] << 4);
        }
        else if (++sub == 4) {
            sub   = 0;
            chunk = pool + ((uint32)chunkRow[tx >> 2] << 4);
        }
        if (t == 0xFFFF)
            continue;
        uint16 img = tileImage[t & 0x3FF];
        if (img == PS1_TILE_NONE) {
            ++misses;
            continue;
        }
        uint32 img2 = (uint32)img << 1, img4 = (uint32)img << 2;
        __asm__("" : "+r"(img2), "+r"(img4)); // 6-byte records: 2i + 4i (GCC folds shifts into a ~12-cycle mult)
        const uint8 *ti = images + img2 + img4;
        uint32 uv       = *(const uint16 *)ti;
        uint32 tpage    = *(const uint16 *)(ti + 2);
        uint32 clut     = *(const uint16 *)(ti + 4);
        if (clut == 0xFFFF)
            clut = bankClut;
        ++pieces;
        if (t & 0xC00) {
            if ((t & 0xC00) != 0x800 || h != 1) {
                FlippedPiece(sx, sy, uv & 0xFF, uv >> 8, v0, h, (t >> 10) & 1, (t >> 11) & 1, clut, tpage);
                continue;
            }
            uv += vFlipRow;
        }
        else {
            uv += vRow;
        }
        if (tpage != lastTPage) {
            slot = 0;
            while (slot < pages && st->tpage[slot] != tpage) ++slot;
            if (slot == pages) {
                if (pages == PS1_LAYER_PAGES) {
                    ++misses;
                    continue;
                }
                st->tpage[pages++] = (uint16)tpage;
            }
            lastTPage = tpage;
        }
        st->xy[n]   = (uint32)(uint16)sx | yWord;
        st->tex[n]  = uv | clut << 16;
        st->page[n] = (uint8)slot;
        ++n;
    }
    if (misses)
        g_ps1LayerMisses = g_ps1LayerMisses + misses;
    uint16 ts = PS1T();
    PS1_LT_ADD(1, ts - tg);
    FlushBand(n, pages, h);
    g_ps1LayerBands  = g_ps1LayerBands + 1;
    g_ps1LayerPieces = g_ps1LayerPieces + pieces;
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
    const uint8 *images     = s_ps1TileBlob + s_imgOff;
    uint32 bankClut         = (uint32)(0 | (240 + bands[0].bank) << 6);
    PS1BandStage *st        = PS1_BAND_STAGE;
    int32 tx                = c0 + dmin;
    if (tx < 0)
        tx += xsize;
    else if (tx >= xsize)
        tx -= xsize;
    const uint16 *chunkRow = &layer->layout[(ty >> 2) << layer->ps1ChunkShift];
    const uint16 *pool     = ps1ChunkPool + ((ty & 3) << 2);
    const uint16 *chunk    = pool + ((uint32)chunkRow[tx >> 2] << 4);
    int32 sub              = tx & 3;
    int32 pages = 0, slot = 0;
    uint32 lastTPage = 0xFFFFFFFF;
    for (int32 k = 0; k < cols; ++k) {
        uint16 t = chunk[sub];
        if (++tx >= xsize) {
            tx = 0, sub = 0;
            chunk = pool + ((uint32)chunkRow[0] << 4);
        }
        else if (++sub == 4) {
            sub   = 0;
            chunk = pool + ((uint32)chunkRow[tx >> 2] << 4);
        }
        if (t == 0xFFFF) {
            st->colFlags[k] = PS1_COL_EMPTY;
            continue;
        }
        uint16 img = tileImage[t & 0x3FF];
        if (img == PS1_TILE_NONE) {
            st->colFlags[k] = PS1_COL_MISS;
            continue;
        }
        if (t & 0x400) { // an H-flipped copy (the .ptl's pairs, 3.3b) makes the piece a sprite
            uint16 flipped = PS1TileHFlipImage(t & 0x3FF);
            if (flipped != PS1_TILE_NONE)
                img = flipped, t &= ~0x400;
        }
        uint32 img2 = (uint32)img << 1, img4 = (uint32)img << 2;
        __asm__("" : "+r"(img2), "+r"(img4)); // 6-byte records: 2i + 4i (GCC folds shifts into a ~12-cycle mult)
        const uint8 *ti = images + img2 + img4;
        uint32 tpage    = *(const uint16 *)(ti + 2);
        uint32 clut     = *(const uint16 *)(ti + 4);
        if (clut == 0xFFFF)
            clut = bankClut;
        if (tpage != lastTPage) {
            slot = 0;
            while (slot < pages && st->tpage[slot] != tpage) ++slot;
            if (slot == pages) {
                if (pages == PS1_LAYER_PAGES) {
                    st->colFlags[k] = PS1_COL_MISS;
                    continue;
                }
                st->tpage[pages++] = (uint16)tpage;
            }
            lastTPage = tpage;
        }
        st->colTex[k]   = *(const uint16 *)ti | clut << 16;
        st->colFlags[k] = (uint8)(slot | (t & 0xC00) >> 6); // 0x400 -> 0x10 (H), 0x800 -> 0x20 (V)
    }
    PS1_LT_ADD(1, PS1T() - tg);
    // each band from the table
    int32 misses = 0, pieces = 0;
    for (int32 b = 0; b < count; ++b) {
        uint16 tb      = PS1T();
        PS1Band *band  = &bands[b];
        int32 h = band->h, v0 = band->ly & 15;
        int32 sx       = -(band->x & 15);
        int32 k        = band->d - dmin;
        uint32 yWord   = (uint32)(uint16)band->sy << 16;
        uint32 vRow = (uint32)v0 << 8, vFlipRow = (uint32)(15 - v0) << 8;
        int32 n        = 0;
        s_quadWords    = 0;
        for (; sx < PS1_SCREEN_PX; sx += TILE_SIZE, ++k) {
            uint32 f = st->colFlags[k];
            if (f & (PS1_COL_EMPTY | PS1_COL_MISS)) {
                if (f & PS1_COL_MISS)
                    ++misses;
                continue;
            }
            uint32 tex = st->colTex[k];
            ++pieces;
            if (f & (PS1_COL_HFLIP | PS1_COL_VFLIP)) {
                if ((f & PS1_COL_HFLIP) || h != 1) {
                    FlippedPiece(sx, band->sy, tex & 0xFF, (tex >> 8) & 0xFF, v0, h, f & PS1_COL_HFLIP, f & PS1_COL_VFLIP,
                                 tex >> 16, st->tpage[f & 7]);
                    continue;
                }
                tex += vFlipRow;
            }
            else {
                tex += vRow;
            }
            st->xy[n]   = (uint32)(uint16)sx | yWord;
            st->tex[n]  = tex;
            st->page[n] = (uint8)(f & 7);
            ++n;
        }
        PS1_LT_ADD(1, PS1T() - tb);
        FlushBand(n, pages, h);
    }
    if (misses)
        g_ps1LayerMisses = g_ps1LayerMisses + misses;
    g_ps1LayerBands  = g_ps1LayerBands + count;
    g_ps1LayerPieces = g_ps1LayerPieces + pieces;
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
    w[1] = 0xE6000001;
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
#define PS1_CLUT_RECS   128 // upload chunks a frame: tile CLUTs + the bank (12 chunks) + sprite CLUTs, and restores
#define PS1_COLOUR_RECS 256 // direct colour words a frame (flat rectangles, one-colour runs)
struct PS1ClutRec {
    uint16_t *data;
    uint16 n;
};
PS1ClutRec s_clutRecs[PS1_CLUT_RECS];
int32 s_clutRecCount = 0;
uint32_t *s_colourRecs[PS1_COLOUR_RECS];
int32 s_colourRecCount = 0;
bool s_ps1Faded        = false;
extern "C" {
volatile uint32 g_ps1FadeCalls    = 0; // FillScreens that blended something (total)
volatile uint32 g_ps1FadeRestores = 0; // unfaded CLUT uploads after a fade (total)
volatile uint32 g_ps1FadeOverflow = 0; // FillScreens that met more CLUT / colour words than the tables hold (must stay 0)
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

// The frame start of the draw lists: this frame's clear, the tile CLUTs and the bank CLUT from the palette
void RSDK::PS1LayersFrameBegin()
{
    PS1FrameStart(true);
    g_ps1FrameBufferY = (uint32)PS1ChainBufferY();
    g_ps1LayerBands = g_ps1LayerPieces = g_ps1LayerHbl = g_ps1LayerSolidRuns = g_ps1LayerRowTables = 0;
    PS1SpritesFrameBegin();
    g_ps1LayerT[0] = g_ps1LayerT[1] = g_ps1LayerT[2] = g_ps1LayerT[3] = 0;
    s_clutRecCount = s_colourRecCount = 0;
    s_ps1Faded = s_fadeRecsLost = false;
    PS1ClutUploadAll();
}

void RSDK::PS1FadeRecordColour(uint32_t *word)
{
    if (s_colourRecCount < PS1_COLOUR_RECS)
        s_colourRecs[s_colourRecCount++] = word;
    else
        s_fadeRecsLost = true;
}

void RSDK::PS1FadeRestore()
{
    if (s_ps1Faded) {
        s_ps1Faded = false;
        PS1ClutUploadAll();
        g_ps1FadeRestores = g_ps1FadeRestores + 1;
    }
}

// FillScreen's arithmetic per 5-bit channel (the PC: blendLookupTable[0x20 * a + c] = a * c >> 8; the fill colour's
// channels c8 >> 3; green is the 565 value's top 5 bits, as the PS1 colours are): c' = (c * (255 - a) >> 8) + (f * a >> 8)
void RSDK::PS1FadeScreen(uint32 color, int32 alphaR, int32 alphaG, int32 alphaB)
{
    int32 fr = (int32)((color >> 16) & 0xFF) >> 3, fg = (int32)((color >> 8) & 0xFF) >> 3, fb = (int32)(color & 0xFF) >> 3;
    int32 cr = fr * alphaR >> 8, cg = fg * alphaG >> 8, cb = fb * alphaB >> 8;
    int32 kr = 0xFF - alphaR, kg = 0xFF - alphaG, kb = 0xFF - alphaB;
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
        uint32 w = *s_colourRecs[r];
        uint32 R = (((w & 0xFF) >> 3) * kr >> 8) + cr, G = ((((w >> 8) & 0xFF) >> 3) * kg >> 8) + cg;
        uint32 B = ((((w >> 16) & 0xFF) >> 3) * kb >> 8) + cb;
        *s_colourRecs[r] = (w & 0xFF000000) | R << 3 | G << 11 | B << 19;
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

__attribute__((optimize("O2"))) void RSDK::PS1DrawLayerHScroll(TileLayer *layer)
{
    if (!layer->xsize || !layer->ysize)
        return;
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
    const uint8 *solid = PS1TileSolidLines((int32)(layer - tileLayers), heightPx);
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
        int32 x = FROM_FIXED(sl->position.x);
        if (x >= widthPx)
            x -= widthPx;
        else if (x < 0)
            x += widthPx;
        int32 ly = FROM_FIXED(sl->position.y);
        if (ly >= heightPx || ly < 0) { // as the software loop: the layout row wraps with the layer height
            ly %= heightPx;
            if (ly < 0)
                ly += heightPx;
        }
        uint8 bank   = gfxLineBuffer[cy];
        int32 h      = 1;
        int32 rowEnd = TILE_SIZE - (ly & 15);
        while (h < rowEnd && cy + h < y2) {
            int32 nx = FROM_FIXED(sl[h].position.x);
            if (nx >= widthPx)
                nx -= widthPx;
            else if (nx < 0)
                nx += widthPx;
            if (nx != x || FROM_FIXED(sl[h].position.y) != ly + h || gfxLineBuffer[cy + h] != bank)
                break;
            ++h;
        }
        if (solid && solid[ly]) {
            // one-colour lines from here (at any X, this bank): a rectangle when the run covers at least the band's
            // lines (so no band is split); inside a band they are drawn with its tiles
            uint8 c = solid[ly];
            int32 r = 1;
            while (cy + r < y2 && gfxLineBuffer[cy + r] == bank) {
                int32 nly = FROM_FIXED(sl[r].position.y);
                if (nly >= heightPx || nly < 0) {
                    nly %= heightPx;
                    if (nly < 0)
                        nly += heightPx;
                }
                if (solid[nly] != c)
                    break;
                ++r;
            }
            if (r >= h) {
                PS1_FLUSH_GROUP();
                DrawSolid(cy, r, c, bank);
                cy += r;
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
    PS1_LT_ADD(0, PS1T() - tl);
    g_ps1LayerHbl = g_ps1LayerHbl + PS1HblanksSince(t0);
}
