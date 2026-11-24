// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: FBZSinkTrash Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

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
// P20b: the trash copies' words written here, the ones PS1DrawFrame sends for them (RSDKv5/RSDK/Graphics/PS1/PS1Sprites.cpp):
// an unflipped full-resolution copy as its 6-word rectangle, a flipped or half-resolution one (the old GPU: no flip rectangles) as PS1TexRect's
// one quad (10 words, its span of 40 asked for as there). Before each, what those paths do first: validDraw (PS1PcClip),
// the frame's CLUT back after a fade (PS1FadeRestoreSlot), the draw count (PROFILE / PERF_HUD builds). A copy outside
// that case (no binding, quarter resolution, a full-resolution quad edge past its page, a palette bank or pending palette write on its top
// line, flip rectangles live, another ink, a clip box smaller than the screen, the 8 MB link's draw log) goes through
// DrawSprite as before. The engine's frame (SpriteFrame + hitboxCount / hitboxOffset / ps1U / ps1V / ps1Attr, 26 bytes)
#if PS1_DEBUG_BIGRAM
#define FBZSINKTRASH_PS1_LOG 1
#else
#define FBZSINKTRASH_PS1_LOG 0
#endif
typedef struct {
    int16 sprX, sprY, width, height, pivotX, pivotY;
    uint16 delay;
    int16 id;
    uint8 sheetID, basePad; // the game's 17 bytes padded to 18 (a C++ base's tail padding is not reused)
    uint8 hitboxCount, pad;
    uint16 hitboxOffset;
    uint8 ps1U, ps1V;
    uint16 ps1Attr;
} FBZSinkTrashPS1Frame;
extern uint32 *FBZSinkTrash_SpanBegin(int32 maxWords) __asm__("_Z17PS1ChainSpanBegini");
extern void FBZSinkTrash_SpanEnd(uint32 *lastHeader, int32 lastWords, uint32 *end) __asm__("_Z15PS1ChainSpanEndPmiS_");
extern void FBZSinkTrash_FadeRestoreSlot(uint32 slot) __asm__("_ZN4RSDK18PS1FadeRestoreSlotEj");
extern bool32 FBZSinkTrash_validDraw __asm__("_ZN4RSDK9validDrawE");
extern uint8 FBZSinkTrash_lineBank[] __asm__("_ZN4RSDK13gfxLineBufferE");
extern volatile int32 g_ps1PalDiffs;
extern volatile uint32 g_ps1FlipRectsOn, g_ps1FlipRectsForce, g_ps1SpriteDraws;
extern bool g_ps1FlipRectsGpu;

// True: the copy's words were written (or nothing was to be drawn); false: DrawSprite draws it
static bool32 FBZSinkTrash_PS1Copy(const FBZSinkTrashPS1Frame *f, int32 x, int32 y, int32 dir, const RSDKScreenInfo *screen)
{
    const int32 w = f->width, h = f->height;
    const int32 attr = f->ps1Attr, u = f->ps1U, v = f->ps1V, slot = attr >> 7, half = attr >> 6 & 1;
    // (PS1FrameTex's binding; half resolution = shift 1, the quarter one left to DrawSprite; PS1DrawFrame's wide pieces too)
    if (attr == 0xFFFF) return false;
    if (half && slot >= 0x140 && slot != 0x1FF) return false;
    if (h > 511) return false;
    if (g_ps1PalDiffs) return false;
    if (FBZSinkTrash_lineBank[y < 0 ? 0 : y >= screen->size.y ? screen->size.y - 1 : y]) return false;
    const int32 tw = (w + half) >> half, th = (h + half) >> half;
    const bool32 fx = dir & FLIP_X, fy = dir & FLIP_Y;
    if (u + tw > 256) return false;
    if (!half && (fx || fy) && (g_ps1FlipRectsGpu || g_ps1FlipRectsForce) && g_ps1FlipRectsOn) return false;
    // PS1TexRect's quad: a flipped axis from its last texel to one before its first; an edge past the page clamped when
    // halved, else its own strip (left to DrawSprite)
    int32 uL = fx ? u + tw - 1 : u, uR = fx ? u - 1 : u + tw, vT = fy ? v + th - 1 : v, vB = fy ? v - 1 : v + th;
    if (uR < 0 || uR > 255) {
        if (!half) return false;
        uR = uR < 0 ? 0 : 255;
    }
    if (vB < 0 || vB > 255) {
        if (!half) return false;
        vB = vB < 0 ? 0 : 255;
    }
    FBZSinkTrash_validDraw = true;
    FBZSinkTrash_FadeRestoreSlot((uint32)slot);
    const uint32 tpage = (attr & 15) | (attr >> 4 & 1) << 4 | (attr >> 5 & 1) << 7;
    const uint32 clut  = slot == 0x1FF ? (uint32)(240 << 6) : (uint32)(slot % 20) | (uint32)(496 + slot / 20) << 6;
    const uint32 xy0 = (uint32)(uint16)y << 16 | (uint16)x, xy1 = (uint32)(uint16)y << 16 | (uint16)(x + w);
    if (!half && !dir) {
        uint32 *wr = FBZSinkTrash_SpanBegin(6);
        if (!wr)
            return true;
        wr[1] = 0xE1000000 | tpage, wr[2] = 0x64808080, wr[3] = xy0, wr[4] = clut << 16 | (uint32)v << 8 | (uint32)u;
        wr[5] = (uint32)h << 16 | (uint32)w;
        FBZSinkTrash_SpanEnd(wr, 5, wr + 6);
    }
    else {
        const uint32 xy2 = (uint32)(uint16)(y + h) << 16 | (uint16)x, xy3 = (uint32)(uint16)(y + h) << 16 | (uint16)(x + w);
        const uint32 tl = (uint32)(uint8)vT << 8, bl = (uint32)(uint8)vB << 8;
        uint32 *wr = FBZSinkTrash_SpanBegin(4 * 10);
        if (!wr)
            return true;
        wr[1] = 0x2C808080, wr[2] = xy0, wr[3] = clut << 16 | tl | (uint8)uL, wr[4] = xy1, wr[5] = tpage << 16 | tl | (uint8)uR;
        wr[6] = xy2, wr[7] = bl | (uint8)uL, wr[8] = xy3, wr[9] = bl | (uint8)uR;
        FBZSinkTrash_SpanEnd(wr, 9, wr + 10);
    }
#if PS1_PROFILE || PS1_PERF_HUD
    g_ps1SpriteDraws = g_ps1SpriteDraws + 1;
#endif
    return true;
}
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
        // the direct words: the default ink, the screen's whole clip box, the engine's frame layout; the 8 MB link logs draws
        const bool32 direct = !FBZSINKTRASH_PS1_LOG && self->inkEffect == INK_NONE && stride == (int32)sizeof(FBZSinkTrashPS1Frame)
                              && !screen->clipBound_X1 && !screen->clipBound_Y1 && screen->clipBound_X2 == screen->size.x
                              && screen->clipBound_Y2 == screen->size.y;
#endif
        for (int32 i = 0; i < 64; ++i) {
#if defined RETRO_PS1_BUILD
            const FBZSinkTrashPS1Frame *f = (const FBZSinkTrashPS1Frame *)(frame0 + self->trashFrame[i] * stride);
            const int32 sx = sx0 + self->ps1TrashX[i], sy = sy0 + self->ps1TrashY[i], dir = self->trashDir[i];
            if (!FBZSinkTrash_PS1Hits((const SpriteFrame *)f, sx, sy, dir, screen))
                continue;
            if (direct && dir <= FLIP_XY
                && FBZSinkTrash_PS1Copy(f, (dir & FLIP_X) ? sx - f->width - f->pivotX : sx + f->pivotX,
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
