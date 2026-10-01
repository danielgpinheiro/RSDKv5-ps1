// PS1 port (docs/32 phase 2.3): entity storage sized per slot. Included by Object.hpp inside namespace RSDK.
//
// Stock v5 keeps ENTITY_COUNT (0x940) entities of sizeof(EntityBase) = 1112 bytes each: 2.6 MB, plus a temp list of the
// same stride while a scene loads. On the PS1 each slot points to its own block, sized for what the slot holds:
//   - reserved slots (players, camera, HUD, pause menu, ...): the largest registered class
//   - scene slots: the class of the entity the scene file puts there (after the mode filter and compaction)
//   - temp slots (CreateEntity): 0xE0 of TEMP_SMALL_CAP bytes and 0x20 of the largest class; CreateEntity skips a slot
//     too small for the class it creates
//   - empty scene slots: "ghosts", unique addresses inside one shared zero page (a ghost reads as a blank entity; it
//     gets a real block from the spare pool when something resets it)
// Every real block starts with a PS1EntityHeader (slot, capacity), so GetEntitySlot works on any entity pointer. A
// reset or copy that needs more than a block holds moves the slot to a spare block (counted: a pointer kept to the old
// block would go stale, on the PC it would not). The slot counts stay the stock ones (ps1/ps1_limits.h): the game's
// constants are the PC's.
#pragma once

struct PS1EntityHeader {
    uint16 slot;
    uint16 capacity; // bytes of entity data after the header
};

#define PS1_TEMP_SMALL_CAP   (256)  // 190 of the 223 classes the game creates at run time fit (docs/32 2.3)
// 2.3l: CreateEntity uses the first PS1_TEMP_BLOCKS temp slots only (createSlot wraps there; the rest stay ghosts). GHZ1
// play peaked at 8 live temp entities; a full ring scatter makes 32. Every PS1_TEMP_LARGE_EVERY-th block has the largest
// class's size, so CreateEntity's 16-slot search meets one wherever createSlot is.
#define PS1_TEMP_BLOCKS      (0x70)
#define PS1_TEMP_LARGE_EVERY (7)
#define PS1_TEMP_IS_LARGE(t) ((t) % ps1TempLargeEvery == ps1TempLargeEvery - 1)
#define PS1_TEMP_LARGE_COUNT (PS1_TEMP_BLOCKS / PS1_TEMP_LARGE_EVERY)
// 7b: a stage with the game's ps1TempStageMarker class (the Mean Bean Machine: up to ~150 beans, temp entities that fit
// a small block) gets PS1_TEMP_BLOCKS_MANY, the same large ones spread over them (every ps1TempLargeEvery-th)
#define PS1_TEMP_BLOCKS_MANY (0x90)
extern int32 ps1TempBlocks, ps1TempLargeEvery; // this stage's
extern volatile uint32 g_ps1EntityCreateOverwrites; // CreateEntity found no free block in its search: it reset a live one

extern EntityBase *objectEntityPtr[ENTITY_COUNT];
extern uint8 ps1EntityGhosts[];

extern volatile uint32 g_ps1EntityRelocations; // slots moved to a spare block (reset / copy into a smaller block)
extern volatile uint32 g_ps1EntityGhostWrites; // non-zero bytes found in the ghost page (writes through a stale ghost)
extern volatile uint32 g_ps1EntitySpareFail;   // a relocation that found no spare room (the entity was blanked)
extern volatile uint32 g_ps1EntityArenaMoved;  // 4.3: STG compactions that moved the entity arena (must stay 0)
void PS1EntityArenaCheck();
void *PS1EntityArenaData(); // 4.3: the arena block (STG compactions keep it in place), or NULL
extern volatile uint32 g_ps1EntityArenaBytes;  // this scene's arena
extern volatile uint32 g_ps1EntityArenaPeak;
extern volatile uint32 g_ps1EntityCreateSkips; // temp slots skipped by CreateEntity for being too small
extern volatile uint32 g_ps1SceneRecordBytes;  // the scene loader's record buffer (TMP) peak
extern volatile uint32 g_ps1SceneRecordDropped;

#define OBJECT_ENTITY(slot) (objectEntityPtr[slot])

// Loops over every slot (update, late update, type groups, draw lists, foreach_all) skip the empty end of the scene
// range: [0, ps1SceneSlotEnd) then [TEMPENTITY_START, ENTITY_COUNT). 2,368 slots per loop cost ~130 hblanks a frame
// (2.3); an empty slot is a ghost with classID 0, which every such loop skips anyway. ps1SceneSlotEnd grows if an empty
// slot gets an entity (PS1EntityRelocate).
extern int32 ps1SceneSlotEnd;
// 3.0: the temp range ends at the last slot with a block (PS1_TEMP_BLOCKS; a relocation into a higher one moves it)
extern int32 ps1TempSlotEnd;
inline int32 PS1NextSlot(int32 e)
{
    ++e;
    if (e == ps1SceneSlotEnd && e < TEMPENTITY_START)
        return TEMPENTITY_START;
    return e == ps1TempSlotEnd ? ENTITY_COUNT : e;
}
// For the loops that keep sceneInfo.entitySlot equal to the loop index: the jump moves it too.
#define PS1_NEXT_SLOT_ES(e) ((e) = PS1NextSlot(e), sceneInfo.entitySlot = (uint16)(e))

inline bool32 PS1EntityIsGhost(const void *entity)
{
    return (const uint8 *)entity >= ps1EntityGhosts && (const uint8 *)entity < ps1EntityGhosts + ENTITY_COUNT * 4;
}

// The slot an entity pointer belongs to, or -1 (a pointer that is no slot's entity, as the stock range check).
inline int32 PS1EntitySlotOf(const void *entity)
{
    if (!entity)
        return -1;
    if (PS1EntityIsGhost(entity)) {
        uint32 off = (uint32)((const uint8 *)entity - ps1EntityGhosts);
        return (off & 3) ? -1 : (int32)(off >> 2);
    }
    const PS1EntityHeader *h = (const PS1EntityHeader *)entity - 1;
    if (h->slot < ENTITY_COUNT && objectEntityPtr[h->slot] == entity)
        return h->slot;
    return -1;
}

inline uint32 PS1EntityCapacity(const void *entity)
{
    if (!entity || PS1EntityIsGhost(entity))
        return 0;
    return ((const PS1EntityHeader *)entity - 1)->capacity;
}

// Scene load (Scene.cpp): every slot back to a ghost, the previous arena released.
void PS1EntityStorageClear();
// A block of at least `size` bytes for `slot` (from the spare pool), the slot pointer moved to it. NULL if no room.
EntityBase *PS1EntityRelocate(uint16 slot, uint32 size);
// Make sure `entity` (slot `slot`) can hold `size` bytes: the same pointer, a relocated one, or NULL.
EntityBase *PS1EntityEnsure(EntityBase *entity, int32 slot, uint32 size);
// The ghost page must stay zero: count and clear anything written into it (once a second, and at scene loads).
void PS1EntityCheckGhosts();

// Type groups (the per-frame lists foreach walks): stock keeps TYPEGROUP_COUNT lists of ENTITY_COUNT entries (1.2 MB);
// on the PS1 the entries of one frame are captured in slot order and counting-sorted into one pool.
void PS1TypeGroupsBegin();
void PS1TypeGroupsAdd(uint16 slot, uint16 classID, uint16 group);
void PS1TypeGroupsCollect(EntityBase **p, EntityBase **pend); // PS1TypeGroupsAdd for each in-range, interacting slot
void PS1TypeGroupsBuild();

// The scene file's entities (Scene.cpp LoadSceneAssets on PS1): parsed into compact records in DATASET_TMP (file slot,
// class, position, the active variables' bytes), then placed in their final slots in one arena sized from them.
extern volatile uint32 g_ps1EntitiesDropped; // scene entities past the file-slot range (GDB; must stay 0)
void PS1SceneEntitiesBegin();
void PS1SceneObjectBegin(uint32 bytes, uint16 classID, uint16 vars); // a block for one object's records (0: none), its layout first
void PS1SceneObjectVar(uint16 offset, uint8 type, uint16 len);     // 7b: its next active variable (file order)
void PS1SceneEntityBegin(uint16 fileSlot, uint16 classID, int32 x, int32 y);
void PS1SceneEntityVar(uint16 offset, uint8 type, const void *data, uint16 len);
void PS1SceneEntitiesPlace();
// 7b: class `classID` has a "childCount" variable at `offset` (the slots after such an entity never go dormant)
void PS1SceneChildVar(uint16 classID, uint16 offset, uint8 type);

// 7b (choice 113): dormant entities (ps1_limits.h PS1DormantSpec). The update passes call this first (after the cameras'
// union): it creates the dormant entities the cameras' boxes (grown by the class range + a margin) reach and returns to
// records the ones out of reach that are as their Create left them. The rest of the engine sees a dormant slot as an
// empty one (a ghost).
void PS1DormantUpdate();
bool PS1DormantLoadCreate(int32 slot); // InitObjects: a dormant record's Create at its slot (stock's load-time create)
void PS1DormantLoadDone();             // after InitObjects
extern volatile uint32 g_ps1DormantDiffs, g_ps1DormantBadActive;
extern volatile uint32 g_ps1DormantRecords, g_ps1DormantPool, g_ps1DormantLive, g_ps1DormantPeak;
extern volatile uint32 g_ps1DormantFail;      // a creation with no pool block left (the entity stayed dormant: must be 0)
extern volatile uint32 g_ps1DormantExtra;     // 7b.3c: pool blocks a band added from the arena spare (total)
extern volatile uint32 g_ps1DormantCreated, g_ps1DormantReturned, g_ps1DormantGone, g_ps1DormantDropped;
