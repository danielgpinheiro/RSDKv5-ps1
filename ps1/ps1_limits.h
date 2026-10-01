/*
 * PS1 port (docs/32): the limits the engine (RSDKv5/RSDK) and the game (SonicMania/GameLink.h) must agree on.
 * Both include this file under RETRO_PS1_BUILD, so a slot count can never differ between the two (the game loops
 * over SCENEENTITY_COUNT slots, indexes TEMPENTITY_START, sizes globals->atlEntityData by it).
 *
 * Phase 2.3: the stock counts. The memory is no longer ENTITY_COUNT x sizeof(EntityBase): every slot points to a
 * block sized for what it holds (RSDKv5/RSDK/Scene/PS1/PS1Entities.hpp), so the game's slot constants stay the PC's.
 */
#ifndef PS1_LIMITS_H
#define PS1_LIMITS_H

#define RESERVE_ENTITY_COUNT (0x40)  /* SLOT_PLAYER1.. and the other fixed slots */
#define SCENEENTITY_COUNT    (0x800) /* the largest Mania-mode scene keeps 2017 entities (PSZ2/2, docs/32 phase 1) */
#define TEMPENTITY_COUNT     (0x100)

/* docs/32 7.4, the Dreamcast port's values: scanlines[0].deform = these, then the camera in scanlines[1..2]: a layer the
   engine draws as 3D tiles (the UFO stages' playfield: RSDK/Graphics/PS1/PS1UfoFloor.cpp) */
#define SCANLINE_MAJOR_MAGIC_3DTILES (0xFEDCBA09u)
#define SCANLINE_MINOR_MAGIC_UFO     (0x90ABCDEFu)
#define SCANLINE_MINOR_MAGIC_UFO_FLOOR (0x2345678Bu) /* 7.5: a "3D Floor" layer (UFO2, UFO6) */
#define SCANLINE_MINOR_MAGIC_ROOF      (0x56789ABCu) /* 7.5: a "3D Roof" layer (UFO4) */

/* docs/32 7b (choice 113): dormant entities. A class listed in the game's ps1DormantSpecs (resident code) may keep no
   entity block while it is out of every camera's reach and still as its Create left it: the engine keeps its scene
   record, creates it when the cameras come near and returns it to a record when they leave
   (RSDK/Scene/PS1/PS1Entities.cpp). */
/* docs/32 7b: SaveGame's checkpoint recall and dormant entities (RSDK/Scene/PS1/PS1Entities.cpp) */
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
    const char *unless[3];           /* not in a stage with one of these classes (they walk or keep this one's entities) */
    unsigned char movesInCreate;     /* its Create moves it: the camera boxes use the created position, Create the
                                        scene file's */
} PS1DormantSpec;

/* docs/32 7b: reserved slots sized by what the game puts there. In a stage with a Zone object (the zones) a reserved
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

#endif /* PS1_LIMITS_H */
