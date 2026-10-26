// PS1 port (docs/ports/mania/32 7.5): DrawDeformedSprite (UFO7's plasma, OOZ's smog). Stock walks the sheet per screen line: pixel x
// of line y shows texel (P + x D) of scanlines[y] (16.16, wrapping on the sheet's power-of-two size). The two users set
// deform.y 0 (a line reads one sheet row, scaled across); their sheets are 128x128 and the atlas places them whole, 4-bit,
// 128-aligned (tools/gfx/build_atlas.py DEFORM_SHEETS; the binding in the surface: LoadSpriteSheet), so the GPU's texture
// window (GP0 E2h, psx-spx "Texture Window setting") wraps u and v: each line is a textured strip, split where its u
// would pass 255 (a quad's u is 8 bits), two lines a strip (the second line's row: the next scanline's; the effects
// are smooth waves). Inks: INK_MASKED with the GPU's mask test (the mask colour's pixels keep bit 15 clear: 4.1b),
// INK_NONE opaque, INK_BLEND and INK_ALPHA as the GPU's half blend (alpha approximate: counted), others not done.
// Linked into the overlays of the folders whose objects call it (gen_overlays EXTRAS 'deform').
#include "RSDK/Core/RetroEngine.hpp"
#include "../../../../ps1/gpu_chain_raw.hh"

using namespace RSDK;

extern "C" {
volatile uint32 g_ps1DeformDraws = 0, g_ps1DeformStrips = 0, g_ps1DeformApprox = 0; // (totals)
extern volatile uint32 g_ps1DeformTodo;
}

namespace
{
__attribute__((optimize("O2"))) void PS1DeformDraw(uint16 sheetID, int32 inkEffect, int32 alpha)
{
    const GFXSurface *sf = &gfxSurface[sheetID];
    const PS1AtlasClut *cl = PS1AtlasCluts();
    const uint32 b = (uint32)sf->lineSize, attr = b >> 16;
    if (sf->width != 128 || !cl || PS1_ATLAS_8BIT(attr) || PS1_ATLAS_CLUT(attr) >= PS1AtlasClutCount()) {
        g_ps1DeformTodo = g_ps1DeformTodo + 1;
        return;
    }
    uint32 semi = 0, abr = 0;
    bool mask = false;
    switch (inkEffect) {
        case INK_NONE: break;
        case INK_MASKED: mask = true; break;
        case INK_ALPHA:
            if (alpha <= 0)
                return;
            if (alpha < 0xFF) {
                semi = 0x02000000;
                if (alpha < 0x70 || alpha > 0x90)
                    g_ps1DeformApprox = g_ps1DeformApprox + 1;
            }
            break;
        case INK_BLEND: semi = 0x02000000; break;
        default: g_ps1DeformTodo = g_ps1DeformTodo + 1; return;
    }
    const int32 x1 = currentScreen->clipBound_X1, x2 = currentScreen->clipBound_X2;
    const int32 y1 = currentScreen->clipBound_Y1, y2 = currentScreen->clipBound_Y2;
    if (x2 <= x1 || y2 <= y1)
        return;
    const uint32 u0 = b & 0xFF, v0 = (b >> 8) & 0xFF;
    const uint32 tpage = PS1_ATLAS_COLUMN(attr) | PS1_ATLAS_ROW(attr) << 4 | abr << 5;
    const PS1AtlasClut &c = cl[PS1_ATLAS_CLUT(attr)];
    const uint32 clut = (uint32)(c.x >> 4) | (uint32)c.y << 6;
    // the window: 128x128 (mask 16 in 8-texel units) at the sheet's place in its page
    uint32 *e2 = (uint32 *)PS1ChainRawBegin(1);
    if (!e2)
        return;
    e2[0] = 0xE2000000 | 16 | 16 << 5 | (u0 >> 3) << 10 | (v0 >> 3) << 15;
    PS1ChainRawEnd();
    if (mask)
        PS1ChainMaskControl(false, true);
    PS1ChainDrawArea(x1, y1, x2, y2);
    const uint32 cmd = 0x2C808080 | semi;
    uint32 *w = NULL, *wend = NULL, *bulk = NULL;
    uint32 strips = 0;
    for (int32 y = y1; y < y2; y += 2) {
        const ScanlineInfo *s = &scanlines[y], *t = &scanlines[y + 1 < y2 ? y + 1 : y];
        const int32 dx = s->deform.x;
        if (dx <= 0 || s->deform.y) {
            g_ps1DeformApprox = g_ps1DeformApprox + 1;
            if (dx <= 0)
                continue;
        }
        // the strip's rows: this line's, and twice the step to the next line's at its bottom edge (one line: one row)
        const uint32 vT = (uint32)(s->position.y >> 16) & 127;
        uint32 d        = ((uint32)(t->position.y >> 16) - vT) & 127;
        d               = d > 64 ? 1 : d;
        const uint32 vB = vT + (y + 1 < y2 ? 2 * d : 1);
        const int32 yb = y + 2 < y2 ? y + 2 : y2;
        // walk the line: a strip from its texel u (in 0..127: the window wraps) until u would pass 255
        int32 lx = s->position.x + x1 * dx;
        for (int32 x = x1; x < x2;) {
            const uint32 us = (uint32)(lx >> 16) & 127;
            // pixels until u reaches 255 (9u: a 32-bit divide: (255 - us) << 16 < 2^24 and dx > 0, the same quotient as the
            // int64 one, which called libgcc's __divdi3 for every strip)
            int32 px = (int32)((255 - us) << 16) / dx;
            if (px < 1)
                px = 1;
            const int32 xe = x + px < x2 ? x + px : x2;
            const uint32 ue = us + (uint32)(((int64)(xe - x) * dx) >> 16);
            if (w == wend) {
                if (w)
                    PS1ChainBulkEnd(32);
                w = bulk = (uint32 *)PS1ChainBulkBegin(32, 9);
                if (!w)
                    goto done;
                wend = w + 32 * 10;
            }
            w[0] = cmd;
            w[1] = (uint32)(uint16)x | (uint32)(uint16)y << 16;
            w[2] = clut << 16 | vT << 8 | us;
            w[3] = (uint32)(uint16)xe | (uint32)(uint16)y << 16;
            w[4] = tpage << 16 | vT << 8 | (ue > 255 ? 255 : ue);
            w[5] = (uint32)(uint16)x | (uint32)(uint16)yb << 16;
            w[6] = (vB > 255 ? 255 : vB) << 8 | us;
            w[7] = (uint32)(uint16)xe | (uint32)(uint16)yb << 16;
            w[8] = (vB > 255 ? 255 : vB) << 8 | (ue > 255 ? 255 : ue);
            w += 10;
            ++strips;
            lx += (xe - x) * dx;
            x = xe;
        }
    }
done:
    if (w && w != bulk)
        PS1ChainBulkEnd((int)((w - bulk) / 10));
    PS1ChainDrawAreaReset();
    if (mask)
        PS1ChainMaskControl(false, false);
    if ((e2 = (uint32 *)PS1ChainRawBegin(1))) { // the window off again
        e2[0] = 0xE2000000;
        PS1ChainRawEnd();
    }
    g_ps1DeformDraws  = g_ps1DeformDraws + 1;
    g_ps1DeformStrips = g_ps1DeformStrips + strips;
}
} // namespace

extern "C" void PS1DeformLink() { g_ps1DeformDraw = PS1DeformDraw; }
