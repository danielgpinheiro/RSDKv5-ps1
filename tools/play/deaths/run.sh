#!/bin/bash
# 9b.3 (docs/32): run.sh EMU BOOT... (EMU pcsx|ds, BOOT = category,scene): per boot, build + snapshot under the boss
# harness's tree lock (build/boss/tree.lock), run deaths.gdb (death, time over, game over), print the L lines.
# NOBUILD=1 reuses the snapshot. Output in build/boss (d3_<boot>.log).
H=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$H/../../.." && pwd)
S=${BOSS_WORK:-$R/build/boss}
mkdir -p $S
emu=$1; shift
for b in "$@"; do
  n=d3_${b/,/_}
  if [ "${NOBUILD:-0}" != 1 ]; then
    until mkdir $S/tree.lock 2>/dev/null; do sleep 5; done
    ( cd $R && make -j8 BOOT=$b > $S/b_$n.log 2>&1 && make disc > $S/d_$n.log 2>&1 && $R/tools/play/boss/snap.sh $n > /dev/null ); ok=$?
    rmdir $S/tree.lock
    [ $ok = 0 ] || { echo "BUILD FAIL $b"; continue; }
  fi
  e=""; [ $emu = ds ] && e="EMU=ds"
  while [ $emu = ds ] && pgrep -f "DuckStation.app/Contents/MacOS" > /dev/null; do sleep 10; done
  ( cd $S/snaps/$n && eval "$e TIMEOUT=${TIMEOUT:-1500} tools/play_test.sh $H/${GDB:-deaths.gdb} $S/$n" ) > /dev/null 2>&1
  echo "== $b"; grep -a "^L \|rror\|No symbol" $S/$n.log | cut -c1-230
done
