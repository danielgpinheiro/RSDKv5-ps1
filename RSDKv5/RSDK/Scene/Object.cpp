#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/ObjectLegacy.cpp"
#endif

ObjectClass RSDK::objectClassList[OBJECT_COUNT];
int32 RSDK::objectClassCount = 0;

int32 RSDK::globalObjectCount = 0;
int32 RSDK::globalObjectIDs[OBJECT_COUNT];
int32 RSDK::stageObjectIDs[OBJECT_COUNT];

#if RETRO_PLATFORM == RETRO_PS1
#include "PS1/PS1Entities.cpp" // per-slot entity storage (docs/ports/mania/32 2.3)
#include "RSDK/Graphics/PS1/PS1Scene3D.hpp" // g_ps1S3DOps.sort: the UFO stages' sorted group (PS1Scene3D.cpp)
#else
EntityBase RSDK::objectEntityList[ENTITY_COUNT];
#endif

EditableVarInfo *RSDK::editableVarList;
int32 RSDK::editableVarCount = 0;

TypeGroupList RSDK::typeGroups[TYPEGROUP_COUNT];

bool32 RSDK::validDraw = false;

ForeachStackInfo RSDK::foreachStackList[FOREACH_STACK_COUNT];
ForeachStackInfo *RSDK::foreachStackPtr = NULL;

#if RETRO_REV0U
#if RETRO_USE_MOD_LOADER
void RSDK::RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(),
                          void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(),
                          void (*editorLoad)(), void (*editorDraw)(), void (*serialize)(), void (*staticLoad)(Object *))
{
    return RegisterObject_STD(staticVars, name, entityClassSize, staticClassSize, update, lateUpdate, staticUpdate, draw, create, stageLoad,
                              editorLoad, editorDraw, serialize, staticLoad);
}

void RSDK::RegisterObject_STD(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, std::function<void()> update,
                              std::function<void()> lateUpdate, std::function<void()> staticUpdate, std::function<void()> draw,
                              std::function<void(void *)> create, std::function<void()> stageLoad, std::function<void()> editorLoad,
                              std::function<void()> editorDraw, std::function<void()> serialize, std::function<void(Object *)> staticLoad)
#else
void RSDK::RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(),
                          void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(),
                          void (*editorLoad)(), void (*editorDraw)(), void (*serialize)(), void (*staticLoad)(Object *))
#endif
#else
#if RETRO_USE_MOD_LOADER
void RSDK::RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(),
                          void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(),
                          void (*editorLoad)(), void (*editorDraw)(), void (*serialize)())
{
    return RegisterObject_STD(staticVars, name, entityClassSize, staticClassSize, update, lateUpdate, staticUpdate, draw, create, stageLoad,
                              editorLoad, editorDraw, serialize);
}

void RSDK::RegisterObject_STD(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, std::function<void()> update,
                              std::function<void()> lateUpdate, std::function<void()> staticUpdate, std::function<void()> draw,
                              std::function<void(void *)> create, std::function<void()> stageLoad, std::function<void()> editorLoad,
                              std::function<void()> editorDraw, std::function<void()> serialize)
#else
void RSDK::RegisterObject(Object **staticVars, const char *name, uint32 entityClassSize, uint32 staticClassSize, void (*update)(),
                          void (*lateUpdate)(), void (*staticUpdate)(), void (*draw)(), void (*create)(void *), void (*stageLoad)(),
                          void (*editorLoad)(), void (*editorDraw)(), void (*serialize)())
#endif
#endif
{
    if (objectClassCount < OBJECT_COUNT) {
        if (entityClassSize > sizeof(EntityBase))
            PrintLog(PRINT_NORMAL, "Class exceeds max entity memory: %s", name);

        ObjectClass *classInfo = &objectClassList[objectClassCount];
        GEN_HASH_MD5(name, classInfo->hash);
        classInfo->staticVars      = staticVars;
        classInfo->entityClassSize = entityClassSize;
        classInfo->staticClassSize = staticClassSize;
        classInfo->update          = update;
        classInfo->lateUpdate      = lateUpdate;
        classInfo->staticUpdate    = staticUpdate;
        classInfo->draw            = draw;
        classInfo->create          = create;
        classInfo->stageLoad       = stageLoad;
        classInfo->editorLoad      = editorLoad;
        classInfo->editorDraw      = editorDraw;
        classInfo->serialize       = serialize;
#if RETRO_REV0U
        classInfo->staticLoad = staticLoad;
#endif

#if !RETRO_USE_ORIGINAL_CODE
        classInfo->name = name;
#endif

        ++objectClassCount;
    }
}

#if RETRO_REV02 || RETRO_USE_MOD_LOADER
void RSDK::RegisterStaticVariables(void **staticVars, const char *name, uint32 classSize)
{
    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(name, hash);
    AllocateStorage((void **)staticVars, classSize, DATASET_STG, true);
    LoadStaticVariables((uint8 *)*staticVars, hash, 0);
}
#endif

#define ALIGN_TO(type)                                                                                                                               \
    aligned = dataPos & -(int32)sizeof(type);                                                                                                        \
    if (aligned < dataPos)                                                                                                                           \
        dataPos = aligned + sizeof(type);

void RSDK::LoadStaticVariables(uint8 *classPtr, uint32 *hash, int32 readOffset)
{
    char fullFilePath[0x40];

    const char *hexChars = "0123456789ABCDEF";
    char classHash[]     = "00000000000000000000000000000000";

    int32 strPos = 0;
    for (int32 i = 0; i < 32; i += 4) classHash[strPos++] = hexChars[(hash[0] >> i) & 0xF];
    for (int32 i = 0; i < 32; i += 4) classHash[strPos++] = hexChars[(hash[1] >> i) & 0xF];
    for (int32 i = 0; i < 32; i += 4) classHash[strPos++] = hexChars[(hash[2] >> i) & 0xF];
    for (int32 i = 0; i < 32; i += 4) classHash[strPos++] = hexChars[(hash[3] >> i) & 0xF];

    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Objects/Static/%s.bin", classHash);

    FileInfo info;
    InitFileInfo(&info);
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        uint32 sig = ReadInt32(&info, false);

        if (sig != RSDK_SIGNATURE_OBJ) {
            CloseFile(&info);
            return;
        }

        int32 dataPos = readOffset;
        while (info.readPos < info.fileSize) {
            int32 type      = ReadInt8(&info);
            int32 arraySize = ReadInt32(&info, false);

            bool32 hasValues = (type & 0x80) != 0;
            type &= 0x7F;

            int32 aligned = 0;
            if (hasValues) {
                uint32 count = ReadInt32(&info, false);

                switch (type) {
                    default:
#if !RETRO_USE_ORIGINAL_CODE
                        PrintLog(PRINT_NORMAL, "Invalid static variable type: %d", type);
#endif
                        break;

                    case SVAR_UINT8:
                    case SVAR_INT8:
                        if (info.readPos + (count * sizeof(uint8)) <= info.fileSize && &classPtr[dataPos]) {
                            for (int32 i = 0; i < count * sizeof(uint8); i += sizeof(uint8)) ReadBytes(&info, &classPtr[dataPos + i], sizeof(uint8));
                        }
                        else {
                            info.readPos += count * sizeof(uint8);
                        }

                        dataPos += count * sizeof(uint8);
                        break;

                    case SVAR_UINT16:
                    case SVAR_INT16: {
                        ALIGN_TO(int16);

                        if (info.readPos + (count * sizeof(int16)) <= info.fileSize && &classPtr[dataPos]) {
                            for (int32 i = 0; i < count * sizeof(int16); i += sizeof(int16)) {
#if !RETRO_USE_ORIGINAL_CODE
                                *(int16 *)&classPtr[dataPos + i] = ReadInt16(&info);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &classPtr[dataPos + i], sizeof(int16));
#endif
                            }
                        }
                        else {
                            info.readPos += count * sizeof(int16);
                        }

                        dataPos += sizeof(int16) * count;
                        break;
                    }

                    case SVAR_UINT32:
                    case SVAR_INT32: {
                        ALIGN_TO(int32);

                        if (info.readPos + (count * sizeof(int32)) <= info.fileSize && &classPtr[dataPos]) {
                            for (int32 i = 0; i < count * sizeof(int32); i += sizeof(int32)) {
#if !RETRO_USE_ORIGINAL_CODE
                                *(int32 *)&classPtr[dataPos + i] = ReadInt32(&info, false);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &classPtr[dataPos + i], sizeof(int32));
#endif
                            }
                        }
                        else {
                            info.readPos += count * sizeof(int32);
                        }

                        dataPos += sizeof(int32) * count;
                        break;
                    }

                    case SVAR_BOOL: {
                        ALIGN_TO(bool32);

                        if (info.readPos + (count * sizeof(bool32)) <= info.fileSize && &classPtr[dataPos]) {
                            for (int32 i = 0; i < count * sizeof(bool32); i += sizeof(bool32)) {
#if !RETRO_USE_ORIGINAL_CODE
                                *(bool32 *)&classPtr[dataPos + i] = (bool32)ReadInt32(&info, false);
#else
                                // This only works as intended on little-endian CPUs.
                                ReadBytes(&info, &classPtr[dataPos + i], sizeof(bool32));
#endif
                            }
                        }
                        else {
                            info.readPos += count * sizeof(bool32);
                        }

                        dataPos += sizeof(bool32) * count;
                        break;
                    }
                }
            }
            else {
                switch (type) {
                    case SVAR_UINT8:
                    case SVAR_INT8: dataPos += sizeof(uint8) * arraySize; break;

                    case SVAR_UINT16:
                    case SVAR_INT16:
                        ALIGN_TO(int16);

                        dataPos += sizeof(int16) * arraySize;
                        break;

                    case SVAR_UINT32:
                    case SVAR_INT32:
                        ALIGN_TO(int32);

                        dataPos += sizeof(int32) * arraySize;
                        break;

                    case SVAR_BOOL:
                        ALIGN_TO(bool32);

                        dataPos += sizeof(bool32) * arraySize;
                        break;

                    case SVAR_POINTER:
                        ALIGN_TO(void *);

                        dataPos += sizeof(void *) * arraySize;
                        break;

                    case SVAR_VECTOR2:
                        ALIGN_TO(int32);

                        dataPos += sizeof(Vector2) * arraySize;
                        break;

                    case SVAR_STRING:
                        ALIGN_TO(void *);

                        dataPos += sizeof(String) * arraySize;
                        break;

                    case SVAR_ANIMATOR:
                        ALIGN_TO(void *);

                        dataPos += sizeof(Animator) * arraySize;
                        break;

                    case SVAR_HITBOX:
                        ALIGN_TO(int16);

                        dataPos += sizeof(Hitbox) * arraySize;
                        break;

                    case SVAR_SPRITEFRAME:
                        ALIGN_TO(int16);

                        dataPos += sizeof(GameSpriteFrame) * arraySize;
                        break;

                    default:
#if !RETRO_USE_ORIGINAL_CODE
                        PrintLog(PRINT_NORMAL, "Invalid data type: %d", type);
#endif
                        break;
                }
            }
        }

        CloseFile(&info);
    }
}

#if PS1_PROFILE
// `make PROFILE=1` (docs/ports/mania/32 phase 4 preview): CPU time per class, in root counter 2 ticks (system clock / 8 = 8 cycles),
// summed over the frames since boot; GDB reads them per objectClassList index. [0] staticUpdate + update + lateUpdate,
// [1] draw. Engine phases: g_ps1ProfPhase (RetroEngine.cpp).
volatile uint32 g_ps1ProfClass[OBJECT_COUNT][2];
volatile uint32 g_ps1ProfCalls[OBJECT_COUNT];
static inline uint16 PS1ProfNow() { return *(volatile uint16 *)0xBF801120; }
#define PS1_PROF(kind, id, call)                                                                                           \
    do {                                                                                                                   \
        uint16 ps1T_ = PS1ProfNow();                                                                                       \
        call;                                                                                                              \
        g_ps1ProfClass[id][kind] += (uint16)(PS1ProfNow() - ps1T_);                                                        \
        g_ps1ProfCalls[id]++;                                                                                              \
    } while (0)
// Engine parts in hblanks (root counter 1): [0] static updates, [1] the update pass, [2] type groups, [3] the late
// update pass, [4] draw-list entity draws, [5] layer scanline callbacks / parallax, [6] layer drawing
volatile uint32 g_ps1ProfPart[8];
volatile uint32 g_ps1ProfForeachAll[2]; // foreach_all loops started, slots they visited
#define PS1_HBL() (*(volatile uint16 *)0xBF801110)
#define PS1_PART_BEGIN(n) uint16 ps1P##n = PS1_HBL()
#define PS1_PART_END(n)   g_ps1ProfPart[n] += (uint16)(PS1_HBL() - ps1P##n)
#else
#define PS1_PROF(kind, id, call) call
#define PS1_PART_BEGIN(n)
#define PS1_PART_END(n)
#endif

#if RETRO_PLATFORM == RETRO_PS1
namespace
{
void PS1ScanSleepBegin(); // 9f UFO item 2 (PS1UpdateScan)
void PS1ScanSleepEnd();
} // namespace
#endif
void RSDK::InitObjects()
{
    sceneInfo.entitySlot = 0;
#if RETRO_PLATFORM == RETRO_PS1
    sceneInfo.createSlot = TEMPENTITY_START; // = ENTITY_COUNT - 0x100 with the stock counts; PS1 has fewer temp slots
#else
    sceneInfo.createSlot = ENTITY_COUNT - 0x100;
#endif
    cameraCount          = 0;
#if RETRO_PLATFORM == RETRO_PS1
    ++ps1CameraGen;
#endif
#if RETRO_PLATFORM == RETRO_PS1
    PS1ScanSleepBegin();
    // docs/ports/mania/32 9i: the second planned STG compaction, before the StageLoads: LoadSceneAssets leaves holes, and a StageLoad's
    // allocation that found no room at the tail forced a compaction that moved the frame tables earlier StageLoads' class
    // animators point into (FBZ2: UIWidgets_StageLoad after Spikes_StageLoad; at frame 147 Spikes' vertical animator read
    // a duration of 0 there and ProcessAnimation never returned). After this one the StageLoads and Creates allocate at a
    // hole-free tail (g_ps1StorageForcedDefrags[STG] stays 0: a guard of the harnesses' L AT lines)
    DefragmentAndGarbageCollectStorage(DATASET_STG);
#endif

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
#if RETRO_USE_MOD_LOADER
        currentObjectID = o;
#endif

        if (objectClassList[stageObjectIDs[o]].stageLoad)
            objectClassList[stageObjectIDs[o]].stageLoad();
    }
#if RETRO_PLATFORM == RETRO_PS1
    PS1ScanSleepEnd(); // (a StageLoad asked: UFO_Setup)
#endif

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONSTAGELOAD, NULL);
#endif

#if RETRO_PLATFORM == RETRO_PS1
    for (int32 e = 0; e < ENTITY_COUNT; e = PS1NextSlot(e)) {
#else
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
#endif
        sceneInfo.entitySlot = e;
        sceneInfo.entity     = OBJECT_ENTITY(e);
#if RETRO_PLATFORM == RETRO_PS1
        if (PS1DormantLoadCreate(e)) // 7b: a dormant record's Create runs here too, into a scratch block
            continue;
#endif

        if (sceneInfo.entity->classID) {
            if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].create) {
                sceneInfo.entity->interaction = true;
                objectClassList[stageObjectIDs[sceneInfo.entity->classID]].create(NULL);
            }
        }
    }

    sceneInfo.state = ENGINESTATE_REGULAR;
#if RETRO_PLATFORM == RETRO_PS1
    PS1DormantLoadDone(); // 7b
    PS1AtlasBindEnd(); // every StageLoad / Create has bound its frames (docs/ports/mania/32 3.1c)
#endif

    if (!cameraCount)
        AddCamera(&screens[0].position, TO_FIXED(screens[0].center.x), TO_FIXED(screens[0].center.y), false);
}
#if RETRO_PLATFORM == RETRO_PS1
// 3.0 (docs/ports/mania/32): the update pass tests each ACTIVE_BOUNDS entity against every camera (three in GHZ1): ~200 hblanks a
// frame. An entity outside the union of the cameras' boxes, grown by its own range, is out of range for every camera
// (in range for camera s means |x - cx| <= rx + ox, so x lies in [min(cx - ox) - rx, max(cx + ox) + rx], and the same
// for y): it skips the per-camera loop. The prefilter only applies where no sum can overflow (|camera position| < 2^30,
// |camera offset| <= 2^28, entity position in [-2^30, 2^30), range in [-2^28, 2^28)); otherwise, and inside the box,
// the stock loop decides.
#include "PS1/PS1ScanShared.hpp"
RSDK::PS1ScanShared RSDK::ps1Scan = { { 0, 0, 0x40000000u, 0, 0, 0x40000000u, 0, 0, 0, 0, 0 }, 0, 0, 0, 0, NULL, 0, 0, ~0u, 0,
                                     false, false, true };
RSDK::EntityBase **(*RSDK::g_ps1ScanSleepFn)(RSDK::EntityBase **, RSDK::EntityBase **) = NULL;
namespace
{
bool s_camUnionOk = false;
// 9w.1: the union box lives where ps1/scan_fast.s reads it: minX, w, vx, minY, h, vy, then the live bits, the De Bruijn
// table and the slot table (set by PS1UpdateScan), then s_camNearX (9w.4: every camera within 2^28 of camera 0 on x)
uint32 (&ps1ScanBox)[11] = ps1Scan.box; // (9w.6: in the state shared with PS1ScanSleep.cpp)
int32 &s_camMinX = (int32 &)ps1ScanBox[0], &s_camMinY = (int32 &)ps1ScanBox[3];
// 9u.7: the union is relative to camera 0's position (HCZ2, 28,000 px wide, three cameras, and SPZ1's second half had
// cameras past the absolute bounds): with d = x - origin and e = camera - origin in the bounds above (|e| < 2^30,
// d in [-2^30, 2^30)), stock's x - camera == d - e without wrapping, so the proof holds for d and e. The origin folds
// into the constants: minX is stored as origin + min(e - ox) (mod 2^32), the validity test adds 2^30 - origin.
// s_camAbs: camera 0 inside +-2^30 (the sleeping groups' margins assume it; with one camera)
uint32 &s_camVX = ps1ScanBox[2], &s_camVY = ps1ScanBox[5];
bool &s_camAbs = ps1Scan.camAbs;
// 7.4: one camera: ACTIVE_RBOUNDS slots (the UFO stages' rings and spheres: ~400) decided in the scan with stock's test
bool &s_camOne = ps1Scan.camOne;
int32 &s_camX = ps1Scan.camX, &s_camY = ps1Scan.camY, &s_camOX = ps1Scan.camOX, &s_camOY = ps1Scan.camOY;
// 9q's exact per-camera scan path (cameras the union can't take) went in 9u.12: since 9u.7 that is a camera 16,384 px or
// more from camera 0, which no act has; the stock loop decides then
uint32 &s_camW = ps1ScanBox[1], &s_camH = ps1ScanBox[4]; // max - min of the union, modulo 2^32
uint32 &s_camNearX = ps1ScanBox[9];
inline bool PS1Small(int32 v, int32 lim) { return (uint32)(v + lim) <= (uint32)(2 * lim); }
uint32 s_camGen = 0;
void PS1CameraUnion(bool passStart = false)
{
    // 9e.2: asked after every update callback (one may add or clear cameras). 9u: the camera table changes during a pass
    // only through AddCamera / ClearCameras (ps1CameraGen; the game has no other access to it, GameLink.h), the positions
    // are copied from their targets before the passes: a pass start recomputes, a callback only after a change (OOZ2's
    // ~200 parallax sprites with two cameras: 27 hblanks a frame)
    if (!passStart && s_camGen == ps1CameraGen)
        return;
    s_camGen = ps1CameraGen;
    s_camUnionOk = cameraCount > 0;
    const uint32 orgX = (uint32)cameras[0].position.x, orgY = (uint32)cameras[0].position.y;
    s_camAbs = PS1Small((int32)orgX, 0x3FFFFFFF) && PS1Small((int32)orgY, 0x3FFFFFFF);
    int32 minX = 0, maxX = 0, minY = 0, maxY = 0;
    s_camNearX = 1;
    for (int32 s = 0; s < cameraCount; ++s) {
        CameraInfo *c = &cameras[s];
        const int32 ex = (int32)((uint32)c->position.x - orgX), ey = (int32)((uint32)c->position.y - orgY);
        if (!PS1Small(ex, 0x3FFFFFFF) || !PS1Small(ey, 0x3FFFFFFF) || !PS1Small(c->offset.x, 0x10000000)
            || !PS1Small(c->offset.y, 0x10000000)) {
            s_camUnionOk = false;
            return;
        }
        if (!PS1Small(ex, 0x0FFFFFFF))
            s_camNearX = 0;
        int32 x0 = ex - c->offset.x, x1 = ex + c->offset.x;
        int32 y0 = ey - c->offset.y, y1 = ey + c->offset.y;
        if (!s || x0 < minX)
            minX = x0;
        if (!s || x1 > maxX)
            maxX = x1;
        if (!s || y0 < minY)
            minY = y0;
        if (!s || y1 > maxY)
            maxY = y1;
    }
    s_camMinX = (int32)(orgX + (uint32)minX), s_camMinY = (int32)(orgY + (uint32)minY);
    s_camVX = 0x40000000u - orgX, s_camVY = 0x40000000u - orgY;
    s_camW = (uint32)maxX - (uint32)minX, s_camH = (uint32)maxY - (uint32)minY;
    s_camOne = cameraCount == 1;
    if (s_camOne)
        s_camX = cameras[0].position.x, s_camY = cameras[0].position.y, s_camOX = cameras[0].offset.x, s_camOY = cameras[0].offset.y;
}

// 3.0f: a bit per slot that may have inRange or onScreen set. The update pass marks each slot it leaves in range, the
// draw lists mark each entity whose onScreen they set; every other slot has inRange false and onScreen 0 (its update
// visit cleared inRange, the late pass cleared onScreen; creating or resetting an entity clears both). Only CopyEntity
// copies them: after a copy the type-group and late passes visit every slot until the bits are whole again (a late
// pass with no copy during it). The late pass clears a slot's bit where it clears that slot's own onScreen (a callback
// that leaves sceneInfo.entity on another entity clears that one's instead, as stock: the slot keeps its bit).
extern "C" {
extern volatile uint32 g_ps1EntityCopies; // PS1Entities.cpp
volatile uint32 g_ps1SlotBitsFull = 0;    // type-group / late passes that visited every slot (total)
}
uint32 ps1SlotBits[(ENTITY_COUNT + 31) / 32];
uint32 s_ps1BitsFrom = 0xFFFFFFFF; // g_ps1EntityCopies when the bits were last whole (never, at first: full passes)
inline void PS1SlotMark(int32 s) { ps1SlotBits[s >> 5] |= 1u << (s & 31); }
inline void PS1SlotUnmark(int32 s) { ps1SlotBits[s >> 5] &= ~(1u << (s & 31)); }

// The next marked slot in [s, end), or end (a De Bruijn table finds a word's lowest bit: no CLZ on the R3000)
const uint8 s_ps1DeBruijn[32] = { 0,  1,  28, 2,  29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4,  8,
                                  31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6,  11, 5,  10, 9 };
inline int32 PS1SlotNext(int32 s, int32 end)
{
    while (s < end) {
        uint32 w = ps1SlotBits[s >> 5] >> (s & 31);
        if (w) {
            uint32 low = w & (0u - w);
            s += s_ps1DeBruijn[(low * 0x077CB531u) >> 27];
            return s < end ? s : end;
        }
        s = (s | 31) + 1;
    }
    return end;
}

#if PS1_CHECK_SLOTBITS // evidence build: audits of the bits (GDB reads the counters; must stay 0)
extern "C" {
volatile uint32 g_ps1SlotBitsWrong = 0, g_ps1SlotBitsChecks = 0;
}
inline bool PS1SlotMarked(int32 s) { return (ps1SlotBits[s >> 5] >> (s & 31)) & 1; }
// every slot of the ranges with inRange (when `range`) or onScreen set must be marked
void PS1SlotAudit(bool range)
{
    g_ps1SlotBitsChecks = g_ps1SlotBitsChecks + 1;
    for (int32 phase = 0; phase < 2; ++phase) {
        int32 s0 = phase ? TEMPENTITY_START : 0, s1 = phase ? ps1TempSlotEnd : ps1SceneSlotEnd;
        for (int32 s = s0; s < s1; ++s) {
            EntityBase *e = objectEntityPtr[s];
            if (((range && e->inRange) || e->onScreen) && !PS1SlotMarked(s))
                g_ps1SlotBitsWrong = g_ps1SlotBitsWrong + 1;
        }
    }
}
#endif

// The three per-frame passes over the slots (ProcessObjects), with the stock semantics: the same slots in the same
// order (the scene range up to ps1SceneSlotEnd, then the temp range up to ps1TempSlotEnd: the other slots are ghosts
// with class 0), the same writes, sceneInfo set as stock whenever a callback runs (and, after the pass, to the last
// slot's entity and ENTITY_COUNT), bounds and cameras re-read after a callback (it may create entities or add cameras).
// Without a data cache every load is a RAM access, so each pass walks the slot table by pointer in a leaf scan (it
// calls nothing: its locals stay in registers) that settles the slots with no callback to run and stops at the next
// one; the pass runs that slot as stock and scans on. GCC's -Os spilled the locals of the inlined loops to the stack.
#define PS1_HOT __attribute__((noinline, optimize("O2")))

// Update: blank slots, ACTIVE_NEVER / ACTIVE_PAUSED, and ACTIVE_BOUNDS outside the camera box are out of range.
// 3.0d: a slot's six fields are loaded before any test (most slots are ACTIVE_BOUNDS entities, which need them all), so
// the loads fill each other's delay slots instead of a nop after each (each load's result is ready one instruction on)
#if PS1_CHECK_LIVEBITS
extern "C" {
volatile uint32 g_ps1LiveChecks = 0, g_ps1LiveWrong = 0; // evidence build: classed slots whose bit was clear (must stay 0)
}
#endif
// 9f: slots with their bits clear (blank, settled: inRange already false) are passed over; a blank slot found has
// its bit cleared after its stock write (the UFO stages' scene ranges: 1,427 of 2,112 slots blank). 9q: each such slot,
// not only words of 32 (a zone's ~550 live slots are spread over nearly every word of its ~2,200: the scan loaded every
// slot's pointer and class): the scan goes to the next set bit (the rest of the word clear: to the next word)
#define PS1_LIVE_SKIP()                                                                                                \
    {                                                                                                                  \
        uint32 lw_ = ps1LiveBits[s >> 5] >> (s & 31);                                                                  \
        if (!lw_) {                                                                                                    \
            uint32 adv_ = 32 - (s & 31);                                                                               \
            if ((uint32)(pend - p) <= adv_)                                                                            \
                return pend;                                                                                           \
            p += adv_ - 1, s += adv_ - 1;                                                                              \
            continue;                                                                                                  \
        }                                                                                                              \
        if (!(lw_ & 1)) { /* 9u: the next live slot by the De Bruijn table (was a bit-by-bit walk) */                         \
            uint32 z_ = s_ps1DeBruijn[((lw_ & (0u - lw_)) * 0x077CB531u) >> 27];                                         \
            if ((uint32)(pend - p) <= z_)                                                                              \
                return pend;                                                                                           \
            p += z_, s += z_;                                                                                          \
        }                                                                                                              \
    }
// the whole-word skip only (9f): with sleeping groups on, a group goes to sleep at its last slot, which must be visited
#define PS1_LIVE_SKIP_WORDS()                                                                                          \
    if (!(s & 31) && !ps1LiveBits[s >> 5]) {                                                                           \
        if (pend - p <= 32)                                                                                            \
            return pend;                                                                                               \
        p += 31, s += 31;                                                                                              \
        continue;                                                                                                      \
    }
// docs/ports/mania/32 9f (UFO item 2): sleeping groups. In the UFO stages ~540 of ~690 live slots (rings, spheres, item boxes:
// ACTIVE_RBOUNDS; decorations: ACTIVE_BOUNDS) are tested and found out of range every frame. A group of 8 slots whose
// live slots are all of those two kinds and were all settled out of range in one stretch of the scan goes to sleep with
// a margin: how far (whole pixels, on each axis) the one camera may move with every slot still out of range in stock's
// own arithmetic (radius slots: the squared distance's floor root minus the range's root rounded up, times 181/256 <
// 1/sqrt(2); box slots: the failing axis's slack). While the camera stays within the margin of where it was, the scan
// passes over the group (its slots keep inRange false: nothing updates them). Woken by any class write into the group
// (PS1ClassNote: create, reset, copy), a camera offset change, and every frame the scan takes the stock path. Opt-in per
// scene: a StageLoad sets g_ps1ScanSleepWant (UFO_Setup; its stages' objects were reviewed: nothing writes an out-of-range
// slot's position, range or active). PS1_CHECK_SLEEP: stock's test on every slot passed over (g_ps1SleepWrong).
void *&s_sleep  = ps1Scan.sleep; // the sleeping groups (9w.6: their scan is PS1ScanSleep.cpp's, in the UFO overlays)
int32 &s_sleepOX = ps1Scan.sleepOX, &s_sleepOY = ps1Scan.sleepOY;
bool &s_grpOk    = ps1Scan.grpOk;
} // namespace
extern "C" volatile int32 g_ps1ScanSleepWant;
volatile int32 g_ps1ScanSleepWant = 0; // set by a scene's StageLoad (SonicMania/Objects/UFO/UFO_Setup.c)
namespace
{
extern "C" volatile int32 g_ps1LogicSteps, g_ps1Uncapped; // ps1/rsdk5_main.cpp
void PS1ScanSleepBegin()
{
    g_ps1ScanSleepWant = 0;
    // 9f: a StageLoad asks for two (UFO_Setup); choice 182: the zones (category 1) take two unless Uncapped FPS is on
    g_ps1LogicSteps = (!g_ps1Uncapped && sceneInfo.activeCategory == 1) ? 2 : 1;
    s_sleep            = NULL; // (STG was reset by the load)
    ps1Scan.grp        = ~0u;
    for (uint32 i = 0; i < sizeof(ps1SleepBits) / 4; ++i) ps1SleepBits[i] = 0;
}
void PS1ScanSleepEnd()
{
    if (g_ps1ScanSleepWant)
        AllocateStorage((void **)&s_sleep, 8 * (ENTITY_COUNT / 8), DATASET_STG, true); // (PS1ScanSleep.cpp's 8-byte groups)
    s_sleepOX = s_camOX, s_sleepOY = s_camOY;
}
// Cameras the union can't take: the stock loop decides every live slot (blank ones settled, as everywhere)
PS1_HOT EntityBase **PS1UpdateScanStock(EntityBase **p, EntityBase **pend)
{
    uint32 s = (uint32)(p - objectEntityPtr);
    for (; p < pend; ++p, ++s) {
        PS1_LIVE_SKIP();
        EntityBase *ent = *p;
        if (ent->classID && ent->active != ACTIVE_NEVER && ent->active != ACTIVE_PAUSED)
            return p;
        ent->inRange = false;
        if (!ent->classID)
            ps1LiveBits[s >> 5] &= ~(1u << (s & 31));
    }
    return p;
}

// 9w.1 (docs/ports/mania/32): one camera box without sleeping groups (every zone) goes to ps1/scan_fast.s, the same
// decisions as the C loop above but for ACTIVE_RBOUNDS (returned to the pass, which decides as stock); its sleeping-group
// state stays as the C loop leaves it with sleepOn false (a slot returned to the pass clears s_grpOk). The sleeping
// groups are 0 without a sleep table (only PS1SleepGroupSet sets them, with one): their clear is left out then.
extern "C" EntityBase **PS1ScanFastAsm(EntityBase **p, EntityBase **pend, uint32 s, const uint32 *box);
#if PS1_CHECK_SCANASM || PS1_CHECK_UPDATESCAN
// evidence build (make EXTRA_CPPFLAGS=-DPS1_CHECK_SCANASM=1, or the older PS1_CHECK_UPDATESCAN; touch this file): every
// slot the assembly passed over has inRange false and stock's test out of range for every camera (blank: its live bit
// clear), and the slot it returns is live and classed (GDB reads the counters; g_ps1ScanAsmWrong must stay 0)
extern "C" {
volatile uint32 g_ps1ScanAsmCalls = 0, g_ps1ScanAsmSlots = 0, g_ps1ScanAsmWrong = 0, g_ps1ScanAsmWho = 0;
}
bool PS1StockInRange(const EntityBase *e)
{
    switch (e->active) {
        default: return e->inRange;
        case ACTIVE_NEVER:
        case ACTIVE_PAUSED: return false;
        case ACTIVE_ALWAYS:
        case ACTIVE_NORMAL: return true;
        case ACTIVE_BOUNDS:
            for (int32 c = 0; c < cameraCount; ++c)
                if (abs(e->position.x - cameras[c].position.x) <= e->updateRange.x + cameras[c].offset.x
                    && abs(e->position.y - cameras[c].position.y) <= e->updateRange.y + cameras[c].offset.y)
                    return true;
            return false;
        case ACTIVE_XBOUNDS:
            for (int32 c = 0; c < cameraCount; ++c)
                if (abs(e->position.x - cameras[c].position.x) <= e->updateRange.x + cameras[c].offset.x)
                    return true;
            return false;
        case ACTIVE_YBOUNDS:
            for (int32 c = 0; c < cameraCount; ++c)
                if (abs(e->position.y - cameras[c].position.y) <= e->updateRange.y + cameras[c].offset.y)
                    return true;
            return false;
        case ACTIVE_RBOUNDS:
            for (int32 c = 0; c < cameraCount; ++c) {
                int32 sx = FROM_FIXED(abs(e->position.x - cameras[c].position.x));
                int32 sy = FROM_FIXED(abs(e->position.y - cameras[c].position.y));
                if (sx * sx + sy * sy <= e->updateRange.x + cameras[c].offset.x)
                    return true;
            }
            return false;
    }
}
// 9w.8: the slots the update pass's helper handled during this scan (decided as stock: not settled by the scan)
uint16 s_auditDone[512];
int32 s_auditDoneN = 0;
void PS1ScanAsmCheck(EntityBase **p, EntityBase **q, EntityBase **pend)
{
    g_ps1ScanAsmCalls = g_ps1ScanAsmCalls + 1;
    for (; p < q; ++p) {
        const EntityBase *e = *p;
        const uint32 s       = (uint32)(p - objectEntityPtr);
        bool done            = false;
        for (int32 k = 0; k < s_auditDoneN && !done; ++k) done = s_auditDone[k] == s;
        if (done)
            continue;
        const bool bit       = (ps1LiveBits[s >> 5] >> (s & 31)) & 1;
        g_ps1ScanAsmSlots    = g_ps1ScanAsmSlots + 1;
        if (e->classID ? (e->inRange || PS1StockInRange(e)) : (e->inRange || bit)) {
            if (!g_ps1ScanAsmWrong)
                g_ps1ScanAsmWho = s;
            g_ps1ScanAsmWrong = g_ps1ScanAsmWrong + 1;
        }
    }
    s_auditDoneN = 0;
    if (q != pend && !ps1ScanBox[10]) { // (with the helper a return before the end follows a handled slot)
        const uint32 s = (uint32)(q - objectEntityPtr);
        if (!(*q)->classID || !((ps1LiveBits[s >> 5] >> (s & 31)) & 1)) {
            if (!g_ps1ScanAsmWrong)
                g_ps1ScanAsmWho = s | 0x10000;
            g_ps1ScanAsmWrong = g_ps1ScanAsmWrong + 1;
        }
    }
}
#endif
__attribute__((always_inline)) inline EntityBase **PS1UpdateScan(EntityBase **p, EntityBase **pend)
{
    // 9w.6: the sleeping groups (one camera, a sleep table, the UFO overlay's scan linked) keep their C scan, in the
    // overlay; any other frame wakes every group (stock decides; sleeping is only a shortcut)
    if (s_sleep && s_camUnionOk && s_camOne && s_camAbs && g_ps1ScanSleepFn)
        return g_ps1ScanSleepFn(p, pend);
    if (s_sleep)
        for (uint32 w = 0; w < sizeof(ps1SleepBits) / 4; ++w) ps1SleepBits[w] = 0;
    if (!s_camUnionOk)
        return PS1UpdateScanStock(p, pend);
    ps1ScanBox[6] = (uint32)ps1LiveBits, ps1ScanBox[7] = (uint32)s_ps1DeBruijn, ps1ScanBox[8] = (uint32)objectEntityPtr;
    EntityBase **q = PS1ScanFastAsm(p, pend, (uint32)(p - objectEntityPtr), ps1ScanBox);
#if PS1_CHECK_SCANASM || PS1_CHECK_UPDATESCAN
    PS1ScanAsmCheck(p, q, pend);
#endif
    if (q != pend)
        s_grpOk = false;
    return q;
}

// 9q: libc's abs, inline (it was a call per test: ~20 hblanks a frame in HCZ2): (v ^ v >> 31) - (v >> 31), INT_MIN stays
__attribute__((always_inline)) inline int32 PS1Abs(int32 v) { return (int32)(((uint32)v ^ (uint32)(v >> 31)) - (uint32)(v >> 31)); }
// The update passes' range decision for a live slot the scan returned (stock's switch): sets and returns inRange
__attribute__((always_inline)) inline bool32 PS1RangeDecide(EntityBase *ent)
{
    bool32 inRange;
    switch (ent->active) {
        default:
        case ACTIVE_DISABLED: inRange = ent->inRange; break;

        case ACTIVE_NEVER:
        case ACTIVE_PAUSED: ent->inRange = inRange = false; break;

        case ACTIVE_ALWAYS:
        case ACTIVE_NORMAL: ent->inRange = inRange = true; break;

        case ACTIVE_BOUNDS:
            inRange = false;
            for (int32 s = 0; s < cameraCount; ++s) {
                int32 sx = PS1Abs(ent->position.x - cameras[s].position.x);
                int32 sy = PS1Abs(ent->position.y - cameras[s].position.y);
                if (sx <= ent->updateRange.x + cameras[s].offset.x && sy <= ent->updateRange.y + cameras[s].offset.y) {
                    inRange = true;
                    break;
                }
            }
            ent->inRange = inRange;
            break;

        case ACTIVE_XBOUNDS:
            inRange = false;
            for (int32 s = 0; s < cameraCount; ++s) {
                int32 sx = PS1Abs(ent->position.x - cameras[s].position.x);
                if (sx <= ent->updateRange.x + cameras[s].offset.x) {
                    inRange = true;
                    break;
                }
            }
            ent->inRange = inRange;
            break;

        case ACTIVE_YBOUNDS:
            inRange = false;
            for (int32 s = 0; s < cameraCount; ++s) {
                int32 sy = PS1Abs(ent->position.y - cameras[s].position.y);
                if (sy <= ent->updateRange.y + cameras[s].offset.y) {
                    inRange = true;
                    break;
                }
            }
            ent->inRange = inRange;
            break;

        case ACTIVE_RBOUNDS:
            inRange = false;
            for (int32 s = 0; s < cameraCount; ++s) {
                int32 sx = FROM_FIXED(PS1Abs(ent->position.x - cameras[s].position.x));
                int32 sy = FROM_FIXED(PS1Abs(ent->position.y - cameras[s].position.y));
                if (sx * sx + sy * sy <= ent->updateRange.x + cameras[s].offset.x) {
                    inRange = true;
                    break;
                }
            }
            ent->inRange = inRange;
            break;
    }

    return inRange;
}

// 9w.8 (docs/ports/mania/32): the update pass's handling of one slot the scan could not settle (stock's range decision,
// the update, the draw group), called by ps1/scan_fast.s itself (box +40) instead of a return to the pass for each one
// (OOZ2's ~200 parallax sprites in range: the scan's entry and the pass's loop around every update). Returns the
// range's end (a callback may add slots), or NULL when the camera union no longer holds (the pass goes on in C).
int32 s_updPhase = 0;
extern "C" __attribute__((noinline)) EntityBase **PS1UpdateOne(EntityBase **p)
{
    EntityBase *ent = *p; // class != 0 (the scan settles blank slots)
#if PS1_CHECK_SCANASM || PS1_CHECK_UPDATESCAN
    if (s_auditDoneN < 512)
        s_auditDone[s_auditDoneN++] = (uint16)(p - objectEntityPtr);
    if (!ent->classID) {
        if (!g_ps1ScanAsmWrong)
            g_ps1ScanAsmWho = (uint32)(p - objectEntityPtr) | 0x20000;
        g_ps1ScanAsmWrong = g_ps1ScanAsmWrong + 1;
    }
#endif
    if (PS1RangeDecide(ent)) {
        sceneInfo.entitySlot = (int32)(p - objectEntityPtr);
        sceneInfo.entity     = ent;
        PS1SlotMark(sceneInfo.entitySlot);
        void (*update)(void) = objectClassList[stageObjectIDs[ent->classID]].update;
        if (update) {
            PS1_PROF(0, stageObjectIDs[ent->classID], update());
            PS1CameraUnion();
        }
        // stock reads these after the callback
        if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT)
            PS1_DRAWLIST_APPEND(&drawGroups[sceneInfo.entity->drawGroup], sceneInfo.entitySlot);
    }
    if (!s_camUnionOk)
        return NULL;
    return objectEntityPtr + (s_updPhase ? ps1TempSlotEnd : ps1SceneSlotEnd);
}

PS1_HOT void PS1UpdatePass()
{
    PS1DormantUpdate(); // 7b: dormant entities the cameras reach become entities first (PS1Entities.cpp)
    PS1CameraUnion(true);
#if PS1_CHECK_LIVEBITS
    g_ps1LiveChecks = g_ps1LiveChecks + 1;
    for (int32 r = 0; r < 2; ++r)
        for (int32 sl = r ? TEMPENTITY_START : 0; sl < (r ? ps1TempSlotEnd : ps1SceneSlotEnd); ++sl)
            if (objectEntityPtr[sl]->classID && !(ps1LiveBits[sl >> 5] & (1u << (sl & 31))))
                g_ps1LiveWrong = g_ps1LiveWrong + 1;
#endif
    EntityBase **p = objectEntityPtr, **pend = objectEntityPtr + ps1SceneSlotEnd;
    int32 phase     = 0;
    EntityBase *ent = NULL; // the last slot visited
    s_updPhase      = 0;
    ps1ScanBox[10]  = (uint32)PS1UpdateOne; // 9w.8 (the frozen pass's scans return their slots)
    for (;;) {
        if (p >= pend) {
            if (phase)
                break;
            phase = 1, p = objectEntityPtr + TEMPENTITY_START, pend = objectEntityPtr + ps1TempSlotEnd;
            s_updPhase = 1;
            continue;
        }
        EntityBase **q = PS1UpdateScan(p, pend);
        if (q != p) {
            ent = q[-1], p = q;
            continue;
        }

        ent = *p; // a slot the stock loop decides (cameras the union can't take)
        PS1UpdateOne(p);
        pend = objectEntityPtr + (phase ? ps1TempSlotEnd : ps1SceneSlotEnd); // (a callback may add slots)
        ++p;
    }
    ps1ScanBox[10] = 0;
    if (ent)
        sceneInfo.entity = ent;
    sceneInfo.entitySlot = ENTITY_COUNT;
}

// Type groups: no callback runs; the scan is PS1TypeGroupsCollect (PS1Entities.cpp)
PS1_HOT void PS1TypeGroupPass()
{
    EntityBase *ent = NULL;
    bool whole      = g_ps1EntityCopies == s_ps1BitsFrom;
    if (!whole)
        g_ps1SlotBitsFull = g_ps1SlotBitsFull + 1;
    for (int32 phase = 0; phase < 2; ++phase) {
        int32 s0 = phase ? TEMPENTITY_START : 0, s1 = phase ? ps1TempSlotEnd : ps1SceneSlotEnd;
        if (s1 <= s0)
            continue;
        if (whole) { // 3.0f: only the marked slots can be in range
#if PS1_CHECK_SLOTBITS
            if (phase == 0)
                PS1SlotAudit(true);
#endif
            PS1TypeGroupsCollectMarked(ps1SlotBits, s0, s1);
        }
        else {
            PS1TypeGroupsCollect(objectEntityPtr + s0, objectEntityPtr + s1);
        }
        ent = objectEntityPtr[s1 - 1];
    }
    if (ent)
        sceneInfo.entity = ent;
    sceneInfo.entitySlot = ENTITY_COUNT;
}

// Late update: the scan clears onScreen on each slot whose lateUpdate does not run (`keep`: clears nothing)
PS1_HOT EntityBase **PS1LateScan(EntityBase **p, EntityBase **pend, bool keep)
{
    if (keep) {
        for (; p < pend; ++p) {
            EntityBase *ent = *p;
            if (ent->inRange && objectClassList[stageObjectIDs[ent->classID]].lateUpdate)
                return p;
        }
        return p;
    }
    for (; p < pend; ++p) {
        EntityBase *ent = *p;
        if (ent->inRange && objectClassList[stageObjectIDs[ent->classID]].lateUpdate)
            return p;
        ent->onScreen = 0;
    }
    return p;
}

// 3.0f: with whole bits, only the marked slots (the others have inRange false and onScreen 0 already: no callback, the
// clear a no-op); a CopyEntity during the pass makes the rest of it visit every slot, and the next frame's passes too
// 9r (choice 185): the stock late pass clears every onScreen and only drawing sets it, so at 30 fps (two logic
// steps a drawn frame) the second step's updates saw every object off-screen (PSZ1's intro destroyed its gliders, Metal
// Sonic, Splats, swinging platforms...). A drawn frame's earlier steps keep onScreen (and the slots' bits: a superset
// stays valid); after such a pass the bits are not declared whole (an entity copy may have carried an onScreen into an
// unmarked slot): the last step's pass clears them as stock. One step a frame: unchanged.
static inline bool PS1KeepOnScreen() { return !g_ps1LastStep; }

PS1_HOT void PS1LateUpdatePass()
{
    bool keep       = PS1KeepOnScreen();
    uint32 copies   = g_ps1EntityCopies;
    bool whole      = copies == s_ps1BitsFrom;
    bool wholeStart = whole;
#if PS1_CHECK_SLOTBITS
    if (whole)
        PS1SlotAudit(true);
#endif
    EntityBase *ent = NULL; // the last slot visited (after its callback: stock's sceneInfo.entity)
    int32 last      = -1;   // its slot
    for (int32 phase = 0; phase < 2; ++phase) {
        int32 s0 = phase ? TEMPENTITY_START : 0;
        int32 s  = s0;
        for (;;) {
            int32 s1 = phase ? ps1TempSlotEnd : ps1SceneSlotEnd; // a callback may add slots
            if (whole)
                s = PS1SlotNext(s, s1);
            else if (s < s1) { // every slot: the scan settles the ones with no callback
                EntityBase **q = PS1LateScan(objectEntityPtr + s, objectEntityPtr + s1, keep);
                int32 qs       = (int32)(q - objectEntityPtr);
                if (!keep)
                    for (int32 k = s; k < qs; ++k) PS1SlotUnmark(k); // cleared their own onScreen
                if (qs != s)
                    ent = q[-1], last = qs - 1;
                s = qs;
            }
            if (s >= s1)
                break;
            EntityBase *e = objectEntityPtr[s];
            if (e->inRange && objectClassList[stageObjectIDs[e->classID]].lateUpdate) {
                sceneInfo.entitySlot = s;
                sceneInfo.entity     = e;
                PS1_PROF(0, stageObjectIDs[e->classID], objectClassList[stageObjectIDs[e->classID]].lateUpdate());
                ent = (EntityBase *)sceneInfo.entity; // stock clears onScreen through sceneInfo.entity
                if (!keep) {
                    ent->onScreen = 0;
                    if (ent == objectEntityPtr[s])
                        PS1SlotUnmark(s);
                    else
                        PS1SlotMark(s); // its own onScreen stays: cleared by a later pass
                }
                if (g_ps1EntityCopies != copies)
                    whole = false; // a copy: every slot from here on
            }
            else {
                if (!keep) {
                    e->onScreen = 0;
                    PS1SlotUnmark(s);
                }
                ent = e;
            }
            last = s;
            ++s;
        }
        if (!wholeStart)
            continue;
        // stock's last visited slot is the range's last: settled without a callback unless the walk ended on it
        int32 s1 = phase ? ps1TempSlotEnd : ps1SceneSlotEnd;
        if (s1 > s0 && last != s1 - 1)
            ent = objectEntityPtr[s1 - 1], last = s1 - 1;
    }
    if (g_ps1EntityCopies == copies && (wholeStart || !keep))
        s_ps1BitsFrom = copies; // no copy since the bits were made whole again: whole from here
#if PS1_CHECK_SLOTBITS
    if (g_ps1EntityCopies == copies)
        PS1SlotAudit(false); // after the pass: every unmarked slot's onScreen is 0
#endif
    if (!wholeStart)
        g_ps1SlotBitsFull = g_ps1SlotBitsFull + 1;
    if (ent)
        sceneInfo.entity = ent;
    sceneInfo.entitySlot = ENTITY_COUNT;
}

// 3.0h: ProcessPausedObjects' two loops (the title card and pause screens run in ENGINESTATE_PAUSED) as leaf scans
// with the stock semantics: the update loop runs ACTIVE_ALWAYS / ACTIVE_PAUSED entities (update, then their draw
// group through sceneInfo.entity) and clears inRange on blank slots; the late loop runs lateUpdate for ALWAYS / PAUSED
// entities and clears onScreen through sceneInfo.entity on every slot. sceneInfo.entity ends on the last slot visited
// (after its callback), entitySlot on ENTITY_COUNT, as PS1_NEXT_SLOT_ES leaves them.
PS1_HOT EntityBase **PS1PausedScan(EntityBase **p, EntityBase **pend)
{
    for (; p < pend; ++p) {
        EntityBase *ent = *p;
        uint32 cls = ent->classID, act = ent->active;
        if (!cls)
            ent->inRange = false;
        else if (act == ACTIVE_ALWAYS || act == ACTIVE_PAUSED)
            return p;
    }
    return p;
}

PS1_HOT EntityBase **PS1PausedLateScan(EntityBase **p, EntityBase **pend, bool keep)
{
    for (; p < pend; ++p) {
        EntityBase *ent = *p;
        uint32 act = ent->active;
        if ((act == ACTIVE_ALWAYS || act == ACTIVE_PAUSED) && objectClassList[stageObjectIDs[ent->classID]].lateUpdate)
            return p;
        if (!keep) // (9r: see PS1LateUpdatePass)
            ent->onScreen = 0;
    }
    return p;
}

PS1_HOT void PS1PausedUpdatePass()
{
    EntityBase *ent = NULL; // stock's sceneInfo.entity after the last slot
    for (int32 phase = 0; phase < 2; ++phase) {
        int32 s = phase ? TEMPENTITY_START : 0;
        for (;;) {
            int32 s1 = phase ? ps1TempSlotEnd : ps1SceneSlotEnd; // a callback may add slots
            if (s >= s1)
                break;
            EntityBase **q = PS1PausedScan(objectEntityPtr + s, objectEntityPtr + s1);
            int32 qs       = (int32)(q - objectEntityPtr);
            if (qs != s) {
                ent = q[-1], s = qs;
                continue;
            }
            ent                  = objectEntityPtr[s];
            sceneInfo.entitySlot = s;
            sceneInfo.entity     = ent;
            void (*update)(void) = objectClassList[stageObjectIDs[ent->classID]].update;
            if (update)
                update();
            ent = (EntityBase *)sceneInfo.entity; // stock reads the draw group through it
            if (ent->drawGroup < DRAWGROUP_COUNT)
                PS1_DRAWLIST_APPEND(&drawGroups[ent->drawGroup], sceneInfo.entitySlot);
            ++s;
        }
    }
    if (ent)
        sceneInfo.entity = ent;
    sceneInfo.entitySlot = ENTITY_COUNT;
}

PS1_HOT void PS1PausedLatePass()
{
    bool keep       = PS1KeepOnScreen(); // (9r: see PS1LateUpdatePass)
    EntityBase *ent = NULL;
    for (int32 phase = 0; phase < 2; ++phase) {
        int32 s = phase ? TEMPENTITY_START : 0;
        for (;;) {
            int32 s1 = phase ? ps1TempSlotEnd : ps1SceneSlotEnd;
            if (s >= s1)
                break;
            EntityBase **q = PS1PausedLateScan(objectEntityPtr + s, objectEntityPtr + s1, keep);
            int32 qs       = (int32)(q - objectEntityPtr);
            if (qs != s) {
                ent = q[-1], s = qs;
                continue;
            }
            ent                  = objectEntityPtr[s];
            sceneInfo.entitySlot = s;
            sceneInfo.entity     = ent;
            objectClassList[stageObjectIDs[ent->classID]].lateUpdate();
            ent = (EntityBase *)sceneInfo.entity; // stock clears onScreen through sceneInfo.entity
            if (!keep)
                ent->onScreen = 0;
            ++s;
        }
    }
    if (ent)
        sceneInfo.entity = ent;
    sceneInfo.entitySlot = ENTITY_COUNT;
}

// 4.3: ProcessFrozenObjects' two loops (the pause menu runs in ENGINESTATE_FROZEN: ~200 hblanks a frame as stock loops)
// with the stock semantics. The update loop decides inRange as the regular one (PS1UpdateScan settles the slots out of
// range, PS1RangeDecide the rest) and runs update only for ACTIVE_ALWAYS / ACTIVE_PAUSED entities; every slot in range
// joins its draw group (read through sceneInfo.entity after the callback) and is marked (3.0f bits). The late loop runs
// lateUpdate for in-range ALWAYS / PAUSED entities, adds the in-range interacting ones to the type groups (through
// sceneInfo.entity, in slot order) and clears onScreen through sceneInfo.entity: only the marked slots can have inRange
// or onScreen set, so it visits them; after an entity copy (bits not whole) it visits every slot and marks the in-range
// ones again
PS1_HOT void PS1FrozenUpdatePass()
{
    PS1DormantUpdate(); // 7b
    PS1CameraUnion(true);
    EntityBase **p = objectEntityPtr, **pend = objectEntityPtr + ps1SceneSlotEnd;
    int32 phase     = 0;
    EntityBase *ent = NULL;
    for (;;) {
        if (p >= pend) {
            if (phase)
                break;
            phase = 1, p = objectEntityPtr + TEMPENTITY_START, pend = objectEntityPtr + ps1TempSlotEnd;
            continue;
        }
        EntityBase **q = PS1UpdateScan(p, pend);
        if (q != p) {
            ent = q[-1], p = q;
            continue;
        }
        ent = *p;
        if (PS1RangeDecide(ent)) {
            sceneInfo.entitySlot = (int32)(p - objectEntityPtr);
            sceneInfo.entity     = ent;
            PS1SlotMark(sceneInfo.entitySlot);
            if (ent->active == ACTIVE_ALWAYS || ent->active == ACTIVE_PAUSED) {
                void (*update)(void) = objectClassList[stageObjectIDs[ent->classID]].update;
                if (update) {
                    update();
                    PS1CameraUnion();
                    pend = objectEntityPtr + (phase ? ps1TempSlotEnd : ps1SceneSlotEnd);
                }
            }
            if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT)
                PS1_DRAWLIST_APPEND(&drawGroups[sceneInfo.entity->drawGroup], sceneInfo.entitySlot);
        }
        ++p;
    }
    if (ent)
        sceneInfo.entity = ent;
    sceneInfo.entitySlot = ENTITY_COUNT;
}

PS1_HOT void PS1FrozenLatePass()
{
    bool keep       = PS1KeepOnScreen(); // (9r: see PS1LateUpdatePass)
    uint32 copies   = g_ps1EntityCopies;
    bool whole      = copies == s_ps1BitsFrom;
    bool wholeStart = whole;
    EntityBase *ent = NULL;
    int32 last      = -1;
    for (int32 phase = 0; phase < 2; ++phase) {
        int32 s0 = phase ? TEMPENTITY_START : 0, s = s0;
        for (;;) {
            int32 s1 = phase ? ps1TempSlotEnd : ps1SceneSlotEnd;
            if (whole)
                s = PS1SlotNext(s, s1);
            if (s >= s1)
                break;
            EntityBase *e        = objectEntityPtr[s];
            sceneInfo.entitySlot = s;
            sceneInfo.entity     = e;
            if (e->inRange) {
                if (e->active == ACTIVE_ALWAYS || e->active == ACTIVE_PAUSED) {
                    void (*lateUpdate)(void) = objectClassList[stageObjectIDs[e->classID]].lateUpdate;
                    if (lateUpdate)
                        lateUpdate();
                }
                EntityBase *c = (EntityBase *)sceneInfo.entity;
                if (c->interaction)
                    PS1TypeGroupsAdd((uint16)s, c->classID, c->group);
            }
            ent = (EntityBase *)sceneInfo.entity;
            if (!keep)
                ent->onScreen = 0;
            if (!wholeStart && (objectEntityPtr[s]->inRange || (keep && objectEntityPtr[s]->onScreen)))
                PS1SlotMark(s); // rebuilding the bits
            if (g_ps1EntityCopies != copies)
                whole = false; // a copy: every slot from here on
            last = s;
            ++s;
        }
        // stock's last visited slot is the range's last: settled with nothing to do unless the walk ended on it
        int32 s1 = phase ? ps1TempSlotEnd : ps1SceneSlotEnd;
        if (s1 > s0 && last != s1 - 1)
            ent = objectEntityPtr[s1 - 1], last = s1 - 1;
    }
    if (g_ps1EntityCopies == copies)
        s_ps1BitsFrom = copies;
    if (!wholeStart)
        g_ps1SlotBitsFull = g_ps1SlotBitsFull + 1;
    if (ent)
        sceneInfo.entity = ent;
    sceneInfo.entitySlot = ENTITY_COUNT;
}
} // namespace
#endif

void RSDK::ProcessObjects()
{
    for (int32 i = 0; i < DRAWGROUP_COUNT; ++i) drawGroups[i].entityCount = 0;
#if RETRO_PLATFORM == RETRO_PS1
    static uint8 s_ps1ClassFrames = 0;
    if (!(++s_ps1ClassFrames & 15))
        PS1ClassRangesNewFrame(); // 9.4: foreach_all ranges re-measured at their next use (between: widened only)
#endif

    PS1_PART_BEGIN(0);
    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
#if RETRO_USE_MOD_LOADER
        currentObjectID = o;
#endif

        ObjectClass *classInfo = &objectClassList[stageObjectIDs[o]];
        if ((*classInfo->staticVars)->active == ACTIVE_ALWAYS || (*classInfo->staticVars)->active == ACTIVE_NORMAL) {
            if (classInfo->staticUpdate)
                PS1_PROF(0, stageObjectIDs[o], classInfo->staticUpdate());
        }
    }
    PS1_PART_END(0);

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONSTATICUPDATE, INT_TO_VOID(ENGINESTATE_REGULAR));
#endif

    for (int32 s = 0; s < cameraCount; ++s) {
        CameraInfo *camera = &cameras[s];

        if (camera->targetPos) {
            if (camera->worldRelative) {
                camera->position.x = camera->targetPos->x;
                camera->position.y = camera->targetPos->y;
            }
            else {
                camera->position.x = TO_FIXED(camera->targetPos->x);
                camera->position.y = TO_FIXED(camera->targetPos->y);
            }
        }
    }

    PS1_PART_BEGIN(1);
#if RETRO_PLATFORM == RETRO_PS1
    PS1UpdatePass();
#else
    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = OBJECT_ENTITY(e);
        if (sceneInfo.entity->classID) {
            switch (sceneInfo.entity->active) {
                default:
                case ACTIVE_DISABLED: break;

                case ACTIVE_NEVER:
                case ACTIVE_PAUSED: sceneInfo.entity->inRange = false; break;

                case ACTIVE_ALWAYS:
                case ACTIVE_NORMAL: sceneInfo.entity->inRange = true; break;

                case ACTIVE_BOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = abs(sceneInfo.entity->position.x - cameras[s].position.x);
                        int32 sy = abs(sceneInfo.entity->position.y - cameras[s].position.y);

                        if (sx <= sceneInfo.entity->updateRange.x + cameras[s].offset.x
                            && sy <= sceneInfo.entity->updateRange.y + cameras[s].offset.y) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_XBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = abs(sceneInfo.entity->position.x - cameras[s].position.x);

                        if (sx <= sceneInfo.entity->updateRange.x + cameras[s].offset.x) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_YBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sy = abs(sceneInfo.entity->position.y - cameras[s].position.y);

                        if (sy <= sceneInfo.entity->updateRange.y + cameras[s].offset.y) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_RBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = FROM_FIXED(abs(sceneInfo.entity->position.x - cameras[s].position.x));
                        int32 sy = FROM_FIXED(abs(sceneInfo.entity->position.y - cameras[s].position.y));

                        if (sx * sx + sy * sy <= sceneInfo.entity->updateRange.x + cameras[s].offset.x) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;
            }

            if (sceneInfo.entity->inRange) {
                if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update)
                    objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update();

                if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT)
                    drawGroups[sceneInfo.entity->drawGroup].entries[drawGroups[sceneInfo.entity->drawGroup].entityCount++] = sceneInfo.entitySlot;
            }
        }
        else {
            sceneInfo.entity->inRange = false;
        }

        sceneInfo.entitySlot++;
    }

#endif
    PS1_PART_END(1);
    PS1_PART_BEGIN(2);
#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONUPDATE, INT_TO_VOID(ENGINESTATE_REGULAR));
#endif

#if RETRO_PLATFORM == RETRO_PS1
    PS1TypeGroupsBegin();
#else
    for (int32 i = 0; i < TYPEGROUP_COUNT; ++i) typeGroups[i].entryCount = 0;
#endif

#if RETRO_PLATFORM == RETRO_PS1
    PS1TypeGroupPass();
#else
    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = OBJECT_ENTITY(e);

        if (sceneInfo.entity->inRange && sceneInfo.entity->interaction) {
            typeGroups[GROUP_ALL].entries[typeGroups[GROUP_ALL].entryCount++] = e; // All active objects

            typeGroups[sceneInfo.entity->classID].entries[typeGroups[sceneInfo.entity->classID].entryCount++] = e; // class-based groups

            if (sceneInfo.entity->group >= TYPE_COUNT)
                typeGroups[sceneInfo.entity->group].entries[typeGroups[sceneInfo.entity->group].entryCount++] = e; // extra groups
        }

        sceneInfo.entitySlot++;
    }
#endif
#if RETRO_PLATFORM == RETRO_PS1
    PS1TypeGroupsBuild();
#endif
    PS1_PART_END(2);
    PS1_PART_BEGIN(3);

#if RETRO_PLATFORM == RETRO_PS1
    PS1LateUpdatePass();
#else
    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = OBJECT_ENTITY(e);

        if (sceneInfo.entity->inRange) {
            if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate)
                objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate();
        }

        sceneInfo.entity->onScreen = 0;
        sceneInfo.entitySlot++;
    }
#endif

    PS1_PART_END(3);
#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONLATEUPDATE, INT_TO_VOID(ENGINESTATE_REGULAR));
#endif
}
void RSDK::ProcessPausedObjects()
{
    for (int32 i = 0; i < DRAWGROUP_COUNT; ++i) drawGroups[i].entityCount = 0;

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
#if RETRO_USE_MOD_LOADER
        currentObjectID = o;
#endif

        ObjectClass *classInfo = &objectClassList[stageObjectIDs[o]];
        if ((*classInfo->staticVars)->active == ACTIVE_ALWAYS || (*classInfo->staticVars)->active == ACTIVE_PAUSED) {
            if (classInfo->staticUpdate)
                classInfo->staticUpdate();
        }
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONSTATICUPDATE, INT_TO_VOID(ENGINESTATE_PAUSED));
#endif

#if RETRO_PLATFORM == RETRO_PS1
    PS1PausedUpdatePass(); // 3.0h
#else
    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = OBJECT_ENTITY(e);

        if (sceneInfo.entity->classID) {
            if (sceneInfo.entity->active == ACTIVE_ALWAYS || sceneInfo.entity->active == ACTIVE_PAUSED) {
                if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update)
                    objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update();

                if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT)
                    drawGroups[sceneInfo.entity->drawGroup].entries[drawGroups[sceneInfo.entity->drawGroup].entityCount++] = sceneInfo.entitySlot;
            }
        }
        else {
            sceneInfo.entity->inRange = false;
        }

        sceneInfo.entitySlot++;
    }
#endif

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONUPDATE, INT_TO_VOID(ENGINESTATE_PAUSED));
#endif

#if RETRO_PLATFORM == RETRO_PS1
    PS1PausedLatePass(); // 3.0h
#else
    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = OBJECT_ENTITY(e);

        if (sceneInfo.entity->active == ACTIVE_ALWAYS || sceneInfo.entity->active == ACTIVE_PAUSED) {
            if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate)
                objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate();
        }

        sceneInfo.entity->onScreen = 0;
        sceneInfo.entitySlot++;
    }
#endif

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONLATEUPDATE, INT_TO_VOID(ENGINESTATE_PAUSED));
#endif
}
void RSDK::ProcessFrozenObjects()
{
    for (int32 i = 0; i < DRAWGROUP_COUNT; ++i) drawGroups[i].entityCount = 0;

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
#if RETRO_USE_MOD_LOADER
        currentObjectID = o;
#endif

        ObjectClass *classInfo = &objectClassList[stageObjectIDs[o]];
        if ((*classInfo->staticVars)->active == ACTIVE_ALWAYS || (*classInfo->staticVars)->active == ACTIVE_PAUSED) {
            if (classInfo->staticUpdate)
                classInfo->staticUpdate();
        }
    }

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONSTATICUPDATE, INT_TO_VOID(ENGINESTATE_FROZEN));
#endif

    for (int32 s = 0; s < cameraCount; ++s) {
        CameraInfo *camera = &cameras[s];

        if (camera->targetPos) {
            if (camera->worldRelative) {
                camera->position.x = camera->targetPos->x;
                camera->position.y = camera->targetPos->y;
            }
            else {
                camera->position.x = TO_FIXED(camera->targetPos->x);
                camera->position.y = TO_FIXED(camera->targetPos->y);
            }
        }
    }

#if RETRO_PLATFORM == RETRO_PS1
    PS1FrozenUpdatePass(); // 4.3
#else
    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = OBJECT_ENTITY(e);

        if (sceneInfo.entity->classID) {
            switch (sceneInfo.entity->active) {
                default:
                case ACTIVE_DISABLED: break;

                case ACTIVE_NEVER:
                case ACTIVE_PAUSED: sceneInfo.entity->inRange = false; break;

                case ACTIVE_ALWAYS:
                case ACTIVE_NORMAL: sceneInfo.entity->inRange = true; break;

                case ACTIVE_BOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = abs(sceneInfo.entity->position.x - cameras[s].position.x);
                        int32 sy = abs(sceneInfo.entity->position.y - cameras[s].position.y);

                        if (sx <= sceneInfo.entity->updateRange.x + cameras[s].offset.x
                            && sy <= sceneInfo.entity->updateRange.y + cameras[s].offset.y) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_XBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = abs(sceneInfo.entity->position.x - cameras[s].position.x);

                        if (sx <= sceneInfo.entity->updateRange.x + cameras[s].offset.x) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_YBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sy = abs(sceneInfo.entity->position.y - cameras[s].position.y);

                        if (sy <= sceneInfo.entity->updateRange.y + cameras[s].offset.y) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;

                case ACTIVE_RBOUNDS:
                    sceneInfo.entity->inRange = false;

                    for (int32 s = 0; s < cameraCount; ++s) {
                        int32 sx = FROM_FIXED(abs(sceneInfo.entity->position.x - cameras[s].position.x));
                        int32 sy = FROM_FIXED(abs(sceneInfo.entity->position.y - cameras[s].position.y));

                        if (sx * sx + sy * sy <= sceneInfo.entity->updateRange.x + cameras[s].offset.x) {
                            sceneInfo.entity->inRange = true;
                            break;
                        }
                    }
                    break;
            }

            if (sceneInfo.entity->inRange) {
                if (sceneInfo.entity->active == ACTIVE_ALWAYS || sceneInfo.entity->active == ACTIVE_PAUSED) {
                    if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update)
                        objectClassList[stageObjectIDs[sceneInfo.entity->classID]].update();
                }

                if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT)
#if RETRO_PLATFORM == RETRO_PS1
                    PS1_DRAWLIST_APPEND(&drawGroups[sceneInfo.entity->drawGroup], sceneInfo.entitySlot);
#else
                    drawGroups[sceneInfo.entity->drawGroup].entries[drawGroups[sceneInfo.entity->drawGroup].entityCount++] = sceneInfo.entitySlot;
#endif
            }
        }
        else {
            sceneInfo.entity->inRange = false;
        }

#if RETRO_PLATFORM != RETRO_PS1
        sceneInfo.entitySlot++;
#endif
    }

#endif

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONUPDATE, INT_TO_VOID(ENGINESTATE_FROZEN));
#endif

#if RETRO_PLATFORM == RETRO_PS1
    PS1TypeGroupsBegin();
#else
    for (int32 i = 0; i < TYPEGROUP_COUNT; ++i) typeGroups[i].entryCount = 0;
#endif

#if RETRO_PLATFORM == RETRO_PS1
    PS1FrozenLatePass(); // 4.3
#else
    sceneInfo.entitySlot = 0;
    for (int32 e = 0; e < ENTITY_COUNT; ++e) {
        sceneInfo.entity = OBJECT_ENTITY(e);

        if (sceneInfo.entity->inRange) {
            if (sceneInfo.entity->active == ACTIVE_ALWAYS || sceneInfo.entity->active == ACTIVE_PAUSED) {
                if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate)
                    objectClassList[stageObjectIDs[sceneInfo.entity->classID]].lateUpdate();
            }

            if (sceneInfo.entity->interaction) {
#if RETRO_PLATFORM == RETRO_PS1
                PS1TypeGroupsAdd(e, sceneInfo.entity->classID, sceneInfo.entity->group);
#else
                typeGroups[GROUP_ALL].entries[typeGroups[GROUP_ALL].entryCount++] = e; // All active entities

                typeGroups[sceneInfo.entity->classID].entries[typeGroups[sceneInfo.entity->classID].entryCount++] = e; // type-based groups

                if (sceneInfo.entity->group >= TYPE_COUNT)
                    typeGroups[sceneInfo.entity->group].entries[typeGroups[sceneInfo.entity->group].entryCount++] = e; // extra groups
#endif
            }
        }

        sceneInfo.entity->onScreen = 0;
#if RETRO_PLATFORM != RETRO_PS1
        sceneInfo.entitySlot++;
#endif
    }
#endif
#if RETRO_PLATFORM == RETRO_PS1
    PS1TypeGroupsBuild();
#endif

#if RETRO_USE_MOD_LOADER
    RunModCallbacks(MODCB_ONLATEUPDATE, INT_TO_VOID(ENGINESTATE_FROZEN));
#endif
}
#if RETRO_PLATFORM == RETRO_PS1
// 9w.7 (docs/ports/mania/32): a draw list's entities, as the stock loop (in ProcessObjectDrawLists, kept for other
// platforms) with its locals in registers: -Os kept the loop's state on the stack around each draw call. The list's
// count and entries, sceneInfo.entity and the screen are re-read after each callback as stock does.
__attribute__((noinline, optimize("O2"))) static void PS1DrawListRun(DrawList *list)
{
    for (int32 i = 0; i < list->entityCount; ++i) {
        const uint16 slot    = list->entries[i];
        EntityBase *e        = objectEntityPtr[slot];
        sceneInfo.entitySlot = slot;
        validDraw            = false;
        sceneInfo.entity     = e;
        if (!e->visible)
            continue;
        void (*draw)() = objectClassList[stageObjectIDs[e->classID]].draw;
        if (draw)
            PS1_PROF(1, stageObjectIDs[e->classID], draw());
#if RETRO_VER_EGS || RETRO_USE_DUMMY_ACHIEVEMENTS
        if (i == list->entityCount - 1)
            SKU::DrawAchievements();
#endif
        EntityBase *d = (EntityBase *)sceneInfo.entity;
        d->onScreen |= validDraw << sceneInfo.currentScreenID;
        if (validDraw) { // 3.0f: this entity's onScreen may be set now
            const int32 ds = d == objectEntityPtr[list->entries[i]] ? list->entries[i] : PS1EntitySlotOf(d);
            if (ds >= 0)
                PS1SlotMark(ds);
        }
    }
}
#endif
void RSDK::ProcessObjectDrawLists()
{
    if (sceneInfo.state != ENGINESTATE_LOAD && sceneInfo.state != (ENGINESTATE_LOAD | ENGINESTATE_STEPOVER)) {
#if RETRO_PLATFORM == RETRO_PS1
        PS1LayersFrameBegin(); // the frame's clear and CLUTs, before the first layer or sprite (docs/ports/mania/32 3.3)
#endif
        for (int32 s = 0; s < videoSettings.screenCount; ++s) {
            currentScreen             = &screens[s];
            sceneInfo.currentScreenID = s;

            for (int32 l = 0; l < DRAWGROUP_COUNT; ++l) drawGroups[l].layerCount = 0;

            for (int32 t = 0; t < LAYER_COUNT; ++t) {
                uint8 drawGroup = tileLayers[t].drawGroup[s];

                if (drawGroup < DRAWGROUP_COUNT)
                    drawGroups[drawGroup].layerDrawList[drawGroups[drawGroup].layerCount++] = t;
            }

            sceneInfo.currentDrawGroup = 0;
            for (int32 l = 0; l < DRAWGROUP_COUNT; ++l) {
                if (engine.drawGroupVisible[l]) {
                    DrawList *list = &drawGroups[l];

                    if (list->hookCB)
                        list->hookCB();

#if RETRO_PLATFORM == RETRO_PS1
                    // 4.2e: the stock bubble sort (below) always makes n^2 / 2 comparisons of two entities' zdepth
                    // (the title's 3D sprites: ~45 hblanks a frame). With a swap only on a strictly greater zdepth its
                    // result is the stable descending order: here the same order by a stable insertion sort on the keys
                    // read once (in the scratchpad: free between the layer drawer's calls; longer lists sort as stock)
                    // UFO (ps1-ufo): the overlay's sort, coherent from frame to frame (the same order); false: as below
                    if (list->sorted && g_ps1S3DOps.sort && g_ps1S3DOps.sort(list)) {
                    }
                    else if (list->sorted && list->entityCount <= 170) {
                        int32 n      = list->entityCount;
                        int32 *key   = (int32 *)0x1F800000;
                        uint16 *slot = (uint16 *)(0x1F800000 + 170 * sizeof(int32));
                        for (int32 i = 0; i < n; ++i) {
                            uint16 s = (uint16)list->entries[i];
                            int32 k  = OBJECT_ENTITY(s)->zdepth, j = i;
                            for (; j > 0 && key[j - 1] < k; --j) key[j] = key[j - 1], slot[j] = slot[j - 1];
                            key[j] = k, slot[j] = s;
                        }
                        for (int32 i = 0; i < n; ++i) list->entries[i] = slot[i];
                    }
                    else
#endif
                    if (list->sorted) {
                        for (int32 e = 0; e < list->entityCount; ++e) {
                            for (int32 i = list->entityCount - 1; i > e; --i) {
                                int32 slot1 = list->entries[i - 1];
                                int32 slot2 = list->entries[i];
                                if (OBJECT_ENTITY(slot2)->zdepth > OBJECT_ENTITY(slot1)->zdepth) {
                                    list->entries[i - 1] = slot2;
                                    list->entries[i]     = slot1;
                                }
                            }
                        }
                    }

                    PS1_PART_BEGIN(4);
#if RETRO_PLATFORM == RETRO_PS1
                    PS1DrawListRun(list); // 9w.7
#else
                    for (int32 i = 0; i < list->entityCount; ++i) {
                        sceneInfo.entitySlot = list->entries[i];
                        validDraw            = false;
                        sceneInfo.entity     = OBJECT_ENTITY(list->entries[i]);
                        if (sceneInfo.entity->visible) {
                            if (objectClassList[stageObjectIDs[sceneInfo.entity->classID]].draw)
                                PS1_PROF(1, stageObjectIDs[sceneInfo.entity->classID], objectClassList[stageObjectIDs[sceneInfo.entity->classID]].draw());

#if RETRO_VER_EGS || RETRO_USE_DUMMY_ACHIEVEMENTS
                            if (i == list->entityCount - 1)
                                SKU::DrawAchievements();
#endif

                            sceneInfo.entity->onScreen |= validDraw << sceneInfo.currentScreenID;
#if RETRO_PLATFORM == RETRO_PS1
                            if (validDraw) { // 3.0f: this entity's onScreen may be set now
                                int32 ds = sceneInfo.entity == objectEntityPtr[list->entries[i]] ? list->entries[i]
                                                                                                  : PS1EntitySlotOf(sceneInfo.entity);
                                if (ds >= 0)
                                    PS1SlotMark(ds);
                            }
#endif
                        }
                    }

#endif
                    PS1_PART_END(4);
                    for (int32 i = 0; i < list->layerCount; ++i) {
                        TileLayer *layer = &tileLayers[list->layerDrawList[i]];

#if RETRO_USE_MOD_LOADER
                        RunModCallbacks(MODCB_ONSCANLINECB, (void *)layer->scanlineCallback);
#endif
                        PS1_PART_BEGIN(5);
#if RETRO_PLATFORM == RETRO_PS1
                        layer->ps1Uniform = layer->ps1Direct = 0; // ProcessParallax may set them (3.3c / h); a callback's lines are its own
                        const uint8 cut = ps1LayerCut[list->layerDrawList[i]]; // 9v.4: a background layer cut
#endif
                        if (layer->scanlineCallback)
                            layer->scanlineCallback(scanlines); // a cut layer's callback still runs (clip bounds, palettes)
#if RETRO_PLATFORM == RETRO_PS1
                        else if (!cut)
#else
                        else
#endif
                            ProcessParallax(layer);
                        PS1_PART_END(5);
#if RETRO_PLATFORM == RETRO_PS1
                        if (cut) {
                            PS1DrawLayerCut(cut);
                            continue;
                        }
#endif

                        switch (layer->type) {
                            case LAYER_HSCROLL: DrawLayerHScroll(layer); break;
                            case LAYER_VSCROLL: DrawLayerVScroll(layer); break;
                            case LAYER_ROTOZOOM: DrawLayerRotozoom(layer); break;
                            case LAYER_BASIC: DrawLayerBasic(layer); break;
                            default: break;
                        }
                    }

#if RETRO_USE_MOD_LOADER
                    RunModCallbacks(MODCB_ONDRAW, INT_TO_VOID(l));
#endif

                    if (currentScreen->clipBound_X1 > 0)
                        currentScreen->clipBound_X1 = 0;

                    if (currentScreen->clipBound_Y1 > 0)
                        currentScreen->clipBound_Y1 = 0;

                    if (currentScreen->size.x >= 0) {
                        if (currentScreen->clipBound_X2 < currentScreen->size.x)
                            currentScreen->clipBound_X2 = currentScreen->size.x;
                    }
                    else {
                        currentScreen->clipBound_X2 = 0;
                    }

                    if (currentScreen->size.y >= 0) {
                        if (currentScreen->clipBound_Y2 < currentScreen->size.y)
                            currentScreen->clipBound_Y2 = currentScreen->size.y;
                    }
                    else {
                        currentScreen->clipBound_Y2 = 0;
                    }
#if RETRO_PLATFORM == RETRO_PS1
                    PS1ClipBoundsChanged(currentScreen); // 13c: the GPU's drawing area back to the screen
#endif
                }

                sceneInfo.currentDrawGroup++;
            }

#if !RETRO_USE_ORIGINAL_CODE
            if (engine.showUpdateRanges) {
                for (int32 l = 0; l < DRAWGROUP_COUNT; ++l) {
                    if (engine.drawGroupVisible[l]) {
                        DrawList *list = &drawGroups[l];
                        for (int32 i = 0; i < list->entityCount; ++i) {
                            Entity *entity     = OBJECT_ENTITY(list->entries[i]);

                            if (entity->visible || (engine.showUpdateRanges & 2)) {
                                switch (entity->active) {
                                    default:
                                    case ACTIVE_DISABLED:
                                    case ACTIVE_NEVER: break;

                                    case ACTIVE_ALWAYS:
                                    case ACTIVE_NORMAL: 
                                    case ACTIVE_PAUSED:
                                        DrawRectangle(entity->position.x, entity->position.y, TO_FIXED(1), TO_FIXED(1), 0x0000FF, 0xFF, INK_NONE,
                                                      false);
                                        break;

                                    case ACTIVE_BOUNDS:
                                        DrawLine(entity->position.x - entity->updateRange.x, entity->position.y - entity->updateRange.y,
                                                 entity->position.x + entity->updateRange.x, entity->position.y - entity->updateRange.y, 0x0000FF,
                                                 0xFF, INK_NONE, false);

                                        DrawLine(entity->position.x - entity->updateRange.x, entity->position.y + entity->updateRange.y,
                                                 entity->position.x + entity->updateRange.x, entity->position.y + entity->updateRange.y, 0x0000FF,
                                                 0xFF, INK_NONE, false);

                                        DrawLine(entity->position.x - entity->updateRange.x, entity->position.y - entity->updateRange.y,
                                                 entity->position.x - entity->updateRange.x, entity->position.y + entity->updateRange.y, 0x0000FF,
                                                 0xFF, INK_NONE, false);

                                        DrawLine(entity->position.x + entity->updateRange.x, entity->position.y - entity->updateRange.y,
                                                 entity->position.x + entity->updateRange.x, entity->position.y + entity->updateRange.y, 0x0000FF,
                                                 0xFF, INK_NONE, false);
                                        break;

                                    case ACTIVE_XBOUNDS:
                                        DrawLine(entity->position.x - entity->updateRange.x, TO_FIXED(currentScreen->position.y),
                                                 entity->position.x - entity->updateRange.x,
                                                 TO_FIXED(currentScreen->position.y + currentScreen->size.y), 0x0000FF, 0xFF, INK_NONE, false);

                                        DrawLine(entity->position.x + entity->updateRange.x, TO_FIXED(currentScreen->position.y),
                                                 entity->position.x + entity->updateRange.x,
                                                 TO_FIXED(currentScreen->position.y + currentScreen->size.y), 0x0000FF, 0xFF, INK_NONE, false);
                                        break;

                                    case ACTIVE_YBOUNDS:
                                        DrawLine(TO_FIXED(currentScreen->position.x), entity->position.y - entity->updateRange.y,
                                                 TO_FIXED(currentScreen->position.x + currentScreen->size.x),
                                                 entity->position.y - entity->updateRange.y, 0x0000FF, 0xFF, INK_NONE, false);

                                        DrawLine(TO_FIXED(currentScreen->position.x), entity->position.y + entity->updateRange.y,
                                                 TO_FIXED(currentScreen->position.x + currentScreen->size.x),
                                                 entity->position.y + entity->updateRange.y, 0x0000FF, 0xFF, INK_NONE, false);
                                        break;

                                    case ACTIVE_RBOUNDS:
                                        DrawCircleOutline(entity->position.x, entity->position.y, FROM_FIXED(entity->updateRange.x),
                                                          FROM_FIXED(entity->updateRange.x) + 1, 0x0000FF, 0xFF, INK_NONE, false);
                                        break;
                                }
                            }
                        }
                    }
                }
            }

            if (engine.showEntityInfo) {
                for (int32 l = 0; l < DRAWGROUP_COUNT; ++l) {
                    if (engine.drawGroupVisible[l]) {
                        DrawList *list = &drawGroups[l];
                        for (int32 i = 0; i < list->entityCount; ++i) {
                            Entity *entity = OBJECT_ENTITY(list->entries[i]);

                            if (entity->visible || (engine.showEntityInfo & 2)) {
                                char buffer[0x100];
                                sprintf_s(buffer, sizeof(buffer), "%s\nx: %g\ny: %g", objectClassList[stageObjectIDs[entity->classID]].name,
                                          entity->position.x / 65536.0f, entity->position.y / 65536.0f);

                                DrawDevString(buffer, FROM_FIXED(entity->position.x) - currentScreen->position.x,
                                              FROM_FIXED(entity->position.y) - currentScreen->position.y, ALIGN_LEFT, 0xF0F0F0);
                            }
                        }
                    }
                }
            }

            if (showHitboxes) {
                for (int32 i = 0; i < debugHitboxCount; ++i) {
                    DebugHitboxInfo *info = &debugHitboxList[i];
                    int32 x               = info->pos.x + TO_FIXED(info->hitbox.left);
                    int32 y               = info->pos.y + TO_FIXED(info->hitbox.top);
                    int32 w               = abs((info->pos.x + TO_FIXED(info->hitbox.right)) - x);
                    int32 h               = abs((info->pos.y + TO_FIXED(info->hitbox.bottom)) - y);

                    switch (info->type) {
                        case H_TYPE_TOUCH: DrawRectangle(x, y, w, h, info->collision ? 0x808000 : 0xFF0000, 0x60, INK_ALPHA, false); break;

                        case H_TYPE_CIRCLE:
                            DrawCircle(info->pos.x, info->pos.y, info->hitbox.left, info->collision ? 0x808000 : 0xFF0000, 0x60, INK_ALPHA, false);
                            break;

                        case H_TYPE_BOX:
                            DrawRectangle(x, y, w, h, 0x0000FF, 0x60, INK_ALPHA, false);

                            if (info->collision & 1) // top
                                DrawRectangle(x, y, w, TO_FIXED(1), 0xFFFF00, 0xC0, INK_ALPHA, false);

                            if (info->collision & 8) // bottom
                                DrawRectangle(x, y + h, w, TO_FIXED(1), 0xFFFF00, 0xC0, INK_ALPHA, false);

                            if (info->collision & 2) { // left
                                int32 sy = y;
                                int32 sh = h;

                                if (info->collision & 1) {
                                    sy += TO_FIXED(1);
                                    sh -= TO_FIXED(1);
                                }

                                if (info->collision & 8)
                                    sh -= TO_FIXED(1);

                                DrawRectangle(x, sy, TO_FIXED(1), sh, 0xFFFF00, 0xC0, INK_ALPHA, false);
                            }

                            if (info->collision & 4) { // right
                                int32 sy = y;
                                int32 sh = h;

                                if (info->collision & 1) {
                                    sy += TO_FIXED(1);
                                    sh -= TO_FIXED(1);
                                }

                                if (info->collision & 8)
                                    sh -= TO_FIXED(1);

                                DrawRectangle(x + w, sy, TO_FIXED(1), sh, 0xFFFF00, 0xC0, INK_ALPHA, false);
                            }
                            break;

                        case H_TYPE_PLAT:
                            DrawRectangle(x, y, w, h, 0x00FF00, 0x60, INK_ALPHA, false);

                            if (info->collision & 1) // top
                                DrawRectangle(x, y, w, TO_FIXED(1), 0xFFFF00, 0xC0, INK_ALPHA, false);

                            if (info->collision & 8) // bottom
                                DrawRectangle(x, y + h, w, TO_FIXED(1), 0xFFFF00, 0xC0, INK_ALPHA, false);
                            break;
                    }
                }
            }

            if (engine.showPaletteOverlay) {
                for (int32 p = 0; p < PALETTE_BANK_COUNT; ++p) {
                    int32 x = (videoSettings.pixWidth - (0x10 << 3));
                    int32 y = (SCREEN_YSIZE - (0x10 << 2));

                    for (int32 c = 0; c < PALETTE_BANK_SIZE; ++c) {
                        uint32 clr = GetPaletteEntry(p, c);

                        DrawRectangle(x + ((c & 0xF) << 1) + ((p % (PALETTE_BANK_COUNT / 2)) * (2 * 16)),
                                      y + ((c >> 4) << 1) + ((p / (PALETTE_BANK_COUNT / 2)) * (2 * 16)), 2, 2, clr, 0xFF, INK_NONE, true);
                    }
                }
            }

#endif

            currentScreen++;
            sceneInfo.currentScreenID++;
        }
#if RETRO_PLATFORM == RETRO_PS1
        PS1LayersFrameEnd(); // 9b.3: the grey tint's CLUT state (docs/ports/mania/32)
#endif
    }
}

uint16 RSDK::FindObject(const char *name)
{
    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(name, hash);

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
        if (HASH_MATCH_MD5(hash, objectClassList[stageObjectIDs[o]].hash))
            return o;
    }

    return TYPE_DEFAULTOBJECT;
}

int32 RSDK::GetEntityCount(uint16 classID, bool32 isActive)
{
    if (classID >= TYPE_COUNT)
        return 0;
    if (isActive)
        return typeGroups[classID].entryCount;

    int32 entityCount = 0;
#if RETRO_PLATFORM == RETRO_PS1
    for (int32 i = 0; i < ENTITY_COUNT; i = PS1NextSlot(i)) {
#else
    for (int32 i = 0; i < ENTITY_COUNT; ++i) {
#endif
        if (OBJECT_ENTITY(i)->classID == classID)
            entityCount++;
    }

    return entityCount;
}

void RSDK::ResetEntity(Entity *entity, uint16 classID, void *data)
{
    if (entity) {
        ObjectClass *info = &objectClassList[stageObjectIDs[classID]];
#if RETRO_PLATFORM == RETRO_PS1
        // A slot's block must hold the new class (a spare block if not: PS1Entities.cpp); only the block is cleared.
        uint32 clearSize = info->entityClassSize;
        int32 slot       = PS1EntitySlotOf(entity);
        if (slot >= 0) {
            if (!classID && PS1EntityIsGhost(entity))
                return; // blanking an empty slot: it is blank already
            if (classID && !(entity = PS1EntityEnsure((EntityBase *)entity, slot, info->entityClassSize)))
                return;
            if (clearSize > PS1EntityCapacity(entity))
                clearSize = PS1EntityCapacity(entity);
        }
        memset(entity, 0, clearSize);
#else
        memset(entity, 0, info->entityClassSize);
#endif

        if (info->create) {
            Entity *curEnt = sceneInfo.entity;

            sceneInfo.entity              = entity;
            sceneInfo.entity->interaction = true;
#if RETRO_USE_MOD_LOADER
            int32 superStore          = superLevels[inheritLevel];
            superLevels[inheritLevel] = 0;
#endif
            info->create(data);
#if RETRO_USE_MOD_LOADER
            superLevels[inheritLevel] = superStore;
#endif
            sceneInfo.entity->classID = classID;

            sceneInfo.entity = curEnt;
        }

        entity->classID = classID;
#if RETRO_PLATFORM == RETRO_PS1
        PS1ClassNote(classID, slot); // 9.4
#endif
    }
}

void RSDK::ResetEntitySlot(uint16 slot, uint16 classID, void *data)
{
    ObjectClass *object = &objectClassList[stageObjectIDs[classID]];
    slot                = slot < ENTITY_COUNT ? slot : (ENTITY_COUNT - 1);

#if RETRO_PLATFORM == RETRO_PS1
    Entity *entity = OBJECT_ENTITY(slot);
    if (!classID && PS1EntityIsGhost(entity))
        return; // blanking an empty slot: it is blank already
    if (classID && !(entity = PS1EntityEnsure((EntityBase *)entity, slot, object->entityClassSize)))
        return;
    memset(entity, 0, object->entityClassSize < PS1EntityCapacity(entity) ? object->entityClassSize : PS1EntityCapacity(entity));
#else
    Entity *entity = OBJECT_ENTITY(slot);
    memset(OBJECT_ENTITY(slot), 0, object->entityClassSize);
#endif

    if (object->create) {
        Entity *curEnt = sceneInfo.entity;

        sceneInfo.entity    = entity;
        entity->interaction = true;
#if RETRO_USE_MOD_LOADER
        int32 superStore          = superLevels[inheritLevel];
        superLevels[inheritLevel] = 0;
#endif
        object->create(data);
#if RETRO_USE_MOD_LOADER
        superLevels[inheritLevel] = superStore;
#endif
        entity->classID = classID;

        sceneInfo.entity = curEnt;
    }
    else {
        entity->classID = classID;
    }
#if RETRO_PLATFORM == RETRO_PS1
    PS1ClassNote(classID, slot); // 9.4
#endif
}

Entity *RSDK::CreateEntity(uint16 classID, void *data, int32 x, int32 y)
{
    ObjectClass *object = &objectClassList[stageObjectIDs[classID]];
    Entity *entity      = OBJECT_ENTITY(sceneInfo.createSlot);

    int32 permCnt = 0, loopCnt = 0;
#if RETRO_PLATFORM == RETRO_PS1
    // PS1: a free temp slot must also be big enough for the class (PS1Entities.hpp: small and large temp slots); a blank
    // one for the class PS1CreateBlankFor names (9b.2b)
    uint32 need = classID == TYPE_DEFAULTOBJECT && ps1BlankCreateSize ? ps1BlankCreateSize : object->entityClassSize;
    while (entity->classID || PS1EntityCapacity(entity) < need) {
        if (!entity->classID)
            g_ps1EntityCreateSkips = g_ps1EntityCreateSkips + 1;
#else
    while (entity->classID) {
#endif
        // after 16 loops, the game says fuck it and will start overwriting non-temp objects
        if (!entity->isPermanent && loopCnt >= 16)
            break;

        if (entity->isPermanent)
            ++permCnt;

        sceneInfo.createSlot++;
#if RETRO_PLATFORM == RETRO_PS1
        if (sceneInfo.createSlot == TEMPENTITY_START + ps1TempBlocks) { // PS1Entities.hpp: the temp slots with blocks
#else
        if (sceneInfo.createSlot == ENTITY_COUNT) {
#endif
            sceneInfo.createSlot = TEMPENTITY_START;
            entity               = OBJECT_ENTITY(sceneInfo.createSlot);
        }
        else {
            entity = OBJECT_ENTITY(sceneInfo.createSlot);
        }

        if (permCnt >= TEMPENTITY_COUNT)
            break;

        ++loopCnt;
    }

#if RETRO_PLATFORM == RETRO_PS1
    if (entity->classID) // 7b: no free block in the search: the stock overwrite of a live entity (counted)
        g_ps1EntityCreateOverwrites = g_ps1EntityCreateOverwrites + 1;
    else if (PS1EntityCapacity(entity) < need) {
        // 9b.2b: the search ended on a free block too small (16 slots without a free large one): any free temp block that
        // is big enough (a large one, or one an earlier relocation enlarged) before a spare block: a burst of big temp
        // entities (SSZ1's small Hotarus, 296 B) would otherwise take a new spare block each round until none is left
        for (int32 t = 0; t < ps1TempBlocks; ++t) {
            EntityBase *b = OBJECT_ENTITY(TEMPENTITY_START + t);
            if (!b->classID && PS1EntityCapacity(b) >= need) {
                sceneInfo.createSlot = TEMPENTITY_START + t;
                entity               = (Entity *)b;
                g_ps1EntityCreateFar = g_ps1EntityCreateFar + 1;
                break;
            }
        }
    }
    {
        static EntityBase s_createOverflow; // no block at all (no spare room): counted, never a write through NULL
        Entity *e = PS1EntityEnsure((EntityBase *)entity, sceneInfo.createSlot, need);
        entity    = e ? e : &s_createOverflow;
    }
    memset(entity, 0, need);
#else
    memset(entity, 0, object->entityClassSize);
#endif
    entity->position.x  = x;
    entity->position.y  = y;
    entity->interaction = true;
#if RETRO_PLATFORM == RETRO_PS1
    int32 ps1Slot = sceneInfo.createSlot; // (a create inside the Create moves createSlot)
#endif

    if (object->create) {
        Entity *curEnt = sceneInfo.entity;

        sceneInfo.entity = entity;
#if RETRO_USE_MOD_LOADER
        int32 superStore          = superLevels[inheritLevel];
        superLevels[inheritLevel] = 0;
#endif
        object->create(data);
#if RETRO_USE_MOD_LOADER
        superLevels[inheritLevel] = superStore;
#endif
        entity->classID = classID;

        sceneInfo.entity = curEnt;
    }
    else {
        entity->classID = classID;
        entity->active  = ACTIVE_NORMAL;
        entity->visible = true;
    }
#if RETRO_PLATFORM == RETRO_PS1
    PS1ClassNote(classID, ps1Slot); // 9.4
#endif

    return entity;
}

#if RETRO_PLATFORM == RETRO_PS1
// docs/ports/mania/32 9i: a type group's slots as foreach_active walks them (its entries, in order; foreach_active also skips an entry
// whose class is no longer the group's), for game loops that walk one many times a frame (FBZTrash's pairs)
extern "C" const uint16 *PS1TypeGroupSlots(uint16 group, int32 *count, void ***entities)
{
    *entities = (void **)objectEntityPtr;
    if (group >= TYPEGROUP_COUNT) {
        *count = 0;
        return NULL;
    }
    *count = typeGroups[group].entryCount;
    return typeGroups[group].entries;
}
#endif
bool32 RSDK::GetActiveEntities(uint16 group, Entity **entity)
{
    if (group >= TYPEGROUP_COUNT)
        return false;

    if (!entity)
        return false;

    if (*entity) {
        ++foreachStackPtr->id;
    }
    else {
        foreachStackPtr++;
        foreachStackPtr->id = 0;
    }

#if RETRO_PLATFORM == RETRO_PS1
    // The count first: stock reads entries[id] before checking it, and an empty group's entries are NULL here
    // (PS1TypeGroupsBuild), so that read lands in the BIOS kernel's RAM (PCSX-Redux's kernel checker stops on it).
    for (; foreachStackPtr->id < typeGroups[group].entryCount; ++foreachStackPtr->id) {
        Entity *nextEntity = OBJECT_ENTITY(typeGroups[group].entries[foreachStackPtr->id]);
        if (nextEntity->classID == group) {
            *entity = nextEntity;
            return true;
        }
    }
#else
    for (Entity *nextEntity = OBJECT_ENTITY(typeGroups[group].entries[foreachStackPtr->id]); foreachStackPtr->id < typeGroups[group].entryCount;
         ++foreachStackPtr->id, nextEntity = OBJECT_ENTITY(typeGroups[group].entries[foreachStackPtr->id])) {
        if (nextEntity->classID == group) {
            *entity = nextEntity;
            return true;
        }
    }
#endif

    foreachStackPtr--;

    return false;
}
#if RETRO_PLATFORM == RETRO_PS1
uint16 RSDK::ps1ClassLo[OBJECT_COUNT], RSDK::ps1ClassHi[OBJECT_COUNT];
uint32 RSDK::ps1LiveBits[ENTITY_COUNT / 32]; // 9f (Object.hpp)
uint32 RSDK::ps1SleepBits[(ENTITY_COUNT / 8 + 31) / 32]; // 9f UFO item 2 (Object.hpp, PS1UpdateScan)
static uint8 s_ps1ClassGen[OBJECT_COUNT], s_ps1ClassGenNow = 1; // a range is measured when its gen is not the frame's
extern "C" {
volatile uint32 g_ps1ClassMeasures = 0; // ranges measured (GDB)
#if PS1_CHECK_FOREACH // evidence build: every foreach_all step against the stock walk (must stay 0)
volatile uint32 g_ps1ForeachChecks = 0, g_ps1ForeachWrong = 0, g_ps1ForeachWrongClass = 0, g_ps1ForeachWrongSlot = 0;
#endif
}
void RSDK::PS1ClassRangesNewFrame()
{
    if (++s_ps1ClassGenNow == 0) // (a stale range is still a superset: the generation only decides when to narrow)
        s_ps1ClassGenNow = 1;
}
// The class's first and last slot, as the slots are now
__attribute__((noinline)) static void PS1ClassMeasure(uint16 classID)
{
    int32 lo = ENTITY_COUNT, hi = -1;
    for (int32 r = 0; r < 2; ++r) {
        int32 from = r ? TEMPENTITY_START : 0;
        int32 to   = r ? (ps1TempSlotEnd < ENTITY_COUNT ? ps1TempSlotEnd : ENTITY_COUNT)
                       : (ps1SceneSlotEnd < TEMPENTITY_START ? ps1SceneSlotEnd : TEMPENTITY_START);
        for (int32 s = from; s < to; ++s) {
            if (objectEntityPtr[s]->classID == classID) {
                if (s < lo)
                    lo = s;
                hi = s;
            }
        }
    }
    ps1ClassLo[classID] = (uint16)(lo < ENTITY_COUNT ? lo : 0xFFFF);
    ps1ClassHi[classID] = (uint16)(hi >= 0 ? hi : 0);
    s_ps1ClassGen[classID] = s_ps1ClassGenNow;
    g_ps1ClassMeasures     = g_ps1ClassMeasures + 1;
}
#if PS1_CHECK_FOREACH
// the stock walk's next slot of the class from `id` (PS1NextSlot's ranges), -1: none
static int32 PS1ForeachStock(uint16 classID, int32 id)
{
    for (int32 s = id; s < ENTITY_COUNT; ++s) {
        if (s >= ps1SceneSlotEnd && s < TEMPENTITY_START)
            s = TEMPENTITY_START;
        if (s >= ps1TempSlotEnd || s >= ENTITY_COUNT)
            break;
        if (objectEntityPtr[s]->classID == classID)
            return s;
    }
    return -1;
}
#endif
#endif

bool32 RSDK::GetAllEntities(uint16 classID, Entity **entity)
{
    if (classID >= OBJECT_COUNT)
        return false;

    if (!entity)
        return false;

    if (*entity) {
        ++foreachStackPtr->id;
    }
    else {
        foreachStackPtr++;
        foreachStackPtr->id = 0;
    }

#if RETRO_PLATFORM == RETRO_PS1
    // 3.0: the cursor in a register (the stock loop re-read foreachStackPtr->id every slot: ~65 hblanks a frame for
    // GHZ's waterfall-sound check); the same slots in the same order, the cursor stored where stock leaves it
#if PS1_PROFILE
    if (!*entity)
        g_ps1ProfForeachAll[0]++;
#endif
    // 4.2e: PS1NextSlot's walk as plain pointer loops over its ranges (the menu's foreach_all scans ~19 hblanks a frame)
    int32 id = foreachStackPtr->id;
    // 9.4: the class's range only (classID 0: every slot, as before)
    if (classID && !*entity && s_ps1ClassGen[classID] != s_ps1ClassGenNow)
        PS1ClassMeasure(classID);
    int32 clsLo = classID ? ps1ClassLo[classID] : 0, clsHi = classID ? ps1ClassHi[classID] : ENTITY_COUNT - 1;
#if PS1_CHECK_FOREACH
    int32 stockSlot = PS1ForeachStock(classID, id);
    g_ps1ForeachChecks = g_ps1ForeachChecks + 1;
#endif
    while (id < ENTITY_COUNT) {
        if (id < clsLo)
            id = clsLo;
        if (id > clsHi)
            break;
        int32 stop = ENTITY_COUNT, next = ENTITY_COUNT; // [id, stop), then next (PS1NextSlot(stop - 1))
        if (ps1SceneSlotEnd > id && ps1SceneSlotEnd < TEMPENTITY_START)
            stop = ps1SceneSlotEnd, next = TEMPENTITY_START;
        if (ps1TempSlotEnd > id && ps1TempSlotEnd < stop)
            stop = ps1TempSlotEnd, next = ENTITY_COUNT;
        if (stop > clsHi + 1)
            stop = clsHi + 1, next = ENTITY_COUNT;
        EntityBase **p = &objectEntityPtr[id], **pend = &objectEntityPtr[stop];
        for (; p + 4 <= pend; p += 4) { // four at a time: the loads fill each other's delay slots; the first match wins
            EntityBase *e0 = p[0], *e1 = p[1], *e2 = p[2], *e3 = p[3];
            uint16 c0 = e0->classID, c1 = e1->classID, c2 = e2->classID, c3 = e3->classID;
            if (c0 == classID || c1 == classID || c2 == classID || c3 == classID) {
                p += c0 == classID ? 0 : c1 == classID ? 1 : c2 == classID ? 2 : 3;
                break;
            }
        }
        for (; p < pend; ++p) {
            if ((*p)->classID == classID) {
#if PS1_PROFILE
                g_ps1ProfForeachAll[1] += (uint32)(p - &objectEntityPtr[id]) + 1;
#endif
                foreachStackPtr->id = (int32)(p - objectEntityPtr);
                *entity             = (Entity *)*p;
#if PS1_CHECK_FOREACH
                if (stockSlot != foreachStackPtr->id)
                    g_ps1ForeachWrong = g_ps1ForeachWrong + 1, g_ps1ForeachWrongClass = classID,
                    g_ps1ForeachWrongSlot = (uint32)stockSlot;
#endif
                return true;
            }
        }
#if PS1_PROFILE
        g_ps1ProfForeachAll[1] += (uint32)(stop - id);
#endif
        id = next == ps1TempSlotEnd ? ENTITY_COUNT : next;
    }
#if PS1_CHECK_FOREACH
    if (stockSlot >= 0)
        g_ps1ForeachWrong = g_ps1ForeachWrong + 1, g_ps1ForeachWrongClass = classID, g_ps1ForeachWrongSlot = (uint32)stockSlot;
#endif
#else
    for (; foreachStackPtr->id < ENTITY_COUNT; ++foreachStackPtr->id) {
        Entity *nextEntity = OBJECT_ENTITY(foreachStackPtr->id);
        if (nextEntity->classID == classID) {
            *entity = nextEntity;
            return true;
        }
    }
#endif

    foreachStackPtr--;

    return false;
}

bool32 RSDK::CheckOnScreen(Entity *entity, Vector2 *range)
{
    if (!entity)
        return false;

    if (!range)
        range = &entity->updateRange;

    return CheckPosOnScreen(&entity->position, range);
}
bool32 RSDK::CheckPosOnScreen(Vector2 *position, Vector2 *range)
{
    if (!position || !range)
        return false;

    for (int32 s = 0; s < cameraCount; ++s) {
        int32 sx = abs(position->x - cameras[s].position.x);
        int32 sy = abs(position->y - cameras[s].position.y);

        if (sx <= range->x + cameras[s].offset.x && sy <= range->y + cameras[s].offset.y)
            return true;
    }

    return false;
}

void RSDK::ClearStageObjects()
{
    // Unload static object classes
    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
        if (objectClassList[stageObjectIDs[o]].staticVars) {
            *objectClassList[stageObjectIDs[o]].staticVars = NULL;
        }
    }
}
