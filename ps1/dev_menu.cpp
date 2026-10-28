// Stage Select dev menu (docs/ports/mania/32 "Stage Select dev menu"; `make DEVMENU=1` only, an 8 MB debug link that
// `make dist` and the builder export refuse): Select on pad 1 opens the engine's dev menu straight at its stage select
// (category list, then scene list); Circle on the categories or Select again closes it. The PC's other dev menu
// screens, its hot keys and `engine.devMenu` stay off (that flag would make the platform PLATFORM_DEV, where R1 starts
// the Encore swap).
//
// DrawDevString's text (PS1DevString) needs no VRAM, which no scene leaves free: the PC's 8x8 dev font, each glyph
// row's lit runs as opaque flat rectangles (GP0 60h), four to a fragment as PS1FlatBatchFlush writes them.
#include "RSDK/Core/RetroEngine.hpp"

#include "RSDK/Dev/DevFont.hpp" // devTextStencil: 128 glyphs of 8x8 bytes (0 / 1), only in this build

#include "gpu_chain_raw.hh"

using namespace RSDK;

bool PS1FsFind(const char *path, uint32_t *lba, uint32_t *size); // ps1/cdrom_fs.cpp
namespace RSDK
{
void PS1DevCategoryMenu(); // OpenDevMenu's state (Dev/Debug.cpp)
}

extern "C" {
// The marker `make dist` / tools/export_builder.sh refuse (nm), set at boot to the RAM the console showed: 8 (MB), or 2
// when the RAM above 2 MB mirrors the first 2 MB (the emulator's 8 MB option off): the build then stops at a red screen
volatile uint32_t g_ps1DevMenuBuild = 0;
volatile uint16_t g_ps1DevSwallow   = 0; // pad 1 buttons kept from the game until released (the Select that toggled)
volatile uint32_t g_ps1DevTextRects = 0, g_ps1DevTextDropped = 0; // dev text rectangles chained / lost (arena full)
}

namespace {

constexpr uint16 SELECT_BIT = 1 << 11; // g_ps1PadButtons bit 11 (RSDK/Input/PS1/PS1InputDevice.hpp)
constexpr int32 BATCH       = 64;      // rectangles a span
constexpr int32 ROWS        = 8;       // list rows shown (the PC's)
constexpr int32 MAX_SCENES  = 0x100;

uint32 s_cmd = 0;
int32 s_n    = 0;
uint32 s_xy[BATCH], s_wh[BATCH];
bool s_selectHeld = false;

// the scene list's entries the disc holds: the entry's filter allows Mania mode (FILTER_MANIA = 2,
// SonicMania/GameVariables.h; GameConfig's 0 reads as 0xFF) and Data/Stages/<folder>/Scene<id>.ptl exists (LSelect,
// Pinball, MSZEnding have none; Encore entries reuse Mania's files but fail the filter)
uint8 s_ok[MAX_SCENES];

void flush()
{
    if (!s_n)
        return;
    uint32_t *w = PS1ChainSpanBegin(((s_n + 3) >> 2) * 13);
    if (!w) {
        g_ps1DevTextDropped = g_ps1DevTextDropped + s_n;
        s_n                 = 0;
        return;
    }
    uint32_t *hdr = NULL;
    int32 fw = 0, k = 0;
    while (k < s_n) {
        if (hdr)
            *hdr = (uint32_t)fw << 24 | ((uintptr_t)w & 0xFFFFFF);
        hdr = w++;
        fw  = 0;
        for (int32 j = 0; j < 4 && k < s_n; ++j, ++k) {
            w[0] = s_cmd;
            w[1] = s_xy[k];
            w[2] = s_wh[k];
            w += 3, fw += 3;
        }
    }
    PS1ChainSpanEnd(hdr, fw, w);
    g_ps1DevTextRects = g_ps1DevTextRects + s_n;
    s_n               = 0;
}

void run(int32 x, int32 y, int32 w)
{
    if (s_n == BATCH)
        flush();
    s_xy[s_n]   = (uint32)(uint16)y << 16 | (uint16)x;
    s_wh[s_n++] = 1u << 16 | (uint16)w;
}

void scanCategory(int32 category)
{
    SceneListInfo *list = &sceneInfo.listCategory[category];
    for (int32 i = 0; i < list->sceneCount && i < MAX_SCENES; ++i) {
        SceneListEntry *e = &sceneInfo.listData[list->sceneOffsetStart + i];
        char path[0x40];
        snprintf(path, sizeof(path), "Data/Stages/%s/Scene%s.ptl", e->folder, e->id);
        uint32_t lba = 0, size = 0;
        s_ok[i] = (e->filter & 2) && PS1FsFind(path, &lba, &size);
    }
}

// Up / Down as the PC's lists: a press moves at once, a held key every 8 frames; the window follows the selection
void listMove(int32 count)
{
    int32 d = 0;
    if (controller[CONT_ANY].keyUp.press || controller[CONT_ANY].keyDown.press) {
        d             = controller[CONT_ANY].keyUp.press ? -1 : 1;
        devMenu.timer = 1;
    }
    else if (controller[CONT_ANY].keyUp.down || controller[CONT_ANY].keyDown.down) {
        if (!devMenu.timer)
            d = controller[CONT_ANY].keyUp.down ? -1 : 1;
        devMenu.timer = (devMenu.timer + 1) & 7;
    }
    if (d && count > 0)
        devMenu.selection = (devMenu.selection + d + count) % count;
    if (devMenu.selection < devMenu.scrollPos)
        devMenu.scrollPos = devMenu.selection;
    else if (devMenu.selection > devMenu.scrollPos + ROWS - 1)
        devMenu.scrollPos = devMenu.selection - (ROWS - 1);
}

// the PC's two boxes (Debug.cpp): a header with the title (and a second line), the list below it
int32 drawFrame(const char *title, const char *sub)
{
    int32 cx = currentScreen->center.x, dy = currentScreen->center.y - 68;
    DrawRectangle(cx - 128, dy - 16, 0x100, 0x30, 0x80, 0xFF, INK_NONE, true);
    DrawDevString(title, cx, dy, ALIGN_CENTER, 0xF0F0F0);
    if (sub)
        DrawDevString(sub, cx, dy + 16, ALIGN_CENTER, 0xF0F080);
    DrawRectangle(cx - 128, dy + 36, 0x100, 0x48, 0x80, 0xFF, INK_NONE, true);
    return dy + 40;
}

bool confirmPress() { return controller[CONT_ANY].keyA.press || controller[CONT_ANY].keyStart.press; }

void PS1DevSceneMenu()
{
    SceneListInfo *list = &sceneInfo.listCategory[devMenu.listPos];
    int32 count         = list->sceneCount < MAX_SCENES ? list->sceneCount : MAX_SCENES;
    int32 y             = drawFrame("SELECT STAGE SCENE", list->name);
    for (int32 i = 0; i < ROWS && devMenu.scrollPos + i < count; ++i, y += 8) {
        int32 s      = devMenu.scrollPos + i;
        bool sel     = s == devMenu.selection;
        uint32 color = s_ok[s] ? (sel ? 0xF0F0F0 : 0x808090) : (sel ? 0x906060 : 0x404048);
        DrawDevString(sceneInfo.listData[list->sceneOffsetStart + s].name, currentScreen->center.x + 96, y, ALIGN_RIGHT, color);
    }

    listMove(count);

    if (confirmPress()) {
        if (devMenu.selection < count && s_ok[devMenu.selection]) {
            // the paused channels stopped, not resumed: a stopped stream lets the XA stop now (PS1AudioDevice FrameInit),
            // the scene's PlayStream starts its own; a resumed one would play on until the load's first read
            for (int32 c = 0; c < CHANNEL_COUNT; ++c)
                if (channels[c].state & CHANNEL_PAUSED)
                    StopChannel(c);
            sceneInfo.activeCategory = devMenu.listPos;
            sceneInfo.listPos        = list->sceneOffsetStart + devMenu.selection;
            sceneInfo.state          = ENGINESTATE_LOAD; // what a BOOT= build does (ps1/rsdk5_main.cpp)
        }
    }
    else if (controller[CONT_ANY].keyB.press) {
        devMenu.state     = PS1DevCategoryMenu;
        devMenu.selection = devMenu.listPos;
        devMenu.scrollPos = devMenu.listPos > ROWS - 1 ? devMenu.listPos - (ROWS - 1) : 0;
        devMenu.timer     = 0;
    }
}

} // namespace

void RSDK::PS1DevCategoryMenu()
{
    int32 count = sceneInfo.categoryCount;
    int32 y     = drawFrame("SELECT STAGE CATEGORY", "SELECT: CLOSE");
    for (int32 i = 0; i < ROWS && devMenu.scrollPos + i < count; ++i, y += 8) {
        bool sel = devMenu.scrollPos + i == devMenu.selection;
        DrawDevString(sceneInfo.listCategory[devMenu.scrollPos + i].name, currentScreen->center.x - 64, y, ALIGN_LEFT, sel ? 0xF0F0F0 : 0x808090);
    }

    listMove(count);

    if (confirmPress()) {
        if (devMenu.selection < count && sceneInfo.listCategory[devMenu.selection].sceneCount) {
            scanCategory(devMenu.selection);
            devMenu.state     = PS1DevSceneMenu;
            devMenu.listPos   = devMenu.selection;
            devMenu.scrollPos = 0;
            devMenu.selection = 0;
            devMenu.timer     = 0;
        }
    }
    else if (controller[CONT_ANY].keyB.press) {
        CloseDevMenu();
    }
}

// Select on pad 1 (raw bits, so before the title assigns a controller, and the harness's g_ps1PadTestMask): opens the
// menu in a running scene (REGULAR, PAUSED, FROZEN: never in a load, a video, an image or an error screen), closes it
// when open. Called before ProcessInputDevices: the press is swallowed (InputDevicePS1::UpdateInput) until released,
// so the game never sees it (the title takes any button)
extern "C" void PS1DevMenuCommands()
{
    bool held = (g_ps1PadButtons | g_ps1PadTestMask) & SELECT_BIT;
    bool press = held && !s_selectHeld;
    s_selectHeld = held;
    if (!held)
        g_ps1DevSwallow = 0;
    if (!press)
        return;
    if (sceneInfo.state == ENGINESTATE_DEVMENU) {
        CloseDevMenu();
        g_ps1DevSwallow = SELECT_BIT;
    }
    else if (sceneInfo.state >= ENGINESTATE_REGULAR && sceneInfo.state <= ENGINESTATE_FROZEN) {
        OpenDevMenu();
        g_ps1DevSwallow = SELECT_BIT;
    }
}

// RSDK's DrawDevString (Graphics/Drawing.cpp) on the PS1: the PC's line, alignment and 8-pixel cell rules; glyphs
// outside 0x00-0x7F skipped. Called inside an open frame only (the draw lists, the DEVMENU state's PS1FrameStart).
void PS1DevString(const char *string, int32 x, int32 y, int32 align, uint32 color)
{
    flush();
    s_cmd    = 0x60000000 | (color >> 16 & 0xFF) | (color & 0xFF00) | (color & 0xFF) << 16; // RGB -> GP0 BGR
    int32 sw = currentScreen->size.x, sh = currentScreen->size.y;

    int32 charOffset = 0;
    bool linesRemain = true;
    while (linesRemain) {
        linesRemain = false;

        int32 lineSize = 0;
        char cur       = string[charOffset];
        if (cur != '\n') {
            while (cur) {
                cur = string[++charOffset];
                lineSize++;
                if (cur == '\n') {
                    linesRemain = true;
                    break;
                }
            }
        }

        if (y >= 0 && y < sh - 7) {
            int32 drawX = x - (align == ALIGN_CENTER ? 4 * lineSize : align == ALIGN_RIGHT ? 8 * lineSize : 0);
            const uint8 *curChar = (const uint8 *)&string[charOffset++ - lineSize];
            for (int32 c = 0; c < lineSize; ++c, ++curChar, drawX += 8) {
                uint8 ch = *curChar;
                if (drawX < 0 || drawX >= sw - 7 || ch >= 0x80 || ch == ' ' || ch == '\t' || ch == '\n')
                    continue;
                const uint8 *g = &devTextStencil[0x40 * ch];
                for (int32 r = 0; r < 8; ++r, g += 8)
                    for (int32 px = 0; px < 8;) {
                        if (!g[px]) {
                            ++px;
                            continue;
                        }
                        int32 p0 = px;
                        while (px < 8 && g[px]) ++px;
                        run(drawX + p0, y + r, px - p0);
                    }
            }
        }
        else {
            ++charOffset;
        }

        y += 8;
    }
    flush();
}

// main()'s first call, before anything allocates: with the emulator's 8 MB option off the RAM above 2 MB mirrors the
// first 2 MB (DuckStation emu/duckstation/src/core/bus.cpp, PCSX-Redux emu/pcsx-redux/src/core/psxmem.cc), and this
// build's STG (up to 0x807FC000) would alias the EXE. One word 64 KB below each RAM top's stack reserve, put back after.
extern "C" void PS1DevRamProbe()
{
    volatile uint32_t *hi = (volatile uint32_t *)0xA07F0000, *lo = (volatile uint32_t *)0xA01F0000;
    uint32_t h = *hi, l = *lo;
    *lo                = 0x2D2D2D2D;
    *hi                = 0x88888888;
    g_ps1DevMenuBuild  = *lo == 0x88888888 ? 2 : 8;
    *hi                = h;
    *lo                = l;
}
