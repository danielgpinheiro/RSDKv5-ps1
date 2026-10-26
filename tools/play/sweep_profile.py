#!/usr/bin/env python3
"""docs/ports/mania/32 9q: tools/play/sweep_profile.gdb's log: per sweep point the classes' CPU (hblanks per drawn frame, the 60-frame
window) and the engine parts; with --top N the N costliest classes per point. Usage: sweep_profile.py LOG [--top N]"""
import glob, os, struct, sys, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import rsdk5_formats as F
TICKS_PER_HBL = 269
here = os.path.dirname(os.path.abspath(__file__))
names = [os.path.basename(f)[:-2] for f in glob.glob(os.path.join(here, '..', '..', 'SonicMania', 'Objects', '*', '*.h'))]
by_hash = {F.name_hash(n): n for n in names}
top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 6
k0, k1, nm, p0, p1, sx, kd = {}, {}, {}, {}, {}, {}, {}
for line in open(sys.argv[1], errors='replace'):
    t = line.split()
    if not t:
        continue
    if t[0] == 'K0':
        k0[(int(t[1]), int(t[2]))] = (int(t[3]), int(t[4]), int(t[5]))
    elif t[0] == 'K1':
        k1[(int(t[1]), int(t[2]))] = (int(t[3]), int(t[4]), int(t[5]))
        key = b''.join(struct.pack('<I', int(t[6][j:j + 8], 16)) for j in range(0, 32, 8))
        nm[int(t[2])] = by_hash.get(key, t[6][:8])
    elif t[0] == 'KP0':
        p0[int(t[1])] = [int(v) for v in t[2:]]
    elif t[0] == 'KP1':
        p1[int(t[1])] = [int(v) for v in t[2:]]
    elif t[0] == 'KD':
        kd[int(t[1])] = ' '.join(t[2:])
    elif t[0] == 'S':
        sx[len(sx) + 1] = line.split()[1]
W = 60 * TICKS_PER_HBL
for k in sorted(p1):
    rows = []
    for (kk, o), (u, d, c) in k1.items():
        if kk != k:
            continue
        b = k0.get((k, o), (0, 0, 0))
        rows.append(((u - b[0]) / W, (d - b[1]) / W, (c - b[2]) / 60, nm[o]))
    rows.sort(key=lambda r: -(r[0] + r[1]))
    d = [(a - b) / 60 for a, b in zip(p1[k], p0.get(k, p1[k]))]  # hblanks (root counter 1) a drawn frame
    print('k=%d %s classes %.0f (upd %.0f draw %.0f)' % (k, sx.get(k, ''), sum(r[0] + r[1] for r in rows),
          sum(r[0] for r in rows), sum(r[1] for r in rows)))
    print('    parts static %.0f update %.0f typegroups %.0f late %.0f draws %.0f scanlines %.0f layers %.0f' % tuple(d[:7]))
    if k in kd:
        print('    ' + kd[k])
    print('    phases input %.0f objects %.0f parallax %.0f drawlists %.0f engine %.0f' % tuple(d[7:12]))
    for u, d, c, n in rows[:top]:
        print('   %-20s %5.1f (upd %5.1f draw %5.1f) %5.1f calls' % (n, u + d, u, d, c))
