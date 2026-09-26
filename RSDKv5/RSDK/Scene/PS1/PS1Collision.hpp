// PS1 port (docs/32 phase 2.3b): the tile collision masks without the stored flips. Included by Scene.hpp inside
// namespace RSDK.
//
// Stock LoadTileConfig stores each tile's masks four times (none / X / Y / XY): 2 planes x 4096 x 64 bytes = 512 KB.
// The flipped masks are pure functions of the unflipped ones, so the PS1 keeps 2 x 1024 (128 KB) and derives a flipped
// value where the collision code reads it. tools/collision/check_flips.py checks every flipped value of all 40
// TileConfigs against the engine's own flip loops (15,728,640 values, identical).
//
// 2.3o: the angles are derived the same way (Scene.cpp's stock flip loops, byte arithmetic kept), and plane B is kept
// only for the tiles where it differs from plane A: 94.5% of the 40,960 tiles of all TileConfigs are equal; the most
// that differ in one is 235 (UFO4), 167 in GHZ. 168 KB -> 88 KB.
#pragma once

#define PS1_PLANEB_MAX (0x100)
extern uint16 ps1PlaneB[TILE_COUNT]; // 0xFFFF: plane B = plane A; else the tile's index in the plane-B tables
extern CollisionMask ps1MasksB[PS1_PLANEB_MAX];
extern TileInfo ps1InfoB[PS1_PLANEB_MAX];
extern uint16 ps1PlaneBCount;
extern volatile uint32 g_ps1PlaneBOverflow; // plane-B tiles with no room (GDB; must stay 0)

// t = 0..1023 (no flip bits)
inline CollisionMask *PS1BaseMask(uint8 plane, uint16 t)
{
    if (plane & 1) {
        uint16 b = ps1PlaneB[t];
        if (b != 0xFFFF)
            return &ps1MasksB[b];
    }
    return &collisionMasks[0][t];
}
inline TileInfo *PS1BaseInfo(uint8 plane, uint16 t)
{
    if (plane & 1) {
        uint16 b = ps1PlaneB[t];
        if (b != 0xFFFF)
            return &ps1InfoB[b];
    }
    return &tileInfo[0][t];
}
// A write into plane B where it shares plane A's tile: plane B gets its own copy first. NULL when there is no room.
bool32 PS1PlaneBSplit(uint16 t);
inline CollisionMask *PS1WritableMask(uint8 plane, uint16 t)
{
    if ((plane & 1) && ps1PlaneB[t] == 0xFFFF && !PS1PlaneBSplit(t))
        return NULL;
    return PS1BaseMask(plane, t);
}
inline TileInfo *PS1WritableInfo(uint8 plane, uint16 t)
{
    if ((plane & 1) && ps1PlaneB[t] == 0xFFFF && !PS1PlaneBSplit(t))
        return NULL;
    return PS1BaseInfo(plane, t);
}

// tile = the layout value's low 12 bits. The stock loops: FlipX negates (the walls swap), FlipY is -0x80 minus the
// opposite side, FlipXY is FlipX applied to the FlipY entry.
inline uint8 PS1TileAngle(uint8 plane, uint16 tile, uint8 cMode)
{
    const TileInfo *info = PS1BaseInfo(plane, tile & 0x3FF);
    uint8 f = info->floorAngle, l = info->lWallAngle, r = info->rWallAngle, ro = info->roofAngle;
    if (tile & 0x800) {
        uint8 yf = (uint8)(-0x80 - ro), yl = (uint8)(-0x80 - l), yr = (uint8)(-0x80 - r), yro = (uint8)(-0x80 - f);
        f = yf, l = yl, r = yr, ro = yro;
    }
    if (tile & 0x400) {
        uint8 xf = (uint8)-f, xl = (uint8)-r, xr = (uint8)-l, xro = (uint8)-ro;
        f = xf, l = xl, r = xr, ro = xro;
    }
    switch (cMode) {
        default: return 0;
        case CMODE_FLOOR: return f;
        case CMODE_LWALL: return l;
        case CMODE_ROOF: return ro;
        case CMODE_RWALL: return r;
    }
}

// M(v): a height seen from the other side (0xFF = no collision stays 0xFF).
inline uint8 PS1MaskMirror(uint8 v) { return v == 0xFF ? 0xFF : (uint8)(0xF - v); }

// tile = the layout value's low 12 bits (tile index | FLIP_X 0x400 | FLIP_Y 0x800), i = 0..15.
inline uint8 PS1MaskFloor(uint8 plane, uint16 tile, int32 i)
{
    const CollisionMask *b = PS1BaseMask(plane, tile & 0x3FF);
    int32 idx              = (tile & 0x400) ? 15 - i : i;
    return (tile & 0x800) ? PS1MaskMirror(b->roofMasks[idx]) : b->floorMasks[idx];
}
inline uint8 PS1MaskRoof(uint8 plane, uint16 tile, int32 i)
{
    const CollisionMask *b = PS1BaseMask(plane, tile & 0x3FF);
    int32 idx              = (tile & 0x400) ? 15 - i : i;
    return (tile & 0x800) ? PS1MaskMirror(b->floorMasks[idx]) : b->roofMasks[idx];
}
inline uint8 PS1MaskLWall(uint8 plane, uint16 tile, int32 i)
{
    const CollisionMask *b = PS1BaseMask(plane, tile & 0x3FF);
    int32 idx              = (tile & 0x800) ? 15 - i : i;
    return (tile & 0x400) ? PS1MaskMirror(b->rWallMasks[idx]) : b->lWallMasks[idx];
}
inline uint8 PS1MaskRWall(uint8 plane, uint16 tile, int32 i)
{
    const CollisionMask *b = PS1BaseMask(plane, tile & 0x3FF);
    int32 idx              = (tile & 0x800) ? 15 - i : i;
    return (tile & 0x400) ? PS1MaskMirror(b->lWallMasks[idx]) : b->rWallMasks[idx];
}
