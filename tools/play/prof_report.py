#!/usr/bin/env python3
"""Ranks the classes of a PROFILE=1 play log (tools/play/ghz1_profile.gdb): hblanks per frame of update and draw.
Usage: prof_report.py LOG [FRAMES]   (FRAMES: the frames the counters ran, default 900: they count from boot, or from
the P0 lines of tools/play/scene_profile.gdb)"""
import glob, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "."))
import rsdk5_formats as F

TICKS_PER_HBL = 269  # root counter 2 = system clock / 8; an NTSC hblank is ~2,150 cycles
here = os.path.dirname(os.path.abspath(__file__))
names = [os.path.basename(f)[:-2] for f in glob.glob(os.path.join(here, '..', '..', 'SonicMania', 'Objects', '*', '*.h'))]
by_hash = {F.name_hash(n): n for n in names}
frames = int(sys.argv[2]) if len(sys.argv) > 2 else 900
rows = []
base = {}  # P0 lines (tools/play/scene_profile.gdb): the counters at the window's start, subtracted
for line in open(sys.argv[1]):
    if line.startswith('P0 '):
        _, o, u, d, c = line.split()
        base[o] = (int(u), int(d), int(c))
for line in open(sys.argv[1]):
    if line.startswith(('PROF ', 'PARTS ', 'FRAMES ')):
        print(line.strip())
    if not line.startswith('PC '):
        continue
    _, o, u, d, c, h = line.split()
    b = base.get(o, (0, 0, 0))
    u, d, c = int(u) - b[0], int(d) - b[1], int(c) - b[2]
    key = b''.join(struct.pack('<I', int(h[k:k + 8], 16)) for k in range(0, 32, 8))
    rows.append((int(u) + int(d), int(u), int(d), int(c), by_hash.get(key, h[:8])))
rows.sort(reverse=True)
print('classes: %.1f hblanks per frame' % (sum(r[0] for r in rows) / TICKS_PER_HBL / frames))
for t, u, d, c, n in rows[:25]:
    print('%-22s %6.1f hbl/frame (update %5.1f, draw %5.1f), %5.1f calls/frame' % (n, t / TICKS_PER_HBL / frames, u / TICKS_PER_HBL / frames, d / TICKS_PER_HBL / frames, c / frames))
