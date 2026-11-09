// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: PBL_Setup Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

#if MANIA_USE_PLUS
ObjectPBL_Setup *PBL_Setup;

void PBL_Setup_Update(void)
{
    RSDK_THIS(PBL_Setup);

    StateMachine_Run(self->state);

    if (self->state != PBL_Setup_SaveAndChangeScene && globals->gameMode < MODE_TIMEATTACK)
        ++SaveGame_GetSaveRAM()->zoneTimes[28];
}

void PBL_Setup_LateUpdate(void) {}

void PBL_Setup_StaticUpdate(void)
{
    RSDK_THIS_GEN();

    ++PBL_Setup->timer;
    PBL_Setup->timer &= 0x7FFF;

    if (!(PBL_Setup->timer & 1)) {
        ++PBL_Setup->ringFrame;
        PBL_Setup->ringFrame &= 0xF;
    }

    if (!(PBL_Setup->timer & 0xF)) {
        for (int32 i = 0; i < 8; ++i) {
            RSDK.RotatePalette(i, 176, 181, true);
            RSDK.RotatePalette(i, 176, 181, true);
        }
    }

    PBL_Setup->scanlineTimer += 0x8000;
    PBL_Setup->scanlineTimer &= 0x7FFFFFFF;

    if ((ControllerInfo->keyStart.press || Unknown_pausePress) && SceneInfo->state == ENGINESTATE_REGULAR
        && !RSDK_GET_ENTITY(SLOT_PAUSEMENU, PauseMenu)->classID) {
        RSDK.ResetEntitySlot(SLOT_PAUSEMENU, PauseMenu->classID, NULL);

        EntityPauseMenu *pauseMenu = RSDK_GET_ENTITY(SLOT_PAUSEMENU, PauseMenu);
        pauseMenu->disableRestart  = true;
        pauseMenu->triggerPlayer   = RSDK.GetEntitySlot(self);
    }
}

void PBL_Setup_Draw(void)
{
    RSDK_THIS(PBL_Setup);

    RSDK.FillScreen(self->color, self->timer, self->timer - 128, self->timer - 256);
}

void PBL_Setup_Create(void *data)
{
    RSDK_THIS(PBL_Setup);

    self->active    = ACTIVE_NORMAL;
    self->visible   = true;
    self->drawGroup = DRAWGROUP_COUNT - 1;
    self->color     = 0xF0F0F0;
    self->timer     = 512;

    self->state = PBL_Setup_State_FadeIn;
}

void PBL_Setup_StageLoad(void)
{
    PBL_Setup->tableLow  = RSDK.GetTileLayerID("Table Low");
    PBL_Setup->tableHigh = RSDK.GetTileLayerID("Table High");

    PBL_Setup->rings       = 0;
    PBL_Setup->sectorID    = 0;
    PBL_Setup->sectorCount = 0;
    PBL_Setup->score       = 0;
    PBL_Setup->score1UP    = 10000;

    RSDK.GetTileLayer(PBL_Setup->tableLow)->scanlineCallback  = PBL_Setup_Scanline_TableLow;
    RSDK.GetTileLayer(PBL_Setup->tableHigh)->scanlineCallback = PBL_Setup_Scanline_TableHigh;
    RSDK.GetTileLayer(1)->scanlineCallback                    = PBL_Setup_Scanline_PinballBG;

    RSDK.SetDrawGroupProperties(1, false, PBL_Setup_DrawHook_PrepareDrawingFX);
    RSDK.SetDrawGroupProperties(3, false, PBL_Setup_DrawHook_PrepareDrawingFX);
    RSDK.SetDrawGroupProperties(4, true, StateMachine_None);

    RSDK.SetLimitedFade(1, 0, 7, 36, 0, 255);
    RSDK.SetLimitedFade(2, 0, 7, 72, 0, 255);
    RSDK.SetLimitedFade(3, 0, 7, 108, 0, 255);
    RSDK.SetLimitedFade(4, 0, 7, 144, 0, 255);
    RSDK.SetLimitedFade(5, 0, 7, 180, 0, 255);
    RSDK.SetLimitedFade(6, 0, 7, 216, 0, 255);

    RSDK.ResetEntitySlot(SLOT_PBL_SETUP, PBL_Setup->classID, NULL);

    PBL_Setup->sfxContinue = RSDK.GetSfx("Special/Continue.wav");
}

void PBL_Setup_Scanline_TableLow(ScanlineInfo *scanlines)
{
    EntityPBL_Camera *camera = RSDK_GET_ENTITY(SLOT_PBL_CAMERA, PBL_Camera);
    RSDK.SetClipBounds(0, 0, camera->centerY, ScreenInfo->size.x, ScreenInfo->size.y);

#if defined RETRO_PS1_BUILD
    // PS1 port (docs/ports/mania/59 4.1), as the Dreamcast port: the camera for the engine's 3D tile floor
    // (RSDK/Graphics/PS1/PS1UfoFloor.cpp) instead of 240 deformed lines, each with the palette bank of its distance
    scanlines->deform.x   = (int32)SCANLINE_MAJOR_MAGIC_3DTILES;
    scanlines->deform.y   = (int32)SCANLINE_MINOR_MAGIC_PINBALL;
    scanlines->position.x = 0;
    scanlines->position.y = 0;
    scanlines++;
    scanlines->deform.x   = RSDK.Sin1024(camera->angle);
    scanlines->deform.y   = RSDK.Cos1024(camera->angle);
    scanlines->position.x = RSDK.Sin1024(-camera->rotationY);
    scanlines->position.y = RSDK.Cos1024(-camera->rotationY);
    scanlines++;
    scanlines->deform.x   = camera->position.x;
    scanlines->deform.y   = camera->worldY;
    scanlines->position.x = camera->position.y;
    scanlines->position.y = (12 | (27 << 8)); // the bank rule: the floor reads shift | bias << 8 (12 + 4, so pos >> 16)
    return;
#endif

    int32 sin    = RSDK.Sin1024(camera->angle) >> 2;
    int32 cos    = RSDK.Cos1024(camera->angle) >> 2;
    int32 negSin = RSDK.Sin1024(-camera->rotationY) >> 2;
    int32 negCos = RSDK.Cos1024(-camera->rotationY) >> 2;
    int32 cosVal = -ScreenInfo->center.y * negCos;

    for (int32 i = -ScreenInfo->center.y; i < ScreenInfo->center.y; ++i) {
        int32 div = negSin + (cosVal >> 8);
        if (!div)
            div = 1;

        int32 mult          = camera->worldY / div;
        scanlines->deform.x = -(cos * mult) >> 8;
        scanlines->deform.y = sin * mult >> 8;

        int32 pos = (negCos * mult >> 8) - (negSin * (i * mult >> 8) >> 8);
        RSDK.SetActivePalette(CLAMP((abs(pos) >> 12) - 27, 0, 7), i + 120, i + 121);

        scanlines->position.x = (sin * pos - ScreenInfo->center.x * scanlines->deform.x) + camera->position.x;
        scanlines->position.y = (cos * pos - ScreenInfo->center.x * scanlines->deform.y) + camera->position.y;

        cosVal += negCos;
        scanlines++;
    }
}
void PBL_Setup_Scanline_TableHigh(ScanlineInfo *scanlines)
{
    EntityPBL_Camera *camera = RSDK_GET_ENTITY(SLOT_PBL_CAMERA, PBL_Camera);
    RSDK.SetClipBounds(0, 0, camera->centerY, ScreenInfo->size.x, ScreenInfo->size.y);

#if defined RETRO_PS1_BUILD
    // PS1 port (docs/ports/mania/59 4.1), as the Dreamcast port: the camera for the engine's 3D tile floor
    // (RSDK/Graphics/PS1/PS1UfoFloor.cpp) instead of 240 deformed lines, each with the palette bank of its distance
    scanlines->deform.x   = (int32)SCANLINE_MAJOR_MAGIC_3DTILES;
    scanlines->deform.y   = (int32)SCANLINE_MINOR_MAGIC_PINBALL;
    scanlines->position.x = 0;
    scanlines->position.y = 0;
    scanlines++;
    scanlines->deform.x   = RSDK.Sin1024(camera->angle);
    scanlines->deform.y   = RSDK.Cos1024(camera->angle);
    scanlines->position.x = RSDK.Sin1024(-camera->rotationY);
    scanlines->position.y = RSDK.Cos1024(-camera->rotationY);
    scanlines++;
    scanlines->deform.x   = camera->position.x;
    scanlines->deform.y   = (camera->worldY - 0x100000);
    scanlines->position.x = camera->position.y;
    scanlines->position.y = (12 | (24 << 8)); // the bank rule: the floor reads shift | bias << 8 (12 + 4, so pos >> 16)
    return;
#endif

    int32 sin    = RSDK.Sin1024(camera->angle) >> 2;
    int32 cos    = RSDK.Cos1024(camera->angle) >> 2;
    int32 negSin = RSDK.Sin1024(-camera->rotationY) >> 2;
    int32 negCos = RSDK.Cos1024(-camera->rotationY) >> 2;
    int32 cosVal = -ScreenInfo->center.y * negCos;

    for (int32 i = -ScreenInfo->center.y; i < ScreenInfo->center.y; ++i) {
        int32 div = negSin + (cosVal >> 8);
        if (!div)
            div = 1;

        int32 mult          = (camera->worldY - 0x100000) / div;
        scanlines->deform.x = -(cos * mult) >> 8;
        scanlines->deform.y = sin * mult >> 8;

        int32 pos = (negCos * mult >> 8) - (negSin * (i * mult >> 8) >> 8);
        RSDK.SetActivePalette(CLAMP((abs(pos) >> 12) - 24, 0, 7), i + 120, i + 121);

        scanlines->position.x = (sin * pos - ScreenInfo->center.x * scanlines->deform.x) + camera->position.x;
        scanlines->position.y = (cos * pos - ScreenInfo->center.x * scanlines->deform.y) + camera->position.y;

        cosVal += negCos;
        scanlines++;
    }
}
void PBL_Setup_Scanline_PinballBG(ScanlineInfo *scanlines)
{
    RSDK.SetClipBounds(0, 0, 0, ScreenInfo->size.x, 112);

#if defined RETRO_PS1_BUILD
    // PS1 port (docs/ports/mania/59 4.2): the roof as the engine's 3D-tile pattern page, drawn by the floor's pattern
    // path: a fixed 45-degree camera at the stock scroll. The PC's version curves each row and a plane cannot, so this
    // is the "cheap, a look a little off" route the user approved (2026-10-31); height and scroll are the two numbers
    // to fit by eye, and the layer only draws from about frame 709 in the attract act.
    const int32 scroll    = (PBL_Setup->scanlineTimer >> 1) << 2; // quarter pixels
    scanlines->deform.x   = (int32)SCANLINE_MAJOR_MAGIC_3DTILES;
    scanlines->deform.y   = (int32)SCANLINE_MINOR_MAGIC_ROOF;
    scanlines->position.x = 0;
    scanlines->position.y = 0;
    scanlines++;
    scanlines->deform.x   = 0, scanlines->deform.y = 1024; // no yaw
    scanlines->position.x = 0;                             // level: the plane stretches to the LOD far
    scanlines->position.y = 1024;
    scanlines++;
    scanlines->deform.x   = scroll;
    scanlines->deform.y   = -0x600000;                     // the plane 96 px above the camera (quarter pixels)
    scanlines->position.x = scroll;
    scanlines->position.y = 14;                            // unused: the page has its own CLUT
    return;
#endif

    int32 x       = 116 << 16;
    int32 centerX = ScreenInfo->center.x;
    int32 sin     = RSDK.Sin256(32);
    int32 cos     = RSDK.Cos256(32);
    int32 timer   = PBL_Setup->scanlineTimer >> 1;

    int32 clr = 0;
    for (int32 i = 160; i > 40; --i) {
        x += 0xE000;
        int32 id = x / (8 * i);

        scanlines->deform.x   = -(cos * id) >> 7;
        scanlines->deform.y   = sin * id >> 7;
        scanlines->position.x = timer + sin * id - centerX * scanlines->deform.x;
        scanlines->position.y = timer + cos * id - centerX * (sin * id >> 7);

        RSDK.SetActivePalette(CLAMP((abs(id) >> 11) - 16, 0, 7), clr, clr + 1);

        scanlines++;
        ++clr;
    }
}

void PBL_Setup_DrawHook_PrepareDrawingFX(void)
{
    RSDK.SetClipBounds(0, 0, 0, ScreenInfo->size.x, ScreenInfo->size.y);
    RSDK.SetActivePalette(0, 0, ScreenInfo->size.y);
}

void PBL_Setup_ExitPinball(void)
{
    EntityPBL_Setup *setup = RSDK_GET_ENTITY(SLOT_BSS_SETUP, PBL_Setup);
    setup->visible         = true;
    setup->state           = PBL_Setup_SaveAndChangeScene;
    setup->color           = 0x000000;

    Music_FadeOut(0.025);

    PauseMenu->disableEvents = true;
    int32 oldScore           = globals->restartScore;
    globals->restartScore += PBL_Setup->score;
    if (globals->restartScore > 9999999 || globals->restartScore < oldScore)
        globals->restartScore = 9999999;

    while (globals->restartScore1UP < globals->restartScore) globals->restartScore1UP += 50000;
}

void PBL_Setup_GiveScore(int32 score)
{
    PBL_Setup->score += score;
    if (PBL_Setup->score > 9999999)
        PBL_Setup->score = 9999999;

    if (PBL_Setup->score > PBL_Setup->score1UP) {
        RSDK.PlaySfx(PBL_Setup->sfxContinue, false, 255);

        SaveRAM *saveRAM = SaveGame_GetSaveRAM();
        if (saveRAM->continues < 20)
            saveRAM->continues++;

        while (PBL_Setup->score1UP <= PBL_Setup->score) PBL_Setup->score1UP += 10000;

        foreach_active(PBL_HUD, hud) { PBL_HUD_DisplayMessage(hud, "!CONTINUE!", PBL_HUD_MSG_SCROLL_LEFT); }
    }
}

void PBL_Setup_GiveLife(void)
{
    SaveRAM *saveRAM = SaveGame_GetSaveRAM();

    if (globals->gameMode != MODE_TIMEATTACK && globals->gameMode != MODE_ENCORE) {
        if (saveRAM->lives < 99)
            saveRAM->lives++;

        Music_PlayJingle(TRACK_1UP);
    }
}

void PBL_Setup_State_FadeIn(void)
{
    RSDK_THIS(PBL_Setup);

    if (self->timer <= 0) {
        self->timer   = 0;
        self->visible = false;
        self->state   = PBL_Setup_State_ManageStageExit;

        SceneInfo->timeEnabled = true;
    }
    else {
        self->timer -= 16;
    }
}

void PBL_Setup_State_ManageStageExit(void)
{
    EntityPBL_Camera *camera = RSDK_GET_ENTITY(SLOT_PBL_CAMERA, PBL_Camera);

    foreach_active(PBL_Player, player)
    {
        if (player->position.y > camera->curCamBoundaryB + 0x400000) {
            PBL_Setup_ExitPinball();
            player->state = StateMachine_None;
        }
    }
}

void PBL_Setup_SaveAndChangeScene(void)
{
    RSDK_THIS(PBL_Setup);

    if (self->timer >= 1024) {
        SaveRAM *saveRAM = SaveGame_GetSaveRAM();
        if (saveRAM) {
            saveRAM->characterFlags = globals->characterFlags;
            saveRAM->stock          = globals->stock;
            saveRAM->playerID       = globals->playerID;

            if (globals->gameMode == MODE_ENCORE)
                RSDK.SetScene("Encore Mode", "");
            else
                RSDK.SetScene("Mania Mode", "");

            SceneInfo->listPos = saveRAM->storedStageID;
        }
        else {
            RSDK.SetScene("Presentation", "Title Screen");
        }

        RSDK.LoadScene();
    }
    else {
        self->timer += 16;
    }
}

#if GAME_INCLUDE_EDITOR
void PBL_Setup_EditorDraw(void) {}

void PBL_Setup_EditorLoad(void) {}
#endif

void PBL_Setup_Serialize(void) {}
#endif
