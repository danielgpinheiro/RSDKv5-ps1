// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: RockemSockem Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectRockemSockem *RockemSockem;

#if defined RETRO_PS1_BUILD
// P2 (docs/perf/57): DrawSprite for a coil joint / the base / the ball in the common case, in this overlay (as P7's
// Snowflakes path: the resident EXE has no room for a faster sprite path). Ten Rock'em Sock'ems at SPZ1 x 4864 draw ~80
// sprites a frame through the general path (17 hblanks). The entity's drawFX is FX_ROTATE | FX_FLIP: DrawSprite drops the
// rotation for ROTSTYLE_NONE, or where FULL / 45 / 90 degree styles round it to 0 (not 180: its case keeps it), and
// then draws FX_FLIP. The common case: that, FLIP_NONE, INK_NONE, a frame bound in the atlas at full resolution within one
// texture page, on a line of palette bank 0 with no bank-0 write pending. It does what DrawSprite -> PS1DrawFrame ->
// PS1TexRect do there (RSDKv5/RSDK/Graphics/Drawing.cpp, Graphics/PS1/PS1Sprites.cpp), in their order: the screen clip
// that sets validDraw, the CLUT back after a fade, the off-screen cull, the texture page and the GP0 64h rectangle (the
// same 6 words). Anything else: false, and the caller draws through DrawSprite
extern RSDKScreenInfo *RockemSockem_PS1Screen __asm__("_ZN4RSDK13currentScreenE");
extern uint8 RockemSockem_PS1ValidDraw __asm__("_ZN4RSDK9validDrawE");
extern uint8 RockemSockem_PS1LineBank[] __asm__("_ZN4RSDK13gfxLineBufferE");
extern volatile int32 g_ps1PalDiffs;
extern volatile uint32 g_ps1SpriteCulled, g_ps1SpriteClipTodo;
extern void RockemSockem_PS1FadeRestoreSlot(uint32 slot) __asm__("_ZN4RSDK18PS1FadeRestoreSlotEj");
extern uint32 *RockemSockem_PS1SpanBegin(int32 n) __asm__("_Z17PS1ChainSpanBegini");
extern void RockemSockem_PS1SpanEnd(uint32 *w, int32 n, uint32 *end) __asm__("_Z15PS1ChainSpanEndPmiS_");
#define ROCKEMSOCKEM_PS1_FRAME_SIZE (26) // the engine's SpriteFrame (the game's 18 bytes + hitbox count / offset, ps1U, ps1V, ps1Attr)

static bool32 RockemSockem_PS1Draw(Animator *animator, Vector2 *position)
{
    RSDK_THIS(RockemSockem);
    if (!animator->frames || self->direction != FLIP_NONE || self->inkEffect != INK_NONE || (self->drawFX & ~(FX_ROTATE | FX_FLIP)))
        return false;
    if (self->drawFX & FX_ROTATE) { // DrawSprite's rotation styles: the rotation it draws with must come out 0
        const int32 r = self->rotation;
        switch (animator->rotationStyle) {
            case ROTSTYLE_NONE: break;
            case ROTSTYLE_FULL:
                if (r & 0x1FF)
                    return false;
                break;
            case ROTSTYLE_45DEG:
                if ((r + 0x20) & 0x1C0)
                    return false;
                break;
            case ROTSTYLE_90DEG:
                if ((r + 0x40) & 0x180)
                    return false;
                break;
            default: return false; // (180: its case keeps FX_ROTATE; static frames: the rotozoom path)
        }
    }
    const uint8 *f        = (const uint8 *)animator->frames + ROCKEMSOCKEM_PS1_FRAME_SIZE * animator->frameID;
    const SpriteFrame *fr = (const SpriteFrame *)f;
    const int32 w = fr->width, h = fr->height;
    const uint32 attr = *(const uint16 *)(f + 24), u = f[22], v = f[23];
    if (w <= 0 || h <= 0 || attr == 0xFFFF || ((attr >> 6) & 1) || w > 1023 || h > 511 || u + (uint32)w > 256)
        return false; // (not bound / halved or quartered / too big / over a page: DrawSprite's own path)
    RSDKScreenInfo *sc = RockemSockem_PS1Screen;
    const Vector2 *pos = position ? position : &self->position;
    const int32 x = (pos->x >> 16) - sc->position.x + fr->pivotX, y = (pos->y >> 16) - sc->position.y + fr->pivotY;
    const int32 sh = sc->size.y;
    if (RockemSockem_PS1LineBank[y < 0 ? 0 : y >= sh ? sh - 1 : y] || g_ps1PalDiffs)
        return false; // (a per-draw CLUT copy)
    // DrawSpriteFlipped's clip: a non-empty rectangle sets validDraw
    int32 x0 = x > sc->clipBound_X1 ? x : sc->clipBound_X1, x1 = x + w < sc->clipBound_X2 ? x + w : sc->clipBound_X2;
    int32 y0 = y > sc->clipBound_Y1 ? y : sc->clipBound_Y1, y1 = y + h < sc->clipBound_Y2 ? y + h : sc->clipBound_Y2;
    if (x1 <= x0 || y1 <= y0)
        return true;
    RockemSockem_PS1ValidDraw = true;
    const uint32 slot = attr >> 7; // (full resolution: no quarter CLUT offset)
    RockemSockem_PS1FadeRestoreSlot(slot);
    const uint32 tpage = (attr & 15) | ((attr >> 4) & 1) << 4 | ((attr >> 5) & 1) << 7;
    const uint32 clut  = slot == 0x1FF ? (uint32)(240 << 6) : (slot % 20) | (496 + slot / 20) << 6;
    const int32 sw     = sc->size.x;
    if (x >= sw || y >= sh || x + w <= 0 || y + h <= 0) {
        g_ps1SpriteCulled = g_ps1SpriteCulled + 1;
        return true;
    }
    if (sc->clipBound_X1 > 0 || sc->clipBound_Y1 > 0 || sc->clipBound_X2 < sw || sc->clipBound_Y2 < sh)
        g_ps1SpriteClipTodo = g_ps1SpriteClipTodo + 1;
    uint32 *wr = RockemSockem_PS1SpanBegin(6);
    if (!wr)
        return true;
    wr[1] = 0xE1000000 | tpage;
    wr[2] = 0x64000000 | 0x808080;
    wr[3] = (uint32)(uint16)y << 16 | (uint16)x;
    wr[4] = clut << 16 | v << 8 | u;
    wr[5] = (uint32)h << 16 | (uint32)w;
    RockemSockem_PS1SpanEnd(wr, 5, wr + 6);
    return true;
}
#define ROCKEMSOCKEM_DRAW(animator, pos)                                                                                                             \
    do {                                                                                                                                             \
        if (!RockemSockem_PS1Draw(animator, pos))                                                                                                    \
            RSDK.DrawSprite(animator, pos, false);                                                                                                   \
    } while (0)
#else
#define ROCKEMSOCKEM_DRAW(animator, pos) RSDK.DrawSprite(animator, pos, false)
#endif

void RockemSockem_Update(void)
{
    RSDK_THIS(RockemSockem);

    RSDK.ProcessAnimation(&self->ballAnimator);

    if (self->bounceTimer > 0) {
        if (!--self->bounceTimer)
            RSDK.SetSpriteAnimation(RockemSockem->aniFrames, 2, &self->ballAnimator, true, self->ballAnimator.frameID);
    }

    if (self->reboundTimer > 0) {
        if (!--self->reboundTimer) {
            self->position.x = self->startPos.x;
            self->position.y = self->startPos.y;
            self->velocity.x = 0;
            self->velocity.y = 0;
            self->active     = ACTIVE_BOUNDS;
        }
    }

    self->position.x += self->velocity.x;
    self->position.y += self->velocity.y;
    self->velocity.x = self->velocity.x + ((self->startPos.x - self->position.x) >> 6) - (self->velocity.x >> 4);
    self->velocity.y = self->velocity.y + ((self->startPos.y - self->position.y) >> 6) - (self->velocity.y >> 4);

    foreach_active(Player, player)
    {
        if (Player_CheckBadnikTouch(player, self, &RockemSockem->hitbox)) {
            int32 vel = abs(player->velocity.x) <= abs(player->velocity.y) ? abs(player->velocity.y) : abs(player->velocity.x);

            int32 angle        = RSDK.ATan2((player->position.x - self->position.x) / 3, player->position.y - self->position.y);
            int32 distX        = abs(self->position.x - self->startPos.x);
            int32 distY        = abs(self->position.y - self->startPos.y);
            self->reboundTimer = 120;

            int32 dx = (distX >> 16) * (distX >> 16);
            int32 dy = (distY >> 16) * (distY >> 16);

            if (dx + dy < 0x100 && !self->bounceTimer) {
                if ((angle - 0x20) & 0xC0) {
                    self->velocity.x -= vel * RSDK.Cos256(angle) >> 8;
                    self->velocity.y -= vel * RSDK.Sin256(angle) >> 8;
                }
                else {
                    self->velocity.x -= vel * RSDK.Cos256(angle) >> 9;
                    self->velocity.y -= 0x2000;
                }

                self->velocity.x = CLAMP(self->velocity.x, -0x40000, 0x40000);
                player->velocity.x -= player->velocity.x >> 4;
                if (player->velocity.y <= 0)
                    player->velocity.y -= player->velocity.y >> 5;
                else
                    player->velocity.y -= player->velocity.y >> 4;

                self->active = ACTIVE_NORMAL;
            }
            else if (player->velocity.y < 0) {
                self->active = ACTIVE_NORMAL;
            }
            else {
                int32 angleVal = ((angle - 0x20) >> 6) & 3;

                int32 anim = player->animator.animationID;
                switch (angleVal) {
                    default:
                    case 0:
                        player->velocity.x = ((RSDK.Cos256(angle) << 9) + player->velocity.x) >> 1;
                        player->velocity.y = -(abs(RSDK.Sin256(angle)) * 0x400);

                        if (anim != ANI_JUMP && anim != ANI_JOG && anim != ANI_RUN && anim != ANI_DASH)
                            player->animator.animationID = ANI_WALK;
                        break;

                    case 1:
                        player->velocity.x = (0x500 * RSDK.Cos256(angle) + player->velocity.x) >> 1;
                        player->velocity.y = -(abs(RSDK.Sin256(angle)) * 0x500);

                        RSDK.SetSpriteAnimation(player->aniFrames, ANI_SPRING_CS, &player->animator, false, 0);
                        break;

                    case 2:
                        player->velocity.x = (0x700 * RSDK.Cos256(angle) + player->velocity.x) >> 1;
                        player->velocity.y = -(abs(RSDK.Sin256(angle)) * 0x700);

                        RSDK.SetSpriteAnimation(player->aniFrames, ANI_SPRING_CS, &player->animator, false, 0);
                        break;

                    case 3: break;
                }

                if (self->ballAnimator.animationID != 3)
                    RSDK.PlaySfx(RockemSockem->sfxRockemSockem, false, 0xFF);

                // Bug Details:
                // this one's actually the opposite of normal, since its fixed in release builds
                // but if you comment out this Player_State_Air line and glide into the rockem sockem as knux
                // you'll get the funny anim bug they showed off at SDCC 2017
                player->state    = Player_State_Air;
                player->onGround = false;
                RSDK.SetSpriteAnimation(RockemSockem->aniFrames, 3, &self->ballAnimator, true, self->ballAnimator.frameID);
                self->bounceTimer = 16;
                self->active      = ACTIVE_NORMAL;
            }
        }
    }

    if (self->position.y > self->drawPos.y - 0xE0000) {
        self->position.y  = self->drawPos.y - 0xE0000;
        self->bounceTimer = 16;
    }

    self->angleVel = 0;
    if (abs(self->startPos.x - self->position.x) > 0x10000)
        self->angleVel = (self->startPos.x - self->position.x) >> 17;

    RockemSockem_HandleJointPositions();
}

void RockemSockem_LateUpdate(void) {}

void RockemSockem_StaticUpdate(void) {}

void RockemSockem_Draw(void)
{
    RSDK_THIS(RockemSockem);

    for (int32 i = 0; i < ROCKEMSOCKEM_COIL_COUNT; ++i) {
        self->rotation = self->jointRotations[i];
        ROCKEMSOCKEM_DRAW(&self->jointAnimator, &self->jointPositions[i]);
    }

    ROCKEMSOCKEM_DRAW(&self->baseAnimator, &self->drawPos);

    if (self->ballAnimator.frameID > 9)
        self->direction = FLIP_X;
    ROCKEMSOCKEM_DRAW(&self->ballAnimator, NULL);

    self->direction = FLIP_NONE;
}

void RockemSockem_Create(void *data)
{
    RSDK_THIS(RockemSockem);

    RSDK.SetSpriteAnimation(RockemSockem->aniFrames, 0, &self->baseAnimator, true, 0);
    RSDK.SetSpriteAnimation(RockemSockem->aniFrames, 1, &self->jointAnimator, true, 0);
    RSDK.SetSpriteAnimation(RockemSockem->aniFrames, 2, &self->ballAnimator, true, 0);

    if (!SceneInfo->inEditor) {
        self->drawPos = self->position;
        self->position.y -= (0xE0000 + (0x50000 * ROCKEMSOCKEM_COIL_COUNT));
        self->startPos      = self->position;
        self->angle         = 0x80;
        self->active        = ACTIVE_BOUNDS;
        self->updateRange.x = 0x800000;
        self->updateRange.y = 0x800000;
        self->visible       = true;
        self->drawGroup     = Zone->objectDrawGroup[0];
        self->drawFX        = FX_ROTATE | FX_FLIP;
    }
}

void RockemSockem_StageLoad(void)
{
    if (RSDK.CheckSceneFolder("SPZ1"))
        RockemSockem->aniFrames = RSDK.LoadSpriteAnimation("SPZ1/RockemSockem.bin", SCOPE_STAGE);
    else
        RockemSockem->aniFrames = RSDK.LoadSpriteAnimation("SPZ2/RockemSockem.bin", SCOPE_STAGE);

    RockemSockem->hitbox.left   = -16;
    RockemSockem->hitbox.top    = -14;
    RockemSockem->hitbox.right  = 16;
    RockemSockem->hitbox.bottom = 14;

    RockemSockem->sfxRockemSockem = RSDK.GetSfx("Stage/RockemSockem.wav");
}

void RockemSockem_HandleJointPositions(void)
{
    RSDK_THIS(RockemSockem);

    int32 x = (RSDK.Sin256(self->angle) << 11) + self->drawPos.x;
    int32 y = (RSDK.Cos256(self->angle) << 11) + self->drawPos.y;

    int32 ang  = RSDK.ATan2(y - self->position.y, (RSDK.Sin256(self->angle) << 11) + self->drawPos.x - self->position.x);
    int32 posX = (0xC00 * RSDK.Sin256(ang) + self->position.x) & 0xFFFF0000;
    int32 posY = (0xC00 * RSDK.Cos256(ang) + self->position.y) & 0xFFFF0000;

    int32 angle = self->angle;
    x >>= 16;
    y >>= 16;
    posX >>= 16;
    posY >>= 16;

    self->jointRotations[0]   = 2 * (640 - angle);
    self->jointPositions[0].x = 0x10002 * x;
    self->jointPositions[0].y = 0x10002 * y;

    angle += self->angleVel;
    self->jointRotations[1]   = 2 * (640 - angle);
    self->jointPositions[1].x = 0x2AAB * (x + posX + 4 * x);
    self->jointPositions[1].y = 0x2AAB * (y + posY + 4 * y);

    angle += self->angleVel;
    self->jointRotations[2]   = 2 * (640 - angle);
    self->jointPositions[2].x = 0x5556 * (posX + 2 * x);
    self->jointPositions[2].y = 0x5556 * (posY + 2 * y);

    angle += self->angleVel;
    self->jointRotations[3]   = 2 * (640 - angle);
    self->jointPositions[3].x = 0x8001 * (x + posX);
    self->jointPositions[3].y = 0x8001 * (y + posY);

    angle += self->angleVel;
    self->jointRotations[4]   = 2 * (640 - angle);
    self->jointPositions[4].x = 0x5556 * (x + 2 * posX);
    self->jointPositions[4].y = 0x5556 * (y + 2 * posY);

    angle -= self->angleVel;
    self->jointRotations[5]   = 2 * (640 - angle);
    self->jointPositions[5].x = 0x2AAB * (posX + x + 4 * posX);
    self->jointPositions[5].y = 0x2AAB * (posY + y + 4 * posY);
}

#if GAME_INCLUDE_EDITOR
void RockemSockem_EditorDraw(void)
{
    RSDK_THIS(RockemSockem);

    self->drawPos = self->position;
    self->position.y -= (0xE0000 + (0x50000 * ROCKEMSOCKEM_COIL_COUNT));
    self->startPos      = self->position;
    self->angle         = 0x80;
    self->active        = ACTIVE_BOUNDS;
    self->updateRange.x = 0x800000;
    self->updateRange.y = 0x800000;
    self->visible       = true;
    self->drawGroup     = Zone->objectDrawGroup[0];
    self->drawFX        = FX_ROTATE | FX_FLIP;

    self->angleVel = 0;
    if (abs(self->startPos.x - self->position.x) > 0x10000)
        self->angleVel = (self->startPos.x - self->position.x) >> 17;

    RockemSockem_HandleJointPositions();

    RockemSockem_Draw();
}

void RockemSockem_EditorLoad(void)
{
    if (RSDK.CheckSceneFolder("SPZ1"))
        RockemSockem->aniFrames = RSDK.LoadSpriteAnimation("SPZ1/RockemSockem.bin", SCOPE_STAGE);
    else
        RockemSockem->aniFrames = RSDK.LoadSpriteAnimation("SPZ2/RockemSockem.bin", SCOPE_STAGE);
}
#endif

void RockemSockem_Serialize(void) {}