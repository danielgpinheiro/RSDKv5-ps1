// PS1 port (docs/ports/mania/32 7.3): Scene3D's draw on the GTE (see PS1Scene3D.hpp and the notes in Scene3D.cpp); linked into the
// overlays of the folders that draw 3D, which call PS1Scene3DLink as they load.
#include "RSDK/Core/RetroEngine.hpp"
#include "PS1Scene3D.hpp"
#include "../../../../ps1/gte3d.hh"
#include "../../../../ps1/gpu_chain_raw.hh" // PS1ChainAlloc / LinkList: the faces straight into the arena (9f: an ordering table)

using namespace RSDK;

extern "C" {
#if UFO_LOWPOLY // 9f: a detail cut (Makefile UFO_LOWPOLY=1): the LOD at any size
volatile int32 g_ps1S3DLodPx = 0x7FFF;
#else
volatile int32 g_ps1S3DLodPx = 40;   // 7.4: a model with a distance LOD uses it below this radius on screen (pixels)
#endif
volatile uint32 g_ps1S3DLodDraws = 0; // models drawn with their LOD (total)
volatile uint32 g_ps1S3DModelFaces[MODEL_COUNT]; // faces looked at per model (total; GDB: where the 3D time goes)
volatile uint32 g_ps1S3DModelDraws[MODEL_COUNT]; // times each model was drawn (total)
volatile int32 g_ps1S3DBlendGte = 1; // ps1-ufo: the keyframe blend on the GTE (0: the CPU's, s3d_blend.s)
volatile uint32 g_ps1SortMoves = 0, g_ps1SortCalls = 0; // ps1-ufo: the sorted group's insertion steps and sorts (totals)
}

namespace
{
// (a << sh) / d with the CPU's 32-bit divide when a << sh fits (the 64-bit one is a libgcc loop: ~16 hblanks a frame)
static inline int32 PS1ShiftDiv(int32 a, int32 sh, int32 d)
{
    const int32 lim = 1 << (30 - sh);
    if (a < lim && a > -lim)
        return (a << sh) / d;
    return (int32)(((int64)a << sh) / d);
}
static inline int32 PS1BitLen(uint32 v)
{
    int32 n = 0;
    while (v) ++n, v >>= 1;
    return n;
}
// 9f: PS1Gte3DProjectOc's vertex outcodes (ps1/gte3d.cpp): bits 0-3 off the screen's sides, 4 nearer than zmin, 5
// outside the band where any face fits the GPU's limits (x in [-352, 671]: spans <= 1023; y in [-136, 375]: <= 511). The
// faces decide as the per-face box test did: any corner near -> not drawn; all corners off one side -> off screen; a
// corner outside the band -> the exact span test
#define PS1_OC_NEAR 16
#define PS1_OC_FAR 32
// 9f (Driver 2's ordering table, docs/38 2.3): the faces are tested, keyed and written in one pass, each primitive linked
// into its depth bucket of an ordering table in the arena (psyqo's layout: 0-word nodes, far bucket first, a primitive
// after its bucket's node, the newest first) instead of keys, a bucket sort and a second pass that read every face again
struct PS1S3DOt {
    uint32 *ot;           // the bucket nodes [0, nb): bucket b's primitives draw before bucket b - 1's
    uint32 kmin, kmax;    // the frame's key range (the models' bounding spheres); keys clamped into it
    int32 shift;          // key - kmin >> shift: the bucket
    uint32 *w, *wend;     // the reserved primitive words (chunks of PS1_S3D_CHUNK primitives)
    uint32 cmd, flatBgr;  // GP0 command (| semi) and the flat modes' colour
    uint32 drawn, culled;
    bool full;            // the arena is full: the rest not drawn (counted by the chain)
};
#define PS1_S3D_CHUNK 32
__attribute__((noinline)) static bool PS1S3DOtRefill(PS1S3DOt &o, int32 need)
{
    PS1ChainAllocTrim((uint32_t *)o.w); // the last chunk's unused tail first (nothing reserved since: the next chunk follows it)
    o.w = (uint32 *)PS1ChainAlloc(PS1_S3D_CHUNK * need);
    o.wend = o.w ? o.w + PS1_S3D_CHUNK * need : NULL;
    o.full = !o.w;
    return o.w != NULL;
}
// one model's faces (FVC corners each, corner colours `cols` unless FLAT): a face with a corner near, wholly off screen,
// past the GPU's limits or turned away is left out (the corners' outcodes from the projection); the others get their
// depth key (the mean depth, 12ths of engine units) and their primitive in its bucket. RSDK's quads go round, the GPU's
// are two triangles (0 1 2, 1 2 3): corners 0 1 3 2
template <int32 FVC, bool FLAT>
__attribute__((optimize("O2"), noinline)) static void PS1S3DFaces(const uint16 *idx, const uint32 *cols, int32 faces, const uint32 *xyp,
                                                                  const uint16 *szp, const uint8 *ocp, int32 k, int32 cull, bool inside,
                                                                  PS1S3DOt &o)
{
    const uint32 scale = FVC == 3 ? 4 : 3;
    const int32 wpp = FLAT ? FVC + 1 : 2 * FVC, need = wpp + 1;
    const uint32 kmin = o.kmin, kmax = o.kmax, cmd = o.cmd, hdr = (uint32)wpp << 24;
    const int32 shift = o.shift;
    uint32 *const ot = o.ot;
    uint32 *w = o.w, *wend = o.wend;
    uint32 drawn = 0, culled = 0;
    for (int32 f = 0; f < faces; ++f, idx += FVC) {
        const uint32 i0 = idx[0], i1 = idx[1], i2 = idx[2], i3 = FVC == 4 ? idx[3] : i2;
        const uint32 o0 = ocp[i0], o1 = ocp[i1], o2 = ocp[i2], o3 = FVC == 4 ? ocp[i3] : o2;
        const uint32 oAll = o0 & o1 & o2 & o3, oAny = o0 | o1 | o2 | o3;
        if ((oAny & PS1_OC_NEAR) || (!inside && (oAll & 15))) {
            ++culled;
            continue;
        }
        const uint32 a = xyp[i0], b = xyp[i1], c = xyp[i2], d = FVC == 4 ? xyp[i3] : c;
        const int32 ax = (int16)a, ay = (int32)a >> 16, bx = (int16)b, by = (int32)b >> 16, cx = (int16)c, cy = (int32)c >> 16;
        if (!inside && (oAny & PS1_OC_FAR)) {
            const int32 dx = (int16)d, dy = (int32)d >> 16;
            int32 x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax, y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;
            x0 = cx < x0 ? cx : x0, x1 = cx > x1 ? cx : x1, y0 = cy < y0 ? cy : y0, y1 = cy > y1 ? cy : y1;
            if (FVC == 4)
                x0 = dx < x0 ? dx : x0, x1 = dx > x1 ? dx : x1, y0 = dy < y0 ? dy : y0, y1 = dy > y1 ? dy : y1;
            if (x1 - x0 > 1023 || y1 - y0 > 511) {
                ++culled;
                continue;
            }
        }
        if (cull) { // turned away: the first three corners' screen winding
            const int32 cross = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
            if (cull > 0 ? cross < 0 : cross > 0) {
                ++culled;
                continue;
            }
        }
        const uint32 z0 = szp[i0], z1 = szp[i1], z2 = szp[i2], z3 = FVC == 4 ? szp[i3] : 0;
        uint32 kk = ((z0 + z1 + z2 + z3) << k) * scale;
        kk = kk < kmin ? kmin : kk > kmax ? kmax : kk;
        if (w + need > wend) {
            o.w = w;
            if (!PS1S3DOtRefill(o, need))
                break;
            w = o.w, wend = o.wend;
        }
        if (FLAT) {
            w[1] = cmd, w[2] = a, w[3] = b;
            if (FVC == 4)
                w[4] = d, w[5] = c;
            else
                w[4] = c;
        }
        else {
            const uint32 *fc = cols + f * FVC;
            w[1] = cmd | fc[0], w[2] = a, w[3] = fc[1], w[4] = b;
            if (FVC == 4)
                w[5] = fc[3], w[6] = d, w[7] = fc[2], w[8] = c;
            else
                w[5] = fc[2], w[6] = c;
        }
        uint32 *node = ot + ((kk - kmin) >> shift);
        w[0]  = hdr | (*node & 0xFFFFFF);
        *node = (uint32)(uintptr_t)w & 0xFFFFFF;
        w += need;
        ++drawn;
    }
    o.w = w, o.wend = wend, o.drawn += drawn, o.culled += culled;
}
// 9f (UFO 60 fps plan, step B): the gouraud triangles' loop in assembly (ps1/s3d_faces.s: the same decisions, keys,
// buckets and words; every value in a register). g_ps1S3DAsm 0: the C loop (A/B pictures; Scene3D.cpp, resident)
struct PS1S3DAsmCtx {
    const uint16 *idx;
    const uint32 *cols;
    const uint16 *idxEnd;
    const uint32 *xyp;
    const uint16 *szp;
    const uint8 *ocp;
    int32 k, cull;
    uint32 sideMask, inside;
    uint32 *w, *wend;
    uint32 kmin, kmax, cmd;
    int32 shift;
    uint32 *ot;
};
static_assert(sizeof(PS1S3DAsmCtx) == 68, "ps1/s3d_faces.s reads these offsets");
extern "C" int PS1S3DFacesTriAsm(PS1S3DAsmCtx *c);
// 9f (UFO item 1): the keyframe blend in assembly (ps1/s3d_blend.s: whole-word reads of the 4-aligned PMD6 frames, the
// blend factor as the multiply's rs); the C loops' results exactly. PS1_CHECK_BLEND: both run, g_ps1BlendWrong counts
// the components that differ (GDB evidence).
extern "C" void PS1S3DBlend16(const uint32 *a, const uint32 *b, uint32 *out, int32 words, int32 t);
extern "C" void PS1S3DBlend8(const uint32 *a, const uint32 *b, uint32 *out, int32 words, int32 t);
// ps1-ufo: the same on the GTE (INTPL, three components a command, no CPU multiply; ps1/s3d_blend.s), the default
extern "C" void PS1S3DBlendGte16(const int16 *a, const int16 *b, int16 *out, int32 comps, int32 t);
extern "C" void PS1S3DBlendGte8(const int8 *a, const int8 *b, int16 *out, int32 comps, int32 t);
extern "C" volatile int32 g_ps1S3DBlendGte;
extern "C" volatile uint32 g_ps1BlendWrong, g_ps1BlendChecked;
static void PS1S3DBlendC(bool pos16, const uint8 *fa, const uint8 *fb, int16 *out, int32 n, int32 t)
{
    if (pos16) {
        const int16 *a = (const int16 *)fa, *b = (const int16 *)fb;
        for (int32 i = 0; i < n; ++i) out[i] = (int16)(a[i] + (((b[i] - a[i]) * t) >> 8));
    }
    else {
        const int8 *a = (const int8 *)fa, *b = (const int8 *)fb;
        for (int32 i = 0; i < n; ++i) out[i] = (int16)((a[i] << 7) + (((b[i] - a[i]) * t) >> 1));
    }
}
// n components of frames fa / fb (from the same vertex) into out (4-aligned), blended by t
static void PS1S3DBlend(bool pos16, const uint8 *fa, const uint8 *fb, int16 *out, int32 n, int32 t)
{
    const uint32 per = pos16 ? 2 : 4; // components a word
    if (g_ps1S3DBlendGte && g_ps1S3DAsm && n % 3 == 0) {
        if (pos16)
            PS1S3DBlendGte16((const int16 *)fa, (const int16 *)fb, out, n, t);
        else
            PS1S3DBlendGte8((const int8 *)fa, (const int8 *)fb, out, n, t);
    }
    else if (!g_ps1S3DAsm || (((uintptr_t)fa | (uintptr_t)fb) & 3) || (n & (per - 1))) {
        PS1S3DBlendC(pos16, fa, fb, out, n, t); // (a mesh built before the padding)
        return;
    }
    else if (pos16)
        PS1S3DBlend16((const uint32 *)fa, (const uint32 *)fb, (uint32 *)out, n / 2, t);
    else
        PS1S3DBlend8((const uint32 *)fa, (const uint32 *)fb, (uint32 *)out, n / 4, t);
#if PS1_CHECK_BLEND
    int16 ref[3 * 96];
    PS1S3DBlendC(pos16, fa, fb, ref, n, t);
    for (int32 i = 0; i < n; ++i) g_ps1BlendWrong = g_ps1BlendWrong + (ref[i] != out[i]);
    g_ps1BlendChecked = g_ps1BlendChecked + n;
#endif
}
static void PS1S3DFacesTri(const uint16 *idx, const uint32 *cols, int32 faces, const uint32 *xyp, const uint16 *szp,
                           const uint8 *ocp, int32 k, int32 cull, bool inside, PS1S3DOt &o)
{
    PS1S3DAsmCtx c = { idx, cols, idx + 3 * faces, xyp, szp, ocp, k, cull, inside ? 0u : 15u, inside, o.w, o.wend,
                       o.kmin, o.kmax, o.cmd, o.shift, o.ot };
    for (;;) {
        const uint16 *i0 = c.idx;
        uint32 *w0       = c.w;
        const int32 st   = PS1S3DFacesTriAsm(&c);
        const uint32 drawn = (uint32)(c.w - w0) / 7;
        o.w = c.w, o.drawn += drawn, o.culled += (uint32)(c.idx - i0) / 3 - drawn;
        if (st == 0)
            return;
        if (st == 1) { // the reserved words are full
            if (!PS1S3DOtRefill(o, 7))
                return;
            c.w = o.w, c.wend = o.wend;
            continue;
        }
        // a corner past the GPU-limit band: the exact span test (C) for this face
        PS1S3DFaces<3, false>(c.idx, c.cols, 1, xyp, szp, ocp, k, cull, inside, o);
        if (o.full)
            return;
        c.idx += 3, c.cols += 3, c.w = o.w, c.wend = o.wend;
    }
}
// 7.5: S3D_WIREFRAME_SHADED_SCREEN (the UFO item box) as stock: positions through the matrix, projected (a face with a
// corner nearer than 0x100 left out), each face's edges as lines in the entity's colour lit by its corners' mean normal y
// (the mesh keeps its normals: PMD6 flag bit3). Faces in mesh order (lines: the sort changes nothing seen).
static int32 PS1WireLit(int32 ny, uint32 colour, int32 shift, int32 diffuse, int32 intensity, int32 specularIntensity)
{
    int32 normalVal = (ny >> 2) * (abs(ny) >> 2);
    int32 specular  = (normalVal >> 6) >> specularIntensity;
    specular        = specular < 0 ? 0 : specular > 0xFF ? 0xFF : specular;
    int32 c         = specular + ((int32)((colour >> shift) & 0xFF) * ((ny >> 10) + diffuse) >> intensity);
    return c < 0 ? 0 : c > 0xFF ? 0xFF : c;
}
static void PS1Scene3DWire(uint16 sceneID)
{
    Scene3D *scn   = &scene3DList[sceneID];
    Entity *entity = sceneInfo.entity;
    for (int32 i = 0; i < g_ps1S3DCount; ++i) {
        const PS1S3DItem &it = g_ps1S3DItems[i];
        if (it.scene != sceneID)
            continue;
        const Model *mdl = &modelList[it.model];
        const int32 fvc  = mdl->faceVertCount;
        if (mdl->vertCount > 64) {
            g_ps1S3DTodo = g_ps1S3DTodo + 1;
            continue;
        }
        const uint8 *data = PS1MeshData(mdl);
        const uint16 *idx = (const uint16 *)(data + 4 * mdl->indexCount);
        const uint8 *pos  = PS1MeshPos(mdl); // frame 0
        const bool nrm    = mdl->ps1NormOffset != 0;
        const Matrix &m   = it.m;
        int32 sx[64], sy[64], sz[64];
        for (int32 v = 0; v < mdl->vertCount; ++v) {
            int32 x, y, z;
            if (mdl->flags & PS1_MESH_POS16) {
                const int16 *p = (const int16 *)pos + 3 * v;
                x = p[0], y = p[1], z = p[2];
            }
            else {
                const int8 *p = (const int8 *)pos + 3 * v;
                x = p[0] * mdl->ps1Scale >> 8, y = p[1] * mdl->ps1Scale >> 8, z = p[2] * mdl->ps1Scale >> 8;
            }
            int32 wx = m.values[0][3] + (z * m.values[0][2] >> 8) + (m.values[0][0] * x >> 8) + (m.values[0][1] * y >> 8);
            int32 wy = m.values[1][3] + (y * m.values[1][1] >> 8) + (z * m.values[1][2] >> 8) + (m.values[1][0] * x >> 8);
            sz[v]    = m.values[2][3] + ((x * m.values[2][0]) >> 8) + ((m.values[2][2] * z >> 8) + (m.values[2][1] * y >> 8));
            if (sz[v] >= 0x100) {
                sx[v] = currentScreen->center.x + (wx << scn->projectionX) / sz[v];
                sy[v] = currentScreen->center.y - (wy << scn->projectionY) / sz[v];
            }
        }
        for (int32 f = 0; f < mdl->indexCount; f += fvc) {
            const uint16 *c = idx + f;
            int32 ny = 0, v = 0;
            for (; v < fvc && sz[c[v]] >= 0x100; ++v) {
                if (nrm) {
                    const int8 *n = PS1MeshNormal(mdl, (uint32)(f + v));
                    ny += (n[1] * 0x10000 / 127 * it.n1[1] >> 8) + (n[2] * 0x10000 / 127 * it.n1[2] >> 8) + (n[0] * 0x10000 / 127 * it.n1[0] >> 8);
                }
            }
            if (v < fvc)
                continue;
            ny /= fvc;
            uint32 colour = (uint32)PS1WireLit(ny, it.colour, 16, scn->diffuseX, scn->diffuseIntensityX, scn->specularIntensityX) << 16
                            | (uint32)PS1WireLit(ny, it.colour, 8, scn->diffuseY, scn->diffuseIntensityY, scn->specularIntensityY) << 8
                            | (uint32)PS1WireLit(ny, it.colour, 0, scn->diffuseZ, scn->diffuseIntensityZ, scn->specularIntensityZ);
            for (v = 0; v < fvc; ++v) {
                int32 w = v + 1 < fvc ? v + 1 : 0;
                DrawLine(sx[c[v]], sy[c[v]], sx[c[w]], sy[c[w]], colour, entity->alpha, entity->inkEffect, true);
            }
        }
    }
    g_ps1S3DDraws = g_ps1S3DDraws + 1;
}

__attribute__((optimize("O2"))) static void PS1Scene3DDraw(uint16 sceneID)
{
    if (sceneID >= SCENE3D_COUNT)
        return;
    Scene3D *scn = &scene3DList[sceneID];
    int32 list[PS1_S3D_ITEMS], n = 0;
    uint32 totalV = 0, totalF = 0;
    for (int32 i = 0; i < g_ps1S3DCount; ++i) {
        if (g_ps1S3DItems[i].scene != sceneID)
            continue;
        const Model *mdl = &modelList[g_ps1S3DItems[i].model];
        list[n++] = i;
        totalV += mdl->vertCount, totalF += mdl->indexCount / mdl->faceVertCount;
    }
    if (!n)
        return;
    if (scn->drawMode == S3D_WIREFRAME_SHADED_SCREEN) { // 7.5: the item box
        PS1Scene3DWire(sceneID);
        return;
    }
    bool flat = scn->drawMode == S3D_SOLIDCOLOR_SCREEN;
    if (!flat && scn->drawMode != S3D_SOLIDCOLOR_SHADED_SCREEN && scn->drawMode != S3D_SOLIDCOLOR_SHADED_BLENDED_SCREEN) {
        g_ps1S3DTodo = g_ps1S3DTodo + 1;
        return;
    }
    if (totalV > g_ps1S3DWorkVerts || totalF > g_ps1S3DWorkFaces) {
        g_ps1S3DOverflow = g_ps1S3DOverflow + 1;
        return;
    }
    uint32 *sxy = (uint32 *)g_ps1S3DWork;
    uint16 *sz  = (uint16 *)(sxy + g_ps1S3DWorkVerts);
    uint8 *oc   = (uint8 *)(sz + g_ps1S3DWorkVerts); // 9f: the vertices' outcodes (PS1Gte3DProjectOc)
#define PS1_HBL() (*(volatile uint16 *)0xBF801110)
    uint16 t0 = PS1_HBL(), tx = 0, tf = 0;
    // the unit shift of each model: every view coordinate |T + M v| within 16 bits (bound: the largest |T + M v| bound);
    // the frame's key range from the models' bounding spheres (a key is ~12 x the view depth, engine units)
    uint32 bounds[PS1_S3D_ITEMS];
    uint32 kmin = 0xFFFFFFFF, kmax = 0;
    for (int32 j = 0; j < n; ++j) {
        const PS1S3DItem &it = g_ps1S3DItems[list[j]];
        const Model *mdl     = &modelList[it.model];
        const Matrix &m      = it.m;
        uint32 bound = 0;
        for (int32 r = 0; r < 3; ++r) {
            uint32 b = (uint32)abs(m.values[r][3])
                       + (uint32)(((uint64)(abs(m.values[r][0]) + abs(m.values[r][1]) + abs(m.values[r][2])) * mdl->ps1Radius) >> 8);
            bound = b > bound ? b : bound;
        }
        bounds[j]       = bound;
        const int32 cz  = m.values[2][3], cr = (int32)(bound - (uint32)abs(cz));
        const int32 zlo = cz - cr > 0 ? cz - cr : 0, zhi = cz + cr > 0 ? cz + cr : 0;
        kmin = 12u * (uint32)zlo < kmin ? 12u * (uint32)zlo : kmin;
        kmax = 12u * (uint32)zhi > kmax ? 12u * (uint32)zhi : kmax;
    }
    if (kmax <= kmin)
        kmax = kmin + 1;
    Entity *entity          = sceneInfo.entity;
    const uint32 flatColour = g_ps1S3DItems[list[n - 1]].colour;
    uint32 flatBgr, semi;
    if (!PS1Face3DBegin(entity->inkEffect, entity->alpha, flatColour, &flatBgr, &semi))
        return;
    // the ordering table: bucket b's node links to b - 1's, bucket 0's to the end node (a GP0 NOP: the list's last node);
    // about one bucket per two faces, 16 to PS1_S3D_BUCKETS (a decoration's own draw: a 93-face tree)
    int32 nbBits = PS1BitLen(totalF >> 1);
    nbBits       = nbBits < 4 ? 4 : nbBits > 8 ? 8 : nbBits;
    const int32 nb = 1 << nbBits;
    uint32 *ot = (uint32 *)PS1ChainAlloc(nb + 2);
    if (!ot) {
        PS1Face3DEnd();
        return;
    }
    uint32 *otEnd = ot + nb;
    ot[0]         = (uint32)(uintptr_t)otEnd & 0xFFFFFF;
    for (int32 b = 1; b < nb; ++b) ot[b] = (uint32)(uintptr_t)(ot + b - 1) & 0xFFFFFF;
    otEnd[1] = 0; // GP0 00h: NOP
    PS1S3DOt o;
    o.ot = ot, o.kmin = kmin, o.kmax = kmax, o.w = o.wend = otEnd + 2, o.drawn = o.culled = 0, o.full = false;
    o.shift = PS1BitLen(kmax - kmin) - nbBits;
    o.shift = o.shift < 0 ? 0 : o.shift;
    o.flatBgr = flatBgr;
    alignas(4) int16 chunk[3 * 96];
    for (int32 j = 0; j < n && !o.full; ++j) {
        const PS1S3DItem &it = g_ps1S3DItems[list[j]];
        const Model *mdl     = &modelList[it.model];
        const Matrix &m      = it.m;
        const bool pos16     = mdl->flags & PS1_MESH_POS16;
        const uint32 bound   = bounds[j];
        int32 k = 0;
        while ((bound >> k) > 32000) ++k;
        int32 R[9];
        bool fits = true;
        for (int32 r = 0; r < 3; ++r) {
            for (int32 c = 0; c < 3; ++c) {
                int32 v = pos16 ? (m.values[r][c] << 4) >> k : (int32)(((int64)m.values[r][c] * mdl->ps1Scale) >> (11 + k));
                v       = r == 1 ? -v : v;
                fits    = fits && v >= -32768 && v <= 32767;
                R[r * 3 + c] = v;
            }
        }
        if (!fits) {
            g_ps1S3DMatrix = g_ps1S3DMatrix + 1;
            continue;
        }
        PS1Gte3DSetup((const int32_t *)R, m.values[0][3] >> k, -(m.values[1][3] >> k), m.values[2][3] >> k, 1u << scn->projectionX,
                      currentScreen->center.x << 16, currentScreen->center.y << 16);
        const uint32 zmin = (0x100u >> k) ? (0x100u >> k) : 1; // nearer corners: the face is not drawn
        const int32 sw = currentScreen->size.x, sh = currentScreen->size.y;
        // the model's bounding sphere wholly on screen and within the GPU's limits: its faces need no box test (9f, UFO
        // item 5b: decided before the projection, which then leaves out the outcodes' other bits: nothing reads them)
        const int32 cz = m.values[2][3], cr = (int32)(bound - (uint32)abs(m.values[2][3]));
        bool inside    = false;
        int32 rs       = 0x7FFF;
        if (cz - cr > 0x100) {
            rs = PS1ShiftDiv(cr, scn->projectionX, cz - cr) + 2; // its radius on screen, rounded out
            const int32 sx = currentScreen->center.x + PS1ShiftDiv(m.values[0][3], scn->projectionX, cz);
            const int32 sy = currentScreen->center.y - PS1ShiftDiv(m.values[1][3], scn->projectionX, cz);
            inside = rs < 255 && sx - rs >= 0 && sx + rs < currentScreen->size.x && sy - rs >= 0 && sy + rs < currentScreen->size.y;
        }
        int (*const project)(const int16_t *, int, uint32_t *, uint16_t *, uint8_t *, uint32_t, int32_t, int32_t) =
            inside ? PS1Gte3DProjectNear : PS1Gte3DProjectOc;
        // the vertices (keyframes blended), 96 at a time
        const uint8 *data  = PS1MeshData(mdl);
        const uint16 *idx  = (const uint16 *)(data + 4 * mdl->indexCount); // PMD6: a colour per corner first
        const uint8 *pos   = PS1MeshPos(mdl);
        const int32 vc     = mdl->vertCount;
        const uint32 fstride = (pos16 ? 6u : 3u) * vc;
        const uint8 *fa = pos + fstride * (it.frameA < mdl->frameCount ? it.frameA : 0);
        const uint8 *fbp = pos + fstride * (it.frameB < mdl->frameCount ? it.frameB : 0);
        const int32 t = it.interp;
        if (pos16 && (!t || it.frameA == it.frameB)) // no blend: the stored positions go to the GTE as they are
            project((const int16_t *)fa, vc, (uint32_t *)sxy, sz, oc, zmin, sw, sh);
        for (int32 v0 = 0; v0 < vc && !(pos16 && (!t || it.frameA == it.frameB)); v0 += 96) {
            int32 cnt = vc - v0 < 96 ? vc - v0 : 96;
            const uint32 o = (pos16 ? 6u : 3u) * v0; // (t is never 0 for 16-bit here: that case projects the frame as it is)
            PS1S3DBlend(pos16, fa + o, fbp + o, chunk, 3 * cnt, t);
            project(chunk, cnt, (uint32_t *)(sxy + v0), sz + v0, oc + v0, zmin, sw, sh);
        }
        uint16 t1 = PS1_HBL();
        tx += (uint16)(t1 - t0);
        const int32 fvc  = mdl->faceVertCount;
        const int32 cull = (mdl->flags & PS1_MESH_CLOSED) ? g_ps1S3DBackCull : 0;
        // 7.4: small on screen: its distance LOD (the same positions, fewer faces)
#if UFO_LOWPOLY
        const bool lod     = mdl->ps1LodIndexCount && rs < g_ps1S3DLodPx;
#else // (9f: the characters' LODs are the cut's only)
        const bool lod     = mdl->ps1LodIndexCount && !(mdl->flags & PS1_MESH_CUTLOD) && rs < g_ps1S3DLodPx;
#endif
        const uint32 *cols = (const uint32 *)(lod ? data + mdl->ps1LodOffset : data);
        const uint16 *fidx = lod ? (const uint16 *)(data + mdl->ps1LodOffset + 4 * mdl->ps1LodIndexCount) : idx;
        const int32 faces  = (lod ? mdl->ps1LodIndexCount : mdl->indexCount) / fvc;
        g_ps1S3DLodDraws   = g_ps1S3DLodDraws + lod;
        o.cmd = (flat ? (fvc == 4 ? 0x28000000 : 0x20000000) | flatBgr : (fvc == 4 ? 0x38000000 : 0x30000000)) | semi;
        if (flat)
            fvc == 4 ? PS1S3DFaces<4, true>(fidx, cols, faces, sxy, sz, oc, k, cull, inside, o)
                     : PS1S3DFaces<3, true>(fidx, cols, faces, sxy, sz, oc, k, cull, inside, o);
        else
            fvc == 4 ? PS1S3DFaces<4, false>(fidx, cols, faces, sxy, sz, oc, k, cull, inside, o)
                     : g_ps1S3DAsm ? PS1S3DFacesTri(fidx, cols, faces, sxy, sz, oc, k, cull, inside, o)
                                   : PS1S3DFaces<3, false>(fidx, cols, faces, sxy, sz, oc, k, cull, inside, o);
        g_ps1S3DModelFaces[it.model] = g_ps1S3DModelFaces[it.model] + faces;
        g_ps1S3DModelDraws[it.model] = g_ps1S3DModelDraws[it.model] + 1;
        t0 = PS1_HBL();
        tf += (uint16)(t0 - t1);
    }
    PS1ChainAllocTrim((uint32_t *)o.w); // the last chunk's unused words back
    if (o.drawn)
        PS1ChainLinkList((uint32_t *)(ot + nb - 1), (uint32_t *)otEnd, 1);
    else
        PS1ChainAllocTrim((uint32_t *)ot); // nothing drawn: the table back too
    PS1Face3DEnd();
    g_ps1S3DCulled = g_ps1S3DCulled + o.culled;
    g_ps1S3DHbl[0] = g_ps1S3DHbl[0] + tx;
    g_ps1S3DHbl[1] = g_ps1S3DHbl[1] + tf; // the faces, their keys and primitives (9f: one pass)
    g_ps1S3DDraws  = g_ps1S3DDraws + 1;
    g_ps1S3DFaces  = g_ps1S3DFaces + o.drawn;
}
// ps1-ufo: the sorted draw group (UFO_Setup's group 4: ~100 rings and spheres in UFO5) in the order the engine's sort
// gives (Object.cpp: descending zdepth, equal ones in list order), from last frame's order: the list is rebuilt in slot
// order every frame and re-sorted from scratch there (~n^2 / 4 moves, ~30 hblanks a frame in UFO5); the depths barely
// change between frames, so last frame's ranks put it nearly in order and the insertion sort that finishes it moves few.
// Exact whatever the start (a strict order on (depth, list position)); entries last frame didn't have go at the end.
uint16 s_sortSlot[PS1_DRAWLIST_MAX]; // last frame's list in list order
uint16 s_sortRank[PS1_DRAWLIST_MAX]; // and each entry's place in its sorted order
uint16 s_sortIdx[PS1_DRAWLIST_MAX];  // this frame: list positions in sort order (first: by last frame's rank)
int32 s_sortN              = 0;
const DrawList *s_sortList = NULL;
__attribute__((optimize("O2"))) static bool PS1Scene3DSort(DrawList *list)
{
    const int32 n = list->entityCount;
    if (n > PS1_DRAWLIST_MAX)
        return false;
    int32 *const key  = (int32 *)0x1F800000; // the scratchpad (the layer drawer's, free between its calls), 256 words
    uint16 *const idx = s_sortIdx;
    uint16 *const e   = list->entries;
    const int32 pn    = list == s_sortList && s_sortN <= PS1_DRAWLIST_MAX ? s_sortN : 0;
    // last frame's rank of each entry (both lists in slot order: one walk), its list position put at that rank
    for (int32 r = 0; r < pn; ++r) idx[r] = 0xFFFF;
    int32 news = 0;
    uint16 *const fresh = (uint16 *)(key + PS1_DRAWLIST_MAX) - n; // the scratchpad's end (read before the keys go in)
    for (int32 i = 0, j = 0; i < n; ++i) {
        const uint32 s = e[i];
        while (j < pn && s_sortSlot[j] < s) ++j;
        uint32 r = 0xFFFF;
        if (j < pn && s_sortSlot[j] == s && s_sortRank[j] < pn && idx[s_sortRank[j]] == 0xFFFF)
            r = s_sortRank[j];
        if (r != 0xFFFF)
            idx[r] = (uint16)i;
        else
            fresh[news++] = (uint16)i; // (not ascending lists, or new this frame)
    }
    int32 m = 0;
    for (int32 r = 0; r < pn; ++r) // compacted in place (m <= r)
        if (idx[r] != 0xFFFF)
            idx[m++] = idx[r];
    for (int32 k = 0; k < news; ++k) idx[m++] = fresh[k];
    // the keys in that order, then the insertion sort: (k, i) before (k', i') when k > k' or k == k' and i < i'
    for (int32 p = 0; p < n; ++p) key[p] = OBJECT_ENTITY(e[idx[p]])->zdepth;
    uint32 moves = 0;
    for (int32 p = 1; p < n; ++p) {
        const int32 k  = key[p];
        const uint32 i = idx[p];
        int32 q        = p;
        while (q > 0 && (key[q - 1] < k || (key[q - 1] == k && idx[q - 1] > i))) {
            key[q] = key[q - 1], idx[q] = idx[q - 1];
            --q;
        }
        key[q] = k, idx[q] = (uint16)i;
        moves += (uint32)(p - q);
    }
    // the list in that order; this frame's list and ranks kept for the next
    for (int32 i = 0; i < n; ++i) s_sortSlot[i] = e[i];
    for (int32 p = 0; p < n; ++p) {
        e[p]               = s_sortSlot[idx[p]];
        s_sortRank[idx[p]] = (uint16)p;
    }
    s_sortN = n, s_sortList = list;
    g_ps1SortMoves = g_ps1SortMoves + moves;
    g_ps1SortCalls = g_ps1SortCalls + 1;
    return true;
}
} // namespace

void PS1UfoFloorDraw(TileLayer *layer); // PS1UfoFloor.cpp

// ps1-ufo: GetActiveEntities' walk (Object.cpp) with the position in *id instead of the foreach stack: the next entity of
// the type group whose class is the group (read as the walk reaches it), or NULL at the end (the UFO objects' per-frame
// foreach_active(UFO_Player): ~500 rings / spheres a frame went through the function table and the stack twice each)
extern "C" void *PS1ActiveNext(uint16 group, int32 *id)
{
    if (group >= TYPEGROUP_COUNT)
        return NULL;
    const TypeGroupList &g = typeGroups[group];
    for (int32 k = *id; k < g.entryCount; ++k) {
        Entity *e = OBJECT_ENTITY(g.entries[k]);
        if (e->classID == group) {
            *id = k + 1;
            return e;
        }
    }
    *id = g.entryCount;
    return NULL;
}

extern "C" void PS1Scene3DLink()
{
    g_ps1S3DOps.draw  = PS1Scene3DDraw;
    g_ps1S3DOps.floor = PS1UfoFloorDraw;
    g_ps1S3DOps.sort  = PS1Scene3DSort;
    s_sortN = 0, s_sortList = NULL;
}
