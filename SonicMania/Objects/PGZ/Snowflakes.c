// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: Snowflakes Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectSnowflakes *Snowflakes;

#if defined RETRO_PS1_BUILD
// P7 (docs/perf/57): DrawSprite for a flake in the common case, in this overlay (the resident EXE has no room for a faster
// sprite path): an unflipped INK_NONE frame of FX_FLIP, bound in the atlas at full resolution within one texture page, on
// a line of palette bank 0 with no bank-0 write pending (no per-draw CLUT copy). It does what DrawSprite -> PS1DrawFrame
// -> PS1TexRect do there (RSDKv5/RSDK/Graphics/Drawing.cpp, Graphics/PS1/PS1Sprites.cpp), in their order: the screen clip
// that sets validDraw, the CLUT back after a fade (PS1FadeRestoreSlot), the off-screen cull, the texture page and the
// GP0 64h rectangle (the same 6 words), the counters. Anything else: false, and the caller draws through DrawSprite
extern RSDKScreenInfo *Snowflakes_PS1Screen __asm__("_ZN4RSDK13currentScreenE");
extern uint8 Snowflakes_PS1ValidDraw __asm__("_ZN4RSDK9validDrawE");
extern uint8 Snowflakes_PS1LineBank[] __asm__("_ZN4RSDK13gfxLineBufferE");
extern volatile int32 g_ps1PalDiffs;
extern volatile uint32 g_ps1SpriteDraws, g_ps1SpriteCulled, g_ps1SpriteClipTodo;
extern void Snowflakes_PS1FadeRestoreSlot(uint32 slot) __asm__("_ZN4RSDK18PS1FadeRestoreSlotEj");
extern uint32 *Snowflakes_PS1SpanBegin(int32 n) __asm__("_Z17PS1ChainSpanBegini");
extern void Snowflakes_PS1SpanEnd(uint32 *w, int32 n, uint32 *end) __asm__("_Z15PS1ChainSpanEndPmiS_");
#define SNOWFLAKES_PS1_FRAME_SIZE (26) // the engine's SpriteFrame (the game's 18 bytes + hitbox count / offset, ps1U, ps1V, ps1Attr)

static bool32 Snowflakes_PS1Draw(Animator *animator, Vector2 *position)
{
    RSDK_THIS(Snowflakes);
    if (!animator->frames || self->drawFX != FX_FLIP || self->direction != FLIP_NONE || self->inkEffect != INK_NONE)
        return false;
    const uint8 *f        = (const uint8 *)animator->frames + SNOWFLAKES_PS1_FRAME_SIZE * animator->frameID;
    const SpriteFrame *fr = (const SpriteFrame *)f;
    const int32 w = fr->width, h = fr->height;
    const uint32 attr = *(const uint16 *)(f + 24), u = f[22], v = f[23];
    if (w <= 0 || h <= 0 || attr == 0xFFFF || ((attr >> 6) & 1) || w > 1023 || h > 511 || u + (uint32)w > 256)
        return false; // (not bound / halved or quartered / too big / over a page: DrawSprite's own path)
    RSDKScreenInfo *sc = Snowflakes_PS1Screen;
    const int32 x = (position->x >> 16) - sc->position.x + fr->pivotX, y = (position->y >> 16) - sc->position.y + fr->pivotY;
    const int32 sh = sc->size.y;
    if (Snowflakes_PS1LineBank[y < 0 ? 0 : y >= sh ? sh - 1 : y] || g_ps1PalDiffs)
        return false; // (a per-draw CLUT copy)
    // DrawSpriteFlipped's clip: a non-empty rectangle sets validDraw
    int32 x0 = x > sc->clipBound_X1 ? x : sc->clipBound_X1, x1 = x + w < sc->clipBound_X2 ? x + w : sc->clipBound_X2;
    int32 y0 = y > sc->clipBound_Y1 ? y : sc->clipBound_Y1, y1 = y + h < sc->clipBound_Y2 ? y + h : sc->clipBound_Y2;
    if (x1 <= x0 || y1 <= y0)
        return true;
    Snowflakes_PS1ValidDraw = true;
    const uint32 slot = attr >> 7; // (full resolution: no quarter CLUT offset)
    Snowflakes_PS1FadeRestoreSlot(slot);
    const uint32 tpage = (attr & 15) | ((attr >> 4) & 1) << 4 | ((attr >> 5) & 1) << 7;
    const uint32 clut  = slot == 0x1FF ? (uint32)(240 << 6) : (slot % 20) | (496 + slot / 20) << 6;
    const int32 sw     = sc->size.x;
    if (x >= sw || y >= sh || x + w <= 0 || y + h <= 0) {
        g_ps1SpriteCulled = g_ps1SpriteCulled + 1;
        return true;
    }
    if (sc->clipBound_X1 > 0 || sc->clipBound_Y1 > 0 || sc->clipBound_X2 < sw || sc->clipBound_Y2 < sh)
        g_ps1SpriteClipTodo = g_ps1SpriteClipTodo + 1;
    uint32 *wr = Snowflakes_PS1SpanBegin(6);
    if (!wr)
        return true;
    wr[1] = 0xE1000000 | tpage;
    wr[2] = 0x64000000 | 0x808080;
    wr[3] = (uint32)(uint16)y << 16 | (uint16)x;
    wr[4] = clut << 16 | v << 8 | u;
    wr[5] = (uint32)h << 16 | (uint32)w;
    Snowflakes_PS1SpanEnd(wr, 5, wr + 6);
    g_ps1SpriteDraws = g_ps1SpriteDraws + 1;
    return true;
}
#define SNOWFLAKES_DRAW(animator, pos)                                                                                                               \
    do {                                                                                                                                             \
        if (!Snowflakes_PS1Draw(animator, pos))                                                                                                      \
            RSDK.DrawSprite(animator, pos, false);                                                                                                   \
    } while (0)
#else
#define SNOWFLAKES_DRAW(animator, pos) RSDK.DrawSprite(animator, pos, false)
#endif

void Snowflakes_Update(void)
{
    RSDK_THIS(Snowflakes);

    if (Snowflakes->count < 0x40 && !(Zone->timer % 16)) {
        for (int32 i = 0; i < 0x40; ++i) {
            if (!self->positions[i].x && !self->positions[i].y && (i & 0x8000) == 0) {
                int32 screenY = ScreenInfo->position.y;
                int32 scrX    = ScreenInfo->position.x % ScreenInfo->size.x;
                int32 posX    = (scrX + ZONE_RAND(0, ScreenInfo->size.x)) % ScreenInfo->size.x;

                self->positions[i].y = (screenY - 5) << 16;
                self->positions[i].x = posX << 16;
                self->frameIDs[i]    = 0;
                self->priority[i]    = ZONE_RAND(0, 10) > 7;

                if (self->priority[i]) {
                    self->animIDs[i] = 2 * (ZONE_RAND(0, 10) > 7) + 2;
                }
                else {
                    int32 type       = ZONE_RAND(0, 10);
                    self->animIDs[i] = type > 8 ? 3 : (type > 4 ? 1 : 0);
                }

                self->angles[i] = ZONE_RAND(0, 256);
                ++Snowflakes->count;
                break;
            }
        }
    }

    Vector2 range;
    range.x = 0x800000;
    range.y = 0x800000;

    for (int32 i = 0; i < 0x40; ++i) {
        if (self->positions[i].x || self->positions[i].y) {
            Vector2 pos = Snowflakes_HandleWrap(i);
            int32 angle = RSDK.Sin256(self->angles[i]) << 6;

            self->positions[i].x += angle;
            self->positions[i].y += 0x8000;
            pos.x += angle;
            pos.y += 0x8000;

            self->frameIDs[i]++;

            if (self->animIDs[i] <= 2)
                self->frameIDs[i] %= 12;

            if (self->animIDs[i] <= 3)
                self->frameIDs[i] %= 24;

            if (self->animIDs[i] <= 4)
                self->frameIDs[i] %= 48;

            ++self->angles[i];
            self->angles[i] &= 0xFF;

            if (!RSDK.CheckPosOnScreen(&pos, &range)) {
                self->positions[i].x = 0;
                self->positions[i].y = 0;
                --Snowflakes->count;
            }
        }
    }

    RSDK.ProcessAnimation(&self->animator);
}

void Snowflakes_LateUpdate(void) {}

void Snowflakes_StaticUpdate(void)
{
    foreach_active(Snowflakes, snowflake) { RSDK.AddDrawListRef(Zone->objectDrawGroup[1], RSDK.GetEntitySlot(snowflake)); }
}

void Snowflakes_Draw(void)
{
    RSDK_THIS(Snowflakes);
    int32 drawHigh  = Zone->objectDrawGroup[1];
    int32 drawGroup = SceneInfo->currentDrawGroup;

    for (int32 i = 0; i < 0x40; ++i) {
        if (self->positions[i].x || self->positions[i].y) {
            int32 priority = self->priority[i];

            if ((priority || drawGroup != drawHigh) && (priority != 1 || drawGroup == drawHigh)) {
                Vector2 drawPos = Snowflakes_HandleWrap(i);
                self->direction = FLIP_NONE;
                int32 angle     = RSDK.Sin256(self->angles[i]) << 6;

                if (self->animIDs[i] <= 2) {
                    RSDK.SetSpriteAnimation(Snowflakes->aniFrames, self->animIDs[i], &self->animator, true, self->frameIDs[i] >> 2);
                    SNOWFLAKES_DRAW(&self->animator, &drawPos);
                }
                else {
                    int32 frame = 0;

                    if (self->animIDs[i] == 4) {
                        frame = self->frameIDs[i] / 3;

                        if (angle > 0)
                            self->direction = FLIP_X;
                    }
                    else if (self->animIDs[i] == 3) {
                        frame = self->frameIDs[i] >> 2;

                        if (angle > 0)
                            self->direction = FLIP_X;
                    }

                    RSDK.SetSpriteAnimation(Snowflakes->aniFrames, self->animIDs[i], &self->animator, true, frame);
                    SNOWFLAKES_DRAW(&self->animator, &drawPos);
                }
            }
        }
    }
}

void Snowflakes_Create(void *data)
{
    RSDK_THIS(Snowflakes);

    self->active        = ACTIVE_NORMAL;
    self->drawGroup     = Zone->objectDrawGroup[0];
    self->visible       = true;
    self->drawFX        = FX_FLIP;
    self->updateRange.x = 0x800000;
    self->updateRange.y = 0x800000;
}

void Snowflakes_StageLoad(void)
{
    Snowflakes->aniFrames = RSDK.LoadSpriteAnimation("PSZ2/Snowflakes.bin", SCOPE_STAGE);

    Snowflakes->active = ACTIVE_ALWAYS;
}

Vector2 Snowflakes_HandleWrap(int32 id)
{
    RSDK_THIS(Snowflakes);

    int32 x = self->positions[id].x;
    int32 y = self->positions[id].y;

    int32 mult = 128;
    if (!self->priority[id])
        mult = 64;

    int32 newX = x - (ScreenInfo->position.x << 8) * mult;
    while (newX < 0) newX += ScreenInfo->size.x << 16;

    int32 posX = ScreenInfo->position.x / ScreenInfo->size.x;
    if ((newX % (ScreenInfo->size.x << 16)) >> 16 < ScreenInfo->position.x % ScreenInfo->size.x)
        posX = ScreenInfo->position.x / ScreenInfo->size.x + 1;

    int32 posY = 0;
    if (y > (ScreenInfo->size.y + ScreenInfo->position.y) << 16)
        posY = -ScreenInfo->size.y;

    Vector2 pos;
    pos.x = (posX * ScreenInfo->size.x << 16) + (newX % (ScreenInfo->size.x << 16));
    pos.y = (posY << 16) + y;
    return pos;
}

#if GAME_INCLUDE_EDITOR
void Snowflakes_EditorDraw(void)
{
    RSDK_THIS(Snowflakes);

    RSDK.SetSpriteAnimation(Snowflakes->aniFrames, 5, &self->animator, true, 0);
    RSDK.DrawSprite(&self->animator, NULL, false);
}

void Snowflakes_EditorLoad(void) { Snowflakes->aniFrames = RSDK.LoadSpriteAnimation("PSZ2/Snowflakes.bin", SCOPE_STAGE); }
#endif

void Snowflakes_Serialize(void) {}
