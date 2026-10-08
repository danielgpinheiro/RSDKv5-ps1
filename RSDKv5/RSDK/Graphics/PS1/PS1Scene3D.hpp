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
struct PS1Scene3DOps {
    void (*draw)(uint16 sceneID);
    void (*floor)(RSDK::TileLayer *layer); // 7.4: the UFO floor (PS1UfoFloor.cpp), for a layer whose scanlines carry a camera
};
extern PS1Scene3DOps g_ps1S3DOps;
extern "C" {
extern volatile uint32 g_ps1S3DDraws, g_ps1S3DFaces, g_ps1S3DCulled, g_ps1S3DOverflow, g_ps1S3DTodo, g_ps1S3DMatrix;
extern volatile uint32 g_ps1S3DHbl[4];
extern volatile int32 g_ps1S3DBackCull;
extern volatile int32 g_ps1S3DAsm; // 9f: the triangle loop in assembly (ps1/s3d_faces.s)
}
