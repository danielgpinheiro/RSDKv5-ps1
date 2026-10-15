#!/bin/bash
# snap.sh NAME: a frozen copy of the current build (EXE, ELF, disc, overlay ELFs, tools) for a DuckStation run
# (EMU=ds $S/snaps/NAME/tools/play_test.sh ...), so PCSX-Redux builds in the tree don't change its files
H=$(cd "$(dirname "$0")" && pwd)                       # these scripts, recipes, PER / EXTRA snippets
S=${BOSS_WORK:-$(cd "$H/../../.." && pwd)/build/boss}  # generated GDB scripts, logs, snapshots
mkdir -p $S
R=$(cd "$H/../../.." && pwd)
# snap.sh -d NAME...: delete those snapshots once their run is over (each disc image is ~0.7 GB; 325 left behind filled
# 228 GB by 2026-10-15). The run scripts call it for the snapshot they made; KEEP_SNAP=1 keeps it for NOBUILD=1 reuse.
if [ "$1" = -d ]; then
  shift; [ "${KEEP_SNAP:-0}" = 1 ] && exit 0
  for n in "$@"; do [ -n "$n" ] && rm -rf "$S/snaps/$n"; done
  exit 0
fi
[ -n "$1" ] || { echo "snap.sh NAME | snap.sh -d NAME..."; exit 1; }
# A snapshot kept or left by a stopped run goes after SNAP_DAYS days (default 3)
[ -d $S/snaps ] && find $S/snaps -mindepth 1 -maxdepth 1 -type d -mtime +${SNAP_DAYS:-3} -exec rm -rf {} + 2>/dev/null
D=$S/snaps/$1
rm -rf $D; mkdir -p $D/build/gen
cp -c $R/rsdkv5.elf $R/rsdkv5.ps-exe $D/
cp -c $R/build/game.cue $R/build/game.bin $D/build/
cp -cR $R/build/gen/ovl $D/build/gen/
cp -cR $R/tools $D/tools
echo "snapshot $D"
