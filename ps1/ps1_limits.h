/*
 * PS1 port (docs/ports/mania/32): the limits the engine (RSDKv5/RSDK) and the game (SonicMania/GameLink.h) must agree on.
 * Both include this file under RETRO_PS1_BUILD, so a slot count can never differ between the two (the game loops
 * over SCENEENTITY_COUNT slots, indexes TEMPENTITY_START, sizes globals->atlEntityData by it).
 *
 * Phase 2.3: the stock counts. The memory is no longer ENTITY_COUNT x sizeof(EntityBase): every slot points to a
 * block sized for what it holds (RSDKv5/RSDK/Scene/PS1/PS1Entities.hpp), so the game's slot constants stay the PC's.
 */
#ifndef PS1_LIMITS_H
#define PS1_LIMITS_H

#define RESERVE_ENTITY_COUNT (0x40)  /* SLOT_PLAYER1.. and the other fixed slots */
#define SCENEENTITY_COUNT    (0x800) /* the largest Mania-mode scene keeps 2017 entities (PSZ2/2, docs/ports/mania/32 phase 1) */
#define TEMPENTITY_COUNT     (0x100)

/* docs/ports/mania/32 7.4, the Dreamcast port's values: scanlines[0].deform = these, then the camera in scanlines[1..2]: a layer the
   engine draws as 3D tiles (the UFO stages' playfield: RSDK/Graphics/PS1/PS1UfoFloor.cpp) */
#define SCANLINE_MAJOR_MAGIC_3DTILES (0xFEDCBA09u)
#define SCANLINE_MINOR_MAGIC_UFO     (0x90ABCDEFu)
#define SCANLINE_MINOR_MAGIC_UFO_FLOOR (0x2345678Bu) /* 7.5: a "3D Floor" layer (UFO2, UFO6) */
#define SCANLINE_MINOR_MAGIC_ROOF      (0x56789ABCu) /* 7.5: a "3D Roof" layer (UFO4) */

/* docs/ports/mania/32 7b (choice 113): dormant entities. A class listed in the game's ps1DormantSpecs (resident code) may keep no
   entity block while it is out of every camera's reach and still as its Create left it: the engine keeps its scene
   record, creates it when the cameras come near and returns it to a record when they leave
   (RSDK/Scene/PS1/PS1Entities.cpp). */
/* docs/ports/mania/32 7b: SaveGame's checkpoint recall and dormant entities (RSDK/Scene/PS1/PS1Entities.cpp) */
#ifdef __cplusplus
extern "C" {
#endif
void PS1DormantStoreDisabled(int *table, int disabled);
int PS1EntitySlotIsGhost(int slot);
void PS1DormantRecallDisabled(int slot);
#ifdef __cplusplus
}
#endif

typedef struct {
    const char *name;                /* the class (its name: no reference from the resident to an overlay's class) */
    int (*eligible)(const void *e);  /* the entity as the scene file sets it, before Create: may it go dormant */
    int range;                       /* its updateRange once created (both axes, 16.16; the engine's range test) */
    int (*rangeOf)(const void *e);   /* or this, from the entity as the scene file sets it (NULL: range) */
    unsigned short transient[4];     /* (offset, size) pairs its update / draw rewrite before any read: not compared */
    unsigned char diffs;             /* may go back to a record with up to 7 changed words, restored when re-created
                                        (an animation's phase, a spring's last frame): exact, as if it had stayed */
    const char *const *unless;       /* not in a stage with one of these classes (they walk or keep this one's entities;
                                        NULL-terminated, PS1_UNLESS) */
    unsigned char movesInCreate;     /* its Create moves it: the camera boxes use the created position, Create the
                                        scene file's */
} PS1DormantSpec;
#define PS1_UNLESS(...) ((const char *const[]){ __VA_ARGS__, NULL })

/* docs/ports/mania/32 7b: reserved slots sized by what the game puts there. In a stage with a Zone object (the zones) a reserved
   slot's block holds the largest of the classes listed for it that the stage has (no listed class: no block); other
   stages, and slots no entry names, keep the largest class. The game's table (Zone.c) names each class by its name (not
   its object pointer: a reference from the resident keeps a class in every act overlay, tools/gen_overlays.py), the
   engine finds it among the stage's classes. A reset into a slot too small still moves it to a spare block (counted:
   g_ps1EntityRelocations). */
typedef struct {
    unsigned short first, last; /* slots */
    const char *needs;          /* the entry applies only when the stage has this class (NULL: always) */
    const char *classes[8];     /* class names, NULL-terminated */
} PS1ReservedSpec;

/* docs/ports/mania/32 7b.7: a scene entity whose Create resets it into another class (HCZ's DCEvent bubbles become Water) gets a block
   for the larger of the two (the game's table, Zone.c, by class names; NULL-terminated). 7b.12: `to` NULL = a whole stock
   slot (sizeof(EntityBase)): for a class that writes past its struct, which the PC's uniform slots absorb */
typedef struct {
    const char *from, *to;
    const char *needs; /* 9b.2b: the entry applies only when the stage has this class (NULL: whenever it has `to`) */
} PS1GrowSpec;

/* docs/ports/mania/32 9b.2b: scene slots the game copies entities into by number (the Mean Bean Machine's landed beans: slot 0x600 +
   their cell) get a block for `cls` from the load when the stage has it: within [first, last], the first `span` slots of
   each `period` and the first `width` of each `stride` (the board's used cells). The game keeps pointers it takes before
   the copy: a block made then (a spare one) would leave them on the old slot. */
typedef struct {
    unsigned short first, last, period, span, stride, width;
    const char *cls;
} PS1SlotRangeSpec;

/* docs/ports/mania/32 9.4: a class written straight into an entity slot (game code past the engine's create / reset / copy: the
 * player slots' Player / DebugMode swaps, BSS_Setup's collectables) widens foreach_all's range of that class. */
#ifdef __cplusplus
extern "C"
#endif
void PS1ClassNoteEntity(void *entity);

/* P6 (docs/perf/57-mania-cost-per-act.md): a class's slots in slot order for a check that walks the whole class every
 * frame (foreach_all reads every slot of the class's range: ~1,400 in MMZ2 for 24 BuzzSaws). Every write of a class into
 * a slot (create, reset, copy, dormant wakes, scene placement, the game's own writes) goes through PS1ClassNote, which
 * counts it in ps1ClassAdds: the list is collected again (one foreach_all) whenever that count moved, and each use
 * checks the slot's class (one blanked since is skipped, as foreach_all skips it). Slots, not pointers: a PS1 entity
 * block may move. The same entities in the same order as foreach_all; more than PS1_CLASS_LIST_MAX: count -1 and the
 * caller takes foreach_all. The list lives in the object's file (its overlay), not in the resident EXE. */
#ifdef __cplusplus
extern "C"
#else
extern
#endif
unsigned int ps1ClassAdds;
#define PS1_CLASS_LIST_MAX (0x40)
typedef struct {
    unsigned int adds; /* ps1ClassAdds when collected */
    int count;         /* -1: more than PS1_CLASS_LIST_MAX (or never collected: adds 0 never matches after a load) */
    unsigned short slot[PS1_CLASS_LIST_MAX];
} PS1ClassList;
/* P8: a list's capacity is its slot array's (PS1ClassList: PS1_CLASS_LIST_MAX; a caller may declare a bigger one with the same
 * three fields) */
#define PS1_CLASS_LIST_CAP(list) ((int)(sizeof((list).slot) / sizeof((list).slot[0])))
#define PS1_CLASS_LIST_SYNC(list, type)                                                                                                              \
    do {                                                                                                                                             \
        if ((list).adds != ps1ClassAdds || !(list).adds) {                                                                                           \
            int ps1N = 0;                                                                                                                            \
            foreach_all(type, ps1E)                                                                                                                  \
            {                                                                                                                                        \
                if (ps1N < PS1_CLASS_LIST_CAP(list))                                                                                                 \
                    (list).slot[ps1N] = (unsigned short)RSDK.GetEntitySlot(ps1E);                                                                    \
                ++ps1N;                                                                                                                              \
            }                                                                                                                                        \
            (list).count = ps1N <= PS1_CLASS_LIST_CAP(list) ? ps1N : -1;                                                                             \
            (list).adds  = ps1ClassAdds;                                                                                                             \
        }                                                                                                                                            \
    } while (0)
#define PS1_FOREACH_LISTED(list, type, entityOut)                                                                                                    \
    for (int ps1I = 0; ps1I < (list).count; ++ps1I)                                                                                                  \
        for (Entity##type *entityOut = RSDK_GET_ENTITY((list).slot[ps1I], type); entityOut && entityOut->classID == type->classID;                 \
             entityOut = NULL)
#ifdef __cplusplus
extern "C"
#endif
void PS1AtlasRequestTrackGroups(const char *track); // docs/ports/mania/32 9i: a track's sprite groups at the next update / draw boundary
#ifdef __cplusplus
extern "C"
#endif
void PS1SetFrameskip(int on); // docs/ports/mania/32 phase 13b: Options > Video > Frameskip (off when 0)
#ifdef __cplusplus
extern "C"
#endif
void PS1DrawLinesWorld(const int *xy, const unsigned int *colors, int n); // docs/ports/mania/32 9q: DrawLine INK_NONE x n, one bulk
void PS1DrawLinesInk(const int *xy, int n, unsigned int color, int alpha, int inkEffect); // 9v.9: DrawLine x n, one ink
#ifndef PS1_LINE_BATCH
#define PS1_LINE_BATCH 1 // docs/ports/mania/32 9q: PaperRoller's lines through PS1DrawLinesWorld (framebuffers identical)
#endif

/* docs/ports/mania/32 9b.2b: a blank temp entity sized for the class the caller copies into it next (Zone_ReloadStoredEntities: the
 * carried sign post / item boxes), not the blank object's full EntityBase (no temp block is that big: a spare block). */
#ifdef __cplusplus
extern "C"
#endif
void *PS1CreateBlankFor(unsigned short classID);

/* docs/ports/mania/32 9b.6: a frame whose texture the game scrolls by rewriting its sprX (ParallaxSprite's SHIFT attribute: CPZ2's
 * masked chemicals) draws `du` texels further right in its atlas strip (build_atlas SHIFT_STRIPS) on its next draw. */
#ifdef __cplusplus
extern "C"
#endif
void PS1SetFrameShift(const void *frame, int du);

/* docs/ports/mania/32 9r / choice 185: every-other-frame visuals under the 30 fps cap (two logic steps a drawn frame). A
 * draw-time `counter & 1` on a counter that ticks once a logic step always shows the same parity there, and a
 * `visible ^= true` in an update toggles twice: the flicker sticks on or off (boss hit flashes, debris, the shield over
 * a ShopWindow). PS1_FLICK(v) takes the bit that changes once a drawn frame; PS1_LAST_STEP is true on a drawn frame's
 * last logic step. Uncapped (one step) both reduce to the stock expressions. */
#ifdef __cplusplus
extern "C" {
#endif
extern volatile int g_ps1FlickShift; /* 0: one logic step a drawn frame; 1: two or more (ps1/rsdk5_main.cpp) */
extern volatile int g_ps1LastStep;   /* 1 on a drawn frame's last logic step (always 1 at one step) */
#ifdef __cplusplus
}
#endif
#define PS1_FLICK(v)  ((int)(((unsigned)(v) >> g_ps1FlickShift) & 1))
#define PS1_LAST_STEP (g_ps1LastStep)

#endif /* PS1_LIMITS_H */
