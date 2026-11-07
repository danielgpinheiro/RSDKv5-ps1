// PS1 port (docs/ports/mania/32 4.1d): LAYER_ROTOZOOM layers (the title's island and clouds; later the UFO playfields, Pinball...)
// drawn by the GPU from their scanlines. Included by Scene.cpp after PS1Layers.cpp (its tile tables and span writer).
//
// DrawLayerRotozoom's software loop shows at screen (x1 + x, y) the texel P_y + x * D_y (scanlines[y].position /
// .deform, 16.16 texels), the tile map wrapping on its power-of-two size. Here every non-empty tile the lines see is one
// textured quad (GP0 2Ch) whose corners go where the lines put those texel points. When every line runs the same way
// (D_y parallel: a plane seen at one rotation, as the title's and the UFO's), a texel point Q has a "depth" R x Q
// (R: the lines' direction), each line has one (R x P_y, monotonic down the lines), and along the line Q sits at
// (R . (Q - P_y)) / (R . D_y) pixels: so a corner is a binary search over the lines' depths, the fraction between the two
// it falls between, and one product per line. Exact at the tile corners; inside a tile the GPU interpolates the texels
// affinely (approximate, as rotated sprites: the host render marks the layer's pixels). The tiles visited are those under
// the region the lines see (the convex quad of the first and last lines' ends), row by row; the quads are clipped to the
// layer's clip rectangle by the drawing area. Lines that don't run the same way are not done (counted).


// 9e: the page path (PS1RotoPage.cpp), linked into the overlays of the folders that have paged layers (gen_overlays EXTRAS)
bool (*RSDK::g_ps1RotoPageDraw)(TileLayer *layer, const PS1TileImage *img, int32 ox, int32 oy, uint8 bank, uint32 t0) = NULL;
// P1: the tile path (PS1RotoTile.cpp, gen_overlays EXTRAS 'rototile'): 0 drawn (its time counted here), 1 handled
int32 (*RSDK::g_ps1RotoTileDraw)(TileLayer *layer, uint8 bank) = NULL;
extern "C" volatile unsigned g_ps1UnlinkedCalls; // (build/gen/ovl_resident's stubs)

__attribute__((optimize("O2"))) void RSDK::PS1DrawLayerRotozoom(TileLayer *layer)
{
    if ((uint32)scanlines[0].deform.x == SCANLINE_MAJOR_MAGIC_3DTILES) { // 7.4: a camera for 3D tiles, not lines
        uint32 minor = (uint32)scanlines[0].deform.y;
        if ((minor == SCANLINE_MINOR_MAGIC_UFO || minor == SCANLINE_MINOR_MAGIC_UFO_FLOOR || minor == SCANLINE_MINOR_MAGIC_ROOF)
            && g_ps1S3DOps.floor)
            g_ps1S3DOps.floor(layer); // the UFO floor (RSDK/Graphics/PS1/PS1UfoFloor.cpp, in the UFO overlays)
        else
            g_ps1RotoTodo = g_ps1RotoTodo + 1;
        return;
    }
    if (!TileTable()) { // 4.2d: as PS1DrawLayerBasic
        g_ps1LayerNoTiles = g_ps1LayerNoTiles + 1;
        return;
    }
    int32 x1 = currentScreen->clipBound_X1, y1 = currentScreen->clipBound_Y1;
    int32 x2 = currentScreen->clipBound_X2, y2 = currentScreen->clipBound_Y2;
    int32 n = y2 - y1, xw = x2 - x1;
    if (!layer->xsize || !layer->ysize || xw <= 0 || n < 2)
        return;
    PS1LayerDebugHook((uint32)(layer - tileLayers));
    PS1FadeRestore();
#if PS1_DEBUG_BIGRAM
    PS1DrawLogAdd(0, 0, 0, 0, 0, 0, 0, (int32)(layer - tileLayers), 0, 0, 0);
#endif
    uint32 t0 = PS1Hblanks();
    for (int32 y = y1 + 1; y < y2; ++y)
        if (gfxLineBuffer[y] != gfxLineBuffer[y1]) {
            g_ps1RotoTodo = g_ps1RotoTodo + 1; // the PC takes each line's palette bank: several are not done
            return;
        }
    uint8 bank = gfxLineBuffer[y1];

    PS1TileImage pageImg;
    int32 pageOx, pageOy;
    if (g_ps1RotoPageDraw && PS1TileRotoPage((int32)(layer - tileLayers), &pageImg, &pageOx, &pageOy)
        && g_ps1RotoPageDraw(layer, &pageImg, pageOx, pageOy, bank, t0)) {
        uint32 dt     = PS1HblanksSince(t0); // (here: an overlay naming the layer counters confuses GDB's lookup of them)
        g_ps1RotoHbl  = g_ps1RotoHbl + dt;
        g_ps1LayerHbl = g_ps1LayerHbl + dt;
        g_ps1LayerHblBy[(layer - tileLayers) & 7] += dt;
        return;
    }

    // P1 (docs/perf/57): the tile path is linked into the overlays of the units with rotozoom layers (PS1RotoTile.cpp)
    if (!g_ps1RotoTileDraw) {
        g_ps1UnlinkedCalls = g_ps1UnlinkedCalls + 1; // (the suite's guard: a unit with a rotozoom layer left out of 'rototile')
        return;
    }
    if (g_ps1RotoTileDraw(layer, bank))
        return; // (flat lines drawn by PS1DrawLayerHScroll, or not done: counted there)
    uint32 dt     = PS1HblanksSince(t0);
    g_ps1RotoHbl  = g_ps1RotoHbl + dt;
    g_ps1LayerHbl = g_ps1LayerHbl + dt;
    g_ps1LayerHblBy[(layer - tileLayers) & 7] += dt;
}

