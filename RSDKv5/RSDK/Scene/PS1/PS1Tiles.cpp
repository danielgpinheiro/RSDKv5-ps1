// PS1 port (docs/ports/mania/32 3.1a): scene tile images (see PS1Tiles.hpp and tools/gfx/build_tiles.py for the file format).
// Included by Scene.cpp.
#include "../../../../ps1/vram.hh"

extern "C" {
volatile uint32 g_ps1TileLoads     = 0;
volatile uint32 g_ps1TileVramBytes = 0;
volatile uint32 g_ps1TileErrors    = 0;
volatile uint32 g_ps1AniTileCalls  = 0;
volatile uint32 g_ps1AniTileMisses = 0;
volatile uint32 g_ps1SolidLines    = 0;
volatile uint32 g_ps1SolidDrops    = 0;
volatile uint32 g_ps1HFlipDrops    = 0;
volatile uint32 g_ps1RotoPageDrops = 0;
}

namespace
{
struct PS1TileHeader {
    char magic[4];
    uint16 imageCount, clutCount, aniSheetCount, aniCellCount, blockCount, solidBytes;
    uint32 dataOffset, pixelBytes;
    uint16 hflipCount; // (tile, image) pairs: the H-flipped copies of the per-line rows' tiles (3.3b)
    uint16 lodFirst;   // 7.4: a UFO floor's minimap: its first image (0: none) and pages across / down
    uint8 lodPagesW, lodPagesH;
    uint16 patternImage; // 7.5: a "3D Floor" / "3D Roof" layer's 256x256 texture (0: none)
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
uint8 *s_ps1TileBlob = NULL; // STG: tileImage[1024], images, CLUTs, sheets, cells, one-colour lines
uint16 s_imageCount = 0, s_clutCount = 0, s_sheetCount = 0, s_cellCount = 0;
uint32 s_imgOff = 0, s_clutOff = 0, s_sheetOff = 0, s_cellOff = 0;
// one-colour lines (3.3b): each layer's table (blob offset, 0 = none) and height, the mask of the tiles on them
uint32 s_solidOff[LAYER_COUNT];
uint16 s_solidHeight[LAYER_COUNT];
uint32 s_solidMaskOff = 0;
uint32 s_hflipOff = 0, s_hflipCount = 0; // (tile, image) pairs, sorted by tile
bool s_flipAll = false; // 9f: the scene stores every flipped tile's copy (V / HV keys: the pairs' last is >= 0x800)
uint32 s_lodInfo = 0;                    // 7.4: lodFirst | pagesW << 16 | pagesH << 24 (0: no minimap)
uint16 s_patternImage = 0;               // 7.5: the 3D floor / roof texture's image (0: none)
uint32 s_sheetHash[PS1_TILE_ANI_SHEETS][4]; // MD5 of each sheet's name, as LoadSpriteSheet hashes it
// 9e (PTL3): rotozoom layers drawn from a 256x256 page: layer index, the page's image, its origin in the map (texels)
struct PS1RotoPageRec {
    uint8 layer, on; // on: bit 0 the file's flag (9u.18: the map repeats the page), bit 7 drawn from the page
    uint16 image, ox, oy;
};
#define PS1_TILE_ROTO_PAGES (4)
PS1RotoPageRec s_rotoPage[PS1_TILE_ROTO_PAGES];
#define PS1_TILE_STRIPS 3
PS1TileStripRec s_strip[PS1_TILE_STRIPS]; // ps1-ufo (PTL5)
uint16 s_stripClutFirst = 0;
uint8 s_stripCount      = 0;
bool s_stripOn          = false;
} // namespace
extern "C" volatile int32 g_ps1StripOn;
volatile int32 g_ps1StripOn = 1; // 0: the strip unused, every band from the tiles (GDB: A/B pictures)
namespace
{
} // namespace
namespace
{
uint32 s_rotoPageCount = 0;

inline uint16 *TileTable() { return (uint16 *)s_ps1TileBlob; }
inline const PS1TileSheet *Sheets() { return (const PS1TileSheet *)(s_ps1TileBlob + s_sheetOff); }
inline const uint16 *Cells() { return (const uint16 *)(s_ps1TileBlob + s_cellOff); }
} // namespace

uint16 RSDK::PS1TileImageOf(uint16 tile) { return s_ps1TileBlob && tile < 1024 ? TileTable()[tile] : PS1_TILE_NONE; }
bool RSDK::PS1TileFlipCopiesAll() { return s_ps1TileBlob && s_flipAll; }
const uint16 *RSDK::PS1TileImageTable() { return s_ps1TileBlob ? TileTable() : NULL; }
uint32 RSDK::PS1TileLodInfo() { return s_ps1TileBlob ? s_lodInfo : 0; }
uint16 RSDK::PS1TilePatternImage() { return s_ps1TileBlob ? s_patternImage : 0; }
const PS1TileImage *RSDK::PS1TileImages() { return (const PS1TileImage *)(s_ps1TileBlob + s_imgOff); }
const PS1TileClut *RSDK::PS1TileCluts() { return (const PS1TileClut *)(s_ps1TileBlob + s_clutOff); }
uint16 RSDK::PS1TileClutCount() { return s_clutCount; }
const PS1TileStripRec *RSDK::PS1TileStrip(int32 layer)
{
    if (!s_ps1TileBlob || !s_stripOn || !g_ps1StripOn)
        return NULL;
    for (uint32 i = 0; i < s_stripCount; ++i)
        if (s_strip[i].layer == layer)
            return &s_strip[i];
    return NULL;
}
uint16 RSDK::PS1TileStripClutFirst() { return s_ps1TileBlob && s_stripOn ? s_stripClutFirst : s_clutCount; }
#if PS1_DEBUG_OVERLAY
// ps1/debug_overlay.cpp: the scene's tile pixels in VRAM (0 without a .ptl: the counter keeps the last load's)
uint32 RSDK::PS1TilesVramBytes() { return s_imageCount ? g_ps1TileVramBytes : 0; }
#endif

// The pairs' answers cached per tile (3.3g: the column tables ask for every H-flipped column; per-line rows show a few
// tiles): tile << 16 | image, direct-mapped (PS1Tiles.hpp: the test inline); cleared when the pairs load or change
uint32 RSDK::ps1HFlipCache[64];
void PS1HFlipCacheClear()
{
    for (int32 i = 0; i < 64; ++i) ps1HFlipCache[i] = 0xFFFFFFFF;
}

uint16 RSDK::PS1TileHFlipImageMiss(uint16 tile)
{
    uint32 &c = ps1HFlipCache[(tile ^ tile >> 6) & 63]; // (9f: keys carry the V flips in bits 10-11)
    // 4.2e: the pairs are sorted by tile: a binary search (the linear one cost ~60 hblanks a frame in the ruby's
    // per-line deformation, which misses the cache)
    uint16 img         = PS1_TILE_NONE;
    const uint16 *pair = (const uint16 *)(s_ps1TileBlob + s_hflipOff);
    uint32 lo = 0, hi = s_hflipCount; // the first pair with pair[0] >= tile is in [lo, hi]
    while (lo < hi) {
        uint32 mid = (lo + hi) >> 1;
        if (pair[mid * 2] < tile)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < s_hflipCount && pair[lo * 2] == tile)
        img = pair[lo * 2 + 1];
    c = (uint32)tile << 16 | img;
    return img;
}

const uint8 *RSDK::PS1TileSolidLines(int32 layer, int32 heightPx)
{
    if (!s_ps1TileBlob || (uint32)layer >= LAYER_COUNT || !s_solidOff[layer] || s_solidHeight[layer] != heightPx)
        return NULL;
    return s_ps1TileBlob + s_solidOff[layer];
}

bool RSDK::PS1TileRotoPage(int32 layer, PS1TileImage *image, int32 *ox, int32 *oy)
{
    for (uint32 i = 0; s_ps1TileBlob && i < s_rotoPageCount; ++i) {
        const PS1RotoPageRec &p = s_rotoPage[i];
        if (p.layer == layer && (p.on & 0x80)) {
            *image = PS1TileImages()[p.image], *ox = p.ox | (p.on & 1) << 16, *oy = p.oy; // (bit 16: periodic)
            return true;
        }
    }
    return false;
}

void RSDK::PS1TileSolidDrop(int32 layer)
{
    // 9e: a page shows the file's layout too: SetTile on its layer turns it off (DrawAniTile's -1 never touches a page:
    // the paged folders have no animated-tile sheets, build_tiles' ANI_SHEETS)
    for (uint32 i = 0; i < s_rotoPageCount; ++i) {
        if (s_rotoPage[i].layer == layer && (s_rotoPage[i].on & 0x80)) {
            s_rotoPage[i].on   = 0;
            g_ps1RotoPageDrops = g_ps1RotoPageDrops + 1;
        }
    }
    for (int32 l = 0; l < LAYER_COUNT; ++l) {
        if ((layer < 0 || l == layer) && s_solidOff[l]) {
            s_solidOff[l]   = 0;
            g_ps1SolidDrops = g_ps1SolidDrops + 1;
        }
    }
}

void RSDK::PS1TilesRelease()
{
    if (s_ps1TileBlob)
        RemoveStorageEntry((void **)&s_ps1TileBlob);
    s_ps1TileBlob = NULL;
}

bool RSDK::PS1TilesLoad(const char *folder, const char *sceneID)
{
    PS1TilesRelease();
    s_imageCount = s_clutCount = s_sheetCount = s_cellCount = 0;
    memset(s_solidOff, 0, sizeof(s_solidOff));
    s_solidMaskOff  = 0;
    g_ps1SolidLines = 0;
    s_hflipOff = s_hflipCount = 0;
    s_rotoPageCount = 0;
    s_stripOn       = false;
    PS1HFlipCacheClear();

    char path[0x40];
    sprintf_s(path, sizeof(path), "Data/Stages/%s/Scene%s.ptl", folder, sceneID);
    FileInfo info;
    InitFileInfo(&info);
    if (!LoadFile(&info, path, FMODE_RB))
        return false;

    PS1TileHeader h;
    ReadBytes(&info, &h, sizeof(h));
    uint32 tables = 1024 * 2 + h.imageCount * sizeof(PS1TileImage) + h.clutCount * sizeof(PS1TileClut)
                    + h.aniSheetCount * sizeof(PS1TileSheet) + h.aniCellCount * 2 + h.solidBytes + h.hflipCount * 4;
    uint32 blocksBytes = h.blockCount * sizeof(PS1TileBlock);
    DataStorage *tmp   = &dataStorage[DATASET_TMP];
    const bool v5      = !memcmp(h.magic, "PTL5", 4); // ps1-ufo: PTL4 + a strip
    const bool v4      = v5 || !memcmp(h.magic, "PTL4", 4); // 9v.3: PTL3 (the table always there) + the tiles' opaque bits
    bool v3            = v4 || !memcmp(h.magic, "PTL3", 4); // PTL2 + the rotozoom pages after the H-flip pairs (9e)
    if ((!v3 && memcmp(h.magic, "PTL2", 4)) || h.aniSheetCount > PS1_TILE_ANI_SHEETS || tmp->usedStorage
        || blocksBytes + 128 * 2 > tmp->storageLimit || sizeof(h) + tables + blocksBytes > h.dataOffset) {
        g_ps1TileErrors = g_ps1TileErrors + 1;
        CloseFile(&info);
        return false;
    }

    AllocateStorage((void **)&s_ps1TileBlob, tables, DATASET_STG, false);
    ReadBytes(&info, s_ps1TileBlob, tables);
    if (v3) { // u16 count, u16 0, then (u8 layer, u8 0, u16 image, u16 ox, u16 oy) each
        uint16 rc[2];
        ReadBytes(&info, rc, 4);
        for (uint32 i = 0; i < rc[0]; ++i) {
            PS1RotoPageRec p;
            ReadBytes(&info, &p, sizeof(p));
            if (s_rotoPageCount < PS1_TILE_ROTO_PAGES && p.image < h.imageCount && p.layer < LAYER_COUNT) {
                p.on                          = (p.on & 1) | 0x80;
                s_rotoPage[s_rotoPageCount++] = p;
            }
            else {
                g_ps1TileErrors = g_ps1TileErrors + 1;
            }
        }
    }
    if (v4)
        Seek_Cur(&info, 128); // 9v.3's opaque bits (u8[128], bit t: tile t's pixels all opaque): no runtime user yet
    if (v5) { // u16 count (1-3), u16 clutFirst, the records
        uint16 sc[2];
        ReadBytes(&info, sc, 4);
        s_stripClutFirst = sc[1];
        s_stripCount     = (uint8)(sc[0] <= PS1_TILE_STRIPS ? sc[0] : 0);
        s_stripOn        = s_stripCount && sc[1] <= h.clutCount;
        if (s_stripCount)
            ReadBytes(&info, s_strip, sizeof(PS1TileStripRec) * s_stripCount);
        for (uint32 i = 0; i < s_stripCount; ++i) {
            const PS1TileStripRec &st = s_strip[i];
            s_stripOn = s_stripOn && st.layer < LAYER_COUNT && st.bands <= 16 && st.width && !(st.width & 255) && st.width <= 1024
                        && !(st.width & (st.width - 1));
        }
        g_ps1TileErrors = g_ps1TileErrors + !s_stripOn;
    }
    s_imageCount = h.imageCount, s_clutCount = h.clutCount, s_sheetCount = h.aniSheetCount, s_cellCount = h.aniCellCount;
    s_imgOff     = 1024 * 2;
    s_clutOff    = s_imgOff + s_imageCount * sizeof(PS1TileImage);
    s_sheetOff   = s_clutOff + s_clutCount * sizeof(PS1TileClut);
    s_cellOff    = s_sheetOff + s_sheetCount * sizeof(PS1TileSheet);
    for (int32 s = 0; s < s_sheetCount; ++s) {
        GEN_HASH_MD5(Sheets()[s].name, s_sheetHash[s]); // two statements
    }
    if (h.solidBytes) {
        // u8 layerCount, u8 0, u16 height[layerCount], the tables (each padded to even), u8 tileMask[128]
        uint32 base         = s_cellOff + s_cellCount * 2;
        uint32 count        = s_ps1TileBlob[base];
        const uint16 *hgt   = (const uint16 *)(s_ps1TileBlob + base + 2);
        uint32 off = base + 2 + count * 2, lines = 0;
        for (uint32 l = 0; l < count; ++l) {
            if (!hgt[l])
                continue;
            if (l < LAYER_COUNT) {
                s_solidOff[l]    = off;
                s_solidHeight[l] = hgt[l];
                for (uint32 y = 0; y < hgt[l]; ++y) lines += s_ps1TileBlob[off + y] != 0;
            }
            off += (hgt[l] + 1) & ~1u;
        }
        s_solidMaskOff = off;
        if (off + 128 != base + h.solidBytes) { // a table the reader does not understand: tiles everywhere
            memset(s_solidOff, 0, sizeof(s_solidOff));
            s_solidMaskOff = 0, lines = 0;
            g_ps1TileErrors = g_ps1TileErrors + 1;
        }
        g_ps1SolidLines = lines;
    }
    s_hflipOff   = s_cellOff + s_cellCount * 2 + h.solidBytes;
    s_hflipCount = h.hflipCount;
    s_flipAll    = h.hflipCount && ((const uint16 *)(s_ps1TileBlob + s_hflipOff))[(h.hflipCount - 1) * 2] >= 0x800; // 9f
    s_lodInfo    = h.lodFirst && h.lodFirst < h.imageCount ? h.lodFirst | (uint32)h.lodPagesW << 16 | (uint32)h.lodPagesH << 24 : 0;
    s_patternImage = h.patternImage < h.imageCount ? h.patternImage : 0;
    PS1HFlipCacheClear();

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
            if (t < TILE_COUNT) {
                TileTable()[t] = (cx < sheet->cellsW && cy < sheet->cellsH) ? Cells()[sheet->firstCell + cy * sheet->cellsW + cx] : PS1_TILE_NONE;
                if (s_solidMaskOff && (s_ps1TileBlob[s_solidMaskOff + (t >> 3)] >> (t & 7) & 1))
                    PS1TileSolidDrop(-1); // a tile on a one-colour line changed its picture
                uint16 *pair = (uint16 *)(s_ps1TileBlob + s_hflipOff);
                for (uint32 i = 0; i < s_hflipCount; ++i, pair += 2) {
                    if (pair[0] == t) { // its H-flipped copy shows the old picture: tiles + quads from now on
                        pair[0]         = 0xFFFF;
                        g_ps1HFlipDrops = g_ps1HFlipDrops + 1;
                        PS1HFlipCacheClear();
                    }
                }
            }
        }
    }
    g_ps1AniTileCalls = g_ps1AniTileCalls + 1;
}
