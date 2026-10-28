// docs/ports/mania/32 "Debug overlay": `make DEBUG_OVERLAY=1` (a debug build: `make dist` and tools/export_builder.sh refuse
// it; the natural EXE has none of this). The top-right corner shows, from the port's own counters,
//   FPS 60/60        frames shown in the last second (one per frame(), i.e. psyqo flips) / the video mode's rate
//   RAM 1934/2048K   main RAM in use / the console's (PS1_DBG_RAM_KB): the kernel's 64 KB, the EXE (text, data, bss),
//                    the stack reserve, psyqo's heap blocks (counted by the --wrap wrappers below) and the STG dataset's
//                    used part, its overlay code and tail blocks; free = psyqo's free bytes + STG's free middle + the
//                    chain's extra block (STG lent per scene, given back before each load: counted as free)
//   VRAM 812/1024K   the scene's VRAM as the loads fill it (the layout is tools/gfx/vram_plan.py's, fixed per scene at
//                    build time): psyqo's two 320x240 framebuffers, the tile pages (.ptl), the atlas's default blocks
//                    and its swap regions at their largest group (.atl), the CLUTs (tiles', atlas's, bank 0's row, the
//                    INK_TINT and bank-CLUT scratch rows, the title's build stamp row); the overlay's own: 0
//   SPU 297/508K     the scene's SFX bank (PS1SfxLoadBank: its samples from 0x1000 up) / the sample area: SPU RAM's
//                    512 KB less what is reserved (ps1/spu.hh): 0x0000-0x0FFF, the capture buffers the hardware writes
//                    (CD audio input is on: XA music plays through it, no SPU RAM of its own), and the 32-byte silent
//                    block at 0x7FFE0 the idle voices loop on, which also holds the reverb work address (0x7FFF0).
//                    Reverb is off; read back each second (SPUCNT bit 7, psx-spx "SPU Reverb Registers": while it is
//                    clear the SPU writes nothing to the work area): were it on, its area, 80000h - mBASE * 8 bytes,
//                    would count as used
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
#include "spu.hh"   // PS1_SPU_SAMPLE_START / END
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
extern "C" void (*g_ps1VersionDraw)(uint32_t colour, int32_t aR, int32_t aG, int32_t aB); // ps1/version_text.cpp: the stamp's
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
// VRAM (bytes) at the last window: in all, and its parts
volatile uint32_t g_ps1DbgVram = 0, g_ps1DbgVramTiles = 0, g_ps1DbgVramAtlas = 0, g_ps1DbgVramGroups = 0, g_ps1DbgVramClut = 0;
volatile uint32_t g_ps1DbgSpu = 0, g_ps1DbgReverb = 0; // SPU RAM bytes the SFX bank takes; the reverb work area (0: off)
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
// each glyph rectangle as the rebuild uses it, one word: x | y << 16 (its place in the cell, added to the cell's GPU
// position word) | w << 3 | h << 19 (the size word after >> 3) | hole << 31
struct GlyphWords {
    uint32_t of[sizeof(kDbgGlyphRects) / sizeof(kDbgGlyphRects[0])];
};
constexpr GlyphWords makeGlyphWords()
{
    GlyphWords t{};
    for (unsigned r = 0; r < sizeof(t.of) / sizeof(t.of[0]); ++r) {
        uint32_t v = kDbgGlyphRects[r];
        t.of[r]    = (v & 7) | (v >> 3 & 7) << 16 | ((v >> 6 & 7) + 1) << 3 | ((v >> 9 & 7) + 1) << 19 | (v >> 12 & 1) << 31;
    }
    return t;
}
constexpr GlyphWords kGlyphWords = makeGlyphWords();
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
constexpr uint32_t BOX_BGR = 0x000000, TEXT_BGR = 0xF0F0F0, SLOW_BGR = 0x00F0F0; // GP0 colours (BGR): black, white, yellow
constexpr int FPS_W = 2;                                     // shown frames a second: up to 99
constexpr uint32_t RAM_KB = PS1_DBG_RAM_KB;
constexpr int RAM_W       = digitsOf(RAM_KB); // the used field as wide as the total
constexpr uint32_t VRAM_KB = 1024;            // 1024 x 512 halfwords
constexpr int VRAM_W       = digitsOf(VRAM_KB);
constexpr uint32_t SPU_AVAIL = PS1_SPU_SAMPLE_END - PS1_SPU_SAMPLE_START, SPU_RESERVED = 512 * 1024 - SPU_AVAIL; // 4,128 B
constexpr uint32_t SPU_KB    = (SPU_AVAIL + 512) / 1024;                                                     // 508
constexpr int SPU_W          = digitsOf(SPU_KB);
// the lines' fixed text and their number fields (each field right-aligned in its width)
constexpr int LINES = 4;
constexpr int LINE_CHARS[LINES] = { 4 + FPS_W + 1 + 2,                         // "FPS ff/tt"
                                    4 + RAM_W + 1 + digitsOf(RAM_KB) + 1,      // "RAM uuuu/2048K"
                                    5 + VRAM_W + 1 + digitsOf(VRAM_KB) + 1,   // "VRAM uuuu/1024K"
                                    4 + SPU_W + 1 + digitsOf(SPU_KB) + 1 };   // "SPU uuu/508K"
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
                          rectsOf("RAM /K") + RAM_W * maxDigitRects() + rectsOfNum(RAM_KB) +
                          rectsOf("VRAM /K") + VRAM_W * maxDigitRects() + rectsOfNum(VRAM_KB) +
                          rectsOf("SPU /K") + SPU_W * maxDigitRects() + rectsOfNum(SPU_KB);
constexpr int PER_FRAG  = 4;                                      // 12 words: fragments stay <= 14 (ps1/gpu_chain.cpp)
constexpr int FRAGS     = (MAX_RECTS + PER_FRAG - 1) / PER_FRAG;
constexpr int LIST_WORDS = FRAGS + 3 * MAX_RECTS;                // a header a fragment, 3 words a rectangle

uint32_t s_list[LIST_WORDS];
} // namespace
extern "C" const uint16_t g_ps1DbgBox[4] = { BOX_X, BOX_Y, BOX_W, BOX_H }; // (tools/play/overlay_ab.sh: the box's pixels)
namespace {
uint32_t *s_last    = nullptr; // the list's last fragment header (psyqo writes it when the next fragment is chained)
int s_lastWords     = 0;
char s_text[LINES][maxChars() + 1];
bool s_dirty        = true;
bool s_started      = false;
bool s_comboHeld    = false;
// the [PERF] line: formatted on one frame, written over the next ones in chunks (the TTY write is the slow part)
constexpr int LOG_CHUNK = 96;
char s_log[384]; // (a line: ~330 characters)
int s_logLen = 0, s_logPos = 0;
int s_logStep = 0; // 0: nothing to do, 1 / 2: to format (its two halves, a frame each), 3: writing
uint32_t s_lastVbl = 0;
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
constexpr uint32_t pct(uint32_t part, uint32_t whole) { return whole ? part / 64 * 100 / (whole / 64) : 0; } // rounded down

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

// The scene's VRAM (see the top). The framebuffers: psyqo's W320 x 240 pair at (0, 0) and (0, 256); a 4-bit CLUT is
// 16 entries (32 B); bank 0's CLUT (0, 240) 256 (PS1Layers.cpp PS1ClutUploadAll: the other banks' rows stay unused);
// the INK_TINT scratch row 510 x 0-319 and the bank-CLUT scratch slots 318 / 319 (row 511, x 256-287:
// RSDK/Graphics/PS1/PS1Sprites.cpp) are the runtime's (vram_plan.py keeps them from the atlases); the build stamp's text
// row 511 x 0-255 and its CLUT at (288, 511) only while the title shows it (ps1/version_stamp.cpp).
constexpr uint32_t VRAM_FB = 2 * 320 * 240 * 2, CLUT4 = 16 * 2, CLUT_FIXED = 256 * 2 + 320 * 2 + 2 * CLUT4;
uint32_t vramUsed()
{
    uint32 groups = 0; // (the engine's uint32: unsigned int, not uint32_t's unsigned long)
    g_ps1DbgVramTiles  = RSDK::PS1TilesVramBytes();
    g_ps1DbgVramAtlas  = RSDK::PS1AtlasVramBytes(&groups);
    g_ps1DbgVramGroups = groups;
    g_ps1DbgVramClut   = (RSDK::PS1TileClutCount() + RSDK::PS1AtlasClutCount()) * CLUT4 + CLUT_FIXED +
                         (g_ps1VersionDraw ? 256 * 2 + CLUT4 : 0);
    return VRAM_FB + g_ps1DbgVramTiles + g_ps1DbgVramAtlas + groups + g_ps1DbgVramClut;
}

// SPU RAM in use: the bank, plus the reverb work area beyond the silent block if reverb is on (SPUCNT 1F801DAAh bit 7,
// mBASE 1F801DA2h in 8-byte units; 16-bit reads through KSEG1)
uint32_t spuUsed()
{
    uint16_t cnt  = *(volatile uint16_t *)0xBF801DAA, base = *(volatile uint16_t *)0xBF801DA2;
    uint32_t work = cnt & 0x80 ? 0x80000 - (uint32_t)base * 8 : 0;
    g_ps1DbgReverb = work;
    return RSDK::PS1SfxBankBytes() + (work > 512 * 1024 - PS1_SPU_SAMPLE_END ? work - (512 * 1024 - PS1_SPU_SAMPLE_END) : 0);
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
    p       = putStr(s_text[2], "VRAM ");
    p       = putNum(p, kb(g_ps1DbgVram), VRAM_W);
    *p++    = '/';
    p       = putNum(p, VRAM_KB, digitsOf(VRAM_KB));
    p       = putStr(p, "K");
    *p      = 0;
    p       = putStr(s_text[3], "SPU ");
    p       = putNum(p, kb(g_ps1DbgSpu), SPU_W);
    *p++    = '/';
    p       = putNum(p, SPU_KB, digitsOf(SPU_KB));
    p       = putStr(p, "K");
    *p      = 0;
}

// The list from s_text: the box, then each glyph's rectangles, four to a fragment (each header linking to the next; the
// last one's psyqo writes). Only after PS1ChainStaticBegin: the last frame's chain no longer reads these words. The FPS
// line in yellow while the rate is under the target. (-O2 for this one, -Os elsewhere: the rebuild at half the time,
// GHZ1 13 -> 6 hblanks, for a few hundred bytes; the whole object at -O2 took 7 KB more for 2-3 hblanks of the rest.)
__attribute__((optimize("O2"))) void rebuild()
{
    uint32_t *w = s_list, *hdr = nullptr;
    int k = PER_FRAG, n = 0;
    auto put = [&](uint32_t cmd, uint32_t xy, uint32_t wh) {
        if (k == PER_FRAG) {
            if (hdr)
                *hdr = (uint32_t)(3 * PER_FRAG) << 24 | ((uintptr_t)w & 0xFFFFFF);
            hdr = w++;
            k   = 0;
        }
        w[0] = cmd, w[1] = xy, w[2] = wh;
        w += 3, ++k, ++n;
    };
    const uint32_t box = 0x60000000 | BOX_BGR;
    put(box, (uint32_t)BOX_Y << 16 | BOX_X, (uint32_t)BOX_H << 16 | BOX_W);
    for (int l = 0; l < LINES; ++l) {
        const uint32_t text = 0x60000000 | (l == 0 && g_ps1DbgFps < g_ps1DbgTarget ? SLOW_BGR : TEXT_BGR);
        uint32_t xy         = (uint32_t)(BOX_Y + MARGIN + l * PITCH) << 16 | (BOX_X + MARGIN);
        for (const char *c = s_text[l]; *c; ++c, xy += CELL) {
            unsigned i = (unsigned)(uint8_t)*c - 32;
            int g      = i < 96 ? kGlyphIndex.of[i] : kDbgGlyphCount - 1;
            for (int r = kDbgGlyphStart[g]; r < kDbgGlyphStart[g + 1]; ++r) {
                uint32_t v = kGlyphWords.of[r];
                put(v >> 31 ? box : text, xy + (v & 0x70007), v >> 3 & 0x70007);
            }
        }
    }
    s_last        = hdr;
    s_lastWords   = 3 * k;
    g_ps1DbgPrims = (uint32_t)n;
    // the list holds GP0 60h rectangles and nothing else (no texture, CLUT, texpage or drawing-mode command)
    uint32_t bad = 0;
    for (uint32_t *f = s_list; f < w; f += 1 + 3 * PER_FRAG)
        for (int j = 0; j < PER_FRAG && f + 1 + 3 * j < w; ++j)
            bad += (f[1 + 3 * j] >> 24) != 0x60;
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
    g_ps1DbgVram    = vramUsed();
    g_ps1DbgSpu     = spuUsed();
}

// The [PERF] line: integers only, no printf (the port's vsnprintf, float support included, took ~15 hblanks a line).
// A format string's %u takes the next value, %k the next value in bytes as KB (rounded); the rest is copied.
char *logPut(char *p, const char *f, const uint32_t *v)
{
    char *end = s_log + sizeof(s_log) - 1;
    while (*f && p < end) {
        if (f[0] != '%' || (f[1] != 'u' && f[1] != 'k')) {
            *p++ = *f++;
            continue;
        }
        uint32_t x = f[1] == 'k' ? kb(*v++) : *v++;
        char d[10];
        int n = 0;
        do d[n++] = (char)('0' + x % 10);
        while ((x /= 10) && n < 10);
        while (n && p < end) *p++ = d[--n];
        f += 2;
    }
    return p;
}

// The line in two halves, a frame each: its start (frames, main RAM), then the rest (VRAM, SPU, the overlay's own).
// (KB unless B; text / data / bss: the EXE's, data = rodata + data; reverb: its work area's bytes, 0 = off)
void logStart()
{
    uint32_t ram = g_ps1DbgRamUsed, rsvTop = g_ps1ReserveEnd > g_ps1ReserveHigh ? g_ps1ReserveEnd - g_ps1ReserveHigh : 0;
    const uint32_t v[] = { g_ps1DbgSeconds, g_ps1DbgFps, g_ps1DbgTarget, g_ps1DbgLogic, ram, RAM_KB, pct(ram, RAM_KB * 1024),
                           g_ps1DbgRamPeak, (uint32_t)(uintptr_t)&__ftext_start - 0x80000000,
                           (uint32_t)(&__text_end - &__ftext_start), (uint32_t)(&__data_end - &__fdata_start),
                           (uint32_t)(&__bss_end - &__bss_start), g_ps1DbgHeap, g_ps1DbgHeapTop, g_ps1DbgStg, g_ps1DbgStgFree,
                           rsvTop, g_ps1DbgLent, PS1_STACK_RESERVE, g_ps1StackUnused };
    s_logLen = (int)(logPut(s_log,
                            "[PERF] t=%u fps=%u/%u logic=%u ram=%k/%uK %u% peak=%k kern=%k text=%k data=%k bss=%k heap=%k "
                            "hpeak=%k stg=%k stgfree=%k rsvtop=%k lent=%k stack=%k su=%uB",
                            v) - s_log);
}

void logRest()
{
    const uint32_t v[] = { g_ps1DbgVram, VRAM_KB, pct(g_ps1DbgVram, VRAM_KB * 1024), VRAM_FB, g_ps1DbgVramTiles,
                           g_ps1DbgVramAtlas, g_ps1DbgVramGroups, g_ps1DbgVramClut, g_ps1DbgSpu, SPU_KB,
                           pct(g_ps1DbgSpu, SPU_AVAIL), SPU_RESERVED, g_ps1DbgReverb, g_ps1DbgPrims, MAX_RECTS,
                           sizeof(s_list), g_ps1DbgHblMeasure, g_ps1DbgHblRebuild, g_ps1DbgHblLog, g_ps1DbgHblMax };
    s_logLen = (int)(logPut(s_log + s_logLen,
                            " vram=%k/%uK %u% fb=%k tiles=%k atlas=%k groups=%k clut=%uB spu=%k/%uK %u% spures=%uB reverb=%u "
                            "overlay_vram=0 overlay_prims=%u/%u overlay_buf=%uB overlay_hbl=%u/%u/%u/%u\n",
                            v) - s_log);
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
        g_ps1DbgVram     = vramUsed();
        g_ps1DbgSpu      = spuUsed();
        format();
    }
    uint32_t ram = ramUsed(false);
    if (ram > g_ps1DbgRamPeak)
        g_ps1DbgRamPeak = ram;
    // a window starts again after a load or any frame longer than a second (a video plays inside one: the game shows no
    // frame of its own meanwhile; such seconds are not a frame rate). It counts the frames shown after its first one.
    bool stall = vbl - s_lastVbl > rate;
    s_lastVbl  = vbl;
    if (s_winLoads != g_ps1LoadingLoads || s_winVbl == 0xFFFFFFFF || stall)
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
        if (s_logStep < 3) {
            if (s_logStep == 1)
                logStart();
            else
                logRest();
            s_logStep = s_logStep + 1;
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
