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

#endif /* PS1_LIMITS_H */
