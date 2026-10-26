#!/bin/bash
# docs/ports/mania/32 9f: tools/play/sweep_timing.gdb over an act in both emulators. Usage: tools/play/perf_sweep.sh NAME BOOT X0 STEP
# END [Y]  (EMUS="redux ds"; NOBUILD=1 reuses the build). Output: build/perf9f/sweep_NAME_<emu>.txt (S lines).
set -u
cd "$(dirname "$0")/../.."
NAME=$1; BOOT=$2; X0=$3; STEP=$4; END=$5; Y=${6:-64}
OUT=build/perf9f; mkdir -p $OUT
if [ "${NOBUILD:-0}" != 1 ]; then
  make -j8 BOOT=$BOOT > $OUT/b_sw_$NAME.log 2>&1 && make disc > $OUT/d_sw_$NAME.log 2>&1 || { echo "BUILD FAIL $NAME"; exit 1; }
fi
{ printf 'set $swX0 = %d\nset $swStep = %d\nset $swEnd = %d\nset $swY = %d\n' $X0 $STEP $END $Y; cat tools/play/sweep_timing.gdb; } > $OUT/sw_$NAME.gdb
for e in ${EMUS:-redux ds}; do
  EMU=$e TIMEOUT=${TIMEOUT:-1500} tools/play_test.sh $OUT/sw_$NAME.gdb $OUT/sw_${NAME}_$e > /dev/null 2>&1
  grep "^S " $OUT/sw_${NAME}_$e.log > $OUT/sweep_${NAME}_$e.txt
  echo "$NAME $e: $(wc -l < $OUT/sweep_${NAME}_$e.txt) points"
done
