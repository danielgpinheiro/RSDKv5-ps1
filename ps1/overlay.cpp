// PS1 port (docs/32 2.4): the stage folder's code overlay (tools/gen_overlays.py).
//
// Each overlay (Data/PS1Ovl/<folder>.bin) holds the code of that folder's stage objects that are not resident, linked
// at STG's fixed top less its size (RSDK/Storage/Storage.cpp 2.4a). LoadSceneFolder calls PS1LoadOverlay after the
// scene-start STG compaction: STG's limit then ends where the overlay starts, so each scene pays for its own code.
// The overlay starts with its header; its register function registers its object classes (after the resident ones)
// and fills the resident's stub table (ps1OvlTable) with the functions resident code or other overlays name.
#include "video_ops.hh"
#include "RSDK/Core/RetroEngine.hpp"
#include "RSDK/Graphics/PS1/PS1Scene3D.hpp"
#include "common/syscalls/syscalls.h"

using namespace RSDK;

extern "C" {
extern void *ps1OvlTable[];
extern void *const ps1OvlMiss[];
extern const unsigned ps1OvlCount;
extern void *const ps1OvlStubsRef;

volatile uint32 g_ps1OvlLoads  = 0; // overlays read (GDB)
volatile uint32 g_ps1OvlSize   = 0; // the loaded one's size
volatile uint32 g_ps1OvlBase   = 0; // and address
volatile uint32 g_ps1OvlErrors = 0; // a bad header or no room (must stay 0)
char g_ps1OvlFolder[0x18]; // the loaded overlay: a folder, or <folder>_<scene id> (7b)
void PS1StorageExhausted(uint32 set, uint32 size); // RSDK/Storage/Storage.cpp: stops
extern const char *const ps1OvlUnits[];  // 7b: the act overlays (generated: build/gen/ovl_resident.c)
const char *const *g_ps1OvlLeftOut = NULL; // 7b: the loaded act overlay's left-out classes (its register sets it)
}

PS1VideoOps g_ps1VideoOps = {};

namespace
{
struct PS1OverlayHeader {
    uint32 magic, size;
    void (*reg)(void);
    uint32 base;
};
#define PS1_OVL_MAGIC 0x314C564F // 'OVL1'
int32 s_residentClasses = -1;
} // namespace

// 7b: the overlay a scene loads: its act's when the build has one (ps1OvlUnits), else its folder's
static void PS1OverlayUnit(const char *folder, const char *scene, char *unit)
{
    sprintf_s(unit, sizeof(g_ps1OvlFolder), "%s_%s", folder, scene);
    for (const char *const *u = ps1OvlUnits; *u; ++u) {
        // 7b.8: the units take the scene files' spelling (Scene1K.bin), GameConfig's ids may differ in case ("1k")
        const char *a = *u, *b = unit;
        while (*a && *b && ((*a | 0x20) == (*b | 0x20) || *a == *b)) ++a, ++b;
        if (!*a && !*b) {
            sprintf_s(unit, sizeof(g_ps1OvlFolder), "%s", *u);
            return;
        }
    }
    sprintf_s(unit, sizeof(g_ps1OvlFolder), "%s", folder);
}

bool32 RSDK::PS1OverlayUnitChanges(const char *folder, const char *scene)
{
    char unit[sizeof(g_ps1OvlFolder)];
    PS1OverlayUnit(folder, scene, unit);
    return g_ps1OvlLoads && strcmp(unit, g_ps1OvlFolder) != 0;
}

bool32 RSDK::PS1OverlayLeftOut(const char *name)
{
    for (const char *const *n = g_ps1OvlLeftOut; n && *n; ++n)
        if (!strcmp(*n, name))
            return true;
    return false;
}

void RSDK::PS1LoadOverlay(const char *sceneFolder, const char *scene)
{
    char folder[sizeof(g_ps1OvlFolder)];
    PS1OverlayUnit(sceneFolder, scene, folder);
    if (s_residentClasses < 0)
        s_residentClasses = objectClassCount; // the first scene load: every resident class is registered
    if (g_ps1OvlLoads && !strcmp(folder, g_ps1OvlFolder))
        return;
    g_ps1OvlLeftOut = NULL;

    // the previous overlay's classes and functions go (its static variables were released by ClearStageObjects)
    objectClassCount = s_residentClasses;
    for (uint32 k = 0; k < ps1OvlCount; ++k) ps1OvlTable[k] = ps1OvlMiss[k];
    g_ps1VideoOps = PS1VideoOps{}; // docs/32 6.2: the video player goes with the overlay that had it
    g_ps1S3DOps   = PS1Scene3DOps{}; // 7.3: and the 3D draw
    g_ps1DeformDraw = NULL;           // 7.5: and the deformed-sprite draw
    g_ps1RotoPageDraw = NULL;         // 9e: and the rotozoom pages' draw
    (void)ps1OvlStubsRef;
    strncpy(g_ps1OvlFolder, folder, sizeof(g_ps1OvlFolder) - 1);
    g_ps1OvlLoads = g_ps1OvlLoads + 1;
    g_ps1OvlSize  = 0;
    g_ps1OvlBase  = 0;

    DataStorage *stg   = &dataStorage[DATASET_STG];
    stg->storageLimit  = ps1StgCapacity;
    uint32 top         = (uint32)(uintptr_t)stg->memoryTable + ps1StgCapacity;

    char path[0x40];
    sprintf_s(path, sizeof(path), "Data/PS1Ovl/%s.bin", folder);
    FileInfo info;
    InitFileInfo(&info);
    if (!LoadFile(&info, path, FMODE_RB))
        return; // a folder whose stage objects are all resident

    PS1OverlayHeader h;
    ReadBytes(&info, &h, sizeof(h));
    if (h.magic != PS1_OVL_MAGIC || h.size != (uint32)info.fileSize || h.base != top - h.size
        || stg->usedStorage * sizeof(uint32) + h.size > ps1StgCapacity) {
        g_ps1OvlErrors = g_ps1OvlErrors + 1;
        CloseFile(&info);
        PS1StorageExhausted(DATASET_STG, h.size); // stops (GDB): an overlay built for another EXE, or no room
    }
    stg->storageLimit = (ps1StgCapacity - h.size) & ~3u;
    Seek_Set(&info, 0);
    ReadBytes(&info, (void *)(uintptr_t)h.base, h.size);
    CloseFile(&info);
    syscall_flushCache(); // new code in RAM: the I-cache may hold the old overlay's lines

    g_ps1OvlSize = h.size;
    g_ps1OvlBase = h.base;
    ((PS1OverlayHeader *)(uintptr_t)h.base)->reg();
}
