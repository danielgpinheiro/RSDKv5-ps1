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

void RSDK::InitObjects()
{
    sceneInfo.entitySlot = 0;
#if RETRO_PLATFORM == RETRO_PS1
    sceneInfo.createSlot = TEMPENTITY_START; // = ENTITY_COUNT - 0x100 with the stock counts; PS1 has fewer temp slots
#else
    sceneInfo.createSlot = ENTITY_COUNT - 0x100;
#endif
    cameraCount          = 0;

    for (int32 o = 0; o < sceneInfo.classCount; ++o) {
#if RETRO_USE_MOD_LOADER
        currentObjectID = o;
#endif

        if (objectClassList[stageObjectIDs[o]].stageLoad)
            objectClassList[stageObjectIDs[o]].stageLoad();
    }

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
int32 s_camX, s_camY, s_camOX;
uint32 s_camW, s_camH; // max - min of the union, modulo 2^32
inline bool PS1Small(int32 v, int32 lim) { return (uint32)(v + lim) <= (uint32)(2 * lim); }
void PS1CameraUnion()
{
    s_camUnionOk = cameraCount > 0;
    int32 minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (int32 s = 0; s < cameraCount; ++s) {
        CameraInfo *c = &cameras[s];
        if (!PS1Small(c->position.x, 0x3FFFFFFF) || !PS1Small(c->position.y, 0x3FFFFFFF) || !PS1Small(c->offset.x, 0x10000000)
            || !PS1Small(c->offset.y, 0x10000000)) {
            s_camUnionOk = false;
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
        s_camX = cameras[0].position.x, s_camY = cameras[0].position.y, s_camOX = cameras[0].offset.x;
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
PS1_HOT EntityBase **PS1UpdateScan(EntityBase **p, EntityBase **pend)
{
    if (!s_camUnionOk) { // the stock loop decides every live slot
        for (; p < pend; ++p) {
            EntityBase *ent = *p;
            if (ent->classID && ent->active != ACTIVE_NEVER && ent->active != ACTIVE_PAUSED)
                return p;
            ent->inRange = false;
        }
        return p;
    }
    const uint32 minX = (uint32)s_camMinX, minY = (uint32)s_camMinY, w = s_camW, h = s_camH;
    for (; p < pend; ++p) {
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
                    return p;
                if (x - minX + rx <= w + 2 * rx) {
                    uint32 y = (uint32)ent->position.y, ry = (uint32)ent->updateRange.y;
                    if ((int32)(y + 0x40000000u) < 0 || (ry + 0x10000000u) >= 0x20000000u || y - minY + ry <= h + 2 * ry)
                        return p;
                }
            }
            else if (active == ACTIVE_RBOUNDS && s_camOne) {
                // stock's test for one camera, in its int32 arithmetic (wrapping as the PC's): out of range settles here
                int32 dx = x - s_camX, dy = ent->position.y - s_camY;
                int32 sx = FROM_FIXED(dx < 0 ? -dx : dx), sy = FROM_FIXED(dy < 0 ? -dy : dy);
                if ((int32)((uint32)sx * (uint32)sx + (uint32)sy * (uint32)sy) <= (int32)((uint32)rx + (uint32)s_camOX))
                    return p;
            }
            else if (active != ACTIVE_NEVER && active != ACTIVE_PAUSED)
                return p;
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
        ent->inRange = false;
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
}

Entity *RSDK::CreateEntity(uint16 classID, void *data, int32 x, int32 y)
{
    ObjectClass *object = &objectClassList[stageObjectIDs[classID]];
    Entity *entity      = OBJECT_ENTITY(sceneInfo.createSlot);

    int32 permCnt = 0, loopCnt = 0;
#if RETRO_PLATFORM == RETRO_PS1
    // PS1: a free temp slot must also be big enough for the class (PS1Entities.hpp: small and large temp slots).
    while (entity->classID || PS1EntityCapacity(entity) < object->entityClassSize) {
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
    {
        static EntityBase s_createOverflow; // no block at all (no spare room): counted, never a write through NULL
        Entity *e = PS1EntityEnsure((EntityBase *)entity, sceneInfo.createSlot, object->entityClassSize);
        entity    = e ? e : &s_createOverflow;
    }
#endif
    memset(entity, 0, object->entityClassSize);
    entity->position.x  = x;
    entity->position.y  = y;
    entity->interaction = true;

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

    return entity;
}

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
    while (id < ENTITY_COUNT) {
        int32 stop = ENTITY_COUNT, next = ENTITY_COUNT; // [id, stop), then next (PS1NextSlot(stop - 1))
        if (ps1SceneSlotEnd > id && ps1SceneSlotEnd < TEMPENTITY_START)
            stop = ps1SceneSlotEnd, next = TEMPENTITY_START;
        if (ps1TempSlotEnd > id && ps1TempSlotEnd < stop)
            stop = ps1TempSlotEnd, next = ENTITY_COUNT;
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
                return true;
            }
        }
#if PS1_PROFILE
        g_ps1ProfForeachAll[1] += (uint32)(stop - id);
#endif
        id = next == ps1TempSlotEnd ? ENTITY_COUNT : next;
    }
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
