// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: PaperRoller Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectPaperRoller *PaperRoller;

void PaperRoller_Update(void)
{
    RSDK_THIS(PaperRoller);

    RSDK.ProcessAnimation(&self->rollerAnimator);

    int32 startX  = self->position.x - (RSDK.Cos256(self->angle) << 7) * self->length;
    int32 startY  = self->position.y - (RSDK.Sin256(self->angle) << 7) * self->length;
    int32 endX    = self->position.x + (RSDK.Cos256(self->angle) << 7) * self->length;
    int32 endY    = self->position.y + (RSDK.Sin256(self->angle) << 7) * self->length;
    int32 centerX = self->position.x;
    int32 centerY = self->position.y;

    // left roller
    self->position.x = startX;
    self->position.y = startY;
    PaperRoller_HandleRollerCollisions();

    // right roller
    self->position.x = endX;
    self->position.y = endY;
    PaperRoller_HandleRollerCollisions();

    // print
    self->position.x = centerX;
    self->position.y = centerY;
    PaperRoller_HandlePrintCollisions();

    if (self->direction == FLIP_NONE)
        self->divotAngle += 6;
    else
        self->divotAngle -= 6;

    if (self->divotAngle < 0)
        self->divotAngle += 0x100;

    self->divotAngle &= 0xFF;
}

void PaperRoller_LateUpdate(void) {}

void PaperRoller_StaticUpdate(void) {}

void PaperRoller_Draw(void)
{
    PaperRoller_DrawPaperLines();
    PaperRoller_DrawRollers();
}

void PaperRoller_Create(void *data)
{
    RSDK_THIS(PaperRoller);

    self->active        = ACTIVE_BOUNDS;
    self->drawGroup     = Zone->objectDrawGroup[0];
    self->startPos.x    = self->position.x;
    self->startPos.y    = self->position.y;
    self->visible       = true;
    self->drawFX        = FX_NONE;
    self->updateRange.x = 0x800000;
    self->updateRange.y = 0x800000;

    self->updateRange.x += abs((RSDK.Cos256(self->angle) << 8) * self->length);
    self->updateRange.y += abs((RSDK.Sin256(self->angle) << 8) * self->length);

    if (self->length > 256)
        self->length = 256;

    self->lastJumpTimer[0] = 0x7FFF;
    self->lastJumpTimer[1] = 0x7FFF;
    self->lastJumpTimer[2] = 0x7FFF;
    self->lastJumpTimer[3] = 0x7FFF;

    self->hitbox.left   = -24;
    self->hitbox.top    = -24;
    self->hitbox.right  = 24;
    self->hitbox.bottom = 24;

    RSDK.SetSpriteAnimation(PaperRoller->aniFrames, 0, &self->rollerAnimator, true, 0);
    RSDK.SetSpriteAnimation(PaperRoller->aniFrames, 1, &self->divotAnimator, true, 0);
}

void PaperRoller_StageLoad(void)
{

    PaperRoller->aniFrames = RSDK.LoadSpriteAnimation("PSZ1/PaperRoller.bin", SCOPE_STAGE);

    color colors[] = { 0x0F0F0E8, 0x0D0B898, 0x987870, 0x586868 };
    for (int32 i = 0; i < 0x40; ++i) PaperRoller->colors[i] = colors[i & 3];

    PaperRoller->sfxPaper = RSDK.GetSfx("PSZ/Paper.wav");
}

void PaperRoller_DrawDeformedLine(int32 startX, int32 startY, int32 endX, int32 endY, int32 offsetX, int32 offsetY, int32 deformX, int32 deformY,
                                  int32 len, uint32 *color)
{
    RSDK_THIS(PaperRoller);

    int32 count = (self->length / 4) + ((self->length % 4) > 0);
#if defined RETRO_PS1_BUILD && PS1_LINE_BATCH
    // docs/32 9q: the lines this call draws, sent at the end in one bulk (PS1DrawLinesWorld: DrawLine's INK_NONE words)
    int32 ps1Lines[4 * 64];
    uint32 ps1Colors[64];
    int32 ps1N = 0;
#define PS1_PAPER_LINE(ax, ay, bx, by, c)                                                                              \
    do {                                                                                                               \
        if (ps1N < 64) {                                                                                               \
            ps1Lines[4 * ps1N] = (ax), ps1Lines[4 * ps1N + 1] = (ay), ps1Lines[4 * ps1N + 2] = (bx);                   \
            ps1Lines[4 * ps1N + 3] = (by), ps1Colors[ps1N++] = (c);                                                    \
        }                                                                                                              \
        else                                                                                                           \
            RSDK.DrawLine((ax), (ay), (bx), (by), (c), 0x7F, INK_NONE, false);                                         \
    } while (0)
#else
#define PS1_PAPER_LINE(ax, ay, bx, by, c) RSDK.DrawLine((ax), (ay), (bx), (by), (c), 0x7F, INK_NONE, false)
#endif
    if (count) {
        int32 negAngle = -self->angle & 0xFF;

        int32 currentX = startX;
        int32 currentY = startY;
        int32 moveX    = (endX - startX) / count;
        int32 moveY    = (endY - startY) / count;

        for (int32 i = 0; i < count; ++i) {
            int32 colorID = self->direction ? (len % count) : (count - len % count - 1);

            uint32 lineColor = color ? *color : PaperRoller->colors[(Zone->timer + colorID) % count];

            if (!deformY) {
                PS1_PAPER_LINE(offsetX + currentX, offsetY + currentY, offsetX + (currentX + moveX), offsetY + (currentY + moveY), lineColor);
            }
            else {
                int32 distX = (currentX - self->position.x) >> 8;
                int32 distY = (currentY - self->position.y) >> 8;

                int32 angValX = distY * RSDK.Sin256(self->angle) + distX * RSDK.Cos256(self->angle);
                int32 angValY = self->position.y - distX * RSDK.Sin256(self->angle) + distY * RSDK.Cos256(self->angle) - self->position.y;

                int32 lenY    = 0;

                if (angValX >= deformX) {
                    int32 length = abs((self->length << 15) - deformX);

                    if (length >> 16 > 0)
                        lenY = (deformY >> 8) * ((length + deformX - angValX) >> 8) / (length >> 16);
                }
                else {
                    int32 length = length = abs(deformX + (self->length << 15));

                    if (length >> 16 > 0)
                        lenY = (deformY >> 8) * (((self->length << 15) + angValX) >> 8) / (length >> 16);
                }

                distX = angValX >> 8;
                distY = (lenY + angValY) >> 8;

                Vector2 offset1;
                offset1.x = self->position.x + distY * RSDK.Sin256(negAngle) + distX * RSDK.Cos256(negAngle);
                offset1.y = self->position.y - distX * RSDK.Sin256(negAngle) + distY * RSDK.Cos256(negAngle);

                distX = ((currentX + moveX) - self->position.x) >> 8;
                distY = ((currentY + moveY) - self->position.y) >> 8;

                angValX = distY * RSDK.Sin256(self->angle) + distX * RSDK.Cos256(self->angle);
                angValY = self->position.y - distX * RSDK.Sin256(self->angle) + distY * RSDK.Cos256(self->angle) - self->position.y;

                lenY = 0;
                if (angValX >= deformX) {
                    int32 length = abs((self->length << 15) - deformX);

                    if (length >> 16 > 0)
                        lenY = (deformY >> 8) * ((length + deformX - angValX) >> 8) / (length >> 16);
                }
                else {
                    int32 length = abs((self->length << 15) + deformX);

                    if (length >> 16 > 0)
                        lenY = (deformY >> 8) * (((self->length << 15) + angValX) >> 8) / (length >> 16);
                }

                distX          = angValX >> 8;
                distY          = (lenY + angValY) >> 8;

                Vector2 offset2;
                offset2.x = self->position.x + distY * RSDK.Sin256(negAngle) + distX * RSDK.Cos256(negAngle);
                offset2.y = self->position.y - distX * RSDK.Sin256(negAngle) + distY * RSDK.Cos256(negAngle);

                PS1_PAPER_LINE(offsetX + offset1.x, offsetY + offset1.y, offsetX + offset2.x, offsetY + offset2.y, lineColor);
            }

            ++len;

            currentX += moveX;
            currentY += moveY;
        }
    }
#if defined RETRO_PS1_BUILD && PS1_LINE_BATCH
    if (ps1N)
        PS1DrawLinesWorld(ps1Lines, ps1Colors, ps1N);
#endif
#undef PS1_PAPER_LINE
}

#if defined RETRO_PS1_BUILD && PS1_LINE_BATCH
// docs/32 9u: PaperRoller_DrawPaperLines' three DrawDeformedLine calls a side compute the same points (only the final
// offset and the colour differ): the points once (the sines / cosines of the two angles read once, every expression as
// DrawDeformedLine's), then the three calls' line batches in their order, the offset added to each end as there. PSZ1's
// rollers: ~190 hblanks a frame of this math at the busiest spot. A side of more than 64 segments (DrawDeformedLine draws
// the rest one by one, before its batch) keeps the stock calls.
// 9v.6: a segment's end is the next one's start (DrawDeformedLine's second point is current + move, the next segment's
// first): each of the count + 1 vertices computed once (vx / vy), not twice (two divisions a point)
static int32 PaperRoller_PS1Points(int32 startX, int32 startY, int32 endX, int32 endY, int32 deformX, int32 deformY, int32 len,
                                   int32 *vx, int32 *vy, uint32 *colIndex)
{
    RSDK_THIS(PaperRoller);
    int32 count = (self->length / 4) + ((self->length % 4) > 0);
    if (!count || count > 64)
        return count ? -1 : 0;
    int32 negAngle = -self->angle & 0xFF;
    int32 sinA = RSDK.Sin256(self->angle), cosA = RSDK.Cos256(self->angle);
    int32 sinN = RSDK.Sin256(negAngle), cosN = RSDK.Cos256(negAngle);
    int32 currentX = startX, currentY = startY;
    int32 moveX = (endX - startX) / count, moveY = (endY - startY) / count;
    // (both of DrawDeformedLine's length expressions are the same sums)
    const int32 lenA = abs((self->length << 15) - deformX), lenB = abs(deformX + (self->length << 15));
    for (int32 k = 0; k <= count; ++k) {
        if (k < count)
            colIndex[k] = (uint32)(self->direction ? ((len + k) % count) : (count - (len + k) % count - 1));
        if (!deformY) {
            vx[k] = currentX, vy[k] = currentY;
        }
        else {
            int32 distX = (currentX - self->position.x) >> 8;
            int32 distY = (currentY - self->position.y) >> 8;
            int32 angValX = distY * sinA + distX * cosA;
            int32 angValY = self->position.y - distX * sinA + distY * cosA - self->position.y;
            int32 lenY = 0;
            if (angValX >= deformX) {
                if (lenA >> 16 > 0)
                    lenY = (deformY >> 8) * ((lenA + deformX - angValX) >> 8) / (lenA >> 16);
            }
            else {
                if (lenB >> 16 > 0)
                    lenY = (deformY >> 8) * (((self->length << 15) + angValX) >> 8) / (lenB >> 16);
            }
            distX = angValX >> 8;
            distY = (lenY + angValY) >> 8;
            vx[k] = self->position.x + distY * sinN + distX * cosN;
            vy[k] = self->position.y - distX * sinN + distY * cosN;
        }
        currentX += moveX;
        currentY += moveY;
    }
    return count;
}

#endif

void PaperRoller_DrawPaperLines(void)
{
    RSDK_THIS(PaperRoller);

    int32 x1 = self->position.x - (RSDK.Cos256(self->angle) << 7) * self->length;
    int32 y1 = self->position.y - (RSDK.Sin256(self->angle) << 7) * self->length;
    int32 x2 = self->position.x + (RSDK.Cos256(self->angle) << 7) * self->length;
    int32 y2 = self->position.y + (RSDK.Sin256(self->angle) << 7) * self->length;

    int32 len     = self->length / 4 + (self->length % 4 > 0);
    int32 startX1 = x1 + 0x1800 * RSDK.Cos256(self->angle - 64);
    int32 startY1 = y1 + 0x1800 * RSDK.Sin256(self->angle - 64);
    int32 endX1   = x2 + 0x1800 * RSDK.Cos256(self->angle - 64);
    int32 endY1   = y2 + 0x1800 * RSDK.Sin256(self->angle - 64);

    int32 startX2 = x2 + 0x1800 * RSDK.Cos256(self->angle + 64);
    int32 startY2 = y2 + 0x1800 * RSDK.Sin256(self->angle + 64);
    int32 endX2   = x1 + 0x1800 * RSDK.Cos256(self->angle + 64);
    int32 endY2   = y1 + 0x1800 * RSDK.Sin256(self->angle + 64);

#if defined RETRO_PS1_BUILD && PS1_LINE_BATCH
    int32 ps1Vx[2][65], ps1Vy[2][65];
    uint32 ps1Col[2][64];
    int32 ps1N1 = PaperRoller_PS1Points(startX1, startY1, endX1, endY1, self->deformPosTop.x, self->deformPosTop.y, 0, ps1Vx[0], ps1Vy[0], ps1Col[0]);
    int32 ps1N2 = PaperRoller_PS1Points(startX2, startY2, endX2, endY2, self->deformPosBottom.x, self->deformPosBottom.y, len, ps1Vx[1], ps1Vy[1],
                                        ps1Col[1]);
    bool32 ps1Fast = ps1N1 >= 0 && ps1N2 >= 0;
    if (!ps1Fast) {
#endif
    PaperRoller_DrawDeformedLine(startX1, startY1, endX1, endY1, 0, 0, self->deformPosTop.x, self->deformPosTop.y, 0, NULL);
    PaperRoller_DrawDeformedLine(startX2, startY2, endX2, endY2, 0, 0, self->deformPosBottom.x, self->deformPosBottom.y, len, NULL);
#if defined RETRO_PS1_BUILD && PS1_LINE_BATCH
    }
#endif

    int32 angle = self->angle + 32;
    if (angle < 0)
        angle = ((-1 - (self->angle + 32)) & 0xFFFFFF00) + self->angle + 288;

    int32 offsetY1 = 0, offsetY2 = 0, offsetX2 = 0, offsetX1 = 0;
    switch (angle >> 6) {
        case 0:
            offsetY1 = 0x10000;
            offsetY2 = -0x10000;
            break;

        case 1:
            offsetX1 = -0x10000;
            offsetX2 = 0x10000;
            break;

        case 2:
            offsetY1 = -0x10000;
            offsetY2 = 0x10000;
            break;

        case 3:
            offsetX1 = 0x10000;
            offsetX2 = -0x10000;
            break;
    }
#if defined RETRO_PS1_BUILD && PS1_LINE_BATCH
    if (ps1Fast) { // the stock order: top 0, bottom 0, top 1, bottom 1, top 2, bottom 2 (each call's batch)
        // a line whose two ends are both left of, right of, above or below the screen's drawing area (4 pixels of margin
        // past the 320 x 240 area) has no pixel in it: not sent (the same pictures; PSZ1's rollers off screen sent ~2,000
        // of them a frame). 9v.6: the vertices' screen positions and the colours once a side (the offsets are whole
        // pixels: FROM_FIXED(p + k * 0x10000) = FROM_FIXED(p) + k)
        int32 lines[4 * 64];
        uint32 colors[64];
        int32 sx[2][65], sy[2][65];
        uint32 col[2][64];
        const int32 scrX = ScreenInfo->position.x, scrY = ScreenInfo->position.y;
        for (int32 side = 0; side < 2; ++side) {
            int32 n = side ? ps1N2 : ps1N1;
            for (int32 k = 0; k <= n && n; ++k) sx[side][k] = FROM_FIXED(ps1Vx[side][k]) - scrX, sy[side][k] = FROM_FIXED(ps1Vy[side][k]) - scrY;
            for (int32 i = 0; i < n; ++i) col[side][i] = PaperRoller->colors[(Zone->timer + ps1Col[side][i]) % n];
        }
        for (int32 v = 0; v < 3; ++v)
            for (int32 side = 0; side < 2; ++side) {
                int32 n = side ? ps1N2 : ps1N1;
                if (!n)
                    continue;
                int32 ox = v * (side ? offsetX2 : offsetX1), oy = v * (side ? offsetY2 : offsetY1);
                const int32 px = ox >> 16, py = oy >> 16;
                const int32 *vx = ps1Vx[side], *vy = ps1Vy[side], *sxs = sx[side], *sys = sy[side];
                int32 k = 0;
                for (int32 i = 0; i < n; ++i) {
                    int32 sx1 = sxs[i] + px, sy1 = sys[i] + py, sx2 = sxs[i + 1] + px, sy2 = sys[i + 1] + py;
                    if ((sx1 < -4 && sx2 < -4) || (sx1 > 324 && sx2 > 324) || (sy1 < -4 && sy2 < -4) || (sy1 > 244 && sy2 > 244))
                        continue;
                    lines[4 * k] = ox + vx[i], lines[4 * k + 1] = oy + vy[i], lines[4 * k + 2] = ox + vx[i + 1], lines[4 * k + 3] = oy + vy[i + 1];
                    colors[k++] = v == 2 ? 0xD0B898 : col[side][i];
                }
                if (k)
                    PS1DrawLinesWorld(lines, colors, k);
            }
        return;
    }
#endif
    PaperRoller_DrawDeformedLine(startX1, startY1, endX1, endY1, offsetX1, offsetY1, self->deformPosTop.x, self->deformPosTop.y, 0, NULL);
    PaperRoller_DrawDeformedLine(startX2, startY2, endX2, endY2, offsetX2, offsetY2, self->deformPosBottom.x, self->deformPosBottom.y, len, NULL);

    color color = 0xD0B898;
    offsetX1 <<= 1;
    offsetY1 <<= 1;
    offsetX2 <<= 1;
    offsetY2 <<= 1;
    PaperRoller_DrawDeformedLine(startX1, startY1, endX1, endY1, offsetX1, offsetY1, self->deformPosTop.x, self->deformPosTop.y, 0, &color);
    PaperRoller_DrawDeformedLine(startX2, startY2, endX2, endY2, offsetX2, offsetY2, self->deformPosBottom.x, self->deformPosBottom.y, len, &color);
}

void PaperRoller_DrawRollers(void)
{
    RSDK_THIS(PaperRoller);

    Vector2 rollerPos;
    Vector2 divotPos;

    rollerPos = self->position;
    rollerPos.x -= (RSDK.Cos256(self->angle) << 7) * self->length;
    rollerPos.y -= (RSDK.Sin256(self->angle) << 7) * self->length;
    RSDK.DrawSprite(&self->rollerAnimator, &rollerPos, false);

    divotPos = rollerPos;
    divotPos.x += -0x900 * RSDK.Cos256(self->divotAngle);
    divotPos.y += -0x900 * RSDK.Sin256(self->divotAngle);
    RSDK.DrawSprite(&self->divotAnimator, &divotPos, false);

    divotPos = rollerPos;
    divotPos.x += 0x900 * RSDK.Cos256(self->divotAngle);
    divotPos.y += 0x900 * RSDK.Sin256(self->divotAngle);
    RSDK.DrawSprite(&self->divotAnimator, &divotPos, false);

    rollerPos = self->position;
    rollerPos.x += (RSDK.Cos256(self->angle) << 7) * self->length;
    rollerPos.y += (RSDK.Sin256(self->angle) << 7) * self->length;
    RSDK.DrawSprite(&self->rollerAnimator, &rollerPos, false);

    divotPos = rollerPos;
    divotPos.x += -0x900 * RSDK.Cos256(self->divotAngle);
    divotPos.y += -0x900 * RSDK.Sin256(self->divotAngle);
    RSDK.DrawSprite(&self->divotAnimator, &divotPos, false);

    divotPos = rollerPos;
    divotPos.x += 0x900 * RSDK.Cos256(self->divotAngle);
    divotPos.y += 0x900 * RSDK.Sin256(self->divotAngle);
    RSDK.DrawSprite(&self->divotAnimator, &divotPos, false);
}

void PaperRoller_HandleRollerCollisions(void)
{
    RSDK_THIS(PaperRoller);

    foreach_active(Player, player)
    {
        int32 playerID = RSDK.GetEntitySlot(player);
        if (self->playerTimer[playerID] <= 0) {
            int32 distX = abs(self->position.x - player->position.x) >> 16;
            int32 distY = abs(self->position.y - player->position.y) >> 16;
            if (MathHelpers_SquareRoot(distX * distX + distY * distY) <= 40 && !self->playerTimer[playerID]) {
                RSDK.PlaySfx(Player->sfxRelease, false, 255);
                int32 angle = RSDK.ATan2(player->position.x - self->position.x, player->position.y - self->position.y);

                int32 ang = angle + (self->direction == FLIP_NONE ? 64 : -64);

                player->position.x = self->position.x + 0x2800 * RSDK.Cos256(angle);
                player->position.y = self->position.y + 0x2800 * RSDK.Sin256(angle);
                player->velocity.x = RSDK.Cos256(ang) << 11;
                player->velocity.y = RSDK.Sin256(ang) << 11;
                player->state      = Player_State_Air;
                RSDK.SetSpriteAnimation(player->aniFrames, ANI_JUMP, &player->animator, false, 0);

                player->nextAirState     = StateMachine_None;
                player->nextGroundState  = StateMachine_None;
                player->onGround         = false;
                player->groundVel        = 0;
                // bug details: fixes a bug where knux can touch this without tile collisions enabled lol
#if GAME_VERSION != VER_100
                player->tileCollisions   = TILECOLLISION_DOWN;
#endif
                player->applyJumpCap     = false;
                player->jumpAbilityState = 0;

                self->playerTimer[playerID] = 10;
            }
        }
        else {
            self->playerTimer[playerID]--;
        }
    }
}

void PaperRoller_HandlePrintCollisions(void)
{
    RSDK_THIS(PaperRoller);

    bool32 hasDeformedTop    = false;
    bool32 hasDeformedBottom = false;

    int32 negAngle          = -self->angle;
    self->deformPosTop.x    = 0;
    self->deformPosTop.y    = 0;
    self->deformPosBottom.x = 0;
    self->deformPosBottom.y = 0;

    foreach_active(Player, player)
    {
        int32 playerID = RSDK.GetEntitySlot(player);

        if (player->jumpPress)
            self->lastJumpTimer[playerID] = 0;
        else
            ++self->lastJumpTimer[playerID];

        Vector2 pivotPos = player->position;
        Zone_RotateOnPivot(&pivotPos, &self->position, self->angle);

        int32 pivotX = pivotPos.x - self->position.x;
        int32 pivotY = pivotPos.y - self->position.y;

        if (abs(pivotX) <= self->length << 15 && abs(pivotY) <= 0x280000) {
            int32 deformY = 0;
            if (pivotY < 0) {
                if (abs(pivotY) > 0x180000) {
                    deformY = pivotY + 0x280000;
                }
                else {
                    Vector2 playerPos;
                    playerPos.x = self->position.x + pivotX;
                    playerPos.y = self->position.y - 0x180000;
                    Zone_RotateOnPivot(&playerPos, &self->position, negAngle);

                    Vector2 pivotPos = { 0, 0 };
                    player->position = playerPos;

                    Vector2 playerVel = player->velocity;
                    Zone_RotateOnPivot(&playerVel, &pivotPos, self->angle);
                    int32 angle = RSDK.ATan2(playerVel.x, -playerVel.y);

                    int32 force = player->jumpHold ? 12 : 6;

                    playerVel.x = force * (RSDK.Cos256(angle) << 8);
                    playerVel.y = force * (RSDK.Sin256(angle) << 8);
                    if (abs(playerVel.x) < 0x10000)
                        playerVel.x += ((2 * (self->direction == FLIP_NONE) - 1) << 16);

                    Zone_RotateOnPivot(&playerVel, &pivotPos, negAngle);
                    player->velocity = playerVel;

                    player->state = Player_State_Air;
                    RSDK.SetSpriteAnimation(player->aniFrames, ANI_JUMP, &player->animator, false, 0);

                    player->nextAirState     = StateMachine_None;
                    player->nextGroundState  = StateMachine_None;
                    player->onGround         = false;
                    player->groundVel        = 0;
                    // bug details: fixes a bug where knux can touch this without tile collisions enabled lol
#if GAME_VERSION != VER_100
                    player->tileCollisions = TILECOLLISION_DOWN;
#endif
                    player->applyJumpCap     = false;
                    player->jumpAbilityState = 0;
                    RSDK.PlaySfx(PaperRoller->sfxPaper, false, 0xFF);

                    deformY = 0x100000;
                }

                if (player->sidekick) {
                    if (!hasDeformedTop) {
                        self->deformPosTop.x = pivotX;
                        self->deformPosTop.y = deformY;
                    }
                }
                else {
                    self->deformPosTop.x = pivotX;
                    self->deformPosTop.y = deformY;
                    hasDeformedTop       = true;
                }
            }
            else {
                if (abs(pivotY) > 0x180000) {
                    deformY = pivotY - 0x280000;
                }
                else {
                    Vector2 playerPos;
                    playerPos.x = self->position.x + pivotX;
                    playerPos.y = self->position.y + 0x180000;
                    Zone_RotateOnPivot(&playerPos, &self->position, negAngle);

                    Vector2 pivotPos = { 0, 0 };
                    player->position = playerPos;

                    Vector2 playerVel = player->velocity;
                    Zone_RotateOnPivot(&playerVel, &pivotPos, self->angle);

                    int32 angle = RSDK.ATan2(playerVel.x, -playerVel.y);
                    int32 force = player->jumpHold ? 12 : 6;

                    playerVel.x = force * (RSDK.Cos256(angle) << 8);
                    playerVel.y = force * (RSDK.Sin256(angle) << 8);
                    if ((self->direction == FLIP_NONE && playerVel.x > -0x10000) || (self->direction == FLIP_X && playerVel.x < 0x10000))
                        playerVel.x += ((2 * (self->direction != FLIP_NONE) - 1) << 18);

                    Zone_RotateOnPivot(&playerVel, &pivotPos, negAngle);
                    player->velocity = playerVel;

                    player->state = Player_State_Air;
                    RSDK.SetSpriteAnimation(player->aniFrames, ANI_JUMP, &player->animator, false, 0);

                    player->nextAirState     = StateMachine_None;
                    player->nextGroundState  = StateMachine_None;
                    player->onGround         = false;
                    player->groundVel        = 0;
                    // bug details: fixes a bug where knux can touch this without tile collisions enabled lol
#if GAME_VERSION != VER_100
                    player->tileCollisions = TILECOLLISION_DOWN;
#endif
                    player->applyJumpCap     = false;
                    player->jumpAbilityState = 0;
                    RSDK.PlaySfx(PaperRoller->sfxPaper, false, 0xFF);

                    deformY = -0x100000;
                }

                if (!player->sidekick) {
                    self->deformPosBottom.x = pivotX;
                    self->deformPosBottom.y = deformY;
                    hasDeformedBottom       = true;
                }
                else if (!hasDeformedBottom) {
                    self->deformPosBottom.x = pivotX;
                    self->deformPosBottom.y = deformY;
                }
            }
        }
    }
}

#if GAME_INCLUDE_EDITOR
void PaperRoller_EditorDraw(void)
{
    RSDK_THIS(PaperRoller);

    self->drawGroup     = Zone->objectDrawGroup[0];
    self->startPos      = self->position;
    self->visible       = true;
    self->drawFX        = FX_NONE;
    self->updateRange.x = 0x800000;
    self->updateRange.y = 0x800000;

    int32 len = self->length;

    self->updateRange.x += abs((RSDK.Cos256(self->angle) << 8) * self->length);
    self->updateRange.y += abs((RSDK.Sin256(self->angle) << 8) * self->length);

    if (self->length > 256)
        self->length = 256;

    RSDK.SetSpriteAnimation(PaperRoller->aniFrames, 0, &self->rollerAnimator, true, 0);
    RSDK.SetSpriteAnimation(PaperRoller->aniFrames, 1, &self->divotAnimator, true, 0);

    PaperRoller_DrawPaperLines();
    PaperRoller_DrawRollers();

    self->length = len;
}

void PaperRoller_EditorLoad(void)
{
    PaperRoller_StageLoad();

    RSDK_ACTIVE_VAR(PaperRoller, direction);
    RSDK_ENUM_VAR("Right", FLIP_NONE);
    RSDK_ENUM_VAR("Left", FLIP_X);
}
#endif

void PaperRoller_Serialize(void)
{
    RSDK_EDITABLE_VAR(PaperRoller, VAR_UINT8, direction);
    RSDK_EDITABLE_VAR(PaperRoller, VAR_ENUM, length);
    RSDK_EDITABLE_VAR(PaperRoller, VAR_ENUM, angle);
}
