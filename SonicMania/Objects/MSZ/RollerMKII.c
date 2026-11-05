// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: RollerMKII Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectRollerMKII *RollerMKII;

void RollerMKII_Update(void)
{
    RSDK_THIS(RollerMKII);

    StateMachine_Run(self->state);
}

void RollerMKII_LateUpdate(void) {}

void RollerMKII_StaticUpdate(void) {}

void RollerMKII_Draw(void)
{
    RSDK_THIS(RollerMKII);

    RSDK.DrawSprite(&self->animator, NULL, false);
}

void RollerMKII_Create(void *data)
{
    RSDK_THIS(RollerMKII);

    self->visible         = true;
    self->drawGroup       = Zone->objectDrawGroup[0];
    self->startPos        = self->position;
    self->startDir        = self->direction;
    self->drawFX          = FX_FLIP;
    self->active          = ACTIVE_BOUNDS;
    self->updateRange.x   = 0x1000000;
    self->updateRange.y   = 0x1000000;
    self->onGround        = false;
    self->tileCollisions  = TILECOLLISION_DOWN;
    self->collisionLayers = Zone->collisionLayers;
    self->collisionPlane  = 0;
    RSDK.SetSpriteAnimation(RollerMKII->aniFrames, 0, &self->animator, true, 5);
    self->state = RollerMKII_State_Init;
}

void RollerMKII_StageLoad(void)
{
    if (RSDK.CheckSceneFolder("MSZ"))
        RollerMKII->aniFrames = RSDK.LoadSpriteAnimation("MSZ/RollerMKII.bin", SCOPE_STAGE);

    RollerMKII->hitboxOuter_Rolling.left   = -9;
    RollerMKII->hitboxOuter_Rolling.top    = -14;
    RollerMKII->hitboxOuter_Rolling.right  = 9;
    RollerMKII->hitboxOuter_Rolling.bottom = 14;

    RollerMKII->hitboxInner_Rolling.left   = -8;
    RollerMKII->hitboxInner_Rolling.top    = -14;
    RollerMKII->hitboxInner_Rolling.right  = 8;
    RollerMKII->hitboxInner_Rolling.bottom = 14;

    RollerMKII->hitboxOuter_Idle.left   = -9;
    RollerMKII->hitboxOuter_Idle.top    = -14;
    RollerMKII->hitboxOuter_Idle.right  = 9;
    RollerMKII->hitboxOuter_Idle.bottom = 24;

    RollerMKII->hitboxInner_Idle.left   = -8;
    RollerMKII->hitboxInner_Idle.top    = -14;
    RollerMKII->hitboxInner_Idle.right  = 8;
    RollerMKII->hitboxInner_Idle.bottom = 24;

    // Hitbox for interacting with player
    RollerMKII->hitboxBadnik.left   = -14;
    RollerMKII->hitboxBadnik.top    = -14;
    RollerMKII->hitboxBadnik.right  = 14;
    RollerMKII->hitboxBadnik.bottom = 14;

    // Hitbox for interacting with other objects
    // It's.... the same... as above???
    RollerMKII->hitboxObject.left   = -14;
    RollerMKII->hitboxObject.top    = -14;
    RollerMKII->hitboxObject.right  = 14;
    RollerMKII->hitboxObject.bottom = 14;

    RollerMKII->sfxBumper   = RSDK.GetSfx("Stage/Bumper3.wav");
    RollerMKII->sfxJump     = RSDK.GetSfx("Stage/Jump2.wav");
    RollerMKII->sfxDropDash = RSDK.GetSfx("Global/DropDash.wav");
    RollerMKII->sfxRelease  = RSDK.GetSfx("Global/Release.wav");
    RollerMKII->sfxSkidding = RSDK.GetSfx("Global/Skidding.wav");

    DEBUGMODE_ADD_OBJ(RollerMKII);
}

void RollerMKII_DebugSpawn(void)
{
    RSDK_THIS(DebugMode);

    EntityRollerMKII *rollerMKII = CREATE_ENTITY(RollerMKII, NULL, self->position.x, self->position.y);
    rollerMKII->direction        = self->direction;
    rollerMKII->startDir         = self->direction;
}

void RollerMKII_DebugDraw(void)
{
    RSDK.SetSpriteAnimation(RollerMKII->aniFrames, 0, &DebugMode->animator, true, 1);
    RSDK.DrawSprite(&DebugMode->animator, NULL, false);
}

void RollerMKII_CheckOffScreen(void)
{
    RSDK_THIS(RollerMKII);

    if (!RSDK.CheckOnScreen(self, NULL) && !RSDK.CheckPosOnScreen(&self->startPos, &self->updateRange)) {
        self->position  = self->startPos;
        self->direction = self->startDir;
        RollerMKII_Create(NULL);
    }
}

void RollerMKII_CheckPlayerCollisions(void)
{
    RSDK_THIS(RollerMKII);

    foreach_active(Player, player)
    {
        if (Player_CheckBadnikTouch(player, self, &RollerMKII->hitboxBadnik))
            Player_CheckBadnikBreak(player, self, true);
    }
}

void RollerMKII_CheckPlayerCollisions_Rolling(void)
{
    RSDK_THIS(RollerMKII);

    foreach_active(Player, player)
    {
        if (Player_CheckBadnikTouch(player, self, &RollerMKII->hitboxBadnik)) {

            int32 anim        = player->animator.animationID;
            bool32 shouldBump = false;

#if MANIA_USE_PLUS
            if (player->state == Player_State_MightyHammerDrop) {
                Player_CheckBadnikBreak(player, self, true);
            }
            else {
                shouldBump = player->characterID == ID_MIGHTY && (anim == ANI_CROUCH || player->jumpAbilityState > 1);
#endif
                if (Player_CheckAttacking(player, self) || shouldBump) {
                    RSDK.PlaySfx(RollerMKII->sfxBumper, false, 255);
                    int32 angle = RSDK.ATan2(player->position.x - self->position.x, player->position.y - self->position.y);
                    int32 velX  = 0x380 * RSDK.Cos256(angle);
                    int32 velY  = 0x380 * RSDK.Sin256(angle);

                    if (anim != ANI_FLY && anim != ANI_FLY_LIFT_TIRED) {
                        if (player->state != Player_State_TailsFlight) {
                            if (player->state != Player_State_DropDash)
                                player->state = Player_State_Air;
                            if (anim != ANI_JUMP && anim != ANI_DASH)
                                player->animator.animationID = ANI_WALK;
                        }
                    }

#if MANIA_USE_PLUS
                    if (player->characterID != ID_MIGHTY || player->jumpAbilityState <= 1) {
#endif
                        player->velocity.x   = velX;
                        player->groundVel    = velX;
                        player->applyJumpCap = false;
                        if (player->characterID == ID_KNUCKLES && player->animator.animationID == ANI_GLIDE) {
                            RSDK.SetSpriteAnimation(player->aniFrames, ANI_GLIDE_DROP, &player->animator, false, 0);
                            player->state = Player_State_KnuxGlideDrop;
                        }
                        player->velocity.y     = velY;
                        player->onGround       = false;
                        player->tileCollisions = TILECOLLISION_DOWN;
#if MANIA_USE_PLUS
                    }
#endif
                    RSDK.PlaySfx(RollerMKII->sfxBumper, false, 255);
                    RSDK.SetSpriteAnimation(RollerMKII->aniFrames, 2, &self->animator, true, 0);
                    self->velocity.y    = -0x40000;
                    self->velocity.x    = -velX;
                    self->onGround      = false;
                    self->touchedGround = 0;
                    self->timer         = 45;
                    self->state         = RollerMKII_State_Bumped;
                    self->direction     = player->position.x < self->position.x;
                }
                else {
                    Player_Hurt(player, self);
                }
#if MANIA_USE_PLUS
            }
#endif
        }
    }
}

int32 RollerMKII_HandleObjectCollisions(Entity *otherEntity, Hitbox *hitbox)
{
    RSDK_THIS(RollerMKII);

    int32 velX = self->velocity.x;
    int32 side = RSDK.CheckObjectCollisionBox(otherEntity, hitbox, self, &RollerMKII->hitboxObject, true);

    if ((side == C_LEFT && velX > 0) || (side == C_RIGHT && velX < 0)) {
        if (self->state != RollerMKII_State_Bumped) {
            RSDK.PlaySfx(RollerMKII->sfxBumper, false, 255);
            RSDK.SetSpriteAnimation(RollerMKII->aniFrames, 0, &self->animator, true, 0);
            self->velocity.y    = -0x40000;
            self->onGround      = false;
            self->touchedGround = 0;
            self->timer         = 30;
            self->velocity.x    = velX < 0 ? 0x20000 : -0x20000;
            self->state         = RollerMKII_State_Bumped;
            self->direction     = self->position.x < Player_GetNearestPlayer()->position.x;
        }
    }
    return side;
}

bool32 RollerMKII_HandlePlatformCollisions(EntityPlatform *platform)
{
    RSDK_THIS(RollerMKII);
    bool32 collided = false;

    if (platform->state != Platform_State_Falling2 && platform->state != Platform_State_Hold) {
        platform->position.x = platform->drawPos.x - platform->collisionOffset.x;
        platform->position.y = platform->drawPos.y - platform->collisionOffset.y;
        if (platform->collision) {
            if (platform->collision != PLATFORM_C_SOLID) {
                if (platform->collision == PLATFORM_C_TILED
                    && RSDK.CheckObjectCollisionTouchBox(platform, &platform->hitbox, self, &RollerMKII->hitboxObject)) {
                    if (self->collisionLayers & Zone->moveLayerMask) {
                        TileLayer *move  = RSDK.GetTileLayer(Zone->moveLayer);
                        move->position.x = -(platform->drawPos.x + platform->tileOrigin.x) >> 16;
                        move->position.y = -(platform->drawPos.y + platform->tileOrigin.y) >> 16;
                    }

                    if (self->velocity.y >= 0x3800)
                        collided = true;
                }
            }
            else {
                Hitbox *hitbox = RSDK.GetHitbox(&platform->animator, 1);
                collided       = RollerMKII_HandleObjectCollisions((Entity *)platform, hitbox);
            }
        }
        else {
            Hitbox *hitbox = RSDK.GetHitbox(&platform->animator, 0);
            RSDK.CheckObjectCollisionPlatform(platform, hitbox, self, &RollerMKII->hitboxObject, true);
        }

        platform->position = platform->centerPos;
    }

    return collided;
}

#if defined RETRO_PS1_BUILD
// P8 (docs/perf/57): RollerMKII_HandleCollisions walked every PlaneSwitch, Platform, Spikes and BreakableWall of the act
// for each roller each frame (MSZ2: ~290 checks a roller, 102 hblanks at x 1792, 43 of them the slot-range walks). The
// same objects in the same order (slot lists as P6's PS1ClassList, foreach_all when one overflows), and an object's
// check is skipped only where it cannot touch the roller, so nothing it writes changes:
// - CheckObjectCollisionBox and TouchBox need the hitboxes to overlap on both axes, CheckObjectCollisionPlatform on x and
//   the roller not above the platform's top (both TILECOLLISION variants): past both half-extents + 2 px (whole pixels,
//   FROM_FIXED as the engine; the +2 covers Box's +-1 edges) they return C_NONE / false and write nothing;
// - PlaneSwitch_CheckCollisions acts only when the roller, turned about the switch, is within 24 px across and size * 8
//   px along it; the turn scales a length by sqrt(Sin256^2 + Cos256^2) / 256 >= 0.98 (the tables' smallest sum of
//   squares is 64,962), so a roller more than 2 * (24 + 8 * size) + 4 px away on an axis cannot be inside; used only
//   below 32,000 px of |dx| + |dy|, where stock's products cannot wrap;
// - a Platform skipped still gets stock's two position writes, which end at `position = centerPos`.
typedef struct {
    unsigned int adds;
    int count;
    unsigned short slot[0x80];
} PS1RollerList;
static PS1RollerList ps1RollerSwitches, ps1RollerPlatforms, ps1RollerSpikes, ps1RollerWalls;

// (abs is a libc call in this build)
static inline __attribute__((always_inline)) int32 RollerMKII_PS1Abs(int32 v) { return v < 0 ? -v : v; }

static inline __attribute__((always_inline)) int32 RollerMKII_PS1Ext(int32 a, int32 b)
{
    a = RollerMKII_PS1Abs(a), b = RollerMKII_PS1Abs(b);
    return a > b ? a : b;
}
// the x / y reach (px) at which the hitboxes h (object) and the roller's can no longer overlap
static inline __attribute__((always_inline)) int32 RollerMKII_PS1ReachX(Hitbox *h)
{
    return RollerMKII_PS1Ext(h->left, h->right) + RollerMKII_PS1Ext(RollerMKII->hitboxObject.left, RollerMKII->hitboxObject.right) + 2;
}
static inline __attribute__((always_inline)) int32 RollerMKII_PS1ReachY(Hitbox *h)
{
    return RollerMKII_PS1Ext(h->top, h->bottom) + RollerMKII_PS1Ext(RollerMKII->hitboxObject.top, RollerMKII->hitboxObject.bottom) + 2;
}
// Box / TouchBox: true when they cannot collide
static inline __attribute__((always_inline)) bool32 RollerMKII_PS1Apart(EntityRollerMKII *self, int32 ox, int32 oy, Hitbox *h)
{
    return RollerMKII_PS1Abs((self->position.x >> 16) - (ox >> 16)) > RollerMKII_PS1ReachX(h)
           || RollerMKII_PS1Abs((self->position.y >> 16) - (oy >> 16)) > RollerMKII_PS1ReachY(h);
}
static void RollerMKII_PS1Platform(EntityRollerMKII *self, EntityPlatform *platform)
{
    if (platform->state == Platform_State_Falling2 || platform->state == Platform_State_Hold)
        return;
    Hitbox *h = NULL;
    switch (platform->collision) {
        case PLATFORM_C_PLATFORM: h = RSDK.GetHitbox(&platform->animator, 0); break;
        case PLATFORM_C_SOLID: h = RSDK.GetHitbox(&platform->animator, 1); break;
        case PLATFORM_C_TILED: h = &platform->hitbox; break;
        default: break; // no check, only the position writes
    }
    if (h) { // (no hitbox: stock's check returns C_NONE / false)
        int32 px = platform->drawPos.x - platform->collisionOffset.x, py = platform->drawPos.y - platform->collisionOffset.y;
        bool32 apart;
        if (platform->collision == PLATFORM_C_PLATFORM)
            apart = RollerMKII_PS1Abs((self->position.x >> 16) - (px >> 16)) > RollerMKII_PS1ReachX(h)
                    || (self->position.y >> 16) - (py >> 16) < -RollerMKII_PS1ReachY(h);
        else
            apart = RollerMKII_PS1Apart(self, px, py, h);
        if (!apart) {
            RollerMKII_HandlePlatformCollisions(platform);
            return;
        }
    }
    platform->position = platform->centerPos;
}
static inline __attribute__((always_inline)) bool32 RollerMKII_PS1SwitchFar(EntityRollerMKII *self, EntityPlaneSwitch *ps)
{
    int32 ax = RollerMKII_PS1Abs((self->position.x >> 16) - (ps->position.x >> 16)), ay = RollerMKII_PS1Abs((self->position.y >> 16) - (ps->position.y >> 16));
    int32 lim = 2 * (24 + 8 * ps->size) + 4;
    return ax + ay < 32000 && (ax > lim || ay > lim);
}
#endif

void RollerMKII_HandleCollisions(void)
{
    RSDK_THIS(RollerMKII);

#if defined RETRO_PS1_BUILD
    PS1_CLASS_LIST_SYNC(ps1RollerSwitches, PlaneSwitch);
    PS1_CLASS_LIST_SYNC(ps1RollerPlatforms, Platform);
    PS1_CLASS_LIST_SYNC(ps1RollerSpikes, Spikes);
    PS1_CLASS_LIST_SYNC(ps1RollerWalls, BreakableWall);
    if (ps1RollerSwitches.count >= 0) {
        PS1_FOREACH_LISTED(ps1RollerSwitches, PlaneSwitch, planeSwitch)
        {
            if (!RollerMKII_PS1SwitchFar(self, planeSwitch))
                PlaneSwitch_CheckCollisions(planeSwitch, self, planeSwitch->flags, planeSwitch->size, true, Zone->playerDrawGroup[0],
                                            Zone->playerDrawGroup[1]);
        }
    }
    else {
        foreach_all(PlaneSwitch, planeSwitch)
        {
            PlaneSwitch_CheckCollisions(planeSwitch, self, planeSwitch->flags, planeSwitch->size, true, Zone->playerDrawGroup[0],
                                        Zone->playerDrawGroup[1]);
        }
    }
    if (ps1RollerPlatforms.count >= 0) {
        PS1_FOREACH_LISTED(ps1RollerPlatforms, Platform, platform) { RollerMKII_PS1Platform(self, platform); }
    }
    else {
        foreach_all(Platform, platform) { RollerMKII_HandlePlatformCollisions(platform); }
    }
    if (ps1RollerSpikes.count >= 0) {
        PS1_FOREACH_LISTED(ps1RollerSpikes, Spikes, spikes)
        {
            if (!RollerMKII_PS1Apart(self, spikes->position.x, spikes->position.y, &spikes->hitbox))
                RollerMKII_HandleObjectCollisions((Entity *)spikes, &spikes->hitbox);
        }
    }
    else {
        foreach_all(Spikes, spikes) { RollerMKII_HandleObjectCollisions((Entity *)spikes, &spikes->hitbox); }
    }
    if (ps1RollerWalls.count >= 0) {
        PS1_FOREACH_LISTED(ps1RollerWalls, BreakableWall, wall)
        {
            if (!RollerMKII_PS1Apart(self, wall->position.x, wall->position.y, &wall->hitbox))
                RollerMKII_HandleObjectCollisions((Entity *)wall, &wall->hitbox);
        }
    }
    else {
        foreach_all(BreakableWall, wall) { RollerMKII_HandleObjectCollisions((Entity *)wall, &wall->hitbox); }
    }
#else
    foreach_all(PlaneSwitch, planeSwitch)
    {
        PlaneSwitch_CheckCollisions(planeSwitch, self, planeSwitch->flags, planeSwitch->size, true, Zone->playerDrawGroup[0],
                                    Zone->playerDrawGroup[1]);
    }

    foreach_all(Platform, platform) { RollerMKII_HandlePlatformCollisions(platform); }
    foreach_all(Spikes, spikes) { RollerMKII_HandleObjectCollisions((Entity *)spikes, &spikes->hitbox); }
    foreach_all(BreakableWall, wall) { RollerMKII_HandleObjectCollisions((Entity *)wall, &wall->hitbox); }
#endif

    if (!self->collisionMode && self->state != RollerMKII_State_Bumped) {
        bool32 collided = self->direction == FLIP_X
                              ? RSDK.ObjectTileCollision(self, Zone->collisionLayers, CMODE_RWALL, self->collisionPlane, -0xA0000, 0, false)
                              : RSDK.ObjectTileCollision(self, Zone->collisionLayers, CMODE_LWALL, self->collisionPlane, 0xA0000, 0, false);
        if (collided) {
            RSDK.PlaySfx(RollerMKII->sfxBumper, false, 255);
            RSDK.SetSpriteAnimation(RollerMKII->aniFrames, 0, &self->animator, true, 0);
            self->onGround      = false;
            self->velocity.x    = self->direction == FLIP_X ? 0x20000 : -0x20000;
            self->velocity.y    = -0x40000;
            self->touchedGround = 0;
            self->timer         = 30;
            self->state         = RollerMKII_State_Bumped;
            self->direction     = self->position.x < Player_GetNearestPlayer()->position.x;
        }
    }
}

void RollerMKII_State_Init(void)
{
    RSDK_THIS(RollerMKII);

    self->active        = ACTIVE_NORMAL;
    self->velocity.x    = 0;
    self->velocity.y    = 0;
    self->groundVel     = 0;
    self->touchedGround = 0;

    self->state = RollerMKII_State_Idle;
    RollerMKII_State_Idle();
}

void RollerMKII_State_Idle(void)
{
    RSDK_THIS(RollerMKII);

    RSDK.ProcessAnimation(&self->animator);

    Vector2 range = { 0x200000, 0x200000 };

    if (self->timer) {
        self->timer--;
    }
    else if (RSDK.CheckOnScreen(self, &range)) {
        bool32 wallCollided = false;
        if (self->direction == FLIP_X)
            wallCollided = RSDK.ObjectTileCollision(self, Zone->collisionLayers, CMODE_RWALL, self->collisionPlane, -0x180000, 0, false);
        else
            wallCollided = RSDK.ObjectTileCollision(self, Zone->collisionLayers, CMODE_LWALL, self->collisionPlane, 0x180000, 0, false);

        if (wallCollided)
            self->direction ^= FLIP_X;

        foreach_active(Player, player)
        {
            if (self->direction) {
                if (player->position.x < self->position.x) {
                    if (self->position.x - player->position.x < 0x800000) {
                        self->velocity.y = -0x40000;
                        RSDK.SetSpriteAnimation(RollerMKII->aniFrames, 1, &self->animator, true, 0);

                        self->playerPtr = player;
                        RSDK.PlaySfx(RollerMKII->sfxJump, false, 0xFF);

                        self->state = RollerMKII_State_SpinUp;
                    }
                }
            }
            else {
                if (player->position.x > self->position.x) {
                    if (player->position.x - self->position.x < 0x800000) {
                        self->velocity.y = -0x40000;
                        RSDK.SetSpriteAnimation(RollerMKII->aniFrames, 1, &self->animator, true, 0);

                        self->playerPtr = player;
                        RSDK.PlaySfx(RollerMKII->sfxJump, false, 0xFF);

                        self->state = RollerMKII_State_SpinUp;
                    }
                }
            }
        }
    }

    RollerMKII_CheckPlayerCollisions();
    RollerMKII_CheckOffScreen();
}

void RollerMKII_State_SpinUp(void)
{
    RSDK_THIS(RollerMKII);

    self->position.x += self->velocity.x;
    self->position.y += self->velocity.y;
    self->velocity.y += 0x3800;

    RSDK.ProcessAnimation(&self->animator);

    if (self->velocity.y > 0) {
        self->timer = 15;
        RSDK.PlaySfx(RollerMKII->sfxDropDash, false, 255);
        self->state = RollerMKII_State_RollDelay;
    }

    if (self->animator.frameID <= 4)
        RollerMKII_CheckPlayerCollisions();
    else
        RollerMKII_CheckPlayerCollisions_Rolling();

    RollerMKII_CheckOffScreen();
}

void RollerMKII_State_RollDelay(void)
{
    RSDK_THIS(RollerMKII);

    RSDK.ProcessAnimation(&self->animator);

    if (!--self->timer)
        self->state = RollerMKII_State_Rolling_Air;

    RollerMKII_CheckPlayerCollisions_Rolling();
    RollerMKII_CheckOffScreen();
}

void RollerMKII_State_Rolling_Air(void)
{
    RSDK_THIS(RollerMKII);

    if (!self->touchedGround) {
        if (self->position.x <= self->playerPtr->position.x) {
            self->direction = FLIP_NONE;

            if (self->velocity.x < 0x80000)
                self->velocity.x += 0x3800;
        }
        else {
            self->direction = FLIP_X;

            if (self->velocity.x > -0x80000)
                self->velocity.x -= 0x3800;
        }
    }

    RSDK.ProcessObjectMovement(self, &RollerMKII->hitboxOuter_Rolling, &RollerMKII->hitboxInner_Rolling);

    self->velocity.y += 0x3800;

    RollerMKII_HandleCollisions();

    if (self->onGround) {
        if (abs(self->angle) & 0x60) {
            self->state = RollerMKII_State_Rolling_Ground;
        }
        else {
            if (self->velocity.x)
                self->groundVel = self->velocity.x;

            if (!self->touchedGround) {
                self->touchedGround = true;

                if (self->direction == FLIP_NONE) {
                    self->groundVel  = 0x80000;
                    self->velocity.x = 0x80000;
                }
                else {
                    self->groundVel  = -0x80000;
                    self->velocity.x = -0x80000;
                }
                self->velocity.y = -0x20000;

                self->onGround = false;
                RSDK.StopSfx(RollerMKII->sfxDropDash);
                RSDK.PlaySfx(RollerMKII->sfxRelease, false, 255);
                Hitbox *hitbox = RSDK.GetHitbox(&self->animator, 0);

                EntityDust *dust = CREATE_ENTITY(Dust, self, self->position.x, self->position.y + 0xE0000);
                RSDK.SetSpriteAnimation(Dust->aniFrames, 2, &dust->animator, true, 0);
                dust->state = Dust_State_DustPuff;
                dust->position.y += hitbox->bottom << 16;
                dust->direction = self->direction;
                dust->drawGroup = self->drawGroup;
                dust->position.x += self->direction ? 0x90000 : -0x90000;
            }
            else if (self->velocity.y <= 0x10000) {
                self->state = RollerMKII_State_Rolling_Ground;
            }
            else {
                self->velocity.y = -0x20000;
                self->onGround   = false;
            }
        }

        self->direction = self->groundVel < 0;
    }

    RSDK.ProcessAnimation(&self->animator);

    RollerMKII_CheckPlayerCollisions_Rolling();
    RollerMKII_CheckOffScreen();
}

void RollerMKII_State_Rolling_Ground(void)
{
    RSDK_THIS(RollerMKII);

    if (!self->collisionMode) {
        if (self->position.x <= self->playerPtr->position.x) {
            self->direction = FLIP_NONE;

            if (self->groundVel < 0x80000) {
                self->groundVel += 0x3800;

                if (self->groundVel > 0) {
                    self->groundVel += 0x1400;

                    if (self->groundVel >= -0x40000) {
                        self->timer = 0;
                    }
                    else {
                        RSDK.PlaySfx(RollerMKII->sfxSkidding, false, 255);
                        self->timer = (self->timer + 1) & 3;
                        if (!self->timer)
                            CREATE_ENTITY(Dust, NULL, self->position.x, self->position.y + 0xE0000)->state = Dust_State_DustPuff;
                    }
                }
            }
        }
        else {
            self->direction = FLIP_X;

            if (self->groundVel > -0x80000) {
                self->groundVel -= 0x3800;

                if (self->groundVel > 0) {
                    self->groundVel += 0x1400;

                    if (self->groundVel <= 0x40000) {
                        self->timer = 0;
                    }
                    else {
                        RSDK.PlaySfx(RollerMKII->sfxSkidding, false, 255);
                        self->timer = (self->timer + 1) & 3;
                        if (!self->timer)
                            CREATE_ENTITY(Dust, NULL, self->position.x, self->position.y + 0xE0000)->state = Dust_State_DustPuff;
                    }
                }
            }
        }
    }

    RSDK.ProcessObjectMovement(self, &RollerMKII->hitboxOuter_Rolling, &RollerMKII->hitboxInner_Rolling);

    self->groundVel += (RSDK.Sin256(self->angle) << 13 >> 8);
    if (self->collisionMode != CMODE_FLOOR) {
        if (self->angle >= 0x40 && self->angle <= 0xC0 && self->groundVel <= 0x20000) {
            self->onGround      = false;
            self->angle         = 0;
            self->collisionMode = 0;
        }
    }

    RollerMKII_HandleCollisions();

    if (!self->onGround && self->state != RollerMKII_State_Bumped)
        self->state = RollerMKII_State_Rolling_Air;

    RSDK.ProcessAnimation(&self->animator);

    RollerMKII_CheckPlayerCollisions_Rolling();
    RollerMKII_CheckOffScreen();
}

void RollerMKII_State_Bumped(void)
{
    RSDK_THIS(RollerMKII);

    RSDK.ProcessObjectMovement(self, &RollerMKII->hitboxOuter_Idle, &RollerMKII->hitboxInner_Idle);

    self->velocity.y += 0x3800;
    RollerMKII_HandleCollisions();

    if (self->onGround) {
        self->groundVel  = 0;
        self->velocity.x = 0;
        self->velocity.y = 0;
        self->state      = RollerMKII_State_Idle;
        RSDK.SetSpriteAnimation(RollerMKII->aniFrames, 0, &self->animator, true, 0);
    }

    RSDK.ProcessAnimation(&self->animator);
    RollerMKII_CheckOffScreen();
}

#if GAME_INCLUDE_EDITOR
void RollerMKII_EditorDraw(void) { RollerMKII_Draw(); }

void RollerMKII_EditorLoad(void)
{
    RollerMKII->aniFrames = RSDK.LoadSpriteAnimation("MSZ/RollerMKII.bin", SCOPE_STAGE);

    RSDK_ACTIVE_VAR(RollerMKII, direction);
    RSDK_ENUM_VAR("Right", FLIP_NONE);
    RSDK_ENUM_VAR("Left", FLIP_X);
}
#endif

void RollerMKII_Serialize(void) { RSDK_EDITABLE_VAR(RollerMKII, VAR_UINT8, direction); }
