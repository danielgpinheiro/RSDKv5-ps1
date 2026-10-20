#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/PaletteLegacy.cpp"
#endif

uint16 RSDK::rgb32To16_R[0x100];
uint16 RSDK::rgb32To16_G[0x100];
uint16 RSDK::rgb32To16_B[0x100];

uint16 RSDK::globalPalette[PALETTE_BANK_COUNT][PALETTE_BANK_SIZE];
uint16 RSDK::activeGlobalRows[PALETTE_BANK_COUNT];
uint16 RSDK::activeStageRows[PALETTE_BANK_COUNT];
uint16 RSDK::stagePalette[PALETTE_BANK_COUNT][PALETTE_BANK_SIZE];

uint16 RSDK::fullPalette[PALETTE_BANK_COUNT][PALETTE_BANK_SIZE];

uint8 RSDK::gfxLineBuffer[SCREEN_YSIZE];
#if RETRO_PLATFORM == RETRO_PS1
uint32 RSDK::ps1LineBankGen = 0;
#endif

int32 RSDK::maskColor = 0;
#if RETRO_REV02
uint16 *RSDK::tintLookupTable = NULL;
#else
uint16 RSDK::tintLookupTable[0x10000];
#endif

#if RETRO_REV02
void RSDK::LoadPalette(uint8 bankID, const char *filename, uint16 disabledRows)
{
    char fullFilePath[0x80];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Palettes/%s", filename);

    FileInfo info;
    InitFileInfo(&info);
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        for (int32 r = 0; r < 0x10; ++r) {
            if (!(disabledRows >> r & 1)) {
                for (int32 c = 0; c < 0x10; ++c) {
                    uint8 red                         = ReadInt8(&info);
                    uint8 green                       = ReadInt8(&info);
                    uint8 blue                        = ReadInt8(&info);
                    fullPalette[bankID][(r << 4) + c] = rgb32To16_B[blue] | rgb32To16_G[green] | rgb32To16_R[red];
                }
            }
            else {
                Seek_Cur(&info, 0x10 * (3 * sizeof(uint8)));
            }
        }

        CloseFile(&info);
    }
}

void RSDK::BlendColors(uint8 destBankID, uint32 *srcColorsA, uint32 *srcColorsB, int32 blendAmount, int32 startIndex, int32 count)
{
    if (destBankID >= PALETTE_BANK_COUNT || !srcColorsA || !srcColorsB)
        return;

    blendAmount = CLAMP(blendAmount, 0x00, 0xFF);

    uint8 blendA         = 0xFF - blendAmount;
    uint16 *paletteColor = &fullPalette[destBankID][startIndex];
    for (int32 i = startIndex; i < startIndex + count; ++i) {
        int32 r = blendAmount * ((*srcColorsB >> 0x10) & 0xFF) + blendA * ((*srcColorsA >> 0x10) & 0xFF);
        int32 g = blendAmount * ((*srcColorsB >> 0x08) & 0xFF) + blendA * ((*srcColorsA >> 0x08) & 0xFF);
        int32 b = blendAmount * ((*srcColorsB >> 0x00) & 0xFF) + blendA * ((*srcColorsA >> 0x00) & 0xFF);

#if RETRO_PLATFORM == RETRO_PS1
        if (!destBankID)
            PS1PaletteWrite((uint8)i, PACK_RGB888((uint8)(r >> 8), (uint8)(g >> 8), (uint8)(b >> 8))); // 9m
#endif
        *paletteColor = PACK_RGB888((uint8)(r >> 8), (uint8)(g >> 8), (uint8)(b >> 8));

        srcColorsA++;
        srcColorsB++;
        ++paletteColor;
    }
}
#endif

void RSDK::SetPaletteFade(uint8 destBankID, uint8 srcBankA, uint8 srcBankB, int16 blendAmount, int32 startIndex, int32 endIndex)
{
    if (destBankID >= PALETTE_BANK_COUNT || srcBankA >= PALETTE_BANK_COUNT || srcBankB >= PALETTE_BANK_COUNT)
        return;

    blendAmount = CLAMP(blendAmount, 0x00, 0xFF);
    endIndex    = MIN(endIndex, 0x100);

    if (startIndex >= endIndex)
        return;

    uint32 blendA        = 0xFF - blendAmount;
    uint16 *paletteColor = &fullPalette[destBankID][startIndex];
#if RETRO_PLATFORM == RETRO_PS1
    // 9u (OOZ's smog fades all 256 entries every frame): the six channel products from tables in the scratchpad (free
    // here: its users only hold it inside their own calls), built once a call: blend * (a channel's 8-bit value as
    // GetPaletteEntry expands it) <= 255 * 252 fits 16 bits; the same sums, so the same colours. As stock: endIndex
    // inclusive, GetPaletteEntry's uint8 index (entry 256 reads index 0)
    {
        uint16 *t = (uint16 *)0x1F800000; // R: A 0-31, B 32-63; G: A 64-127, B 128-191; B: A 192-223, B 224-255
        for (int32 k = 0; k < 32; ++k)
            t[k] = (uint16)(blendA * (uint32)(k << 3)), t[32 + k] = (uint16)(blendAmount * (k << 3)),
            t[192 + k] = (uint16)(blendA * (uint32)(k << 3)), t[224 + k] = (uint16)(blendAmount * (k << 3));
        for (int32 k = 0; k < 64; ++k)
            t[64 + k] = (uint16)(blendA * (uint32)(k << 2)), t[128 + k] = (uint16)(blendAmount * (k << 2));
        const uint16 *pa = fullPalette[srcBankA], *pb = fullPalette[srcBankB];
        for (int32 i = startIndex; i <= endIndex; ++i, ++paletteColor) {
            uint32 a = pa[(uint8)i], c = pb[(uint8)i];
            int32 r = t[32 + (c >> 11)] + t[a >> 11];
            int32 g = t[128 + ((c >> 5) & 63)] + t[64 + ((a >> 5) & 63)];
            int32 b = t[224 + (c & 31)] + t[192 + (a & 31)];
            uint16 v = PACK_RGB888((uint8)(r >> 8), (uint8)(g >> 8), (uint8)(b >> 8));
            if (!destBankID && i < 0x100)
                PS1PaletteWrite((uint8)i, v); // 9m
            *paletteColor = v;
        }
        return;
    }
#endif
    for (int32 i = startIndex; i <= endIndex; ++i) {
        uint32 clrA = GetPaletteEntry(srcBankA, i);
        uint32 clrB = GetPaletteEntry(srcBankB, i);

        int32 r = blendAmount * ((clrB >> 0x10) & 0xFF) + blendA * ((clrA >> 0x10) & 0xFF);
        int32 g = blendAmount * ((clrB >> 0x08) & 0xFF) + blendA * ((clrA >> 0x08) & 0xFF);
        int32 b = blendAmount * ((clrB >> 0x00) & 0xFF) + blendA * ((clrA >> 0x00) & 0xFF);

#if RETRO_PLATFORM == RETRO_PS1
        if (!destBankID && i < 0x100)
            PS1PaletteWrite((uint8)i, PACK_RGB888((uint8)(r >> 8), (uint8)(g >> 8), (uint8)(b >> 8))); // 9m
#endif
        *paletteColor = PACK_RGB888((uint8)(r >> 8), (uint8)(g >> 8), (uint8)(b >> 8));

        ++paletteColor;
    }
}