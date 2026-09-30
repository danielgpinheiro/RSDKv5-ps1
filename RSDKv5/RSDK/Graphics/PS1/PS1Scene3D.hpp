// PS1 port (docs/32 7.3): Scene3D on the GTE. Scene3D.cpp keeps the resident part (PMD5 loading, the models added
// since each Prepare, the work block); the draw (PS1Scene3D.cpp, with ps1/gte3d) is linked into the overlays of the stage
// folders that draw 3D (tools/gen_overlays.py S3D_OBJECTS: the UFO stages), which fill g_ps1S3DOps when they load (the
// overlay loader clears it): GHZ1's STG can't hold 7 KB more resident code.
#pragma once

struct PS1S3DItem {
    RSDK::Matrix m;
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
struct PS1Scene3DOps {
    void (*draw)(uint16 sceneID);
};
extern PS1Scene3DOps g_ps1S3DOps;
extern "C" {
extern volatile uint32 g_ps1S3DDraws, g_ps1S3DFaces, g_ps1S3DCulled, g_ps1S3DOverflow, g_ps1S3DTodo, g_ps1S3DMatrix;
extern volatile uint32 g_ps1S3DHbl[4];
extern volatile int32 g_ps1S3DBackCull;
}
