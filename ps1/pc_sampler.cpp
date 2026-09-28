/*
 * PC sampler (`make SAMPLE=1`, docs/32 3.3e): where the CPU spends its time, by instruction.
 * Root counter 0 counts the system clock and raises its IRQ every PS1_SAMPLE_PERIOD cycles (psx-spx "Timers": mode
 * bit 3 reset at target, bit 4 IRQ at target, bit 6 repeat; source 0 = system clock). The first handler of the BIOS
 * IRQ chain (priority 0, like irq_guard's CD filter) acknowledges it and, while g_ps1SampleOn is set, adds one to the
 * interrupted instruction's counter: EPC (COP0 r14) still holds it, as nothing raises another exception inside the
 * handler. The counters are one word per instruction of the 2 MB address space, at 0x80400000 (2 MB): RAM that exists
 * only in an 8 MB emulator (PCSX-Redux -8mb; tools/emu.sh EMU_RAM=8mb), which the 2 MB program never touches, so the
 * profiled program is the 2 MB build. GDB dumps them (tools/play/ghz1_sample.gdb); tools/play/sample_report.py maps
 * them to functions (the EXE's and the overlays' symbols).
 */
#include <stdint.h>

#if PS1_SAMPLE
#include "common/psxlibc/handlers.h"
#include "common/syscalls/syscalls.h"

#define PS1_SAMPLE_PERIOD 9973 // cycles between samples (prime: no lock-step with a loop or the hblank)
#define PS1_SAMPLE_HIST   ((volatile uint32_t *)0x80400000)
#define PS1_SAMPLE_WORDS  (0x200000 / 4)

extern "C" {
volatile uint32_t g_ps1SampleOn    = 0; // GDB: 1 = count samples
volatile uint32_t g_ps1SampleCount = 0; // samples counted (total)
volatile uint32_t g_ps1SamplePeriod = PS1_SAMPLE_PERIOD;
}

static int pcSample() {
    volatile uint32_t *istat = (volatile uint32_t *)0xBF801070, *imask = (volatile uint32_t *)0xBF801074;
    if (!(*istat & *imask & 0x10))
        return 0;
    *istat = ~0x10u; // 0 acknowledges, 1 leaves the other bits alone
    if (g_ps1SampleOn) {
        uint32_t epc;
        __asm__ volatile("mfc0 %0, $14" : "=r"(epc));
        PS1_SAMPLE_HIST[(epc & 0x1FFFFF) >> 2] += 1;
        g_ps1SampleCount = g_ps1SampleCount + 1;
    }
    return 0;
}
static HandlerInfo s_sampler = {nullptr, nullptr, pcSample, 0};

void PS1SamplerInstall() {
    for (uint32_t i = 0; i < PS1_SAMPLE_WORDS; ++i) PS1_SAMPLE_HIST[i] = 0;
    syscall_sysEnqIntRP(0, &s_sampler);
    volatile uint16_t *t0 = (volatile uint16_t *)0xBF801100;
    t0[4] = PS1_SAMPLE_PERIOD; // target
    t0[2] = 0x0058;            // mode: system clock, reset at target, IRQ at target, repeat, pulse
    *(volatile uint32_t *)0xBF801074 |= 0x10;
}
#else
void PS1SamplerInstall() {}
#endif
