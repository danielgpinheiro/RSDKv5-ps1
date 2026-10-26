#pragma once
#include <stdint.h>

// The main stack's overflow check (MA-188, the RSDKv3-ps1 CD-092 family, ../docs/44). nugget's allocator sizes the heap
// up to __stack_start, the RAM top the stack grows down from; Sonic CD / Sonic 2 / Nexus moved their stack into a .bss
// array for that. Mania never needed to: since docs/ports/mania/32 2.4a InitStorage (RSDK/Storage/Storage.cpp) ends STG at
// __stack_start - PS1_STACK_RESERVE and allocates the rest up to the RAM top as one guard block nothing else can get,
// so the stack owns those 16 KB (before InitStorage the heap is ~1 MB below them). What was missing is a check: a stack
// deeper than the reserve would write into STG's top, where the scene's code overlay sits. main() paints the reserve
// before the app starts; the bottom guard words are checked every frame and the low-water mark is updated at each
// load's end (no per-frame scan).
#define PS1_STACK_RESERVE    (16 * 1024) // = RSDK/Storage/Storage.cpp PS1_STACK_RESERVE, tools/gen_overlays.py
#define PS1_STACK_HEADER_GAP 64          // the guard block's 8-byte allocator header sits at (or just below) the
                                         // reserve's start: the painted area begins above it

extern volatile uint32_t g_ps1StackOverflow; // frames whose check found the stack's bottom guard words overwritten
extern volatile uint32_t g_ps1StackLowWater; // lowest address the stack had written at the last load's end (GDB)
extern volatile uint32_t g_ps1StackUnused;   // painted bytes above the guard words never written (same moment)
extern volatile uint32_t g_ps1StackMainSp;   // sp when main() painted (the paint ends 256 B below it)

void PS1StackPaint();       // from main(), before the app runs: paint the reserve up to just below the current sp
bool PS1StackGuardBroken(); // the bottom guard words are no longer paint
void PS1StackScan();        // update g_ps1StackLowWater / g_ps1StackUnused (a scan: load ends, GDB)
