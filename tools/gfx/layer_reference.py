#!/usr/bin/python3
"""Reference render of a frame's tile layers (docs/32 3.3 evidence), compared with the PS1's framebuffer.

Inputs (a GDB capture, tools/gfx/layer_reference.gdb): per layer draw call c, the scanlines (sl_<c>_<layer>.bin: 240
ScanlineInfo {position.x, position.y, deform.x, deform.y} 16.16) and gfxLineBuffer (lb_<c>.bin); the palette
(pal.bin: fullPalette[0], RGB565), the tile table (tiletable.bin: image per tile), the framebuffer the frame drew
into (fb.bin, 320 x 240 BGR555). The layouts come from the scene file, the tile images from the scene's .ptl
(decoded through its CLUT maps, not from the builder). The render follows DrawLayerHScroll's software loop: each
line from its scanline position, index 0 transparent, colours as the CLUT upload makes them (BGR555 | STP).
Bank 0 only (per-line banks are 3.5): lines on another bank are reported.

Sprites (3.4): with drawlog.bin (the frame's draw calls in order, 8 MB link: PS1Sprites.cpp g_ps1DrawLog), layers and
sprites are composited in that order; each sprite's texels come from the scene's .atl (the default blocks: the state
at the scene start, before any group swap; CLUT maps), fetched as the GPU is told to (binding u / v / attr, flips as the port's mirror rule:
texel u + w - 1 - i). Half-resolution frames depend on the GPU's UV interpolation at 2x: their pixels are rendered
approximately and their differences reported apart.

Usage: layer_reference.py CAPTURE_DIR FOLDER SCENE_ID [--png OUT_PREFIX]
"""
import glob, os, re, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import rsdk5_formats as F  # noqa: E402

DATA = os.path.join(HERE, '..', '..', '..', 'SonicMania_Extracted', 'Data')


MASK_PS1 = None  # the mask colour as a PS1 pixel (bit 15 clear), set per capture


def ps1_colour(c):
    return 0x8000 | ((c >> 11) & 31) | (((c >> 6) & 31) << 5) | ((c & 31) << 10)


def tile_images(ptl):
    """Each image of a .ptl as 16x16 palette indices, decoded from its VRAM blocks through its CLUT map."""
    d = open(ptl, 'rb').read()
    magic, ni, nc, ns, ncell, nb, solid, doff, _, nhf = struct.unpack_from('<4sHHHHHHIIH', d, 0)
    assert magic == b'PTL2', magic
    vram = np.zeros((512, 1024), np.uint16)
    # the one-colour tables and the H-flip pairs sit before the blocks
    boff = 32 + 2048 + ni * 6 + nc * 20 + ns * 38 + ncell * 2 + solid + 4 * nhf
    for i in range(nb):
        x, y, w, h, o = struct.unpack_from('<HHHHI', d, boff + 12 * i)
        vram[y:y + h, x:x + w] = np.frombuffer(d, '<u2', w * h, doff + o).reshape(h, w)
    cluts = {}
    for i in range(nc):
        x, y = struct.unpack_from('<HH', d, 32 + 2048 + ni * 6 + 20 * i)
        cluts[(x // 16) | (y << 6)] = list(d[32 + 2048 + ni * 6 + 20 * i + 4:32 + 2048 + ni * 6 + 20 * i + 20])
    imgs = []
    for i in range(ni):
        u, v, tpage, clut = struct.unpack_from('<BBHH', d, 32 + 2048 + 6 * i)
        col, row, depth8 = tpage & 15, (tpage >> 4) & 1, (tpage >> 7) & 1
        X0, Y0 = col * 64, row * 256 + v
        if depth8:
            w = vram[Y0:Y0 + 16, X0 + u // 2:X0 + u // 2 + 8].astype(np.int32)
            imgs.append(np.stack([w & 0xFF, w >> 8], -1).reshape(16, 16))
        else:
            w = vram[Y0:Y0 + 16, X0 + u // 4:X0 + u // 4 + 4].astype(np.int32)
            nib = np.stack([(w >> s) & 0xF for s in (0, 4, 8, 12)], -1).reshape(16, 16)
            imgs.append(np.array(cluts[clut], np.int32)[nib] * (nib != 0))
    return imgs


def atlas_vram(path):
    """The .atl's default VRAM image (the state at the scene start) and its CLUT maps (tools/gfx/build_atlas.py)."""
    d = open(path, 'rb').read()
    magic, nf, nc, nb, ng, bind_off, bind_bytes, data_off, pix_bytes = struct.unpack_from('<4sHHHHIIII', d, 0)
    assert magic == b'ATL5', magic
    off = 32 + 24 * nf
    cluts = []
    for i in range(nc):
        cluts.append(list(d[off + 4:off + 20]))
        off += 20
    vram = np.zeros((512, 1024), np.uint16)
    for i in range(nb):
        x, y, w, h, o = struct.unpack_from('<HHHHI', d, off)
        off += 12
        vram[y:y + h, x:x + w] = np.frombuffer(d, '<u2', w * h, data_off + o).reshape(h, w)
    # the default blocks already hold the groups loaded at the start (their own blocks are for later swaps)
    return vram, cluts


def ink_mode(ink, alpha):
    """PS1Sprites.cpp's ink rules: (semi-transparency mode or -1, passes, modulation) or None (not drawn / not done)."""
    if ink == 0:
        return -1, 1, 0x80
    if ink == 1:
        return 0, 1, 0x80
    if ink == 2:
        level = 0 if alpha < 64 else 1 if alpha < 160 else 2 if alpha < 224 else 3
        return None if level == 0 else (-1, 1, 0x80) if level == 3 else (0, level, 0x80)
    if ink in (3, 4):
        return None if alpha <= 0 else (1 if ink == 3 else 2, 1, (min(alpha, 255) + 1) >> 1)
    if ink == 6:  # INK_MASKED: only where the framebuffer holds the mask colour (the PC's test)
        return None if MASK_PS1 is None else (-2, 1, 0x80)
    return None


def blend(b, f, mode):
    """psx-spx semi-transparency on 5-bit channels (BGR555 words, bit 15 kept)."""
    out = 0x8000
    for s in (0, 5, 10):
        bc, fc = (b >> s) & 31, (f >> s) & 31
        c = (bc + fc) >> 1 if mode == 0 else min(31, bc + fc) if mode == 1 else max(0, bc - fc) if mode == 2 else min(31, bc + (fc >> 2))
        out |= c << s
    return out


def modulate(f, m):
    """Texture modulation (psx-spx): texel * colour / 0x80 per channel, saturated, dither off."""
    if m == 0x80:
        return f
    out = f & 0x8000
    for s in (0, 5, 10):
        out |= min(31, (((f >> s) & 31) * m) >> 7) << s
    return out


def draw_sprite(out, mask, e, vram, cluts, colour):
    """One draw-log sprite onto out (BGR555 | STP); mask marks its pixels (half-resolution frames: 2)."""
    _, direction, ink, alpha, u, v, attr, x, y, w, h = e
    im = ink_mode(ink, alpha)
    if im is None:
        return
    mode, passes, mod = im
    col, row, bit8, half, slot = attr & 15, (attr >> 4) & 1, (attr >> 5) & 1, (attr >> 6) & 1, attr >> 7
    if half and mode != -2:  # the GPU's 2x sampling decides which edge pixels a halved frame covers: its whole rectangle
        # is approximate (a masked frame: only the pixels it draws, so a draw outside the mask colour's pixels counts)
        mask[max(0, y):max(0, min(240, y + h)), max(0, x):max(0, min(320, x + w))] = 2
    px0, py0 = col * 64, row * 256
    fx, fy = direction & 1, direction & 2
    tw, th = ((w + 1) >> 1, (h + 1) >> 1) if half else (w, h)
    for j in range(h):
        sy = y + j
        if sy < 0 or sy >= 240:
            continue
        if half:  # the quad's linear map, sampled at the pixel's centre (approximate)
            tv = (v + th - 1 - int((j + 0.5) * th / h)) if fy else (v + int((j + 0.5) * th / h))
        else:
            tv = v + (h - 1 - j if fy else j)
        for i in range(w):
            sx = x + i
            if sx < 0 or sx >= 320:
                continue
            if half:
                tu = (u + tw - 1 - int((i + 0.5) * tw / w)) if fx else (u + int((i + 0.5) * tw / w))
            else:
                tu = u + (w - 1 - i if fx else i)
            if bit8:
                idx = (int(vram[py0 + tv, px0 + (tu >> 1)]) >> (8 * (tu & 1))) & 0xFF
            else:
                nib = (int(vram[py0 + tv, px0 + (tu >> 2)]) >> (4 * (tu & 3))) & 15
                idx = cluts[slot][nib] if nib else 0
            if idx:
                f = modulate(int(colour[idx]), mod)
                # 4.2b: a masked draw over an approximate pixel (a half-resolution mask frame's edge) is approximate
                under = mode == -2 and mask[sy, sx] == 2
                if mode == -2:
                    if int(out[sy, sx]) == MASK_PS1:
                        out[sy, sx] = f
                elif mode < 0:
                    out[sy, sx] = f
                else:
                    for _ in range(passes):
                        out[sy, sx] = blend(int(out[sy, sx]), f, mode)
                mask[sy, sx] = 2 if (half or under) else 1


def texel_atlas(vram, cluts, attr, tu, tv):
    col, row, bit8, slot = attr & 15, (attr >> 4) & 1, (attr >> 5) & 1, attr >> 7
    if bit8:
        return (int(vram[row * 256 + tv, col * 64 + (tu >> 1)]) >> (8 * (tu & 1))) & 0xFF
    nib = (int(vram[row * 256 + tv, col * 64 + (tu >> 2)]) >> (4 * (tu & 3))) & 15
    return cluts[slot][nib] if nib else 0


def draw_tile(out, mask, e, imgs, table, colour):
    """A DrawTile piece (type 2): the 16x16 tile image with its flips, as the layers draw tiles (exact)."""
    _, direction, ink, alpha, _, _, tile, x, y, w, h = e
    im = ink_mode(ink, alpha)
    img = table[tile & 0x3FF]
    if im is None or img == 0xFFFF:
        return
    mode, passes, mod = im
    for j in range(16):
        for i in range(16):
            sx, sy = x + i, y + j
            if not (0 <= sx < 320 and 0 <= sy < 240):
                continue
            p = int(imgs[img][15 - j if direction & 2 else j, 15 - i if direction & 1 else i])
            if p:
                f = modulate(int(colour[p]), mod)
                out[sy, sx] = f if mode < 0 else blend(int(out[sy, sx]), f, mode)
                if mode >= 0 and passes == 2:
                    out[sy, sx] = blend(int(out[sy, sx]), f, mode)
                mask[sy, sx] = 1


def draw_quad(out, mask, corners, uvs, fetch, im, colour):
    """A rotated / scaled rectangle (approximate): the GPU's two triangles (A B C, B C D) with affine UVs; mask 2."""
    mode, passes, mod = im
    xs = [c[0] for c in corners]; ys = [c[1] for c in corners]
    def tri(a, b, c):
        (x0, y0), (x1, y1), (x2, y2) = corners[a], corners[b], corners[c]
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if den == 0:
            return
        for sy in range(max(0, min(y0, y1, y2)), min(240, max(y0, y1, y2))):
            for sx in range(max(0, min(x0, x1, x2)), min(320, max(x0, x1, x2))):
                px, py = sx + 0.5, sy + 0.5
                l0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / den
                l1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / den
                l2 = 1 - l0 - l1
                if l0 < 0 or l1 < 0 or l2 < 0:
                    continue
                mask[sy, sx] = 2  # the whole quad is approximate (the GPU's edges and sampling)
                tu = int(l0 * uvs[a][0] + l1 * uvs[b][0] + l2 * uvs[c][0] + 0.5)
                tv = int(l0 * uvs[a][1] + l1 * uvs[b][1] + l2 * uvs[c][1] + 0.5)
                idx = fetch(tu, tv)
                if idx:
                    f = modulate(int(colour[idx]), mod)
                    out[sy, sx] = f if mode < 0 else blend(int(out[sy, sx]), f, mode)
                    mask[sy, sx] = 2
    xs0, ys0 = [c[0] for c in corners], [c[1] for c in corners]
    mask[max(0, min(ys0)):max(0, min(240, max(ys0) + 1)), max(0, min(xs0)):max(0, min(320, max(xs0) + 1))] = 2
    tri(0, 1, 2)
    tri(1, 2, 3)


def render(cap, folder, scene_id):
    g = F.read_gameconfig(os.path.join(DATA, 'Game', 'GameConfig.bin'))
    sc = F.read_stageconfig(os.path.join(DATA, 'Stages', folder, 'StageConfig.bin'))
    layers = F.read_scene(os.path.join(DATA, 'Stages', folder, 'Scene%s.bin' % scene_id), g['objects'] + sc['objects'])['layers']
    imgs = tile_images(os.path.join(HERE, '..', '..', 'build', 'gfx', folder, 'Scene%s.ptl' % scene_id))
    tt = os.path.join(cap, 'tiletable.bin')  # none when the scene has no tile images loaded
    table = struct.unpack('<1024H', open(tt, 'rb').read()) if os.path.exists(tt) else (0xFFFF,) * 1024
    pal = np.frombuffer(open(os.path.join(cap, 'pal.bin'), 'rb').read(), '<u2')
    mk = os.path.join(cap, 'mask.bin')  # maskColor (RGB565): its CLUT entries keep STP clear (4.1b)
    mask_c = struct.unpack('<i', open(mk, 'rb').read())[0] & 0xFFFF if os.path.exists(mk) else 0
    def clut_colour(c):
        p = ps1_colour(c)
        return p & 0x7FFF if mask_c and c == mask_c and (p & 0x7FFF) else p
    colour = np.array([0] + [clut_colour(int(c)) for c in pal[1:]], np.uint16)
    global MASK_PS1
    MASK_PS1 = (ps1_colour(mask_c) & 0x7FFF) if mask_c else None
    # the frame's clear: black; while a mask colour is set the port forces bit 15 over it (4.1b)
    out = np.full((240, 320), 0x8000 if mask_c else 0, np.uint16)
    other_bank = 0
    calls = sorted(glob.glob(os.path.join(cap, 'sl_*_*.bin')), key=lambda p: int(re.findall(r'sl_(\d+)_', p)[0]))
    mask = np.zeros((240, 320), np.uint8)  # 1 = a sprite pixel, 2 = approximate (half-res / rotated sprite, GPU line / face)
    log_path = os.path.join(cap, 'drawlog.bin')
    order = [('layer', p) for p in calls]
    nsprites = 0
    if os.path.exists(log_path):
        d = open(log_path, 'rb').read()
        entries = [struct.unpack_from('<BBBBBBHhhHH', d, k) for k in range(0, len(d) - 15, 16)]
        order, k, n = [], 0, 0
        while n < len(entries):
            e = entries[n]
            if e[0] == 0:
                if k < len(calls):
                    order.append(('layer', calls[k]))
                k += 1
            elif e[0] in (3, 6) and n + 2 < len(entries):
                order.append(('quad', (e, entries[n + 1], entries[n + 2])))
                nsprites += 1
                n += 2
            elif e[0] in (1, 2):
                order.append(('sprite' if e[0] == 1 else 'tile', e))
                nsprites += 1
            elif e[0] == 7:
                order.append(('flat', e))
                nsprites += 1
            elif e[0] == 8:
                order.append(('fill', e))
            elif e[0] in (9, 10):  # a GPU line (x1, y1, x2, y2) / gouraud face (bounding box): approximate pixels
                order.append(('approx', e))
            n += 1
        vram, cluts = atlas_vram(os.path.join(HERE, '..', '..', 'build', 'iso', 'Data', 'Stages', folder, 'Scene%s.atl' % scene_id))
    for kind, path in order:
        if kind == 'sprite':
            draw_sprite(out, mask, path, vram, cluts, colour)
            continue
        if kind == 'tile':
            draw_tile(out, mask, path, imgs, table, colour)
            continue
        if kind == 'fill':  # FillScreen: every pixel so far, per 5-bit channel c' = (c * (255 - a) >> 8) + (f * a >> 8)
            _, _, _, _, r8, g8, b8, ar, ag, ab, _ = path
            cr, cg, cb = (r8 >> 3) * ar >> 8, (g8 >> 3) * ag >> 8, (b8 >> 3) * ab >> 8
            o = out.astype(np.int32)
            R = ((o & 31) * (255 - ar) >> 8) + cr
            G = (((o >> 5) & 31) * (255 - ag) >> 8) + cg
            B = (((o >> 10) & 31) * (255 - ab) >> 8) + cb
            out[:, :] = ((o & 0x8000) | R | G << 5 | B << 10).astype(np.uint16)
            continue
        if kind == 'approx':  # the GPU rasterises these itself: their pixels (a line widened by one) are not counted
            s16 = lambda v: struct.unpack('<h', struct.pack('<H', v))[0]
            x, y, a9, a10 = path[7], path[8], s16(path[9]), s16(path[10])
            if path[0] == 9:
                n = max(abs(a9 - x), abs(a10 - y), 1)
                for i in range(n + 1):
                    px, py = x + (a9 - x) * i // n, y + (a10 - y) * i // n
                    mask[max(0, py - 1):max(0, min(240, py + 2)), max(0, px - 1):max(0, min(320, px + 2))] = 2
            else:
                mask[max(0, y):max(0, min(240, y + a10)), max(0, x):max(0, min(320, x + a9))] = 2
            continue
        if kind == 'flat':  # a flat rectangle as sent (the GPU keeps each channel's top 5 bits; mask bit 0)
            _, mode1, passes, masked, r8, g8, b8, x, y, w, h = path
            f = (r8 >> 3) | (g8 >> 3) << 5 | (b8 >> 3) << 10
            # 4.2b: with a mask colour set, bit 15 is forced unless the PC would store the mask colour (RGB565) there;
            # `masked` (INK_MASKED): only over the mask colour's pixels
            c565 = (r8 >> 3) << 11 | (g8 >> 2) << 5 | (b8 >> 3)
            bit = 0x8000 if (MASK_PS1 is not None and c565 != mask_c) else 0
            for sy in range(max(0, y), min(240, y + h)):
                for sx in range(max(0, x), min(320, x + w)):
                    under = masked and mask[sy, sx] == 2  # over an approximate pixel: approximate (as masked sprites)
                    if masked and int(out[sy, sx]) != MASK_PS1:
                        continue
                    if mode1 == 0:
                        out[sy, sx] = f | bit
                    else:
                        for _ in range(passes):
                            out[sy, sx] = (blend(int(out[sy, sx]), f, mode1 - 1) & 0x7FFF) | bit
                    mask[sy, sx] = 2 if under else 1
            continue
        if kind == 'quad':
            a, b, d = path
            im = ink_mode(a[2], a[3])
            if im is None:
                continue
            corners = [(a[7], a[8]), (b[7], b[8]), (struct.unpack('<h', struct.pack('<H', b[9]))[0],
                       struct.unpack('<h', struct.pack('<H', b[10]))[0]), (d[7], d[8])]
            w, h, direction = a[9], a[10], a[1]
            if a[0] == 3:
                attr, u, v = a[6], a[4], a[5]
                half = (attr >> 6) & 1
                tw, th = ((w + 1) >> 1, (h + 1) >> 1) if half else (w, h)
                fetch = lambda tu, tv, attr=attr: texel_atlas(vram, cluts, attr, tu, tv)
            else:
                img = table[a[6] & 0x3FF]
                if img == 0xFFFF:
                    continue
                u, v, tw, th = 0, 0, 16, 16
                fetch = lambda tu, tv, img=img: int(imgs[img][min(15, max(0, tv)), min(15, max(0, tu))])
            u0, u1 = (u + tw - 1, u) if direction & 1 else (u, u + tw - 1)
            uvs = [(u0, v), (u1, v), (u0, v + th - 1), (u1, v + th - 1)]
            draw_quad(out, mask, corners, uvs, fetch, im, colour)
            continue
        c, li = map(int, re.findall(r'sl_(\d+)_(\d+)', path)[0])
        L = layers[li]
        xs, ys = L['xsize'], L['ysize']
        tiles = np.array(L['tiles'], np.uint32).reshape(ys, xs)
        sl = np.frombuffer(open(path, 'rb').read(), '<i4').reshape(240, 4)
        lb = open(os.path.join(cap, 'lb_%d.bin' % c), 'rb').read()
        cp = os.path.join(cap, 'clip_%d.bin' % c)  # the call's clip bounds (X1, Y1, X2, Y2): lines outside aren't drawn
        cx1, cy1, cx2, cy2 = struct.unpack('<4i', open(cp, 'rb').read()) if os.path.exists(cp) else (0, 0, 320, 240)
        if L.get('type') == 2:  # LAYER_ROTOZOOM (the PC's per-pixel loop; the GPU's tile quads: approximate)
            wsh, hsh = max(0, (xs - 1).bit_length()), max(0, (ys - 1).bit_length())
            wm, hm = (1 << wsh) - 1, (1 << hsh) - 1
            s32 = lambda v: ((v + (1 << 31)) & 0xFFFFFFFF) - (1 << 31)
            drawn = np.zeros((240, 320), bool)
            for y in range(max(0, cy1), min(240, cy2)):
                px, py, dx, dy = (int(v) for v in sl[y])
                for sx in range(max(0, cx1), min(320, cx2)):
                    X, Y = s32(px + (sx - cx1) * dx), s32(py + (sx - cx1) * dy)
                    tt = int(tiles[(Y >> 20) & hm, (X >> 20) & wm]) if ((Y >> 20) & hm) < ys and ((X >> 20) & wm) < xs else 0xFFFF
                    if tt == 0xFFFF:
                        continue
                    img = table[tt & 0x3FF]
                    if img == 0xFFFF:
                        continue
                    tx, tyy = (X >> 16) & 15, (Y >> 16) & 15
                    if tt & 0x400:
                        tx = 15 - tx
                    if tt & 0x800:
                        tyy = 15 - tyy
                    p = int(imgs[img][tyy, tx])
                    if p:
                        out[y, sx] = colour[p]
                        mask[y, sx] = 2
                        drawn[y, sx] = True
            # the GPU's affine tiles move the layer's silhouette by a pixel or two: its neighbourhood is approximate too
            near = drawn.copy()
            for dy in (-2, -1, 1, 2):
                near |= np.roll(drawn, dy, axis=0)
            for dx in (-2, -1, 1, 2):
                near |= np.roll(drawn, dx, axis=1)
            clip = np.zeros_like(drawn)
            clip[max(0, cy1):min(240, cy2), max(0, cx1):min(320, cx2)] = True
            mask[near & clip] = 2
            continue
        basic = L.get('type') == 3  # LAYER_BASIC: the first line's scroll position for every line, bank 0
        for y in range(240):
            if y < cy1 or y >= cy2:
                continue
            if lb[y] and not basic:
                other_bank += 1
            if basic:
                x0 = (sl[cy1, 0] >> 16) % (xs * 16)
                ly = ((sl[cy1, 1] >> 16) + y - cy1) % (ys * 16)
            else:
                x0 = (sl[y, 0] >> 16) % (xs * 16)
                ly = (sl[y, 1] >> 16) % (ys * 16)
            lx = (x0 + np.arange(320)) % (xs * 16)
            t = tiles[ly >> 4, lx >> 4]
            for sx in range(320):
                tt = int(t[sx])
                if tt == 0xFFFF:
                    continue
                img = table[tt & 0x3FF]
                if img == 0xFFFF:
                    continue
                tx, tyy = int(lx[sx]) & 15, ly & 15
                if tt & 0x400:
                    tx = 15 - tx
                if tt & 0x800:
                    tyy = 15 - tyy
                p = int(imgs[img][tyy, tx])
                if p:
                    out[y, sx] = colour[p]
                    mask[y, sx] = 0
    return out, len(calls), other_bank, mask, nsprites


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    cap, folder, sid = a[0], a[1], a[2]
    exp, ncalls, other_bank, mask, nsprites = render(cap, folder, sid)
    got = np.frombuffer(open(os.path.join(cap, 'fb.bin'), 'rb').read(), '<u2').reshape(240, 320)
    diff_all = got != exp
    diff = diff_all & (mask != 2)
    print('%d layer draws, %d sprites; %d of 76800 pixels differ (%d where half-resolution / rotated sprites, GPU lines or gouraud faces draw: approximate, not counted); '
          'lines on a bank other than 0: %d' % (ncalls, nsprites, int(diff.sum()), int((diff_all & (mask == 2)).sum()), other_bank))
    if diff.any():
        ys, xs = np.nonzero(diff)
        print('  first differences (y, x, got, expected):', [(int(y), int(x), hex(int(got[y, x])), hex(int(exp[y, x]))) for y, x in zip(ys[:6], xs[:6])])
        rows = sorted(set(ys.tolist()))
        print('  rows with differences: %d (%s ...)' % (len(rows), rows[:12]))
    if '--png' in sys.argv:
        from PIL import Image
        pre = sys.argv[sys.argv.index('--png') + 1]
        for name, img in (('got', got), ('expected', exp), ('diff', np.where(diff, 0x801F, 0).astype(np.uint16))):
            a32 = img.astype(np.uint32)
            rgb = np.stack([(a32 & 31) << 3, ((a32 >> 5) & 31) << 3, ((a32 >> 10) & 31) << 3], -1).astype(np.uint8)
            Image.fromarray(rgb).save('%s_%s.png' % (pre, name))
    return 0 if not diff.any() else 1


if __name__ == '__main__':
    sys.exit(main())
