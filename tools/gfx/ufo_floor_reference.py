#!/usr/bin/python3
"""The PC's UFO playfield for one frame (docs/ports/mania/32 7.5 evidence): UFO_Setup_Scanline_Playfield's per-line maths (the
camera from tools/play/ufo_camera.gdb) with DrawLayerRotozoom's texel walk and each line's palette bank (the palette
dumped from RAM), over the scene's Playfield layer and tiles; drawn over a framebuffer dump (320x240 BGR555) from the
camera's clip line down, so the two can be compared side by side.
Usage: ufo_floor_reference.py DATA FOLDER CAMERA_LINE PAL.bin FB.bin OUT.png"""
import os, struct, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_tiles as B

data, folder, camline, palf, fbf, out = sys.argv[1:7]
cam = dict(kv.split('=') for kv in camline.split()[2:])
cam = {k: int(v) for k, v in cam.items()}
pal = np.frombuffer(open(palf, 'rb').read(), '<u2').reshape(8, 256).astype(np.int32)
rgb = np.stack([(pal >> 11 & 31) << 3, (pal >> 5 & 63) << 2, (pal & 31) << 3], -1).astype(np.uint8)  # RGB565
L = [l for l in B.scene_layers(data, folder, '1') if l['name'] == 'Playfield'][0]
W, H = L['xsize'], L['ysize']
wmask, hmask = (1 << (W - 1).bit_length()) * 16 - 1, (1 << (H - 1).bit_length()) * 16 - 1
lay = np.full(((hmask + 1) // 16, (wmask + 1) // 16), 0xFFFF, np.int64)
lay[:H, :W] = np.array(L['tiles'], np.int64).reshape(H, W)
gif = np.array(Image.open(os.path.join(data, 'Stages', folder, '16x16Tiles.gif')))
fb = np.frombuffer(open(fbf, 'rb').read()[:153600], '<u2').reshape(240, 320).astype(np.int32)
img = np.stack([(fb & 31) << 3, (fb >> 5 & 31) << 3, (fb >> 10 & 31) << 3], -1).astype(np.uint8)
ref = img.copy()

def s1024(a):  # RSDK Sin1024 / Cos1024 (the engine's table: round(sin * 1024))
    import math
    return int(round(math.sin((a & 1023) * math.pi / 512) * 1024))
def c1024(a):
    import math
    return int(round(math.cos((a & 1023) * math.pi / 512) * 1024))
def tdiv(a, b):  # C division
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q

sin, cos = s1024(cam['angle']) >> 2, c1024(cam['angle']) >> 2
sinX, cosX = s1024(-cam['angleX']) >> 2, c1024(-cam['angleX']) >> 2
cosVal = -120 * cosX
for i in range(-120, 120):
    y = i + 120
    div = sinX + (cosVal >> 8) or 1
    h = tdiv(cam['height'], div)
    dx, dy = (-cos * h) >> 8, (sin * h) >> 8
    pos = ((cosX * h) >> 8) - (sinX * ((i * h) >> 8) >> 8)
    bank = min(max(abs(pos) >> 15, 0), 7)
    px = (sin * pos - 160 * dx) + cam['x']
    py = (cos * pos - 160 * dy) + cam['y']
    cosVal += cosX
    if y < cam['clipY']:
        continue
    xs = np.arange(320)
    tx = ((px + xs * dx) >> 16) & wmask
    ty = ((py + xs * dy) >> 16) & hmask
    t = lay[ty >> 4, tx >> 4]
    ok = t != 0xFFFF
    tile, u, v = t & 0x3FF, tx & 15, ty & 15
    u = np.where(t & 0x400, 15 - u, u)
    v = np.where(t & 0x800, 15 - v, v)
    idx = np.where(ok, gif[np.clip(tile, 0, gif.shape[0] // 16 - 1) * 16 + v, u], 0)
    ref[y] = np.where((idx != 0)[:, None], rgb[bank][idx], 0)
o = Image.new('RGB', (640, 240))
o.paste(Image.fromarray(img), (0, 0))
o.paste(Image.fromarray(ref), (320, 0))
o.resize((1280, 480), Image.NEAREST).save(out)
print('left: the PS1 framebuffer; right: the PC playfield from the clip line (%d) down' % cam['clipY'])
