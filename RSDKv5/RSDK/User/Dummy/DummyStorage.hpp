
#if RETRO_REV02

struct DummyFileInfo {
    void (*callback)(int32 status);
    int32 type;
#if RETRO_PLATFORM == RETRO_PS1
    char path[32]; // docs/32 9.2: the user files' names are 16 characters at most (TimeAttackDB.bin)
#else
    char path[64];
#endif
    void *fileBuffer;
    int32 fileSize;
    int32 storageTime;
    bool32 compressed;
#if RETRO_PLATFORM == RETRO_PS1
    int32 ps1Status; // phase 8: the I/O done at the request (the status the deferred callback gets), 0 = not yet
#endif
};

// This is the "dummy" struct, it serves as the base in the event a suitable API isn't loaded (such as in this decomp)
// This struct should never be removed, other structs such as "SteamUserStorage" would be added and "userStorage" would be set to that instead
struct DummyUserStorage : UserStorage {
    void FrameInit()
    {
        ProcessFileLoadTime();

        if (authStatus == STATUS_CONTINUE) {
            if (authTime <= 0) {
                authStatus = GetAPIValue(GetAPIValueID("SYSTEM_USERSTORAGE_AUTH_STATUS", 0));
            }
            else {
                authTime--;
            }
        }

        if (storageStatus == STATUS_CONTINUE) {
            if (storageInitTime <= 0) {
                storageStatus = GetAPIValue(GetAPIValueID("SYSTEM_USERSTORAGE_STORAGE_STATUS", 0));
            }
            else {
                storageInitTime--;
            }
        }

        if (!saveStatus) {
            if (authStatus == STATUS_ERROR || storageStatus == STATUS_ERROR)
                saveStatus = STATUS_ERROR;
            else if (storageStatus == STATUS_OK)
                saveStatus = STATUS_OK;
        }
    }
    void StageLoad() { UserStorage::StageLoad(); }
    int32 TryAuth();
    int32 TryInitStorage();
    bool32 GetUsername(String *name);
    bool32 TryLoadUserFile(const char *filename, void *buffer, uint32 size, void (*callback)(int32 status));
    bool32 TrySaveUserFile(const char *filename, void *buffer, uint32 size, void (*callback)(int32 status), bool32 compressed);
    bool32 TryDeleteUserFile(const char *filename, void (*callback)(int32 status));
    void ClearPrerollErrors();

    void ProcessFileLoadTime();

    int32 authTime        = 0;
    int32 storageInitTime = 0;
    List<DummyFileInfo> fileList;
};

#endif