// PS1 port (docs/ports/mania/32 9w.6): the update scan's state shared by the resident (RSDK/Scene/Object.cpp: the camera
// union, ps1/scan_fast.s, the stock fallback) and the UFO stages' sleeping-group scan (PS1ScanSleep.cpp, linked into
// their overlays only: gen_overlays EXTRAS 'scansleep').
#pragma once

namespace RSDK
{
struct EntityBase;
struct PS1ScanShared {
    // the union box as ps1/scan_fast.s reads it: minX, w, vx, minY, h, vy, the live bits, the De Bruijn table, the slot
    // table, cameras near camera 0 on x (9w.4)
    uint32 box[10];
    int32 camX, camY, camOX, camOY; // the one camera (s_camOne)
    void *sleep;                    // the sleeping groups (STG, PS1ScanSleep.cpp's PS1SleepGroup); NULL: off
    int32 sleepOX, sleepOY;         // the camera offset the sleeping groups were measured with
    uint32 grp, grpLive;            // the group the scan is in, and its live slots so far
    bool grpOk, camOne, camAbs;     // all its slots so far settled out of range; one camera; camera 0 inside +-2^30
};
extern PS1ScanShared ps1Scan;
// set by the overlay that carries PS1ScanSleep.cpp (PS1ScanSleepLink), cleared with every overlay (ps1/overlay.cpp)
extern EntityBase **(*g_ps1ScanSleepFn)(EntityBase **p, EntityBase **pend);
} // namespace RSDK
