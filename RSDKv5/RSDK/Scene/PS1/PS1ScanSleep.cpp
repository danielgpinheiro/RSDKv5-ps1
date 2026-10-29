// PS1 port (docs/ports/mania/32 9w.6): the update scan with sleeping groups (9f, UFO item 2), for the UFO stages only.
// Linked into the overlays of the folders whose StageLoad asks for sleeping groups (gen_overlays EXTRAS 'scansleep':
// UFO_Setup), not the resident (it was 2.8 KB there): PS1ScanSleepLink plugs it into PS1UpdateScan (Object.cpp,
// g_ps1ScanSleepFn), which calls it with one camera inside +-2^30, the union valid and a sleep table. The camera union and
// the live / sleep bits are the resident's (PS1ScanShared.hpp, Object.hpp).
#include "RSDK/Core/RetroEngine.hpp"
#include "PS1ScanShared.hpp"

using namespace RSDK;

namespace
{
// docs/ports/mania/32 9f (UFO item 2): sleeping groups. In the UFO stages ~540 of ~690 live slots (rings, spheres, item
// boxes: ACTIVE_RBOUNDS; decorations: ACTIVE_BOUNDS) are tested and found out of range every frame. A group of 8 slots
// whose live slots are all of those two kinds and were all settled out of range in one stretch of the scan goes to sleep
// with a margin: how far (whole pixels, on each axis) the one camera may move with every slot still out of range in
// stock's own arithmetic (radius slots: the squared distance's floor root minus the range's root rounded up, times
// 181/256 < 1/sqrt(2); box slots: the failing axis's slack). While the camera stays within the margin of where it was,
// the scan passes over the group (its slots keep inRange false: nothing updates them). Woken by any class write into the
// group (PS1ClassNote: create, reset, copy), a camera offset change, and every frame the scan takes another path. Opt-in
// per scene: a StageLoad sets g_ps1ScanSleepWant (UFO_Setup; its stages' objects were reviewed: nothing writes an
// out-of-range slot's position, range or active). PS1_CHECK_SLEEP: stock's test on every slot passed over.
struct PS1SleepGroup {
    int16 x, y;    // the camera (whole pixels) when the group went to sleep
    uint16 margin; // pixels
    uint16 pad;
};
static_assert(sizeof(PS1SleepGroup) == 8, "Object.cpp PS1ScanSleepEnd allocates 8 bytes a group");
inline bool PS1Small(int32 v, int32 lim) { return (uint32)(v + lim) <= (uint32)(2 * lim); }
inline uint32 PS1Isqrt(uint32 v) // floor(sqrt(v))
{
    uint32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b)
            v -= r + b, r = (r >> 1) + b;
        else
            r >>= 1;
        b >>= 2;
    }
    return r;
}
// pixels the camera may move (each axis) with this settled slot still out of range for stock; 0: none. ps1-ufo:
// ACTIVE_NEVER / ACTIVE_PAUSED are out of range wherever the camera is: no limit (0xFFFF; the review above: nothing writes
// an out-of-range slot's active; a class write wakes the group)
uint32 PS1SleepMargin(const EntityBase *e)
{
    if (e->active == ACTIVE_NEVER || e->active == ACTIVE_PAUSED)
        return 0xFFFF;
    const int32 x = e->position.x, y = e->position.y;
    if (!PS1Small(x, 0x3FFFFFFF) || !PS1Small(y, 0x3FFFFFFF))
        return 0;
    if (e->active == ACTIVE_RBOUNDS) {
        const int32 dx = x - ps1Scan.camX, dy = y - ps1Scan.camY;
        const uint32 sx = (uint32)(dx < 0 ? -dx : dx) >> 16, sy = (uint32)(dy < 0 ? -dy : dy) >> 16;
        const int32 R = (int32)((uint32)e->updateRange.x + (uint32)ps1Scan.camOX);
        if (sx > 16383 || sy > 16383 || R < 0 || R >= 0x40000000)
            return 0;
        const uint32 d2 = sx * sx + sy * sy;
        if (d2 <= (uint32)R)
            return 0;
        const uint32 sr = PS1Isqrt(d2), rr = PS1Isqrt((uint32)R) + 1;
        return sr > rr ? ((sr - rr) * 181) >> 8 : 0;
    }
    if (e->active == ACTIVE_BOUNDS) {
        const int32 rx = e->updateRange.x, ry = e->updateRange.y;
        if (!PS1Small(rx, 0x10000000) || !PS1Small(ry, 0x10000000))
            return 0;
        const int32 ax = abs(x - ps1Scan.camX) - (rx + ps1Scan.camOX), ay = abs(y - ps1Scan.camY) - (ry + ps1Scan.camOY);
        const int32 slack = ax > ay ? ax : ay;
        return slack > 0 ? (uint32)(slack - 1) >> 16 : 0;
    }
    return 0;
}
__attribute__((noinline)) void PS1SleepGroupSet(uint32 g)
{
    uint32 m = 0xFFFF;
    for (uint32 k = 0; k < 8 && m; ++k) {
        const EntityBase *e = objectEntityPtr[g * 8 + k];
        if (e->classID) {
            const uint32 em = PS1SleepMargin(e);
            m = em < m ? em : m;
        }
    }
    if (!m)
        return;
    PS1SleepGroup *sl = (PS1SleepGroup *)ps1Scan.sleep;
    sl[g].x = (int16)(ps1Scan.camX >> 16), sl[g].y = (int16)(ps1Scan.camY >> 16), sl[g].margin = (uint16)m;
    ps1SleepBits[g >> 5] |= 1u << (g & 31);
}
inline bool PS1SleepValid(uint32 g)
{
    const PS1SleepGroup *sl = (const PS1SleepGroup *)ps1Scan.sleep;
    const int32 mx = (ps1Scan.camX >> 16) - sl[g].x, my = (ps1Scan.camY >> 16) - sl[g].y;
    return (uint32)((mx < 0 ? -mx : mx) + 1) <= sl[g].margin && (uint32)((my < 0 ? -my : my) + 1) <= sl[g].margin;
}
} // namespace
#if PS1_CHECK_SLEEP
extern "C" {
volatile uint32 g_ps1SleepSkipped = 0, g_ps1SleepWrong = 0; // slots passed over asleep; of them, not settled for stock
}
namespace
{
void PS1SleepCheck(uint32 g)
{
    for (uint32 k = 0; k < 8; ++k) {
        const EntityBase *e = objectEntityPtr[g * 8 + k];
        g_ps1SleepSkipped = g_ps1SleepSkipped + 1;
        if (!e->classID)
            continue;
        bool in = e->active != ACTIVE_NEVER && e->active != ACTIVE_PAUSED; // (stock: never in range; others decided below)
        if (e->active == ACTIVE_RBOUNDS) {
            int32 dx = e->position.x - ps1Scan.camX, dy = e->position.y - ps1Scan.camY;
            int32 sx = FROM_FIXED(dx < 0 ? -dx : dx), sy = FROM_FIXED(dy < 0 ? -dy : dy);
            in = (int32)((uint32)sx * (uint32)sx + (uint32)sy * (uint32)sy) <= (int32)((uint32)e->updateRange.x + (uint32)ps1Scan.camOX);
        }
        else if (e->active == ACTIVE_BOUNDS)
            in = abs(e->position.x - ps1Scan.camX) <= e->updateRange.x + ps1Scan.camOX
                 && abs(e->position.y - ps1Scan.camY) <= e->updateRange.y + ps1Scan.camOY;
        g_ps1SleepWrong = g_ps1SleepWrong + (in || e->inRange);
    }
}
} // namespace
#endif

#define PS1_SCAN_RET()                                                                                                 \
    do {                                                                                                               \
        ps1Scan.grpOk = false;                                                                                         \
        return p;                                                                                                      \
    } while (0)
// The scan (Object.cpp's C loop with sleeping groups on, as it was there): the slots it passes over are settled out of
// range (inRange false; a blank one leaves the live bits), the one it returns goes to the pass, which decides as stock
__attribute__((optimize("O2"))) static EntityBase **PS1ScanSleep(EntityBase **p, EntityBase **pend)
{
    uint32 s           = (uint32)(p - objectEntityPtr);
    const uint32 *box  = ps1Scan.box;
    const uint32 minX = box[0], w = box[1], vx = box[2], minY = box[3], h = box[4], vy = box[5];
    if (ps1Scan.camOX != ps1Scan.sleepOX || ps1Scan.camOY != ps1Scan.sleepOY) {
        for (uint32 i = 0; i < sizeof(ps1SleepBits) / 4; ++i) ps1SleepBits[i] = 0;
        ps1Scan.sleepOX = ps1Scan.camOX, ps1Scan.sleepOY = ps1Scan.camOY;
    }
    const uint32 send = (uint32)(pend - objectEntityPtr);
    for (; p < pend; ++p, ++s) {
        // the whole-word skip only (9f): a group goes to sleep at its last slot, which must be visited
        if (!(s & 31) && !ps1LiveBits[s >> 5]) {
            if (pend - p <= 32)
                return pend;
            p += 31, s += 31;
            continue;
        }
        if (!(s & 7)) {
            const uint32 g = s >> 3;
            if ((ps1SleepBits[g >> 5] >> (g & 31)) & 1) {
                if (s + 8 <= send && PS1SleepValid(g)) {
#if PS1_CHECK_SLEEP
                    PS1SleepCheck(g);
#endif
                    p += 7, s += 7;
                    continue;
                }
                ps1SleepBits[g >> 5] &= ~(1u << (g & 31));
            }
            ps1Scan.grp = g, ps1Scan.grpOk = true, ps1Scan.grpLive = 0;
        }
        EntityBase *ent = *p;
        uint32 cls    = ent->classID;
        uint32 active = ent->active;
        uint32 x = (uint32)ent->position.x, rx = (uint32)ent->updateRange.x;
        __asm__("" : "+r"(cls), "+r"(active), "+r"(x), "+r"(rx)); // all loaded here (GCC won't hoist loads over a branch)
        if (cls) {
            if (active == ACTIVE_BOUNDS) {
                // x in [minX - rx, maxX + rx] <=> (uint32)(x - (minX - rx)) <= (uint32)(w + 2 * rx) when the interval is
                // not empty; an empty one (negative offsets or ranges) answers "inside", which leaves it to the stock loop.
                // 3.0g: outside on x (x and rx valid) is out of range for every camera whatever y is, so y and its range
                // are loaded only for the slots inside on x
                if ((int32)(x + vx) < 0 || (rx + 0x10000000u) >= 0x20000000u)
                    PS1_SCAN_RET();
                if (x - minX + rx <= w + 2 * rx) {
                    uint32 y = (uint32)ent->position.y, ry = (uint32)ent->updateRange.y;
                    if ((int32)(y + vy) < 0 || (ry + 0x10000000u) >= 0x20000000u || y - minY + ry <= h + 2 * ry)
                        PS1_SCAN_RET();
                }
            }
            else if (active == ACTIVE_XBOUNDS) {
                if ((int32)(x + vx) < 0 || (rx + 0x10000000u) >= 0x20000000u || x - minX + rx <= w + 2 * rx)
                    PS1_SCAN_RET();
            }
            else if (active == ACTIVE_RBOUNDS) {
                // stock's test for the one camera, in its int32 arithmetic (wrapping as the PC's): out of range settles here
                int32 dx = x - ps1Scan.camX, dy = ent->position.y - ps1Scan.camY;
                int32 sx = FROM_FIXED(dx < 0 ? -dx : dx), sy = FROM_FIXED(dy < 0 ? -dy : dy);
                if ((int32)((uint32)sx * (uint32)sx + (uint32)sy * (uint32)sy) <= (int32)((uint32)rx + (uint32)ps1Scan.camOX))
                    PS1_SCAN_RET();
            }
            else if (active != ACTIVE_NEVER && active != ACTIVE_PAUSED)
                PS1_SCAN_RET();
        }
        // ps1-ufo: blank and never-active slots sleep too (in UFO5 ~190 blank slots in live words, the decorations
        // UFO_NO_DECOR removes, and ~68 inactive UFO_Circuit nodes were tested every frame: groups of them never slept);
        // a group of blank slots only sleeps with no limit (a class write into it wakes it)
        if (ps1Scan.grp == (s >> 3)) {
            if (cls)
                ps1Scan.grpOk = ps1Scan.grpOk && (active == ACTIVE_BOUNDS || active == ACTIVE_RBOUNDS || active == ACTIVE_NEVER
                                                  || active == ACTIVE_PAUSED);
            if ((s & 7) == 7 && ps1Scan.grpOk)
                PS1SleepGroupSet(s >> 3);
        }
        ent->inRange = false;
        if (!cls)
            ps1LiveBits[s >> 5] &= ~(1u << (s & 31));
    }
    return p;
}

extern "C" void PS1ScanSleepLink() { g_ps1ScanSleepFn = PS1ScanSleep; }
