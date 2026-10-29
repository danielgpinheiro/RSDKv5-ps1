/*
 * Scene3D's projection on the GTE — see gte3d.hh.
 */
#include "gte3d.hh"

#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"

volatile uint32_t g_ps1GteVertices = 0;
volatile uint32_t g_ps1GteFlagged  = 0;
volatile uint32_t g_ps1GteCamLeft = 0; // ps1-ufo: PS1GteCamSpace calls left to the caller (total)

namespace {
inline uint32_t pack(int lo, int hi) { return (uint32_t)(uint16_t)lo | ((uint32_t)(uint16_t)hi << 16); }
}

namespace {
uint32_t s_rotGen = 0; // PS1Gte3DSetup calls (the rotation matrix changed)
}

void PS1Gte3DSetup(const int32_t r[9], int32_t trx, int32_t trY, int32_t trz, uint32_t h, int32_t ofx, int32_t ofy) {
    using namespace psyqo::GTE;
    ++s_rotGen;
    write<Register::R11R12, Unsafe>(pack(r[0], r[1]));
    write<Register::R13R21, Unsafe>(pack(r[2], r[3]));
    write<Register::R22R23, Unsafe>(pack(r[4], r[5]));
    write<Register::R31R32, Unsafe>(pack(r[6], r[7]));
    write<Register::R33, Unsafe>((uint32_t)(uint16_t)r[8]);
    write<Register::TRX, Unsafe>((uint32_t)trx);
    write<Register::TRY, Unsafe>((uint32_t)trY);
    write<Register::TRZ, Unsafe>((uint32_t)trz);
    write<Register::OFX, Unsafe>((uint32_t)ofx);
    write<Register::OFY, Unsafe>((uint32_t)ofy);
    write<Register::H, Safe>(h);
}

namespace {
// 9f: a projected vertex's outcode (PS1Scene3D.cpp's face test): bits 0-3 off the screen's left / right / top / bottom,
// 4 nearer than zmin (a flagged vertex's depth is 0), 5 outside the band where every face fits the GPU's limits
inline uint8_t outcode(uint32_t a, uint32_t z, uint32_t zmin, int32_t sw, int32_t sh) {
    const int32_t x = (int16_t)a, y = (int32_t)a >> 16;
    uint32_t oc = (x < 0) | (x >= sw) << 1 | (y < 0) << 2 | (y >= sh) << 3 | (z < zmin) << 4;
    return (uint8_t)(oc | ((uint32_t)(x + 352) > 1023u || (uint32_t)(y + 136) > 511u) << 5);
}

// OC: 0 none, 1 the full outcode, 2 the near bit only (9f, UFO item 5b: a model wholly on screen, where the face loops
// read no other bit)
template <int OC>
__attribute__((always_inline)) inline int project(const int16_t *xyz, int count, uint32_t *sxy, uint16_t *sz, uint8_t *oc,
                                                  uint32_t zmin, int32_t sw, int32_t sh) {
    using namespace psyqo::GTE;
    int flagged = 0, v = 0;
    for (; v + 3 <= count; v += 3, xyz += 9) {
        write<Register::VXY0, Unsafe>(pack(xyz[0], xyz[1]));
        write<Register::VZ0, Unsafe>((uint32_t)xyz[2]);
        write<Register::VXY1, Unsafe>(pack(xyz[3], xyz[4]));
        write<Register::VZ1, Unsafe>((uint32_t)xyz[5]);
        write<Register::VXY2, Unsafe>(pack(xyz[6], xyz[7]));
        write<Register::VZ2, Safe>((uint32_t)xyz[8]);
        Kernels::rtpt();
        int32_t flag = (int32_t)readRaw<Register::FLAG, Safe>();
        if (flag >= 0) {
            // (9f: kept in locals: the outcodes no longer reload them from the arrays they were just stored to)
            const uint32_t a0 = readRaw<Register::SXY0, Safe>(), a1 = readRaw<Register::SXY1, Safe>();
            const uint32_t a2 = readRaw<Register::SXY2, Safe>();
            const uint32_t z0 = (uint16_t)readRaw<Register::SZ1, Safe>(), z1 = (uint16_t)readRaw<Register::SZ2, Safe>();
            const uint32_t z2 = (uint16_t)readRaw<Register::SZ3, Safe>();
            sxy[v] = a0, sxy[v + 1] = a1, sxy[v + 2] = a2;
            sz[v] = (uint16_t)z0, sz[v + 1] = (uint16_t)z1, sz[v + 2] = (uint16_t)z2;
            if (OC) {
                oc[v]     = OC == 2 ? (uint8_t)((z0 < zmin) << 4) : outcode(a0, z0, zmin, sw, sh);
                oc[v + 1] = OC == 2 ? (uint8_t)((z1 < zmin) << 4) : outcode(a1, z1, zmin, sw, sh);
                oc[v + 2] = OC == 2 ? (uint8_t)((z2 < zmin) << 4) : outcode(a2, z2, zmin, sw, sh);
            }
            continue;
        }
        // one vertex at a time (RTPS): only the flagged ones lose their depth
        for (int k = 0; k < 3; ++k) {
            write<Register::VXY0, Unsafe>(pack(xyz[3 * k], xyz[3 * k + 1]));
            write<Register::VZ0, Safe>((uint32_t)xyz[3 * k + 2]);
            Kernels::rtps();
            int32_t fl  = (int32_t)readRaw<Register::FLAG, Safe>();
            sxy[v + k] = readRaw<Register::SXY2, Safe>();
            sz[v + k]  = fl < 0 ? 0 : (uint16_t)readRaw<Register::SZ3, Safe>();
            if (OC)
                oc[v + k] = OC == 2 ? (uint8_t)((sz[v + k] < zmin) << 4) : outcode(sxy[v + k], sz[v + k], zmin, sw, sh);
            flagged += fl < 0;
        }
    }
    for (; v < count; ++v, xyz += 3) {
        write<Register::VXY0, Unsafe>(pack(xyz[0], xyz[1]));
        write<Register::VZ0, Safe>((uint32_t)xyz[2]);
        Kernels::rtps();
        int32_t fl = (int32_t)readRaw<Register::FLAG, Safe>();
        sxy[v]     = readRaw<Register::SXY2, Safe>();
        sz[v]      = fl < 0 ? 0 : (uint16_t)readRaw<Register::SZ3, Safe>();
        if (OC)
            oc[v] = OC == 2 ? (uint8_t)((sz[v] < zmin) << 4) : outcode(sxy[v], sz[v], zmin, sw, sh);
        flagged += fl < 0;
    }
    g_ps1GteVertices = g_ps1GteVertices + count;
    g_ps1GteFlagged  = g_ps1GteFlagged + flagged;
    return flagged;
}
} // namespace

namespace {
uint32_t s_camRot = 0xFFFFFFFF; // the GTE rotation the camera's matrix was loaded as
const int32_t (*s_camM)[4] = nullptr;
int32_t s_camT[3];
bool s_camOk = false;
}

static void PS1GteCamPut() {
    using namespace psyqo::GTE;
    const int32_t (*m)[4] = s_camM;
    write<Register::R11R12, Unsafe>(pack(m[0][0], m[0][1]));
    write<Register::R13R21, Unsafe>(pack(m[0][2], m[1][0]));
    write<Register::R22R23, Unsafe>(pack(m[1][1], m[1][2]));
    write<Register::R31R32, Unsafe>(pack(m[2][0], m[2][1]));
    write<Register::R33, Safe>((uint32_t)(uint16_t)m[2][2]);
    s_camRot = ++s_rotGen; // (anyone else's matrix is gone: their next PS1Gte3DSetup reloads it)
}

void PS1GteCamLoad(const int32_t (*m)[4]) {
    s_camM  = m;
    s_camOk = m != nullptr;
    for (int r = 0; r < 3 && s_camOk; ++r)
        for (int c = 0; c < 3; ++c) s_camOk = s_camOk && m[r][c] >= -256 && m[r][c] <= 256;
    if (s_camOk) {
        s_camT[0] = m[0][3], s_camT[1] = m[1][3], s_camT[2] = m[2][3];
        PS1GteCamPut();
    }
}

int PS1GteCamSpace(int32_t x, int32_t y, int32_t z, int32_t *out) {
    using namespace psyqo::GTE;
    // whole pixels within +-16383 (on the 16.16 values: the low half 0, x + (16383 << 16) in [0, 32766 << 16])
    if (((x | y | z) & 0xFFFF) || (uint32_t)(x + 0x3FFF0000) > 0x7FFE0000u || (uint32_t)(y + 0x3FFF0000) > 0x7FFE0000u
        || (uint32_t)(z + 0x3FFF0000) > 0x7FFE0000u || !s_camOk) {
        g_ps1GteCamLeft = g_ps1GteCamLeft + 1;
        return 0;
    }
    if (s_camRot != s_rotGen) // another GTE user since the camera's load
        PS1GteCamPut();
    write<Register::VXY0, Unsafe>((uint32_t)x >> 16 | ((uint32_t)y & 0xFFFF0000u));
    write<Register::VZ0, Safe>((uint32_t)(z >> 16));
    Kernels::mvmva<Kernels::MX::RT, Kernels::MV::V0, Kernels::TV::Zero, Kernels::SF::Unshifted>();
    out[0] = s_camT[0] + (int32_t)readRaw<Register::MAC1, Safe>();
    out[1] = s_camT[1] + (int32_t)readRaw<Register::MAC2, Safe>();
    out[2] = s_camT[2] + (int32_t)readRaw<Register::MAC3, Safe>();
    return 1;
}

int PS1Gte3DProject(const int16_t *xyz, int count, uint32_t *sxy, uint16_t *sz) {
    return project<0>(xyz, count, sxy, sz, nullptr, 0, 0, 0);
}
int PS1Gte3DProjectOc(const int16_t *xyz, int count, uint32_t *sxy, uint16_t *sz, uint8_t *oc, uint32_t zmin, int32_t sw,
                      int32_t sh) {
    return project<1>(xyz, count, sxy, sz, oc, zmin, sw, sh);
}
int PS1Gte3DProjectNear(const int16_t *xyz, int count, uint32_t *sxy, uint16_t *sz, uint8_t *oc, uint32_t zmin, int32_t sw,
                        int32_t sh) {
    return project<2>(xyz, count, sxy, sz, oc, zmin, sw, sh);
}
