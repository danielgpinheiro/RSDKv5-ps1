"""VRAM plan of the Mania PS1 port (docs/ports/mania/32 phase 3): what the build-time graphics tools (tools/gfx) may use.

VRAM is 1024 x 512 halfwords. psyqo's two 320x240 framebuffers sit at (0, 0) and (0, 256) (nugget/psyqo/src/gpu.cpp).
Texture pages start at X multiples of 64 halfwords and Y 0 / 256: a 4-bit page spans one 64-halfword column (256
texels), an 8-bit page two (256 texels; a lone column holds an 8-bit page with u < 128). Columns 5-15 (X 320-1023) of
both page rows are the texture area. The 16 rows under each framebuffer (Y 240-255, 496-511, X 0-319) hold CLUTs:
- rows 240-247, X 0-255: the 8 palette banks as 256-entry CLUTs (8-bit textures; filled at runtime from fullPalette);
- rows 240-247, X 256-319 and rows 248-255: 4-bit CLUTs (16 entries) of the tiles;
- rows 496-511: 4-bit CLUTs of the sprites.
"""

VRAM_W, VRAM_H = 1024, 512
PAGE = 256                     # texels per page side
COL_HW = 64                    # halfwords per column
TEX_COLUMNS = list(range(5, 16))
BANK_CLUT_Y = 240              # bank b at (0, 240 + b)
TILE_CLUT_SLOTS = [(x, y) for y in range(240, 248) for x in range(256, 320, 16)] + \
                  [(x, y) for y in range(248, 256) for x in range(0, 320, 16)]
SPRITE_CLUT_SLOTS = [(x, y) for y in range(496, 512) for x in range(0, 320, 16)]
# slots an atlas may use: row 510 (slots 280-299) is the runtime's INK_TINT scratch row (4.2d), row 511 x 256-287 (318,
# 319) its bank-CLUT scratch (PS1Sprites.cpp)
SPRITE_CLUT_USABLE = 280


def texpage(col, row, depth):
    """GPU texpage attribute (psx-spx GPU "Texpage Attribute"): bits 0-3 X base / 64, bit 4 Y base / 256, bits 7-8
    colour depth (0 = 4-bit, 1 = 8-bit)."""
    return (col & 15) | (row & 1) << 4 | (0 if depth == 4 else 1) << 7


def clut_attr(x, y):
    """GPU CLUT attribute (psx-spx GPU "Clut Attribute"): bits 0-5 X / 16, bits 6-14 Y."""
    assert x % 16 == 0 and 0 <= y < 512
    return (x // 16) | y << 6


def bank_clut(bank=0):
    return clut_attr(0, BANK_CLUT_Y + bank)
