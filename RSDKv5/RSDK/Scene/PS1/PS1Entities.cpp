// PS1 port (docs/32 phase 2.3): per-slot entity storage (see PS1Entities.hpp). Included by Object.cpp.

EntityBase *RSDK::objectEntityPtr[ENTITY_COUNT];
// One zero page for every ghost: slot s reads at +4 * s, and a ghost reads sizeof(EntityBase) bytes at most.
uint8 RSDK::ps1EntityGhosts[ENTITY_COUNT * 4 + sizeof(EntityBase)] __attribute__((aligned(8)));

volatile uint32 RSDK::g_ps1EntityRelocations = 0;
volatile uint32 RSDK::g_ps1EntityGhostWrites = 0;
volatile uint32 RSDK::g_ps1EntitySpareFail   = 0;
volatile uint32 RSDK::g_ps1EntityArenaMoved  = 0; // 4.3: STG compactions that moved the entity arena (must stay 0)
volatile uint32 RSDK::g_ps1EntityArenaBytes  = 0;
volatile uint32 RSDK::g_ps1EntityArenaPeak   = 0;
volatile uint32 RSDK::g_ps1EntityCreateSkips = 0;
volatile uint32 RSDK::g_ps1SceneRecordBytes  = 0;
volatile uint32 RSDK::g_ps1SceneRecordDropped = 0;
volatile uint32 RSDK::g_ps1EntitiesDropped    = 0;
int32 RSDK::ps1SceneSlotEnd = RESERVE_ENTITY_COUNT;
int32 RSDK::ps1TempSlotEnd  = TEMPENTITY_START + PS1_TEMP_BLOCKS;
int32 RSDK::ps1TempBlocks = PS1_TEMP_BLOCKS, RSDK::ps1TempLargeEvery = PS1_TEMP_LARGE_EVERY;
volatile uint32 RSDK::g_ps1EntityCreateOverwrites = 0;

namespace
{
uint8 *s_arena      = NULL; // DATASET_STG, one per scene
uint8 *s_arenaAt    = NULL; // 4.3: where it was placed (a compaction must never move it: the slots point into it)
uint8 *s_spare      = NULL; // the arena's tail, bump-allocated by relocations
uint8 *s_spareEnd   = NULL;

inline uint32 Align4(uint32 v) { return (v + 3) & ~3u; }
void DormClear(); // 7b (below)

inline EntityBase *Ghost(int32 slot) { return (EntityBase *)&RSDK::ps1EntityGhosts[slot * 4]; }

// A block at `p` for `slot` holding `cap` bytes; returns the entity (after the header).
inline EntityBase *Carve(uint8 *&p, uint16 slot, uint32 cap)
{
    PS1EntityHeader *h = (PS1EntityHeader *)p;
    h->slot            = slot;
    h->capacity        = (uint16)cap;
    p += sizeof(PS1EntityHeader) + cap;
    return (EntityBase *)(h + 1);
}

// The largest entity class of the loaded stage (its classes include the global ones; DefaultObject's sizeof(EntityBase)
// left out: it is the blank class). A reserved or temp slot can only hold one of these.
// Reserved slots no SLOT_ constant names (SonicMania/GameVariables.h ReservedEntities: 13, 14, 38, 39, 49-59) stay ghosts
// (2.3m); a reset into one gets a spare block (counted in g_ps1EntityRelocations).
inline bool PS1ReservedUnused(int32 s) { return s == 13 || s == 14 || s == 38 || s == 39 || (s >= 49 && s <= 59); }

// 7b: the stage class named `name` (the game's tables), 0 if none
extern "C" const PS1ReservedSpec ps1ReservedSpecs[]; // the game (resident: SonicMania/Objects/Global/Zone.c)
extern "C" const char *const ps1ReservedStageMarker;
extern "C" const char *const ps1TempStageMarker; // 7b: (Zone.c) a stage with this class gets PS1_TEMP_BLOCKS_MANY
__attribute__((noinline)) int32 StageClassOf(const char *name)
{
    RETRO_HASH_MD5(hash);
    GenerateHashMD5(hash, (char *)name, (int32)strlen(name));
    for (int32 o = 1; o < sceneInfo.classCount; ++o)
        if (HASH_MATCH_MD5(objectClassList[stageObjectIDs[o]].hash, hash))
            return o;
    return 0;
}

// 7b: each reserved slot's capacity: in a zone (the stage has the marker's class) the largest class the game's table
// names for the slot that the stage has (0: the slot stays a ghost); elsewhere, or a slot the table leaves out, `maxCap`
// (0 for the slots no SLOT_ constant names)
__attribute__((noinline)) void ReservedCaps(uint32 maxCap, uint16 *caps)
{
    bool zone = StageClassOf(ps1ReservedStageMarker) != 0;
    for (int32 s = 0; s < RESERVE_ENTITY_COUNT; ++s) caps[s] = PS1ReservedUnused(s) ? 0 : (uint16)maxCap;
    if (!zone)
        return;
    for (const PS1ReservedSpec *r = ps1ReservedSpecs; r->first != 0xFFFF; ++r) // the named slots start empty
        for (int32 s = r->first; s <= r->last && s < RESERVE_ENTITY_COUNT; ++s) caps[s] = 0;
    for (const PS1ReservedSpec *r = ps1ReservedSpecs; r->first != 0xFFFF; ++r) {
        if (r->needs && !StageClassOf(r->needs))
            continue;
        uint32 cap = 0;
        for (int32 k = 0; k < 8 && r->classes[k]; ++k) {
            int32 o = StageClassOf(r->classes[k]);
            if (o && Align4(objectClassList[stageObjectIDs[o]].entityClassSize) > cap)
                cap = Align4(objectClassList[stageObjectIDs[o]].entityClassSize);
        }
        for (int32 s = r->first; s <= r->last && s < RESERVE_ENTITY_COUNT; ++s)
            if (cap > caps[s])
                caps[s] = (uint16)cap;
    }
}

uint32 MaxClassSize()
{
    uint32 m = sizeof(Entity);
    for (int32 i = 0; i < sceneInfo.classCount; ++i) {
        uint32 size = objectClassList[stageObjectIDs[i]].entityClassSize;
        if (size > m && size < sizeof(EntityBase))
            m = size;
    }
    return Align4(m);
}
} // namespace

void *RSDK::PS1EntityArenaData() { return s_arena; }

// 4.3: after an STG compaction: the arena must still be where the slots point (counted if not)
void RSDK::PS1EntityArenaCheck()
{
    if (s_arena != s_arenaAt)
        g_ps1EntityArenaMoved = g_ps1EntityArenaMoved + 1;
}

void RSDK::PS1EntityStorageClear()
{
    PS1EntityCheckGhosts();
    for (int32 s = 0; s < ENTITY_COUNT; ++s) objectEntityPtr[s] = Ghost(s);
    if (s_arena)
        RemoveStorageEntry((void **)&s_arena);
    s_arena = s_spare = s_spareEnd = s_arenaAt = NULL;
    DormClear(); // 7b: the dormant records lived in the arena
    g_ps1EntityArenaBytes = 0;
    ps1SceneSlotEnd       = RESERVE_ENTITY_COUNT;
    ps1TempSlotEnd        = TEMPENTITY_START + ps1TempBlocks;
}

EntityBase *RSDK::PS1EntityRelocate(uint16 slot, uint32 size)
{
    uint32 cap = Align4(size < sizeof(Entity) ? sizeof(Entity) : size);
    if (!s_spare || s_spare + sizeof(PS1EntityHeader) + cap > s_spareEnd) {
        g_ps1EntitySpareFail = g_ps1EntitySpareFail + 1;
        return NULL;
    }
    EntityBase *e = Carve(s_spare, slot, cap);
    memset(e, 0, cap);
    objectEntityPtr[slot]  = e;
    g_ps1EntityRelocations = g_ps1EntityRelocations + 1;
    if (slot >= ps1SceneSlotEnd && slot < TEMPENTITY_START)
        ps1SceneSlotEnd = slot + 1; // a formerly empty scene slot: the slot loops must visit it now
    if (slot >= ps1TempSlotEnd)
        ps1TempSlotEnd = slot + 1; // a temp slot past the blocks: the same
    return e;
}

EntityBase *RSDK::PS1EntityEnsure(EntityBase *entity, int32 slot, uint32 size)
{
    if (slot < 0)
        return entity; // not a slot's entity (a scratch one): the stock behaviour
    if (PS1EntityCapacity(entity) >= size)
        return entity;
    return PS1EntityRelocate((uint16)slot, size);
}

void RSDK::PS1EntityCheckGhosts()
{
    uint32 *w = (uint32 *)ps1EntityGhosts;
    for (uint32 i = 0; i < sizeof(ps1EntityGhosts) / 4; ++i) {
        if (w[i]) {
            g_ps1EntityGhostWrites = g_ps1EntityGhostWrites + 1;
            w[i]                   = 0;
        }
    }
}

// ---- type groups ----
namespace
{
// 2.3q: the entities one frame updates (GHZ1 play: at most 20, GHZ2 39); an entity past the cap is left out of the
// frame's groups and counted (g_ps1TypeGroupOverflow)
#define PS1_TYPEGROUP_ENTITIES (0x200)
uint16 s_tgSlot[PS1_TYPEGROUP_ENTITIES], s_tgClass[PS1_TYPEGROUP_ENTITIES], s_tgGroup[PS1_TYPEGROUP_ENTITIES];
uint16 s_tgPool[PS1_TYPEGROUP_ENTITIES * 3];
int32 s_tgCount = 0;
} // namespace

void RSDK::PS1TypeGroupsBegin()
{
    s_tgCount = 0;
    for (int32 i = 0; i < TYPEGROUP_COUNT; ++i) typeGroups[i].entryCount = 0;
}

volatile uint32 g_ps1TypeGroupOverflow = 0;

void RSDK::PS1TypeGroupsAdd(uint16 slot, uint16 classID, uint16 group)
{
    if (s_tgCount >= PS1_TYPEGROUP_ENTITIES) {
        g_ps1TypeGroupOverflow = g_ps1TypeGroupOverflow + 1;
        return;
    }
    s_tgSlot[s_tgCount]  = slot;
    s_tgClass[s_tgCount] = classID;
    s_tgGroup[s_tgCount] = group;
    ++s_tgCount;
}

// The type-group pass (Object.cpp) over [p, pend): a leaf loop, so its locals stay in registers (3.0c)
__attribute__((optimize("O2"))) void RSDK::PS1TypeGroupsCollect(EntityBase **p, EntityBase **pend)
{
    int32 n = s_tgCount;
    for (; p < pend; ++p) {
        EntityBase *ent = *p;
        if (ent->inRange && ent->interaction) {
            if (n >= PS1_TYPEGROUP_ENTITIES) {
                g_ps1TypeGroupOverflow = g_ps1TypeGroupOverflow + 1;
                continue;
            }
            s_tgSlot[n]  = (uint16)(p - objectEntityPtr);
            s_tgClass[n] = ent->classID;
            s_tgGroup[n] = ent->group;
            ++n;
        }
    }
    s_tgCount = n;
}

// The stock loop appends each entity to GROUP_ALL, its class's group and (group >= TYPE_COUNT) its extra group, in slot
// order: the same lists, each one contiguous in the pool.
volatile uint32 g_ps1TypeGroupPeak = 0; // most entities captured in one frame (GDB)

void RSDK::PS1TypeGroupsBuild()
{
    if ((uint32)s_tgCount > g_ps1TypeGroupPeak)
        g_ps1TypeGroupPeak = s_tgCount;
    for (int32 i = 0; i < TYPEGROUP_COUNT; ++i) typeGroups[i].entryCount = 0;
    for (int32 n = 0; n < s_tgCount; ++n) {
        typeGroups[GROUP_ALL].entryCount++;
        typeGroups[s_tgClass[n]].entryCount++;
        if (s_tgGroup[n] >= TYPE_COUNT)
            typeGroups[s_tgGroup[n]].entryCount++;
    }
    int32 pos = 0;
    for (int32 i = 0; i < TYPEGROUP_COUNT; ++i) {
        typeGroups[i].entries = &s_tgPool[pos];
        pos += typeGroups[i].entryCount;
        typeGroups[i].entryCount = 0;
    }
    for (int32 n = 0; n < s_tgCount; ++n) {
        TypeGroupList *g = &typeGroups[GROUP_ALL];
        g->entries[g->entryCount++] = s_tgSlot[n];
        g                           = &typeGroups[s_tgClass[n]];
        g->entries[g->entryCount++] = s_tgSlot[n];
        if (s_tgGroup[n] >= TYPE_COUNT) {
            g                           = &typeGroups[s_tgGroup[n]];
            g->entries[g->entryCount++] = s_tgSlot[n];
        }
    }
}

// ---- the scene file's entities ----
// Record: {u16 fileSlot, u16 classID, i32 x, i32 y, u8 filter, u8 pad, u16 payloadLen} then payload items
// {u16 offset, u8 type, u8 pad, u16 len, data[len]}, each item padded to 4 bytes.
namespace
{
struct SceneRec {
    uint16 fileSlot;
    uint16 classID;
    int32 x, y;
    uint8 filter;
    uint8 pad;
    uint16 payloadLen;
};

// One block of records per object (sized exactly from its variable list and entity count): carved from the top of STG's
// free space while the scene loads (docs/32 2.3h: the arena is placed below it, the stage's sprites fill that space only
// later), else DATASET_TMP.
#define PS1_SCENE_BLOCKS 0x100
uint8 *s_blocks[PS1_SCENE_BLOCKS];
uint32 s_blockSize[PS1_SCENE_BLOCKS], s_blockUsed[PS1_SCENE_BLOCKS];
bool s_blockTail[PS1_SCENE_BLOCKS];
int32 s_blockCount = 0;
uint8 *s_rec       = NULL; // the current object's block
uint32 s_recSize   = 0;
uint32 s_recUsed   = 0;
SceneRec *s_cur    = NULL;

// 7b: the classes with a "childCount" variable (PS1SceneChildVar) and the slots their entities' counts cover
#define PS1_CHILD_VARS 8
uint16 s_childClass[PS1_CHILD_VARS], s_childOffset[PS1_CHILD_VARS];
int32 s_childVars = 0;
uint32 s_noDormant[(ENTITY_COUNT + 31) / 32];

// A record's payload: {u16 offset, u8 type, u8 pad, u16 len, data[len]} items, each 4-byte aligned. Into an entity
// block of `cap` bytes, as the stock loader writes the variables.
__attribute__((noinline)) void ApplyPayload(EntityBase *e, uint32 cap, const uint8 *q, uint32 payloadLen)
{
    uint8 *eb = (uint8 *)e;
    for (const uint8 *end = q + payloadLen; q < end;) {
        uint16 offset, len;
        memcpy(&offset, q, 2);
        uint8 type = q[2];
        memcpy(&len, q + 4, 2);
        const uint8 *data = q + 6;
        if (type == VAR_STRING) {
            if (offset + sizeof(String) <= cap) {
                String *string = (String *)&eb[offset];
                uint16 chars   = len / 2;
                InitString(string, "", chars);
                for (string->length = 0; string->length < chars; ++string->length)
                    memcpy(&string->chars[string->length], data + 2 * string->length, 2);
            }
        }
        else if (offset + len <= cap) {
            memcpy(&eb[offset], data, len);
        }
        q += Align4(6 + len);
    }
}

// The entity a record makes before its Create (Place's writes): the block cleared, class, filter, position, variables
__attribute__((noinline)) void FillEntity(EntityBase *e, uint32 cap, uint16 classID, int32 x, int32 y, const uint8 *payload, uint32 payloadLen)
{
    memset(e, 0, cap);
    e->classID    = classID;
    e->filter     = 0xFF;
    e->position.x = x;
    e->position.y = y;
    ApplyPayload(e, cap, payload, payloadLen);
}

// ---- 7b (choice 113): dormant entities ----
// A dormant record: its slot (PS1_DORMANT_GONE once it is no longer dormant-managed), its payload (one copy per distinct
// payload: a scene's static rings share a handful) and position. Sorted by x: the creation pass walks a window of them.
struct DormRec {
    uint16 slot;
    uint16 unique;
    int32 x, y;
};
#define PS1_DORMANT_GONE    0x8000
#define PS1_DORMANT_UNIQUES 160 // (the most in base Mania: SPZ2 act 1, 118)
#define PS1_DORMANT_SPECS   4
#define PS1_DORMANT_SCRATCH 256 // the largest class a spec may name (bytes)
#define PS1_DORMANT_CREATE  TO_FIXED(32) // creation margin beyond the stock range (16.16)
#define PS1_DORMANT_RETURN  TO_FIXED(64) // return margin (> creation: a ring at the edge does not flip every frame)
#define PS1_DORMANT_SPREAD  64           // pool bound: cameras apart by up to this many pixels (each side)
#define PS1_DORMANT_CAMERAS 4
struct DormUnique { // in the arena, then the variables' raw bytes (in its class's layout order)
    uint16 classID;
    uint16 spec;
    uint16 len;
    uint16 pad;
    int32 range; // its entities' updateRange (the larger axis: the boxes are a superset of stock's test)
};
// A dormant class's variables (every scene entity of a class carries the same list): offset, length, in payload order
struct DormVar {
    uint16 offset;
    uint16 len;
};
const DormVar *s_dLayout[PS1_DORMANT_SPECS];
uint8 s_dLayoutN[PS1_DORMANT_SPECS];
extern "C" const PS1DormantSpec ps1DormantSpecs[]; // the game (resident: SonicMania/Objects/Global/Ring.c)
uint8 s_classSpec[TYPE_COUNT];                   // stage class ID -> 1 + spec index (0: none)
const DormUnique *s_dUnique[PS1_DORMANT_UNIQUES];
int32 s_dUniques = 0;
DormRec *s_dRec  = NULL;
int32 s_dN = 0, s_dLo = 0, s_dHi = 0;
EntityBase **s_dFree = NULL, **s_dMatBlk = NULL;
uint16 *s_dMatRec = NULL;
int32 s_dFreeN = 0, s_dMatN = 0, s_dPool = 0;
uint32 s_dCap  = 0;
int32 s_dRange = 0;
uint8 s_dScratch[PS1_DORMANT_SCRATCH] __attribute__((aligned(8)));
// this frame's boxes per camera, before an entity's range: creation (c) and return (r), x0 x1 y0 y1 (16.16, clamped)
int32 s_dCams = 0;
int32 s_dBoxC[PS1_DORMANT_CAMERAS][4], s_dBoxR[PS1_DORMANT_CAMERAS][4];

// a + b, saturated to the int32 range
inline int32 SatAdd(int32 a, int32 b)
{
    int32 r = (int32)((uint32)a + (uint32)b);
    if (b > 0 && r < a)
        return 0x7FFFFFFF;
    if (b < 0 && r > a)
        return -0x7FFFFFFF - 1;
    return r;
}

// in a camera's box grown by range r (>= 0, < 2^28): x + r >= x0 and x - r <= x1 (no overflow for |x| < 2^30)
inline bool InBoxes(const int32 (*box)[4], int32 n, int32 x, int32 y, int32 r)
{
    for (int32 c = 0; c < n; ++c)
        if (x + r >= box[c][0] && x - r <= box[c][1] && y + r >= box[c][2] && y - r <= box[c][3])
            return true;
    return false;
}

// Shell sort of (x, y) pairs by x (a few hundred entries, once per load)
__attribute__((noinline)) void SortPairs(int32 *xy, int32 n)
{
    for (int32 gap = n / 2; gap > 0; gap /= 2) {
        for (int32 i = gap; i < n; ++i) {
            int32 x = xy[2 * i], y = xy[2 * i + 1], j = i;
            for (; j >= gap && xy[2 * (j - gap)] > x; j -= gap) {
                xy[2 * j]     = xy[2 * (j - gap)];
                xy[2 * j + 1] = xy[2 * (j - gap) + 1];
            }
            xy[2 * j] = x, xy[2 * j + 1] = y;
        }
    }
}

// The most points (pixels) any w x h box holds: x-sorted pairs swept by x, the window's y values kept sorted
__attribute__((noinline)) int32 BoxBound(int32 *xy, int32 *ys, int32 n, int32 w, int32 h)
{
    SortPairs(xy, n);
    int32 best = 0, cnt = 0, j = 0;
    for (int32 i = 0; i < n; ++i) {
        for (; j < n && xy[2 * j] - xy[2 * i] <= w; ++j) { // insert y[j]
            int32 y = xy[2 * j + 1], k = cnt++;
            for (; k > 0 && ys[k - 1] > y; --k) ys[k] = ys[k - 1];
            ys[k] = y;
        }
        for (int32 a = 0, b = 0; b < cnt; ++b) {
            while (ys[b] - ys[a] > h) ++a;
            if (b - a + 1 > best)
                best = b - a + 1;
        }
        int32 y = xy[2 * i + 1], k = 0; // remove y[i]
        while (k < cnt && ys[k] != y) ++k;
        for (--cnt; k < cnt; ++k) ys[k] = ys[k + 1];
    }
    return best;
}

__attribute__((noinline)) void SortRecs(DormRec *r, int32 n)
{
    for (int32 gap = n / 2; gap > 0; gap /= 2) {
        for (int32 i = gap; i < n; ++i) {
            DormRec t = r[i];
            int32 j   = i;
            for (; j >= gap && r[j - gap].x > t.x; j -= gap) r[j] = r[j - gap];
            r[j] = t;
        }
    }
}

// A record's childCount value (its class's variable), 0 if it has none
__attribute__((noinline)) int32 ChildCount(const SceneRec *r)
{
    for (int32 c = 0; c < s_childVars; ++c) {
        if (s_childClass[c] != r->classID)
            continue;
        for (const uint8 *q = (const uint8 *)(r + 1), *end = q + r->payloadLen; q < end;) {
            uint16 offset, len;
            memcpy(&offset, q, 2);
            memcpy(&len, q + 4, 2);
            if (offset == s_childOffset[c] && q[2] != VAR_STRING) {
                int32 v = 0;
                memcpy(&v, q + 6, len < 4 ? len : 4);
                if (len == 1 && q[2] == VAR_INT8)
                    v = (int8)v;
                else if (len == 2 && q[2] == VAR_INT16)
                    v = (int16)v;
                return v;
            }
            q += Align4(6 + len);
        }
    }
    return 0;
}

// Does record r's variable list match `layout` (n entries, or none yet: n = 0 and layout NULL)? Its raw bytes in `raw`.
__attribute__((noinline)) bool SameLayout(const SceneRec *r, const SceneRec *layout, uint32 *raw)
{
    const uint8 *q = (const uint8 *)(r + 1), *end = q + r->payloadLen;
    const uint8 *l = layout ? (const uint8 *)(layout + 1) : NULL, *lend = layout ? l + layout->payloadLen : NULL;
    *raw = 0;
    while (q < end) {
        uint16 len;
        memcpy(&len, q + 4, 2);
        if (l) {
            if (l >= lend || memcmp(l, q, 6))
                return false;
            l += Align4(6 + len);
        }
        *raw += len;
        q += Align4(6 + len);
    }
    return !l || l == lend;
}

__attribute__((noinline)) bool HasString(const SceneRec *r)
{
    for (const uint8 *q = (const uint8 *)(r + 1), *end = q + r->payloadLen; q < end;) {
        uint16 len;
        memcpy(&len, q + 4, 2);
        if (q[2] == VAR_STRING)
            return true;
        q += Align4(6 + len);
    }
    return false;
}
} // namespace

#define PS1_SCENE_FILESLOTS (2 * SCENEENTITY_COUNT) // stock: < SCENEENTITY_COUNT main list, then the temp list

void RSDK::PS1SceneEntitiesBegin()
{
    PS1StorageTailRelease(DATASET_STG); // (a load that never reached PS1SceneEntitiesPlace)
    s_childVars  = 0;
    s_blockCount = 0;
    s_rec        = NULL;
    s_recSize = s_recUsed = 0;
    s_cur                 = NULL;
}

void RSDK::PS1SceneObjectBegin(uint32 bytes)
{
    if (s_rec && s_blockCount < PS1_SCENE_BLOCKS) {
        s_blockUsed[s_blockCount - 1] = s_recUsed;
        g_ps1SceneRecordBytes         = g_ps1SceneRecordBytes; // (peak kept in Place)
    }
    s_rec = NULL;
    s_recSize = s_recUsed = 0;
    s_cur                 = NULL;
    if (!bytes)
        return;
    if (s_blockCount >= PS1_SCENE_BLOCKS) {
        g_ps1SceneRecordDropped = g_ps1SceneRecordDropped + 1;
        return;
    }
    s_blocks[s_blockCount]    = (uint8 *)PS1StorageTailAlloc(DATASET_STG, bytes);
    s_blockTail[s_blockCount] = s_blocks[s_blockCount] != NULL;
    if (!s_blocks[s_blockCount])
        PS1TryAllocateStorage((void **)&s_blocks[s_blockCount], bytes, DATASET_TMP, false);
    if (!s_blocks[s_blockCount]) {
        g_ps1SceneRecordDropped = g_ps1SceneRecordDropped + 1;
        return;
    }
    s_rec                      = s_blocks[s_blockCount];
    s_recSize                  = bytes;
    s_blockSize[s_blockCount]  = bytes;
    s_blockUsed[s_blockCount]  = 0;
    ++s_blockCount;
}

void RSDK::PS1SceneEntityBegin(uint16 fileSlot, uint16 classID, int32 x, int32 y)
{
    s_cur = NULL;
    if (!s_rec || s_recUsed + sizeof(SceneRec) > s_recSize) {
        g_ps1SceneRecordDropped = g_ps1SceneRecordDropped + 1;
        return;
    }
    s_cur             = (SceneRec *)(s_rec + s_recUsed);
    s_cur->fileSlot   = fileSlot;
    s_cur->classID    = classID;
    s_cur->x          = x;
    s_cur->y          = y;
    s_cur->filter     = 0xFF; // as the stock loader, before the variables
    s_cur->pad        = 0;    // 7b: 1 + its dormant payload's index when it goes dormant (Place)
    s_cur->payloadLen = 0;
    s_recUsed += sizeof(SceneRec);
}

void RSDK::PS1SceneEntityVar(uint16 offset, uint8 type, const void *data, uint16 len)
{
    if (!s_cur)
        return;
    uint32 item = Align4(6 + len); // records and items stay 4-byte aligned (SceneRec holds int32s)
    if (s_recUsed + item > s_recSize) {
        g_ps1SceneRecordDropped = g_ps1SceneRecordDropped + 1;
        return;
    }
    uint8 *p = s_rec + s_recUsed;
    memcpy(p, &offset, 2);
    p[2] = type;
    p[3] = 0;
    memcpy(p + 4, &len, 2);
    memcpy(p + 6, data, len);
    s_recUsed += item;
    s_cur->payloadLen += item;
    if (offset == offsetof(Entity, filter) && len == 1)
        s_cur->filter = *(const uint8 *)data;
}

// Stock REV02 compaction: the scene list (file slots < SCENEENTITY_COUNT) then the temp list, each in slot order; an
// entity is kept when (sceneInfo.filter & its filter), and takes the next slot from RESERVE_ENTITY_COUNT on.
void RSDK::PS1SceneEntitiesPlace()
{
    if (s_rec && s_blockCount)
        s_blockUsed[s_blockCount - 1] = s_recUsed;
    uint32 recordBytes = 0;
    for (int32 b = 0; b < s_blockCount; ++b) recordBytes += s_blockSize[b];
    if (recordBytes > g_ps1SceneRecordBytes)
        g_ps1SceneRecordBytes = recordBytes;

    // file slot -> record (a later record for the same slot replaces an earlier one, as stock)
    SceneRec **index = NULL;
    AllocateStorage((void **)&index, PS1_SCENE_FILESLOTS * sizeof(SceneRec *), DATASET_TMP, true);
    for (int32 b = 0; b < s_blockCount; ++b) {
        for (uint32 p = 0; p < s_blockUsed[b];) {
            SceneRec *r = (SceneRec *)(s_blocks[b] + p);
            if (r->fileSlot < PS1_SCENE_FILESLOTS)
                index[r->fileSlot] = r;
            else
                g_ps1EntitiesDropped = g_ps1EntitiesDropped + 1;
            p += sizeof(SceneRec) + r->payloadLen;
        }
    }

    // 7b: the classes that may go dormant this stage (matched by their Create)
    memset(s_classSpec, 0, sizeof(s_classSpec));
    int32 candidates = 0;
    for (int32 o = 1; o < sceneInfo.classCount && o < TYPE_COUNT; ++o) {
        ObjectClass *oc = &objectClassList[stageObjectIDs[o]];
        for (int32 k = 0; oc->create && k < PS1_DORMANT_SPECS && ps1DormantSpecs[k].create; ++k) {
            if (ps1DormantSpecs[k].create == oc->create && Align4(oc->entityClassSize) <= PS1_DORMANT_SCRATCH)
                s_classSpec[o] = (uint8)(k + 1);
        }
    }
    for (int32 f = 0; index && f < PS1_SCENE_FILESLOTS; ++f)
        if (index[f] && index[f]->classID < TYPE_COUNT && s_classSpec[index[f]->classID])
            ++candidates;
    int32 *dormXY = NULL, *dormYs = NULL;
    if (candidates) {
        AllocateStorage((void **)&dormXY, candidates * 2 * sizeof(int32), DATASET_TMP, false);
        AllocateStorage((void **)&dormYs, candidates * sizeof(int32), DATASET_TMP, false);
        if (!dormXY || !dormYs)
            candidates = 0;
    }
    memset(s_noDormant, 0, sizeof(s_noDormant));
    s_dUniques       = 0;
    int32 uniqueRange[PS1_DORMANT_UNIQUES];
    const SceneRec *layoutRec[PS1_DORMANT_SPECS] = {};
    uint8 layoutN[PS1_DORMANT_SPECS]             = {};
    int32 dormant    = 0;
    uint32 blobBytes = 0, dormCap = 0, dormRange = 0;

    // capacities: reserved + scene (in final order) + temp + spare
    uint32 maxCap = MaxClassSize();
    uint32 bytes  = 0;
    uint16 resvCap[RESERVE_ENTITY_COUNT];
    ReservedCaps(maxCap, resvCap);
    for (int32 s = 0; s < RESERVE_ENTITY_COUNT; ++s) bytes += resvCap[s] ? sizeof(PS1EntityHeader) + resvCap[s] : 0;
    int32 placed  = 0;
    for (int32 f = 0; index && f < PS1_SCENE_FILESLOTS && placed < SCENEENTITY_COUNT; ++f) {
        if (!index[f])
            continue;
        SceneRec *r = index[f];
        if (!(sceneInfo.filter & r->filter))
            continue;
        int32 slot = RESERVE_ENTITY_COUNT + placed;
        if (r->classID) {
            uint32 classBytes = Align4(objectClassList[stageObjectIDs[r->classID]].entityClassSize);
            // 7b: the slots this entity's childCount covers stay entities
            int32 children = s_childVars ? ChildCount(r) : 0;
            for (int32 c = slot + 1; c <= slot + children && c < ENTITY_COUNT; ++c) s_noDormant[c >> 5] |= 1u << (c & 31);
            uint8 spec = candidates && r->classID < TYPE_COUNT ? s_classSpec[r->classID] : 0;
            uint32 raw = 0;
            if (spec && !((s_noDormant[slot >> 5] >> (slot & 31)) & 1) && !HasString(r)
                && SameLayout(r, layoutRec[spec - 1], &raw)) {
                EntityBase *pre = (EntityBase *)s_dScratch;
                FillEntity(pre, classBytes, r->classID, r->x, r->y, (const uint8 *)(r + 1), r->payloadLen);
                if (ps1DormantSpecs[spec - 1].eligible(pre)) {
                    int32 u = 0; // its payload among the distinct ones (pointers into the records until the arena exists)
                    for (; u < s_dUniques; ++u) {
                        const SceneRec *o = (const SceneRec *)s_dUnique[u];
                        if (o->classID == r->classID && o->payloadLen == r->payloadLen
                            && !memcmp(o + 1, r + 1, r->payloadLen))
                            break;
                    }
                    if (u == s_dUniques && u < PS1_DORMANT_UNIQUES) {
                        const PS1DormantSpec *sp = &ps1DormantSpecs[spec - 1];
                        int32 range              = sp->rangeOf ? sp->rangeOf(pre) : sp->range;
                        if (range < 0 || range >= 0x10000000)
                            u = PS1_DORMANT_UNIQUES; // (never in the data: such an entity stays one)
                        else {
                            uniqueRange[s_dUniques] = range;
                            s_dUnique[s_dUniques++] = (const DormUnique *)r;
                            blobBytes += sizeof(DormUnique) + Align4(raw);
                            if (!layoutRec[spec - 1]) {
                                layoutRec[spec - 1] = r;
                                for (const uint8 *q = (const uint8 *)(r + 1), *end = q + r->payloadLen; q < end; ++layoutN[spec - 1]) {
                                    uint16 len;
                                    memcpy(&len, q + 4, 2);
                                    q += Align4(6 + len);
                                }
                                blobBytes += layoutN[spec - 1] * sizeof(DormVar);
                            }
                        }
                    }
                    if (u < s_dUniques) {
                        r->pad = (uint8)(u + 1);
                        dormXY[2 * dormant]     = r->x >> 16;
                        dormXY[2 * dormant + 1] = r->y >> 16;
                        ++dormant;
                        if (classBytes > dormCap)
                            dormCap = classBytes;
                        if ((uint32)uniqueRange[u] > dormRange)
                            dormRange = uniqueRange[u];
                    }
                }
            }
            if (!r->pad)
                bytes += sizeof(PS1EntityHeader) + classBytes;
        }
        ++placed;
    }
    // 7b: the temp blocks (more in a stage with the game's ps1TempStageMarker class: the Mean Bean Machine's beans)
    ps1TempBlocks     = StageClassOf(ps1TempStageMarker) ? PS1_TEMP_BLOCKS_MANY : PS1_TEMP_BLOCKS;
    ps1TempLargeEvery = ps1TempBlocks / PS1_TEMP_LARGE_COUNT;
    int32 tempLarge   = ps1TempBlocks / ps1TempLargeEvery;
    bytes += (ps1TempBlocks - tempLarge) * (sizeof(PS1EntityHeader) + PS1_TEMP_SMALL_CAP);
    bytes += tempLarge * (sizeof(PS1EntityHeader) + maxCap);
    // 7b: the dormant records, their payloads and a pool of blocks for the most records the cameras' boxes reach at once
    int32 pool = 0;
    if (dormant) {
        int32 ox = screens[0].center.x ? screens[0].center.x : 160, oy = screens[0].center.y ? screens[0].center.y : 120;
        int32 m  = (int32)(dormRange >> 16) + FROM_FIXED(PS1_DORMANT_RETURN) + PS1_DORMANT_SPREAD;
        pool     = BoxBound(dormXY, dormYs, dormant, 2 * (ox + m), 2 * (oy + m)) + 4;
        if (pool > dormant)
            pool = dormant;
        bytes += dormant * sizeof(DormRec) + blobBytes;
        bytes += pool * (2 * sizeof(EntityBase *) + sizeof(PS1EntityHeader) + dormCap) + Align4(pool * sizeof(uint16));
    }
    uint32 spareBytes = 8 * 1024; // GHZ1 play: 0 relocations (2.3m)
    bytes += spareBytes;

    s_arena = NULL;
    AllocateStorage((void **)&s_arena, bytes, DATASET_STG, true); // stops at PS1StorageExhausted if STG is too small
    s_arenaAt = s_arena;
    g_ps1EntityArenaBytes = bytes;
    if (bytes > g_ps1EntityArenaPeak)
        g_ps1EntityArenaPeak = bytes;

    uint8 *p = s_arena;
    for (int32 s = 0; s < RESERVE_ENTITY_COUNT; ++s) {
        if (resvCap[s])
            objectEntityPtr[s] = Carve(p, s, resvCap[s]);
    }

    // 7b: the dormant area first (records, payloads), filled while the slots are placed
    s_dRec = (DormRec *)p;
    s_dN   = 0;
    p += dormant * sizeof(DormRec);
    for (int32 k = 0; k < PS1_DORMANT_SPECS; ++k) { // the classes' variable layouts
        s_dLayout[k]  = (const DormVar *)p;
        s_dLayoutN[k] = layoutN[k];
        if (!layoutRec[k])
            continue;
        DormVar *v = (DormVar *)p;
        for (const uint8 *q = (const uint8 *)(layoutRec[k] + 1), *end = q + layoutRec[k]->payloadLen; q < end; ++v) {
            memcpy(&v->offset, q, 2);
            memcpy(&v->len, q + 4, 2);
            q += Align4(6 + v->len);
        }
        p += layoutN[k] * sizeof(DormVar);
    }
    for (int32 u = 0; u < s_dUniques; ++u) { // the distinct payloads: their raw bytes
        const SceneRec *r = (const SceneRec *)s_dUnique[u];
        DormUnique *d     = (DormUnique *)p;
        d->classID        = r->classID;
        d->spec           = (uint16)(s_classSpec[r->classID] - 1);
        d->pad            = 0;
        d->range          = uniqueRange[u];
        uint8 *raw        = (uint8 *)(d + 1);
        uint32 n          = 0;
        for (const uint8 *q = (const uint8 *)(r + 1), *end = q + r->payloadLen; q < end;) {
            uint16 len;
            memcpy(&len, q + 4, 2);
            memcpy(raw + n, q + 6, len);
            n += len;
            q += Align4(6 + len);
        }
        d->len       = (uint16)n;
        s_dUnique[u] = d;
        p += sizeof(DormUnique) + Align4(n);
    }

    int32 slot = RESERVE_ENTITY_COUNT;
    for (int32 f = 0; index && f < PS1_SCENE_FILESLOTS && slot < RESERVE_ENTITY_COUNT + SCENEENTITY_COUNT; ++f) {
        if (!index[f])
            continue;
        SceneRec *r = index[f];
        if (!(sceneInfo.filter & r->filter))
            continue;
        if (r->classID && r->pad) { // dormant: the slot stays a ghost until the cameras come near
            DormRec *d = &s_dRec[s_dN++];
            d->slot    = (uint16)slot;
            d->unique  = (uint16)(r->pad - 1);
            d->x       = r->x;
            d->y       = r->y;
        }
        else if (r->classID) {
            EntityBase *e = Carve(p, slot, Align4(objectClassList[stageObjectIDs[r->classID]].entityClassSize));
            objectEntityPtr[slot] = e;
            FillEntity(e, PS1EntityCapacity(e), r->classID, r->x, r->y, (const uint8 *)(r + 1), r->payloadLen);
        }
        // classID 0 (a class this build does not have): the slot stays a ghost (stock: a blank entity with a position)
        ++slot;
    }

    for (int32 t = 0; t < ps1TempBlocks; ++t) {
        uint16 s           = TEMPENTITY_START + t;
        objectEntityPtr[s] = Carve(p, s, PS1_TEMP_IS_LARGE(t) ? maxCap : PS1_TEMP_SMALL_CAP);
    }
    // 7b: the pool
    SortRecs(s_dRec, s_dN);
    s_dLo = s_dHi = 0;
    s_dPool = pool, s_dCap = dormCap, s_dRange = (int32)dormRange;
    s_dFree   = (EntityBase **)p;
    s_dMatBlk = s_dFree + pool;
    s_dMatRec = (uint16 *)(s_dMatBlk + pool);
    p += pool * 2 * sizeof(EntityBase *) + Align4(pool * sizeof(uint16));
    s_dFreeN = s_dMatN = 0;
    for (int32 k = 0; k < pool; ++k) s_dFree[s_dFreeN++] = Carve(p, 0, dormCap);
    g_ps1DormantRecords = s_dN;
    g_ps1DormantPool    = pool;
    g_ps1DormantLive    = 0;

    s_spare         = p;
    s_spareEnd      = s_arena + bytes;
    ps1SceneSlotEnd = slot;
    ps1TempSlotEnd  = TEMPENTITY_START + ps1TempBlocks;

    if (dormYs)
        RemoveStorageEntry((void **)&dormYs);
    if (dormXY)
        RemoveStorageEntry((void **)&dormXY);
    RemoveStorageEntry((void **)&index);
    for (int32 b = 0; b < s_blockCount; ++b) {
        if (!s_blockTail[b])
            RemoveStorageEntry((void **)&s_blocks[b]);
    }
    PS1StorageTailRelease(DATASET_STG);
    s_blockCount = 0;
    s_rec        = NULL;
}

void RSDK::PS1SceneChildVar(uint16 classID, uint16 offset, uint8 type)
{
    (void)type;
    for (int32 c = 0; c < s_childVars; ++c)
        if (s_childClass[c] == classID)
            return;
    if (s_childVars < PS1_CHILD_VARS) {
        s_childClass[s_childVars]  = classID;
        s_childOffset[s_childVars] = offset;
        ++s_childVars;
    }
}

// ---- 7b: dormant entities at run time ----
volatile uint32 RSDK::g_ps1DormantRecords = 0, RSDK::g_ps1DormantPool = 0, RSDK::g_ps1DormantLive = 0;
volatile uint32 RSDK::g_ps1DormantPeak = 0, RSDK::g_ps1DormantFail = 0, RSDK::g_ps1DormantCreated = 0;
volatile uint32 RSDK::g_ps1DormantReturned = 0, RSDK::g_ps1DormantGone = 0, RSDK::g_ps1DormantDropped = 0;

namespace
{
void DormClear()
{
    s_dN = s_dMatN = s_dFreeN = s_dPool = 0;
    s_dRec = NULL;
}

// Record `i`'s entity as its Create makes it, in `e` (a pool block or the scratch), with the stock create's context
void DormCreate(const DormRec *r, EntityBase *e)
{
    const DormUnique *u = s_dUnique[r->unique];
    memset(e, 0, s_dCap); // Place's writes (FillEntity), from the class's layout and the raw bytes
    e->classID    = u->classID;
    e->filter     = 0xFF;
    e->position.x = r->x;
    e->position.y = r->y;
    const uint8 *raw = (const uint8 *)(u + 1);
    for (int32 v = 0; v < s_dLayoutN[u->spec]; ++v) {
        const DormVar *dv = &s_dLayout[u->spec][v];
        if (dv->offset + dv->len <= s_dCap)
            memcpy((uint8 *)e + dv->offset, raw, dv->len);
        raw += dv->len;
    }
    Entity *curEnt       = sceneInfo.entity;
    int32 curSlot        = sceneInfo.entitySlot;
    sceneInfo.entity     = e;
    sceneInfo.entitySlot = r->slot & ~PS1_DORMANT_GONE;
    e->interaction       = true; // InitObjects' create loop
    ps1DormantSpecs[u->spec].create(NULL);
    sceneInfo.entity     = curEnt;
    sceneInfo.entitySlot = curSlot;
}

// Is the live entity `e` what record `r`'s Create makes (its spec's transient fields aside)?
bool DormPristine(const DormRec *r, const EntityBase *e)
{
    if (e->inRange || e->onScreen)
        return false;
    EntityBase *f = (EntityBase *)s_dScratch;
    DormCreate(r, f);
    const uint16 *t = ps1DormantSpecs[s_dUnique[r->unique]->spec].transient;
    uint32 o[2] = { t[0], t[2] }, n[2] = { t[1], t[3] };
    if (n[1] && (!n[0] || o[1] < o[0])) { // in offset order
        uint32 sw = o[0];
        o[0] = o[1], o[1] = sw;
        sw = n[0];
        n[0] = n[1], n[1] = sw;
    }
    const uint8 *a = (const uint8 *)e, *b = (const uint8 *)f;
    uint32 at = 0;
    for (int32 k = 0; k < 2; ++k) {
        if (!n[k])
            continue;
        if (memcmp(a + at, b + at, o[k] - at))
            return false;
        at = o[k] + n[k];
    }
    return !memcmp(a + at, b + at, s_dCap - at);
}

// Back to a record: the slot reads as a ghost, the block returns to the pool, the record leaves the live list
inline void DormRelease(int32 k, bool ghost)
{
    uint16 slot = s_dRec[s_dMatRec[k]].slot & ~PS1_DORMANT_GONE;
    if (ghost)
        objectEntityPtr[slot] = Ghost(slot);
    s_dFree[s_dFreeN++] = s_dMatBlk[k];
    --s_dMatN;
    s_dMatRec[k] = s_dMatRec[s_dMatN];
    s_dMatBlk[k] = s_dMatBlk[s_dMatN];
}

// The live records out of every camera's box `box`, as their Create left them, go back to records
void DormReturn(const int32 (*box)[4])
{
    for (int32 k = s_dMatN - 1; k >= 0; --k) {
        DormRec *r      = &s_dRec[s_dMatRec[k]];
        uint16 slot     = r->slot & ~PS1_DORMANT_GONE;
        EntityBase *e   = objectEntityPtr[slot];
        EntityBase *blk = s_dMatBlk[k];
        if (e != blk) { // the slot got another block (a reset or copy into a bigger class): the record is over
            r->slot |= PS1_DORMANT_GONE;
            g_ps1DormantDropped = g_ps1DormantDropped + 1;
            DormRelease(k, false);
        }
        else if (!e->classID) { // destroyed (a ring collected): blank, as a ghost reads
            r->slot |= PS1_DORMANT_GONE;
            g_ps1DormantGone = g_ps1DormantGone + 1;
            DormRelease(k, true);
        }
        else if (e->classID != s_dUnique[r->unique]->classID) { // reset into another class: an entity for good
            r->slot |= PS1_DORMANT_GONE;
            g_ps1DormantDropped = g_ps1DormantDropped + 1;
            --s_dPool; // its block stays in use
            --s_dMatN;
            s_dMatRec[k] = s_dMatRec[s_dMatN];
            s_dMatBlk[k] = s_dMatBlk[s_dMatN];
        }
        else if (!InBoxes(box, s_dCams, r->x, r->y, s_dUnique[r->unique]->range) && DormPristine(r, e)) {
            g_ps1DormantReturned = g_ps1DormantReturned + 1;
            DormRelease(k, true);
        }
    }
}
} // namespace

void RSDK::PS1DormantUpdate()
{
    if (!s_dN)
        return;
    // the cameras' boxes: stock's range test |p - c| <= range + offset, grown by the margins (16.16, saturated)
    s_dCams      = cameraCount < PS1_DORMANT_CAMERAS ? cameraCount : PS1_DORMANT_CAMERAS;
    int32 bandX0 = 0x7FFFFFFF, bandX1 = -0x7FFFFFFF - 1;
    for (int32 c = 0; c < s_dCams; ++c) {
        int32 cx = cameras[c].position.x, cy = cameras[c].position.y;
        for (int32 b = 0; b < 2; ++b) {
            int32 m    = b ? PS1_DORMANT_RETURN : PS1_DORMANT_CREATE;
            int32 *box = b ? s_dBoxR[c] : s_dBoxC[c];
            box[0]     = SatAdd(cx, -(cameras[c].offset.x + m));
            box[1]     = SatAdd(cx, cameras[c].offset.x + m);
            box[2]     = SatAdd(cy, -(cameras[c].offset.y + m));
            box[3]     = SatAdd(cy, cameras[c].offset.y + m);
        }
        if (SatAdd(s_dBoxC[c][0], -s_dRange) < bandX0) // the creation band: grown by the largest range
            bandX0 = SatAdd(s_dBoxC[c][0], -s_dRange);
        if (SatAdd(s_dBoxC[c][1], s_dRange) > bandX1)
            bandX1 = SatAdd(s_dBoxC[c][1], s_dRange);
    }

    DormReturn(s_dBoxR);

    // the creation band: the x-sorted records in [bandX0, bandX1]
    while (s_dLo > 0 && s_dRec[s_dLo - 1].x >= bandX0) --s_dLo;
    while (s_dLo < s_dN && s_dRec[s_dLo].x < bandX0) ++s_dLo;
    if (s_dHi < s_dLo)
        s_dHi = s_dLo;
    while (s_dHi < s_dN && s_dRec[s_dHi].x <= bandX1) ++s_dHi;
    while (s_dHi > s_dLo && s_dRec[s_dHi - 1].x > bandX1) --s_dHi;
    for (int32 i = s_dLo; i < s_dHi; ++i) {
        DormRec *r = &s_dRec[i];
        if ((r->slot & PS1_DORMANT_GONE) || !InBoxes(s_dBoxC, s_dCams, r->x, r->y, s_dUnique[r->unique]->range)
            || !PS1EntityIsGhost(objectEntityPtr[r->slot]))
            continue;
        if (!s_dFreeN) // the pool is out: the live ones beyond the creation boxes go first
            DormReturn(s_dBoxC);
        if (!s_dFreeN) {
            g_ps1DormantFail = g_ps1DormantFail + 1;
            continue;
        }
        EntityBase *e         = s_dFree[--s_dFreeN];
        ((PS1EntityHeader *)e - 1)->slot = r->slot;
        DormCreate(r, e);
        objectEntityPtr[r->slot] = e;
        s_dMatRec[s_dMatN]       = (uint16)i;
        s_dMatBlk[s_dMatN++]     = e;
        g_ps1DormantCreated      = g_ps1DormantCreated + 1;
    }
    g_ps1DormantLive = s_dMatN;
    if ((uint32)s_dMatN > g_ps1DormantPeak)
        g_ps1DormantPeak = s_dMatN;
}

// Stock copies sizeof(EntityBase) bytes. Here: the source's class (its whole block for a slot-less pointer), into a
// destination slot block made big enough, the rest of that block cleared.
extern "C" {
volatile uint32 g_ps1EntityCopies = 0; // CopyEntity calls (total): they copy inRange / onScreen (3.0f: the passes scan all)
}

void RSDK::CopyEntity(void *destEntity, void *srcEntity, bool32 clearSrcEntity)
{
    g_ps1EntityCopies = g_ps1EntityCopies + 1;
    if (!destEntity || !srcEntity)
        return;
    EntityBase *src = (EntityBase *)srcEntity;
    uint32 n;
    if (PS1EntityIsGhost(src))
        n = 0;
    else if (PS1EntitySlotOf(src) < 0)
        n = sizeof(EntityBase);
    else {
        n = src->classID ? objectClassList[stageObjectIDs[src->classID]].entityClassSize : sizeof(Entity);
        if (n > PS1EntityCapacity(src))
            n = PS1EntityCapacity(src);
    }
    EntityBase *dst = (EntityBase *)destEntity;
    int32 dslot     = PS1EntitySlotOf(dst);
    if (dslot >= 0) {
        if (!n && PS1EntityIsGhost(dst))
            return; // an empty slot copied onto an empty slot
        if (n && !(dst = PS1EntityEnsure(dst, dslot, n)))
            return;
        uint32 cap = PS1EntityCapacity(dst);
        memcpy(dst, src, n);
        if (cap > n)
            memset((uint8 *)dst + n, 0, cap - n);
    }
    else {
        memcpy(dst, src, n);
    }
    if (clearSrcEntity && n)
        memset(src, 0, n);
}
