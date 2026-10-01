// PS1 port (docs/32 7.3): Scene3D's draw on the GTE (see PS1Scene3D.hpp and the notes in Scene3D.cpp); linked into the
// overlays of the folders that draw 3D, which call PS1Scene3DLink as they load.
#include "RSDK/Core/RetroEngine.hpp"
#include "PS1Scene3D.hpp"
#include "../../../../ps1/gte3d.hh"
#include "../../../../ps1/gpu_chain_raw.hh" // PS1ChainBulkBegin / End: the faces straight into the arena

using namespace RSDK;

extern "C" {
volatile int32 g_ps1S3DLodPx = 40;   // 7.4: a model with a distance LOD uses it below this radius on screen (pixels)
volatile uint32 g_ps1S3DLodDraws = 0; // models drawn with their LOD (total)
volatile uint32 g_ps1S3DModelFaces[MODEL_COUNT]; // faces looked at per model (total; GDB: where the 3D time goes)
volatile uint32 g_ps1S3DModelDraws[MODEL_COUNT]; // times each model was drawn (total)
}

namespace
{
uint32 *s_ps1S3DBulk = NULL; // the open chain bulk (32 faces)
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
// one model's faces (FVC corners each): the ones to draw get their depth key, the others 0xFFFFFFFF; owner = the model
template <int32 FVC>
__attribute__((optimize("O2"), always_inline)) static inline void PS1S3DFaces(const uint16 *idx, int32 faces, const uint32 *xyp,
                                                                              const uint16 *szp, uint32 zmin, int32 k, int32 cull, uint8 j,
                                                                              uint32 *keyp, uint8 *ownp, uint32 &kmin, uint32 &kmax,
                                                                              uint32 &culled, bool inside)
{
    const uint32 scale = FVC == 3 ? 4 : 3;
    const int32 sw = currentScreen->size.x, sh = currentScreen->size.y;
    for (int32 f = 0; f < faces; ++f, idx += FVC) {
        const uint32 i0 = idx[0], i1 = idx[1], i2 = idx[2], i3 = FVC == 4 ? idx[3] : i2;
        const uint32 z0 = szp[i0], z1 = szp[i1], z2 = szp[i2], z3 = FVC == 4 ? szp[i3] : 0;
        ownp[f] = j;
        if (z0 < zmin || z1 < zmin || z2 < zmin || (FVC == 4 && z3 < zmin)) {
            keyp[f] = 0xFFFFFFFF, ++culled;
            continue;
        }
        const uint32 a = xyp[i0], b = xyp[i1], c = xyp[i2];
        const int32 ax = (int16)a, ay = (int32)a >> 16, bx = (int16)b, by = (int32)b >> 16, cx = (int16)c, cy = (int32)c >> 16;
        bool ok = true;
        if (!inside) {
            const uint32 d = xyp[i3];
            const int32 dx = (int16)d, dy = (int32)d >> 16;
            int32 x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax, y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;
            x0 = cx < x0 ? cx : x0, x1 = cx > x1 ? cx : x1, y0 = cy < y0 ? cy : y0, y1 = cy > y1 ? cy : y1;
            if (FVC == 4)
                x0 = dx < x0 ? dx : x0, x1 = dx > x1 ? dx : x1, y0 = dy < y0 ? dy : y0, y1 = dy > y1 ? dy : y1;
            ok = x1 >= 0 && x0 < sw && y1 >= 0 && y0 < sh && x1 - x0 <= 1023 && y1 - y0 <= 511;
        }
        if (ok && cull) { // turned away: the first three corners' screen winding
            const int32 cross = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
            ok                = cull > 0 ? cross >= 0 : cross <= 0;
        }
        if (!ok) {
            keyp[f] = 0xFFFFFFFF, ++culled;
            continue;
        }
        const uint32 kk = ((z0 + z1 + z2 + z3) << k) * scale;
        keyp[f]         = kk;
        kmin            = kk < kmin ? kk : kmin;
        kmax            = kk > kmax ? kk : kmax;
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
        const uint8 *pos  = data + 6 * mdl->indexCount; // frame 0
        const int8 *nrm   = mdl->ps1NormOffset ? (const int8 *)(data + mdl->ps1NormOffset) : NULL;
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
                    const int8 *n = nrm + 4 * (f + v);
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
    uint32 *sxy   = (uint32 *)g_ps1S3DWork;
    uint32 *key   = sxy + g_ps1S3DWorkVerts;
    uint16 *sz    = (uint16 *)(key + g_ps1S3DWorkFaces);
    uint16 *next  = sz + g_ps1S3DWorkVerts;
    uint16 *head  = next + g_ps1S3DWorkFaces;
    uint8 *owner  = (uint8 *)(head + PS1_S3D_BUCKETS); // the face's model (index into list)
#define PS1_HBL() (*(volatile uint16 *)0xBF801110)
    uint16 t0 = PS1_HBL(), tx = 0;
    uint32 kmin = 0xFFFFFFFF, kmax = 0, culled = 0;
    uint32 vb = 0, fb = 0;
    int16 chunk[3 * 96];
    // per model drawn: its face corners' colours and indices (the mesh's or its distance LOD's), its face count
    const uint32 *mcols[PS1_S3D_ITEMS];
    const uint16 *midx[PS1_S3D_ITEMS];
    int32 mfaces[PS1_S3D_ITEMS], mfvc[PS1_S3D_ITEMS];
    uint32 mvb[PS1_S3D_ITEMS], mfb[PS1_S3D_ITEMS]; // its first vertex / face in the work arrays
    for (int32 j = 0; j < n; ++j) {
        mfaces[j] = 0, mcols[j] = NULL, midx[j] = NULL, mvb[j] = vb, mfb[j] = fb; // (a model skipped owns no faces)
        mfvc[j] = modelList[g_ps1S3DItems[list[j]].model].faceVertCount;
        const PS1S3DItem &it = g_ps1S3DItems[list[j]];
        const Model *mdl     = &modelList[it.model];
        const Matrix &m      = it.m;
        const bool pos16     = mdl->flags & PS1_MESH_POS16;
        // the unit shift: every view coordinate |T + M v| within 16 bits
        uint32 bound = 0;
        for (int32 r = 0; r < 3; ++r) {
            uint32 b = (uint32)abs(m.values[r][3])
                       + (uint32)(((uint64)(abs(m.values[r][0]) + abs(m.values[r][1]) + abs(m.values[r][2])) * mdl->ps1Radius) >> 8);
            bound = b > bound ? b : bound;
        }
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
        // the vertices (keyframes blended), 96 at a time
        const uint8 *data  = PS1MeshData(mdl);
        const uint16 *idx  = (const uint16 *)(data + 4 * mdl->indexCount); // PMD6: a colour per corner first
        const uint8 *pos   = data + 6 * mdl->indexCount;
        const int32 vc     = mdl->vertCount;
        const uint32 fstride = (pos16 ? 6u : 3u) * vc;
        const uint8 *fa = pos + fstride * (it.frameA < mdl->frameCount ? it.frameA : 0);
        const uint8 *fbp = pos + fstride * (it.frameB < mdl->frameCount ? it.frameB : 0);
        const int32 t = it.interp;
        if (pos16 && (!t || it.frameA == it.frameB)) // no blend: the stored positions go to the GTE as they are
            PS1Gte3DProject((const int16_t *)fa, vc, (uint32_t *)(sxy + vb), sz + vb);
        for (int32 v0 = 0; v0 < vc && !(pos16 && (!t || it.frameA == it.frameB)); v0 += 96) {
            int32 cnt = vc - v0 < 96 ? vc - v0 : 96;
            if (pos16) {
                const int16 *a = (const int16 *)fa + 3 * v0, *b = (const int16 *)fbp + 3 * v0;
                for (int32 i = 0; i < 3 * cnt; ++i) chunk[i] = t ? (int16)(a[i] + (((b[i] - a[i]) * t) >> 8)) : a[i];
            }
            else {
                const int8 *a = (const int8 *)fa + 3 * v0, *b = (const int8 *)fbp + 3 * v0;
                for (int32 i = 0; i < 3 * cnt; ++i) chunk[i] = (int16)((a[i] << 7) + (((b[i] - a[i]) * t) >> 1));
            }
            PS1Gte3DProject(chunk, cnt, (uint32_t *)(sxy + vb + v0), sz + vb + v0);
        }
        uint16 t1 = PS1_HBL();
        tx += (uint16)(t1 - t0);
        // the faces: valid ones get a depth key (the mean depth, in 12ths, engine units)
        const uint32 zmin = (0x100u >> k) ? (0x100u >> k) : 1;
        const int32 fvc   = mdl->faceVertCount;
        const int32 cull  = (mdl->flags & PS1_MESH_CLOSED) ? g_ps1S3DBackCull : 0;
        // the model's bounding sphere wholly on screen and within the GPU's limits: its faces need no box test
        const int32 cz = m.values[2][3], cr = (int32)(bound - (uint32)abs(m.values[2][3]));
        bool inside    = false;
        int32 rs       = 0x7FFF;
        if (cz - cr > 0x100) {
            rs = PS1ShiftDiv(cr, scn->projectionX, cz - cr) + 2; // its radius on screen, rounded out
            const int32 sx = currentScreen->center.x + PS1ShiftDiv(m.values[0][3], scn->projectionX, cz);
            const int32 sy = currentScreen->center.y - PS1ShiftDiv(m.values[1][3], scn->projectionX, cz);
            inside = rs < 255 && sx - rs >= 0 && sx + rs < currentScreen->size.x && sy - rs >= 0 && sy + rs < currentScreen->size.y;
        }
        // 7.4: small on screen: its distance LOD (the same positions, fewer faces)
        const bool lod = mdl->ps1LodIndexCount && rs < g_ps1S3DLodPx;
        mcols[j]       = (const uint32 *)(lod ? data + mdl->ps1LodOffset : data);
        midx[j]        = lod ? (const uint16 *)(data + mdl->ps1LodOffset + 4 * mdl->ps1LodIndexCount) : idx;
        const int32 faces = (lod ? mdl->ps1LodIndexCount : mdl->indexCount) / fvc;
        mfaces[j]         = faces;
        g_ps1S3DLodDraws  = g_ps1S3DLodDraws + lod;
        if (fvc == 4)
            PS1S3DFaces<4>(midx[j], faces, sxy + vb, sz + vb, zmin, k, cull, (uint8)j, key + fb, owner + fb, kmin, kmax, culled, inside);
        else
            PS1S3DFaces<3>(midx[j], faces, sxy + vb, sz + vb, zmin, k, cull, (uint8)j, key + fb, owner + fb, kmin, kmax, culled, inside);
        vb += vc, fb += faces;
        g_ps1S3DModelFaces[it.model] = g_ps1S3DModelFaces[it.model] + faces;
        g_ps1S3DModelDraws[it.model] = g_ps1S3DModelDraws[it.model] + 1;
        t0 = PS1_HBL();
        g_ps1S3DHbl[1] = g_ps1S3DHbl[1] + (uint16)(t0 - t1);
    }
    g_ps1S3DCulled = g_ps1S3DCulled + culled;
    g_ps1S3DHbl[0] = g_ps1S3DHbl[0] + tx;
    uint16 t2 = PS1_HBL();
    if (kmin > kmax)
        return;
    // bucket sort, far first
    int32 shift = PS1BitLen(kmax - kmin) - 8;
    shift       = shift < 0 ? 0 : shift;
    for (int32 b = 0; b < PS1_S3D_BUCKETS; ++b) head[b] = 0xFFFF;
    for (uint32 g = 0; g < fb; ++g) {
        if (key[g] == 0xFFFFFFFF)
            continue;
        uint32 b = (key[g] - kmin) >> shift;
        next[g]  = head[b];
        head[b]  = (uint16)g;
    }
    uint16 t3 = PS1_HBL();
    g_ps1S3DHbl[2] = g_ps1S3DHbl[2] + (uint16)(t3 - t2);
    Entity *entity = sceneInfo.entity;
    const uint32 flatColour = g_ps1S3DItems[list[n - 1]].colour;
    uint32 flatBgr, semi;
    if (!PS1Face3DBegin(entity->inkEffect, entity->alpha, flatColour, &flatBgr, &semi))
        return;
    uint32 drawn = 0;
    int32 cj = -1, fvc = 3, wpp = 0;
    const uint32 *cols = NULL, *vxy = NULL;
    const uint16 *idx0 = NULL;
    uint32 fbase = 0, cmd = 0;
    uint32 *w = NULL, *wend = NULL; // this bulk's next slot / its end
    for (int32 b = PS1_S3D_BUCKETS - 1; b >= 0; --b) {
        for (uint32 g = head[b]; g != 0xFFFF; g = next[g]) {
            const int32 j = owner[g];
            if (j != cj) { // another model: its pointers, and a new bulk if the face size changes
                cj = j, fvc = mfvc[j], cols = mcols[j], vxy = sxy + mvb[j], idx0 = midx[j], fbase = mfb[j];
                const int32 nw = flat ? fvc + 1 : 2 * fvc;
                cmd            = (flat ? (fvc == 4 ? 0x28000000 : 0x20000000) : (fvc == 4 ? 0x38000000 : 0x30000000)) | semi;
                if (nw != wpp && w) {
                    PS1ChainBulkEnd((int)((w - s_ps1S3DBulk) / (wpp + 1)));
                    w = wend = NULL;
                }
                wpp = nw;
            }
            if (w == wend) {
                if (w)
                    PS1ChainBulkEnd(32);
                w = s_ps1S3DBulk = (uint32 *)PS1ChainBulkBegin(32, wpp);
                if (!w)
                    goto done;
                wend = w + 32 * (wpp + 1);
            }
            const uint32 fo = (g - fbase) * fvc; // the face's first corner
            const uint16 *fi = idx0 + fo;
            // RSDK's quads go round; the GPU's are two triangles (0 1 2, 1 2 3): corners 0 1 3 2
            const uint32 i0 = fi[0], i1 = fi[1], i2 = fvc == 4 ? fi[3] : fi[2];
            if (flat) {
                w[0] = cmd | flatBgr, w[1] = vxy[i0], w[2] = vxy[i1], w[3] = vxy[i2];
                if (fvc == 4)
                    w[4] = vxy[fi[2]];
            }
            else {
                const uint32 *fc = cols + fo; // the corners' colours (PMD6)
                w[0] = cmd | fc[0], w[1] = vxy[i0], w[2] = fc[1], w[3] = vxy[i1], w[4] = fc[fvc == 4 ? 3 : 2], w[5] = vxy[i2];
                if (fvc == 4)
                    w[6] = fc[2], w[7] = vxy[fi[2]];
            }
            w += wpp + 1;
            ++drawn;
        }
    }
done:
    if (w && w != s_ps1S3DBulk)
        PS1ChainBulkEnd((int)((w - s_ps1S3DBulk) / (wpp + 1)));
    PS1Face3DEnd();
    g_ps1S3DHbl[3] = g_ps1S3DHbl[3] + (uint16)(PS1_HBL() - t3);
    g_ps1S3DDraws = g_ps1S3DDraws + 1;
    g_ps1S3DFaces = g_ps1S3DFaces + drawn;
}
} // namespace

void PS1UfoFloorDraw(TileLayer *layer); // PS1UfoFloor.cpp

extern "C" void PS1Scene3DLink()
{
    g_ps1S3DOps.draw  = PS1Scene3DDraw;
    g_ps1S3DOps.floor = PS1UfoFloorDraw;
}
