#!/bin/bash
# docs/32 9f: tools/play/ufo_logic.gdb over stage N's chase (tools/play/ufo_min.sh's pad script) in PCSX-Redux:
# build/perf9f/ufoN_logic_TAG.txt (the L lines). Usage: tools/play/ufo_logic.sh N TAG (NOBUILD=1: the current build).
set -u
cd "$(dirname "$0")/../.."
N=$1; TAG=$2; OUT=build/perf9f
if [ "${NOBUILD:-0}" != 1 ]; then
  make -j8 BOOT=3,$((N - 1)) > $OUT/b_logic.log 2>&1 && make disc > $OUT/d_logic.log 2>&1 || { echo BUILD FAIL; exit 1; }
fi
sets=$(sed -n '/^sets=""/,/^sets="${sets}set var g_ps1PadScriptSteps/p' tools/play/ufo_min.sh)
TO=1500; eval "$sets"
{ printf 'tbreak main\ncommands\nsilent\n%b\ncontinue\nend\n' "$sets"; cat tools/play/ufo_logic.gdb; echo continue; } > $OUT/ufo${N}_logic_$TAG.gdb
TIMEOUT=${TIMEOUT:-1500} tools/play_test.sh $OUT/ufo${N}_logic_$TAG.gdb $OUT/ufo${N}_logic_$TAG > /dev/null 2>&1
grep -a "^L " $OUT/ufo${N}_logic_$TAG.log > $OUT/ufo${N}_logic_$TAG.txt
wc -l < $OUT/ufo${N}_logic_$TAG.txt
