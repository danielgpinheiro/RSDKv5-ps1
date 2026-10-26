// docs/ports/mania/32 9u: RSDK::MatrixInverse, the PS1's table-driven version against stock's unrolled one (RSDKv5/RSDK/Graphics/Scene3D.cpp,
// both pasted here 2026-10-20), bit for bit on random matrices (c++ -O2 -ffp-contract=off: no fused
// multiply-adds, as the PS1's soft doubles). Exit 0 = identical.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
typedef int32_t int32; typedef uint16_t uint16;
struct Matrix { int32 values[4][4]; };
namespace S { void MatrixInverse(Matrix *dest, Matrix *matrix)
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
 }
namespace N { static const uint16 ps1InvTerms[96] = { 0x0FA5, 0x1EB5, 0x1F69, 0x0E79, 0x0B6D, 0x1A7D, 0x1FA1, 0x0EB1, 0x0F29, 0x1E39, 0x1B2D, 0x0A3D, 0x0F61, 0x1E71, 0x1F25, 0x0E35, 0x072D, 0x163D, 0x1B61, 0x0A71, 0x0B25, 0x1A35, 0x1729, 0x0639, 0x1FA4, 0x0EB4, 0x0F68, 0x1E78, 0x1B6C, 0x0A7C, 0x0FA0, 0x1EB0, 0x1F28, 0x0E38, 0x0B2C, 0x1A3C, 0x1F60, 0x0E70, 0x0F24, 0x1E34, 0x172C, 0x063C, 0x0B60, 0x1A70, 0x1B24, 0x0A34, 0x0728, 0x1638, 0x0F94, 0x1DB4, 0x1F58, 0x0D78, 0x0B5C, 0x197C, 0x1F90, 0x0DB0, 0x0F18, 0x1D38, 0x1B1C, 0x093C, 0x0F50, 0x1D70, 0x1F14, 0x0D34, 0x071C, 0x153C, 0x1B50, 0x0970, 0x0B14, 0x1934, 0x1718, 0x0538, 0x1E94, 0x0DA4, 0x0E58, 0x1D68, 0x1A5C, 0x096C, 0x0E90, 0x1DA0, 0x1E18, 0x0D28, 0x0A1C, 0x192C, 0x1E50, 0x0D60, 0x0E14, 0x1D24, 0x161C, 0x052C, 0x0A50, 0x1960, 0x1A14, 0x0924, 0x0618, 0x1528 }; void MatrixInverse(Matrix *dest, Matrix *matrix)
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
 }
static uint32_t rng = 12345;
static uint32_t nx() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 4000000, bad = 0, zero = 0;
    for (long i = 0; i < n; ++i) {
        Matrix m, a, b;
        int mode = i % 4;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            uint32_t r = nx();
            int32 v = mode == 0 ? (int32)(r % 513) - 256 : mode == 1 ? (int32)(r % 0x20001) - 0x10000 : mode == 2 ? (int32)r >> (r & 15) : (int32)(r % 3) - 1;
            m.values[y][x] = v;
        }
        memset(&a, 0x55, sizeof a); memset(&b, 0x55, sizeof b);
        S::MatrixInverse(&a, &m); N::MatrixInverse(&b, &m);
        if (memcmp(&a, &b, sizeof a)) { if (bad++ < 3) printf("differ at %ld\n", i); }
        int32 c = 0; for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) c |= a.values[y][x] != 0x55555555;
        zero += !c;
    }
    printf("%ld matrices, %ld differ, %ld singular (left unchanged by both)\n", n, bad, zero);
    return bad != 0;
}
