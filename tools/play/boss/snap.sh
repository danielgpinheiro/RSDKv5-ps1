#!/bin/bash
# snap.sh NAME: a frozen copy of the current build (EXE, ELF, disc, overlay ELFs, tools) for a DuckStation run
# (EMU=ds $S/snaps/NAME/tools/play_test.sh ...), so PCSX-Redux builds in the tree don't change its files
H=$(cd "$(dirname "$0")" && pwd)                       # these scripts, recipes, PER / EXTRA snippets
S=${BOSS_WORK:-$(cd "$H/../../.." && pwd)/build/boss}  # generated GDB scripts, logs, snapshots
mkdir -p $S
R=$(cd "$H/../../.." && pwd)
[ -n "$1" ] || { echo "snap.sh NAME"; exit 1; }
D=$S/snaps/$1
rm -rf $D; mkdir -p $D/build/gen
cp -c $R/rsdkv5.elf $R/rsdkv5.ps-exe $D/
cp -c $R/build/game.cue $R/build/game.bin $D/build/
cp -cR $R/build/gen/ovl $D/build/gen/
cp -cR $R/tools $D/tools
echo "snapshot $D"
