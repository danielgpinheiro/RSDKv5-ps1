/*
 * PS1 port (docs/32): the limits the engine (RSDKv5/RSDK) and the game (SonicMania/GameLink.h) must agree on.
 * Both include this file under RETRO_PS1_BUILD, so a slot count can never differ between the two (the game loops
 * over SCENEENTITY_COUNT slots, indexes TEMPENTITY_START, sizes globals->atlEntityData by it).
 *
 * Phase 2.2 (headless boot to the title, 8 MB debug link): the stock stride with a small list. Logos has 5 scene
 * entities and Title 77. Phase 2.3 replaces this with per-scene storage sized from the scene files.
 */
#ifndef PS1_LIMITS_H
#define PS1_LIMITS_H

#define RESERVE_ENTITY_COUNT (0x40)  /* stock: SLOT_PLAYER1.. and the other fixed slots live here */
#define SCENEENTITY_COUNT    (0x100) /* stock 0x800 */
#define TEMPENTITY_COUNT     (0x40)  /* stock 0x100 */

#endif /* PS1_LIMITS_H */
