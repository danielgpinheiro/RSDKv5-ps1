#!/bin/bash
# docs/ports/mania/32 "Stage Select dev menu": the `make DEVMENU=1` debug disc (an 8 MB link; Select opens the engine's
# stage select), built in a shadow copy, PS1/work/shadow/mania-devmenu (beside the workspace's shim links, as
# tests/run.py build_mania), never in this checkout: other sessions' builds here are never disturbed and natural
# objects never meet dev ones. Sources: this checkout's tracked + untracked files; data, graphics, audio and video: an
# APFS clone of its build/ (the asset pipeline is not rerun: run it here first). The disc's VERSION.TXT is this
# checkout's commit (the shadow is no repository).
# Output: build/dev/SonicMania-PS1-DEV.bin / .cue, with rsdkv5.elf and the overlay ELFs (GDB symbols).
# Run it with the emulator's 8 MB RAM option on (DuckStation: Settings > Console > Enable 8MB RAM; PCSX-Redux:
# Configuration > Emulation > 8MB), off again for the stock discs; with 2 MB it stops at a red screen.
# Harness runs (tools/emu.sh reads the EXE and the disc from the current directory) start in the shadow, so
# KEEP_SHADOW=1 keeps it for tools/play/devmenu/run.sh (which deletes it when it ends); by default the shadow is deleted
# once the disc is copied out (user 2026-10-28: delete the runs' copies after them; the next build is a full one).
# Licensed by default (2026-11-08: the Makefile's `disc:` rule passes a literal `none`, so `LICENSE ?= ../LICENSE.DAT`
# -- the `dist:` default -- never reached it and every dev disc came out unlicensed; `../LICENSE.DAT` resolves inside the
# shadow through the workspace's shim). Pass LICENSE=none for an emulator-only image.
# Usage: [KEEP_SHADOW=1] tools/dev_disc.sh [BOOT=c,i] [other make switches]
set -e
cd "$(dirname "$0")/.."
REPO=$PWD
SH=$(cd ../.. && pwd)/work/shadow/mania-devmenu
OUT=$REPO/build/dev
[ -d "$REPO/build/iso/Data" ] || { echo "no build/iso here: build the natural disc first (docs/ports/mania/32 phase 11)"; exit 1; }
mkdir -p "$SH" "$OUT"

LST=$(mktemp)
git ls-files -co --exclude-standard | while IFS= read -r f; do [ -f "$f" ] && printf '%s\n' "$f"; done > "$LST"
rsync -a --checksum --files-from="$LST" "$REPO/" "$SH/"
rm -f "$LST"
src_m=$(stat -f %m "$REPO/build/iso")
if [ ! -d "$SH/build" ] || [ "$(cat "$SH/.build_clone_of" 2>/dev/null)" != "$src_m" ]; then
  rm -rf "$SH/build"
  cp -c -R "$REPO/build" "$SH/build"
  rm -rf "$SH/build/dev"
  echo "$src_m" > "$SH/.build_clone_of"
fi
/usr/bin/python3 tools/gen_version.py "$SH/VERSION.dev.txt" "$REPO" >/dev/null
export PS1_VERSION_FILE=$SH/VERSION.dev.txt

cd "$SH"
# the same switches for both: make reads its stamps at parse time (a bare `make disc` would rebuild the natural EXE)
make -j8 DEVMENU=1 "$@"
make disc DEVMENU=1 LICENSE=${LICENSE:-../LICENSE.DAT} "$@"
[ "$(od -An -tx4 -j48 -N4 rsdkv5.ps-exe | tr -d ' ')" = "807fff00" ] || { echo "not an 8 MB link"; exit 1; }

rm -rf "$OUT/ovl"
mkdir -p "$OUT/ovl"
cp -c build/game.bin "$OUT/SonicMania-PS1-DEV.bin"
sed 's/"game\.bin"/"SonicMania-PS1-DEV.bin"/' build/game.cue > "$OUT/SonicMania-PS1-DEV.cue"
cp rsdkv5.elf rsdkv5.ps-exe "$OUT/"
for e in build/gen/ovl/*/*.elf; do cp "$e" "$OUT/ovl/"; done
printf '%s %s\n' "$(cat "$PS1_VERSION_FILE")" "$*" > "$OUT/BUILD.txt"
if [ "${KEEP_SHADOW:-0}" = 1 ]; then
  echo "dev disc: $OUT/SonicMania-PS1-DEV.cue ($(cat "$OUT/BUILD.txt"); shadow kept: $SH)"
else
  cd "$REPO" && rm -rf "$SH"
  echo "dev disc: $OUT/SonicMania-PS1-DEV.cue ($(cat "$OUT/BUILD.txt"); shadow deleted)"
fi
