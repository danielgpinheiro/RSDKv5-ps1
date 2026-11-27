// PS1 port (docs/perf/57 P25, PSZ1's paper rollers): the hidden background pieces' foreground cells. Linked into the
// overlays of the folders whose setup asks for it (tools/gen_overlays.py EXTRAS 'occlude': PSZ1Setup); PS1OccludeLink
// plugs it in (Scene.hpp g_ps1OcclFn, g_ps1TileOpaqueDst: the tile load reads the .ptl's opaque bits into s_opaque).
//
// Once a frame, at the first background layer that asks (PS1DrawLayerBasic), the screen's 16 x 23 cells of every
// foreground that will draw after it are marked opaque where the tile there is: an H-scroll layer drawn this frame with
// one scroll info and no deformation (ProcessParallax's uniform case: one X, consecutive rows; no scanline callback, no
// cut, no one-colour table, no strip), its X / Y computed as ProcessParallax will (the camera and scroll positions are
// final by the draw), its tile opaque (the .ptl's bit: all 256 texels non-transparent; index 0 is the only transparent
// CLUT entry, every other one has STP set, the fades keep it) and drawn (an image: a tile with none is not drawn). A
// background piece under opaque cells only is drawn over by them with the same pixels whatever it was (no foreground
// tile blends). Clip bounds: a group's draw resets them to the screen at its end, and the foregrounds of the scenes that
// ask draw in full-screen clips (PSZ1: FG Low in group 0 with the backgrounds, FG High in group 6).
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

extern "C" volatile uint32 g_ps1LayoutWrites; // Scene.cpp: tiles changed by PS1SetTile
#if PS1_CHECK_OCCLUDE
extern "C" {
volatile uint32 g_ps1OcclBuilds = 0, g_ps1OcclShifts = 0, g_ps1OcclWrong = 0; // frames; of them kept + shifted; rows wrong
}
#endif

namespace
{
uint8 s_opaque[128]; // bit t: tile t is opaque (the .ptl's, the tiles with no image cleared)
PS1OcclRows s_rows;
uint32 s_frame = 0, s_loads = 0xFFFFFFFF, s_minKey = 0; // s_minKey: the first foreground's draw order (group << 8 | layer)
bool s_ok = false;
// The cells kept from frame to frame (they change only when the cells' origin, a tile, the tile set or the foregrounds do):
// s_rows.row as built for origin (s_cx, s_cy) = the first foreground's cell column - 1 and row on screen x / y 0 with
// the foregrounds of s_set at s_dx / s_dy cells from it; the phases are each frame's
struct PS1OcclFg {
    TileLayer *layer;
    int32 cx, cy; // cell column - 1 / row at screen 0
};
PS1OcclFg s_fg[4];
int32 s_fgCount = 0, s_cx = 0, s_cy = 0;
uint32 s_set = 0, s_writes = 0xFFFFFFFF, s_offs = 0;
bool s_have = false;

// bits [j0, j0 + n) of cell row r (layer row ty) for one foreground whose column - 1 is tx0 (23 columns: j 0-22)
__attribute__((optimize("O2"))) uint32 PS1OcclBits(const TileLayer *layer, int32 ty, int32 tx, int32 j0, int32 n)
{
    const int32 xs = layer->xsize;
    const uint16 *chunkRow = PS1ChunkRow(layer, (uint32)ty >> 2);
    const uint16 *rowSel   = ps1ChunkPool + (ty & 3);
    const uint16 *chunk    = PS1ChunkTiles(rowSel, PS1ChunkIdx(chunkRow, (uint32)tx >> 2));
    uint32 m               = 0;
    for (int32 j = j0; j < j0 + n; ++j) {
        const uint32 t = chunk[tx & 3];
        if (t != 0xFFFF && ((s_opaque[(t & 0x3FF) >> 3] >> (t & 7)) & 1))
            m |= 1u << j;
        if (++tx == xs)
            tx = 0, chunk = PS1ChunkTiles(rowSel, PS1ChunkIdx(chunkRow, 0));
        else if (!(tx & 3))
            chunk = PS1ChunkTiles(rowSel, PS1ChunkIdx(chunkRow, (uint32)tx >> 2));
    }
    return m;
}
inline int32 PS1Wrap(int32 v, int32 n) { return v < 0 ? v + n : v >= n ? v - n : v; }
// cell row r's bits [j0, j0 + n) of every foreground, at origin (cx, cy) (each foreground at its offset from it)
uint32 PS1OcclRow(int32 r, int32 j0, int32 n)
{
    uint32 m = 0;
    for (int32 f = 0; f < s_fgCount; ++f) {
        const TileLayer *layer = s_fg[f].layer;
        m |= PS1OcclBits(layer, PS1Wrap(s_fg[f].cy + r, layer->ysize), PS1Wrap(s_fg[f].cx + j0, layer->xsize), j0, n);
    }
    return m;
}

__attribute__((optimize("O2"))) void PS1OcclBuild()
{
    s_ok = false;
    if (videoSettings.screenCount != 1 || currentScreen->size.x != 320 || currentScreen->size.y > 240)
        return;
    if (s_loads != g_ps1TileLoads) { // the scene's tiles: those without an image are never drawn
        s_loads = g_ps1TileLoads, s_have = false;
        for (uint32 t = 0; t < TILE_COUNT; ++t)
            if (((s_opaque[t >> 3] >> (t & 7)) & 1) && PS1TileImageOf((uint16)t) == PS1_TILE_NONE)
                s_opaque[t >> 3] &= ~(1 << (t & 7));
    }
    const int32 scr = sceneInfo.currentScreenID;
    int32 fx = -1, fy = -1, n = 0;
    uint32 minKey = 0xFFFF, set = 0, offs = 0;
    PS1OcclFg fg[4];
    for (int32 l = 0; l < LAYER_COUNT && n < 4; ++l) {
        TileLayer *layer = &tileLayers[l];
        const uint8 g    = layer->drawGroup[scr];
        if (g >= DRAWGROUP_COUNT || !engine.drawGroupVisible[g] || layer->type != LAYER_HSCROLL || !layer->xsize || !layer->ysize
            || layer->scanlineCallback || ps1LayerCut[l] || layer->ps1LineScrollOne >= layer->scrollInfoCount)
            continue;
        const ScrollInfo *si = &layer->scrollInfo[layer->ps1LineScrollOne];
        const int32 pw = TILE_SIZE * layer->xsize, ph = TILE_SIZE * layer->ysize;
        if (si->deform || PS1TileSolidLines(l, ph) || PS1TileStrip(l))
            continue;
        // ProcessParallax's LAYER_HSCROLL, the uniform case (Scene.cpp): ps1UniformX / ps1UniformY
        int16 x = FROM_FIXED(si->scrollPos + (currentScreen->position.x * si->parallaxFactor << 8)) % pw;
        if (x < 0)
            x += pw;
        int16 y = FROM_FIXED((int32)((layer->scrollPos + (layer->parallaxFactor * currentScreen->position.y << 8)) & 0xFFFF0000)) % ph;
        if (y < 0)
            y += ph;
        if (fx < 0)
            fx = x & 15, fy = y & 15;
        else if ((x & 15) != fx || (y & 15) != fy)
            continue; // (another phase: left out, which only draws more)
        const uint32 key = (uint32)g << 8 | (uint32)l;
        minKey           = key < minKey ? key : minKey;
        fg[n].layer = layer, fg[n].cx = PS1Wrap((x >> 4) - 1, layer->xsize), fg[n].cy = y >> 4;
        set |= 1u << l;
        if (n)
            offs = offs * 31 + (uint32)(fg[n].cx - fg[0].cx) * 7 + (uint32)(fg[n].cy - fg[0].cy);
        ++n;
    }
    if (!n)
        return;
    // the kept cells: whole again after a change of tiles / tile set / foregrounds / their offsets or a move of more than a
    // cell; a move of one cell on x and / or y shifts them and reads the new column / row (a layer wrap: whole again)
    const int32 dcx = fg[0].cx - s_cx, dcy = fg[0].cy - s_cy;
    const bool whole = !s_have || set != s_set || offs != s_offs || s_writes != g_ps1LayoutWrites || dcx < -1 || dcx > 1 || dcy < -1
                       || dcy > 1;
    for (int32 f = 0; f < n; ++f) s_fg[f] = fg[f];
    s_fgCount = n, s_cx = fg[0].cx, s_cy = fg[0].cy, s_set = set, s_offs = offs, s_writes = g_ps1LayoutWrites, s_have = true;
    if (whole) {
        for (int32 r = 0; r < 16; ++r) s_rows.row[r] = PS1OcclRow(r, 0, 23);
    }
    else {
        if (dcy == 1) {
            for (int32 r = 0; r < 15; ++r) s_rows.row[r] = s_rows.row[r + 1];
        }
        else if (dcy == -1) {
            for (int32 r = 15; r > 0; --r) s_rows.row[r] = s_rows.row[r - 1];
        }
        if (dcx == 1)
            for (int32 r = 0; r < 16; ++r) s_rows.row[r] >>= 1;
        else if (dcx == -1)
            for (int32 r = 0; r < 16; ++r) s_rows.row[r] = (s_rows.row[r] << 1) & 0x7FFFFF;
        if (dcy) { // the new row, whole
            const int32 r = dcy > 0 ? 15 : 0;
            s_rows.row[r] = PS1OcclRow(r, 0, 23);
        }
        if (dcx) { // the new column on the other rows
            const int32 j = dcx > 0 ? 22 : 0, skip = dcy > 0 ? 15 : dcy < 0 ? 0 : -1;
            for (int32 r = 0; r < 16; ++r)
                if (r != skip)
                    s_rows.row[r] |= PS1OcclRow(r, j, 1);
        }
    }
#if PS1_CHECK_OCCLUDE // evidence build (make EXTRA_CPPFLAGS=-DPS1_CHECK_OCCLUDE=1): the kept cells = a whole rebuild
    g_ps1OcclBuilds = g_ps1OcclBuilds + 1;
    g_ps1OcclShifts = g_ps1OcclShifts + !whole;
    for (int32 r = 0; r < 16; ++r)
        if (s_rows.row[r] != PS1OcclRow(r, 0, 23))
            g_ps1OcclWrong = g_ps1OcclWrong + 1;
#endif
    s_rows.fx = fx, s_rows.fy = fy, s_minKey = minKey, s_ok = true;
}

const PS1OcclRows *PS1OcclGet(TileLayer *layer, uint32 frame)
{
    if (frame != s_frame) {
        s_frame = frame;
        PS1OcclBuild();
    }
    const uint32 key = (uint32)layer->drawGroup[sceneInfo.currentScreenID] << 8 | (uint32)(layer - tileLayers);
    return s_ok && key < s_minKey ? &s_rows : NULL; // only for a layer every marked foreground draws after
}
} // namespace

extern "C" void PS1OccludeLink()
{
#if PS1_NO_OCCLUDE // (test switch)
    return;
#endif
    g_ps1OcclFn        = PS1OcclGet;
    g_ps1TileOpaqueDst = s_opaque;
}
