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
#include "PS1/PS1Entities.cpp" // per-slot entity storage (docs/32 2.3)
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
// `make PROFILE=1` (docs/32 phase 4 preview): CPU time per class, in root counter 2 ticks (system clock / 8 = 8 cycles),
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
    PS1ScanSleepBegin();
    // docs/32 9i: the second planned STG compaction, before the StageLoads: LoadSceneAssets leaves holes, and a StageLoad's
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
    PS1AtlasBindEnd(); // every StageLoad / Create has bound its frames (docs/32 3.1c)
#endif

    if (!cameraCount)
        AddCamera(&screens[0].position, TO_FIXED(screens[0].center.x), TO_FIXED(screens[0].center.y), false);
}
#if RETRO_PLATFORM == RETRO_PS1
// 3.0 (docs/32): the update pass tests each ACTIVE_BOUNDS entity against every camera (three in GHZ1): ~200 hblanks a
// frame. An entity outside the union of the cameras' boxes, grown by its own range, is out of range for every camera
// (in range for camera s means |x - cx| <= rx + ox, so x lies in [min(cx - ox) - rx, max(cx + ox) + rx], and the same
// for y): it skips the per-camera loop. The prefilter only applies where no sum can overflow (|camera position| < 2^30,
// |camera offset| <= 2^28, entity position in [-2^30, 2^30), range in [-2^28, 2^28)); otherwise, and inside the box,
// the stock loop decides.
namespace
{
bool s_camUnionOk = false;
int32 s_camMinX, s_camMinY;
// 7.4: one camera: ACTIVE_RBOUNDS slots (the UFO stages' rings and spheres: ~400) decided in the scan with stock's test
bool s_camOne = false;
int32 s_camX, s_camY, s_camOX, s_camOY;
// 9q: cameras the union can't take (a position or offset past the union's +-16,384 px: SPZ1, 24,000 px wide, two
// cameras, from x 16,384 on): the scan settles ACTIVE_BOUNDS slots with stock's own test per camera, in its int32
// arithmetic (up to PS1_EXACT_CAMS cameras)
#define PS1_EXACT_CAMS 4
bool s_camExact = false;
int32 s_exN;
uint32 s_exX[PS1_EXACT_CAMS], s_exY[PS1_EXACT_CAMS], s_exOX[PS1_EXACT_CAMS], s_exOY[PS1_EXACT_CAMS];
uint32 s_camW, s_camH; // max - min of the union, modulo 2^32
inline bool PS1Small(int32 v, int32 lim) { return (uint32)(v + lim) <= (uint32)(2 * lim); }
void PS1CameraUnion()
{
    // 9e.2: recomputed after every update callback (one may move the camera); one camera that did not change keeps the
    // union (D.A. Garden: ~90 callbacks a frame, ~12 hblanks)
    if (s_camOne && s_camUnionOk && cameraCount == 1 && cameras[0].position.x == s_camX && cameras[0].position.y == s_camY
        && cameras[0].offset.x == s_camOX && cameras[0].offset.y == s_camOY)
        return;
    s_camUnionOk = cameraCount > 0;
    s_camExact   = false;
    int32 minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (int32 s = 0; s < cameraCount; ++s) {
        CameraInfo *c = &cameras[s];
        if (!PS1Small(c->position.x, 0x3FFFFFFF) || !PS1Small(c->position.y, 0x3FFFFFFF) || !PS1Small(c->offset.x, 0x10000000)
            || !PS1Small(c->offset.y, 0x10000000)) {
            s_camUnionOk = false;
            if (cameraCount <= PS1_EXACT_CAMS) { // 9q
                s_camExact = true;
                s_exN      = cameraCount;
                for (int32 k = 0; k < cameraCount; ++k) {
                    s_exX[k] = (uint32)cameras[k].position.x, s_exY[k] = (uint32)cameras[k].position.y;
                    s_exOX[k] = (uint32)cameras[k].offset.x, s_exOY[k] = (uint32)cameras[k].offset.y;
                }
            }
            return;
        }
        int32 x0 = c->position.x - c->offset.x, x1 = c->position.x + c->offset.x;
        int32 y0 = c->position.y - c->offset.y, y1 = c->position.y + c->offset.y;
        if (!s || x0 < minX)
            minX = x0;
        if (!s || x1 > maxX)
            maxX = x1;
        if (!s || y0 < minY)
            minY = y0;
        if (!s || y1 > maxY)
            maxY = y1;
    }
    s_camMinX = minX, s_camMinY = minY;
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
#if PS1_CHECK_UPDATESCAN
extern "C" {
volatile uint32 g_ps1ScanSettled = 0, g_ps1ScanWrong = 0; // bounds slots settled out of range; of them, in range for stock
}
#endif
#if PS1_CHECK_LIVEBITS
extern "C" {
volatile uint32 g_ps1LiveChecks = 0, g_ps1LiveWrong = 0; // evidence build: classed slots whose bit was clear (must stay 0)
}
#endif
// 9f: 32 slots with their bits clear (blank, settled: inRange already false) are passed over; a blank slot found has
// its bit cleared after its stock write (the UFO stages' scene ranges: 1,427 of 2,112 slots blank)
#define PS1_LIVE_SKIP()                                                                                                \
    if (!(s & 31) && !ps1LiveBits[s >> 5]) {                                                                           \
        if (pend - p <= 32)                                                                                            \
            return pend;                                                                                               \
        p += 31, s += 31;                                                                                              \
        continue;                                                                                                      \
    }
// docs/32 9f (UFO item 2): sleeping groups. In the UFO stages ~540 of ~690 live slots (rings, spheres, item boxes:
// ACTIVE_RBOUNDS; decorations: ACTIVE_BOUNDS) are tested and found out of range every frame. A group of 8 slots whose
// live slots are all of those two kinds and were all settled out of range in one stretch of the scan goes to sleep with
// a margin: how far (whole pixels, on each axis) the one camera may move with every slot still out of range in stock's
// own arithmetic (radius slots: the squared distance's floor root minus the range's root rounded up, times 181/256 <
// 1/sqrt(2); box slots: the failing axis's slack). While the camera stays within the margin of where it was, the scan
// passes over the group (its slots keep inRange false: nothing updates them). Woken by any class write into the group
// (PS1ClassNote: create, reset, copy), a camera offset change, and every frame the scan takes the stock path. Opt-in per
// scene: a StageLoad sets g_ps1ScanSleepWant (UFO_Setup; its stages' objects were reviewed: nothing writes an out-of-range
// slot's position, range or active). PS1_CHECK_SLEEP: stock's test on every slot passed over (g_ps1SleepWrong).
struct PS1SleepGroup {
    int16 x, y;    // the camera (whole pixels) when the group went to sleep
    uint16 margin; // pixels
    uint16 pad;
};
PS1SleepGroup *s_sleep = NULL; // STG, (ENTITY_COUNT / 8) groups; NULL: off
int32 s_sleepOX, s_sleepOY;    // the camera offset the sleeping groups were measured with
uint32 s_grp = ~0u;            // the group the scan is in, and whether all its slots so far were settled out of range
bool s_grpOk = false;
uint32 s_grpLive = 0;
inline uint32 PS1Isqrt(uint32 v) // floor(sqrt(v))
{
    uint32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b)
            v -= r + b, r = (r >> 1) + b;
        else
            r >>= 1;
        b >>= 2;
    }
    return r;
}
// pixels the camera may move (each axis) with this settled slot still out of range for stock; 0: none
uint32 PS1SleepMargin(const EntityBase *e)
{
    const int32 x = e->position.x, y = e->position.y;
    if (!PS1Small(x, 0x3FFFFFFF) || !PS1Small(y, 0x3FFFFFFF))
        return 0;
    if (e->active == ACTIVE_RBOUNDS) {
        const int32 dx = x - s_camX, dy = y - s_camY;
        const uint32 sx = (uint32)(dx < 0 ? -dx : dx) >> 16, sy = (uint32)(dy < 0 ? -dy : dy) >> 16;
        const int32 R = (int32)((uint32)e->updateRange.x + (uint32)s_camOX);
        if (sx > 16383 || sy > 16383 || R < 0 || R >= 0x40000000)
            return 0;
        const uint32 d2 = sx * sx + sy * sy;
        if (d2 <= (uint32)R)
            return 0;
        const uint32 sr = PS1Isqrt(d2), rr = PS1Isqrt((uint32)R) + 1;
        return sr > rr ? ((sr - rr) * 181) >> 8 : 0;
    }
    if (e->active == ACTIVE_BOUNDS) {
        const int32 rx = e->updateRange.x, ry = e->updateRange.y;
        if (!PS1Small(rx, 0x10000000) || !PS1Small(ry, 0x10000000))
            return 0;
        const int32 ax = abs(x - s_camX) - (rx + s_camOX), ay = abs(y - s_camY) - (ry + s_camOY);
        const int32 slack = ax > ay ? ax : ay;
        return slack > 0 ? (uint32)(slack - 1) >> 16 : 0;
    }
    return 0;
}
__attribute__((noinline)) void PS1SleepGroupSet(uint32 g)
{
    uint32 m = 0xFFFF;
    for (uint32 k = 0; k < 8 && m; ++k) {
        const EntityBase *e = objectEntityPtr[g * 8 + k];
        if (e->classID) {
            const uint32 em = PS1SleepMargin(e);
            m = em < m ? em : m;
        }
    }
    if (!m)
        return;
    s_sleep[g].x = (int16)(s_camX >> 16), s_sleep[g].y = (int16)(s_camY >> 16), s_sleep[g].margin = (uint16)m;
    ps1SleepBits[g >> 5] |= 1u << (g & 31);
}
inline bool PS1SleepValid(uint32 g)
{
    const int32 mx = (s_camX >> 16) - s_sleep[g].x, my = (s_camY >> 16) - s_sleep[g].y;
    return (uint32)((mx < 0 ? -mx : mx) + 1) <= s_sleep[g].margin && (uint32)((my < 0 ? -my : my) + 1) <= s_sleep[g].margin;
}
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
    s_grp              = ~0u;
    for (uint32 i = 0; i < sizeof(ps1SleepBits) / 4; ++i) ps1SleepBits[i] = 0;
}
void PS1ScanSleepEnd()
{
    if (g_ps1ScanSleepWant)
        AllocateStorage((void **)&s_sleep, sizeof(PS1SleepGroup) * (ENTITY_COUNT / 8), DATASET_STG, true);
    s_sleepOX = s_camOX, s_sleepOY = s_camOY;
}
#if PS1_CHECK_SLEEP
} // namespace
extern "C" {
volatile uint32 g_ps1SleepSkipped = 0, g_ps1SleepWrong = 0; // slots passed over asleep; of them, not settled for stock
}
namespace
{
void PS1SleepCheck(uint32 g)
{
    for (uint32 k = 0; k < 8; ++k) {
        const EntityBase *e = objectEntityPtr[g * 8 + k];
        g_ps1SleepSkipped = g_ps1SleepSkipped + 1;
        if (!e->classID)
            continue;
        bool in = true;
        if (e->active == ACTIVE_RBOUNDS) {
            int32 dx = e->position.x - s_camX, dy = e->position.y - s_camY;
            int32 sx = FROM_FIXED(dx < 0 ? -dx : dx), sy = FROM_FIXED(dy < 0 ? -dy : dy);
            in = (int32)((uint32)sx * (uint32)sx + (uint32)sy * (uint32)sy) <= (int32)((uint32)e->updateRange.x + (uint32)s_camOX);
        }
        else if (e->active == ACTIVE_BOUNDS)
            in = abs(e->position.x - s_camX) <= e->updateRange.x + s_camOX && abs(e->position.y - s_camY) <= e->updateRange.y + s_camOY;
        g_ps1SleepWrong = g_ps1SleepWrong + (in || e->inRange);
    }
}
#endif
#define PS1_SCAN_RET()                                                                                                 \
    do {                                                                                                               \
        s_grpOk = false;                                                                                               \
        return p;                                                                                                      \
    } while (0)
PS1_HOT EntityBase **PS1UpdateScan(EntityBase **p, EntityBase **pend)
{
    uint32 s = (uint32)(p - objectEntityPtr);
    if (!s_camUnionOk || !s_camOne) // (stock decides: every sleeping group wakes, entities may move meanwhile)
        for (uint32 w = 0; w < sizeof(ps1SleepBits) / 4; ++w) ps1SleepBits[w] = 0;
    if (!s_camUnionOk && s_camExact) { // 9q: cameras past the union's range: stock's ACTIVE_BOUNDS test here
        const int32 n = s_exN;
        for (; p < pend; ++p, ++s) {
            PS1_LIVE_SKIP();
            EntityBase *ent = *p;
            uint32 cls      = ent->classID;
            uint32 active   = ent->active;
            if (cls) {
                if (active == ACTIVE_BOUNDS) {
                    // per camera: abs(position - camera) <= updateRange + offset on both axes, each in int32 as stock
                    // (wrapping; abs of INT_MIN stays negative, as libc's)
                    const uint32 x = (uint32)ent->position.x, rx = (uint32)ent->updateRange.x;
                    for (int32 c = 0; c < n; ++c) {
                        int32 dx = (int32)(x - s_exX[c]);
                        dx       = dx < 0 ? (int32)(0u - (uint32)dx) : dx;
                        if (dx <= (int32)(rx + s_exOX[c])) {
                            int32 dy = (int32)((uint32)ent->position.y - s_exY[c]);
                            dy       = dy < 0 ? (int32)(0u - (uint32)dy) : dy;
                            if (dy <= (int32)((uint32)ent->updateRange.y + s_exOY[c]))
                                return p;
                        }
                    }
#if PS1_CHECK_UPDATESCAN
                    g_ps1ScanSettled = g_ps1ScanSettled + 1;
                    for (int32 c = 0; c < cameraCount; ++c)
                        if (abs(ent->position.x - cameras[c].position.x) <= ent->updateRange.x + cameras[c].offset.x
                            && abs(ent->position.y - cameras[c].position.y) <= ent->updateRange.y + cameras[c].offset.y) {
                            g_ps1ScanWrong = g_ps1ScanWrong + 1;
                            break;
                        }
#endif
                }
                else if (active != ACTIVE_NEVER && active != ACTIVE_PAUSED)
                    return p;
            }
            ent->inRange = false;
            if (!cls)
                ps1LiveBits[s >> 5] &= ~(1u << (s & 31));
        }
        return p;
    }
    if (!s_camUnionOk) { // the stock loop decides every live slot
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
    const uint32 minX = (uint32)s_camMinX, minY = (uint32)s_camMinY, w = s_camW, h = s_camH;
    const bool sleepOn = s_sleep && s_camOne;
    if (sleepOn && (s_camOX != s_sleepOX || s_camOY != s_sleepOY)) {
        for (uint32 i = 0; i < sizeof(ps1SleepBits) / 4; ++i) ps1SleepBits[i] = 0;
        s_sleepOX = s_camOX, s_sleepOY = s_camOY;
    }
    const uint32 send = (uint32)(pend - objectEntityPtr);
    for (; p < pend; ++p, ++s) {
        PS1_LIVE_SKIP();
        if (sleepOn && !(s & 7)) {
            const uint32 g = s >> 3;
            if ((ps1SleepBits[g >> 5] >> (g & 31)) & 1) {
                if (s + 8 <= send && PS1SleepValid(g)) {
#if PS1_CHECK_SLEEP
                    PS1SleepCheck(g);
#endif
                    p += 7, s += 7;
                    continue;
                }
                ps1SleepBits[g >> 5] &= ~(1u << (g & 31));
            }
            s_grp = g, s_grpOk = true, s_grpLive = 0;
        }
        EntityBase *ent = *p;
        uint32 cls    = ent->classID;
        uint32 active = ent->active;
        uint32 x = (uint32)ent->position.x, rx = (uint32)ent->updateRange.x;
        __asm__("" : "+r"(cls), "+r"(active), "+r"(x), "+r"(rx)); // all loaded here (GCC won't hoist loads over a branch)
        if (cls) {
            if (active == ACTIVE_BOUNDS) {
                // x in [minX - rx, maxX + rx] <=> (uint32)(x - (minX - rx)) <= (uint32)(w + 2 * rx) when the interval is
                // not empty; an empty one (negative offsets or ranges) answers "inside", which leaves it to the stock loop.
                // 3.0g: outside on x (x and rx valid) is out of range for every camera whatever y is (stock needs both
                // axes inside for one camera), so y and its range are loaded only for the slots inside on x
                if ((int32)(x + 0x40000000u) < 0 || (rx + 0x10000000u) >= 0x20000000u)
                    PS1_SCAN_RET();
                if (x - minX + rx <= w + 2 * rx) {
                    uint32 y = (uint32)ent->position.y, ry = (uint32)ent->updateRange.y;
                    if ((int32)(y + 0x40000000u) < 0 || (ry + 0x10000000u) >= 0x20000000u || y - minY + ry <= h + 2 * ry)
                        PS1_SCAN_RET();
                }
            }
            else if (active == ACTIVE_RBOUNDS && s_camOne) {
                // stock's test for one camera, in its int32 arithmetic (wrapping as the PC's): out of range settles here
                int32 dx = x - s_camX, dy = ent->position.y - s_camY;
                int32 sx = FROM_FIXED(dx < 0 ? -dx : dx), sy = FROM_FIXED(dy < 0 ? -dy : dy);
                if ((int32)((uint32)sx * (uint32)sx + (uint32)sy * (uint32)sy) <= (int32)((uint32)rx + (uint32)s_camOX))
                    PS1_SCAN_RET();
            }
            else if (active != ACTIVE_NEVER && active != ACTIVE_PAUSED)
                PS1_SCAN_RET();
        }
#if PS1_CHECK_UPDATESCAN // evidence build: stock's test on every slot settled here (GDB reads the counters)
        if (cls && active == ACTIVE_BOUNDS) {
            g_ps1ScanSettled = g_ps1ScanSettled + 1;
            for (int32 s = 0; s < cameraCount; ++s)
                if (abs(ent->position.x - cameras[s].position.x) <= ent->updateRange.x + cameras[s].offset.x
                    && abs(ent->position.y - cameras[s].position.y) <= ent->updateRange.y + cameras[s].offset.y) {
                    g_ps1ScanWrong = g_ps1ScanWrong + 1;
                    break;
                }
        }
#endif
        if (sleepOn && s_grp == (s >> 3)) {
            if (cls) {
                ++s_grpLive;
                s_grpOk = s_grpOk && (active == ACTIVE_BOUNDS || active == ACTIVE_RBOUNDS);
            }
            if ((s & 7) == 7 && s_grpOk && s_grpLive)
                PS1SleepGroupSet(s >> 3);
        }
        ent->inRange = false;
        if (!cls)
            ps1LiveBits[s >> 5] &= ~(1u << (s & 31));
    }
    return p;
}

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
                int32 sx = abs(ent->position.x - cameras[s].position.x);
                int32 sy = abs(ent->position.y - cameras[s].position.y);
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
                int32 sx = abs(ent->position.x - cameras[s].position.x);
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
                int32 sy = abs(ent->position.y - cameras[s].position.y);
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
                int32 sx = FROM_FIXED(abs(ent->position.x - cameras[s].position.x));
                int32 sy = FROM_FIXED(abs(ent->position.y - cameras[s].position.y));
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

PS1_HOT void PS1UpdatePass()
{
    PS1DormantUpdate(); // 7b: dormant entities the cameras reach become entities first (PS1Entities.cpp)
    PS1CameraUnion();
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

        ent = *p; // class != 0 (the scan settles blank slots)
        bool32 inRange = PS1RangeDecide(ent);

        if (inRange) {
            sceneInfo.entitySlot = (int32)(p - objectEntityPtr);
            sceneInfo.entity     = ent;
            PS1SlotMark(sceneInfo.entitySlot);
            void (*update)(void) = objectClassList[stageObjectIDs[ent->classID]].update;
            if (update) {
                PS1_PROF(0, stageObjectIDs[ent->classID], update());
                PS1CameraUnion();
                pend = objectEntityPtr + (phase ? ps1TempSlotEnd : ps1SceneSlotEnd);
            }
            // stock reads these after the callback
            if (sceneInfo.entity->drawGroup < DRAWGROUP_COUNT)
                PS1_DRAWLIST_APPEND(&drawGroups[sceneInfo.entity->drawGroup], sceneInfo.entitySlot);
        }
        ++p;
    }
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
            for (int32 s = PS1SlotNext(s0, s1); s < s1; s = PS1SlotNext(s + 1, s1)) {
                EntityBase *e = objectEntityPtr[s];
                if (e->inRange && e->interaction)
                    PS1TypeGroupsAdd((uint16)s, e->classID, e->group);
            }
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

// Late update: the scan clears onScreen on each slot whose lateUpdate does not run
PS1_HOT EntityBase **PS1LateScan(EntityBase **p, EntityBase **pend)
{
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
PS1_HOT void PS1LateUpdatePass()
{
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
                EntityBase **q = PS1LateScan(objectEntityPtr + s, objectEntityPtr + s1);
                int32 qs       = (int32)(q - objectEntityPtr);
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
                ent           = (EntityBase *)sceneInfo.entity; // stock clears onScreen through sceneInfo.entity
                ent->onScreen = 0;
                if (ent == objectEntityPtr[s])
                    PS1SlotUnmark(s);
                else
                    PS1SlotMark(s); // its own onScreen stays: cleared by a later pass
                if (g_ps1EntityCopies != copies)
                    whole = false; // a copy: every slot from here on
            }
            else {
                e->onScreen = 0;
                PS1SlotUnmark(s);
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
    if (g_ps1EntityCopies == copies)
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

PS1_HOT EntityBase **PS1PausedLateScan(EntityBase **p, EntityBase **pend)
{
    for (; p < pend; ++p) {
        EntityBase *ent = *p;
        uint32 act = ent->active;
        if ((act == ACTIVE_ALWAYS || act == ACTIVE_PAUSED) && objectClassList[stageObjectIDs[ent->classID]].lateUpdate)
            return p;
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
    EntityBase *ent = NULL;
    for (int32 phase = 0; phase < 2; ++phase) {
        int32 s = phase ? TEMPENTITY_START : 0;
        for (;;) {
            int32 s1 = phase ? ps1TempSlotEnd : ps1SceneSlotEnd;
            if (s >= s1)
                break;
            EntityBase **q = PS1PausedLateScan(objectEntityPtr + s, objectEntityPtr + s1);
            int32 qs       = (int32)(q - objectEntityPtr);
            if (qs != s) {
                ent = q[-1], s = qs;
                continue;
            }
            ent                  = objectEntityPtr[s];
            sceneInfo.entitySlot = s;
            sceneInfo.entity     = ent;
            objectClassList[stageObjectIDs[ent->classID]].lateUpdate();
            ent           = (EntityBase *)sceneInfo.entity; // stock clears onScreen through sceneInfo.entity
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
    PS1CameraUnion();
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
            ent           = (EntityBase *)sceneInfo.entity;
            ent->onScreen = 0;
            if (!wholeStart && objectEntityPtr[s]->inRange)
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
void RSDK::ProcessObjectDrawLists()
{
    if (sceneInfo.state != ENGINESTATE_LOAD && sceneInfo.state != (ENGINESTATE_LOAD | ENGINESTATE_STEPOVER)) {
#if RETRO_PLATFORM == RETRO_PS1
        PS1LayersFrameBegin(); // the frame's clear and CLUTs, before the first layer or sprite (docs/32 3.3)
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
                    if (list->sorted && list->entityCount <= 170) {
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

                    PS1_PART_END(4);
                    for (int32 i = 0; i < list->layerCount; ++i) {
                        TileLayer *layer = &tileLayers[list->layerDrawList[i]];

#if RETRO_USE_MOD_LOADER
                        RunModCallbacks(MODCB_ONSCANLINECB, (void *)layer->scanlineCallback);
#endif
                        PS1_PART_BEGIN(5);
#if RETRO_PLATFORM == RETRO_PS1
                        layer->ps1Uniform = layer->ps1Direct = 0; // ProcessParallax may set them (3.3c / h); a callback's lines are its own
#endif
                        if (layer->scanlineCallback)
                            layer->scanlineCallback(scanlines);
                        else
                            ProcessParallax(layer);
                        PS1_PART_END(5);

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
        PS1LayersFrameEnd(); // 9b.3: the grey tint's CLUT state (docs/32)
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
// docs/32 9i: a type group's slots as foreach_active walks them (its entries, in order; foreach_active also skips an entry
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
