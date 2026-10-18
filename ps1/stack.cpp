#include "stack.hh"

// See stack.hh (MA-188).
extern "C" char __stack_start;
volatile uint32_t g_ps1StackOverflow = 0;
volatile uint32_t g_ps1StackLowWater = 0;
volatile uint32_t g_ps1StackUnused   = 0;
volatile uint32_t g_ps1StackMainSp   = 0;

static constexpr uint32_t STACK_PAINT = 0x57ACC0DEu;
static constexpr int GUARD_WORDS      = 4; // the painted area's bottom 16 bytes: never reached unless the stack overflowed

static uint32_t *paintBottom() {
    return (uint32_t *)((uintptr_t)&__stack_start - PS1_STACK_RESERVE + PS1_STACK_HEADER_GAP);
}

void PS1StackPaint() {
    uintptr_t sp;
    __asm__ volatile("move %0, $sp" : "=r"(sp));
    g_ps1StackMainSp = sp;
    uint32_t *w = paintBottom(), *end = (uint32_t *)((sp - 256) & ~3u); // nothing below sp is live
    while (w < end) *w++ = STACK_PAINT;
    PS1StackScan();
}

bool PS1StackGuardBroken() {
    const uint32_t *b = paintBottom();
    for (int i = 0; i < GUARD_WORDS; i++)
        if (b[i] != STACK_PAINT)
            return true;
    return false;
}

void PS1StackScan() {
    const uint32_t *b = paintBottom(), *w = b + GUARD_WORDS, *top = (const uint32_t *)&__stack_start;
    while (w < top && *w == STACK_PAINT) w++;
    g_ps1StackLowWater = (uint32_t)(uintptr_t)w;
    g_ps1StackUnused   = (uint32_t)((uintptr_t)w - (uintptr_t)(b + GUARD_WORDS));
}
