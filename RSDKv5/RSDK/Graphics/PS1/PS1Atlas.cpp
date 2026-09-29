// PS1 port (docs/32 3.1c): the scene's sprite atlas (see PS1Atlas.hpp; the file format is tools/gfx/build_atlas.py's).
// Included by Animation.cpp.
#include "../../../../ps1/vram.hh"

extern "C" {
volatile uint32 g_ps1AtlasLoads        = 0;
volatile uint32 g_ps1AtlasVramBytes    = 0;
volatile uint32 g_ps1AtlasErrors       = 0;
volatile uint32 g_ps1AtlasBound        = 0;
volatile uint32 g_ps1AtlasUnplaced     = 0;
volatile uint32 g_ps1AtlasUnknownFiles = 0;
volatile uint32 g_ps1AtlasLateBinds    = 0;
volatile uint32 g_ps1AtlasGroupLoads   = 0;
volatile uint32 g_ps1AtlasGroupErrors  = 0;
volatile int32 g_ps1AtlasGroupReq      = -1;
}

namespace
{
struct PS1AtlasHeader {
    char magic[4];
    uint16 fileCount, clutCount, blockCount, groupCount;
    uint32 bindOffset, bindBytes, dataOffset, pixelBytes;
    uint32 pad;
};
struct PS1AtlasFile {
    uint32 hash[4];
    uint32 bindOffset; // bytes from the header's bindOffset
    uint16 frameCount, pad;
};
struct PS1AtlasBlock {
    uint16 x, y, w, h;
    uint32 offset;
};
struct PS1AtlasGroup {
    char name[8];
    uint8 region, loadedAtStart;
    uint16 blockCount, patchCount, pad;
    uint32 blockTable, patchTable, pixels, pixelBytes; // file offsets (block offsets are from `pixels`)
};
struct PS1AtlasPatch {
    uint16 file, frame;
    uint8 u, v;
    uint16 attr;
};

// STG block: file table, CLUTs, groups, then the sprite slot of each file (0xFF: not loaded)
uint8 *s_ps1AtlasBlob = NULL;
uint8 *s_ps1AtlasBind = NULL; // the STG tail from LoadSceneAssets' end to InitObjects' end
uint16 s_fileCount = 0, s_clutCount = 0, s_groupCount = 0;
uint32 s_clutOff = 0, s_groupOff = 0, s_slotOff = 0;
int8 s_region[2] = { -1, -1 }; // the group each region holds
char s_atlasPath[0x40];
uint32 s_bindOffset = 0, s_bindBytes = 0;

inline PS1AtlasFile *Files() { return (PS1AtlasFile *)s_ps1AtlasBlob; }
inline PS1AtlasGroup *Groups() { return (PS1AtlasGroup *)(s_ps1AtlasBlob + s_groupOff); }
inline uint8 *Slots() { return s_ps1AtlasBlob + s_slotOff; }

// Streams `count` blocks' pixels (table at `table`, offsets from `base`) through `stage` (`stageBytes`) into VRAM.
uint32 PS1AtlasUpload(FileInfo *info, const PS1AtlasBlock *table, uint16 count, uint32 base, uint8 *stage, uint32 stageBytes)
{
    uint32 uploaded = 0;
    for (int32 b = 0; b < count; ++b) {
        uint32 lineBytes = table[b].w * 2;
        uint32 perChunk  = stageBytes / lineBytes;
        Seek_Set(info, base + table[b].offset);
        for (uint32 line = 0; line < table[b].h; line += perChunk) {
            uint32 n = table[b].h - line < perChunk ? table[b].h - line : perChunk;
            ReadBytes(info, stage, n * lineBytes);
            PS1VramUpload((const uint16_t *)stage, (int16_t)table[b].x, (int16_t)(table[b].y + line), (int16_t)table[b].w, (int16_t)n);
            uploaded += n * lineBytes;
        }
    }
    return uploaded;
}

// Applies (or, with clear, undoes) a group's binding patches, read through `stage`.
void PS1AtlasPatches(FileInfo *info, const PS1AtlasGroup *g, bool clear, uint8 *stage, uint32 stageBytes)
{
    uint32 per = stageBytes / sizeof(PS1AtlasPatch);
    Seek_Set(info, g->patchTable);
    for (uint32 p = 0; p < g->patchCount; p += per) {
        uint32 n = g->patchCount - p < per ? g->patchCount - p : per;
        ReadBytes(info, stage, n * sizeof(PS1AtlasPatch));
        const PS1AtlasPatch *pt = (const PS1AtlasPatch *)stage;
        for (uint32 i = 0; i < n; ++i) {
            if (pt[i].file >= s_fileCount || Slots()[pt[i].file] == 0xFF)
                continue;
            SpriteAnimation *spr = &spriteAnimationList[Slots()[pt[i].file]];
            if (!spr->frames || pt[i].frame >= Files()[pt[i].file].frameCount)
                continue;
            SpriteFrame *f = &spr->frames[pt[i].frame];
            f->ps1U        = clear ? 0xFF : pt[i].u;
            f->ps1V        = clear ? 0xFF : pt[i].v;
            f->ps1Attr     = clear ? PS1_ATLAS_MISSING : pt[i].attr;
        }
    }
}
} // namespace

const PS1AtlasClut *RSDK::PS1AtlasCluts() { return s_ps1AtlasBlob ? (const PS1AtlasClut *)(s_ps1AtlasBlob + s_clutOff) : NULL; }
uint16 RSDK::PS1AtlasClutCount() { return s_clutCount; }

void RSDK::PS1AtlasRelease()
{
    if (s_ps1AtlasBlob)
        RemoveStorageEntry((void **)&s_ps1AtlasBlob);
    s_ps1AtlasBlob = NULL;
}

bool RSDK::PS1AtlasLoad(const char *folder, const char *sceneID)
{
    PS1AtlasRelease();
    s_fileCount = s_clutCount = s_groupCount = 0;
    s_region[0] = s_region[1] = -1;
    s_bindBytes                = 0;
    g_ps1AtlasBound = g_ps1AtlasUnplaced = 0;

    sprintf_s(s_atlasPath, sizeof(s_atlasPath), "Data/Stages/%s/Scene%s.atl", folder, sceneID);
    FileInfo info;
    InitFileInfo(&info);
    if (!LoadFile(&info, s_atlasPath, FMODE_RB)) {
        s_atlasPath[0] = 0;
        return false;
    }
    PS1AtlasHeader h;
    ReadBytes(&info, &h, sizeof(h));
    DataStorage *tmp = &dataStorage[DATASET_TMP];
    uint32 tables    = h.fileCount * sizeof(PS1AtlasFile) + h.clutCount * sizeof(PS1AtlasClut) + h.groupCount * sizeof(PS1AtlasGroup);
    uint32 blocks    = h.blockCount * sizeof(PS1AtlasBlock);
    if (memcmp(h.magic, "ATL5", 4) || tmp->usedStorage || blocks + 1024 > tmp->storageLimit) {
        g_ps1AtlasErrors = g_ps1AtlasErrors + 1;
        CloseFile(&info);
        s_atlasPath[0] = 0;
        return false;
    }
    AllocateStorage((void **)&s_ps1AtlasBlob, tables + h.fileCount, DATASET_STG, false);
    // file table and CLUTs are contiguous in the file; the default blocks' table sits between them and the groups
    ReadBytes(&info, s_ps1AtlasBlob, h.fileCount * sizeof(PS1AtlasFile) + h.clutCount * sizeof(PS1AtlasClut));
    uint8 *stage         = (uint8 *)tmp->memoryTable;
    PS1AtlasBlock *table = (PS1AtlasBlock *)stage;
    ReadBytes(&info, table, blocks);
    s_fileCount = h.fileCount, s_clutCount = h.clutCount, s_groupCount = h.groupCount;
    s_clutOff   = h.fileCount * sizeof(PS1AtlasFile);
    s_groupOff  = s_clutOff + h.clutCount * sizeof(PS1AtlasClut);
    s_slotOff   = s_groupOff + h.groupCount * sizeof(PS1AtlasGroup);
    ReadBytes(&info, Groups(), h.groupCount * sizeof(PS1AtlasGroup));
    memset(Slots(), 0xFF, h.fileCount);
    for (int32 g = 0; g < s_groupCount; ++g)
        if (Groups()[g].loadedAtStart && Groups()[g].region < 2)
            s_region[Groups()[g].region] = (int8)g;
    s_bindOffset = h.bindOffset;
    s_bindBytes  = h.bindBytes;

    uint8 *buf         = stage + ((blocks + 3) & ~3u);
    uint32 bufBytes    = (tmp->storageLimit - (uint32)(buf - stage)) & ~3u;
    g_ps1AtlasVramBytes = PS1AtlasUpload(&info, table, h.blockCount, h.dataOffset, buf, bufBytes);
    CloseFile(&info);
    g_ps1AtlasLoads = g_ps1AtlasLoads + 1;
    return true;
}

void RSDK::PS1AtlasLoadBindings()
{
    s_ps1AtlasBind = NULL;
    if (!s_atlasPath[0] || !s_bindBytes)
        return;
    uint8 *t = (uint8 *)PS1StorageTailAlloc(DATASET_STG, s_bindBytes);
    FileInfo info;
    InitFileInfo(&info);
    if (!t || !LoadFile(&info, s_atlasPath, FMODE_RB)) {
        g_ps1AtlasErrors = g_ps1AtlasErrors + 1;
        return;
    }
    Seek_Set(&info, s_bindOffset);
    ReadBytes(&info, t, s_bindBytes);
    CloseFile(&info);
    s_ps1AtlasBind = t;
}

void RSDK::PS1AtlasBind(uint16 sprID)
{
    SpriteAnimation *spr = &spriteAnimationList[sprID];
    int32 frameCount     = 0;
    for (int32 a = 0; a < spr->animCount; ++a) frameCount += spr->animations[a].frameCount;
    for (int32 f = 0; f < frameCount; ++f) {
        spr->frames[f].ps1U = spr->frames[f].ps1V = 0xFF;
        spr->frames[f].ps1Attr                     = PS1_ATLAS_MISSING;
    }
    if (!s_ps1AtlasBlob)
        return;
    int32 fi = 0;
    for (; fi < s_fileCount; ++fi)
        if (HASH_MATCH_MD5(Files()[fi].hash, spr->hash))
            break;
    if (fi >= s_fileCount || Files()[fi].frameCount != frameCount) {
        g_ps1AtlasUnknownFiles = g_ps1AtlasUnknownFiles + 1;
        return;
    }
    Slots()[fi] = (uint8)sprID;
    if (!s_ps1AtlasBind) {
        g_ps1AtlasLateBinds = g_ps1AtlasLateBinds + 1; // loaded after the scene start: phase 3.2 reads its bindings
        return;
    }
    const uint8 *b = s_ps1AtlasBind + Files()[fi].bindOffset;
    for (int32 f = 0; f < frameCount; ++f, b += 4) {
        SpriteFrame *fr = &spr->frames[f];
        fr->ps1U        = b[0];
        fr->ps1V        = b[1];
        fr->ps1Attr     = (uint16)(b[2] | b[3] << 8);
        if (fr->ps1Attr == PS1_ATLAS_MISSING)
            g_ps1AtlasUnplaced = g_ps1AtlasUnplaced + 1;
        else
            g_ps1AtlasBound = g_ps1AtlasBound + 1;
    }
}

void RSDK::PS1AtlasBindEnd()
{
    if (s_ps1AtlasBind)
        PS1StorageTailRelease(DATASET_STG);
    s_ps1AtlasBind = NULL;
}

bool RSDK::PS1AtlasLoadGroup(const char *name)
{
    int32 g = 0;
    for (; g < s_groupCount; ++g)
        if (!strncmp(Groups()[g].name, name, 8))
            break;
    if (!s_ps1AtlasBlob || g >= s_groupCount || Groups()[g].region > 1) {
        g_ps1AtlasGroupErrors = g_ps1AtlasGroupErrors + 1;
        return false;
    }
    int32 r = Groups()[g].region;
    if (s_region[r] == g)
        return true;
    // staging: the STG tail (a group swap happens in play, where DATASET_TMP may be in use)
    uint32 stageBytes = 8 * 1024;
    uint8 *stage      = (uint8 *)PS1StorageTailAlloc(DATASET_STG, stageBytes);
    FileInfo info;
    InitFileInfo(&info);
    if (!stage || !LoadFile(&info, s_atlasPath, FMODE_RB)) {
        if (stage)
            PS1StorageTailRelease(DATASET_STG);
        g_ps1AtlasGroupErrors = g_ps1AtlasGroupErrors + 1;
        return false;
    }
    if (s_region[r] >= 0)
        PS1AtlasPatches(&info, &Groups()[s_region[r]], true, stage, stageBytes); // the old group's frames go missing
    const PS1AtlasGroup *gr = &Groups()[g];
    uint32 tableBytes       = gr->blockCount * sizeof(PS1AtlasBlock);
    Seek_Set(&info, gr->blockTable);
    ReadBytes(&info, stage, tableBytes);
    // the block table stays at the start of the staging area; the rest streams the pixels
    PS1AtlasUpload(&info, (const PS1AtlasBlock *)stage, gr->blockCount, gr->pixels, stage + ((tableBytes + 3) & ~3u),
                   (stageBytes - ((tableBytes + 3) & ~3u)) & ~3u);
    PS1AtlasPatches(&info, gr, false, stage + ((tableBytes + 3) & ~3u), (stageBytes - ((tableBytes + 3) & ~3u)) & ~3u);
    CloseFile(&info);
    PS1StorageTailRelease(DATASET_STG);
    s_region[r]         = (int8)g;
    g_ps1AtlasGroupLoads = g_ps1AtlasGroupLoads + 1;
    return true;
}

void RSDK::PS1AtlasServiceRequests()
{
    int32 g = g_ps1AtlasGroupReq;
    if (g < 0)
        return;
    g_ps1AtlasGroupReq = -1;
    if (s_ps1AtlasBlob && g < s_groupCount) {
        char name[9];
        memcpy(name, Groups()[g].name, 8);
        name[8] = 0;
        PS1AtlasLoadGroup(name);
    }
    else {
        g_ps1AtlasGroupErrors = g_ps1AtlasGroupErrors + 1;
    }
}
