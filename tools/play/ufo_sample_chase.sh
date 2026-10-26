#!/bin/bash
# docs/ports/mania/32 9f: the PC sampler over UFO stage N's chase (tools/play/ufo_min.sh's pad script, frames FROM-TO: the same frames
# ufo_min.sh times), so the profile matches the measurement. Builds SAMPLE=1 BOOT=3,N-1 (2 MB) under the tree lock,
# EMU_RAM=8mb (the histogram at 0x80400000). Output: build/perf9f/ufoN_chase_hist.bin, then the report (top 30).
# Usage: tools/play/ufo_sample_chase.sh N [FROM TO] (read the report before rebuilding: it maps through the current ELFs)
set -u
cd "$(dirname "$0")/../.."
N=$1; FROM=${2:-180}; TO=${3:-1500}; OUT=build/perf9f
sets=$(sed -n '/^sets=""/,/^sets="${sets}set var g_ps1PadScriptSteps/p' tools/play/ufo_min.sh); eval "$sets"
until mkdir build/boss/tree.lock 2>/dev/null; do sleep 5; done
( make -j8 SAMPLE=1 BOOT=3,$((N - 1)) > $OUT/b_chs.log 2>&1 && make disc > $OUT/d_chs.log 2>&1 ); ok=$?
rmdir build/boss/tree.lock
[ $ok = 0 ] || { echo BUILD FAIL; exit 1; }
{ printf 'tbreak main\ncommands\nsilent\n%b\nset var g_ps1TestStops[0] = %d\nset var g_ps1TestStops[1] = %d\ncontinue\nend\n' "$sets" $FROM $TO
  printf 'break ps1TestStop\ncommands\nsilent\nif g_ps1PerfFrames == %d\n  set g_ps1SampleOn = 1\nelse\n  set g_ps1SampleOn = 0\n  dump binary memory %s/ufo%d_chase_hist.bin 0x80400000 0x80600000\n  quit\nend\ncontinue\nend\ncontinue\n' $FROM $OUT $N; } > $OUT/ufo${N}_chase.gdb
rm -f $OUT/ufo${N}_chase_hist.bin
EMU_RAM=8mb TIMEOUT=${TIMEOUT:-1800} tools/play_test.sh $OUT/ufo${N}_chase.gdb $OUT/ufo${N}_chase > /dev/null 2>&1
/usr/bin/python3 tools/play/sample_report.py $OUT/ufo${N}_chase_hist.bin $((TO - FROM)) --ovl UFO$N --top ${TOP:-30}
