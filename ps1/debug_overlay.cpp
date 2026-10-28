// docs/ports/mania/32 "Debug overlay": `make DEBUG_OVERLAY=1` (a debug build: `make dist` and tools/export_builder.sh refuse
// it; the natural EXE has none of this). The top-right corner shows, from the port's own counters,
//   FPS 60/60        frames shown in the last second (one per frame(), i.e. psyqo flips) / the video mode's rate
//   RAM 1934/2048K   main RAM in use / the console's (PS1_DBG_RAM_KB): the kernel's 64 KB, the EXE (text, data, bss),
//                    the stack reserve, psyqo's heap blocks (counted by the --wrap wrappers below) and the STG dataset's
//                    used part, its overlay code and tail blocks; free = psyqo's free bytes + STG's free middle + the
//                    chain's extra block (STG lent per scene, given back before each load: counted as free)
// L2 + R2 together hides / shows it (both outside the port's 12-button map, ps1/rsdk5_main.cpp readPad: the game never
// sees them); shown at boot. GDB: g_ps1DbgVisible, g_ps1DbgLog.
//
// No VRAM: the text is opaque monochrome rectangles (GP0 60h) only, drawn last into the frame's draw buffer like any
// other primitive. No texture, CLUT or texpage, no E1-E6 setting, no semi-transparency: the GPU state the game relies on
// is never touched. The glyphs come from tools/overlay_font.py's table (debug_overlay_font.inc, .rodata): each a few
// rectangles, some of them "holes" in the box colour (see the script). The primitives live in s_list (BSS, sized for the
// worst case at compile time), rebuilt once a second and linked into every frame's chain as they are: no arena words,
// no per-frame formatting. The values are recomputed once a second too (one window = refresh-rate vblanks): cheaper,
// and steadier to read than per-frame numbers.
//
// Once a second, also while hidden, a [PERF] line goes to the TTY (BIOS write: PCSX-Redux's log, DuckStation's TTY
// channel) with the same values, overlay_vram=0, the primitive count and the list's size.

// psyqo first: the engine's headers bring the port's one-argument assert (ps1/rsdk5_main.cpp's order)
#include <psyqo/advancedpad.hh>
#include <psyqo/gpu.hh>

#include "common/syscalls/syscalls.h" // syscall_write

#include "gpu_chain_raw.hh"
#include "pad.hh"
#include "stack.hh" // PS1_STACK_RESERVE, g_ps1StackUnused
#include "video.hh" // PS1Hblanks

#include "RSDK/Core/RetroEngine.hpp" // dataStorage, ps1StgCapacity

#ifndef PS1_DBG_RAM_KB
#define PS1_DBG_RAM_KB 2048 // the console's RAM (a modded one: make DEBUG_OVERLAY=1 EXTRA_CPPFLAGS=-DPS1_DBG_RAM_KB=8192)
#endif

extern psyqo::GPU *g_ps1GPU; // ps1/rsdk5_main.cpp
extern PS1Pad *g_ps1Pad;
extern "C" {
extern volatile uint32_t g_ps1StepsTotal; // ps1/rsdk5_main.cpp: logic steps run
}
extern volatile uint32_t g_ps1LoadingLoads; // ps1/loading.cpp: loads begun
extern "C" {
extern volatile uint32_t g_ps1ReserveEnd, g_ps1ReserveHigh; // RSDK/Storage/Storage.cpp, ps1/libc/stdlib.cpp
// nugget/ps-exe.ld: text, then rodata + data, then bss; the heap from bss's end to the RAM top (the stack's top)
extern char __ftext_start, __text_end, __fdata_start, __data_end, __bss_start, __bss_end, __heap_start, __stack_start;

// --- psyqo's heap (nugget/common/libc/alloc.c): every block it hands out and takes back, through the linker's --wrap
// (DEBUG_OVERLAY=1's LDFLAGS: libc_malloc / free / realloc, operator delete; operator new reaches alloc.c's own
// libc_malloc, so ps1/libc/new_guard.cpp reports those blocks). A block's size word sits just before it (alloc.c's
// allocated_block: its 8-byte header included), so the count is the heap's real use, rounding and headers too.
void *__real_libc_malloc(size_t size);
void __real_libc_free(void *ptr);
void *__real_libc_realloc(void *ptr, size_t size);
volatile uint32_t g_ps1DbgHeapLive = 0, g_ps1DbgHeapPeak = 0; // bytes in psyqo blocks (STG's and the guard's included)
}
namespace {
inline uint32_t blockBytes(void *p) { return ((const uint32_t *)p)[-1]; }
inline void heapAdd(uint32_t bytes)
{
    uint32_t live    = g_ps1DbgHeapLive + bytes;
    g_ps1DbgHeapLive = live;
    if (live > g_ps1DbgHeapPeak)
        g_ps1DbgHeapPeak = live;
}
} // namespace
extern "C" {
void *__wrap_libc_malloc(size_t size)
{
    void *p = __real_libc_malloc(size);
    if (p)
        heapAdd(blockBytes(p));
    return p;
}
void __wrap_libc_free(void *ptr)
{
    if (ptr)
        g_ps1DbgHeapLive = g_ps1DbgHeapLive - blockBytes(ptr);
    __real_libc_free(ptr);
}
void *__wrap_libc_realloc(void *ptr, size_t size)
{
    uint32_t old = ptr ? blockBytes(ptr) : 0;
    void *p      = __real_libc_realloc(ptr, size);
    if (p) {
        g_ps1DbgHeapLive = g_ps1DbgHeapLive - old;
        heapAdd(blockBytes(p));
    }
    else if (ptr && !size) { // realloc(p, 0) frees
        g_ps1DbgHeapLive = g_ps1DbgHeapLive - old;
    }
    return p;
}
void PS1DbgHeapNew(void *p)
{
    if (p)
        heapAdd(blockBytes(p));
}
void __wrap__ZdlPv(void *p) { __wrap_libc_free(p); }
void __wrap__ZdaPv(void *p) { __wrap_libc_free(p); }
void __wrap__ZdlPvj(void *p, unsigned int) { __wrap_libc_free(p); }
void __wrap__ZdaPvj(void *p, unsigned int) { __wrap_libc_free(p); }
}

extern "C" {
volatile uint32_t g_ps1DbgVisible = 1; // drawn (L2 + R2 toggles; GDB may poke it)
volatile uint32_t g_ps1DbgLog     = 1; // the [PERF] line once a second
// the last window's values (GDB reads them on emulators without a TTY log)
volatile uint32_t g_ps1DbgSeconds = 0, g_ps1DbgFps = 0, g_ps1DbgTarget = 0, g_ps1DbgLogic = 0;
// main RAM (bytes): in use now, the most seen at a frame's end, and its parts at the last window
volatile uint32_t g_ps1DbgRamUsed = 0, g_ps1DbgRamPeak = 0;
volatile uint32_t g_ps1DbgHeap = 0, g_ps1DbgHeapTop = 0, g_ps1DbgStg = 0, g_ps1DbgStgFree = 0, g_ps1DbgLent = 0;
volatile uint32_t g_ps1DbgPrims    = 0; // rectangles in the list (the box included)
volatile uint32_t g_ps1DbgBadPrims = 0; // list words that are not a GP0 60h rectangle (must stay 0)
// hblanks the overlay took: the last frame, the worst; the last frame that measured, the last that rebuilt the list, the
// worst that formatted or wrote the log line: the second's work is spread over consecutive frames, a part each
volatile uint32_t g_ps1DbgHbl = 0, g_ps1DbgHblMax = 0, g_ps1DbgHblMeasure = 0, g_ps1DbgHblRebuild = 0, g_ps1DbgHblLog = 0;
}

namespace {

#include "debug_overlay_font.inc"

// --- the font's compile-time check: each glyph's rectangles, drawn in order, give its bitmap; all inside the 5x7 cell
constexpr bool fontMatches()
{
    for (int g = 0; g < kDbgGlyphCount; ++g) {
        uint8_t px[7] = {};
        for (int r = kDbgGlyphStart[g]; r < kDbgGlyphStart[g + 1]; ++r) {
            uint16_t v = kDbgGlyphRects[r];
            int x = v & 7, y = v >> 3 & 7, w = (v >> 6 & 7) + 1, h = (v >> 9 & 7) + 1;
            if (x + w > 5 || y + h > 7)
                return false;
            for (int yy = y; yy < y + h; ++yy)
                for (int xx = x; xx < x + w; ++xx)
                    px[yy] = v >> 12 & 1 ? px[yy] & ~(0x10 >> xx) : px[yy] | 0x10 >> xx;
        }
        for (int r = 0; r < 7; ++r)
            if (px[r] != kDbgGlyphBits[g][r])
                return false;
    }
    return true;
}
static_assert(fontMatches(), "debug_overlay_font.inc does not draw its bitmaps: rerun tools/overlay_font.py");

constexpr int glyphOf(char c)
{
    for (int g = 0; g < kDbgGlyphCount; ++g)
        if (kDbgGlyphChars[g] == c)
            return g;
    return kDbgGlyphCount - 1; // ' '
}
struct GlyphIndex {
    uint8_t of[96]; // ' ' .. 0x7F
};
constexpr GlyphIndex makeGlyphIndex()
{
    GlyphIndex t{};
    for (int c = 0; c < 96; ++c) t.of[c] = (uint8_t)glyphOf((char)(32 + c));
    return t;
}
constexpr GlyphIndex kGlyphIndex = makeGlyphIndex();
constexpr int rectsOf(char c) { return kDbgGlyphStart[glyphOf(c) + 1] - kDbgGlyphStart[glyphOf(c)]; }
constexpr int rectsOf(const char *s)
{
    int n = 0;
    while (*s) n += rectsOf(*s++);
    return n;
}
constexpr int digitsOf(uint32_t v)
{
    int d = 1;
    while (v >= 10) v /= 10, ++d;
    return d;
}
constexpr int rectsOfNum(uint32_t v)
{
    int n = 0;
    do n += rectsOf((char)('0' + v % 10));
    while (v /= 10);
    return n;
}
constexpr int maxDigitRects()
{
    int m = 0;
    for (char c = '0'; c <= '9'; ++c) m = rectsOf(c) > m ? rectsOf(c) : m;
    return m;
}

// --- layout: 5x7 glyphs in 6 x 8 cells, a 2-pixel margin inside the box, the box's top-right corner at (304, 16): inside
// the TV's overscan, clear of Mania's HUD and PERF_HUD (both top-left)
constexpr int CELL = 6, PITCH = 8, MARGIN = 2, RIGHT = 304, TOP = 16;
constexpr uint32_t BOX_BGR = 0x000000, TEXT_BGR = 0xF0F0F0; // GP0 colours (BGR)
constexpr int FPS_W = 2;                                     // shown frames a second: up to 99
constexpr uint32_t RAM_KB = PS1_DBG_RAM_KB;
constexpr int RAM_W       = digitsOf(RAM_KB); // the used field as wide as the total
// the lines' fixed text and their number fields (each field right-aligned in its width)
constexpr int LINES = 2;
constexpr int LINE_CHARS[LINES] = { 4 + FPS_W + 1 + 2,                      // "FPS ff/tt"
                                    4 + RAM_W + 1 + digitsOf(RAM_KB) + 1 }; // "RAM uuuu/2048K"
constexpr int maxChars()
{
    int m = 0;
    for (int l = 0; l < LINES; ++l) m = LINE_CHARS[l] > m ? LINE_CHARS[l] : m;
    return m;
}
constexpr int BOX_W = maxChars() * CELL - 1 + 2 * MARGIN, BOX_H = LINES * PITCH - 1 + 2 * MARGIN;
constexpr int BOX_X = RIGHT - BOX_W, BOX_Y = TOP;

// the worst case: every variable digit its costliest glyph, every fixed part as drawn (the targets 60 / 50 alike)
constexpr int MAX_RECTS = 1 + rectsOf("FPS /") + FPS_W * maxDigitRects() + (rectsOf("60") > rectsOf("50") ? rectsOf("60") : rectsOf("50")) +
                          rectsOf("RAM /K") + RAM_W * maxDigitRects() + rectsOfNum(RAM_KB);
constexpr int PER_FRAG  = 4;                                      // 12 words: fragments stay <= 14 (ps1/gpu_chain.cpp)
constexpr int FRAGS     = (MAX_RECTS + PER_FRAG - 1) / PER_FRAG;
constexpr int LIST_WORDS = FRAGS + 3 * MAX_RECTS;                // a header a fragment, 3 words a rectangle

uint32_t s_list[LIST_WORDS];
uint32_t *s_last    = nullptr; // the list's last fragment header (psyqo writes it when the next fragment is chained)
int s_lastWords     = 0;
char s_text[LINES][maxChars() + 1];
bool s_dirty        = true;
bool s_started      = false;
bool s_comboHeld    = false;
// the [PERF] line: formatted on one frame, written over the next ones in chunks (the TTY write is the slow part)
constexpr int LOG_CHUNK = 96;
char s_log[400];
int s_logLen = 0, s_logPos = 0;
int s_logStep = 0; // 0: nothing to do, 1: to format, 2: writing
uint32_t s_winVbl = 0xFFFFFFFF, s_winFrames = 0, s_winSteps = 0, s_winLoads = 0;

// v right-aligned in `width` cells (blank cells before it); clamped to what fits
char *putNum(char *p, uint32_t v, int width)
{
    uint32_t lim = 1;
    for (int i = 0; i < width; ++i) lim *= 10;
    if (v >= lim)
        v = lim - 1;
    char d[10];
    int n = 0;
    do d[n++] = (char)('0' + v % 10);
    while ((v /= 10) && n < 10);
    for (int i = n; i < width; ++i) *p++ = ' ';
    while (n) *p++ = d[--n];
    return p;
}
constexpr uint32_t kb(uint32_t bytes) { return (bytes + 512) / 1024; }
constexpr uint32_t pct(uint32_t part, uint32_t whole) { return whole ? (part / 64 * 100 + whole / 128) / (whole / 64) : 0; }

// Main RAM in use (see the top): what the EXE's link spans (kernel + EXE + heap up to the RAM top) less what is free in
// psyqo's heap and in STG. With `parts`, its parts for the window's values too.
uint32_t ramUsed(bool parts)
{
    const RSDK::DataStorage *st = &RSDK::dataStorage[RSDK::DATASET_STG];
    uint32_t span     = (uint32_t)(uintptr_t)&__stack_start - 0x80000000;
    // the heap as alloc.c lays it out at its first malloc: from __heap_start aligned to 8, up to the RAM top less one
    // 8-byte block header
    uint32_t start    = ((uint32_t)(uintptr_t)&__heap_start + 7) & ~7u, live = g_ps1DbgHeapLive;
    uint32_t heapSpan = ((uint32_t)(uintptr_t)&__stack_start - start - 8 + 7) & ~7u;
    uint32_t heapFree = heapSpan > live ? heapSpan - live : 0;
    uint32_t stgUsed  = st->usedStorage * sizeof(uint32_t);
    uint32_t stgFree  = st->storageLimit > stgUsed ? st->storageLimit - stgUsed : 0;
    uint32_t lent     = PS1ChainExtraBytes();
    uint32_t used     = span - heapFree - stgFree - lent;
    if (parts && st->memoryTable) {
        // STG and the guard block over the stack reserve are psyqo blocks too (RSDK/Storage/Storage.cpp 2.4a)
        uint32_t stgBlock = blockBytes(st->memoryTable);
        uint32_t guard    = (uint32_t)(&__stack_start - ((char *)st->memoryTable - 8 + stgBlock));
        g_ps1DbgHeap      = live - stgBlock - guard;
        g_ps1DbgHeapTop   = g_ps1DbgHeapPeak - stgBlock - guard;
        g_ps1DbgStg       = stgUsed + (RSDK::ps1StgCapacity - st->storageLimit) - lent; // used + overlay code + tail
        g_ps1DbgStgFree   = stgFree;
        g_ps1DbgLent      = lent;
    }
    return used;
}

char *putStr(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

void format()
{
    char *p = s_text[0];
    p       = putStr(p, "FPS ");
    p       = putNum(p, g_ps1DbgFps, FPS_W);
    *p++    = '/';
    p       = putNum(p, g_ps1DbgTarget, digitsOf(g_ps1DbgTarget));
    *p      = 0;
    p       = putStr(s_text[1], "RAM ");
    p       = putNum(p, kb(g_ps1DbgRamUsed), RAM_W);
    *p++    = '/';
    p       = putNum(p, RAM_KB, digitsOf(RAM_KB));
    p       = putStr(p, "K");
    *p      = 0;
}

// The list from s_text: the box, then each glyph's rectangles, four to a fragment (headers linking each to the next; the
// last one's psyqo writes). Only after PS1ChainStaticBegin: the last frame's chain no longer reads these words.
struct Writer {
    uint32_t *w, *hdr;
    int k, n;
    void rect(uint32_t bgr, int x, int y, int wd, int ht)
    {
        if (!hdr || k == PER_FRAG) {
            if (hdr)
                *hdr = (uint32_t)(3 * k) << 24 | ((uintptr_t)w & 0xFFFFFF);
            hdr = w++;
            k   = 0;
        }
        w[0] = 0x60000000 | bgr;
        w[1] = (uint32_t)y << 16 | (uint16_t)x;
        w[2] = (uint32_t)ht << 16 | (uint16_t)wd;
        w += 3, ++k, ++n;
    }
};

void rebuild()
{
    Writer o = { s_list, nullptr, 0, 0 };
    o.rect(BOX_BGR, BOX_X, BOX_Y, BOX_W, BOX_H);
    for (int l = 0; l < LINES; ++l) {
        int x = BOX_X + MARGIN, y = BOX_Y + MARGIN + l * PITCH;
        for (const char *c = s_text[l]; *c; ++c, x += CELL) {
            unsigned i = (unsigned)(uint8_t)*c - 32;
            int g      = i < 96 ? kGlyphIndex.of[i] : kDbgGlyphCount - 1;
            for (int r = kDbgGlyphStart[g]; r < kDbgGlyphStart[g + 1]; ++r) {
                uint16_t v = kDbgGlyphRects[r];
                o.rect(v >> 12 & 1 ? BOX_BGR : TEXT_BGR, x + (v & 7), y + (v >> 3 & 7), (v >> 6 & 7) + 1, (v >> 9 & 7) + 1);
            }
        }
    }
    s_last      = o.hdr;
    s_lastWords = 3 * o.k;
    g_ps1DbgPrims = (uint32_t)o.n;
    uint32_t bad  = 0;
    for (uint32_t *f = s_list; f; f = f == s_last ? nullptr : (uint32_t *)(0x80000000 | (*f & 0xFFFFFF))) {
        int words = f == s_last ? s_lastWords : (int)(*f >> 24);
        for (int i = 0; i < words; i += 3)
            bad += (f[1 + i] >> 24) != 0x60;
    }
    g_ps1DbgBadPrims = bad;
}

// L2 + R2 pressed together (the pair's rising edge) toggles the overlay
void pollToggle()
{
    using Pad = psyqo::AdvancedPad;
    bool both = g_ps1Pad && g_ps1Pad->isPadConnected(Pad::Pad::Pad1a) && g_ps1Pad->isButtonPressed(Pad::Pad::Pad1a, Pad::L2) &&
                g_ps1Pad->isButtonPressed(Pad::Pad::Pad1a, Pad::R2);
    if (both && !s_comboHeld)
        g_ps1DbgVisible = !g_ps1DbgVisible;
    s_comboHeld = both;
}

// The window's values (vblanks >= the refresh rate): frames and logic steps a second, rounded
void measure(uint32_t vblanks, uint32_t rate)
{
    g_ps1DbgFps    = (s_winFrames * rate + vblanks / 2) / vblanks;
    g_ps1DbgLogic  = ((g_ps1StepsTotal - s_winSteps) * rate + vblanks / 2) / vblanks;
    g_ps1DbgTarget = rate;
    g_ps1DbgRamUsed = ramUsed(true);
}

// A [PERF] line builder: integers only, no printf (the port's vsnprintf, float support included, took ~15 hblanks)
struct Line {
    char *p, *end;
    Line &s(const char *t)
    {
        while (*t && p < end) *p++ = *t++;
        return *this;
    }
    Line &u(uint32_t v)
    {
        char d[10];
        int n = 0;
        do d[n++] = (char)('0' + v % 10);
        while ((v /= 10) && n < 10);
        while (n && p < end) *p++ = d[--n];
        return *this;
    }
    Line &k(const char *key, uint32_t bytes) { return s(key).u(kb(bytes)); } // " key=KB"
};

void logFormat()
{
    // (KB unless B; text / data / bss: the EXE's, data = rodata + data)
    uint32_t ram = g_ps1DbgRamUsed, rsvTop = g_ps1ReserveEnd > g_ps1ReserveHigh ? g_ps1ReserveEnd - g_ps1ReserveHigh : 0;
    Line l   = { s_log, s_log + sizeof(s_log) - 1 };
    l.s("[PERF] t=").u(g_ps1DbgSeconds).s(" fps=").u(g_ps1DbgFps).s("/").u(g_ps1DbgTarget).s(" logic=").u(g_ps1DbgLogic);
    l.k(" ram=", ram).s("/").u(RAM_KB).s("K ").u(pct(ram, RAM_KB * 1024)).s("%").k(" peak=", g_ps1DbgRamPeak);
    l.k(" kern=", (uint32_t)(uintptr_t)&__ftext_start - 0x80000000).k(" text=", (uint32_t)(&__text_end - &__ftext_start));
    l.k(" data=", (uint32_t)(&__data_end - &__fdata_start)).k(" bss=", (uint32_t)(&__bss_end - &__bss_start));
    l.k(" heap=", g_ps1DbgHeap).k(" hpeak=", g_ps1DbgHeapTop).k(" stg=", g_ps1DbgStg).k(" stgfree=", g_ps1DbgStgFree);
    l.k(" rsvtop=", rsvTop).k(" lent=", g_ps1DbgLent).k(" stack=", PS1_STACK_RESERVE).s(" su=").u(g_ps1StackUnused).s("B");
    l.s(" overlay_vram=0 overlay_prims=").u(g_ps1DbgPrims).s("/").u(MAX_RECTS).s(" overlay_buf=").u(sizeof(s_list));
    l.s("B overlay_hbl=").u(g_ps1DbgHblMeasure).s("/").u(g_ps1DbgHblRebuild).s("/").u(g_ps1DbgHblLog);
    l.s("/").u(g_ps1DbgHblMax).s("\n");
    s_logLen = (int)(l.p - s_log);
    s_logPos = 0;
}

} // namespace

// PS1RenderEndFrame, after everything else of the frame (draw: false while a video plays). One call a frame shown.
void PS1DebugOverlayFrame(bool draw)
{
    uint32_t t0 = PS1Hblanks();
    pollToggle();
    // psyqo's vblank count, latched at each flip (the one perfTick uses; the BIOS VBlank event missed a few at boot)
    uint32_t vbl = g_ps1GPU->getFrameCount(), rate = g_ps1GPU->getRefreshRate();
    bool measured = false, rebuilt = false, logged = false;
    if (!s_started) { // (the first frame: init's blocks are in place and its 116 KB reserve placeholder freed)
        s_started        = true;
        g_ps1DbgTarget   = rate;
        g_ps1DbgHeapPeak = g_ps1DbgHeapLive;
        g_ps1DbgRamUsed  = ramUsed(true);
        format();
    }
    uint32_t ram = ramUsed(false);
    if (ram > g_ps1DbgRamPeak)
        g_ps1DbgRamPeak = ram;
    // a window starts again after a load (no frame is shown while it runs: the seconds of a load are not a frame rate)
    // (a window counts the frames shown after its first vblank, its first frame's)
    if (s_winLoads != g_ps1LoadingLoads || s_winVbl == 0xFFFFFFFF)
        s_winVbl = vbl, s_winFrames = 0, s_winSteps = g_ps1StepsTotal, s_winLoads = g_ps1LoadingLoads;
    else
        ++s_winFrames;
    if (vbl - s_winVbl >= rate) {
        measure(vbl - s_winVbl, rate);
        g_ps1DbgSeconds = g_ps1DbgSeconds + 1;
        format();
        s_dirty   = true, measured = true;
        s_logStep = g_ps1DbgLog ? 1 : 0; // (a line still being written is cut short: the newer one wins)
        s_winVbl = vbl, s_winFrames = 0, s_winSteps = g_ps1StepsTotal;
    }
    if (draw && g_ps1DbgVisible && PS1ChainStaticBegin()) {
        if (s_dirty && !measured) {
            rebuild();
            s_dirty = false, rebuilt = true;
        }
        PS1ChainLinkList(s_list, s_last, s_lastWords);
    }
    if (!measured && !rebuilt && s_logStep) { // the log line's part for this frame
        if (s_logStep == 1) {
            logFormat();
            s_logStep = 2;
        }
        else {
            int n = s_logLen - s_logPos < LOG_CHUNK ? s_logLen - s_logPos : LOG_CHUNK;
            syscall_write(1, s_log + s_logPos, n);
            s_logPos += n;
            if (s_logPos >= s_logLen)
                s_logStep = 0;
        }
        logged = true;
    }
    uint32_t hbl = PS1HblanksSince(t0);
    g_ps1DbgHbl  = hbl;
    if (hbl > g_ps1DbgHblMax)
        g_ps1DbgHblMax = hbl;
    if (measured)
        g_ps1DbgHblMeasure = hbl;
    if (rebuilt)
        g_ps1DbgHblRebuild = hbl;
    if (logged && hbl > g_ps1DbgHblLog)
        g_ps1DbgHblLog = hbl;
}
