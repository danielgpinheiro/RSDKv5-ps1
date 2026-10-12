#!/usr/bin/python3
"""docs/32 phase 13 M0.1 (modded consoles, measurement only: writes nothing to the disc tree): for a scene's atlas,
the stock build (1 MB VRAM, the HALF_PLAN steps it needs) against the same frames at full resolution and full colour
(no plan step) packed into a 2 MB VRAM (GP1(09h) = 1): the stock texture columns plus every page of rows 512-1023
(page rows 2 and 3; CLUTs stay where they are). Prints one line:
  M FOLDER ID CHAR stock_kb=.. stock_steps=.. full_kb=.. resident=.. player=.. events=.. cluts=.. fits2mb=yes|no (why)
Usage: mods_measure.py DATA_DIR DISC_DATA_DIR FOLDER SCENE_ID [CHARACTER]
"""
import io
import os
import sys
import contextlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_atlas as A  # noqa: E402
import build_tiles as T  # noqa: E402
import vram_plan as V    # noqa: E402


def places_kb(clusters):
    out = {}
    for c in clusters or []:
        out.setdefault(c['where'], []).append(c)
    return {w: A.kb_of(cs) for w, cs in out.items()}


def fmt(sizes, kind):
    return '+'.join('%.0f' % k for w, k in sorted(sizes.items(), key=str) if w != 'resident' and w[0] == kind) or '0'


def main():
    if len(sys.argv) not in (5, 6):
        sys.exit(__doc__)
    data, disc, folder, sid = sys.argv[1:5]
    ch = sys.argv[5] if len(sys.argv) > 5 else 'sonic'
    with contextlib.redirect_stdout(io.StringIO()):
        st = A.build_fit(data, disc, folder, sid, ch)
    st_rep, st_cl = st[1], st[2]
    steps = st_rep.split('big sprites at half resolution: ')[-1].split('\n')[0] if 'half resolution' in st_rep else '?'
    st_kb = sum(places_kb(st_cl).values())

    # full resolution, full colour, 2 MB VRAM
    A.HALF_RULES, A.KEEP_TEXT, A.LOSSY_LEVEL = {}, True, False
    V.VRAM_H = 1024
    tbin, _, _, tblocks = T.build(data, folder, sid)
    tcols = {(c, y // V.PAGE) for (x, y, w, h) in tblocks for c in range(x // V.COL_HW, (x + w - 1) // V.COL_HW + 1)}
    cols = [(c, r) for r in (0, 1) for c in V.TEX_COLUMNS if (c, r) not in tcols] + [(c, r) for r in (2, 3) for c in range(16)]
    why = ''
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            res = A.build(data, disc, folder, sid, ch, columns=cols)
        fits = res[0] is not None
        if not fits:
            why = res[1].split('\n')[0]
        cl = res[2]
    except A.AtlasError as e:
        fits, why, cl = False, str(e), None
    sz = places_kb(cl)
    ncl = len({c.get('clut') for c in cl or [] if c.get('depth') == 4})
    print('M %s %s %s stock_kb=%.0f stock_steps=[%s] full_kb=%.0f resident=%.0f player=%s events=%s cluts=%d fits2mb=%s%s' % (
        folder, sid, ch, st_kb, steps, sum(sz.values()), sz.get('resident', 0), fmt(sz, 'player'), fmt(sz, 'event'), ncl,
        'yes' if fits else 'no', '' if fits else ' (%s)' % why[:160]))


if __name__ == '__main__':
    main()
