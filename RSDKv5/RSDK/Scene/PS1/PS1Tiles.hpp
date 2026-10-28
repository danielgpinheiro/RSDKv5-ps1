#pragma once
// PS1 port (docs/32 3.1a): the scene's tile images in VRAM, from Data/Stages/<Folder>/Scene<N>.ptl
// (tools/gfx/build_tiles.py). Replaces tilesetPixels (1024 tiles x 4 flips in RAM on the PC): each tile the scene's
// layers use is an image in a texture column (4-bit on a shared CLUT, or 8-bit on the palette bank's CLUT; flips are
// drawn with backwards UVs). DrawAniTile re-points tiles to the animated-tile sheet's cell images. The file also lists
// the layers' one-colour lines (3.3b). Included by Scene.hpp inside namespace RSDK.

struct PS1TileImage {
    uint8 u, v;
    uint16 tpage; // GPU texpage attribute (page, depth)
    uint16 clut;  // GPU CLUT attribute; 0xFFFF = the palette bank's 256-entry CLUT (8-bit)
};

struct PS1TileClut {
    uint16 x, y;
    uint8 map[16]; // palette index per 4-bit value (entry 0 = transparent)
};

#define PS1_TILE_NONE (0xFFFF)

// Loads the scene's tiles (LoadSceneAssets, while DATASET_TMP is empty: its memory stages the pixels). False: no file.
bool PS1TilesLoad(const char *folder, const char *sceneID);
void PS1TilesRelease(); // 4.3: a same-folder reload drops the tables before its STG compaction (PS1TilesLoad reloads them)
// DrawAniTile on the PS1: 16-aligned copies re-point the tiles; others are counted (g_ps1AniTileMisses)
void PS1TilesAniTile(uint16 sheetID, uint16 tileIndex, uint16 srcX, uint16 srcY, uint16 width, uint16 height);

// The tables (one STG block; reached through its storage pointer, so a compaction can move it)
uint16 PS1TileImageOf(uint16 tile);          // image of a tile (PS1_TILE_NONE: none)
const uint16 *PS1TileImageTable();           // the same for tiles 0-1023 as a table (NULL: none loaded)
uint32 PS1TileLodInfo(); // 7.4: a UFO floor's minimap: first image | pages across << 16 | down << 24 (0: none)
uint16 PS1TilePatternImage(); // 7.5: a UFO stage's 3D floor / roof texture (256x256, 8-bit), 0: none
const PS1TileImage *PS1TileImages();
const PS1TileClut *PS1TileCluts();
uint16 PS1TileClutCount();
#if PS1_DEBUG_OVERLAY
uint32 PS1TilesVramBytes(); // ps1/debug_overlay.cpp
#endif
// One-colour lines (docs/32 3.3b): the layer's table (palette index per pixel row, 0 = drawn with tiles), NULL when the
// scene has none for it (or a tile on its lines changed: SetTile on the layer, DrawAniTile on one of those tiles).
// The pointer is into the STG block: use it within one draw.
const uint8 *PS1TileSolidLines(int32 layer, int32 heightPx);
void PS1TileSolidDrop(int32 layer); // SetTile on the layer (-1: every layer); also turns the layer's roto page off
// 9e: a rotozoom layer drawn from one 256x256 page (tools/gfx/build_tiles.py ROTO_PAGES): its image (u = v = 0) and the
// page's origin in the layer's map (texels; the page repeats with the map). False: none (or SetTile turned it off)
bool PS1TileRotoPage(int32 layer, PS1TileImage *image, int32 *ox, int32 *oy); // ox bit 16: the map repeats the page (9u.18)
// The image of `tile` H-flipped (built for the tiles per-line rows show H-flipped), PS1_TILE_NONE if there is none
// key: the tile (H), tile | 0x800 (V), tile | 0xC00 (both); PS1_TILE_NONE: no copy. 9u.14: the cache test inline (a call per
// flipped tile cost the UFO stages' backgrounds 8-11 hblanks a frame), 64 entries (tile << 16 | image, direct-mapped)
extern uint32 ps1HFlipCache[64];
uint16 PS1TileHFlipImageMiss(uint16 tile);
inline uint16 PS1TileHFlipImage(uint16 tile)
{
    uint32 c = ps1HFlipCache[(tile ^ tile >> 6) & 63];
    return (c >> 16) == tile ? (uint16)c : PS1TileHFlipImageMiss(tile);
}
bool PS1TileFlipCopiesAll(); // 9f: every flipped tile of the scene's layers has its copy (FLIP_COPIES folders)

extern "C" {
extern volatile uint32 g_ps1TileLoads;     // .ptl files loaded
extern volatile uint32 g_ps1TileVramBytes; // pixel bytes uploaded by the last load
extern volatile uint32 g_ps1TileErrors;    // a bad or unreadable file (must stay 0)
extern volatile uint32 g_ps1AniTileCalls;  // DrawAniTile calls re-pointed
extern volatile uint32 g_ps1AniTileMisses; // DrawAniTile calls the PS1 cannot do yet (unaligned / unknown sheet)
extern volatile uint32 g_ps1SolidLines;    // one-colour lines in the scene's tables (as loaded)
extern volatile uint32 g_ps1SolidDrops;    // tables switched off by a SetTile / DrawAniTile on their tiles
extern volatile uint32 g_ps1HFlipDrops;    // H-flipped copies switched off by a DrawAniTile on their tile
extern volatile uint32 g_ps1RotoPageDrops; // roto pages switched off by a SetTile on their layer
}
