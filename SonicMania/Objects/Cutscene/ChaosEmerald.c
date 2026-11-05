// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: ChaosEmerald Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectChaosEmerald *ChaosEmerald;

#if defined RETRO_PS1_BUILD
// P14 (docs/perf/57): the engine's bank-0 palette state (PS1Layers.cpp, Palette.hpp), for ChaosEmerald_Draw's swap
extern uint16 ChaosEmerald_PS1Palette[] __asm__("_ZN4RSDK11fullPaletteE"); // fullPalette[0]
extern uint16 ChaosEmerald_PS1To16B[] __asm__("_ZN4RSDK11rgb32To16_BE");
extern uint16 ChaosEmerald_PS1To16G[] __asm__("_ZN4RSDK11rgb32To16_GE");
extern uint16 ChaosEmerald_PS1To16R[] __asm__("_ZN4RSDK11rgb32To16_RE");
extern void ChaosEmerald_PS1ClutRow(const uint16 *row, int32 n, int16 x, int16 y) __asm__("_Z10PS1ClutRowPKtiss");
extern uint16 ChaosEmerald_PS1PalRaw[] __asm__("s_palRaw");
extern uint16 ChaosEmerald_PS1Pal16[] __asm__("s_pal16");
extern uint8 ChaosEmerald_PS1Row0Swapped __asm__("s_palRow0Swapped");
extern volatile int32 g_ps1PalDiffs;
static uint16 ChaosEmerald_PS1Colors[36];
static bool32 ChaosEmerald_PS1Ready = false;

// entries 128 .. 163 of bank 0 = values (save: the old ones first), with PS1PaletteWrite's bookkeeping for each changing write
static void ChaosEmerald_PS1Swap(uint16 *pal, const uint16 *raw, const uint16 *values, uint16 *save)
{
    int32 diffs = g_ps1PalDiffs;
    bool32 zero = false;
    for (int32 c = 0; c < 36; ++c) {
        uint16 old = pal[c], v = values[c];
        if (save)
            save[c] = old;
        if (old != v) {
            diffs += (int32)(v != raw[c]) - (int32)(old != raw[c]);
            zero |= !diffs;
        }
        pal[c] = v;
    }
    g_ps1PalDiffs = diffs;
    if (zero && ChaosEmerald_PS1Row0Swapped) {
        ChaosEmerald_PS1Row0Swapped = false;
        ChaosEmerald_PS1ClutRow(ChaosEmerald_PS1Pal16, 256, 0, 240);
    }
}
#endif

void ChaosEmerald_Update(void)
{
    RSDK_THIS(ChaosEmerald);

    StateMachine_Run(self->state);
}

void ChaosEmerald_LateUpdate(void) {}

void ChaosEmerald_StaticUpdate(void) {}

void ChaosEmerald_Draw(void)
{
    RSDK_THIS(ChaosEmerald);

#if defined RETRO_PS1_BUILD
    // P14 (docs/perf/57; was 9u.12's PS1PaletteSwapIn in the resident engine): the same 36 bank-0 writes each way, here.
    // Each write does what PS1PaletteWrite (RSDKv5/RSDK/Scene/PS1/PS1Layers.cpp) does: count the entries that differ from
    // the frame's CLUTs (g_ps1PalDiffs) and, at a write that brings the count to 0 while row 240 holds a per-draw copy,
    // put row 240 back. The emerald colours are the table's, converted once (SetPaletteEntry's conversion); the saved
    // entries go back as they were. Nothing is drawn between the writes, so the chain is the same
    uint16 *pal = ChaosEmerald_PS1Palette + 128;
    const uint16 *raw = ChaosEmerald_PS1PalRaw + 128;
    uint16 *save = (uint16 *)ChaosEmerald->colorStorage;
    if (!ChaosEmerald_PS1Ready) {
        for (int32 c = 0; c < 36; ++c) {
            uint32 color = (uint32)ChaosEmerald->emeraldColors[c];
            ChaosEmerald_PS1Colors[c] = ChaosEmerald_PS1To16B[color & 0xFF] | ChaosEmerald_PS1To16G[(color >> 8) & 0xFF] | ChaosEmerald_PS1To16R[(color >> 16) & 0xFF];
        }
        ChaosEmerald_PS1Ready = true;
    }
    ChaosEmerald_PS1Swap(pal, raw, ChaosEmerald_PS1Colors, save);
    RSDK.DrawSprite(&self->animator, NULL, false);
    ChaosEmerald_PS1Swap(pal, raw, save, NULL);
#else
    for (int32 c = 0; c < 36; ++c) {
        ChaosEmerald->colorStorage[c] = RSDK.GetPaletteEntry(0, c + 128);
        RSDK.SetPaletteEntry(0, c + 128, ChaosEmerald->emeraldColors[c]);
    }

    RSDK.DrawSprite(&self->animator, NULL, false);

    for (int32 c = 0; c < 36; ++c) {
        RSDK.SetPaletteEntry(0, c + 0x80, ChaosEmerald->colorStorage[c]);
    }
#endif
}

void ChaosEmerald_Create(void *data)
{
    RSDK_THIS(ChaosEmerald);

    self->active        = ACTIVE_BOUNDS;
    self->drawGroup     = Zone->objectDrawGroup[1];
    self->originPos     = self->position;
    self->visible       = true;
    self->drawFX        = FX_FLIP;
    self->updateRange.x = 0x800000;
    self->updateRange.y = 0x800000;
    self->state         = ChaosEmerald_State_None;
    RSDK.SetSpriteAnimation(ChaosEmerald->aniFrames, 0, &self->animator, true, self->type);
}

void ChaosEmerald_StageLoad(void) { ChaosEmerald->aniFrames = RSDK.LoadSpriteAnimation("Cutscene/Emeralds.bin", SCOPE_STAGE); }

void ChaosEmerald_State_None(void)
{
    // hello
}
void ChaosEmerald_State_Rotate(void)
{
    RSDK_THIS(ChaosEmerald);

    self->angle += self->groundVel;
    self->position.x = self->radius * RSDK.Cos256(self->angle >> 8) + self->originPos.x;
    self->position.y = self->radius * RSDK.Sin256(self->angle >> 8) + self->originPos.y;
}

#if GAME_INCLUDE_EDITOR
void ChaosEmerald_EditorDraw(void)
{
    RSDK_THIS(ChaosEmerald);

    RSDK.SetSpriteAnimation(ChaosEmerald->aniFrames, 0, &self->animator, true, self->type);
    RSDK.DrawSprite(&self->animator, NULL, false);
}

void ChaosEmerald_EditorLoad(void)
{
    ChaosEmerald->aniFrames = RSDK.LoadSpriteAnimation("Cutscene/Emeralds.bin", SCOPE_STAGE);

    RSDK_ACTIVE_VAR(ChaosEmerald, type);
    RSDK_ENUM_VAR("Green", CHAOSEMERALD_GREEN);
    RSDK_ENUM_VAR("Yellow", CHAOSEMERALD_YELLOW);
    RSDK_ENUM_VAR("Blue", CHAOSEMERALD_BLUE);
    RSDK_ENUM_VAR("Purple", CHAOSEMERALD_PURPLE);
    RSDK_ENUM_VAR("Gray", CHAOSEMERALD_GRAY);
    RSDK_ENUM_VAR("Cyan", CHAOSEMERALD_CYAN);
    RSDK_ENUM_VAR("Red", CHAOSEMERALD_RED);
}
#endif

void ChaosEmerald_Serialize(void) { RSDK_EDITABLE_VAR(ChaosEmerald, VAR_UINT8, type); }
