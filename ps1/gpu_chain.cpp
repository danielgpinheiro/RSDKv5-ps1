/*
 * PS1 DMA-chained renderer helpers — see gpu_chain.hh.
 */
#include "gpu_chain.hh"

#include <psyqo/alloc.h>

#include <string.h>

#include <new>

#include <psyqo/fragments.hh>
#include <psyqo/gpu.hh>
#include <psyqo/primitives/control.hh>
#include <psyqo/primitives/misc.hh>

#include "video.hh" // PS1Hblanks

extern "C" void PS1NoteAlloc(void *p, size_t size); // ps1/libc/stdlib.cpp: the init reserve's high-water mark

// The arena. Sonic CD (docs/28 3.6): one per frame parity, 64 KB each, on the heap, time-shared with the CD
// read-ahead windows (user decision 2026-09-24): released at the start of a load (PS1ChainSuspend), allocated again
// at its end. Mania (docs/32 3.2): in play the heap has ~52 KB free in one block (GHZ1: the init reserve less what
// stays allocated after the loads): 3.2 split it into 2 x 25 KB; 3.3b makes it ONE arena of 50 KB: psyqo sends frame
// N's chain at the flip, before frame N+1 starts, and frame N+1 writes its chain only after its logic, by when the GPU
// has taken frame N's (a few thousand small primitives: ~30 hblanks). The frame's first write waits for that DMA to be
// over (arenaReady; g_ps1ChainIdleWaits counts the frames that had to wait).
#ifndef PS1_CHAIN_ARENA_KB
#define PS1_CHAIN_ARENA_KB 60 // a measuring build may pass more (EXTRA_CPPFLAGS=-DPS1_CHAIN_ARENA_KB=128, 8 MB link). 7.1: 50 -> 60: GHZ1's
                              // title card over its busy middle (after Blue Spheres) needs 14,302 words (57 KB)
#endif
#define PS1_CHAIN_ARENA_WORDS (PS1_CHAIN_ARENA_KB * 1024 / 4)
// Every chained fragment is kept <= 14 words (psyqo GPU::c_chainThreshold / 4). psyqo links
// bigger fragments with bit 23 set so the linked-list DMA stops, then its DMA IRQ reads DMA2
// MADR to find and send the big node by hand. Real hardware leaves MADR at the failing link,
// but PCSX-Redux just treats bit 23 as end-of-list and leaves MADR at the chain start, so psyqo
// re-sends earlier nodes: primitives get drawn twice and out of order (verified: Sonic drawn
// over the front grass). Small fragments avoid that path on both.
#define PS1_CHAIN_MAX_WORDS   14
#define PS1_TILE_BATCH        4  // 4 * 3 words = 12 <= 14
#define PS1_SPRITE_BATCH      3  // 3 * 4 words = 12 <= 14
#define PS1_CLUT_CHUNK        22 // halfwords per CLUT upload chunk: 3 + 11 words = 14 // 84 * 3 words = 252 <= 255-word chained fragment limit

volatile uint32_t g_ps1ChainOverflow  = 0;
volatile uint32_t g_ps1ChainHighWater = 0;
volatile uint32_t g_ps1ChainLastWords = 0;
volatile uint32_t g_ps1ChainReplayWords = 0;
volatile uint32_t g_ps1ChainIdleWaits   = 0; // frames whose first write waited for the last frame's DMA
volatile uint32_t g_ps1ChainIdleWaitHbl = 0; // hblanks spent in those waits (total)

namespace {

typedef psyqo::Fragments::FixedFragment<psyqo::Prim::Sprite16x16, PS1_TILE_BATCH> TileBatch;
typedef psyqo::Fragments::FixedFragment<psyqo::Prim::Sprite, PS1_SPRITE_BATCH> SpriteBatch;

struct ClutChunkFragment {
    uintptr_t head;                 // psyqo chain link (must be uintptr_t)
    psyqo::Prim::VRAMUpload upload; // 3 words
    uint16_t data[PS1_CLUT_CHUNK];  // 11 words
    unsigned words;                 // payload size in words (not sent)
    size_t getActualFragmentSize() const { return words; }
};
static_assert(sizeof(psyqo::Prim::VRAMUpload) / 4 + PS1_CLUT_CHUNK / 2 <= PS1_CHAIN_MAX_WORDS, "CLUT chunk too big");
static_assert(PS1_TILE_BATCH * sizeof(psyqo::Prim::Sprite16x16) / 4 <= PS1_CHAIN_MAX_WORDS, "tile batch too big");
static_assert(PS1_SPRITE_BATCH * sizeof(psyqo::Prim::Sprite) / 4 <= PS1_CHAIN_MAX_WORDS, "sprite batch too big");

// A copied fragment (PS1ChainReplay): payload <= PS1_CHAIN_MAX_WORDS like every fragment here.
struct RawFragment {
    uintptr_t head;
    uint32_t data[PS1_CHAIN_MAX_WORDS];
    unsigned words;
    size_t getActualFragmentSize() const { return words; }
};

// A replayed fragment of exactly its size: the size field sits before `head` (psyqo's chain() only
// reads &head and getActualFragmentSize()), so a copy takes 2 + n arena words instead of 16.
struct VarFragment {
    unsigned words;
    uintptr_t head;
    uint32_t data[PS1_CHAIN_MAX_WORDS]; // only `words` of them are allocated
    size_t getActualFragmentSize() const { return words; }
};

uint32_t *s_arenaMem = nullptr; // s_arenaMemWords (heap); kept while suspended: lent to the CD windows (docs/32 9.2)
uint32_t s_arenaMemWords = 0;
uint32_t s_arenaWords = PS1_CHAIN_ARENA_WORDS; // smaller while an FMV plays (PS1ChainSetArenaWords)
bool s_waitIdle = false; // the frame has not written the arena yet: the last frame's DMA may still read it
void (*s_hook)() = nullptr;
extern uint32_t *s_slot;

inline void runHook() {
    if (s_hook) {
        auto h = s_hook;
        s_hook = nullptr;
        h();
    }
}
uint32_t *s_base    = nullptr;
uint32_t *s_cur     = nullptr;
uint32_t *s_end     = nullptr;
psyqo::GPU *s_gpu   = nullptr;

// 4.2d: a second block for the frames that fill the arena (the AIZ sea's per-line parallax, the ruby's deformed layers):
// lent by the engine from STG's tail after a load when the scene has STG to spare (PS1ChainExtraTake, Storage.cpp) and
// given back before the next load (PS1ChainSuspend). A frame moves on into it once; both are reused by the next frame
// after the same DMA wait.
extern "C" uint32_t *PS1ChainExtraTake(uint32_t *words) __attribute__((weak));
extern "C" void PS1ChainExtraGive() __attribute__((weak));
uint32_t *s_extra = nullptr;
uint32_t s_extraWords = 0;
bool s_inExtra = false;          // this frame writes in the extra block
uint32_t s_mainUsed = 0;         // words the frame used in the main arena before it moved on
volatile uint32_t g_ps1ChainExtraWords  = 0; // the block lent this scene (words; GDB)
volatile uint32_t g_ps1ChainExtraFrames = 0; // frames that needed it
volatile uint32_t g_ps1ChainExtraBusy   = 0; // loads begun while this frame's chain used it (must stay 0)

inline bool roomFor(size_t words) {
    if (s_cur + words <= s_end)
        return true;
    if (s_inExtra || !s_extra || words > s_extraWords)
        return false;
    s_mainUsed = (uint32_t)(s_cur - s_base);
    s_cur = s_extra, s_end = s_extra + s_extraWords, s_inExtra = true;
    g_ps1ChainExtraFrames = g_ps1ChainExtraFrames + 1;
    return true;
}

// Before the frame's first arena write: the last frame's chain must be DMA'd (one arena, see above)
inline void arenaReady() {
    if (s_waitIdle) {
        s_waitIdle = false;
        if (s_gpu->isChainTransferring()) {
            uint32_t t0 = PS1Hblanks();
            s_gpu->waitChainIdle();
            g_ps1ChainIdleWaits   = g_ps1ChainIdleWaits + 1;
            g_ps1ChainIdleWaitHbl = g_ps1ChainIdleWaitHbl + PS1HblanksSince(t0);
        }
    }
}
TileBatch *s_batch  = nullptr; // open tile batch (not yet chained)
SpriteBatch *s_sbatch = nullptr; // open sprite batch (not yet chained); at most one batch is open
uint16_t s_lastTPage = 0xFFFF;  // raw TPageAttr of the last chained TPage (0xFFFF = unknown)
bool s_tpageValid    = false;

VarFragment *allocVar(unsigned n) {
    arenaReady();
    if (!s_cur || !roomFor(2 + n)) {
        g_ps1ChainOverflow = g_ps1ChainOverflow + 1;
        return nullptr;
    }
    auto *f = reinterpret_cast<VarFragment *>(s_cur);
    s_cur += 2 + n;
    f->words = n;
    return f;
}

template <typename T>
T *allocRaw() {
    size_t words = (sizeof(T) + 3) / 4;
    arenaReady();
    if (!s_cur || !roomFor(words)) {
        g_ps1ChainOverflow = g_ps1ChainOverflow + 1;
        return nullptr;
    }
    T *p = reinterpret_cast<T *>(s_cur);
    s_cur += words;
    return p;
}

void flushBatch() {
    if (s_batch) {
        if (s_batch->count)
            s_gpu->chain(*s_batch);
        s_batch = nullptr;
    }
    if (s_sbatch) {
        if (s_sbatch->count)
            s_gpu->chain(*s_sbatch);
        s_sbatch = nullptr;
    }
}

template <typename Prim>
void chainSimple(const Prim &prim) {
    runHook();
    flushBatch();
    auto *frag = allocRaw<psyqo::Fragments::SimpleFragment<Prim>>();
    if (!frag)
        return;
    new (frag) psyqo::Fragments::SimpleFragment<Prim>(prim);
    s_gpu->chain(*frag);
}

} // namespace

volatile uint32_t g_ps1ChainArenaFail = 0; // frames without an arena (heap full): nothing drawn
volatile uint32_t g_ps1ChainSuspends  = 0; // arenas released for a load
static bool s_suspended = false;

void PS1ChainBegin(psyqo::GPU &gpu) {
    s_gpu = &gpu;
    if (s_arenaMem && s_arenaMemWords != s_arenaWords && !s_suspended) { // (an FMV's smaller arena: PS1ChainSetArenaWords)
        psyqo_free(s_arenaMem);
        s_arenaMem = nullptr;
    }
    if (!s_arenaMem && !s_suspended) {
        s_arenaMem      = (uint32_t *)psyqo_malloc(s_arenaWords * 4);
        s_arenaMemWords = s_arenaMem ? s_arenaWords : 0;
        PS1NoteAlloc(s_arenaMem, s_arenaWords * 4); // docs/32 4.2a: the heap headroom counter missed the arena
    }
    if (!s_arenaMem) {
        g_ps1ChainArenaFail = g_ps1ChainArenaFail + 1;
        s_base = s_cur = s_end = nullptr;
        s_batch  = nullptr;
        s_sbatch = nullptr;
        return;
    }
    s_base       = s_arenaMem;
    s_waitIdle   = true;
    s_cur        = s_base;
    s_end        = s_base + s_arenaWords;
    s_inExtra    = false;
    s_mainUsed   = 0;
    s_batch      = nullptr;
    s_sbatch     = nullptr;
    s_tpageValid = false;
    s_hook       = nullptr;
    s_slot       = nullptr;
    g_ps1ChainReplayWords = 0;
}

void PS1ChainEnd() {
    flushBatch();
    uint32_t used = s_inExtra ? s_mainUsed + (uint32_t)(s_cur - s_extra) : (uint32_t)(s_cur - s_base);
    g_ps1ChainLastWords = used;
    if (used > g_ps1ChainHighWater)
        g_ps1ChainHighWater = used;
}

void PS1ChainSuspend() {
    // 4.2d: the lent block always goes back before a load (its records and staging use STG's tail), even when the
    // arena stays (this frame chained its clear / CLUTs first: the usual case), once the last chain's DMA is over.
    // A frame already writing in it (a load after the frame drew that much) would lose it: counted, must stay 0
    if (s_extra) {
        if (s_inExtra) {
            g_ps1ChainExtraBusy = g_ps1ChainExtraBusy + 1;
        }
        else {
            PS1GpuImmediate();
            if (PS1ChainExtraGive)
                PS1ChainExtraGive();
            s_extra = nullptr, s_extraWords = 0, g_ps1ChainExtraWords = 0;
        }
    }
    if (!s_arenaMem || s_suspended)
        return;
    flushBatch();
    if (s_cur != s_base || s_inExtra) // this frame already chained something psyqo points at: keep the arenas
        return;
    PS1GpuImmediate(); // the previous frame's chain may still be DMA'd from the arena
    // docs/32 9.2: the arena is lent to the load's CD read-ahead windows (PS1ChainLent), not freed: freed, small
    // allocations of the load (FILEs) landed between the windows in its hole and it could not come back (no chain)
    s_base = s_cur = s_end = nullptr;
    s_suspended = true;
    g_ps1ChainSuspends = g_ps1ChainSuspends + 1;
}

// The suspended arena's memory for the CD windows during a load (nullptr: none to lend)
uint8_t *PS1ChainLent(uint32_t *bytes) {
    if (!s_suspended || !s_arenaMem)
        return nullptr;
    *bytes = s_arenaMemWords * 4;
    return (uint8_t *)s_arenaMem;
}

void PS1ChainSetArenaWords(uint32_t words) { s_arenaWords = words ? words : PS1_CHAIN_ARENA_WORDS; }

void PS1ChainResume() {
    if (!s_extra && PS1ChainExtraTake) { // 4.2d: a second block when the loaded scene leaves STG to spare (every load)
        uint32_t words = 0;
        s_extra = PS1ChainExtraTake(&words);
        s_extraWords = s_extra ? words : 0;
        g_ps1ChainExtraWords = s_extraWords;
    }
    if (!s_suspended)
        return;
    s_suspended = false;
    if (s_gpu)
        PS1ChainBegin(*s_gpu); // the current frame's chain starts over in the new arena
}

// The psyqo-free wrappers (gpu_chain_raw.hh)
extern psyqo::GPU *g_ps1GPU;
void PS1ChainBeginFrame() { PS1ChainBegin(*g_ps1GPU); }
void *PS1ChainClearRGB(uint8_t r, uint8_t g, uint8_t b) { return PS1ChainClear(r, g, b); }
void PS1ChainClearSetRGB(void *clear, uint8_t r, uint8_t g, uint8_t b) {
    if (clear)
        ((psyqo::Prim::FastFill *)clear)->setColor(psyqo::Color{{.r = r, .g = g, .b = b}});
}
void PS1ChainTPageRaw(uint16_t attr) {
    psyqo::PrimPieces::TPageAttr a;
    static_assert(sizeof(a) == sizeof(attr), "TPageAttr is 16 bits");
    memcpy(&a, &attr, sizeof(attr));
    PS1ChainTPage(a);
}

volatile uint32_t g_ps1VramGen = 0; // immediate GPU operations and loads (VRAM may have changed outside the chains)

void PS1GpuImmediate() {
    g_ps1VramGen = g_ps1VramGen + 1;
    if (s_gpu)
        s_gpu->waitChainIdle();
}

psyqo::Prim::FastFill *PS1ChainClear(uint8_t r, uint8_t g, uint8_t b) {
    flushBatch();
    auto *frag = allocRaw<psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill>>();
    if (!frag)
        return nullptr;
    new (frag) psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill>();
    s_gpu->getNextClear(frag->primitive, psyqo::Color{{.r = r, .g = g, .b = b}});
    s_gpu->chain(*frag);
    return &frag->primitive;
}

void PS1ChainUploadRow(const uint16_t *data, int count, int16_t x, int16_t y) {
    runHook();
    flushBatch();
    for (int off = 0; off < count; off += PS1_CLUT_CHUNK) {
        int n    = (count - off < PS1_CLUT_CHUNK) ? (count - off) : PS1_CLUT_CHUNK; // 256 = 22 x 11 + 14
        auto *fr = allocRaw<ClutChunkFragment>();
        if (!fr)
            return;
        new (&fr->upload) psyqo::Prim::VRAMUpload();
        fr->upload.region = psyqo::Rect{{(int16_t)(x + off), y}, {(int16_t)n, 1}};
        memcpy(fr->data, data + off, n * sizeof(uint16_t));
        fr->words = (unsigned)(sizeof(psyqo::Prim::VRAMUpload) / 4 + n / 2); // n is even
        s_gpu->chain(*fr);
    }
}

int PS1ChainReserveRow(int count, int16_t x, int16_t y, uint16_t **chunkData, uint16_t *chunkLen, int maxChunks) {
    runHook();
    flushBatch();
    int chunks = 0;
    for (int off = 0; off < count && chunks < maxChunks; off += PS1_CLUT_CHUNK) {
        int n    = (count - off < PS1_CLUT_CHUNK) ? (count - off) : PS1_CLUT_CHUNK;
        auto *fr = allocRaw<ClutChunkFragment>();
        if (!fr)
            return 0;
        new (&fr->upload) psyqo::Prim::VRAMUpload();
        fr->upload.region = psyqo::Rect{{(int16_t)(x + off), y}, {(int16_t)n, 1}};
        fr->words         = (unsigned)(sizeof(psyqo::Prim::VRAMUpload) / 4 + n / 2); // n is even
        s_gpu->chain(*fr);
        chunkData[chunks] = fr->data;
        chunkLen[chunks]  = (uint16_t)n;
        ++chunks;
    }
    return chunks;
}

namespace {
// GP0(80h) VRAM-to-VRAM rectangle copy (no psyqo primitive for it).
struct VRAMCopy {
    uint32_t command = 0x80000000;
    psyqo::Vertex src, dst, size;
};
static_assert(sizeof(VRAMCopy) == 16, "VRAMCopy is 4 words");
} // namespace

void PS1ChainRepeatLastFrame() {
    psyqo::Prim::FastFill next; // getNextClear() tells which buffer this frame's chain draws into
    s_gpu->getNextClear(next, psyqo::Color{{.r = 0, .g = 0, .b = 0}});
    VRAMCopy c;
    c.dst  = next.rect.pos;
    c.src  = psyqo::Vertex{{.x = next.rect.pos.x, .y = (int16_t)(next.rect.pos.y ? 0 : 256)}};
    c.size = next.rect.size;
    chainSimple(c);
}

void PS1ChainFlushCache() {
    flushBatch();
    chainSimple(psyqo::Prim::FlushCache());
}

void PS1ChainUploadCLUT(const uint16_t *clut256, int16_t x, int16_t y) {
    PS1ChainUploadRow(clut256, 256, x, y);
    // The GPU caches the CLUT: flush after changing it at the same VRAM location.
    PS1ChainFlushCache();
}

void PS1ChainTPage(psyqo::PrimPieces::TPageAttr attr) {
    runHook();
    uint16_t raw;
    static_assert(sizeof(attr) == sizeof(raw), "TPageAttr is 16 bits");
    memcpy(&raw, &attr, sizeof(raw));
    if (s_tpageValid && raw == s_lastTPage)
        return;
    psyqo::Prim::TPage tp;
    tp.attr = attr;
    chainSimple(tp);
    s_lastTPage  = raw;
    s_tpageValid = true;
}

void PS1ChainInvalidateTPage() { s_tpageValid = false; }

bool g_ps1ChainSkipPrims = false;

void PS1ChainSprite16(const psyqo::Prim::Sprite16x16 &s) {
    if (g_ps1ChainSkipPrims)
        return;
    runHook();
    if (!s_batch || s_batch->count >= PS1_TILE_BATCH) {
        flushBatch(); // also closes an open sprite batch (keeps the draw order)
        s_batch = allocRaw<TileBatch>();
        if (!s_batch)
            return;
        new (s_batch) TileBatch();
        s_batch->count = 0;
    }
    s_batch->primitives[s_batch->count++] = s;
}

void PS1ChainSprite16Raw(uint32_t xy, uint32_t tex) {
    static const uint32_t cmd = [] {
        psyqo::Prim::Sprite16x16 s(psyqo::Color{{.r = 0x80, .g = 0x80, .b = 0x80}});
        uint32_t w;
        memcpy(&w, &s, 4);
        return w;
    }();
    if (g_ps1ChainSkipPrims)
        return;
    runHook();
    if (!s_batch || s_batch->count >= PS1_TILE_BATCH) {
        flushBatch();
        s_batch = allocRaw<TileBatch>();
        if (!s_batch)
            return;
        new (s_batch) TileBatch();
        s_batch->count = 0;
    }
    uint32_t *w = reinterpret_cast<uint32_t *>(&s_batch->primitives[s_batch->count++]);
    w[0]        = cmd;
    w[1]        = xy;
    w[2]        = tex;
}

void PS1ChainSprite16RawN(const uint32_t *xy, const uint32_t *tex, int n) {
    static uint32_t cmd = 0;
    if (!cmd) {
        psyqo::Prim::Sprite16x16 s(psyqo::Color{{.r = 0x80, .g = 0x80, .b = 0x80}});
        memcpy(&cmd, &s, 4);
    }
    if (g_ps1ChainSkipPrims || n <= 0)
        return;
    runHook();
    flushBatch();
    while (n > 0) {
        int k = n < PS1_TILE_BATCH ? n : PS1_TILE_BATCH;
        VarFragment *f = allocVar(3 * k);
        if (!f)
            return;
        uint32_t *w = f->data;
        for (int i = 0; i < k; ++i) {
            w[0] = cmd;
            w[1] = *xy++;
            w[2] = *tex++;
            w += 3;
        }
        s_gpu->chain(*f);
        n -= k;
    }
}

void PS1ChainSpriteRawN(const uint32_t *words, int n) {
    static uint32_t cmd = 0;
    if (!cmd) {
        psyqo::Prim::Sprite s(psyqo::Color{{.r = 0x80, .g = 0x80, .b = 0x80}});
        memcpy(&cmd, &s, 4);
    }
    if (g_ps1ChainSkipPrims || n <= 0)
        return;
    runHook();
    flushBatch();
    while (n > 0) {
        int k = n < PS1_SPRITE_BATCH ? n : PS1_SPRITE_BATCH;
        VarFragment *f = allocVar(4 * k);
        if (!f)
            return;
        uint32_t *w = f->data;
        for (int i = 0; i < k; ++i) {
            w[0] = cmd;
            w[1] = words[0];
            w[2] = words[1];
            w[3] = words[2];
            w += 4;
            words += 3;
        }
        s_gpu->chain(*f);
        n -= k;
    }
}

static VarFragment *s_rawFrag = nullptr;
uint32_t *PS1ChainRawBegin(int words) {
    if (g_ps1ChainSkipPrims || words <= 0 || words > PS1_CHAIN_MAX_WORDS)
        return nullptr;
    runHook();
    flushBatch();
    s_rawFrag = allocVar(words);
    return s_rawFrag ? s_rawFrag->data : nullptr;
}

void PS1ChainRawEnd() {
    if (s_rawFrag)
        s_gpu->chain(*s_rawFrag);
    s_rawFrag    = nullptr;
    s_tpageValid = false;
}

static uint32_t *s_span = nullptr;
static int s_spanLastWords = 0;
// The last fragment of a span as psyqo's chain(first, last) sees it (psyqo writes its header when the next chain comes)
struct SpanFrag {
    uintptr_t head;
    size_t getActualFragmentSize() const { return (size_t)s_spanLastWords; }
};

namespace {
// An empty fragment (a DMA node of 0 words, as an ordering table's): a place in the chain to splice into later
struct SlotFrag {
    uintptr_t head;
    size_t getActualFragmentSize() const { return 0; }
};

uint32_t *s_slot = nullptr; // this frame's slot (PS1ChainBegin clears it)
} // namespace

bool PS1ChainSlot() {
    runHook();
    flushBatch();
    auto *frag = allocRaw<SlotFrag>();
    s_slot     = nullptr;
    if (!frag)
        return false;
    s_gpu->chain(*frag);
    s_slot = reinterpret_cast<uint32_t *>(&frag->head);
    return true;
}

bool PS1ChainHasSlot() { return s_slot != nullptr; }

uint32_t *PS1ChainDetachedBegin(int maxWords) {
    if (maxWords <= 0)
        return nullptr;
    flushBatch();
    arenaReady();
    if (!s_cur || !roomFor(maxWords)) {
        g_ps1ChainOverflow = g_ps1ChainOverflow + 1;
        return nullptr;
    }
    return s_cur;
}

bool PS1ChainSplice(uint32_t *first, uint32_t *lastHeader, int lastWords, uint32_t *end) {
    uint32_t *slot = s_slot;
    if (!slot || !first || !lastHeader || lastWords <= 0 || lastWords > PS1_CHAIN_MAX_WORDS || end <= lastHeader)
        return false;
    // the slot's header was written when the next fragment was chained (psyqo: its size, 0, and that address)
    *lastHeader = (uint32_t)lastWords << 24 | (*slot & 0xFFFFFF);
    *slot       = (uint32_t)((uintptr_t)first & 0xFFFFFF);
    s_cur       = end;
    s_slot      = nullptr; // one splice a slot
    return true;
}

uint32_t *PS1ChainSpanBegin(int maxWords) {
    s_span = nullptr;
    if (g_ps1ChainSkipPrims || maxWords <= 0)
        return nullptr;
    runHook();
    flushBatch();
    arenaReady();
    if (!s_cur || !roomFor(maxWords)) {
        g_ps1ChainOverflow = g_ps1ChainOverflow + 1;
        return nullptr;
    }
    s_span = s_cur;
    return s_cur;
}

void PS1ChainSpanEnd(uint32_t *lastHeader, int lastWords, uint32_t *end) {
    if (!s_span || !lastHeader || lastWords <= 0 || lastWords > PS1_CHAIN_MAX_WORDS || end <= lastHeader) {
        s_span = nullptr;
        return;
    }
    s_spanLastWords = lastWords;
    s_gpu->chain(reinterpret_cast<SpanFrag *>(s_span), reinterpret_cast<SpanFrag *>(lastHeader));
    s_cur        = end;
    s_span       = nullptr;
    s_tpageValid = false;
}

// 9f: arena words the caller links itself (an ordering table and its primitives, PS1Scene3D.cpp): reserved, not chained
uint32_t *PS1ChainAlloc(int words) {
    if (g_ps1ChainSkipPrims || words <= 0)
        return nullptr;
    runHook();
    flushBatch();
    arenaReady();
    if (!s_cur || !roomFor(words)) {
        g_ps1ChainOverflow = g_ps1ChainOverflow + 1;
        return nullptr;
    }
    uint32_t *p = s_cur;
    s_cur += words;
    return p;
}
// the words from `end` on, of the last PS1ChainAlloc, back (nothing reserved since)
void PS1ChainAllocTrim(uint32_t *end) {
    if (end && end < s_cur && end >= s_base)
        s_cur = end;
}
// a list of nodes in reserved words into the chain: first's header starts it, last (lastWords words) ends it
void PS1ChainLinkList(uint32_t *first, uint32_t *last, int lastWords) {
    if (!first || !last || lastWords <= 0 || lastWords > PS1_CHAIN_MAX_WORDS)
        return;
    s_spanLastWords = lastWords;
    s_gpu->chain(reinterpret_cast<SpanFrag *>(first), reinterpret_cast<SpanFrag *>(last));
    s_tpageValid = false;
}

static uint32_t *s_bulk = nullptr;
static int s_bulkWpp = 0;
// A bulk primitive as psyqo's chain(first, last) sees it: the DMA header word, then the words.
struct BulkFrag {
    uintptr_t head;
    size_t getActualFragmentSize() const { return (size_t)s_bulkWpp; }
};

uint32_t *PS1ChainBulkBegin(int n, int wpp) {
    s_bulk = nullptr;
    if (g_ps1ChainSkipPrims || n <= 0 || wpp <= 0 || wpp > PS1_CHAIN_MAX_WORDS)
        return nullptr;
    runHook();
    flushBatch();
    arenaReady();
    if (!s_cur || !roomFor(n * (wpp + 1))) {
        g_ps1ChainOverflow = g_ps1ChainOverflow + 1;
        return nullptr;
    }
    s_bulk    = s_cur;
    s_bulkWpp = wpp;
    return s_cur + 1;
}

void PS1ChainBulkEnd(int used) {
    if (!s_bulk || used <= 0) {
        s_bulk = nullptr;
        return;
    }
    uint32_t stride = (uint32_t)s_bulkWpp + 1, size = (uint32_t)s_bulkWpp << 24;
    uint32_t *h = s_bulk;
    for (int k = 0; k < used - 1; ++k, h += stride) // DMA linked-list headers: size << 24 | next packet address
        *h = size | (reinterpret_cast<uintptr_t>(h + stride) & 0xffffff);
    s_gpu->chain(reinterpret_cast<BulkFrag *>(s_bulk), reinterpret_cast<BulkFrag *>(h));
    s_cur        = s_bulk + used * stride;
    s_bulk       = nullptr;
    s_tpageValid = false;
}

void PS1ChainPrimsRawN(const uint32_t *words, int wpp, int n) {
    if (g_ps1ChainSkipPrims || n <= 0 || wpp <= 0 || wpp > PS1_CHAIN_MAX_WORDS)
        return;
    runHook();
    flushBatch();
    int per = PS1_CHAIN_MAX_WORDS / wpp;
    while (n > 0) {
        int k = n < per ? n : per;
        VarFragment *f = allocVar(wpp * k);
        if (!f)
            return;
        memcpy(f->data, words, wpp * k * 4);
        s_gpu->chain(*f);
        words += wpp * k;
        n -= k;
    }
    s_tpageValid = false;
}

void PS1ChainSprite(const psyqo::Prim::Sprite &s) {
    if (g_ps1ChainSkipPrims)
        return;
    runHook();
    if (!s_sbatch || s_sbatch->count >= PS1_SPRITE_BATCH) {
        flushBatch();
        s_sbatch = allocRaw<SpriteBatch>();
        if (!s_sbatch)
            return;
        new (s_sbatch) SpriteBatch();
        s_sbatch->count = 0;
    }
    s_sbatch->primitives[s_sbatch->count++] = s;
}

void PS1ChainRect(const psyqo::Prim::Rectangle &r) {
    if (!g_ps1ChainSkipPrims)
        chainSimple(r);
}

void PS1ChainQuad(const psyqo::Prim::TexturedQuad &q) {
    if (g_ps1ChainSkipPrims)
        return;
    chainSimple(q);
    s_tpageValid = false; // a textured polygon's texpage attribute changes the GPU draw mode
}

uint32_t *PS1ChainMark() {
    flushBatch();
    return s_cur;
}

int PS1ChainReplay(uint32_t *from, uint32_t *to) {
    flushBatch();
    int words = 0;
    uint32_t *p = from;
    for (int guard = 0; p && p != to && p >= s_base && p < s_end && guard < 4096; ++guard) {
        uint32_t head = *p;
        unsigned n    = head >> 24;
        uint32_t *next = reinterpret_cast<uint32_t *>(0x80000000u | (head & 0xffffff));
        if (n && n <= PS1_CHAIN_MAX_WORDS && (p[1] >> 24) != 0xA0) { // skip CPU->VRAM uploads (CLUT rows)
            auto *f = allocVar(n);
            if (!f)
                break;
            memcpy(f->data, p + 1, n * 4);
            s_gpu->chain(*f);
            words += n + 1;
        }
        p = next;
    }
    g_ps1ChainReplayWords = g_ps1ChainReplayWords + words;
    s_tpageValid = false;
    return words;
}

// Screen Y extent of the GP0 packets in n words (psx-spx "GPU Render Polygon / Rectangle Commands").
// Returns false when the words hold no drawing packet (state commands only): always replayed.
static bool packetsYRange(const uint32_t *w, unsigned n, int &ymin, int &ymax) {
    bool drawn = false;
    ymin = 0x7FFF, ymax = -0x8000;
    auto yOf = [](uint32_t xy) { return (int)(int16_t)(xy >> 16); };
    unsigned i = 0;
    while (i < n) {
        uint32_t cmd = w[i] >> 24;
        if (cmd >= 0x20 && cmd < 0x40) { // polygon: 3/4 vertices, [color], xy, [uv]
            bool quad = cmd & 0x08, tex = cmd & 0x04, gouraud = cmd & 0x10;
            unsigned verts = quad ? 4 : 3, k = i + 1;
            for (unsigned v = 0; v < verts && k < n; ++v) {
                if (v && gouraud)
                    ++k; // vertex colour
                int y = yOf(w[k++]);
                ymin = y < ymin ? y : ymin;
                ymax = y > ymax ? y : ymax;
                if (tex)
                    ++k;
            }
            drawn = true;
            i = k;
        } else if (cmd >= 0x60 && cmd < 0x80) { // rectangle: color, xy, [uv], [wh]
            bool tex = cmd & 0x04;
            unsigned size = (cmd >> 3) & 3, k = i + 1;
            int y = yOf(w[k++]);
            if (tex)
                ++k;
            int h = size == 0 ? (k < n ? (int)(w[k++] >> 16) : 0) : size == 1 ? 1 : size == 2 ? 8 : 16;
            ymin = y < ymin ? y : ymin;
            ymax = y + h - 1 > ymax ? y + h - 1 : ymax;
            drawn = true;
            i = k;
        } else {
            return drawn ? true : false; // state / other packets end the scan (kept)
        }
    }
    return drawn;
}

int PS1ChainReplayLines(uint32_t *from, uint32_t *to, int y0, int y1) {
    flushBatch();
    int words = 0;
    uint32_t *p = from;
    for (int guard = 0; p && p != to && p >= s_base && p < s_end && guard < 4096; ++guard) {
        uint32_t head = *p;
        unsigned n    = head >> 24;
        uint32_t *next = reinterpret_cast<uint32_t *>(0x80000000u | (head & 0xffffff));
        int ymin, ymax;
        bool keep = n && n <= PS1_CHAIN_MAX_WORDS && (p[1] >> 24) != 0xA0 // not a CPU->VRAM upload (CLUT rows)
                    && (!packetsYRange(p + 1, n, ymin, ymax) || (ymax >= y0 && ymin < y1));
        if (keep) {
            auto *f = allocVar(n);
            if (!f)
                break;
            memcpy(f->data, p + 1, n * 4);
            s_gpu->chain(*f);
            words += n + 1;
        }
        p = next;
    }
    g_ps1ChainReplayWords = g_ps1ChainReplayWords + words;
    s_tpageValid = false;
    return words;
}

static int16_t chainBufferY() { // the buffer this frame's chain draws into (psyqo getNextScissor)
    return s_gpu->getParity() ? 0 : 256;
}

int PS1ChainBufferY() { return s_gpu ? chainBufferY() : 0; }

void PS1ChainDrawArea(int x0, int y0, int x1, int y1) {
    int16_t by = chainBufferY();
    chainSimple(psyqo::Prim::DrawingAreaStart(psyqo::Vertex{{.x = (int16_t)x0, .y = (int16_t)(by + y0)}}));
    chainSimple(psyqo::Prim::DrawingAreaEnd(psyqo::Vertex{{.x = (int16_t)x1, .y = (int16_t)(by + y1)}}));
}

void PS1ChainDrawAreaReset() { PS1ChainDrawArea(0, 0, 320, 240); }

void PS1ChainFillLines(int y0, int y1, uint8_t r, uint8_t g, uint8_t b) {
    psyqo::Prim::FastFill f(psyqo::Color{{.r = r, .g = g, .b = b}});
    f.rect = psyqo::Rect{psyqo::Vertex{{.x = 0, .y = (int16_t)(chainBufferY() + y0)}}, psyqo::Vertex{{.w = 320, .h = (int16_t)(y1 - y0)}}};
    chainSimple(f);
}

void PS1ChainVramCopy(int sx, int sy, int dx, int dy, int w, int h) {
    VRAMCopy c;
    c.src  = psyqo::Vertex{{.x = (int16_t)sx, .y = (int16_t)sy}};
    c.dst  = psyqo::Vertex{{.x = (int16_t)dx, .y = (int16_t)dy}};
    c.size = psyqo::Vertex{{.w = (int16_t)w, .h = (int16_t)h}};
    chainSimple(c);
}

void PS1ChainCopyLastFrameLines(int y0, int y1) {
    int16_t by = chainBufferY();
    VRAMCopy c;
    c.dst  = psyqo::Vertex{{.x = 0, .y = (int16_t)(by + y0)}};
    c.src  = psyqo::Vertex{{.x = 0, .y = (int16_t)((by ? 0 : 256) + y0)}};
    c.size = psyqo::Vertex{{.w = 320, .h = (int16_t)(y1 - y0)}};
    chainSimple(c);
}

void PS1ChainMaskControl(bool forceSet, bool test) {
    chainSimple(psyqo::Prim::MaskControl(forceSet ? psyqo::Prim::MaskControl::ForceSet : psyqo::Prim::MaskControl::FromSource,
                                         test ? psyqo::Prim::MaskControl::Test::Yes : psyqo::Prim::MaskControl::Test::No));
}

void PS1ChainSetHook(void (*fn)()) { s_hook = fn; }
void PS1ChainRunHook() { runHook(); }
