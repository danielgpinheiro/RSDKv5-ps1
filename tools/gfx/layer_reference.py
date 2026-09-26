#!/usr/bin/python3
"""Reference render of a frame's tile layers (docs/32 3.3 evidence), compared with the PS1's framebuffer.

Inputs (a GDB capture, tools/gfx/layer_reference.gdb): per layer draw call c, the scanlines (sl_<c>_<layer>.bin: 240
ScanlineInfo {position.x, position.y, deform.x, deform.y} 16.16) and gfxLineBuffer (lb_<c>.bin); the palette
(pal.bin: fullPalette[0], RGB565), the tile table (tiletable.bin: image per tile), the framebuffer the frame drew
into (fb.bin, 320 x 240 BGR555). The layouts come from the scene file, the tile images from the scene's .ptl
(decoded through its CLUT maps, not from the builder). The render follows DrawLayerHScroll's software loop: each
line from its scanline position, index 0 transparent, colours as the CLUT upload makes them (BGR555 | STP).
Bank 0 only (per-line banks are 3.5): lines on another bank are reported.

Usage: layer_reference.py CAPTURE_DIR FOLDER SCENE_ID [--png OUT_PREFIX]
"""
import glob, os, re, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import rsdk5_formats as F  # noqa: E402

DATA = os.path.join(HERE, '..', '..', '..', 'SonicMania_Extracted', 'Data')


def ps1_colour(c):
    return 0x8000 | ((c >> 11) & 31) | (((c >> 6) & 31) << 5) | ((c & 31) << 10)


def tile_images(ptl):
    """Each image of a .ptl as 16x16 palette indices, decoded from its VRAM blocks through its CLUT map."""
    d = open(ptl, 'rb').read()
    _, ni, nc, ns, ncell, nb, _, doff, _ = struct.unpack_from('<4sHHHHHHII', d, 0)
    vram = np.zeros((512, 1024), np.uint16)
    boff = 32 + 2048 + ni * 6 + nc * 20 + ns * 38 + ncell * 2
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


def render(cap, folder, scene_id):
    g = F.read_gameconfig(os.path.join(DATA, 'Game', 'GameConfig.bin'))
    sc = F.read_stageconfig(os.path.join(DATA, 'Stages', folder, 'StageConfig.bin'))
    layers = F.read_scene(os.path.join(DATA, 'Stages', folder, 'Scene%s.bin' % scene_id), g['objects'] + sc['objects'])['layers']
    imgs = tile_images(os.path.join(HERE, '..', '..', 'build', 'gfx', folder, 'Scene%s.ptl' % scene_id))
    table = struct.unpack('<1024H', open(os.path.join(cap, 'tiletable.bin'), 'rb').read())
    pal = np.frombuffer(open(os.path.join(cap, 'pal.bin'), 'rb').read(), '<u2')
    colour = np.array([0] + [ps1_colour(int(c)) for c in pal[1:]], np.uint16)
    out = np.zeros((240, 320), np.uint16)
    other_bank = 0
    calls = sorted(glob.glob(os.path.join(cap, 'sl_*_*.bin')), key=lambda p: int(re.findall(r'sl_(\d+)_', p)[0]))
    for path in calls:
        c, li = map(int, re.findall(r'sl_(\d+)_(\d+)', path)[0])
        L = layers[li]
        xs, ys = L['xsize'], L['ysize']
        tiles = np.array(L['tiles'], np.uint32).reshape(ys, xs)
        sl = np.frombuffer(open(path, 'rb').read(), '<i4').reshape(240, 4)
        lb = open(os.path.join(cap, 'lb_%d.bin' % c), 'rb').read()
        for y in range(240):
            if lb[y]:
                other_bank += 1
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
    return out, len(calls), other_bank


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    cap, folder, sid = a[0], a[1], a[2]
    exp, ncalls, other_bank = render(cap, folder, sid)
    got = np.frombuffer(open(os.path.join(cap, 'fb.bin'), 'rb').read(), '<u2').reshape(240, 320)
    diff = got != exp
    print('%d layer draws; %d of 76800 pixels differ; lines on a bank other than 0: %d' % (ncalls, int(diff.sum()), other_bank))
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
