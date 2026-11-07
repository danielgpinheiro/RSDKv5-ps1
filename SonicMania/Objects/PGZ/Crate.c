// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: Crate Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectCrate *Crate;

#if defined RETRO_PS1_BUILD
// P16 (docs/perf/57): PSZ1 x 6400 updates ~50 crates a frame (49 hblanks), each colliding every player twice (Crate_Collide,
// then Platform_Update's Platform_Collision_Solid). A player beyond both hitboxes' reach is skipped where the engine's
// checks cannot find a side: CheckObjectCollisionBox needs the boxes to reach each other on both axes (its +-1
// adjustments aside: + 2 here) and writes nothing on C_NONE (P2's InvisibleBlock skip); CheckObjectCollisionPlatform
// needs them to overlap on x (strictly; its y test uses the previous position, so only x is used there) and writes
// nothing then (the hitbox flips it makes are undone)
static int32 Crate_PS1Reach(const Hitbox *h, bool32 yAxis)
{
    if (!h)
        return 0;
    int32 a = yAxis ? h->top : h->left, b = yAxis ? h->bottom : h->right;
    a = a < 0 ? -a : a, b = b < 0 ? -b : b;
    return a > b ? a : b;
}
// Far on x (or, with yToo, on y): every box of the player (its outerbox, the animator's) out of reach of `box`
static bool32 Crate_PS1Far(EntityCrate *crate, const Hitbox *box, EntityPlayer *player, Hitbox *playerHitbox, bool32 yToo)
{
    const Hitbox *ob = player->outerbox ? player->outerbox : playerHitbox;
    int32 pw = Crate_PS1Reach(playerHitbox, false), t = Crate_PS1Reach(ob, false);
    pw       = pw > t ? pw : t;
    int32 dx = (player->position.x >> 16) - (crate->position.x >> 16), rx = Crate_PS1Reach(box, false) + pw + 2;
    if (dx > rx || -dx > rx)
        return true;
    if (!yToo)
        return false;
    int32 pt = Crate_PS1Reach(playerHitbox, true);
    t        = Crate_PS1Reach(ob, true);
    pt       = pt > t ? pt : t;
    int32 dy = (player->position.y >> 16) - (crate->position.y >> 16), ry = Crate_PS1Reach(box, true) + pt + 2;
    return dy > ry || -dy > ry;
}
// Platform_Collision_Solid with the x-far skip (Crate_Create sets it in its place): the stood / push resets as stock
static void Crate_PS1Collision_Solid(void)
{
    RSDK_THIS(Crate);

    Hitbox *solidHitbox    = RSDK.GetHitbox(&self->animator, 1);
    Hitbox *platformHitbox = RSDK.GetHitbox(&self->animator, 0);
    int32 stoodPlayers     = self->stoodPlayers;
    int32 reach            = Crate_PS1Reach(solidHitbox, false), t = Crate_PS1Reach(platformHitbox, false);
    Hitbox wide            = { (int16)-(reach > t ? reach : t), 0, (int16)(reach > t ? reach : t), 0 };

    self->stoodPlayers = 0;
    self->pushPlayersL = 0;
    self->pushPlayersR = 0;
    foreach_active(Player, player)
    {
        if (Crate_PS1Far(self, &wide, player, Player_GetHitbox(player), false))
            continue;
        int32 playerID = RSDK.GetEntitySlot(player);
        Player_CheckCollisionPlatform(player, self, platformHitbox);

        switch (Player_CheckCollisionBox(player, self, solidHitbox)) {
            case C_TOP:
                Platform_HandleStood((EntityPlatform *)self, player, playerID, stoodPlayers);

                if (self->velocity.y <= 0)
                    player->collisionFlagV |= 1;
                break;

            case C_LEFT:
                if (player->onGround && player->right)
                    self->pushPlayersL |= 1 << playerID;

                if (self->velocity.x <= 0) {
                    if (player->left) {
                        player->groundVel  = self->velocity.x;
                        player->velocity.x = player->groundVel;
                        player->velocity.x -= player->acceleration;
                    }
                    player->collisionFlagH |= 1;
                }
                break;

            case C_RIGHT:
                if (player->onGround && player->left)
                    self->pushPlayersR |= 1 << playerID;

                if (self->velocity.x >= 0) {
                    if (player->right) {
                        player->groundVel  = self->velocity.x;
                        player->velocity.x = player->groundVel;
                        player->velocity.x += player->acceleration;
                    }
                    player->collisionFlagH |= 2;
                }
                break;

            case C_BOTTOM:
                if (self->velocity.y >= 0)
                    player->collisionFlagV |= 2;
                break;

            default: break;
        }
    }
}
#endif

void Crate_Update(void)
{
    RSDK_THIS(Crate);

    if (self->collision != PLATFORM_C_SOLID || !Crate_Collide()) {
        if (self->animator.animationID) {
            StateMachine_Run(self->state);
        }
        else {
            Platform_Update();
        }
    }
}

void Crate_LateUpdate(void) {}

void Crate_StaticUpdate(void) {}

void Crate_Draw(void)
{
    RSDK_THIS(Crate);
    RSDK.DrawSprite(&self->animator, &self->drawPos, false);
}

void Crate_Create(void *data)
{
    RSDK_THIS(Crate);

    int32 frameID = self->frameID;
    if (frameID == 3)
        self->collision = PLATFORM_C_NONE;
    else
        self->collision = PLATFORM_C_SOLID;

    Platform_Create(NULL);
    self->frameID = frameID;
#if defined RETRO_PS1_BUILD
    if (self->stateCollide == Platform_Collision_Solid)
        self->stateCollide = Crate_PS1Collision_Solid;
#endif

    if (!SceneInfo->inEditor) {
        self->hitbox.left   = -24;
        self->hitbox.top    = -24;
        self->hitbox.right  = 24;
        self->hitbox.bottom = 24;

        self->active    = ACTIVE_XBOUNDS;
        self->drawFX    = FX_SCALE | FX_FLIP;
        self->scale.x   = 0x200;
        self->scale.y   = 0x200;
        self->drawGroup = frameID == 3 ? Zone->objectDrawGroup[0] : Zone->objectDrawGroup[1];
        RSDK.SetSpriteAnimation(Crate->aniFrames, 0, &self->animator, true, frameID);
        self->state = Crate_State_None;
    }
}

void Crate_StageLoad(void)
{
    if (RSDK.CheckSceneFolder("PSZ1"))
        Crate->aniFrames = RSDK.LoadSpriteAnimation("PSZ1/Crate.bin", SCOPE_STAGE);

    Crate->sfxExplosion2 = RSDK.GetSfx("Stage/Explosion2.wav");
}

void Crate_Break(EntityCrate *create)
{
    for (int32 s = 0; s < 64; ++s) {
        // ice is used to create a shattering effect
        int32 x        = create->position.x + (RSDK.Rand(-24, 25) << 16);
        int32 y        = create->position.y + (RSDK.Rand(-24, 25) << 16);
        EntityIce *ice = CREATE_ENTITY(Ice, INT_TO_VOID(ICE_CHILD_SHARD), x, y);

        ice->velocity.x          = RSDK.Rand(-6, 8) << 15;
        ice->velocity.y          = RSDK.Rand(-10, 2) << 15;
        ice->direction           = RSDK.Rand(0, 4);
        ice->blockAnimator.speed = RSDK.Rand(1, 4);
        ice->drawGroup           = Zone->objectDrawGroup[0] + 1;

        switch (create->animator.frameID) {
            case 0:
            case 3: RSDK.SetSpriteAnimation(Crate->aniFrames, 1, &ice->blockAnimator, true, 0); break;

            case 1:
            case 2:
                if (RSDK.Rand(0, 6) >= 2)
                    RSDK.SetSpriteAnimation(Crate->aniFrames, 2, &ice->blockAnimator, true, 0);
                else
                    RSDK.SetSpriteAnimation(Crate->aniFrames, 3, &ice->blockAnimator, true, 0);
                break;

            default: break;
        }
    }

    RSDK.PlaySfx(Crate->sfxExplosion2, false, 255);
    create->position.y -= 0x10000;

    foreach_active(Crate, crate)
    {
        if (crate != create && crate->state == Crate_State_None
            && RSDK.CheckObjectCollisionTouchBox(create, &create->hitbox, crate, &crate->hitbox)) {
            crate->state = Crate_State_ApplyGravity;
        }
    }

    destroyEntity(create);
}
void Crate_MoveY(EntityCrate *self, int32 offset)
{
    self->drawPos.y += offset;
    self->centerPos.x = self->drawPos.x;
    self->centerPos.y = self->drawPos.y;
    self->position.x  = self->drawPos.x;
    self->position.y  = self->drawPos.y;

    int32 start = (self->drawPos.y - 0x300000) & 0xFFFF0000;
    foreach_active(Crate, crate)
    {
        if (crate != self && RSDK.CheckObjectCollisionBox(self, &self->hitbox, crate, &crate->hitbox, true) == C_TOP)
            Crate_MoveY(crate, start - crate->drawPos.y);
    }
}
bool32 Crate_Collide(void)
{
    RSDK_THIS(Crate);

    foreach_active(Player, player)
    {
        int32 storeX         = player->position.x;
        int32 storeY         = player->position.y;
        int32 storeXVel      = player->velocity.x;
        int32 storeYVel      = player->velocity.y;
        int32 storeVel       = player->groundVel;
        int32 storeGrounded  = player->onGround;
        Hitbox *playerHitbox = Player_GetHitbox(player);
#if defined RETRO_PS1_BUILD
        if (Crate_PS1Far(self, &self->hitbox, player, playerHitbox, true))
            continue; // (C_NONE both times: nothing written, the stores restored unchanged)
#endif

        int32 shieldAnim = RSDK_GET_ENTITY(Player->playerCount + RSDK.GetEntitySlot(player), Shield)->shieldAnimator.animationID;

        switch (MathHelpers_CheckBoxCollision(self, &self->hitbox, player, playerHitbox)) {
            default:
            case C_NONE:
                player->velocity.x = storeXVel;
                player->velocity.y = storeYVel;
                player->position.x = storeX;
                player->position.y = storeY;
                Player_CheckCollisionBox(player, self, &self->hitbox);
                break;

            case C_TOP:
                if (self->frameID != 1) {
                    player->velocity.x = storeXVel;
                    player->velocity.y = storeYVel;
                    player->position.x = storeX;
                    player->position.y = storeY;
                    Player_CheckCollisionBox(player, self, &self->hitbox);
                    break;
                }
#if MANIA_USE_PLUS
                else if (player->state == Player_State_MightyHammerDrop) {
                    player->velocity.y -= 0x10000;
                    Crate_Break(self);
                    player->velocity.x = storeXVel;
                    player->velocity.y = storeYVel;
                    player->position.x = storeX;
                    player->position.y = storeY;
                    player->groundVel  = storeVel;
                    player->onGround   = storeGrounded;
                    foreach_return true;
                }
#endif
                else if (player->shield == SHIELD_BUBBLE && player->invincibleTimer <= 0) {
                    if (shieldAnim == 8 && player->velocity.y >= 0x80000) {
                        Crate_Break(self);
                        player->velocity.x = storeXVel;
                        player->velocity.y = storeYVel;
                        player->position.x = storeX;
                        player->position.y = storeY;
                        player->groundVel  = storeVel;
                        player->onGround   = storeGrounded;
                        foreach_return true;
                    }
                }

                player->velocity.x = storeXVel;
                player->velocity.y = storeYVel;
                player->position.x = storeX;
                player->position.y = storeY;
                Player_CheckCollisionBox(player, self, &self->hitbox);
                break;

            case C_LEFT:
            case C_RIGHT:
                if (self->frameID != 1 || player->shield != SHIELD_FIRE || player->invincibleTimer > 0 || shieldAnim != 2) {
                    player->velocity.x = storeXVel;
                    player->velocity.y = storeYVel;
                    player->position.x = storeX;
                    player->position.y = storeY;
                    Player_CheckCollisionBox(player, self, &self->hitbox);
                }
                else if (player->position.x < self->position.x) {
                    player->velocity.x = storeXVel;
                    player->velocity.y = storeYVel;
                    player->position.x = storeX;
                    player->position.y = storeY;

                    if (player->velocity.x >= 0x78000) {
                        Crate_Break(self);
                        player->groundVel = storeVel;
                        player->onGround  = storeGrounded;
                        foreach_return true;
                    }
                    else {
                        Player_CheckCollisionBox(player, self, &self->hitbox);
                    }
                }
                else {
                    player->velocity.x = storeXVel;
                    player->velocity.y = storeYVel;
                    player->position.x = storeX;
                    player->position.y = storeY;

                    if (player->velocity.x <= -0x78000) {
                        Crate_Break(self);

                        player->groundVel = storeVel;
                        player->onGround  = storeGrounded;
                        foreach_return true;
                    }
                    else {
                        Player_CheckCollisionBox(player, self, &self->hitbox);
                    }
                }
                break;

            case C_BOTTOM:
                if (self->collisionOffset.y >= 0)
                    player->collisionFlagV |= 2;

                player->velocity.x = storeXVel;
                player->velocity.y = storeYVel;
                player->position.x = storeX;
                player->position.y = storeY;
                Player_CheckCollisionBox(player, self, &self->hitbox);
                break;
        }
    }

    return false;
}
void Crate_State_None(void)
{
    // hehe
}
void Crate_State_ApplyGravity(void)
{
    RSDK_THIS(Crate);

    self->position.y -= 0x10000;

    foreach_active(Crate, crate)
    {
        if (crate != self && crate->state == Crate_State_None && RSDK.CheckObjectCollisionTouchBox(self, &self->hitbox, crate, &crate->hitbox)) {
            crate->state = Crate_State_ApplyGravity;
        }
    }

    self->position.y += 0x10000;
    self->timer = 15;
    self->state = Crate_State_WaitToFall;
}
void Crate_State_WaitToFall(void)
{
    RSDK_THIS(Crate);

    if (--self->timer <= 0)
        self->state = Crate_State_Fall;
}
void Crate_State_Fall(void)
{
    RSDK_THIS(Crate);

    self->drawPos.y += self->velocity.y;

    int32 x = self->position.x;
    int32 y = self->position.y;

    self->velocity.y += 0x3800;
    self->position.x = self->drawPos.x;
    self->position.y = self->drawPos.y;

    if (RSDK.ObjectTileCollision(self, Zone->collisionLayers, 0, CMODE_FLOOR, 0, 0x180000, true)) {
        self->velocity.y = 0;
        self->state      = Crate_State_None;
    }
    else {
        foreach_active(Crate, crate)
        {
            if (crate != self && !crate->velocity.y && RSDK.CheckObjectCollisionBox(crate, &crate->hitbox, self, &self->hitbox, true) == C_TOP) {
                self->velocity.y = 0;
                self->state      = Crate_State_None;
            }
        }
    }

    self->drawPos.x   = self->position.x;
    self->drawPos.y   = self->position.y;
    self->centerPos.x = self->position.x;
    self->centerPos.y = self->position.y;

    self->position.x = x;
    self->position.y = y;
}

#if GAME_INCLUDE_EDITOR
void Crate_EditorDraw(void)
{
    RSDK_THIS(Crate);

    RSDK.SetSpriteAnimation(Crate->aniFrames, 0, &self->animator, true, self->frameID);
    self->drawPos = self->position;

    Crate_Draw();

    if (showGizmos()) {
        RSDK_DRAWING_OVERLAY(true);

        for (int32 s = SceneInfo->entitySlot + 1, i = 0; i < self->childCount; ++i) {
            Entity *child = RSDK_GET_ENTITY_GEN(s + i);
            if (!child)
                continue;

            DrawHelpers_DrawArrow(self->position.x, self->position.y, child->position.x, child->position.y, 0xE0E0E0, INK_NONE, 0xFF);
        }

        RSDK_DRAWING_OVERLAY(false);
    }
}

void Crate_EditorLoad(void)
{
    Crate->aniFrames = RSDK.LoadSpriteAnimation("PSZ1/Crate.bin", SCOPE_STAGE);

    RSDK_ACTIVE_VAR(Crate, frameID);
    RSDK_ENUM_VAR("Blue", CRATE_FRAME_BLUE);
    RSDK_ENUM_VAR("Orange (Broken)", CRATE_FRAME_ORANGE_BROKEN);
    RSDK_ENUM_VAR("Orange", CRATE_FRAME_ORANGE);
    RSDK_ENUM_VAR("Blue (BG)", CRATE_FRAME_BLUE_BG);
}
#endif

void Crate_Serialize(void)
{
    RSDK_EDITABLE_VAR(Crate, VAR_UINT8, frameID);
    RSDK_EDITABLE_VAR(Crate, VAR_ENUM, childCount);
    RSDK_EDITABLE_VAR(Crate, VAR_BOOL, ignoreItemBox);
}
