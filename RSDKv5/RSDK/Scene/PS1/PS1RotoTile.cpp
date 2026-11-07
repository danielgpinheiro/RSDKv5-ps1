// PS1 port (docs/ports/mania/32 4.1d, docs/perf/57 P1): the rotozoom layers' tile path (a quad per tile the lines see), moved
// out of the resident EXE (RSDK/Scene/PS1/PS1Roto.cpp keeps PS1DrawLayerRotozoom, its page path hook and the timing):
// linked into the overlays of the act units whose scenes have a LAYER_ROTOZOOM layer (tools/gen_overlays.py EXTRAS
// 'rototile': the Title and D.A. Garden, FBZ1, LRZ3, MMZ1, ERZ, the UFO stages, Pinball), PS1RotoTileLink plugs it in
// (g_ps1RotoTileDraw); elsewhere a rotozoom layer counts g_ps1UnlinkedCalls (the suite's guard). The code below is the
// resident's as it was (8.4 KB of it), the span writer's inline half copied from PS1Layers.cpp.
#include "RSDK/Core/RetroEngine.hpp"
#include "../../../../ps1/gpu_chain_raw.hh"
#include "../../../../ps1/video.hh"

using namespace RSDK;

extern "C" {
extern volatile uint32 g_ps1RotoQuads, g_ps1RotoLayers, g_ps1RotoTodo, g_ps1RotoFlat;
}

namespace
{
// --- PS1Layers.cpp's span writer (3.3f), the part the tile path uses ---------------------------------------------------
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

} // namespace

namespace
{
#define PS1_ROTO_MAX_COLS 128 // tile columns of one tile row's span

struct PS1RotoLine {
    int32 depth; // R x P_y >> 12
    int32 along; // R . P_y >> 12
    int32 inv;   // 2^30 / (R . D_y >> 12): pixels per unit of `along`
    int32 step;  // 2^24 / (the next line's depth - this one's): the fraction between them per unit of depth
};

struct PS1RotoPt {
    int16 x, y; // screen position (clamped to +-1000: the GPU's vertex range)
    uint32 xy;  // the vertex word (y << 16 | x)
};

inline int16 PS1RotoClamp(int32 v) { return (int16)(v < -1000 ? -1000 : v > 1000 ? 1000 : v); }
// 9u: (int32)(((int64)n << 16) / d) for d > 0, without libgcc's 64-bit divide (MA-185: __divdi3 was the resident's only
// user of it): |n| / d in hardware, then the 16 low quotient bits by restoring division (r < d <= 2^31 - 1: 2r fits);
// truncated toward zero and wrapped to 32 bits as the int64 version (tools/host/roto_div_check.cpp)
inline int32 PS1DivShl16(int32 n, int32 d)
{
    uint32 an = n < 0 ? 0u - (uint32)n : (uint32)n, ud = (uint32)d;
    uint32 q = an / ud, r = an % ud;
    for (int32 i = 0; i < 16; ++i) {
        r <<= 1, q <<= 1;
        if (r >= ud)
            r -= ud, q |= 1;
    }
    return (int32)(n < 0 ? 0u - q : q);
}

// A row of corners: layer points (qx0 + 16 i texels, qy), i < count (16.16 texels), to screen points. Along the row the
// depth and along values step by constants (a tile is 16 << 16, >> 12 = 256 units) and the line pair walks one way
__attribute__((always_inline)) inline void PS1RotoSolveRow(const PS1RotoLine *L, int32 n, int32 rx, int32 ry, int32 qx0, int32 qy,
                                                         int32 count, PS1RotoPt *out, int32 x1, int32 y1)
{
    int32 d = (int32)(((int64)rx * qy - (int64)ry * qx0) >> 12);
    int32 a = (int32)(((int64)rx * qx0 + (int64)ry * qy) >> 12);
    int32 dStep = -ry * 256, aStep = rx * 256;
    // the pair [lo, lo + 1] with depth[lo] <= d < depth[lo + 1] (the ends extrapolate): search once, then walk
    int32 lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int32 mid = (lo + hi) >> 1;
        if (L[mid].depth <= d)
            lo = mid;
        else
            hi = mid;
    }
    if (!dStep) {
        // the lines don't turn (R along x): the row's depth is constant, so one line pair and one fraction for the whole
        // row, and x steps by a constant: 16.16 accumulators, one add a corner
        while (lo < n - 2 && L[lo + 1].depth <= d)
            ++lo;
        while (lo > 0 && L[lo].depth > d)
            --lo;
        const PS1RotoLine &A = L[lo], &B = L[lo + 1];
        int32 dd = d - A.depth, lim = (B.depth - A.depth) << 6;
        dd       = dd > lim ? lim : dd < -lim ? -lim : dd;
        int32 t  = (int32)(((int64)dd * A.step) >> 16);
        int32 xa = (int32)(((int64)(a - A.along) * A.inv) >> 14), dxa = (int32)(((int64)aStep * A.inv) >> 14);
        int32 xb = (int32)(((int64)(a - B.along) * B.inv) >> 14), dxb = (int32)(((int64)aStep * B.inv) >> 14);
        int32 x = xa + (int32)(((int64)(xb - xa) * t) >> 8), dx = dxa + (int32)(((int64)(dxb - dxa) * t) >> 8);
        int16 sy = PS1RotoClamp(y1 + lo + (t >> 8));
        uint32 yw = (uint32)(uint16)sy << 16;
        for (int32 i = 0; i < count; ++i, x += dx) {
            int16 sx = PS1RotoClamp(x1 + (x >> 16));
            out[i].x = sx, out[i].y = sy, out[i].xy = yw | (uint16)sx;
        }
        return;
    }
    for (int32 i = 0; i < count; ++i, d += dStep, a += aStep) {
        while (lo < n - 2 && L[lo + 1].depth <= d)
            ++lo;
        while (lo > 0 && L[lo].depth > d)
            --lo;
        const PS1RotoLine &A = L[lo], &B = L[lo + 1];
        int32 dd = d - A.depth, lim = (B.depth - A.depth) << 6; // extrapolated up to 64 lines past an end
        dd       = dd > lim ? lim : dd < -lim ? -lim : dd;
        int32 t  = (int32)(((int64)dd * A.step) >> 16); // 8 fractional bits
        int32 xa = (int32)(((int64)(a - A.along) * A.inv) >> 30);
        int32 xb = (int32)(((int64)(a - B.along) * B.inv) >> 30);
        int16 sx = PS1RotoClamp(x1 + xa + (((xb - xa) * t) >> 8)), sy = PS1RotoClamp(y1 + lo + (t >> 8));
        out[i].x = sx, out[i].y = sy, out[i].xy = (uint32)(uint16)sy << 16 | (uint16)sx;
    }
}

// The quad's edges, lower y first, with their slope (dx per dy, 16.16) computed once
struct PS1RotoEdge {
    int32 px, py, qx, qy, slope;
};

// The x extent (16.16 texels) of the convex quad (edges E[0..3]) over the band y in [ya, yb] (16.16). False: no overlap
inline bool PS1RotoSpan(const PS1RotoEdge *E, int32 ya, int32 yb, int32 &xmin, int32 &xmax)
{
    bool any = false;
    for (int32 k = 0; k < 4; ++k) {
        int32 px = E[k].px, py = E[k].py, qx = E[k].qx, qy = E[k].qy;
        if (qy < ya || py > yb)
            continue;
        // the edge clipped to the band: its x at max(py, ya) and min(qy, yb)
        int32 c0 = py < ya ? ya : py, c1 = qy > yb ? yb : qy;
        for (int32 e = 0; e < 2; ++e) {
            int32 cy = e ? c1 : c0;
            int32 cx = qy == py ? px : px + (int32)(((int64)E[k].slope * (cy - py)) >> 16);
            if (!any || cx < xmin)
                xmin = cx;
            if (!any || cx > xmax)
                xmax = cx;
            any = true;
        }
    }
    return any;
}
} // namespace

// The tile path (4.1d): one quad per tile the lines see
__attribute__((optimize("O2"))) static int32 PS1RotoTileDraw(TileLayer *layer, uint8 bank)
{
    int32 x1 = currentScreen->clipBound_X1, y1 = currentScreen->clipBound_Y1;
    int32 x2 = currentScreen->clipBound_X2, y2 = currentScreen->clipBound_Y2;
    int32 n = y2 - y1, xw = x2 - x1;
    // R: the middle line's direction, narrowed to 11 bits
    const ScanlineInfo *sl = &scanlines[y1];
    int32 rx = sl[n >> 1].deform.x, ry = sl[n >> 1].deform.y;
    while (rx > 1023 || rx < -1023 || ry > 1023 || ry < -1023)
        rx >>= 1, ry >>= 1;
    // per line: depth, along, 1 / scale; every line parallel to R (within 1/256), depths monotonic (else not done)
    PS1RotoLine L[240];
    bool ok = rx || ry;
    for (int32 k = 0; ok && k < n; ++k) {
        const ScanlineInfo *s = &sl[k];
        // P11 (docs/perf/57): a line with the previous line's direction (MMZ1's far plane: all 240) has its test, kd and
        // 1 / scale too (they depend on D_y alone): reused, no 64-bit products or divide for it
        if (k && s->deform.x == s[-1].deform.x && s->deform.y == s[-1].deform.y)
            L[k].inv = L[k - 1].inv;
        else {
            int64 cr = (int64)s->deform.x * ry - (int64)s->deform.y * rx;
            int64 dt = (int64)s->deform.x * rx + (int64)s->deform.y * ry;
            int32 kd = (int32)(dt >> 12);
            if (kd <= 0 || (cr < 0 ? -cr : cr) * 256 > dt) {
                ok = false;
                break;
            }
            L[k].inv = (1 << 30) / kd; // a 32-bit divide (kd > 0)
        }
        L[k].depth = (int32)(((int64)rx * s->position.y - (int64)ry * s->position.x) >> 12);
        L[k].along = (int32)(((int64)rx * s->position.x + (int64)ry * s->position.y) >> 12);
        L[k].step  = 0;
    }
    // depths must rise down the lines (a plane seen from one side); falling ones are mirrored into rising ones
    int32 sign = ok && L[n - 1].depth < L[0].depth ? -1 : 1;
    if (sign < 0) {
        rx = -rx, ry = -ry;
        for (int32 k = 0; k < n; ++k) L[k].depth = -L[k].depth, L[k].along = -L[k].along, L[k].inv = -L[k].inv;
    }
    for (int32 k = 1; ok && k < n; ++k)
        if (L[k].depth <= L[k - 1].depth)
            ok = false;
        else
            L[k - 1].step = (int32)((1 << 24) / (L[k].depth - L[k - 1].depth));
    if (ok)
        L[n - 1].step = L[n - 2].step; // past the last line: the last pair's spacing (PS1RotoSolve only reads lo < n - 1)
    if (!ok) {
        // 7b.6: lines that scale only horizontally (FBZ1's inside background: a cylinder whose rows wrap, no plane):
        // drawn unscaled by the line drawer, each line's row as the PC picks it, centred where the PC centres it (the
        // horizontal scale is not drawn: counted)
        bool flat = true;
        for (int32 k = 0; flat && k < n; ++k)
            flat = !sl[k].deform.y && sl[k].deform.x > 0;
        if (flat) {
            int32 cx = currentScreen->center.x;
            for (int32 k = 0; k < n; ++k) ((ScanlineInfo *)sl)[k].position.x += (sl[k].deform.x - 0x10000) * cx;
            layer->ps1Uniform = layer->ps1Direct = 0;
            g_ps1RotoFlat = g_ps1RotoFlat + 1;
            PS1DrawLayerHScroll(layer);
            return 1;
        }
        g_ps1RotoTodo = g_ps1RotoTodo + 1;
        return 1;
    }

    // the region seen: the quad of the first and last lines' ends (16.16 texels), in tile rows
    int32 V[4][2] = { { sl[0].position.x, sl[0].position.y },
                      { sl[0].position.x + xw * sl[0].deform.x, sl[0].position.y + xw * sl[0].deform.y },
                      { sl[n - 1].position.x + xw * sl[n - 1].deform.x, sl[n - 1].position.y + xw * sl[n - 1].deform.y },
                      { sl[n - 1].position.x, sl[n - 1].position.y } };
    PS1RotoEdge E[4];
    for (int32 k = 0; k < 4; ++k) {
        const int32 *p = V[k], *q = V[(k + 1) & 3];
        bool sw = p[1] > q[1];
        E[k].px = sw ? q[0] : p[0], E[k].py = sw ? q[1] : p[1], E[k].qx = sw ? p[0] : q[0], E[k].qy = sw ? p[1] : q[1];
        int32 dy = E[k].qy - E[k].py;
        E[k].slope = dy > 0 ? PS1DivShl16(E[k].qx - E[k].px, dy) : 0; // four divides a layer
    }
    int32 minY = V[0][1], maxY = V[0][1];
    for (int32 k = 1; k < 4; ++k) minY = V[k][1] < minY ? V[k][1] : minY, maxY = V[k][1] > maxY ? V[k][1] : maxY;
    int32 ty0 = minY >> 20, ty1 = maxY >> 20;

    int32 wMask = (1 << layer->widthShift) - 1, hMask = (1 << layer->heightShift) - 1;
    const uint16 *tileImage = PS1TileImageTable(); // (the resident checked it: a scene with tile images)
    const uint8 *images     = (const uint8 *)PS1TileImages();
    uint32 bankClut         = (uint32)(240 + bank) << 6;

    // corner rows: `up` holds row ty's corners for columns [upA, upA + upN), `dn` row ty + 1's
    PS1RotoPt rowA[PS1_ROTO_MAX_COLS + 1], rowB[PS1_ROTO_MAX_COLS + 1];
    PS1RotoPt *up = rowA, *dn = rowB;
    int32 upA = 0, upN = 0;

    PS1ChainDrawArea(x1, y1, x2, y2);
    PS1Emit e;
    EmitOpen(e);
    e.page = 0xFFFFFFFF;
    uint32_t *w = e.w, *lim = e.hdr + 10, *end = e.end;
    uint32 page = e.page;
    int32 quads = 0, todo = 0;
    for (int32 ty = ty0; ty <= ty1; ++ty) {
        int32 xmin, xmax;
        if (!PS1RotoSpan(E, ty << 20, ((ty + 1) << 20) - 1, xmin, xmax))
            continue;
        int32 txa = (xmin >> 20) - 1, txb = (xmax >> 20) + 1; // a tile of margin each side (the quad's edges are straight)
        // only the columns between the row's first and last non-empty tiles (a sparse map, as the title's island). P11:
        // through the row's chunks as the tile walk below (one chunk lookup per 4 tiles; PS1TilePtr's element), not a
        // whole PS1TilePtr per tile
        {
            const uint16 *tcRow = PS1ChunkRow(layer, (uint32)(ty & hMask) >> 2), *tcSel = ps1ChunkPool + (ty & 3);
            int32 tx              = txa & wMask;
            const uint16 *tc      = PS1ChunkTiles(tcSel, PS1ChunkAt(layer, tcRow, (uint32)tx >> 2));
            while (txa <= txb && tc[tx & 3] == 0xFFFF) {
                ++txa;
                tx = (tx + 1) & wMask;
                if (!(tx & 3))
                    tc = PS1ChunkTiles(tcSel, PS1ChunkAt(layer, tcRow, (uint32)tx >> 2));
            }
            tx = txb & wMask;
            tc = PS1ChunkTiles(tcSel, PS1ChunkAt(layer, tcRow, (uint32)tx >> 2));
            while (txb >= txa && tc[tx & 3] == 0xFFFF) {
                --txb;
                tx = (tx - 1) & wMask;
                if ((tx & 3) == 3)
                    tc = PS1ChunkTiles(tcSel, PS1ChunkAt(layer, tcRow, (uint32)tx >> 2));
            }
        }
        if (txa > txb) {
            upN = 0; // no corners kept for the next row
            continue;
        }
        if (txb - txa + 1 > PS1_ROTO_MAX_COLS) {
            ++todo;
            continue;
        }
        int32 cn = txb - txa + 2; // corners
        // top corners: the previous row's bottom ones where the columns match, else solved
        PS1RotoPt *top = dn;
        if (ty != ty0 && txa >= upA && txa + cn <= upA + upN) {
            for (int32 i = 0; i < cn; ++i) top[i] = up[txa - upA + i];
        }
        else {
            PS1RotoSolveRow(L, n, rx, ry, txa << 20, ty << 20, cn, top, x1, y1);
        }
        PS1RotoPt *bot = up; // this row's bottom corners go into the other buffer
        PS1RotoSolveRow(L, n, rx, ry, txa << 20, (ty + 1) << 20, cn, bot, x1, y1);
        // the row's tiles walked through its 4x4 chunks (one chunk index per four tiles), wrapping on the map's width
        const uint16 *chunkRow = PS1ChunkRow(layer, (uint32)(ty & hMask) >> 2);
        const uint16 *rowSel   = ps1ChunkPool + (ty & 3);
        int32 tx               = txa & wMask;
        const uint16 *chunk    = PS1ChunkTiles(rowSel, PS1ChunkAt(layer, chunkRow, (uint32)tx >> 2));
        for (int32 i = 0; i < cn - 1; ++i) {
            uint16 t = chunk[tx & 3];
            tx       = (tx + 1) & wMask;
            if (!(tx & 3))
                chunk = PS1ChunkTiles(rowSel, PS1ChunkAt(layer, chunkRow, (uint32)tx >> 2));
            if (t == 0xFFFF)
                continue;
            const PS1RotoPt &a = top[i], &b = top[i + 1], &c = bot[i], &d = bot[i + 1];
            // not seen: all four corners past the lines' ends or the clip's sides
            if ((a.y >= y2 && b.y >= y2 && c.y >= y2 && d.y >= y2) || (a.y < y1 && b.y < y1 && c.y < y1 && d.y < y1))
                continue;
            if ((a.x >= x2 && b.x >= x2 && c.x >= x2 && d.x >= x2) || (a.x < x1 && b.x < x1 && c.x < x1 && d.x < x1))
                continue;
            uint16 img = tileImage[t & 0x3FF];
            if (img == PS1_TILE_NONE)
                continue;
            uint32 img2 = (uint32)img << 1, img4 = (uint32)img << 2;
            __asm__("" : "+r"(img2), "+r"(img4)); // 9v.13: 6-byte records as 2i + 4i (GCC folds the shifts into a mult)
            const uint8 *ti = images + img2 + img4;
            int32 u = ti[0], v = ti[1];
            uint32 tpage = *(const uint16 *)(ti + 2), clut = *(const uint16 *)(ti + 4);
            if (clut == 0xFFFF)
                clut = bankClut;
            // the tile's texels [u, u + 16) x [v, v + 16) (an edge past the page is clamped: approximate anyway)
            int32 uL = u, uR = u + 16 > 255 ? 255 : u + 16, vT = v, vB = v + 16 > 255 ? 255 : v + 16;
            if (t & 0x400)
                uL = uR, uR = u;
            if (t & 0x800)
                vT = vB, vB = v;
            if (__builtin_expect(w > lim - 4, 0))
                EmitFrag(w, lim, end, page);
            w[0] = 0x2C808080;
            w[1] = a.xy;
            w[2] = clut << 16 | (uint32)vT << 8 | (uint32)uL;
            w[3] = b.xy;
            w[4] = tpage << 16 | (uint32)vT << 8 | (uint32)uR;
            w[5] = c.xy;
            w[6] = (uint32)vB << 8 | (uint32)uL;
            w[7] = d.xy;
            w[8] = (uint32)vB << 8 | (uint32)uR;
            w += 9, page = tpage;
            ++quads;
        }
        // the bottom corners become the next row's top ones
        up = bot, dn = top, upA = txa, upN = cn;
    }
    e.w = w, e.hdr = lim - 10;
    EmitClose(e);
    PS1ChainDrawAreaReset();
    if (todo)
        g_ps1RotoTodo = g_ps1RotoTodo + 1;
    g_ps1RotoQuads  = g_ps1RotoQuads + quads;
    g_ps1RotoLayers = g_ps1RotoLayers + 1;
    return 0; // (the resident counts its time: an overlay naming the layer counters confuses GDB's lookup of them)
}

extern "C" void PS1RotoTileLink() { g_ps1RotoTileDraw = PS1RotoTileDraw; }
