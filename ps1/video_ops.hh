/*
 * docs/ports/mania/32 6.2: the video player (ps1/fmv, ps1/video, ps1/mdec: ~19 KB) is linked into the overlays of the stage folders
 * that play videos (tools/gen_overlays.py VIDEO_OBJECTS: the Title, the endings), not the resident EXE: zones never
 * need it and GHZ1's STG margin can't hold it. The overlay's register function fills this table (PS1VideoLink, in
 * ps1/fmv.cpp); the overlay loader clears it (ps1/overlay.cpp). Resident code reaches the player only through it.
 */
#pragma once

struct PS1VideoOps {
    void (*beginFrame)();                         // PS1FmvBeginFrame: at each frame's start (nothing chained yet)
    bool (*request)(const char *name, int audio); // PS1FmvRequest: the sequence opens at the next frame's start
    int (*update)(bool skip);                     // PS1FmvUpdate: once per frame in video mode; 1 = done
    bool (*closeForRead)();                       // cdrom_fs's safety net: close an open STR before a file read
};
extern PS1VideoOps g_ps1VideoOps;
