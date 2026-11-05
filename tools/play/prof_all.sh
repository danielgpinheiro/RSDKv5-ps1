#!/bin/bash
# docs/ports/mania/32 9v: per-class CPU profiles of several acts at once (tools/play/sweep_profile.gdb, make PROFILE=1),
# one emulator slot per worker. Each act: built under the tree lock (PROFILE=1 BOOT=1,i), frozen in a snapshot
# (tools/play/boss/snap.sh), swept from x 256 every STEP px to its FG width less 256 on the worker's slot, the snapshot
# deleted after. Usage: SLOTS="1 2 d" STEP=1536 TAG=pf tools/play/prof_all.sh ACT...   (ACT = category-1 scene index;
# slot d = the default instance; X0=x XEND=x: from x0 to that last point instead of 256 .. the FG width less 256).
# Output: build/perf9f/prof_<TAG>_<i>.log + .rep (sweep_profile.py --top 14).
set -u
cd "$(dirname "$0")/../.."
TAG=${TAG:-pf}; STEP=${STEP:-1536}; SLOTS=${SLOTS:-1 2}; X0=${X0:-256} # X0 / XEND: one stretch (XEND: the last point)
O=build/perf9f; mkdir -p $O build/boss   # (the tree lock lives in build/boss)
Q=$O/prof_$TAG.queue
printf '%s\n' "$@" > $Q
worker() {
  local slot=$1 i w end S
  [ "$slot" = d ] && slot=
  while :; do
    until mkdir $Q.lock 2>/dev/null; do sleep 1; done
    i=$(head -1 $Q); [ -n "$i" ] && sed -i '' 1d $Q
    rmdir $Q.lock
    [ -z "$i" ] && return
    w=$(/usr/bin/python3 - $i <<'P'
import sys
sys.path.insert(0, 'tools')
import rsdk5_formats as F
D = '../SonicMania_Extracted/Data/'
s = F.read_gameconfig(D + 'Game/GameConfig.bin')['categories'][1]['scenes'][int(sys.argv[1])]
sc = F.read_scene(D + 'Stages/%s/Scene%s.bin' % (s['folder'], s['id']))
fg = [l for l in sc['layers'] if l.get('name', '').startswith('FG')]
print(max(l['xsize'] for l in fg) * 16 if fg else 4096)
P
)
    end=$((w - 256)); [ $end -lt 1024 ] && end=1024; [ -n "${XEND:-}" ] && end=$XEND
    until mkdir build/boss/tree.lock 2>/dev/null; do sleep 5; done
    ( make -j8 PROFILE=1 BOOT=1,$i > $O/b_prof_${TAG}_$i.log 2>&1 && make disc PROFILE=1 BOOT=1,$i >> $O/b_prof_${TAG}_$i.log 2>&1 \
      && tools/play/boss/snap.sh pf${TAG}_$i > /dev/null ); local ok=$?
    rmdir build/boss/tree.lock
    [ $ok = 0 ] || { echo "BUILD FAIL $i"; continue; }
    S=build/boss/snaps/pf${TAG}_$i
    { printf 'set $swX0 = %d\nset $swStep = %d\nset $swEnd = %d\nset $swY = 64\n' $X0 $STEP $end; cat tools/play/sweep_profile.gdb; } > $S/pf.gdb
    ( cd $S && EMU_SLOT=$slot TIMEOUT=${TIMEOUT:-7200} tools/play_test.sh pf.gdb pf > /dev/null 2>&1 )
    cp $S/pf.log $O/prof_${TAG}_$i.log
    # a run stopped by its timeout leaves its last point incomplete: the report takes the complete ones
    k=$(grep "^KP1 " $O/prof_${TAG}_$i.log | tail -1 | awk '{print $2}')
    awk -v k="$k" '!/Quit/ && !($1 ~ /^K/ && $2 > k)' $O/prof_${TAG}_$i.log > $O/prof_${TAG}_$i.ok.log
    /usr/bin/python3 tools/play/sweep_profile.py $O/prof_${TAG}_$i.ok.log --top 14 > $O/prof_${TAG}_$i.rep 2>&1
    echo "act $i slot ${slot:-d}: $(grep -c '^k=' $O/prof_${TAG}_$i.rep) points"
    tools/play/boss/snap.sh -d pf${TAG}_$i
  done
}
for s in $SLOTS; do worker $s & sleep 2; done
wait
rm -f $Q
