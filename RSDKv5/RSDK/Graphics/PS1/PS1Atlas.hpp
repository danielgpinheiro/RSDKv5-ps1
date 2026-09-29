#pragma once
// PS1 port (docs/32 3.1c): the scene's sprite atlas, Data/Stages/<Folder>/Scene<N>.atl (tools/gfx/build_atlas.py).
// Included by Animation.hpp inside namespace RSDK.
//
// The sheets' pixels live in VRAM only: each frame record carries its binding (SpriteFrame::ps1U / ps1V / ps1Attr:
// the texture page, the u / v of its top-left texel, depth, half resolution, CLUT slot) or PS1_ATLAS_MISSING. The
// atlas has a default state (resident frames, the player's normal frames, the title card), loaded with the scene, and
// groups that take turns in two regions (the player region: normal / Super; the event region: title card, boss, sign
// post), swapped where the music changes: PS1AtlasLoadGroup.

#define PS1_ATLAS_MISSING (0xFFFF) // ps1Attr of a frame the atlas does not hold (or whose group is not loaded)
#define PS1_ATLAS_OUTSIDE (0xFE)   // ps1U / ps1V with PS1_ATLAS_MISSING: the frame lies wholly outside its sheet (4.2d)
// ps1Attr: bits 0-3 texture column (X / 64), 4 page row (Y / 256), 5 8-bit (else 4-bit), 6 half resolution (the frame is
// stored halved and drawn 2x), 7-15 CLUT slot (tools/gfx/vram_plan.py SPRITE_CLUT_SLOTS; 0x1FF = the palette bank's)
#define PS1_ATLAS_COLUMN(attr) ((attr) & 15)
#define PS1_ATLAS_ROW(attr)    (((attr) >> 4) & 1)
#define PS1_ATLAS_8BIT(attr)   (((attr) >> 5) & 1)
#define PS1_ATLAS_HALF(attr)   (((attr) >> 6) & 1)
#define PS1_ATLAS_CLUT(attr)   ((attr) >> 7)
#define PS1_ATLAS_BANK_CLUT    (0x1FF)

struct PS1AtlasClut {
    uint16 x, y;
    uint8 map[16]; // palette index per 4-bit value (entry 0 = transparent)
};

bool PS1AtlasLoad(const char *folder, const char *sceneID); // LoadSceneAssets start (DATASET_TMP empty: pixel staging)
void PS1AtlasRelease(); // 4.3: a same-folder reload drops the tables before its STG compaction (PS1AtlasLoad reloads them)
void PS1AtlasLoadBindings();                                 // LoadSceneAssets end: the bindings, in the STG tail
void PS1AtlasBind(uint16 sprID);                             // LoadSpriteAnimation: its frames' bindings
void PS1AtlasBindEnd();                                      // after InitObjects: the table goes
bool PS1AtlasLoadGroup(const char *name);                    // a group into its region (a CD read: where the music changes)
void PS1AtlasServiceRequests();                              // frame() start: a GDB-requested group load (g_ps1AtlasGroupReq)
const PS1AtlasClut *PS1AtlasCluts();
uint16 PS1AtlasClutCount();

// 3.4 (PS1Sprites.cpp): a frame at screen (x, y) (its top-left, as DrawSpriteFlipped gets it) through the GPU chain
void PS1DrawFrame(const SpriteFrame *frame, int32 x, int32 y, int32 direction, int32 inkEffect, int32 alpha);
// 3.6: DrawRectangle (clipped), DrawFace, DrawCircle (screen x, y), DrawLine (screen), DrawBlendedFace through the GPU
void PS1DrawRect(int32 x, int32 y, int32 w, int32 h, uint32 color, int32 alpha, int32 inkEffect);
void PS1DrawFace(Vector2 *vertices, int32 vertCount, int32 r, int32 g, int32 b, int32 alpha, int32 inkEffect);
void PS1DrawCircle(int32 x, int32 y, int32 radius, uint32 color, int32 alpha, int32 inkEffect);
void PS1FillOpaque(uint32 r5, uint32 g5, uint32 b5); // 4.2e: FillScreen at full alpha (5-bit channels of the result)
void PS1DrawCircleOutline(int32 x, int32 y, int32 innerRadius, int32 outerRadius, int32 left, int32 top, int32 right, int32 bottom,
                          uint32 color, int32 alpha, int32 inkEffect);
void PS1DrawLine(int32 x1, int32 y1, int32 x2, int32 y2, uint32 color, int32 alpha, int32 inkEffect);
void PS1DrawBlendedFace(Vector2 *vertices, uint32 *colors, int32 vertCount, int32 alpha, int32 inkEffect);
// 3.4c-d: rotated / scaled frames (DrawSpriteRotozoom's geometry), DrawTile's pieces from the scene's tile images
void PS1DrawFrameRotozoom(const SpriteFrame *frame, int32 x, int32 y, int32 scaleX, int32 scaleY, int32 direction, int32 rotation,
                          int32 inkEffect, int32 alpha);
void PS1DrawTile(uint16 tile, int32 x, int32 y, int32 inkEffect, int32 alpha);
void PS1DrawTileRotozoom(uint16 tile, int32 x, int32 y, int32 pivotX, int32 pivotY, int32 scaleX, int32 scaleY, int32 direction,
                         int32 rotation, int32 inkEffect, int32 alpha);
void PS1SpritesFrameBegin(); // PS1LayersFrameBegin: the frame's sprite counters and draw log start over
#if PS1_DEBUG_BIGRAM
// the frame's draw calls in order, for the host reference (8 MB link): type 0 = a tile layer (x = its call number)
void PS1DrawLogAdd(uint8 type, uint8 direction, uint8 ink, uint8 alpha, uint8 u, uint8 v, uint16 attr, int32 x, int32 y, int32 w,
                   int32 h);
#endif

extern "C" {
extern volatile uint32 g_ps1AtlasLoads;        // .atl files loaded
extern volatile uint32 g_ps1AtlasVramBytes;    // default pixels uploaded by the last load
extern volatile uint32 g_ps1AtlasErrors;       // a bad or unreadable file (must stay 0)
extern volatile uint32 g_ps1AtlasBound;        // frame records given a binding (this scene)
extern volatile uint32 g_ps1AtlasUnplaced;     // frame records the atlas leaves out (bound as missing; fine unless drawn)
extern volatile uint32 g_ps1AtlasUnknownFiles; // sprite files the atlas does not list (every frame missing: must stay 0)
extern volatile uint32 g_ps1AtlasLateBinds;    // sprite files loaded after the scene start (bound as missing for now)
extern volatile uint32 g_ps1AtlasGroupLoads;   // group swaps
extern volatile uint32 g_ps1AtlasGroupErrors;  // unknown group / unreadable (must stay 0)
extern volatile int32 g_ps1AtlasGroupReq;      // GDB: a group index to load at the next frame (-1: none)
}
