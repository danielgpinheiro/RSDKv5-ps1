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

bool32 RSDK::InitStorage()
{
    // Storage limits.
#if RETRO_PLATFORM == RETRO_PS1 && PS1_DEBUG_BIGRAM
    // PS1 port, 8 MB debug/measuring link only (docs/32 2.2): room to measure what the datasets really need
    // (g_ps1StorageLivePeak below); 2.3 sizes the 2 MB limits from those peaks.
    dataStorage[DATASET_STG].storageLimit = 4 * 1024 * 1024;
    dataStorage[DATASET_MUS].storageLimit = 64 * 1024;
    dataStorage[DATASET_SFX].storageLimit = 64 * 1024;
    dataStorage[DATASET_STR].storageLimit = 128 * 1024;
    dataStorage[DATASET_TMP].storageLimit = 1024 * 1024;
#elif RETRO_PLATFORM == RETRO_PS1
    // PS1 port (docs/32 2.3): 2 MB in all. Sprites, tiles and audio never live in RAM (VRAM / SPU / CD-XA, converted
    // at build time): the datasets hold globals, static variables, the entity arena, layouts and tables. Sized from the
    // measured live peaks (g_ps1StorageLivePeak) of Logos / Title with margin; the zones set theirs with overlays (2.4).
    dataStorage[DATASET_STG].storageLimit = 360 * 1024;
    dataStorage[DATASET_MUS].storageLimit = 4 * 1024;
    dataStorage[DATASET_SFX].storageLimit = 8 * 1024;
    dataStorage[DATASET_STR].storageLimit = 24 * 1024;
    dataStorage[DATASET_TMP].storageLimit = 64 * 1024;
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
        dataStorage[s].memoryTable = (uint32 *)malloc(dataStorage[s].storageLimit);

        if (dataStorage[s].memoryTable == NULL)
            return false;
    }

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
volatile uint32 g_ps1StorageLivePeak[DATASET_MAX];  // bytes incl. the 16-byte headers
volatile uint32 g_ps1StorageEntryPeak[DATASET_MAX]; // live entries
volatile uint32 g_ps1StorageLargest[DATASET_MAX];   // largest single allocation
volatile uint32 g_ps1StorageFail[DATASET_MAX];      // allocations that failed after garbage collection (must be 0)
// Compactions forced by a full dataset inside AllocateStorage. They move blocks, and only the owner pointers follow:
// entity pointers into the arena and animator frame pointers into the sprite lists would go stale. STG must stay 0
// (the scene-start compaction in LoadSceneFolder is the planned one).
volatile uint32 g_ps1StorageForcedDefrags[DATASET_MAX];

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

static void PS1StorageStats(uint32 set, bool32 ok, uint32 size)
{
    DataStorage *st = &dataStorage[set];
    if (!ok)
        g_ps1StorageFail[set] = g_ps1StorageFail[set] + 1;
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
    if (size > g_ps1StorageLargest[set])
        g_ps1StorageLargest[set] = size;
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
