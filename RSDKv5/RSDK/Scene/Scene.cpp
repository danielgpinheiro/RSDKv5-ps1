#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_PLATFORM == RETRO_PS1
#endif

#if RETRO_REV0U
#include "Legacy/SceneLegacy.cpp"
#endif

#if RETRO_PLATFORM == RETRO_PS1
#include "PS1/PS1Tiles.cpp"  // the scene's tile images in VRAM (docs/32 3.1a)
#include "PS1/PS1Layers.cpp" // tile layers drawn by the GPU (3.3)
#include "RSDK/Graphics/PS1/PS1Scene3D.hpp" // g_ps1S3DOps.floor: the UFO floor (7.4)
#include "PS1/PS1Roto.cpp"   // rotozoom layers (4.1d)
#endif

#if RETRO_PLATFORM == RETRO_PS1
uint8 RSDK::tilesetPixels[TILE_DATASIZE];
#else
uint8 RSDK::tilesetPixels[TILESET_SIZE * 4];
#endif

ScanlineInfo *RSDK::scanlines = NULL;
TileLayer RSDK::tileLayers[LAYER_COUNT];
#if RETRO_PLATFORM == RETRO_PS1
// PS1 (docs/32 2.3o): plane A; plane B where it differs (PS1/PS1Collision.hpp)
CollisionMask RSDK::collisionMasks[1][TILE_COUNT];
TileInfo RSDK::tileInfo[1][TILE_COUNT];
uint16 RSDK::ps1PlaneB[TILE_COUNT];
CollisionMask RSDK::ps1MasksB[PS1_PLANEB_MAX];
TileInfo RSDK::ps1InfoB[PS1_PLANEB_MAX];
uint16 RSDK::ps1PlaneBCount                = 0;
volatile uint32 RSDK::g_ps1PlaneBOverflow = 0;

bool32 RSDK::PS1PlaneBSplit(uint16 t)
{
    if (ps1PlaneBCount >= PS1_PLANEB_MAX) {
        g_ps1PlaneBOverflow = g_ps1PlaneBOverflow + 1;
        return false;
    }
    ps1MasksB[ps1PlaneBCount] = collisionMasks[0][t];
    ps1InfoB[ps1PlaneBCount]  = tileInfo[0][t];
    ps1PlaneB[t]              = ps1PlaneBCount++;
    return true;
}
#else
CollisionMask RSDK::collisionMasks[CPATH_COUNT][TILE_COUNT * 4];
TileInfo RSDK::tileInfo[CPATH_COUNT][TILE_COUNT * 4];
#endif

#if RETRO_REV02
bool32 RSDK::forceHardReset = false;
#endif
char RSDK::currentSceneFolder[0x10];
char RSDK::currentSceneID[0x10];

SceneInfo RSDK::sceneInfo;
#if RETRO_PLATFORM == RETRO_PS1
volatile uint32 RSDK::g_ps1StageObjectsMissing = 0; // this scene's StageConfig objects not registered (GDB; must be 0)
volatile uint32 RSDK::g_ps1StageObjectsLeftOut = 0; // 7b: of them, the ones the act's overlay leaves out on purpose
volatile uint32 RSDK::g_ps1ScrollInfoDropped   = 0; // scroll infos past the PS1's 0x80 per layer (GDB; must be 0)
#endif

#if RETRO_PLATFORM == RETRO_PS1
// PS1 (docs/32 2.3g, choice 29): the scene's layout chunks (Scene.hpp).
uint16 *RSDK::ps1ChunkPool                   = NULL;
uint16 *RSDK::ps1RowPool                     = NULL;
uint32 RSDK::ps1RowShared                    = 0;
uint32 RSDK::ps1RowNext                      = 0;
uint32 RSDK::ps1RowCap                       = 0;
uint32 RSDK::ps1ChunkShared                  = 0;
uint32 RSDK::ps1ChunkNext                    = 0;
uint32 RSDK::ps1ChunkCap                     = 0;
volatile uint32 RSDK::g_ps1ChunkOverflow     = 0;
volatile uint32 RSDK::g_ps1ChunkPrivatePeak  = 0;

#define PS1_CHUNK_BYTES   (4 * sizeof(uint16)) // 7b.3: 4 row indices
#define PS1_CHUNK_PRIVATE (128) // chunks for writes per scene (1 KB): GHZ1 play uses 6 (g_ps1ChunkPrivatePeak)
#define PS1_ROW_PRIVATE   (256) // rows for writes per scene (2 KB)
#define PS1_ROW_BYTES     (4 * sizeof(uint16))

namespace
{
// A raw block of the PS1 data disc (tools/data_disc.py: u32 size + 4, u32 big-endian size, the bytes) read straight into
// a new block of `set` with `extra` bytes after it. Returns the block's size (0: none allocated).
uint32 PS1ReadRawBlock(FileInfo *info, void **buffer, StorageDataSets set, uint32 extra = 0)
{
    uint32 cSize  = ReadInt32(info, false) - 4;
    uint32 sizeBE = ReadInt32(info, false);
    uint32 size   = (sizeBE << 24) | ((sizeBE << 8) & 0x00FF0000) | ((sizeBE >> 8) & 0x0000FF00) | (sizeBE >> 24);
    *buffer       = NULL;
    if (cSize != size) {
        g_ps1CompressedMismatch = g_ps1CompressedMismatch + 1;
        Seek_Cur(info, cSize);
        return 0;
    }
    if (!size && !extra)
        return 0;
    AllocateStorage(buffer, size + extra, set, false);
    ReadBytes(info, *buffer, size);
    return size;
}

void PS1ReadChunkPool(FileInfo *info)
{
    uint32 bytes   = PS1ReadRawBlock(info, (void **)&ps1ChunkPool, DATASET_STG, PS1_CHUNK_PRIVATE * PS1_CHUNK_BYTES);
    ps1ChunkShared = bytes / PS1_CHUNK_BYTES;
    ps1ChunkNext   = ps1ChunkShared;
    ps1ChunkCap    = ps1ChunkShared + PS1_CHUNK_PRIVATE;
    bytes          = PS1ReadRawBlock(info, (void **)&ps1RowPool, DATASET_STG, PS1_ROW_PRIVATE * PS1_ROW_BYTES);
    ps1RowShared   = bytes / PS1_ROW_BYTES;
    ps1RowNext     = ps1RowShared;
    ps1RowCap      = ps1RowShared + PS1_ROW_PRIVATE;
}
} // namespace

extern "C" {
volatile uint32 g_ps1SetTileOutside = 0; // 7b: SetTile past a layer's size (the PC writes its padding; total)
}
void RSDK::PS1SetTile(TileLayer *layer, int32 tx, int32 ty, uint16 tile)
{
    uint32 cx = (uint32)tx >> 2, cy = (uint32)ty >> 2;
    if (cx >= layer->ps1ChunkStride || cy >= layer->ps1ChunkRows) { // past the layer (the PC's padding: never drawn)
        g_ps1SetTileOutside = g_ps1SetTileOutside + 1;
        return;
    }
    uint16 *index = &layer->layout[cx + cy * layer->ps1ChunkStride];
    uint32 r = ty & 3, c = tx & 3;
    if (ps1RowPool[((uint32)ps1ChunkPool[((uint32)*index << 2) + r] << 2) + c] == tile)
        return;
    PS1TileSolidDrop((int32)(layer - tileLayers)); // its one-colour lines were computed from the file's layout
    if (*index < ps1ChunkShared) {
        // a chunk from the file may stand for other places too: this place gets its own copy
        if (ps1ChunkNext >= ps1ChunkCap) {
            g_ps1ChunkOverflow = g_ps1ChunkOverflow + 1;
            return;
        }
        memcpy(&ps1ChunkPool[ps1ChunkNext << 2], &ps1ChunkPool[(uint32)*index << 2], PS1_CHUNK_BYTES);
        *index = (uint16)ps1ChunkNext++;
        if (ps1ChunkNext - ps1ChunkShared > g_ps1ChunkPrivatePeak)
            g_ps1ChunkPrivatePeak = ps1ChunkNext - ps1ChunkShared;
    }
    // 7b.3: and a row from the file may stand for other chunks: this (private) chunk gets its own copy of it
    uint16 *row = &ps1ChunkPool[((uint32)*index << 2) + r];
    if (*row < ps1RowShared) {
        if (ps1RowNext >= ps1RowCap) {
            g_ps1ChunkOverflow = g_ps1ChunkOverflow + 1;
            return;
        }
        memcpy(&ps1RowPool[ps1RowNext << 2], &ps1RowPool[(uint32)*row << 2], PS1_ROW_BYTES);
        *row = (uint16)ps1RowNext++;
    }
    ps1RowPool[((uint32)*row << 2) + c] = tile;
}
#endif

void RSDK::LoadSceneFolder()
{
#if RETRO_PLATFORM == RETRO_ANDROID
    ShowLoadingIcon();
#endif

#if RETRO_USE_MOD_LOADER
    // run this before the game actually unloads all the objects & scene assets
    RunModCallbacks(MODCB_ONSTAGEUNLOAD, NULL);
#endif

    sceneInfo.timeCounter  = 0;
    sceneInfo.minutes      = 0;
    sceneInfo.seconds      = 0;
    sceneInfo.milliseconds = 0;

    // clear draw groups
    for (int32 i = 0; i < DRAWGROUP_COUNT; ++i) {
        drawGroups[i].entityCount = 0;
        drawGroups[i].layerCount  = 0;
    }

    // Clear Type groups
    for (int32 i = 0; i < TYPEGROUP_COUNT; ++i) {
        typeGroups[i].entryCount = 0;
    }

#if RETRO_PLATFORM == RETRO_PS1
    // PS1 (docs/32 2.3): the previous scene's entity arena is released before the STG compaction below (both paths).
    // Released only in LoadSceneAssets, it stayed a hole that the next compaction closed in the middle of the load, moving
    // the new arena from under objectEntityPtr (Title: TitleSonic_StageLoad's sprite load filled STG). The stock code
    // clears the entity list in LoadSceneAssets; nothing reads it between here and there.
    PS1EntityStorageClear();
    // 4.3 / 4.4: the previous scene's tile and atlas tables too: freed later by PS1TilesLoad / PS1AtlasLoad they left a
    // hole under the new entity arena (GHZ1 after GHZCutscene: 10 KB), and a forced compaction in the StageLoads then
    // moved the arena from under objectEntityPtr (a restart read a sprite file's bytes as entities)
    PS1TilesRelease();
    PS1AtlasRelease();
    // the layout chunks go with the layers (cleared below): the next compaction reclaims them
    ps1ChunkPool   = NULL;
    ps1ChunkShared = ps1ChunkNext = ps1ChunkCap = 0;
    ps1RowPool     = NULL;
    ps1RowShared = ps1RowNext = ps1RowCap = 0;
#endif

#if RETRO_REV02
    // Unload debug values
    ClearViewableVariables();

    // "unload" tint table
    tintLookupTable = NULL;
#endif

    // Unload TileLayers
    for (int32 l = 0; l < LAYER_COUNT; ++l) {
        MEM_ZERO(tileLayers[l]);
        for (int32 c = 0; c < CAMERA_COUNT; ++c) tileLayers[l].drawGroup[c] = -1;
    }

    SceneListInfo *list = &sceneInfo.listCategory[sceneInfo.activeCategory];
#if RETRO_REV02
#if RETRO_PLATFORM == RETRO_PS1
    // 7b: two acts of a folder with an overlay each: the stage's objects change, a full load (as another folder)
    if (strcmp(currentSceneFolder, sceneInfo.listData[sceneInfo.listPos].folder) == 0 && !forceHardReset
        && !PS1OverlayUnitChanges(sceneInfo.listData[sceneInfo.listPos].folder, sceneInfo.listData[sceneInfo.listPos].id)) {
#else
    if (strcmp(currentSceneFolder, sceneInfo.listData[sceneInfo.listPos].folder) == 0 && !forceHardReset) {
#endif
        // Reload
#if RETRO_PLATFORM == RETRO_PS1
        // PS1 (docs/32 4.3): a first load places the entity arena while the scene records sit in STG's tail and before
        // any StageLoad loads a sprite file; a reload kept the stage's sprite animations (GHZ1: 92 files, 113 KB) under
        // that peak and ran out of STG (restart, deaths). They are released as a hard reset releases them: StageLoad
        // loads them again after the arena, in the same order (the same IDs; object static variables stay, as stock)
        ClearSpriteAnimations();
#endif
        DefragmentAndGarbageCollectStorage(DATASET_STG);
        sceneInfo.filter = sceneInfo.listData[sceneInfo.listPos].filter;
        PrintLog(PRINT_NORMAL, "Reloading Scene \"%s - %s\" with filter %d", list->name, sceneInfo.listData[sceneInfo.listPos].name,
                 sceneInfo.listData[sceneInfo.listPos].filter);

#if RETRO_USE_MOD_LOADER
        // reload object hooks
        for (int32 h = 0; h < (int32)objectHookList.size(); ++h) {
            for (int32 i = 0; i < objectClassCount; ++i) {
                if (HASH_MATCH_MD5(objectClassList[i].hash, objectHookList[h].hash)) {
                    if (objectHookList[h].staticVars && objectClassList[i].staticVars)
                        *objectHookList[h].staticVars = *objectClassList[i].staticVars;
                    break;
                }
            }
        }
#endif

        return;
    }
#endif

#if !RETRO_REV02
    if (strcmp(currentSceneFolder, sceneInfo.listData[sceneInfo.listPos].folder) == 0) {
        // Reload
        DefragmentAndGarbageCollectStorage(DATASET_STG);
        PrintLog(PRINT_NORMAL, "Reloading Scene \"%s - %s\"", list->name, sceneInfo.listData[sceneInfo.listPos].name);

#if RETRO_USE_MOD_LOADER
        // reload object hooks
        for (int32 h = 0; h < (int32)objectHookList.size(); ++h) {
            for (int32 i = 0; i < objectClassCount; ++i) {
                if (HASH_MATCH_MD5(objectClassList[i].hash, objectHookList[h].hash)) {
                    if (objectHookList[h].staticVars && objectClassList[i].staticVars)
                        *objectHookList[h].staticVars = *objectClassList[i].staticVars;
                    break;
                }
            }
        }
#endif
        return;
    }
#endif

    // Unload stage 3DScenes & models
    Clear3DScenes();

    // Unload stage sprite animations
    ClearSpriteAnimations();

    // Unload stage surfaces
    ClearGfxSurfaces();

    // Unload stage sfx & audio channels
    ClearStageSfx();

    // Unload stage objects
    ClearStageObjects();

    // Clear draw groups
    for (int32 l = 0; l < DRAWGROUP_COUNT; ++l) {
        MEM_ZERO(drawGroups[l]);
        drawGroups[l].sorted = false;
    }

    // Clear stage storage
    DefragmentAndGarbageCollectStorage(DATASET_STG);
    DefragmentAndGarbageCollectStorage(DATASET_SFX);
#if RETRO_PLATFORM == RETRO_PS1
    // PS1 (docs/32 2.4): the stage folder's code overlay, at the top of STG (ps1/overlay.cpp)
    PS1LoadOverlay(sceneInfo.listData[sceneInfo.listPos].folder, sceneInfo.listData[sceneInfo.listPos].id);
#endif

    for (int32 s = 0; s < SCREEN_COUNT; ++s) {
        screens[s].position.x = 0;
        screens[s].position.y = 0;
    }

    SceneListEntry *sceneEntry = &sceneInfo.listData[sceneInfo.listPos];
    strcpy(currentSceneFolder, sceneEntry->folder);

#if RETRO_REV02
    forceHardReset   = false;
    sceneInfo.filter = sceneEntry->filter;
    PrintLog(PRINT_NORMAL, "Loading Scene \"%s - %s\" with filter %d", list->name, sceneEntry->name, sceneEntry->filter);
#endif

#if !RETRO_REV02
    PrintLog(PRINT_NORMAL, "Loading Scene \"%s - %s\"", list->name, sceneEntry->name);
#endif

    char fullFilePath[0x40];

    // Load TileConfig
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Stages/%s/TileConfig.bin", currentSceneFolder);
    LoadTileConfig(fullFilePath);

    // Load StageConfig
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Stages/%s/StageConfig.bin", currentSceneFolder);

    FileInfo info;
    InitFileInfo(&info);
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        uint32 sig = ReadInt32(&info, false);

        if (sig != RSDK_SIGNATURE_CFG) {
            CloseFile(&info);
            return;
        }

        sceneInfo.useGlobalObjects = ReadInt8(&info);
        sceneInfo.classCount       = 0;

        if (sceneInfo.useGlobalObjects) {
            for (int32 o = 0; o < globalObjectCount; ++o) stageObjectIDs[o] = globalObjectIDs[o];
            sceneInfo.classCount = globalObjectCount;
        }
        else {
            for (int32 o = 0; o < TYPE_DEFAULT_COUNT; ++o) stageObjectIDs[o] = globalObjectIDs[o];

            sceneInfo.classCount = TYPE_DEFAULT_COUNT;
        }

        uint8 objectCount = ReadInt8(&info);
#if RETRO_PLATFORM == RETRO_PS1
        g_ps1StageObjectsMissing = 0;
        g_ps1StageObjectsLeftOut = 0;
#endif
        for (int32 o = 0; o < objectCount; ++o) {
            ReadString(&info, textBuffer);

            RETRO_HASH_MD5(hash);
            GEN_HASH_MD5_BUFFER(textBuffer, hash);

            stageObjectIDs[sceneInfo.classCount] = 0;
#if RETRO_PLATFORM == RETRO_PS1
            int32 before = sceneInfo.classCount;
#endif
            for (int32 id = 0; id < objectClassCount; ++id) {
                if (HASH_MATCH_MD5(hash, objectClassList[id].hash)) {
                    stageObjectIDs[sceneInfo.classCount] = id;
                    sceneInfo.classCount++;
                }
            }
#if RETRO_PLATFORM == RETRO_PS1
            // a StageConfig object this build does not register (`make objects STAGES=` without this stage): its
            // entities load as blank ones, the scene half-works (2.5: GHZ1 without its Bridge)
            if (sceneInfo.classCount == before) {
                if (PS1OverlayLeftOut(textBuffer)) // 7b: not this act's (its scene has none, no kept code names it)
                    g_ps1StageObjectsLeftOut = g_ps1StageObjectsLeftOut + 1;
                else
                    g_ps1StageObjectsMissing = g_ps1StageObjectsMissing + 1;
            }
#endif
        }

        for (int32 o = 0; o < sceneInfo.classCount; ++o) {
            ObjectClass *objClass = &objectClassList[stageObjectIDs[o]];
            if (objClass->staticVars && !*objClass->staticVars) {
                AllocateStorage((void **)objClass->staticVars, objClass->staticClassSize, DATASET_STG, true);

#if RETRO_REV0U
                if (objClass->staticLoad)
                    objClass->staticLoad(*objClass->staticVars);
                else
                    LoadStaticVariables((uint8 *)*objClass->staticVars, objClass->hash, sizeof(Object));

#else
                LoadStaticVariables((uint8 *)*objClass->staticVars, objClass->hash, sizeof(Object));
#endif

#if RETRO_USE_MOD_LOADER
                // even though the static load event is rev0U only, this point in the engine is "static loading"
                RunModCallbacks(MODCB_ONSTATICLOAD, (void *)objClass);
#endif

#if RETRO_USE_MOD_LOADER
                for (ModInfo &mod : modList) {
                    if (mod.staticVars.find(objClass->hash) != mod.staticVars.end()) {
                        auto sVars = mod.staticVars.at(objClass->hash);
                        RegisterStaticVariables((void **)sVars.staticVars, sVars.name.c_str(), sVars.size);
                    }
                }
#endif

                (*objClass->staticVars)->classID = o;
                if (o >= TYPE_DEFAULT_COUNT)
                    (*objClass->staticVars)->active = ACTIVE_NORMAL;
            }
        }

        for (int32 p = 0; p < PALETTE_BANK_COUNT; ++p) {
            activeStageRows[p] = ReadInt16(&info);

            for (int32 r = 0; r < 0x10; ++r) {
                if ((activeStageRows[p] >> r & 1)) {
                    for (int32 c = 0; c < 0x10; ++c) {
                        uint8 red                     = ReadInt8(&info);
                        uint8 green                   = ReadInt8(&info);
                        uint8 blue                    = ReadInt8(&info);
                        stagePalette[p][(r << 4) + c] = rgb32To16_B[blue] | rgb32To16_G[green] | rgb32To16_R[red];
                    }
                }
                else {
                    for (int32 c = 0; c < 0x10; ++c) stagePalette[p][(r << 4) + c] = 0;
                }
            }
        }

        uint8 sfxCount = ReadInt8(&info);
        char sfxPath[0x100];
        for (int32 i = 0; i < sfxCount; ++i) {
            ReadString(&info, sfxPath);
            uint8 maxConcurrentPlays = ReadInt8(&info);
            LoadSfx(sfxPath, maxConcurrentPlays, SCOPE_STAGE);
        }

        CloseFile(&info);
    }

    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Stages/%s/16x16Tiles.gif", currentSceneFolder);
    LoadStageGIF(fullFilePath);

#if RETRO_USE_MOD_LOADER
    for (int32 h = 0; h < (int32)objectHookList.size(); ++h) {
        for (int32 i = 0; i < objectClassCount; ++i) {
            if (HASH_MATCH_MD5(objectClassList[i].hash, objectHookList[h].hash)) {
                if (objectHookList[h].staticVars && objectClassList[i].staticVars)
                    *objectHookList[h].staticVars = *objectClassList[i].staticVars;
                break;
            }
        }
    }
#endif
}
void RSDK::LoadSceneAssets()
{
#if RETRO_PLATFORM == RETRO_ANDROID
    ShowLoadingIcon();
#endif

#if RETRO_PLATFORM == RETRO_PS1
    PS1EntityStorageClear(); // every slot a ghost until the scene's arena is placed (PS1Entities.cpp)
#else
    memset(objectEntityList, 0, ENTITY_COUNT * sizeof(EntityBase));
#endif

    SceneListEntry *sceneEntry = &sceneInfo.listData[sceneInfo.listPos];
    char fullFilePath[0x40];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Stages/%s/Scene%s.bin", currentSceneFolder, sceneEntry->id);

    dataStorage[DATASET_TMP].usedStorage = 0;
#if RETRO_PLATFORM == RETRO_PS1
    PS1TilesLoad(currentSceneFolder, sceneEntry->id); // tilesetPixels' job: the tile images go to VRAM (docs/32 3.1a)
    PS1AtlasLoad(currentSceneFolder, sceneEntry->id); // the sheets' job: the sprite atlas's default state (3.1c)
    PS1SfxLoadBank(currentSceneFolder, sceneEntry->id); // 5.1: the scene's SFX samples into SPU RAM (TMP staging)
#endif

    for (int32 s = 0; s < SCREEN_COUNT; ++s) screens[s].waterDrawPos = screens[s].size.y;

    if (screens[0].size.y > 0)
        memset(gfxLineBuffer, 0, screens[0].size.y * sizeof(uint8));
#if RETRO_PLATFORM == RETRO_PS1
    ++ps1LineBankGen;
#endif

    memset(tileLayers, 0, LAYER_COUNT * sizeof(TileLayer));

    // Reload palette
    for (int32 b = 0; b < 8; ++b) {
        for (int32 r = 0; r < 0x10; ++r) {
            if ((activeGlobalRows[b] >> r & 1)) {
                for (int32 c = 0; c < 0x10; ++c) fullPalette[b][(r << 4) + c] = globalPalette[b][(r << 4) + c];
            }

            if ((activeStageRows[b] >> r & 1)) {
                for (int32 c = 0; c < 0x10; ++c) fullPalette[b][(r << 4) + c] = stagePalette[b][(r << 4) + c];
            }
        }
    }

    FileInfo info;
    InitFileInfo(&info);
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        uint32 sig = ReadInt32(&info, false);

        if (sig != RSDK_SIGNATURE_SCN) {
            CloseFile(&info);
            return;
        }

        // Editor Metadata

        // I'm leaving this section here so that the "format" can be documented, since the official code is 3 lines and just skips it lol

        /*
        uint8 unknown1 = ReadInt8(&info); // usually 3, sometimes 4, LRZ1 (old) is 2

        uint8 b                = ReadInt8(&info);
        uint8 g                = ReadInt8(&info);
        uint8 r                = ReadInt8(&info);
        uint8 a                = ReadInt8(&info);
        color backgroundColor1 = (a << 24) | (r << 16) | (g << 8) | (b << 0);

        b                      = ReadInt8(&info);
        g                      = ReadInt8(&info);
        r                      = ReadInt8(&info);
        a                      = ReadInt8(&info);
        color backgroundColor2 = (a << 24) | (r << 16) | (g << 8) | (b << 0);

        uint8 unknown2 = ReadInt8(&info); // always 1 afaik
        uint8 unknown3 = ReadInt8(&info); // always 1 afaik
        uint8 unknown4 = ReadInt8(&info); // always 4 afaik
        uint8 unknown5 = ReadInt8(&info); // always 0 afaik
        uint8 unknown6 = ReadInt8(&info); // always 1 afaik
        uint8 unknown7 = ReadInt8(&info); // always 4 afaik
        uint8 unknown8 = ReadInt8(&info); // always 0 afaik

        char stampName[0x20];
        ReadString(&info, stampName);

        uint8 unknown9 = ReadInt8(&info); // usually 3, 4, or 5
        */

        // Skip over Metadata, since we won't be using it at all in-game
        Seek_Cur(&info, 0x10);
        uint8 strLen = ReadInt8(&info);
        Seek_Cur(&info, strLen + 1);

#if RETRO_PLATFORM == RETRO_PS1
        // PS1 (docs/32 7b): the disc's scene files hold the objects before the layers (tools/data_disc.py): the entities
        // are placed while STG holds no layout yet (SPZ1: 250 KB of chunk pool + layouts beside the records and the
        // arena did not fit). The layers load after them, here as in the file.
        auto ps1Layers = [&]() {
#endif
        // Tile Layers
        uint8 layerCount = ReadInt8(&info);
        for (int32 l = 0; l < layerCount; ++l) {
            TileLayer *layer = &tileLayers[l];

            // Tests in RetroED & comparing images of the RSDKv5 editor we have puts this as the most likely use for this (otherwise unused) variable
            bool32 visibleInEditor = ReadInt8(&info) != 0;
            (void)visibleInEditor; // unused

            ReadString(&info, textBuffer);
            GEN_HASH_MD5_BUFFER(textBuffer, layer->name);

            layer->type         = ReadInt8(&info);
            layer->drawGroup[0] = ReadInt8(&info);
            for (int32 s = 1; s < CAMERA_COUNT; ++s) layer->drawGroup[s] = layer->drawGroup[0];

            layer->xsize = ReadInt16(&info);
            int32 shift  = 1;
            int32 shift2 = 1;
            int32 val    = 0;
            do {
                shift = shift2;
                val   = 1 << shift2++;
            } while (val < layer->xsize);
            layer->widthShift = shift;

            layer->ysize = ReadInt16(&info);
            shift        = 1;
            shift2       = 1;
            val          = 0;
            do {
                shift = shift2;
                val   = 1 << shift2++;
            } while (val < layer->ysize);
            layer->heightShift = shift;

            layer->parallaxFactor = ReadInt16(&info);
            layer->scrollSpeed    = ReadInt16(&info) << 8;
            layer->scrollPos      = 0;

            layer->layout = NULL;
#if RETRO_PLATFORM != RETRO_PS1
            if (layer->xsize || layer->ysize) {
                AllocateStorage((void **)&layer->layout, sizeof(uint16) * (1UL << layer->widthShift) * (1UL << layer->heightShift), DATASET_STG, true);
                memset(layer->layout, 0xFF, sizeof(uint16) * (1UL << layer->widthShift) * (1UL << layer->heightShift));
            }
#endif

            int32 size = layer->xsize;
            if (size <= layer->ysize)
                size = layer->ysize;
#if RETRO_PLATFORM == RETRO_PS1
            // PS1 (docs/32 2.3g): ProcessParallax indexes lineScroll by pixel row for H-scroll layers and by pixel column for
            // V-scroll ones; the stock buffer is sized for the larger side (FG: 16 KB of which 2 KB are read)
            int32 lineScrollSize = TILE_SIZE * (layer->type == LAYER_HSCROLL ? layer->ysize : layer->type == LAYER_VSCROLL ? layer->xsize : size);
            AllocateStorage((void **)&layer->lineScroll, lineScrollSize, DATASET_STG, true);
#else
            AllocateStorage((void **)&layer->lineScroll, TILE_SIZE * size, DATASET_STG, true);
#endif

            layer->scrollInfoCount = ReadInt16(&info);
#if RETRO_PLATFORM == RETRO_PS1
            // scrollInfo holds 0x80 on the PS1 (Scene.hpp): entries past it are read over and counted
            int32 ps1ScrollExtra = layer->scrollInfoCount - (int32)(sizeof(layer->scrollInfo) / sizeof(ScrollInfo));
            if (ps1ScrollExtra > 0) {
                g_ps1ScrollInfoDropped = g_ps1ScrollInfoDropped + ps1ScrollExtra;
                layer->scrollInfoCount = sizeof(layer->scrollInfo) / sizeof(ScrollInfo);
            }
#endif
            for (int32 s = 0; s < layer->scrollInfoCount; ++s) {
                layer->scrollInfo[s].parallaxFactor = ReadInt16(&info);
                layer->scrollInfo[s].scrollSpeed    = ReadInt16(&info) << 8;
                layer->scrollInfo[s].scrollPos      = 0;
                layer->scrollInfo[s].tilePos        = 0;
                layer->scrollInfo[s].deform         = ReadInt8(&info);

                // this isn't used anywhere in-engine, and is never set in the files. so as you might expect, no one knows what it is for!
                layer->scrollInfo[s].unknown = ReadInt8(&info);
            }
#if RETRO_PLATFORM == RETRO_PS1
            if (ps1ScrollExtra > 0)
                Seek_Cur(&info, 6 * ps1ScrollExtra); // int16 parallax, int16 speed, u8 deform, u8 unknown
#endif

            uint8 *scrollIndexes = NULL;
            ReadCompressed(&info, (uint8 **)&scrollIndexes);
#if RETRO_PLATFORM == RETRO_PS1
            memcpy(layer->lineScroll, scrollIndexes, lineScrollSize);
            layer->ps1LineScrollOne = lineScrollSize ? scrollIndexes[0] : 0xFF; // 3.3c: one scroll info for every line?
            for (int32 i = 1; i < lineScrollSize && layer->ps1LineScrollOne != 0xFF; ++i)
                if (scrollIndexes[i] != layer->ps1LineScrollOne)
                    layer->ps1LineScrollOne = 0xFF;
            layer->ps1Uniform = 0;
            layer->ps1Direct  = 0;
            if (layer->ps1LineScrollOne != 0xFF) // 7b: one value for every line: the table goes (STG)
                RemoveStorageEntry((void **)&layer->lineScroll);
#else
            memcpy(layer->lineScroll, scrollIndexes, TILE_SIZE * size * sizeof(uint8));
#endif
#if !RETRO_USE_ORIGINAL_CODE
            RemoveStorageEntry((void **)&scrollIndexes);
#endif
            scrollIndexes = NULL;

#if RETRO_PLATFORM == RETRO_PS1
            // the chunk index, read straight into its STG block (no TMP copy of the layout)
            layer->ps1ChunkStride = (uint16)((layer->xsize + 3) >> 2); // 7b: the layer's own size (tools/data_disc.py)
            layer->ps1ChunkRows   = (uint16)((layer->ysize + 3) >> 2);
            PS1ReadRawBlock(&info, (void **)&layer->layout, DATASET_STG);
            continue;
#endif
            uint8 *tileLayout = NULL;
            ReadCompressed(&info, (uint8 **)&tileLayout);

            int32 id = 0;
            for (int32 y = 0; y < layer->ysize; ++y) {
                for (int32 x = 0; x < layer->xsize; ++x) {
                    layer->layout[x + (y << layer->widthShift)] = (tileLayout[id + 1] << 8) + tileLayout[id + 0];
                    id += 2;
                }
            }

#if !RETRO_USE_ORIGINAL_CODE
            RemoveStorageEntry((void **)&tileLayout);
#endif
            tileLayout = NULL;
        }

#if RETRO_PLATFORM == RETRO_PS1
        PS1ReadChunkPool(&info);
        };
#endif

        // Objects
        uint8 objectCount = ReadInt8(&info);
        editableVarList   = NULL;
        AllocateStorage((void **)&editableVarList, sizeof(EditableVarInfo) * EDITABLEVAR_COUNT, DATASET_TMP, false);

#if RETRO_PLATFORM == RETRO_PS1
        // PS1 (docs/32 2.3): the entities go to compact records first (file slot, class, position, the active
        // variables), then PS1SceneEntitiesPlace compacts them as below and gives each slot a block of its class's size.
        PS1SceneEntitiesBegin();
        // 7b: the "childCount" variables (Platform & co. move or read the next childCount slots: they never go dormant)
        RETRO_HASH_MD5(ps1ChildHash);
        GEN_HASH_MD5("childCount", ps1ChildHash);

        for (int32 i = 0; i < objectCount; ++i) {
            RETRO_HASH_MD5(objHash);
            objHash[0] = ReadInt32(&info, false);
            objHash[1] = ReadInt32(&info, false);
            objHash[2] = ReadInt32(&info, false);
            objHash[3] = ReadInt32(&info, false);

            int32 classID = 0;
            for (int32 o = 0; o < sceneInfo.classCount; ++o) {
                if (HASH_MATCH_MD5(objHash, objectClassList[stageObjectIDs[o]].hash)) {
                    classID = o;
                    break;
                }
            }

#if !RETRO_USE_ORIGINAL_CODE
            if (!classID && i >= TYPE_DEFAULT_COUNT)
                PrintLog(PRINT_NORMAL, "Object Class %d is unimplemented!", i);
#endif

            ObjectClass *objectClass = &objectClassList[stageObjectIDs[classID]];

            uint8 varCount           = ReadInt8(&info);
            EditableVarInfo *varList = NULL;
            AllocateStorage((void **)&varList, sizeof(EditableVarInfo) * varCount, DATASET_TMP, false);
            editableVarCount = 0;
            if (classID) {
#if RETRO_REV02
                SetEditableVar(VAR_UINT8, "filter", classID, offsetof(Entity, filter));
#endif

#if RETRO_USE_MOD_LOADER
                currentObjectID = classID;
#endif

                if (objectClass->serialize)
                    objectClass->serialize();
            }

            for (int32 e = 1; e < varCount; ++e) {
                RETRO_HASH_MD5(varHash);
                varHash[0] = ReadInt32(&info, false);
                varHash[1] = ReadInt32(&info, false);
                varHash[2] = ReadInt32(&info, false);
                varHash[3] = ReadInt32(&info, false);

                int32 varID = 0;
                MEM_ZERO(varList[e]);
                for (int32 v = 0; v < editableVarCount; ++v) {
                    if (HASH_MATCH_MD5(varHash, editableVarList[v].hash)) {
                        varID = v;
                        HASH_COPY_MD5(varList[e].hash, editableVarList[v].hash);
                        varList[e].offset = editableVarList[v].offset;
                        varList[e].active = true;
                        break;
                    }
                }

                editableVarList[varID].type = varList[e].type = ReadInt8(&info);
                if (varList[e].active && HASH_MATCH_MD5(varHash, ps1ChildHash))
                    PS1SceneChildVar((uint16)classID, varList[e].offset, varList[e].type);
            }

            uint16 entityCount = ReadInt16(&info);
            {
                // this object's records, exactly: a 16-byte header per entity + each active variable's raw bytes (a
                // string: its count + at most 0x100 characters), 4-byte aligned; its layout (PS1SceneObjectVar) first
                uint32 perEntity = 16, vars = 0;
                for (int32 v = 1; v < varCount; ++v) {
                    if (!varList[v].active)
                        continue;
                    uint32 len = 0;
                    switch (varList[v].type) {
                        case VAR_UINT8:
                        case VAR_INT8: len = 1; break;
                        case VAR_UINT16:
                        case VAR_INT16: len = 2; break;
                        case VAR_VECTOR2: len = 8; break;
                        case VAR_STRING: len = 2 + 0x100 * sizeof(uint16); break;
                        default: len = 4; break;
                    }
                    perEntity += len;
                    ++vars;
                }
                PS1SceneObjectBegin(entityCount * ((perEntity + 3) & ~3u), (uint16)classID, (uint16)vars);
                for (int32 v = 1; v < varCount; ++v) {
                    if (!varList[v].active)
                        continue;
                    uint32 len = 4;
                    switch (varList[v].type) {
                        case VAR_UINT8:
                        case VAR_INT8: len = 1; break;
                        case VAR_UINT16:
                        case VAR_INT16: len = 2; break;
                        case VAR_VECTOR2: len = 8; break;
                        case VAR_STRING: len = 0; break;
                        default: break;
                    }
                    PS1SceneObjectVar(varList[v].offset, varList[v].type, (uint16)len);
                }
            }
            for (int32 e = 0; e < entityCount; ++e) {
                uint16 slotID = ReadInt16(&info);
                int32 posX    = ReadInt32(&info, false);
                int32 posY    = ReadInt32(&info, false);
                PS1SceneEntityBegin(slotID, classID, posX, posY);

                uint8 tempBuffer[0x10];
                for (int32 v = 1; v < varCount; ++v) {
                    uint16 len = 0;
                    switch (varList[v].type) {
                        case VAR_UINT8:
                        case VAR_INT8: len = sizeof(int8); break;
                        case VAR_UINT16:
                        case VAR_INT16: len = sizeof(int16); break;
                        case VAR_UINT32:
                        case VAR_INT32:
                        case VAR_ENUM:
                        case VAR_BOOL:
                        case VAR_FLOAT:
                        case VAR_COLOR: len = sizeof(int32); break;
                        case VAR_VECTOR2: len = 2 * sizeof(int32); break;
                        case VAR_STRING: {
                            uint16 chars = ReadInt16(&info);
                            if (varList[v].active) {
                                uint16 buf[0x100];
                                uint16 n = chars < 0x100 ? chars : 0x100;
                                ReadBytes(&info, buf, n * sizeof(uint16));
                                if (chars > n)
                                    Seek_Cur(&info, (chars - n) * sizeof(uint16));
                                PS1SceneEntityVar(varList[v].offset, VAR_STRING, buf, n * sizeof(uint16));
                            }
                            else {
                                Seek_Cur(&info, chars * sizeof(uint16));
                            }
                            continue;
                        }
                    }
                    // The stock loader copies each value's bytes into the entity (little-endian as the PS1): kept as
                    // raw bytes in the record.
                    ReadBytes(&info, tempBuffer, len);
                    if (varList[v].active)
                        PS1SceneEntityVar(varList[v].offset, varList[v].type, tempBuffer, len);
                }
            }

#if !RETRO_USE_ORIGINAL_CODE
            RemoveStorageEntry((void **)&varList);
            varList = NULL;
#endif
        }

        PS1SceneEntitiesPlace();
        ps1Layers(); // 7b: the layers after the entities (the file's order on the PS1)
        PS1AtlasLoadBindings(); // the tail is free again: the frame bindings for the StageLoads to come (3.1c)
#else
#if RETRO_REV02
        EntityBase *tempEntityList = NULL;
        AllocateStorage((void **)&tempEntityList, SCENEENTITY_COUNT * sizeof(EntityBase), DATASET_TMP, true);
#endif

        for (int32 i = 0; i < objectCount; ++i) {
            RETRO_HASH_MD5(objHash);
            objHash[0] = ReadInt32(&info, false);
            objHash[1] = ReadInt32(&info, false);
            objHash[2] = ReadInt32(&info, false);
            objHash[3] = ReadInt32(&info, false);

            int32 classID = 0;
            for (int32 o = 0; o < sceneInfo.classCount; ++o) {
                if (HASH_MATCH_MD5(objHash, objectClassList[stageObjectIDs[o]].hash)) {
                    classID = o;
                    break;
                }
            }

#if !RETRO_USE_ORIGINAL_CODE
            if (!classID && i >= TYPE_DEFAULT_COUNT)
                PrintLog(PRINT_NORMAL, "Object Class %d is unimplemented!", i);
#endif

            ObjectClass *objectClass = &objectClassList[stageObjectIDs[classID]];

            uint8 varCount           = ReadInt8(&info);
            EditableVarInfo *varList = NULL;
            AllocateStorage((void **)&varList, sizeof(EditableVarInfo) * varCount, DATASET_TMP, false);
            editableVarCount = 0;
            if (classID) {
#if RETRO_REV02
                SetEditableVar(VAR_UINT8, "filter", classID, offsetof(Entity, filter));
#endif

#if RETRO_USE_MOD_LOADER
                currentObjectID = classID;
#endif

                if (objectClass->serialize)
                    objectClass->serialize();
            }

            for (int32 e = 1; e < varCount; ++e) {
                RETRO_HASH_MD5(varHash);
                varHash[0] = ReadInt32(&info, false);
                varHash[1] = ReadInt32(&info, false);
                varHash[2] = ReadInt32(&info, false);
                varHash[3] = ReadInt32(&info, false);

                int32 varID = 0;
                MEM_ZERO(varList[e]);
                for (int32 v = 0; v < editableVarCount; ++v) {
                    if (HASH_MATCH_MD5(varHash, editableVarList[v].hash)) {
                        varID = v;
                        HASH_COPY_MD5(varList[e].hash, editableVarList[v].hash);
                        varList[e].offset = editableVarList[v].offset;
                        varList[e].active = true;
                        break;
                    }
                }

                editableVarList[varID].type = varList[e].type = ReadInt8(&info);
            }

            uint16 entityCount = ReadInt16(&info);
            for (int32 e = 0; e < entityCount; ++e) {
                uint16 slotID = ReadInt16(&info);

                EntityBase *entity = NULL;

#if RETRO_REV02
                if (slotID < SCENEENTITY_COUNT)
                    entity = &objectEntityList[slotID + RESERVE_ENTITY_COUNT];
                else
                    entity = &tempEntityList[slotID - SCENEENTITY_COUNT];
#else
                entity = &objectEntityList[slotID + RESERVE_ENTITY_COUNT];
#endif

                entity->classID = classID;
#if RETRO_REV02
                entity->filter = 0xFF;
#endif
                entity->position.x = ReadInt32(&info, false);
                entity->position.y = ReadInt32(&info, false);

                uint8 *entityBuffer = (uint8 *)entity;

                uint8 tempBuffer[0x10];
                for (int32 v = 1; v < varCount; ++v) {
                    switch (varList[v].type) {
                        case VAR_UINT8:
                        case VAR_INT8:
                            if (varList[v].active)
                                ReadBytes(&info, &entityBuffer[varList[v].offset], sizeof(int8));
                            else
                                ReadBytes(&info, tempBuffer, sizeof(int8));
                            break;

                        case VAR_UINT16:
                        case VAR_INT16:
                            if (varList[v].active)
#if !RETRO_USE_ORIGINAL_CODE
                                *(int16 *)&entityBuffer[varList[v].offset] = ReadInt16(&info);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &entityBuffer[varList[v].offset], sizeof(int16));
#endif
                            else
                                ReadBytes(&info, tempBuffer, sizeof(int16));
                            break;

                        case VAR_UINT32:
                        case VAR_INT32:
                            if (varList[v].active)
#if !RETRO_USE_ORIGINAL_CODE
                                *(int32 *)&entityBuffer[varList[v].offset] = ReadInt32(&info, false);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &entityBuffer[varList[v].offset], sizeof(int32));
#endif
                            else
                                ReadBytes(&info, tempBuffer, sizeof(int32));
                            break;

                        // not entirely sure on specifics here, should always be sizeof(int32) but it having a unique type implies it isn't always
                        case VAR_ENUM:
                            if (varList[v].active)
#if !RETRO_USE_ORIGINAL_CODE
                                *(int32 *)&entityBuffer[varList[v].offset] = ReadInt32(&info, false);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &entityBuffer[varList[v].offset], sizeof(int32));
#endif
                            else
                                ReadBytes(&info, tempBuffer, sizeof(int32));
                            break;

                        case VAR_BOOL:
                            if (varList[v].active)
#if !RETRO_USE_ORIGINAL_CODE
                                *(bool32 *)&entityBuffer[varList[v].offset] = (bool32)ReadInt32(&info, false);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &entityBuffer[varList[v].offset], sizeof(bool32));
#endif
                            else
                                ReadBytes(&info, tempBuffer, sizeof(bool32));
                            break;

                        case VAR_STRING:
                            if (varList[v].active) {
                                String *string = (String *)&entityBuffer[varList[v].offset];
                                uint16 len     = ReadInt16(&info);

                                InitString(string, "", len);
                                for (string->length = 0; string->length < len; ++string->length) string->chars[string->length] = ReadInt16(&info);
                            }
                            else {
                                Seek_Cur(&info, ReadInt16(&info) * sizeof(uint16));
                            }
                            break;

                        case VAR_VECTOR2:
                            if (varList[v].active) {
#if !RETRO_USE_ORIGINAL_CODE
                                *(int32 *)&entityBuffer[varList[v].offset]                 = ReadInt32(&info, false);
                                *(int32 *)&entityBuffer[varList[v].offset + sizeof(int32)] = ReadInt32(&info, false);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &entityBuffer[varList[v].offset], sizeof(int32));
                                ReadBytes(&info, &entityBuffer[varList[v].offset + sizeof(int32)], sizeof(int32));
#endif
                            }
                            else {
                                ReadBytes(&info, tempBuffer, sizeof(int32)); // x
                                ReadBytes(&info, tempBuffer, sizeof(int32)); // y
                            }
                            break;

                        // Never used in mania so we don't know for sure, but it's our best guess!
                        case VAR_FLOAT:
                            if (varList[v].active)
#if !RETRO_USE_ORIGINAL_CODE
                                *(float *)&entityBuffer[varList[v].offset] = ReadSingle(&info);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &entityBuffer[varList[v].offset], sizeof(float));
#endif
                            else
                                ReadBytes(&info, tempBuffer, sizeof(float));
                            break;

                        case VAR_COLOR:
                            if (varList[v].active)
#if !RETRO_USE_ORIGINAL_CODE
                                *(color *)&entityBuffer[varList[v].offset] = ReadInt32(&info, false);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &entityBuffer[varList[v].offset], sizeof(color));
#endif
                            else
                                ReadBytes(&info, tempBuffer, sizeof(color));
                            break;
                    }
                }
            }

#if !RETRO_USE_ORIGINAL_CODE
            RemoveStorageEntry((void **)&varList);
            varList = NULL;
#endif
        }

#if RETRO_REV02
        // handle filter and stuff
        EntityBase *entity = &objectEntityList[RESERVE_ENTITY_COUNT];
        int32 activeSlot   = RESERVE_ENTITY_COUNT;
        for (int32 i = RESERVE_ENTITY_COUNT; i < SCENEENTITY_COUNT + RESERVE_ENTITY_COUNT; ++i) {
            if (sceneInfo.filter & entity->filter) {
                if (i != activeSlot) {
                    memcpy(&objectEntityList[activeSlot], entity, sizeof(EntityBase));
                    memset(entity, 0, sizeof(EntityBase));
                }

                ++activeSlot;
            }
            else {
                memset(entity, 0, sizeof(EntityBase));
            }

            entity++;
        }

        for (int32 i = 0; i < SCENEENTITY_COUNT; ++i) {
            if (sceneInfo.filter & tempEntityList[i].filter)
                memcpy(&objectEntityList[activeSlot++], &tempEntityList[i], sizeof(EntityBase));

            if (activeSlot >= SCENEENTITY_COUNT + RESERVE_ENTITY_COUNT)
                break;
        }

#if !RETRO_USE_ORIGINAL_CODE
        RemoveStorageEntry((void **)&tempEntityList);
#endif
        tempEntityList = NULL;
#endif
#endif // RETRO_PLATFORM == RETRO_PS1

#if !RETRO_USE_ORIGINAL_CODE
        RemoveStorageEntry((void **)&editableVarList);
#endif
        editableVarList = NULL;

        CloseFile(&info);
    }
#if RETRO_USE_MOD_LOADER
    LoadGameXML(true); // override the stage palette *somewhere* idfk
#endif
}
void RSDK::LoadTileConfig(char *filepath)
{
    FileInfo info;
    InitFileInfo(&info);

    if (LoadFile(&info, filepath, FMODE_RB)) {
        uint32 sig = ReadInt32(&info, false);
        if (sig != RSDK_SIGNATURE_TIL) {
            CloseFile(&info);
            return;
        }

#if RETRO_PLATFORM == RETRO_PS1
        // PS1 (docs/32 2.3): the block is stored raw on the disc (tools/data_disc.py), so the 2 x 1024 tile records (38 B
        // each, 77,824 B) are read one at a time instead of into a DATASET_TMP buffer.
        uint32 cSize  = ReadInt32(&info, false) - 4;
        uint32 sizeBE = ReadInt32(&info, false);
        uint32 sizeLE = (sizeBE << 24) | ((sizeBE << 8) & 0x00FF0000) | ((sizeBE >> 8) & 0x0000FF00) | (sizeBE >> 24);
        if (cSize != sizeLE || sizeLE != CPATH_COUNT * TILE_COUNT * 38) {
            g_ps1CompressedMismatch = g_ps1CompressedMismatch + 1;
            CloseFile(&info);
            return;
        }
        uint8 tileRecord[38];
        uint8 *buffer = tileRecord;
#else
        uint8 *buffer = NULL;
        ReadCompressed(&info, &buffer);
#endif

#if RETRO_PLATFORM == RETRO_PS1
        // PS1 (docs/32 2.3o): each tile is computed into ps1Mask / ps1Info, then stored as plane A, or for plane B only
        // where it differs from plane A (PS1/PS1Collision.hpp); the flipped variants are derived where they are read.
        CollisionMask ps1Mask;
        TileInfo ps1Info;
        memset(ps1PlaneB, 0xFF, sizeof(ps1PlaneB));
        ps1PlaneBCount = 0;
#define PS1_TILE_MASK ps1Mask
#define PS1_TILE_INFO ps1Info
#else
#define PS1_TILE_MASK collisionMasks[p][t]
#define PS1_TILE_INFO tileInfo[p][t]
#endif
        int32 bufPos = 0;
        for (int32 p = 0; p < CPATH_COUNT; ++p) {
            // No Flip/Stored in file
            for (int32 t = 0; t < TILE_COUNT; ++t) {
#if RETRO_PLATFORM == RETRO_PS1
                ReadBytes(&info, tileRecord, sizeof(tileRecord));
                bufPos = 0;
                memset(&ps1Mask, 0, sizeof(ps1Mask));
#endif
                uint8 maskHeights[0x10];
                uint8 maskActive[0x10];

                memcpy(maskHeights, buffer + bufPos, TILE_SIZE * sizeof(uint8));
                bufPos += TILE_SIZE;
                memcpy(maskActive, buffer + bufPos, TILE_SIZE * sizeof(uint8));
                bufPos += TILE_SIZE;

                bool32 yFlip              = buffer[bufPos++];
                PS1_TILE_INFO.floorAngle = buffer[bufPos++];
                PS1_TILE_INFO.lWallAngle = buffer[bufPos++];
                PS1_TILE_INFO.rWallAngle = buffer[bufPos++];
                PS1_TILE_INFO.roofAngle  = buffer[bufPos++];
                PS1_TILE_INFO.flag       = buffer[bufPos++];

                if (yFlip) {
                    for (int32 c = 0; c < TILE_SIZE; c++) {
                        if (maskActive[c]) {
                            PS1_TILE_MASK.floorMasks[c] = 0x00;
                            PS1_TILE_MASK.roofMasks[c]  = maskHeights[c];
                        }
                        else {
                            PS1_TILE_MASK.floorMasks[c] = 0xFF;
                            PS1_TILE_MASK.roofMasks[c]  = 0xFF;
                        }
                    }

                    // LWall rotations
                    for (int32 c = 0; c < TILE_SIZE; ++c) {
                        int32 h = 0;
                        while (true) {
                            if (h == TILE_SIZE) {
                                PS1_TILE_MASK.lWallMasks[c] = 0xFF;
                                break;
                            }

                            uint8 m = PS1_TILE_MASK.roofMasks[h];
                            if (m != 0xFF && c <= m) {
                                PS1_TILE_MASK.lWallMasks[c] = h;
                                break;
                            }
                            else {
                                ++h;
                                if (h <= -1)
                                    break;
                            }
                        }
                    }

                    // RWall rotations
                    for (int32 c = 0; c < TILE_SIZE; ++c) {
                        int32 h = TILE_SIZE - 1;
                        while (true) {
                            if (h == -1) {
                                PS1_TILE_MASK.rWallMasks[c] = 0xFF;
                                break;
                            }

                            uint8 m = PS1_TILE_MASK.roofMasks[h];
                            if (m != 0xFF && c <= m) {
                                PS1_TILE_MASK.rWallMasks[c] = h;
                                break;
                            }
                            else {
                                --h;
                                if (h >= TILE_SIZE)
                                    break;
                            }
                        }
                    }
                }
                else // Regular Tile
                {
                    // Collision heights
                    for (int32 c = 0; c < TILE_SIZE; ++c) {
                        if (maskActive[c]) {
                            PS1_TILE_MASK.floorMasks[c] = maskHeights[c];
                            PS1_TILE_MASK.roofMasks[c]  = 0x0F;
                        }
                        else {
                            PS1_TILE_MASK.floorMasks[c] = 0xFF;
                            PS1_TILE_MASK.roofMasks[c]  = 0xFF;
                        }
                    }

                    // LWall rotations
                    for (int32 c = 0; c < TILE_SIZE; ++c) {
                        int32 h = 0;
                        while (true) {
                            if (h == TILE_SIZE) {
                                PS1_TILE_MASK.lWallMasks[c] = 0xFF;
                                break;
                            }

                            uint8 m = PS1_TILE_MASK.floorMasks[h];
                            if (m != 0xFF && c >= m) {
                                PS1_TILE_MASK.lWallMasks[c] = h;
                                break;
                            }
                            else {
                                ++h;
                                if (h <= -1)
                                    break;
                            }
                        }
                    }

                    // RWall rotations
                    for (int32 c = 0; c < TILE_SIZE; ++c) {
                        int32 h = TILE_SIZE - 1;
                        while (true) {
                            if (h == -1) {
                                PS1_TILE_MASK.rWallMasks[c] = 0xFF;
                                break;
                            }

                            uint8 m = PS1_TILE_MASK.floorMasks[h];
                            if (m != 0xFF && c >= m) {
                                PS1_TILE_MASK.rWallMasks[c] = h;
                                break;
                            }
                            else {
                                --h;
                                if (h >= TILE_SIZE)
                                    break;
                            }
                        }
                    }
                }
#if RETRO_PLATFORM == RETRO_PS1
                if (!p) {
                    collisionMasks[0][t] = ps1Mask;
                    tileInfo[0][t]       = ps1Info;
                }
                else if (memcmp(&ps1Mask, &collisionMasks[0][t], sizeof(CollisionMask)) || memcmp(&ps1Info, &tileInfo[0][t], sizeof(TileInfo))) {
                    if (ps1PlaneBCount < PS1_PLANEB_MAX) {
                        ps1MasksB[ps1PlaneBCount] = ps1Mask;
                        ps1InfoB[ps1PlaneBCount]  = ps1Info;
                        ps1PlaneB[t]              = ps1PlaneBCount++;
                    }
                    else {
                        g_ps1PlaneBOverflow = g_ps1PlaneBOverflow + 1;
                    }
                }
#endif
            }

#if RETRO_PLATFORM != RETRO_PS1 // PS1: flips derived where read
            // FlipX
            for (int32 t = 0; t < TILE_COUNT; ++t) {
                int32 off                       = (FLIP_X * TILE_COUNT);
                tileInfo[p][t + off].flag       = tileInfo[p][t].flag;
                tileInfo[p][t + off].floorAngle = -tileInfo[p][t].floorAngle;
                tileInfo[p][t + off].lWallAngle = -tileInfo[p][t].rWallAngle;
                tileInfo[p][t + off].roofAngle  = -tileInfo[p][t].roofAngle;
                tileInfo[p][t + off].rWallAngle = -tileInfo[p][t].lWallAngle;

#if RETRO_PLATFORM != RETRO_PS1 // PS1: flipped masks derived on access (PS1/PS1Collision.hpp)
                for (int32 c = 0; c < TILE_SIZE; ++c) {
                    int32 h = collisionMasks[p][t].lWallMasks[c];
                    if (h == 0xFF)
                        collisionMasks[p][t + off].rWallMasks[c] = 0xFF;
                    else
                        collisionMasks[p][t + off].rWallMasks[c] = 0xF - h;

                    h = collisionMasks[p][t].rWallMasks[c];
                    if (h == 0xFF)
                        collisionMasks[p][t + off].lWallMasks[c] = 0xFF;
                    else
                        collisionMasks[p][t + off].lWallMasks[c] = 0xF - h;

                    collisionMasks[p][t + off].floorMasks[c] = collisionMasks[p][t].floorMasks[0xF - c];
                    collisionMasks[p][t + off].roofMasks[c]  = collisionMasks[p][t].roofMasks[0xF - c];
                }
#endif
            }

            // FlipY
            for (int32 t = 0; t < TILE_COUNT; ++t) {
                int32 off                       = (FLIP_Y * TILE_COUNT);
                tileInfo[p][t + off].flag       = tileInfo[p][t].flag;
                tileInfo[p][t + off].floorAngle = -0x80 - tileInfo[p][t].roofAngle;
                tileInfo[p][t + off].lWallAngle = -0x80 - tileInfo[p][t].lWallAngle;
                tileInfo[p][t + off].roofAngle  = -0x80 - tileInfo[p][t].floorAngle;
                tileInfo[p][t + off].rWallAngle = -0x80 - tileInfo[p][t].rWallAngle;

#if RETRO_PLATFORM != RETRO_PS1 // PS1: flipped masks derived on access (PS1/PS1Collision.hpp)
                for (int32 c = 0; c < TILE_SIZE; ++c) {
                    int32 h = collisionMasks[p][t].roofMasks[c];
                    if (h == 0xFF)
                        collisionMasks[p][t + off].floorMasks[c] = 0xFF;
                    else
                        collisionMasks[p][t + off].floorMasks[c] = 0xF - h;

                    h = collisionMasks[p][t].floorMasks[c];
                    if (h == 0xFF)
                        collisionMasks[p][t + off].roofMasks[c] = 0xFF;
                    else
                        collisionMasks[p][t + off].roofMasks[c] = 0xF - h;

                    collisionMasks[p][t + off].lWallMasks[c] = collisionMasks[p][t].lWallMasks[0xF - c];
                    collisionMasks[p][t + off].rWallMasks[c] = collisionMasks[p][t].rWallMasks[0xF - c];
                }
#endif
            }

            // FlipXY
            for (int32 t = 0; t < TILE_COUNT; ++t) {
                int32 off                       = (FLIP_XY * TILE_COUNT);
                int32 offY                      = (FLIP_Y * TILE_COUNT);
                tileInfo[p][t + off].flag       = tileInfo[p][t + offY].flag;
                tileInfo[p][t + off].floorAngle = -tileInfo[p][t + offY].floorAngle;
                tileInfo[p][t + off].lWallAngle = -tileInfo[p][t + offY].rWallAngle;
                tileInfo[p][t + off].roofAngle  = -tileInfo[p][t + offY].roofAngle;
                tileInfo[p][t + off].rWallAngle = -tileInfo[p][t + offY].lWallAngle;

#if RETRO_PLATFORM != RETRO_PS1 // PS1: flipped masks derived on access (PS1/PS1Collision.hpp)
                for (int32 c = 0; c < TILE_SIZE; ++c) {
                    int32 h = collisionMasks[p][t + offY].lWallMasks[c];
                    if (h == 0xFF)
                        collisionMasks[p][t + off].rWallMasks[c] = 0xFF;
                    else
                        collisionMasks[p][t + off].rWallMasks[c] = 0xF - h;

                    h = collisionMasks[p][t + offY].rWallMasks[c];
                    if (h == 0xFF)
                        collisionMasks[p][t + off].lWallMasks[c] = 0xFF;
                    else
                        collisionMasks[p][t + off].lWallMasks[c] = 0xF - h;

                    collisionMasks[p][t + off].floorMasks[c] = collisionMasks[p][t + offY].floorMasks[0xF - c];
                    collisionMasks[p][t + off].roofMasks[c]  = collisionMasks[p][t + offY].roofMasks[0xF - c];
                }
#endif
            }
#endif
        }
#undef PS1_TILE_MASK
#undef PS1_TILE_INFO

#if !RETRO_USE_ORIGINAL_CODE && RETRO_PLATFORM != RETRO_PS1
        RemoveStorageEntry((void **)&buffer);
        buffer = NULL;
#endif
        CloseFile(&info);
    }
}
void RSDK::LoadStageGIF(char *filepath)
{
#if RETRO_PLATFORM == RETRO_PS1
    // PS1 port (docs/32): the tileset's pixels go to VRAM at build time (phase 3) and the GIF is not on the disc. The
    // palette rows are applied from <tileset>.pal (the GIF's colour table, 256 x RGB, written by the data-disc tool)
    // exactly as below.
    char palPath[0x100];
    strcpy(palPath, filepath);
    char *ext = strrchr(palPath, '.');
    if (ext)
        strcpy(ext, ".pal");
    FileInfo palInfo;
    InitFileInfo(&palInfo);
    if (LoadFile(&palInfo, palPath, FMODE_RB)) {
        uint8 rgb[0x100 * 3];
        ReadBytes(&palInfo, rgb, sizeof(rgb));
        CloseFile(&palInfo);
        for (int32 r = 0; r < 0x10; ++r) {
            // only overwrite inactive rows
            if (!(activeStageRows[0] >> r & 1) && !(activeGlobalRows[0] >> r & 1)) {
                for (int32 c = 0; c < 0x10; ++c) {
                    uint8 *clr                   = &rgb[((r << 4) + c) * 3];
                    fullPalette[0][(r << 4) + c] = rgb32To16_B[clr[2]] | rgb32To16_G[clr[1]] | rgb32To16_R[clr[0]];
                }
            }
        }
    }
    return;
#endif
    ImageGIF tileset;

    if (tileset.Load(filepath, true) && tileset.width == TILE_SIZE && tileset.height <= TILE_COUNT * TILE_SIZE) {
        tileset.pixels = tilesetPixels;
        tileset.Load(NULL, false);

        for (int32 r = 0; r < 0x10; ++r) {
            // only overwrite inactive rows
            if (!(activeStageRows[0] >> r & 1) && !(activeGlobalRows[0] >> r & 1)) {
                for (int32 c = 0; c < 0x10; ++c) {
                    uint8 red                    = (tileset.palette[(r << 4) + c] >> 0x10);
                    uint8 green                  = (tileset.palette[(r << 4) + c] >> 0x08);
                    uint8 blue                   = (tileset.palette[(r << 4) + c] >> 0x00);
                    fullPalette[0][(r << 4) + c] = rgb32To16_B[blue] | rgb32To16_G[green] | rgb32To16_R[red];
                }
            }
        }

        // Flip X
        uint8 *srcPixels = tilesetPixels;
        uint8 *dstPixels = &tilesetPixels[(FLIP_X * TILESET_SIZE) + (TILE_SIZE - 1)];
        for (int32 t = 0; t < 0x400 * TILE_SIZE; ++t) {
            for (int32 r = 0; r < TILE_SIZE; ++r) {
                *dstPixels-- = *srcPixels++;
            }

            dstPixels += (TILE_SIZE * 2);
        }

        // Flip Y
        srcPixels = tilesetPixels;
        for (int32 t = 0; t < 0x400; ++t) {
            dstPixels = &tilesetPixels[(FLIP_Y * TILESET_SIZE) + (t * TILE_DATASIZE) + (TILE_DATASIZE - TILE_SIZE)];
            for (int32 y = 0; y < TILE_SIZE; ++y) {
                for (int32 x = 0; x < TILE_SIZE; ++x) {
                    *dstPixels++ = *srcPixels++;
                }

                dstPixels -= (TILE_SIZE * 2);
            }
        }

        // Flip XY
        srcPixels = &tilesetPixels[(FLIP_Y * TILESET_SIZE)];
        dstPixels = &tilesetPixels[(FLIP_XY * TILESET_SIZE) + (TILE_SIZE - 1)];
        for (int32 t = 0; t < 0x400 * TILE_SIZE; ++t) {
            for (int32 r = 0; r < TILE_SIZE; ++r) {
                *dstPixels-- = *srcPixels++;
            }

            dstPixels += (TILE_SIZE * 2);
        }

#if RETRO_USE_ORIGINAL_CODE
        tileset.palette = NULL;
        tileset.decoder = NULL;
#endif
        tileset.pixels  = NULL;
    }
}

void RSDK::ProcessParallaxAutoScroll()
{
    for (int32 l = 0; l < LAYER_COUNT; ++l) {
        TileLayer *layer = &tileLayers[l];

        if (layer->layout) {
            layer->scrollPos += layer->scrollSpeed;

            for (int32 s = 0; s < layer->scrollInfoCount; ++s) layer->scrollInfo[s].scrollPos += layer->scrollInfo[s].scrollSpeed;
        }
    }
}
#if RETRO_PLATFORM == RETRO_PS1
// 7b: a layer whose lines all use one scroll info frees its lineScroll table at the load (ps1LineScrollOne holds it)
#define PS1_LS_BASE(layer) ((layer)->lineScroll ? (layer)->lineScroll : &(layer)->ps1LineScrollOne)
#define PS1_LS_STEP(layer) ((layer)->lineScroll ? 1 : 0)
#else
#define PS1_LS_BASE(layer) ((layer)->lineScroll)
#define PS1_LS_STEP(layer) (1)
#endif
void RSDK::ProcessParallax(TileLayer *layer)
{
    if (!layer->xsize || !layer->ysize)
        return;

    int32 pixelWidth       = TILE_SIZE * layer->xsize;
    int32 pixelHeight      = TILE_SIZE * layer->ysize;
    ScanlineInfo *scanline = scanlines;
    ScrollInfo *scrollInfo = layer->scrollInfo;

    switch (layer->type) {
        default: break;

        case LAYER_HSCROLL: {
            for (int32 i = 0; i < layer->scrollInfoCount; ++i) {
                scrollInfo->tilePos = scrollInfo->scrollPos + (currentScreen->position.x * scrollInfo->parallaxFactor << 8);

                int16 tilePos = FROM_FIXED(scrollInfo->tilePos) % pixelWidth;
                if (tilePos < 0)
                    tilePos += pixelWidth;
                scrollInfo->tilePos = TO_FIXED(tilePos);

                ++scrollInfo;
            }

            int16 scrollPos =
                FROM_FIXED((int32)((layer->scrollPos + (layer->parallaxFactor * currentScreen->position.y << 8)) & 0xFFFF0000)) % pixelHeight;
            if (scrollPos < 0)
                scrollPos += pixelHeight;

            #if RETRO_PLATFORM == RETRO_PS1
            // 3.3c: every line on one scroll info without deformation (and no one-colour table for the scan) is one X and
            // consecutive rows: the PS1 drawer builds its 16-line bands from these two numbers, the per-line fill below is
            // skipped (kept on the 8 MB evidence link: layer_reference dumps the scanlines)
            layer->ps1Uniform = 0;
            if (layer->ps1LineScrollOne < layer->scrollInfoCount && !layer->scrollInfo[layer->ps1LineScrollOne].deform
                && !PS1TileSolidLines((int32)(layer - tileLayers), pixelHeight)) {
                layer->ps1Uniform  = 1;
                layer->ps1UniformX = (int16)FROM_FIXED(layer->scrollInfo[layer->ps1LineScrollOne].tilePos);
                layer->ps1UniformY = scrollPos;
#if !PS1_DEBUG_BIGRAM
                break;
#endif
            }
            // 3.3h: no scroll info deforms: the drawer takes each line's X from lineScroll (static: set at the scene load)
            // and the scroll infos' tilePos, the rows consecutive from scrollPos; the fill below is skipped likewise
            layer->ps1Direct = 0;
            if (!layer->ps1Uniform && layer->scrollInfoCount <= 0x80) {
                int32 i = 0;
                while (i < layer->scrollInfoCount && !layer->scrollInfo[i].deform)
                    ++i;
                if (i == layer->scrollInfoCount) {
                    layer->ps1Direct   = 1;
                    layer->ps1UniformY = scrollPos;
#if !PS1_DEBUG_BIGRAM
                    break;
#endif
                }
            }
#endif
            uint8 *lsBase = PS1_LS_BASE(layer); // 7b: a one-value layer keeps no table (its value, not advanced)
            const int32 lsStep = PS1_LS_STEP(layer);
            uint8 *lineScrollPtr = lsBase + scrollPos * lsStep;

            // Above water
            int32 *deformationData = &layer->deformationData[(scrollPos + (uint16)layer->deformationOffset) & 0x1FF];
            for (int32 i = 0; i < currentScreen->waterDrawPos; ++i) {
                scanline->position.x = layer->scrollInfo[*lineScrollPtr].tilePos;
                if (layer->scrollInfo[*lineScrollPtr].deform)
                    scanline->position.x += TO_FIXED(*deformationData);

                scanline->position.y = TO_FIXED(scrollPos++);

                deformationData++;
                if (scrollPos == pixelHeight) {
                    lineScrollPtr = lsBase;
                    scrollPos     = 0;
                }
                else {
                    lineScrollPtr += lsStep;
                }
                scanline++;
            }

            // Under water
            deformationData = &layer->deformationDataW[(scrollPos + (uint16)layer->deformationOffsetW) & 0x1FF];
            for (int32 i = currentScreen->waterDrawPos; i < currentScreen->size.y; ++i) {
                scanline->position.x = layer->scrollInfo[*lineScrollPtr].tilePos;
                if (layer->scrollInfo[*lineScrollPtr].deform)
                    scanline->position.x += TO_FIXED(*deformationData);

                scanline->position.y = TO_FIXED(scrollPos++);

                deformationData++;
                if (scrollPos == pixelHeight) {
                    lineScrollPtr = lsBase;
                    scrollPos     = 0;
                }
                else {
                    lineScrollPtr += lsStep;
                }
                scanline++;
            }
            break;
        }

        case LAYER_VSCROLL: {
            for (int32 i = 0; i < layer->scrollInfoCount; ++i) {
                scrollInfo->tilePos = scrollInfo->scrollPos + (currentScreen->position.y * scrollInfo->parallaxFactor << 8);
                scrollInfo->tilePos = TO_FIXED(FROM_FIXED(scrollInfo->tilePos) % pixelHeight);

                ++scrollInfo;
            }

            int16 scrollPos =
                FROM_FIXED((int32)((layer->scrollPos + (layer->parallaxFactor * currentScreen->position.x << 8)) & 0xFFFF0000)) % pixelWidth;
            if (scrollPos < 0)
                scrollPos += pixelWidth;

            uint8 *lsBase = PS1_LS_BASE(layer); // 7b: a one-value layer keeps no table (its value, not advanced)
            const int32 lsStep = PS1_LS_STEP(layer);
            uint8 *lineScrollPtr = lsBase + scrollPos * lsStep;

            // Above water
            for (int32 i = 0; i < currentScreen->size.x; ++i) {
                scanline->position.x = TO_FIXED(scrollPos++);
                scanline->position.y = layer->scrollInfo[*lineScrollPtr].tilePos;

                if (scrollPos == pixelWidth) {
                    lineScrollPtr = lsBase;
                    scrollPos     = 0;
                }
                else {
                    lineScrollPtr += lsStep;
                }

                scanline++;
            }
            break;
        }

        case LAYER_ROTOZOOM: {
            int16 scrollPosX =
                FROM_FIXED((int32)((layer->scrollPos + (layer->parallaxFactor * currentScreen->position.x << 8)) & 0xFFFF0000)) % pixelWidth;
            if (scrollPosX < 0)
                scrollPosX += pixelWidth;

            int16 scrollPosY =
                FROM_FIXED((int32)((layer->scrollPos + (layer->parallaxFactor * currentScreen->position.y << 8)) & 0xFFFF0000)) % pixelHeight;
            if (scrollPosY < 0)
                scrollPosY += pixelHeight;

            for (int32 i = 0; i < currentScreen->size.y; ++i) {
                scanline->position.x = TO_FIXED(scrollPosX);
                scanline->position.y = TO_FIXED(scrollPosY++);
                scanline->deform.x   = TO_FIXED(1);
                scanline->deform.y   = TO_FIXED(0);

                scanline++;
            }
            break;
        }

        case LAYER_BASIC: {
            for (int32 i = 0; i < layer->scrollInfoCount; ++i) {
                scrollInfo->tilePos = scrollInfo->scrollPos + (currentScreen->position.x * scrollInfo->parallaxFactor << 8);

                int16 tilePos = FROM_FIXED(scrollInfo->tilePos) % pixelWidth;
                if (tilePos < 0)
                    tilePos += pixelWidth;
                scrollInfo->tilePos = TO_FIXED(tilePos);

                ++scrollInfo;
            }

            int16 scrollPos =
                FROM_FIXED((int32)((layer->scrollPos + (layer->parallaxFactor * currentScreen->position.y << 8)) & 0xFFFF0000)) % pixelHeight;
            if (scrollPos < 0)
                scrollPos += pixelHeight;

            uint8 *lsBase = PS1_LS_BASE(layer); // 7b: a one-value layer keeps no table (its value, not advanced)
            const int32 lsStep = PS1_LS_STEP(layer);
            uint8 *lineScrollPtr = lsBase + scrollPos * lsStep;

            // Above water
            for (int32 i = 0; i < currentScreen->waterDrawPos; ++i) {
                scanline->position.x = layer->scrollInfo[*lineScrollPtr].tilePos;
                scanline->position.y = TO_FIXED(scrollPos++);

                if (scrollPos == pixelHeight) {
                    lineScrollPtr = lsBase;
                    scrollPos     = 0;
                }
                else {
                    lineScrollPtr += lsStep;
                }
                scanline++;
            }

            // Under water
            for (int32 i = currentScreen->waterDrawPos; i < currentScreen->size.y; ++i) {
                scanline->position.x = layer->scrollInfo[*lineScrollPtr].tilePos;
                scanline->position.y = TO_FIXED(scrollPos++);

                if (scrollPos == pixelHeight) {
                    lineScrollPtr = lsBase;
                    scrollPos     = 0;
                }
                else {
                    lineScrollPtr += lsStep;
                }

                scanline++;
            }
            break;
        }
    }
}

void RSDK::ProcessSceneTimer()
{
    if (sceneInfo.timeEnabled) {
        sceneInfo.timeCounter += 100;

        if (sceneInfo.timeCounter >= 6000) {
            sceneInfo.timeCounter -= 6025;

            if (++sceneInfo.seconds >= 60) {
                sceneInfo.seconds = 0;

                if (++sceneInfo.minutes >= 60)
                    sceneInfo.minutes = 0;
            }
        }

        sceneInfo.milliseconds = sceneInfo.timeCounter / 60; // 60 == refreshRate
    }
}

void RSDK::SetScene(const char *categoryName, const char *sceneName)
{
    RETRO_HASH_MD5(catHash);
    GEN_HASH_MD5(categoryName, catHash);

    RETRO_HASH_MD5(scnHash);
    GEN_HASH_MD5(sceneName, scnHash);

    for (int32 i = 0; i < sceneInfo.categoryCount; ++i) {
        if (HASH_MATCH_MD5(sceneInfo.listCategory[i].hash, catHash)) {
            sceneInfo.activeCategory = i;
            sceneInfo.listPos        = sceneInfo.listCategory[i].sceneOffsetStart;

            for (int32 s = 0; s < sceneInfo.listCategory[i].sceneCount; ++s) {
                if (HASH_MATCH_MD5(sceneInfo.listData[sceneInfo.listCategory[i].sceneOffsetStart + s].hash, scnHash)) {
                    sceneInfo.listPos = sceneInfo.listCategory[i].sceneOffsetStart + s;
                    break;
                }
            }

            break;
        }
    }
}

void RSDK::CopyTileLayer(uint16 dstLayerID, int32 dstStartX, int32 dstStartY, uint16 srcLayerID, int32 srcStartX, int32 srcStartY, int32 countX,
                         int32 countY)
{
    if (dstLayerID < LAYER_COUNT && srcLayerID < LAYER_COUNT) {
        TileLayer *dstLayer = &tileLayers[dstLayerID];
        TileLayer *srcLayer = &tileLayers[srcLayerID];

        if (dstStartX >= 0 && dstStartX < dstLayer->xsize && dstStartY >= 0 && dstStartY < dstLayer->ysize) {
            if (srcStartX >= 0 && srcStartX < srcLayer->xsize && srcStartY >= 0 && srcStartY < srcLayer->ysize) {
                if (dstStartX + countX > dstLayer->xsize)
                    countX = dstLayer->xsize - dstStartX;

                if (dstStartY + countY > dstLayer->ysize)
                    countY = dstLayer->ysize - dstStartY;

                if (srcStartX + countX > srcLayer->xsize)
                    countX = srcLayer->xsize - srcStartX;

                if (srcStartY + countY > srcLayer->ysize)
                    countY = srcLayer->ysize - srcStartY;

                for (int32 y = 0; y < countY; ++y) {
                    for (int32 x = 0; x < countX; ++x) {
#if RETRO_PLATFORM == RETRO_PS1
                        PS1SetTile(dstLayer, x + dstStartX, y + dstStartY, PS1_LAYOUT(srcLayer, x + srcStartX, y + srcStartY));
#else
                        uint16 tile = srcLayer->layout[(x + srcStartX) + ((y + srcStartY) << srcLayer->widthShift)];
                        dstLayer->layout[(x + dstStartX) + ((y + dstStartY) << dstLayer->widthShift)] = tile;
#endif
                    }
                }
            }
        }
    }
}

void RSDK::DrawLayerHScroll(TileLayer *layer)
{
#if RETRO_PLATFORM == RETRO_PS1
    PS1DrawLayerHScroll(layer); // bands of tile pieces through the GPU chain (docs/32 3.3)
    return;
#endif
#if RETRO_PS1_HEADLESS
    return; // PS1 phase 2: headless (docs/32)
#endif
    if (!layer->xsize || !layer->ysize)
        return;

    int32 lineTileCount    = (currentScreen->pitch >> 4) - 1;
    uint8 *lineBuffer      = &gfxLineBuffer[currentScreen->clipBound_Y1];
    ScanlineInfo *scanline = &scanlines[currentScreen->clipBound_Y1];
    uint16 *frameBuffer    = &currentScreen->frameBuffer[currentScreen->pitch * currentScreen->clipBound_Y1];

    for (int32 cy = currentScreen->clipBound_Y1; cy < currentScreen->clipBound_Y2; ++cy) {
        int32 x               = scanline->position.x;
        int32 y               = scanline->position.y;
        int32 tileX           = FROM_FIXED(x);
        uint16 *activePalette = fullPalette[*lineBuffer++];

        if (tileX >= TILE_SIZE * layer->xsize)
            x = TO_FIXED(tileX - TILE_SIZE * layer->xsize);
        else if (tileX < 0)
            x = TO_FIXED(tileX + TILE_SIZE * layer->xsize);

        int32 tileRemain = TILE_SIZE - (FROM_FIXED(x) & 0xF);
        int32 sheetX     = FROM_FIXED(x) & 0xF;
        int32 sheetY     = TILE_SIZE * (FROM_FIXED(y) & 0xF);
        int32 lineRemain = currentScreen->pitch;

        int32 tx       = x >> 20;
        uint16 *layout = &layer->layout[tx + ((y >> 20) << layer->widthShift)];
        lineRemain -= tileRemain;

        if (*layout >= 0xFFFF) {
            frameBuffer += tileRemain;
        }
        else {
            uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + sheetY + sheetX];
            for (int32 x = 0; x < tileRemain; ++x) {
                if (*pixels)
                    *frameBuffer = activePalette[*pixels];
                ++pixels;
                ++frameBuffer;
            }
        }

        for (int32 l = 0; l < lineTileCount; ++l) {
            ++layout;

            if (++tx == layer->xsize) {
                tx = 0;
                layout -= layer->xsize;
            }

            if (*layout < 0xFFFF) {
                uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + sheetY];

                uint8 index = *pixels;
                if (index)
                    *frameBuffer = activePalette[index];

                index = pixels[1];
                if (index)
                    frameBuffer[1] = activePalette[index];

                index = pixels[2];
                if (index)
                    frameBuffer[2] = activePalette[index];

                index = pixels[3];
                if (index)
                    frameBuffer[3] = activePalette[index];

                index = pixels[4];
                if (index)
                    frameBuffer[4] = activePalette[index];

                index = pixels[5];
                if (index)
                    frameBuffer[5] = activePalette[index];

                index = pixels[6];
                if (index)
                    frameBuffer[6] = activePalette[index];

                index = pixels[7];
                if (index)
                    frameBuffer[7] = activePalette[index];

                index = pixels[8];
                if (index)
                    frameBuffer[8] = activePalette[index];

                index = pixels[9];
                if (index)
                    frameBuffer[9] = activePalette[index];

                index = pixels[10];
                if (index)
                    frameBuffer[10] = activePalette[index];

                index = pixels[11];
                if (index)
                    frameBuffer[11] = activePalette[index];

                index = pixels[12];
                if (index)
                    frameBuffer[12] = activePalette[index];

                index = pixels[13];
                if (index)
                    frameBuffer[13] = activePalette[index];

                index = pixels[14];
                if (index)
                    frameBuffer[14] = activePalette[index];

                index = pixels[15];
                if (index)
                    frameBuffer[15] = activePalette[index];
            }

            frameBuffer += TILE_SIZE;
            lineRemain -= TILE_SIZE;
        }

        while (lineRemain > 0) {
            ++layout;

            if (++tx == layer->xsize) {
                tx = 0;
                layout -= layer->xsize;
            }

            tileRemain = lineRemain >= TILE_SIZE ? TILE_SIZE : lineRemain;

            if (*layout >= 0xFFFF) {
                frameBuffer += tileRemain;
            }
            else {
                uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + sheetY];
                for (int32 x = 0; x < tileRemain; ++x) {
                    if (*pixels)
                        *frameBuffer = activePalette[*pixels];
                    ++pixels;
                    ++frameBuffer;
                }
            }

            lineRemain -= TILE_SIZE;
        }

        ++scanline;
    }
}
void RSDK::DrawLayerVScroll(TileLayer *layer)
{
#if RETRO_PS1_HEADLESS
    return; // PS1 phase 2: headless (docs/32)
#endif
    if (!layer->xsize || !layer->ysize)
        return;

    int32 lineTileCount    = (currentScreen->size.y >> 4) - 1;
    uint16 *frameBuffer    = &currentScreen->frameBuffer[currentScreen->clipBound_X1];
    ScanlineInfo *scanline = &scanlines[currentScreen->clipBound_X1];
    uint16 *activePalette  = fullPalette[gfxLineBuffer[0]];

    for (int32 cx = currentScreen->clipBound_X1; cx < currentScreen->clipBound_X2; ++cx) {
        int32 x  = scanline->position.x;
        int32 y  = scanline->position.y;
        int32 ty = FROM_FIXED(y);

        if (ty >= TILE_SIZE * layer->ysize)
            y -= TO_FIXED(TILE_SIZE * layer->ysize);
        else if (ty < 0)
            y += TO_FIXED(TILE_SIZE * layer->ysize);

        int32 tileRemain = TILE_SIZE - (FROM_FIXED(y) & 0xF);
        int32 sheetX     = FROM_FIXED(x) & 0xF;
        int32 sheetY     = FROM_FIXED(y) & 0xF;
        int32 lineRemain = currentScreen->size.y;

        uint16 *layout = &layer->layout[(x >> 20) + ((y >> 20) << layer->widthShift)];
        lineRemain -= tileRemain;

        if (*layout >= 0xFFFF) {
            frameBuffer += currentScreen->pitch * tileRemain;
        }
        else {
            uint8 *pixels = &tilesetPixels[TILE_SIZE * (sheetY + TILE_SIZE * (*layout & 0xFFF)) + sheetX];
            for (int32 y = 0; y < tileRemain; ++y) {
                if (*pixels)
                    *frameBuffer = activePalette[*pixels];
                pixels += TILE_SIZE;
                frameBuffer += currentScreen->pitch;
            }
        }

        ty = y >> 20;
        for (int32 l = 0; l < lineTileCount; ++l) {
            layout += layer->xsize;

            if (++ty == layer->ysize) {
                ty = 0;
                layout -= layer->ysize << layer->widthShift;
            }

            if (*layout >= 0xFFFF) {
                frameBuffer += TILE_SIZE * currentScreen->pitch;
            }
            else {
                uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + sheetX];

                if (*pixels)
                    *frameBuffer = activePalette[*pixels];

                if (pixels[0x10])
                    frameBuffer[currentScreen->pitch * 1] = activePalette[pixels[0x10]];

                if (pixels[0x20])
                    frameBuffer[currentScreen->pitch * 2] = activePalette[pixels[0x20]];

                if (pixels[0x30])
                    frameBuffer[currentScreen->pitch * 3] = activePalette[pixels[0x30]];

                if (pixels[0x40])
                    frameBuffer[currentScreen->pitch * 4] = activePalette[pixels[0x40]];

                if (pixels[0x50])
                    frameBuffer[currentScreen->pitch * 5] = activePalette[pixels[0x50]];

                if (pixels[0x60])
                    frameBuffer[currentScreen->pitch * 6] = activePalette[pixels[0x60]];

                if (pixels[0x70])
                    frameBuffer[currentScreen->pitch * 7] = activePalette[pixels[0x70]];

                if (pixels[0x80])
                    frameBuffer[currentScreen->pitch * 8] = activePalette[pixels[0x80]];

                if (pixels[0x90])
                    frameBuffer[currentScreen->pitch * 9] = activePalette[pixels[0x90]];

                if (pixels[0xA0])
                    frameBuffer[currentScreen->pitch * 10] = activePalette[pixels[0xA0]];

                if (pixels[0xB0])
                    frameBuffer[currentScreen->pitch * 11] = activePalette[pixels[0xB0]];

                if (pixels[0xC0])
                    frameBuffer[currentScreen->pitch * 12] = activePalette[pixels[0xC0]];

                if (pixels[0xD0])
                    frameBuffer[currentScreen->pitch * 13] = activePalette[pixels[0xD0]];

                if (pixels[0xE0])
                    frameBuffer[currentScreen->pitch * 14] = activePalette[pixels[0xE0]];

                if (pixels[0xF0])
                    frameBuffer[currentScreen->pitch * 15] = activePalette[pixels[0xF0]];

                frameBuffer += currentScreen->pitch * TILE_SIZE;
            }

            lineRemain -= TILE_SIZE;
        }

        while (lineRemain > 0) {
            layout += layer->xsize;

            if (++ty == layer->ysize) {
                ty = 0;
                layout -= layer->ysize << layer->widthShift;
            }

            tileRemain = lineRemain >= TILE_SIZE ? TILE_SIZE : lineRemain;
            if (*layout >= 0xFFFF) {
                frameBuffer += currentScreen->pitch * sheetY;
            }
            else {
                uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + sheetX];
                for (int32 y = 0; y < tileRemain; ++y) {
                    if (*pixels)
                        *frameBuffer = activePalette[*pixels];

                    pixels += TILE_SIZE;
                    frameBuffer += currentScreen->pitch;
                }
            }

            lineRemain -= TILE_SIZE;
        }

        frameBuffer -= currentScreen->pitch * currentScreen->size.y;

        ++scanline;
        ++frameBuffer;
    }
}
void RSDK::DrawLayerRotozoom(TileLayer *layer)
{
#if RETRO_PLATFORM == RETRO_PS1
    PS1DrawLayerRotozoom(layer); // tile quads through the GPU chain (docs/32 4.1d)
    return;
#endif
#if RETRO_PS1_HEADLESS
    return; // PS1 phase 2: headless (docs/32)
#endif
    if (!layer->xsize || !layer->ysize)
        return;

    uint16 *layout         = layer->layout;
    uint8 *lineBuffer      = &gfxLineBuffer[currentScreen->clipBound_Y1];
    ScanlineInfo *scanline = &scanlines[currentScreen->clipBound_Y1];
    uint16 *frameBuffer    = &currentScreen->frameBuffer[currentScreen->clipBound_X1 + currentScreen->clipBound_Y1 * currentScreen->pitch];

    int32 width    = (TILE_SIZE << layer->widthShift) - 1;
    int32 height   = (TILE_SIZE << layer->heightShift) - 1;
    int32 lineSize = currentScreen->clipBound_X2 - currentScreen->clipBound_X1;

    for (int32 cy = currentScreen->clipBound_Y1; cy < currentScreen->clipBound_Y2; ++cy) {
        int32 posX = scanline->position.x;
        int32 posY = scanline->position.y;

        uint16 *activePalette = fullPalette[*lineBuffer];
        ++lineBuffer;
        int32 fbOffset = currentScreen->pitch - lineSize;

        for (int32 cx = 0; cx < lineSize; ++cx) {
            int32 tx = posX >> 20;
            int32 ty = posY >> 20;
            int32 x  = FROM_FIXED(posX) & 0xF;
            int32 y  = FROM_FIXED(posY) & 0xF;

            uint16 tile = layout[((width >> 4) & tx) + (((height >> 4) & ty) << layer->widthShift)] & 0xFFF;
            uint8 idx   = tilesetPixels[TILE_SIZE * (y + TILE_SIZE * tile) + x];

            if (idx)
                *frameBuffer = activePalette[idx];

            posX += scanline->deform.x;
            posY += scanline->deform.y;
            ++frameBuffer;
        }

        frameBuffer += fbOffset;
        ++scanline;
    }
}
void RSDK::DrawLayerBasic(TileLayer *layer)
{
#if RETRO_PLATFORM == RETRO_PS1
    PS1DrawLayerBasic(layer); // bands of tile pieces through the GPU chain (docs/32 4.1c)
    return;
#endif
#if RETRO_PS1_HEADLESS
    return; // PS1 phase 2: headless (docs/32)
#endif
    if (!layer->xsize || !layer->ysize)
        return;

    if (currentScreen->clipBound_X1 >= currentScreen->clipBound_X2 || currentScreen->clipBound_Y1 >= currentScreen->clipBound_Y2)
        return;

    uint16 *activePalette = fullPalette[0];
    if (currentScreen->clipBound_X1 < currentScreen->clipBound_X2 && currentScreen->clipBound_Y1 < currentScreen->clipBound_Y2) {
        int32 lineSize = (currentScreen->clipBound_X2 - currentScreen->clipBound_X1) >> 4;

        ScanlineInfo *scanline = &scanlines[currentScreen->clipBound_Y1];

        int32 tx          = (currentScreen->clipBound_X1 + FROM_FIXED(scanline->position.x)) >> 4;
        int32 ty          = FROM_FIXED(scanline->position.y) >> 4;
        int32 sheetY      = FROM_FIXED(scanline->position.y) & 0xF;
        int32 sheetX      = (currentScreen->clipBound_X1 + FROM_FIXED(scanline->position.x)) & 0xF;
        int32 tileRemainX = TILE_SIZE - sheetX;
        int32 tileRemainY = TILE_SIZE - sheetY;

        uint16 *frameBuffer = &currentScreen->frameBuffer[currentScreen->clipBound_X1 + currentScreen->clipBound_Y1 * currentScreen->pitch];
        uint16 *layout      = &layer->layout[tx + (ty << layer->widthShift)];

        // Remaining pixels on top
        {
            if (*layout == 0xFFFF) {
                frameBuffer += TILE_SIZE - sheetX;
            }
            else {
                uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + TILE_SIZE * sheetY + sheetX];

                for (int32 y = 0; y < tileRemainY; ++y) {
                    for (int32 x = 0; x < tileRemainX; ++x) {
                        if (*pixels)
                            *frameBuffer = activePalette[*pixels];
                        ++pixels;
                        ++frameBuffer;
                    }

                    pixels += sheetX;
                    frameBuffer += currentScreen->pitch - tileRemainX;
                }

                frameBuffer += tileRemainX - currentScreen->pitch * tileRemainY;
            }

            ++layout;
            if (++tx == layer->xsize) {
                tx = 0;
                layout -= layer->xsize;
            }

            for (int32 x = 0; x < lineSize; ++x) {
                if (*layout == 0xFFFF) {
                    frameBuffer += TILE_SIZE;
                }
                else {
                    uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + TILE_SIZE * sheetY];
                    for (int32 y = 0; y < tileRemainY; ++y) {
                        uint8 index = *pixels;
                        if (index)
                            *frameBuffer = activePalette[index];

                        index = pixels[1];
                        if (index)
                            frameBuffer[1] = activePalette[index];

                        index = pixels[2];
                        if (index)
                            frameBuffer[2] = activePalette[index];

                        index = pixels[3];
                        if (index)
                            frameBuffer[3] = activePalette[index];

                        index = pixels[4];
                        if (index)
                            frameBuffer[4] = activePalette[index];

                        index = pixels[5];
                        if (index)
                            frameBuffer[5] = activePalette[index];

                        index = pixels[6];
                        if (index)
                            frameBuffer[6] = activePalette[index];

                        index = pixels[7];
                        if (index)
                            frameBuffer[7] = activePalette[index];

                        index = pixels[8];
                        if (index)
                            frameBuffer[8] = activePalette[index];

                        index = pixels[9];
                        if (index)
                            frameBuffer[9] = activePalette[index];

                        index = pixels[10];
                        if (index)
                            frameBuffer[10] = activePalette[index];

                        index = pixels[11];
                        if (index)
                            frameBuffer[11] = activePalette[index];

                        index = pixels[12];
                        if (index)
                            frameBuffer[12] = activePalette[index];

                        index = pixels[13];
                        if (index)
                            frameBuffer[13] = activePalette[index];

                        index = pixels[14];
                        if (index)
                            frameBuffer[14] = activePalette[index];

                        index = pixels[15];
                        if (index)
                            frameBuffer[15] = activePalette[index];

                        frameBuffer += currentScreen->pitch;
                        pixels += TILE_SIZE;
                    }

                    frameBuffer += TILE_SIZE - currentScreen->pitch * tileRemainY;
                }

                ++layout;
                if (++tx == layer->xsize) {
                    tx = 0;
                    layout -= layer->xsize;
                }
            }

            if (*layout == 0xFFFF) {
                frameBuffer += currentScreen->pitch * tileRemainY;
            }
            else {
                uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + TILE_SIZE * sheetY];

                for (int32 y = 0; y < tileRemainY; ++y) {
                    for (int32 x = 0; x < sheetX; ++x) {
                        if (*pixels)
                            *frameBuffer = activePalette[*pixels];
                        ++pixels;
                        ++frameBuffer;
                    }

                    pixels += tileRemainX;
                    frameBuffer += currentScreen->pitch - sheetX;
                }
            }
        }

        // We've drawn a single line of pixels, increase our variables
        frameBuffer += sheetX + -TILE_SIZE * lineSize - TILE_SIZE;
        scanline += tileRemainY;
        if (++ty == layer->ysize)
            ty = 0;

        // Draw the bulk of the tiles
        int32 lineTileCount = ((currentScreen->clipBound_Y2 - currentScreen->clipBound_Y1) >> 4) - 1;
        for (int32 l = 0; l < lineTileCount; ++l) {
            sheetX      = (currentScreen->clipBound_X1 + FROM_FIXED(scanline->position.x)) & 0xF;
            tx          = (currentScreen->clipBound_X1 + FROM_FIXED(scanline->position.x)) >> 4;
            tileRemainX = TILE_SIZE - sheetX;
            layout      = &layer->layout[tx + (ty << layer->widthShift)];

            // Draw any stray pixels on the left
            if (*layout == 0xFFFF) {
                frameBuffer += tileRemainX;
            }
            else {
                uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + sheetX];

                for (int32 y = 0; y < TILE_SIZE; ++y) {
                    for (int32 x = 0; x < tileRemainX; ++x) {
                        if (*pixels)
                            *frameBuffer = activePalette[*pixels];
                        ++pixels;
                        ++frameBuffer;
                    }

                    pixels += sheetX;
                    frameBuffer += currentScreen->pitch - tileRemainX;
                }

                frameBuffer += tileRemainX - TILE_SIZE * currentScreen->pitch;
            }
            ++layout;
            if (++tx == layer->xsize) {
                tx = 0;
                layout -= layer->xsize;
            }

            // Draw the bulk of the tiles on this line
            for (int32 x = 0; x < lineSize; ++x) {
                if (*layout == 0xFFFF) {
                    frameBuffer += TILE_SIZE;
                }
                else {
                    uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF)];

                    for (int32 y = 0; y < TILE_SIZE; ++y) {
                        uint8 index = *pixels;
                        if (index)
                            *frameBuffer = activePalette[index];

                        index = pixels[1];
                        if (index)
                            frameBuffer[1] = activePalette[index];

                        index = pixels[2];
                        if (index)
                            frameBuffer[2] = activePalette[index];

                        index = pixels[3];
                        if (index)
                            frameBuffer[3] = activePalette[index];

                        index = pixels[4];
                        if (index)
                            frameBuffer[4] = activePalette[index];

                        index = pixels[5];
                        if (index)
                            frameBuffer[5] = activePalette[index];

                        index = pixels[6];
                        if (index)
                            frameBuffer[6] = activePalette[index];

                        index = pixels[7];
                        if (index)
                            frameBuffer[7] = activePalette[index];

                        index = pixels[8];
                        if (index)
                            frameBuffer[8] = activePalette[index];

                        index = pixels[9];
                        if (index)
                            frameBuffer[9] = activePalette[index];

                        index = pixels[10];
                        if (index)
                            frameBuffer[10] = activePalette[index];

                        index = pixels[11];
                        if (index)
                            frameBuffer[11] = activePalette[index];

                        index = pixels[12];
                        if (index)
                            frameBuffer[12] = activePalette[index];

                        index = pixels[13];
                        if (index)
                            frameBuffer[13] = activePalette[index];

                        index = pixels[14];
                        if (index)
                            frameBuffer[14] = activePalette[index];

                        index = pixels[15];
                        if (index)
                            frameBuffer[15] = activePalette[index];

                        pixels += TILE_SIZE;
                        frameBuffer += currentScreen->pitch;
                    }

                    frameBuffer -= TILE_SIZE * currentScreen->pitch;
                    frameBuffer += TILE_SIZE;
                }

                ++layout;
                if (++tx == layer->xsize) {
                    tx = 0;
                    layout -= layer->xsize;
                }
            }

            // Draw any stray pixels on the right
            if (*layout == 0xFFFF) {
                frameBuffer += TILE_SIZE * currentScreen->pitch;
            }
            else {
                uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF)];

                for (int32 y = 0; y < TILE_SIZE; ++y) {
                    for (int32 x = 0; x < sheetX; ++x) {
                        if (*pixels)
                            *frameBuffer = activePalette[*pixels];
                        ++pixels;
                        ++frameBuffer;
                    }

                    pixels += tileRemainX;
                    frameBuffer += currentScreen->pitch - sheetX;
                }
            }
            ++layout;
            if (++tx == layer->xsize) {
                tx = 0;
                layout -= layer->xsize;
            }

            // We've drawn a single line, increase our variables
            scanline += TILE_SIZE;
            frameBuffer += sheetX + -TILE_SIZE * lineSize - TILE_SIZE;
            if (++ty == layer->ysize)
                ty = 0;
        }

        // Remaining pixels on bottom
        {
            tx          = (currentScreen->clipBound_X1 + FROM_FIXED(scanline->position.x)) >> 4;
            sheetX      = (currentScreen->clipBound_X1 + FROM_FIXED(scanline->position.x)) & 0xF;
            tileRemainX = TILE_SIZE - sheetX;
            layout      = &layer->layout[tx + (ty << layer->widthShift)];

            if (*layout != 0xFFFF) {
                frameBuffer += tileRemainX;
            }
            else {
                uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF) + sheetX];

                for (int32 y = 0; y < sheetY; ++y) {
                    for (int32 x = 0; x < tileRemainX; ++x) {
                        if (*pixels)
                            *frameBuffer = activePalette[*pixels];
                        ++pixels;
                        ++frameBuffer;
                    }

                    pixels += sheetX;
                    frameBuffer += currentScreen->pitch - tileRemainX;
                }

                frameBuffer += tileRemainX - currentScreen->pitch * sheetY;
            }
            ++layout;
            if (++tx == layer->xsize) {
                tx = 0;
                layout -= layer->xsize;
            }

            for (int32 x = 0; x < lineSize; ++x) {
                if (*layout == 0xFFFF) {
                    frameBuffer += TILE_SIZE;
                }
                else {
                    uint8 *pixels = &tilesetPixels[TILE_DATASIZE * (*layout & 0xFFF)];
                    for (int32 y = 0; y < sheetY; ++y) {
                        uint8 index = *pixels;
                        if (index)
                            *frameBuffer = activePalette[index];

                        index = pixels[1];
                        if (index)
                            frameBuffer[1] = activePalette[index];

                        index = pixels[2];
                        if (index)
                            frameBuffer[2] = activePalette[index];

                        index = pixels[3];
                        if (index)
                            frameBuffer[3] = activePalette[index];

                        index = pixels[4];
                        if (index)
                            frameBuffer[4] = activePalette[index];

                        index = pixels[5];
                        if (index)
                            frameBuffer[5] = activePalette[index];

                        index = pixels[6];
                        if (index)
                            frameBuffer[6] = activePalette[index];

                        index = pixels[7];
                        if (index)
                            frameBuffer[7] = activePalette[index];

                        index = pixels[8];
                        if (index)
                            frameBuffer[8] = activePalette[index];

                        index = pixels[9];
                        if (index)
                            frameBuffer[9] = activePalette[index];

                        index = pixels[10];
                        if (index)
                            frameBuffer[10] = activePalette[index];

                        index = pixels[11];
                        if (index)
                            frameBuffer[11] = activePalette[index];

                        index = pixels[12];
                        if (index)
                            frameBuffer[12] = activePalette[index];

                        index = pixels[13];
                        if (index)
                            frameBuffer[13] = activePalette[index];

                        index = pixels[14];
                        if (index)
                            frameBuffer[14] = activePalette[index];

                        index = pixels[15];
                        if (index)
                            frameBuffer[15] = activePalette[index];

                        pixels += TILE_SIZE;
                        frameBuffer += currentScreen->pitch;
                    }

                    frameBuffer += TILE_SIZE - currentScreen->pitch * sheetY;
                }

                ++layout;
                if (++tx == layer->xsize) {
                    tx = 0;
                    layout -= layer->xsize;
                }
            }

            if (*layout != 0xFFFF) {
                uint8 *pixels = &tilesetPixels[256 * (*layout & 0xFFF)];

                for (int32 y = 0; y < sheetY; ++y) {
                    for (int32 x = 0; x < sheetX; ++x) {
                        if (*pixels)
                            *frameBuffer = activePalette[*pixels];
                        ++pixels;
                        ++frameBuffer;
                    }

                    pixels += tileRemainX;
                }

                frameBuffer += currentScreen->pitch - sheetX;
            }
        }
    }
}
