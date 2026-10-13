#!/usr/bin/python3
"""docs/32 choice 182: the PS1's Video page (Options > Video > UNCAPPED FPS  NO / YES; phase 13b: FRAMESKIP with the
stock OFF / ON) needs labels the game's text art doesn't have. They are built here, at build time, from the game's own
letters: UI/TextEN.bin's list 17 ("PC Menu", the PC's video page: SCREEN FILTER, FULL SCREEN, VSYNC, OFF, ON, ...)
draws an 11-pixel font with a one-pixel shadow.

- The glyphs: each label of the list cut at the columns without a face pixel (a letter's columns, then its shadow's);
  every copy of a letter must be the same pixels, and every source label re-composed from the cut glyphs must equal the
  original frame pixel for pixel (the spacing rules hold).
- The new labels: composed the same way into rows added below UI/TextEN.gif (512 x 512; rows 512 and down), appended to
  list 17 as frames 21-25 (SonicMania/Objects/Menu/OptionsMenu.h PS1_TEXT_*): left-aligned like the PC's row
  labels (pivot x 0), the choices centred like OFF / ON (pivot x -w / 2).
- tools/data_disc.py writes the extended UI/TextEN.bin to the disc tree; tools/gfx/build_atlas.py reads the extended
  sheet (Sheets.get) and so places the new frames in the Menu's atlas with the rest of the list (stages place only
  lists 0, 7, 9, 10: ANIM_OVERRIDES).
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import rsdk5_formats as F  # noqa: E402

SPRITE = 'UI/TextEN.bin'
SHEET = 'UI/TextEN.gif'
LIST = 17
FACE, SHADOW = 185, 178  # the font's two colours in TextEN.gif's palette
# the labels ((list, frame): text) the glyphs are cut from: list 17's, and list 8's ("Character Names", the same 11-px
# font: MIGHTY's I / G / T / Y and KNUCKLES' N / U / C / L / E / S are the PC menu's pixel for pixel) for the M and the K
# (docs/32 phase 13b: FRAMESKIP) list 17 lacks
SOURCES = {(LIST, 0): 'EXIT', (LIST, 1): 'DEFAULT', (LIST, 2): 'SCREEN FILTER', (LIST, 3): 'NONE', (LIST, 4): 'CLEAN',
           (LIST, 7): 'WINDOW SIZE', (LIST, 12): 'BIG PICTURE', (LIST, 13): 'WINDOW BORDER', (LIST, 14): 'FULL SCREEN',
           (LIST, 15): 'VSYNC', (LIST, 16): 'TRIPLE BUFFERING', (LIST, 17): 'OFF', (LIST, 18): 'ON',
           (8, 3): 'MIGHTY', (8, 2): 'KNUCKLES'}  # (list 8's TAILS kerns its T against the A: left out)
# the new frames, in order (21-25): text ('\n': a second line below, left-aligned), centred
LABELS = [('UNCAPPED FPS\nMORE PRONE TO SLOWDOWNS', False), ('NO', True), ('YES', True),
          ('FRAMESKIP\nFULL SPEED WITH FEWER FRAMES', False),  # docs/32 phase 13b
          ('FULL RES SPRITES\nSONIC AND TAILS NEED IT', False)]  # phase 13b, consoles with the mods
FIRST_NEW = 21
LINE_PITCH = 16  # a label's lines: 11 px + 5, the second line clear of the row's bar and the choice's lower edge


def _frame_px(px, f):
    return px[f['y']:f['y'] + f['h'], f['x']:f['x'] + f['w']]


def _segments(img):
    """Column runs holding a face pixel: [(start, end)] (end inclusive)."""
    face = (img == FACE).any(axis=0)
    runs, x = [], 0
    while x < img.shape[1]:
        if face[x]:
            s = x
            while x < img.shape[1] and face[x]:
                x += 1
            runs.append((s, x - 1))
        else:
            x += 1
    return runs


def glyphs(data_root):
    """{char: cell pixels (11 rows; the letter's columns + its shadow column)}, space advance; checked."""
    spr = F.read_sprite(os.path.join(data_root, 'Sprites', SPRITE))
    px = np.array(Image.open(os.path.join(data_root, 'Sprites', SHEET)))
    assert len(spr['anims'][LIST]['frames']) == FIRST_NEW, 'list 17 changed: %d frames' % len(spr['anims'][LIST]['frames'])
    def frame(key):
        f = spr['anims'][key[0]]['frames'][key[1]]
        assert f['sheet'] == 0 and f['h'] == 11, (key, f)
        return f
    cells, space = {}, None
    for k, text in SOURCES.items():
        img = _frame_px(px, frame(k))
        assert set(np.unique(img).tolist()) <= {0, FACE, SHADOW}, (k, text)
        runs = _segments(img)
        letters = [c for c in text if c != ' ']
        assert len(runs) == len(letters), (text, len(runs), len(letters))
        i = 0
        for wi, word in enumerate(text.split(' ')):
            for ci, c in enumerate(word):
                s, e = runs[i]
                if ci:  # within a word: the next letter starts right after the previous one's shadow column
                    assert s == runs[i - 1][1] + 2, (text, c)
                elif wi:
                    adv = s - (runs[i - 1][1] + 2)
                    assert space in (None, adv), (text, space, adv)
                    space = adv
                cell = img[:, s:e + 2]
                assert c not in cells or np.array_equal(cells[c], cell), 'two different %r glyphs (%s)' % (c, text)
                cells[c] = cell
                i += 1
    assert space is not None
    # self-check: every source label re-composed equals the original frame
    for k, text in SOURCES.items():
        img = _frame_px(px, frame(k))
        assert np.array_equal(compose(text, cells, space), img), 're-composed %r differs' % text
    return cells, space


def compose_line(text, cells, space):
    parts = []
    for c in text:
        parts.append(np.zeros((11, space), np.uint8) if c == ' ' else cells[c])
    return np.concatenate(parts, axis=1)


def compose(text, cells, space):
    """One line, or lines stacked LINE_PITCH apart, left-aligned (the frame as wide as the widest)."""
    lines = [compose_line(t, cells, space) for t in text.split('\n')]
    if len(lines) == 1:
        return lines[0]
    out = np.zeros((LINE_PITCH * (len(lines) - 1) + 11, max(l.shape[1] for l in lines)), np.uint8)
    for i, l in enumerate(lines):
        out[LINE_PITCH * i:LINE_PITCH * i + 11, :l.shape[1]] = l
    return out


def new_frames(data_root):
    """[(pixels, frame dict)] for frames 21.. (their place: rows added below the sheet)."""
    cells, space = glyphs(data_root)
    h = Image.open(os.path.join(data_root, 'Sprites', SHEET)).size[1]
    out, y = [], h
    for text, centred in LABELS:
        img = compose(text, cells, space)
        fh, w = img.shape
        # pivot y -5: the first line where a one-line label sits (on the row's bar), the others below it
        out.append((img, {'sheet': 0, 'duration': 0, 'unicode': 0, 'x': 0, 'y': y, 'w': w, 'h': fh,
                          'pivot_x': -(w // 2) if centred else 0, 'pivot_y': -5, 'hitboxes': []}))
        y += fh + 1
    return out


def extend_sheet(data_root, px):
    """UI/TextEN.gif's pixels with the new labels' rows below (index 0 elsewhere: transparent)."""
    nf = new_frames(data_root)
    rows = nf[-1][1]['y'] + nf[-1][1]['h'] - px.shape[0]
    ext = np.zeros((px.shape[0] + rows, px.shape[1]), px.dtype)
    ext[:px.shape[0]] = px
    for img, f in nf:
        ext[f['y']:f['y'] + f['h'], f['x']:f['x'] + f['w']] = img
    return ext


def extend_sprite(data_root, data):
    """UI/TextEN.bin (bytes) with the new frames appended to list 17 (Animation.cpp's order; frame count updated)."""
    nf = new_frames(data_root)
    r = F.R(data)
    assert r.bytes(4) == b'SPR\0'
    total = r.u32()
    for _ in range(r.u8()):
        r.string()
    hitboxes = r.u8()
    assert hitboxes == 0
    out = bytearray(data[:4]) + struct.pack('<I', total + len(nf)) + data[8:r.p]
    anims = r.u16()
    out += struct.pack('<H', anims)
    for ai in range(anims):
        a0 = r.p
        r.string()
        count_at = r.p
        n = r.u16()
        r.bytes(4)  # speed, loop, rotation
        f0 = r.p
        r.bytes(n * 17)
        if ai != LIST:
            out += data[a0:r.p]
            continue
        out += data[a0:count_at] + struct.pack('<H', n + len(nf)) + data[count_at + 2:r.p]
        for _, f in nf:
            out += struct.pack('<BhH6h', f['sheet'], f['duration'], f['unicode'], f['x'], f['y'], f['w'], f['h'],
                               f['pivot_x'], f['pivot_y'])
        del f0
    r.done('sprite')
    return bytes(out)


def check_sprite(data_root, src_path, out_path):
    """The extended file: the source's animations unchanged, list 17 + the new frames."""
    a, b = F.read_sprite(src_path), F.read_sprite(out_path)
    nf = [f for _, f in new_frames(data_root)]
    assert (a['sheets'], a['hitboxes'], len(a['anims'])) == (b['sheets'], b['hitboxes'], len(b['anims']))
    for ai, (aa, bb) in enumerate(zip(a['anims'], b['anims'])):
        assert bb['frames'] == aa['frames'] + (nf if ai == LIST else [])
        assert {k: v for k, v in aa.items() if k not in ('frames', 'frame_count')} == \
            {k: v for k, v in bb.items() if k not in ('frames', 'frame_count')}


if __name__ == '__main__':
    # a picture of the new labels (and the glyph check): ps1_text.py [out.png]
    root = F.data_root()
    im = Image.open(os.path.join(root, 'Sprites', SHEET))
    ext = extend_sheet(root, np.array(im))
    crop = ext[im.size[1]:]
    pic = Image.fromarray(crop, 'P')
    pic.putpalette(im.getpalette())
    out = sys.argv[1] if len(sys.argv) > 1 else 'ps1_text.png'
    pic.resize((pic.size[0] * 4, pic.size[1] * 4), Image.NEAREST).save(out)
    for img, f in new_frames(root):
        print('frame: %(x)d,%(y)d %(w)dx%(h)d pivot %(pivot_x)d,%(pivot_y)d' % f)
    print('ps1_text: glyphs checked, %s' % out)
