// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: CreditsSetup Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectCreditsSetup *CreditsSetup;

#if defined RETRO_PS1_BUILD
// PS1 (docs/32 7b.14): stock creates every credits line at the start (874 UICreditsText of ~1 KB: ~900 KB). The PS1 keeps
// the list (its chars in STG: Text.cpp InitStringList) and one record per text line, and creates each line just before
// it can start (UICreditsText_State_Init waits for the screen's lower edge), in a temp block (every one large here:
// Zone.c ps1TempLargeStageMarker), and destroys it once it is above the screen: the same lines at the same places.
typedef struct {
    int32 y;      // its position at creditsPos 0 (stock's offset)
    uint16 start; // its first char in the list
    uint8 length; // its chars (the line, with its "[TS" prefix)
    uint8 spare;
} PS1CreditsLine;
static String s_ps1CreditsList;       // (static: the list's owner while the scene runs)
#define PS1_CREDITS_LINES (960)       // Credits.txt: 874 text lines (7.7 KB, in the Credits overlay)
static PS1CreditsLine s_ps1CreditsLines[PS1_CREDITS_LINES];
static int32 s_ps1CreditsCount = 0, s_ps1CreditsNext = 0;
#define PS1_CREDITS_AHEAD (32 << 16)  // created this far below the screen (State_Init starts 16 px above its edge)
#define PS1_CREDITS_ABOVE (-96 << 16) // destroyed above this (the tallest line is an emblem frame)

static void CreditsSetup_PS1SpawnLines(void)
{
    String string;
    INIT_STRING(string);
    int32 bottom = (ScreenInfo->size.y << 16) + PS1_CREDITS_AHEAD;
    while (s_ps1CreditsNext < s_ps1CreditsCount && s_ps1CreditsLines[s_ps1CreditsNext].y - CreditsSetup->creditsPos < bottom) {
        PS1CreditsLine *l = &s_ps1CreditsLines[s_ps1CreditsNext++];
        uint16 *chars     = &s_ps1CreditsList.chars[l->start];
        int32 type        = chars[1] - '0';
        bool32 hasShape   = chars[2] == 'U';
        // stock's text: the line from its 4th char ("]" + the words); chars[1] is an emblem line's frame
        string.chars  = chars + 3;
        string.length = l->length - 3;
        string.size   = string.length;
        EntityUICreditsText *text = CREATE_ENTITY(UICreditsText, NULL, 0x1000000, l->y - CreditsSetup->creditsPos);
        text->hasShape            = hasShape;
        UICreditsText_SetText(type, text, &string);
        text->drawGroup = Zone->playerDrawGroup[1];
    }
}
#endif

void CreditsSetup_Update(void) {}

void CreditsSetup_LateUpdate(void) {}

void CreditsSetup_StaticUpdate(void)
{
    if (CreditsSetup->started) {
        CreditsSetup->scrollPos += 0x1000;

        EntityFXFade *fade = CreditsSetup->fxFade;

        if (!CreditsSetup->skipped) {
            // Check if we should play the next track
            if (!RSDK.ChannelActive(0))
                Music_PlayTrack(++CreditsSetup->creditsTrack);

            if (ControllerInfo->keyStart.press || (CreditsSetup->creditsSize && CreditsSetup->creditsPos >= CreditsSetup->creditsSize)) {
                CreditsSetup->skipped = true;

                fade->state    = FXFade_State_FadeOut;
                fade->speedIn  = 8;
                fade->wait     = 64;
                fade->speedOut = 8;
                fade->timer    = 0;
                fade->oneWay   = false;
                Music_FadeOut(0.0125);
            }
        }
        else {
            if (fade->state == FXFade_State_Wait && fade->wait == 1) {
                EntityMenuParam *param = MenuParam_GetParam();

#if MANIA_USE_PLUS
                if (param->creditsReturnToMenu) {
                    RSDK.SetScene("Presentation", "Menu");
                }
                else if (SaveGame_GetSaveRAM()->collectedEmeralds < 0b01111111) {
                    globals->gameMode == MODE_ENCORE ? RSDK.SetScene("Cutscenes", "Try Again Encore") : RSDK.SetScene("Cutscenes", "Try Again");
                }
                else if (globals->gameMode == MODE_ENCORE) {
                    RSDK.SetScene("Cutscenes", "Mirage Saloon Encore End");
                }
                else {
                    API.CheckDLC(DLC_PLUS) ? RSDK.SetScene("Presentation", "Game Summary") : RSDK.SetScene("Presentation", "Menu");
                }
#else
                if (param->creditsReturnToMenu)
                    RSDK.SetScene("Presentation", "Menu");
                else if (SaveGame_GetSaveRAM()->collectedEmeralds < 0b01111111)
                    RSDK.SetScene("Cutscenes", "Try Again");
                else
                    RSDK.SetScene("Presentation", "Menu");
#endif

                RSDK.LoadScene();
            }
        }

        int32 scrollSpeed = 0x10000;
        if (ControllerInfo->keyA.down || ControllerInfo->keyB.down || ControllerInfo->keyC.down)
            scrollSpeed = 0x80000;

        foreach_all(UICreditsText, text)
        {
            text->drawGroup = Zone->playerDrawGroup[1];
            text->position.y -= scrollSpeed;
#if defined RETRO_PS1_BUILD
            if (text->position.y < PS1_CREDITS_ABOVE)
                destroyEntity(text);
#endif
        }

        CreditsSetup->creditsPos += scrollSpeed;
#if defined RETRO_PS1_BUILD
        CreditsSetup_PS1SpawnLines();
#endif
    }
    else {
        foreach_all(Player, player) { destroyEntity(player); }
        foreach_all(Camera, camera) { destroyEntity(camera); }

        CreditsSetup->creditsSize = 0;
        CreditsSetup->creditsPos  = 0;
        CreditsSetup_LoadCreditsStrings();
        CreditsSetup->started = true;
    }
}

void CreditsSetup_Draw(void) {}

void CreditsSetup_Create(void *data) {}

void CreditsSetup_StageLoad(void)
{
    CreditsSetup->started = false;

    foreach_all(FXFade, fxFade) { CreditsSetup->fxFade = fxFade; }
}

void CreditsSetup_LoadCreditsStrings(void)
{
#if defined RETRO_PS1_BUILD
    // the list, then one record per text line with stock's offsets (lines split at '\n', a '\r' before it dropped as
    // SplitStringList does; a line of at most 4 chars is a gap)
    INIT_STRING(s_ps1CreditsList);
    RSDK.LoadStringList(&s_ps1CreditsList, "Credits.txt", 8);
    s_ps1CreditsCount = s_ps1CreditsNext = 0;
    int32 offset = (ScreenInfo->size.y + 128) << 16;
    for (int32 pos = 0; pos < s_ps1CreditsList.length;) {
        int32 end = pos;
        while (end < s_ps1CreditsList.length && s_ps1CreditsList.chars[end] != '\n') ++end;
        int32 length = end - pos;
        if (length && s_ps1CreditsList.chars[end - 1] == '\r')
            --length;
        if (length <= 4) {
            offset += 0x200000;
        }
        else if (s_ps1CreditsCount < PS1_CREDITS_LINES) {
            int32 type          = s_ps1CreditsList.chars[pos + 1] - '0';
            PS1CreditsLine *l   = &s_ps1CreditsLines[s_ps1CreditsCount++];
            l->y                = offset;
            l->start            = (uint16)pos;
            l->length           = (uint8)(length > 0x83 ? 0x83 : length); // (stock's string holds 0x80 chars)
            SpriteFrame *frame  = RSDK.GetFrame(UICreditsText->aniFrames, type, 0);
            if (frame)
                offset += (frame->height + 8) << 16;
            if (type == 7)
                offset -= 0x200000;
        }
        pos = end + 1;
    }
    CreditsSetup->creditsSize = offset + (ScreenInfo->size.y << 15);
    CreditsSetup_PS1SpawnLines();
#else
    String string;
    String stringList;

    INIT_STRING(stringList);
    INIT_STRING(string);
    RSDK.LoadStringList(&stringList, "Credits.txt", 8);
    RSDK.InitString(&string, "", 0x80);

    int32 offset = (ScreenInfo->size.y + 128) << 16;
    for (int32 i = 0; RSDK.SplitStringList(&string, &stringList, i, 1); ++i) {
        if (string.length <= 4) {
            offset += 0x200000;
        }
        else {
            int32 type      = string.chars[1] - '0';
            bool32 hasShape = string.chars[2] == 'U';

            string.length -= 3;
            for (int32 c = 0; c < string.length; ++c) string.chars[c] = string.chars[c + 3];

            EntityUICreditsText *text = RSDK_GET_ENTITY(i + 0x100, UICreditsText);
            RSDK.ResetEntity(text, UICreditsText->classID, 0);
            text->hasShape   = hasShape;
            text->position.x = 0x1000000;
            text->position.y = offset;
            UICreditsText_SetText(type, text, &string);

            SpriteFrame *frame = RSDK.GetFrame(UICreditsText->aniFrames, type, 0);
            if (frame)
                offset += (frame->height + 8) << 16;
            if (type == 7)
                offset -= 0x200000;
        }
    }

    CreditsSetup->creditsSize = offset + (ScreenInfo->size.y << 15);
#endif
}

#if GAME_INCLUDE_EDITOR
void CreditsSetup_EditorDraw(void) {}

void CreditsSetup_EditorLoad(void) {}
#endif

void CreditsSetup_Serialize(void) {}
