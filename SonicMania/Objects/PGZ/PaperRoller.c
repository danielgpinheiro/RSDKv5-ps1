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
    // (docs/ports/mania/32 9q batched these lines through PS1DrawLinesWorld; P12b (docs/perf/57) dropped that resident
    // function: on the PS1 this stock path runs only for a side of more than 64 segments, which Create's length clamp (256,
    // 4 px a segment) rules out, so it is DrawLine's own again, the pictures 9q found identical)
#define PS1_PAPER_LINE(ax, ay, bx, by, c) RSDK.DrawLine((ax), (ay), (bx), (by), (c), 0x7F, INK_NONE, false)
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
#undef PS1_PAPER_LINE
}

#if defined RETRO_PS1_BUILD && PS1_LINE_BATCH
#ifndef PS1_PAPER_PALE_LINE
#define PS1_PAPER_PALE_LINE 1 // P12b M2: a straight side's pale third pass as one line (0: its segments, the stock pixels)
#endif
// docs/ports/mania/32 9u: PaperRoller_DrawPaperLines' three DrawDeformedLine calls a side compute the same points (only the final
// offset and the colour differ): the points once (the sines / cosines of the two angles read once, every expression as
// DrawDeformedLine's), then the three calls' line batches in their order, the offset added to each end as there. PSZ1's
// rollers: ~190 hblanks a frame of this math at the busiest spot. A side of more than 64 segments (DrawDeformedLine draws
// the rest one by one, before its batch) keeps the stock calls.
// 9v.6: a segment's end is the next one's start (DrawDeformedLine's second point is current + move, the next segment's
// first): each of the count + 1 vertices computed once (vx / vy), not twice (two divisions a point)
// P12b (docs/perf/57): the segments' colour indices (a remainder each) are no longer made here: DrawPaperLines steps them
__attribute__((optimize("O2"))) static int32 PaperRoller_PS1Points(int32 startX, int32 startY, int32 endX, int32 endY, int32 deformX, int32 deformY,
                                   int32 *vx, int32 *vy)
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

#if defined RETRO_PS1_BUILD
__attribute__((optimize("O2"))) // P12 (docs/perf/57): this overlay-only function at -O2 (the paper lines' loops)
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
    int32 ps1N1 = PaperRoller_PS1Points(startX1, startY1, endX1, endY1, self->deformPosTop.x, self->deformPosTop.y, ps1Vx[0], ps1Vy[0]);
    int32 ps1N2 = PaperRoller_PS1Points(startX2, startY2, endX2, endY2, self->deformPosBottom.x, self->deformPosBottom.y, ps1Vx[1], ps1Vy[1]);
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
        // P12b (docs/perf/57): the lines written straight from the vertices' screen positions by PS1DrawLineStrip (each vertex
        // packed and tested once a pass, the colours as their GP0 40h words): no trip back to world coordinates and out again
        // through 9q's PS1DrawLinesWorld (removed); the same culls, the same words in the same order
        int32 sx[2][65], sy[2][65];
        uint32 col[2][64];
        bool32 strip = true;
        const int32 scrX = ScreenInfo[SceneInfo->currentScreenID].position.x, scrY = ScreenInfo[SceneInfo->currentScreenID].position.y;
        // P12 (docs/perf/57): a side whose every vertex lies past one edge of that area by more than the offsets (at most 2
        // pixels) would have every line left out below: it is skipped, its colours too (they only feed those lines); the
        // colour index (timer + c) % n with c in [0, n) as one remainder a side and a subtraction
        // P12b: a vertex at least 2 px inside the cull's area (x in [-2, 322], y in [-2, 242]) stays inside it, and inside
        // the GPU's range, at every pass's offset (|offset| <= 2): marked inner, PS1DrawLineStrip gives it outcode 0 at
        // every pass without testing it
        // (DrawLine's GP0 colour word: 0x40, then the colour's blue, green, red bytes)
#define PS1_PAPER_CMD(c) (0x40000000u | ((c) & 0xFF) << 16 | ((c) & 0xFF00) | (((c) >> 16) & 0xFF))
        uint32 cmd[2][64];
        static uint32 ps1Pale[64]; // the third pass's one colour as each line's GP0 word (filled once)
        if (!ps1Pale[0])
            for (int32 i = 0; i < 64; ++i) ps1Pale[i] = PS1_PAPER_CMD(0xD0B898u);
        uint8 inner[2][65];
        bool32 off[2];
        for (int32 side = 0; side < 2; ++side) {
            int32 n = side ? ps1N2 : ps1N1;
            int32 x0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF;
            for (int32 k = 0; k <= n && n; ++k) {
                int32 kx = FROM_FIXED(ps1Vx[side][k]) - scrX, ky = FROM_FIXED(ps1Vy[side][k]) - scrY;
                sx[side][k] = kx, sy[side][k] = ky;
                x0 = kx < x0 ? kx : x0, x1 = kx > x1 ? kx : x1, y0 = ky < y0 ? ky : y0, y1 = ky > y1 ? ky : y1;
                const uint32 in = (uint32)(kx + 2) <= 324u && (uint32)(ky + 2) <= 244u;
                inner[side][k] = (uint8)in;
            }
            off[side] = !n || x1 + 2 < -4 || x0 - 2 > 324 || y1 + 2 < -4 || y0 - 2 > 244;
            if (off[side])
                continue;
            // segment i's colour: colors[(timer + c_i) % n], c_i = DrawDeformedLine's colorID for len L (0 on the top side, len
            // on the bottom one): (L + i) % n, or n - 1 - (L + i) % n when direction is 0. So the index starts at
            // (t + c_0) % n (t = timer % n; t + c_0 < 2n: one subtraction) and steps by +1 (or -1) mod n: no remainder a segment
            const uint32 un = (uint32)n, t = (uint32)Zone->timer % un, r0 = (uint32)(side ? len : 0) % un;
            uint32 q = t + (self->direction ? r0 : un - 1 - r0);
            q = q >= un ? q - un : q;
            if (self->direction)
                for (int32 i = 0; i < n; ++i) {
                    const uint32 c = PaperRoller->colors[q];
                    col[side][i] = c, cmd[side][i] = PS1_PAPER_CMD(c);
                    q = q + 1 == un ? 0 : q + 1;
                }
            else
                for (int32 i = 0; i < n; ++i) {
                    const uint32 c = PaperRoller->colors[q];
                    col[side][i] = c, cmd[side][i] = PS1_PAPER_CMD(c);
                    q = q ? q - 1 : un - 1;
                }
        }
        for (int32 v = 0; v < 3; ++v)
            for (int32 side = 0; side < 2; ++side) {
                int32 n = side ? ps1N2 : ps1N1;
                if (off[side])
                    continue;
                int32 ox = v * (side ? offsetX2 : offsetX1), oy = v * (side ? offsetY2 : offsetY1);
                const int32 px = ox >> 16, py = oy >> 16;
                const int32 *vx = ps1Vx[side], *vy = ps1Vy[side], *sxs = sx[side], *sys = sy[side];
                if (strip) {
#if PS1_PAPER_PALE_LINE
                    // P12b M2 (docs/perf/57, near-invisible, user-approved option 2026-11-22): the third pass is one colour, so
                    // on a straight side (no deform: its vertices start + k * move, on one line) its n segments are drawn as
                    // one line from the first vertex to the last. The GPU rasterises that line along its own slope, so a
                    // pixel can sit one step off where a segment's rounded end put it; the same colour, the same ends
                    if (v == 2 && !(side ? self->deformPosBottom.y : self->deformPosTop.y)) {
                        const int32 ex[2] = { sxs[0], sxs[n] }, ey[2] = { sys[0], sys[n] };
                        const uint8 ein[2] = { inner[side][0], inner[side][n] };
                        if (PS1DrawLineStrip(ex, ey, ein, px, py, ps1Pale, 1))
                            continue;
                        strip = false;
                    }
                    else
#endif
                    if (PS1DrawLineStrip(sxs, sys, inner[side], px, py, v == 2 ? ps1Pale : cmd[side], n))
                        continue;
                    strip = false; // a mask colour: the world lines below from here on
                }
                // (a mask colour set: each line through DrawLine, as PS1DrawLinesWorld drew them then)
                for (int32 i = 0; i < n; ++i) {
                    int32 sx1 = sxs[i] + px, sy1 = sys[i] + py, sx2 = sxs[i + 1] + px, sy2 = sys[i + 1] + py;
                    if ((sx1 < -4 && sx2 < -4) || (sx1 > 324 && sx2 > 324) || (sy1 < -4 && sy2 < -4) || (sy1 > 244 && sy2 > 244))
                        continue;
                    RSDK.DrawLine(ox + vx[i], oy + vy[i], ox + vx[i + 1], oy + vy[i + 1], v == 2 ? 0xD0B898 : col[side][i], 0x7F, INK_NONE, false);
                }
            }
#undef PS1_PAPER_CMD
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
