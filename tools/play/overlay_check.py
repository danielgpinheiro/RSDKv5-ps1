#!/usr/bin/env python3
"""Checks for the debug overlay (docs/ports/mania/32 "Debug overlay", ps1/debug_overlay.cpp).

  overlay_check.py files FOLDER SCENE [SUFFIX]   what the scene's files say the loads put in VRAM and SPU RAM (from
                                                 build/iso: Scene<N>.ptl / .atl headers, PS1Sfx<N>.bin's table), in the
                                                 [PERF] line's units, to compare with a run's line for that scene
  overlay_check.py perf LOG                      every [PERF] line of an emulator log: values within their totals, the
                                                 parts adding up; a summary per distinct state
  overlay_check.py vram A.bin B.bin [X Y W H]    two 1 MB VRAM dumps (tools/vramshot.py's _vram.bin): halfwords that
                                                 differ outside both framebuffers (must be 0), and inside them, whether
                                                 every difference lies within the rectangle X Y W H (the overlay's box)
"""
import os
import re
import struct
import sys

ISO = os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'iso', 'Data', 'Stages')
kb = lambda b: (b + 512) // 1024


def files(folder, scene, suffix=''):
    d = os.path.join(ISO, folder)
    ptl = open(os.path.join(d, 'Scene%s.ptl' % scene), 'rb').read()
    magic, images, cluts_t, sheets, cells, blocks, solid, data_off, tile_px = struct.unpack_from('<4sHHHHHHII', ptl, 0)
    atl = open(os.path.join(d, 'Scene%s%s.atl' % (scene, suffix)), 'rb').read()
    amagic, nfiles, cluts_a, ablocks, groups, bind_off, bind_bytes, adata_off, atlas_px = struct.unpack_from('<4sHHHHIIII', atl, 0)
    off = 32 + nfiles * 24 + cluts_a * 20 + ablocks * 12
    most, start = [0, 0], [0, 0]
    for g in range(groups):
        name, region, at_start, nb, npatch, _, btab, ptab, pix, px = struct.unpack_from('<8sBBHHHIIII', atl, off + 32 * g)
        if region < 2:
            most[region] = max(most[region], px)
            if at_start:
                start[region] = px
    extra = most[0] - start[0] + most[1] - start[1]
    clut = (cluts_t + cluts_a) * 32 + 256 * 2 + 320 * 2 + 2 * 32
    vram = 2 * 320 * 240 * 2 + tile_px + atlas_px + extra + clut
    sfx = open(os.path.join(d, 'PS1Sfx%s%s.bin' % (scene, suffix)), 'rb').read()
    smagic, count, crc = struct.unpack_from('<4sII', sfx, 0)
    bank = 0
    for e in range(count & 0xFFFF):
        name, o, size, rate, frames = struct.unpack_from('<40sIIII', sfx, 12 + 56 * e)
        bank = max(bank, o + size)
    print('%s %s%s: vram=%d (%dK) tiles=%d (%dK) atlas=%d (%dK) groups=%d (%dK) clut=%dB (+ the build stamp row on the title); '
          'spu bank=%d (%dK)' % (folder, scene, suffix, vram, kb(vram), tile_px, kb(tile_px), atlas_px, kb(atlas_px), extra,
                                kb(extra), clut, bank, kb(bank)))


def perf(log):
    lines = [l for l in open(log, errors='replace').read().split('\n') if l.startswith('[PERF]')]
    seen, bad = {}, 0
    for l in lines:
        v = dict(re.findall(r' ([a-z_]+)=([0-9/]+)', l))
        ram_u, ram_t = map(int, v['ram'].split('/'))
        parts = sum(int(v[k]) for k in ('kern', 'text', 'data', 'bss', 'heap', 'stg', 'stack'))
        checks = [ram_u <= ram_t, abs(parts - ram_u) <= 3]
        if 'vram' in v:
            vu, vt = map(int, v['vram'].split('/'))
            vparts = sum(int(v[k]) for k in ('fb', 'tiles', 'atlas', 'groups')) + int(v['clut']) / 1024
            checks += [vu <= vt, abs(vparts - vu) <= 3, v.get('overlay_vram') == '0']
        if 'spu' in v:
            su, st = map(int, v['spu'].split('/'))
            checks += [su <= st]
        if not all(checks):
            bad += 1
            print('BAD', checks, l)
        key = re.sub(r' t=\d+| fps=\S+| logic=\d+| peak=\d+| hpeak=\d+| su=\S+| overlay_hbl=\S+| overlay_prims=\S+', '', l)
        seen.setdefault(key, []).append(v['t'])
    for k, ts in seen.items():
        print('%3d x (t %s-%s) %s' % (len(ts), ts[0], ts[-1], k))
    print('%d lines, %d bad' % (len(lines), bad))
    return 1 if bad else 0


def vram(a, b, box=None):
    A, B = open(a, 'rb').read(), open(b, 'rb').read()
    assert len(A) == len(B) == 1024 * 512 * 2
    out = inside = inbox = 0
    for i in range(0, len(A), 2):
        if A[i:i + 2] != B[i:i + 2]:
            x, y = (i // 2) % 1024, (i // 2) // 1024
            fb = x < 320 and (y < 240 or 256 <= y < 496)
            if not fb:
                out += 1
            else:
                inside += 1
                yy = y if y < 256 else y - 256
                if box and box[0] <= x < box[0] + box[2] and box[1] <= yy < box[1] + box[3]:
                    inbox += 1
    print('halfwords differing: outside the framebuffers %d; inside them %d%s' % (
        out, inside, ', of them inside the box %d (outside it %d)' % (inbox, inside - inbox) if box else ''))
    return 1 if out or (box and inside != inbox) else 0


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'files':
        files(*sys.argv[2:5])
    elif cmd == 'perf':
        sys.exit(perf(sys.argv[2]))
    elif cmd == 'vram':
        sys.exit(vram(sys.argv[2], sys.argv[3], tuple(map(int, sys.argv[4:8])) if len(sys.argv) >= 8 else None))
    else:
        print(__doc__)
        sys.exit(2)
