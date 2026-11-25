// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: FBZSinkTrash Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"
#include "../../../ps1/ps1_direct_sprite.h" // docs/perf/57 P20: the trash copies' words

ObjectFBZSinkTrash *FBZSinkTrash;

void FBZSinkTrash_Update(void)
{
    RSDK_THIS(FBZSinkTrash);

    switch (self->type) {
        case FBZSINKTRASH_SINK: {
            foreach_active(Player, player)
            {
                if (Player_CheckCollisionTouch(player, self, &self->hitboxTrash)) {
                    if (player->velocity.y >= 0) {
#if MANIA_USE_PLUS
                        if (player->state == Player_State_MightyHammerDrop) {
                            player->velocity.y = player->velocity.y - (player->velocity.y >> 5) - (player->velocity.y >> 4);
                            if (player->velocity.y >= 0x30000) {
                                int32 speed = abs(player->velocity.y + player->velocity.x);
                                if (speed > 0x10000 && !(Zone->timer & 7)) {
                                    int32 x              = player->position.x + RSDK.Rand(-0x40000, 0x40000);
                                    int32 y              = player->position.y + 0x40000 + RSDK.Rand(-0x40000, 0x40000);
                                    EntityDebris *debris = CREATE_ENTITY(Debris, Debris_State_Fall, x, y);

                                    RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, RSDK.Rand(0, 2) + 9, &debris->animator, false, 0);
                                    debris->velocity.x      = RSDK.Rand(-0x20000, 0x20000);
                                    debris->velocity.y      = -0x20000;
                                    debris->gravityStrength = 0x3800;
                                    debris->updateRange.y   = 0x200000;
                                    debris->updateRange.x   = 0x200000;
                                    debris->drawGroup       = Zone->objectDrawGroup[0];
                                }
                            }
                            else {
                                player->state         = Player_State_Air;
                                player->onGround      = true;
                                player->velocity.y    = 0;
                                player->collisionMode = CMODE_FLOOR;
                                player->angle         = 0;
                                player->position.y += 0x10000;
                                int32 speed = abs(player->velocity.y + player->velocity.x);
                                if (speed > 0x10000 && !(Zone->timer & 7)) {
                                    int32 x              = player->position.x + RSDK.Rand(-0x40000, 0x40000);
                                    int32 y              = player->position.y + 0x40000 + RSDK.Rand(-0x40000, 0x40000);
                                    EntityDebris *debris = CREATE_ENTITY(Debris, Debris_State_Fall, x, y);

                                    RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, RSDK.Rand(0, 2) + 9, &debris->animator, false, 0);
                                    debris->velocity.x      = RSDK.Rand(-0x20000, 0x20000);
                                    debris->velocity.y      = -0x20000;
                                    debris->gravityStrength = 0x3800;
                                    debris->updateRange.y   = 0x200000;
                                    debris->updateRange.x   = 0x200000;
                                    debris->drawGroup       = Zone->objectDrawGroup[0];
                                }
                            }
                        }
                        else {
#endif
                            player->onGround      = true;
                            player->velocity.y    = 0;
                            player->collisionMode = CMODE_FLOOR;
                            player->angle         = 0;
                            player->position.y += 0x10000;
                            int32 speed = abs(player->velocity.y + player->velocity.x);
                            if (speed > 0x10000 && !(Zone->timer & 7)) {
                                int32 x              = player->position.x + RSDK.Rand(-0x40000, 0x40000);
                                int32 y              = player->position.y + 0x40000 + RSDK.Rand(-0x40000, 0x40000);
                                EntityDebris *debris = CREATE_ENTITY(Debris, Debris_State_Fall, x, y);

                                RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, RSDK.Rand(0, 2) + 9, &debris->animator, false, 0);
                                debris->velocity.x      = RSDK.Rand(-0x20000, 0x20000);
                                debris->velocity.y      = -0x20000;
                                debris->gravityStrength = 0x3800;
                                debris->updateRange.y   = 0x200000;
                                debris->updateRange.x   = 0x200000;
                                debris->drawGroup       = Zone->objectDrawGroup[0];
                            }
#if MANIA_USE_PLUS
                        }
#endif
                    }
                }
                else if (Player_CheckCollisionTouch(player, self, &self->hitboxDebris)) {
#if MANIA_USE_PLUS
                    if (player->state == Player_State_MightyHammerDrop) {
                        player->velocity.y = player->velocity.y - (player->velocity.y >> 5) - (player->velocity.y >> 4);
                        if (player->velocity.y < 0x30000) {
                            player->state         = Player_State_Air;
                            player->onGround      = true;
                            player->velocity.y    = 0;
                            player->collisionMode = 0;
                            player->angle         = 0;
                            player->position.y += 0x10000;
                        }
                    }
#endif

                    if (!(Zone->timer & 7)) {
                        EntityDebris *debris = CREATE_ENTITY(Debris, Debris_State_Fall, player->position.x + RSDK.Rand(-0x40000, 0x40000),
                                                             player->position.y + RSDK.Rand(-0x100000, 0));
                        RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, 0, &debris->animator, false, RSDK.Rand(0, 20));
                        debris->velocity.x      = RSDK.Rand(-0x20000, 0x20000);
                        debris->velocity.y      = player->velocity.y;
                        debris->gravityStrength = 0x3800;
                        debris->updateRange.y   = 0x200000;
                        debris->updateRange.x   = 0x200000;
                        debris->drawGroup       = Zone->objectDrawGroup[0];
                    }
                }
            }
            break;
        }

        default:
        case FBZSINKTRASH_SOLID: {
            foreach_active(Player, player) { Player_CheckCollisionPlatform(player, self, &self->hitboxTrash); }

            foreach_active(SignPost, signPost)
            {
                if (signPost->state == SignPost_State_Falling && signPost->position.y + 0x180000 > self->position.y - (self->size.y >> 1)) {
                    RSDK.PlaySfx(SignPost->sfxSlide, false, 255);
                    signPost->spinCount  = 4;
                    signPost->position.y = self->position.y - (self->size.y >> 1) - 0x180000;
                    signPost->velocity.y = 0;
                    Music_FadeOut(0.025);
                    signPost->state = SignPost_State_Spin;
                }
            }
            break;
        }

        case FBZSINKTRASH_DECOR: break;
    }
}

void FBZSinkTrash_LateUpdate(void) {}

void FBZSinkTrash_StaticUpdate(void) {}

#if defined RETRO_PS1_BUILD
// docs/perf/57 §9 P20: a pile draws up to ~90 sprites (its body tiles and 64 trash copies) every frame it is in range,
// and most lie off the screen (FBZ2: 10-16 piles drawn a frame, FBZSinkTrash 139-243 hblanks). A copy whose rectangle
// misses the screen's clip box draws nothing and changes nothing in DrawSprite (PS1DrawFrame returns at PS1PcClip, before
// any side effect; FX_FLIP, no rotation), so it is left out here with DrawSprite's own arithmetic: (position >> 16) less
// the screen, the pivot, the flipped axes. The copies that touch the box still go through DrawSprite: exact chain words.
static inline bool32 FBZSinkTrash_PS1Hits(const SpriteFrame *frame, int32 sx, int32 sy, int32 dir, const RSDKScreenInfo *screen)
{
    const int32 w = frame->width, h = frame->height;
    const int32 x = (dir & FLIP_X) ? sx - w - frame->pivotX : sx + frame->pivotX;
    const int32 y = (dir & FLIP_Y) ? sy - h - frame->pivotY : sy + frame->pivotY;
    return w > 0 && h > 0 && x < screen->clipBound_X2 && x + w > screen->clipBound_X1 && y < screen->clipBound_Y2 && y + h > screen->clipBound_Y1;
}
// P20b: an on-screen trash copy's words written here (ps1/ps1_direct_sprite.h: the words PS1DrawFrame sends for it, every
// case it does not cover left to DrawSprite)
#define FBZSINKTRASH_PS1_DRAW(animator, pos, frame)                                                                    \
    do {                                                                                                               \
        if (FBZSinkTrash_PS1Hits(frame, ((pos)->x >> 16) - screen->position.x, ((pos)->y >> 16) - screen->position.y,  \
                                 self->direction, screen))                                                             \
            RSDK.DrawSprite(animator, pos, false);                                                                     \
    } while (0)
#else
#define FBZSINKTRASH_PS1_DRAW(animator, pos, frame) RSDK.DrawSprite(animator, pos, false)
#endif

void FBZSinkTrash_Draw(void)
{
    RSDK_THIS(FBZSinkTrash);
    Vector2 drawPos;
#if defined RETRO_PS1_BUILD
    const RSDKScreenInfo *screen = &ScreenInfo[SceneInfo->currentScreenID];
#endif

    self->direction = FLIP_NONE;
    int32 drawX     = self->position.x - (self->size.x >> 1);

    SpriteFrame *frame = RSDK.GetFrame(FBZSinkTrash->aniFrames, 11, 0);
    drawPos.x          = self->position.x - (self->size.x >> 1);
    drawPos.y          = self->position.y - (self->size.y >> 1);
    frame->width       = 64;
    for (int32 i = 0; i < self->size.x; i += 0x400000) {
        if (self->size.x - i < 0x400000)
            frame->width = (self->size.x - i) >> 16;

        FBZSINKTRASH_PS1_DRAW(&self->topAnimator, &drawPos, frame);

        drawPos.x += 0x400000;
    }

    frame         = RSDK.GetFrame(FBZSinkTrash->aniFrames, 11, 1);
    frame->height = 64;
    for (int32 i = 0; i < self->size.y; i += 0x400000) {
        drawPos.x = drawX;

        frame->width = 64;
        if (self->size.y - i < 0x400000)
            frame->height = (self->size.y - i) >> 16;

        for (int32 x = 0; x < self->size.x; x += 0x400000) {
            if (self->size.x - x < 0x400000)
                frame->width = (self->size.x - x) >> 16;

            FBZSINKTRASH_PS1_DRAW(&self->mainAnimator, &drawPos, frame);

            drawPos.x += 0x400000;
        }
        drawPos.y += 0x400000;
    }

    frame           = RSDK.GetFrame(FBZSinkTrash->aniFrames, 11, 0);
    self->direction = FLIP_Y;
    drawPos.x       = drawX;
    frame->width    = 64;

    for (int32 i = 0; i < self->size.x; i += 0x400000) {
        if (self->size.x - i < 0x400000)
            frame->width = (self->size.x - i) >> 16;

        FBZSINKTRASH_PS1_DRAW(&self->topAnimator, &drawPos, frame);

        drawPos.x += 0x400000;
    }

    if (self->type < FBZSINKTRASH_SOLID) {
#if defined RETRO_PS1_BUILD
        // the copies' frames by the engine's stride (its SpriteFrame is longer than the game's), the screen in pixels
        const uint8 *frame0  = (const uint8 *)RSDK.GetFrame(FBZSinkTrash->aniFrames, 0, 0);
        const int32 stride   = (int32)((const uint8 *)RSDK.GetFrame(FBZSinkTrash->aniFrames, 0, 1) - frame0);
        const int32 sx0      = (self->position.x >> 16) - screen->position.x;
        const int32 sy0      = (self->position.y >> 16) - screen->position.y;
        // the direct words: the default ink, the engine's frame layout, the screen's whole clip box (not the draw-log build)
        const bool32 direct = self->inkEffect == INK_NONE && stride == (int32)sizeof(PS1EngineFrame) && PS1DirectScreenOK(screen);
#endif
        for (int32 i = 0; i < 64; ++i) {
#if defined RETRO_PS1_BUILD
            const PS1EngineFrame *f = (const PS1EngineFrame *)(frame0 + self->trashFrame[i] * stride);
            const int32 sx = sx0 + self->ps1TrashX[i], sy = sy0 + self->ps1TrashY[i], dir = self->trashDir[i];
            if (!FBZSinkTrash_PS1Hits((const SpriteFrame *)f, sx, sy, dir, screen))
                continue;
            if (direct && dir <= FLIP_XY
                && PS1DirectWrite(f, (dir & FLIP_X) ? sx - f->width - f->pivotX : sx + f->pivotX,
                                        (dir & FLIP_Y) ? sy - f->height - f->pivotY : sy + f->pivotY, dir, screen))
                continue;
            drawPos.x = self->position.x + (self->ps1TrashX[i] << 16);
            drawPos.y = self->position.y + (self->ps1TrashY[i] << 16);
#else
            drawPos.x                   = self->position.x + self->trashPos[i].x;
            drawPos.y                   = self->position.y + self->trashPos[i].y;
#endif
            self->trashAnimator.frameID = self->trashFrame[i];
            self->direction             = self->trashDir[i];
            RSDK.DrawSprite(&self->trashAnimator, &drawPos, false);
        }
#if defined RETRO_PS1_BUILD
        self->trashAnimator.frameID = self->trashFrame[63]; // the loop's last write, as when every copy was drawn
#endif

        self->direction = FLIP_NONE;
    }
}

void FBZSinkTrash_Create(void *data)
{
    RSDK_THIS(FBZSinkTrash);
    if (!SceneInfo->inEditor) {
        self->updateRange.x = self->size.x >> 1;
        self->updateRange.y = (self->size.y >> 1) + 0x400000;
        self->active        = ACTIVE_BOUNDS;
        self->visible       = true;
        self->drawFX        = FX_FLIP;
        self->drawGroup     = self->type == FBZSINKTRASH_SINK ? (Zone->objectDrawGroup[1] - 2) : Zone->objectDrawGroup[0];

        self->hitboxTrash.left   = -(self->size.x >> 17);
        self->hitboxTrash.top    = -(self->size.y >> 17);
        self->hitboxTrash.right  = self->size.x >> 17;
        self->hitboxTrash.bottom = 24 - (self->size.y >> 17);

        self->hitboxDebris.left   = -(self->size.x >> 17);
        self->hitboxDebris.top    = (self->size.y >> 17) - 16;
        self->hitboxDebris.right  = self->size.x >> 17;
        self->hitboxDebris.bottom = self->size.y >> 17;

        RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, 0, &self->trashAnimator, true, 0);
        RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, 11, &self->topAnimator, true, 0);
        RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, 11, &self->mainAnimator, true, 1);

        for (int32 i = 0; i < 64; ++i) {
#if defined RETRO_PS1_BUILD
            self->ps1TrashX[i] = (int16)(RSDK.Rand(-(self->size.x >> 1), self->size.x >> 1) >> 16);
            self->ps1TrashY[i] = (int16)(RSDK.Rand(-(self->size.y >> 1), self->size.y >> 1) >> 16);
#else
            self->trashPos[i].x = RSDK.Rand(-(self->size.x >> 1), self->size.x >> 1);
            self->trashPos[i].y = RSDK.Rand(-(self->size.y >> 1), self->size.y >> 1);
#endif
            self->trashFrame[i] = RSDK.Rand(0, 22);
            self->trashDir[i]   = RSDK.Rand(FLIP_NONE, FLIP_XY + 1);
        }
    }
}

void FBZSinkTrash_StageLoad(void) { FBZSinkTrash->aniFrames = RSDK.LoadSpriteAnimation("FBZ/Trash.bin", SCOPE_STAGE); }

#if GAME_INCLUDE_EDITOR
void FBZSinkTrash_EditorDraw(void)
{
    RSDK_THIS(FBZSinkTrash);

    self->updateRange.x = self->size.x >> 1;
    self->updateRange.y = (self->size.y >> 1) + 0x400000;
    self->drawFX        = FX_FLIP;
    self->direction     = FLIP_NONE;

    RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, 0, &self->trashAnimator, true, 0);
    RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, 11, &self->topAnimator, true, 0);
    RSDK.SetSpriteAnimation(FBZSinkTrash->aniFrames, 11, &self->mainAnimator, true, 1);

    // this is pretty nasty but I didn't wanna make new vars and I needed smth to store it
    int32 *prevX = (int32 *)&self->hitboxTrash.left;
    int32 *prevY = (int32 *)&self->hitboxTrash.right;

    if (*prevX != self->size.x || *prevY != self->size.y) {
        for (int32 i = 0; i < 64; ++i) {
            self->trashPos[i].x = RSDK.Rand(-(self->size.x >> 1), self->size.x >> 1);
            self->trashPos[i].y = RSDK.Rand(-(self->size.y >> 1), self->size.y >> 1);
            self->trashFrame[i] = RSDK.Rand(0, 22);
            self->trashDir[i]   = RSDK.Rand(0, 4);
        }

        *prevX = self->size.x;
        *prevY = self->size.y;
    }

    // Draw everything except the trash, we'll do that seperately so it's part of the overlay, not the base sprite
    int32 type = self->type;
    self->type = FBZSINKTRASH_SOLID;
    FBZSinkTrash_Draw();
    self->type = type;

    if (self->type < FBZSINKTRASH_SOLID) {
        RSDK_DRAWING_OVERLAY(true);
        Vector2 drawPos;
        for (int32 i = 0; i < 64; ++i) {
            drawPos.x                   = self->position.x + self->trashPos[i].x;
            drawPos.y                   = self->position.y + self->trashPos[i].y;
            self->trashAnimator.frameID = self->trashFrame[i];
            self->direction             = self->trashDir[i];
            RSDK.DrawSprite(&self->trashAnimator, &drawPos, false);
        }

        self->direction = FLIP_NONE;
        RSDK_DRAWING_OVERLAY(false);
    }

    DrawHelpers_DrawRectOutline(self->position.x, self->position.y, self->size.x, self->size.y, 0xFFFF00);
}

void FBZSinkTrash_EditorLoad(void)
{
    FBZSinkTrash->aniFrames = RSDK.LoadSpriteAnimation("FBZ/Trash.bin", SCOPE_STAGE);

    RSDK_ACTIVE_VAR(FBZSinkTrash, type);
    RSDK_ENUM_VAR("Sink Trash", FBZSINKTRASH_SINK);
    RSDK_ENUM_VAR("Solid", FBZSINKTRASH_SOLID);
    RSDK_ENUM_VAR("Decoration", FBZSINKTRASH_DECOR);
}
#endif

void FBZSinkTrash_Serialize(void)
{
    RSDK_EDITABLE_VAR(FBZSinkTrash, VAR_ENUM, type);
    RSDK_EDITABLE_VAR(FBZSinkTrash, VAR_VECTOR2, size);
}
