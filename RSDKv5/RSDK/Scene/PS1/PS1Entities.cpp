// PS1 port (docs/32 phase 2.3): per-slot entity storage (see PS1Entities.hpp). Included by Object.cpp.

EntityBase *RSDK::objectEntityPtr[ENTITY_COUNT];
// One zero page for every ghost: slot s reads at +4 * s, and a ghost reads sizeof(EntityBase) bytes at most.
uint8 RSDK::ps1EntityGhosts[ENTITY_COUNT * 4 + sizeof(EntityBase)] __attribute__((aligned(8)));

volatile uint32 RSDK::g_ps1EntityRelocations = 0;
volatile uint32 RSDK::g_ps1EntityGhostWrites = 0;
volatile uint32 RSDK::g_ps1EntitySpareFail   = 0;
volatile uint32 RSDK::g_ps1EntityArenaBytes  = 0;
volatile uint32 RSDK::g_ps1EntityArenaPeak   = 0;
volatile uint32 RSDK::g_ps1EntityCreateSkips = 0;
volatile uint32 RSDK::g_ps1SceneRecordBytes  = 0;
volatile uint32 RSDK::g_ps1SceneRecordDropped = 0;
volatile uint32 RSDK::g_ps1EntitiesDropped    = 0;
int32 RSDK::ps1SceneSlotEnd = RESERVE_ENTITY_COUNT;

namespace
{
uint8 *s_arena      = NULL; // DATASET_STG, one per scene
uint8 *s_spare      = NULL; // the arena's tail, bump-allocated by relocations
uint8 *s_spareEnd   = NULL;

inline uint32 Align4(uint32 v) { return (v + 3) & ~3u; }

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

void RSDK::PS1EntityStorageClear()
{
    PS1EntityCheckGhosts();
    for (int32 s = 0; s < ENTITY_COUNT; ++s) objectEntityPtr[s] = Ghost(s);
    if (s_arena)
        RemoveStorageEntry((void **)&s_arena);
    s_arena = s_spare = s_spareEnd = NULL;
    g_ps1EntityArenaBytes = 0;
    ps1SceneSlotEnd       = RESERVE_ENTITY_COUNT;
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
uint16 s_tgSlot[ENTITY_COUNT], s_tgClass[ENTITY_COUNT], s_tgGroup[ENTITY_COUNT];
uint16 s_tgPool[ENTITY_COUNT * 3];
int32 s_tgCount = 0;
} // namespace

void RSDK::PS1TypeGroupsBegin()
{
    s_tgCount = 0;
    for (int32 i = 0; i < TYPEGROUP_COUNT; ++i) typeGroups[i].entryCount = 0;
}

void RSDK::PS1TypeGroupsAdd(uint16 slot, uint16 classID, uint16 group)
{
    s_tgSlot[s_tgCount]  = slot;
    s_tgClass[s_tgCount] = classID;
    s_tgGroup[s_tgCount] = group;
    ++s_tgCount;
}

// The stock loop appends each entity to GROUP_ALL, its class's group and (group >= TYPE_COUNT) its extra group, in slot
// order: the same lists, each one contiguous in the pool.
void RSDK::PS1TypeGroupsBuild()
{
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
} // namespace

#define PS1_SCENE_FILESLOTS (2 * SCENEENTITY_COUNT) // stock: < SCENEENTITY_COUNT main list, then the temp list

void RSDK::PS1SceneEntitiesBegin()
{
    PS1StorageTailRelease(DATASET_STG); // (a load that never reached PS1SceneEntitiesPlace)
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

    // capacities: reserved + scene (in final order) + temp + spare
    uint32 maxCap = MaxClassSize();
    uint32 bytes  = 0;
    for (int32 s = 0; s < RESERVE_ENTITY_COUNT; ++s) bytes += PS1ReservedUnused(s) ? 0 : sizeof(PS1EntityHeader) + maxCap;
    int32 placed  = 0;
    for (int32 f = 0; index && f < PS1_SCENE_FILESLOTS && placed < SCENEENTITY_COUNT; ++f) {
        if (!index[f])
            continue;
        SceneRec *r = index[f];
        if (!(sceneInfo.filter & r->filter))
            continue;
        if (r->classID)
            bytes += sizeof(PS1EntityHeader) + Align4(objectClassList[stageObjectIDs[r->classID]].entityClassSize);
        ++placed;
    }
    bytes += (PS1_TEMP_BLOCKS - PS1_TEMP_LARGE_COUNT) * (sizeof(PS1EntityHeader) + PS1_TEMP_SMALL_CAP);
    bytes += PS1_TEMP_LARGE_COUNT * (sizeof(PS1EntityHeader) + maxCap);
    uint32 spareBytes = 8 * 1024; // GHZ1 play: 0 relocations (2.3m)
    bytes += spareBytes;

    s_arena = NULL;
    AllocateStorage((void **)&s_arena, bytes, DATASET_STG, true); // stops at PS1StorageExhausted if STG is too small
    g_ps1EntityArenaBytes = bytes;
    if (bytes > g_ps1EntityArenaPeak)
        g_ps1EntityArenaPeak = bytes;

    uint8 *p = s_arena;
    for (int32 s = 0; s < RESERVE_ENTITY_COUNT; ++s) {
        if (!PS1ReservedUnused(s))
            objectEntityPtr[s] = Carve(p, s, maxCap);
    }

    int32 slot = RESERVE_ENTITY_COUNT;
    for (int32 f = 0; index && f < PS1_SCENE_FILESLOTS && slot < RESERVE_ENTITY_COUNT + SCENEENTITY_COUNT; ++f) {
        if (!index[f])
            continue;
        SceneRec *r = index[f];
        if (!(sceneInfo.filter & r->filter))
            continue;
        if (r->classID) {
            EntityBase *e = Carve(p, slot, Align4(objectClassList[stageObjectIDs[r->classID]].entityClassSize));
            objectEntityPtr[slot] = e;
            e->classID            = r->classID;
            e->filter             = 0xFF;
            e->position.x         = r->x;
            e->position.y         = r->y;
            uint8 *eb             = (uint8 *)e;
            uint32 cap            = PS1EntityCapacity(e);
            for (uint8 *q = (uint8 *)(r + 1), *end = q + r->payloadLen; q < end;) {
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
        // classID 0 (a class this build does not have): the slot stays a ghost (stock: a blank entity with a position)
        ++slot;
    }

    for (int32 t = 0; t < PS1_TEMP_BLOCKS; ++t) {
        uint16 s           = TEMPENTITY_START + t;
        objectEntityPtr[s] = Carve(p, s, PS1_TEMP_IS_LARGE(t) ? maxCap : PS1_TEMP_SMALL_CAP);
    }
    s_spare         = p;
    s_spareEnd      = s_arena + bytes;
    ps1SceneSlotEnd = slot;

    RemoveStorageEntry((void **)&index);
    for (int32 b = 0; b < s_blockCount; ++b) {
        if (!s_blockTail[b])
            RemoveStorageEntry((void **)&s_blocks[b]);
    }
    PS1StorageTailRelease(DATASET_STG);
    s_blockCount = 0;
    s_rec        = NULL;
}

// Stock copies sizeof(EntityBase) bytes. Here: the source's class (its whole block for a slot-less pointer), into a
// destination slot block made big enough, the rest of that block cleared.
void RSDK::CopyEntity(void *destEntity, void *srcEntity, bool32 clearSrcEntity)
{
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
