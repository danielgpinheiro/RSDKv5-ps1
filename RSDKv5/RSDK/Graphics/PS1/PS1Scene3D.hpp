// PS1 port (docs/32 7.3): Scene3D on the GTE. Scene3D.cpp keeps the resident part (PMD6 loading, the models added
// since each Prepare, the work block); the draw (PS1Scene3D.cpp, with ps1/gte3d) is linked into the overlays of the stage
// folders that draw 3D (tools/gen_overlays.py S3D_OBJECTS: the UFO stages), which fill g_ps1S3DOps when they load (the
// overlay loader clears it): GHZ1's STG can't hold 7 KB more resident code.
#pragma once

struct PS1S3DItem {
    RSDK::Matrix m;
    int32 n1[3]; // 7.4: the normals matrix's y row (the lit modes that don't project: the special ring)
    uint32 colour;
    uint16 model, scene;
    uint16 frameA, frameB;
    uint16 interp; // 0..256: frameA -> frameB
    uint8 drawMode;
};
#define PS1_S3D_ITEMS   8
#define PS1_S3D_BUCKETS 256
extern PS1S3DItem g_ps1S3DItems[PS1_S3D_ITEMS];
extern int32 g_ps1S3DCount;
extern uint8 *g_ps1S3DWork; // STG block: projected positions, depths, face keys / links / owners, buckets
extern uint32 g_ps1S3DWorkVerts, g_ps1S3DWorkFaces;
inline uint8 *PS1MeshData(const RSDK::Model *m) { return (uint8 *)m->vertices; }
// PMD6 positions (frame 0): after the corners' colours and indices, at a 4-byte boundary (9f: whole-word keyframes,
// tools/mesh/build_meshes.py)
inline uint32 PS1MeshPosOffset(const RSDK::Model *m) { return (6u * m->indexCount + 3) & ~3u; }
inline uint8 *PS1MeshPos(const RSDK::Model *m) { return PS1MeshData(m) + PS1MeshPosOffset(m); }
// 7.4 / 9o: face corner k's normal (i8 x, y, z): per corner, or through the corner's index into the unique normals
inline const int8 *PS1MeshNormal(const RSDK::Model *m, uint32 k)
{
    const uint8 *b = PS1MeshData(m) + m->ps1NormOffset;
    if (m->flags & PS1_MESH_NRMIDX)
        return (const int8 *)(b + 4 + 4u * b[4 + 4u * *(const uint16 *)b + k]);
    return (const int8 *)(b + 4u * k);
}
struct PS1Scene3DOps {
    void (*draw)(uint16 sceneID);
    void (*floor)(RSDK::TileLayer *layer); // 7.4: the UFO floor (PS1UfoFloor.cpp), for a layer whose scanlines carry a camera
    bool (*sort)(RSDK::DrawList *list);   // ps1-ufo: a sorted draw group's order (PS1Scene3D.cpp); false: the engine sorts
    // ps1-ufo: a band of the strip layer (PS1TileStrip) from the strip: screen lines [sy, sy + h), layer rows [ly, ly + h),
    // layer x at the left edge, palette bank; false: not in the strip (the tiles draw it)
    bool (*band)(RSDK::TileLayer *layer, int32 sy, int32 h, int32 x, int32 ly, uint8 bank);
    // ps1-ufo2: a deforming H-scroll layer with a strip. skyTake (ProcessParallax): true = the overlay draws it from its
    // scroll infos and the scanlines are not filled (ps1Direct = 2); skyDraw (PS1DrawLayerHScroll) then draws its bands,
    // the ones PS1ScanBands would build from those scanlines, through PS1StripBands
    bool (*skyTake)(RSDK::TileLayer *layer);
    void (*skyDraw)(RSDK::TileLayer *layer, bool oneBank); // (the clip's lines, oneBank: PS1DrawLayerHScroll's)
};
extern PS1Scene3DOps g_ps1S3DOps;
extern "C" {
extern volatile uint32 g_ps1S3DDraws, g_ps1S3DFaces, g_ps1S3DCulled, g_ps1S3DOverflow, g_ps1S3DTodo, g_ps1S3DMatrix;
extern volatile uint32 g_ps1S3DHbl[4];
extern volatile int32 g_ps1S3DBackCull;
extern volatile int32 g_ps1S3DAsm; // 9f: the triangle loop in assembly (ps1/s3d_faces.s)
}
