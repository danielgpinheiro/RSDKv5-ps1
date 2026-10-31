// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: UIMedallionPanel Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectUIMedallionPanel *UIMedallionPanel;

void UIMedallionPanel_Update(void) {}

void UIMedallionPanel_LateUpdate(void) {}

void UIMedallionPanel_StaticUpdate(void) {}

void UIMedallionPanel_Draw(void)
{
    if (!API_GetNoSave())
        UIMedallionPanel_DrawPanel();
}

void UIMedallionPanel_Create(void *data)
{
    RSDK_THIS(UIMedallionPanel);

    self->active        = ACTIVE_BOUNDS;
    self->drawGroup     = 2;
    self->visible       = true;
    self->drawFX        = FX_FLIP;
    self->updateRange.x = TO_FIXED(128);
    self->updateRange.y = TO_FIXED(128);
}

void UIMedallionPanel_StageLoad(void) { UIMedallionPanel->aniFrames = RSDK.LoadSpriteAnimation("UI/MedallionPanel.bin", SCOPE_STAGE); }

void UIMedallionPanel_DrawPanel(void)
{
    RSDK_THIS(UIMedallionPanel);
    Vector2 drawPos;

    ProgressRAM *progressRAM = GameProgress_GetProgressRAM();
#if defined RETRO_PS1_BUILD
    // PS1 port (user 2026-10-31): the PC's 424-pixel layout puts the panel's right column past the 320-pixel screen's
    // edge (Extras): drawn 16 px further left, so its last medallion (centre x + 50, radius 6) ends inside the screen
    const int32 left = TO_FIXED(49 + 16);
#else
    const int32 left = TO_FIXED(49);
#endif
    for (int32 m = 0; m < GAMEPROGRESS_MEDAL_COUNT; ++m) {
        drawPos.x = (self->position.x - left) + TO_FIXED(14) * (m % 8);
        drawPos.y = (self->position.y - TO_FIXED(21)) + TO_FIXED(14) * (m / 8);

        if (SceneInfo->inEditor)
            RSDK.SetSpriteAnimation(UIMedallionPanel->aniFrames, 0, &self->animator, true, 1 + (m & 1));
        else if (progressRAM)
            RSDK.SetSpriteAnimation(UIMedallionPanel->aniFrames, 0, &self->animator, true, m < progressRAM->goldMedalCount ? 2 : m < progressRAM->silverMedalCount ? 1 : 0);
        else
            RSDK.SetSpriteAnimation(UIMedallionPanel->aniFrames, 0, &self->animator, true, 0);

        if (!SceneInfo->inEditor && !self->animator.frameID)
            RSDK.DrawCircleOutline(drawPos.x, drawPos.y, 5, 6, 0x000000, 0xFF, INK_BLEND, false);

        drawPos.x += TO_FIXED(1);
        RSDK.DrawSprite(&self->animator, &drawPos, false);
    }
}

#if GAME_INCLUDE_EDITOR
void UIMedallionPanel_EditorDraw(void) { UIMedallionPanel_DrawPanel(); }

void UIMedallionPanel_EditorLoad(void) { UIMedallionPanel->aniFrames = RSDK.LoadSpriteAnimation("UI/MedallionPanel.bin", SCOPE_STAGE); }
#endif

void UIMedallionPanel_Serialize(void) {}
