// docs/32 phase 13 M1: the console's memory mods, detected once at boot (start(), before the engine allocates).
// One disc for every console: the extras (phase 13) turn on only when the hardware is there; a stock console takes
// none of these paths beyond the probes themselves.
//
// - Main RAM (psx-spx memorycontrol.md, 1F801060h RAM_SIZE): every BIOS writes 0B88h, one 8 MB bank on /RAS0, so a
//   stock console's 2 MB repeats through the first 8 MB. A word written 2 / 4 / 6 MB above a probe word reaches the
//   probe word there and doesn't with 8 MB fitted. With 8 MB, bit 10 (/RAS1) maps a second bank at 8 MB (the 16 MB
//   mods); without one the bank may bus-fault (GTA3-ps1 bench/ram16.cpp), so it is read first under a temporary
//   exception vector that skips a faulting load, and RAM_SIZE goes back to the BIOS value if the bank isn't there.
// - VRAM (psx-spx GPU, GP1(09h) "Set VRAM size (v2)"): bit 0 opens rows 512-1023 on the 2 MB VRAM mod (Tunertom's
//   PU-18 mod; the arcade boards' layout). A word written at Y >= 512 must read back and must not show at its 9-bit
//   mirror; without the mod GP1(09h) goes back to 0. (GTA3-ps1 ps1/re3/vram2mb_ps1.cpp, the same probe.)
#include <stdint.h>

#include <stdio.h>
#include <string.h>

#include "common/syscalls/syscalls.h"

// The code runs from the Logos overlay (tools/gen_overlays.py EXTRAS 'hwdetect': no resident bytes on a stock console,
// the first scene of every boot); `make BOOT=... HWDETECT=1` links it into a test build's resident and runs it from
// start(). The results live in the resident (ps1/rsdk5_main.cpp).
extern "C" {
extern volatile uint32_t g_ps1RamMB, g_ps1Vram2MB, g_ps1RamCtrl, g_ps1HwFaults, g_ps1ExtrasOff, g_ps1Extras, g_ps1HwDetected;
uint32_t ps1HwProbeWord = 0; // the RAM probe's word (its mirrors 2 / 4 / 6 MB up)
uint32_t ps1HwProbeRead(uint32_t addr);          // lw at addr (asm below: never in a delay slot)
void ps1HwProbeWrite(uint32_t addr, uint32_t v); // sw at addr
extern uint32_t ps1HwProbeVector[4];             // the 4 words copied to the exception vector during the probe
}

// The exception handler the 16 MB probe installs at 0x80000080: interrupts are off (critical section), so the only
// exception that can come is the probe's bus error. It counts it, sets v0 to a value no probe expects and returns
// past the faulting load (ps1HwProbeRead's lw is followed by a nop, never in a branch delay slot).
__asm__(R"(
    .section .text.ps1HwProbe, "ax", @progbits
    .set push
    .set noreorder
    .set noat
    .global ps1HwProbeRead
ps1HwProbeRead:
    lw    $v0, 0($a0)
    nop
    jr    $ra
    nop
    .global ps1HwProbeWrite
ps1HwProbeWrite:
    sw    $a1, 0($a0)
    jr    $ra
    nop
ps1HwProbeHandler:
    lui   $k0, %hi(g_ps1HwFaults)
    lw    $k1, %lo(g_ps1HwFaults)($k0)
    nop
    addiu $k1, $k1, 1
    sw    $k1, %lo(g_ps1HwFaults)($k0)
    lui   $v0, 0xDEAD
    mfc0  $k0, $14
    nop
    addiu $k0, $k0, 4
    jr    $k0
    rfe
    .section .data.ps1HwProbe, "aw", @progbits
    .global ps1HwProbeVector
ps1HwProbeVector:
    lui   $k0, %hi(ps1HwProbeHandler)
    addiu $k0, $k0, %lo(ps1HwProbeHandler)
    jr    $k0
    nop
    .set pop
)");

namespace {
volatile uint32_t &ramSize() { return *(volatile uint32_t *)0xBF801060u; }
volatile uint32_t &gpuData() { return *(volatile uint32_t *)0xBF801810u; }
volatile uint32_t &gpuStat() { return *(volatile uint32_t *)0xBF801814u; }

void critEnter() { __asm__ volatile("mtc0 %0, $12 ; nop ; nop" : : "r"(0x40000000)); }
void critLeave() { __asm__ volatile("mtc0 %0, $12" : : "r"(0x40000401)); }

// 2 or 8: the probe word and its mirrors 2, 4, 6 MB up (KSEG1: uncached, through the write buffer in order)
uint32_t probeRam8() {
    const uint32_t a = 0xA0000000u | ((uint32_t)(uintptr_t)&ps1HwProbeWord & 0x1FFFFCu);
    const uint32_t keep = ps1HwProbeRead(a);
    bool own = true;
    ps1HwProbeWrite(a, 0x5A5AA5A5u);
    for (uint32_t k = 1; k < 4; k++) ps1HwProbeWrite(a + k * 0x200000u, 0x13572468u + k);
    for (uint32_t k = 1; k < 4; k++) own = own && ps1HwProbeRead(a + k * 0x200000u) == 0x13572468u + k;
    own = own && ps1HwProbeRead(a) == 0x5A5AA5A5u;
    ps1HwProbeWrite(a, keep);
    return own ? 8 : 2;
}

// 16 when a second 8 MB bank answers at 8 MB with /RAS1 on, else 8 (RAM_SIZE restored)
uint32_t probeRam16(uint32_t bios) {
    uint32_t *vec = (uint32_t *)0xA0000080u, saved[4];
    // PCSX-Redux's kernel-memory checker (psyqo turns it on: nugget pcsxhw.h pcsx_checkKernel; on hardware a write to
    // the expansion region psyqo makes too) stops a program touching the BIOS's RAM: off while the vector is ours
    *(volatile char *)0x1F802088 = 0;
    for (int i = 0; i < 4; i++) saved[i] = vec[i], vec[i] = ps1HwProbeVector[i];
    syscall_flushCache();
    ramSize() = bios | 0x400u;
    const uint32_t a = 0xA0000000u | ((uint32_t)(uintptr_t)&ps1HwProbeWord & 0x1FFFFCu), b = a + 0x800000u;
    const uint32_t faults = g_ps1HwFaults, keep = ps1HwProbeRead(a);
    bool own = false;
    (void)ps1HwProbeRead(b); // a missing bank faults here (a load: the fault is precise), before any store
    if (g_ps1HwFaults == faults) {
        ps1HwProbeWrite(a, 0x5A5AA5A5u);
        ps1HwProbeWrite(b, 0x24681357u);
        ps1HwProbeWrite(b + 4, 0xDBB79CA8u); // its complement next to it: an open bus doesn't keep the first
        own = g_ps1HwFaults == faults && ps1HwProbeRead(b) == 0x24681357u && ps1HwProbeRead(b + 4) == 0xDBB79CA8u &&
              ps1HwProbeRead(a) == 0x5A5AA5A5u;
        ps1HwProbeWrite(a, keep);
    }
    if (!own) ramSize() = bios;
    for (int i = 0; i < 4; i++) vec[i] = saved[i];
    syscall_flushCache();
    *(volatile char *)0x1F802088 = 1;
    return own ? 16 : 8;
}

void waitCmd() {
    while (!(gpuStat() & 0x04000000u)) {
    }
}
void vramWrite(uint16_t x, uint16_t y, uint32_t twoTexels) { // GP0(A0h): two halfwords at (x, y)
    waitCmd();
    gpuData() = 0xA0000000u;
    gpuData() = (uint32_t)x | ((uint32_t)y << 16);
    gpuData() = 2u | (1u << 16);
    gpuData() = twoTexels;
}
uint32_t vramRead(uint16_t x, uint16_t y) { // GP0(C0h), then GPUREAD once GPUSTAT bit 27 says it is there
    waitCmd();
    gpuData() = 0x01000000u;
    waitCmd();
    gpuData() = 0xC0000000u;
    gpuData() = (uint32_t)x | ((uint32_t)y << 16);
    gpuData() = 2u | (1u << 16);
    for (int i = 0; i < 100000 && !(gpuStat() & 0x08000000u); i++) {
    }
    return gpuData();
}
uint32_t probeVram() {
    // a GPU DMA in flight (the loading icon's chain) finishes first: the probe's GP0 words go straight to the port
    for (int i = 0; i < 1000000 && (*(volatile uint32_t *)0xBF8010A8u & 0x01000000u); i++) {
    }
    gpuStat() = 0x09000001u; // GP1(09h): Y 512-1023
    const uint16_t x = 1020, yHigh = 1020, yLow = yHigh - 512;
    const uint32_t keep = vramRead(x, yLow);
    vramWrite(x, yLow, 0x5A5AA5A5u);
    vramWrite(x, yHigh, 0x13572468u);
    const bool ok = vramRead(x, yHigh) == 0x13572468u && vramRead(x, yLow) == 0x5A5AA5A5u;
    vramWrite(x, yLow, keep);
    if (!ok) gpuStat() = 0x09000000u;
    return ok ? 1 : 0;
}
} // namespace

extern "C" void PS1HwDetect() {
    critEnter();
    g_ps1RamCtrl = ramSize();
    uint32_t mb  = 2;
    if ((g_ps1RamCtrl & 0xA00u) == 0xA00u) // bits 9 and 11: the BIOS mapped an 8 MB bank (a 2 MB map can't be probed)
        mb = probeRam8();
    if (mb == 8)
        mb = probeRam16(g_ps1RamCtrl);
    g_ps1RamMB   = mb;
    g_ps1Vram2MB = probeVram();
    critLeave();
    // (an 8 MB measuring link, `make MEASURE_RAM=8M`, has its own heap above 2 MB: no extras there)
    extern volatile uint32_t g_ps1LinkTop; // ps1/rsdk5_main.cpp (__stack_start: the overlays can't name it)
    g_ps1Extras     = mb >= 8 && !g_ps1ExtrasOff && g_ps1LinkTop <= 0x80200000u;
    g_ps1HwDetected = 1;
}

// M2: the Mods overlay (ps1/mods/, 'MOD1' at 0x80200000) when the console has the RAM for it. Its registration fills
// g_ps1Mods (ps1/mods_ops.h); a file that doesn't check out leaves the console stock.
extern "C" volatile uint32_t g_ps1ModsLoaded;
static void modsLoad() {
    FILE *f = fopen("Data/PS1Ovl/Mods.bin", "rb");
    if (!f)
        return;
    uint32_t h[4];
    if (fread(h, 1, sizeof(h), f) == sizeof(h) && h[0] == 0x31444F4Du && h[3] == 0x80200000u && h[1] > sizeof(h) &&
        h[1] <= 0x100000u) {
        memcpy((void *)0x80200000u, h, sizeof(h));
        if (fread((uint8_t *)0x80200000u + sizeof(h), 1, h[1] - sizeof(h), f) == h[1] - sizeof(h)) {
            syscall_flushCache(); // new code in RAM
            ((void (*)())h[2])();
            g_ps1ModsLoaded = 1;
        }
    }
    fclose(f);
}

// The Logos overlay's registration (every load of the scene): the probes run once per boot
extern "C" void PS1HwDetectLink() {
    if (g_ps1HwDetected)
        return;
    PS1HwDetect();
    if (g_ps1Extras)
        modsLoad();
    // a disc built without the full-resolution atlases (Scene<N>.atx): the console is used as one without the VRAM mod
    // (no .atx to load, no Sonic & Tails: its art is only there)
    if (g_ps1Vram2MB) {
        FILE *f = fopen("Data/Stages/GHZ/Scene1.atx", "rb");
        if (f)
            fclose(f);
        else
            g_ps1Vram2MB = 0;
    }
}
