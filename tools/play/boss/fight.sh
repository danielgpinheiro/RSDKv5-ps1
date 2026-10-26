#!/bin/bash
# docs/ports/mania/32 9i (boss check): a boss fight's frame timing, phase by phase. fight.sh NAME BOOT OVL STATE_EXPR X Y FRAMES
#   BOOT: GameConfig category,scene (a BOOT= build at 2 MB, built + snapshot under the tree lock unless NOBUILD=1)
#   OVL: the scene's overlay (build/gen/ovl/OVL/OVL.elf: the boss's functions and types)
#   STATE_EXPR: GDB expression of the boss's state, e.g. '((struct EntityMetalSonic *)RSDK::objectEntityPtr[93])->state'
#   X Y: where player 1 is put at frame WARPF (120), pixels; the player is kept unhurt (50 rings, blinking)
#   FRAMES: the run's length. PAD: buttons held after the warp (g_ps1PadTestMask). EMU=ds: DuckStation.
# Every TICK (60) frames: L T f, the boss's state (its function's name), the running timing since the warp (avg vblanks
# a frame x100, worst vblanks, worst CPU hblanks) and the last frame's CPU hblanks; guards at the end.
# Output: build/boss/fight_NAME.log (+ the L lines on stdout).
H=$(cd "$(dirname "$0")" && pwd)
S=${BOSS_WORK:-$(cd "$H/../../.." && pwd)/build/boss}
mkdir -p $S
R=$(cd "$H/../../.." && pwd)
NAME=$1; BOOT=$2; OVL=$3; ST=$4; X=$5; Y=$6; FRAMES=$7
n=fight_$NAME
if [ "${NOBUILD:-0}" != 1 ]; then
  until mkdir $S/tree.lock 2>/dev/null; do sleep 5; done
  ( cd $R && make -j8 BOOT=$BOOT > $S/b_$n.log 2>&1 && make disc > $S/d_$n.log 2>&1 && $H/snap.sh $n > /dev/null ); ok=$?
  rmdir $S/tree.lock
  [ $ok = 0 ] || { echo "BUILD FAIL $BOOT"; exit 1; }
fi
cat > $S/$n.in.gdb <<G
add-symbol-file $S/snaps/$n/build/gen/ovl/$OVL/$OVL.elf
set \$warped = 0
set \$pv0 = 0
set \$pf0 = 0
break perfTick if g_ps1PerfFrames % ${TICK:-60} == 0
commands
silent
set \$p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
if !\$warped && g_ps1PerfFrames >= ${WARPF:-120}
  set var \$p->position.x = $X << 16
  set var \$p->position.y = $Y << 16
  set var \$p->velocity.x = 0
  set var \$p->velocity.y = 0
  set var g_ps1PadTestMask = ${PAD:-0}
  set \$pv0 = g_ps1PerfVsyncs
  set \$pf0 = g_ps1PerfFrames
  set var g_ps1PerfWorstVsyncs = 0
  set var g_ps1PerfMaxHbl = 0
  set \$warped = 1
  printf "L WARP f=%u x=%d y=%d\n", g_ps1PerfFrames, $X, $Y
end
@ACTS@
if \$warped
  set var \$p->rings = 50
  set var \$p->blinkTimer = 240
  printf "L T f=%u avg=%u wv=%u mh=%u hbl=%u x=%d y=%d st=", g_ps1PerfFrames, (g_ps1PerfVsyncs - \$pv0) * 100 / (g_ps1PerfFrames - \$pf0 + (g_ps1PerfFrames == \$pf0)), g_ps1PerfWorstVsyncs, g_ps1PerfMaxHbl, g_ps1PerfLastHbl, \$p->position.x >> 16, \$p->position.y >> 16
  info symbol (unsigned)($ST)
end
if g_ps1PerfFrames >= $FRAMES
  printf "L END f=%u avg=%u wv=%u mh=%u oom=%u missing=%u spriteMissing=%u relocations=%u ghost=%u stgFail=%u\n", g_ps1PerfFrames, (g_ps1PerfVsyncs - \$pv0) * 100 / (g_ps1PerfFrames - \$pf0 + 1), g_ps1PerfWorstVsyncs, g_ps1PerfMaxHbl, g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, *(unsigned *)&g_ps1SpriteMissing, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntityGhostWrites, g_ps1StorageFail[0]
  quit
end
continue
end
continue
printf "L STUCK f=%u\n", g_ps1PerfFrames
bt 10
G
# ACT1F / ACT1 .. ACT4F / ACT4 (environment): GDB lines run once, at the first tick at or past that frame (e.g. a boss's
# state set to reach a phase the scripted player cannot)
python3 - "$S/$n.in.gdb" <<'P'
import os, sys
p = sys.argv[1]
s = open(p).read()
acts = ''
for k in range(1, 5):
    f, a = os.environ.get('ACT%dF' % k), os.environ.get('ACT%d' % k)
    if f and a:
        acts += 'if !$act%d && g_ps1PerfFrames >= %s\n  %s\n  set $act%d = 1\n  printf "L ACT%d f=%%u\\n", g_ps1PerfFrames\nend\n' % (k, f, a, k, k)
s = s.replace('@ACTS@\n', acts)
s = s.replace('set $warped = 0\n', 'set $warped = 0\n' + ''.join('set $act%d = 0\n' % k for k in range(1, 5)), 1)
open(p, 'w').write(s)
P
e=""; [ "${EMU:-pcsx}" = ds ] && e="EMU=ds"
( cd $S/snaps/$n && eval "$e TIMEOUT=${TIMEOUT:-2400} tools/play_test.sh $S/$n.in.gdb $S/$n" ) > /dev/null 2>&1
[ "${NOBUILD:-0}" = 1 ] || $H/snap.sh -d $n   # the snapshot this run made (KEEP_SNAP=1 keeps it)
grep -a "^L \|No symbol\|rror" $S/$n.log | sed 's/ in section .*//' | cut -c1-200
