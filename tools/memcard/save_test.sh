#!/bin/bash
# NOT ADAPTED to Mania yet (phase 8): the card file id / payload checks below are still Sonic CD's.
# Save test (docs/28 phase 8) on temporary card images: never the player's cards. PCSX-Redux stores
# -memcard1/-memcard2 in ~/.config/pcsx-redux/pcsx.json, so it is backed up and restored on exit.
# Steps (GDB, default build, Title): first read (fresh formatted card: no save) -> write saveRAM with two
# marker values -> snapshot of card 1 (tools/memcard/mcd.py: the file is listed) -> RAM scrambled -> re-read
# from the card -> the markers are back.
# Usage: tools/memcard/save_test.sh [WORKDIR]
set -u
cd "$(dirname "$0")/../.."
W=${1:-/tmp/rsdkv5_save_test}; mkdir -p "$W"
PCSX=/Applications/PCSX-Redux.app/Contents/MacOS/PCSX-Redux
CFG="$HOME/.config/pcsx-redux"
MCD="python3 tools/memcard/mcd.py"
cp -p "$CFG/pcsx.json" "$W/pcsx.json.bak"
shasum "$CFG"/memcard*.mcd > "$W/cards.sha" 2>/dev/null
restore() { cp -p "$W/pcsx.json.bak" "$CFG/pcsx.json"; }
trap restore EXIT
$MCD format "$W/mc1.mcd" >/dev/null && $MCD raw "$W/mc2.mcd" >/dev/null
cat > "$W/save.gdb" <<GDBEOF
set pagination off
set confirm off
target remote localhost:3333
symbol-file rsdkv5.elf
break PS1PerfTick if g_ps1PerfFrames == 100
continue
delete
set var g_ps1SaveTest = 2
break PS1PerfTick if g_ps1PerfFrames == 110
continue
delete
printf "READ1 reads=%u err=%d state=%d\n", g_ps1SaveReads, g_ps1SaveLastError, s_ps1SaveState
set var saveRAM[3] = 0x12345678
set var saveRAM[1000] = 777
set var g_ps1SaveTest = 1
break PS1PerfTick if g_ps1PerfFrames == 200
continue
delete
printf "WRITE writes=%u err=%d outOfRange=%u\n", g_ps1SaveWrites, g_ps1SaveLastError, g_ps1SaveRamOutOfRange
shell cp $W/mc1.mcd $W/mc1_saved.mcd
set var saveRAM[3] = 0
set var saveRAM[1000] = 0
set var g_ps1SaveTest = 2
break PS1PerfTick if g_ps1PerfFrames == 300
continue
delete
printf "READ2 reads=%u err=%d saveRAM[3]=%x saveRAM[1000]=%d\n", g_ps1SaveReads, g_ps1SaveLastError, saveRAM[3], saveRAM[1000]
detach
GDBEOF
# Other sessions share this emulator: never kill theirs, only our own PID.
pgrep -x PCSX-Redux >/dev/null && { echo "PCSX-Redux is already running (another session?): not starting it" >&2; exit 2; }
$PCSX -fastboot -interpreter -debugger -gdb -gdb-port 3333 -webserver -memcard1 "$W/mc1.mcd" -memcard2 "$W/mc2.mcd" \
      -iso build/game.cue -exe rsdkv5.ps-exe >/dev/null 2>&1 &
EMU=$!
sleep 4
timeout ${TIMEOUT:-300} /opt/homebrew/bin/gdb --batch -x "$W/save.gdb" 2>&1 | grep -E "^(READ|WRITE)"
kill $EMU 2>/dev/null; wait $EMU 2>/dev/null
echo "card 1 after the write:"; $MCD list "$W/mc1_saved.mcd"; $MCD check "$W/mc1_saved.mcd" BAHBRW-00003SONICCD
echo "card 2 (blank, slot 2) untouched:"; $MCD list "$W/mc2.mcd" 2>&1 | head -2
shasum "$CFG"/memcard*.mcd 2>/dev/null | diff -q - "$W/cards.sha" >/dev/null && echo "player cards unchanged" || echo "player cards CHANGED"
