// docs/ports/mania/32 "Debug overlay": `make DEBUG_OVERLAY=1` (a debug build: `make dist` and tools/export_builder.sh refuse
// it; the natural EXE has none of this). The top-right corner shows, from the port's own counters,
//   FPS 60/60        frames shown in the last second (one per frame(), i.e. psyqo flips) / the video mode's rate
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
// Once a second, also while hidden, a [PERF] line goes to the TTY (printf -> BIOS write: PCSX-Redux's log, DuckStation's
// TTY channel) with the same values, overlay_vram=0, the primitive count and the list's size.

// psyqo first: the engine's headers bring the port's one-argument assert (ps1/rsdk5_main.cpp's order)
#include <psyqo/advancedpad.hh>
#include <psyqo/gpu.hh>

#include <stdio.h>

#include "gpu_chain_raw.hh"
#include "pad.hh"
#include "video.hh" // PS1Hblanks

extern psyqo::GPU *g_ps1GPU; // ps1/rsdk5_main.cpp
extern PS1Pad *g_ps1Pad;
extern "C" {
extern volatile uint32_t g_ps1StepsTotal; // ps1/rsdk5_main.cpp: logic steps run
}
extern volatile uint32_t g_ps1LoadingLoads; // ps1/loading.cpp: loads begun

extern "C" {
volatile uint32_t g_ps1DbgVisible = 1; // drawn (L2 + R2 toggles; GDB may poke it)
volatile uint32_t g_ps1DbgLog     = 1; // the [PERF] line once a second
// the last window's values (GDB reads them on emulators without a TTY log)
volatile uint32_t g_ps1DbgSeconds = 0, g_ps1DbgFps = 0, g_ps1DbgTarget = 0, g_ps1DbgLogic = 0;
volatile uint32_t g_ps1DbgPrims    = 0; // rectangles in the list (the box included)
volatile uint32_t g_ps1DbgBadPrims = 0; // list words that are not a GP0 60h rectangle (must stay 0)
// hblanks the overlay took: the last frame, the worst, the last frame that rebuilt the list, the last log frame
volatile uint32_t g_ps1DbgHbl = 0, g_ps1DbgHblMax = 0, g_ps1DbgHblRebuild = 0, g_ps1DbgHblLog = 0;
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
// the lines' fixed text and their number fields (each field right-aligned in its width)
constexpr int LINES = 1;
constexpr int LINE_CHARS[LINES] = { 4 + FPS_W + 1 + 2 }; // "FPS ff/tt"
constexpr int maxChars()
{
    int m = 0;
    for (int l = 0; l < LINES; ++l) m = LINE_CHARS[l] > m ? LINE_CHARS[l] : m;
    return m;
}
constexpr int BOX_W = maxChars() * CELL - 1 + 2 * MARGIN, BOX_H = LINES * PITCH - 1 + 2 * MARGIN;
constexpr int BOX_X = RIGHT - BOX_W, BOX_Y = TOP;

// the worst case: every variable digit its costliest glyph, every fixed part as drawn (the targets 60 / 50 alike)
constexpr int MAX_RECTS = 1 + rectsOf("FPS /") + FPS_W * maxDigitRects() + (rectsOf("60") > rectsOf("50") ? rectsOf("60") : rectsOf("50"));
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
            int g = glyphOf(*c);
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
}

void log()
{
    printf("[PERF] t=%u fps=%u/%u logic=%u overlay_vram=0 overlay_prims=%u/%u overlay_buf=%uB overlay_hbl=%u/%u\n", g_ps1DbgSeconds,
           g_ps1DbgFps, g_ps1DbgTarget, g_ps1DbgLogic, g_ps1DbgPrims, MAX_RECTS, (unsigned)sizeof(s_list), g_ps1DbgHbl,
           g_ps1DbgHblMax);
}

} // namespace

// PS1RenderEndFrame, after everything else of the frame (draw: false while a video plays). One call a frame shown.
void PS1DebugOverlayFrame(bool draw)
{
    uint32_t t0 = PS1Hblanks();
    pollToggle();
    // psyqo's vblank count, latched at each flip (the one perfTick uses; the BIOS VBlank event missed a few at boot)
    uint32_t vbl = g_ps1GPU->getFrameCount(), rate = g_ps1GPU->getRefreshRate();
    bool logged  = false;
    if (!s_started) {
        s_started = true;
        g_ps1DbgTarget = rate;
        format();
    }
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
        s_dirty = true;
        if (g_ps1DbgLog) {
            log();
            logged = true;
        }
        s_winVbl = vbl, s_winFrames = 0, s_winSteps = g_ps1StepsTotal;
    }
    bool rebuilt = false;
    if (draw && g_ps1DbgVisible && PS1ChainStaticBegin()) {
        if (s_dirty) {
            rebuild();
            s_dirty = false, rebuilt = true;
        }
        PS1ChainLinkList(s_list, s_last, s_lastWords);
    }
    uint32_t hbl = PS1HblanksSince(t0);
    g_ps1DbgHbl  = hbl;
    if (hbl > g_ps1DbgHblMax)
        g_ps1DbgHblMax = hbl;
    if (rebuilt)
        g_ps1DbgHblRebuild = hbl;
    if (logged)
        g_ps1DbgHblLog = hbl;
}
