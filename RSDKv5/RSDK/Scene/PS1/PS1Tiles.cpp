// PS1 port (docs/32 3.1a): scene tile images (see PS1Tiles.hpp and tools/gfx/build_tiles.py for the file format).
// Included by Scene.cpp.
#include "../../../../ps1/vram.hh"

extern "C" {
volatile uint32 g_ps1TileLoads     = 0;
volatile uint32 g_ps1TileVramBytes = 0;
volatile uint32 g_ps1TileErrors    = 0;
volatile uint32 g_ps1AniTileCalls  = 0;
volatile uint32 g_ps1AniTileMisses = 0;
}

namespace
{
struct PS1TileHeader {
    char magic[4];
    uint16 imageCount, clutCount, aniSheetCount, aniCellCount, blockCount, pad;
    uint32 dataOffset, pixelBytes;
    uint8 pad2[8];
};
struct PS1TileSheet {
    char name[32];
    uint16 cellsW, cellsH, firstCell;
};
struct PS1TileBlock {
    uint16 x, y, w, h;
    uint32 offset;
};

#define PS1_TILE_ANI_SHEETS (4)
uint8 *s_ps1TileBlob = NULL; // STG: tileImage[1024], images, CLUTs, sheets, cells
uint16 s_imageCount = 0, s_clutCount = 0, s_sheetCount = 0, s_cellCount = 0;
uint32 s_imgOff = 0, s_clutOff = 0, s_sheetOff = 0, s_cellOff = 0;
uint32 s_sheetHash[PS1_TILE_ANI_SHEETS][4]; // MD5 of each sheet's name, as LoadSpriteSheet hashes it

inline uint16 *TileTable() { return (uint16 *)s_ps1TileBlob; }
inline const PS1TileSheet *Sheets() { return (const PS1TileSheet *)(s_ps1TileBlob + s_sheetOff); }
inline const uint16 *Cells() { return (const uint16 *)(s_ps1TileBlob + s_cellOff); }
} // namespace

uint16 RSDK::PS1TileImageOf(uint16 tile) { return s_ps1TileBlob && tile < 1024 ? TileTable()[tile] : PS1_TILE_NONE; }
const PS1TileImage *RSDK::PS1TileImages() { return (const PS1TileImage *)(s_ps1TileBlob + s_imgOff); }
const PS1TileClut *RSDK::PS1TileCluts() { return (const PS1TileClut *)(s_ps1TileBlob + s_clutOff); }
uint16 RSDK::PS1TileClutCount() { return s_clutCount; }

bool RSDK::PS1TilesLoad(const char *folder, const char *sceneID)
{
    if (s_ps1TileBlob)
        RemoveStorageEntry((void **)&s_ps1TileBlob);
    s_ps1TileBlob = NULL;
    s_imageCount = s_clutCount = s_sheetCount = s_cellCount = 0;

    char path[0x40];
    sprintf_s(path, sizeof(path), "Data/Stages/%s/Scene%s.ptl", folder, sceneID);
    FileInfo info;
    InitFileInfo(&info);
    if (!LoadFile(&info, path, FMODE_RB))
        return false;

    PS1TileHeader h;
    ReadBytes(&info, &h, sizeof(h));
    uint32 tables = 1024 * 2 + h.imageCount * sizeof(PS1TileImage) + h.clutCount * sizeof(PS1TileClut)
                    + h.aniSheetCount * sizeof(PS1TileSheet) + h.aniCellCount * 2;
    uint32 blocksBytes = h.blockCount * sizeof(PS1TileBlock);
    DataStorage *tmp   = &dataStorage[DATASET_TMP];
    if (memcmp(h.magic, "PTL1", 4) || h.aniSheetCount > PS1_TILE_ANI_SHEETS || tmp->usedStorage
        || blocksBytes + 128 * 2 > tmp->storageLimit || sizeof(h) + tables + blocksBytes > h.dataOffset) {
        g_ps1TileErrors = g_ps1TileErrors + 1;
        CloseFile(&info);
        return false;
    }

    AllocateStorage((void **)&s_ps1TileBlob, tables, DATASET_STG, false);
    ReadBytes(&info, s_ps1TileBlob, tables);
    s_imageCount = h.imageCount, s_clutCount = h.clutCount, s_sheetCount = h.aniSheetCount, s_cellCount = h.aniCellCount;
    s_imgOff     = 1024 * 2;
    s_clutOff    = s_imgOff + s_imageCount * sizeof(PS1TileImage);
    s_sheetOff   = s_clutOff + s_clutCount * sizeof(PS1TileClut);
    s_cellOff    = s_sheetOff + s_sheetCount * sizeof(PS1TileSheet);
    for (int32 s = 0; s < s_sheetCount; ++s) {
        GEN_HASH_MD5(Sheets()[s].name, s_sheetHash[s]); // two statements
    }

    // The blocks' table, then their pixels through DATASET_TMP's memory (empty: LoadSceneAssets just reset it)
    uint8 *stage        = (uint8 *)tmp->memoryTable;
    PS1TileBlock *block = (PS1TileBlock *)stage;
    ReadBytes(&info, block, blocksBytes);
    uint8 *buf       = stage + ((blocksBytes + 3) & ~3u);
    uint32 bufBytes  = (tmp->storageLimit - (uint32)(buf - stage)) & ~3u;
    uint32 uploaded  = 0;
    Seek_Set(&info, h.dataOffset);
    for (int32 b = 0; b < h.blockCount; ++b) {
        uint32 lineBytes = block[b].w * 2;
        uint32 perChunk  = bufBytes / lineBytes;
        for (uint32 line = 0; line < block[b].h; line += perChunk) {
            uint32 n = block[b].h - line < perChunk ? block[b].h - line : perChunk;
            ReadBytes(&info, buf, n * lineBytes);
            PS1VramUpload((const uint16_t *)buf, (int16_t)block[b].x, (int16_t)(block[b].y + line), (int16_t)block[b].w, (int16_t)n);
            uploaded += n * lineBytes;
        }
    }
    CloseFile(&info);
    g_ps1TileVramBytes = uploaded;
    g_ps1TileLoads     = g_ps1TileLoads + 1;
    return true;
}

void RSDK::PS1TilesAniTile(uint16 sheetID, uint16 tileIndex, uint16 srcX, uint16 srcY, uint16 width, uint16 height)
{
    int32 s = 0;
    if (sheetID < SURFACE_COUNT)
        for (; s < s_sheetCount; ++s)
            if (HASH_MATCH_MD5(gfxSurface[sheetID].hash, s_sheetHash[s]))
                break;
    if (!s_ps1TileBlob || s >= s_sheetCount || ((srcX | srcY) & (TILE_SIZE - 1))) {
        g_ps1AniTileMisses = g_ps1AniTileMisses + 1;
        return;
    }
    // DrawAniTile's order: 16x16 cells row by row into consecutive tiles
    const PS1TileSheet *sheet = &Sheets()[s];
    int32 t                   = tileIndex;
    for (int32 fy = 0; fy < height; fy += TILE_SIZE) {
        int32 cy = (srcY + fy) >> 4;
        for (int32 fx = 0; fx < width; fx += TILE_SIZE, ++t) {
            int32 cx = (srcX + fx) >> 4;
            if (t < TILE_COUNT)
                TileTable()[t] = (cx < sheet->cellsW && cy < sheet->cellsH) ? Cells()[sheet->firstCell + cy * sheet->cellsW + cx] : PS1_TILE_NONE;
        }
    }
    g_ps1AniTileCalls = g_ps1AniTileCalls + 1;
}
