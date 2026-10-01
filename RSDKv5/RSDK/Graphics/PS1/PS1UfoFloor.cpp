// PS1 port (docs/32 7.4): the UFO stages' floor as GTE-projected tile quads, the Dreamcast port's "3D tiles".
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
volatile uint32 g_ps1FloorDraws = 0, g_ps1FloorQuads = 0, g_ps1FloorHbl = 0, g_ps1FloorTodo = 0, g_ps1FloorCluts = 0;
volatile uint32 g_ps1FloorBanks = 0; // bit b: bank b drawn (the last frame)
volatile int32 g_ps1FloorFar    = 24;  // real tiles' distance, tiles (the Dreamcast: 40, then its LOD)
volatile int32 g_ps1FloorLodFar = 160; // the minimap's (up to the horizon's clip line)
volatile uint32 g_ps1FloorLodQuads = 0; // minimap cells drawn (total)
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
uint16 s_attr[PS1_FLOOR_CLUTS + 2][8]; // this frame: CLUT attribute per (s_imgClut, bank), 0: not checked yet
uint16 s_raw[8][PS1_FLOOR_CLUTS][16];  // [bank][clut][entry]: the raw colours of the uploaded copies
uint16 s_rawOk[8];                    // bit c: s_raw[b][c] valid
uint32 s_lod = 0;                     // PS1TileLodInfo() when the tables were built
uint32 s_sum8[8], s_sum8Ok;           // 8-bit rows: a checksum of the bank's colours uploaded (bit b: valid)
uint32 s_frame = 0xFFFFFFFF, s_fadeSeen = 0xFFFFFFFF, s_vramGen = 0xFFFFFFFF;
bool s_force = true;
uint16 s_row[256];

void PS1FloorClutsReset()
{
    s_cl    = PS1TileCluts();
    s_loads = g_ps1TileLoads;
    s_nc    = s_cl ? PS1TileClutCount() : 0;
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
}

// the attribute for (k = s_imgClut[img], bank) this frame, uploading the copy if needed (no bulk may be open: the
// upload goes into the chain)
uint32 PS1FloorClutFill(uint32 k, int32 bank, uint32 ownClut)
{
    uint32 attr = ownClut;
    if (k == PS1_FLOOR_K8) { // 8-bit: the bank's row
        attr = (uint32)(240 + bank) << 6;
        if (bank) {
            uint32 sum = 0;
            for (int32 i = 1; i < 256; ++i) sum = (sum << 1 | sum >> 31) + fullPalette[bank][i];
            if (s_force || !(s_sum8Ok & (1u << bank)) || s_sum8[bank] != sum) {
                s_row[0] = 0;
                for (int32 i = 1; i < 256; ++i) s_row[i] = PS1FloorClutColour(fullPalette[bank][i]);
                PS1UploadClut(s_row, 256, 0, (int16)(240 + bank));
                s_sum8[bank] = sum, s_sum8Ok |= 1u << bank;
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
            int32 cx = dy ? p[0] + (int32)(((int64)(q[0] - p[0]) * (cy - p[1])) / dy) : p[0];
            if (!any || cx < xmin)
                xmin = cx;
            if (!any || cx > xmax)
                xmax = cx;
            any = true;
        }
    }
    return any;
}

#define PS1_FLOOR_MAX_ROWS 128 // tile rows of the region

// a corner row: floor points (x0 + 64 i, hq, zq) relative to the camera, i < n, projected
uint32 s_xyA[PS1_FLOOR_MAX_COLS + 2], s_xyB[PS1_FLOOR_MAX_COLS + 2];
uint16 s_zA[PS1_FLOOR_MAX_COLS + 2], s_zB[PS1_FLOOR_MAX_COLS + 2];
int16 s_rowA[PS1_FLOOR_MAX_ROWS], s_rowB[PS1_FLOOR_MAX_ROWS]; // per tile row: first / last column (b < a: none)

// -> the corners the GTE flagged (depth 0: behind, too near — nearer than H / 2, 32 pixels — or far off screen)
inline int32 PS1FloorProject(int32 x0, int32 hq, int32 zq, int32 n, uint32 *xy, uint16 *z)
{
    int16 v[3 * (PS1_FLOOR_MAX_COLS + 2)];
    for (int32 i = 0; i < n; ++i, x0 += 64) v[3 * i] = (int16)x0, v[3 * i + 1] = (int16)hq, v[3 * i + 2] = (int16)zq;
    return PS1Gte3DProject(v, n, (uint32_t *)xy, z);
}
uint16 s_t[PS1_FLOOR_MAX_COLS + 4]; // a row's tiles (0xFFFF: empty)
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

// The minimap cells (8 x 8 tiles, 512 quarter pixels) between forward distances fA (the real tiles' end: cells wholly
// nearer are skipped) and f1; the GTE is set up. Cells over four empty chunks are skipped.
__attribute__((optimize("O2"), noinline)) uint32 PS1FloorLod(const PS1FloorCtx &c, TileLayer *layer, int32 fA, int32 f1)
{
    const uint32 info = PS1TileLodInfo();
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
    uint32 *w = NULL, *wend = NULL, *bulk = NULL;
    uint32 quads = 0;
    for (int32 cyy = minY >> 9; cyy <= maxY >> 9; ++cyy) {
        int32 xmin, xmax;
        if (!PS1FloorSpan(V, cyy << 9, (cyy << 9) + 511, xmin, xmax))
            continue;
        int32 ca = xmin >> 9, cb = xmax >> 9;
        if (cb - ca + 1 > PS1_FLOOR_MAX_COLS)
            cb = ca + PS1_FLOOR_MAX_COLS - 1;
        const int32 n = cb - ca + 1, zq = (cyy << 9) - c.camY;
        int16 v[3 * (PS1_FLOOR_MAX_COLS + 2)]; // the cells' corners, 512 apart
        for (int32 i = 0; i <= n; ++i) v[3 * i] = (int16)(((ca + i) << 9) - c.camX), v[3 * i + 1] = (int16)c.hq, v[3 * i + 2] = (int16)zq;
        const int32 badT = PS1Gte3DProject(v, n + 1, (uint32_t *)s_xyA, s_zA);
        for (int32 i = 0; i <= n; ++i) v[3 * i + 2] = (int16)(zq + 512);
        const int32 badB = PS1Gte3DProject(v, n + 1, (uint32_t *)s_xyB, s_zB);
        const uint16 *chunkRow0 = PS1ChunkRow(layer, (uint32)((cyy << 3) & hMask) >> 2);
        const uint16 *chunkRow1 = PS1ChunkRow(layer, ((uint32)((cyy << 3) & hMask) >> 2) + 1);
        const int32 my = (cyy << 3) & hMask;
        int32 fd = c.sA * (((ca << 9) + 256) - c.camX) + c.cA * (zq + 256);
        const int32 fStep = c.sA * 512;
        for (int32 i = 0; i < n; ++i, fd += fStep) {
            const int32 mx = ((ca + i) << 3) & wMask;
            const uint32 c0 = (uint32)mx >> 2;
            if (!(PS1ChunkAt(layer, chunkRow0, c0) | PS1ChunkAt(layer, chunkRow0, c0 + 1) | PS1ChunkAt(layer, chunkRow1, c0)
                  | PS1ChunkAt(layer, chunkRow1, c0 + 1)))
                continue; // four empty chunks
            const int32 fc = fd >> 10;
            if (fc + 363 < fA || fc - 363 > f1)
                continue;
            if ((badT || badB) && (!s_zA[i] || !s_zA[i + 1] || !s_zB[i] || !s_zB[i + 1]))
                continue;
            const uint32 img = (uint32)(lodFirst + (my >> 8) * pw + (mx >> 8));
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
            const uint32 u = mx & 255, vv = my & 255;
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
    for (int32 cyy = minY >> 10; cyy <= maxY >> 10; ++cyy) {
        int32 xmin, xmax;
        if (!PS1FloorSpan(V, cyy << 10, (cyy << 10) + 1023, xmin, xmax))
            continue;
        int32 ca = xmin >> 10, cb = xmax >> 10;
        if (cb - ca + 1 > PS1_FLOOR_MAX_COLS)
            cb = ca + PS1_FLOOR_MAX_COLS - 1;
        const int32 n = cb - ca + 1, zq = (cyy << 10) - c.camY;
        int16 v[3 * (PS1_FLOOR_MAX_COLS + 2)];
        for (int32 i = 0; i <= n; ++i) v[3 * i] = (int16)(((ca + i) << 10) - c.camX), v[3 * i + 1] = (int16)c.hq, v[3 * i + 2] = (int16)zq;
        const int32 badT = PS1Gte3DProject(v, n + 1, (uint32_t *)s_xyA, s_zA);
        for (int32 i = 0; i <= n; ++i) v[3 * i + 2] = (int16)(zq + 1024);
        const int32 badB = PS1Gte3DProject(v, n + 1, (uint32_t *)s_xyB, s_zB);
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
            int64 f = (int64)hq * (256 * cX - sX * i) / den;
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
    int32 fNear = (int32)((int64)hq * (256 * cX - sX * iB) / dB) - 64, fTop = 0x7FFF;
    if (dT > 0) {
        int64 f = (int64)hq * (256 * cX - sX * iT) / dT + 64;
        fTop    = f < 0x7FFF ? (int32)f : 0x7FFF;
    }
    fNear     = fNear < 0 ? 0 : fNear;
    int32 far = g_ps1FloorFar * 64, lodFar = g_ps1FloorLodFar * 64;
    far       = fTop < far ? fTop : far;
    lodFar    = fTop < lodFar ? fTop : lodFar;
    lodFar    = lodFar > 0x6400 ? 0x6400 : lodFar; // corners within 16 bits (the region reaches 1.18 x its distance)
    const PS1FloorCtx ctx = { sA, cA, sX, cX, camX, camY, hq, x1, y1, x2, y2, cx - x1 > x2 - cx ? cx - x1 : x2 - cx };

    // the camera: rows x (right), y (down), z (depth) of floor points (dx, H, dz), 4.12
    int32 R[9] = { -cA * 4, 0, sA * 4, -(sX * sA) >> 8, cX * 4, -(sX * cA) >> 8, (cX * sA) >> 8, sX * 4, (cX * cA) >> 8 };
    PS1Gte3DSetup((const int32_t *)R, 0, 0, 0, 256, cx << 16, cy << 16);
    PS1ChainDrawArea(x1, y1, x2, y2);
    // the minimap beyond the real tiles (drawn first: the real tiles cover it where both are)
    g_ps1FloorLodQuads = g_ps1FloorLodQuads + PS1FloorLod(ctx, layer, far > fNear ? far : fNear, lodFar);
    if (far <= fNear) {
        PS1ChainDrawAreaReset();
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
    for (int32 r = 0; r < nRows; ++r) {
        int32 ty = ty0 + r, xmin, xmax;
        s_rowA[r] = 1, s_rowB[r] = 0;
        if (!PS1FloorSpan(V, ty << 6, (ty << 6) + 63, xmin, xmax))
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
        for (int32 i = 0, tx = txa; i < n;) {
            uint32 ch = PS1ChunkAt(layer, chunkRow, (uint32)(tx & wMask) >> 2);
            int32 k   = 4 - (tx & 3); // to the chunk's end
            k         = k > n - i ? n - i : k;
            if (!ch)
                for (int32 e = 0; e < k; ++e) s_t[i + e] = 0xFFFF;
            else {
                const uint16 *q = PS1ChunkTiles(rowSel, ch) + (tx & 3);
                for (int32 e = 0; e < k; ++e) s_t[i + e] = q[e];
            }
            i += k, tx += k;
        }
        // the quads; each tile takes the bank of its centre's forward distance (x 1024)
        int32 fd          = sA * (((txa << 6) + 32) - camX) + cA * (zq + 32);
        const int32 fStep = sA * 64;
        const bool check  = topBad || botBad; // corners flagged: test each tile's
        for (int32 i = 0; i < n; ++i, fd += fStep) {
            const uint32 t = s_t[i];
            if (t == 0xFFFF)
                continue;
            if (check && (!tZ[i] || !tZ[i + 1] || !bZ[i] || !bZ[i + 1]))
                continue;
            const uint32 img = table[t & 0x3FF];
            if (img == PS1_TILE_NONE)
                continue;
            const PS1TileImage &ti = images[img];
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
            uint32 tl = uv, tr = uv + 15, bl = uv + 0xF00, br = uv + 0xF0F;
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
        // the bottom corners become the next row's top ones
        uint32 *sx = topXY;
        uint16 *sz = topZ;
        topXY = botXY, topZ = botZ, botXY = sx, botZ = sz, topA = botA, topN = botN, topBad = botBad;
    }
done:
    if (w && w != bulk)
        PS1ChainBulkEnd((int)((w - bulk) / 10));
    PS1ChainDrawAreaReset();
    if (todo)
        g_ps1FloorTodo = g_ps1FloorTodo + todo;
    g_ps1FloorBanks = banks;
    g_ps1FloorQuads = g_ps1FloorQuads + quads;
    g_ps1FloorDraws = g_ps1FloorDraws + 1;
    g_ps1FloorHbl   = g_ps1FloorHbl + (uint16)(PS1_HBL() - t0);
}
