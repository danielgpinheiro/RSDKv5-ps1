// PS1 port (docs/32 phase 2.3b): the tile collision masks without the stored flips. Included by Scene.hpp inside
// namespace RSDK.
//
// Stock LoadTileConfig stores each tile's masks four times (none / X / Y / XY): 2 planes x 4096 x 64 bytes = 512 KB.
// The flipped masks are pure functions of the unflipped ones, so the PS1 keeps 2 x 1024 (128 KB) and derives a flipped
// value where the collision code reads it. tools/collision/check_flips.py checks every flipped value of all 40
// TileConfigs against the engine's own flip loops (15,728,640 values, identical). tileInfo (angles, flags) keeps the
// stock pre-flipped layout: SetTileAngle / SetTileFlags write single entries of it.
#pragma once

// M(v): a height seen from the other side (0xFF = no collision stays 0xFF).
inline uint8 PS1MaskMirror(uint8 v) { return v == 0xFF ? 0xFF : (uint8)(0xF - v); }

// tile = the layout value's low 12 bits (tile index | FLIP_X 0x400 | FLIP_Y 0x800), i = 0..15.
inline uint8 PS1MaskFloor(uint8 plane, uint16 tile, int32 i)
{
    const CollisionMask *b = &collisionMasks[plane][tile & 0x3FF];
    int32 idx              = (tile & 0x400) ? 15 - i : i;
    return (tile & 0x800) ? PS1MaskMirror(b->roofMasks[idx]) : b->floorMasks[idx];
}
inline uint8 PS1MaskRoof(uint8 plane, uint16 tile, int32 i)
{
    const CollisionMask *b = &collisionMasks[plane][tile & 0x3FF];
    int32 idx              = (tile & 0x400) ? 15 - i : i;
    return (tile & 0x800) ? PS1MaskMirror(b->floorMasks[idx]) : b->roofMasks[idx];
}
inline uint8 PS1MaskLWall(uint8 plane, uint16 tile, int32 i)
{
    const CollisionMask *b = &collisionMasks[plane][tile & 0x3FF];
    int32 idx              = (tile & 0x800) ? 15 - i : i;
    return (tile & 0x400) ? PS1MaskMirror(b->rWallMasks[idx]) : b->lWallMasks[idx];
}
inline uint8 PS1MaskRWall(uint8 plane, uint16 tile, int32 i)
{
    const CollisionMask *b = &collisionMasks[plane][tile & 0x3FF];
    int32 idx              = (tile & 0x800) ? 15 - i : i;
    return (tile & 0x400) ? PS1MaskMirror(b->lWallMasks[idx]) : b->rWallMasks[idx];
}
