#!/bin/bash
# tvvan_all.sh SNAP EMU TAG (docs/32 9r): every van of SPZ1 through tvvan_route.gdb on snapshot SNAP (tools/play/boss/snap.sh,
# a BOOT=1,4 build; kept until tools/play/boss/snap.sh -d SNAP or 3 days); logs build/perf9f/van_TAG_EMU_<van>.log, one END
# line per van on stdout
cd "$(dirname "$0")/../.."; R=$PWD; mkdir -p build/perf9f; SNAP=$1; E=$2; TAG=$3
# van slot: x y type(0 left: door at +16, 1 right: door at -16) exitTV
for v in "88 8836 672 0 11336,1896" "621 3948 1936 0 5312,632" "1185 8148 864 1 8504,520" "1213 4932 1088 0 5312,632" "1214 6828 448 0 7512,1720" "1449 13008 1248 0 14784,840" "1587 9864 800 0 9280,536"; do
  set -- $v
  dx=$(( $4 == 0 ? $2 + 16 : $2 - 16 ))
  { printf 'set $vanX = %d\nset $vanY = %d\nset $endF = 1400\n' $dx $3; cat tools/play/tvvan_route.gdb; } > build/perf9f/van_${TAG}_${E}_$1.in.gdb
  cp tools/emu.sh build/boss/snaps/$SNAP/tools/emu.sh
  (cd build/boss/snaps/$SNAP && EMU=$E TIMEOUT=600 tools/play_test.sh $R/build/perf9f/van_${TAG}_${E}_$1.in.gdb $R/build/perf9f/van_${TAG}_${E}_$1 > /dev/null 2>&1)
  echo "van $1 (exit TV $5): $(grep -a '^END\|^ABORT' build/perf9f/van_${TAG}_${E}_$1.log | head -1)"
done
