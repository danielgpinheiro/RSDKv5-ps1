#!/bin/bash
# runrec.sh EMU NAME...: build + snapshot each recipe's boot (the tree is shared: a mkdir lock), run it on EMU (pcsx|ds)
H=$(cd "$(dirname "$0")" && pwd)                       # these scripts, recipes, PER / EXTRA snippets
S=${BOSS_WORK:-$(cd "$H/../../.." && pwd)/build/boss}  # generated GDB scripts, logs, snapshots
mkdir -p $S
R=$(cd "$H/../../.." && pwd)
emu=$1; shift
for n in "$@"; do
  line=$(grep "^$n|" $H/recipes.txt) || { echo "no recipe $n"; continue; }
  IFS='|' read -r name boot ovl cls fn hits dx dy wait envs <<< "$line"
  prison=0; case "$envs" in *PRISON=1*) prison=1;; esac
  if [ "${NOBUILD:-0}" != 1 ]; then
    until mkdir $S/tree.lock 2>/dev/null; do sleep 5; done
    ( cd $R && make -j8 BOOT=$boot > $S/b_$n.log 2>&1 && make disc > $S/d_$n.log 2>&1 && $H/snap.sh $n > /dev/null ); ok=$?
    rmdir $S/tree.lock
    [ $ok = 0 ] || { echo "BUILD FAIL $n"; continue; }
  fi
  e=""; [ $emu = ds ] && e="EMU=ds"
  while [ $emu = ds ] && pgrep -f "DuckStation.app/Contents/MacOS" > /dev/null; do sleep 10; done
  ( cd $S/snaps/$n && eval "$e EXTRA=${EXTRA:-$H/x_gen.gdb} $envs R=$S/snaps/$n OVL=$ovl TIMEOUT=${TIMEOUT:-1800} $H/boss.sh bs_$n '*(short *)$cls' $fn $hits $dx $dy $wait $prison" ) > /dev/null 2>&1
  [ "${NOBUILD:-0}" = 1 ] || $H/snap.sh -d $n   # the snapshot this run made (KEEP_SNAP=1 keeps it)
  echo "== $n"; grep -a "^L \|rror" $S/bs_$n.log | grep -v "^L P \|^L B\|^L PL\|^L H " | cut -c1-230
done
