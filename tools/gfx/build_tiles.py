#!/usr/bin/python3
"""Scene tile images for the Mania PS1 port (docs/ports/mania/32 3.1a; golden rule: build time only).

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
  - one-colour lines (docs/ports/mania/32 3.3b): per layer, each pixel row on which every tile of the layer shows one opaque
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
The header's last 6 bytes (docs/ports/mania/32 7.4): u16 lodFirst, u8 lodPagesW, lodPagesH, 2 pad: a UFO stage's floor minimap (the
Dreamcast port's LOD texture: one texel per tile of the Playfield layer, its most common opaque colour, drawn beyond the
real tiles' distance): lodPagesW x lodPagesH 4-bit texture pages (256 x 256 texels each: map tile (x, y) at page
(x >> 8, y >> 8), texel (x & 255, y & 255); blocks in the tile columns after the images'), images lodFirst.. (row-major,
u = v = 0, the page's texpage, CLUT = the last CLUT: the 15 most used colours; the other tiles' colours take the nearest
of them). lodFirst 0: none. Then (7.5) u16 patternImage (0: none): a UFO stage's "3D Floor" / "3D Roof" layer (16x16 tiles)
pre-rendered as one 256x256 8-bit texture page (the Dreamcast port's floor / roof texture; blocks after the minimap's),
drawn repeated; its image: u = v = 0, the page, CLUT 0xFFFF (the palette bank's).
PTL3 (docs/ports/mania/32 9e) = PTL2 + after the H-flip pairs: u16 rotoCount, u16 0, then rotoCount x (u8 layer, u8 0, u16 image,
u16 ox, u16 oy): the ROTO_PAGES layers, each pre-rendered as one 256x256 page (its image: u = v = 0, 4-bit on its own
CLUT or 8-bit on the bank's) holding the map's texels [ox, ox + 256) x [oy, oy + 256) (the rest of the map empty, or
the map is the page); their tiles get no images (unless another layer shows them), no one-colour table. Blocks after the
pattern page's. A scene without such layers stays PTL2 (byte-identical).
PTL4 (docs/ports/mania/32 9v.3) = PTL3 (the rotozoom table always there, count 0 when none) + u8 opaque[128]: bit t set when tile t's
16x16 pixels are all non-transparent (palette index != 0): the runtime skips background pieces such tiles of a later
foreground layer cover on screen (occlusion: the same picture). Every scene is PTL4.

PTL5 (ps1-ufo) = PTL4 + after opaque[128]: u16 stripCount (1-3), u16 clutFirst (the strips' CLUTs are the clut table's
from clutFirst on), then stripCount x (u8 layer, u8 bands, u16 rowFirst, u16 width (pixels), u16 tpage[4], u16 clut[16]
(0xFFFF: a band the strip leaves to the tiles; 0xFFFE: an empty band, nothing drawn)). STRIP_LAYERS: an H-scroll layer pre-rendered, rows [rowFirst, rowFirst
+ 16 bands), as one 4-bit image `width` texels wide across width / 256 texture columns (texel row v = layer row -
rowFirst), each 16-row band on its own CLUT when its colours fit 15; the runtime draws a band of it as 2-3 sprites
instead of ~21 tile pieces (the same pixels). A scene without one stays PTL4 (byte-identical).

Floor mip levels (ps1-ufo2, MIP_FLOORS, no format change): the Playfield's images come first (0..mip_n - 1); after the
rotozoom pages' images, each MIP_SIZES level (8x8; 4x4 too when listed) has mip_n images (image i's copy: level k's
first + i, same CLUT), then one record image: u = 'M', v = 'P', tpage = the first level's first image, clut = mip_n
(imageCount = that record's index + 1). The UFO floor's overlay reads it (PS1UfoFloor.cpp PS1FloorClutsReset).

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
# 9e (docs/ports/mania/32): rotozoom layers drawn from one pre-rendered 256x256 texture page instead of tile quads (PS1Roto.cpp's
# page path), per folder: the layers' names. A layer qualifies when its map is 256x256 texels (the page repeats over the
# plane) or its non-empty tiles fit one 256x256 block of a larger map (the page is that block, repeated with the map).
ROTO_PAGES = {'Title': ('Clouds', 'Island'), 'DAGarden': ('Clouds', 'Island'), 'ERZ': ('Background 2',)}
# 9u.18 (user 2026-10-21: the sprite resolution it may cost accepted): a paged layer whose map repeats itself every 16 tiles
# or less on both axes (ERZ's sky: 8) is one period on its page, drawn repeating at the page's own size (record flag 1)
# 9e.2: folders whose tile images drawn flipped are packed away from a page's edge texels (u or v 0 / 240): a flipped
# tile there would pass the page's edge (u - 1 or u + 16) and is split out of line (PS1Layers.cpp EmitFlipped). Holes
# in the column instead; opt-in (other scenes' .ptl unchanged): D.A. Garden's Horizon has 57 flipped tiles
# 9f (UFO 60 fps plan): folders whose layers' flipped tiles all get flipped copies (H, V and both: the pairs' keys are
# the tile, tile | 0x800, tile | 0xC00), so every band draws them as sprites instead of 9-word quads (PS1Layers.cpp);
# 128 bytes of VRAM each. Other folders keep the per-line rows' H copies only (their .ptl unchanged)
FLIP_COPIES = {'UFO1', 'UFO2', 'UFO3', 'UFO4', 'UFO5', 'UFO6', 'UFO7'}
EDGE_FREE_FLIPS = {'DAGarden', 'UFO1', 'UFO2', 'UFO3', 'UFO4', 'UFO5', 'UFO6', 'UFO7'}
# ps1-ufo: the UFO skies as a strip (PTL5): the layer, its first row (the camera shows rows ~392-631 on level ground)
# (in order of preference; ps1-ufo2: the second / third sky layers too where the columns allow)
STRIP_LAYERS = {'UFO1': (('Background 1', 384), ('Background 2', 384)),
                'UFO2': (('Background 1', 384), ('Background 2', 384)),
                'UFO3': (('Background 1', 384), ('Background 3', 384), ('Background 2', 384)),
                'UFO4': (('Background 1', 384), ('Background 2', 384)),
                'UFO5': (('Background 1', 384), ('Background 2', 384)),
                'UFO6': (('Background 1', 384), ('Background 2', 384), ('Background 3', 384)),
                'UFO7': (('Background 1', 384), ('Background 2', 384))}
STRIP_BANDS = 16
STRIP_MAX = 3              # strip records a scene (the runtime's PS1_TILE_STRIPS)
# texture columns a strip after the first must leave free (ps1-ufo2: 3, the UFO atlases build in them for every character;
# 5 where a deform sheet takes one more: UFO7's UFO_Plasma, gen_overlays EXTRAS 'deform')
STRIP_ATLAS_COLUMNS = 3
# ps1-ufo2: UFO floors whose Playfield images get mip levels (the image table's last entries; see build()), and their
# sizes (8x8 alone: a 4x4 level greyed the far track's borders, pictures 2026-10-30; (8, 4) still builds)
MIP_FLOORS = {'UFO1', 'UFO2', 'UFO3', 'UFO4', 'UFO5', 'UFO6', 'UFO7'}
MIP_SIZES = (8,)
# 63 M11: scenes whose floor layers each need a minimap, sharing one 256x256 page (Pinball's two tables). The UFO stages
# keep their single page-padded Playfield minimap.
FLOOR_MINIMAP_LAYERS = {'Pinball': ('Table Low', 'Table High')}
STRIP_ATLAS_COLUMNS_DEFORM = {'UFO7': 5}


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
    """7.4: the floor layer's minimap: per tile its most common opaque palette index, mapped to 15 codes (the most used
    colours; others to the nearest by RGB) -> (one page [256, 256] uint8, the 15 palette indices, [(layer, ox, oy)]) or
    None. 63 M11: a scene with several floor layers (Pinball's two tables) puts them side by side in ONE page -- two
    pages are 64 KB of VRAM the stage's blob cannot hold -- and returns each layer's origin in the page, in tiles."""
    shared = FLOOR_MINIMAP_LAYERS.get(folder)
    if shared:
        pf = [(i, L) for i, L in enumerate(layers) if L['type'] == 2 and L['name'] in shared]
        pf.sort(key=lambda iL: shared.index(iL[1]['name']))
    else:
        pf = [(i, L) for i, L in enumerate(layers) if L['name'] == 'Playfield' and L['type'] == 2] if folder.startswith('UFO') else []
    if not pf:
        return None
    rep = {}
    for t in used:
        px = gif[t * TILE:(t + 1) * TILE].ravel()
        nz = px[px != 0]
        rep[t] = int(np.bincount(nz).argmax()) if nz.size else 0
    counts = {}
    for i, L in pf:
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
            r0, g0, b0 = pal[i]
            best = min(top, key=lambda j: (pal[j][0] - r0) ** 2 + (pal[j][1] - g0) ** 2 + (pal[j][2] - b0) ** 2)
            code[i] = top.index(best) + 1
    texs = []
    for _, L in pf:  # at its natural size: a shared page holds several, a lone minimap still pads up to whole pages
        W, H = L['xsize'], L['ysize']
        tex = np.zeros((H, W), np.uint8)
        for y in range(H):
            row = L['tiles'][y * W:(y + 1) * W]
            for x, v in enumerate(row):
                if v != 0xFFFF:
                    tex[y, x] = code[rep.get(v & 0x3FF, 0)]
        texs.append(tex)
    if len(texs) == 1:  # a lone minimap keeps the old page-rounded size (UFO1's Playfield is 512x512, i.e. 2x2 pages)
        t = texs[0]
        pw = -(-t.shape[1] // V.PAGE) * V.PAGE
        ph = -(-t.shape[0] // V.PAGE) * V.PAGE
        pad = np.zeros((ph, pw), np.uint8)
        pad[:t.shape[0], :t.shape[1]] = t
        return pad, top, [(pf[0][0], 0, 0)]
    step = max(t.shape[1] for t in texs)
    if step * len(texs) > V.PAGE or max(t.shape[0] for t in texs) > V.PAGE:
        raise TileError('%s: the %d floor minimaps need %d texels across, more than a page' % (folder, len(texs), step * len(texs)))
    pad = np.zeros((V.PAGE, V.PAGE), np.uint8)
    origins = []
    for k, (i, L) in enumerate(pf):
        t = texs[k]
        pad[:t.shape[0], k * step:k * step + t.shape[1]] = t
        origins.append((i, k * step, 0))
    return pad, top, origins

def build(data, folder, scene_id, columns=TILE_COLUMNS):
    gif = np.array(Image.open(os.path.join(data, 'Stages', folder, '16x16Tiles.gif')))
    if gif.shape[1] != TILE or gif.shape[0] % TILE:
        raise TileError('%s: 16x16Tiles.gif is %s' % (folder, gif.shape))
    ntiles = gif.shape[0] // TILE
    layers = scene_layers(data, folder, scene_id)
    # 9e: the rotozoom layers drawn from a page (their tiles need no images unless another layer shows them)
    paged = {}                      # layer index -> (ox, oy) texels of the page's origin in the map
    periodic = set()                # 9u.18: paged layers whose map repeats every page (flag 1)
    for li, L in enumerate(layers):
        if L['name'] not in ROTO_PAGES.get(folder, ()) or L['type'] != 2:
            continue
        xs, ys = L['xsize'], L['ysize']
        if xs * TILE < V.PAGE or ys * TILE < V.PAGE or xs & (xs - 1) or ys & (ys - 1):
            raise TileError('%s%s: %s is %dx%d tiles (a page needs a power-of-two map of >= 16x16)' % (folder, scene_id, L['name'], xs, ys))
        ne = [(k % xs, k // xs) for k, v in enumerate(L['tiles']) if v != 0xFFFF]
        bx0, by0 = (min(x for x, _ in ne), min(y for _, y in ne)) if ne else (0, 0)
        bx1, by1 = (max(x for x, _ in ne), max(y for _, y in ne)) if ne else (0, 0)
        if xs * TILE == V.PAGE:
            bx0, bx1 = 0, 15
        if ys * TILE == V.PAGE:
            by0, by1 = 0, 15
        if (bx1 - bx0 >= 16 or by1 - by0 >= 16) and xs % 16 == 0 and ys % 16 == 0 and all(
                L['tiles'][y * xs + x] == L['tiles'][(y % 16) * xs + x % 16] for y in range(ys) for x in range(xs)):
            paged[li] = (0, 0)
            periodic.add(li)
            continue
        if bx1 - bx0 >= 16 or by1 - by0 >= 16:
            raise TileError('%s%s: %s: its tiles span %dx%d (more than one page)' % (folder, scene_id, L['name'], bx1 - bx0 + 1, by1 - by0 + 1))
        paged[li] = (bx0 * TILE, by0 * TILE)
    used = sorted({t & 0x3FF for li, L in enumerate(layers) if li not in paged for t in L['tiles'] if t != 0xFFFF} & set(range(ntiles)))

    images, index = [], {}          # 16x16 uint8 arrays, bytes -> image number

    def image_of(px):
        k = px.tobytes()
        if k not in index:
            index[k] = len(images)
            images.append(px)
        return index[k]

    tile_image = [0xFFFF] * 1024
    # ps1-ufo2 (MIP_FLOORS): the floor's (Playfield's) images first, images 0..mip_n - 1, so their mip levels are two runs
    # of mip_n images
    pf = [L for L in layers if L['name'] == 'Playfield' and L['type'] == 2] if folder in MIP_FLOORS else []
    for t in sorted({v & 0x3FF for L in pf for v in L['tiles'] if v != 0xFFFF} & set(used)):
        tile_image[t] = image_of(gif[t * TILE:(t + 1) * TILE])
    mip_n = len(images)
    for t in used:
        if tile_image[t] == 0xFFFF:
            tile_image[t] = image_of(gif[t * TILE:(t + 1) * TILE])
    hflip = {}                      # tile -> image of the tile H-flipped (per-line rows only)
    for t in sorted(hflip_tiles([L for li, L in enumerate(layers) if li not in paged])):
        if t < ntiles:
            hflip[t] = image_of(np.ascontiguousarray(gif[t * TILE:(t + 1) * TILE, ::-1]))
    if folder in FLIP_COPIES:  # 9f: every flipped tile of the band-drawn layers (type 0: H-scroll; the floors flip by UVs)
        for L in [L for li, L in enumerate(layers) if li not in paged and L['type'] == 0]:
            for v in L['tiles']:
                if v == 0xFFFF or not v & 0xC00 or (v & 0x3FF) >= ntiles:
                    continue
                t, f = v & 0x3FF, v & 0xC00
                key = t if f == 0x400 else t | f
                if key not in hflip:
                    px = gif[t * TILE:(t + 1) * TILE]
                    px = px[:, ::-1] if f & 0x400 else px
                    px = px[::-1, :] if f & 0x800 else px
                    hflip[key] = image_of(np.ascontiguousarray(px))
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
    flipped = set()
    if folder in EDGE_FREE_FLIPS:
        flipped = {tile_image[v & 0x3FF] for li, L in enumerate(layers) if li not in paged for v in L['tiles']
                   if v != 0xFFFF and v & 0xC00 and (v & 0x3FF) < ntiles}
    for want in (8, 4):
        for i in sorted((i for i in range(len(images)) if depth[i] == want), key=lambda i: i in flipped):
            hw = TILE * want // 16  # 8 or 4 halfwords
            while True:
                if cur[2] + hw > V.COL_HW:
                    cur[1], cur[2] = cur[1] + 1, 0
                if cur[1] == V.PAGE // TILE:
                    cur[0], cur[1], cur[2] = cur[0] + 1, 0, 0
                u, v = cur[2] * 16 // want, cur[1] * TILE
                if i not in flipped or want != 4 or (0 < u < V.PAGE - TILE and 0 < v < V.PAGE - TILE):
                    break
                cur[2] += hw  # an edge slot: left empty
            if cur[0] >= len(columns):
                raise TileError('%s%s: out of tile columns' % (folder, scene_id))
            col, row = columns[cur[0]]
            place[i] = (col, row, want, cur[2] * 16 // want, cur[1] * TILE)
            cur[2] += hw
    if folder not in EDGE_FREE_FLIPS:
        # 9u (docs/ports/mania/32): inside each page the 4-bit images keep exactly the slots the packing gave them (same pages, same
        # lines: the atlas and the VRAM plan see the same blocks), but the slots off the page's edge texels (u, v not 0 /
        # 240) go to the images drawn flipped most often: a flipped tile on an edge passes the page (u - 1 or u + 16) and is
        # split out of line (PS1Layers.cpp EmitFlipped: ~49 hblanks a frame in SPZ1; 18-53 % of the flipped cells sat on
        # edges, ~0 after this). Weight = the image's flipped cells in the band-drawn (non-paged) layers
        fw = [0] * len(images)
        for li, L in enumerate(layers):
            if li in paged:
                continue
            for v in L['tiles']:
                if v != 0xFFFF and v & 0xC00 and (v & 0x3FF) < ntiles and tile_image[v & 0x3FF] != 0xFFFF:
                    fw[tile_image[v & 0x3FF]] += 1
        by_page = {}
        for i, pl in enumerate(place):
            if pl is not None and pl[2] == 4:
                by_page.setdefault((pl[0], pl[1]), []).append(i)
        for ims in by_page.values():
            slots = [place[i] for i in ims]
            inner = [sl for sl in slots if 0 < sl[3] < V.PAGE - TILE and 0 < sl[4] < V.PAGE - TILE]
            edge = [sl for sl in slots if not (0 < sl[3] < V.PAGE - TILE and 0 < sl[4] < V.PAGE - TILE)]
            order = sorted(ims, key=lambda i: (-fw[i], i))  # the heaviest flipped images first, then the image order
            for i, sl in zip(order, inner + edge):
                place[i] = sl
    # ps1-ufo2 (user 2026-10-30: the floor's textures with mipmaps inside its real tiles' 20): images 0..mip_n - 1 at each
    # MIP_SIZES size, each texel its block's most common opaque colour (ties: the one nearest the block's RGB mean;
    # transparent when most of the block is), so thin borders keep their colours (the RGB mean snapped to the CLUT
    # turned them grey: pictures 2026-10-30); 4-bit images keep their CLUT (the floor's fog copies serve them too).
    # Packed in the slots the images left empty, then after them
    mip_px, mip_place = {}, {}      # (size, image) -> pixels, (col, row, depth, u, v)
    if mip_n:
        pal = bank0_rgb(data, folder)
        rgb = np.array([pal.get(p, (0, 0, 0)) for p in range(256)], np.float64)
        for size in MIP_SIZES:
            f = TILE // size
            for i in range(mip_n):
                blk = images[i].reshape(size, f, size, f).transpose(0, 2, 1, 3).reshape(size, size, f * f)
                m = np.zeros((size, size), np.uint8)
                for y in range(size):
                    for x in range(size):
                        o = blk[y, x][blk[y, x] != 0]
                        if o.size * 2 >= f * f:
                            c = np.bincount(o)
                            top = np.flatnonzero(c == c.max())
                            mean = rgb[o].mean(0)
                            m[y, x] = top[int(((rgb[top] - mean) ** 2).sum(1).argmin())]
                mip_px[(size, i)] = m
        # first fit in cells of 1 halfword x 4 lines: the slots the images left empty (EDGE_FREE_FLIPS' edge holes, the
        # last row's end) first, then on after the images
        last = cur[0] + (1 if cur[1] or cur[2] else 0)
        busy = np.zeros((len(columns), V.PAGE // 4, V.COL_HW), bool)
        for pl in place:
            ci = columns.index((pl[0], pl[1]))
            hx = pl[3] * pl[2] // 16
            busy[ci, pl[4] // 4:(pl[4] + TILE) // 4, hx:hx + TILE * pl[2] // 16] = True
        for size in MIP_SIZES:
            for want in (8, 4):
                hw, ch = size * want // 16, size // 4
                for i in (i for i in range(mip_n) if depth[i] == want):
                    spot = next(((ci, y, x) for ci in range(len(columns)) for y in range(0, V.PAGE // 4, ch)
                                 for x in range(0, V.COL_HW, hw) if not busy[ci, y:y + ch, x:x + hw].any()), None)
                    if spot is None:
                        raise TileError('%s%s: out of tile columns (mip levels)' % (folder, scene_id))
                    ci, y, x = spot
                    busy[ci, y:y + ch, x:x + hw] = True
                    last = max(last, ci + 1)
                    mip_place[(size, i)] = (columns[ci][0], columns[ci][1], want, x * 16 // want, y * 4)
        cur = [last - 1, 0, 0]
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
    for (size, i), (col, row, d, u, v) in mip_place.items():
        im, X0, Y0 = mip_px[(size, i)], col * V.COL_HW, row * V.PAGE + v
        if d == 8:
            words = im[:, 0::2].astype(np.uint16) | (im[:, 1::2].astype(np.uint16) << 8)
            hx = X0 + u // 2
        else:
            lut = {p: n for n, p in enumerate(clut_maps[clut_of[i]]) if n}
            lut[0] = 0
            q = np.vectorize(lambda p: lut[p])(im).astype(np.uint16)
            words = q[:, 0::4] | (q[:, 1::4] << 4) | (q[:, 2::4] << 8) | (q[:, 3::4] << 12)
            hx = X0 + u // 4
        if vram[Y0:Y0 + size, hx:hx + words.shape[1]].any():
            raise TileError('%s%s: mip %d of image %d overlaps' % (folder, scene_id, size, i))
        vram[Y0:Y0 + size, hx:hx + words.shape[1]] = words
        lines[(col, row)] = max(lines.get((col, row), 0), v + size)

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
    for (size, i), (col, row, d, u, v) in mip_place.items():
        X0, Y0 = col * V.COL_HW, row * V.PAGE + v
        if d == 8:
            w = vram[Y0:Y0 + size, X0 + u // 2:X0 + u // 2 + size // 2].astype(np.int32)
            back = np.stack([w & 0xFF, w >> 8], -1).reshape(size, size)
        else:
            w = vram[Y0:Y0 + size, X0 + u // 4:X0 + u // 4 + size // 4].astype(np.int32)
            nib = np.stack([(w >> s) & 0xF for s in (0, 4, 8, 12)], -1).reshape(size, size)
            back = np.array(clut_maps[clut_of[i]], np.int32)[nib]
        if not np.array_equal(back, mip_px[(size, i)].astype(np.int32)):
            raise TileError('%s%s: mip %d of image %d does not decode back' % (folder, scene_id, size, i))
    for t in used:
        if not np.array_equal(images[tile_image[t]], gif[t * TILE:(t + 1) * TILE]):
            raise TileError('%s%s: tile %d maps to the wrong image' % (folder, scene_id, t))

    for key, i in hflip.items():
        t = key & 0x3FF
        want = gif[t * TILE:(t + 1) * TILE]
        want = want[::-1, ::-1] if key & 0xC00 == 0xC00 else want[::-1, :] if key & 0x800 else want[:, ::-1]
        if not np.array_equal(images[i], want):
            raise TileError('%s%s: tile %d: its flipped image (key %x) is wrong' % (folder, scene_id, t, key))
    hflip_tab = b''.join(struct.pack('<HH', t, hflip[t]) for t in sorted(hflip))

    # one-colour lines, from the GIF; checked again through the stored images
    solid_tabs, solid_tiles, solid_count = [], set(), 0
    for li, L in enumerate(layers):
        c, ts = solid_lines(L, lambda t: gif[t * TILE:(t + 1) * TILE])
        if li not in paged and np.count_nonzero(c) >= SOLID_MIN:
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
    lod_count, lod_first_layer, lod_step = 0, 0, 0  # 63 M11: the floor layers sharing the page, and their origin step
    if lod is not None:
        tex, top, origins = lod
        lod_count       = len(origins)
        lod_first_layer = origins[0][0]
        lod_step        = (origins[1][1] - origins[0][1]) if len(origins) > 1 else 0
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

    # 9e: the paged rotozoom layers (ROTO_PAGES): each a 256x256 page (4-bit on its own CLUT when <= 15 colours, else
    # 8-bit on the bank's CLUT) in the columns left after the tile images, the minimap and the pattern page
    roto = []                       # (layer, image, ox, oy, (col, row), depth)
    taken = set(used_cols) | set(lod_pages) | ({pattern_page, (pattern_page[0] + 1, pattern_page[1])} if pattern_page else set())
    for li in sorted(paged):
        L, (ox, oy) = layers[li], paged[li]
        pg = np.zeros((V.PAGE, V.PAGE), np.uint8)
        for ty in range(16):
            for tx in range(16):
                v = L['tiles'][(oy // TILE + ty) * L['xsize'] + ox // TILE + tx]
                if v == 0xFFFF or (v & 0x3FF) >= ntiles:
                    continue
                px = gif[(v & 0x3FF) * TILE:((v & 0x3FF) + 1) * TILE]
                if v & 0x400:
                    px = px[:, ::-1]
                if v & 0x800:
                    px = px[::-1, :]
                pg[ty * TILE:(ty + 1) * TILE, tx * TILE:(tx + 1) * TILE] = px
        cs = sorted(set(np.unique(pg).tolist()) - {0})
        d = 4 if len(cs) <= 15 else 8
        cands = [(c, r) for (c, r) in LOD_COLUMNS if (c, r) not in taken and (d == 4 or ((c + 1, r) not in taken and c + 1 <= 15))]
        if not cands:
            raise TileError('%s%s: no free column%s for the %s page' % (folder, scene_id, '' if d == 4 else 's', L['name']))
        col, row = cands[0]
        X0, Y0 = col * V.COL_HW, row * V.PAGE
        if d == 4:
            if len(clut_maps) >= len(V.TILE_CLUT_SLOTS):
                raise TileError('%s%s: no CLUT slot for the %s page' % (folder, scene_id, L['name']))
            clut_maps.append([0] + cs + [0] * (15 - len(cs)))
            lut = np.zeros(256, np.uint16)
            for n, p in enumerate(cs):
                lut[p] = n + 1
            q = lut[pg]
            vram[Y0:Y0 + V.PAGE, X0:X0 + 64] = q[:, 0::4] | (q[:, 1::4] << 4) | (q[:, 2::4] << 8) | (q[:, 3::4] << 12)
            w = vram[Y0:Y0 + V.PAGE, X0:X0 + 64].astype(np.int32)
            nib = np.stack([(w >> s) & 0xF for s in (0, 4, 8, 12)], -1).reshape(V.PAGE, V.PAGE)
            back = np.array(clut_maps[-1], np.int32)[nib]
            taken |= {(col, row)}
            img = struct.pack('<BBHH', 0, 0, V.texpage(col, row, 4), V.clut_attr(*V.TILE_CLUT_SLOTS[len(clut_maps) - 1]))
        else:
            vram[Y0:Y0 + V.PAGE, X0:X0 + 128] = pg[:, 0::2].astype(np.uint16) | (pg[:, 1::2].astype(np.uint16) << 8)
            w = vram[Y0:Y0 + V.PAGE, X0:X0 + 128].astype(np.int32)
            back = np.stack([w & 0xFF, w >> 8], -1).reshape(V.PAGE, V.PAGE)
            taken |= {(col, row), (col + 1, row)}
            img = struct.pack('<BBHH', 0, 0, V.texpage(col, row, 8), 0xFFFF)
        if not np.array_equal(back, pg.astype(np.int32)):
            raise TileError('%s%s: the %s page does not decode back' % (folder, scene_id, L['name']))
        # the page against the map itself: every texel of the map (one period) through the page, empty outside it
        mw, mh = L['xsize'] * TILE, L['ysize'] * TILE
        for (mx, my) in ((ox + 7, oy + 250), (ox + 255, oy), (ox, oy + 255), ((ox + 300) % mw, (oy + 300) % mh)):
            v = L['tiles'][(my // TILE) * L['xsize'] + mx // TILE]
            inside = ox <= mx < ox + V.PAGE and oy <= my < oy + V.PAGE or (mw == V.PAGE and mh == V.PAGE) or li in periodic
            want = 0 if v == 0xFFFF else int(gif[(v & 0x3FF) * TILE + ((15 - my % TILE) if v & 0x800 else my % TILE),
                                                 (15 - mx % TILE) if v & 0x400 else mx % TILE])
            got = int(back[(my - oy) % V.PAGE, (mx - ox) % V.PAGE]) if inside else 0
            if want != got:
                raise TileError('%s%s: the %s page differs from the map at %d,%d' % (folder, scene_id, L['name'], mx, my))
        roto.append((li, img, ox, oy, (col, row), d))

    # ps1-ufo: the strips (STRIP_LAYERS, in order): 4-bit, one CLUT a 16-row band, in free columns (page row 0 first; the
    # atlas, built after, packs around these blocks). A strip after the first only when STRIP_ATLAS_COLUMNS stay free
    strips = []                     # (layer, rowFirst, width, [(col, row)], [clut attr or 0xFFFF], clutFirst)
    taken = set(used_cols) | set(lod_pages) | ({pattern_page, (pattern_page[0] + 1, pattern_page[1])} if pattern_page else set())
    taken |= {c for (_, _, _, _, c, _) in roto} | {(c[0] + 1, c[1]) for (_, _, _, _, c, d) in roto if d == 8}
    for name, row0 in STRIP_LAYERS.get(folder, ()):
        li = next((i for i, L in enumerate(layers) if L['name'] == name and L['type'] == 0 and i not in paged), None)
        if li is None or len(strips) == STRIP_MAX:
            continue
        L = layers[li]
        width = L['xsize'] * TILE
        if width % V.PAGE or width // V.PAGE > 4 or row0 % TILE or row0 + STRIP_BANDS * TILE > L['ysize'] * TILE:
            raise TileError('%s%s: %s can not be a strip (%d px wide, rows %d+)' % (folder, scene_id, name, width, row0))
        free = [(c, r) for r in (0, 1) for c in reversed(V.TEX_COLUMNS) if (c, r) not in taken]
        need = width // V.PAGE
        if len(free) < need or (strips and len(free) - need < STRIP_ATLAS_COLUMNS_DEFORM.get(folder, STRIP_ATLAS_COLUMNS)):
            if not strips:
                raise TileError('%s%s: no %d free columns for the %s strip' % (folder, scene_id, need, name))
            continue
        cols = free[:need]
        pic = np.zeros((STRIP_BANDS * TILE, width), np.uint8)
        for ty in range(STRIP_BANDS):
            for tx in range(L['xsize']):
                v = L['tiles'][(row0 // TILE + ty) * L['xsize'] + tx]
                if v == 0xFFFF or (v & 0x3FF) >= ntiles:
                    continue
                px = gif[(v & 0x3FF) * TILE:((v & 0x3FF) + 1) * TILE]
                px = px[:, ::-1] if v & 0x400 else px
                px = px[::-1, :] if v & 0x800 else px
                pic[ty * TILE:(ty + 1) * TILE, tx * TILE:(tx + 1) * TILE] = px
        clut_first, band_clut, band_map = len(clut_maps), [], []
        q = np.zeros(pic.shape, np.uint16)
        for b in range(STRIP_BANDS):
            band = pic[b * TILE:(b + 1) * TILE]
            cs = sorted(set(np.unique(band).tolist()) - {0})
            if not cs:  # nothing to draw on these rows (the runtime draws nothing: 0xFFFE)
                band_clut.append(0xFFFE)
                band_map.append([0] * 16)
                continue
            if len(cs) > 15 or len(clut_maps) >= len(V.TILE_CLUT_SLOTS):
                band_clut.append(0xFFFF)  # (the tiles draw it)
                band_map.append(None)
                continue
            clut_maps.append([0] + cs + [0] * (15 - len(cs)))
            band_clut.append(V.clut_attr(*V.TILE_CLUT_SLOTS[len(clut_maps) - 1]))
            band_map.append(clut_maps[-1])
            lut = np.zeros(256, np.uint16)
            for n, p in enumerate(cs):
                lut[p] = n + 1
            q[b * TILE:(b + 1) * TILE] = lut[band]
        for k, (col, row) in enumerate(cols):
            X0, Y0 = col * V.COL_HW, row * V.PAGE
            part = q[:, k * V.PAGE:(k + 1) * V.PAGE]
            vram[Y0:Y0 + STRIP_BANDS * TILE, X0:X0 + 64] = part[:, 0::4] | (part[:, 1::4] << 4) | (part[:, 2::4] << 8) | (part[:, 3::4] << 12)
        # self-check: every texel of every strip band back through its CLUT equals the layer's pixel
        for b in range(STRIP_BANDS):
            if band_map[b] is None:
                continue
            m = np.array(band_map[b], np.int32)
            for k, (col, row) in enumerate(cols):
                w = vram[row * V.PAGE + b * TILE:row * V.PAGE + (b + 1) * TILE, col * V.COL_HW:col * V.COL_HW + 64].astype(np.int32)
                nib = np.stack([(w >> sh) & 0xF for sh in (0, 4, 8, 12)], -1).reshape(TILE, V.PAGE)
                if not np.array_equal(m[nib], pic[b * TILE:(b + 1) * TILE, k * V.PAGE:(k + 1) * V.PAGE].astype(np.int32)):
                    raise TileError('%s%s: the %s strip band %d does not decode back' % (folder, scene_id, name, b))
        taken |= set(cols)
        strips.append((li, row0, width, cols, band_clut, clut_first))

    blocks = []
    for (col, row) in used_cols:
        n = lines.get((col, row), 0)
        if n:
            blocks.append((col * V.COL_HW, row * V.PAGE, V.COL_HW, n))
    for (col, row) in lod_pages:
        blocks.append((col * V.COL_HW, row * V.PAGE, V.COL_HW, V.PAGE))
    if pattern_page:
        blocks.append((pattern_page[0] * V.COL_HW, pattern_page[1] * V.PAGE, 128, V.PAGE))
    for (_, _, _, _, (col, row), d) in roto:
        blocks.append((col * V.COL_HW, row * V.PAGE, 64 if d == 4 else 128, V.PAGE))
    for st in strips:
        for (col, row) in st[3]:
            blocks.append((col * V.COL_HW, row * V.PAGE, V.COL_HW, STRIP_BANDS * TILE))
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
    roto_first = len(images) + len(lod_pages) + (1 if pattern_page else 0)
    roto_tab = b''
    for k, (li, img, ox, oy, _, _) in enumerate(roto):
        img_tab += img
        roto_tab += struct.pack('<BBHHH', li, 1 if li in periodic else 0, roto_first + k, ox, oy)
    if roto:
        roto_tab = struct.pack('<HH', len(roto), 0) + roto_tab
    if not roto:
        roto_tab = struct.pack('<HH', 0, 0)
    mip_first = roto_first + len(roto)
    for size in MIP_SIZES if mip_n else ():
        for i in range(mip_n):
            col, row, d, u, v = mip_place[(size, i)]
            img_tab += struct.pack('<BBHH', u, v, V.texpage(col, row, d),
                                   V.clut_attr(*V.TILE_CLUT_SLOTS[clut_of[i]]) if d == 4 else 0xFFFF)
    if mip_n:  # the last image: 'M', 'P', the first level's first image, mip_n (level k's first: + k * mip_n)
        img_tab += struct.pack('<BBHH', 0x4D, 0x50, mip_first, mip_n)
    opaque = bytearray(128)
    for t in range(min(ntiles, 1024)):
        if np.all(gif[t * TILE:(t + 1) * TILE] != 0):
            opaque[t >> 3] |= 1 << (t & 7)
    roto_tab += bytes(opaque)
    if strips or lod_count > 1:  # PTL5 (and PTL6 always: the runtime reads this header whenever the magic implies it)
        roto_tab += struct.pack('<HH', len(strips), strips[0][5] if strips else 0)
        for li, row0, width, cols, band_clut, _ in strips:
            tp = [V.texpage(c, r, 4) for (c, r) in cols] + [0] * (4 - len(cols))
            roto_tab += struct.pack('<BBHH4H16H', li, STRIP_BANDS, row0, width, *tp, *band_clut)
    clut_tab = b''.join(struct.pack('<HH', *V.TILE_CLUT_SLOTS[g]) + bytes(m) for g, m in enumerate(clut_maps))
    sheet_tab = b''.join(struct.pack('<32sHHH', n.encode()[:31], w, h, f) for n, w, h, f in sheets)
    cell_tab = struct.pack('<%dH' % len(cells), *cells)
    blk_tab, pixels = bytearray(), bytearray()
    for (x, y, w, h) in blocks:
        blk_tab += struct.pack('<HHHHI', x, y, w, h, len(pixels))
        pixels += vram[y:y + h, x:x + w].astype('<u2').tobytes()
    head = (32 + 2048 + len(img_tab) + len(clut_tab) + len(sheet_tab) + len(cell_tab) + len(solid_tab) + len(hflip_tab)
            + len(roto_tab) + (4 if lod_count > 1 else 0) + len(blk_tab))  # the PTL6 list sits after the strips block
    data_off = (head + 2047) // 2048 * 2048
    out += struct.pack('<4sHHHHHHIIHHBBH', b'PTL6' if lod_count > 1 else (b'PTL5' if strips else b'PTL4'), mip_first + (len(MIP_SIZES) * mip_n + 1 if mip_n else 0), len(clut_maps),
                       len(sheets), len(cells), len(blocks), len(solid_tab), data_off, len(pixels), len(hflip), lod_first,
                       lod_dims[0] if lod_pages else 0, lod_dims[1] if lod_pages else 0, pattern_image)
    out += struct.pack('<1024H', *tile_image) + img_tab + clut_tab + sheet_tab + cell_tab + solid_tab + hflip_tab + roto_tab
    if lod_count > 1:  # 63 M11: after the strips -- the order the runtime reads: tables, roto pages, opaque bits, strips, this
        out += struct.pack('<BBBB', lod_count, lod_first_layer, lod_step, 0)
    out += blk_tab
    out += bytes(data_off - len(out)) + pixels

    n4 = sum(1 for d in depth if d == 4)
    report = ('%s/Scene%s tiles %d (+%d ani cells) -> %d images (4-bit %d, 8-bit %d), CLUTs %d, columns %s, VRAM %d KB, '
              'one-colour lines %d in %d layers, H-flipped copies %d, file %d B, runtime tables %d B, self-check exact%s' % (
                  folder, scene_id, len(used), len(cells), len(images), n4, len(images) - n4, len(clut_maps),
                  ' '.join('%d/%d' % c for c in used_cols), sum(w * h for _, _, w, h in blocks) * 2 // 1024,
                  solid_count, sum(1 for c in solid_tabs if c is not None), len(hflip), len(out),
                  2048 + len(img_tab) + len(clut_tab) + len(sheet_tab) + len(cell_tab) + len(solid_tab) + len(hflip_tab),
                  '; minimap %dx%d pages (%s), CLUT %d' % (lod_dims[0], lod_dims[1], ' '.join('%d/%d' % c for c in lod_pages), lod_clut)
                  if lod_pages else '') + ('; floor mips %s of %d images (images %d-%d)' % (' + '.join('%dx%d' % (z, z) for z in MIP_SIZES), mip_n, mip_first, mip_first + len(MIP_SIZES) * mip_n) if mip_n else '')
              + ('; %s texture page %d/%d' % (pl[0]['name'], pattern_page[0], pattern_page[1]) if pattern_page else '')
              + ''.join('; %s page %d-bit %d/%d at texel %d,%d' % (layers[li]['name'], d, c[0], c[1], ox, oy)
                        for li, _, ox, oy, c, d in roto)
              + ''.join('; %s strip rows %d-%d in %s, %d of %d bands' % (layers[st[0]]['name'], st[1], st[1] + STRIP_BANDS * TILE - 1,
                                                                ' '.join('%d/%d' % c for c in st[3]),
                                                                sum(1 for c in st[4] if c < 0xFFFE), STRIP_BANDS) for st in strips))
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
