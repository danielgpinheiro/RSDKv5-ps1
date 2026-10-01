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
  - one-colour lines (docs/32 3.3b): per layer, each pixel row on which every tile of the layer shows one opaque
    colour (its palette index; 0 = drawn normally); the runtime draws a run of such lines as one rectangle. Stored only
    for layers with >= SOLID_MIN such lines, with a bitmask of the tiles those lines use (an animated-tile copy or a
    SetTile touching them switches the table off at run time).
  - H-flipped copies (3.3b) of the tiles that per-line rows show H-flipped (a row whose 16 lines use more than one
    scroll info, or a deforming one): there a piece is one line, so with the copy it is a sprite instead of a quad.
Self-check: every image is decoded back from the VRAM image + CLUT maps and compared with its tile / cell (exact);
every one-colour line is checked again through the stored images; every H-flipped copy against the flipped tile.

Output <out>/<Folder>/Scene<N>.ptl (little-endian):
  header : 'PTL2', u16 imageCount, clutCount, aniSheetCount, aniCellCount, blockCount, solidBytes, u32 dataOffset,
           u32 pixelBytes, u16 hflipCount, 6 pad                                              (32 B)
  u16 tileImage[1024]      image of each tile (0xFFFF: the scene's layers never show it)
  image  : u8 u, v, u16 tpage (GPU texpage attribute), u16 clut (GPU CLUT attribute; 0xFFFF = the bank's)  (6 B)
  clut   : u16 x, y, u8 map[16] (palette index per 4-bit value; entry 0 = transparent)          (20 B)
  sheet  : char name[32] (as LoadSpriteSheet names it), u16 cellsW, cellsH, firstCell           (38 B)
  u16 aniCell[aniCellCount] image of each cell, row-major per sheet
  solid  : u8 layerCount, u8 0, u16 height[layerCount] (pixel rows; 0 = no table), then for each layer with a table
           u8 colour[height] (padded to even), then u8 tileMask[128] (bit t: tile t is on a one-colour line)  (solidBytes)
  hflip  : u16 tile, image [hflipCount], sorted by tile: the image of tile t H-flipped                   (4 B each)
  block  : u16 x, y, w, h (VRAM halfwords), u32 offset at dataOffset                            (12 B)
  pixels : at dataOffset (a sector boundary), each block's w*h halfwords row-major
The header's last 6 bytes (docs/32 7.4): u16 lodFirst, u8 lodPagesW, lodPagesH, 2 pad: a UFO stage's floor minimap (the
Dreamcast port's LOD texture: one texel per tile of the Playfield layer, its most common opaque colour, drawn beyond the
real tiles' distance): lodPagesW x lodPagesH 4-bit texture pages (256 x 256 texels each: map tile (x, y) at page
(x >> 8, y >> 8), texel (x & 255, y & 255); blocks in the tile columns after the images'), images lodFirst.. (row-major,
u = v = 0, the page's texpage, CLUT = the last CLUT: the 15 most used colours; the other tiles' colours take the nearest
of them). lodFirst 0: none. Then (7.5) u16 patternImage (0: none): a UFO stage's "3D Floor" / "3D Roof" layer (16x16 tiles)
pre-rendered as one 256x256 8-bit texture page (the Dreamcast port's floor / roof texture; blocks after the minimap's),
drawn repeated; its image: u = v = 0, the page, CLUT 0xFFFF (the palette bank's).

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
LOD_COLUMNS = TILE_COLUMNS + [(9, 1), (8, 1), (7, 1), (6, 1)]  # 7.4: a UFO floor's minimap pages go after the tile images


SOLID_MIN = 16  # one-colour lines a layer needs for its table


class TileError(Exception):
    pass


def scene_layers(data, folder, scene_id):
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    sc = F.read_stageconfig(os.path.join(data, 'Stages', folder, 'StageConfig.bin'))
    return F.read_scene(os.path.join(data, 'Stages', folder, 'Scene%s.bin' % scene_id), g['objects'] + sc['objects'])['layers']


def scene_tiles(data, folder, scene_id):
    used = set()
    for L in scene_layers(data, folder, scene_id):
        for t in L.get('tiles') or ():
            if t != 0xFFFF:
                used.add(t & 0x3FF)
    return used


def per_line_rows(layer):
    """Tile rows drawn one line at a time: their 16 lines use more than one scroll info, or a deforming one."""
    ls, infos = layer.get('line_scroll') or b'', layer['scroll_infos']
    rows = []
    for ty in range(layer['ysize']):
        idx = ls[ty * TILE:(ty + 1) * TILE]
        if len(idx) == TILE and (len(set(idx)) > 1 or any(i < len(infos) and infos[i][2] for i in idx)):
            rows.append(ty)
    return rows


def hflip_tiles(layers):
    """The tiles per-line rows show H-flipped."""
    out = set()
    for L in layers:
        xs = L['xsize']
        for ty in per_line_rows(L):
            for v in L['tiles'][ty * xs:(ty + 1) * xs]:
                if v != 0xFFFF and v & 0x400:
                    out.add(v & 0x3FF)
    return out


def solid_lines(layer, pixels_of):
    """Per pixel row of the layer: the palette index when every tile of the row shows that one opaque colour on the
    line (V-flips mirror the tile's rows; H-flips don't matter), else 0; and the tiles on those rows.
    pixels_of(t): the 16x16 palette indices of tile t."""
    xs, ys = layer['xsize'], layer['ysize']
    t = np.array(layer['tiles'], np.uint32).reshape(ys, xs)
    col = np.zeros(ys * TILE, np.uint8)
    tiles = set()
    for ty in range(ys):
        row = t[ty]
        if (row == 0xFFFF).any():
            continue
        ids = sorted(set((row & 0x3FF).tolist()))
        px = {i: pixels_of(i) for i in ids}
        for r in range(TILE):
            c = None
            for v in set(row.tolist()):
                line = px[v & 0x3FF][TILE - 1 - r if v & 0x800 else r]
                if line[0] == 0 or (line != line[0]).any() or (c is not None and line[0] != c):
                    c = -1
                    break
                c = int(line[0])
            if c is not None and c > 0:
                col[ty * TILE + r] = c
                tiles.update(ids)
    return col, tiles


def bank0_rgb(data, folder):
    """Palette bank 0 as the scene starts it: GameConfig's rows, the StageConfig's over them (index -> (r, g, b))."""
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))['palettes'][0]
    st = F.read_stageconfig(os.path.join(data, 'Stages', folder, 'StageConfig.bin'))['palettes'][0]
    pal = {}
    for rows in (g, st):
        for r, cs in rows.items():
            for i, c in enumerate(cs):
                pal[r * 16 + i] = c
    return pal


def lod_minimap(data, folder, layers, gif, used):
    """7.4: the Playfield layer's minimap: per tile its most common opaque palette index, mapped to 15 codes (the most
    used colours; others to the nearest by RGB). -> (code texture [H, W] uint8 padded to 256s, the 15 palette indices)
    or None."""
    pf = [L for L in layers if L['name'] == 'Playfield' and L['type'] == 2] if folder.startswith('UFO') else []
    if not pf:
        return None
    L = pf[0]
    rep = {}
    for t in used:
        px = gif[t * TILE:(t + 1) * TILE].ravel()
        nz = px[px != 0]
        rep[t] = int(np.bincount(nz).argmax()) if nz.size else 0
    counts = {}
    for v in L['tiles']:
        if v != 0xFFFF and rep.get(v & 0x3FF):
            counts[rep[v & 0x3FF]] = counts.get(rep[v & 0x3FF], 0) + 1
    top = [i for i, _ in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:15]]
    pal = bank0_rgb(data, folder)
    code = {0: 0}
    for i in counts:
        if i in top:
            code[i] = top.index(i) + 1
        else:
            c = pal.get(i, (0, 0, 0))
            code[i] = 1 + min(range(len(top)), key=lambda k: sum((a - b) ** 2 for a, b in zip(c, pal.get(top[k], (0, 0, 0)))))
    W, H = L['xsize'], L['ysize']
    tex = np.zeros((-(-H // V.PAGE) * V.PAGE, -(-W // V.PAGE) * V.PAGE), np.uint8)
    t = np.array(L['tiles'], np.int64).reshape(H, W)
    for y in range(H):
        for x in range(W):
            v = int(t[y, x])
            if v != 0xFFFF:
                tex[y, x] = code[rep.get(v & 0x3FF, 0)]
    return tex, top


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
    layers = scene_layers(data, folder, scene_id)
    hflip = {}                      # tile -> image of the tile H-flipped (per-line rows only)
    for t in sorted(hflip_tiles(layers)):
        if t < ntiles:
            hflip[t] = image_of(np.ascontiguousarray(gif[t * TILE:(t + 1) * TILE, ::-1]))
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
    # (u < 128 inside its column's page), a 4-bit one 4; 8-bit first so they stay on 8-halfword boundaries (packing the
    # per-line rows' images first was tried in 3.3b: it spread the FG tiles over more pages, a net loss)
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
            # index 0 must stay nibble 0 (transparent): the map's padding entries are 0 too, and a plain {p: n} took
            # the last of them (nibble 15), which decodes back to index 0 here but is an opaque colour on the GPU
            lut = {p: n for n, p in enumerate(clut_maps[clut_of[i]]) if n}
            lut[0] = 0
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
            if ((nib == 0) != (im == 0)).any():
                raise TileError('%s%s: image %d: a transparent texel is not nibble 0 (or an opaque one is)' % (folder, scene_id, i))
            back = np.array(clut_maps[clut_of[i]], np.int32)[nib]
        if not np.array_equal(back, im.astype(np.int32)):
            raise TileError('%s%s: image %d does not decode back' % (folder, scene_id, i))
    for t in used:
        if not np.array_equal(images[tile_image[t]], gif[t * TILE:(t + 1) * TILE]):
            raise TileError('%s%s: tile %d maps to the wrong image' % (folder, scene_id, t))

    for t, i in hflip.items():
        if not np.array_equal(images[i], gif[t * TILE:(t + 1) * TILE, ::-1]):
            raise TileError('%s%s: tile %d: its H-flipped image is wrong' % (folder, scene_id, t))
    hflip_tab = b''.join(struct.pack('<HH', t, hflip[t]) for t in sorted(hflip))

    # one-colour lines, from the GIF; checked again through the stored images
    solid_tabs, solid_tiles, solid_count = [], set(), 0
    for L in layers:
        c, ts = solid_lines(L, lambda t: gif[t * TILE:(t + 1) * TILE])
        if np.count_nonzero(c) >= SOLID_MIN:
            back, _ = solid_lines(L, lambda t: images[tile_image[t]])
            if not np.array_equal(back, c):
                raise TileError('%s%s: layer %s: one-colour lines differ through the stored images' % (folder, scene_id, L['name']))
            solid_tabs.append(c)
            solid_tiles |= ts
            solid_count += int(np.count_nonzero(c))
        else:
            solid_tabs.append(None)
    if len(layers) > 255:
        raise TileError('%s%s: %d layers' % (folder, scene_id, len(layers)))
    solid_tab = bytearray(struct.pack('<BB', len(layers), 0))
    solid_tab += b''.join(struct.pack('<H', 0 if c is None else len(c)) for c in solid_tabs)
    for c in solid_tabs:
        if c is not None:
            solid_tab += c.tobytes() + bytes(len(c) & 1)
    mask = bytearray(128)
    for t in solid_tiles:
        mask[t >> 3] |= 1 << (t & 7)
    solid_tab += mask
    if not any(c is not None for c in solid_tabs):
        solid_tab = bytearray()    # nothing to store: solidBytes 0
    if len(solid_tab) > 0xFFFF:
        raise TileError('%s%s: one-colour tables %d B' % (folder, scene_id, len(solid_tab)))

    # 7.4: a UFO floor's minimap (see the header's last bytes): 4-bit pages after the tile images' columns
    lod = lod_minimap(data, folder, layers, gif, used)
    lod_pages, lod_first, lod_clut = [], 0, None
    if lod is not None:
        tex, top = lod
        ph, pw = tex.shape[0] // V.PAGE, tex.shape[1] // V.PAGE
        free_lod = [c for c in LOD_COLUMNS if c not in used_cols]
        if len(free_lod) < pw * ph:
            raise TileError('%s%s: minimap needs %d pages, %d free' % (folder, scene_id, pw * ph, len(free_lod)))
        if len(clut_maps) >= len(V.TILE_CLUT_SLOTS):
            raise TileError('%s%s: no CLUT slot for the minimap' % (folder, scene_id))
        clut_maps.append([0] + top + [0] * (15 - len(top)))
        lod_clut = len(clut_maps) - 1
        for py in range(ph):
            for px in range(pw):
                col, row = free_lod[py * pw + px]
                q = tex[py * V.PAGE:(py + 1) * V.PAGE, px * V.PAGE:(px + 1) * V.PAGE].astype(np.uint16)
                words = q[:, 0::4] | (q[:, 1::4] << 4) | (q[:, 2::4] << 8) | (q[:, 3::4] << 12)
                vram[row * V.PAGE:(row + 1) * V.PAGE, col * V.COL_HW:(col + 1) * V.COL_HW] = words
                # decode back
                w = vram[row * V.PAGE:(row + 1) * V.PAGE, col * V.COL_HW:(col + 1) * V.COL_HW].astype(np.int32)
                nib = np.stack([(w >> s) & 0xF for s in (0, 4, 8, 12)], -1).reshape(V.PAGE, V.PAGE)
                if not np.array_equal(nib, tex[py * V.PAGE:(py + 1) * V.PAGE, px * V.PAGE:(px + 1) * V.PAGE]):
                    raise TileError('%s%s: minimap page %d,%d does not decode back' % (folder, scene_id, px, py))
                lod_pages.append((col, row))
        lod_first = len(images)
        lod_dims = (pw, ph)

    # 7.5: a "3D Floor" / "3D Roof" layer as one 256x256 8-bit page (two columns, the first even, after the minimap's)
    pattern_page, pattern_image = None, 0
    pl = [L for L in layers if L['name'] in ('3D Floor', '3D Roof') and L['type'] == 2] if folder.startswith('UFO') else []
    if pl:
        L = pl[0]
        if L['xsize'] * TILE > V.PAGE or L['ysize'] * TILE > V.PAGE:
            raise TileError('%s%s: %s is %dx%d tiles' % (folder, scene_id, L['name'], L['xsize'], L['ysize']))
        pat = np.zeros((V.PAGE, V.PAGE), np.uint8)
        t = L['tiles']
        for ty in range(L['ysize']):
            for tx in range(L['xsize']):
                v = t[ty * L['xsize'] + tx]
                if v == 0xFFFF:
                    continue
                px = gif[(v & 0x3FF) * TILE:((v & 0x3FF) + 1) * TILE]
                if v & 0x400:
                    px = px[:, ::-1]
                if v & 0x800:
                    px = px[::-1, :]
                pat[ty * TILE:(ty + 1) * TILE, tx * TILE:(tx + 1) * TILE] = px
        # the pattern repeats: a map smaller than 16x16 tiles is tiled to fill the page
        h, w = L['ysize'] * TILE, L['xsize'] * TILE
        if (V.PAGE % h) or (V.PAGE % w):
            raise TileError('%s%s: %s does not tile 256 px' % (folder, scene_id, L['name']))
        pat = np.tile(pat[:h, :w], (V.PAGE // h, V.PAGE // w))
        taken = set(used_cols) | set(lod_pages)
        cands = [(c, r) for (c, r) in LOD_COLUMNS if c % 2 == 0 and (c, r) not in taken and (c + 1, r) not in taken]
        if not cands:
            raise TileError('%s%s: no two free columns for the %s texture' % (folder, scene_id, L['name']))
        col, row = cands[0]
        words = pat[:, 0::2].astype(np.uint16) | (pat[:, 1::2].astype(np.uint16) << 8)
        vram[row * V.PAGE:(row + 1) * V.PAGE, col * V.COL_HW:col * V.COL_HW + 128] = words
        back = vram[row * V.PAGE:(row + 1) * V.PAGE, col * V.COL_HW:col * V.COL_HW + 128].astype(np.int32)
        if not np.array_equal(np.stack([back & 0xFF, back >> 8], -1).reshape(V.PAGE, V.PAGE), pat.astype(np.int32)):
            raise TileError('%s%s: the %s texture does not decode back' % (folder, scene_id, L['name']))
        pattern_page = (col, row)
        pattern_image = len(images) + len(lod_pages)

    blocks = []
    for (col, row) in used_cols:
        n = lines.get((col, row), 0)
        if n:
            blocks.append((col * V.COL_HW, row * V.PAGE, V.COL_HW, n))
    for (col, row) in lod_pages:
        blocks.append((col * V.COL_HW, row * V.PAGE, V.COL_HW, V.PAGE))
    if pattern_page:
        blocks.append((pattern_page[0] * V.COL_HW, pattern_page[1] * V.PAGE, 128, V.PAGE))
    out = bytearray()
    img_tab = bytearray()
    for i in range(len(images)):
        col, row, d, u, v = place[i]
        img_tab += struct.pack('<BBHH', u, v, V.texpage(col, row, d),
                               V.clut_attr(*V.TILE_CLUT_SLOTS[clut_of[i]]) if d == 4 else 0xFFFF)
    for (col, row) in lod_pages:
        img_tab += struct.pack('<BBHH', 0, 0, V.texpage(col, row, 4), V.clut_attr(*V.TILE_CLUT_SLOTS[lod_clut]))
    if pattern_page:
        img_tab += struct.pack('<BBHH', 0, 0, V.texpage(pattern_page[0], pattern_page[1], 8), 0xFFFF)
    clut_tab = b''.join(struct.pack('<HH', *V.TILE_CLUT_SLOTS[g]) + bytes(m) for g, m in enumerate(clut_maps))
    sheet_tab = b''.join(struct.pack('<32sHHH', n.encode()[:31], w, h, f) for n, w, h, f in sheets)
    cell_tab = struct.pack('<%dH' % len(cells), *cells)
    blk_tab, pixels = bytearray(), bytearray()
    for (x, y, w, h) in blocks:
        blk_tab += struct.pack('<HHHHI', x, y, w, h, len(pixels))
        pixels += vram[y:y + h, x:x + w].astype('<u2').tobytes()
    head = (32 + 2048 + len(img_tab) + len(clut_tab) + len(sheet_tab) + len(cell_tab) + len(solid_tab) + len(hflip_tab)
            + len(blk_tab))
    data_off = (head + 2047) // 2048 * 2048
    out += struct.pack('<4sHHHHHHIIHHBBH', b'PTL2', len(images) + len(lod_pages) + (1 if pattern_page else 0), len(clut_maps),
                       len(sheets), len(cells), len(blocks), len(solid_tab), data_off, len(pixels), len(hflip), lod_first,
                       lod_dims[0] if lod_pages else 0, lod_dims[1] if lod_pages else 0, pattern_image)
    out += struct.pack('<1024H', *tile_image) + img_tab + clut_tab + sheet_tab + cell_tab + solid_tab + hflip_tab + blk_tab
    out += bytes(data_off - len(out)) + pixels

    n4 = sum(1 for d in depth if d == 4)
    report = ('%s/Scene%s tiles %d (+%d ani cells) -> %d images (4-bit %d, 8-bit %d), CLUTs %d, columns %s, VRAM %d KB, '
              'one-colour lines %d in %d layers, H-flipped copies %d, file %d B, runtime tables %d B, self-check exact%s' % (
                  folder, scene_id, len(used), len(cells), len(images), n4, len(images) - n4, len(clut_maps),
                  ' '.join('%d/%d' % c for c in used_cols), sum(w * h for _, _, w, h in blocks) * 2 // 1024,
                  solid_count, sum(1 for c in solid_tabs if c is not None), len(hflip), len(out),
                  2048 + len(img_tab) + len(clut_tab) + len(sheet_tab) + len(cell_tab) + len(solid_tab) + len(hflip_tab),
                  '; minimap %dx%d pages (%s), CLUT %d' % (lod_dims[0], lod_dims[1], ' '.join('%d/%d' % c for c in lod_pages), lod_clut)
                  if lod_pages else '') + ('; %s texture page %d/%d' % (pl[0]['name'], pattern_page[0], pattern_page[1]) if pattern_page else ''))
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
