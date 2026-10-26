// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: LRZRockPile Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectLRZRockPile *LRZRockPile;

#if defined RETRO_PS1_BUILD
// PS1 (docs/ports/mania/32 7b.10): the debris table LRZRockPile_SetupDebris fills from constants (by type: at most 22 pieces x 6) and
// LRZRockPile_SpawnRockDebris reads right after, in the same call: one table for every pile, not 768 B in each of them
static int32 s_ps1RockDebris[22 * 6];
#define LRZROCKPILE_DEBRIS s_ps1RockDebris
#else
#define LRZROCKPILE_DEBRIS self->rockDebrisInfo
#endif

void LRZRockPile_Update(void)
{
    RSDK_THIS(LRZRockPile);

    StateMachine_Run(self->state);

    if (self->timer > 0)
        self->timer--;
}

void LRZRockPile_LateUpdate(void) {}

void LRZRockPile_StaticUpdate(void) {}

void LRZRockPile_Draw(void)
{
    RSDK_THIS(LRZRockPile);

    RSDK.DrawSprite(&self->animator, NULL, false);
}

void LRZRockPile_Create(void *data)
{
    RSDK_THIS(LRZRockPile);

    self->active = ACTIVE_BOUNDS;
    if (self->planeFilter > 0 && ((uint8)(self->planeFilter - 1) & 2))
        self->drawGroup = Zone->playerDrawGroup[1];
    else
        self->drawGroup = Zone->objectDrawGroup[0];

    self->startPos      = self->position;
    self->visible       = true;
    self->drawFX        = FX_ROTATE | FX_FLIP;
    self->updateRange.x = 0x800000;
    self->updateRange.y = 0x800000;

    switch (self->type) {
        case LRZROCKPILE_WALL:
            RSDK.SetSpriteAnimation(LRZRockPile->aniFrames, 0, &self->animator, true, 0);
            self->hitbox = LRZRockPile->hitboxWall;
            self->state  = LRZRockPile_State_Wall;
            break;

        case LRZROCKPILE_FLOOR_TOPSOLID:
            RSDK.SetSpriteAnimation(LRZRockPile->aniFrames, 1, &self->animator, true, 0);
            self->hitbox = LRZRockPile->hitboxFloorTopSolid;
            self->state  = LRZRockPile_State_Floor_TopSolid;
            break;

        case LRZROCKPILE_FLOOR_ALLSOLID:
            RSDK.SetSpriteAnimation(LRZRockPile->aniFrames, 1, &self->animator, true, 0);
            self->hitbox = LRZRockPile->hitboxFloorAllSolid;
            self->state  = LRZRockPile_State_Floor_AllSolid;
            break;
    }
}

void LRZRockPile_StageLoad(void)
{
    if (RSDK.CheckSceneFolder("LRZ1")) {
        LRZRockPile->aniFrames      = RSDK.LoadSpriteAnimation("LRZ1/LRZRockPile.bin", SCOPE_STAGE);
        LRZRockPile->particleFrames = RSDK.LoadSpriteAnimation("LRZ1/Particles.bin", SCOPE_STAGE);

        LRZRockPile->hitboxWall.left   = -16;
        LRZRockPile->hitboxWall.top    = -40;
        LRZRockPile->hitboxWall.right  = 16;
        LRZRockPile->hitboxWall.bottom = 40;
    }
    else if (RSDK.CheckSceneFolder("LRZ2")) {
        LRZRockPile->aniFrames = RSDK.LoadSpriteAnimation("LRZ2/LRZRockPile.bin", SCOPE_STAGE);

        LRZRockPile->hitboxWall.left   = -16;
        LRZRockPile->hitboxWall.top    = -32;
        LRZRockPile->hitboxWall.right  = 16;
        LRZRockPile->hitboxWall.bottom = 32;
    }

    LRZRockPile->sfxLedgeBreak = RSDK.GetSfx("Stage/LedgeBreak.wav");

    LRZRockPile->hitboxFloorTopSolid.left   = -64;
    LRZRockPile->hitboxFloorTopSolid.top    = -32;
    LRZRockPile->hitboxFloorTopSolid.right  = 64;
    LRZRockPile->hitboxFloorTopSolid.bottom = -16;

    LRZRockPile->hitboxFloorAllSolid.left   = -64;
    LRZRockPile->hitboxFloorAllSolid.top    = -32;
    LRZRockPile->hitboxFloorAllSolid.right  = 64;
    LRZRockPile->hitboxFloorAllSolid.bottom = 32;
}

void LRZRockPile_SetupDebris(void)
{
    RSDK_THIS(LRZRockPile);

    switch (self->type) {
        case LRZROCKPILE_WALL:
            self->debrisCount = 6;

            LRZROCKPILE_DEBRIS[0] = 3;
            LRZROCKPILE_DEBRIS[1] = 2;
            LRZROCKPILE_DEBRIS[2] = -0x10000;
            LRZROCKPILE_DEBRIS[3] = -0x190000;
            LRZROCKPILE_DEBRIS[4] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[5] = 0;

            LRZROCKPILE_DEBRIS[6]  = 3;
            LRZROCKPILE_DEBRIS[7]  = 4;
            LRZROCKPILE_DEBRIS[8]  = 0;
            LRZROCKPILE_DEBRIS[9]  = 0x190000;
            LRZROCKPILE_DEBRIS[10] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[11] = 0;

            LRZROCKPILE_DEBRIS[12] = 2;
            LRZROCKPILE_DEBRIS[13] = 0;
            LRZROCKPILE_DEBRIS[14] = -0x80000;
            LRZROCKPILE_DEBRIS[15] = -0x100000;
            LRZROCKPILE_DEBRIS[16] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[17] = 0;

            LRZROCKPILE_DEBRIS[18] = 2;
            LRZROCKPILE_DEBRIS[19] = 2;
            LRZROCKPILE_DEBRIS[20] = 0x80000;
            LRZROCKPILE_DEBRIS[21] = 0x200000;
            LRZROCKPILE_DEBRIS[22] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[23] = 0;

            LRZROCKPILE_DEBRIS[24] = 3;
            LRZROCKPILE_DEBRIS[25] = 0;
            LRZROCKPILE_DEBRIS[26] = -0x10000;
            LRZROCKPILE_DEBRIS[27] = -0x20000;
            LRZROCKPILE_DEBRIS[28] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[29] = 0;

            LRZROCKPILE_DEBRIS[30] = 3;
            LRZROCKPILE_DEBRIS[31] = 2;
            LRZROCKPILE_DEBRIS[32] = -0x10000;
            LRZROCKPILE_DEBRIS[33] = 0xC0000;
            LRZROCKPILE_DEBRIS[34] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[35] = 0;
            break;

        case LRZROCKPILE_FLOOR_TOPSOLID:
        case LRZROCKPILE_FLOOR_ALLSOLID:
            self->debrisCount       = 22;
            LRZROCKPILE_DEBRIS[0] = 3;
            LRZROCKPILE_DEBRIS[1] = 3;
            LRZROCKPILE_DEBRIS[2] = -0x2D0000;
            LRZROCKPILE_DEBRIS[3] = 0xB0000;
            LRZROCKPILE_DEBRIS[4] = FLIP_X;
            LRZROCKPILE_DEBRIS[5] = 9;

            LRZROCKPILE_DEBRIS[6]  = 3;
            LRZROCKPILE_DEBRIS[7]  = 3;
            LRZROCKPILE_DEBRIS[8]  = 0x2D0000;
            LRZROCKPILE_DEBRIS[9]  = 0xB0000;
            LRZROCKPILE_DEBRIS[10] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[11] = 9;

            LRZROCKPILE_DEBRIS[12] = 3;
            LRZROCKPILE_DEBRIS[13] = 3;
            LRZROCKPILE_DEBRIS[14] = -0x200000;
            LRZROCKPILE_DEBRIS[15] = -0x100000;
            LRZROCKPILE_DEBRIS[16] = FLIP_X;
            LRZROCKPILE_DEBRIS[17] = 30;

            LRZROCKPILE_DEBRIS[18] = 3;
            LRZROCKPILE_DEBRIS[19] = 3;
            LRZROCKPILE_DEBRIS[20] = 0x200000;
            LRZROCKPILE_DEBRIS[21] = -0x100000;
            LRZROCKPILE_DEBRIS[22] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[23] = 30;

            LRZROCKPILE_DEBRIS[24] = 2;
            LRZROCKPILE_DEBRIS[25] = 0;
            LRZROCKPILE_DEBRIS[26] = 0;
            LRZROCKPILE_DEBRIS[27] = 0x180000;
            LRZROCKPILE_DEBRIS[28] = FLIP_XY;
            LRZROCKPILE_DEBRIS[29] = 0;

            LRZROCKPILE_DEBRIS[30] = 2;
            LRZROCKPILE_DEBRIS[31] = 0;
            LRZROCKPILE_DEBRIS[32] = -0x370000;
            LRZROCKPILE_DEBRIS[33] = 0x10000;
            LRZROCKPILE_DEBRIS[34] = 0;
            LRZROCKPILE_DEBRIS[35] = 20;

            LRZROCKPILE_DEBRIS[36] = 2;
            LRZROCKPILE_DEBRIS[37] = 1;
            LRZROCKPILE_DEBRIS[38] = -0x300000;
            LRZROCKPILE_DEBRIS[39] = 0xF0000;
            LRZROCKPILE_DEBRIS[40] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[41] = 14;

            LRZROCKPILE_DEBRIS[42] = 2;
            LRZROCKPILE_DEBRIS[43] = 3;
            LRZROCKPILE_DEBRIS[44] = -0x220000;
            LRZROCKPILE_DEBRIS[45] = 0x180000;
            LRZROCKPILE_DEBRIS[46] = FLIP_X;
            LRZROCKPILE_DEBRIS[47] = 6;

            LRZROCKPILE_DEBRIS[48] = 1;
            LRZROCKPILE_DEBRIS[49] = 5;
            LRZROCKPILE_DEBRIS[50] = -0x160000;
            LRZROCKPILE_DEBRIS[51] = 0x130000;
            LRZROCKPILE_DEBRIS[52] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[53] = 12;

            LRZROCKPILE_DEBRIS[54] = 2;
            LRZROCKPILE_DEBRIS[55] = 0;
            LRZROCKPILE_DEBRIS[56] = 3604480;
            LRZROCKPILE_DEBRIS[57] = 0x10000;
            LRZROCKPILE_DEBRIS[58] = FLIP_X;
            LRZROCKPILE_DEBRIS[59] = 20;

            LRZROCKPILE_DEBRIS[60] = 2;
            LRZROCKPILE_DEBRIS[61] = 1;
            LRZROCKPILE_DEBRIS[62] = 0x300000;
            LRZROCKPILE_DEBRIS[63] = 0xF0000;
            LRZROCKPILE_DEBRIS[64] = FLIP_X;
            LRZROCKPILE_DEBRIS[65] = 14;

            LRZROCKPILE_DEBRIS[66] = 2;
            LRZROCKPILE_DEBRIS[67] = 3;
            LRZROCKPILE_DEBRIS[68] = 0x220000;
            LRZROCKPILE_DEBRIS[69] = 0x180000;
            LRZROCKPILE_DEBRIS[70] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[71] = 6;

            LRZROCKPILE_DEBRIS[72] = 1;
            LRZROCKPILE_DEBRIS[73] = 5;
            LRZROCKPILE_DEBRIS[74] = 0x160000;
            LRZROCKPILE_DEBRIS[75] = 0x130000;
            LRZROCKPILE_DEBRIS[76] = FLIP_X;
            LRZROCKPILE_DEBRIS[77] = 12;

            LRZROCKPILE_DEBRIS[78] = 2;
            LRZROCKPILE_DEBRIS[79] = 2;
            LRZROCKPILE_DEBRIS[80] = -0xA0000;
            LRZROCKPILE_DEBRIS[81] = 0x140000;
            LRZROCKPILE_DEBRIS[82] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[83] = 8;

            LRZROCKPILE_DEBRIS[84] = 2;
            LRZROCKPILE_DEBRIS[85] = 2;
            LRZROCKPILE_DEBRIS[86] = 0xA0000;
            LRZROCKPILE_DEBRIS[87] = 0x140000;
            LRZROCKPILE_DEBRIS[88] = FLIP_X;
            LRZROCKPILE_DEBRIS[89] = 7;

            LRZROCKPILE_DEBRIS[90] = 3;
            LRZROCKPILE_DEBRIS[91] = 0;
            LRZROCKPILE_DEBRIS[92] = -0x310000;
            LRZROCKPILE_DEBRIS[93] = -0x110000;
            LRZROCKPILE_DEBRIS[94] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[95] = 26;

            LRZROCKPILE_DEBRIS[96]  = 3;
            LRZROCKPILE_DEBRIS[97]  = 0;
            LRZROCKPILE_DEBRIS[98]  = 0xF0000;
            LRZROCKPILE_DEBRIS[99]  = -0x110000;
            LRZROCKPILE_DEBRIS[100] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[101] = 24;

            LRZROCKPILE_DEBRIS[102] = 3;
            LRZROCKPILE_DEBRIS[103] = 0;
            LRZROCKPILE_DEBRIS[104] = -0xF0000;
            LRZROCKPILE_DEBRIS[105] = -0x110000;
            LRZROCKPILE_DEBRIS[106] = FLIP_X;
            LRZROCKPILE_DEBRIS[107] = 24;

            LRZROCKPILE_DEBRIS[108] = 3;
            LRZROCKPILE_DEBRIS[109] = 2;
            LRZROCKPILE_DEBRIS[110] = 0x310000;
            LRZROCKPILE_DEBRIS[111] = -0x110000;
            LRZROCKPILE_DEBRIS[112] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[113] = 26;

            LRZROCKPILE_DEBRIS[114] = 3;
            LRZROCKPILE_DEBRIS[115] = 0;
            LRZROCKPILE_DEBRIS[116] = -0x210000;
            LRZROCKPILE_DEBRIS[117] = 0x20000;
            LRZROCKPILE_DEBRIS[118] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[119] = 18;

            LRZROCKPILE_DEBRIS[120] = 3;
            LRZROCKPILE_DEBRIS[121] = 0;
            LRZROCKPILE_DEBRIS[122] = 0x210000;
            LRZROCKPILE_DEBRIS[123] = 0x20000;
            LRZROCKPILE_DEBRIS[124] = FLIP_X;
            LRZROCKPILE_DEBRIS[125] = 17;

            LRZROCKPILE_DEBRIS[126] = 3;
            LRZROCKPILE_DEBRIS[127] = 1;
            LRZROCKPILE_DEBRIS[128] = 0;
            LRZROCKPILE_DEBRIS[129] = 0x80000;
            LRZROCKPILE_DEBRIS[130] = FLIP_NONE;
            LRZROCKPILE_DEBRIS[131] = 16;
            break;
    }
}

void LRZRockPile_SpawnRockDebris(int32 x, int32 y, int32 velX, int32 velY)
{
    RSDK_THIS(LRZRockPile);

    LRZRockPile_SetupDebris();

    int32 sizeX = (self->hitbox.right - self->hitbox.left) << 16;
    int32 sizeY = (self->hitbox.bottom - self->hitbox.top) << 16;

    int32 destroyDelay = 0;
    if (RSDK.CheckSceneFolder("LRZ1")) {
        for (int32 i = 0; i < self->debrisCount; ++i) {
            int32 animationID = LRZROCKPILE_DEBRIS[(i * 6) + 0];
            int32 frameID     = LRZROCKPILE_DEBRIS[(i * 6) + 1];
            int32 offsetX     = LRZROCKPILE_DEBRIS[(i * 6) + 2];
            int32 offsetY     = LRZROCKPILE_DEBRIS[(i * 6) + 3];
            int32 direction   = LRZROCKPILE_DEBRIS[(i * 6) + 4];
            int32 delay       = LRZROCKPILE_DEBRIS[(i * 6) + 5];

            if (!offsetX && !offsetY)
                break;

            int32 spawnX       = self->position.x + offsetX;
            int32 spawnY       = self->position.y + offsetY;
            EntityDebris *rock = CREATE_ENTITY(Debris, Debris_State_Fall, spawnX, spawnY);

            RSDK.SetSpriteAnimation(LRZRockPile->particleFrames, animationID, &rock->animator, true, frameID);
            rock->drawGroup       = self->drawGroup;
            rock->delay           = delay;
            rock->direction       = direction;
            rock->gravityStrength = 0x3800;
            rock->updateRange.x   = 0x800000;
            rock->updateRange.y   = 0x800000;

            rock->velocity.x = (velX >> 8) * ((abs(x - spawnX) / (sizeX >> 16)) >> 8);
            if (velX)
                rock->velocity.x += ((RSDK.Rand(0, 16) + 8) * (2 * (velX > 1) - 1)) << 12;

            rock->velocity.y = (velY >> 8) * (((y - spawnY) / (sizeY >> 16)) >> 8);
            if (velY)
                rock->velocity.y += ((RSDK.Rand(0, 16) + 8) * (2 * (velY > 1) - 1)) << 12;

            destroyDelay = MAX(destroyDelay, delay);
        }
    }
    else if (RSDK.CheckSceneFolder("LRZ2")) {
        int32 spawnY = self->position.y - 0x100000;

        EntityDebris *debris = CREATE_ENTITY(Debris, Debris_State_Fall, self->position.x, spawnY);
        RSDK.SetSpriteAnimation(LRZRockPile->aniFrames, 0, &debris->animator, true, 1);
        debris->drawFX          = FX_ROTATE | FX_FLIP;
        debris->drawGroup       = self->drawGroup;
        debris->gravityStrength = 0x3800;
        debris->direction       = self->direction;
        debris->updateRange.x   = 0x800000;
        debris->updateRange.y   = 0x800000;
        debris->rotSpeed        = 8 * (velX <= 0) - 4;

        debris->velocity.x = (velX >> 8) * (abs(abs(x - self->position.x) / (sizeX >> 16)) >> 8);
        if (velX)
            debris->velocity.x += ((RSDK.Rand(0, 16) + 8) * (2 * (velX > 1) - 1)) << 12;

        debris->velocity.y = (velY >> 8) * (((y - spawnY) / (sizeY >> 16)) >> 8);
        if (velY)
            debris->velocity.y += ((RSDK.Rand(0, 16) + 8) * (2 * (velY > 1) - 1)) << 12;

        sizeX >>= 16;
        sizeY >>= 16;
        spawnY = self->position.y + 0x100000;

        debris = CREATE_ENTITY(Debris, Debris_State_Fall, self->position.x, spawnY);
        RSDK.SetSpriteAnimation(LRZRockPile->aniFrames, 0, &debris->animator, true, 2);
        debris->drawFX          = FX_ROTATE | FX_FLIP;
        debris->drawGroup       = self->drawGroup;
        debris->gravityStrength = 0x3800;
        debris->direction       = self->direction;
        debris->updateRange.x   = 0x800000;
        debris->updateRange.y   = 0x800000;
        debris->rotSpeed        = 8 * (velX > 0) - 4;
        debris->velocity.x      = (velX >> 8) * ((abs(x - self->position.x) / sizeX) >> 8);

        if (velX)
            debris->velocity.x += ((RSDK.Rand(0, 16) + 8) * (2 * (velX > 1) - 1)) << 12;

        debris->velocity.y = (velY >> 8) * (((y - spawnY) / sizeY) >> 8);
        if (velY)
            debris->velocity.y += ((RSDK.Rand(0, 16) + 8) * (2 * (velY > 1) - 1)) << 12;
    }

    self->destroyTimer = destroyDelay;
    RSDK.PlaySfx(LRZRockPile->sfxLedgeBreak, false, 255);

    if (self->destroyTimer > 0) {
        self->visible = false;
        self->state   = LRZRockPile_State_Broken;
    }
    else {
        destroyEntity(self);
    }
}

void LRZRockPile_State_Wall(void)
{
    RSDK_THIS(LRZRockPile);

    foreach_active(Player, player)
    {
        if (self->planeFilter <= 0 || player->collisionPlane == ((uint8)(self->planeFilter - 1) & 1)) {
            if ((!self->onlyKnux || player->characterID == ID_KNUCKLES)
#if MANIA_USE_PLUS
                && (!self->onlyMighty || player->characterID == ID_MIGHTY)
#endif
            ) {
                bool32 canBreak = abs(player->groundVel) >= 0x48000 && player->onGround && player->animator.animationID == ANI_JUMP;
                if (player->shield == SHIELD_FIRE) {
                    EntityShield *shield = RSDK_GET_ENTITY(Player->playerCount + RSDK.GetEntitySlot(player), Shield);
                    canBreak |= shield->shieldAnimator.animationID == SHIELDANI_FIREATTACK;
                }

                canBreak |= player->characterID == ID_KNUCKLES;
                if (player->characterID == ID_SONIC && player->superState == SUPERSTATE_SUPER)
                    canBreak |= true;

                if ((canBreak && !player->sidekick) && !self->unbreakable) {
                    if (Player_CheckCollisionTouch(player, self, &self->hitbox)) {
                        if (player->position.x > self->position.x)
                            LRZRockPile_SpawnRockDebris(player->position.x, player->position.y, -0x40000, -0x20000);
                        else
                            LRZRockPile_SpawnRockDebris(player->position.x, player->position.y, 0x40000, -0x20000);
                    }
                    continue; // rock's broken now, we don't want to check any solid collisions
                }
            }

            Player_CheckCollisionBox(player, self, &self->hitbox);
        }
    }
}

void LRZRockPile_State_Floor_TopSolid(void)
{
    RSDK_THIS(LRZRockPile);

    if (self->canCollapse && self->timer <= 0) {
        LRZRockPile_SpawnRockDebris(0, 0, 0, 0);
    }
    else {
        foreach_active(Player, player)
        {
            if (self->planeFilter <= 0 || player->collisionPlane == ((uint8)(self->planeFilter - 1) & 1)) {
                if (Player_CheckCollisionPlatform(player, self, &self->hitbox)
#if MANIA_USE_PLUS
                    && (!self->onlyMighty || (player->characterID == ID_MIGHTY && player->state == Player_State_MightyHammerDrop))
#endif
                    && !player->sidekick && !self->unbreakable && player->onGround) {
                    if (self->delay != (uint16)-1) {
                        if (!self->canCollapse) {
                            self->canCollapse = true;
                            self->timer       = self->delay;
                        }
#if MANIA_USE_PLUS
                        if (player->state == Player_State_MightyHammerDrop)
                            self->timer = 1;
#endif
                    }
                }
            }
        }
    }
}

void LRZRockPile_State_Floor_AllSolid(void)
{
    RSDK_THIS(LRZRockPile);

    if (self->canCollapse && self->timer <= 0) {
        LRZRockPile_SpawnRockDebris(0, 0, 0, 0);
    }
    else {
        foreach_active(Player, player)
        {
            if (self->planeFilter <= 0 || player->collisionPlane == ((uint8)(self->planeFilter - 1) & 1)) {
                if (Player_CheckCollisionBox(player, self, &self->hitbox) == C_TOP
#if MANIA_USE_PLUS
                    && (!self->onlyMighty || (player->characterID == ID_MIGHTY && player->state == Player_State_MightyHammerDrop))
#endif
                    && !player->sidekick && !self->unbreakable && player->onGround) {
                    if (self->delay != (uint16)-1) {
                        if (!self->canCollapse) {
                            self->canCollapse = true;
                            self->timer       = self->delay;
                        }
#if MANIA_USE_PLUS
                        if (player->state == Player_State_MightyHammerDrop)
                            self->timer = 1;
#endif
                    }
                }
            }
        }
    }
}

void LRZRockPile_State_Broken(void)
{
    RSDK_THIS(LRZRockPile);

    if (self->destroyTimer <= 0) {
        destroyEntity(self);
    }
    else {
        foreach_active(Player, player)
        {
            if (self->planeFilter <= 0 || player->collisionPlane == ((uint8)(self->planeFilter - 1) & 1))
                Player_CheckCollisionBox(player, self, &self->hitbox);
        }
    }

    --self->destroyTimer;
}

#if GAME_INCLUDE_EDITOR
void LRZRockPile_EditorDraw(void)
{
    RSDK_THIS(LRZRockPile);

    switch (self->type) {
        case LRZROCKPILE_WALL: RSDK.SetSpriteAnimation(LRZRockPile->aniFrames, 0, &self->animator, true, 0); break;
        case LRZROCKPILE_FLOOR_TOPSOLID: RSDK.SetSpriteAnimation(LRZRockPile->aniFrames, 1, &self->animator, true, 0); break;
        case LRZROCKPILE_FLOOR_ALLSOLID: RSDK.SetSpriteAnimation(LRZRockPile->aniFrames, 1, &self->animator, true, 0); break;
    }

    LRZRockPile_Draw();
}

void LRZRockPile_EditorLoad(void)
{
    if (RSDK.CheckSceneFolder("LRZ1")) {
        LRZRockPile->aniFrames      = RSDK.LoadSpriteAnimation("LRZ1/LRZRockPile.bin", SCOPE_STAGE);
        LRZRockPile->particleFrames = RSDK.LoadSpriteAnimation("LRZ1/Particles.bin", SCOPE_STAGE);
    }
    else if (RSDK.CheckSceneFolder("LRZ2")) {
        LRZRockPile->aniFrames = RSDK.LoadSpriteAnimation("LRZ2/LRZRockPile.bin", SCOPE_STAGE);
    }

    RSDK_ACTIVE_VAR(LRZRockPile, type);
    RSDK_ENUM_VAR("Wall", LRZROCKPILE_WALL);
    RSDK_ENUM_VAR("Floor (Top Solid)", LRZROCKPILE_FLOOR_TOPSOLID);
    RSDK_ENUM_VAR("Floor (All Solid)", LRZROCKPILE_FLOOR_ALLSOLID);

    RSDK_ACTIVE_VAR(LRZRockPile, planeFilter);
    RSDK_ENUM_VAR("None", PLANEFILTER_NONE);
    RSDK_ENUM_VAR("AL", PLANEFILTER_AL);
    RSDK_ENUM_VAR("BL", PLANEFILTER_BL);
    RSDK_ENUM_VAR("AH", PLANEFILTER_AH);
    RSDK_ENUM_VAR("BH", PLANEFILTER_BH);

    // No idea what "debugParticles" is, assuming it was an editor-only thing to preview what the rock particles were like???
}
#endif

void LRZRockPile_Serialize(void)
{
    RSDK_EDITABLE_VAR(LRZRockPile, VAR_UINT8, type);
    RSDK_EDITABLE_VAR(LRZRockPile, VAR_UINT16, delay);
    RSDK_EDITABLE_VAR(LRZRockPile, VAR_BOOL, debugParticles);
    RSDK_EDITABLE_VAR(LRZRockPile, VAR_BOOL, unbreakable);
    RSDK_EDITABLE_VAR(LRZRockPile, VAR_ENUM, planeFilter);
    RSDK_EDITABLE_VAR(LRZRockPile, VAR_BOOL, onlyKnux);
#if MANIA_USE_PLUS
    RSDK_EDITABLE_VAR(LRZRockPile, VAR_BOOL, onlyMighty);
#endif
}
