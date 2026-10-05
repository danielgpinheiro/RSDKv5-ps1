#!/bin/bash
# shot.sh NAME BOOT FRAMES [PRE.gdb]: 8 MB measuring build of BOOT (docs/32 9c pictures), both framebuffers dumped at each
# frame in FRAMES (comma list) -> build/shots/NAME_<frame>_<y>.png (320x240; the displayed one is the complete picture).
# PRE.gdb: extra GDB lines (breakpoints that set up the scene, e.g. the boss hook). PCSX-Redux, shared tree lock of
# tools/play/boss (build/boss/tree.lock).
set -u
cd "$(dirname "$0")/../.."
R=$PWD; W=$R/build/shots; mkdir -p $W $R/build/boss
NAME=$1; BOOT=$2; FRAMES=$3; PRE=${4:-}
until mkdir $R/build/boss/tree.lock 2>/dev/null; do sleep 5; done
( make -j8 MEASURE_RAM=8M BOOT=$BOOT > $W/b_$NAME.log 2>&1 && make disc > $W/d_$NAME.log 2>&1 && tools/play/boss/snap.sh shot_$NAME > /dev/null ); ok=$?
make -j8 BOOT=$BOOT > /dev/null 2>&1   # the tree back to a 2 MB link
rmdir $R/build/boss/tree.lock
[ $ok = 0 ] || { echo "BUILD FAIL"; exit 1; }
cond=$(echo $FRAMES | tr ',' '\n' | sed 's/^/g_ps1PerfFrames == /; s/$/ || /' | tr -d '\n' | sed 's/ || $//')
last=$(echo $FRAMES | tr ',' '\n' | sort -n | tail -1)
{
[ -n "$PRE" ] && cat $PRE
cat <<G
set \$half = 0
break perfTick if $cond || (\$half && g_ps1PerfFrames == \$half)
commands
silent
set var g_ps1VramDumpX = 0
set var g_ps1VramDumpW = 320
set var g_ps1VramDumpH = 240
if \$half && g_ps1PerfFrames == \$half
  set var g_ps1VramDumpY = 256
  set \$half = 0
else
  set var g_ps1VramDumpY = 0
  set \$half = g_ps1PerfFrames + 1
end
set var g_ps1VramDumpReq = 1
continue
end
break PS1VramDumpHook
commands
silent
eval "dump binary memory $W/${NAME}_%u_%d.bin g_ps1VramDumpBuf ((char*)g_ps1VramDumpBuf)+153600", g_ps1PerfFrames, g_ps1VramDumpY
printf "L SHOT f=%u y=%d\n", g_ps1PerfFrames, g_ps1VramDumpY
if g_ps1PerfFrames > $last && g_ps1VramDumpY == 256
  quit
end
continue
end
continue
G
} > $W/$NAME.in.gdb
cd $R/build/boss/snaps/shot_$NAME && TIMEOUT=${TIMEOUT:-900} tools/play_test.sh $W/$NAME.in.gdb $W/$NAME > /dev/null 2>&1
cd $R; /usr/bin/python3 - $W $NAME <<'P'
import sys, glob, os
from PIL import Image
W, n = sys.argv[1], sys.argv[2]
for f in sorted(glob.glob('%s/%s_*_*.bin' % (W, n))):
    d = open(f, 'rb').read()
    if len(d) < 153600: continue
    im = Image.new('RGB', (320, 240))
    px = [((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3) for v in (d[i] | d[i + 1] << 8 for i in range(0, 153600, 2))]
    im.putdata(px); im.save(f[:-4] + '.png'); os.remove(f)
    print(f[:-4] + '.png')
P
grep -a "^L \|rror" $W/$NAME.log | head -20
