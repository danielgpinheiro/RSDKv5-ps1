// docs/perf/80 (test switch `make CHAINCRC=1`, never in `make dist`): a hash of every frame's GPU command words, for
// exact A/B proofs of a renderer change over a whole run (a boss recipe, an act sweep), not one frame's dump
// (tools/play/chain_ab.sh). PS1RenderEndFrame calls PS1ChainCrcFrame after PS1ChainEnd, when the frame's chain is
// complete: it walks psyqo's list as sendChain() will send it (the walk of tools/play/gpu_cost.py: links left out, the
// tail node's count from m_chainTailCount, the oversized flags), so two builds whose nodes sit at other addresses hash
// the same when the GPU receives the same words. FNV-1a 32 over the payload words.
// g_ps1ChainCrc[f & 255] = frame f's hash (f = g_ps1PerfFrames), g_ps1ChainCrcWords the same for its word count; every
// 256 frames ps1ChainCrcFull() is called (a GDB breakpoint dumps the ring there: tools/play/chain_crc.gdb).
// psyqo's chain fields are private: this test-only file opens them.
#define private public
#include <psyqo/gpu.hh>
#undef private

#include <stdint.h>

extern psyqo::GPU *g_ps1GPU;
extern volatile uint32_t g_ps1PerfFrames;

volatile uint32_t g_ps1ChainCrc[256]      = {};
volatile uint32_t g_ps1ChainCrcWords[256] = {};
volatile uint32_t g_ps1ChainCrcAll        = 2166136261u; // every frame's hash folded in, frame by frame
volatile uint32_t g_ps1ChainCrcFrames     = 0;           // frames hashed

extern "C" __attribute__((noinline)) void ps1ChainCrcFull() { __asm__ volatile(""); }

void PS1ChainCrcFrame() {
    psyqo::GPU &g = *g_ps1GPU;
    uint32_t h = 2166136261u, n = 0;
    uintptr_t *node = g.m_chainHead;
    bool over = g.m_chainHeadOversized;
    for (unsigned guard = 0; node && guard < 200000; guard++) {
        uint32_t hd = *node;
        bool last = node == g.m_chainTail;
        uint32_t cnt = last ? (uint32_t)(g.m_chainTailCount >> 24) : hd >> 24;
        if (over)
            cnt <<= 4;
        for (uint32_t i = 1; i <= cnt; i++)
            h = (h ^ node[i]) * 16777619u;
        n += cnt;
        uint32_t nxt = hd & 0xFFFFFF;
        if (last || nxt == 0xFFFFFF)
            break;
        over = (nxt & 0x800000) && (nxt & 1);
        node = reinterpret_cast<uintptr_t *>(0x80000000u | (nxt & 0x7FFFFC));
    }
    uint32_t f = g_ps1PerfFrames;
    g_ps1ChainCrc[f & 255]      = h;
    g_ps1ChainCrcWords[f & 255] = n;
    g_ps1ChainCrcAll            = (g_ps1ChainCrcAll ^ h) * 16777619u;
    g_ps1ChainCrcFrames         = g_ps1ChainCrcFrames + 1;
    if ((f & 255) == 255)
        ps1ChainCrcFull();
}
