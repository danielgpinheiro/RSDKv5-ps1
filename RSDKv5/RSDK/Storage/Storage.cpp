#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/UserStorageLegacy.cpp"
#endif

// Macro to access the header variables of a block of memory.
// Note that this is pointless if the pointer is already pointing directly at the header rather than the memory after it.
#define HEADER(memory, header_value) memory[-HEADER_SIZE + header_value]

// Every block of allocated memory is prefixed with a header that consists of the following four longwords.
enum {
    // Whether the block of memory is actually allocated or not.
    HEADER_ACTIVE,
    // Which 'data set' this block of memory belongs to.
    HEADER_SET_ID,
    // The offset in the buffer which the block of memory begins at.
    HEADER_DATA_OFFSET,
    // How long the block of memory is (measured in 'uint32's).
    HEADER_DATA_LENGTH,
    // This is not part of the header: it's just a bit of enum magic to calculate the size of the header.
    HEADER_SIZE
};

DataStorage RSDK::dataStorage[DATASET_MAX];

#if RETRO_PLATFORM == RETRO_PS1
extern "C" char __heap_start, __stack_start;
extern "C" void *psyqo_heap_end();
#define PS1_STACK_RESERVE (16 * 1024)
// 2.3p: user database (0xE0 rows); in play the chain arena takes 50 KB of it (3.2). 4.2a: the database 62 -> 35 KB (6 columns
// of 4 bytes), 128 -> 104 KB: the menu at 2 MB (its save-file list, TA table) leaves ~3.5 KB of it never reached
#define PS1_INIT_RESERVE  (116 * 1024) // 7.1: + 12 KB: the chain arena 50 -> 60 KB, and 2 KB for the load-time heap
                                        // layout it changes (the reserve's headroom 1,912 -> 247 B with + 10 KB only)
volatile uint32 g_ps1StgLimit = 0; // STG's usable size now (GDB)
// 2.4a: STG ends at a fixed address, the RAM top less the stack (__stack_start - PS1_STACK_RESERVE): a scene's code
// overlay sits at the top of STG, linked at that address less its size, and STG's limit ends where the overlay starts
// (PS1SetOverlaySize). Below STG, the init reserve (the blocks allocated after InitStorage: user core and database,
// the CD file index and stream windows, scanlines) is one freed placeholder; above it a guard block covers the stack.
uint32 RSDK::ps1StgCapacity = 0;
extern "C" {
volatile uint32 g_ps1ReserveStart = 0, g_ps1ReserveEnd = 0; // the init reserve (ps1/libc: its high-water mark)
extern volatile uint32 g_ps1ReserveHigh;
}
#endif

#if RETRO_PLATFORM == RETRO_PS1
extern volatile uint32 g_ps1StorageLivePeak[DATASET_MAX];
extern volatile uint32 g_ps1StorageEntryPeak[DATASET_MAX];
#endif
bool32 RSDK::InitStorage()
{
    // Storage limits.
#if RETRO_PLATFORM == RETRO_PS1 && PS1_DEBUG_BIGRAM
    // PS1 port, 8 MB debug/measuring link only (docs/32 2.2): room to measure what the datasets really need
    // (g_ps1StorageLivePeak below); STG takes the rest of the 8 MB (2.4a layout below).
    dataStorage[DATASET_MUS].storageLimit = 64 * 1024;
    dataStorage[DATASET_SFX].storageLimit = 64 * 1024;
    dataStorage[DATASET_STR].storageLimit = 128 * 1024;
    dataStorage[DATASET_TMP].storageLimit = 1024 * 1024;
#elif RETRO_PLATFORM == RETRO_PS1
    // PS1 port (docs/32 2.3): 2 MB in all. Sprites, tiles and audio never live in RAM (VRAM / SPU / CD-XA, converted
    // at build time): the datasets hold globals, static variables, the entity arena, layouts and tables.
    // 2.3j: GHZ1 peaks TMP 23.6 KB (the records moved to the STG tail), STR 14.6 KB, MUS / SFX never used (music is
    // CD-XA, sound effects live in SPU RAM). STG takes the rest of the heap (2.4a layout below).
    dataStorage[DATASET_MUS].storageLimit = 256;
    dataStorage[DATASET_SFX].storageLimit = 256;
    dataStorage[DATASET_STR].storageLimit = 24 * 1024;
    dataStorage[DATASET_TMP].storageLimit = 32 * 1024;
#else
    dataStorage[DATASET_STG].storageLimit = 24 * 1024 * 1024; // 24MB
    dataStorage[DATASET_MUS].storageLimit = 8 * 1024 * 1024;  //  8MB
    dataStorage[DATASET_SFX].storageLimit = 32 * 1024 * 1024; // 32MB
    dataStorage[DATASET_STR].storageLimit = 2 * 1024 * 1024;  //  2MB
    dataStorage[DATASET_TMP].storageLimit = 8 * 1024 * 1024;  //  8MB
#endif

    for (int32 s = 0; s < DATASET_MAX; ++s) {
        dataStorage[s].usedStorage = 0;
        dataStorage[s].entryCount  = 0;
        dataStorage[s].clearCount  = 0;
#if RETRO_PLATFORM == RETRO_PS1
        g_ps1StorageLivePeak[s] = 0, g_ps1StorageEntryPeak[s] = 0; // 4.2e: keeps them linked for the GDB scripts
        if (s == DATASET_STG)
            continue; // below
#endif
        dataStorage[s].memoryTable = (uint32 *)malloc(dataStorage[s].storageLimit);

        if (dataStorage[s].memoryTable == NULL)
            return false;
    }

#if RETRO_PLATFORM == RETRO_PS1
    {
        // 2.4a layout (see ps1StgCapacity): reserve placeholder, STG up to the fixed top, guard over the stack
        void *reserve = malloc(PS1_INIT_RESERVE);
        if (!reserve)
            return false;
        uint32 from = (uint32)(uintptr_t)psyqo_heap_end(); // the first free byte: nothing has been freed yet
        uint32 top  = (uint32)(uintptr_t)&__stack_start - PS1_STACK_RESERVE;
        uint32 size = (top - from - 8) & ~7u; // psyqo puts an 8-byte header before each block
        dataStorage[DATASET_STG].memoryTable = (uint32 *)malloc(size);
        if (!dataStorage[DATASET_STG].memoryTable)
            return false;
        // the rest, up to the stack, as one block no allocation can get (a best-fit allocator gives small requests the
        // smallest free block: that would be this one)
        uint32 next = (uint32)(uintptr_t)psyqo_heap_end();
        for (uint32 g = (uint32)(uintptr_t)&__stack_start - next; g >= 16; g -= 8) {
            if (malloc(g - 8))
                break;
        }
        g_ps1ReserveStart = (uint32)(uintptr_t)reserve;
        g_ps1ReserveEnd   = (uint32)(uintptr_t)reserve + PS1_INIT_RESERVE;
        g_ps1ReserveHigh  = g_ps1ReserveStart;
        free(reserve);
        ps1StgCapacity                        = size;
        dataStorage[DATASET_STG].storageLimit = size;
        g_ps1StgLimit                         = size;
    }
#endif

    return true;
}

void RSDK::ReleaseStorage()
{
    for (int32 s = 0; s < DATASET_MAX; ++s) {
        if (dataStorage[s].memoryTable != NULL)
            free(dataStorage[s].memoryTable);

        dataStorage[s].usedStorage = 0;
        dataStorage[s].entryCount  = 0;
        dataStorage[s].clearCount  = 0;
    }

    // this code isn't in steam executable, since it omits the "load datapack into memory" feature.
    // I don't think it's in the console versions either, but this never seems to be freed in those versions.
    // so, I figured doing it here would be the neatest.
#if !RETRO_USE_ORIGINAL_CODE
    for (int32 p = 0; p < dataPackCount; ++p) {
        if (dataPacks[p].fileBuffer)
            free(dataPacks[p].fileBuffer);

        dataPacks[p].fileBuffer = NULL;
    }
#endif
}

#if RETRO_PLATFORM == RETRO_PS1
// PS1 port (docs/32 2.2): what each dataset really needs. The datasets are bump allocators that garbage-collect when
// full, so the high-water mark of usedStorage says little; the live bytes (entries whose owner pointer still points
// at them: what GarbageCollectStorage keeps) after each allocation give the limit a dataset needs. GDB reads these.
volatile uint32 g_ps1StorageLivePeak[DATASET_MAX];  // bytes incl. the 16-byte headers (measuring link only, 4.2e: kept for the GDB scripts)
volatile uint32 g_ps1StorageEntryPeak[DATASET_MAX]; // live entries (measuring link only)
volatile uint32 g_ps1StorageLargest[DATASET_MAX];   // largest single allocation
volatile uint32 g_ps1StorageFail[DATASET_MAX];      // allocations that failed after garbage collection (must be 0)
// Compactions forced by a full dataset inside AllocateStorage. They move blocks, and only the owner pointers follow:
// entity pointers into the arena and animator frame pointers into the sprite lists would go stale. STG must stay 0
// (the scene-start compaction in LoadSceneFolder is the planned one).
volatile uint32 g_ps1StorageForcedDefrags[DATASET_MAX];
volatile uint32 g_ps1StoragePinHoles = 0; // 4.3: compactions that left a gap under the pinned entity arena (total)

bool32 RSDK::ps1StorageMayFail = false;
volatile uint32 g_ps1StorageExhausted = 0; // 1 + the dataset of the allocation that stopped the PS1 (GDB)

// GDB: `break PS1StorageExhausted` (tools/boot_test_ds.sh). Never returns.
extern "C" __attribute__((noinline)) void PS1StorageExhausted(uint32 set, uint32 size)
{
    g_ps1StorageExhausted = set + 1;
    __asm__ volatile("" ::"r"(size));
    for (;;) {
    }
}

// Tail blocks (Storage.hpp): bytes held per dataset, and the peak of used + tail (what the dataset had to hold).
static uint32 s_ps1TailBytes[DATASET_MAX];
volatile uint32 g_ps1StorageTailPeak[DATASET_MAX];

void *RSDK::PS1StorageTailAlloc(StorageDataSets set, uint32 size)
{
    DataStorage *st = &dataStorage[set];
    size            = (size + 3) & ~3u;
    if (st->usedStorage * sizeof(uint32) + (HEADER_SIZE * sizeof(uint32)) + size >= st->storageLimit)
        return NULL;
    st->storageLimit -= size;
    s_ps1TailBytes[set] += size;
    if (st->usedStorage * sizeof(uint32) + s_ps1TailBytes[set] > g_ps1StorageTailPeak[set])
        g_ps1StorageTailPeak[set] = st->usedStorage * sizeof(uint32) + s_ps1TailBytes[set];
    return (uint8 *)st->memoryTable + st->storageLimit;
}

void RSDK::PS1StorageTailRelease(StorageDataSets set)
{
    dataStorage[set].storageLimit += s_ps1TailBytes[set];
    s_ps1TailBytes[set] = 0;
}

// docs/32 4.2d: the chain's second block (ps1/gpu_chain.cpp PS1ChainResume / Suspend): STG's tail after a load when the
// scene leaves more than PS1_CHAIN_EXTRA_MARGIN free (what the scene allocates while it runs: sprite loads, objects);
// given back before the next load, whose records and staging use the tail. Zones near the STG limit lend nothing.
#define PS1_CHAIN_EXTRA_MARGIN (48 * 1024)
#define PS1_CHAIN_EXTRA_MAX    (176 * 1024) // the ruby's deformed frames (AIZ, GHZCutscene) need ~160 KB of chain in all
extern "C" uint32_t *PS1ChainExtraTake(uint32_t *words)
{
    DataStorage *st = &dataStorage[DATASET_STG];
    uint32 used     = st->usedStorage * sizeof(uint32) + HEADER_SIZE * sizeof(uint32);
    if (s_ps1TailBytes[DATASET_STG] || st->storageLimit < used + PS1_CHAIN_EXTRA_MARGIN + 16 * 1024)
        return NULL;
    uint32 take = st->storageLimit - used - PS1_CHAIN_EXTRA_MARGIN;
    take        = (take > PS1_CHAIN_EXTRA_MAX ? PS1_CHAIN_EXTRA_MAX : take) & ~15u;
    void *p     = PS1StorageTailAlloc(DATASET_STG, take);
    if (!p)
        return NULL;
    *words = take / sizeof(uint32);
    return (uint32_t *)p;
}
extern "C" void PS1ChainExtraGive() { PS1StorageTailRelease(DATASET_STG); }

// docs/32 6.2: the video's buffers (ps1/video.cpp, ~110 KB) from STG's tail: nothing allocates while the engine is in
// ENGINESTATE_VIDEOPLAYBACK (the scene waits; a load comes after). The chain gave its extra block back as the video
// opened (PS1ChainSuspend) and takes none while these are out (PS1ChainExtraTake sees the tail in use).
extern "C" void *psyqo_malloc(size_t size); // psyqo/alloc.h (its headers clash with the engine's assert)
extern "C" void psyqo_free(void *ptr);
static uint32 s_ps1VideoTailBlocks = 0;
volatile uint32 g_ps1VideoTailBytes = 0;
extern "C" void *PS1VideoMalloc(size_t n)
{
    void *p = PS1StorageTailAlloc(DATASET_STG, (uint32)(n + 63) & ~63u); // 64-byte multiples: every buffer word-aligned
    if (!p)
        return psyqo_malloc(n);
    ++s_ps1VideoTailBlocks;
    g_ps1VideoTailBytes = g_ps1VideoTailBytes + ((uint32)(n + 63) & ~63u);
    return p;
}
extern "C" void PS1VideoFree(void *p)
{
    DataStorage *st = &dataStorage[DATASET_STG];
    if (p >= (void *)st->memoryTable && p < (void *)((uint8 *)st->memoryTable + st->storageLimit + s_ps1TailBytes[DATASET_STG])) {
        if (s_ps1VideoTailBlocks && !--s_ps1VideoTailBlocks) {
            PS1StorageTailRelease(DATASET_STG);
            g_ps1VideoTailBytes = 0;
        }
    }
    else if (p) {
        psyqo_free(p);
    }
}

static void PS1StorageStats(uint32 set, bool32 ok, uint32 size)
{
    DataStorage *st = &dataStorage[set];
    if (!ok)
        g_ps1StorageFail[set] = g_ps1StorageFail[set] + 1;
    if (size > g_ps1StorageLargest[set])
        g_ps1StorageLargest[set] = size;
#if PS1_DEBUG_BIGRAM // 4.2e: the live bytes / entries walk (every allocation: ~8 hblanks a frame in the menu) measures only
    uint32 live = 0, entries = 0;
    for (int32 e = 0; e < (int32)st->entryCount; ++e) {
        if (st->dataEntries[e] && *st->dataEntries[e] == st->storageEntries[e]) {
            live += ((uint32 *)st->storageEntries[e])[-HEADER_SIZE + HEADER_DATA_LENGTH] + HEADER_SIZE * sizeof(uint32);
            ++entries;
        }
    }
    if (live > g_ps1StorageLivePeak[set])
        g_ps1StorageLivePeak[set] = live;
    if (entries > g_ps1StorageEntryPeak[set])
        g_ps1StorageEntryPeak[set] = entries;
#endif
}
#endif

void RSDK::AllocateStorage(void **dataPtr, uint32 size, StorageDataSets dataSet, bool32 clear)
{
    uint32 **data = (uint32 **)dataPtr;
    *data         = NULL;

    if ((uint32)dataSet < DATASET_MAX) {
        // Align allocation to prevent unaligned memory accesses later on.
        const uint32 size_aligned = size & -(int32)sizeof(void *);

        if (size_aligned < size)
            size = size_aligned + sizeof(void *);

        if (dataStorage[dataSet].entryCount < STORAGE_ENTRY_COUNT) {
            DataStorage *storage = &dataStorage[dataSet];

#if !RETRO_USE_ORIGINAL_CODE
            // Bug: The original release never takes into account the size of the header when checking if there's enough storage left.
            // Omitting this will overflow the memory pool when (storageLimit - usedStorage + size) < header size (16 bytes here).
            if (storage->usedStorage * sizeof(uint32) + size + (HEADER_SIZE * sizeof(uint32)) < storage->storageLimit) {
#else
            if (storage->usedStorage * sizeof(uint32) + size < storage->storageLimit) {
#endif
                // HEADER_ACTIVE
                storage->memoryTable[storage->usedStorage] = true;
                ++storage->usedStorage;

                // HEADER_SET_ID
                storage->memoryTable[storage->usedStorage] = dataSet;
                ++storage->usedStorage;

                // HEADER_DATA_OFFSET
                storage->memoryTable[storage->usedStorage] = storage->usedStorage + HEADER_SIZE - HEADER_DATA_OFFSET;
                ++storage->usedStorage;

                // HEADER_DATA_LENGTH
                storage->memoryTable[storage->usedStorage] = size;
                ++storage->usedStorage;

                *data = &storage->memoryTable[storage->usedStorage];
                storage->usedStorage += size / sizeof(uint32);

                dataStorage[dataSet].dataEntries[storage->entryCount]    = data;
                dataStorage[dataSet].storageEntries[storage->entryCount] = *data;

                ++storage->entryCount;
            }
            else {
                // We've run out of room, so perform defragmentation and garbage-collection.
#if RETRO_PLATFORM == RETRO_PS1
                g_ps1StorageForcedDefrags[dataSet] = g_ps1StorageForcedDefrags[dataSet] + 1;
#endif
                DefragmentAndGarbageCollectStorage(dataSet);

                // If there is now room, then perform allocation.
                // Yes, this really is a massive chunk of duplicate code.
#if !RETRO_USE_ORIGINAL_CODE
                if (storage->usedStorage * sizeof(uint32) + size + (HEADER_SIZE * sizeof(uint32)) < storage->storageLimit) {
#else
                if (storage->usedStorage * sizeof(uint32) + size < storage->storageLimit) {
#endif
                    // HEADER_ACTIVE
                    storage->memoryTable[storage->usedStorage] = true;
                    ++storage->usedStorage;

                    // HEADER_SET_ID
                    storage->memoryTable[storage->usedStorage] = dataSet;
                    ++storage->usedStorage;

                    // HEADER_DATA_OFFSET
                    storage->memoryTable[storage->usedStorage] = storage->usedStorage + HEADER_SIZE - HEADER_DATA_OFFSET;
                    ++storage->usedStorage;

                    // HEADER_DATA_LENGTH
                    storage->memoryTable[storage->usedStorage] = size;
                    ++storage->usedStorage;

                    *data = &storage->memoryTable[storage->usedStorage];
                    storage->usedStorage += size / sizeof(uint32);

                    dataStorage[dataSet].dataEntries[storage->entryCount]    = data;
                    dataStorage[dataSet].storageEntries[storage->entryCount] = *data;

                    ++storage->entryCount;
                }
            }

            // If there are too many storage entries, then perform garbage collection.
            if (storage->entryCount >= STORAGE_ENTRY_COUNT)
                GarbageCollectStorage(dataSet);

            // Clear the allocated memory if requested.
            if (*data != NULL && clear == (bool32)true)
                memset(*data, 0, size);
#if RETRO_PLATFORM == RETRO_PS1
            PS1StorageStats(dataSet, *data != NULL, size);
            if (!*data && !ps1StorageMayFail)
                PS1StorageExhausted(dataSet, size);
#endif
        }
    }
}

void RSDK::RemoveStorageEntry(void **dataPtr)
{
    if (dataPtr != NULL && *dataPtr != NULL) {
        uint32 *data = *(uint32 **)dataPtr;

        uint32 set = HEADER(data, HEADER_SET_ID);
        for (int32 e = 0; e < dataStorage[set].entryCount; ++e) {
#if !RETRO_USE_ORIGINAL_CODE
            // make sure dataEntries[e] isn't null. If it is null by some ungodly chance then it was prolly already freed or something idk
            if (dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e] && *dataPtr == *dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]) {
                *dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e] = NULL;
                dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]  = NULL;
            }
#else
            if (*dataPtr == *dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]) {
                *dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e] = NULL;
                dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]  = NULL;
            }
#endif

            set = HEADER(data, HEADER_SET_ID);
        }

        uint32 newEntryCount = 0;
        set                  = HEADER(data, HEADER_SET_ID);
        for (uint32 entryID = 0; entryID < dataStorage[set].entryCount; ++entryID) {
            if (dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[entryID]) {
                if (entryID != newEntryCount) {
                    dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[newEntryCount] =
                        dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[entryID];
                    dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[entryID] = NULL;
                    dataStorage[HEADER(data, HEADER_SET_ID)].storageEntries[newEntryCount] =
                        dataStorage[HEADER(data, HEADER_SET_ID)].storageEntries[entryID];
                    dataStorage[HEADER(data, HEADER_SET_ID)].storageEntries[entryID] = NULL;
                }

                ++newEntryCount;
            }

            set = HEADER(data, HEADER_SET_ID);
        }

        dataStorage[HEADER(data, HEADER_SET_ID)].entryCount = newEntryCount;

        for (uint32 e = newEntryCount; e < STORAGE_ENTRY_COUNT; ++e) {
            dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]    = NULL;
            dataStorage[HEADER(data, HEADER_SET_ID)].storageEntries[e] = NULL;
        }

        HEADER(data, HEADER_ACTIVE) = false;
    }
}

// This defragments the storage, leaving all empty space at the end.
#if RETRO_PLATFORM == RETRO_PS1
// 4.2e: stock checks every block against every entry, twice (blocks x entries: the menu's string pool, ~600 blocks x ~500
// entries, took 3,000-5,400 hblanks, a hitch every ~60 frames). Here the entries are sorted by pointer once and both
// passes walk the blocks and the sorted entries together: the same blocks stay, move and get their pointers updated (the
// blocks' data pointers rise; a block whose pointer does not restarts the walk). The order lives in the scratchpad (free
// outside the layer / flat drawing calls)
#define PS1_DEFRAG_ORDER ((uint16 *)0x1F800000)
static_assert(STORAGE_ENTRY_COUNT * sizeof(uint16) <= 1024, "the entry order must fit the scratchpad");
void RSDK::DefragmentAndGarbageCollectStorage(StorageDataSets set)
{
    DataStorage *st = &dataStorage[set];
    ++st->clearCount;
    GarbageCollectStorage(set);

    uint16 *order = PS1_DEFRAG_ORDER;
    int32 count   = (int32)st->entryCount;
#define PS1_ENTRY_PTR(i) ((uint32 *)st->storageEntries[order[i]])
    for (int32 i = 0; i < count; ++i) { // insertion sort: the entries are mostly in allocation order
        uint32 *v = (uint32 *)st->storageEntries[i];
        int32 j   = i;
        for (; j > 0 && PS1_ENTRY_PTR(j - 1) > v; --j) order[j] = order[j - 1];
        order[j] = (uint16)i;
    }

    // 4.3: the scene's entity arena never moves (the slots, and whatever StageLoad / Create kept, point into it): the
    // blocks under it compact up to it and the gap left becomes one inactive block, reclaimed after the arena is
    // released (the next load). A forced compaction during GHZ1's load (from GHZCutscene, STG full) had moved it
    uint32 *pin = set == DATASET_STG ? (uint32 *)PS1EntityArenaData() : NULL;
    bool moved  = false;
    uint32 processedStorage = 0, unusedStorage = 0;
    uint32 *defragmentDestination = st->memoryTable, *currentHeader = st->memoryTable, *last = NULL;
    int32 k = 0;
    while (processedStorage < st->usedStorage) {
        uint32 *dataPtr = &st->memoryTable[currentHeader[HEADER_DATA_OFFSET]];
        uint32 size     = (currentHeader[HEADER_DATA_LENGTH] / sizeof(uint32)) + HEADER_SIZE;
        if (dataPtr < last)
            k = 0;
        last = dataPtr;
        while (k < count && PS1_ENTRY_PTR(k) < dataPtr) ++k;
        currentHeader[HEADER_ACTIVE] = k < count && PS1_ENTRY_PTR(k) == dataPtr;

        if (currentHeader[HEADER_ACTIVE]) {
            processedStorage += size;
            if (currentHeader > defragmentDestination && dataPtr == pin) {
                uint32 hole = (uint32)(currentHeader - defragmentDestination); // >= HEADER_SIZE: freed blocks' words
                defragmentDestination[HEADER_ACTIVE]      = false;
                defragmentDestination[HEADER_SET_ID]      = set;
                defragmentDestination[HEADER_DATA_OFFSET] = (uint32)(defragmentDestination - st->memoryTable) + HEADER_SIZE;
                defragmentDestination[HEADER_DATA_LENGTH] = (hole - HEADER_SIZE) * sizeof(uint32);
                unusedStorage -= hole;
                defragmentDestination = currentHeader;
                g_ps1StoragePinHoles  = g_ps1StoragePinHoles + 1;
            }
            if (currentHeader > defragmentDestination) {
                for (uint32 i = 0; i < size; ++i) *defragmentDestination++ = *currentHeader++;
                moved = true;
            }
            else {
                defragmentDestination += size;
                currentHeader += size;
            }
        }
        else {
            currentHeader += size;
            processedStorage += size;
            unusedStorage += size;
        }
    }

    if (moved || unusedStorage != 0) {
        st->usedStorage -= unusedStorage;
        uint32 *currentHeader = st->memoryTable;
        uint32 dataOffset     = 0;
        last = NULL, k = 0;
        while (dataOffset < st->usedStorage) {
            uint32 *dataPtr = &st->memoryTable[currentHeader[HEADER_DATA_OFFSET]];
            uint32 size     = (currentHeader[HEADER_DATA_LENGTH] / sizeof(uint32)) + HEADER_SIZE;
            if (dataPtr < last)
                k = 0;
            last = dataPtr;
            // an updated entry points below its old place, so below every later block's: the walk skips it
            while (k < count && PS1_ENTRY_PTR(k) < dataPtr) ++k;
            for (int32 m = k; m < count && PS1_ENTRY_PTR(m) == dataPtr; ++m) {
                int32 c = order[m];
                if (st->dataEntries[c])
                    st->storageEntries[c] = *st->dataEntries[c] = currentHeader + HEADER_SIZE;
            }
            currentHeader[HEADER_DATA_OFFSET] = dataOffset + HEADER_SIZE;
            currentHeader += size;
            dataOffset += size;
        }
    }
#undef PS1_ENTRY_PTR
    if (set == DATASET_STG)
        PS1EntityArenaCheck();
}
#else
void RSDK::DefragmentAndGarbageCollectStorage(StorageDataSets set)
{
    uint32 processedStorage = 0;
    uint32 unusedStorage    = 0;

    uint32 *defragmentDestination = dataStorage[set].memoryTable;
    uint32 *currentHeader         = dataStorage[set].memoryTable;

    ++dataStorage[set].clearCount;

    // Perform garbage-collection. This deallocates all memory allocations that are no longer being used.
    GarbageCollectStorage(set);

    // This performs defragmentation. It works by removing 'gaps' between the various blocks of allocated memory,
    // grouping them all together at the start of the buffer while all the empty space goes at the end.
    // Avoiding fragmentation is important, as fragmentation can cause allocations to fail despite there being
    // enough free memory because that free memory isn't contiguous.
    while (processedStorage < dataStorage[set].usedStorage) {
        uint32 *dataPtr = &dataStorage[set].memoryTable[currentHeader[HEADER_DATA_OFFSET]];
        uint32 size     = (currentHeader[HEADER_DATA_LENGTH] / sizeof(uint32)) + HEADER_SIZE;

        // Check if this block of memory is currently allocated.
        currentHeader[HEADER_ACTIVE] = false;

        for (int32 e = 0; e < dataStorage[set].entryCount; ++e)
            if (dataPtr == dataStorage[set].storageEntries[e])
                currentHeader[HEADER_ACTIVE] = true;

        if (currentHeader[HEADER_ACTIVE]) {
            // This memory is being used.
            processedStorage += size;

            if (currentHeader > defragmentDestination) {
                // This memory has a gap before it, so move it backwards into that free space.
                for (uint32 i = 0; i < size; ++i) *defragmentDestination++ = *currentHeader++;
            }
            else {
                // This memory doesn't have a gap before it, so we don't need to move it - just skip it instead.
                defragmentDestination += size;
                currentHeader += size;
            }
        }
        else {
            // This memory is not being used, so skip it.
            currentHeader += size;
            processedStorage += size;
            unusedStorage += size;
        }
    }

    // If defragmentation occurred, then we need to update every single
    // pointer to allocated memory to point to their new locations in the buffer.
    if (unusedStorage != 0) {
        dataStorage[set].usedStorage -= unusedStorage;

        uint32 *currentHeader = dataStorage[set].memoryTable;

        uint32 dataOffset = 0;
        while (dataOffset < dataStorage[set].usedStorage) {
            uint32 *dataPtr = &dataStorage[set].memoryTable[currentHeader[HEADER_DATA_OFFSET]];
            uint32 size     = (currentHeader[HEADER_DATA_LENGTH] / sizeof(uint32)) + HEADER_SIZE; // size (in int32s)

            // Find every single pointer to this memory allocation and update them with its new address.
            for (int32 c = 0; c < dataStorage[set].entryCount; ++c)

#if !RETRO_USE_ORIGINAL_CODE
                // make sure dataEntries[e] isn't null. If it is null by some ungodly chance then it was prolly already freed or something idk
                if (dataPtr == dataStorage[set].storageEntries[c] && dataStorage[set].dataEntries[c])
                    dataStorage[set].storageEntries[c] = *dataStorage[set].dataEntries[c] = currentHeader + HEADER_SIZE;
#else
                if (dataPtr == dataStorage[set].storageEntries[c])
                    dataStorage[set].storageEntries[c] = *dataStorage[set].dataEntries[c] = currentHeader + HEADER_SIZE;
#endif


            // Update the offset in the allocation's header too.
            currentHeader[HEADER_DATA_OFFSET] = dataOffset + HEADER_SIZE;

            // Advance to the next memory allocation.
            currentHeader += size;
            dataOffset += size;
        }
    }
}

#endif

void RSDK::CopyStorage(uint32 **src, uint32 **dst)
{
    if (dst != NULL) {
        uint32 *dstPtr = *dst;
        *src           = *dst;

        if (dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount < STORAGE_ENTRY_COUNT) {
            dataStorage[HEADER(dstPtr, HEADER_SET_ID)].dataEntries[dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount]    = src;
            dataStorage[HEADER(dstPtr, HEADER_SET_ID)].storageEntries[dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount] = *src;

            ++dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount;

            if (dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount >= STORAGE_ENTRY_COUNT)
                GarbageCollectStorage((StorageDataSets)HEADER(dstPtr, HEADER_SET_ID));
        }
    }
}

void RSDK::GarbageCollectStorage(StorageDataSets set)
{
    if ((uint32)set < DATASET_MAX) {
        for (uint32 e = 0; e < dataStorage[set].entryCount; ++e) {
            // So what's happening here is the engine is checking to see if the storage entry
            // (which is the pointer to the "memoryTable" offset that is allocated for this entry)
            // matches what the actual variable that allocated the storage is currently pointing to.
            // if they don't match, the storage entry is considered invalid and marked for removal.

            if (dataStorage[set].dataEntries[e] != NULL && *dataStorage[set].dataEntries[e] != dataStorage[set].storageEntries[e])
                dataStorage[set].dataEntries[e] = NULL;
        }

        uint32 newEntryCount = 0;
        for (uint32 entryID = 0; entryID < dataStorage[set].entryCount; ++entryID) {
            if (dataStorage[set].dataEntries[entryID]) {
                if (entryID != newEntryCount) {
                    dataStorage[set].dataEntries[newEntryCount]    = dataStorage[set].dataEntries[entryID];
                    dataStorage[set].dataEntries[entryID]          = NULL;
                    dataStorage[set].storageEntries[newEntryCount] = dataStorage[set].storageEntries[entryID];
                    dataStorage[set].storageEntries[entryID]       = NULL;
                }

                ++newEntryCount;
            }
        }
        dataStorage[set].entryCount = newEntryCount;

        for (int32 e = dataStorage[set].entryCount; e < STORAGE_ENTRY_COUNT; ++e) {
            dataStorage[set].dataEntries[e]    = NULL;
            dataStorage[set].storageEntries[e] = NULL;
        }
    }
}
