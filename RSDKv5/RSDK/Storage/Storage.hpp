#ifndef STORAGE_H
#define STORAGE_H

#if RETRO_PLATFORM == RETRO_PS1
extern "C" void PS1OutOfMemory(unsigned int size); // ps1/libc/new_guard.cpp: stops (GDB), never returns
#endif

namespace RSDK
{
#if RETRO_PLATFORM == RETRO_PS1
#define STORAGE_ENTRY_COUNT (0x200) // PS1: allocations per dataset (2.2 peaks: STG 37, STR 70, TMP 3); stock 0x1000
#else
#define STORAGE_ENTRY_COUNT (0x1000)
#endif

enum StorageDataSets {
    DATASET_STG = 0,
    DATASET_MUS = 1,
    DATASET_SFX = 2,
    DATASET_STR = 3,
    DATASET_TMP = 4,
    DATASET_MAX, // used to signify limits
};

struct DataStorage {
    uint32 *memoryTable;
    uint32 usedStorage;
    uint32 storageLimit;
    uint32 **dataEntries[STORAGE_ENTRY_COUNT];   // pointer to the actual variable
    uint32 *storageEntries[STORAGE_ENTRY_COUNT]; // pointer to the storage in "memoryTable"
    uint32 entryCount;
    uint32 clearCount;
};

template <typename T> class List
{
    T *entries   = NULL;
    int32 count  = 0;
    int32 length = 0;

public:
    List()
    {
        entries = NULL;
        count   = 0;
    }
    ~List()
    {
        if (entries) {
            free(entries);
            entries = NULL;
        }
    }
    T *Append()
    {
        if (count == length) {
            length += 32;
            size_t len         = sizeof(T) * length;
            T *entries_realloc = (T *)realloc(entries, len);

            if (entries_realloc) {
                entries = entries_realloc;
            }
#if RETRO_PLATFORM == RETRO_PS1
            else // docs/32 4.2a: the stock goes on and clears an entry at NULL (the BIOS kernel's RAM)
                PS1OutOfMemory((unsigned int)len);
#endif
        }

        T *entry = &entries[count];
        memset(entry, 0, sizeof(T));
        count++;
        return entry;
    }
    void Remove(uint32 index)
    {
        // move every item back one
        for (int32 i = index; i < count; i++) {
            if (i + 1 < count) {
                entries[i] = entries[i + 1];
            }
            else { // Last Item, free it
                count--;
            }
        }

        if (count < length - 32) {
            length -= 32;
            size_t len         = sizeof(T) * length;
            T *entries_realloc = (T *)realloc(entries, len);

            if (entries_realloc)
                entries = entries_realloc;
        }
    }

    inline T *At(int32 index) { return &entries[index]; }

    inline void Clear(bool32 dealloc = false)
    {
        for (int32 i = count - 1; i >= 0; i--) {
            Remove(i);
        }

        if (entries && dealloc) {
            free(entries);
            entries = NULL;
        }
    }

    inline int32 Count() { return count; }
};

extern DataStorage dataStorage[DATASET_MAX];

bool32 InitStorage();
void ReleaseStorage();

void AllocateStorage(void **dataPtr, uint32 size, StorageDataSets dataSet, bool32 clear);
#if RETRO_PLATFORM == RETRO_PS1
// PS1 port (docs/32 2.3): a failed AllocateStorage stops at PS1StorageExhausted (stock code writes through the NULL it
// would get, into the BIOS kernel's RAM). PS1TryAllocateStorage is for the few callers that handle NULL themselves.
extern bool32 ps1StorageMayFail;
inline void PS1TryAllocateStorage(void **dataPtr, uint32 size, StorageDataSets dataSet, bool32 clear)
{
    ps1StorageMayFail = true;
    AllocateStorage(dataPtr, size, dataSet, clear);
    ps1StorageMayFail = false;
}
// Load-time scratch from the top of a dataset's free space (docs/32 2.3h): the dataset's limit is lowered by the block
// until PS1StorageTailRelease, so no allocation (and no compaction) reaches it. NULL when the free space is too small.
void *PS1StorageTailAlloc(StorageDataSets set, uint32 size);
extern uint32 ps1StgCapacity; // STG's memory, which ends at the fixed top (Storage.cpp 2.4a)
void PS1StorageTailRelease(StorageDataSets set);
// 7b.3c: the newest block of a dataset gives its tail back (true; false when another block came after it)
bool32 PS1StorageTrimLast(void **dataPtr, uint32 size, StorageDataSets set);
#endif
void DefragmentAndGarbageCollectStorage(StorageDataSets set);
void RemoveStorageEntry(void **dataPtr);
void CopyStorage(uint32 **src, uint32 **dst);
void GarbageCollectStorage(StorageDataSets dataSet);

#if RETRO_REV0U
#include "Legacy/UserStorageLegacy.hpp"
#endif

} // namespace RSDK

#endif // STORAGE_H
