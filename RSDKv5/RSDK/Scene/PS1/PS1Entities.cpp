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
extern "C" const PS1GrowSpec ps1GrowSpecs[];   // 7b.7: (Zone.c) scene classes whose Create becomes another class
extern "C" const char *const ps1TempStageMarker; // 7b: (Zone.c) a stage with this class gets PS1_TEMP_BLOCKS_MANY
extern "C" const char *const ps1TempLargeStageMarker; // 7b.14: (Zone.c) a stage with this class: PS1_TEMP_BLOCKS_LARGE, all large
__attribute__((noinline)) int32 StageClassOf(const char *name)
{
    RETRO_HASH_MD5(hash);
    GenerateHashMD5(hash, (char *)name, (int32)strlen(name));
    for (int32 o = 1; o < sceneInfo.classCount; ++o)
        if (HASH_MATCH_MD5(objectClassList[stageObjectIDs[o]].hash, hash))
            return o;
    return 0;
}

// 7b.7: a scene entity's block: its class, or the larger class its Create turns it into (ps1GrowSpecs)
uint16 s_growCap[TYPE_COUNT];
void SceneCapsBegin()
{
    memset(s_growCap, 0, sizeof(s_growCap));
    for (const PS1GrowSpec *g = ps1GrowSpecs; g->from; ++g) {
        // 7b.13: "Blank Object" = the scene's blank entities (class 0: a ghost slot unless an entry here gives it a block)
        bool blank = !strcmp(g->from, "Blank Object");
        int32 a = blank ? 0 : StageClassOf(g->from), b = g->to ? StageClassOf(g->to) : 0;
        uint32 cap = 0; // 7b.13: several entries for one class: the largest the stage has
        if ((a || blank) && b)
            cap = Align4(objectClassList[stageObjectIDs[b]].entityClassSize);
        else if ((a || blank) && !g->to)
            cap = sizeof(EntityBase);
        if (cap > s_growCap[a])
            s_growCap[a] = (uint16)cap;
    }
}
inline uint32 SceneCap(uint16 classID)
{
    uint32 c = Align4(objectClassList[stageObjectIDs[classID]].entityClassSize);
    return s_growCap[classID] > c ? s_growCap[classID] : c;
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
// 7b.12: an entity whose group is past the type groups (a corrupt header; stock would write out of the table): counted,
// the first one recorded (slot | class << 16, its group), and it joins no extra group
volatile uint32 g_ps1BadGroups = 0, g_ps1BadGroupWho = 0, g_ps1BadGroupValue = 0;

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
        if (s_tgGroup[n] >= TYPEGROUP_COUNT || s_tgClass[n] >= TYPE_COUNT) {
            if (!g_ps1BadGroups)
                g_ps1BadGroupWho = s_tgSlot[n] | (uint32)s_tgClass[n] << 16, g_ps1BadGroupValue = s_tgGroup[n];
            g_ps1BadGroups = g_ps1BadGroups + 1;
            s_tgGroup[n]   = 0;
            if (s_tgClass[n] >= TYPE_COUNT)
                s_tgClass[n] = 0;
        }
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
// One block per object of the scene file: its variable layout (RecLayout: the active variables in file order), then its
// records: {u16 fileSlot, u16 classID, i32 x, i32 y, u8 filter, u8 pad, u16 payloadLen} and the variables' raw bytes
// in the layout's order (a string: u16 characters, then them), each record 4-byte aligned (7b: no per-variable headers:
// SPZ1's 2,372 entities took 237 KB of records with them).
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
struct RecVar {
    uint16 offset;
    uint8 type;
    uint8 len; // 0: a string
};
struct RecLayout {
    uint16 n;
    uint8 hasString;
    uint8 pad;
    RecVar v[1]; // n of them
};
uint32 s_blockStart[PS1_SCENE_BLOCKS];  // the records' offset in their block (after its layout)
const RecLayout *s_classLayout[TYPE_COUNT]; // a class's block's layout (classID 0, the classes this build lacks: none)
RecLayout *s_curLayout = NULL;
inline const RecLayout *LayoutOf(const SceneRec *r) { return r->classID < TYPE_COUNT ? s_classLayout[r->classID] : NULL; }
inline uint32 RecBytes(const SceneRec *r) { return Align4(sizeof(SceneRec) + r->payloadLen); }

// 7b: the classes with a "childCount" variable (PS1SceneChildVar) and the slots their entities' counts cover
#define PS1_CHILD_VARS 8
uint16 s_childClass[PS1_CHILD_VARS], s_childOffset[PS1_CHILD_VARS];
int32 s_childVars = 0;
uint32 s_noDormant[(ENTITY_COUNT + 31) / 32];

// A record's variables (its class's layout, the raw bytes) into an entity block of `cap` bytes, as the stock loader
// writes them
__attribute__((noinline)) void ApplyPayload(EntityBase *e, uint32 cap, const SceneRec *r)
{
    const RecLayout *l = LayoutOf(r);
    uint8 *eb          = (uint8 *)e;
    const uint8 *q     = (const uint8 *)(r + 1), *end = q + r->payloadLen;
    for (int32 i = 0; l && i < l->n && q < end; ++i) {
        const RecVar *v = &l->v[i];
        if (!v->len) { // a string
            uint16 chars;
            memcpy(&chars, q, 2);
            q += 2;
            if (v->offset + sizeof(String) <= cap) {
                String *string = (String *)&eb[v->offset];
                InitString(string, "", chars);
                for (string->length = 0; string->length < chars; ++string->length)
                    memcpy(&string->chars[string->length], q + 2 * string->length, 2);
            }
            q += 2 * chars;
            continue;
        }
        if (v->offset + v->len <= cap)
            memcpy(&eb[v->offset], q, v->len);
        q += v->len;
    }
}

// The entity a record makes before its Create (Place's writes): the block cleared, class, filter, position, variables
__attribute__((noinline)) void FillEntity(EntityBase *e, uint32 cap, const SceneRec *r)
{
    memset(e, 0, cap);
    e->classID    = r->classID;
    e->filter     = 0xFF;
    e->position.x = r->x;
    e->position.y = r->y;
    ApplyPayload(e, cap, r);
}

// ---- 7b (choice 113): dormant entities ----
// A dormant record: its slot (PS1_DORMANT_GONE once it is no longer dormant-managed), its payload (one copy per distinct
// payload: a scene's static rings share a handful) and position. Sorted by x: the creation pass walks a window of them.
struct DormRec {
    uint16 slot;
    uint16 unique;
    int32 x, y;
    uint16 diff; // its changed words (a DormDiff), or PS1_DORMANT_NODIFF
    uint16 flags; // PS1_DORMANT_DISABLED: gone as stock leaves a collected ring (blank, ACTIVE_DISABLED); bits 1-15:
                  // 1 + its scene position's index (a class whose Create moves it: x / y above are the created one)
};
#define PS1_DORMANT_DISABLED 1
#define PS1_DORMANT_SCENEPOS(r) (((r)->flags >> 1) - 1)
#define PS1_DORMANT_NODIFF 0xFFFF
// A record's words that differ from what its Create makes (word index in the block, value)
#define PS1_DORMANT_DIFF_WORDS 7
struct DormDiff {
    uint8 n;
    uint8 word[PS1_DORMANT_DIFF_WORDS];
    uint32 value[PS1_DORMANT_DIFF_WORDS];
};
#define PS1_DORMANT_GONE    0x8000
#define PS1_DORMANT_SPECS   32
#define PS1_DORMANT_SCRATCH 704 // the largest class a spec may name (bytes; CollapsingPlatform: 656)
#define PS1_DORMANT_CREATE  TO_FIXED(32) // creation margin beyond the stock range (16.16)
#define PS1_DORMANT_RETURN  TO_FIXED(64) // return margin (> creation: a ring at the edge does not flip every frame)
#define PS1_DORMANT_SPREAD  0 // pool bound: cameras apart by up to this many pixels (each side; 7b.3c: one camera in scope)
#define PS1_DORMANT_MAX_RANGE TO_FIXED(512) // 7b.3c: an entity with a larger range stays one (its pool share would be big)
#define PS1_DORMANT_EXTRA   16 // 7b.3c: blocks a band may add from the arena's spare when its pool is out (counted)
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
const DormUnique **s_dUnique = NULL; // the distinct payloads (in the arena; one per record at most)
int32 s_dUniques = 0;
DormRec *s_dRec  = NULL;
int32 s_dN = 0, s_dLo = 0, s_dHi = 0;
EntityBase **s_dMatBlk = NULL;
// the pools of blocks: one per size band of the specs' classes (PS1_DORMANT_BAND), each a stack of free blocks of
// the band's largest class; a spec's entities take its band's blocks (s_dCapK: its class's own size)
#define PS1_DORMANT_BANDS 3
inline int32 PS1_DORMANT_BAND(uint32 cap) { return cap <= 256 ? 0 : cap <= 448 ? 1 : 2; }
EntityBase **s_dFreeK[PS1_DORMANT_BANDS];
int32 s_dFreeNK[PS1_DORMANT_BANDS];
int32 s_dExtraK[PS1_DORMANT_BANDS];     // blocks added from the spare (at most PS1_DORMANT_EXTRA)
uint16 s_dBandCapK[PS1_DORMANT_BANDS];  // a band's block capacity
uint8 s_dBandK[PS1_DORMANT_SPECS];
uint16 s_dCapK[PS1_DORMANT_SPECS];
uint16 *s_dMatRec = NULL;
int32 s_dMatN = 0, s_dPool = 0;
DormDiff *s_dDiff = NULL;   // one per record a diff spec may return with changes
uint16 *s_dDiffFree = NULL;
int32 s_dDiffFreeN  = 0;
int32 s_dLoadNext   = 0;     // InitObjects: the next record (slot order) whose Create runs at the load
int32 *s_dScenePos  = NULL;  // (x, y) pairs: the scene file's positions of the records whose Create moves them
int32 s_dRange = 0;
// the scratch entity, after a block header: a Create into it sees a slot's block (GetEntitySlot; a destroyEntity clears
// only its capacity, not sizeof(EntityBase) as for a pointer that is no slot's)
uint8 s_dScratchBlk[sizeof(PS1EntityHeader) + PS1_DORMANT_SCRATCH] __attribute__((aligned(8)));
uint8 *const s_dScratch = s_dScratchBlk + sizeof(PS1EntityHeader);
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
    const RecLayout *l = LayoutOf(r);
    for (int32 c = 0; l && !l->hasString && c < s_childVars; ++c) {
        if (s_childClass[c] != r->classID)
            continue;
        const uint8 *q = (const uint8 *)(r + 1);
        for (int32 i = 0; i < l->n; q += l->v[i].len, ++i) {
            if (l->v[i].offset != s_childOffset[c])
                continue;
            int32 v = 0;
            memcpy(&v, q, l->v[i].len < 4 ? l->v[i].len : 4);
            if (l->v[i].len == 1 && l->v[i].type == VAR_INT8)
                v = (int8)v;
            else if (l->v[i].len == 2 && l->v[i].type == VAR_INT16)
                v = (int16)v;
            return v;
        }
    }
    return 0;
}

inline bool HasString(const SceneRec *r)
{
    const RecLayout *l = LayoutOf(r);
    return !l || l->hasString;
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
    s_curLayout           = NULL;
    memset(s_classLayout, 0, sizeof(s_classLayout));
}

void RSDK::PS1SceneObjectBegin(uint32 bytes, uint16 classID, uint16 vars)
{
    if (s_rec && s_blockCount < PS1_SCENE_BLOCKS) {
        s_blockUsed[s_blockCount - 1] = Align4(s_recUsed);
        g_ps1SceneRecordBytes         = g_ps1SceneRecordBytes; // (peak kept in Place)
    }
    s_rec = NULL;
    s_recSize = s_recUsed = 0;
    s_cur                 = NULL;
    s_curLayout           = NULL;
    if (!bytes)
        return;
    bytes += Align4(sizeof(RecLayout) + vars * sizeof(RecVar)); // its layout first
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
    s_curLayout                = (RecLayout *)s_rec;
    s_curLayout->n             = 0;
    s_curLayout->hasString     = 0;
    s_recUsed                  = Align4(sizeof(RecLayout) + vars * sizeof(RecVar));
    s_blockStart[s_blockCount] = s_recUsed;
    s_blockUsed[s_blockCount]  = s_recUsed;
    if (classID && classID < TYPE_COUNT)
        s_classLayout[classID] = s_curLayout;
    ++s_blockCount;
}

void RSDK::PS1SceneObjectVar(uint16 offset, uint8 type, uint16 len)
{
    if (!s_curLayout)
        return;
    RecVar *v = &s_curLayout->v[s_curLayout->n++];
    v->offset = offset;
    v->type   = type;
    v->len    = type == VAR_STRING ? 0 : (uint8)len;
    if (type == VAR_STRING)
        s_curLayout->hasString = 1;
}

void RSDK::PS1SceneEntityBegin(uint16 fileSlot, uint16 classID, int32 x, int32 y)
{
    s_cur = NULL;
    if (!s_rec || s_recUsed + sizeof(SceneRec) > s_recSize) {
        g_ps1SceneRecordDropped = g_ps1SceneRecordDropped + 1;
        return;
    }
    s_recUsed         = Align4(s_recUsed); // (the previous record's raw bytes end anywhere)
    if (s_recUsed + sizeof(SceneRec) > s_recSize) {
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
    uint32 item = type == VAR_STRING ? 2 + len : len; // raw: a string's character count first
    if (s_recUsed + item > s_recSize) {
        g_ps1SceneRecordDropped = g_ps1SceneRecordDropped + 1;
        return;
    }
    uint8 *p = s_rec + s_recUsed;
    if (type == VAR_STRING) {
        uint16 chars = len / 2;
        memcpy(p, &chars, 2);
        p += 2;
    }
    memcpy(p, data, len);
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
        s_blockUsed[s_blockCount - 1] = Align4(s_recUsed);
    uint32 recordBytes = 0;
    for (int32 b = 0; b < s_blockCount; ++b) recordBytes += s_blockSize[b];
    if (recordBytes > g_ps1SceneRecordBytes)
        g_ps1SceneRecordBytes = recordBytes;

    // file slot -> record (a later record for the same slot replaces an earlier one, as stock)
    SceneRec **index = NULL;
    AllocateStorage((void **)&index, PS1_SCENE_FILESLOTS * sizeof(SceneRec *), DATASET_TMP, true);
    for (int32 b = 0; b < s_blockCount; ++b) {
        for (uint32 p = s_blockStart[b]; p < s_blockUsed[b];) {
            SceneRec *r = (SceneRec *)(s_blocks[b] + p);
            if (r->fileSlot < PS1_SCENE_FILESLOTS)
                index[r->fileSlot] = r;
            else
                g_ps1EntitiesDropped = g_ps1EntitiesDropped + 1;
            p += RecBytes(r);
        }
    }

    // 7b: the classes that may go dormant this stage (the game's specs name them)
    memset(s_classSpec, 0, sizeof(s_classSpec));
    int32 candidates = 0;
    for (int32 k = 0; k < PS1_DORMANT_SPECS && ps1DormantSpecs[k].name; ++k) {
        bool walked = false; // a class of the stage walks or keeps this one's entities (foreach_all, pointers)
        for (int32 u = 0; u < 3 && ps1DormantSpecs[k].unless[u]; ++u) walked |= StageClassOf(ps1DormantSpecs[k].unless[u]) != 0;
        int32 o = walked ? 0 : StageClassOf(ps1DormantSpecs[k].name);
        if (o && o < TYPE_COUNT && objectClassList[stageObjectIDs[o]].create
            && Align4(objectClassList[stageObjectIDs[o]].entityClassSize) <= PS1_DORMANT_SCRATCH)
            s_classSpec[o] = (uint8)(k + 1);
    }
    for (int32 f = 0; index && f < PS1_SCENE_FILESLOTS; ++f)
        if (index[f] && index[f]->classID < TYPE_COUNT && s_classSpec[index[f]->classID])
            ++candidates;
    int32 *dormXY = NULL, *dormYs = NULL; // (x, y, spec) of each dormant record, pixels
    // per candidate: x, y, spec; the bound's scratch; its distinct payload (record pointer, range); its record's payload
    // index (STG's tail, below the records: released with them at the end)
    const uint32 perCand = 6 * sizeof(int32) + sizeof(void *) + sizeof(int32) + sizeof(uint32);
    const SceneRec **uniqRec = NULL;
    int32 *uniqRange = NULL;
    uint32 *uniqOf   = NULL;
    if (candidates) {
        dormXY = (int32 *)PS1StorageTailAlloc(DATASET_STG, candidates * perCand);
        if (dormXY) {
            dormYs    = dormXY + 3 * candidates;
            uniqRec   = (const SceneRec **)(dormYs + 3 * candidates);
            uniqRange = (int32 *)(uniqRec + candidates);
            uniqOf    = (uint32 *)(uniqRange + candidates);
        }
        else
            candidates = 0; // (no room: nothing goes dormant in this scene)
    }
    memset(s_noDormant, 0, sizeof(s_noDormant));
    s_dUniques       = 0;
    const SceneRec *layoutRec[PS1_DORMANT_SPECS] = {};
    uint8 layoutN[PS1_DORMANT_SPECS]             = {};
    int32 dormant    = 0, diffRecs = 0, movedRecs = 0;
    uint32 blobBytes = 0, dormRange = 0;
    uint16 capK[PS1_DORMANT_SPECS]  = {};
    int32 poolK[PS1_DORMANT_BANDS]  = {};
    uint16 bandCap[PS1_DORMANT_BANDS] = {};

    // capacities: reserved + scene (in final order) + temp + spare
    uint32 maxCap = MaxClassSize();
    uint32 bytes  = 0;
    uint16 resvCap[RESERVE_ENTITY_COUNT];
    ReservedCaps(maxCap, resvCap);
    SceneCapsBegin();
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
            uint32 classBytes = SceneCap(r->classID);
            // 7b: the slots this entity's childCount covers stay entities
            int32 children = s_childVars ? ChildCount(r) : 0;
            for (int32 c = slot + 1; c <= slot + children && c < ENTITY_COUNT; ++c) s_noDormant[c >> 5] |= 1u << (c & 31);
            uint8 spec = candidates && r->classID < TYPE_COUNT ? s_classSpec[r->classID] : 0;
            uint32 raw = r->payloadLen; // (one layout per class: every record of a class has it)
            if (spec && !((s_noDormant[slot >> 5] >> (slot & 31)) & 1) && !HasString(r)) {
                EntityBase *pre = (EntityBase *)s_dScratch;
                FillEntity(pre, classBytes, r);
                if (ps1DormantSpecs[spec - 1].eligible(pre)) {
                    int32 u = 0; // its payload among the distinct ones (pointers into the records until the arena exists)
                    for (; u < s_dUniques; ++u) {
                        const SceneRec *o = uniqRec[u];
                        if (o->classID == r->classID && o->payloadLen == r->payloadLen
                            && !memcmp(o + 1, r + 1, r->payloadLen))
                            break;
                    }
                    if (u == s_dUniques) {
                        const PS1DormantSpec *sp = &ps1DormantSpecs[spec - 1];
                        int32 range              = sp->rangeOf ? sp->rangeOf(pre) : sp->range;
                        if (range < 0 || range > PS1_DORMANT_MAX_RANGE)
                            u = s_dUniques + 1; // such an entity stays one
                        else {
                            uniqRange[s_dUniques] = range;
                            uniqRec[s_dUniques++] = r;
                            blobBytes += sizeof(DormUnique) + Align4(raw);
                            if (!layoutRec[spec - 1]) {
                                layoutRec[spec - 1] = r;
                                layoutN[spec - 1]   = (uint8)LayoutOf(r)->n;
                                blobBytes += layoutN[spec - 1] * sizeof(DormVar);
                            }
                        }
                    }
                    if (u < s_dUniques) {
                        if (ps1DormantSpecs[spec - 1].diffs)
                            ++diffRecs;
                        if (ps1DormantSpecs[spec - 1].movesInCreate)
                            ++movedRecs;
                        r->pad          = 1; // dormant: its payload index in uniqOf (the dormant records' order)
                        uniqOf[dormant] = (uint32)u;
                        dormXY[3 * dormant]     = r->x >> 16;
                        dormXY[3 * dormant + 1] = r->y >> 16;
                        dormXY[3 * dormant + 2] = spec - 1;
                        ++dormant;
                        if (classBytes > capK[spec - 1])
                            capK[spec - 1] = (uint16)classBytes;
                        if ((uint32)uniqRange[u] > dormRange)
                            dormRange = uniqRange[u];
                    }
                }
            }
            if (!r->pad)
                bytes += sizeof(PS1EntityHeader) + classBytes;
        }
        else if (s_growCap[0]) // 7b.13: a blank entity the stage's grow entries give a block
            bytes += sizeof(PS1EntityHeader) + SceneCap(0);
        ++placed;
    }
    // 7b: the temp blocks (more in a stage with the game's ps1TempStageMarker class: the Mean Bean Machine's beans)
    ps1TempBlocks     = StageClassOf(ps1TempStageMarker) ? PS1_TEMP_BLOCKS_MANY : PS1_TEMP_BLOCKS;
    ps1TempLargeEvery = ps1TempBlocks / PS1_TEMP_LARGE_COUNT;
    if (StageClassOf(ps1TempLargeStageMarker)) // 7b.14: every temp block large (the credits' text lines, ~1 KB each)
        ps1TempBlocks = PS1_TEMP_BLOCKS_LARGE, ps1TempLargeEvery = 1;
    int32 tempLarge   = ps1TempBlocks / ps1TempLargeEvery;
    bytes += (ps1TempBlocks - tempLarge) * (sizeof(PS1EntityHeader) + PS1_TEMP_SMALL_CAP);
    bytes += tempLarge * (sizeof(PS1EntityHeader) + maxCap);
    // 7b: the dormant records, their payloads and a pool of blocks for the most records the cameras' boxes reach at once
    int32 pool = 0;
    if (dormant) {
        // each class's pool: the most of its records a camera box reaches (its points alone through BoxBound) + 2
        int32 ox = screens[0].center.x ? screens[0].center.x : 160, oy = screens[0].center.y ? screens[0].center.y : 120;
        for (int32 k = 0; k < PS1_DORMANT_SPECS; ++k)
            if (capK[k] > bandCap[PS1_DORMANT_BAND(capK[k])])
                bandCap[PS1_DORMANT_BAND(capK[k])] = capK[k];
        // 7b.3c: a band's records by range tier, each tier's box bound with its own largest range, summed (>= the max)
        static const int32 tierPx[] = { 64, 128, 256, PS1_DORMANT_MAX_RANGE >> 16 };
        for (int32 band = 0; band < PS1_DORMANT_BANDS; ++band) {
            int32 all = 0;
            for (int32 t = 0; t < 4; ++t) {
                int32 n = 0, lo = t ? tierPx[t - 1] : -1;
                for (int32 i = 0; i < dormant; ++i) {
                    int32 r = uniqRange[uniqOf[i]] >> 16;
                    if (capK[dormXY[3 * i + 2]] && PS1_DORMANT_BAND(capK[dormXY[3 * i + 2]]) == band && r > lo && r <= tierPx[t])
                        dormYs[2 * n] = dormXY[3 * i], dormYs[2 * n + 1] = dormXY[3 * i + 1], ++n;
                }
                if (!n)
                    continue;
                all += n;
                int32 m = tierPx[t] + FROM_FIXED(PS1_DORMANT_RETURN) + PS1_DORMANT_SPREAD;
                poolK[band] += BoxBound(dormYs, dormYs + 2 * n, n, 2 * (ox + m), 2 * (oy + m));
            }
            if (!all)
                continue;
            poolK[band] += 3;
            if (poolK[band] > all)
                poolK[band] = all;
            pool += poolK[band];
            bytes += poolK[band] * (sizeof(PS1EntityHeader) + bandCap[band]);
        }
        bytes += dormant * sizeof(DormRec) + blobBytes + s_dUniques * sizeof(DormUnique *);
        int32 slots = pool + PS1_DORMANT_BANDS * PS1_DORMANT_EXTRA; // free stacks and the live list: room for extra blocks
        bytes += slots * 2 * sizeof(EntityBase *) + Align4(slots * sizeof(uint16));
        bytes += diffRecs * sizeof(DormDiff) + Align4(diffRecs * sizeof(uint16)) + movedRecs * 2 * sizeof(int32);
    }
    uint32 spareBytes = 6 * 1024; // GHZ1 play: 0 relocations (2.3m); 8.5: 8 -> 6 KB (0 relocations / extra blocks in every boot)
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
        DormVar *v         = (DormVar *)p;
        const RecLayout *l = LayoutOf(layoutRec[k]);
        for (int32 i = 0; i < layoutN[k]; ++i) {
            v[i].offset = l->v[i].offset;
            v[i].len    = l->v[i].len;
        }
        p += layoutN[k] * sizeof(DormVar);
    }
    s_dUnique = (const DormUnique **)p;
    p += s_dUniques * sizeof(DormUnique *);
    for (int32 u = 0; u < s_dUniques; ++u) { // the distinct payloads: their raw bytes
        const SceneRec *r = uniqRec[u];
        DormUnique *d     = (DormUnique *)p;
        d->classID        = r->classID;
        d->spec           = (uint16)(s_classSpec[r->classID] - 1);
        d->pad            = 0;
        d->range          = uniqRange[u];
        uint32 n          = r->payloadLen;
        memcpy(d + 1, r + 1, n);
        d->len       = (uint16)n;
        s_dUnique[u] = d;
        p += sizeof(DormUnique) + Align4(n);
    }
    s_dScenePos      = (int32 *)p;
    p += movedRecs * 2 * sizeof(int32);
    int32 scenePosN  = 0;

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
            d->unique  = (uint16)uniqOf[s_dN - 1];
            d->x       = r->x;
            d->y       = r->y;
            d->diff    = PS1_DORMANT_NODIFF;
            d->flags   = 0;
            if (ps1DormantSpecs[s_classSpec[r->classID] - 1].movesInCreate) {
                s_dScenePos[2 * scenePosN]     = r->x;
                s_dScenePos[2 * scenePosN + 1] = r->y;
                d->flags                       = (uint16)((++scenePosN) << 1);
            }
        }
        else if (r->classID || s_growCap[0]) { // (7b.13: blank entities too when the stage's grow entries say so)
            EntityBase *e = Carve(p, slot, SceneCap(r->classID));
            objectEntityPtr[slot] = e;
            FillEntity(e, PS1EntityCapacity(e), r);
        }
        // classID 0 (a class this build does not have, or a blank entity): the slot stays a ghost (stock: a blank entity
        // with a position)
        ++slot;
    }

    for (int32 t = 0; t < ps1TempBlocks; ++t) {
        uint16 s           = TEMPENTITY_START + t;
        objectEntityPtr[s] = Carve(p, s, PS1_TEMP_IS_LARGE(t) ? maxCap : PS1_TEMP_SMALL_CAP);
    }
    // 7b: the pool (the records stay in slot order until InitObjects has run their Creates: PS1DormantLoadDone sorts)
    s_dLoadNext = 0;
    s_dLo = s_dHi = 0;
    s_dPool = pool, s_dRange = (int32)dormRange;
    int32 slots         = pool + PS1_DORMANT_BANDS * PS1_DORMANT_EXTRA;
    EntityBase **stacks = (EntityBase **)p;
    s_dMatBlk = stacks + slots;
    s_dMatRec = (uint16 *)(s_dMatBlk + slots);
    p += slots * 2 * sizeof(EntityBase *) + Align4(slots * sizeof(uint16));
    s_dMatN = 0;
    for (int32 k = 0; k < PS1_DORMANT_SPECS; ++k) {
        s_dCapK[k]  = capK[k];
        s_dBandK[k] = (uint8)PS1_DORMANT_BAND(capK[k]);
    }
    for (int32 band = 0; band < PS1_DORMANT_BANDS; ++band) {
        s_dFreeK[band]    = stacks;
        s_dFreeNK[band]   = 0;
        s_dExtraK[band]   = 0;
        s_dBandCapK[band] = bandCap[band];
        stacks += poolK[band] + PS1_DORMANT_EXTRA;
        for (int32 b = 0; b < poolK[band]; ++b) s_dFreeK[band][s_dFreeNK[band]++] = Carve(p, 0, bandCap[band]);
    }
    s_dDiff     = (DormDiff *)p;
    p += diffRecs * sizeof(DormDiff);
    s_dDiffFree = (uint16 *)p;
    p += Align4(diffRecs * sizeof(uint16));
    for (s_dDiffFreeN = 0; s_dDiffFreeN < diffRecs; ++s_dDiffFreeN) s_dDiffFree[s_dDiffFreeN] = (uint16)s_dDiffFreeN;
    g_ps1DormantRecords = s_dN;
    g_ps1DormantPool    = pool;
    g_ps1DormantLive    = 0;

    s_spare         = p;
    s_spareEnd      = s_arena + bytes;
    ps1SceneSlotEnd = slot;
    ps1TempSlotEnd  = TEMPENTITY_START + ps1TempBlocks;

    // (the candidates' scratch: STG's tail, released below with the records)
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
volatile uint32 RSDK::g_ps1DormantExtra = 0; // 7b.3c: pool blocks added from the spare (total)
volatile uint32 RSDK::g_ps1DormantReturned = 0, RSDK::g_ps1DormantGone = 0, RSDK::g_ps1DormantDropped = 0;
volatile uint32 RSDK::g_ps1DormantDiffs = 0, RSDK::g_ps1DormantBadActive = 0;

namespace
{
void DormClear()
{
    s_dN = s_dMatN = s_dPool = 0;
    for (int32 k = 0; k < PS1_DORMANT_BANDS; ++k) s_dFreeNK[k] = 0;
    s_dRec = NULL;
}

// Record `i`'s entity as its Create makes it, in `e` (a pool block or the scratch), with the stock create's context
void DormCreate(const DormRec *r, EntityBase *e)
{
    const DormUnique *u = s_dUnique[r->unique];
    const uint32 cap = s_dCapK[u->spec];
    memset(e, 0, cap); // Place's writes (FillEntity), from the class's layout and the raw bytes
    e->classID    = u->classID;
    e->filter     = 0xFF;
    e->position.x = r->flags >> 1 ? s_dScenePos[2 * PS1_DORMANT_SCENEPOS(r)] : r->x;
    e->position.y = r->flags >> 1 ? s_dScenePos[2 * PS1_DORMANT_SCENEPOS(r) + 1] : r->y;
    const uint8 *raw = (const uint8 *)(u + 1);
    for (int32 v = 0; v < s_dLayoutN[u->spec]; ++v) {
        const DormVar *dv = &s_dLayout[u->spec][v];
        if (dv->offset + dv->len <= cap)
            memcpy((uint8 *)e + dv->offset, raw, dv->len);
        raw += dv->len;
    }
    // the Create runs as at the load: on the slot's entity (the slot points at `e` meanwhile: a block, with its header)
    uint16 slot          = r->slot & ~PS1_DORMANT_GONE;
    PS1EntityHeader *h   = (PS1EntityHeader *)e - 1;
    h->slot              = slot;
    h->capacity          = (uint16)cap;
    EntityBase *was      = objectEntityPtr[slot];
    objectEntityPtr[slot] = e;
    Entity *curEnt       = sceneInfo.entity;
    int32 curSlot        = sceneInfo.entitySlot;
    sceneInfo.entity     = e;
    sceneInfo.entitySlot = slot;
    e->interaction       = true; // InitObjects' create loop
    objectClassList[stageObjectIDs[u->classID]].create(NULL);
    sceneInfo.entity     = curEnt;
    sceneInfo.entitySlot = curSlot;
    if (objectEntityPtr[slot] == e) // (a Create that moved its slot elsewhere: counted by the caller's checks)
        objectEntityPtr[slot] = was;
}

// May the live entity `e` go back to record `r`? Out of range and in its bounds mode (ACTIVE_BOUNDS: out of reach it
// would not run), and what its Create makes (a scratch Create, compared word by word, the spec's transient bytes aside)
// but for at most PS1_DORMANT_DIFF_WORDS words a diff spec keeps in `diff` (none may be its class, position or
// range: the record's box test reads those). Returns false if not.
bool DormCompare(const DormRec *r, const EntityBase *e, DormDiff *diff)
{
    if (e->inRange || e->onScreen || e->active != ACTIVE_BOUNDS)
        return false;
    EntityBase *f = (EntityBase *)s_dScratch;
    DormCreate(r, f);
    const PS1DormantSpec *sp = &ps1DormantSpecs[s_dUnique[r->unique]->spec];
    const uint32 *a = (const uint32 *)e, *b = (const uint32 *)f;
    diff->n = 0;
    for (uint32 w = 0; w < s_dCapK[s_dUnique[r->unique]->spec] / 4u; ++w) {
        uint32 m = 0xFFFFFFFF; // the word's bytes that count (the transient ones do not)
        for (int32 k = 0; k < 4; k += 2) {
            uint32 o = sp->transient[k], n = sp->transient[k + 1];
            for (uint32 i = 0; i < 4; ++i)
                if (n && 4 * w + i >= o && 4 * w + i < o + n)
                    m &= ~(0xFFu << (8 * i));
        }
        if (!((a[w] ^ b[w]) & m))
            continue;
        uint32 at = 4 * w;
        if (!sp->diffs || diff->n == PS1_DORMANT_DIFF_WORDS || at < offsetof(EntityBase, scale)
            || (at >= offsetof(EntityBase, updateRange) && at < offsetof(EntityBase, updateRange) + sizeof(Vector2)))
            return false; // (position: the record's; updateRange: the box test's)
        diff->word[diff->n]  = (uint8)w;
        diff->value[diff->n] = a[w];
        ++diff->n;
    }
    return true;
}

// Back to a record: the slot reads as a ghost, the block returns to the pool, the record leaves the live list
inline void DormRelease(int32 k, bool ghost)
{
    uint16 slot = s_dRec[s_dMatRec[k]].slot & ~PS1_DORMANT_GONE;
    if (ghost)
        objectEntityPtr[slot] = Ghost(slot);
    int32 band = s_dBandK[s_dUnique[s_dRec[s_dMatRec[k]].unique]->spec];
    s_dFreeK[band][s_dFreeNK[band]++] = s_dMatBlk[k];
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
            if (e->active == ACTIVE_DISABLED) // (SaveGame's checkpoint recall tells these apart: PS1DormantStoreDisabled)
                r->flags |= PS1_DORMANT_DISABLED;
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
        else if (!InBoxes(box, s_dCams, r->x, r->y, s_dUnique[r->unique]->range)) {
            DormDiff d;
            if (DormCompare(r, e, &d) && (!d.n || s_dDiffFreeN)) {
                if (d.n) { // its changed words wait in a diff
                    r->diff          = s_dDiffFree[--s_dDiffFreeN];
                    s_dDiff[r->diff] = d;
                    g_ps1DormantDiffs = g_ps1DormantDiffs + 1;
                }
                g_ps1DormantReturned = g_ps1DormantReturned + 1;
                DormRelease(k, true);
            }
        }
    }
}
} // namespace

// InitObjects' create loop at slot `slot`: a dormant record there runs its Create now, as stock creates every scene
// entity at the load (its side effects: ShopWindow's on the bumpers it covers), into the scratch block. One its Create
// destroys (InvisibleBlock outside time attack) is gone; one not in its bounds mode stays an entity (counted: the
// game's spec must not let it go dormant). Returns whether the slot had a record.
bool RSDK::PS1DormantLoadCreate(int32 slot)
{
    if (s_dLoadNext >= s_dN || s_dRec[s_dLoadNext].slot != slot)
        return false;
    DormRec *r    = &s_dRec[s_dLoadNext++];
    EntityBase *f = (EntityBase *)s_dScratch;
    DormCreate(r, f);
    if (r->flags >> 1) { // the camera boxes test the created position (stock's range test reads the entity's)
        r->x = f->position.x;
        r->y = f->position.y;
    }
    if (!f->classID) {
        r->slot |= PS1_DORMANT_GONE;
        if (f->active == ACTIVE_DISABLED)
            r->flags |= PS1_DORMANT_DISABLED;
        g_ps1DormantGone = g_ps1DormantGone + 1;
    }
    else if (f->active != ACTIVE_BOUNDS) {
        g_ps1DormantBadActive = g_ps1DormantBadActive + 1;
        int32 sp = s_dUnique[r->unique]->spec, band = s_dBandK[sp];
        if (s_dFreeNK[band]) { // an entity for good, from the pool
            EntityBase *e = s_dFreeK[band][--s_dFreeNK[band]];
            ((PS1EntityHeader *)e - 1)->slot = (uint16)slot;
            memcpy(e, f, s_dCapK[sp]);
            objectEntityPtr[slot] = e;
            --s_dPool;
        }
        r->slot |= PS1_DORMANT_GONE;
    }
    return true;
}

// 7b: SaveGame's checkpoint state (SaveGame_StoreStageState / LoadStageState) keeps per scene slot whether its entity
// was destroyed (blank, ACTIVE_DISABLED: a collected ring); a dormant slot reads as an empty one. Store: the records
// gone that way are written into the game's table too. Recall: an empty slot the table disables is no write into the
// ghost page; its record, if it has one, is gone for good.
extern "C" void PS1DormantStoreDisabled(int *table, int disabled)
{
    for (int32 i = 0; i < s_dN; ++i)
        if ((s_dRec[i].slot & PS1_DORMANT_GONE) && (s_dRec[i].flags & PS1_DORMANT_DISABLED))
            table[s_dRec[i].slot & ~PS1_DORMANT_GONE] = disabled;
}

extern "C" int PS1EntitySlotIsGhost(int slot) { return slot >= 0 && slot < ENTITY_COUNT && PS1EntityIsGhost(objectEntityPtr[slot]); }

extern "C" void PS1DormantRecallDisabled(int slot)
{
    for (int32 i = 0; i < s_dN; ++i)
        if ((s_dRec[i].slot & ~PS1_DORMANT_GONE) == slot) {
            s_dRec[i].slot |= PS1_DORMANT_GONE;
            s_dRec[i].flags |= PS1_DORMANT_DISABLED;
            return;
        }
}

// After InitObjects: the records sorted by x for the creation band
void RSDK::PS1DormantLoadDone()
{
    SortRecs(s_dRec, s_dN);
    s_dLo = s_dHi = 0;
}

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
        int32 band = s_dBandK[s_dUnique[r->unique]->spec];
        if (!s_dFreeNK[band]) // its pool is out: the live ones beyond the creation boxes go first
            DormReturn(s_dBoxC);
        if (!s_dFreeNK[band] && s_dExtraK[band] < PS1_DORMANT_EXTRA && s_spare
            && s_spare + sizeof(PS1EntityHeader) + s_dBandCapK[band] <= s_spareEnd) { // 7b.3c: then a block from the spare
            s_dFreeK[band][s_dFreeNK[band]++] = Carve(s_spare, 0, s_dBandCapK[band]);
            ++s_dExtraK[band];
            g_ps1DormantExtra = g_ps1DormantExtra + 1;
        }
        if (!s_dFreeNK[band]) {
            g_ps1DormantFail = g_ps1DormantFail + 1;
            continue;
        }
        EntityBase *e         = s_dFreeK[band][--s_dFreeNK[band]];
        ((PS1EntityHeader *)e - 1)->slot = r->slot;
        DormCreate(r, e);
        if (r->diff != PS1_DORMANT_NODIFF) { // as it was when it went back to a record
            const DormDiff *d = &s_dDiff[r->diff];
            for (int32 w = 0; w < d->n; ++w) ((uint32 *)e)[d->word[w]] = d->value[w];
            s_dDiffFree[s_dDiffFreeN++] = r->diff;
            r->diff                     = PS1_DORMANT_NODIFF;
        }
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
    else if (PS1EntitySlotOf(src) < 0) // a slot-less copy (globals->atlEntityData, scratch): its class, at most EntityBase
        n = src->classID && src->classID < sceneInfo.classCount ? objectClassList[stageObjectIDs[src->classID]].entityClassSize : sizeof(EntityBase);
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
