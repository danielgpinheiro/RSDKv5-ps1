#!/bin/bash
# docs/32 9n: tools/play/perf_sweep.sh over every Mania-mode act (category 1, scenes FROM..TO, default 0..28) in both
# emulators: each act swept every 768 px from x 256 to its FG width less 256 (sweep_timing.gdb: frames 90-149 after each
# warp timed). Builds BOOT=1,i under the tree lock. Output build/perf9f/sweep_<TAG>_<i>_<emu>.txt (TAG=9n by default) +
# tools/play/perf_all.py [TAG]'s table. CAP=1 (docs/32 choice 182): the capped zones (30 fps, two logic steps a frame).
set -u
cd "$(dirname "$0")/../.."
FROM=${1:-0}; TO=${2:-28}; TAG=${TAG:-9n}
for i in $(seq $FROM $TO); do
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
  end=$((w - 256)); [ $end -lt 1024 ] && end=1024
  until mkdir build/boss/tree.lock 2>/dev/null; do sleep 5; done
  ( make -j8 BOOT=1,$i ${CAP:+CAP=$CAP} > build/perf9f/b_${TAG}_$i.log 2>&1 && make disc > build/perf9f/d_${TAG}_$i.log 2>&1 && tools/play/boss/snap.sh p$TAG > /dev/null ); ok=$?
  rmdir build/boss/tree.lock
  [ $ok = 0 ] || { echo "BUILD FAIL $i"; continue; }
  ( cd build/boss/snaps/p$TAG && NOBUILD=1 TIMEOUT=${TIMEOUT:-2400} tools/play/perf_sweep.sh ${TAG}_$i 1,$i 256 768 $end )
  cp build/boss/snaps/p$TAG/build/perf9f/sweep_${TAG}_${i}_*.txt build/perf9f/ 2>/dev/null
done
