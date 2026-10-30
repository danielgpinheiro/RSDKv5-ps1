#ifndef OBJ_UFO_SETUP_H
#define OBJ_UFO_SETUP_H

#include "Game.h"

typedef enum {
    UFO_TFLAGS_NORMAL,
    UFO_TFLAGS_BUMPER,
    UFO_TFLAGS_GRITTYGROUND,
} TileFlagsUFO;

// Object Class
struct ObjectUFO_Setup {
    RSDK_OBJECT
    StateMachine(deformCB);
    int32 timedOut;
#if MANIA_USE_PLUS
    bool32 encoreStage;
#endif
    int32 machLevel;
    int32 scoreBonus;
    int32 machPoints;
    int32 rings;
    int32 timer;
    int32 ringFrame;
    int32 ringPan;
    int32 spherePan;
    int32 specialStageID;
    int32 machQuotas[3];
    uint8 playFieldLayer;
    uint16 sfxBlueSphere;
    uint16 sfxSSExit;
    uint16 sfxEmerald;
    uint16 sfxEvent;
    bool32 resetToTitle;
};

// Entity Class
struct EntityUFO_Setup {
    RSDK_ENTITY
    StateMachine(state);
    int32 timer;
    color fadeColor;
};

// Object Struct
extern ObjectUFO_Setup *UFO_Setup;

// Standard Entity Events
void UFO_Setup_Update(void);
void UFO_Setup_LateUpdate(void);
void UFO_Setup_StaticUpdate(void);
void UFO_Setup_Draw(void);
void UFO_Setup_Create(void *data);
void UFO_Setup_StageLoad(void);
#if GAME_INCLUDE_EDITOR
void UFO_Setup_EditorDraw(void);
void UFO_Setup_EditorLoad(void);
#endif
void UFO_Setup_Serialize(void);

// Extra Entity Functions
void UFO_Setup_DrawHook_PrepareDrawingFX(void);
void UFO_Setup_Scanline_Playfield(ScanlineInfo *scanlines);
void UFO_Setup_Scanline_3DFloor(ScanlineInfo *scanlines);
void UFO_Setup_Scanline_3DRoof(ScanlineInfo *scanlines);

void UFO_Setup_PlaySphereSfx(void);
void UFO_Setup_Finish_Win(void);
void UFO_Setup_Finish_Fail(void);
void UFO_Setup_Deform_UFO3(void);
void UFO_Setup_Deform_UFO4(void);
void UFO_Setup_Deform_UFO5(void);
void UFO_Setup_State_ShowStartMessage(void);
void UFO_Setup_State_FinishFadeout(void);
void UFO_Setup_State_HandleRingDrain(void);
void UFO_Setup_State_TimedOver(void);

#if defined RETRO_PS1_BUILD
// ps1-ufo: foreach_active without the engine's foreach stack (RSDK/Graphics/PS1/PS1Scene3D.cpp, the UFO overlays): the
// same walk of the class's type group, entity by entity (no foreach_break / foreach_return in these loops)
void *PS1ActiveNext(uint16 group, int32 *id);
// ps1-ufo: a ring's / sphere's camera-space position on the GTE, exactly as their LateUpdates compute it (ps1/gte3d.hh
// PS1GteCamSpace; 0: not done, compute it as stock); UFO_Camera's LateUpdate loads its matrix (PS1GteCamLoad)
void PS1GteCamLoad(const int32 (*m)[4]);
int PS1GteCamSpace(int32 x, int32 y, int32 z, int32 *out);
// ps1-ufo2 (round 3, B3; user 2026-10-30: far sprites may go): rings and spheres at this camera depth or farther are
// not drawn (0x7FFFFFFF: all; 0x8000 is full size, 0x20000 a quarter); RSDK/Graphics/PS1/PS1Scene3D.cpp
extern volatile int32 g_ps1UfoSpriteFar;
#define ufo_foreach_active(type, entityOut)                                                                            \
    Entity##type *entityOut = NULL;                                                                                    \
    for (int32 entityOut##Id_ = 0; (entityOut = (Entity##type *)PS1ActiveNext(type->classID, &entityOut##Id_)) != NULL;)
#else
#define ufo_foreach_active foreach_active
#endif

#endif //! OBJ_UFO_SETUP_H
