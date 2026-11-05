#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/Scene3DLegacy.cpp"
#endif

Model RSDK::modelList[MODEL_COUNT];
Scene3D RSDK::scene3DList[SCENE3D_COUNT];

ScanEdge RSDK::scanEdgeBuffer[SCREEN_YSIZE * 2];

enum ModelFlags {
    MODEL_NOFLAGS     = 0,
    MODEL_USENORMALS  = 1 << 0,
    MODEL_USETEXTURES = 1 << 1,
    MODEL_USECOLOURS  = 1 << 2,
};

void RSDK::ProcessScanEdge(int32 x1, int32 y1, int32 x2, int32 y2)
{
    int32 ix1 = FROM_FIXED(x1);
    int32 iy1 = FROM_FIXED(y1);
    int32 ix2 = FROM_FIXED(x2);
    int32 iy2 = FROM_FIXED(y2);

    int32 top = FROM_FIXED(y1);
    if (iy1 != iy2) {
        if (iy1 > iy2) {
            top = FROM_FIXED(y2);
            ix1 = FROM_FIXED(x2);
            ix2 = FROM_FIXED(x1);
            iy1 = FROM_FIXED(y2);
            iy2 = FROM_FIXED(y1);
        }

        int32 bottom = iy2 + 1;
        if (top < currentScreen->clipBound_Y2 && bottom >= currentScreen->clipBound_Y1) {
            if (bottom > currentScreen->clipBound_Y2)
                bottom = currentScreen->clipBound_Y2;
            int32 scanPos = TO_FIXED(ix1);
            int32 delta   = TO_FIXED(ix2 - ix1) / (iy2 - iy1);
            if (top < 0) {
                scanPos -= top * delta;
                top = 0;
            }

            ScanEdge *edge = &scanEdgeBuffer[top];
            for (int32 i = top; i < bottom; ++i) {
                int32 scanX = scanPos >> 16;
                if (scanX < edge->start)
                    edge->start = scanX;
                if (scanX > edge->end)
                    edge->end = scanX;
                scanPos += delta;
                ++edge;
            }
        }
    }
}

void RSDK::ProcessScanEdgeClr(uint32 c1, uint32 c2, int32 x1, int32 y1, int32 x2, int32 y2)
{
    int32 iy1 = FROM_FIXED(y1);
    int32 iy2 = FROM_FIXED(y2);
    int32 ix1 = FROM_FIXED(x1);
    int32 ix2 = FROM_FIXED(x2);

    int32 top     = FROM_FIXED(y1);
    uint32 color1 = c1;
    uint32 color2 = c2;
    if (iy1 != iy2) {
        if (iy1 > iy2) {
            top    = FROM_FIXED(y2);
            ix1    = FROM_FIXED(x2);
            ix2    = FROM_FIXED(x1);
            iy1    = FROM_FIXED(y2);
            iy2    = FROM_FIXED(y1);
            color1 = c2;
            color2 = c1;
        }

        int32 bottom = iy2 + 1;
        if (top < currentScreen->clipBound_Y2 && bottom >= currentScreen->clipBound_Y1) {
            if (bottom > currentScreen->clipBound_Y2)
                bottom = currentScreen->clipBound_Y2;

            int32 size   = iy2 - iy1;
            int32 scanX  = TO_FIXED(ix1);
            int32 deltaX = TO_FIXED(ix2 - ix1) / size;

            int32 c1R   = (color1 & 0xFF0000);
            int32 c2R   = (color2 & 0xFF0000);
            int32 scanR = c1R;

            int32 deltaR = 0;
            if (c1R != c2R)
                deltaR = (c2R - c1R) / size;

            int32 c1G   = (color1 & 0x00FF00) << 8;
            int32 c2G   = (color2 & 0x00FF00) << 8;
            int32 scanG = c1G;

            int32 deltaG = 0;
            if (c1G != c2G)
                deltaG = (c2G - c1G) / size;

            int32 c1B   = (color1 & 0x0000FF) << 16;
            int32 c2B   = (color2 & 0x0000FF) << 16;
            int32 scanB = c1B;

            int32 deltaB = 0;
            if (c1B != c2B)
                deltaB = (c2B - c1B) / size;

            if (top < 0) {
                scanX -= top * deltaX;

                scanR -= top * deltaR;
                scanG -= top * deltaG;
                scanB -= top * deltaB;

                top = 0;
            }

            ScanEdge *edge = &scanEdgeBuffer[top];
            for (int32 i = top; i < bottom; ++i) {
                if (FROM_FIXED(scanX) < edge->start) {
                    edge->start = FROM_FIXED(scanX);

                    edge->startR = scanR;
                    edge->startG = scanG;
                    edge->startB = scanB;
                }

                if (FROM_FIXED(scanX) > edge->end) {
                    edge->end = FROM_FIXED(scanX);

                    edge->endR = scanR;
                    edge->endG = scanG;
                    edge->endB = scanB;
                }

                scanX += deltaX;

                scanR += deltaR;
                scanG += deltaG;
                scanB += deltaB;

                ++edge;
            }
        }
    }
}

void RSDK::SetIdentityMatrix(Matrix *matrix)
{
    matrix->values[0][0] = 0x100;
    matrix->values[1][0] = 0;
    matrix->values[2][0] = 0;
    matrix->values[3][0] = 0;
    matrix->values[0][1] = 0;
    matrix->values[1][1] = 0x100;
    matrix->values[2][1] = 0;
    matrix->values[3][1] = 0;
    matrix->values[0][2] = 0;
    matrix->values[1][2] = 0;
    matrix->values[2][2] = 0x100;
    matrix->values[3][2] = 0;
    matrix->values[0][3] = 0;
    matrix->values[1][3] = 0;
    matrix->values[2][3] = 0;
    matrix->values[3][3] = 0x100;
}
#if RETRO_PLATFORM == RETRO_PS1
// docs/ports/mania/32 9f: the same sums, a product skipped where A's factor is 0 (it adds 0 exactly): the UFO stages' matrices are
// rotations and translations, mostly zeros, and the R3000's mult takes 6-13 cycles (UFO1: ~20 calls a frame)
__attribute__((optimize("O2"))) void RSDK::MatrixMultiply(Matrix *dest, Matrix *matrixA, Matrix *matrixB)
{
    int32 result[4][4];
    for (int32 rowA = 0; rowA < 4; ++rowA) {
        const int32 a0 = matrixA->values[0][rowA], a1 = matrixA->values[1][rowA], a2 = matrixA->values[2][rowA],
                    a3 = matrixA->values[3][rowA];
        for (int32 rowB = 0; rowB < 4; ++rowB) {
            const int32 *b = matrixB->values[rowB];
            int32 s        = 0;
            if (a3)
                s += a3 * b[3] >> 8;
            if (a2)
                s += a2 * b[2] >> 8;
            if (a1)
                s += a1 * b[1] >> 8;
            if (a0)
                s += a0 * b[0] >> 8;
            result[rowB][rowA] = s;
        }
    }
    memcpy(dest, result, sizeof(result));
}
#else
void RSDK::MatrixMultiply(Matrix *dest, Matrix *matrixA, Matrix *matrixB)
{
    int32 result[4][4];
    memset(result, 0, 4 * 4 * sizeof(int32));

    for (int32 i = 0; i < 0x10; ++i) {
        uint32 rowA        = i / 4;
        uint32 rowB        = i % 4;
        result[rowB][rowA] = (matrixA->values[3][rowA] * matrixB->values[rowB][3] >> 8) + (matrixA->values[2][rowA] * matrixB->values[rowB][2] >> 8)
                             + (matrixA->values[1][rowA] * matrixB->values[rowB][1] >> 8)
                             + (matrixA->values[0][rowA] * matrixB->values[rowB][0] >> 8);
    }

    for (int32 i = 0; i < 0x10; ++i) {
        uint32 rowA              = i / 4;
        uint32 rowB              = i % 4;
        dest->values[rowB][rowA] = result[rowB][rowA];
    }
}
#endif
void RSDK::MatrixTranslateXYZ(Matrix *matrix, int32 x, int32 y, int32 z, bool32 setIdentity)
{
    if (setIdentity) {
        matrix->values[0][0] = 0x100;
        matrix->values[1][0] = 0;
        matrix->values[2][0] = 0;
        matrix->values[0][1] = 0;
        matrix->values[1][1] = 0x100;
        matrix->values[2][1] = 0;
        matrix->values[0][2] = 0;
        matrix->values[1][2] = 0;
        matrix->values[2][2] = 0x100;
        matrix->values[3][0] = 0;
        matrix->values[3][1] = 0;
        matrix->values[3][2] = 0;
        matrix->values[3][3] = 0x100;
    }

    matrix->values[0][3] = x >> 8;
    matrix->values[1][3] = y >> 8;
    matrix->values[2][3] = z >> 8;
}
void RSDK::MatrixScaleXYZ(Matrix *matrix, int32 scaleX, int32 scaleY, int32 scaleZ)
{
    matrix->values[0][0] = scaleX;
    matrix->values[1][0] = 0;
    matrix->values[2][0] = 0;
    matrix->values[3][0] = 0;
    matrix->values[0][1] = 0;
    matrix->values[1][1] = scaleY;
    matrix->values[2][1] = 0;
    matrix->values[3][1] = 0;
    matrix->values[0][2] = 0;
    matrix->values[1][2] = 0;
    matrix->values[2][2] = scaleZ;
    matrix->values[3][2] = 0;
    matrix->values[0][3] = 0;
    matrix->values[1][3] = 0;
    matrix->values[2][3] = 0;
    matrix->values[3][3] = 0x100;
}
void RSDK::MatrixRotateX(Matrix *matrix, int16 rotationX)
{
    int32 sine   = sin1024LookupTable[rotationX & 0x3FF] >> 2;
    int32 cosine = cos1024LookupTable[rotationX & 0x3FF] >> 2;

    matrix->values[0][0] = 0x100;
    matrix->values[1][0] = 0;
    matrix->values[2][0] = 0;
    matrix->values[3][0] = 0;
    matrix->values[0][1] = 0;
    matrix->values[1][1] = cosine;
    matrix->values[2][1] = sine;
    matrix->values[3][1] = 0;
    matrix->values[0][2] = 0;
    matrix->values[1][2] = -sine;
    matrix->values[2][2] = cosine;
    matrix->values[3][2] = 0;
    matrix->values[0][3] = 0;
    matrix->values[1][3] = 0;
    matrix->values[2][3] = 0;
    matrix->values[3][3] = 0x100;
}
void RSDK::MatrixRotateY(Matrix *matrix, int16 rotationY)
{
    int32 sine           = sin1024LookupTable[rotationY & 0x3FF] >> 2;
    int32 cosine         = cos1024LookupTable[rotationY & 0x3FF] >> 2;
    matrix->values[0][0] = cosine;
    matrix->values[1][0] = 0;
    matrix->values[2][0] = sine;
    matrix->values[3][0] = 0;
    matrix->values[0][1] = 0;
    matrix->values[1][1] = 0x100;
    matrix->values[2][1] = 0;
    matrix->values[3][1] = 0;
    matrix->values[0][2] = -sine;
    matrix->values[1][2] = 0;
    matrix->values[2][2] = cosine;
    matrix->values[3][2] = 0;
    matrix->values[0][3] = 0;
    matrix->values[1][3] = 0;
    matrix->values[2][3] = 0;
    matrix->values[3][3] = 0x100;
}
void RSDK::MatrixRotateZ(Matrix *matrix, int16 rotationZ)
{
    int32 sine           = sin1024LookupTable[rotationZ & 0x3FF] >> 2;
    int32 cosine         = cos1024LookupTable[rotationZ & 0x3FF] >> 2;
    matrix->values[0][0] = cosine;
    matrix->values[1][0] = -sine;
    matrix->values[2][0] = 0;
    matrix->values[3][0] = 0;
    matrix->values[0][1] = sine;
    matrix->values[1][1] = cosine;
    matrix->values[2][1] = 0;
    matrix->values[3][1] = 0;
    matrix->values[0][2] = 0;
    matrix->values[1][2] = 0;
    matrix->values[2][2] = 0x100;
    matrix->values[3][2] = 0;
    matrix->values[0][3] = 0;
    matrix->values[1][3] = 0;
    matrix->values[2][3] = 0;
    matrix->values[3][3] = 0x100;
}
void RSDK::MatrixRotateXYZ(Matrix *matrix, int16 rotationX, int16 rotationY, int16 rotationZ)
{
    int32 sinX = sin1024LookupTable[rotationX & 0x3FF] >> 2;
    int32 cosX = cos1024LookupTable[rotationX & 0x3FF] >> 2;
    int32 sinY = sin1024LookupTable[rotationY & 0x3FF] >> 2;
    int32 cosY = cos1024LookupTable[rotationY & 0x3FF] >> 2;
    int32 sinZ = sin1024LookupTable[rotationZ & 0x3FF] >> 2;
    int32 cosZ = cos1024LookupTable[rotationZ & 0x3FF] >> 2;

    matrix->values[0][0] = (cosZ * cosY >> 8) + (sinZ * (sinY * sinX >> 8) >> 8);
    matrix->values[0][1] = -(sinZ * cosX) >> 8;
    matrix->values[0][2] = (sinZ * (cosY * sinX >> 8) >> 8) - (cosZ * sinY >> 8);
    matrix->values[0][3] = 0;
    matrix->values[1][0] = (sinZ * cosY >> 8) - (cosZ * (sinY * sinX >> 8) >> 8);
    matrix->values[1][1] = cosZ * cosX >> 8;
    matrix->values[1][2] = (-(sinZ * sinY) >> 8) - (cosZ * (cosY * sinX >> 8) >> 8);
    matrix->values[1][3] = 0;
    matrix->values[2][0] = sinY * cosX >> 8;
    matrix->values[2][1] = sinX;
    matrix->values[2][2] = cosY * cosX >> 8;
    matrix->values[2][3] = 0;
    matrix->values[3][0] = 0;
    matrix->values[3][1] = 0;
    matrix->values[3][2] = 0;
    matrix->values[3][3] = 0x100;
}
#if RETRO_PLATFORM == RETRO_PS1
// docs/ports/mania/32 9u: stock's cofactor formulas from a table (6.3 KB of unrolled soft-double code -> ~0.5 KB; no Mania game code
// calls it, the function table keeps it linked). Each cofactor is its six signed triple products evaluated in stock's
// order: ((m[a] * m[b]) * m[c]), the first negated when stock writes it with a leading minus (exact in IEEE: (-a) * b =
// -(a * b)), then added / subtracted left to right: the same double operations, so the same results
// (tools/host/minv_check.cpp: identical to the stock code on 4,000,000 matrices)
static const uint16 ps1InvTerms[96] = { 0x0FA5, 0x1EB5, 0x1F69, 0x0E79, 0x0B6D, 0x1A7D, 0x1FA1, 0x0EB1, 0x0F29, 0x1E39, 0x1B2D, 0x0A3D, 0x0F61, 0x1E71, 0x1F25, 0x0E35, 0x072D, 0x163D, 0x1B61, 0x0A71, 0x0B25, 0x1A35, 0x1729, 0x0639, 0x1FA4, 0x0EB4, 0x0F68, 0x1E78, 0x1B6C, 0x0A7C, 0x0FA0, 0x1EB0, 0x1F28, 0x0E38, 0x0B2C, 0x1A3C, 0x1F60, 0x0E70, 0x0F24, 0x1E34, 0x172C, 0x063C, 0x0B60, 0x1A70, 0x1B24, 0x0A34, 0x0728, 0x1638, 0x0F94, 0x1DB4, 0x1F58, 0x0D78, 0x0B5C, 0x197C, 0x1F90, 0x0DB0, 0x0F18, 0x1D38, 0x1B1C, 0x093C, 0x0F50, 0x1D70, 0x1F14, 0x0D34, 0x071C, 0x153C, 0x1B50, 0x0970, 0x0B14, 0x1934, 0x1718, 0x0538, 0x1E94, 0x0DA4, 0x0E58, 0x1D68, 0x1A5C, 0x096C, 0x0E90, 0x1DA0, 0x1E18, 0x0D28, 0x0A1C, 0x192C, 0x1E50, 0x0D60, 0x0E14, 0x1D24, 0x161C, 0x052C, 0x0A50, 0x1960, 0x1A14, 0x0924, 0x0618, 0x1528 };
void RSDK::MatrixInverse(Matrix *dest, Matrix *matrix)
{
    double inv[16], det;
    double m[16];
    for (int32 y = 0; y < 4; ++y)
        for (int32 x = 0; x < 4; ++x) m[(y << 2) + x] = matrix->values[y][x] / 256.0;
    const uint16 *t = ps1InvTerms;
    for (int32 k = 0; k < 16; ++k) {
        double acc = 0;
        for (int32 j = 0; j < 6; ++j, ++t) {
            double p = m[*t & 15] * m[(*t >> 4) & 15] * m[(*t >> 8) & 15];
            if (!j)
                acc = (*t & 0x1000) ? -p : p;
            else
                acc = (*t & 0x1000) ? acc - p : acc + p;
        }
        inv[k] = acc;
    }
    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0)
        return;
    det = 1.0 / det;
    for (int32 i = 0; i < 0x10; ++i) inv[i] = (int32)((inv[i] * det) * 256);
    for (int32 i = 0; i < 0x10; ++i) dest->values[i / 4][i % 4] = (int32)inv[i];
}
#else
void RSDK::MatrixInverse(Matrix *dest, Matrix *matrix)
{
    double inv[16], det;
    double m[16];
    for (int32 y = 0; y < 4; ++y) {
        for (int32 x = 0; x < 4; ++x) {
            m[(y << 2) + x] = matrix->values[y][x] / 256.0;
        }
    }

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];

    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];

    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];

    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];

    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];

    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];

    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];

    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];

    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];

    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    if (det == 0)
        return;

    det = 1.0 / det;

    for (int32 i = 0; i < 0x10; ++i) inv[i] = (int32)((inv[i] * det) * 256);
    for (int32 i = 0; i < 0x10; ++i) dest->values[i / 4][i % 4] = (int32)inv[i];
}
#endif

#if RETRO_PLATFORM == RETRO_PS1
// docs/ports/mania/32 7.3: Scene3D on the GTE (the Dreamcast port's approach: meshes with baked lighting, transformed in hardware).
// AddModelTo3DScene / AddMeshFrameTo3DScene record the model, its matrix, colour and (mesh frames) the two keyframes
// and the blend between them; Draw3DScene then, per recorded model: picks a unit shift k from the model's reach in view
// space (every view coordinate within 16 bits: exact for near models, coarser only for far ones), loads the GTE with
// the matrix (4.12, the y row negated for the screen), the translation >> k, H = 1 << projectionX and the screen centre,
// and projects the vertices (8-bit positions as << 7 against the matrix times the model's scale, 16-bit as they are;
// keyframes blended on the CPU first). A face with a vertex nearer than the PC's cut (z < 0x100) or flagged by the GTE
// is dropped (the PC skips it), one wholly off screen or past the GPU's primitive limits too; the rest are sorted by
// their mean depth (a bucket sort over the scene's own depth span: 256 steps; the PC's insertion sort orders exactly)
// and sent back to front as gouraud faces in their baked colours (the flat modes: the model's colour). Buffers: one STG
// block sized when the meshes load (twice the largest model: a scene holds the UFO and its emerald).
#include "PS1/PS1Scene3D.hpp"
PS1S3DItem g_ps1S3DItems[PS1_S3D_ITEMS];
int32 g_ps1S3DCount      = 0;
uint8 *g_ps1S3DWork       = NULL; // STG block (its owner pointer)
uint32 g_ps1S3DWorkVerts  = 0, g_ps1S3DWorkFaces = 0;
PS1Scene3DOps g_ps1S3DOps = {};
extern "C" {
volatile uint32 g_ps1S3DDraws    = 0; // Draw3DScene calls that drew (total)
volatile uint32 g_ps1S3DFaces    = 0; // faces sorted and sent (total)
volatile uint32 g_ps1S3DCulled   = 0; // faces dropped: near / flagged / off screen / too big (total)
volatile uint32 g_ps1S3DOverflow = 0; // models dropped: more than PS1_S3D_ITEMS in the scenes at once, or the work short
volatile uint32 g_ps1S3DTodo     = 0; // draws in a mode not done yet (wireframe, the non-screen modes)
volatile uint32 g_ps1S3DMatrix   = 0; // models whose matrix doesn't fit the GTE (skipped)
volatile uint32 g_ps1S3DHbl[4] = { 0, 0, 0, 0 }; // hblanks: transform, faces, sort, emit (total; GDB)
volatile int32 g_ps1S3DBackCull  = -1; // closed meshes' faces turned away dropped (positive screen winding: checked
                                       // against the unculled picture), 0 = none; 1 = the other sign (GDB test)
}
void RSDK::PS1Scene3DPrepare(uint16 sceneID)
{
    int32 k = 0;
    for (int32 i = 0; i < g_ps1S3DCount; ++i)
        if (g_ps1S3DItems[i].scene != sceneID)
            g_ps1S3DItems[k++] = g_ps1S3DItems[i];
    g_ps1S3DCount = k;
}
void RSDK::PS1Scene3DClear()
{
    g_ps1S3DCount = 0;
    g_ps1S3DWork = NULL, g_ps1S3DWorkVerts = 0, g_ps1S3DWorkFaces = 0; // (the stage's STG goes with it)
}
// a model loaded: its reach, and the work block grown to hold two of the largest models
static void PS1Scene3DModelLoaded(Model *model)
{
    uint32 faces = model->indexCount / model->faceVertCount;
    if (model->flags & PS1_MESH_POS16) {
        const int16 *p = (const int16 *)PS1MeshPos(model);
        int32 r = 0;
        for (uint32 i = 0; i < 3u * model->vertCount * model->frameCount; ++i) r = abs(p[i]) > r ? abs(p[i]) : r;
        model->ps1Radius = (uint16)r;
    }
    else {
        model->ps1Radius = (uint16)((127 * model->ps1Scale) >> 8);
    }
    uint32 needV = 2 * model->vertCount, needF = 2 * faces;
    if (needV <= g_ps1S3DWorkVerts && needF <= g_ps1S3DWorkFaces)
        return;
    needV = needV > g_ps1S3DWorkVerts ? needV : g_ps1S3DWorkVerts;
    needF = needF > g_ps1S3DWorkFaces ? needF : g_ps1S3DWorkFaces;
    if (g_ps1S3DWork)
        RemoveStorageEntry((void **)&g_ps1S3DWork);
    AllocateStorage((void **)&g_ps1S3DWork, needV * 7 + 16 /* 9f: positions, depths, outcodes (the faces go straight into the chain) */, DATASET_STG, false);
    g_ps1S3DWorkVerts = g_ps1S3DWork ? needV : 0, g_ps1S3DWorkFaces = g_ps1S3DWork ? needF : 0;
}
static void PS1Scene3DAdd(uint16 model, uint16 sceneIndex, uint8 drawMode, Matrix *matWorld, Matrix *matNormals, uint32 colour, uint16 a,
                          uint16 b, uint16 interp)
{
    if (model >= MODEL_COUNT || sceneIndex >= SCENE3D_COUNT || !matWorld || !modelList[model].vertices)
        return;
    if (g_ps1S3DCount >= PS1_S3D_ITEMS) {
        g_ps1S3DOverflow = g_ps1S3DOverflow + 1;
        return;
    }
    PS1S3DItem &it = g_ps1S3DItems[g_ps1S3DCount++];
    it.m = *matWorld, it.colour = colour, it.model = model, it.scene = sceneIndex;
    it.frameA = a, it.frameB = b, it.interp = interp, it.drawMode = drawMode;
    for (int32 c = 0; c < 3; ++c) it.n1[c] = matNormals ? matNormals->values[1][c] : 0;
    scene3DList[sceneIndex].drawMode = drawMode; // as the PC: the scene draws in its last model's mode
}
extern "C" {
volatile uint32 g_ps1MeshLoads  = 0; // PMD6 meshes loaded (total)
volatile uint32 g_ps1MeshBytes  = 0; // their bytes in STG (total)
volatile uint32 g_ps1MeshErrors = 0; // a mesh missing or not PMD6 (must stay 0)
}
#endif
uint16 RSDK::LoadMesh(const char *filename, uint8 scope)
{
    if (!scope || scope > SCOPE_STAGE)
        return -1;

    char fullFilePath[0x100];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Meshes/%s", filename);

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(fullFilePath, hash);

    for (int32 i = 0; i < MODEL_COUNT; ++i) {
        if (HASH_MATCH_MD5(hash, modelList[i].hash)) {
            return i;
        }
    }

    uint16 id = -1;
    for (id = 0; id < MODEL_COUNT; ++id) {
        if (modelList[id].scope == SCOPE_NONE)
            break;
    }

    if (id >= MODEL_COUNT)
        return -1;

    Model *model = &modelList[id];
    FileInfo info;
    InitFileInfo(&info);
#if RETRO_PLATFORM == RETRO_PS1
    // docs/ports/mania/32 7.2 / 7.4: the disc carries the PMD6 mesh (the Dreamcast port's optimised mesh, packed at build time): one read
    // into one STG block, no decoding
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        uint8 head[16];
        ReadBytes(&info, head, sizeof(head));
        uint32 size = (uint32)info.fileSize - sizeof(head);
        if (memcmp(head, "PMD6", 4) || (head[5] != 3 && head[5] != 4)) {
            CloseFile(&info);
            g_ps1MeshErrors = g_ps1MeshErrors + 1;
            return -1;
        }
        model->scope         = scope;
        HASH_COPY_MD5(model->hash, hash);
        model->flags         = ((head[4] & 1) ? PS1_MESH_POS16 : 0) | ((head[4] & 2) ? PS1_MESH_CLOSED : 0)
                       | ((head[4] & 16) ? PS1_MESH_CUTLOD : 0);
        model->faceVertCount = head[5];
        model->vertCount     = head[6] | head[7] << 8;
        model->frameCount    = head[8] | head[9] << 8;
        model->ps1FrameStep  = head[10] ? head[10] : 1;
        model->ps1Scale      = head[12] | head[13] << 8;
        model->indexCount    = head[14] | head[15] << 8;
        model->texCoords = NULL, model->colors = NULL, model->indices = NULL;
        AllocateStorage((void **)&model->vertices, size, DATASET_STG, false);
        if (model->vertices)
            ReadBytes(&info, model->vertices, size);
        CloseFile(&info);
        g_ps1MeshBytes = g_ps1MeshBytes + size;
        g_ps1MeshLoads = g_ps1MeshLoads + 1;
        if (!model->vertices)
            return -1;
        // 7.4: a distance LOD after the positions (u16 corners, u16 0, colours, indices)
        model->ps1LodIndexCount = 0, model->ps1LodOffset = 0, model->ps1NormOffset = 0;
        uint32 off = (PS1MeshPosOffset(model) + (uint32)model->frameCount * model->vertCount * ((head[4] & 1) ? 6 : 3) + 3) & ~3u;
        if (head[4] & 4) {
            uint32 n = off + 4 <= size ? *(uint16 *)((uint8 *)model->vertices + off) : 0;
            if (n && off + 4 + 6 * n <= size && n % model->faceVertCount == 0)
                model->ps1LodIndexCount = (uint16)n, model->ps1LodOffset = off + 4;
            else
                g_ps1MeshErrors = g_ps1MeshErrors + 1;
            off = (off + 4 + 6 * n + 3) & ~3u;
        }
        if (head[4] & 8) { // normals per corner (the lit modes that don't project)
            uint32 need = 4u * model->indexCount;
            if ((head[4] & 32) && off + 4 <= size) // 9o: indexed
                need = 4 + 4u * *(uint16 *)((uint8 *)model->vertices + off) + model->indexCount;
            if (off + need <= size) {
                model->ps1NormOffset = off;
                if (head[4] & 32)
                    model->flags |= PS1_MESH_NRMIDX;
            }
            else
                g_ps1MeshErrors = g_ps1MeshErrors + 1;
        }
        PS1Scene3DModelLoaded(model);
        return id;
    }
    g_ps1MeshErrors = g_ps1MeshErrors + 1;
    return -1;
#endif
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        uint32 sig = ReadInt32(&info, false);

        if (sig != RSDK_SIGNATURE_MDL) {
            CloseFile(&info);
            return -1;
        }

        model->scope = scope;
        HASH_COPY_MD5(model->hash, hash);

        model->flags         = ReadInt8(&info);
        model->faceVertCount = ReadInt8(&info);

        model->vertCount  = ReadInt16(&info);
        model->frameCount = ReadInt16(&info);

        AllocateStorage((void **)&model->vertices, sizeof(ModelVertex) * model->vertCount * model->frameCount, DATASET_STG, true);
        if (model->flags & MODEL_USETEXTURES)
            AllocateStorage((void **)&model->texCoords, sizeof(TexCoord) * model->vertCount, DATASET_STG, true);
        if (model->flags & MODEL_USECOLOURS)
            AllocateStorage((void **)&model->colors, sizeof(Color) * model->vertCount, DATASET_STG, true);

        if (model->flags & MODEL_USETEXTURES) {
            for (int32 v = 0; v < model->vertCount; ++v) {
                model->texCoords[v].x = ReadSingle(&info);
                model->texCoords[v].y = ReadSingle(&info);
            }
        }

        if (model->flags & MODEL_USECOLOURS) {
            for (int32 v = 0; v < model->vertCount; ++v) {
                model->colors[v].color = ReadInt32(&info, false);
            }
        }

        model->indexCount = ReadInt16(&info);
        AllocateStorage((void **)&model->indices, sizeof(uint16) * model->indexCount, DATASET_STG, true);
        for (int32 i = 0; i < model->indexCount; ++i) model->indices[i] = ReadInt16(&info);

        for (int32 f = 0; f < model->frameCount; ++f) {
            for (int32 v = 0; v < model->vertCount; ++v) {
                model->vertices[(f * model->vertCount) + v].x = (int32)(ReadSingle(&info) * 0x100);
                model->vertices[(f * model->vertCount) + v].y = (int32)(ReadSingle(&info) * 0x100);
                model->vertices[(f * model->vertCount) + v].z = (int32)(ReadSingle(&info) * 0x100);

                model->vertices[(f * model->vertCount) + v].nx = 0;
                model->vertices[(f * model->vertCount) + v].ny = 0;
                model->vertices[(f * model->vertCount) + v].nz = 0;
                if (model->flags & MODEL_USENORMALS) {
                    model->vertices[(f * model->vertCount) + v].nx = (int32)(ReadSingle(&info) * 0x10000);
                    model->vertices[(f * model->vertCount) + v].ny = (int32)(ReadSingle(&info) * 0x10000);
                    model->vertices[(f * model->vertCount) + v].nz = (int32)(ReadSingle(&info) * 0x10000);
                }
            }
        }

        CloseFile(&info);
        return id;
    }
    return -1;
}
uint16 RSDK::Create3DScene(const char *name, uint16 vertexLimit, uint8 scope)
{
    if (!scope || scope > SCOPE_STAGE)
        return -1;

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(name, hash);

    for (int32 i = 0; i < SCENE3D_COUNT; ++i) {
        if (HASH_MATCH_MD5(hash, scene3DList[i].hash)) {
            return i;
        }
    }

    uint16 id = -1;
    for (id = 0; id < SCENE3D_COUNT; ++id) {
        if (scene3DList[id].scope == SCOPE_NONE)
            break;
    }

    if (id >= SCENE3D_COUNT)
        return -1;

    Scene3D *scene = &scene3DList[id];

    if (vertexLimit > SCENE3D_VERT_COUNT || !vertexLimit)
        vertexLimit = SCENE3D_VERT_COUNT;
#if RETRO_PLATFORM == RETRO_PS1
    // PS1 (docs/ports/mania/32 2.3n): no software scene buffers (SpecialRing's: 41.5 KB). Phase 3 transforms the models with the GTE
    // straight into GPU packets (the ordering table sorts them); until then AddModelTo3DScene adds nothing (its limit
    // check) and Draw3DScene returns.
    vertexLimit = 0;
#endif

    scene->scope = scope;
    HASH_COPY_MD5(scene->hash, hash);
    scene->vertLimit = vertexLimit;
    scene->faceCount = 6;

    scene->projectionX = 8;
    scene->projectionY = 8;
    AllocateStorage((void **)&scene->vertices, sizeof(Scene3DVertex) * vertexLimit, DATASET_STG, true);
    AllocateStorage((void **)&scene->normals, sizeof(Scene3DVertex) * vertexLimit, DATASET_STG, true);
    AllocateStorage((void **)&scene->faceVertCounts, sizeof(uint8) * vertexLimit, DATASET_STG, true);
    AllocateStorage((void **)&scene->faceBuffer, sizeof(Scene3DFace) * vertexLimit, DATASET_STG, true);

    return id;
}
void RSDK::AddModelToScene(uint16 modelFrames, uint16 sceneIndex, uint8 drawMode, Matrix *matWorld, Matrix *matNormals, color color)
{
#if RETRO_PLATFORM == RETRO_PS1
    PS1Scene3DAdd(modelFrames, sceneIndex, drawMode, matWorld, matNormals, color, 0, 0, 0); // docs/ports/mania/32 7.3
    return;
#endif
    if (modelFrames < MODEL_COUNT && sceneIndex < SCENE3D_COUNT) {
        if (matWorld) {
            Model *mdl            = &modelList[modelFrames];
            Scene3D *scn          = &scene3DList[sceneIndex];
            uint16 *indices       = mdl->indices;
            int32 vertID          = scn->vertexCount;
            uint8 *faceVertCounts = &scn->faceVertCounts[scn->faceCount];
            int32 indCnt          = mdl->indexCount;
            if (scn->vertLimit - vertID >= indCnt) {
                scn->vertexCount += mdl->indexCount;
                scn->drawMode = drawMode;
                scn->faceCount += indCnt / mdl->faceVertCount;

                int32 i = 0;
                int32 f = 0;
                switch (mdl->flags) {
                    default:
                    case MODEL_NOFLAGS:
                    case MODEL_USECOLOURS:
                        for (; i < mdl->indexCount;) {
                            faceVertCounts[f++] = mdl->faceVertCount;

                            for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                ModelVertex *modelVert = &mdl->vertices[indices[i++]];
                                Scene3DVertex *vertex  = &scn->vertices[vertID++];

                                vertex->x = matWorld->values[0][3] + (modelVert->z * matWorld->values[0][2] >> 8)
                                            + (matWorld->values[0][0] * modelVert->x >> 8) + (matWorld->values[0][1] * modelVert->y >> 8);
                                vertex->y = matWorld->values[1][3] + (modelVert->y * matWorld->values[1][1] >> 8)
                                            + (modelVert->z * matWorld->values[1][2] >> 8) + (matWorld->values[1][0] * modelVert->x >> 8);
                                vertex->z = matWorld->values[2][3] + ((modelVert->x * matWorld->values[2][0]) >> 8)
                                            + ((matWorld->values[2][2] * modelVert->z >> 8) + (matWorld->values[2][1] * modelVert->y >> 8));

                                vertex->color = color;
                            }
                        }
                        break;

                    case MODEL_USENORMALS:
                        if (matNormals) {
                            for (; i < mdl->indexCount;) {
                                faceVertCounts[f++] = mdl->faceVertCount;

                                for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                    ModelVertex *modelVert = &mdl->vertices[indices[i++]];
                                    Scene3DVertex *vertex  = &scn->vertices[vertID++];

                                    vertex->x = matWorld->values[0][3] + (modelVert->z * matWorld->values[0][2] >> 8)
                                                + (modelVert->x * matWorld->values[0][0] >> 8) + (modelVert->y * matWorld->values[0][1] >> 8);
                                    vertex->y = matWorld->values[1][3] + (modelVert->y * matWorld->values[1][1] >> 8)
                                                + (matWorld->values[1][0] * modelVert->x >> 8) + (modelVert->z * matWorld->values[1][2] >> 8);
                                    vertex->z = matWorld->values[2][3] + (modelVert->x * matWorld->values[2][0] >> 8)
                                                + (matWorld->values[2][2] * modelVert->z >> 8) + (matWorld->values[2][1] * modelVert->y >> 8);

                                    vertex->nx = (modelVert->nz * matNormals->values[0][2] >> 8) + (modelVert->nx * matNormals->values[0][0] >> 8)
                                                 + (matNormals->values[0][1] * modelVert->ny >> 8);
                                    vertex->ny = (modelVert->ny * matNormals->values[1][1] >> 8) + (modelVert->nz * matNormals->values[1][2] >> 8)
                                                 + (modelVert->nx * matNormals->values[1][0] >> 8);
                                    vertex->nz =
                                        ((modelVert->ny * matNormals->values[2][1]) >> 8)
                                        + ((matNormals->values[2][0] * modelVert->nx >> 8) + (modelVert->nz * matNormals->values[2][2] >> 8));

                                    vertex->color = color;
                                }
                            }
                        }
                        else {
                            for (; i < mdl->indexCount;) {
                                faceVertCounts[f++] = mdl->faceVertCount;

                                for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                    ModelVertex *modelVert = &mdl->vertices[indices[i++]];
                                    Scene3DVertex *vertex  = &scn->vertices[vertID++];

                                    vertex->x = matWorld->values[0][3] + (modelVert->z * matWorld->values[0][2] >> 8)
                                                + (matWorld->values[0][0] * modelVert->x >> 8) + (matWorld->values[0][1] * modelVert->y >> 8);
                                    vertex->y = matWorld->values[1][3] + (modelVert->y * matWorld->values[1][1] >> 8)
                                                + (modelVert->z * matWorld->values[1][2] >> 8) + (matWorld->values[1][0] * modelVert->x >> 8);
                                    vertex->z = matWorld->values[2][3] + ((matWorld->values[2][2] * modelVert->z) >> 8)
                                                + ((matWorld->values[2][0] * modelVert->x >> 8) + (matWorld->values[2][1] * modelVert->y >> 8));

                                    vertex->color = color;
                                }
                            }
                        }
                        break;

                    case MODEL_USENORMALS | MODEL_USECOLOURS:
                        if (matNormals) {
                            for (; i < mdl->indexCount;) {
                                faceVertCounts[f++] = mdl->faceVertCount;

                                for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                    ModelVertex *modelVert = &mdl->vertices[indices[i]];
                                    Color *modelColor      = &mdl->colors[indices[i++]];
                                    Scene3DVertex *vertex  = &scn->vertices[vertID++];

                                    vertex->x = matWorld->values[0][3] + (matWorld->values[0][2] * modelVert->z >> 8)
                                                + (modelVert->y * matWorld->values[0][1] >> 8) + (matWorld->values[0][0] * modelVert->x >> 8);
                                    vertex->y = matWorld->values[1][3] + (matWorld->values[1][2] * modelVert->z >> 8)
                                                + (modelVert->y * matWorld->values[1][1] >> 8) + (matWorld->values[1][0] * modelVert->x >> 8);
                                    vertex->z = matWorld->values[2][3] + (modelVert->x * matWorld->values[2][0] >> 8)
                                                + (modelVert->y * matWorld->values[2][1] >> 8) + (matWorld->values[2][2] * modelVert->z >> 8);

                                    vertex->nx = (matNormals->values[0][0] * modelVert->nx >> 8) + (modelVert->ny * matNormals->values[0][1] >> 8)
                                                 + (matNormals->values[0][2] * modelVert->nz >> 8);
                                    vertex->ny = (matNormals->values[1][0] * modelVert->nx >> 8) + (modelVert->ny * matNormals->values[1][1] >> 8)
                                                 + (matNormals->values[1][2] * modelVert->nz >> 8);
                                    vertex->nz =
                                        ((matNormals->values[2][2] * modelVert->nz) >> 8)
                                        + ((modelVert->ny * matNormals->values[2][1] >> 8) + (matNormals->values[2][0] * modelVert->nx >> 8));

                                    vertex->color = modelColor->color;
                                }
                            }
                        }
                        else {
                            for (; i < mdl->indexCount;) {
                                faceVertCounts[f++] = mdl->faceVertCount;

                                for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                    ModelVertex *modelVert = &mdl->vertices[indices[i]];
                                    Color *modelColor      = &mdl->colors[indices[i++]];
                                    Scene3DVertex *vertex  = &scn->vertices[vertID++];

                                    vertex->x = matWorld->values[0][3] + (matWorld->values[0][0] * modelVert->x >> 8)
                                                + (modelVert->y * matWorld->values[0][1] >> 8) + (modelVert->z * matWorld->values[0][2] >> 8);
                                    vertex->y = matWorld->values[1][3] + (modelVert->z * matWorld->values[1][2] >> 8)
                                                + (matWorld->values[1][0] * modelVert->x >> 8) + (modelVert->y * matWorld->values[1][1] >> 8);
                                    vertex->z = matWorld->values[2][3] + (matWorld->values[2][2] * modelVert->z >> 8)
                                                + (modelVert->y * matWorld->values[2][1] >> 8) + (modelVert->x * matWorld->values[2][0] >> 8);

                                    vertex->color = modelColor->color;
                                }
                            }
                        }
                        break;
                }
            }
        }
    }
}
void RSDK::AddMeshFrameToScene(uint16 modelFrames, uint16 sceneIndex, Animator *animator, uint8 drawMode, Matrix *matWorld, Matrix *matNormals,
                               color color)
{
#if RETRO_PLATFORM == RETRO_PS1
    // docs/ports/mania/32 7.3: the keyframe, the next one (the loop's start after the last) and the blend (the timer over a kept
    // keyframe's span, 0x100 x the frame step)
    if (animator && modelFrames < MODEL_COUNT) {
        int32 next = animator->frameID + 1;
        if (next >= animator->frameCount)
            next = animator->loopIndex;
        int32 span   = animator->frameDuration > 0 ? animator->frameDuration : 0x100;
        int32 interp = (animator->timer << 8) / span;
        PS1Scene3DAdd(modelFrames, sceneIndex, drawMode, matWorld, matNormals, color, (uint16)animator->frameID, (uint16)next,
                      (uint16)(interp < 0 ? 0 : interp > 0x100 ? 0x100 : interp));
    }
    return;
#endif
    if (modelFrames < MODEL_COUNT && sceneIndex < SCENE3D_COUNT) {
        if (matWorld && animator) {
            Model *mdl            = &modelList[modelFrames];
            Scene3D *scn          = &scene3DList[sceneIndex];
            uint16 *indices       = mdl->indices;
            int32 vertID          = scn->vertexCount;
            uint8 *faceVertCounts = &scn->faceVertCounts[scn->faceCount];
            int32 indCnt          = mdl->indexCount;
            if (scn->vertLimit - vertID >= indCnt) {
                scn->vertexCount += mdl->indexCount;
                scn->drawMode = drawMode;
                scn->faceCount += indCnt / mdl->faceVertCount;

                int32 nextFrame = animator->frameID + 1;
                if (nextFrame >= animator->frameCount)
                    nextFrame = animator->loopIndex;
                int32 frameOffset     = animator->frameID * mdl->vertCount;
                int32 nextFrameOffset = nextFrame * mdl->vertCount;

                int32 i           = 0;
                int32 f           = 0;
                int32 interpolate = animator->timer;
                switch (mdl->flags) {
                    default:
                    case MODEL_NOFLAGS:
                    case MODEL_USECOLOURS:
                        for (; i < mdl->indexCount;) {
                            faceVertCounts[f++] = mdl->faceVertCount;

                            for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                ModelVertex *frameVert     = &mdl->vertices[frameOffset + indices[i]];
                                ModelVertex *nextFrameVert = &mdl->vertices[nextFrameOffset + indices[i]];
                                int32 x                    = frameVert->x + ((interpolate * (nextFrameVert->x - frameVert->x)) >> 8);
                                int32 y                    = frameVert->y + ((interpolate * (nextFrameVert->y - frameVert->y)) >> 8);
                                int32 z                    = frameVert->z + ((interpolate * (nextFrameVert->z - frameVert->z)) >> 8);
                                i++;
                                Scene3DVertex *vertex = &scn->vertices[vertID++];
                                vertex->x             = matWorld->values[0][3] + (z * matWorld->values[0][2] >> 8) + (matWorld->values[0][0] * x >> 8)
                                            + (matWorld->values[0][1] * y >> 8);
                                vertex->y = matWorld->values[1][3] + (y * matWorld->values[1][1] >> 8) + (z * matWorld->values[1][2] >> 8)
                                            + (matWorld->values[1][0] * x >> 8);
                                vertex->z = matWorld->values[2][3] + ((x * matWorld->values[2][0]) >> 8)
                                            + ((matWorld->values[2][2] * z >> 8) + (matWorld->values[2][1] * y >> 8));
                                vertex->color = color;
                            }
                        }
                        break;

                    case MODEL_USENORMALS:
                        if (matNormals) {
                            for (; i < mdl->indexCount;) {
                                faceVertCounts[f++] = mdl->faceVertCount;

                                for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                    ModelVertex *frameVert     = &mdl->vertices[frameOffset + indices[i]];
                                    ModelVertex *nextFrameVert = &mdl->vertices[nextFrameOffset + indices[i]];
                                    int32 x                    = frameVert->x + ((interpolate * (nextFrameVert->x - frameVert->x)) >> 8);
                                    int32 y                    = frameVert->y + ((interpolate * (nextFrameVert->y - frameVert->y)) >> 8);
                                    int32 z                    = frameVert->z + ((interpolate * (nextFrameVert->z - frameVert->z)) >> 8);
                                    int32 nx                   = frameVert->nx + ((interpolate * (nextFrameVert->nx - frameVert->nx)) >> 8);
                                    int32 ny                   = frameVert->ny + ((interpolate * (nextFrameVert->ny - frameVert->ny)) >> 8);
                                    int32 nz                   = frameVert->nz + ((interpolate * (nextFrameVert->nz - frameVert->nz)) >> 8);
                                    i++;

                                    Scene3DVertex *vertex = &scn->vertices[vertID++];
                                    vertex->x = matWorld->values[0][3] + (z * matWorld->values[0][2] >> 8) + (x * matWorld->values[0][0] >> 8)
                                                + (y * matWorld->values[0][1] >> 8);
                                    vertex->y = matWorld->values[1][3] + (y * matWorld->values[1][1] >> 8) + (matWorld->values[1][0] * x >> 8)
                                                + (z * matWorld->values[1][2] >> 8);
                                    vertex->z = matWorld->values[2][3] + (x * matWorld->values[2][0] >> 8) + (matWorld->values[2][2] * z >> 8)
                                                + (matWorld->values[2][1] * y >> 8);
                                    vertex->nx = (nz * matNormals->values[0][2] >> 8) + (nx * matNormals->values[0][0] >> 8)
                                                 + (matNormals->values[0][1] * ny >> 8);
                                    vertex->ny = (ny * matNormals->values[1][1] >> 8) + (nz * matNormals->values[1][2] >> 8)
                                                 + (nx * matNormals->values[1][0] >> 8);
                                    vertex->nz = ((ny * matNormals->values[2][1]) >> 8)
                                                 + ((matNormals->values[2][0] * nx >> 8) + (nz * matNormals->values[2][2] >> 8));
                                    vertex->color = color;
                                }
                            }
                        }
                        else {
                            for (; i < mdl->indexCount;) {
                                faceVertCounts[f++] = mdl->faceVertCount;

                                for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                    ModelVertex *frameVert     = &mdl->vertices[frameOffset + indices[i]];
                                    ModelVertex *nextFrameVert = &mdl->vertices[nextFrameOffset + indices[i]];
                                    int32 x                    = frameVert->x + ((interpolate * (nextFrameVert->x - frameVert->x)) >> 8);
                                    int32 y                    = frameVert->y + ((interpolate * (nextFrameVert->y - frameVert->y)) >> 8);
                                    int32 z                    = frameVert->z + ((interpolate * (nextFrameVert->z - frameVert->z)) >> 8);
                                    i++;
                                    Scene3DVertex *vertex = &scn->vertices[vertID++];
                                    vertex->x = matWorld->values[0][3] + (z * matWorld->values[0][2] >> 8) + (matWorld->values[0][0] * x >> 8)
                                                + (matWorld->values[0][1] * y >> 8);
                                    vertex->y = matWorld->values[1][3] + (y * matWorld->values[1][1] >> 8) + (z * matWorld->values[1][2] >> 8)
                                                + (matWorld->values[1][0] * x >> 8);
                                    vertex->z = matWorld->values[2][3] + ((matWorld->values[2][2] * z) >> 8)
                                                + ((matWorld->values[2][0] * x >> 8) + (matWorld->values[2][1] * y >> 8));
                                    vertex->color = color;
                                }
                            }
                        }
                        break;

                    case MODEL_USENORMALS | MODEL_USECOLOURS:
                        if (matNormals) {
                            for (; i < mdl->indexCount;) {
                                faceVertCounts[f++] = mdl->faceVertCount;

                                for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                    ModelVertex *frameVert     = &mdl->vertices[frameOffset + indices[i]];
                                    ModelVertex *nextFrameVert = &mdl->vertices[nextFrameOffset + indices[i]];
                                    int32 x                    = frameVert->x + ((interpolate * (nextFrameVert->x - frameVert->x)) >> 8);
                                    int32 y                    = frameVert->y + ((interpolate * (nextFrameVert->y - frameVert->y)) >> 8);
                                    int32 z                    = frameVert->z + ((interpolate * (nextFrameVert->z - frameVert->z)) >> 8);
                                    int32 nx                   = frameVert->nx + ((interpolate * (nextFrameVert->nx - frameVert->nx)) >> 8);
                                    int32 ny                   = frameVert->ny + ((interpolate * (nextFrameVert->ny - frameVert->ny)) >> 8);
                                    int32 nz                   = frameVert->nz + ((interpolate * (nextFrameVert->nz - frameVert->nz)) >> 8);

                                    Color *modelColor     = &mdl->colors[indices[i++]];
                                    Scene3DVertex *vertex = &scn->vertices[vertID++];
                                    vertex->x = matWorld->values[0][3] + (matWorld->values[0][2] * z >> 8) + (y * matWorld->values[0][1] >> 8)
                                                + (matWorld->values[0][0] * x >> 8);
                                    vertex->y = matWorld->values[1][3] + (matWorld->values[1][2] * z >> 8) + (y * matWorld->values[1][1] >> 8)
                                                + (matWorld->values[1][0] * x >> 8);
                                    vertex->z = matWorld->values[2][3] + (x * matWorld->values[2][0] >> 8) + (y * matWorld->values[2][1] >> 8)
                                                + (matWorld->values[2][2] * z >> 8);
                                    vertex->nx = (matNormals->values[0][0] * nx >> 8) + (ny * matNormals->values[0][1] >> 8)
                                                 + (matNormals->values[0][2] * nz >> 8);
                                    vertex->ny = (matNormals->values[1][0] * nx >> 8) + (ny * matNormals->values[1][1] >> 8)
                                                 + (matNormals->values[1][2] * nz >> 8);
                                    vertex->nz = ((matNormals->values[2][2] * nz) >> 8)
                                                 + ((ny * matNormals->values[2][1] >> 8) + (matNormals->values[2][0] * nx >> 8));
                                    vertex->color = modelColor->color;
                                }
                            }
                        }
                        else {
                            for (; i < mdl->indexCount;) {
                                faceVertCounts[f++] = mdl->faceVertCount;

                                for (int32 c = 0; c < mdl->faceVertCount; ++c) {
                                    ModelVertex *frameVert     = &mdl->vertices[frameOffset + indices[i]];
                                    ModelVertex *nextFrameVert = &mdl->vertices[nextFrameOffset + indices[i]];
                                    int32 x                    = frameVert->x + ((interpolate * (nextFrameVert->x - frameVert->x)) >> 8);
                                    int32 y                    = frameVert->y + ((interpolate * (nextFrameVert->y - frameVert->y)) >> 8);
                                    int32 z                    = frameVert->z + ((interpolate * (nextFrameVert->z - frameVert->z)) >> 8);
                                    Color *modelColor          = &mdl->colors[indices[i++]];
                                    Scene3DVertex *vertex      = &scn->vertices[vertID++];
                                    vertex->x = matWorld->values[0][3] + (matWorld->values[0][0] * x >> 8) + (y * matWorld->values[0][1] >> 8)
                                                + (z * matWorld->values[0][2] >> 8);
                                    vertex->y = matWorld->values[1][3] + (z * matWorld->values[1][2] >> 8) + (matWorld->values[1][0] * x >> 8)
                                                + (y * matWorld->values[1][1] >> 8);
                                    vertex->z = matWorld->values[2][3] + (matWorld->values[2][2] * z >> 8) + (y * matWorld->values[2][1] >> 8)
                                                + (x * matWorld->values[2][0] >> 8);
                                    vertex->color = modelColor->color;
                                }
                            }
                        }
                        break;
                }
            }
        }
    }
}

#if RETRO_PLATFORM == RETRO_PS1
// docs/ports/mania/32 7.4: the lit modes that don't project (the special ring: S3D_SOLIDCOLOR_SHADED_BLENDED, S3D_WIREFRAME_SHADED),
// as stock: positions through matWorld (8.8 world pixels), each corner's colour from its normal's y through matNormals
// with the scene's diffuse / specular, faces far first (stock's depth and insertion sort), drawn as gouraud quads / lines
// into the Face3D bulks. Up to PS1_S3D_ORTHO faces a scene (the ring: 80). False: not one of those modes.
#define PS1_S3D_ORTHO 128
static int32 PS1Lit(int32 ny, uint32 colour, int32 shift, int32 diffuse, int32 intensity, int32 specularIntensity)
{
    int32 normalVal = (ny >> 2) * (abs(ny) >> 2);
    int32 specular  = (normalVal >> 6) >> specularIntensity;
    specular        = specular < 0 ? 0 : specular > 0xFF ? 0xFF : specular;
    int32 c         = specular + ((int32)((colour >> shift) & 0xFF) * ((ny >> 10) + diffuse) >> intensity);
    return c < 0 ? 0 : c > 0xFF ? 0xFF : c;
}
// (9f: PS1Lit's three channels with the normal's product computed once: the same values)
__attribute__((optimize("O2"))) static uint32 PS1LitColour(const Scene3D *scn, int32 ny, uint32 colour)
{
    const int32 normalVal = (ny >> 2) * (abs(ny) >> 2), n6 = normalVal >> 6, ny10 = ny >> 10;
    int32 sx = n6 >> scn->specularIntensityX, sy = n6 >> scn->specularIntensityY, sz = n6 >> scn->specularIntensityZ;
    sx = sx < 0 ? 0 : sx > 0xFF ? 0xFF : sx, sy = sy < 0 ? 0 : sy > 0xFF ? 0xFF : sy, sz = sz < 0 ? 0 : sz > 0xFF ? 0xFF : sz;
    int32 cx = sx + ((int32)((colour >> 16) & 0xFF) * (ny10 + scn->diffuseX) >> scn->diffuseIntensityX);
    int32 cy = sy + ((int32)((colour >> 8) & 0xFF) * (ny10 + scn->diffuseY) >> scn->diffuseIntensityY);
    int32 cz = sz + ((int32)(colour & 0xFF) * (ny10 + scn->diffuseZ) >> scn->diffuseIntensityZ);
    cx = cx < 0 ? 0 : cx > 0xFF ? 0xFF : cx, cy = cy < 0 ? 0 : cy > 0xFF ? 0xFF : cy, cz = cz < 0 ? 0 : cz > 0xFF ? 0xFF : cz;
    return (uint32)cx << 16 | (uint32)cy << 8 | (uint32)cz;
}
// 9f: a closed mesh's faces turned away on screen are left out before the sort (the giant ring is opaque and closed:
// its visible pixels are all front faces'); the sign a front face's screen winding is checked against (-1, picked by
// pictures: +1 dropped the visible side), 0: none dropped
volatile int32 g_ps1S3DOrthoCull = -1;
volatile int32 g_ps1S3DAsm = 1; // 9f: the UFO 3D faces / keyframe blend in assembly (ps1/s3d_faces.s, s3d_blend.s; 0: C)
extern "C" volatile uint32 g_ps1BlendWrong, g_ps1BlendChecked;
volatile uint32 g_ps1BlendWrong = 0, g_ps1BlendChecked = 0; // 9f: PS1_CHECK_BLEND builds: components compared, wrong
// 9f: a normal component (int8, x 127) as LoadMesh keeps it (x 0x10000 / 127), from a table: the same quotients
static int32 s_ps1Nrm127[256];
static bool s_ps1Nrm127Ok = false;
// P5 (docs/perf/57-mania-cost-per-act.md, 2026-11-04): the giant ring only (one model in the scene, 16-bit positions,
// quads, indexed normals, INK_NONE: every model the game draws in these modes and that fits; anything else is counted in
// g_ps1S3DTodo and not drawn, as before), ~55 -> ~30 hblanks a frame with the same words on screen:
// - each position's screen coordinates once (FROM_FIXED((p << 8) - (camera << 16)) == (p >> 8) - camera), not once a
//   corner;
// - each normal's y and lit colour (as a GPU colour) once, when a drawn face first uses it, not every normal every frame;
// - a solid face as one gouraud quad (PS1BlendedQuadBulk: stock's two triangles), its lines into the bulks (PS1LineFace);
// - no per-face model / item lookups (DrawBlendedFace / DrawLine fallbacks and the A/B knobs gone: they drew nothing
//   the ring needs).
__attribute__((optimize("O2"))) static bool PS1Scene3DOrtho(uint16 sceneID)
{
    Scene3D *scn = &scene3DList[sceneID];
    if (scn->drawMode != S3D_SOLIDCOLOR_SHADED_BLENDED && scn->drawMode != S3D_WIREFRAME_SHADED)
        return false;
    if (!s_ps1Nrm127Ok) {
        for (int32 v = 0; v < 256; ++v) s_ps1Nrm127[v] = (int32)(int8)v * 0x10000 / 127;
        s_ps1Nrm127Ok = true;
    }
    const bool solid = scn->drawMode == S3D_SOLIDCOLOR_SHADED_BLENDED;
    Entity *entity   = sceneInfo.entity;
    int32 item = -1, n = 0;
    for (int32 i = 0; i < g_ps1S3DCount; ++i)
        if (g_ps1S3DItems[i].scene == sceneID)
            item = i, ++n;
    if (n != 1) {
        g_ps1S3DTodo = g_ps1S3DTodo + n;
        return true;
    }
    const PS1S3DItem &it = g_ps1S3DItems[item];
    const Model *mdl     = &modelList[it.model];
    const uint8 *data    = PS1MeshData(mdl);
    const uint8 *nb      = data + mdl->ps1NormOffset;
    const int32 vc = mdl->vertCount, nf = mdl->indexCount >> 2, U = (mdl->flags & PS1_MESH_NRMIDX) ? *(const uint16 *)nb : 0x7FFF;
    uint32 f3dBgr, f3dSemi;
    if (!(mdl->flags & PS1_MESH_POS16) || mdl->faceVertCount != 4 || vc > PS1_S3D_ORTHO || nf > PS1_S3D_ORTHO
        || U > PS1_S3D_ORTHO || entity->inkEffect != INK_NONE || !PS1Face3DBegin(INK_NONE, entity->alpha, 0, &f3dBgr, &f3dSemi)) {
        g_ps1S3DTodo = g_ps1S3DTodo + 1;
        return true;
    }
    // the positions through matWorld (8.8 world pixels: stock's per-term shifts)
    const Matrix &m = it.m;
    const int16 *pos = (const int16 *)PS1MeshPos(mdl); // frame 0
    int32 px[PS1_S3D_ORTHO], py[PS1_S3D_ORTHO], pz[PS1_S3D_ORTHO];
    for (int32 v = 0; v < vc; ++v) {
        int32 x = pos[3 * v], y = pos[3 * v + 1], z = pos[3 * v + 2];
        px[v] = m.values[0][3] + (z * m.values[0][2] >> 8) + (m.values[0][0] * x >> 8) + (m.values[0][1] * y >> 8);
        py[v] = m.values[1][3] + (y * m.values[1][1] >> 8) + (z * m.values[1][2] >> 8) + (m.values[1][0] * x >> 8);
        pz[v] = m.values[2][3] + ((x * m.values[2][0]) >> 8) + ((m.values[2][2] * z >> 8) + (m.values[2][1] * y >> 8));
    }
    // the faces kept (the solid closed ring: those turned to the screen, the first three corners' winding in world pixels,
    // y down), each its depth (stock: the sum of z / 4)
    const uint16 *idx = (const uint16 *)(data + 4 * mdl->indexCount);
    const int32 cullSign = solid && (mdl->flags & PS1_MESH_CLOSED) ? g_ps1S3DOrthoCull : 0;
    int32 depth[PS1_S3D_ORTHO];
    uint8 fIdx[PS1_S3D_ORTHO];
    int32 faces = 0;
    for (int32 f = 0; f < nf; ++f) {
        const uint16 *c = idx + 4 * f;
        const int32 c0 = c[0], c1 = c[1], c2 = c[2];
        if (cullSign) {
            const int64 cross = (int64)(px[c1] - px[c0]) * (py[c2] - py[c0]) - (int64)(px[c2] - px[c0]) * (py[c1] - py[c0]);
            if (cullSign > 0 ? cross < 0 : cross > 0)
                continue;
        }
        depth[faces] = (pz[c0] >> 2) + (pz[c1] >> 2) + (pz[c2] >> 2) + (pz[c[3]] >> 2);
        fIdx[faces++] = (uint8)f;
    }
    // far first (stock's insertion sort: equal depths keep their order). 9o: as one key a face, depth << 8 | 255 - its
    // place in the list: a total order whose descending sort is stock's result whatever order it starts from, so it
    // starts from the previous frame's (the ring turns slowly: nearly sorted, a few moves instead of ~1,600); the plain
    // sort when a depth leaves the key's 23 bits
    static uint8 s_prevOrder[PS1_S3D_ORTHO];
    static int32 s_prevFaces = -1;
    static uint16 s_prevScene = 0xFFFF;
    bool keyed = true;
    for (int32 i = 0; i < faces && keyed; ++i) keyed = depth[i] >= -(1 << 22) && depth[i] < (1 << 22);
    uint8 order[PS1_S3D_ORTHO];
    if (keyed) {
        uint32 key[PS1_S3D_ORTHO];
        const bool seed = s_prevFaces == faces && s_prevScene == sceneID;
        for (int32 i = 0; i < faces; ++i) {
            const int32 k = seed ? s_prevOrder[i] : i;
            key[i]        = (uint32)(depth[k] + (1 << 22)) << 8 | (uint32)(255 - k);
        }
        for (int32 i = 1; i < faces; ++i) {
            const uint32 v = key[i];
            int32 j        = i - 1;
            for (; j >= 0 && key[j] < v; --j) key[j + 1] = key[j];
            key[j + 1] = v;
        }
        for (int32 i = 0; i < faces; ++i) {
            const int32 k = 255 - (int32)(key[i] & 0xFF);
            order[i] = fIdx[k], s_prevOrder[i] = (uint8)k;
        }
        s_prevFaces = faces, s_prevScene = sceneID;
    }
    else {
        s_prevFaces = -1;
        for (int32 i = 1; i < faces; ++i) {
            int32 d = depth[i], j = i - 1;
            uint8 f = fIdx[i];
            for (; j >= 0 && depth[j] < d; --j) depth[j + 1] = depth[j], fIdx[j + 1] = fIdx[j];
            depth[j + 1] = d, fIdx[j + 1] = f;
        }
        for (int32 i = 0; i < faces; ++i) order[i] = fIdx[i];
    }
    // each normal's y through matNormals (LoadMesh keeps normals x 0x10000), then (solid) its lit colour, on first use
    const uint8 *cidx = nb + 4 + 4 * U; // a normal index a corner
    int32 nyU[PS1_S3D_ORTHO];
    uint32 bgrU[PS1_S3D_ORTHO];
    uint8 have[PS1_S3D_ORTHO]; // bit 0: nyU, bit 1: bgrU
    for (int32 u = 0; u < U; ++u) have[u] = 0;
    const int32 n0 = it.n1[0], n1 = it.n1[1], n2 = it.n1[2];
    auto normalY = [&](int32 u) -> int32 {
        if (!(have[u] & 1)) {
            const int8 *nr = (const int8 *)(nb + 4 + 4 * u);
            int32 nx = s_ps1Nrm127[(uint8)nr[0]], ny = s_ps1Nrm127[(uint8)nr[1]], nz = s_ps1Nrm127[(uint8)nr[2]];
            nyU[u] = (ny * n1 >> 8) + (nz * n2 >> 8) + (nx * n0 >> 8);
            have[u] |= 1;
        }
        return nyU[u];
    };
    const int32 camX = currentScreen->position.x, camY = currentScreen->position.y;
    if (solid) {
        for (int32 v = 0; v < vc; ++v) px[v] = (int32)((uint32)(uint16)((py[v] >> 8) - camY) << 16 | (uint16)((px[v] >> 8) - camX));
        for (int32 i = 0; i < faces; ++i) {
            const int32 f   = order[i];
            const uint16 *c = idx + 4 * f;
            const uint8 *ci = cidx + 4 * f;
            uint32 xy[4], bgr[4];
            for (int32 v = 0; v < 4; ++v) {
                const int32 u = ci[v];
                if (!(have[u] & 2)) {
                    const uint32 col = PS1LitColour(scn, normalY(u), it.colour);
                    bgrU[u] = (col & 0xFF) << 16 | (col & 0xFF00) | ((col >> 16) & 0xFF), have[u] |= 2;
                }
                bgr[v] = bgrU[u], xy[v] = (uint32)px[c[v]];
            }
            PS1BlendedQuadBulk(xy, bgr);
        }
    }
    else { // each face's edges in the colour of its corners' mean normal y
        for (int32 v = 0; v < vc; ++v) px[v] = (px[v] >> 8) - camX, py[v] = (py[v] >> 8) - camY;
        for (int32 i = 0; i < faces; ++i) {
            const int32 f   = order[i];
            const uint8 *ci = cidx + 4 * f;
            const int32 ny  = normalY(ci[0]) + normalY(ci[1]) + normalY(ci[2]) + normalY(ci[3]);
            const uint32 colour = PS1LitColour(scn, ny / 4, it.colour);
            PS1LineFace(px, py, idx + 4 * f, 4, (colour & 0xFF) << 16 | (colour & 0xFF00) | ((colour >> 16) & 0xFF));
        }
    }
    PS1Face3DEnd();
    g_ps1S3DDraws = g_ps1S3DDraws + 1;
    g_ps1S3DFaces = g_ps1S3DFaces + faces;
    return true;
}
#endif

void RSDK::Draw3DScene(uint16 sceneID)
{
#if RETRO_PLATFORM == RETRO_PS1
    if (sceneID < SCENE3D_COUNT && PS1Scene3DOrtho(sceneID)) // 7.4: the special ring's modes (no projection)
        return;
    if (g_ps1S3DOps.draw) // docs/ports/mania/32 7.3: on the GTE (the draw is linked into the overlays of the folders that need it)
        g_ps1S3DOps.draw(sceneID);
    else
        g_ps1S3DTodo = g_ps1S3DTodo + 1;
    return;
#endif
    if (sceneID < SCENE3D_COUNT) {
        Entity *entity = sceneInfo.entity;
        Scene3D *scn   = &scene3DList[sceneID];

        // Setup face buffer.
        // Each face's depth is an average of the depth of its vertices.
        Scene3DVertex *vertices = scn->vertices;
        Scene3DFace *faceBuffer = scn->faceBuffer;
        uint8 *faceVertCounts = scn->faceVertCounts;

        int32 vertIndex = 0;
        for (int32 i = 0; i < scn->faceCount; ++i) {
            switch (*faceVertCounts) {
                default:
                case 1:
                    faceBuffer->depth = vertices[0].z;
                    vertices += *faceVertCounts;
                    break;

                case 2:
                    faceBuffer->depth = vertices[0].z >> 1;
                    faceBuffer->depth += vertices[1].z >> 1;
                    vertices += 2;
                    break;

                case 3:
                    faceBuffer->depth = vertices[0].z >> 1;
                    faceBuffer->depth = (faceBuffer->depth + (vertices[1].z >> 1)) >> 1;
                    faceBuffer->depth += vertices[2].z >> 1;
                    vertices += 3;
                    break;

                case 4:
                    faceBuffer->depth = vertices[0].z >> 2;
                    faceBuffer->depth += vertices[1].z >> 2;
                    faceBuffer->depth += vertices[2].z >> 2;
                    faceBuffer->depth += vertices[3].z >> 2;
                    vertices += 4;
                    break;
            }

            faceBuffer->index = vertIndex;
            vertIndex += *faceVertCounts;

            ++faceBuffer;
            ++faceVertCounts;
        }

        // Sort the face buffer. This is needed so that the faces don't overlap each other incorrectly when they're rendered.
        // This is an insertion sort, taken from here:
        // https://web.archive.org/web/20110108233032/http://rosettacode.org/wiki/Sorting_algorithms/Insertion_sort#C

        Scene3DFace *a = scn->faceBuffer;

        int i, j;
        Scene3DFace temp;

        for(i=1; i<scn->faceCount; i++)
        {
            temp = a[i];
            j = i-1;
            while(j>=0 && a[j].depth < temp.depth)
            {
                a[j+1] = a[j];
                j -= 1;
            }
            a[j+1] = temp;
        }

        // Finally, display the faces.

        uint8 *vertCnt = scn->faceVertCounts;
        Vector2 vertPos[4];
        uint32 vertClrs[4];

        switch (scn->drawMode) {
            default: break;

            case S3D_WIREFRAME:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    for (int32 v = 0; v < *vertCnt - 1; ++v) {
                        DrawLine(drawVert[v + 0].x << 8, drawVert[v + 0].y << 8, drawVert[v + 1].x << 8, drawVert[v + 1].y << 8, drawVert[0].color,
                                 entity->alpha, entity->inkEffect, false);
                    }
                    DrawLine(drawVert[0].x << 8, drawVert[0].y << 8, drawVert[*vertCnt - 1].x << 8, drawVert[*vertCnt - 1].y << 8, drawVert[0].color,
                             entity->alpha, entity->inkEffect, false);
                    vertCnt++;
                }
                break;

            case S3D_SOLIDCOLOR:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    for (int32 v = 0; v < *vertCnt; ++v) {
                        vertPos[v].x = (drawVert[v].x << 8) - (currentScreen->position.x << 16);
                        vertPos[v].y = (drawVert[v].y << 8) - (currentScreen->position.y << 16);
                    }
                    DrawFace(vertPos, *vertCnt, (drawVert->color >> 16) & 0xFF, (drawVert->color >> 8) & 0xFF, (drawVert->color >> 0) & 0xFF,
                             entity->alpha, entity->inkEffect);
                    vertCnt++;
                }
                break;

            // Might have been reserved for textures?
            // not sure about this, just a guess based on tex coords existing in the model format spec
            case S3D_UNUSED_1: break;
            case S3D_UNUSED_2: break;

            case S3D_WIREFRAME_SHADED:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    int32 vertCount         = *vertCnt;

                    int32 ny1 = 0;
                    for (int32 v = 0; v < vertCount; ++v) {
                        ny1 += drawVert[v].ny;
                    }

                    int32 normal    = ny1 / vertCount;
                    int32 normalVal = (normal >> 2) * (abs(normal) >> 2);

                    int32 specular = normalVal >> 6 >> scn->specularIntensityX;
                    specular       = CLAMP(specular, 0x00, 0xFF);
                    int32 r = specular + ((int32)((drawVert->color >> 16) & 0xFF) * ((normal >> 10) + scn->diffuseX) >> scn->diffuseIntensityX);

                    specular = normalVal >> 6 >> scn->specularIntensityY;
                    specular = CLAMP(specular, 0x00, 0xFF);
                    int32 g  = specular + ((int32)((drawVert->color >> 8) & 0xFF) * ((normal >> 10) + scn->diffuseY) >> scn->diffuseIntensityY);

                    specular = normalVal >> 6 >> scn->specularIntensityZ;
                    specular = CLAMP(specular, 0x00, 0xFF);
                    int32 b  = specular + ((int32)((drawVert->color >> 0) & 0xFF) * ((normal >> 10) + scn->diffuseZ) >> scn->diffuseIntensityZ);

                    r = CLAMP(r, 0x00, 0xFF);
                    g = CLAMP(g, 0x00, 0xFF);
                    b = CLAMP(b, 0x00, 0xFF);

                    uint32 color = (r << 16) | (g << 8) | (b << 0);

                    for (int32 v = 0; v < vertCount - 1; ++v) {
                        DrawLine(drawVert[v + 0].x << 8, drawVert[v + 0].y << 8, drawVert[v + 1].x << 8, drawVert[v + 1].y << 8, color, entity->alpha,
                                 entity->inkEffect, false);
                    }
                    DrawLine(drawVert[vertCount - 1].x << 8, drawVert[vertCount - 1].y << 8, drawVert[0].x << 8, drawVert[0].y << 8, color,
                             entity->alpha, entity->inkEffect, false);

                    vertCnt++;
                }
                break;

            case S3D_SOLIDCOLOR_SHADED:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    int32 vertCount         = *vertCnt;

                    int32 ny = 0;
                    for (int32 v = 0; v < vertCount; ++v) {
                        ny += drawVert[v].ny;
                        vertPos[v].x = (drawVert[v].x << 8) - (currentScreen->position.x << 16);
                        vertPos[v].y = (drawVert[v].y << 8) - (currentScreen->position.y << 16);
                    }

                    int32 normal    = ny / vertCount;
                    int32 normalVal = (normal >> 2) * (abs(normal) >> 2);

                    int32 specular = normalVal >> 6 >> scn->specularIntensityX;
                    specular       = CLAMP(specular, 0x00, 0xFF);
                    int32 r = specular + ((int32)((drawVert->color >> 16) & 0xFF) * ((normal >> 10) + scn->diffuseX) >> scn->diffuseIntensityX);

                    specular = normalVal >> 6 >> scn->specularIntensityY;
                    specular = CLAMP(specular, 0x00, 0xFF);
                    int32 g  = specular + ((int32)((drawVert->color >> 8) & 0xFF) * ((normal >> 10) + scn->diffuseY) >> scn->diffuseIntensityY);

                    specular = normalVal >> 6 >> scn->specularIntensityZ;
                    specular = CLAMP(specular, 0x00, 0xFF);
                    int32 b  = specular + ((int32)((drawVert->color >> 0) & 0xFF) * ((normal >> 10) + scn->diffuseZ) >> scn->diffuseIntensityZ);

                    r = CLAMP(r, 0x00, 0xFF);
                    g = CLAMP(g, 0x00, 0xFF);
                    b = CLAMP(b, 0x00, 0xFF);

                    uint32 color = (r << 16) | (g << 8) | (b << 0);

                    drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    DrawFace(vertPos, *vertCnt, (color >> 16) & 0xFF, (color >> 8) & 0xFF, (color >> 0) & 0xFF, entity->alpha, entity->inkEffect);

                    vertCnt++;
                }
                break;

            case S3D_SOLIDCOLOR_SHADED_BLENDED:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    int32 vertCount         = *vertCnt;

                    for (int32 v = 0; v < vertCount; ++v) {
                        vertPos[v].x = (drawVert[v].x << 8) - (currentScreen->position.x << 16);
                        vertPos[v].y = (drawVert[v].y << 8) - (currentScreen->position.y << 16);

                        int32 normal    = drawVert[v].ny;
                        int32 normalVal = (normal >> 2) * (abs(normal) >> 2);

                        int32 specular = (normalVal >> 6) >> scn->specularIntensityX;
                        specular       = CLAMP(specular, 0x00, 0xFF);
                        int32 r = specular + ((int32)((drawVert->color >> 16) & 0xFF) * ((normal >> 10) + scn->diffuseX) >> scn->diffuseIntensityX);

                        specular = (normalVal >> 6) >> scn->specularIntensityY;
                        specular = CLAMP(specular, 0x00, 0xFF);
                        int32 g  = specular + ((int32)((drawVert->color >> 8) & 0xFF) * ((normal >> 10) + scn->diffuseY) >> scn->diffuseIntensityY);

                        specular = (normalVal >> 6) >> scn->specularIntensityZ;
                        specular = CLAMP(specular, 0x00, 0xFF);
                        int32 b  = specular + ((int32)((drawVert->color >> 0) & 0xFF) * ((normal >> 10) + scn->diffuseZ) >> scn->diffuseIntensityZ);

                        r = CLAMP(r, 0x00, 0xFF);
                        g = CLAMP(g, 0x00, 0xFF);
                        b = CLAMP(b, 0x00, 0xFF);

                        vertClrs[v] = (r << 16) | (g << 8) | (b << 0);
                    }

                    DrawBlendedFace(vertPos, vertClrs, *vertCnt, entity->alpha, entity->inkEffect);

                    vertCnt++;
                }
                break;

            case S3D_WIREFRAME_SCREEN:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];

                    int32 v = 0;
                    for (; v < *vertCnt && v < 0xFF; ++v) {
                        int32 vertZ = drawVert[v].z;
                        if (vertZ < 0x100) {
                            v = 0xFF;
                        }
                        else {
                            vertPos[v].x = currentScreen->center.x + (drawVert[v].x << scn->projectionX) / vertZ;
                            vertPos[v].y = currentScreen->center.y - (drawVert[v].y << scn->projectionY) / vertZ;
                        }
                    }

                    if (v < 0xFF) {
                        for (int32 v = 0; v < *vertCnt - 1; ++v) {
                            DrawLine(vertPos[v + 0].x, vertPos[v + 0].y, vertPos[v + 1].x, vertPos[v + 1].y, drawVert[0].color, entity->alpha,
                                     entity->inkEffect, true);
                        }
                        DrawLine(vertPos[0].x, vertPos[0].y, vertPos[*vertCnt - 1].x, vertPos[*vertCnt - 1].y, drawVert[0].color, entity->alpha,
                                 entity->inkEffect, true);
                    }

                    vertCnt++;
                }
                break;

            case S3D_SOLIDCOLOR_SCREEN:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    int32 vertCount         = *vertCnt;

                    int32 v = 0;
                    for (; v < vertCount && v < 0xFF; ++v) {
                        int32 vertZ = drawVert[v].z;
                        if (vertZ < 0x100) {
                            v = 0xFF;
                        }
                        else {
                            vertPos[v].x = (currentScreen->center.x << 16) + ((drawVert[v].x << scn->projectionX) / vertZ << 16);
                            vertPos[v].y = (currentScreen->center.y << 16) - ((drawVert[v].y << scn->projectionY) / vertZ << 16);
                        }
                    }

                    if (v < 0xFF) {
                        DrawFace(vertPos, *vertCnt, (drawVert[0].color >> 16) & 0xFF, (drawVert[0].color >> 8) & 0xFF,
                                 (drawVert[0].color >> 0) & 0xFF, entity->alpha, entity->inkEffect);
                    }
                    vertCnt++;
                }
                break;

            case S3D_WIREFRAME_SHADED_SCREEN:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    int32 vertCount         = *vertCnt;

                    int32 v   = 0;
                    int32 ny1 = 0;
                    for (; v < *vertCnt && v < 0xFF; ++v) {
                        int32 vertZ = drawVert[v].z;
                        if (vertZ < 0x100) {
                            v = 0xFF;
                        }
                        else {
                            vertPos[v].x = currentScreen->center.x + (drawVert[v].x << scn->projectionX) / vertZ;
                            vertPos[v].y = currentScreen->center.y - (drawVert[v].y << scn->projectionY) / vertZ;
                            ny1 += drawVert[v].ny;
                        }
                    }

                    if (v < 0xFF) {
                        int32 normal    = ny1 / vertCount;
                        int32 normalVal = (normal >> 2) * (abs(normal) >> 2);

                        int32 specular = normalVal >> 6 >> scn->specularIntensityX;
                        specular       = CLAMP(specular, 0x00, 0xFF);
                        int32 r = specular + ((int32)((drawVert[0].color >> 16) & 0xFF) * ((normal >> 10) + scn->diffuseX) >> scn->diffuseIntensityX);

                        specular = normalVal >> 6 >> scn->specularIntensityY;
                        specular = CLAMP(specular, 0x00, 0xFF);
                        int32 g  = specular + ((int32)((drawVert[0].color >> 8) & 0xFF) * ((normal >> 10) + scn->diffuseY) >> scn->diffuseIntensityY);

                        specular = normalVal >> 6 >> scn->specularIntensityZ;
                        specular = CLAMP(specular, 0x00, 0xFF);
                        int32 b  = specular + ((int32)((drawVert[0].color >> 0) & 0xFF) * ((normal >> 10) + scn->diffuseZ) >> scn->diffuseIntensityZ);

                        r = CLAMP(r, 0x00, 0xFF);
                        g = CLAMP(g, 0x00, 0xFF);
                        b = CLAMP(b, 0x00, 0xFF);

                        uint32 color = (r << 16) | (g << 8) | (b << 0);

                        for (int32 v = 0; v < *vertCnt - 1; ++v) {
                            DrawLine(vertPos[v + 0].x, vertPos[v + 0].y, vertPos[v + 1].x, vertPos[v + 1].y, color, entity->alpha, entity->inkEffect,
                                     true);
                        }
                        DrawLine(vertPos[*vertCnt - 1].x, vertPos[*vertCnt - 1].y, vertPos[0].x, vertPos[0].y, color, entity->alpha,
                                 entity->inkEffect, true);
                    }

                    vertCnt++;
                }
                break;

            case S3D_SOLIDCOLOR_SHADED_SCREEN:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    int32 vertCount         = *vertCnt;

                    int32 v  = 0;
                    int32 ny = 0;
                    for (; v < vertCount && v < 0xFF; ++v) {
                        int32 vertZ = drawVert[v].z;
                        if (vertZ < 0x100) {
                            v = 0xFF;
                        }
                        else {
                            vertPos[v].x = (currentScreen->center.x << 16) + ((drawVert[v].x << scn->projectionX) / vertZ << 16);
                            vertPos[v].y = (currentScreen->center.y << 16) - ((drawVert[v].y << scn->projectionY) / vertZ << 16);
                            ny += drawVert[v].ny;
                        }
                    }

                    if (v < 0xFF) {
                        int32 normal    = ny / vertCount;
                        int32 normalVal = (normal >> 2) * (abs(normal) >> 2);

                        int32 specular = normalVal >> 6 >> scn->specularIntensityX;
                        specular       = CLAMP(specular, 0x00, 0xFF);
                        int32 r = specular + ((int32)((drawVert[0].color >> 16) & 0xFF) * ((normal >> 10) + scn->diffuseX) >> scn->diffuseIntensityX);

                        specular = normalVal >> 6 >> scn->specularIntensityY;
                        specular = CLAMP(specular, 0x00, 0xFF);
                        int32 g  = specular + ((int32)((drawVert[0].color >> 8) & 0xFF) * ((normal >> 10) + scn->diffuseY) >> scn->diffuseIntensityY);

                        specular = normalVal >> 6 >> scn->specularIntensityZ;
                        specular = CLAMP(specular, 0x00, 0xFF);
                        int32 b  = specular + ((int32)((drawVert[0].color >> 0) & 0xFF) * ((normal >> 10) + scn->diffuseZ) >> scn->diffuseIntensityZ);

                        r = CLAMP(r, 0x00, 0xFF);
                        g = CLAMP(g, 0x00, 0xFF);
                        b = CLAMP(b, 0x00, 0xFF);

                        uint32 color = (r << 16) | (g << 8) | (b << 0);

                        drawVert = &scn->vertices[scn->faceBuffer[f].index];
                        DrawFace(vertPos, *vertCnt, (color >> 16) & 0xFF, (color >> 8) & 0xFF, (color >> 0) & 0xFF, entity->alpha, entity->inkEffect);
                    }

                    vertCnt++;
                }
                break;

            case S3D_SOLIDCOLOR_SHADED_BLENDED_SCREEN:
                for (int32 f = 0; f < scn->faceCount; ++f) {
                    Scene3DVertex *drawVert = &scn->vertices[scn->faceBuffer[f].index];
                    int32 vertCount         = *vertCnt;

                    int32 v = 0;
                    for (; v < vertCount && v < 0xFF; ++v) {
                        int32 vertZ = drawVert[v].z;
                        if (vertZ < 0x100) {
                            v = 0xFF;
                        }
                        else {
                            vertPos[v].x = (currentScreen->center.x << 16) + ((drawVert[v].x << scn->projectionX) / vertZ << 16);
                            vertPos[v].y = (currentScreen->center.y << 16) - ((drawVert[v].y << scn->projectionY) / vertZ << 16);

                            int32 normal    = drawVert[v].ny;
                            int32 normalVal = (normal >> 2) * (abs(normal) >> 2);

                            int32 specular = normalVal >> 6 >> scn->specularIntensityX;
                            specular       = CLAMP(specular, 0x00, 0xFF);
                            int32 r =
                                specular + ((int32)((drawVert[v].color >> 16) & 0xFF) * ((normal >> 10) + scn->diffuseX) >> scn->diffuseIntensityX);

                            specular = normalVal >> 6 >> scn->specularIntensityY;
                            specular = CLAMP(specular, 0x00, 0xFF);
                            int32 g =
                                specular + ((int32)((drawVert[v].color >> 8) & 0xFF) * ((normal >> 10) + scn->diffuseY) >> scn->diffuseIntensityY);

                            specular = normalVal >> 6 >> scn->specularIntensityZ;
                            specular = CLAMP(specular, 0x00, 0xFF);
                            int32 b =
                                specular + ((int32)((drawVert[v].color >> 0) & 0xFF) * ((normal >> 10) + scn->diffuseZ) >> scn->diffuseIntensityZ);

                            r = CLAMP(r, 0x00, 0xFF);
                            g = CLAMP(g, 0x00, 0xFF);
                            b = CLAMP(b, 0x00, 0xFF);

                            vertClrs[v] = (r << 16) | (g << 8) | (b << 0);
                        }
                    }

                    if (v < 0xFF) {
                        drawVert = &scn->vertices[scn->faceBuffer[f].index];
                        DrawBlendedFace(vertPos, vertClrs, *vertCnt, entity->alpha, entity->inkEffect);
                    }

                    vertCnt++;
                }
                break;
        }
    }
}
