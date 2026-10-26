#!/bin/bash
# shot.sh NAME BOOT FRAMES [PRE.gdb]: 8 MB measuring build of BOOT (docs/ports/mania/32 9c pictures), both framebuffers dumped at each
# frame in FRAMES (comma list) -> build/shots/NAME_<frame>_<y>.png (320x240; the displayed one is the complete picture).
# SHOT_X=x: dump the 320 x 240 area from VRAM column x instead of 0. PRE.gdb: extra GDB lines (breakpoints that set up the scene, e.g. the boss hook). PCSX-Redux, shared tree lock of
# tools/play/boss (build/boss/tree.lock).
set -u
cd "$(dirname "$0")/../.."
R=$PWD; W=$R/build/shots; mkdir -p $W $R/build/boss
NAME=$1; BOOT=$2; FRAMES=$3; PRE=${4:-}
until mkdir $R/build/boss/tree.lock 2>/dev/null; do sleep 5; done
( make -j8 MEASURE_RAM=8M BOOT=$BOOT ${SHOT_MAKE:-} > $W/b_$NAME.log 2>&1 && make disc > $W/d_$NAME.log 2>&1 && tools/play/boss/snap.sh shot_$NAME > /dev/null ); ok=$?
make -j8 BOOT=$BOOT > /dev/null 2>&1   # the tree back to a 2 MB link
rmdir $R/build/boss/tree.lock
[ $ok = 0 ] || { echo "BUILD FAIL"; exit 1; }
frames=$(echo $FRAMES | tr ',' '\n' | sort -n)
first=$(echo "$frames" | head -1)
last=$(echo "$frames" | tail -1)
{
# 9b.5: GDB stops only at the shot frames (g_ps1TestStops[3], ps1/rsdk5_main.cpp; set after main starts: start-up clears
# BSS), not on a per-frame condition; PRE may define ps1_onstop (called at every stop) and use g_ps1TestStops[0-2, 4-7]
echo 'define ps1_onstop'
echo 'end'
echo 'define ps1_shot_next'
echo '  set $next = 0'
for f in $(echo "$frames" | sort -rn); do echo "  if g_ps1PerfFrames < $f"; echo "    set \$next = $f"; echo "  end"; done
echo 'end'
[ -n "$PRE" ] && cat $PRE
cat <<G
set \$half = 0
tbreak main
commands
silent
set var g_ps1TestStops[3] = $first
continue
end
break ps1TestStop
commands
silent
if g_ps1PerfFrames == g_ps1TestStops[3]
  set var g_ps1VramDumpX = ${SHOT_X:-0}
  set var g_ps1VramDumpW = 320
  set var g_ps1VramDumpH = 240
  if \$half && g_ps1PerfFrames == \$half
    set var g_ps1VramDumpY = 256
    set \$half = 0
    ps1_shot_next
    set var g_ps1TestStops[3] = \$next
  else
    set var g_ps1VramDumpY = 0
    set \$half = g_ps1PerfFrames + 1
    set var g_ps1TestStops[3] = \$half
  end
  set var g_ps1VramDumpReq = 1
end
ps1_onstop
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
(cd $R/build/boss/snaps/shot_$NAME && TIMEOUT=${TIMEOUT:-900} tools/play_test.sh $W/$NAME.in.gdb $W/$NAME > /dev/null 2>&1)
$R/tools/play/boss/snap.sh -d shot_$NAME   # done with it (KEEP_SNAP=1 keeps it)
cd $R; /usr/bin/python3 - $W $NAME <<'P'
import sys, glob, os
from PIL import Image
W, n = sys.argv[1], sys.argv[2]
for f in sorted(glob.glob('%s/%s_*_*.bin' % (W, n))):
    d = open(f, 'rb').read()
    if len(d) < 153600: continue
    im = Image.new('RGB', (320, 240))
    px = [((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3) for v in (d[i] | d[i + 1] << 8 for i in range(0, 153600, 2))]
    im.putdata(px); im.save(f[:-4] + ".png"); os.rename(f, f[:-4] + ".raw")
    print(f[:-4] + '.png')
P
grep -a "^L \|rror" $W/$NAME.log | head -20
