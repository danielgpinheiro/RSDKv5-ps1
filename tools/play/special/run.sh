#!/bin/bash
# 9b.4 (docs/32): run.sh EMU NAME BOOT UFO POSTNTH PID: zone BOOT -> giant ring -> UFO (UFO = 0-6) -> back -> star post
# (POSTNTH-th) -> Blue Spheres -> back, as PID (1 Sonic, 2 Tails, 4 Knuckles). Builds + snapshots BOOT under the tree
# lock unless NOBUILD=1 (snapshot sp_<boot>); output build/boss/sp_NAME.log.
H=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$H/../../.." && pwd)
S=${BOSS_WORK:-$R/build/boss}
mkdir -p $S
emu=$1 name=$2 boot=$3 ufo=$4 post=$5 pid=$6
n=sp_${boot/,/_}
if [ "${NOBUILD:-0}" != 1 ]; then
  until mkdir $S/tree.lock 2>/dev/null; do sleep 5; done
  ( cd $R && make -j8 BOOT=$boot > $S/b_$n.log 2>&1 && make disc > $S/d_$n.log 2>&1 && $R/tools/play/boss/snap.sh $n > /dev/null ); ok=$?
  rmdir $S/tree.lock
  [ $ok = 0 ] || { echo "BUILD FAIL $boot"; exit 1; }
fi
sed "s|@R@|$S/snaps/$n|; s|@UFO@|$ufo|; s|@POSTNTH@|$post|; s|@PID@|$pid|" $H/special.gdb.in > $S/sp_$name.in.gdb
e=""; [ $emu = ds ] && e="EMU=ds"
while [ $emu = ds ] && pgrep -f "DuckStation.app/Contents/MacOS" > /dev/null; do sleep 10; done
( cd $S/snaps/$n && eval "$e TIMEOUT=${TIMEOUT:-2400} tools/play_test.sh $S/sp_$name.in.gdb $S/sp_$name" ) > /dev/null 2>&1
echo "== $name"; grep -a "^L \|rror\|No symbol" $S/sp_$name.log | cut -c1-230
