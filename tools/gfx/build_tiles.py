#!/usr/bin/python3
"""Scene tile images for the Mania PS1 port (docs/32 3.1a; golden rule: build time only).

What RSDKv5's LoadStageGIF + DrawAniTile keep in RAM (tilesetPixels: 1024 tiles x 4 flips = 1 MB), done here once
per scene, for VRAM:
  - the tiles the scene's layers use (layout word bits 0-9; flips are drawn with backwards UVs, not stored);
  - every 16x16 cell of the folder's animated-tile sheets (DrawAniTile copies whole 16x16 cells into consecutive
    tiles; the runtime re-points those tiles to the cells' images when the copy is 16-aligned);
  - identical images stored once; index 0 is transparent (v5 draws only non-zero indices);
  - an image with <= 15 colours is 4-bit (images share 16-entry CLUTs of palette indices, entry 0 transparent),
    otherwise 8-bit on the palette bank's 256-entry CLUT;
  - images packed in halfword space (an 8-bit image 8 halfwords wide, a 4-bit one 4) into the texture columns
    tools/gfx/vram_plan.py leaves to tiles; a column's images may mix depths (the depth is per primitive).
Self-check: every image is decoded back from the VRAM image + CLUT maps and compared with its tile / cell (exact).

Output <out>/<Folder>/Scene<N>.ptl (little-endian):
  header : 'PTL1', u16 imageCount, clutCount, aniSheetCount, aniCellCount, blockCount, pad, u32 dataOffset,
           u32 pixelBytes, 8 pad                                                              (32 B)
  u16 tileImage[1024]      image of each tile (0xFFFF: the scene's layers never show it)
  image  : u8 u, v, u16 tpage (GPU texpage attribute), u16 clut (GPU CLUT attribute; 0xFFFF = the bank's)  (6 B)
  clut   : u16 x, y, u8 map[16] (palette index per 4-bit value; entry 0 = transparent)          (20 B)
  sheet  : char name[32] (as LoadSpriteSheet names it), u16 cellsW, cellsH, firstCell           (38 B)
  u16 aniCell[aniCellCount] image of each cell, row-major per sheet
  block  : u16 x, y, w, h (VRAM halfwords), u32 offset at dataOffset                            (12 B)
  pixels : at dataOffset (a sector boundary), each block's w*h halfwords row-major

Usage: build_tiles.py DATA_DIR OUT_DIR FOLDER SCENE_ID... [--report]
"""
import os, struct, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import vram_plan as V          # noqa: E402
import rsdk5_formats as F      # noqa: E402

TILE = 16
# The animated-tile sheets each folder's Setup object loads (LoadSpriteSheet in <Folder>Setup_StageLoad)
ANI_SHEETS = {'GHZ': ['GHZ/AniTiles.gif']}
# Texture columns for tiles (column, page row), filled in this order: the right end of page row 1 first
TILE_COLUMNS = [(15, 1), (14, 1), (13, 1), (12, 1), (11, 1), (10, 1)]


class TileError(Exception):
    pass


def scene_tiles(data, folder, scene_id):
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    sc = F.read_stageconfig(os.path.join(data, 'Stages', folder, 'StageConfig.bin'))
    s = F.read_scene(os.path.join(data, 'Stages', folder, 'Scene%s.bin' % scene_id), g['objects'] + sc['objects'])
    used = set()
    for L in s['layers']:
        for t in L.get('tiles') or ():
            if t != 0xFFFF:
                used.add(t & 0x3FF)
    return used


def build(data, folder, scene_id, columns=TILE_COLUMNS):
    gif = np.array(Image.open(os.path.join(data, 'Stages', folder, '16x16Tiles.gif')))
    if gif.shape[1] != TILE or gif.shape[0] % TILE:
        raise TileError('%s: 16x16Tiles.gif is %s' % (folder, gif.shape))
    ntiles = gif.shape[0] // TILE
    used = sorted(t for t in scene_tiles(data, folder, scene_id) if t < ntiles)

    images, index = [], {}          # 16x16 uint8 arrays, bytes -> image number

    def image_of(px):
        k = px.tobytes()
        if k not in index:
            index[k] = len(images)
            images.append(px)
        return index[k]

    tile_image = [0xFFFF] * 1024
    for t in used:
        tile_image[t] = image_of(gif[t * TILE:(t + 1) * TILE])
    sheets = []                     # (name, cellsW, cellsH, firstCell)
    cells = []
    for name in ANI_SHEETS.get(folder, []):
        a = np.array(Image.open(os.path.join(data, 'Sprites', name)))
        H, W = a.shape
        if H % TILE or W % TILE:
            raise TileError('%s: %dx%d is not whole 16x16 cells' % (name, W, H))
        sheets.append((name, W // TILE, H // TILE, len(cells)))
        for cy in range(H // TILE):
            for cx in range(W // TILE):
                cells.append(image_of(a[cy * TILE:(cy + 1) * TILE, cx * TILE:(cx + 1) * TILE]))

    # depth and CLUT groups (greedy, as the Sonic CD atlas: biggest colour sets first, least growth)
    cols = [set(np.unique(im).tolist()) - {0} for im in images]
    depth = [4 if len(c) <= 15 else 8 for c in cols]
    clut_of = [0xFFFF] * len(images)
    groups = []
    for i in sorted((i for i in range(len(images)) if depth[i] == 4), key=lambda i: (-len(cols[i]), i)):
        best = None
        for gi, g in enumerate(groups):
            u = len(g | cols[i])
            if u <= 15 and (best is None or u - len(g) < best[1]):
                best = (gi, u - len(g))
        if best:
            groups[best[0]] |= cols[i]
            clut_of[i] = best[0]
        else:
            groups.append(set(cols[i]))
            clut_of[i] = len(groups) - 1
    if len(groups) > len(V.TILE_CLUT_SLOTS):
        raise TileError('%s%s: %d CLUTs > %d slots' % (folder, scene_id, len(groups), len(V.TILE_CLUT_SLOTS)))
    clut_maps = [[0] + sorted(g) + [0] * (15 - len(g)) for g in groups]

    # packing in halfword space: each column is 16 rows of 16 lines x 64 halfwords; an 8-bit image takes 8 halfwords
    # (u < 128 inside its column's page), a 4-bit one 4; 8-bit first so they stay on 8-halfword boundaries
    place = [None] * len(images)    # (col, row, depth, u, v)
    cur = [0, 0, 0]                 # column index, image row, halfword
    for want in (8, 4):
        for i in (i for i in range(len(images)) if depth[i] == want):
            hw = TILE * want // 16  # 8 or 4 halfwords
            if cur[2] + hw > V.COL_HW:
                cur[1], cur[2] = cur[1] + 1, 0
            if cur[1] == V.PAGE // TILE:
                cur[0], cur[1], cur[2] = cur[0] + 1, 0, 0
            if cur[0] >= len(columns):
                raise TileError('%s%s: out of tile columns' % (folder, scene_id))
            col, row = columns[cur[0]]
            place[i] = (col, row, want, cur[2] * 16 // want, cur[1] * TILE)
            cur[2] += hw
    used_cols = columns[:cur[0] + 1] if images else []
    free = [c for c in columns if c not in used_cols]

    vram = np.zeros((V.VRAM_H, V.VRAM_W), np.uint16)
    lines = {}
    for i, im in enumerate(images):
        col, row, d, u, v = place[i]
        X0, Y0 = col * V.COL_HW, row * V.PAGE + v
        if d == 8:
            words = im[:, 0::2].astype(np.uint16) | (im[:, 1::2].astype(np.uint16) << 8)
            hx = X0 + u // 2
        else:
            lut = {p: n for n, p in enumerate(clut_maps[clut_of[i]])}
            q = np.vectorize(lambda p: lut[p])(im).astype(np.uint16)
            words = q[:, 0::4] | (q[:, 1::4] << 4) | (q[:, 2::4] << 8) | (q[:, 3::4] << 12)
            hx = X0 + u // 4
        vram[Y0:Y0 + TILE, hx:hx + words.shape[1]] = words
        lines[(col, row)] = max(lines.get((col, row), 0), v + TILE)

    # self-check: decode every image back
    for i, im in enumerate(images):
        col, row, d, u, v = place[i]
        X0, Y0 = col * V.COL_HW, row * V.PAGE + v
        if d == 8:
            w = vram[Y0:Y0 + TILE, X0 + u // 2:X0 + u // 2 + 8].astype(np.int32)
            back = np.stack([w & 0xFF, w >> 8], -1).reshape(TILE, TILE)
        else:
            w = vram[Y0:Y0 + TILE, X0 + u // 4:X0 + u // 4 + 4].astype(np.int32)
            nib = np.stack([(w >> s) & 0xF for s in (0, 4, 8, 12)], -1).reshape(TILE, TILE)
            back = np.array(clut_maps[clut_of[i]], np.int32)[nib]
        if not np.array_equal(back, im.astype(np.int32)):
            raise TileError('%s%s: image %d does not decode back' % (folder, scene_id, i))
    for t in used:
        if not np.array_equal(images[tile_image[t]], gif[t * TILE:(t + 1) * TILE]):
            raise TileError('%s%s: tile %d maps to the wrong image' % (folder, scene_id, t))

    blocks = []
    for (col, row) in used_cols:
        n = lines.get((col, row), 0)
        if n:
            blocks.append((col * V.COL_HW, row * V.PAGE, V.COL_HW, n))
    out = bytearray()
    img_tab = bytearray()
    for i in range(len(images)):
        col, row, d, u, v = place[i]
        img_tab += struct.pack('<BBHH', u, v, V.texpage(col, row, d),
                               V.clut_attr(*V.TILE_CLUT_SLOTS[clut_of[i]]) if d == 4 else 0xFFFF)
    clut_tab = b''.join(struct.pack('<HH', *V.TILE_CLUT_SLOTS[g]) + bytes(m) for g, m in enumerate(clut_maps))
    sheet_tab = b''.join(struct.pack('<32sHHH', n.encode()[:31], w, h, f) for n, w, h, f in sheets)
    cell_tab = struct.pack('<%dH' % len(cells), *cells)
    blk_tab, pixels = bytearray(), bytearray()
    for (x, y, w, h) in blocks:
        blk_tab += struct.pack('<HHHHI', x, y, w, h, len(pixels))
        pixels += vram[y:y + h, x:x + w].astype('<u2').tobytes()
    head = 32 + 2048 + len(img_tab) + len(clut_tab) + len(sheet_tab) + len(cell_tab) + len(blk_tab)
    data_off = (head + 2047) // 2048 * 2048
    out += struct.pack('<4sHHHHHHII8x', b'PTL1', len(images), len(clut_maps), len(sheets), len(cells), len(blocks), 0,
                       data_off, len(pixels))
    out += struct.pack('<1024H', *tile_image) + img_tab + clut_tab + sheet_tab + cell_tab + blk_tab
    out += bytes(data_off - len(out)) + pixels

    n4 = sum(1 for d in depth if d == 4)
    report = ('%s/Scene%s tiles %d (+%d ani cells) -> %d images (4-bit %d, 8-bit %d), CLUTs %d, columns %s, VRAM %d KB, '
              'file %d B, runtime tables %d B, self-check exact' % (
                  folder, scene_id, len(used), len(cells), len(images), n4, len(images) - n4, len(clut_maps),
                  ' '.join('%d/%d' % c for c in used_cols), sum(w * h for _, _, w, h in blocks) * 2 // 1024, len(out),
                  2048 + len(img_tab) + len(clut_tab) + len(sheet_tab) + len(cell_tab)))
    return bytes(out), report, vram, blocks


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    if len(a) < 4:
        sys.exit(__doc__)
    data, outdir, folder = a[0], a[1], a[2]
    for sid in a[3:]:
        blob, report, _, _ = build(data, folder, sid)
        if '--report' not in sys.argv:
            os.makedirs(os.path.join(outdir, folder), exist_ok=True)
            open(os.path.join(outdir, folder, 'Scene%s.ptl' % sid), 'wb').write(blob)
        print(report)


if __name__ == '__main__':
    main()
