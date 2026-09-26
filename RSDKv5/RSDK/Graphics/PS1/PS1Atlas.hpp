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
void PS1AtlasLoadBindings();                                 // LoadSceneAssets end: the bindings, in the STG tail
void PS1AtlasBind(uint16 sprID);                             // LoadSpriteAnimation: its frames' bindings
void PS1AtlasBindEnd();                                      // after InitObjects: the table goes
bool PS1AtlasLoadGroup(const char *name);                    // a group into its region (a CD read: where the music changes)
void PS1AtlasServiceRequests();                              // frame() start: a GDB-requested group load (g_ps1AtlasGroupReq)
const PS1AtlasClut *PS1AtlasCluts();
uint16 PS1AtlasClutCount();

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
