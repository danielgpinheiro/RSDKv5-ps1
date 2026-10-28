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
// stored halved and drawn 2x), 7-15 CLUT slot (tools/gfx/vram_plan.py SPRITE_CLUT_SLOTS; 0x1FF = the palette bank's).
// 7b.12: quarter resolution (stored at a quarter, drawn 4x: build_atlas QUARTER, 4-bit only) = bit 6 with the CLUT field
// slot + PS1_ATLAS_QUARTER_CLUT (the field's values 0x140-0x1FE are no slot)
#define PS1_ATLAS_COLUMN(attr)    ((attr) & 15)
#define PS1_ATLAS_ROW(attr)       (((attr) >> 4) & 1)
#define PS1_ATLAS_8BIT(attr)      (((attr) >> 5) & 1)
#define PS1_ATLAS_HALF(attr)      (((attr) >> 6) & 1)
#define PS1_ATLAS_BANK_CLUT       (0x1FF)
#define PS1_ATLAS_QUARTER_CLUT    (0x140)
#define PS1_ATLAS_QUARTER(attr)   (PS1_ATLAS_HALF(attr) && ((attr) >> 7) >= PS1_ATLAS_QUARTER_CLUT && ((attr) >> 7) != PS1_ATLAS_BANK_CLUT)
#define PS1_ATLAS_CLUT(attr)      (PS1_ATLAS_QUARTER(attr) ? ((attr) >> 7) - PS1_ATLAS_QUARTER_CLUT : (attr) >> 7)
#define PS1_ATLAS_SHIFT(attr)     (PS1_ATLAS_QUARTER(attr) ? 2 : PS1_ATLAS_HALF(attr)) // texel = 1 << shift pixels

struct PS1AtlasClut {
    uint16 x, y;
    uint8 map[16]; // palette index per 4-bit value (entry 0 = transparent)
};

bool PS1AtlasLoad(const char *folder, const char *sceneID); // LoadSceneAssets start (DATASET_TMP empty: pixel staging)
const char *PS1CharSfxSuffix(); // 9g: "_mighty" / "_ray" with the Plus characters setting (the SFX bank variant), else ""
void PS1AtlasRelease(); // 4.3: a same-folder reload drops the tables before its STG compaction (PS1AtlasLoad reloads them)
void PS1AtlasLoadBindings();                                 // LoadSceneAssets end: the bindings, in the STG tail
void PS1AtlasBind(uint16 sprID);                             // LoadSpriteAnimation: its frames' bindings
uint32 PS1AtlasSheetBinding(const uint32 *hash);             // 7.5: a deform sheet's place (u | v << 8 | attr << 16)
extern void (*g_ps1DeformDraw)(uint16 sheetID, int32 inkEffect, int32 alpha); // 7.5: DrawDeformedSprite (overlay extra)
void PS1AtlasBindEnd();                                      // after InitObjects: the table goes
bool PS1AtlasLoadGroup(const char *name);                    // a group into its region (a CD read: where the music changes)
void PS1AtlasServiceRequests();                              // frame() start: a GDB-requested group load (g_ps1AtlasGroupReq)
void PS1AtlasMusicGroups(const char *track);
void PS1AtlasPlayerGroup(); // 9l: each frame after the music's groups: the player group the game needs (Super Sonic)
const uint8 *PS1AtlasDropped(const uint32 *hash); // 7b: a sprite file's dropped animations (Scene<N>.pda entry), or NULL // 7b: the groups a music track brings (AudioDevice::FrameInit, before its XA)
const PS1AtlasClut *PS1AtlasCluts();
uint16 PS1AtlasClutCount();
#if PS1_DEBUG_OVERLAY
uint32 PS1AtlasVramBytes(uint32 *groupExtra); // ps1/debug_overlay.cpp
#endif

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
// docs/32 7.3: Scene3D faces (Scene3D.cpp PS1 Draw3DScene), back to front: Begin sets the ink (false: nothing to draw;
// the flat colour as the GPU takes it and the semi-transparency bit), Slot gives the words of one face (wpp: G3 6, G4 8,
// F3 4, F4 5; NULL: the chain is full), End sends what is batched
bool PS1Face3DBegin(int32 inkEffect, int32 alpha, uint32 flatColor, uint32 *flatBgr, uint32 *semi);
uint32 *PS1Face3DSlot(int32 wpp);
void PS1Face3DEnd();
void PS1BlendedFaceBulk(Vector2 *vertices, uint32 *colors, int32 vertCount); // 9f: an opaque blended face into the open Face3D bulk
void PS1LineBulk(int32 x1, int32 y1, int32 x2, int32 y2, uint32 bgr); // 9o: an INK_NONE line into the open Face3D bulk
void PS1LineFace(const int32 *xs, const int32 *ys, const uint16 *c, int32 n, uint32 bgr); // 9o: a face's edges, the same way
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
