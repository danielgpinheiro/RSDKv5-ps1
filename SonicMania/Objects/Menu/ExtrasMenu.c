// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: ExtrasMenu Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

#if MANIA_USE_PLUS
ObjectExtrasMenu *ExtrasMenu;

void ExtrasMenu_Update(void) {}

void ExtrasMenu_LateUpdate(void) {}

void ExtrasMenu_StaticUpdate(void) {}

void ExtrasMenu_Draw(void) {}

void ExtrasMenu_Create(void *data) {}

void ExtrasMenu_StageLoad(void) {}

#if defined RETRO_PS1_BUILD
static EntityUIButton *s_ps1ExtrasBss = NULL, *s_ps1ExtrasPuyo = NULL, *s_ps1ExtrasGarden = NULL;
#endif

void ExtrasMenu_Initialize(void)
{
    String tag;
    INIT_STRING(tag);
    RSDK.SetString(&tag, "Extras");

    LogHelpers_Print("ManiaModeMenu_Initialize()");

    foreach_all(UIControl, control)
    {
        if (RSDK.CompareStrings(&tag, &control->tag, false))
            ExtrasMenu->extrasControl = control;
    }

#if defined RETRO_PS1_BUILD
    // docs/32 9b.5: Puyo Puyo is out of the port's scope (user decision 2026-09-26): its button leaves the Extras list,
    // which closes up as the Main Menu's does (4.2c); the unlock checks keep the buttons by what they are
    EntityUIControl *extrasList = (EntityUIControl *)ExtrasMenu->extrasControl;
    if (extrasList && extrasList->buttonCount >= 3) {
        s_ps1ExtrasBss    = extrasList->buttons[0];
        s_ps1ExtrasPuyo   = extrasList->buttons[1];
        s_ps1ExtrasGarden = extrasList->buttons[2];
        s_ps1ExtrasPuyo->visible = false;
        s_ps1ExtrasPuyo->active  = ACTIVE_NEVER;
        // its choices (1P vs CPU / 2P): the menu makes a button's current choice visible again (UIButton_Update once the
        // control reactivates its area), so they and the button go far below the screen and the button has none
        foreach_all(UIChoice, choice)
        {
            if (choice->listID == 7 && (choice->frameID == 2 || choice->frameID == 3)) {
                choice->visible = false;
                choice->active  = ACTIVE_NEVER;
                choice->position.y += TO_FIXED(0x1000);
            }
        }
        // the buttons below it move up a place (fixed positions, not a shifter's)
        Vector2 slot = s_ps1ExtrasPuyo->position;
        s_ps1ExtrasPuyo->position.y += TO_FIXED(0x1000);
        s_ps1ExtrasPuyo->choiceCount = 0;
        for (int32 i = 2; i < extrasList->buttonCount; ++i) {
            Vector2 next                     = extrasList->buttons[i]->position;
            extrasList->buttons[i]->position = slot;
            slot                             = next;
        }
        for (int32 i = 1; i + 1 < extrasList->buttonCount; ++i) extrasList->buttons[i] = extrasList->buttons[i + 1];
        extrasList->buttons[--extrasList->buttonCount] = NULL;
        if (extrasList->rowCount > extrasList->buttonCount)
            extrasList->rowCount = extrasList->buttonCount;
    }
#endif
}

void ExtrasMenu_HandleUnlocks(void)
{
    EntityUIControl *control = (EntityUIControl *)ExtrasMenu->extrasControl;

#if defined RETRO_PS1_BUILD
    if (s_ps1ExtrasBss) { // (ExtrasMenu_Initialize: the list without Puyo Puyo)
        s_ps1ExtrasBss->disabled = !GameProgress_CheckUnlock(GAMEPROGRESS_UNLOCK_BLUESPHERES);
        if (s_ps1ExtrasBss->disabled)
            UIButton_ManageChoices(s_ps1ExtrasBss);
        s_ps1ExtrasGarden->disabled = !GameProgress_CheckUnlock(GAMEPROGRESS_UNLOCK_DAGARDEN) && !globals->medallionDebug;
        return;
    }
#endif
    EntityUIButton *bssButton = control->buttons[0];
    bssButton->disabled       = !GameProgress_CheckUnlock(GAMEPROGRESS_UNLOCK_BLUESPHERES);
    if (bssButton->disabled)
        UIButton_ManageChoices(bssButton);

    EntityUIButton *puyoButton = control->buttons[1];
    puyoButton->disabled       = !GameProgress_CheckUnlock(GAMEPROGRESS_UNLOCK_MEANBEAN);
    if (puyoButton->disabled)
        UIButton_ManageChoices(puyoButton);

    EntityUIButton *daGardenButton = control->buttons[2];
    daGardenButton->disabled       = !GameProgress_CheckUnlock(GAMEPROGRESS_UNLOCK_DAGARDEN) && !globals->medallionDebug;
}

void ExtrasMenu_SetupActions(void)
{
    EntityUIControl *control = (EntityUIControl *)ExtrasMenu->extrasControl;

    control->processButtonInputCB = ExtrasMenu_ProcessInputs;

    foreach_all(UIButton, button)
    {
        if (button->listID == 7) {
            switch (button->frameID) {
                default: break;

                case 4:
                    button->actionCB         = ExtrasMenu_DAGarden_ActionCB;
                    button->clearParentState = true;
                    break;

                case 8:
                    button->actionCB         = ExtrasMenu_CreditsButton_ActionCB;
                    button->clearParentState = true;
                    break;
            }
        }
    }

    foreach_all(UIChoice, choice)
    {
        if (choice->listID == 7) {
            switch (choice->frameID) {
                default: break;

                case 2:
                    choice->actionCB         = ExtrasMenu_Puyo_vsAI_ActionCB;
                    choice->clearParentState = true;
                    break;

                case 3:
                    choice->actionCB         = ExtrasMenu_Puyo_vs2P_ActionCB;
                    choice->clearParentState = true;
                    break;

                case 6:
                    choice->actionCB         = ExtrasMenu_BSS_S3_ActionCB;
                    choice->clearParentState = true;
                    break;

                case 7:
                    choice->actionCB         = ExtrasMenu_BSS_Mania_ActionCB;
                    choice->clearParentState = true;
                    break;
            }
        }
    }
}

void ExtrasMenu_ProcessMedallionCheat(void)
{
    int32 key = 0;
    if (UIControl->anyLeftPress)
        key = 1;
    else if (UIControl->anyRightPress)
        key = 2;

    if (key) {
        for (int32 i = 0; i < 7; ++i) ExtrasMenu->cheatCode[i] = ExtrasMenu->cheatCode[i + 1];

        ExtrasMenu->cheatCode[7] = key;
    }
}

bool32 ExtrasMenu_CheckMedallionCheat(void)
{
    // left, left, right, right, left, right, left, right
    return ExtrasMenu->cheatCode[0] == 1 && ExtrasMenu->cheatCode[1] == 1 && ExtrasMenu->cheatCode[2] == 2 && ExtrasMenu->cheatCode[3] == 2
           && ExtrasMenu->cheatCode[4] == 1 && ExtrasMenu->cheatCode[5] == 2 && ExtrasMenu->cheatCode[6] == 1 && ExtrasMenu->cheatCode[7] == 2;
}

void ExtrasMenu_ProcessInputs(void)
{
    RSDK_THIS(UIControl);

#if GAME_VERSION == VER_106
    // this "cheat" is technically PC only, but I can't be bothered to ifdef it and it's cooler to have on all platforms

    // buttonID == 2: DAGarden
    if (self->buttonID == 2) {
        ExtrasMenu_ProcessMedallionCheat();

        if (ExtrasMenu_CheckMedallionCheat()) {
            if (!globals->medallionDebug) {
                RSDK.PlaySfx(UIWidgets->sfxEvent, false, 255);
                globals->medallionDebug = true;
                ExtrasMenu_HandleUnlocks();
            }
        }
    }
#endif

    UIControl_ProcessButtonInput();
}

void ExtrasMenu_Start_Puyo_vsAI(void)
{
    EntityMenuParam *param = MenuParam_GetParam();

    TimeAttackData_Clear();

    param->puyoSelection = PUYO_SELECTION_VS_CPU;
    strcpy(param->menuTag, "Extras");
    param->menuSelection = EXTRAS_SELECTION_PUYO;

    RSDK.SetScene("Extras", "Puyo Puyo");
    RSDK.LoadScene();
}

void ExtrasMenu_Puyo_vsAI_ActionCB(void) { MenuSetup_StartTransition(ExtrasMenu_Start_Puyo_vsAI, 32); }

void ExtrasMenu_Start_Puyo_vs2P(void)
{
    EntityMenuParam *param = MenuParam_GetParam();

    TimeAttackData_Clear();

    param->puyoSelection = PUYO_SELECTION_VS_2P;
    strcpy(param->menuTag, "Extras");
    param->menuSelection = EXTRAS_SELECTION_PUYO;

    RSDK.SetScene("Extras", "Puyo Puyo");
    RSDK.LoadScene();
}

void ExtrasMenu_Puyo_vs2P_ActionCB(void) { MenuSetup_StartTransition(ExtrasMenu_Start_Puyo_vs2P, 32); }

void ExtrasMenu_Start_Credits(void)
{
    EntityMenuParam *param = MenuParam_GetParam();

    TimeAttackData_Clear();

    param->bssSelection = CREDITS_SELECTION_EXTRAS;
    strcpy(param->menuTag, "Extras");
    param->menuSelection       = EXTRAS_SELECTION_CREDITS;
    param->creditsReturnToMenu = true;

    RSDK.SetScene("Presentation", "Credits");
    RSDK.LoadScene();
}

void ExtrasMenu_CreditsButton_ActionCB(void) { MenuSetup_StartTransition(ExtrasMenu_Start_Credits, 32); }

void ExtrasMenu_Start_DAGarden(void)
{
    EntityMenuParam *param = MenuParam_GetParam();

    TimeAttackData_Clear();

    strcpy(param->menuTag, "Extras");
    param->menuSelection = EXTRAS_SELECTION_DAGARDEN;

#if MANIA_USE_PLUS
    if (API.CheckDLC(DLC_PLUS))
        RSDK.SetScene("Extras", "D.A. Garden Plus");
    else
#endif
        RSDK.SetScene("Extras", "D.A. Garden");

    RSDK.LoadScene();
}

void ExtrasMenu_DAGarden_ActionCB(void) { MenuSetup_StartTransition(ExtrasMenu_Start_DAGarden, 32); }

void ExtrasMenu_Start_BSS_3K(void)
{
    EntityMenuParam *param = MenuParam_GetParam();

    TimeAttackData_Clear();

    param->bssSelection = BSS_SELECTION_EXTRAS;
    strcpy(param->menuTag, "Extras");
    param->menuSelection = EXTRAS_SELECTION_BSS;

    RSDK.SetScene("Blue Spheres", "Random");
    RSDK.LoadScene();
}

void ExtrasMenu_BSS_S3_ActionCB(void) { MenuSetup_StartTransition(ExtrasMenu_Start_BSS_3K, 32); }

void ExtrasMenu_Start_BSS_Mania(void)
{
    EntityMenuParam *param = MenuParam_GetParam();

    TimeAttackData_Clear();

    param->bssSelection = BSS_SELECTION_EXTRAS;
    strcpy(param->menuTag, "Extras");
    param->menuSelection = EXTRAS_SELECTION_BSS;

    RSDK.SetScene("Blue Spheres", "Random 2");
    RSDK.LoadScene();
}

void ExtrasMenu_BSS_Mania_ActionCB(void) { MenuSetup_StartTransition(ExtrasMenu_Start_BSS_Mania, 32); }

#if GAME_INCLUDE_EDITOR
void ExtrasMenu_EditorDraw(void) {}

void ExtrasMenu_EditorLoad(void) {}
#endif

void ExtrasMenu_Serialize(void) {}
#endif
