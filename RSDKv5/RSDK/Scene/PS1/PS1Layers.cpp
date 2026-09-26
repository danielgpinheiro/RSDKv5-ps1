// PS1 port (docs/32 3.3): tile layers drawn by the GPU. Included by Scene.cpp.
//
// DrawLayerHScroll's software loop draws each screen line from scanlines[line].position (the layer pixel at the
// line's left edge, 16.16) with the line's palette bank (gfxLineBuffer). Here consecutive lines with the same X, a
// layer row one further per line, the same tile row and the same bank form a band, drawn as one row of tile pieces
// (the tiles' images from PS1Tiles: 4-bit on shared CLUTs or 8-bit on the bank CLUT): an unflipped piece is a sprite
// (a whole 16x16 tile the fast 16x16 kind), grouped by texture page (the pieces of a band never overlap); a flipped
// one a textured quad with backwards UVs (no rectangle flip bits on old GPUs, psx-spx), split where an edge would
// leave its page (u -1 or v -1: that column / row drawn as a constant strip). The GPU clips at the screen edges.
// The CLUTs are filled from the live palette at the frame start (PS1LayersFrameBegin); per-line banks are 3.5.
#include "../../../../ps1/gpu_chain_raw.hh"
#include "../../../../ps1/video.hh" // PS1Hblanks

namespace RSDK
{
extern "C" {
volatile uint32 g_ps1LayerBands  = 0; // bands drawn (this frame)
volatile uint32 g_ps1LayerPieces = 0; // tile pieces chained (this frame)
volatile uint32 g_ps1LayerMisses = 0; // tiles with no image in VRAM (must stay 0)
volatile uint32 g_ps1LayerHbl    = 0; // hblanks spent drawing layers (this frame)
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
// one band's pieces, per texture page: fast 16x16 sprites (xy, tex) and sized sprites (xy, tex, wh)
struct PS1PagePieces {
    uint16 tpage;
    uint8 n16, nSized;
    uint32_t xy16[24], tex16[24];
    uint32_t sized[24 * 3];
};
PS1PagePieces s_pages[PS1_LAYER_PAGES];
int32 s_pageCount = 0;
uint32_t s_quads[24 * 18];
int32 s_quadWords = 0;

// RGB565 (fullPalette) -> PS1 BGR555, STP set on every visible entry (0x0000 is the GPU's transparent texel: an opaque
// black must read 0x8000; STP also makes semi-transparent primitives blend every texel, as Sonic CD's CLUTs)
inline uint16 PS1Colour(uint16 c) { return (uint16)(0x8000 | ((c >> 11) & 31) | (((c >> 6) & 31) << 5) | ((c & 31) << 10)); }

PS1PagePieces *PageOf(uint16 tpage)
{
    for (int32 i = 0; i < s_pageCount; ++i)
        if (s_pages[i].tpage == tpage)
            return &s_pages[i];
    if (s_pageCount == PS1_LAYER_PAGES)
        return NULL;
    PS1PagePieces *p = &s_pages[s_pageCount++];
    p->tpage         = tpage;
    p->n16 = p->nSized = 0;
    return p;
}

// A textured quad (GP0 2Ch, colour 0x80: texel colours unchanged) over texels [uL, uR) x [vT, vB), which may run
// backwards; corners A B C D = top-left, top-right, bottom-left, bottom-right.
void Quad(int32 x, int32 y, int32 w, int32 h, uint32 clut, uint32 tpage, int32 uL, int32 uR, int32 vT, int32 vB)
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
void FlippedPiece(int32 sx, int32 sy, int32 u, int32 v, int32 ty0, int32 h, bool fx, bool fy, uint32 clut, uint32 tpage)
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

// Screen lines [sy, sy + h) showing layer rows [ly, ly + h) (inside one tile row), the line's left edge at layer x.
// Hot (hundreds of pieces a frame): -O2, the tiles fetched walking the 4x4 chunks (one chunk index per 4 tiles), the
// tile tables read straight from PS1Tiles' block (the same translation unit), the last texture page cached.
__attribute__((optimize("O2"))) void DrawBand(TileLayer *layer, int32 sy, int32 h, int32 x, int32 ly, uint8 bank)
{
    uint16 tg                  = PS1T();
    const uint16 *tileImage    = TileTable();
    const PS1TileImage *images = (const PS1TileImage *)(s_ps1TileBlob + s_imgOff);
    uint32 bankClut            = (uint32)(0 | (240 + bank) << 6); // GPU CLUT attribute of the bank's 256 entries
    int32 ty = ly >> 4, v0 = ly & 15;
    int32 tx = x >> 4, sx = -(x & 15);
    int32 xsize             = layer->xsize;
    const uint16 *chunkRow  = &layer->layout[(ty >> 2) << layer->ps1ChunkShift];
    const uint16 *pool      = ps1ChunkPool + ((ty & 3) << 2);
    const uint16 *chunk     = pool + ((uint32)chunkRow[tx >> 2] << 4);
    int32 sub               = tx & 3;
    s_pageCount = 0;
    s_quadWords = 0;
    int32 pieces        = 0, misses = 0;
    PS1PagePieces *last = NULL;
    uint32 sizeWord     = TILE_SIZE | (uint32)h << 16;
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
        const PS1TileImage *ti = &images[img];
        uint32 clut            = ti->clut == 0xFFFF ? bankClut : ti->clut;
        ++pieces;
        if (t & 0xC00) {
            FlippedPiece(sx, sy, ti->u, ti->v, v0, h, (t >> 10) & 1, (t >> 11) & 1, clut, ti->tpage);
            continue;
        }
        PS1PagePieces *p = last && last->tpage == ti->tpage ? last : PageOf(ti->tpage);
        if (!p) {
            ++misses;
            continue;
        }
        last      = p;
        uint32 xy = (uint32)(uint16)sx | (uint32)(uint16)sy << 16;
        if (h == TILE_SIZE) {
            p->xy16[p->n16]  = xy;
            p->tex16[p->n16] = ti->u | (uint32)ti->v << 8 | clut << 16;
            ++p->n16;
        }
        else {
            uint32_t *w = &p->sized[p->nSized * 3];
            w[0]        = xy;
            w[1]        = ti->u | (uint32)(ti->v + v0) << 8 | clut << 16;
            w[2]        = sizeWord;
            ++p->nSized;
        }
    }
    if (misses)
        g_ps1LayerMisses = g_ps1LayerMisses + misses;
    uint16 ts = PS1T();
    PS1_LT_ADD(1, ts - tg);
    for (int32 i = 0; i < s_pageCount; ++i) {
        PS1PagePieces *p = &s_pages[i];
        PS1ChainTPageRaw(p->tpage);
        if (p->n16)
            PS1ChainSprite16RawN(p->xy16, p->tex16, p->n16);
        if (p->nSized)
            PS1ChainSpriteRawN(p->sized, p->nSized);
    }
    uint16 tq = PS1T();
    PS1_LT_ADD(2, tq - ts);
    if (s_quadWords)
        PS1ChainPrimsRawN(s_quads, 9, s_quadWords / 9);
    PS1_LT_ADD(3, PS1T() - tq);
    g_ps1LayerBands  = g_ps1LayerBands + 1;
    g_ps1LayerPieces = g_ps1LayerPieces + pieces;
}
} // namespace

// The frame start of the draw lists: this frame's clear, the tile CLUTs and the bank CLUT from the palette
void RSDK::PS1LayersFrameBegin()
{
    PS1FrameStart(true);
    g_ps1FrameBufferY = (uint32)PS1ChainBufferY();
    g_ps1LayerBands = g_ps1LayerPieces = g_ps1LayerHbl = 0;
    g_ps1LayerT[0] = g_ps1LayerT[1] = g_ps1LayerT[2] = g_ps1LayerT[3] = 0;
    const PS1TileClut *cl = PS1TileCluts();
    uint16 row[16];
    for (int32 c = 0; cl && c < PS1TileClutCount(); ++c) {
        row[0] = 0; // index 0: transparent
        for (int32 i = 1; i < 16; ++i) row[i] = cl[c].map[i] ? PS1Colour(fullPalette[0][cl[c].map[i]]) : 0; // padding: transparent
        PS1ChainUploadRow(row, 16, (int16_t)cl[c].x, (int16_t)cl[c].y);
    }
    static uint16 s_bank[256];
    s_bank[0] = 0;
    for (int32 i = 1; i < 256; ++i) s_bank[i] = PS1Colour(fullPalette[0][i]);
    PS1ChainUploadRow(s_bank, 256, 0, 240);
    PS1ChainFlushCache(); // the GPU's CLUT cache may hold the last frame's colours
}

void RSDK::PS1DrawLayerHScroll(TileLayer *layer)
{
    if (!layer->xsize || !layer->ysize)
        return;
    PS1LayerDebugHook((uint32)(layer - tileLayers));
    uint32 t0     = PS1Hblanks();
    uint16 tl     = PS1T();
    int32 widthPx = layer->xsize << 4, heightPx = layer->ysize << 4;
    int32 y1 = currentScreen->clipBound_Y1, y2 = currentScreen->clipBound_Y2;
    ScanlineInfo *sl = &scanlines[y1];
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
        uint16 tb = PS1T();
        DrawBand(layer, cy, h, x, ly, bank);
#if PS1_PROFILE
        g_ps1LayerT[0] -= (uint16)(PS1T() - tb); // scan time = total - bands
#endif
        cy += h;
        sl += h;
    }
    PS1_LT_ADD(0, PS1T() - tl);
    g_ps1LayerHbl = g_ps1LayerHbl + PS1HblanksSince(t0);
}
