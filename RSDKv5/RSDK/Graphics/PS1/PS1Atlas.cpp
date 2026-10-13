// PS1 port (docs/32 3.1c): the scene's sprite atlas (see PS1Atlas.hpp; the file format is tools/gfx/build_atlas.py's).
// Included by Animation.cpp.
#include "../../../../ps1/vram.hh"
#include "../../../../ps1/loading.hh"

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
volatile uint32 g_ps1AtlasGroupStageFallbacks = 0; // 7b.3c: swaps staged in DATASET_TMP (STG tail short)
volatile int32 g_ps1AtlasGroupReq      = -1;
// docs/32 phase 13 M4: 0x40 while the scene's full-resolution atlas (Scene<N>.atx, 2 MB VRAM) is loaded: binding bit 6 is
// then texture page row 2 / 3 (texpage bit 11), never half resolution (PS1Sprites.cpp, PS1Deform.cpp)
volatile uint32 g_ps1AtlasHiMask = 0;
volatile uint32 g_ps1AtlasAtxLoads = 0;
extern volatile uint32_t g_ps1Extras, g_ps1Vram2MB, g_ps1FullResOff; // ps1/rsdk5_main.cpp (ps1/hwdetect.cpp)
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
char s_ps1TrackReq[32]; // 9i: PS1AtlasRequestTrackGroups' pending track (cleared with the atlas)
uint8 *s_ps1AtlasBind = NULL; // the STG tail from LoadSceneAssets' end to InitObjects' end
uint8 *s_ps1AtlasBindTmp = NULL; // 7b.3c: or a DATASET_TMP block (its owner: a TMP compaction may move it)
bool s_bindInTmp         = false;
uint8 *s_groupStageTmp   = NULL; // 7b.3c: a group swap's staging in DATASET_TMP (when STG's tail is too small)
inline const uint8 *BindData() { return s_bindInTmp ? s_ps1AtlasBindTmp : s_ps1AtlasBind; }
uint16 s_fileCount = 0, s_clutCount = 0, s_groupCount = 0;
uint32 s_clutOff = 0, s_groupOff = 0, s_slotOff = 0;
int8 s_region[2] = { -1, -1 }; // the group each region holds
char s_atlasPath[0x40];
uint32 s_bindOffset = 0, s_bindBytes = 0;
uint32 s_bindOffset2 = 0, s_bindBytes2 = 0; // phase 13: a .atx's character section's bindings (after the common's)
bool s_atx = false;

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

// 7b: the scene's dropped animations (Scene<N>.pda, tools/gfx/build_atlas.py dropped_animations)
uint8 *s_ps1Pda = NULL;
uint32 s_ps1PdaCount = 0;
extern "C" {
volatile uint32 g_ps1AnimDroppedSets = 0;
volatile uint32 g_ps1AnimDroppedFrames = 0; // (the frames the last load did not store)
}

const uint8 *RSDK::PS1AtlasDropped(const uint32 *hash)
{
    const uint8 *e = s_ps1Pda;
    for (uint32 i = 0; e && i < s_ps1PdaCount; ++i) {
        uint16 n;
        memcpy(&n, e + 18, 2);
        if (!memcmp(e, hash, 16))
            return e;
        e += 20 + 4 * n;
    }
    return NULL;
}

void RSDK::PS1AtlasRelease()
{
    if (s_ps1AtlasBlob)
        RemoveStorageEntry((void **)&s_ps1AtlasBlob);
    s_ps1TrackReq[0] = 0;
    s_ps1AtlasBlob = NULL;
    if (s_ps1Pda)
        RemoveStorageEntry((void **)&s_ps1Pda);
    s_ps1Pda      = NULL;
    s_ps1PdaCount = 0;
}

// 9b.1: a zone's scene has an atlas per character (tools/gfx/build_atlas.py: Scene<N>.atl Sonic's, Scene<N>_tails / _knux
// the others', each with its .pda); a presentation scene has one for all. The character: the game's globals->playerID
// (its first character: the low byte), at the offset SonicMania/Objects/Global/Zone.c exports
extern "C" const int32 ps1GlobalsPlayerIDOffset;
void PS1FsPin(const char *path); // ps1/cdrom_fs.cpp: the file stays found after the load (group swaps reopen it)
static const char *PS1AtlasCharSuffix()
{
    if (!globalVarsPtr)
        return "";
    int32 id = *(const int32 *)((const uint8 *)globalVarsPtr + ps1GlobalsPlayerIDOffset) & 0xFF;
    return id == (1 << 1) ? "_tails" : id == (1 << 2) ? "_knux" : id == (1 << 3) ? "_mighty" : id == (1 << 4) ? "_ray" : "";
    // ID_TAILS, ID_KNUCKLES, ID_MIGHTY, ID_RAY (GameVariables.h; 9g: Mighty / Ray with the Plus characters setting)
}
// 9g: the SFX bank variant's suffix: Mighty's / Ray's own sounds (the Plus characters setting), else none
extern "C" volatile int g_ps1PlusChars; // ps1/rsdk5_main.cpp
const char *RSDK::PS1CharSfxSuffix()
{
    if (!g_ps1PlusChars || !globalVarsPtr)
        return "";
    int32 id = *(const int32 *)((const uint8 *)globalVarsPtr + ps1GlobalsPlayerIDOffset) & 0xFF;
    return id == (1 << 3) ? "_mighty" : id == (1 << 4) ? "_ray" : "";
}
// The scene's file `ext` for the character, else the scene's one (Sonic's, or every character's); its path in s_atlasPath
static bool PS1AtlasOpen(FileInfo *info, const char *folder, const char *sceneID, const char *ext)
{
    const char *suf = PS1AtlasCharSuffix();
    if (*suf) {
        sprintf_s(s_atlasPath, sizeof(s_atlasPath), "Data/Stages/%s/Scene%s%s.%s", folder, sceneID, suf, ext);
        InitFileInfo(info);
        if (LoadFile(info, s_atlasPath, FMODE_RB)) {
            PS1FsPin(s_atlasPath); // (only in the loading folder's table: tools/disc/build_disc.py local_entry)
            return true;
        }
    }
    sprintf_s(s_atlasPath, sizeof(s_atlasPath), "Data/Stages/%s/Scene%s.%s", folder, sceneID, ext);
    InitFileInfo(info);
    return LoadFile(info, s_atlasPath, FMODE_RB);
}

// docs/32 phase 13 M4: Scene<N>.atx (tools/gfx/build_atlas.py build_atx): 'ATX1', u16 sections, pad, then per section
// u32 offset, bytes: a common ATL5 section (the sprites every character's scene loads) and one per character
// (build_atlas ATX_CHARS: Sonic, Tails, Knuckles, Mighty, Ray), all resident, CLUT slots after the common's. Both are
// joined here into the stock tables: files and CLUTs one after the other, the character's bindings after the common's.
namespace RSDK
{
static bool PS1AtlasLoadAtx(const char *folder, const char *sceneID)
{
    const char *suf = PS1AtlasCharSuffix();
    uint32 ch = !*suf ? 0 : suf[1] == 't' ? 1 : suf[1] == 'k' ? 2 : suf[1] == 'm' ? 3 : 4;
    sprintf_s(s_atlasPath, sizeof(s_atlasPath), "Data/Stages/%s/Scene%s.atx", folder, sceneID);
    FileInfo info;
    InitFileInfo(&info);
    if (!LoadFile(&info, s_atlasPath, FMODE_RB))
        return false;
    uint32 head[2 + 2 * 6];
    ReadBytes(&info, head, sizeof(head));
    uint32 n = head[1] & 0xFFFF;
    PS1AtlasHeader h[2];
    uint32 off[2] = { head[2], ch + 1 < n ? head[2 + 2 * (ch + 1)] : 0 };
    DataStorage *tmp = &dataStorage[DATASET_TMP];
    bool ok = head[0] == ('A' | 'T' << 8 | 'X' << 16 | (uint32)'1' << 24) && n <= 6 && off[0] && off[1] && !tmp->usedStorage;
    for (int32 k = 0; ok && k < 2; ++k) {
        Seek_Set(&info, off[k]);
        ReadBytes(&info, &h[k], sizeof(h[k]));
        ok = !memcmp(h[k].magic, "ATL5", 4) && !h[k].groupCount && h[k].blockCount * sizeof(PS1AtlasBlock) + 1024 <= tmp->storageLimit;
    }
    if (!ok) {
        g_ps1AtlasErrors = g_ps1AtlasErrors + 1;
        CloseFile(&info);
        return false;
    }
    uint32 files = h[0].fileCount + h[1].fileCount, cluts = h[0].clutCount + h[1].clutCount;
    AllocateStorage((void **)&s_ps1AtlasBlob, files * sizeof(PS1AtlasFile) + cluts * sizeof(PS1AtlasClut) + files, DATASET_STG, false);
    s_fileCount = (uint16)files, s_clutCount = (uint16)cluts, s_groupCount = 0;
    s_clutOff = files * sizeof(PS1AtlasFile);
    s_groupOff = s_slotOff = s_clutOff + cluts * sizeof(PS1AtlasClut);
    memset(Slots(), 0xFF, files);
    uint8 *stage = (uint8 *)tmp->memoryTable;
    g_ps1AtlasVramBytes = 0;
    for (int32 k = 0; k < 2; ++k) {
        uint32 f0 = k ? h[0].fileCount : 0, c0 = k ? h[0].clutCount : 0;
        Seek_Set(&info, off[k] + sizeof(PS1AtlasHeader));
        ReadBytes(&info, Files() + f0, h[k].fileCount * sizeof(PS1AtlasFile));
        ReadBytes(&info, (PS1AtlasClut *)(s_ps1AtlasBlob + s_clutOff) + c0, h[k].clutCount * sizeof(PS1AtlasClut));
        if (k)
            for (uint32 i = 0; i < h[1].fileCount; ++i) Files()[f0 + i].bindOffset += h[0].bindBytes;
        uint32 blocks = h[k].blockCount * sizeof(PS1AtlasBlock);
        ReadBytes(&info, stage, blocks);
        uint8 *buf = stage + ((blocks + 3) & ~3u);
        g_ps1AtlasVramBytes += PS1AtlasUpload(&info, (PS1AtlasBlock *)stage, h[k].blockCount, off[k] + h[k].dataOffset, buf,
                                              (tmp->storageLimit - (uint32)(buf - stage)) & ~3u);
    }
    CloseFile(&info);
    s_bindOffset = off[0] + h[0].bindOffset, s_bindBytes = h[0].bindBytes;
    s_bindOffset2 = off[1] + h[1].bindOffset, s_bindBytes2 = h[1].bindBytes;
    s_atx = true;
    g_ps1AtlasHiMask = 0x40;
    g_ps1AtlasLoads = g_ps1AtlasLoads + 1;
    g_ps1AtlasAtxLoads = g_ps1AtlasAtxLoads + 1;
    return true;
}
} // namespace RSDK

bool RSDK::PS1AtlasLoad(const char *folder, const char *sceneID)
{
    PS1AtlasRelease();
    s_atx = false, g_ps1AtlasHiMask = 0, s_bindBytes2 = 0;
    s_fileCount = s_clutCount = s_groupCount = 0;
    s_region[0] = s_region[1] = -1;
    s_bindBytes                = 0;
    g_ps1AtlasBound = g_ps1AtlasUnplaced = 0;

    FileInfo info;
    // 7b: the dropped animations first (kept until the next load: LoadSpriteAnimation reads them)
    if (PS1AtlasOpen(&info, folder, sceneID, "pda")) {
        uint32 head[2];
        ReadBytes(&info, head, sizeof(head));
        uint32 bytes = (uint32)info.fileSize - sizeof(head);
        if (head[0] == ('P' | 'D' << 8 | 'A' << 16 | (uint32)'1' << 24) && bytes) {
            AllocateStorage((void **)&s_ps1Pda, bytes, DATASET_STG, false);
            ReadBytes(&info, s_ps1Pda, bytes);
            s_ps1PdaCount = head[1];
        }
        CloseFile(&info);
    }
    if (PS1_FULL_RES() && PS1AtlasLoadAtx(folder, sceneID)) { // (13b: unless Options > Video > Full-res sprites is Off)
        s_ps1PdaCount = 0; // (its bindings cover every frame: no animation dropped; frames in the Mods arena, M5)
        return true;       // phase 13: the full-resolution atlas (the stock one when the scene has none)
    }
    if (!PS1AtlasOpen(&info, folder, sceneID, "atl")) {
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
    s_bindInTmp    = false;
    if (!s_atlasPath[0] || !s_bindBytes)
        return;
    // 7b.3c: in DATASET_TMP (free while the StageLoads run: the atlas staging is done), else STG's tail: the load's STG
    // peak comes with the StageLoads (FBZ2: 9.7 KB of bindings)
    uint8 *t = NULL;
    uint32 total = s_bindBytes + s_bindBytes2; // (phase 13: a .atx's two ranges, one after the other)
    PS1TryAllocateStorage((void **)&s_ps1AtlasBindTmp, total, DATASET_TMP, false);
    if (s_ps1AtlasBindTmp)
        t = s_ps1AtlasBindTmp, s_bindInTmp = true;
    else
        t = (uint8 *)PS1StorageTailAlloc(DATASET_STG, total);
    FileInfo info;
    InitFileInfo(&info);
    if (!t || !LoadFile(&info, s_atlasPath, FMODE_RB)) {
        g_ps1AtlasErrors = g_ps1AtlasErrors + 1;
        return;
    }
    Seek_Set(&info, s_bindOffset);
    ReadBytes(&info, t, s_bindBytes);
    if (s_bindBytes2) {
        Seek_Set(&info, s_bindOffset2);
        ReadBytes(&info, t + s_bindBytes, s_bindBytes2);
    }
    CloseFile(&info);
    s_ps1AtlasBind = t;
}

// 7.5: a sheet DrawDeformedSprite draws (the atlas's file entry with its path's md5 and one binding): u | v << 8 |
// attr << 16, 0xFFFFFFFF when the atlas has none (LoadSpriteSheet, while the bindings are loaded)
uint32 RSDK::PS1AtlasSheetBinding(const uint32 *hash)
{
    if (!s_ps1AtlasBlob || !s_ps1AtlasBind)
        return 0xFFFFFFFF;
    for (int32 fi = 0; fi < s_fileCount; ++fi)
        if (Files()[fi].frameCount == 1 && HASH_MATCH_MD5(Files()[fi].hash, hash)) {
            const uint8 *b = BindData() + Files()[fi].bindOffset;
            return (uint32)b[0] | (uint32)b[1] << 8 | (uint32)(b[2] | b[3] << 8) << 16;
        }
    return 0xFFFFFFFF;
}

void RSDK::PS1AtlasBind(uint16 sprID)
{
    SpriteAnimation *spr = &spriteAnimationList[sprID];
    int32 frameCount     = spr->ps1Frames; // 7b: the stored frames (a dropped animation's placeholder last)
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
    const uint8 *b = BindData() + Files()[fi].bindOffset;
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
    if (s_bindInTmp)
        RemoveStorageEntry((void **)&s_ps1AtlasBindTmp);
    else if (s_ps1AtlasBind)
        PS1StorageTailRelease(DATASET_STG);
    s_ps1AtlasBind = NULL;
    s_bindInTmp    = false;
}

bool RSDK::PS1AtlasLoadGroup(const char *name)
{
    if (s_atx)
        return true; // phase 13: a .atx holds every group at once
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
    // 9i (user 2026-10-09): a boss's group can take a second of CD reads (HCZ1): the loading icon (shown only past
    // PS1_LOADING_DELAY_US, so a short swap shows nothing)
    struct LoadingBracket {
        LoadingBracket() { PS1LoadingBegin(); }
        ~LoadingBracket() { PS1LoadingEnd(); }
    } bracket;
    // staging: the STG tail (a group swap happens in play, where DATASET_TMP may be in use); 7b.3c: a zone that leaves
    // less STG free (FBZ2: ~7 KB) stages in a TMP block, else in a 4 KB tail (the pixels stream through it)
    uint32 stageBytes = 8 * 1024;
    uint8 *stage      = (uint8 *)PS1StorageTailAlloc(DATASET_STG, stageBytes);
    uint8 *stageTmp   = NULL;
    if (!stage) {
        PS1TryAllocateStorage((void **)&s_groupStageTmp, stageBytes, DATASET_TMP, false);
        stageTmp = stage = s_groupStageTmp;
        g_ps1AtlasGroupStageFallbacks = g_ps1AtlasGroupStageFallbacks + 1;
    }
    if (!stage) {
        stageBytes = 4 * 1024;
        stage      = (uint8 *)PS1StorageTailAlloc(DATASET_STG, stageBytes);
    }
    FileInfo info;
    InitFileInfo(&info);
    if (!stage || !LoadFile(&info, s_atlasPath, FMODE_RB)) {
        if (stageTmp)
            RemoveStorageEntry((void **)&s_groupStageTmp);
        else if (stage)
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
    if (stageTmp)
        RemoveStorageEntry((void **)&s_groupStageTmp);
    else
        PS1StorageTailRelease(DATASET_STG);
    s_region[r]         = (int8)g;
    g_ps1AtlasGroupLoads = g_ps1AtlasGroupLoads + 1;
    return true;
}

// 7b: the swaps docs/32 3.1 planned "where the music changes", keyed by the track a PlayStream starts (its old track
// stopped: the CD is free for the group's read): a boss track (and the Heavies' theme) brings the boss group, the act
// clear the end group, Super's track Super's frames; a jingle (1-up, power-ups, drowning, game over) changes nothing;
// any other track (the stage's, again after Super) the normal frames. A group the scene's atlas lacks is no error.
// 9l (user 2026-10-09: Super Sonic drawn transparent in SSZ1's and Metal Sonic's fights): the player region's group the
// game needs: Super Sonic's frames while a Sonic draws from SuperSonic.bin past the transformation (Player.c; its
// ANI_TRANSFORM frames are with the normal ones), else the normal frames
extern "C" int32 PS1GameWantsSuperGroup(void) __attribute__((weak));
namespace
{
const char *PS1PlayerGroupName() { return (PS1GameWantsSuperGroup && PS1GameWantsSuperGroup()) ? "super" : "normal"; }
int32 PS1FindGroup(const char *name)
{
    for (int32 g = 0; g < s_groupCount; ++g)
        if (!strncmp(Groups()[g].name, name, 8))
            return g;
    return -1;
}
void PS1LoadGroupIfAny(const char *name)
{
    if (PS1FindGroup(name) >= 0)
        PS1AtlasLoadGroup(name);
}
} // namespace

// Each frame between the updates and the draws (after the music's groups): the player region follows the game when it
// changes without a music change (a boss track already playing when Super starts or ends, Super's music off, a
// respawn). A swap outside a music change stops the XA for the read; the stream resumes where it was (PS1StreamFrame)
// 9i: a track's groups asked for before the track starts (Music_TransitionTrack: the boss on screen during the old
// track's fade), loaded at the next PS1AtlasPlayerGroup (between the updates and the draws); the track's own start
// finds them loaded
extern "C" void PS1AtlasRequestTrackGroups(const char *track)
{
    if (!track)
        return;
    strncpy(s_ps1TrackReq, track, sizeof(s_ps1TrackReq) - 1);
    s_ps1TrackReq[sizeof(s_ps1TrackReq) - 1] = 0;
}

void RSDK::PS1AtlasPlayerGroup()
{
    if (s_ps1TrackReq[0]) {
        char name[sizeof(s_ps1TrackReq)];
        memcpy(name, s_ps1TrackReq, sizeof(name));
        s_ps1TrackReq[0] = 0;
        PS1AtlasMusicGroups(name);
    }
    if (!s_ps1AtlasBlob || !PS1GameWantsSuperGroup)
        return;
    const char *name = PS1PlayerGroupName();
    int32 g          = PS1FindGroup(name);
    if (g >= 0 && Groups()[g].region <= 1 && s_region[Groups()[g].region] != g)
        PS1AtlasLoadGroup(name);
}

void RSDK::PS1AtlasMusicGroups(const char *track)
{
    if (!s_ps1AtlasBlob || !track)
        return;
    static const char *const jingles[] = { "1up.ogg", "Invincible.ogg", "Sneakers.ogg", "Drowning.ogg", "GameOver.ogg" };
    for (const char *j : jingles)
        if (!strcmp(track, j))
            return;
    auto load = PS1LoadGroupIfAny;
    if (!strcmp(track, "Super.ogg")) {
        load("super");
        return;
    }
    if (!strcmp(track, "ActClear.ogg")) {
        load("end");
        return;
    }
    if (!strncmp(track, "Boss", 4) || !strcmp(track, "MetalSonic.ogg") || !strcmp(track, "HBHMischief.ogg"))
        load("boss");
    load(PS1PlayerGroupName()); // 9l: Super Sonic keeps his frames under a boss track
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
