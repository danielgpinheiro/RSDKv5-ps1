// ---------------------------------------------------------------------
// RSDK Project: Sonic Mania
// Object Description: Soundboard Object
// Object Author: Christian Whitehead/Simon Thomley/Hunter Bridges
// Decompiled by: Rubberduckycooly & RMGRich
// ---------------------------------------------------------------------

#include "Game.h"

ObjectSoundboard *Soundboard;

void Soundboard_Update(void) {}

void Soundboard_LateUpdate(void) {}

#if PS1_PROFILE
volatile unsigned ps1ProfSoundboard[2]; // counter-2 ticks of the check / update callbacks (docs/ports/mania/32 3.0)
#endif

void Soundboard_StaticUpdate(void)
{
    for (int32 s = 0; s < MIN(Soundboard->sfxCount, SOUNDBOARD_SFX_COUNT); ++s) {
        bool32 shouldStopSfx = true;

        if (Soundboard->sfxCheckCallback[s] && SceneInfo->state == ENGINESTATE_REGULAR) {
#if PS1_PROFILE
            unsigned short ps1T = *(volatile unsigned short *)0xBF801120;
            bool32 ps1Check     = Soundboard->sfxCheckCallback[s]();
            ps1ProfSoundboard[0] += (unsigned short)(*(volatile unsigned short *)0xBF801120 - ps1T);
            if (ps1Check) {
#else
            if (Soundboard->sfxCheckCallback[s]()) {
#endif
                shouldStopSfx                  = false;
                Soundboard->sfxFadeOutTimer[s] = 0;

                if (!Soundboard->sfxIsPlaying[s]) {
                    Soundboard->sfxChannel[s]      = RSDK.PlaySfx(Soundboard->sfxList[s], Soundboard->sfxLoopPoint[s], 255);
                    Soundboard->sfxIsPlaying[s]    = true;
                    Soundboard->sfxPlayingTimer[s] = 0;
                }

                if (Soundboard->sfxFadeOutDuration[s] > 0)
                    RSDK.SetChannelAttributes(Soundboard->sfxChannel[s], 1.0, 0.0, 1.0);

                if (Soundboard->sfxUpdateCallback[s]) {
#if PS1_PROFILE
                    unsigned short ps1U = *(volatile unsigned short *)0xBF801120;
                    Soundboard->sfxUpdateCallback[s](s);
                    ps1ProfSoundboard[1] += (unsigned short)(*(volatile unsigned short *)0xBF801120 - ps1U);
#else
                    Soundboard->sfxUpdateCallback[s](s);
#endif
                    ++Soundboard->sfxPlayingTimer[s];
                }
            }
        }

        if (shouldStopSfx) {
            if (Soundboard->sfxIsPlaying[s]) {
                Soundboard->sfxFadeOutTimer[s] = 0;

                if (!Soundboard->sfxFadeOutDuration[s])
                    RSDK.StopSfx(Soundboard->sfxList[s]);

                Soundboard->sfxIsPlaying[s]    = false;
                Soundboard->sfxPlayingTimer[s] = 0;
            }
            else if (Soundboard->sfxFadeOutDuration[s] > 0 && Soundboard->sfxChannel[s] > 0) {
                if (Soundboard->sfxFadeOutTimer[s] >= Soundboard->sfxFadeOutDuration[s]) {
                    RSDK.StopSfx(Soundboard->sfxList[s]);
                }
                else {
                    RSDK.SetChannelAttributes(Soundboard->sfxChannel[s],
                                              1.0 - (Soundboard->sfxFadeOutTimer[s] / (float)Soundboard->sfxFadeOutDuration[s]), 0.0, 1.0);
                    ++Soundboard->sfxFadeOutTimer[s];
                }
            }
        }
    }
}

void Soundboard_Draw(void) {}

void Soundboard_Create(void *data) {}

void Soundboard_StageLoad(void)
{
    Soundboard->active   = ACTIVE_ALWAYS;
    Soundboard->sfxCount = 0;

    for (int32 i = 0; i < SOUNDBOARD_SFX_COUNT; ++i) {
        Soundboard->sfxList[i]            = 0;
        Soundboard->sfxLoopPoint[i]       = 0;
        Soundboard->sfxCheckCallback[i]   = NULL;
        Soundboard->sfxUpdateCallback[i]  = NULL;
        Soundboard->sfxIsPlaying[i]       = false;
        Soundboard->sfxPlayingTimer[i]    = 0;
        Soundboard->sfxFadeOutDuration[i] = 0;
    }
}

uint8 Soundboard_LoadSfx(const char *sfxName, uint32 loopPoint, bool32 (*checkCallback)(void), void (*updateCallback)(int32))
{
    if (!Soundboard)
        return -1;
    if (Soundboard->sfxCount >= SOUNDBOARD_SFX_COUNT)
        return -1;

    int32 sfxID = Soundboard->sfxCount;

    Soundboard->sfxList[sfxID]            = RSDK.GetSfx(sfxName);
    Soundboard->sfxLoopPoint[sfxID]       = loopPoint;
    Soundboard->sfxCheckCallback[sfxID]   = checkCallback;
    Soundboard->sfxUpdateCallback[sfxID]  = updateCallback;
    Soundboard->sfxFadeOutDuration[sfxID] = 0;
    RSDK.StopSfx(Soundboard->sfxList[sfxID]);

    ++Soundboard->sfxCount;
    return sfxID;
}

#if GAME_INCLUDE_EDITOR
void Soundboard_EditorDraw(void) {}

void Soundboard_EditorLoad(void) {}
#endif

void Soundboard_Serialize(void) {}
