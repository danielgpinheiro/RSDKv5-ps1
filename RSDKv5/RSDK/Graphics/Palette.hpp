#ifndef PALETTE_H
#define PALETTE_H

namespace RSDK
{

#define PALETTE_BANK_COUNT (0x8)
#define PALETTE_BANK_SIZE  (0x100)

union Color {
    uint8 bytes[4];
    uint32 color;
};

extern uint16 rgb32To16_R[0x100];
extern uint16 rgb32To16_G[0x100];
extern uint16 rgb32To16_B[0x100];

extern uint16 globalPalette[PALETTE_BANK_COUNT][PALETTE_BANK_SIZE];
extern uint16 activeGlobalRows[PALETTE_BANK_COUNT];
extern uint16 activeStageRows[PALETTE_BANK_COUNT];
extern uint16 stagePalette[PALETTE_BANK_COUNT][PALETTE_BANK_SIZE];

extern uint16 fullPalette[PALETTE_BANK_COUNT][PALETTE_BANK_SIZE];

extern uint8 gfxLineBuffer[SCREEN_YSIZE]; // Pointers to active palette
#if RETRO_PLATFORM == RETRO_PS1
extern uint32 ps1LineBankGen; // 3.3k: counts the writes to gfxLineBuffer (the layer drawer caches its one-bank test)
#endif

extern int32 maskColor;

#if RETRO_REV02
extern uint16 *tintLookupTable;
#else
extern uint16 tintLookupTable[0x10000];
#endif

#define RGB888_TO_RGB565(r, g, b) ((b) >> 3) | (((g) >> 2) << 5) | (((r) >> 3) << 11)

#define PACK_RGB888(r, g, b) RGB888_TO_RGB565(r, g, b)

#if RETRO_REV02
void LoadPalette(uint8 bankID, const char *filePath, uint16 disabledRows);
#endif

inline void SetActivePalette(uint8 newActiveBank, int32 startLine, int32 endLine)
{
    if (newActiveBank < PALETTE_BANK_COUNT)
        for (int32 l = startLine; l < endLine && l < SCREEN_YSIZE; l++) gfxLineBuffer[l] = newActiveBank;
#if RETRO_PLATFORM == RETRO_PS1
    ++ps1LineBankGen;
#endif
}

inline uint32 GetPaletteEntry(uint8 bankID, uint8 index)
{
    // 0xF800 = 1111 1000 0000 0000 = R
    // 0x7E0  = 0000 0111 1110 0000 = G
    // 0x1F   = 0000 0000 0001 1111 = B
    uint16 clr = fullPalette[bankID & 7][index];

    int32 R = (clr & 0xF800) << 8;
    int32 G = (clr & 0x7E0) << 5;
    int32 B = (clr & 0x1F) << 3;
    return R | G | B;
}

#if RETRO_PLATFORM == RETRO_PS1
// docs/32 9f: a bank-0 write (RSDK/Scene/PS1/PS1Layers.cpp): counts the entries that now differ from the CLUTs uploaded at
// the draw phase's start, so the draws after it take CLUTs of the palette as it is (CutsceneHBH: each Heavy's colours)
void PS1PaletteWrite(uint8 index, uint16 value);
extern "C" uint32 g_ps1ClutCopyGen; // P4: per-draw CLUT copies (RSDK/Scene/PS1/PS1Layers.cpp)
// 9m (user 2026-10-09: no hit flash on the Stardust Speedway bosses): every bank-0 write goes through it, not only
// SetPaletteEntry's (Metal Sonic's flash is a CopyPalette around its draws)
#define PS1_PAL_SET(bank, index, value)                                                                                 \
    do {                                                                                                               \
        uint16 v_ = (value);                                                                                           \
        if (!(bank))                                                                                                   \
            PS1PaletteWrite((uint8)(index), v_);                                                                       \
        fullPalette[bank][index] = v_;                                                                                 \
    } while (0)
// P3 (docs/perf/57): a run of bank-0 writes with PS1PaletteWrite's bookkeeping done in registers (OOZ's smog fades all
// 256 entries every frame: 256 noinline calls, each a volatile read-modify-write of g_ps1PalDiffs). For each write,
// before the entry is assigned: PS1PalRunNote(run, index, value); after the run: PS1PalRunEnd(run). The same count, and
// the same row 240 restore at a write that brings it to 0 (PS1PaletteWrite's), done once at the end: nothing is drawn
// inside a run
struct PS1PalRun {
    const uint16 *raw; // the values the frame's CLUTs came from (PS1Layers.cpp s_palRaw)
    int32 diffs;
    bool zero;
    bool changed; // P4: an entry took a new value (g_ps1ClutCopyGen)
};
PS1PalRun PS1PalRunBegin();
void PS1PalRunEnd(const PS1PalRun &run);
inline void PS1PalRunNote(PS1PalRun &run, uint8 index, uint16 value)
{
    uint16 old = fullPalette[0][index];
    if (index && old != value) {
        run.diffs += (int32)(value != run.raw[index]) - (int32)(old != run.raw[index]);
        run.zero |= !run.diffs;
        run.changed = true;
    }
}
#else
#define PS1_PAL_SET(bank, index, value) (fullPalette[bank][index] = (value))
#endif
inline void SetPaletteEntry(uint8 bankID, uint8 index, uint32 color)
{
    uint16 value = rgb32To16_B[(color >> 0) & 0xFF] | rgb32To16_G[(color >> 8) & 0xFF] | rgb32To16_R[(color >> 16) & 0xFF];
#if RETRO_PLATFORM == RETRO_PS1
    if (!bankID)
        PS1PaletteWrite(index, value);
#endif
    fullPalette[bankID][index] = value;
}

inline void SetPaletteMask(uint32 color)
{
    maskColor = rgb32To16_B[(color >> 0) & 0xFF] | rgb32To16_G[(color >> 8) & 0xFF] | rgb32To16_R[(color >> 16) & 0xFF];
#if RETRO_PLATFORM == RETRO_PS1
    ++g_ps1ClutCopyGen; // P4: a CLUT copy's colours depend on the mask colour
#endif
}

#if RETRO_REV02
inline void SetTintLookupTable(uint16 *lookupTable) { tintLookupTable = lookupTable; }

#if RETRO_USE_MOD_LOADER && RETRO_MOD_LOADER_VER >= 2
inline uint16 *GetTintLookupTable() { return tintLookupTable; }
#endif

#else
inline uint16 *GetTintLookupTable() { return tintLookupTable; }
#endif

inline void CopyPalette(uint8 sourceBank, uint8 srcBankStart, uint8 destinationBank, uint8 destBankStart, uint8 count)
{
    if (sourceBank < PALETTE_BANK_COUNT && destinationBank < PALETTE_BANK_COUNT) {
        for (int32 i = 0; i < count; ++i) {
            PS1_PAL_SET(destinationBank, destBankStart + i, fullPalette[sourceBank][srcBankStart + i]);
        }
    }
}

inline void RotatePalette(uint8 bankID, uint8 startIndex, uint8 endIndex, bool32 right)
{
    if (right) {
        uint16 startClr = fullPalette[bankID][endIndex];
        for (int32 i = endIndex; i > startIndex; --i) PS1_PAL_SET(bankID, i, fullPalette[bankID][i - 1]);
        PS1_PAL_SET(bankID, startIndex, startClr);
    }
    else {
        uint16 startClr = fullPalette[bankID][startIndex];
        for (int32 i = startIndex; i < endIndex; ++i) PS1_PAL_SET(bankID, i, fullPalette[bankID][i + 1]);
        PS1_PAL_SET(bankID, endIndex, startClr);
    }
}

#if RETRO_REV02
void BlendColors(uint8 destBankID, uint32 *srcColorsA, uint32 *srcColorsB, int32 blendAmount, int32 startIndex, int32 count);
#endif
void SetPaletteFade(uint8 destBankID, uint8 srcBankA, uint8 srcBankB, int16 blendAmount, int32 startIndex, int32 endIndex);

#if RETRO_REV0U
#include "Legacy/PaletteLegacy.hpp"
#endif

} // namespace RSDK

#endif
