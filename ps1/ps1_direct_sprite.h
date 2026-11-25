/* docs/perf/57-mania-cost-per-act.md P20 / P22: a sprite's GPU words written by game code (an overlay), the words
 * RSDK::PS1DrawFrame sends for it (RSDKv5/RSDK/Graphics/PS1/PS1Sprites.cpp), without DrawSprite's and PS1DrawFrame's
 * per-call work. For game C files only (they include Game.h first); everything here is static, so each overlay that uses
 * it carries its own copy and the resident EXE does not grow (FBZ2's STG room).
 *
 * What is covered (else the caller draws with RSDK.DrawSprite, which is always correct):
 *   - the default ink (INK_NONE), no rotation or scale, the screen's whole clip box, not the 8 MB link's draw log;
 *   - a frame bound in the atlas at full or half resolution (quarter resolution left out), within one texture page and
 *     under the GPU's height, its top line on palette bank 0 with no palette write pending;
 *   - unflipped at full resolution: PS1DrawFrame's fast path, the 6-word rectangle; flipped or half resolution:
 *     PS1TexRect's quad (10 words in a span of 40, as there), a flipped axis from its last texel to one before its
 *     first, an edge past the page clamped when halved (a full-resolution one is its own strip there: left out);
 *   - flip rectangles live (a GPU that draws them, PS1FlipRectsLive) with a flipped full-resolution frame: left out.
 * Before the words, what those paths do first: validDraw (PS1PcClip), the frame's CLUT back after a fade
 * (PS1FadeRestoreSlot), and after them the draw count (PROFILE / PERF_HUD builds). With the whole clip box, PS1PcClip
 * passing means PS1OffScreen passes. The 9b.6 shifted draws (PS1SetFrameShift / VShift) name their own frames
 * (ParallaxSprite, AmoebaDroid, UISaveSlot), never one drawn through here.
 *
 * Keep in step with PS1DrawFrame / PS1TexRect: a change there that changes their words breaks the exactness here (the
 * proof is a chain-word A/B, docs/perf/57 P20). */
#ifndef PS1_DIRECT_SPRITE_H
#define PS1_DIRECT_SPRITE_H
#if defined RETRO_PS1_BUILD

#if PS1_DEBUG_BIGRAM
#define PS1_DIRECT_SPRITE_LOG 1 /* the 8 MB link logs every draw (PS1DrawLogAdd): DrawSprite does it */
#else
#define PS1_DIRECT_SPRITE_LOG 0
#endif

/* The engine's frame (Animation.hpp SpriteFrame on the PS1, 26 bytes: the game's 17 bytes padded to 18 (a C++ base's
 * tail padding is not reused), hitboxCount, hitboxOffset, ps1U, ps1V, ps1Attr). animator->frames points at these. */
typedef struct {
    int16 sprX, sprY, width, height, pivotX, pivotY;
    uint16 delay;
    int16 id;
    uint8 sheetID, basePad;
    uint8 hitboxCount, pad;
    uint16 hitboxOffset;
    uint8 ps1U, ps1V;
    uint16 ps1Attr;
} PS1EngineFrame;
typedef char PS1EngineFrameSize[sizeof(PS1EngineFrame) == 26 ? 1 : -1];

extern uint32 *PS1Direct_SpanBegin(int32 maxWords) __asm__("_Z17PS1ChainSpanBegini");
extern void PS1Direct_SpanEnd(uint32 *lastHeader, int32 lastWords, uint32 *end) __asm__("_Z15PS1ChainSpanEndPmiS_");
extern void PS1Direct_FadeRestoreSlot(uint32 slot) __asm__("_ZN4RSDK18PS1FadeRestoreSlotEj");
extern bool32 PS1Direct_validDraw __asm__("_ZN4RSDK9validDrawE");
extern uint8 PS1Direct_lineBank[] __asm__("_ZN4RSDK13gfxLineBufferE");
extern volatile int32 g_ps1PalDiffs;
extern volatile uint32 g_ps1FlipRectsOn, g_ps1FlipRectsForce, g_ps1SpriteDraws;
extern bool g_ps1FlipRectsGpu;

/* The screen allows the direct words: its whole clip box, and not the draw-log build */
static inline bool32 PS1DirectScreenOK(const RSDKScreenInfo *screen)
{
    return !PS1_DIRECT_SPRITE_LOG && !screen->clipBound_X1 && !screen->clipBound_Y1 && screen->clipBound_X2 == screen->size.x
           && screen->clipBound_Y2 == screen->size.y;
}

/* DrawSprite's FX_FLIP placement and PS1PcClip's test: false = nothing drawn and nothing changed (PS1DrawFrame returns
 * at PS1PcClip before any side effect). sx, sy: the position in screen pixels ((position >> 16) less the screen's). */
static inline bool32 PS1DirectHits(const PS1EngineFrame *f, int32 sx, int32 sy, int32 dir, const RSDKScreenInfo *screen)
{
    const int32 w = f->width, h = f->height;
    const int32 x = (dir & FLIP_X) ? sx - w - f->pivotX : sx + f->pivotX;
    const int32 y = (dir & FLIP_Y) ? sy - h - f->pivotY : sy + f->pivotY;
    return w > 0 && h > 0 && x < screen->clipBound_X2 && x + w > screen->clipBound_X1 && y < screen->clipBound_Y2 && y + h > screen->clipBound_Y1;
}

/* The words for frame f at screen (x, y) (its top-left, flips applied as DrawSprite places it), INK_NONE, after
 * PS1DirectHits passed and PS1DirectScreenOK holds. True: written (or the chain had no room, as PS1DrawFrame's own
 * return); false: a case left out, DrawSprite draws it. */
static bool32 PS1DirectWrite(const PS1EngineFrame *f, int32 x, int32 y, int32 dir, const RSDKScreenInfo *screen)
{
    const int32 w = f->width, h = f->height;
    const int32 attr = f->ps1Attr, u = f->ps1U, v = f->ps1V, slot = attr >> 7, half = attr >> 6 & 1;
    if (attr == 0xFFFF || (half && slot >= 0x140 && slot != 0x1FF) || h > 511 || g_ps1PalDiffs
        || PS1Direct_lineBank[y < 0 ? 0 : y >= screen->size.y ? screen->size.y - 1 : y])
        return false;
    const int32 tw = (w + half) >> half, th = (h + half) >> half;
    const bool32 fx = dir & FLIP_X, fy = dir & FLIP_Y;
    if (u + tw > 256 || (!half && (fx || fy) && (g_ps1FlipRectsGpu || g_ps1FlipRectsForce) && g_ps1FlipRectsOn))
        return false;
    int32 uL = fx ? u + tw - 1 : u, uR = fx ? u - 1 : u + tw, vT = fy ? v + th - 1 : v, vB = fy ? v - 1 : v + th;
    if (uR < 0 || uR > 255) {
        if (!half)
            return false;
        uR = uR < 0 ? 0 : 255;
    }
    if (vB < 0 || vB > 255) {
        if (!half)
            return false;
        vB = vB < 0 ? 0 : 255;
    }
    PS1Direct_validDraw = true;
    PS1Direct_FadeRestoreSlot((uint32)slot);
    const uint32 tpage = (attr & 15) | (attr >> 4 & 1) << 4 | (attr >> 5 & 1) << 7;
    const uint32 clut  = slot == 0x1FF ? (uint32)(240 << 6) : (uint32)(slot % 20) | (uint32)(496 + slot / 20) << 6;
    const uint32 xy0 = (uint32)(uint16)y << 16 | (uint16)x, xy1 = (uint32)(uint16)y << 16 | (uint16)(x + w);
    if (!half && !dir) {
        uint32 *wr = PS1Direct_SpanBegin(6);
        if (!wr)
            return true;
        wr[1] = 0xE1000000 | tpage, wr[2] = 0x64808080, wr[3] = xy0, wr[4] = clut << 16 | (uint32)v << 8 | (uint32)u;
        wr[5] = (uint32)h << 16 | (uint32)w;
        PS1Direct_SpanEnd(wr, 5, wr + 6);
    }
    else {
        const uint32 xy2 = (uint32)(uint16)(y + h) << 16 | (uint16)x, xy3 = (uint32)(uint16)(y + h) << 16 | (uint16)(x + w);
        const uint32 tl = (uint32)(uint8)vT << 8, bl = (uint32)(uint8)vB << 8;
        uint32 *wr = PS1Direct_SpanBegin(4 * 10);
        if (!wr)
            return true;
        wr[1] = 0x2C808080, wr[2] = xy0, wr[3] = clut << 16 | tl | (uint8)uL, wr[4] = xy1, wr[5] = tpage << 16 | tl | (uint8)uR;
        wr[6] = xy2, wr[7] = bl | (uint8)uL, wr[8] = xy3, wr[9] = bl | (uint8)uR;
        PS1Direct_SpanEnd(wr, 9, wr + 10);
    }
#if PS1_PROFILE || PS1_PERF_HUD
    g_ps1SpriteDraws = g_ps1SpriteDraws + 1;
#endif
    return true;
}

/* RSDK.DrawSprite(animator, position, false) for the current entity (SceneInfo->entity), when it is a case covered
 * above: drawFX FX_NONE or FX_FLIP, INK_NONE. True: drawn here, or nothing for DrawSprite to draw; false: call DrawSprite. */
static inline bool32 PS1DirectDrawSprite(const Animator *animator, const Vector2 *position)
{
    if (!animator || !animator->frames)
        return true;
    const Entity *e = (const Entity *)SceneInfo->entity;
    if (e->inkEffect != INK_NONE || (e->drawFX & ~FX_FLIP))
        return false;
    const int32 dir = e->drawFX ? e->direction : FLIP_NONE;
    if (dir > FLIP_XY)
        return true; /* DrawSprite's FX_FLIP case draws nothing */
    const RSDKScreenInfo *screen = &ScreenInfo[SceneInfo->currentScreenID];
    if (!PS1DirectScreenOK(screen))
        return false;
    const PS1EngineFrame *f = (const PS1EngineFrame *)animator->frames + animator->frameID;
    const Vector2 *p        = position ? position : &e->position;
    const int32 sx = (p->x >> 16) - screen->position.x, sy = (p->y >> 16) - screen->position.y;
    if (!PS1DirectHits(f, sx, sy, dir, screen))
        return true;
    return PS1DirectWrite(f, (dir & FLIP_X) ? sx - f->width - f->pivotX : sx + f->pivotX,
                          (dir & FLIP_Y) ? sy - f->height - f->pivotY : sy + f->pivotY, dir, screen);
}
#define PS1_DRAW_SPRITE(animator, position)                                                                            \
    do {                                                                                                               \
        if (!PS1DirectDrawSprite(animator, position))                                                                  \
            RSDK.DrawSprite(animator, position, false);                                                                \
    } while (0)

#endif /* RETRO_PS1_BUILD */
#endif /* PS1_DIRECT_SPRITE_H */
