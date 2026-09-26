#!/bin/bash
# Scripted play in DuckStation (docs/32 2.5): a GDB script drives pad 1 through g_ps1PadTestMask at chosen frames
# (break on perfTick) and prints the player, rings, timer, logic hblanks and live temp slots; the guards at the end.
# Usage: tools/play_test_ds.sh SCRIPT.gdb [OUT_PREFIX]   (TIMEOUT=seconds, default 500). The EXE must be a BOOT= build.
set -u
cd "$(dirname "$0")/.."
SCRIPT="$1"
OUT="${2:-/tmp/ps1_play}"
DS=/Applications/DuckStation.app/Contents/MacOS/DuckStation
SET="$HOME/Library/Application Support/DuckStation/settings.ini"
{ echo "set pagination off"; echo "set confirm off"; echo "file rsdkv5.elf"; echo "set architecture mips:3000"
  echo "target remote localhost:2345"; cat "$SCRIPT"; } > "$OUT.gdb"
SP=$(python3 -c "import struct;print(hex(struct.unpack_from('<I',open('rsdkv5.ps-exe','rb').read(0x34),0x30)[0]))")
cp "$SET" "$OUT.settings.bak"
restore() { cp "$OUT.settings.bak" "$SET"; }
trap restore EXIT
if [ "$SP" != "0x801fff00" ]; then
  sed -i '' 's/^Enable8MBRAM = false/Enable8MBRAM = true/' "$SET"
  echo "8 MB debug EXE (SP $SP): DuckStation Enable8MBRAM on for this run"
fi
pkill -f "DuckStation.app/Contents/MacOS" 2>/dev/null; sleep 1
"$DS" -batch -fastboot -nofullscreen -- "$PWD/build/game.cue" >/dev/null 2>&1 &
EMU=$!
sleep 6
timeout -s INT ${TIMEOUT:-500} /opt/homebrew/bin/gdb --batch -x "$OUT.gdb" > "$OUT.log" 2>&1
kill $EMU 2>/dev/null; wait $EMU 2>/dev/null
grep -E "^(F |GUARDS)" "$OUT.log" | tee "$OUT.txt"
echo "(log: $OUT.log)"
