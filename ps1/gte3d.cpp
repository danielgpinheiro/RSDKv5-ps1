/*
 * Scene3D's projection on the GTE — see gte3d.hh.
 */
#include "gte3d.hh"

#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"

volatile uint32_t g_ps1GteVertices = 0;
volatile uint32_t g_ps1GteFlagged  = 0;

namespace {
inline uint32_t pack(int lo, int hi) { return (uint32_t)(uint16_t)lo | ((uint32_t)(uint16_t)hi << 16); }
}

void PS1Gte3DSetup(const int32_t r[9], int32_t trx, int32_t trY, int32_t trz, uint32_t h, int32_t ofx, int32_t ofy) {
    using namespace psyqo::GTE;
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

int PS1Gte3DProject(const int16_t *xyz, int count, uint32_t *sxy, uint16_t *sz) {
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
            sxy[v]     = readRaw<Register::SXY0, Safe>();
            sxy[v + 1] = readRaw<Register::SXY1, Safe>();
            sxy[v + 2] = readRaw<Register::SXY2, Safe>();
            sz[v]      = (uint16_t)readRaw<Register::SZ1, Safe>();
            sz[v + 1]  = (uint16_t)readRaw<Register::SZ2, Safe>();
            sz[v + 2]  = (uint16_t)readRaw<Register::SZ3, Safe>();
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
        flagged += fl < 0;
    }
    g_ps1GteVertices = g_ps1GteVertices + count;
    g_ps1GteFlagged  = g_ps1GteFlagged + flagged;
    return flagged;
}
