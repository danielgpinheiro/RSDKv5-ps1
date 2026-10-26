#ifndef OBJ_CHEMICALPOOL_H
#define OBJ_CHEMICALPOOL_H

#include "Game.h"

typedef enum { CHEMICALPOOL_BLUE, CHEMICALPOOL_GREEN, CHEMICALPOOL_CYAN } ChemicalPoolTypes;

// Object Class
struct ObjectChemicalPool {
    RSDK_OBJECT
    TABLE(color surfaceColorsFlash[4], { 0xF0F0F0, 0xF0F0F0, 0xE0E0E0, 0xE0E0E0 });
    uint16 aniFrames;
#if defined RETRO_PS1_BUILD
// PS1 port (docs/ports/mania/32 7b): indexed by the pools' tile columns, and CPZ (the only zone with pools) is 1200 / 512 tiles wide:
// 0x500 columns (15 KB, not 48: CPZ1 was 21 KB short of STG); a pool past them is cut (ChemicalPool_Create)
#define CHEMICALPOOL_COLUMNS (0x500)
#else
#define CHEMICALPOOL_COLUMNS (0x1000)
#endif
    int32 surfaceDeformation[CHEMICALPOOL_COLUMNS];
    int32 deformTable[CHEMICALPOOL_COLUMNS];
    int32 impactTable[CHEMICALPOOL_COLUMNS];
    uint16 unused; // exists, it's in the static object, but never used
    uint16 sfxChemYellow;
    uint16 sfxChemRed;
    uint16 sfxChemChange;
};

// Entity Class
struct EntityChemicalPool {
    RSDK_ENTITY
    StateMachine(state);
    int32 type;
    Vector2 size;
    int32 changeTimer;
    int32 timer;
    int32 tileSizeX;
    int32 offsetY;
    int32 maxDeform;
    int32 r;
    int32 g;
    int32 b;
    int32 leftEdge;
    int32 rightEdge;
    int32 impactPower;
    int32 impactPowerSides;
    Hitbox hitbox;
#if defined RETRO_PS1_BUILD
    int32 vertexY[66]; // PS1 port (docs/ports/mania/32 7b): the vertices' y; their x stay Create's grid (ChemicalPool.c VX): 264 B a pool
#else
    Vector2 vertices[66];
#endif
    color surfaceColors[4];
    Animator animator;
};

// Object Struct
extern ObjectChemicalPool *ChemicalPool;

// Standard Entity Events
void ChemicalPool_Update(void);
void ChemicalPool_LateUpdate(void);
void ChemicalPool_StaticUpdate(void);
void ChemicalPool_Draw(void);
void ChemicalPool_Create(void *data);
void ChemicalPool_StageLoad(void);
#if GAME_INCLUDE_EDITOR
void ChemicalPool_EditorDraw(void);
void ChemicalPool_EditorLoad(void);
#endif
void ChemicalPool_Serialize(void);

// Extra Entity Functions
void ChemicalPool_ChangeState(EntityChemicalPool *chemPool, int32 newType, int32 newR, int32 newG, int32 newB);
void ChemicalPool_ProcessDeformations(void);
void ChemicalPool_SetDeform(int32 impactX, int32 impactVelocity);
void ChemicalPool_SpawnDebris(int32 x, int32 y);
void ChemicalPool_SetupColors(void);

// States
void ChemicalPool_State_HarmfulBlue(void);
void ChemicalPool_State_Green(void);
void ChemicalPool_State_Cyan(void);
void ChemicalPool_State_Changing(void);

#endif //! OBJ_CHEMICALPOOL_H
