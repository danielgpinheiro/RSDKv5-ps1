/*
 * Scene3D's projection on the GTE (docs/ports/mania/32 7.3; Sonic 2's PS1TransformProject3D, RSDKv4-ps1): RTPT, three vertices a
 * command (psx-spx "GTE Coordinate Calculation Commands"). The engine's Scene3D.cpp can't include psyqo's headers (its
 * assert), so this file is the psyqo side and speaks plain integers.
 *
 * PS1Gte3DSetup loads the rotation (3x3, 4.12), the translation, H and the screen offset; PS1Gte3DProject transforms
 * `count` vertices (int16 x, y, z each) into screen positions (x | y << 16, as the GPU takes them) and depths (the GTE's
 * SZ). A vertex the GTE flags (IR / SZ / SX / SY saturated, divide overflow: behind or at the camera, far off screen)
 * gets depth 0: faces using it are not drawn.
 */
#pragma once

#include <stdint.h>

void PS1Gte3DSetup(const int32_t r[9], int32_t trx, int32_t trY, int32_t trz, uint32_t h, int32_t ofx, int32_t ofy);
int PS1Gte3DProject(const int16_t *xyz, int count, uint32_t *sxy, uint16_t *sz); // -> vertices flagged
// 9f: the same, plus each vertex's outcode (PS1Scene3D.cpp's face test; the screen sw x sh, depth zmin)
int PS1Gte3DProjectOc(const int16_t *xyz, int count, uint32_t *sxy, uint16_t *sz, uint8_t *oc, uint32_t zmin, int32_t sw,
                      int32_t sh);
// 9f (UFO item 5b): the same with only the near bit (PS1_OC_NEAR) in each outcode: a model wholly on screen (the face
// loops read no other bit of its corners)
int PS1Gte3DProjectNear(const int16_t *xyz, int count, uint32_t *sxy, uint16_t *sz, uint8_t *oc, uint32_t zmin, int32_t sw,
                        int32_t sh);

extern volatile uint32_t g_ps1GteVertices; // vertices projected (total)
extern volatile uint32_t g_ps1GteFlagged;  // ... of them flagged (total)
