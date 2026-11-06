// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: Fireflies Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectFireflies *Fireflies;

#if defined RETRO_PS1_BUILD
// P16 (docs/perf/57): DrawSprite for a firefly in the common case, in this overlay (P2's RockemSockem path, P7's
// Snowflakes: the resident EXE has no room for a faster sprite path). SSZ1 keeps ~48 fireflies alive, ~9 hblanks a frame
// of draws through the general path. A firefly is FX_FLIP, FLIP_NONE, INK_ADD (the spawner: INK_NONE). This does what
// DrawSprite -> PS1DrawFrame -> PS1InkOf -> PS1TexRect do for a frame bound at full resolution within one texture page,
// on a line of palette bank 0 with no bank-0 write pending, in their order: INK_ADD's alpha <= 0 early return (no
// validDraw), the screen clip that sets validDraw, the CLUT back after a fade, the off-screen cull, then the texture
// page with the add mode and the semi-transparent GP0 64h rectangle modulated by (alpha + 1) / 2 (the same 6 words).
// Anything else: false, and the caller draws through DrawSprite
extern RSDKScreenInfo *Fireflies_PS1Screen __asm__("_ZN4RSDK13currentScreenE");
extern uint8 Fireflies_PS1ValidDraw __asm__("_ZN4RSDK9validDrawE");
extern uint8 Fireflies_PS1LineBank[] __asm__("_ZN4RSDK13gfxLineBufferE");
extern volatile int32 g_ps1PalDiffs;
extern volatile uint32 g_ps1SpriteCulled, g_ps1SpriteClipTodo;
extern void Fireflies_PS1FadeRestoreSlot(uint32 slot) __asm__("_ZN4RSDK18PS1FadeRestoreSlotEj");
extern uint32 *Fireflies_PS1SpanBegin(int32 n) __asm__("_Z17PS1ChainSpanBegini");
extern void Fireflies_PS1SpanEnd(uint32 *w, int32 n, uint32 *end) __asm__("_Z15PS1ChainSpanEndPmiS_");
#define FIREFLIES_PS1_FRAME_SIZE (26) // the engine's SpriteFrame (the game's 18 bytes + hitbox count / offset, ps1U, ps1V, ps1Attr)

static bool32 Fireflies_PS1Draw(Animator *animator)
{
    RSDK_THIS(Fireflies);
    const int32 ink = self->inkEffect;
    if (!animator->frames || self->direction != FLIP_NONE || (ink != INK_NONE && ink != INK_ADD) || (self->drawFX & ~FX_FLIP))
        return false;
    if (ink == INK_ADD && self->alpha <= 0)
        return true; // (PS1PcDraws: nothing drawn, validDraw untouched)
    const uint8 *f        = (const uint8 *)animator->frames + FIREFLIES_PS1_FRAME_SIZE * animator->frameID;
    const SpriteFrame *fr = (const SpriteFrame *)f;
    const int32 w = fr->width, h = fr->height;
    const uint32 attr = *(const uint16 *)(f + 24), u = f[22], v = f[23];
    if (w <= 0 || h <= 0 || attr == 0xFFFF || ((attr >> 6) & 1) || w > 1023 || h > 511 || u + (uint32)w > 256)
        return false; // (not bound / halved or quartered / too big / over a page: DrawSprite's own path)
    RSDKScreenInfo *sc = Fireflies_PS1Screen;
    const int32 x = (self->position.x >> 16) - sc->position.x + fr->pivotX, y = (self->position.y >> 16) - sc->position.y + fr->pivotY;
    const int32 sh = sc->size.y;
    if (Fireflies_PS1LineBank[y < 0 ? 0 : y >= sh ? sh - 1 : y] || g_ps1PalDiffs)
        return false; // (a per-draw CLUT copy)
    // DrawSpriteFlipped's clip: a non-empty rectangle sets validDraw
    int32 x0 = x > sc->clipBound_X1 ? x : sc->clipBound_X1, x1 = x + w < sc->clipBound_X2 ? x + w : sc->clipBound_X2;
    int32 y0 = y > sc->clipBound_Y1 ? y : sc->clipBound_Y1, y1 = y + h < sc->clipBound_Y2 ? y + h : sc->clipBound_Y2;
    if (x1 <= x0 || y1 <= y0)
        return true;
    Fireflies_PS1ValidDraw = true;
    const uint32 slot = attr >> 7; // (full resolution: no quarter CLUT offset)
    Fireflies_PS1FadeRestoreSlot(slot);
    uint32 tpage       = (attr & 15) | ((attr >> 4) & 1) << 4 | ((attr >> 5) & 1) << 7;
    const uint32 clut  = slot == 0x1FF ? (uint32)(240 << 6) : (slot % 20) | (496 + slot / 20) << 6;
    const int32 sw     = sc->size.x;
    if (x >= sw || y >= sh || x + w <= 0 || y + h <= 0) {
        g_ps1SpriteCulled = g_ps1SpriteCulled + 1;
        return true;
    }
    if (sc->clipBound_X1 > 0 || sc->clipBound_Y1 > 0 || sc->clipBound_X2 < sw || sc->clipBound_Y2 < sh)
        g_ps1SpriteClipTodo = g_ps1SpriteClipTodo + 1;
    uint32 cmd = 0x64000000 | 0x808080;
    if (ink == INK_ADD) { // PS1InkOf: mode 1 (B + F), mod = (min(alpha, 255) + 1) / 2
        const uint32 mod = (uint32)((self->alpha > 0xFF ? 0xFF : self->alpha) + 1) >> 1;
        tpage |= 1 << 5;
        cmd = 0x64000000 | 0x02000000 | mod | mod << 8 | mod << 16;
    }
    uint32 *wr = Fireflies_PS1SpanBegin(6);
    if (!wr)
        return true;
    wr[1] = 0xE1000000 | tpage;
    wr[2] = cmd;
    wr[3] = (uint32)(uint16)y << 16 | (uint16)x;
    wr[4] = clut << 16 | v << 8 | u;
    wr[5] = (uint32)h << 16 | (uint32)w;
    Fireflies_PS1SpanEnd(wr, 5, wr + 6);
    return true;
}
#endif

void Fireflies_Update(void)
{
    RSDK_THIS(Fireflies);

    StateMachine_Run(self->state);
}

void Fireflies_LateUpdate(void) {}

void Fireflies_StaticUpdate(void) {}

void Fireflies_Draw(void)
{
    RSDK_THIS(Fireflies);

#if defined RETRO_PS1_BUILD
    if (Fireflies_PS1Draw(&self->animator))
        return;
#endif
    RSDK.DrawSprite(&self->animator, NULL, false);
}

void Fireflies_Create(void *data)
{
    RSDK_THIS(Fireflies);

    if (!SceneInfo->inEditor) {
        self->active        = ACTIVE_NORMAL;
        self->drawGroup     = Zone->objectDrawGroup[0];
        self->visible       = true;
        self->drawFX        = FX_FLIP;
        self->updateRange.x = TO_FIXED(128);
        self->updateRange.y = TO_FIXED(128);

        if (data) {
            self->inkEffect = INK_ADD;
            self->state     = Fireflies_State_Firefly;
        }
        else {
            self->state       = Fireflies_State_Spawner;
            self->screenCount = RSDK.GetVideoSetting(VIDEOSETTING_SCREENCOUNT);
        }
    }
}

void Fireflies_StageLoad(void) { Fireflies->aniFrames = RSDK.LoadSpriteAnimation("SSZ1/Fireflies.bin", SCOPE_STAGE); }

void Fireflies_State_Spawner(void)
{
    RSDK_THIS(Fireflies);

    if (self->timer <= 0) {
        self->timer = 1;

        if (Fireflies->activeFireflyCount < 48) {
            Vector2 startPos;

            RSDKScreenInfo *screen = &ScreenInfo[self->screenID];
            int32 screenCenterX    = TO_FIXED(screen->position.x + (screen->size.x / 2));
            int32 screenCenterY    = TO_FIXED(screen->position.y + (screen->size.y / 2));
            int32 offsetX          = TO_FIXED(ZONE_RAND(-screen->size.x, screen->size.x));
            int32 offsetY          = TO_FIXED(ZONE_RAND(-screen->size.y, screen->size.y));

            startPos.x     = screenCenterX + offsetX;
            startPos.y     = screenCenterY + offsetY;
            bool32 isLarge = ZONE_RAND(0, 10) > 7;

            EntityFireflies *fireflies = CREATE_ENTITY(Fireflies, INT_TO_VOID(true), startPos.x, startPos.y);
            RSDK.SetSpriteAnimation(Fireflies->aniFrames, isLarge ? 3 : 0, &fireflies->animator, true, 0);
            fireflies->drawGroup = isLarge ? Zone->objectDrawGroup[1] : 1;

            fireflies->duration      = ZONE_RAND(45, 75);
            fireflies->updateRange.x = TO_FIXED(128);
            fireflies->updateRange.y = TO_FIXED(128);
            fireflies->active        = ACTIVE_NORMAL;

            int32 points3_delta = ZONE_RAND(32, 128);
            int32 points3_angle = ZONE_RAND(0, 511);
            int32 points3_x = startPos.x + points3_delta * (RSDK.Cos512(points3_angle) << 7);
            int32 points3_y = startPos.y + points3_delta * (RSDK.Sin512(points3_angle) << 7);

            int32 points1_delta = ZONE_RAND(32, 64);
            int32 points1_angle = ZONE_RAND(0, 511);
            int32 points1_x = startPos.x + points1_delta * (RSDK.Cos512(points1_angle) << 7);
            int32 points1_y = startPos.y + points1_delta * (RSDK.Sin512(points1_angle) << 7);

            int32 points2_delta = ZONE_RAND(32, 64);
            int32 points2_angle = ZONE_RAND(0, 511);
            int32 points2_x = points3_x + points2_delta * (RSDK.Cos512(points2_angle) << 7);
	    int32 points2_y = points3_y + points2_delta * (RSDK.Sin512(points2_angle) << 7);

            fireflies->points[0].x = startPos.x;
            fireflies->points[0].y = startPos.y;
            fireflies->points[1].x = points1_x;
            fireflies->points[1].y = points1_y;
            fireflies->points[2].x = points2_x;
            fireflies->points[2].y = points2_y;
            fireflies->points[3].x = points3_x;
            fireflies->points[3].y = points3_y;

            ++Fireflies->activeFireflyCount;

            self->screenID = (self->screenID + 1) % self->screenCount;
        }
    }
    else {
        self->timer--;
    }
}

void Fireflies_State_Firefly(void)
{
    RSDK_THIS(Fireflies);

    int32 frame = 0, animLow = 0, animHigh = 0;
    if (self->animator.animationID % 3 || self->animator.frameID != self->animator.frameCount - 1) {
        if (self->timer == self->duration - 28) {
            frame    = 0;
            animLow  = 2;
            animHigh = 5;
            if (self->drawGroup == Zone->objectDrawGroup[1])
                animLow = animHigh;

            RSDK.SetSpriteAnimation(Fireflies->aniFrames, animLow, &self->animator, true, frame);
        }
    }
    else {
        frame    = self->animator.animationID % 3;
        animLow  = 1;
        animHigh = 4;
        if (self->drawGroup == Zone->objectDrawGroup[1])
            animLow = animHigh;

        RSDK.SetSpriteAnimation(Fireflies->aniFrames, animLow, &self->animator, true, frame);
    }

    int32 percent  = TO_FIXED(self->timer) / self->duration;
    self->position = MathHelpers_GetBezierPoint(percent, self->points[0].x, self->points[0].y, self->points[1].x, self->points[1].y,
                                                self->points[2].x, self->points[2].y, self->points[3].x, self->points[3].y);

    if (!percent)
        self->position = self->points[0];

    if (self->timer + 8 < self->duration) {
        if (self->alpha < 0x100)
            self->alpha += 0x20;
    }
    else {
        self->alpha -= 0x20;
    }

    if (self->timer >= self->duration) {
        destroyEntity(self);
        --Fireflies->activeFireflyCount;
    }

    RSDK.ProcessAnimation(&self->animator);
    ++self->timer;
}

#if GAME_INCLUDE_EDITOR
void Fireflies_EditorDraw(void)
{
    RSDK_THIS(Fireflies);

    self->drawFX = FX_FLIP;
    RSDK.SetSpriteAnimation(Fireflies->aniFrames, 5, &self->animator, true, 0);

    Fireflies_Draw();
}

void Fireflies_EditorLoad(void) { Fireflies->aniFrames = RSDK.LoadSpriteAnimation("SSZ1/Fireflies.bin", SCOPE_STAGE); }
#endif

void Fireflies_Serialize(void) {}
