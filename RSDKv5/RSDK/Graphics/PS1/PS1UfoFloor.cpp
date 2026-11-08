// PS1 port (docs/ports/mania/32 7.4): the UFO stages' floor as GTE-projected tile quads, the Dreamcast port's "3D tiles".
//
// Stock UFO_Setup_Scanline_Playfield fills 240 scanlines (a divide each) and gives every line the palette bank of its
// distance (the fog: banks 1-6 are colours 160-255 faded towards bank 7). On the PS1 (as on the Dreamcast) the callback
// passes the camera instead (ps1/ps1_limits.h's magic values: scanlines[0] the magic, [1] sin / cos of the yaw and of
// the pitch (1024 = 1), [2] the position and height, 16.16), and this draws the Playfield layer's tiles in front of the
// camera up to g_ps1FloorFar tiles as textured quads whose corners the GTE projects; beyond, up to g_ps1FloorLodFar or the
// horizon, the Dreamcast's LOD: 8 x 8-tile cells textured from the scene's minimap (one texel per tile, tools/gfx/
// build_tiles.py), drawn first (the real tiles cover them where both are).
//
// The PC's projection, from its line equations: a line i pixels below the screen centre shows the floor at forward
// distance F = H (256 cos - i sin) / (i cos + 256 sin) (H the camera's height, sin / cos of the pitch), pixel x at
// (x - centre) F' / 256 to the right (F' = F cos + H sin, the depth): a pinhole camera of focal length 256 looking along
// (sin yaw, cos yaw), right = (-cos yaw, sin yaw). So the GTE's rotation is that camera's, H = 256, the offset the screen
// centre; vertices are floor points relative to the camera in quarter pixels. Each tile takes the bank the PC gives the
// line through its centre: F >> 7 pixels (0-7), its CLUT that bank's copy (4-bit tiles: copies of the scene's tile
// CLUTs per bank in the tile CLUT slots the scene leaves free, built when first used in a frame; 8-bit: the bank's row).
// Linked into the UFO overlays with the 3D draw (PS1Scene3DLink fills g_ps1S3DOps.floor).
#include "RSDK/Core/RetroEngine.hpp"
#include "PS1Scene3D.hpp"
#include "../../../../ps1/gte3d.hh"
#include "../../../../ps1/gpu_chain_raw.hh"

using namespace RSDK;

extern volatile uint32_t g_ps1PerfFrames; // ps1/rsdk5_main.cpp: frames run

extern "C" {
extern volatile uint32 g_ps1FadeCalls, g_ps1FadeOpaque, g_ps1TintGrey; // RSDK/Scene/PS1/PS1Layers.cpp
extern volatile uint32 g_ps1LayoutWrites;                                  // RSDK/Scene/Scene.cpp PS1SetTile
extern volatile int32 g_ps1MaskRowsEnd;                                   // RSDK/Graphics/PS1/PS1RenderDevice.cpp
// (definitions in RSDK/Scene/PS1/PS1Layers.cpp, resident: GDB reads them at ps1TestStop; plan 59 4.2)
extern volatile uint32 g_ps1FloorDraws, g_ps1FloorQuads, g_ps1FloorHbl, g_ps1FloorTodo, g_ps1FloorCluts;
extern volatile uint32 g_ps1FloorBanks, g_ps1FloorSpanRows, g_ps1FloorSpanWrong, g_ps1FloorLodQuads;
extern volatile int32 g_ps1FloorFar, g_ps1FloorLodFar, g_ps1FloorAsm, g_ps1FloorMapFar, g_ps1FloorCoarseFar;
extern volatile int32 g_ps1FloorMip1, g_ps1FloorMip2;
}

namespace
{
#define PS1_FLOOR_MAX_COLS 96 // tile columns of one row's span
#define PS1_FLOOR_CLUT_SLOTS 192 // tools/gfx/vram_plan.py TILE_CLUT_SLOTS
#define PS1_HBL() (*(volatile uint16 *)0xBF801110)

// tile CLUT slot s -> VRAM (x, y) (vram_plan.py: rows 240-247 x 256-319, then rows 248-255 x 0-319)
inline void PS1FloorSlotXY(int32 s, int16 &x, int16 &y)
{
    if (s < 32)
        x = (int16)(256 + (s & 3) * 16), y = (int16)(240 + (s >> 2));
    else
        s -= 32, x = (int16)((s % 20) * 16), y = (int16)(248 + s / 20);
}

inline uint16 PS1FloorClutColour(uint16 c) // PS1Sprites.cpp's PS1ClutColour
{
    uint16 p = (uint16)(0x8000 | ((c >> 11) & 31) | (((c >> 6) & 31) << 5) | ((c & 31) << 10));
    if (maskColor && c == (uint16)maskColor && (p & 0x7FFF))
        p &= 0x7FFF;
    return p;
}

// The CLUT a tile image takes under bank b. 4-bit images: bank 0 their own; banks 1-7 copies of the scene's tile CLUTs
// with that bank's colours, in the tile CLUT slots the scene leaves free (from the last one down). 8-bit images: the
// bank's 256-entry row (240 + b). A copy is uploaded when its colours changed (the raw palette entries kept to compare) or
// after a frame with a screen fade / tint (they recolour the frame's CLUT uploads: VRAM then holds recoloured copies);
// the first frame of a fade sees the floor unfaded (not seen: fades start from nothing or from full white).
const PS1TileClut *s_cl = NULL;
int32 s_nc              = 0;
uint32 s_loads          = 0xFFFFFFFF; // g_ps1TileLoads when the tables were built
#define PS1_FLOOR_CLUTS 12            // tile CLUTs with per-bank copies (the tiles' + the minimap's)
#define PS1_FLOOR_K8    PS1_FLOOR_CLUTS      // s_imgClut: an 8-bit image
#define PS1_FLOOR_KNONE (PS1_FLOOR_CLUTS + 1) // s_imgClut: a CLUT of none of the scene's (bank 0 only)
uint8 s_imgClut[1024];                // image -> its CLUT's index, or K8 / KNONE
uint32 s_mip1 = 0, s_mipN = 0, s_mipLevels = 0; // ps1-ufo2: the 8x8 level's first image (the 4x4's: + s_mipN), images 0..s_mipN - 1 have them
uint16 s_attr[PS1_FLOOR_CLUTS + 2][8]; // this frame: CLUT attribute per (s_imgClut, bank), 0: not checked yet
uint16 s_raw[8][PS1_FLOOR_CLUTS][16];  // [bank][clut][entry]: the raw colours of the uploaded copies
uint16 s_rawOk[8];                    // bit c: s_raw[b][c] valid
uint32 s_lod = 0;                     // PS1TileLodInfo() when the tables were built
uint32 s_sum8Ok;                      // 8-bit rows: bit b, s_pal8[b] holds bank b's colours uploaded
// ps1-ufo2: the colours each 8-bit row was uploaded from (entries 0-255 as 128 words; entry 0 not compared): compared
// two entries a load (the checksum it replaces ran 255 rotate-adds a bank every frame: UFO6's pattern floor ~12 hblanks)
typedef uint32 __attribute__((may_alias)) PS1PalWord;
uint32 s_pal8[8][128];
uint32 s_frame = 0xFFFFFFFF, s_fadeSeen = 0xFFFFFFFF, s_vramGen = 0xFFFFFFFF;
bool s_force = true;
uint16 s_row[256];

void PS1FloorClutsReset()
{
    s_cl    = PS1TileCluts();
    s_loads = g_ps1TileLoads;
    s_nc    = s_cl ? PS1TileStripClutFirst() : 0; // (ps1-ufo: a strip's CLUTs after the scene's own: no fog copies)
    if (s_nc > PS1_FLOOR_CLUTS || s_nc * 8 > PS1_FLOOR_CLUT_SLOTS)
        s_nc = 0; // no room for the copies: every tile drawn with bank 0 (counted)
    uint8 clutOf[16 * 20]; // (y - 240) * 20 + x / 16 -> CLUT index + 1
    memset(clutOf, 0, sizeof(clutOf));
    for (int32 c = 0; c < s_nc; ++c) {
        int32 y = s_cl[c].y - 240, x = s_cl[c].x >> 4;
        if (y >= 0 && y < 16 && x < 20)
            clutOf[y * 20 + x] = (uint8)(c + 1);
    }
    const PS1TileImage *im = PS1TileImages();
    const uint16 *table    = PS1TileImageTable();
    memset(s_imgClut, PS1_FLOOR_KNONE, sizeof(s_imgClut));
    s_lod           = PS1TileLodInfo();
    const int32 lod = s_lod & 0xFFFF, lodN = (int32)((s_lod >> 16) & 0xFF) * (int32)(s_lod >> 24);
    for (int32 t = 0; table && t < 1024 + lodN + 1; ++t) { // the tiles' images, the minimap's, the floor / roof texture
        uint32 img = t < 1024 ? table[t] : t < 1024 + lodN ? (uint32)(lod + t - 1024) : PS1TilePatternImage();
        if (!img && t >= 1024)
            continue;
        if (img == PS1_TILE_NONE || img >= 1024)
            continue;
        uint32 clut = im[img].clut;
        int32 y = (int32)(clut >> 6) - 240, x = (int32)(clut & 63);
        s_imgClut[img] = clut == 0xFFFF ? PS1_FLOOR_K8
                         : (y >= 0 && y < 16 && x < 20 && clutOf[y * 20 + x]) ? clutOf[y * 20 + x] - 1
                                                                            : PS1_FLOOR_KNONE;
    }
    memset(s_rawOk, 0, sizeof(s_rawOk));
    s_sum8Ok = 0;
    // ps1-ufo2: the mip levels' record, the image table's last entry ('M', 'P', 8x8 level's first image, count); one or two
    // levels (8x8, 4x4) between it and the record
    s_mip1 = s_mipN = s_mipLevels = 0;
    if (table) {
        const uint32 n = (uint32)((const uint8 *)PS1TileCluts() - (const uint8 *)im) / sizeof(PS1TileImage);
        const PS1TileImage *m = n ? &im[n - 1] : NULL;
        const uint32 levels = m && m->clut ? (n - 1 - m->tpage) / m->clut : 0;
        if (m && m->u == 'M' && m->v == 'P' && (levels == 1 || levels == 2) && m->tpage + levels * m->clut + 1 == n)
            s_mip1 = m->tpage, s_mipN = m->clut, s_mipLevels = levels;
    }
}

// the attribute for (k = s_imgClut[img], bank) this frame, uploading the copy if needed (no bulk may be open: the
// upload goes into the chain)
uint32 PS1FloorClutFill(uint32 k, int32 bank, uint32 ownClut)
{
    uint32 attr = ownClut;
    if (k == PS1_FLOOR_K8) { // 8-bit: the bank's row
        attr = (uint32)(240 + bank) << 6;
        if (bank) {
            const PS1PalWord *pw = (const PS1PalWord *)fullPalette[bank];
            uint32 *cw           = s_pal8[bank];
            bool same            = !s_force && (s_sum8Ok & (1u << bank)) && !((pw[0] ^ cw[0]) >> 16);
            for (int32 i = 1; i < 128 && same; i += 4)
                same = !((pw[i] ^ cw[i]) | (pw[i + 1] ^ cw[i + 1]) | (pw[i + 2] ^ cw[i + 2]) | (i + 3 < 128 ? pw[i + 3] ^ cw[i + 3] : 0));
            if (!same) {
                s_row[0] = 0;
                for (int32 i = 1; i < 256; ++i) s_row[i] = PS1FloorClutColour(fullPalette[bank][i]);
                PS1UploadClut(s_row, 256, 0, (int16)(240 + bank));
                for (int32 i = 0; i < 128; ++i) cw[i] = pw[i];
                s_sum8Ok |= 1u << bank;
                g_ps1FloorCluts = g_ps1FloorCluts + 1;
            }
        }
    }
    else if (k < PS1_FLOOR_CLUTS && bank) {
        int16 sx, sy;
        PS1FloorSlotXY(PS1_FLOOR_CLUT_SLOTS - 1 - ((bank - 1) * s_nc + (int32)k), sx, sy);
        attr             = (uint32)(sx >> 4) | (uint32)sy << 6;
        const uint8 *map = s_cl[k].map;
        uint16 *raw      = s_raw[bank][k];
        bool same        = !s_force && (s_rawOk[bank] & (1u << k));
        for (int32 i = 1; i < 16 && same; ++i) same = raw[i] == fullPalette[bank][map[i]];
        if (!same) {
            s_row[0] = 0;
            for (int32 i = 1; i < 16; ++i) {
                raw[i]   = fullPalette[bank][map[i]];
                s_row[i] = map[i] ? PS1FloorClutColour(raw[i]) : 0;
            }
            PS1UploadClut(s_row, 16, sx, sy);
            s_rawOk[bank] |= (uint16)(1u << k);
            g_ps1FloorCluts = g_ps1FloorCluts + 1;
        }
    }
    else if (k == PS1_FLOOR_KNONE && bank)
        g_ps1FloorTodo = g_ps1FloorTodo + 1; // a CLUT the copies don't cover: bank 0's colours
    s_attr[k][bank] = (uint16)attr;
    return attr;
}

// ps1-ufo: (int64)a * b / d with the CPU's 32-bit divide when a * b fits (the same truncated quotient; libgcc's
// 64-bit divide is a loop of a few hundred cycles on the console)
inline int64 PS1FloorMulDiv(int32 a, int32 b, int32 d)
{
    int32 p;
    if (!__builtin_mul_overflow(a, b, &p) && d != -1)
        return p / d;
    return (int64)a * b / d;
}

// The x extent of the convex quad V (4 corners, quarter pixels) over the band y in [ya, yb]. False: no overlap
bool PS1FloorSpan(const int32 V[4][2], int32 ya, int32 yb, int32 &xmin, int32 &xmax)
{
    bool any = false;
    for (int32 k = 0; k < 4; ++k) {
        const int32 *p = V[k], *q = V[(k + 1) & 3];
        if (p[1] > q[1]) {
            const int32 *t = p;
            p = q, q = t;
        }
        if (q[1] < ya || p[1] > yb)
            continue;
        int32 c0 = p[1] < ya ? ya : p[1], c1 = q[1] > yb ? yb : q[1];
        int32 dy = q[1] - p[1];
        for (int32 e = 0; e < 2; ++e) {
            int32 cy = e ? c1 : c0;
            // (9f: in 32 bits when the product fits: the same quotient, truncated alike; libgcc's 64-bit divide was
            // ~1 % of a UFO frame)
            const int32 ex = q[0] - p[0], ey = cy - p[1]; // ey in [0, dy]
            int32 cx = p[0];
            if (dy)
                cx += (ex > -0x8000 && ex < 0x8000 && ey < 0x10000) ? ex * ey / dy : (int32)(((int64)ex * ey) / dy);
            if (!any || cx < xmin)
                xmin = cx;
            if (!any || cx > xmax)
                xmax = cx;
            any = true;
        }
    }
    return any;
}

// ps1-ufo: PS1FloorSpan for the rows [y0 + r step, y0 + r step + step - 1], r = 0, 1, 2... asked in order, with no
// divide a row. An edge's x at a line ey into it is p.x + trunc(ex * ey / dy) = p.x + sgn * floor(|ex| ey / dy); its end
// lines give its vertices exactly (ey = 0: p.x; ey = dy: p.x + ex); the band's two inner lines (ya, yb) move by `step`
// a row, so their quotients and remainders step by |ex| step / dy and its remainder (set up once, by the same divide).
// The same x values as PS1FloorSpan (PS1_CHECK_FLOORSPAN: both run, g_ps1FloorSpanWrong counts the rows that differ).
struct PS1FloorSpanner {
    struct Line {
        int32 ey = -1; // the line this state holds (-1: none)
        uint32 q = 0, r = 0;
    };
    struct Edge {
        int32 px, py, qy, ex, dy, sgn;
        uint32 ax, Q, R; // |ex|; |ex| step / dy and its remainder (dda: |ex| step fits 32 bits)
        bool dda;
        Line a, b; // the bands' first / last lines
    } e[4];
    int32 y0, step;
    void Init(const int32 V[4][2], int32 y0_, int32 step_)
    {
        y0 = y0_, step = step_;
        for (int32 k = 0; k < 4; ++k) {
            const int32 *p = V[k], *q = V[(k + 1) & 3];
            if (p[1] > q[1]) {
                const int32 *t = p;
                p = q, q = t;
            }
            Edge &g = e[k];
            g.px = p[0], g.py = p[1], g.qy = q[1], g.ex = q[0] - p[0], g.dy = q[1] - p[1];
            g.sgn = g.ex < 0 ? -1 : 1, g.ax = (uint32)(g.ex < 0 ? -g.ex : g.ex);
            g.dda = g.dy > 0 && g.ax < (1u << 20); // (step <= 1024)
            g.Q = g.dda ? g.ax * (uint32)step / (uint32)g.dy : 0, g.R = g.dda ? g.ax * (uint32)step % (uint32)g.dy : 0;
            g.a.ey = g.b.ey = -1;
        }
    }
    // x at line ey (0 < ey < dy) of edge g through its line state s (stepped when ey is the next one, else set up)
    static inline int32 At(Edge &g, Line &s, int32 ey, int32 step)
    {
        if (g.dda && s.ey >= 0 && ey == s.ey + step) {
            s.q += g.Q, s.r += g.R;
            if (s.r >= (uint32)g.dy)
                ++s.q, s.r -= (uint32)g.dy;
        }
        else {
            const uint64 n = (uint64)g.ax * (uint32)ey;
            s.q = (uint32)(n / (uint32)g.dy), s.r = (uint32)(n % (uint32)g.dy);
        }
        s.ey = ey;
        return g.px + g.sgn * (int32)s.q;
    }
    bool Row(int32 r, int32 &xmin, int32 &xmax)
    {
        const int32 ya = y0 + r * step, yb = ya + step - 1;
        bool any = false;
        for (int32 k = 0; k < 4; ++k) {
            Edge &g = e[k];
            if (g.qy < ya || g.py > yb)
                continue;
            int32 x0, x1;
            if (!g.dy)
                x0 = x1 = g.px;
            else {
                x0 = g.py >= ya ? g.px : At(g, g.a, ya - g.py, step);
                x1 = g.qy <= yb ? g.px + g.ex : At(g, g.b, yb - g.py, step);
            }
            const int32 lo = x0 < x1 ? x0 : x1, hi = x0 < x1 ? x1 : x0;
            if (!any || lo < xmin)
                xmin = lo;
            if (!any || hi > xmax)
                xmax = hi;
            any = true;
        }
#if PS1_CHECK_FLOORSPAN
        int32 cmin = 0, cmax = 0;
        const bool cany = PS1FloorSpan(V0, ya, yb, cmin, cmax);
        if (cany != any || (any && (cmin != xmin || cmax != xmax)))
            g_ps1FloorSpanWrong = g_ps1FloorSpanWrong + 1;
        g_ps1FloorSpanRows = g_ps1FloorSpanRows + 1;
#endif
        return any;
    }
#if PS1_CHECK_FLOORSPAN
    const int32 (*V0)[2];
#endif
};
#if PS1_CHECK_FLOORSPAN
#define PS1_SPANNER_INIT(sp, V, y0, step) ((sp).Init(V, y0, step), (sp).V0 = (V))
#else
#define PS1_SPANNER_INIT(sp, V, y0, step) (sp).Init(V, y0, step)
#endif

#define PS1_FLOOR_MAX_ROWS 128 // tile rows of the region

// a corner row: floor points (x0 + 64 i, hq, zq) relative to the camera, i < n, projected
uint32 s_xyA[PS1_FLOOR_MAX_COLS + 2], s_xyB[PS1_FLOOR_MAX_COLS + 2];
uint16 s_zA[PS1_FLOOR_MAX_COLS + 2], s_zB[PS1_FLOOR_MAX_COLS + 2];
int16 s_rowA[PS1_FLOOR_MAX_ROWS], s_rowB[PS1_FLOOR_MAX_ROWS]; // per tile row: first / last column (b < a: none)

// -> the corners the GTE flagged (depth 0: behind, too near — nearer than H / 2, 32 pixels — or far off screen)
inline int32 PS1FloorProject(int32 x0, int32 hq, int32 zq, int32 n, uint32 *xy, uint16 *z, int32 step = 64)
{
    int16 v[3 * (PS1_FLOOR_MAX_COLS + 2)];
    for (int32 i = 0; i < n; ++i, x0 += step) v[3 * i] = (int16)x0, v[3 * i + 1] = (int16)hq, v[3 * i + 2] = (int16)zq;
    return PS1Gte3DProject(v, n, (uint32_t *)xy, z);
}
// 9f (UFO item 3): a pass's corner rows shared. Row r's cells span columns s_rowA[r]..s_rowB[r] (b < a: none); the
// corner row between rows r and r + 1 is projected once, over both rows' columns (the real tiles' pass did this already):
// the same corners (x = column x step, z = the row line), half the projections. Sets *t* (the top corners of row r, from
// column ta) and *b* (its bottom ones, from column ba); keeps the bottom as the next row's top.
struct PS1FloorRows {
    uint32 *topXY = s_xyA, *botXY = s_xyB;
    uint16 *topZ = s_zA, *botZ = s_zB;
    int32 topA = 0, topN = 0, topBad = 0, botA = 0, botN = 0, botBad = 0;
    // r's corners: (x0 + column * step) - camX, hq, z (its top line; the bottom one at z + step)
    void Row(int32 r, int32 nRows, int32 step, int32 shift, int32 camX, int32 hq, int32 zq)
    {
        const int32 txa = s_rowA[r], txb = s_rowB[r];
        if (!topN || txa < topA || txb + 1 >= topA + topN) { // the row above had no corners here: project them
            topA = txa, topN = txb - txa + 2;
            topBad = PS1FloorProject((txa << shift) - camX, hq, zq, topN, topXY, topZ, step);
        }
        int32 ba = txa, bb = txb;
        if (r + 1 < nRows && s_rowB[r + 1] >= s_rowA[r + 1])
            ba = s_rowA[r + 1] < ba ? s_rowA[r + 1] : ba, bb = s_rowB[r + 1] > bb ? s_rowB[r + 1] : bb;
        if (bb - ba + 1 > PS1_FLOOR_MAX_COLS) // the union too wide: this row's only (the next projects its own)
            ba = txa, bb = txb;
        botA = ba, botN = bb - ba + 2;
        botBad = PS1FloorProject((ba << shift) - camX, hq, zq + step, botN, botXY, botZ, step);
    }
    void Next()
    {
        uint32 *x = topXY;
        uint16 *z = topZ;
        topXY = botXY, topZ = botZ, botXY = x, botZ = z, topA = botA, topN = botN, topBad = botBad;
    }
};
// 9f (UFO item 3c): ps1/floor_quads.s's arguments (byte offsets in its header); g_ps1FloorAsm 0: the C loop (A/B)
struct PS1FloorAsmCtx {
    int32 j, nn;
    const uint8 *col;
    const uint16 *tiles, *imgs;
    const uint32 *tXY, *bXY;
    const uint16 *tZ, *bZ;
    int32 check, fd0, fStep;
    const PS1TileImage *images;
    const uint8 *imgClut;
    const uint16 *attr;
    uint32 *w, *wend;
    int32 ext; // ps1-ufo2: the images' last texel (15; 7 / 3: the mip levels)
};
static_assert(sizeof(PS1FloorAsmCtx) == 72, "ps1/floor_quads.s reads these offsets");
extern "C" int PS1FloorQuadsAsm(PS1FloorAsmCtx *c);
uint16 s_t[PS1_FLOOR_MAX_COLS + 4]; // a row's drawable tiles (9f: their image and column too)
uint16 s_tImg[PS1_FLOOR_MAX_COLS + 4];
uint8 s_tCol[PS1_FLOOR_MAX_COLS + 4];
struct PS1FloorCtx {
    int32 sA, cA, sX, cX;  // yaw and pitch sin / cos (1024 = 1)
    int32 camX, camY, hq;  // quarter pixels
    int32 x1, y1, x2, y2, half;
};

// The floor region seen between forward distances f0 and f1 (quarter pixels): the screen's width at those depths, plus a
// tile each side; absolute quarter pixels, corners in order
void PS1FloorRegion(const PS1FloorCtx &c, int32 f0, int32 f1, int32 V[4][2])
{
    int32 z0 = (c.cX * f0 + c.sX * c.hq) >> 10, z1 = (c.cX * f1 + c.sX * c.hq) >> 10;
    int32 l0 = (c.half * (z0 < 0 ? 0 : z0) >> 8) + 64, l1 = (c.half * (z1 < 0 ? 0 : z1) >> 8) + 64;
    V[0][0] = (f0 * c.sA - l0 * c.cA) >> 10, V[0][1] = (f0 * c.cA + l0 * c.sA) >> 10;
    V[1][0] = (f0 * c.sA + l0 * c.cA) >> 10, V[1][1] = (f0 * c.cA - l0 * c.sA) >> 10;
    V[2][0] = (f1 * c.sA + l1 * c.cA) >> 10, V[2][1] = (f1 * c.cA - l1 * c.sA) >> 10;
    V[3][0] = (f1 * c.sA - l1 * c.cA) >> 10, V[3][1] = (f1 * c.cA + l1 * c.sA) >> 10;
    for (int32 k = 0; k < 4; ++k) V[k][0] += c.camX, V[k][1] += c.camY;
}

// 9f (UFO item 3b): whether a minimap cell has a chunk that is not chunk 0 (the four-lookup test below), kept per cell
// (bit pairs: known, non-empty) until the layout changes (PS1SetTile's counter) or another layer / scene is drawn
#define PS1_LOD_CELLS 4096
uint32 s_lodKnown[PS1_LOD_CELLS / 32], s_lodSet[PS1_LOD_CELLS / 32];
// ps1-ufo: each cell row's first / last non-empty cell (cleared with the cell bits; bit r of s_lodRowKnown: known;
// lo > hi: none), so a row's span leaves out its empty ends (the track is ~21 % of the map)
#define PS1_LOD_ROWS 128
uint8 s_lodRowLo[PS1_LOD_ROWS], s_lodRowHi[PS1_LOD_ROWS];
uint32 s_lodRowKnown[PS1_LOD_ROWS / 32];
// whether cell (column mx >> 3, row my >> 3) has a chunk that is not chunk 0, through the per-cell cache
inline bool PS1LodCellSet(const TileLayer *layer, const uint16 *chunkRow0, const uint16 *chunkRow1, int32 mx, uint32 cell, bool cache)
{
    const uint32 cw = cell >> 5, cb = 1u << (cell & 31);
    if (cache && (s_lodKnown[cw] & cb))
        return (s_lodSet[cw] & cb) != 0;
    const uint32 c0   = (uint32)mx >> 2;
    const bool any = PS1ChunkAt(layer, chunkRow0, c0) | PS1ChunkAt(layer, chunkRow0, c0 + 1) | PS1ChunkAt(layer, chunkRow1, c0)
                     | PS1ChunkAt(layer, chunkRow1, c0 + 1);
    if (cache)
        s_lodKnown[cw] |= cb, s_lodSet[cw] = any ? s_lodSet[cw] | cb : s_lodSet[cw] & ~cb;
    return any;
}
const TileLayer *s_lodLayer = NULL;
uint32 s_lodWrites = 0, s_lodLoads = 0xFFFFFFFF;
// 63 M11: the minimap page a floor layer draws from, and the texel offset of its map inside it. The UFO stages' Playfield
// is the whole page (offset 0); Pinball's two table floors share one page, each with its own origin (plan 59 4.1).
static inline uint32 PS1LodInfoOf(TileLayer *layer, int32 &ox, int32 &oy)
{
    const int32 li = (int32)(layer - tileLayers);
    const uint32 org = PS1TileLodOrigin(li);
    ox = (int32)(org & 0xFFFF), oy = (int32)(org >> 16);
    return PS1TileLodInfoFor(li);
}

// The minimap cells (8 x 8 tiles, 512 quarter pixels) between forward distances fA (the real tiles' end: cells wholly
// nearer are skipped) and f1; the GTE is set up. Cells over four empty chunks are skipped.
__attribute__((optimize("O2"), noinline)) uint32 PS1FloorLod(const PS1FloorCtx &c, TileLayer *layer, int32 fA, int32 f1)
{
    int32 ox, oy;
    const uint32 info = PS1LodInfoOf(layer, ox, oy);
    if (!info || f1 <= fA)
        return 0;
    const int32 lodFirst = info & 0xFFFF, pw = (info >> 16) & 0xFF;
    const PS1TileImage *images = PS1TileImages();
    const int32 f0 = fA - 363 > 0 ? fA - 363 : 0; // a cell's half diagonal: 256 x 1.414
    int32 V[4][2];
    PS1FloorRegion(c, f0, f1, V);
    int32 minY = V[0][1], maxY = V[0][1];
    for (int32 k = 1; k < 4; ++k) minY = V[k][1] < minY ? V[k][1] : minY, maxY = V[k][1] > maxY ? V[k][1] : maxY;
    const int32 wMask = (1 << layer->widthShift) - 1, hMask = (1 << layer->heightShift) - 1;
    const uint32 cellsW = (uint32)(wMask + 1) >> 3, cells = cellsW * ((uint32)(hMask + 1) >> 3);
    const bool cache = cells <= PS1_LOD_CELLS;
    if (cache && (layer != s_lodLayer || g_ps1LayoutWrites != s_lodWrites || g_ps1TileLoads != s_lodLoads)) {
        memset(s_lodKnown, 0, sizeof(s_lodKnown));
        memset(s_lodRowKnown, 0, sizeof(s_lodRowKnown));
        s_lodLayer = layer, s_lodWrites = g_ps1LayoutWrites, s_lodLoads = g_ps1TileLoads;
    }
    uint32 *w = NULL, *wend = NULL, *bulk = NULL;
    uint32 quads = 0;
    const int32 cy0 = minY >> 9;
    int32 nRows = (maxY >> 9) - cy0 + 1;
    nRows = nRows > PS1_FLOOR_MAX_ROWS ? PS1_FLOOR_MAX_ROWS : nRows;
    PS1FloorSpanner sp;
    PS1_SPANNER_INIT(sp, V, cy0 << 9, 512);
    for (int32 r = 0; r < nRows; ++r) { // each row's cells (9f: first, so that corner rows are shared)
        int32 xmin, xmax;
        s_rowA[r] = 1, s_rowB[r] = 0;
        if (!sp.Row(r, xmin, xmax))
            continue;
        int32 ca = xmin >> 9, cb = xmax >> 9;
        if (cb - ca + 1 > PS1_FLOOR_MAX_COLS)
            cb = ca + PS1_FLOOR_MAX_COLS - 1;
        // ps1-ufo: the row's empty ends left out (its cells there are all skipped below: the same quads, fewer
        // corners projected); only when the span doesn't wrap round the map
        const int32 cellRows = (hMask + 1) >> 3, crow = ((cy0 + r) << 3 & hMask) >> 3, n = cb - ca + 1;
        if (cache && (int32)cellsW <= 256 && cellRows <= PS1_LOD_ROWS && n < (int32)cellsW) {
            const uint32 kw = (uint32)crow >> 5, kb = 1u << (crow & 31);
            if (!(s_lodRowKnown[kw] & kb)) {
                const int32 my = crow << 3;
                const uint16 *row0 = PS1ChunkRow(layer, (uint32)my >> 2), *row1 = PS1ChunkRow(layer, ((uint32)my >> 2) + 1);
                int32 lo = 255, hi = -1;
                for (int32 cx = 0; cx < (int32)cellsW; ++cx)
                    if (PS1LodCellSet(layer, row0, row1, cx << 3, (uint32)crow * cellsW + (uint32)cx, true))
                        lo = cx < lo ? cx : lo, hi = cx;
                s_lodRowLo[crow] = (uint8)(hi < 0 ? 255 : lo), s_lodRowHi[crow] = (uint8)(hi < 0 ? 0 : hi);
                s_lodRowKnown[kw] |= kb;
            }
            const int32 lo = s_lodRowLo[crow], hi = s_lodRowHi[crow], wa = ca & (int32)(cellsW - 1);
            if (lo > hi)
                continue; // (s_rowA > s_rowB: no cells)
            if (wa + n <= (int32)cellsW) {
                const int32 a = wa > lo ? wa : lo, b = wa + n - 1 < hi ? wa + n - 1 : hi;
                if (a > b)
                    continue;
                cb = ca + (b - wa), ca = ca + (a - wa);
            }
        }
        s_rowA[r] = (int16)ca, s_rowB[r] = (int16)cb;
    }
    PS1FloorRows rows;
    for (int32 r = 0; r < nRows; ++r, rows.Next()) {
        if (s_rowB[r] < s_rowA[r]) {
            rows.botN = 0; // (Next: no top corners for the row below)
            continue;
        }
        const int32 cyy = cy0 + r, ca = s_rowA[r];
        const int32 n = s_rowB[r] - ca + 1, zq = (cyy << 9) - c.camY;
        rows.Row(r, nRows, 512, 9, c.camX, c.hq, zq);
        const uint32 *s_xyA = rows.topXY + (ca - rows.topA), *s_xyB = rows.botXY + (ca - rows.botA);
        const uint16 *s_zA = rows.topZ + (ca - rows.topA), *s_zB = rows.botZ + (ca - rows.botA);
        const int32 badT = rows.topBad, badB = rows.botBad;
        const uint16 *chunkRow0 = PS1ChunkRow(layer, (uint32)((cyy << 3) & hMask) >> 2);
        const uint16 *chunkRow1 = PS1ChunkRow(layer, ((uint32)((cyy << 3) & hMask) >> 2) + 1);
        const int32 my = (cyy << 3) & hMask;
        int32 fd = c.sA * (((ca << 9) + 256) - c.camX) + c.cA * (zq + 256);
        const int32 fStep = c.sA * 512;
        for (int32 i = 0; i < n; ++i, fd += fStep) {
            const int32 mx = ((ca + i) << 3) & wMask;
            const uint32 c0 = (uint32)mx >> 2;
            const uint32 cell = ((uint32)my >> 3) * cellsW + ((uint32)mx >> 3), cw = cell >> 5, cb = 1u << (cell & 31);
            if (cache && (s_lodKnown[cw] & cb)) {
                if (!(s_lodSet[cw] & cb))
                    continue; // four empty chunks
            }
            else {
                const bool any = PS1ChunkAt(layer, chunkRow0, c0) | PS1ChunkAt(layer, chunkRow0, c0 + 1)
                                 | PS1ChunkAt(layer, chunkRow1, c0) | PS1ChunkAt(layer, chunkRow1, c0 + 1);
                if (cache)
                    s_lodKnown[cw] |= cb, s_lodSet[cw] = any ? s_lodSet[cw] | cb : s_lodSet[cw] & ~cb;
                if (!any)
                    continue; // four empty chunks
            }
            const int32 fc = fd >> 10;
            if (fc + 363 < fA || fc - 363 > f1)
                continue;
            if ((badT || badB) && (!s_zA[i] || !s_zA[i + 1] || !s_zB[i] || !s_zB[i + 1]))
                continue;
            const int32 mxs = mx + ox, mys = my + oy; // 63 M11: the shared page's origin for this layer
            const uint32 img = (uint32)(lodFirst + (mys >> 8) * pw + (mxs >> 8));
            int32 bank = fd >> 19;
            bank       = bank < 0 ? 0 : bank > 7 ? 7 : bank;
            const uint32 k = s_imgClut[img];
            uint32 clut    = s_attr[k][bank];
            if (!clut) {
                if (w) {
                    PS1ChainBulkEnd((int)((w - bulk) / 10));
                    w = wend = NULL;
                }
                clut = PS1FloorClutFill(k, bank, images[img].clut);
            }
            const uint32 u = (uint32)mxs & 255, vv = (uint32)mys & 255;
            const uint32 uR = u + 8 > 255 ? 255 : u + 8, vB = vv + 8 > 255 ? 255 : vv + 8;
            if (w == wend) {
                if (w)
                    PS1ChainBulkEnd(32);
                w = bulk = (uint32 *)PS1ChainBulkBegin(32, 9);
                if (!w)
                    return quads;
                wend = w + 32 * 10;
            }
            w[0] = 0x2C808080;
            w[1] = s_xyA[i];
            w[2] = clut << 16 | vv << 8 | u;
            w[3] = s_xyA[i + 1];
            w[4] = (uint32)images[img].tpage << 16 | vv << 8 | uR;
            w[5] = s_xyB[i];
            w[6] = vB << 8 | u;
            w[7] = s_xyB[i + 1];
            w[8] = vB << 8 | uR;
            w += 10;
            ++quads;
        }
    }
    if (w && w != bulk)
        PS1ChainBulkEnd((int)((w - bulk) / 10));
    return quads;
}
// ps1-ufo2 (round 3, B2; user 2026-10-22: mild visible simplification allowed): the minimap from g_ps1FloorCoarseFar tiles
// on in cells of 16 x 16 tiles (1024 quarter pixels): the same minimap texels (16 a cell side), a quarter of the quads and
// half the rows of PS1FloorLod's 8 x 8 cells; a cell is drawn when any of its four 8 x 8 cells has a chunk; its fog bank
// is its centre's. Drawn before PS1FloorLod, which takes the nearer cells up to that distance (and covers the overlap).
__attribute__((optimize("O2"), noinline)) uint32 PS1FloorLodCoarse(const PS1FloorCtx &c, TileLayer *layer, int32 fA, int32 f1)
{
    int32 ox, oy;
    const uint32 info = PS1LodInfoOf(layer, ox, oy);
    if (!info || f1 <= fA)
        return 0;
    const int32 lodFirst = info & 0xFFFF, pw = (info >> 16) & 0xFF;
    const PS1TileImage *images = PS1TileImages();
    const int32 f0 = fA - 724 > 0 ? fA - 724 : 0; // a cell's half diagonal: 512 x 1.414
    int32 V[4][2];
    PS1FloorRegion(c, f0, f1, V);
    int32 minY = V[0][1], maxY = V[0][1];
    for (int32 k = 1; k < 4; ++k) minY = V[k][1] < minY ? V[k][1] : minY, maxY = V[k][1] > maxY ? V[k][1] : maxY;
    const int32 wMask = (1 << layer->widthShift) - 1, hMask = (1 << layer->heightShift) - 1;
    const uint32 cellsW = (uint32)(wMask + 1) >> 3, cells = cellsW * ((uint32)(hMask + 1) >> 3);
    const bool cache = cells <= PS1_LOD_CELLS;
    if (cache && (layer != s_lodLayer || g_ps1LayoutWrites != s_lodWrites || g_ps1TileLoads != s_lodLoads)) {
        memset(s_lodKnown, 0, sizeof(s_lodKnown));
        memset(s_lodRowKnown, 0, sizeof(s_lodRowKnown));
        s_lodLayer = layer, s_lodWrites = g_ps1LayoutWrites, s_lodLoads = g_ps1TileLoads;
    }
    uint32 *w = NULL, *wend = NULL, *bulk = NULL;
    uint32 quads = 0;
    const int32 cy0 = minY >> 10;
    int32 nRows = (maxY >> 10) - cy0 + 1;
    nRows = nRows > PS1_FLOOR_MAX_ROWS ? PS1_FLOOR_MAX_ROWS : nRows;
    PS1FloorSpanner sp;
    PS1_SPANNER_INIT(sp, V, cy0 << 10, 1024);
    for (int32 r = 0; r < nRows; ++r) {
        int32 xmin, xmax;
        s_rowA[r] = 1, s_rowB[r] = 0;
        if (!sp.Row(r, xmin, xmax))
            continue;
        int32 ca = xmin >> 10, cb = xmax >> 10;
        if (cb - ca + 1 > PS1_FLOOR_MAX_COLS)
            cb = ca + PS1_FLOOR_MAX_COLS - 1;
        s_rowA[r] = (int16)ca, s_rowB[r] = (int16)cb;
    }
    PS1FloorRows rows;
    for (int32 r = 0; r < nRows; ++r, rows.Next()) {
        if (s_rowB[r] < s_rowA[r]) {
            rows.botN = 0;
            continue;
        }
        const int32 cyy = cy0 + r, ca = s_rowA[r];
        const int32 n = s_rowB[r] - ca + 1, zq = (cyy << 10) - c.camY;
        rows.Row(r, nRows, 1024, 10, c.camX, c.hq, zq);
        const uint32 *xyA = rows.topXY + (ca - rows.topA), *xyB = rows.botXY + (ca - rows.botA);
        const uint16 *zA = rows.topZ + (ca - rows.topA), *zB = rows.botZ + (ca - rows.botA);
        const int32 badT = rows.topBad, badB = rows.botBad;
        const int32 my = (cyy << 4) & hMask; // the cell's first tile row (a multiple of 16: my + 8 stays in the layer)
        const uint16 *cr0 = PS1ChunkRow(layer, (uint32)my >> 2), *cr1 = PS1ChunkRow(layer, ((uint32)my >> 2) + 1);
        const uint16 *cr2 = PS1ChunkRow(layer, ((uint32)my >> 2) + 2), *cr3 = PS1ChunkRow(layer, ((uint32)my >> 2) + 3);
        const uint32 rowCell0 = ((uint32)my >> 3) * cellsW, rowCell1 = rowCell0 + cellsW;
        int32 fd = c.sA * (((ca << 10) + 512) - c.camX) + c.cA * (zq + 512);
        const int32 fStep = c.sA * 1024;
        for (int32 i = 0; i < n; ++i, fd += fStep) {
            const int32 mx = ((ca + i) << 4) & wMask;
            const uint32 cx = (uint32)mx >> 3;
            if (!(PS1LodCellSet(layer, cr0, cr1, mx, rowCell0 + cx, cache) || PS1LodCellSet(layer, cr0, cr1, mx + 8, rowCell0 + cx + 1, cache)
                  || PS1LodCellSet(layer, cr2, cr3, mx, rowCell1 + cx, cache)
                  || PS1LodCellSet(layer, cr2, cr3, mx + 8, rowCell1 + cx + 1, cache)))
                continue; // sixteen empty chunks
            const int32 fc = fd >> 10;
            if (fc + 724 < fA || fc - 724 > f1)
                continue;
            if ((badT || badB) && (!zA[i] || !zA[i + 1] || !zB[i] || !zB[i + 1]))
                continue;
            const int32 mxs = mx + ox, mys = my + oy; // 63 M11: the shared page's origin for this layer
            const uint32 img = (uint32)(lodFirst + (mys >> 8) * pw + (mxs >> 8));
            int32 bank = fd >> 19;
            bank       = bank < 0 ? 0 : bank > 7 ? 7 : bank;
            const uint32 k = s_imgClut[img];
            uint32 clut    = s_attr[k][bank];
            if (!clut) {
                if (w) {
                    PS1ChainBulkEnd((int)((w - bulk) / 10));
                    w = wend = NULL;
                }
                clut = PS1FloorClutFill(k, bank, images[img].clut);
            }
            const uint32 u = (uint32)mxs & 255, vv = (uint32)mys & 255;
            const uint32 uR = u + 16 > 255 ? 255 : u + 16, vB = vv + 16 > 255 ? 255 : vv + 16;
            if (w == wend) {
                if (w)
                    PS1ChainBulkEnd(32);
                w = bulk = (uint32 *)PS1ChainBulkBegin(32, 9);
                if (!w)
                    return quads;
                wend = w + 32 * 10;
            }
            w[0] = 0x2C808080;
            w[1] = xyA[i];
            w[2] = clut << 16 | vv << 8 | u;
            w[3] = xyA[i + 1];
            w[4] = (uint32)images[img].tpage << 16 | vv << 8 | uR;
            w[5] = xyB[i];
            w[6] = vB << 8 | u;
            w[7] = xyB[i + 1];
            w[8] = vB << 8 | uR;
            w += 10;
            ++quads;
        }
    }
    if (w && w != bulk)
        PS1ChainBulkEnd((int)((w - bulk) / 10));
    return quads;
}

// 7.5: a "3D Floor" / "3D Roof" layer (the Dreamcast's texture: its 16x16 tiles pre-rendered into one 256x256 page,
// PS1TilePatternImage) as cells of the whole pattern (1024 quarter pixels), repeated, over the forward distances its clip
// lines see (a roof: above the camera, hq < 0) up to g_ps1FloorLodFar; each cell the bank of its centre's distance
// (bank shift / bias from scanlines[2].position.y). The GTE is set up.
__attribute__((optimize("O2"), noinline)) void PS1FloorPattern(const PS1FloorCtx &c, int32 f0, int32 f1, uint32 bankInfo)
{
    const uint32 img = PS1TilePatternImage();
    if (!img || f1 <= f0) {
        g_ps1FloorTodo = g_ps1FloorTodo + !img;
        return;
    }
    const PS1TileImage &ti = PS1TileImages()[img];
    const int32 shift = (bankInfo & 0xFF) ? (int32)(bankInfo & 0xFF) + 4 : 19, bias = (int32)((bankInfo >> 8) & 0xFF);
    int32 V[4][2];
    PS1FloorRegion(c, f0, f1, V);
    int32 minY = V[0][1], maxY = V[0][1];
    for (int32 k = 1; k < 4; ++k) minY = V[k][1] < minY ? V[k][1] : minY, maxY = V[k][1] > maxY ? V[k][1] : maxY;
    uint32 *w = NULL, *wend = NULL, *bulk = NULL;
    uint32 quads = 0;
    const int32 cy0 = minY >> 10;
    int32 nRows = (maxY >> 10) - cy0 + 1;
    nRows = nRows > PS1_FLOOR_MAX_ROWS ? PS1_FLOOR_MAX_ROWS : nRows;
    PS1FloorSpanner sp;
    PS1_SPANNER_INIT(sp, V, cy0 << 10, 1024);
    for (int32 r = 0; r < nRows; ++r) { // each row's cells (9f: first, so that corner rows are shared)
        int32 xmin, xmax;
        s_rowA[r] = 1, s_rowB[r] = 0;
        if (!sp.Row(r, xmin, xmax))
            continue;
        int32 ca = xmin >> 10, cb = xmax >> 10;
        if (cb - ca + 1 > PS1_FLOOR_MAX_COLS)
            cb = ca + PS1_FLOOR_MAX_COLS - 1;
        s_rowA[r] = (int16)ca, s_rowB[r] = (int16)cb;
    }
    PS1FloorRows rows;
    for (int32 r = 0; r < nRows; ++r, rows.Next()) {
        if (s_rowB[r] < s_rowA[r]) {
            rows.botN = 0;
            continue;
        }
        const int32 cyy = cy0 + r, ca = s_rowA[r];
        const int32 n = s_rowB[r] - ca + 1, zq = (cyy << 10) - c.camY;
        rows.Row(r, nRows, 1024, 10, c.camX, c.hq, zq);
        const uint32 *s_xyA = rows.topXY + (ca - rows.topA), *s_xyB = rows.botXY + (ca - rows.botA);
        const uint16 *s_zA = rows.topZ + (ca - rows.topA), *s_zB = rows.botZ + (ca - rows.botA);
        const int32 badT = rows.topBad, badB = rows.botBad;
        const uint32 vv = 0, vB = 255;
        int32 fd = c.sA * (((ca << 10) + 512) - c.camX) + c.cA * (zq + 512);
        const int32 fStep = c.sA * 1024;
        for (int32 i = 0; i < n; ++i, fd += fStep) {
            if ((badT || badB) && (!s_zA[i] || !s_zA[i + 1] || !s_zB[i] || !s_zB[i + 1]))
                continue;
            int32 fc = fd >> 10;
            if (fc < f0 - 725 || fc > f1 + 725)
                continue;
            int32 bank = (fd >> shift) - bias;
            bank       = bank < 0 ? 0 : bank > 7 ? 7 : bank;
            uint32 clut = s_attr[PS1_FLOOR_K8][bank];
            if (!clut) {
                if (w) {
                    PS1ChainBulkEnd((int)((w - bulk) / 10));
                    w = wend = NULL;
                }
                clut = PS1FloorClutFill(PS1_FLOOR_K8, bank, 0xFFFF);
            }
            const uint32 u = 0, uR = 255;
            if (w == wend) {
                if (w)
                    PS1ChainBulkEnd(32);
                w = bulk = (uint32 *)PS1ChainBulkBegin(32, 9);
                if (!w)
                    return;
                wend = w + 32 * 10;
            }
            w[0] = 0x2C808080;
            w[1] = s_xyA[i];
            w[2] = clut << 16 | vv << 8 | u;
            w[3] = s_xyA[i + 1];
            w[4] = (uint32)ti.tpage << 16 | vv << 8 | uR;
            w[5] = s_xyB[i];
            w[6] = vB << 8 | u;
            w[7] = s_xyB[i + 1];
            w[8] = vB << 8 | uR;
            w += 10;
            ++quads;
        }
    }
    if (w && w != bulk)
        PS1ChainBulkEnd((int)((w - bulk) / 10));
    g_ps1FloorLodQuads = g_ps1FloorLodQuads + quads;
}
} // namespace

__attribute__((optimize("O2"))) void PS1UfoFloorDraw(TileLayer *layer)
{
    const ScanlineInfo *sl = scanlines;
    const int32 sA = sl[1].deform.x, cA = sl[1].deform.y, sX = sl[1].position.x, cX = sl[1].position.y;
    const int32 camX = sl[2].deform.x >> 14, hq = sl[2].deform.y >> 14, camY = sl[2].position.x >> 14; // quarter pixels
    const int32 x1 = currentScreen->clipBound_X1, y1 = currentScreen->clipBound_Y1;
    const int32 x2 = currentScreen->clipBound_X2, y2 = currentScreen->clipBound_Y2;
    const int32 cx = currentScreen->center.x, cy = currentScreen->center.y;
    const uint16 *table = PS1TileImageTable();
    const uint32 minor = (uint32)sl[0].deform.y;
    const bool pattern = minor == SCANLINE_MINOR_MAGIC_UFO_FLOOR || minor == SCANLINE_MINOR_MAGIC_ROOF;
    if (!table || x2 <= x1 || y2 <= y1 || (!pattern && hq <= 0) || !hq || hq > 0x7000 || hq < -0x7000 || !layer->xsize) {
        g_ps1FloorTodo = g_ps1FloorTodo + 1;
        return;
    }
    uint16 t0 = PS1_HBL();
    if (s_cl != PS1TileCluts() || s_loads != g_ps1TileLoads || s_lod != PS1TileLodInfo())
        PS1FloorClutsReset();
    if (s_frame != (uint32)g_ps1PerfFrames) { // a new frame (not a new draw: UFO2 / 4 / 6 draw two floor layers a
        s_frame = (uint32)g_ps1PerfFrames;    // frame, and the second would miss the re-upload after a fade)
        memset(s_attr, 0, sizeof(s_attr));
        uint32 fades = g_ps1FadeCalls + g_ps1FadeOpaque + g_ps1TintGrey; // the last frame recoloured its CLUT uploads
        s_force      = fades != s_fadeSeen || g_ps1VramGen != s_vramGen;
        s_fadeSeen = fades, s_vramGen = g_ps1VramGen;
    }

    if (pattern) { // 7.5: the forward distances its two clip lines see (a line past the horizon: to the draw distance)
        const PS1FloorCtx pc = { sA, cA, sX, cX, camX, camY, hq, x1, y1, x2, y2, cx - x1 > x2 - cx ? cx - x1 : x2 - cx };
        int32 fa = 0x7FFF, fb = 0, lines = 0;
        for (int32 e = 0; e < 2; ++e) {
            int32 i = (e ? y2 : y1) - cy, den = i * cX + 256 * sX;
            if (!den || (den > 0) != (hq > 0))
                continue;
            int64 f = PS1FloorMulDiv(hq, 256 * cX - sX * i, den);
            if (f <= 0)
                continue;
            int32 fi = f > 0x7FFF ? 0x7FFF : (int32)f;
            fa = fi < fa ? fi : fa, fb = fi > fb ? fi : fb, ++lines;
        }
        if (!lines)
            return;
        int32 lodFar = g_ps1FloorLodFar * 64;
        lodFar       = lodFar > 0x6400 ? 0x6400 : lodFar;
        int32 f1     = lines == 2 ? (fb + 64 < lodFar ? fb + 64 : lodFar) : lodFar;
        int32 f0     = fa - 64 > 0 ? fa - 64 : 0;
        int32 R[9] = { -cA * 4, 0, sA * 4, -(sX * sA) >> 8, cX * 4, -(sX * cA) >> 8, (cX * sA) >> 8, sX * 4, (cX * cA) >> 8 };
        PS1Gte3DSetup((const int32_t *)R, 0, 0, 0, 256, cx << 16, cy << 16);
        PS1ChainDrawArea(x1, y1, x2, y2);
        PS1FloorPattern(pc, f0, f1, (uint32)sl[2].position.y);
        PS1ChainDrawAreaReset();
        g_ps1FloorDraws = g_ps1FloorDraws + 1;
        g_ps1FloorHbl   = g_ps1FloorHbl + (uint16)(PS1_HBL() - t0);
        return;
    }
    // forward distances seen (quarter pixels): the bottom clip line's, up to the top line's (the horizon's clip); real
    // tiles up to g_ps1FloorFar, the minimap up to g_ps1FloorLodFar
    int32 iB = y2 - cy, iT = y1 - cy;
    int32 dB = iB * cX + 256 * sX, dT = iT * cX + 256 * sX;
    if (dB <= 0) // the whole clip is above the horizon
        return;
    int32 fNear = (int32)PS1FloorMulDiv(hq, 256 * cX - sX * iB, dB) - 64, fTop = 0x7FFF;
    if (dT > 0) {
        int64 f = PS1FloorMulDiv(hq, 256 * cX - sX * iT, dT) + 64;
        fTop    = f < 0x7FFF ? (int32)f : 0x7FFF;
    }
    fNear     = fNear < 0 ? 0 : fNear;
    int32 far = g_ps1FloorFar * 64, lodFar = (g_ps1FloorMapFar < g_ps1FloorLodFar ? g_ps1FloorMapFar : g_ps1FloorLodFar) * 64;
    far       = fTop < far ? fTop : far;
    lodFar    = fTop < lodFar ? fTop : lodFar;
    lodFar    = lodFar > 0x6400 ? 0x6400 : lodFar; // corners within 16 bits (the region reaches 1.18 x its distance)
    const PS1FloorCtx ctx = { sA, cA, sX, cX, camX, camY, hq, x1, y1, x2, y2, cx - x1 > x2 - cx ? cx - x1 : x2 - cx };

    // the camera: rows x (right), y (down), z (depth) of floor points (dx, H, dz), 4.12
    int32 R[9] = { -cA * 4, 0, sA * 4, -(sX * sA) >> 8, cX * 4, -(sX * cA) >> 8, (cX * sA) >> 8, sX * 4, (cX * cA) >> 8 };
    PS1Gte3DSetup((const int32_t *)R, 0, 0, 0, 256, cx << 16, cy << 16);
    PS1ChainDrawArea(x1, y1, x2, y2);
    // the minimap beyond the real tiles (drawn first: the real tiles cover it where both are)
    const int32 lodA = far > fNear ? far : fNear, coarse = g_ps1FloorCoarseFar * 64; // (round 3, B2)
    if (coarse > lodA && coarse < lodFar) {
        g_ps1FloorLodQuads = g_ps1FloorLodQuads + PS1FloorLodCoarse(ctx, layer, coarse, lodFar);
        g_ps1FloorLodQuads = g_ps1FloorLodQuads + PS1FloorLod(ctx, layer, lodA, coarse);
    }
    else
        g_ps1FloorLodQuads = g_ps1FloorLodQuads + PS1FloorLod(ctx, layer, lodA, lodFar);
    if (far <= fNear) {
        PS1ChainDrawAreaReset();
        g_ps1MaskRowsEnd = 0x7FFF; // (the minimap reaches the bottom)
        return;
    }
    int32 V[4][2];
    PS1FloorRegion(ctx, fNear, far, V);
    int32 minY = V[0][1], maxY = V[0][1];
    for (int32 k = 1; k < 4; ++k) minY = V[k][1] < minY ? V[k][1] : minY, maxY = V[k][1] > maxY ? V[k][1] : maxY;
    int32 ty0 = minY >> 6, nRows = (maxY >> 6) - ty0 + 1;
    if (nRows > PS1_FLOOR_MAX_ROWS)
        nRows = PS1_FLOOR_MAX_ROWS, g_ps1FloorTodo = g_ps1FloorTodo + 1;

    // pass 1: each tile row's columns (the region's span less its empty ends: whole empty chunks skipped)
    const int32 wMask = (1 << layer->widthShift) - 1, hMask = (1 << layer->heightShift) - 1;
    uint32 todo = 0;
    PS1FloorSpanner sp;
    PS1_SPANNER_INIT(sp, V, ty0 << 6, 64);
    for (int32 r = 0; r < nRows; ++r) {
        int32 ty = ty0 + r, xmin, xmax;
        s_rowA[r] = 1, s_rowB[r] = 0;
        if (!sp.Row(r, xmin, xmax))
            continue;
        int32 txa = xmin >> 6, txb = xmax >> 6;
        const uint16 *chunkRow = PS1ChunkRow(layer, (uint32)(ty & hMask) >> 2);
        const uint16 *rowSel   = ps1ChunkPool + (ty & 3);
        while (txa <= txb) {
            uint32 ch = PS1ChunkAt(layer, chunkRow, (uint32)(txa & wMask) >> 2);
            if (!ch) { // chunk 0: all empty
                txa = (txa | 3) + 1;
                continue;
            }
            if (PS1ChunkTiles(rowSel, ch)[txa & 3] != 0xFFFF)
                break;
            ++txa;
        }
        while (txb >= txa) {
            uint32 ch = PS1ChunkAt(layer, chunkRow, (uint32)(txb & wMask) >> 2);
            if (!ch) {
                txb = (txb & ~3) - 1;
                continue;
            }
            if (PS1ChunkTiles(rowSel, ch)[txb & 3] != 0xFFFF)
                break;
            --txb;
        }
        if (txa > txb)
            continue;
        if (txb - txa + 1 > PS1_FLOOR_MAX_COLS) {
            ++todo;
            continue;
        }
        s_rowA[r] = (int16)txa, s_rowB[r] = (int16)txb; // columns unwrapped (the map wraps at the lookups)
    }

    const PS1TileImage *images = PS1TileImages();
    // pass 2: corner row r (between tile rows r - 1 and r) covers both rows' columns, projected once
    uint32 *topXY = s_xyA, *botXY = s_xyB;
    uint16 *topZ = s_zA, *botZ = s_zB;
    int32 topA = 0, topN = 0, topBad = 0; // the top corner row's first column, count, flagged corners
    uint32 *w = NULL, *wend = NULL, *bulk = NULL;
    uint32 quads = 0, banks = 0;
    // ps1-ufo2: forward distances (x 1024, as fd) from which the mip levels draw (none without them)
    const int32 mip1 = s_mipN ? g_ps1FloorMip1 << 16 : 0x7FFFFFFF, mip2 = s_mipLevels > 1 ? g_ps1FloorMip2 << 16 : 0x7FFFFFFF;
    const uint32 mipOff[3] = { 0, s_mip1, s_mip1 + s_mipN };
    // ps1-ufo2: the lowest screen row a real tile reaches (a tile row is a line on screen: its ends' corners bound it; the
    // minimap lies farther, higher); rows with flagged corners: no bound (g_ps1MaskRowsEnd, UFO7's plasma)
    int32 maskEnd = -0x8000;
    for (int32 r = 0; r < nRows; ++r) {
        int32 txa = s_rowA[r], txb = s_rowB[r];
        if (txb < txa) {
            topN = 0;
            continue;
        }
        const int32 ty = ty0 + r, zq = (ty << 6) - camY;
        if (!topN || txa < topA || txb + 1 >= topA + topN) { // the row above had no corners here: project them
            topA = txa, topN = txb - txa + 2;
            topBad = PS1FloorProject((txa << 6) - camX, hq, zq, topN, topXY, topZ);
        }
        // the bottom corner row: this row's columns and the next row's
        int32 ba = txa, bb = txb;
        if (r + 1 < nRows && s_rowB[r + 1] >= s_rowA[r + 1])
            ba = s_rowA[r + 1] < ba ? s_rowA[r + 1] : ba, bb = s_rowB[r + 1] > bb ? s_rowB[r + 1] : bb;
        if (bb - ba + 1 > PS1_FLOOR_MAX_COLS) // the union too wide: this row's only (the next projects its own)
            ba = txa, bb = txb;
        const int32 botA = ba, botN = bb - ba + 2;
        const int32 botBad = PS1FloorProject((ba << 6) - camX, hq, zq + 64, botN, botXY, botZ);
        const uint32 *tXY = topXY + (txa - topA), *bXY = botXY + (txa - botA);
        const uint16 *tZ = topZ + (txa - topA), *bZ = botZ + (txa - botA);
        // the row's tiles through their 4x4 chunks (a whole empty chunk at once)
        const uint16 *chunkRow = PS1ChunkRow(layer, (uint32)(ty & hMask) >> 2);
        const uint16 *rowSel   = ps1ChunkPool + (ty & 3);
        const int32 n          = txb - txa + 1;
        // 9f (UFO item 3b): only the row's drawable tiles (most of a row's span is the empty space around the playfield):
        // column, tile and image, in column order
        int32 nn = 0;
        for (int32 i = 0, tx = txa; i < n;) {
            uint32 ch = PS1ChunkAt(layer, chunkRow, (uint32)(tx & wMask) >> 2);
            int32 k   = 4 - (tx & 3); // to the chunk's end
            k         = k > n - i ? n - i : k;
            if (ch) {
                const uint16 *q = PS1ChunkTiles(rowSel, ch) + (tx & 3);
                for (int32 e = 0; e < k; ++e) {
                    const uint32 t = q[e];
                    if (t == 0xFFFF)
                        continue;
                    const uint32 img = table[t & 0x3FF];
                    if (img == PS1_TILE_NONE)
                        continue;
                    s_t[nn] = (uint16)t, s_tImg[nn] = (uint16)img, s_tCol[nn] = (uint8)(i + e), ++nn;
                }
            }
            i += k, tx += k;
        }
        // the quads; each tile takes the bank of its centre's forward distance (x 1024)
        const int32 fd0   = sA * (((txa << 6) + 32) - camX) + cA * (zq + 32);
        const int32 fStep = sA * 64;
        const bool check  = topBad || botBad; // corners flagged: test each tile's
        if (check)
            maskEnd = 0x7FFF;
        else if (nn) {
            const int32 a = s_tCol[0], b = s_tCol[nn - 1] + 1;
            const int32 m0 = (int32)tXY[a] >> 16 > (int32)tXY[b] >> 16 ? (int32)tXY[a] >> 16 : (int32)tXY[b] >> 16;
            const int32 m1 = (int32)bXY[a] >> 16 > (int32)bXY[b] >> 16 ? (int32)bXY[a] >> 16 : (int32)bXY[b] >> 16;
            const int32 m  = m0 > m1 ? m0 : m1;
            maskEnd        = m > maskEnd ? m : maskEnd;
        }
        if (g_ps1FloorAsm) { // 9f (UFO item 3c): ps1/floor_quads.s, the same quads
            PS1FloorAsmCtx c = { 0, nn, s_tCol, s_t, s_tImg, tXY, bXY, tZ, bZ, check, fd0, fStep, images, s_imgClut,
                                 &s_attr[0][0], w, wend, 15 };
            int32 lastClut = -1;
            // ps1-ufo2: the row in runs of one mip level (fd is linear along the row: at most three runs; the ends'
            // levels tell when the whole row is one)
#define PS1_FLOOR_LEVEL(j) ((fd0 + fStep * s_tCol[j]) >= mip2 ? 2 : (fd0 + fStep * s_tCol[j]) >= mip1 ? 1 : 0)
            int32 lvl = nn ? PS1_FLOOR_LEVEL(0) : 0;
            int32 end = nn && PS1_FLOOR_LEVEL(nn - 1) != lvl ? 1 : nn;
            while (end < nn && PS1_FLOOR_LEVEL(end) == lvl) ++end;
            c.nn = end, c.images = images + mipOff[lvl], c.ext = 15 >> lvl;
            for (;;) {
                uint32 *w0 = c.w;
                const int32 st = PS1FloorQuadsAsm(&c);
                w = c.w, quads += (uint32)(w - w0) / 10;
                if (!st) {
                    if (end == nn)
                        break;
                    lvl = PS1_FLOOR_LEVEL(end), c.j = end; // the next run
                    while (++end < nn && PS1_FLOOR_LEVEL(end) == lvl) {}
                    c.nn = end, c.images = images + mipOff[lvl], c.ext = 15 >> lvl;
                    continue;
                }
                if (st == 1) { // the bulk full (or none open)
                    if (w)
                        PS1ChainBulkEnd(32);
                    w = bulk = (uint32 *)PS1ChainBulkBegin(32, 9);
                    if (!w)
                        goto done;
                    wend = w + 32 * 10, c.w = w, c.wend = wend;
                    continue;
                }
                // the tile's CLUT copy for its bank: as the C loop (close the bulk, fill, go on)
                const int32 j = c.j, i = s_tCol[j];
                const uint32 img = s_tImg[j], k = s_imgClut[img];
                int32 bank = (fd0 + fStep * i) >> 19;
                bank       = bank < 0 ? 0 : bank > 7 ? 7 : bank;
                if (w) {
                    PS1ChainBulkEnd((int)((w - bulk) / 10));
                    w = wend = NULL, c.w = c.wend = NULL;
                }
                PS1FloorClutFill(k, (uint32)bank, images[img].clut);
                banks |= 1u << bank;
                if (lastClut == j || !s_attr[k][bank]) { // (an attribute of 0, never seen: CLUT slots are on rows 240+): skipped, counted
                    g_ps1FloorTodo = g_ps1FloorTodo + 1;
                    ++c.j;
                }
                lastClut = j;
            }
            goto rowDone;
        }
        for (int32 j = 0; j < nn; ++j) {
            const int32 i  = s_tCol[j];
            const int32 fd = fd0 + fStep * i;
            const uint32 t = s_t[j];
            if (check && (!tZ[i] || !tZ[i + 1] || !bZ[i] || !bZ[i + 1]))
                continue;
            const uint32 img = s_tImg[j];
            const int32 lvl  = fd >= mip2 ? 2 : fd >= mip1 ? 1 : 0, e = 15 >> lvl;
            const PS1TileImage &ti = images[img + mipOff[lvl]];
            int32 bank = fd >> 19;
            bank       = bank < 0 ? 0 : bank > 7 ? 7 : bank;
            const uint32 k = s_imgClut[img];
            uint32 clut    = s_attr[k][bank];
            if (!clut) { // first use this frame: may upload a copy, which goes into the chain: close the bulk first
                if (w) {
                    PS1ChainBulkEnd((int)((w - bulk) / 10));
                    w = wend = NULL;
                }
                clut = PS1FloorClutFill(k, bank, ti.clut);
                banks |= 1u << bank;
            }
            // texels [u, u + 15] x [v, v + 15]: the GPU's first pixel takes the edge's texel exactly, so an edge at
            // u + 16 would show the neighbouring image's column (the floor's tiles squeezed by 1/16, not seen)
            const uint32 uv = *(const uint16 *)&ti; // u | v << 8
            uint32 tl = uv, tr = uv + e, bl = uv + (e << 8), br = uv + (e << 8) + e;
            if (t & 0x400) { // H-flip
                uint32 x = tl;
                tl = tr, tr = x, x = bl, bl = br, br = x;
            }
            if (t & 0x800) { // V-flip
                uint32 x = tl;
                tl = bl, bl = x, x = tr, tr = br, br = x;
            }
            if (w == wend) {
                if (w)
                    PS1ChainBulkEnd(32);
                w = bulk = (uint32 *)PS1ChainBulkBegin(32, 9);
                if (!w)
                    goto done;
                wend = w + 32 * 10;
            }
            w[0] = 0x2C808080;
            w[1] = tXY[i];
            w[2] = clut << 16 | tl;
            w[3] = tXY[i + 1];
            w[4] = (uint32)ti.tpage << 16 | tr;
            w[5] = bXY[i];
            w[6] = bl;
            w[7] = bXY[i + 1];
            w[8] = br;
            w += 10;
            ++quads;
        }
    rowDone:
        // the bottom corners become the next row's top ones
        uint32 *sx = topXY;
        uint16 *sz = topZ;
        topXY = botXY, topZ = botZ, botXY = sx, botZ = sz, topA = botA, topN = botN, topBad = botBad;
    }
done:
    if (w && w != bulk)
        PS1ChainBulkEnd((int)((w - bulk) / 10));
    PS1ChainDrawAreaReset();
    g_ps1MaskRowsEnd = maskEnd >= 0x7FFF || maskEnd == -0x8000 ? 0x7FFF : maskEnd + 1; // (no real tile: the minimap may reach low)
    if (todo)
        g_ps1FloorTodo = g_ps1FloorTodo + todo;
    g_ps1FloorBanks = banks;
    g_ps1FloorQuads = g_ps1FloorQuads + quads;
    g_ps1FloorDraws = g_ps1FloorDraws + 1;
    g_ps1FloorHbl   = g_ps1FloorHbl + (uint16)(PS1_HBL() - t0);
}
