// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: WaterfallSound Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

#if MANIA_USE_PLUS
ObjectWaterfallSound *WaterfallSound;

#if defined RETRO_PS1_BUILD
// PS1 (docs/ports/mania/32 3.0b): the check and the update below walked every entity slot twice a frame with foreach_all (~18 of
// Soundboard's ~56 hblanks in GHZ1). No code creates a WaterfallSound: the scene's are collected once per stage load, in
// slot order, and each use re-checks the class (one blanked since is skipped, as foreach_all would skip it). Slots, not
// pointers: a PS1 entity block may move. More than PS1_WATERFALL_MAX falls back to foreach_all.
#define PS1_WATERFALL_MAX (0x40)
static uint16 ps1WaterfallSlots[PS1_WATERFALL_MAX];
static int32 ps1WaterfallCount = -1; // -1: not collected for this stage load; -2: too many (foreach_all)

static void PS1WaterfallCollect(void)
{
    int32 count = 0;
    foreach_all(WaterfallSound, sound)
    {
        if (count < PS1_WATERFALL_MAX)
            ps1WaterfallSlots[count] = RSDK.GetEntitySlot(sound);
        ++count;
    }
    ps1WaterfallCount = count <= PS1_WATERFALL_MAX ? count : -2;
}

#define PS1_FOREACH_WATERFALL(entityOut)                                                                                                             \
    for (int32 ps1W = 0; ps1W < ps1WaterfallCount; ++ps1W)                                                                                           \
        for (EntityWaterfallSound *entityOut = RSDK_GET_ENTITY(ps1WaterfallSlots[ps1W], WaterfallSound);                                             \
             entityOut && entityOut->classID == WaterfallSound->classID; entityOut = NULL)
#endif

void WaterfallSound_Update(void) {}

void WaterfallSound_LateUpdate(void) {}

void WaterfallSound_StaticUpdate(void) {}

void WaterfallSound_Draw(void)
{
    RSDK_THIS(WaterfallSound);

    RSDK.DrawLine(self->position.x - TILE_SIZE * self->size.x, self->position.y - TILE_SIZE * self->size.y,
                  self->position.x + TILE_SIZE * self->size.x, self->position.y - TILE_SIZE * self->size.y, 0xFFFF00, 0xFF, INK_NONE, false);
    RSDK.DrawLine(self->position.x - TILE_SIZE * self->size.x, self->position.y + TILE_SIZE * self->size.y,
                  self->position.x + TILE_SIZE * self->size.x, self->position.y + TILE_SIZE * self->size.y, 0xFFFF00, 0xFF, INK_NONE, false);
    RSDK.DrawLine(self->position.x - TILE_SIZE * self->size.x, self->position.y - TILE_SIZE * self->size.y,
                  self->position.x - TILE_SIZE * self->size.x, self->position.y + TILE_SIZE * self->size.y, 0xFFFF00, 0xFF, INK_NONE, false);
    RSDK.DrawLine(self->position.x + TILE_SIZE * self->size.x, self->position.y - TILE_SIZE * self->size.y,
                  self->position.x + TILE_SIZE * self->size.x, self->position.y + TILE_SIZE * self->size.y, 0xFFFF00, 0xFF, INK_NONE, false);

    if (self->sfxActive)
        DrawHelpers_DrawCross(self->sfxPos.x, self->sfxPos.y, 0x200000, 0x200000, 0xFF00FF);
}

void WaterfallSound_Create(void *data)
{
    RSDK_THIS(WaterfallSound);
    if (!SceneInfo->inEditor) {
        self->updateRange.x = TILE_SIZE * self->size.x;
        self->updateRange.y = TILE_SIZE * self->size.y;
        self->visible       = true;
        self->active        = ACTIVE_NEVER;
        self->sfxPos.x      = 0;
        self->sfxPos.y      = 0;
        self->drawGroup     = Zone->objectDrawGroup[1];
    }
}

void WaterfallSound_StageLoad(void)
{
#if defined RETRO_PS1_BUILD
    ps1WaterfallCount = -1;
#endif
    if (globals->gameMode != MODE_COMPETITION)
        Soundboard_LoadSfx("Stage/WaterfallLoop.wav", 2820, WaterfallSound_SfxCheck_WaterfallLoop, WaterfallSound_SfxUpdate_WaterfallLoop);
}

#if defined RETRO_PS1_BUILD
#if PS1_CHECK_WATERFALL
volatile uint32 g_ps1WaterfallEarly = 0, g_ps1WaterfallEarlyWrong = 0; // 3.0e evidence: early-outs, and those the full test denies
#endif
// the foreach_all body of WaterfallSound_SfxCheck_WaterfallLoop (the PS1 walks the collected slots; 3.0e: an exact
// early-out before the first distance)
static void WaterfallSound_PS1CheckOne(EntityWaterfallSound *sound, int32 worldCenterX, int32 worldCenterY, int32 *pcount)
{
#define count (*pcount)
        sound->sfxActive = false;
        sound->sfxPos.x  = 0;
        sound->sfxPos.y  = 0;

        Hitbox hitbox;
        hitbox.left   = -(sound->size.x >> 12);
        hitbox.top    = -(sound->size.y >> 12);
        hitbox.right  = sound->size.x >> 12;
        hitbox.bottom = sound->size.y >> 12;

        if (MathHelpers_PointInHitbox(sound->position.x, sound->position.y, worldCenterX, worldCenterY, FLIP_NONE, &hitbox)) {
            ++count;
            sound->sfxActive = true;
            sound->sfxPos.x  = worldCenterX;
            sound->sfxPos.y  = worldCenterY;
        }
        else {
            // 3.0e: MathHelpers_Distance rounds sqrt(dx * dx + dy * dy) of whole pixels, never below the larger of dx and
            // dy: past the range on one axis the test below is false, so the root is skipped (the same answer)
            int32 ax = abs(worldCenterX - sound->position.x) >> 16, ay = abs(worldCenterY - sound->position.y) >> 16;
            int32 am = ax > ay ? ax : ay;
            if (am < 0x8000 && (am << 16) - 8 * sound->size.y - 8 * sound->size.x > 0x2800000) {
#if PS1_CHECK_WATERFALL // evidence build: the full test must agree (GDB reads the counters)
                g_ps1WaterfallEarly = g_ps1WaterfallEarly + 1;
                if (MathHelpers_Distance(sound->position.x, sound->position.y, worldCenterX, worldCenterY) - 8 * sound->size.y
                        - 8 * sound->size.x
                    <= 0x2800000)
                    g_ps1WaterfallEarlyWrong = g_ps1WaterfallEarlyWrong + 1;
#endif
                return;
            }
            if (MathHelpers_Distance(sound->position.x, sound->position.y, worldCenterX, worldCenterY) - 8 * sound->size.y - 8 * sound->size.x
                <= 0x2800000) {
                if (MathHelpers_ConstrainToBox(&sound->sfxPos, worldCenterX, worldCenterY, sound->position, hitbox)) {
                    if (MathHelpers_Distance(sound->sfxPos.x, sound->sfxPos.y, worldCenterX, worldCenterY) <= 0x2800000) {
                        sound->sfxActive = true;
                        ++count;
                    }
                }
            }
        }
#undef count
}
#endif

bool32 WaterfallSound_SfxCheck_WaterfallLoop(void)
{
    int32 worldCenterX = (ScreenInfo->position.x + ScreenInfo->center.x) << 16;
    int32 worldCenterY = (ScreenInfo->position.y + ScreenInfo->center.y) << 16;

    WaterfallSound->activeCount = 0;
    int32 count                 = 0;

#if defined RETRO_PS1_BUILD
    if (ps1WaterfallCount == -1)
        PS1WaterfallCollect();
    if (ps1WaterfallCount >= 0) {
        PS1_FOREACH_WATERFALL(sound) { WaterfallSound_PS1CheckOne(sound, worldCenterX, worldCenterY, &count); }
        WaterfallSound->activeCount = count;
        return WaterfallSound->activeCount > 0;
    }
#endif
    foreach_all(WaterfallSound, sound)
    {
        sound->sfxActive = false;
        sound->sfxPos.x  = 0;
        sound->sfxPos.y  = 0;

        Hitbox hitbox;
        hitbox.left   = -(sound->size.x >> 12);
        hitbox.top    = -(sound->size.y >> 12);
        hitbox.right  = sound->size.x >> 12;
        hitbox.bottom = sound->size.y >> 12;

        if (MathHelpers_PointInHitbox(sound->position.x, sound->position.y, worldCenterX, worldCenterY, FLIP_NONE, &hitbox)) {
            ++count;
            sound->sfxActive = true;
            sound->sfxPos.x  = worldCenterX;
            sound->sfxPos.y  = worldCenterY;
        }
        else {
            if (MathHelpers_Distance(sound->position.x, sound->position.y, worldCenterX, worldCenterY) - 8 * sound->size.y - 8 * sound->size.x
                <= 0x2800000) {
                if (MathHelpers_ConstrainToBox(&sound->sfxPos, worldCenterX, worldCenterY, sound->position, hitbox)) {
                    if (MathHelpers_Distance(sound->sfxPos.x, sound->sfxPos.y, worldCenterX, worldCenterY) <= 0x2800000) {
                        sound->sfxActive = true;
                        ++count;
                    }
                }
            }
        }
    }

    WaterfallSound->activeCount = count;
    return WaterfallSound->activeCount > 0;
}
void WaterfallSound_SfxUpdate_WaterfallLoop(int32 sfxID)
{
    int32 worldCenterX = (ScreenInfo->position.x + ScreenInfo->center.x) << 16;
    int32 worldCenterY = (ScreenInfo->position.y + ScreenInfo->center.y) << 16;
    int32 worldLeft    = worldCenterX - (ScreenInfo->center.x << 16);
    int32 worldRight   = worldCenterX + (ScreenInfo->center.x << 16);

    float pan        = 0.0f;
    float volDivisor = 0.0f;
    int32 dist       = 0x7FFF0000;

#if defined RETRO_PS1_BUILD
    if (ps1WaterfallCount >= 0) {
        PS1_FOREACH_WATERFALL(sound)
        {
            if (sound->sfxActive) {
                int16 sqRoot   = MIN(MathHelpers_Distance(sound->sfxPos.x, sound->sfxPos.y, worldCenterX, worldCenterY) >> 16, 640);
                float volume   = (sqRoot / -640.0f) + 1.0f;
                float distance = -1.0;
                if (sound->sfxPos.x > worldLeft) {
                    distance = 1.0;
                    if (sound->sfxPos.x < worldRight) {
                        distance = (((sound->sfxPos.x - worldCenterX) >> 16) / (float)ScreenInfo->center.x);
                    }
                }
                volDivisor += volume;
                if (dist >= (sqRoot << 16))
                    dist = (sqRoot << 16);
                pan += volume * distance;
            }
        }
    }
    else
#endif
    {
    foreach_all(WaterfallSound, sound)
    {
        if (sound->sfxActive) {
            int16 sqRoot   = MIN(MathHelpers_Distance(sound->sfxPos.x, sound->sfxPos.y, worldCenterX, worldCenterY) >> 16, 640);
            float volume   = (sqRoot / -640.0f) + 1.0f;
            float distance = -1.0;
            if (sound->sfxPos.x > worldLeft) {
                distance = 1.0;
                if (sound->sfxPos.x < worldRight) {
                    distance = (((sound->sfxPos.x - worldCenterX) >> 16) / (float)ScreenInfo->center.x);
                }
            }
            volDivisor += volume;
            if (dist >= (sqRoot << 16))
                dist = (sqRoot << 16);
            pan += volume * distance;
        }
    }
    }

    float div = FABS(volDivisor);
    if (div > 0)
        pan /= volDivisor;

    float volume = MIN(dist >> 16, 640);
    RSDK.SetChannelAttributes(Soundboard->sfxChannel[sfxID], (volume / -640.0f) + 1.0, CLAMP(pan, -1.0, 1.0), 1.0f);
}

#if GAME_INCLUDE_EDITOR
void WaterfallSound_EditorDraw(void)
{
    RSDK_THIS(WaterfallSound);

    self->updateRange.x = TILE_SIZE * self->size.x;
    self->updateRange.y = TILE_SIZE * self->size.y;

    RSDK.DrawLine(self->position.x - TILE_SIZE * self->size.x, self->position.y - TILE_SIZE * self->size.y,
                  self->position.x + TILE_SIZE * self->size.x, self->position.y - TILE_SIZE * self->size.y, 0xFFFF00, 0xFF, INK_NONE, false);
    RSDK.DrawLine(self->position.x - TILE_SIZE * self->size.x, self->position.y + TILE_SIZE * self->size.y,
                  self->position.x + TILE_SIZE * self->size.x, self->position.y + TILE_SIZE * self->size.y, 0xFFFF00, 0xFF, INK_NONE, false);
    RSDK.DrawLine(self->position.x - TILE_SIZE * self->size.x, self->position.y - TILE_SIZE * self->size.y,
                  self->position.x - TILE_SIZE * self->size.x, self->position.y + TILE_SIZE * self->size.y, 0xFFFF00, 0xFF, INK_NONE, false);
    RSDK.DrawLine(self->position.x + TILE_SIZE * self->size.x, self->position.y - TILE_SIZE * self->size.y,
                  self->position.x + TILE_SIZE * self->size.x, self->position.y + TILE_SIZE * self->size.y, 0xFFFF00, 0xFF, INK_NONE, false);

    self->drawFX = FX_FLIP;
    RSDK.SetSpriteAnimation(WaterfallSound->aniFrames, 0, &self->animator, true, 0);

    Vector2 drawPos;
    drawPos.x = self->position.x;
    drawPos.y = self->position.y;
    drawPos.x -= (TILE_SIZE * self->size.x);
    drawPos.y -= (TILE_SIZE * self->size.y);

    self->direction = FLIP_NONE;
    RSDK.DrawSprite(&self->animator, &drawPos, false);

    drawPos.x += (TILE_SIZE * self->size.x) << 1;
    self->direction = FLIP_X;
    RSDK.DrawSprite(&self->animator, &drawPos, false);

    drawPos.y += (TILE_SIZE * self->size.y) << 1;
    self->direction = FLIP_XY;
    RSDK.DrawSprite(&self->animator, &drawPos, false);

    drawPos.x -= (TILE_SIZE * self->size.x) << 1;
    self->direction = FLIP_Y;
    RSDK.DrawSprite(&self->animator, &drawPos, false);
}

void WaterfallSound_EditorLoad(void) { WaterfallSound->aniFrames = RSDK.LoadSpriteAnimation("Global/TicMark.bin", SCOPE_STAGE); }
#endif

void WaterfallSound_Serialize(void) { RSDK_EDITABLE_VAR(WaterfallSound, VAR_VECTOR2, size); }
#endif
