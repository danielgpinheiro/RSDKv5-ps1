#ifndef OBJ_LOGHELPERS_H
#define OBJ_LOGHELPERS_H

#include "Game.h"

// Object Class
struct ObjectLogHelpers {
    RSDK_OBJECT
};

// Entity Class
struct EntityLogHelpers {
    RSDK_ENTITY
};

// Object Entity
extern ObjectLogHelpers *LogHelpers;

// Standard Entity Events
void LogHelpers_Update(void);
void LogHelpers_LateUpdate(void);
void LogHelpers_StaticUpdate(void);
void LogHelpers_Draw(void);
void LogHelpers_Create(void *data);
void LogHelpers_StageLoad(void);
#if GAME_INCLUDE_EDITOR
void LogHelpers_EditorDraw(void);
void LogHelpers_EditorLoad(void);
#endif
void LogHelpers_Serialize(void);

// Extra Entity Functions
void LogHelpers_PrintText(const char *message);
#if MANIA_USE_PLUS
void LogHelpers_PrintString(String *message);
#endif
void LogHelpers_PrintInt32(const char *message, int32 value);
void LogHelpers_Print(const char *message, ...);
#if MANIA_USE_PLUS
void LogHelpers_PrintVector2(const char *message, Vector2 value);
#endif

// docs/32 phase 13: the PS1's release builds print no game logs (they went to the BIOS TTY, which only an emulator shows):
// the calls and their strings cost resident RAM the stock 2 MB doesn't have. `make PS1_LOGS=1` keeps them.
#if defined RETRO_PS1_BUILD && !defined PS1_GAME_LOGS && !defined LOGHELPERS_IMPL
#define LogHelpers_PrintText(...)    ((void)0)
#define LogHelpers_PrintString(...)  ((void)0)
#define LogHelpers_PrintInt32(...)   ((void)0)
#define LogHelpers_Print(...)        ((void)0)
#define LogHelpers_PrintVector2(...) ((void)0)
#endif

#endif //! OBJ_LOGHELPERS_H
