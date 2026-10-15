#!/bin/bash
# vshot.sh NAME SNAP FRAMES [PRE.gdb] (docs/32 phase 13): pictures from a 2 MB-link snapshot (tools/play/boss/snap.sh; kept
# until tools/play/boss/snap.sh -d SNAP or 3 days) in
# the local 16 MB PCSX-Redux (EMU=r16; EMU_RAM / EMU_VRAM as tools/emu.sh), the whole VRAM taken through its web API at
# each drawn frame of FRAMES (comma list, ascending; g_ps1TestStops) while GDB holds the CPU:
# build/shots/NAME_<frame>.vram (1024 x 512 or 1024 x 1024 halfwords) and NAME_<frame>_fb0 / _fb1 / _hi.png (the two
# framebuffers, VRAM rows 512-1023). PRE: extra GDB lines (ps1_onstop runs at every stop).
set -u
cd "$(dirname "$0")/../.."
R=$PWD; W=$R/build/shots; mkdir -p $W
NAME=$1; SNAP=$2; FRAMES=$3; PRE=${4:-}
. tools/emu.sh >/dev/null 2>&1 || true
{
echo 'define ps1_onstop'
echo 'end'
[ -n "$PRE" ] && cat $PRE
n=0
echo 'tbreak main'; echo 'commands'; echo 'silent'
for f in $(echo $FRAMES | tr ',' ' '); do echo "set var g_ps1TestStops[$n] = $f"; n=$((n + 1)); done
echo 'continue'; echo 'end'
echo 'break ps1TestStop'; echo 'commands'; echo 'silent'
echo 'ps1_onstop'
echo "eval \"shell curl -s http://localhost:${EMU_R16_WEB:-8104}/api/v1/gpu/vram/raw -o $W/${NAME}_%u.vram\", *(unsigned *)&g_ps1PerfFrames"
echo 'printf "L SHOT f=%u\n", *(unsigned *)&g_ps1PerfFrames'
last=$(echo $FRAMES | tr ',' '\n' | tail -1)
echo "if *(unsigned *)&g_ps1PerfFrames >= $last"; echo '  quit'; echo 'end'
echo 'continue'; echo 'end'
echo 'continue'
echo 'printf "T stopped f=%u\n", *(unsigned *)&g_ps1PerfFrames'
echo 'quit'
} > $W/$NAME.in.gdb
(cd build/boss/snaps/$SNAP && EMU=r16 TIMEOUT=${TIMEOUT:-600} tools/play_test.sh $W/$NAME.in.gdb $W/$NAME > /dev/null 2>&1)
/usr/bin/python3 - $W $NAME <<'P'
import sys, glob, os
import numpy as np
from PIL import Image
W, n = sys.argv[1], sys.argv[2]
def img(a):
    r = ((a & 31) << 3).astype(np.uint8); g = (((a >> 5) & 31) << 3).astype(np.uint8); b = (((a >> 10) & 31) << 3).astype(np.uint8)
    return Image.fromarray(np.dstack([r, g, b]))
for f in sorted(glob.glob('%s/%s_*.vram' % (W, n))):
    d = np.frombuffer(open(f, 'rb').read(), '<u2')
    if d.size % 1024: continue
    v = d.reshape(-1, 1024)
    img(v[0:240, 0:320]).save(f[:-5] + '_fb0.png'); img(v[256:496, 0:320]).save(f[:-5] + '_fb1.png')
    if v.shape[0] >= 1024: img(v[512:1024]).save(f[:-5] + '_hi.png')
    print(f[:-5], v.shape)
P
grep -a "^L \|^T \|rror" $W/$NAME.log | head -20
