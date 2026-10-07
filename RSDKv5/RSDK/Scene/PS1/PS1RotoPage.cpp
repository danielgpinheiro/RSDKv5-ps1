// PS1 port (docs/32 9e): rotozoom layers drawn from one pre-rendered 256x256 texture page (tools/gfx/build_tiles.py
// ROTO_PAGES: the title's clouds and island) instead of a quad per tile. Linked into the overlays of the folders whose
// objects set up such layers (gen_overlays EXTRAS 'rotopage': TitleBG), not the resident: PS1RotoPageLink plugs it into
// PS1DrawLayerRotozoom (RSDK/Scene/PS1/PS1Roto.cpp, g_ps1RotoPageDraw).
#include "RSDK/Core/RetroEngine.hpp"
#include "../../../../ps1/gpu_chain_raw.hh"

using namespace RSDK;

extern "C" {
extern volatile uint32 g_ps1RotoQuads, g_ps1RotoLayers, g_ps1RotoPageLayers, g_ps1RotoPageCuts;
}

namespace
{
// 9e (docs/32): a layer drawn from one 256x256 page (tools/gfx/build_tiles.py ROTO_PAGES): the page holds the map's
// texels [ox, ox + 256) x [oy, oy + 256) (the rest of the map is empty, or the map is the page), repeated with the map.
// Each line is cut where it enters and leaves the page's copies ("cells"): per cell an exact pixel span [xs, xe] and the
// texels at its ends. Lines are grouped in bands (up to 16, halved until the middle line is within half a texel of the
// blend of the band's first and last lines and the three lines cross the same cells through the same edges); a band's
// span per cell is one quad from its first line's span to its last line's (the last line drawn by the next band).
// Nothing per tile: ~100 quads for the title's two layers instead of ~560.
#define PS1_ROTO_SEGS (6)
struct PS1RotoSeg {
    int16 xs, xe;          // pixels of the line inside the cell (inclusive)
    uint8 us, vs, ue, ve;  // texels at xs and at xe + 1 (clamped to the page)
    int8 kx, ky;           // the cell (its copy of the page)
    uint8 whyS, whyE;      // what ends the span: 0 the clip, 1 / 2 the cell's low / high u, 3 / 4 its low / high v
};
struct PS1RotoPageLine {
    int32 tag;   // line index (-1: empty slot)
    int32 count; // spans
    int32 e[4];  // texels (16.16) at x = 0 and x = xw: the band test
    PS1RotoSeg seg[PS1_ROTO_SEGS];
};
struct PS1RotoPageCtx {
    const ScanlineInfo *sl;
    int32 n, xw;
    int32 ox, oy;          // 16.16 texels
    int32 wShift, hShift;  // the map's size: texels << 16 = 1 << shift
    ScanlineInfo past;     // line n (the last band's end), extrapolated from the last two
    uint32 drops;
    PS1RotoPageLine cache[32]; // lines by index & 31 (a band spans at most 17)
};

__attribute__((always_inline)) inline const ScanlineInfo &PS1RotoLineIn(const PS1RotoPageCtx &c, int32 k)
{
    return k < c.n ? c.sl[k] : c.past;
}

// floor(a / |d|) from r = 2^31 / |d| (one 32 x 32 product instead of a divide; off by at most 1/16 pixel for |a| < 2^27,
// and the same function of the edge for both cells that share it, so their spans still meet exactly)
__attribute__((always_inline)) inline int32 PS1RotoQuot(int32 a, int32 r) { return (int32)(((int64)a * r) >> 31); }

// x range of a line where A <= p + x d < B (16.16 texels; r = 2^31 / |d|), intersected with [lo, hi]; the bound that
// cut it in why*
__attribute__((always_inline)) inline void PS1RotoSlab(int32 p, int32 d, int32 r, int32 A, int32 B, int32 &lo, int32 &hi,
                                                       uint32 &whyS, uint32 &whyE, uint32 codeLo)
{
    if (!d) {
        if (p < A || p >= B)
            hi = -1, lo = 0;
        return;
    }
    int32 a, b; // first and last x inside
    if (d > 0) {
        a = -PS1RotoQuot(p - A, r);     // ceil((A - p) / d)
        b = -PS1RotoQuot(p - B, r) - 1; // ceil((B - p) / d) - 1
        if (a > lo)
            lo = a, whyS = codeLo;
        if (b < hi)
            hi = b, whyE = codeLo + 1;
    }
    else {
        a = PS1RotoQuot(p - B, r) + 1; // the high edge is met first
        b = PS1RotoQuot(p - A, r);
        if (a > lo)
            lo = a, whyS = codeLo + 1;
        if (b < hi)
            hi = b, whyE = codeLo;
    }
}

__attribute__((always_inline)) inline uint32 PS1RotoTexel(int32 t) { return t < 0 ? 0 : t > (255 << 16) ? 255 : (uint32)t >> 16; }

// The line's spans per cell (cached). The context's fields are copied to locals first: the stores into the cache would
// otherwise make the compiler load them again after each one
__attribute__((noinline)) const PS1RotoPageLine &PS1RotoPageLineFill(PS1RotoPageCtx &c, int32 k)
{
    PS1RotoPageLine &L = c.cache[k & 31];
    const ScanlineInfo &sl = PS1RotoLineIn(c, k);
    const int32 px = sl.position.x, py = sl.position.y, dx = sl.deform.x, dy = sl.deform.y;
    const int32 xw = c.xw, last = xw - 1, ox = c.ox, oy = c.oy, wShift = c.wShift, hShift = c.hShift;
    L.tag = k, L.count = 0;
    if (dx > 0x400000 || dx < -0x400000 || dy > 0x400000 || dy < -0x400000) { // past the 32-bit products: not drawn
        L.e[0] = L.e[1] = L.e[2] = L.e[3] = 0;
        ++c.drops;
        return L;
    }
    L.e[0] = px, L.e[1] = py, L.e[2] = px + xw * dx, L.e[3] = py + xw * dy;
    int32 u0 = px, u1 = px + last * dx, v0 = py, v1 = py + last * dy;
    if (u0 > u1)
        u0 ^= u1, u1 ^= u0, u0 ^= u1;
    if (v0 > v1)
        v0 ^= v1, v1 ^= v0, v0 ^= v1;
    // the copies of the page the line's bounding box meets
    int32 kx0 = (u0 - ox) >> wShift, kx1 = (u1 - ox) >> wShift;
    int32 ky0 = (v0 - oy) >> hShift, ky1 = (v1 - oy) >> hShift;
    int32 rx = dx ? 0x7FFFFFFF / (dx < 0 ? -dx : dx) : 0, ry = dy ? 0x7FFFFFFF / (dy < 0 ? -dy : dy) : 0;
    int32 count = 0;
    for (int32 ky = ky0; ky <= ky1; ++ky) {
        int32 cy = oy + (ky << hShift);
        if (v1 < cy || v0 >= cy + (256 << 16))
            continue;
        for (int32 kx = kx0; kx <= kx1; ++kx) {
            int32 cx = ox + (kx << wShift);
            if (u1 < cx || u0 >= cx + (256 << 16))
                continue;
            int32 lo = 0, hi = last;
            uint32 whyS = 0, whyE = 0;
            PS1RotoSlab(px, dx, rx, cx, cx + (256 << 16), lo, hi, whyS, whyE, 1);
            if (lo <= hi)
                PS1RotoSlab(py, dy, ry, cy, cy + (256 << 16), lo, hi, whyS, whyE, 3);
            if (lo > hi)
                continue;
            if (count == PS1_ROTO_SEGS) {
                ++c.drops;
                continue;
            }
            PS1RotoSeg &s = L.seg[count++];
            s.xs = (int16)lo, s.xe = (int16)hi, s.kx = (int8)kx, s.ky = (int8)ky, s.whyS = (uint8)whyS, s.whyE = (uint8)whyE;
            s.us = (uint8)PS1RotoTexel(px + lo * dx - cx), s.vs = (uint8)PS1RotoTexel(py + lo * dy - cy);
            s.ue = (uint8)PS1RotoTexel(px + (hi + 1) * dx - cx), s.ve = (uint8)PS1RotoTexel(py + (hi + 1) * dy - cy);
        }
    }
    L.count = count;
    return L;
}

__attribute__((always_inline)) inline const PS1RotoPageLine &PS1RotoPageLineAt(PS1RotoPageCtx &c, int32 k)
{
    const PS1RotoPageLine &L = c.cache[k & 31];
    return L.tag == k ? L : PS1RotoPageLineFill(c, k);
}

inline bool PS1RotoSameCuts(const PS1RotoPageLine &a, const PS1RotoPageLine &b)
{
    if (a.count != b.count)
        return false;
    for (int32 i = 0; i < a.count; ++i) {
        const PS1RotoSeg &p = a.seg[i], &q = b.seg[i];
        if (p.kx != q.kx || p.ky != q.ky || p.whyS != q.whyS || p.whyE != q.whyE)
            return false;
    }
    return true;
}

// A band [a, b) is one quad per cell when its ends cross the same cells through the same edges and its middle line is
// within half a texel of their blend at both ends. (The middle's cuts are not compared: for lines of one direction the
// edges a line enters and leaves a cell through change monotonically down the lines, so equal ends mean equal middles.)
inline bool PS1RotoBandOk(PS1RotoPageCtx &c, int32 a, int32 b)
{
    const PS1RotoPageLine &la = PS1RotoPageLineAt(c, a), &lb = PS1RotoPageLineAt(c, b);
    const ScanlineInfo &m = PS1RotoLineIn(c, (a + b) >> 1);
    int32 xw = c.xw;
    int32 em[4] = { m.position.x, m.position.y, m.position.x + xw * m.deform.x, m.position.y + xw * m.deform.y };
    for (int32 i = 0; i < 4; ++i) {
        int32 err = 2 * em[i] - la.e[i] - lb.e[i]; // twice the middle's distance from the blend: within half a texel
        if (err > 0x10000 || err < -0x10000)
            return false;
    }
    return PS1RotoSameCuts(la, lb);
}

bool PS1RotoPageDraw(TileLayer *layer, const PS1TileImage *img, int32 ox, int32 oy, uint8 bank, uint32 t0)
{
    int32 x1 = currentScreen->clipBound_X1, y1 = currentScreen->clipBound_Y1;
    int32 x2 = currentScreen->clipBound_X2, y2 = currentScreen->clipBound_Y2;
    int32 n = y2 - y1, xw = x2 - x1;
    int32 wShift = layer->widthShift + 20, hShift = layer->heightShift + 20; // the map's size in 16.16 texels
    if (wShift > 30 || hShift > 30)
        return false;
    PS1RotoPageCtx c; // 3 KB of stack (the tile path's 6 KB frame is not on it: PS1RotoTileDraw)
    c.sl = &scanlines[y1];
    c.n = n, c.xw = xw, c.ox = ox << 16, c.oy = oy << 16, c.wShift = wShift, c.hShift = hShift, c.drops = 0;
    const ScanlineInfo &l1 = c.sl[n - 1], &l2 = c.sl[n - 2];
    c.past.position.x = 2 * l1.position.x - l2.position.x, c.past.position.y = 2 * l1.position.y - l2.position.y;
    c.past.deform.x = 2 * l1.deform.x - l2.deform.x, c.past.deform.y = 2 * l1.deform.y - l2.deform.y;
    for (int32 i = 0; i < 32; ++i) c.cache[i].tag = -1;

    uint32 clutW  = (img->clut == 0xFFFF ? (uint32)(240 + bank) << 6 : img->clut) << 16;
    uint32 tpageW = (uint32)img->tpage << 16;
    PS1ChainDrawArea(x1, y1, x2, y2);
    int32 quads = 0;
    for (int32 a = 0; a < n;) {
        int32 size = 16;
        while (a + size > n) size >>= 1;
        int32 st[8][2], sp = 0;
        st[sp][0] = a, st[sp][1] = size, ++sp;
        while (sp) {
            --sp;
            int32 ba = st[sp][0], bs = st[sp][1];
            if (bs > 1 && !PS1RotoBandOk(c, ba, ba + bs)) {
                int32 h = bs >> 1;
                st[sp][0] = ba + h, st[sp][1] = h, ++sp;
                st[sp][0] = ba, st[sp][1] = h, ++sp;
                continue;
            }
            // the band [ba, ba + bs): one quad per cell from its first line's span to its last line's (a single line: that
            // line's span, one pixel tall), each its own fragment
            const PS1RotoPageLine &ta = PS1RotoPageLineAt(c, ba);
            const PS1RotoPageLine &tb = bs > 1 ? PS1RotoPageLineAt(c, ba + bs) : ta;
            if (!ta.count)
                continue;
            uint32_t *w = PS1ChainBulkBegin(ta.count, 9);
            if (!w) {
                ++c.drops;
                continue;
            }
            uint32 yt = (uint32)(uint16)(y1 + ba) << 16, yb = (uint32)(uint16)(y1 + ba + bs) << 16;
            for (int32 i = 0; i < ta.count; ++i, w += 10) {
                const PS1RotoSeg &s = ta.seg[i], &q = tb.seg[i];
                w[0] = 0x2C808080;
                w[1] = yt | (uint16)(x1 + s.xs);
                w[2] = clutW | (uint32)s.vs << 8 | s.us;
                w[3] = yt | (uint16)(x1 + s.xe + 1);
                w[4] = tpageW | (uint32)s.ve << 8 | s.ue;
                w[5] = yb | (uint16)(x1 + q.xs);
                w[6] = (uint32)q.vs << 8 | q.us;
                w[7] = yb | (uint16)(x1 + q.xe + 1);
                w[8] = (uint32)q.ve << 8 | q.ue;
            }
            PS1ChainBulkEnd(ta.count);
            quads += ta.count;
        }
        a += size;
    }
    PS1ChainDrawAreaReset();
    if (c.drops)
        g_ps1RotoPageCuts = g_ps1RotoPageCuts + c.drops;
    g_ps1RotoQuads      = g_ps1RotoQuads + quads;
    g_ps1RotoLayers     = g_ps1RotoLayers + 1;
    g_ps1RotoPageLayers = g_ps1RotoPageLayers + 1;
    return true; // (the caller adds the time: the layer counters live in the resident)
}
} // namespace

extern "C" void PS1RotoPageLink() { g_ps1RotoPageDraw = PS1RotoPageDraw; }
