#!/usr/bin/env python3
"""docs/32 9n: the table of tools/play/perf_all.sh's sweeps (build/perf9f/sweep_9n_<i>_<emu>.txt, S lines): per act and
emulator the points, mean vblanks a frame (x100), the worst point (vblanks x100, its x), mean / max CPU hblanks.
perf_all.py [TAG] (default 9n; 9q = the capped zones, docs/32 choice 182: 2.00 = full speed at 30 fps)."""
import glob, os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import rsdk5_formats as F
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'SonicMania_Extracted', 'Data')
scenes = F.read_gameconfig(os.path.join(D, 'Game', 'GameConfig.bin'))['categories'][1]['scenes']
TAG = sys.argv[1] if len(sys.argv) > 1 else '9n'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', 'perf9f')
print('%-4s %-10s %-6s %6s %8s %14s %8s %8s' % ('#', 'scene', 'emu', 'points', 'mean', 'worst (x)', 'cpu', 'cpuMax'))
for i, s in enumerate(scenes):
    for emu in ('redux', 'ds'):
        p = os.path.join(OUT, 'sweep_%s_%d_%s.txt' % (TAG, i, emu))
        if not os.path.exists(p):
            continue
        rows = []
        for l in open(p):
            m = re.search(r'x=(-?\d+).*vsyncs x100=(\d+).*cpu=(\d+) max=(\d+)', l)
            if m:
                rows.append(tuple(int(v) for v in m.groups()))
        if not rows:
            print('%-4d %-10s %-6s %6s' % (i, s['folder'] + s['id'], emu, 0))
            continue
        w = max(rows, key=lambda r: r[1])
        print('%-4d %-10s %-6s %6d %8.2f %8.2f (%5d) %8d %8d' % (i, s['folder'] + s['id'], emu, len(rows),
              sum(r[1] for r in rows) / len(rows) / 100, w[1] / 100, w[0], sum(r[2] for r in rows) // len(rows),
              max(r[3] for r in rows)))
