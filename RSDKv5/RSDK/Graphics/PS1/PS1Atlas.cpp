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
uint8 *s_ps1AtlasBindTmp = NULL; // 7b.3c: or a DATASET_TMP block (its owner: a TMP compaction may move it)
bool s_bindInTmp         = false;
uint8 *s_groupStageTmp   = NULL; // 7b.3c: a group swap's staging in DATASET_TMP (when STG's tail is too small)
inline const uint8 *BindData() { return s_bindInTmp ? s_ps1AtlasBindTmp : s_ps1AtlasBind; }
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

bool RSDK::PS1AtlasLoad(const char *folder, const char *sceneID)
{
    PS1AtlasRelease();
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
    PS1TryAllocateStorage((void **)&s_ps1AtlasBindTmp, s_bindBytes, DATASET_TMP, false);
    if (s_ps1AtlasBindTmp)
        t = s_ps1AtlasBindTmp, s_bindInTmp = true;
    else
        t = (uint8 *)PS1StorageTailAlloc(DATASET_STG, s_bindBytes);
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
void RSDK::PS1AtlasMusicGroups(const char *track)
{
    if (!s_ps1AtlasBlob || !track)
        return;
    static const char *const jingles[] = { "1up.ogg", "Invincible.ogg", "Sneakers.ogg", "Drowning.ogg", "GameOver.ogg" };
    for (const char *j : jingles)
        if (!strcmp(track, j))
            return;
    auto load = [](const char *name) {
        for (int32 g = 0; g < s_groupCount; ++g)
            if (!strncmp(Groups()[g].name, name, 8)) {
                PS1AtlasLoadGroup(name);
                return;
            }
    };
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
    load("normal");
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
