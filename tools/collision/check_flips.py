#!/usr/bin/env python3
"""Proof for docs/32 2.3b: the PS1 derives the flipped collision masks on access instead of storing them.
For every tile of every TileConfig (both planes) this computes the base masks and the X/Y/XY flips exactly as
RSDKv5/RSDK/Scene/Scene.cpp LoadTileConfig does, then compares each flipped value with the accessor formulas of
RSDKv5/RSDK/Scene/PS1/PS1Collision.hpp. Usage: check_flips.py [SRC_DATA]
"""
import glob, os, struct, sys, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import rsdk5_formats as F


def base_masks(heights, active, yflip):
    floor, roof, lw, rw = [0] * 16, [0] * 16, [0] * 16, [0] * 16
    if yflip:
        for c in range(16):
            if active[c]:
                floor[c], roof[c] = 0x00, heights[c]
            else:
                floor[c] = roof[c] = 0xFF
        ref, cmp = roof, (lambda c, m: c <= m)
    else:
        for c in range(16):
            if active[c]:
                floor[c], roof[c] = heights[c], 0x0F
            else:
                floor[c] = roof[c] = 0xFF
        ref, cmp = floor, (lambda c, m: c >= m)
    for c in range(16):
        lw[c] = next((h for h in range(16) if ref[h] != 0xFF and cmp(c, ref[h])), 0xFF)
        rw[c] = next((h for h in range(15, -1, -1) if ref[h] != 0xFF and cmp(c, ref[h])), 0xFF)
    return {'floor': floor, 'roof': roof, 'lwall': lw, 'rwall': rw}


def M(v):
    return 0xFF if v == 0xFF else 0xF - v


def stock_flips(B):
    X = {'floor': [B['floor'][15 - c] for c in range(16)], 'roof': [B['roof'][15 - c] for c in range(16)],
         'rwall': [M(B['lwall'][c]) for c in range(16)], 'lwall': [M(B['rwall'][c]) for c in range(16)]}
    Y = {'floor': [M(B['roof'][c]) for c in range(16)], 'roof': [M(B['floor'][c]) for c in range(16)],
         'lwall': [B['lwall'][15 - c] for c in range(16)], 'rwall': [B['rwall'][15 - c] for c in range(16)]}
    XY = {'floor': [Y['floor'][15 - c] for c in range(16)], 'roof': [Y['roof'][15 - c] for c in range(16)],
          'rwall': [M(Y['lwall'][c]) for c in range(16)], 'lwall': [M(Y['rwall'][c]) for c in range(16)]}
    return {1: X, 2: Y, 3: XY}


def accessor(B, kind, flip, i):  # PS1Collision.hpp
    fx, fy = flip & 1, flip & 2
    if kind in ('floor', 'roof'):
        idx = 15 - i if fx else i
        if not fy:
            return B[kind][idx]
        return M(B['roof' if kind == 'floor' else 'floor'][idx])
    idx = 15 - i if fy else i
    if not fx:
        return B[kind][idx]
    return M(B['rwall' if kind == 'lwall' else 'lwall'][idx])


def main():
    data = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', '..', 'SonicMania_Extracted', 'Data')
    files = sorted(glob.glob(os.path.join(data, 'Stages', '*', 'TileConfig.bin')))
    checked = 0
    for path in files:
        r = F.R(open(path, 'rb').read())
        assert r.bytes(4) == b'TIL\0'
        buf = r.compressed()
        pos = 0
        for p in range(2):
            for t in range(1024):
                heights, active = buf[pos:pos + 16], buf[pos + 16:pos + 32]
                yflip = buf[pos + 32]
                pos += 38
                B = base_masks(heights, active, yflip)
                S = stock_flips(B)
                for flip in (1, 2, 3):
                    for kind in ('floor', 'roof', 'lwall', 'rwall'):
                        for i in range(16):
                            a = accessor(B, kind, flip, i)
                            if a != S[flip][kind][i]:
                                sys.exit('MISMATCH %s plane %d tile %d flip %d %s[%d]: %d vs %d' % (
                                    path, p, t, flip, kind, i, a, S[flip][kind][i]))
                            checked += 1
    print('check_flips: %d TileConfigs, %d flipped mask values identical (stock loops vs PS1 accessors)' % (len(files), checked))


if __name__ == '__main__':
    main()
