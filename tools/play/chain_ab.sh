#!/bin/bash
# docs/32 9u: exact A/B of a renderer / CPU change on the GPU commands (the RSDKv4 chain_cmp idea). One frame's whole chain
# (tools/play/perf_gpu.sh's RAM dump, walked by gpu_cost.py --dump-words: payload words in GPU order, links left out) from
# a reference build and from the working tree, compared word for word.
# Usage: tools/play/chain_ab.sh NAME BOOT "FRAMES" [RIGHT_FROM] [REF]   (REF: a commit, default HEAD)
# The reference builds in its own git worktree, ../.wt-cab (a PS1 sibling of this repo: the same ../nugget and
# ../SonicMania_Extracted paths), with an APFS clone of this build/ the first time (kept between runs: make rebuilds only
# what the commit changed); the working tree is never stashed or touched, so other runs (the regression suite's shadow
# copy, another session) see it as it is. EXTRA= extra make flags for both builds. Both use the default-config
# PCSX-Redux (GDB 3333), one after the other. Output: build/perf9f/cab_NAME_{ref,cur}_F.words, a line per frame: SAME /
# DIFF (first differing word index).
set -u
cd "$(dirname "$0")/../.."
REPO=$(pwd)
NAME=$1; BOOT=$2; FRAMES=$3; RIGHT=${4:-240}; REF=${5:-HEAD}
OUT=$REPO/build/perf9f; mkdir -p "$OUT"
WT=$(cd .. && pwd)/.wt-cab
sha=$(git rev-parse --verify "$REF^{commit}") || exit 1
if [ ! -d "$WT" ]; then
  git worktree add -q --detach "$WT" "$sha" || exit 1
  cp -c -R build "$WT/build" || exit 1
  rmdir "$WT/build/boss/tree.lock" 2>/dev/null # (a lock held while cloning would be copied and never released)
else
  git -C "$WT" checkout -q --detach "$sha" || exit 1
fi
run_side() { # side dir
  local side=$1 dir=$2
  ( cd "$dir" && make -j8 BOOT=$BOOT ${EXTRA:-} > "$OUT/b_cab_${NAME}_$side.log" 2>&1 \
      && make disc BOOT=$BOOT ${EXTRA:-} > "$OUT/d_cab_${NAME}_$side.log" 2>&1 ) || { echo "BUILD FAIL $side"; return 1; }
  for f in $FRAMES; do
    ( cd "$dir" && NOBUILD=1 tools/play/perf_gpu.sh cab_${NAME}_${side}_$f $BOOT $f $RIGHT > /dev/null 2>&1 )
    local d=$dir/build/perf9f
    h=$(grep "^HEAD " $d/gpu_cab_${NAME}_${side}_${f}_run.log | head -1 | awk '{print "0x" $2, "0x" $3, "0x" $4, $5}')
    /usr/bin/python3 tools/play/gpu_cost.py $d/gpu_cab_${NAME}_${side}_$f.ram $h --top 0 --dump-words "$OUT/cab_${NAME}_${side}_$f.words" > "$OUT/cab_${NAME}_${side}_$f.txt" 2>&1
    rm -f $d/gpu_cab_${NAME}_${side}_$f.ram
  done
}
run_side ref "$WT" || exit 1
run_side cur "$REPO" || exit 1
for f in $FRAMES; do
  /usr/bin/python3 - "$OUT/cab_${NAME}_ref_$f.words" "$OUT/cab_${NAME}_cur_$f.words" $f <<'PY'
import sys, struct
a, b = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
if a == b and a:
    print('frame %s SAME (%d words)' % (sys.argv[3], len(a) // 4))
else:
    wa, wb = struct.unpack('<%dI' % (len(a) // 4), a), struct.unpack('<%dI' % (len(b) // 4), b)
    k = next((i for i in range(min(len(wa), len(wb))) if wa[i] != wb[i]), min(len(wa), len(wb)))
    print('frame %s DIFF at word %d of %d / %d' % (sys.argv[3], k, len(wa), len(wb)))
PY
done
