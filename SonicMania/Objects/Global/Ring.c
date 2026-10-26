// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: Ring Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectRing *Ring;

void Ring_Update(void)
{
    RSDK_THIS(Ring);

    StateMachine_Run(self->state);
}

void Ring_LateUpdate(void) {}

void Ring_StaticUpdate(void) {}

void Ring_Draw(void)
{
    RSDK_THIS(Ring);

    StateMachine_Run(self->stateDraw);
}

void Ring_Create(void *data)
{
    RSDK_THIS(Ring);
    self->visible = true;

    self->drawGroup = Zone->objectDrawGroup[0] + 1;
    if (self->planeFilter > 0 && ((uint8)self->planeFilter - 1) & 2)
        self->drawGroup = Zone->objectDrawGroup[1] + 1;

    if (self->type == RING_TYPE_BIG) {
        self->drawFX |= FX_FLIP;
        self->ringAmount = 5;
    }

    if (!SceneInfo->inEditor) {
        if (!data) {
            RSDK.SetSpriteAnimation(Ring->aniFrames, self->type, &self->animator, true, 0);

            self->amplitude.x >>= 10;
            self->amplitude.y >>= 10;
            self->active = ACTIVE_BOUNDS;

            switch (self->moveType) {
                case RING_MOVE_LINEAR:
                    self->updateRange.x = (abs(self->amplitude.x) + 0x1000) << 10;
                    self->updateRange.y = (abs(self->amplitude.y) + 0x1000) << 10;
                    self->state         = Ring_State_Linear;
                    self->stateDraw     = Ring_Draw_Oscillating;
                    break;

                case RING_MOVE_CIRCLE:
                    self->updateRange.x = (abs(self->amplitude.x) + 0x1000) << 10;
                    self->updateRange.y = (abs(self->amplitude.y) + 0x1000) << 10;
                    self->state         = Ring_State_Circular;
                    self->stateDraw     = Ring_Draw_Oscillating;
                    break;

                case RING_MOVE_TRACK:
                    self->updateRange.x = (abs(self->amplitude.x) + 0x2000) << 9;
                    self->updateRange.y = (abs(self->amplitude.y) + 0x2000) << 9;
                    self->state         = Ring_State_Track;
                    self->stateDraw     = Ring_Draw_Oscillating;
                    break;

                case RING_MOVE_PATH:
                    self->updateRange.x = TO_FIXED(64);
                    self->updateRange.y = TO_FIXED(64);
                    self->active        = ACTIVE_NEVER;
                    self->state         = Ring_State_Path;
                    self->stateDraw     = Ring_Draw_Oscillating;
                    break;

                default:
                case RING_MOVE_FIXED:
                    self->updateRange.x = TO_FIXED(64);
                    self->updateRange.y = TO_FIXED(64);
                    self->state         = Ring_State_Normal;
                    self->stateDraw     = Ring_Draw_Normal;
                    break;
            }
        }
        else {
            self->active = ACTIVE_NORMAL;
            RSDK.SetSpriteAnimation(Ring->aniFrames, RING_TYPE_NORMAL, &self->animator, true, 0);
        }
    }
}

void Ring_StageLoad(void)
{
    Ring->aniFrames = RSDK.LoadSpriteAnimation("Global/Ring.bin", SCOPE_STAGE);

    Ring->hitbox.left   = -8;
    Ring->hitbox.top    = -8;
    Ring->hitbox.right  = 8;
    Ring->hitbox.bottom = 8;

    DEBUGMODE_ADD_OBJ(Ring);

    Ring->sfxRing = RSDK.GetSfx("Global/Ring.wav");
}

void Ring_DebugSpawn(void)
{
    RSDK_THIS(DebugMode);

    CREATE_ENTITY(Ring, NULL, self->position.x, self->position.y);
}

void Ring_DebugDraw(void)
{
    RSDK.SetSpriteAnimation(Ring->aniFrames, 0, &DebugMode->animator, true, 0);
    RSDK.DrawSprite(&DebugMode->animator, NULL, false);
}

void Ring_Collect(void)
{
    RSDK_THIS(Ring);

    int32 storeX = self->position.x;
    int32 storeY = self->position.y;

    if (self->drawPos.x)
        self->position = self->drawPos;

    foreach_active(Player, player)
    {
        if (Player_CheckCollisionTouch(player, self, &Ring->hitbox)) {
            if (!self->planeFilter || player->collisionPlane == (((uint8)self->planeFilter - 1) & 1)) {
                if (player->sidekick)
                    player = RSDK_GET_ENTITY(SLOT_PLAYER1, Player);

                int32 ringAmount = 1;
                if (self->type == RING_TYPE_BIG) {
                    player->ringExtraLife += 100 * (self->ringAmount / 100);
                    ringAmount = self->ringAmount;
                }
                Player_GiveRings(player, ringAmount, true);

                int32 max = TO_FIXED(8);
                if (self->type == RING_TYPE_BIG)
                    max = TO_FIXED(16);

                int32 cnt = 4 * (self->type == RING_TYPE_BIG) + 4;
                int32 min = -max;
                for (int32 i = 0; i < cnt; ++i) {
                    int32 x             = self->position.x + RSDK.Rand(min, max);
                    int32 y             = self->position.y + RSDK.Rand(min, max);
                    EntityRing *sparkle = CREATE_ENTITY(Ring, NULL, x, y);

                    sparkle->state     = Ring_State_Sparkle;
                    sparkle->stateDraw = Ring_Draw_Sparkle;
                    sparkle->active    = ACTIVE_NORMAL;
                    sparkle->visible   = false;
                    if (self->drawGroup == 1)
                        sparkle->drawGroup = 1;
                    else
                        sparkle->drawGroup = Zone->objectDrawGroup[1];

                    RSDK.SetSpriteAnimation(Ring->aniFrames, RING_TYPE_SPARKLE1 + (i % 3), &sparkle->animator, true, 0);
                    int32 frameCount = sparkle->animator.frameCount;
                    if (sparkle->animator.animationID == 2) {
                        sparkle->alpha = 0xE0;
                        frameCount >>= 1;
                    }
                    sparkle->maxFrameCount  = frameCount - 1;
                    sparkle->animator.speed = RSDK.Rand(6, 8);
                    sparkle->timer          = 2 * i++;
                }

                destroyEntity(self);
                self->active = ACTIVE_DISABLED; // not sure what the purpose of this is but sure
                foreach_return;
            }
        }
        else if (self->state != Ring_State_Attracted && player->shield == SHIELD_LIGHTNING
                 && RSDK.CheckObjectCollisionTouchCircle(self, TO_FIXED(80), player, TO_FIXED(1))) {
            self->drawPos.x    = 0;
            self->state        = Ring_State_Attracted;
            self->stateDraw    = Ring_Draw_Normal;
            self->active       = ACTIVE_NORMAL;
            self->storedPlayer = player;
            foreach_return;
        }
    }

    self->position.x = storeX;
    self->position.y = storeY;
}
void Ring_LoseRings(EntityPlayer *player, int32 rings, uint8 cPlane)
{
    RSDK_THIS(Player);

    int32 outerRingCount = CLAMP(rings, 0, 16);
    int32 innerRingCount = CLAMP(rings - 16, 0, 16);
    int32 bigRingCount   = CLAMP(rings - 32, 0, 16);

    int32 angleStart = 0xC0 - 8 * (innerRingCount & -2);
    int32 angle      = angleStart + 8;
    if (!(innerRingCount & 1))
        angle = angleStart - 8;

    for (int32 i = 0; i < innerRingCount; ++i) {
        EntityRing *ring     = CREATE_ENTITY(Ring, player, player->position.x, player->position.y);
        ring->velocity.x     = RSDK.Cos256(angle) << 9;
        ring->velocity.y     = RSDK.Sin256(angle) << 9;
        ring->animator.speed = 0x200;
        ring->collisionPlane = cPlane;
        ring->inkEffect      = INK_ALPHA;
        ring->alpha          = 0x100;
        ring->isPermanent    = true;
        ring->state          = Ring_State_Lost;
        ring->stateDraw      = Ring_Draw_Normal;
        ring->drawGroup      = self->drawGroup;
        ring->moveType       = RING_MOVE_LINEAR;
        angle += 0x10;
    }

    angleStart = 0xC0 - 8 * (outerRingCount & -2);
    angle      = angleStart + 8;
    if (!(outerRingCount & 1))
        angle = angleStart - 8;

    for (int32 i = 0; i < outerRingCount; ++i) {
        EntityRing *ring     = CREATE_ENTITY(Ring, player, player->position.x, player->position.y);
        ring->velocity.x     = RSDK.Cos256(angle) << 10;
        ring->velocity.y     = RSDK.Sin256(angle) << 10;
        ring->animator.speed = 512;
        ring->collisionPlane = cPlane;
        ring->inkEffect      = INK_ALPHA;
        ring->alpha          = 0x100;
        ring->isPermanent    = true;
        ring->state          = Ring_State_Lost;
        ring->stateDraw      = Ring_Draw_Normal;
        ring->drawGroup      = self->drawGroup;
        angle += 0x10;
    }

    angleStart = 0xC0 - 8 * (bigRingCount & -2);
    angle      = angleStart + 8;
    if (!(bigRingCount & 1))
        angle = angleStart - 8;

    for (int32 i = 0; i < bigRingCount; ++i) {
        EntityRing *ringGrow = CREATE_ENTITY(Ring, player, player->position.x, player->position.y);
        ringGrow->velocity.x = RSDK.Cos256(angle) << 11;
        ringGrow->velocity.y = RSDK.Sin256(angle) << 11;
        RSDK.SetSpriteAnimation(Ring->aniFrames, RING_TYPE_BIG, &ringGrow->animator, true, 0);
        ringGrow->scale.x        = 0x100;
        ringGrow->scale.y        = 0x100;
        ringGrow->animator.speed = 0x200;
        ringGrow->drawFX         = FX_FLIP | FX_ROTATE | FX_SCALE;
        ringGrow->state          = Ring_State_LostFX;
        ringGrow->stateDraw      = Ring_Draw_Normal;
        angle += 0x10;
    }
}
void Ring_LoseHyperRings(EntityPlayer *player, int32 rings, uint8 cPlane)
{
    int32 ringCount = CLAMP(rings >> 2, 1, 8);
    int32 ringValue = rings / ringCount;

    int32 startAngle = 0xC0 - 0x10 * (ringCount & 0xFFFFFFFE);
    int32 angle      = startAngle + 0x10;
    if (!(ringCount & 1))
        angle = startAngle - 0x10;

    for (int32 i = 0; i < ringCount; ++i) {
        EntityRing *hyperRing = CREATE_ENTITY(Ring, player, player->position.x, player->position.y);
        RSDK.SetSpriteAnimation(Ring->aniFrames, RING_TYPE_BIG, &hyperRing->animator, true, 0);
        hyperRing->type           = RING_TYPE_BIG;
        hyperRing->velocity.x     = 0x300 * RSDK.Cos256(angle);
        hyperRing->isPermanent    = true;
        hyperRing->drawFX         = FX_FLIP | FX_SCALE;
        hyperRing->alpha          = 0x100;
        hyperRing->velocity.y     = 0x300 * RSDK.Sin256(angle);
        hyperRing->animator.speed = 0x180;
        hyperRing->collisionPlane = cPlane;
        hyperRing->angle          = i << 6;
        hyperRing->ringAmount     = ringValue;
        hyperRing->state          = Ring_State_Big;
        hyperRing->stateDraw      = Ring_Draw_Normal;
        angle += 0x20;
    }
}
void Ring_FakeLoseRings(Vector2 *position, int32 ringCount, uint8 drawGroup)
{
    int32 ringCount2 = ringCount - 16;
    int32 ringCount3 = ringCount2 - 16;
    int32 radius     = 0x800;
    int32 scale      = 0x100;

    if (ringCount > 16) {
        ringCount = 16;
        radius    = 0x600;
        scale     = 0xC0;
    }

    if (ringCount2 <= 16) {
        if (ringCount2 < 0)
            ringCount2 = 0;
    }
    else {
        radius -= 0x200;
        ringCount2 = 16;
        scale -= 0x40;
    }

    ringCount3 = CLAMP(ringCount3, 0, 16);

    int32 angleVal = 0xC0 - 8 * (ringCount2 & 0xFFFFFFFE);
    int32 angle    = angleVal + 8;
    if (!(ringCount2 & 1))
        angle = angleVal - 8;

    for (int32 r = 0; r < ringCount2; ++r) {
        EntityRing *ring = CREATE_ENTITY(Ring, position, position->x, position->y);
        ring->velocity.x = radius * RSDK.Cos256(angle);
        ring->velocity.y = radius * RSDK.Sin256(angle);
        RSDK.SetSpriteAnimation(Ring->aniFrames, RING_TYPE_BIG, &ring->animator, true, 0);
        ring->animator.speed = 0x200;
        ring->scale.x        = scale;
        ring->scale.y        = scale;
        ring->drawFX         = FX_FLIP | FX_SCALE;
        ring->drawGroup      = drawGroup;
        ring->state          = Ring_State_LostFX;
        ring->stateDraw      = Ring_Draw_Normal;
        angle += 0x10;
    }

    angleVal = 0xC0 - 8 * (ringCount & 0xFFFFFFFE);
    angle    = angleVal + 8;
    if (!(ringCount & 1))
        angle = angleVal - 8;

    for (int32 r = 0; r < ringCount; ++r) {
        EntityRing *ring = CREATE_ENTITY(Ring, position, position->x, position->y);
        ring->velocity.x = (radius + 0x200) * RSDK.Cos256(angle);
        ring->velocity.y = (radius + 0x200) * RSDK.Sin256(angle);
        RSDK.SetSpriteAnimation(Ring->aniFrames, RING_TYPE_BIG, &ring->animator, true, 0);
        ring->animator.speed = 0x200;
        ring->scale.x        = scale + 0x40;
        ring->scale.y        = scale + 0x40;
        ring->drawFX         = FX_FLIP | FX_SCALE;
        ring->drawGroup      = drawGroup;
        ring->state          = Ring_State_LostFX;
        ring->stateDraw      = Ring_Draw_Normal;
        angle += 0x10;
    }

    angleVal = 0xC0 - 8 * (ringCount3 & 0xFFFFFFFE);
    angle    = angleVal + 8;
    if (!(ringCount3 & 1))
        angle = angleVal - 8;

    for (int32 r = 0; r < ringCount3; ++r) {
        EntityRing *ringGrow = CREATE_ENTITY(Ring, position, position->x, position->y);
        ringGrow->velocity.x = (radius + 0x400) * RSDK.Cos256(angle);
        ringGrow->velocity.y = (radius + 0x400) * RSDK.Sin256(angle);
        RSDK.SetSpriteAnimation(Ring->aniFrames, RING_TYPE_BIG, &ringGrow->animator, true, 0);
        ringGrow->animator.speed = 0x200;
        ringGrow->scale.x        = scale + 0x80;
        ringGrow->scale.y        = scale + 0x80;
        ringGrow->drawFX         = FX_FLIP | FX_SCALE;
        ringGrow->drawGroup      = drawGroup;
        ringGrow->state          = Ring_State_LostFX;
        ringGrow->stateDraw      = Ring_Draw_Normal;
        angle += 0x10;
    }
}

uint8 Ring_CheckPlatformCollisions(EntityPlatform *platform)
{
    RSDK_THIS(Ring);

    int32 side = C_NONE;
    if (platform->state != Platform_State_Falling2 && platform->state != Platform_State_Hold) {
        platform->position.x = platform->drawPos.x - platform->collisionOffset.x;
        platform->position.y = platform->drawPos.y - platform->collisionOffset.y;
        switch (platform->collision) {
            case PLATFORM_C_PLATFORM:
                side = RSDK.CheckObjectCollisionPlatform(platform, RSDK.GetHitbox(&platform->animator, 0), self, &Ring->hitbox, true);
                break;

            case PLATFORM_C_SOLID:
            case PLATFORM_C_SOLID_HURT_SIDES:
            case PLATFORM_C_SOLID_HURT_BOTTOM:
            case PLATFORM_C_SOLID_HURT_TOP:
            case PLATFORM_C_SOLID_HOLD:
            case PLATFORM_C_SOLID_STICKY:
            case PLATFORM_C_STICKY_TOP:
            case PLATFORM_C_STICKY_LEFT:
            case PLATFORM_C_STICKY_RIGHT:
            case PLATFORM_C_STICKY_BOTTOM:
            case PLATFORM_C_SOLID_BARREL:
            case PLATFORM_C_SOLID_NOCRUSH:
                side = RSDK.CheckObjectCollisionBox(platform, RSDK.GetHitbox(&platform->animator, 1), self, &Ring->hitbox, true);
                break;

            case PLATFORM_C_TILED:
                side = C_NONE;
                if (RSDK.CheckObjectCollisionTouchBox(platform, &platform->hitbox, self, &Ring->hitbox)
                    && self->collisionLayers & Zone->moveLayerMask) {
                    TileLayer *moveLayer  = RSDK.GetTileLayer(Zone->moveLayer);
                    moveLayer->position.x = -(platform->drawPos.x + platform->tileOrigin.x) >> 16;
                    moveLayer->position.y = -(platform->drawPos.y + platform->tileOrigin.y) >> 16;
                }
                break;

            default: break;
        }
        platform->position.x = platform->centerPos.x;
        platform->position.y = platform->centerPos.y;
        if (side == C_LEFT || side == C_RIGHT)
            self->velocity.x -= platform->velocity.x;
    }

    return side;
}

void Ring_CheckObjectCollisions(int32 x, int32 y)
{
    RSDK_THIS(Ring);

    int32 collisionSides = 0;
    int32 xVel           = self->velocity.x;
    int32 yVel           = self->velocity.y;

    if (Platform) {
        foreach_active(Platform, platform) { collisionSides |= 1 << Ring_CheckPlatformCollisions(platform); }
    }

    if (Crate) {
        foreach_active(Crate, crate) { collisionSides |= 1 << Ring_CheckPlatformCollisions((EntityPlatform *)crate); }
    }

    if (Bridge) {
        foreach_active(Bridge, bridge)
        {
            bool32 collided = Bridge_HandleCollisions(self, bridge, &Ring->hitbox, false, false);
            if (collided)
                collisionSides |= 2;
        }
    }

    if (Spikes) {
        foreach_active(Spikes, spikes) { collisionSides |= 1 << RSDK.CheckObjectCollisionBox(spikes, &spikes->hitbox, self, &Ring->hitbox, true); }
    }

    if (Ice) {
        foreach_active(Ice, ice) { collisionSides |= 1 << RSDK.CheckObjectCollisionBox(ice, &ice->hitboxBlock, self, &Ring->hitbox, true); }
    }

    if (BigSqueeze) {
        foreach_active(BigSqueeze, bigSqueeze)
        {
            if (self->position.x < BigSqueeze->crusherX[BIGSQUEEZE_CRUSHER_L] + TO_FIXED(32))
                collisionSides |= 8;

            if (self->position.x > BigSqueeze->crusherX[BIGSQUEEZE_CRUSHER_R] - TO_FIXED(32))
                collisionSides |= 4;

            if (self->position.y > BigSqueeze->boundsB - TO_FIXED(8))
                collisionSides |= 2;
        }
    }

    if (SpikeCorridor) {
        foreach_active(SpikeCorridor, corridor)
        {
            if (corridor->parent) {
                collisionSides |= (1 << RSDK.CheckObjectCollisionBox(corridor, &corridor->hitboxes[0], self, &Ring->hitbox, true));
                collisionSides |= (1 << RSDK.CheckObjectCollisionBox(corridor, &corridor->hitboxes[1], self, &Ring->hitbox, true));
                collisionSides |= (1 << RSDK.CheckObjectCollisionBox(corridor, &corridor->hitboxes[2], self, &Ring->hitbox, true));
                collisionSides |= (1 << RSDK.CheckObjectCollisionBox(corridor, &corridor->hitboxes[3], self, &Ring->hitbox, true));
            }
        }
    }

    if (xVel <= 0) {
        if (!(collisionSides & 8)) {
            if (RSDK.ObjectTileCollision(self, Zone->collisionLayers, CMODE_RWALL, self->collisionPlane, -x, 0, true))
                self->velocity.x = -xVel;
        }
        else {
            self->velocity.x = -xVel;
        }
    }
    else {
        if (!(collisionSides & 4)) {
            if (RSDK.ObjectTileCollision(self, Zone->collisionLayers, CMODE_LWALL, self->collisionPlane, x, 0, true))
                self->velocity.x = -xVel;
        }
        else {
            self->velocity.x = -xVel;
        }
    }

    if (yVel <= 0) {
        if (collisionSides & 0x10 || RSDK.ObjectTileCollision(self, Zone->collisionLayers, CMODE_ROOF, self->collisionPlane, 0, -y, true))
            self->velocity.y = -yVel;
    }
    else if (collisionSides & 2 || RSDK.ObjectTileCollision(self, Zone->collisionLayers, CMODE_FLOOR, self->collisionPlane, 0, y, true)) {
        self->velocity.y = (yVel >> 2) - yVel;
        if (self->velocity.y > -TO_FIXED(1))
            self->velocity.y = -TO_FIXED(1);
    }
}

void Ring_State_Normal(void)
{
    RSDK_THIS(Ring);

    Ring_Collect();

    self->animator.frameID = Zone->ringFrame;
}
void Ring_State_Linear(void)
{
    RSDK_THIS(Ring);

    self->drawPos.x = self->position.x + self->amplitude.x * RSDK.Sin1024(self->speed * Zone->timer);
    self->drawPos.y = self->position.y + self->amplitude.y * RSDK.Sin1024(self->speed * Zone->timer);

    Ring_Collect();

    self->animator.frameID = Zone->ringFrame;
}
void Ring_State_Circular(void)
{
    RSDK_THIS(Ring);

    self->drawPos.x = self->position.x + self->amplitude.x * RSDK.Cos1024(self->speed * Zone->timer + 4 * self->angle);
    self->drawPos.y = self->position.y + self->amplitude.y * RSDK.Sin1024(self->speed * Zone->timer + 4 * self->angle);

    Ring_Collect();

    self->animator.frameID = Zone->ringFrame;
}
void Ring_State_Path(void)
{
    RSDK_THIS(Ring);

    self->drawPos.x += self->velocity.x;
    self->drawPos.y += self->velocity.y;

    Entity *node = RSDK_GET_ENTITY_GEN(self->speed);

    if (self->velocity.x <= 0) {
        if (self->drawPos.x < node->position.x) {
            self->drawPos.x = node->position.x;
        }
    }
    else if (self->drawPos.x > node->position.x) {
        self->drawPos.x = node->position.x;
    }

    if (self->velocity.y <= 0) {
        if (self->drawPos.y < node->position.y) {
            self->drawPos.y = node->position.y;
        }
    }
    else if (self->drawPos.y > node->position.y) {
        self->drawPos.y = node->position.y;
    }

    Ring_Collect();

    self->animator.frameID = Zone->ringFrame;
}
void Ring_State_Track(void)
{
    RSDK_THIS(Ring);

    uint16 timeVal = Zone->timer << 7;
    if ((timeVal & 0x100) == self->direction) {
        self->drawPos.x = self->position.x + (timeVal * self->amplitude.x >> 6) - (self->amplitude.x << 15);
        self->drawPos.y = self->position.y + (timeVal * self->amplitude.y >> 6) - (self->amplitude.y << 15);
    }
    else {
        self->drawPos.x = self->position.x + (self->amplitude.x << 15) - (timeVal * self->amplitude.x >> 6);
        self->drawPos.y = self->position.y + (self->amplitude.y << 15) - (timeVal * self->amplitude.y >> 6);
    }

    Ring_Collect();

    self->animator.frameID = Zone->ringFrame;
}
void Ring_State_Lost(void)
{
    RSDK_THIS(Ring);

    self->velocity.y += 0x1800;

    self->position.x += self->velocity.x;
    self->position.y += self->velocity.y;

    if (self->moveType != RING_MOVE_FIXED) {
        if (self->velocity.y > 0 && RSDK.ObjectTileCollision(self, Zone->collisionLayers, CMODE_FLOOR, self->collisionPlane, 0, TO_FIXED(8), true)) {
            self->velocity.y = (self->velocity.y >> 2) - self->velocity.y;
            if (self->velocity.y > -TO_FIXED(1))
                self->velocity.y = -TO_FIXED(1);
        }
    }
    else {
        Ring_CheckObjectCollisions(TO_FIXED(8), TO_FIXED(8));
    }

    RSDK.ProcessAnimation(&self->animator);

    if (!(++self->timer & 7)) {
        if (self->animator.speed > 0x40)
            self->animator.speed -= 0x10;
    }

    if (self->timer > 0x3F)
        Ring_Collect();

    if (self->timer > 0xFF)
        destroyEntity(self);
    else if (self->timer >= 0xF0)
        self->alpha -= 0x10;
}
void Ring_State_LostFX(void)
{
    RSDK_THIS(Ring);

    self->velocity.y += 0x1800;
    self->position.x += self->velocity.x;
    self->position.y += self->velocity.y;

    RSDK.ProcessAnimation(&self->animator);

    self->scale.x += 0x10;
    self->scale.y += 0x10;

    if (++self->timer > 64)
        destroyEntity(self);
}
void Ring_State_Big(void)
{
    RSDK_THIS(Ring);

    self->velocity.y += 0x1200;
    self->position.x += self->velocity.x;
    self->position.y += self->velocity.y;

    self->angle += self->animator.speed >> 6;
    if (self->timer <= 0xF0) {
        self->scale.x = (-RSDK.Sin256(self->angle) >> 1) + 0x180;
        self->scale.y = (RSDK.Sin256(self->angle) >> 1) + 0x180;
    }
    else {
        if (self->scale.x > self->scale.y) {
            self->scale.x += 8;
            self->scale.y -= (self->scale.y >> 3);
        }
        else {
            self->scale.y += 8;
            self->scale.x -= (self->scale.x >> 3);
        }
    }

    int32 x = self->scale.x << 11;
    int32 y = self->scale.y << 11;
    Ring_CheckObjectCollisions(x, y);

    if (!(self->angle & 0xF)) {
        EntityRing *sparkle = CREATE_ENTITY(Ring, NULL, self->position.x + RSDK.Rand(-x, x), self->position.y + RSDK.Rand(-y, y));
        sparkle->state      = Ring_State_Sparkle;
        sparkle->stateDraw  = Ring_Draw_Sparkle;
        sparkle->active     = ACTIVE_NORMAL;
        sparkle->visible    = false;
        RSDK.SetSpriteAnimation(Ring->aniFrames, self->sparkleType + 2, &sparkle->animator, true, 0);
        int32 frameCount = sparkle->animator.frameCount;
        if (sparkle->animator.animationID == 2) {
            sparkle->alpha = 0xE0;
            frameCount >>= 1;
        }
        sparkle->maxFrameCount  = frameCount - 1;
        sparkle->animator.speed = 4;
        self->sparkleType       = (self->sparkleType + 1) % 3;
    }

    RSDK.ProcessAnimation(&self->animator);

    if (!(++self->timer & 7)) {
        if (self->animator.speed > 0x80) {
            self->animator.speed -= 8;
        }
    }

    if (self->timer >= 72)
        Ring_Collect();

    if (self->timer > 0xFF)
        destroyEntity(self);
}
void Ring_State_Attracted(void)
{
    RSDK_THIS(Ring);

    EntityPlayer *player = self->storedPlayer;
    if (player->shield == SHIELD_LIGHTNING) {
        int32 startX = self->position.x;
        int32 startY = self->position.y;

        if (self->position.x <= player->position.x) {
            if (self->velocity.x >= 0)
                self->velocity.x += 0x3000;
            else
                self->velocity.x += 0xC000;
        }
        else {
            if (self->velocity.x <= 0)
                self->velocity.x -= 0x3000;
            else
                self->velocity.x -= 0xC000;
        }

        if (startY <= player->position.y) {
            if (self->velocity.y >= 0)
                self->velocity.y += 0x3000;
            else
                self->velocity.y += 0xC000;
        }
        else {
            if (self->velocity.y <= 0)
                self->velocity.y -= 0x3000;
            else
                self->velocity.y -= 0xC000;
        }

        self->position.x = startX + self->velocity.x;
        self->position.y = startY + self->velocity.y;
    }
    else {
        self->state          = Ring_State_Lost;
        self->animator.speed = 0x80;
        self->alpha          = 0x100;
        self->inkEffect      = INK_ALPHA;
    }

    Ring_Collect();

    self->animator.frameID = Zone->ringFrame;
}
void Ring_State_Sparkle(void)
{
    RSDK_THIS(Ring);

    self->position.x += self->velocity.x;
    self->position.y += self->velocity.y;

    if (self->timer <= 0) {
        self->visible = true;
        RSDK.ProcessAnimation(&self->animator);

        if (self->animator.frameID >= self->maxFrameCount)
            destroyEntity(self);
    }
    else {
        self->visible = false;
        self->timer--;
    }
}

void Ring_Draw_Normal(void)
{
    RSDK_THIS(Ring);

    self->direction = self->animator.frameID > 8;
    RSDK.DrawSprite(&self->animator, NULL, false);
}
void Ring_Draw_Oscillating(void)
{
    RSDK_THIS(Ring);

    self->direction = self->animator.frameID > 8;
    RSDK.DrawSprite(&self->animator, &self->drawPos, false);
}
void Ring_Draw_Sparkle(void)
{
    RSDK_THIS(Ring);

    if (self->alpha == 0xE0) {
        self->animator.frameID += 16;
        self->inkEffect = INK_ADD;
        RSDK.DrawSprite(&self->animator, NULL, false);

        self->inkEffect = INK_NONE;
        self->animator.frameID -= 16;
    }
    RSDK.DrawSprite(&self->animator, NULL, false);
}

#if GAME_INCLUDE_EDITOR
void Ring_EditorDraw(void)
{
    RSDK_THIS(Ring);

    RSDK.SetSpriteAnimation(Ring->aniFrames, self->type, &self->animator, true, 0);
    RSDK.DrawSprite(&self->animator, NULL, false);

    self->updateRange.x = TO_FIXED(128);
    self->updateRange.y = TO_FIXED(128);
    if (showGizmos()) {
        RSDK_DRAWING_OVERLAY(true);

        Vector2 pos;
        Vector2 amplitude;
        amplitude.x = self->amplitude.x >> 10;
        amplitude.y = self->amplitude.y >> 10;

        switch (self->moveType) {
            case RING_MOVE_LINEAR:
                self->updateRange.x = (abs(amplitude.x) + 0x1000) << 10;
                self->updateRange.y = (abs(amplitude.y) + 0x1000) << 10;

                // draw distance previews
                self->inkEffect = INK_BLEND;
                self->drawPos.x = amplitude.x * RSDK.Sin1024(0x100) + self->position.x;
                self->drawPos.y = amplitude.y * RSDK.Sin1024(0x100) + self->position.y;
                pos             = self->drawPos;
                Ring_Draw_Oscillating();

                self->drawPos.x = amplitude.x * RSDK.Sin1024(0x300) + self->position.x;
                self->drawPos.y = amplitude.y * RSDK.Sin1024(0x300) + self->position.y;
                Ring_Draw_Oscillating();

                RSDK.DrawLine(pos.x, pos.y, self->drawPos.x, self->drawPos.y, 0x00FF00, 0, INK_NONE, false);
                break;

            case RING_MOVE_CIRCLE:
                self->updateRange.x = (abs(amplitude.x) + 0x1000) << 10;
                self->updateRange.y = (abs(amplitude.y) + 0x1000) << 10;

                // draw distance preview
                self->inkEffect = INK_BLEND;
                self->drawPos.x = amplitude.x * RSDK.Cos1024(4 * self->angle) + self->position.x;
                self->drawPos.y = amplitude.y * RSDK.Sin1024(4 * self->angle) + self->position.y;

                Ring_Draw_Oscillating();
                break;

            case RING_MOVE_TRACK:
                self->updateRange.x = (abs(amplitude.x) + 0x2000) << 9;
                self->updateRange.y = (abs(amplitude.y) + 0x2000) << 9;
                self->state         = Ring_State_Track;

                // draw distance preview
                self->inkEffect = INK_BLEND;

                // draw distance previews
                self->inkEffect = INK_BLEND;
                self->drawPos.x = self->position.x + (0x100 * amplitude.x >> 6) - (amplitude.x << 15);
                self->drawPos.y = self->position.y + (0x100 * amplitude.y >> 6) - (amplitude.y << 15);
                pos             = self->drawPos;
                Ring_Draw_Oscillating();

                self->drawPos.x = self->position.x + (amplitude.x << 15) - (0x000 * amplitude.x >> 6);
                self->drawPos.y = self->position.y + (amplitude.y << 15) - (0x000 * amplitude.y >> 6);
                Ring_Draw_Oscillating();

                RSDK.DrawLine(pos.x, pos.y, self->drawPos.x, self->drawPos.y, 0x00FF00, 0, INK_NONE, false);
                break;

            case RING_MOVE_PATH: {
                Entity *target = RSDK_GET_ENTITY_GEN(self->speed);

                if (target)
                    DrawHelpers_DrawArrow(self->position.x, self->position.y, target->position.x, target->position.y, 0x00FF00, INK_NONE, 0xFF);
                break;
            }

            default:
            case RING_MOVE_FIXED:
                // nothin'
                break;
        }

        self->inkEffect = INK_NONE;
        RSDK_DRAWING_OVERLAY(false);
    }
}

void Ring_EditorLoad(void)
{
    Ring->aniFrames = RSDK.LoadSpriteAnimation("Global/Ring.bin", SCOPE_STAGE);

    RSDK_ACTIVE_VAR(Ring, type);
    RSDK_ENUM_VAR("Normal", RING_TYPE_NORMAL);
    RSDK_ENUM_VAR("Big", RING_TYPE_BIG);

    RSDK_ACTIVE_VAR(Ring, moveType);
    RSDK_ENUM_VAR("Fixed", RING_MOVE_FIXED);
    RSDK_ENUM_VAR("Linear", RING_MOVE_LINEAR);
    RSDK_ENUM_VAR("Circle", RING_MOVE_CIRCLE);
    RSDK_ENUM_VAR("Track", RING_MOVE_TRACK);
    RSDK_ENUM_VAR("Path", RING_MOVE_PATH);

    RSDK_ACTIVE_VAR(Ring, planeFilter);
    RSDK_ENUM_VAR("None", PLANEFILTER_NONE);
    RSDK_ENUM_VAR("AL", PLANEFILTER_AL);
    RSDK_ENUM_VAR("BL", PLANEFILTER_BL);
    RSDK_ENUM_VAR("AH", PLANEFILTER_AH);
    RSDK_ENUM_VAR("BH", PLANEFILTER_BH);
}
#endif

void Ring_Serialize(void)
{
    RSDK_EDITABLE_VAR(Ring, VAR_ENUM, type);
    RSDK_EDITABLE_VAR(Ring, VAR_ENUM, planeFilter);
    RSDK_EDITABLE_VAR(Ring, VAR_ENUM, moveType);
    RSDK_EDITABLE_VAR(Ring, VAR_VECTOR2, amplitude);
    RSDK_EDITABLE_VAR(Ring, VAR_ENUM, speed);
    RSDK_EDITABLE_VAR(Ring, VAR_INT32, angle);
}

#if defined RETRO_PS1_BUILD
// PS1 port (docs/ports/mania/32 7b, choices 113 / 117): the classes whose entities go dormant out of the cameras' reach
// (ps1_limits.h PS1DormantSpec; the engine side: RSDK/Scene/PS1/PS1Entities.cpp). Each one reviewed in its code: its
// Create has no side effect a second run would change (it runs at the load as stock, and again when its record is
// re-created), no other object keeps a pointer to it, out of range (ACTIVE_BOUNDS) nothing runs it.
// Rings: a fixed ring's update (Ring_State_Normal) writes only animator.frameID (= Zone->ringFrame) before its draw sets
// direction from it: both rewritten before any read. Linear and circular rings (7b.3) set drawPos from position and
// Zone->timer at the start of their update (Ring_Collect and the draw read it after): transient with the animator's
// frames pointer (set in Create) between it and frameID. Track rings stay entities (their update reads direction,
// which the draw rewrites), path rings too (they move by velocity).
static int Ring_PS1Eligible(const void *e)
{
    int32 t = ((const EntityRing *)e)->moveType;
    return t == RING_MOVE_FIXED || t == RING_MOVE_LINEAR || t == RING_MOVE_CIRCLE;
}
// 9u.10: LaundroMobile (HCZ2's boss) walks every slot and moves the rings whose state is not Ring_State_Normal; a fixed
// ring is created in that state (and one that leaves it is never returned to its record), so in its scene fixed rings may
// still go dormant (a dormant slot is a class-0 ghost the walk passes over). HCZ2 kept its ~330 rings live all act.
static int Ring_PS1EligibleFixed(const void *e) { return ((const EntityRing *)e)->moveType == RING_MOVE_FIXED; }
static int Ring_PS1Range(const void *e)
{
    const EntityRing *r = (const EntityRing *)e; // (the scene's amplitude: Create shifts it by 10)
    if (r->moveType == RING_MOVE_FIXED)
        return TO_FIXED(64);
    int32 x = (abs(r->amplitude.x >> 10) + 0x1000) << 10, y = (abs(r->amplitude.y >> 10) + 0x1000) << 10;
    return x > y ? x : y;
}

// Plane switches: their update only switches the players' planes (and shows them in debug mode, which is off); their
// range follows size and angle (PlaneSwitch_Create).
static int PS1Always(const void *e) { return true; }
static int PlaneSwitch_PS1Range(const void *e)
{
    const EntityPlaneSwitch *ps = (const EntityPlaneSwitch *)e;
    int32 x = TO_FIXED(32) + abs(ps->size * RSDK.Sin256(ps->angle) << 11);
    int32 y = TO_FIXED(32) + abs(ps->size * RSDK.Cos256(ps->angle) << 11);
    return x > y ? x : y;
}

// Shop windows (SPZ1): as created until one shatters (State_Shattered destroys it two frames on); the draw rewrites
// animator.frameID; Create sets the CircleBumpers it covers to the low draw group (the same every run). Range: size.
static int ShopWindow_PS1Range(const void *e)
{
    const EntityShopWindow *w = (const EntityShopWindow *)e;
    return w->size.x > w->size.y ? w->size.x : w->size.y;
}

// Invisible blocks: collision only; the ones in ACTIVE_NORMAL stay entities; outside time attack the time-attack ones
// destroy themselves in Create (the record is then gone).
static int InvisibleBlock_PS1Eligible(const void *e) { return !((const EntityInvisibleBlock *)e)->activeNormal; }
static int InvisibleBlock_PS1Range(const void *e)
{
    const EntityInvisibleBlock *b = (const EntityInvisibleBlock *)e;
    int32 x = (b->width + 5) << 19, y = (b->height + 5) << 19;
    return x > y ? x : y;
}

// Spikes: the moving ones stay entities; range: their count (Spikes_Create)
static int Spikes_PS1Eligible(const void *e) { return !((const EntitySpikes *)e)->moving; }
// docs/ports/mania/32 9r: TVVan_Create's updateRange per type (exit TVs 256 px, the rest 128); the radio keeps its last node by
// pointer on the PC: on the PS1 by slot (TVVan.c), a woken node may take the block another one left
static int TVVan_PS1Range(const void *e) { return ((const EntityTVVan *)e)->type == TVVAN_EXITTV ? TO_FIXED(256) : TO_FIXED(128); }
static int Spikes_PS1Range(const void *e)
{
    int32 r = (((const EntitySpikes *)e)->count + 6) << 20;
    return r > TO_FIXED(96) ? r : TO_FIXED(96);
}
// Collapsing platforms: Create aligns them (movesInCreate) and copies their layer's tiles, which only their own
// collapse changes: one that collapses is destroyed; the respawning ones stay entities
static int CollapsingPlatform_PS1Eligible(const void *e) { return !((const EntityCollapsingPlatform *)e)->respawn; }

// Circle bumpers (SPZ): fixed, linear and circular ones (their update sets drawPos from position and Zone->timer before
// the collision and the draw read it: transient); a hit changes their animation and hit count (diffs). ShopWindow's
// draw-group change on them is baked into the scene on the PS1 (CircleBumper.ps1Low). Range: CircleBumper_Create.
static int CircleBumper_PS1Eligible(const void *e) { return ((const EntityCircleBumper *)e)->type <= CIRCLEBUMPER_CIRCLE; }
static int CircleBumper_PS1Range(const void *e)
{
    const EntityCircleBumper *b = (const EntityCircleBumper *)e;
    if (b->type == CIRCLEBUMPER_FIXED)
        return 0x400000;
    int32 x = (abs(b->amplitude.x >> 10) + 0x1000) << 10, y = (abs(b->amplitude.y >> 10) + 0x1000) << 10;
    return x > y ? x : y;
}

// FBZ (7b.3c), each reviewed: Create sets only the entity's own fields from its editor values (no random numbers, no
// counters, no children); others reach them only with foreach_active (in range: live) or not at all; what their update
// leaves (timers, animators, hitboxes it rewrites, the players it holds) comes back as diffs.
// BG switches / generic triggers: the update only writes the object's statics or runs the zone's callback; range: size
static int BGSwitch_PS1Range(const void *e)
{
    const EntityBGSwitch *b = (const EntityBGSwitch *)e;
    return b->size.x > b->size.y ? b->size.x : b->size.y;
}
static int GenericTrigger_PS1Range(const void *e)
{
    const EntityGenericTrigger *t = (const EntityGenericTrigger *)e;
    return t->size.x > t->size.y ? t->size.x : t->size.y;
}
// No diffs where a record that left changed is rare (it then stays an entity until it matches again): mines (a touch
// arms one: ACTIVE_NORMAL, then it explodes: gone), triggers (no writes), fans (their trigger box's top / bottom are
// rewritten first in each update: transient; activePlayers clears while the player is still near), propeller shafts
// (their release timers run out in 30 frames), the badniks below (reset off screen)
// propeller shafts: range max(64 px, size); hang points: max(64 px, length + 64 px); electromagnets: max(128 px, height
// + 64 px) (their Create)
static int PropellerShaft_PS1Range(const void *e)
{
    int32 r = ((const EntityPropellerShaft *)e)->size << 16;
    return r > TO_FIXED(64) ? r : TO_FIXED(64);
}
static int HangPoint_PS1Range(const void *e)
{
    int32 r = (((const EntityHangPoint *)e)->length + 64) << 16;
    return r > TO_FIXED(64) ? r : TO_FIXED(64);
}
static int ElectroMagnet_PS1Range(const void *e)
{
    int32 r = (((const EntityElectroMagnet *)e)->height + 64) << 16;
    return r > TO_FIXED(128) ? r : TO_FIXED(128);
}
// currents: Create links the button (or pull chain) its tag names (foreach_all over classes that stay entities);
// activation comes back as a diff; range: half the size
static int Current_PS1Range(const void *e)
{
    const EntityCurrent *c = (const EntityCurrent *)e;
    return (c->size.x > c->size.y ? c->size.x : c->size.y) >> 1;
}

// FBZ badniks: off screen they go back to their start and their Create state (Technosqueek_CheckOffScreen,
// Clucker_CheckOffScreen), so the ones that leave that way return to records; a Clucker's Create reads the slot before
// it (a Platform parent: never a record); eggs (data) are temp entities
// `unless`: the stages' classes that walk or keep these entities (foreach_all, stored pointers: their whole object
// files reviewed for each class; a ring another object creates is a temp entity, never a record)
const PS1DormantSpec ps1DormantSpecs[] = {
    // (the fixed-ring entry first: where both apply, the general one below, later in the table, takes the class)
    { "Ring", Ring_PS1EligibleFixed, 0, Ring_PS1Range,
      { offsetof(EntityRing, drawPos), offsetof(EntityRing, animator.frameID) + sizeof(int32) - offsetof(EntityRing, drawPos),
        offsetof(EntityRing, direction), sizeof(uint8) },
      false,
      PS1_UNLESS("ERZOutro", "RingField") },
    { "Ring", Ring_PS1Eligible, 0, Ring_PS1Range,
      { offsetof(EntityRing, drawPos), offsetof(EntityRing, animator.frameID) + sizeof(int32) - offsetof(EntityRing, drawPos),
        offsetof(EntityRing, direction), sizeof(uint8) },
      false,
      PS1_UNLESS("ERZOutro", "RingField", "LaundroMobile") },
    { "PlaneSwitch", PS1Always, 0, PlaneSwitch_PS1Range, { 0, 0, 0, 0 }, false, PS1_UNLESS("HeavyRider", "RollerMKII") },
    { "ShopWindow", PS1Always, 0, ShopWindow_PS1Range, { offsetof(EntityShopWindow, animator.frameID), sizeof(int32), 0, 0 },
      false, NULL },
    { "InvisibleBlock", InvisibleBlock_PS1Eligible, 0, InvisibleBlock_PS1Range, { 0, 0, 0, 0 }, false,
      PS1_UNLESS("Shiversaw") },
    // springs (a bounce leaves the animation on its last frame) and decorations (animation, rotation) come back with
    // their changed words
    { "Spring", PS1Always, TO_FIXED(96), NULL, { 0, 0, 0, 0 }, true, PS1_UNLESS("PopOut", "MetalSonic") },
    { "Decoration", PS1Always, TO_FIXED(128), NULL, { 0, 0, 0, 0 }, true, PS1_UNLESS("AIZSetup", "MSZSetup", "EncoreGoodEnd") },
    // (7b.12: PopOut reads its spring / spikes from the next slot: a stage with PopOut keeps them entities)
    { "Spikes", Spikes_PS1Eligible, 0, Spikes_PS1Range, { 0, 0, 0, 0 }, false, PS1_UNLESS("PopOut") },
    // SPZ: TV vans / satellites / TVs (the radio signals find them with foreach_active: in range only; a TV broken
    // comes back with its changed words); Rock'em Sock'em bumpers (raised in Create; back to their start after a hit)
    { "CircleBumper", CircleBumper_PS1Eligible, 0, CircleBumper_PS1Range, { offsetof(EntityCircleBumper, drawPos), sizeof(Vector2), 0, 0 },
      true, NULL },
    { "TVVan", PS1Always, 0, TVVan_PS1Range, { 0, 0, 0, 0 }, true, NULL },
    { "RockemSockem", PS1Always, TO_FIXED(128), NULL, { 0, 0, 0, 0 }, true, NULL, true },
    // (7b.12: Blastoid / DiveEggman / EggPistonsMKII read their collapsing platforms by slot: those stages keep them;
    // 9b.2b: so do BuckwildBall / RockDrill / MegaOctus, and BigSqueeze (+ FBZ1Outro) / PuyoGame walk them with
    // foreach_all: BigSqueeze's intro arms the arena floor, a dormant one stayed solid and the outro never dropped the player)
    { "CollapsingPlatform", CollapsingPlatform_PS1Eligible, TO_FIXED(128), NULL, { 0, 0, 0, 0 }, false,
      PS1_UNLESS("Blastoid", "DiveEggman", "EggPistonsMKII", "BuckwildBall", "RockDrill", "MegaOctus", "BigSqueeze", "PuyoGame"), true },
    { "BGSwitch", PS1Always, 0, BGSwitch_PS1Range, { 0, 0, 0, 0 }, false, NULL },
    { "GenericTrigger", PS1Always, 0, GenericTrigger_PS1Range, { 0, 0, 0, 0 }, false, NULL },
    { "Mine", PS1Always, TO_FIXED(64), NULL, { 0, 0, 0, 0 }, false, NULL },
    { "FBZFan", PS1Always, TO_FIXED(128), NULL,
      { offsetof(EntityFBZFan, hitboxTrigger.top), sizeof(int16), offsetof(EntityFBZFan, hitboxTrigger.bottom), sizeof(int16) }, false, NULL },
    { "PropellerShaft", PS1Always, 0, PropellerShaft_PS1Range, { 0, 0, 0, 0 }, false, NULL },
    { "Propeller", PS1Always, TO_FIXED(128), NULL, { 0, 0, 0, 0 }, true, NULL },
    { "HangPoint", PS1Always, 0, HangPoint_PS1Range, { 0, 0, 0, 0 }, true, NULL },
    { "FlameSpring", PS1Always, TO_FIXED(128), NULL, { 0, 0, 0, 0 }, true, NULL },
    { "ElectroMagnet", PS1Always, 0, ElectroMagnet_PS1Range, { 0, 0, 0, 0 }, true, NULL },
    { "Current", PS1Always, 0, Current_PS1Range, { 0, 0, 0, 0 }, true, PS1_UNLESS("LaundroMobile") },
    // launchers (FBZ): a Platform without children (only direction is editable: a fixed platform, 128 px); a launch ends
    // back at the start, idle (ACTIVE_BOUNDS)
    { "Launcher", PS1Always, TO_FIXED(128), NULL, { 0, 0, 0, 0 }, true, NULL },
    // PSZ (7b.4): ice blocks (Create reads only globals and the player's character; Player.c's foreach_all destroys
    // only the frozen-player blocks, temp entities; a broken block is gone) and petal piles (Init sets the hitbox and
    // state on the first update: diffs; a scattered pile's tiles go with its state)
    { "Ice", PS1Always, TO_FIXED(128), NULL, { 0, 0, 0, 0 }, true, NULL },
    { "PetalPile", PS1Always, TO_FIXED(16), NULL, { 0, 0, 0, 0 }, true, NULL },
    { "Technosqueek", PS1Always, TO_FIXED(128), NULL, { 0, 0, 0, 0 }, false, NULL },
    { "Clucker", PS1Always, TO_FIXED(128), NULL, { 0, 0, 0, 0 }, false, NULL },
    { NULL, NULL, 0, NULL, { 0, 0, 0, 0 }, false, NULL },
};
#endif
