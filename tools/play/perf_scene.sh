#!/bin/bash
# docs/32 9f: frame timing of one scene in PCSX-Redux and DuckStation (tools/play/scene_timing.gdb: vsyncs per frame,
# slow frames, CPU / GPU-wait / flip hblanks). Usage: tools/play/perf_scene.sh NAME BOOT [FROM TO] [RIGHT_FROM]
# (frames FROM..TO, default 300..600; RIGHT_FROM: pad 1 holds right from that frame, through the scripted pad).
# EMUS="redux ds" by default; NOBUILD=1 reuses the current build; SETS = GDB lines run at main (test switches). Output: build/perf9f/NAME_<emu>.txt (the T line).
set -u
cd "$(dirname "$0")/../.."
NAME=$1; BOOT=$2; FROM=${3:-300}; TO=${4:-600}; RIGHT=${5:-}
OUT=build/perf9f; mkdir -p $OUT
if [ "${NOBUILD:-0}" != 1 ]; then
  make -j8 BOOT=$BOOT > $OUT/b_$NAME.log 2>&1 && make disc > $OUT/d_$NAME.log 2>&1 || { echo "BUILD FAIL $NAME"; exit 1; }
fi
{ printf 'tbreak main\ncommands\nsilent\n'
  if [ -n "$RIGHT" ]; then
    printf 'set var g_ps1PadScript[0] = 0xFFFF\nset var g_ps1PadScript[1] = %s\nset var g_ps1PadScript[2] = 0x8\nset var g_ps1PadScript[3] = 60000\nset var g_ps1PadScriptSteps = 1\nset var g_ps1PadScriptStep = 0\n' "$RIGHT"
  fi
  [ -n "${SETS:-}" ] && printf '%b\n' "$SETS" # GDB lines run at main (e.g. 'set var g_ps1UfoLite = 1')
  printf 'continue\nend\n'
  sed "s/g_ps1PerfFrames == 300 || g_ps1PerfFrames == 600/g_ps1PerfFrames == $FROM || g_ps1PerfFrames == $TO/; s/set \$winA = 300/set \$winA = $FROM/; s/set \$winB = 600/set \$winB = $TO/" tools/play/scene_timing.gdb
} > $OUT/$NAME.gdb
for e in ${EMUS:-redux ds}; do
  EMU=$e TIMEOUT=${TIMEOUT:-400} tools/play_test.sh $OUT/$NAME.gdb $OUT/${NAME}_$e > /dev/null 2>&1
  t=$(grep "^T " $OUT/${NAME}_$e.log | head -1)
  echo "$NAME $e ${t:-NO RESULT}" | tee $OUT/${NAME}_$e.txt
done
