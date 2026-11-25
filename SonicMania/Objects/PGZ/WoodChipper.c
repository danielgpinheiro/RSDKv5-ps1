// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: WoodChipper Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"
#if defined RETRO_PS1_BUILD
#include "../../../ps1/ps1_direct_sprite.h" // docs/perf/57 P23: the plain-ink body and debris sprites (the saw's FX_ROTATE falls back)
#define WOODCHIPPER_DRAW(animator, position) PS1_DRAW_SPRITE(animator, position)
#ifndef PS1_WOODCHIPPER_LAYER
#define PS1_WOODCHIPPER_LAYER 1 // P24 / MA-225: the debris layer (0: one temp entity a debris piece, as stock)
#endif
#else
#define WOODCHIPPER_DRAW(animator, position) RSDK.DrawSprite(animator, position, false)
#endif

ObjectWoodChipper *WoodChipper;

#if defined RETRO_PS1_BUILD && PS1_WOODCHIPPER_LAYER
// docs/perf/57-mania-cost-per-act.md P24 / docs/regression/44 MA-225: the debris as pieces in their own slots, moved and
// drawn by one temp entity (the layer, made by the first piece and gone with the last), instead of one temp entity a
// piece. A log makes ~16 debris a pixel it goes down (1,280 for an 80 px one): they filled the PS1's 112 temp slots,
// and every create past that overwrote a live temp entity, whatever it was (g_ps1EntityCreateOverwrites 0 -> 768 over
// 90 frames at chipper 0). The pieces' slots copy the engine's CreateEntity on its 112 temp slots (the cursor, the
// search, the overwrite after 16 tries) and draw in slot order, so the spray looks as it did (debris thinned along its
// length); only other temp entities are out of their reach now. Each piece keeps a debris entity's work in the same
// order: the same RSDK.Rand calls at its creation, ProcessAnimation, the move, the on-screen test with the debris'
// update range, its sprite in the same draw group; created in the chipper's update, a piece moves that frame (the layer
// is a temp slot after the chipper's, as the debris were).
#define WOODCHIPPER_PS1_PIECES (112) // PS1_TEMP_BLOCKS (RSDKv5/RSDK/Scene/PS1/PS1Entities.hpp)
typedef struct {
    Vector2 position;
    Vector2 velocity;
    Animator animator;
    uint8 direction, used;
} WoodChipperPS1Piece;
static WoodChipperPS1Piece WoodChipper_PS1Pieces[WOODCHIPPER_PS1_PIECES];
static int32 WoodChipper_PS1Cursor = 0, WoodChipper_PS1Count = 0, WoodChipper_PS1LayerSlot = -1;
void WoodChipper_PS1State_Layer(void);

static WoodChipperPS1Piece *WoodChipper_PS1NewPiece(void)
{
    EntityWoodChipper *layer = WoodChipper_PS1LayerSlot >= 0 ? RSDK_GET_ENTITY(WoodChipper_PS1LayerSlot, WoodChipper) : NULL;
    if (!layer || layer->classID != WoodChipper->classID || layer->state != WoodChipper_PS1State_Layer) {
        layer                    = CREATE_ENTITY(WoodChipper, INT_TO_VOID(2), 0, 0);
        WoodChipper_PS1LayerSlot = RSDK.GetEntitySlot(layer);
    }
    // CreateEntity's search (RSDKv5/RSDK/Scene/Object.cpp): from the cursor, at most 16 steps, then the slot it is on
    int32 slot = WoodChipper_PS1Cursor;
    for (int32 loop = 0; WoodChipper_PS1Pieces[slot].used && loop < 16; ++loop)
        slot = (slot + 1) % WOODCHIPPER_PS1_PIECES;
    WoodChipper_PS1Cursor  = slot;
    WoodChipperPS1Piece *d = &WoodChipper_PS1Pieces[slot];
    if (!d->used)
        ++WoodChipper_PS1Count;
    memset(d, 0, sizeof(*d));
    d->used = true;
    return d;
}

void WoodChipper_PS1State_Layer(void)
{
    RSDK_THIS(WoodChipper);
    Vector2 range = { 0x400000, 0x400000 }; // the debris' updateRange
    for (int32 i = 0; i < WOODCHIPPER_PS1_PIECES; ++i) {
        WoodChipperPS1Piece *d = &WoodChipper_PS1Pieces[i];
        if (!d->used)
            continue;
        RSDK.ProcessAnimation(&d->animator);
        d->position.x += d->velocity.x;
        d->position.y += d->velocity.y;
        d->velocity.y += 0x3800;
        if (!RSDK.CheckPosOnScreen(&d->position, &range)) {
            d->used = false;
            --WoodChipper_PS1Count;
        }
    }
    if (!WoodChipper_PS1Count) {
        WoodChipper_PS1LayerSlot = -1;
        destroyEntity(self);
    }
}

static void WoodChipper_PS1DrawLayer(void)
{
    RSDK_THIS(WoodChipper);
    const RSDKScreenInfo *screen = &ScreenInfo[SceneInfo->currentScreenID];
    const bool32 direct          = self->inkEffect == INK_NONE && PS1DirectScreenOK(screen);
    for (int32 i = 0; i < WOODCHIPPER_PS1_PIECES; ++i) {
        WoodChipperPS1Piece *d = &WoodChipper_PS1Pieces[i];
        if (!d->used)
            continue;
        const int32 dir        = d->direction;
        if (direct && d->animator.frames) {
            const PS1EngineFrame *f = (const PS1EngineFrame *)d->animator.frames + d->animator.frameID;
            const int32 sx = (d->position.x >> 16) - screen->position.x, sy = (d->position.y >> 16) - screen->position.y;
            if (!PS1DirectHits(f, sx, sy, dir, screen))
                continue;
            if (PS1DirectWrite(f, (dir & FLIP_X) ? sx - f->width - f->pivotX : sx + f->pivotX,
                               (dir & FLIP_Y) ? sy - f->height - f->pivotY : sy + f->pivotY, dir, screen))
                continue;
        }
        self->direction = dir;
        RSDK.DrawSprite(&d->animator, &d->position, false);
    }
}
#endif

void WoodChipper_Update(void)
{
    RSDK_THIS(WoodChipper);

    StateMachine_Run(self->state);
}

void WoodChipper_LateUpdate(void) {}

void WoodChipper_StaticUpdate(void)
{
    int32 cuttingCount = 0;
    int32 debrisCount  = 0;

    if (RSDK_GET_ENTITY(SLOT_PAUSEMENU, PauseMenu)->classID != PauseMenu->classID) {
        foreach_active(WoodChipper, chipper)
        {
            if (!chipper->timer)
                cuttingCount++;

            if (chipper->state == WoodChipper_State_Debris)
                debrisCount++;
#if defined RETRO_PS1_BUILD && PS1_WOODCHIPPER_LAYER
            else if (chipper->state == WoodChipper_PS1State_Layer)
                debrisCount++; // (the layer exists while a piece does; its timer is 0 as the debris' was: cutting too)
#endif
        }
    }

    if (cuttingCount) {
        if (!WoodChipper->playingWoodSfx) {
            RSDK.PlaySfx(WoodChipper->sfxChipperWood, true, 255);
            WoodChipper->playingWoodSfx = true;
        }
    }
    else if (WoodChipper->playingWoodSfx) {
        RSDK.StopSfx(WoodChipper->sfxChipperWood);
        WoodChipper->playingWoodSfx = false;
    }

    if (debrisCount) {
        if (!WoodChipper->playingChipSfx) {
            RSDK.PlaySfx(WoodChipper->sfxChipperChips, true, 255);
            WoodChipper->playingChipSfx = true;
        }
    }
    else if (WoodChipper->playingChipSfx) {
        RSDK.StopSfx(WoodChipper->sfxChipperChips);
        WoodChipper->playingChipSfx = false;
    }
}

void WoodChipper_Draw(void)
{
    RSDK_THIS(WoodChipper);

#if defined RETRO_PS1_BUILD && PS1_WOODCHIPPER_LAYER
    if (self->state == WoodChipper_PS1State_Layer) {
        WoodChipper_PS1DrawLayer();
        return;
    }
#endif
    if (self->state == WoodChipper_State_Debris) {
        WOODCHIPPER_DRAW(&self->animator, NULL);
    }
    else {
        Vector2 drawPos = self->position;

        if (self->height < 0) {
            self->animator.frameID = 3;
            drawPos.y += self->height + (RSDK.Sin256(self->angle) << 10);
            WOODCHIPPER_DRAW(&self->animator, &drawPos);

            int32 pos = self->height >> 16;
            if (pos <= -16) {
                self->animator.frameID = 4;
                int32 dist             = -16 - pos;
                if (dist >= -79) {
                    int32 size = dist / 80 + 2;
                    for (int32 i = 0; i < size; ++i) {
                        WOODCHIPPER_DRAW(&self->animator, &drawPos);
                        drawPos.y += 0x500000;
                    }
                }
            }
        }

        drawPos.x                 = self->position.x - 0x140000;
        drawPos.y                 = self->position.y - 0x280000;
        self->drawFX              = FX_ROTATE;
        self->sawAnimator.frameID = 0;
        WOODCHIPPER_DRAW(&self->sawAnimator, &drawPos);

        drawPos.x += 0x280000;
        self->sawAnimator.frameID = 1;
        self->rotation            = 0x200 - self->rotation;
        WOODCHIPPER_DRAW(&self->sawAnimator, &drawPos);

        self->rotation         = 0x200 - self->rotation;
        self->drawFX           = FX_NONE;
        self->animator.frameID = 0;
        drawPos.x              = self->position.x + self->shakeOffsets[0].x;
        drawPos.y              = self->position.y + self->shakeOffsets[0].y;
        WOODCHIPPER_DRAW(&self->animator, &drawPos);

        self->drawFX           = FX_FLIP;
        self->animator.frameID = 2;
        drawPos.x              = self->position.x + self->shakeOffsets[1].x;
        drawPos.y              = self->position.y + self->shakeOffsets[1].y;
        WOODCHIPPER_DRAW(&self->animator, &drawPos);

        self->animator.frameID = 6;
        WOODCHIPPER_DRAW(&self->animator, &drawPos);

        self->animator.frameID = 1;
        drawPos.x              = self->position.x + self->shakeOffsets[2].x;
        drawPos.y              = self->position.y + self->shakeOffsets[2].y;
        WOODCHIPPER_DRAW(&self->animator, &drawPos);

        self->drawFX = FX_NONE;
    }
}

void WoodChipper_Create(void *data)
{
    RSDK_THIS(WoodChipper);

    self->active    = ACTIVE_BOUNDS;
    self->visible   = true;
    self->drawGroup = Zone->objectDrawGroup[0];

    if (!SceneInfo->inEditor) {
#if defined RETRO_PS1_BUILD && PS1_WOODCHIPPER_LAYER
        if (VOID_TO_INT(data) == 2) { // the debris layer (P24): a debris entity's setup, the pieces its own
            --self->drawGroup;
            self->active        = ACTIVE_NORMAL;
            self->updateRange.x = 0x400000;
            self->updateRange.y = 0x400000;
            self->drawFX        = FX_FLIP;
            self->state         = WoodChipper_PS1State_Layer;
        }
        else
#endif
        if (data) {
            --self->drawGroup;
            self->active        = ACTIVE_NORMAL;
            self->updateRange.x = 0x400000;
            self->updateRange.y = 0x400000;
            self->drawFX        = FX_FLIP;
            RSDK.SetSpriteAnimation(WoodChipper->aniFrames, 1, &self->animator, true, 0);
            self->state = WoodChipper_State_Debris;
        }
        else {
            self->speed <<= 12;
            self->updateRange.x = 0x1000000;
            self->updateRange.y = (self->size + 256) << 16;
            self->height        = -0x10000 * self->size;
            RSDK.SetSpriteAnimation(WoodChipper->aniFrames, 0, &self->animator, true, 0);
            RSDK.SetSpriteAnimation(WoodChipper->aniFrames, 3, &self->sawAnimator, true, 0);
            self->state = WoodChipper_State_Chipper;
        }
    }
}

void WoodChipper_StageLoad(void)
{
    WoodChipper->active = ACTIVE_ALWAYS;
#if defined RETRO_PS1_BUILD && PS1_WOODCHIPPER_LAYER
    memset(WoodChipper_PS1Pieces, 0, sizeof(WoodChipper_PS1Pieces));
    WoodChipper_PS1Cursor = WoodChipper_PS1Count = 0;
    WoodChipper_PS1LayerSlot = -1;
#endif

    WoodChipper->aniFrames = RSDK.LoadSpriteAnimation("PSZ2/WoodChipper.bin", SCOPE_STAGE);

    WoodChipper->hitboxStump.left   = -46;
    WoodChipper->hitboxStump.top    = -48;
    WoodChipper->hitboxStump.right  = 83;
    WoodChipper->hitboxStump.bottom = 32;

    WoodChipper->hitboxWood.left   = -40;
    WoodChipper->hitboxWood.top    = -48;
    WoodChipper->hitboxWood.right  = 40;
    WoodChipper->hitboxWood.bottom = 0;

    WoodChipper->hitboxRazor.left   = -40;
    WoodChipper->hitboxRazor.top    = -58;
    WoodChipper->hitboxRazor.right  = 40;
    WoodChipper->hitboxRazor.bottom = 0;

    WoodChipper->sfxChipperWood  = RSDK.GetSfx("PSZ/ChipperWood.wav");
    WoodChipper->sfxChipperChips = RSDK.GetSfx("PSZ/ChipperChips.wav");
}

void WoodChipper_HandlePlayerCollisions(void)
{
    RSDK_THIS(WoodChipper);
    EntityPlayer *player2 = RSDK_GET_ENTITY(SLOT_PLAYER2, Player);

    int32 prevPlayers   = self->activePlayers;
    self->activePlayers = 0;

    WoodChipper->hitboxWood.top = (((RSDK.Sin256(self->angle) << 10) + self->height) >> 16) - 48;

    foreach_active(Player, player)
    {
        if (Player_CheckCollisionTouch(player, self, &WoodChipper->hitboxRazor)
            && (!self->height || (prevPlayers && (prevPlayers != 0b10 || !player2->sidekick)))) {
#if MANIA_USE_PLUS
            if (!Player_CheckMightyUnspin(player, 0x400, false, &player->uncurlTimer))
#endif
                Player_Hurt(player, self);
        }

        if (WoodChipper->hitboxWood.top > -48) {
            self->height = 0;
            Player_CheckCollisionBox(player, self, &WoodChipper->hitboxStump);
        }
        else if (Player_CheckCollisionBox(player, self, &WoodChipper->hitboxWood) == C_TOP) {
            self->activePlayers |= 1 << player->playerID;

#if MANIA_USE_PLUS
            if (player->state == Player_State_MightyHammerDrop) {
                self->timer   = 1;
                player->state = Player_State_Air;
            }
#endif
        }
        else {
            Player_CheckCollisionBox(player, self, &WoodChipper->hitboxStump);
        }
    }
}

void WoodChipper_State_Chipper(void)
{
    RSDK_THIS(WoodChipper);

    EntityPlayer *player2 = RSDK_GET_ENTITY(SLOT_PLAYER2, Player);

    WoodChipper_HandlePlayerCollisions();

    if (!self->activePlayers || (self->activePlayers == 2 && player2->sidekick)) {
        if (self->height) {
            if (self->timer < 30)
                self->timer++;

            if (self->angle > 0)
                self->angle -= 4;

            self->shakeOffsets[0].x = 0;
            self->shakeOffsets[0].y = 0;
            self->shakeOffsets[1].x = 0;
            self->shakeOffsets[1].y = 0;
            self->shakeOffsets[2].x = 0;
            self->shakeOffsets[2].y = 0;
        }
        else {
            self->shakeOffsets[0].x = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[0].y = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[1].x = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[1].y = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[2].x = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[2].y = 2 * RSDK.Rand(-1, 2);

            self->rotation = (self->rotation + 8) & 0x1FF;
        }
    }
    else {
        if (self->timer <= 0) {
            if (self->height < 0) {
                int32 height = ((RSDK.Sin256(self->angle) << 10) + self->height) & 0xFFFF0000;

                self->height += self->speed;
                if (self->height > 0)
                    self->height = 0;

                if (self->angle < 0x40)
                    self->angle += 4;

                int32 move = height - (((RSDK.Sin256(self->angle) << 10) + self->height) & 0xFFFF0000);

                foreach_active(Player, player)
                {
                    if ((1 << player->playerID) & self->activePlayers)
                        player->position.y -= move;
                }

                for (int32 i = self->speed >> 12; i > 0; --i) {
                    int32 x                   = self->position.x;
                    int32 y                   = self->position.y + ((RSDK.Rand(0, 17) - 40) << 16);
#if defined RETRO_PS1_BUILD && PS1_WOODCHIPPER_LAYER
                    WoodChipperPS1Piece *debris = WoodChipper_PS1NewPiece();
                    debris->position.x          = x;
                    debris->position.y          = y;
                    RSDK.SetSpriteAnimation(WoodChipper->aniFrames, 1, &debris->animator, true, 0);
#else
                    EntityWoodChipper *debris = CREATE_ENTITY(WoodChipper, INT_TO_VOID(true), x, y);
#endif

                    if (self->direction) {
                        debris->position.x += -0x530000 - (RSDK.Rand(-3, 4) << 16);
                        debris->velocity.x = -RSDK.Rand(4, 17) << 15;
                    }
                    else {
                        debris->position.x += (RSDK.Rand(-3, 4) + 83) << 16;
                        debris->velocity.x = RSDK.Rand(4, 17) << 15;
                    }

                    debris->animator.speed = RSDK.Rand(1, 3);
                    debris->direction      = RSDK.Rand(0, 4);
                }
            }

            self->shakeOffsets[0].x = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[0].y = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[1].x = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[1].y = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[2].x = 2 * RSDK.Rand(-1, 2);
            self->shakeOffsets[2].y = 2 * RSDK.Rand(-1, 2);

            self->rotation = (self->rotation + 8) & 0x1FF;
        }
        else {
            self->timer--;

            self->shakeOffsets[0].x = 0;
            self->shakeOffsets[0].y = 0;
            self->shakeOffsets[1].x = 0;
            self->shakeOffsets[1].y = 0;
            self->shakeOffsets[2].x = 0;
            self->shakeOffsets[2].y = 0;

            int32 height = ((RSDK.Sin256(self->angle) << 10) + (self->height & 0xFC00)) & 0xFFFF0000;

            if (self->angle < 0x40)
                self->angle += 4;

            int32 move = height - (((RSDK.Sin256(self->angle) << 10) + (self->height & 0xFC00)) & 0xFFFF0000);

            foreach_active(Player, player)
            {
                if ((1 << player->playerID) & self->activePlayers)
                    player->position.y -= move;
            }
        }
    }
}

void WoodChipper_State_Debris(void)
{
    RSDK_THIS(WoodChipper);

    RSDK.ProcessAnimation(&self->animator);

    self->position.x += self->velocity.x;
    self->position.y += self->velocity.y;
    self->velocity.y += 0x3800;

    if (!RSDK.CheckOnScreen(self, &self->updateRange))
        destroyEntity(self);
}

#if GAME_INCLUDE_EDITOR
void WoodChipper_EditorDraw(void)
{
    RSDK_THIS(WoodChipper);

    self->updateRange.x = 0x1000000;
    self->updateRange.y = (self->size + 256) << 16;
    self->height        = -0x10000 * self->size;

    RSDK.SetSpriteAnimation(WoodChipper->aniFrames, 0, &self->animator, true, 0);
    RSDK.SetSpriteAnimation(WoodChipper->aniFrames, 3, &self->sawAnimator, true, 0);

    WoodChipper_Draw();
}

void WoodChipper_EditorLoad(void)
{
    WoodChipper->aniFrames = RSDK.LoadSpriteAnimation("PSZ2/WoodChipper.bin", SCOPE_STAGE);

    RSDK_ACTIVE_VAR(WoodChipper, direction);
    RSDK_ENUM_VAR("Right", FLIP_NONE);
    RSDK_ENUM_VAR("Left", FLIP_X);
}
#endif

void WoodChipper_Serialize(void)
{
    RSDK_EDITABLE_VAR(WoodChipper, VAR_UINT8, direction);
    RSDK_EDITABLE_VAR(WoodChipper, VAR_UINT16, size);
    RSDK_EDITABLE_VAR(WoodChipper, VAR_ENUM, speed);
}
