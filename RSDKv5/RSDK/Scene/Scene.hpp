#ifndef SCENE_H
#define SCENE_H

namespace RSDK
{

#define TILE_COUNT    (0x400)
#define TILE_SIZE     (0x10)
#define TILE_DATASIZE (TILE_SIZE * TILE_SIZE)
#define TILESET_SIZE  (TILE_COUNT * TILE_DATASIZE)

#define CPATH_COUNT (2)

#define RSDK_SIGNATURE_CFG (0x474643) // "CFG"
#define RSDK_SIGNATURE_SCN (0x4E4353) // "SCN"
#define RSDK_SIGNATURE_TIL (0x4C4954) // "TIL"

enum LayerTypes {
    LAYER_HSCROLL,
    LAYER_VSCROLL,
    LAYER_ROTOZOOM,
    LAYER_BASIC,
};

struct SceneListInfo {
    RETRO_HASH_MD5(hash);
    char name[0x20];
    uint16 sceneOffsetStart;
    uint16 sceneOffsetEnd;
    uint8 sceneCount;
};

struct SceneListEntry {
    RETRO_HASH_MD5(hash);
    char name[0x20];
    char folder[0x10];
    char id[0x08];
#if RETRO_REV02
    uint8 filter;
#endif
};

enum CollisionModes {
    CMODE_FLOOR,
    CMODE_LWALL,
    CMODE_ROOF,
    CMODE_RWALL,
};

enum EngineStates {
    ENGINESTATE_LOAD,
    ENGINESTATE_REGULAR,
    ENGINESTATE_PAUSED,
    ENGINESTATE_FROZEN,
    ENGINESTATE_STEPOVER = 4,
    ENGINESTATE_DEVMENU  = 8,
    ENGINESTATE_VIDEOPLAYBACK,
    ENGINESTATE_SHOWIMAGE,
#if RETRO_REV02
    ENGINESTATE_ERRORMSG,
    ENGINESTATE_ERRORMSG_FATAL,
#endif
    ENGINESTATE_NONE,
#if RETRO_REV0U
    // Prolly origins-only, called by the ending so I assume this handles playing ending movies and returning to menu
    ENGINESTATE_GAME_FINISHED,
#endif
};

struct SceneInfo {
    Entity *entity;
    SceneListEntry *listData;
    SceneListInfo *listCategory;
    int32 timeCounter;
    int32 currentDrawGroup;
    int32 currentScreenID;
    uint16 listPos;
    uint16 entitySlot;
    uint16 createSlot;
    uint16 classCount;
    bool32 inEditor;
    bool32 effectGizmo;
    bool32 debugMode;
    bool32 useGlobalObjects;
    bool32 timeEnabled;
    uint8 activeCategory;
    uint8 categoryCount;
    uint8 state;
#if RETRO_REV02
    uint8 filter;
#endif
    uint8 milliseconds;
    uint8 seconds;
    uint8 minutes;
};

struct ScrollInfo {
    int32 tilePos;
    int32 parallaxFactor;
    int32 scrollSpeed;
    int32 scrollPos;
    uint8 deform;
    uint8 unknown; // stored in the scene, but always 0, never referenced in-engine either...
};

struct ScanlineInfo {
    Vector2 position; // position of the scanline
    Vector2 deform;   // deformation that should be applied (only applies to RotoZoom type)
};

struct TileLayer {
    uint8 type;
    uint8 drawGroup[CAMERA_COUNT];
    uint8 widthShift;
    uint8 heightShift;
    uint16 xsize;
    uint16 ysize;
    Vector2 position;
    int32 parallaxFactor;
    int32 scrollSpeed;
    int32 scrollPos;
    int32 deformationOffset;
    int32 deformationOffsetW;
    int32 deformationData[0x400];
    int32 deformationDataW[0x400];
    void (*scanlineCallback)(ScanlineInfo *scanlines);
    uint16 scrollInfoCount;
#if RETRO_PLATFORM == RETRO_PS1
    ScrollInfo scrollInfo[0x80]; // PS1 (docs/32 2.3q): 109 in the scenes in scope (GHZ1); SonicMania/GameLink.h matches
#else
    ScrollInfo scrollInfo[0x100];
#endif
    RETRO_HASH_MD5(name);
    uint16 *layout;
    uint8 *lineScroll;
#if RETRO_PLATFORM == RETRO_PS1
    uint8 ps1ChunkUnused; // (was ps1ChunkShift: 7b stores the layout at the layer's own size)
    // PS1 (3.3c): the scroll info every line uses when lineScroll is one value (0xFF: several; set at the scene load:
    // nothing writes lineScroll later), and this frame's uniform scroll when ProcessParallax found no per-line work
    uint8 ps1LineScrollOne, ps1Uniform;
    uint8 ps1Direct; // 3.3h: this frame's lines come from lineScroll and the scroll infos (no deformation), from row ps1UniformY
    int16 ps1UniformX, ps1UniformY;
    // PS1 (docs/32 2.3g, 7b): layout is a chunk index of ps1ChunkRows rows of ps1ChunkStride chunks (the layer's size in
    // 4x4-tile chunks), then a row of chunk 0: columns / rows past the layer read chunk 0 (the stock padding)
    uint16 ps1ChunkStride, ps1ChunkRows;
#endif
};

struct CollisionMask {
    uint8 floorMasks[TILE_SIZE];
    uint8 lWallMasks[TILE_SIZE];
    uint8 rWallMasks[TILE_SIZE];
    uint8 roofMasks[TILE_SIZE];
};

struct TileInfo {
    uint8 floorAngle;
    uint8 lWallAngle;
    uint8 rWallAngle;
    uint8 roofAngle;
    uint8 flag;
};

extern ScanlineInfo *scanlines;
extern TileLayer tileLayers[LAYER_COUNT];
#if RETRO_PLATFORM == RETRO_PS1
void PS1LoadOverlay(const char *folder); // ps1/overlay.cpp (docs/32 2.4)
extern volatile uint32 g_ps1StageObjectsMissing;
extern volatile uint32 g_ps1ScrollInfoDropped;
#endif

#if RETRO_PLATFORM == RETRO_PS1
// PS1 (docs/32 2.3g, choice 29): a layer's `layout` is an index of 4x4-tile chunks (the stock power-of-two padded size in
// chunks) into the scene's chunk pool: the scene file's distinct chunks (tools/data_disc.py; chunk 0 = all 0xFFFF, the
// stock padding) then private chunks for writes. A write into a shared chunk copies it first (PS1SetTile).
extern uint16 *ps1ChunkPool;
extern uint32 ps1ChunkShared; // chunks [0, shared) come from the file and may be shared
extern uint32 ps1ChunkNext;   // next private chunk
extern uint32 ps1ChunkCap;
extern volatile uint32 g_ps1ChunkOverflow; // writes lost for want of a private chunk (GDB; must stay 0)
extern volatile uint32 g_ps1ChunkPrivatePeak;

// 7b: chunk row cy of the layer's index (past the last: the row of chunk 0), and column cx of such a row (past the
// layer: chunk 0)
inline const uint16 *PS1ChunkRow(const TileLayer *layer, uint32 cy)
{
    return &layer->layout[(cy < layer->ps1ChunkRows ? cy : layer->ps1ChunkRows) * layer->ps1ChunkStride];
}
inline uint32 PS1ChunkAt(const TileLayer *layer, const uint16 *row, uint32 cx) { return cx < layer->ps1ChunkStride ? row[cx] : 0; }
inline uint16 *PS1TilePtr(TileLayer *layer, int32 tx, int32 ty)
{
    uint32 chunk = PS1ChunkAt(layer, PS1ChunkRow(layer, (uint32)ty >> 2), (uint32)tx >> 2);
    return &ps1ChunkPool[(chunk << 4) + ((ty & 3) << 2) + (tx & 3)];
}
void PS1SetTile(TileLayer *layer, int32 tx, int32 ty, uint16 tile);
#define PS1_LAYOUT(layer, tx, ty) (*PS1TilePtr(layer, tx, ty))
#else
#define PS1_LAYOUT(layer, tx, ty) ((layer)->layout[(tx) + ((ty) << (layer)->widthShift)])
#endif

#if RETRO_PLATFORM == RETRO_PS1
// PS1: plane A's unflipped masks and angles; plane B only where it differs (PS1/PS1Collision.hpp)
extern CollisionMask collisionMasks[1][TILE_COUNT];
extern TileInfo tileInfo[1][TILE_COUNT];
#include "PS1/PS1Collision.hpp"
#include "PS1/PS1Tiles.hpp"
// 3.3 (PS1/PS1Layers.cpp): the draw lists' frame start (clear + CLUTs), tile layers through the GPU chain
void PS1LayersFrameBegin();
// 3.5 (PS1Layers.cpp): FillScreen on the PS1 blends what the frame drew so far: the CLUT uploads chained this frame and
// the direct colours recorded (flat rectangles, one-colour runs, the clear) are rewritten in the arena with the PC's
// per-channel formula; the next CLUT-using draw uploads the unfaded CLUTs again (PS1FadeRestore)
void PS1FadeScreen(uint32 color, int32 alphaR, int32 alphaG, int32 alphaB);
// 4.3: the pause's grey tint over everything drawn so far (its table marked PS1_TINT_GREY_MARK: PauseMenu.c)
#define PS1_TINT_GREY_MARK (0x0001)
void PS1TintScreenGrey();
void PS1FadeRecordColour(uint32_t *word); // a primitive's colour word (bits 0-23: 0xBBGGRR) drawn this frame
// 3.6b: a span of flat rectangles as PS1FlatBatchFlush writes it (from its first fragment header: header, the texpage
// word when semi, then up to four 3-word rectangles, the next fragment right after): one record for all their colours
void PS1FadeRecordSpan(uint32_t *firstHeader, int32 rects, bool semi);
void PS1FadeRestore();                     // before a CLUT-using draw: the unfaded CLUTs, once after a fade
// 3.5b: a CLUT row uploaded in the chain now (recorded for a later FillScreen), then the GPU's CLUT cache flushed
void PS1UploadClut(const uint16 *row, int32 n, int16 x, int16 y);
void PS1DrawLayerHScroll(TileLayer *layer);
void PS1DrawLayerBasic(TileLayer *layer); // 4.1c: DrawLayerBasic (one scroll position for every line, palette bank 0)
void PS1DrawLayerRotozoom(TileLayer *layer); // 4.1d: DrawLayerRotozoom (a quad per tile, corners placed by the scanlines)
extern "C" {
extern volatile uint32 g_ps1LayerBands, g_ps1LayerPieces, g_ps1LayerMisses, g_ps1LayerHbl, g_ps1LayerSolidRuns;
}
#define PS1_TILE_ANGLE(plane, tile, mode, field) PS1TileAngle(plane, (tile) & 0xFFF, mode)
#else
extern CollisionMask collisionMasks[CPATH_COUNT][TILE_COUNT * 4]; // 1024 * 1 per direction
extern TileInfo tileInfo[CPATH_COUNT][TILE_COUNT * 4];            // 1024 * 1 per direction
#define PS1_TILE_ANGLE(plane, tile, mode, field) tileInfo[plane][(tile) & 0xFFF].field
#endif

#if RETRO_REV02
extern bool32 forceHardReset;
#endif
extern char currentSceneFolder[0x10];
extern char currentSceneID[0x10];
#if RETRO_REV02
extern uint8 currentSceneFilter;
#endif

extern SceneInfo sceneInfo;

#if RETRO_PLATFORM == RETRO_PS1
extern uint8 tilesetPixels[TILE_DATASIZE]; // PS1: tiles live in VRAM (phase 3); nothing writes this
#else
extern uint8 tilesetPixels[TILESET_SIZE * 4];
#endif

void LoadSceneFolder();
void LoadSceneAssets();
void LoadTileConfig(char *filepath);
void LoadStageGIF(char *filepath);

void ProcessParallaxAutoScroll();
void ProcessParallax(TileLayer *layer);
void ProcessSceneTimer();

void SetScene(const char *categoryName, const char *sceneName);
inline void LoadScene()
{
    if ((sceneInfo.state & ENGINESTATE_STEPOVER) == ENGINESTATE_STEPOVER)
        sceneInfo.state = ENGINESTATE_LOAD | ENGINESTATE_STEPOVER;
    else
        sceneInfo.state = ENGINESTATE_LOAD;
}

#if RETRO_REV02
inline void ForceHardReset(bool32 shouldHardReset) { forceHardReset = shouldHardReset; }
#endif

inline bool32 CheckValidScene()
{
    if (sceneInfo.activeCategory >= sceneInfo.categoryCount)
        return false;

    SceneListInfo *list = &sceneInfo.listCategory[sceneInfo.activeCategory];
    return sceneInfo.listPos >= list->sceneOffsetStart && sceneInfo.listPos <= list->sceneOffsetEnd;
}

inline bool32 CheckSceneFolder(const char *folderName) { return strcmp(folderName, sceneInfo.listData[sceneInfo.listPos].folder) == 0; }

inline uint16 GetTileLayerID(const char *name)
{
    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(name, hash);

    for (int32 i = 0; i < LAYER_COUNT; ++i) {
        if (HASH_MATCH_MD5(tileLayers[i].name, hash))
            return i;
    }

    return (uint16)-1;
}

inline TileLayer *GetTileLayer(uint16 layerID) { return layerID < LAYER_COUNT ? &tileLayers[layerID] : NULL; }

inline void GetLayerSize(uint16 layerID, Vector2 *size, bool32 usePixelUnits)
{
    if (layerID < LAYER_COUNT && size) {
        TileLayer *layer = &tileLayers[layerID];

        if (usePixelUnits) {
            size->x = TILE_SIZE * layer->xsize;
            size->y = TILE_SIZE * layer->ysize;
        }
        else {
            size->x = layer->xsize;
            size->y = layer->ysize;
        }
    }
}

inline uint16 GetTile(uint16 layerID, int32 tileX, int32 tileY)
{
    if (layerID < LAYER_COUNT) {
        TileLayer *layer = &tileLayers[layerID];
        if (tileX >= 0 && tileX < layer->xsize && tileY >= 0 && tileY < layer->ysize)
            return PS1_LAYOUT(layer, tileX, tileY);
    }

    return (uint16)-1;
}

inline void SetTile(uint16 layerID, int32 tileX, int32 tileY, uint16 tile)
{
    if (layerID < LAYER_COUNT) {
        TileLayer *layer = &tileLayers[layerID];
        if (tileX >= 0 && tileX < layer->xsize && tileY >= 0 && tileY < layer->ysize)
#if RETRO_PLATFORM == RETRO_PS1
            PS1SetTile(layer, tileX, tileY, tile);
#else
            layer->layout[tileX + (tileY << layer->widthShift)] = tile;
#endif
    }
}

#if RETRO_PLATFORM == RETRO_PS1
inline int32 GetTileAngle(uint16 tile, uint8 cPlane, uint8 cMode) { return PS1TileAngle(cPlane, tile & 0xFFF, cMode); }
inline void SetTileAngle(uint16 tile, uint8 cPlane, uint8 cMode, uint8 angle)
{
    // stock writes the unflipped entry only; here the flipped angles follow it (Mania never calls this)
    TileInfo *info = PS1WritableInfo(cPlane, tile & 0x3FF);
    if (!info)
        return;
    switch (cMode) {
        default: break;
        case CMODE_FLOOR: info->floorAngle = angle; break;
        case CMODE_LWALL: info->lWallAngle = angle; break;
        case CMODE_ROOF: info->roofAngle = angle; break;
        case CMODE_RWALL: info->rWallAngle = angle; break;
    }
}
inline uint8 GetTileFlags(uint16 tile, uint8 cPlane) { return PS1BaseInfo(cPlane, tile & 0x3FF)->flag; }
inline void SetTileFlags(uint16 tile, uint8 cPlane, uint8 flag)
{
    TileInfo *info = PS1WritableInfo(cPlane, tile & 0x3FF);
    if (info)
        info->flag = flag;
}
#else
inline int32 GetTileAngle(uint16 tile, uint8 cPlane, uint8 cMode)
{
    switch (cMode) {
        default: return 0;
        case CMODE_FLOOR: return tileInfo[cPlane & 1][tile & 0xFFF].floorAngle;
        case CMODE_LWALL: return tileInfo[cPlane & 1][tile & 0xFFF].lWallAngle;
        case CMODE_ROOF: return tileInfo[cPlane & 1][tile & 0xFFF].roofAngle;
        case CMODE_RWALL: return tileInfo[cPlane & 1][tile & 0xFFF].rWallAngle;
    }
}
inline void SetTileAngle(uint16 tile, uint8 cPlane, uint8 cMode, uint8 angle)
{
    switch (cMode) {
        default: break;
        case CMODE_FLOOR: tileInfo[cPlane & 1][tile & 0x3FF].floorAngle = angle; break;
        case CMODE_LWALL: tileInfo[cPlane & 1][tile & 0x3FF].lWallAngle = angle; break;
        case CMODE_ROOF: tileInfo[cPlane & 1][tile & 0x3FF].roofAngle = angle; break;
        case CMODE_RWALL: tileInfo[cPlane & 1][tile & 0x3FF].rWallAngle = angle; break;
    }
}

inline uint8 GetTileFlags(uint16 tile, uint8 cPlane) { return tileInfo[cPlane & 1][tile & 0x3FF].flag; }
inline void SetTileFlags(uint16 tile, uint8 cPlane, uint8 flag) { tileInfo[cPlane & 1][tile & 0x3FF].flag = flag; }
#endif

void CopyTileLayer(uint16 dstLayerID, int32 dstStartX, int32 dstStartY, uint16 srcLayerID, int32 srcStartX, int32 srcStartY, int32 countX,
                   int32 countY);

inline void CopyTile(uint16 dest, uint16 src, uint16 count)
{
#if RETRO_PS1_HEADLESS
    return; // PS1: tile pixels live in VRAM (phase 3 copies them there)
#endif
    if (dest > TILE_COUNT)
        dest = TILE_COUNT - 1;

    if (src > TILE_COUNT)
        src = TILE_COUNT - 1;

    if (count > TILE_COUNT)
        count = TILE_COUNT - 1;

    uint8 *destPixels = &tilesetPixels[TILE_DATASIZE * dest];
    uint8 *srcPixels  = &tilesetPixels[TILE_DATASIZE * src];

    uint8 *destPixelsX = &tilesetPixels[(TILE_DATASIZE * dest) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_X)];
    uint8 *srcPixelsX  = &tilesetPixels[(TILE_DATASIZE * src) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_X)];

    uint8 *destPixelsY = &tilesetPixels[(TILE_DATASIZE * dest) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_Y)];
    uint8 *srcPixelsY  = &tilesetPixels[(TILE_DATASIZE * src) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_Y)];

    uint8 *destPixelsXY = &tilesetPixels[(TILE_DATASIZE * dest) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_XY)];
    uint8 *srcPixelsXY  = &tilesetPixels[(TILE_DATASIZE * src) + ((TILE_COUNT * TILE_DATASIZE) * FLIP_XY)];

    for (int32 t = 0; t < count; ++t) {
        for (int32 p = 0; p < TILE_DATASIZE; ++p) {
            *destPixels++   = *srcPixels++;
            *destPixelsX++  = *srcPixelsX++;
            *destPixelsY++  = *srcPixelsY++;
            *destPixelsXY++ = *srcPixelsXY++;
        }
    }
}

inline ScanlineInfo *GetScanlines() { return scanlines; }

// Draw a layer with horizonal scrolling capabilities
void DrawLayerHScroll(TileLayer *layer);
// Draw a layer with vertical scrolling capabilities
void DrawLayerVScroll(TileLayer *layer);
// Draw a layer with rotozoom (via scanline callback) capabilities
void DrawLayerRotozoom(TileLayer *layer);
// Draw a "basic" layer, no special capabilities, but it's the fastest to draw
void DrawLayerBasic(TileLayer *layer);

#if RETRO_REV0U
#include "Legacy/SceneLegacy.hpp"
#endif

} // namespace RSDK

#endif
