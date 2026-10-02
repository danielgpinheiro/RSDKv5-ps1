#!/bin/bash
# Save test (docs/32 phase 8.1) on temporary card images, never the player's cards. PCSX-Redux stores -memcard1/-memcard2
# in ~/.config/pcsx-redux/pcsx.json, so it is backed up and restored on exit. The natural build (Logos first), 2 MB.
# Steps (GDB, through the engine's test hook g_ps1CardTest: the game's own buffers, the real staging):
#   boot on a fresh formatted card: the game's loads find nothing, nothing written;
#   markers in globals->saveRAM ([3], [0x9FF] saved; [0xA00] past the kept part) and optionsRAM[5] -> SaveData.bin and
#   Options.bin saved; Achievements.bin skipped (not written);
#   the writes run in the background (8.4): the card image is copied once they are done;
#   RAM scrambled -> both re-read (from the resident copy of the card file: the reboot flows read the card itself)
#   -> the markers back, [0xA00] untouched;
#   host: the card lists one file (title, icon, blocks, CRC), its MSV1 sections decode to the markers; card 2 blank.
# Usage: tools/memcard/save_test.sh [WORKDIR]   (TIMEOUT=seconds)
set -u
cd "$(dirname "$0")/../.."
W=${1:-${TMPDIR:-/tmp}/rsdkv5_save_test}; rm -rf "$W"; mkdir -p "$W"
PCSX=/Applications/PCSX-Redux.app/Contents/MacOS/PCSX-Redux
CFG="$HOME/.config/pcsx-redux"
MCD="/usr/bin/python3 tools/memcard/mcd.py"
FILE=BAHBRW-00006MANIA
# Other sessions share this emulator (their -portable copies run on their own ports): never start beside a default one
ps -Ao command | grep "[P]CSX-Redux.app/Contents/MacOS/PCSX-Redux" | grep -qv -- "-portable" && { echo "PCSX-Redux busy" >&2; exit 2; }
cp -p "$CFG/pcsx.json" "$W/pcsx.json.bak"
shasum "$CFG"/memcard*.mcd > "$W/cards.sha" 2>/dev/null
restore() { cp -p "$W/pcsx.json.bak" "$CFG/pcsx.json"; }
trap restore EXIT
$MCD format "$W/mc1.mcd" >/dev/null && $MCD format "$W/mc2.mcd" >/dev/null && cp "$W/mc2.mcd" "$W/mc2_blank.mcd"
cat > "$W/save.gdb" <<GDBEOF
set pagination off
set confirm off
file rsdkv5.elf
set architecture mips:3000
target remote localhost:3333
break perfTick if g_ps1PerfFrames == 300
continue
delete
printf "BOOT loads=%u saves=%u skipped=%u errors=%u lastError=%u stageFail=%u\n", g_ps1CardLoads, g_ps1CardSaves, g_ps1CardSkipped, g_ps1CardErrors, g_ps1CardLastError, g_ps1CardStageFail
set var globals->saveRAM[3] = 0x12345678
set var globals->saveRAM[0x9FF] = 777
set var globals->saveRAM[0xA00] = 555
set var globals->optionsRAM[5] = 0xABCD
set var g_ps1CardTestPtr = (void *)globals->saveRAM
set var g_ps1CardTestSize = sizeof(globals->saveRAM)
set var g_ps1CardTestFile = 0
set var g_ps1CardTest = 1
break perfTick if g_ps1PerfFrames == 310
continue
delete
printf "SAVE SaveData result=%u saves=%u payload=%u errors=%u lastError=%u stageFail=%u tooBig=%u\n", g_ps1CardTestResult, g_ps1CardSaves, g_ps1CardPayload, g_ps1CardErrors, g_ps1CardLastError, g_ps1CardStageFail, g_ps1CardTooBig
set var g_ps1CardTestPtr = (void *)globals->optionsRAM
set var g_ps1CardTestSize = sizeof(globals->optionsRAM)
set var g_ps1CardTestFile = 1
set var g_ps1CardTest = 1
break perfTick if g_ps1PerfFrames == 320
continue
delete
printf "SAVE Options result=%u saves=%u payload=%u errors=%u\n", g_ps1CardTestResult, g_ps1CardSaves, g_ps1CardPayload, g_ps1CardErrors
set var g_ps1CardTestFile = 3
set var g_ps1CardTest = 1
break perfTick if g_ps1PerfFrames == 330
continue
delete
printf "SAVE Achievements result=%u saves=%u skipped=%u\n", g_ps1CardTestResult, g_ps1CardSaves, g_ps1CardSkipped
break perfTick if g_ps1PerfFrames == 600
continue
delete
printf "WRITES (background) done=%u errors=%u lastWriteUs=%u lastError=%u\n", g_ps1CardAsyncDone, g_ps1CardAsyncErrors, g_ps1CardWriteUs, g_ps1CardLastError
shell cp $W/mc1.mcd $W/mc1_saved.mcd
set var globals->saveRAM[3] = 0
set var globals->saveRAM[0x9FF] = 0
set var globals->saveRAM[0xA00] = 999
set var globals->optionsRAM[5] = 0
set var g_ps1CardTestPtr = (void *)globals->saveRAM
set var g_ps1CardTestSize = sizeof(globals->saveRAM)
set var g_ps1CardTestFile = 0
set var g_ps1CardTest = 2
break perfTick if g_ps1PerfFrames == 610
continue
delete
printf "LOAD SaveData result=%u loads=%u saveRAM[3]=%x [0x9FF]=%d [0xA00]=%d\n", g_ps1CardTestResult, g_ps1CardLoads, globals->saveRAM[3], globals->saveRAM[0x9FF], globals->saveRAM[0xA00]
set var g_ps1CardTestPtr = (void *)globals->optionsRAM
set var g_ps1CardTestSize = sizeof(globals->optionsRAM)
set var g_ps1CardTestFile = 1
set var g_ps1CardTest = 2
break perfTick if g_ps1PerfFrames == 620
continue
delete
printf "LOAD Options result=%u loads=%u optionsRAM[5]=%x errors=%u stageFail=%u\n", g_ps1CardTestResult, g_ps1CardLoads, globals->optionsRAM[5], g_ps1CardErrors, g_ps1CardStageFail
printf "TMP tailPeak=%u limit=%u\n", g_ps1StorageTailPeak[4], RSDK::dataStorage[4].storageLimit
detach
GDBEOF
$PCSX -fastboot -interpreter -debugger -gdb -gdb-port 3333 -memcard1 "$W/mc1.mcd" -memcard2 "$W/mc2.mcd" \
      -iso build/game.cue -exe rsdkv5.ps-exe >/dev/null 2>&1 &
EMU=$!
sleep 4
timeout ${TIMEOUT:-400} /opt/homebrew/bin/gdb --batch -x "$W/save.gdb" > "$W/gdb.log" 2>&1
grep -aE "^(BOOT|SAVE|WRITES|LOAD|TMP)" "$W/gdb.log"
kill $EMU 2>/dev/null; wait $EMU 2>/dev/null
echo "card 1 after the saves:"; $MCD list "$W/mc1_saved.mcd"
$MCD check "$W/mc1_saved.mcd" $FILE --title "SONIC MANIA" --icon s_ps1McIconMania
$MCD msv "$W/mc1_saved.mcd" $FILE --dump "$W/sections"
/usr/bin/python3 - "$W/sections" <<'PYEOF'
import struct, sys, os
d = sys.argv[1]
s = open(os.path.join(d, 'SaveData.bin'), 'rb').read()
o = open(os.path.join(d, 'Options.bin'), 'rb').read()
w = lambda b, i: struct.unpack_from('<I', b, 4 * i)[0]
ok = len(s) == 0xA00 * 4 and w(s, 3) == 0x12345678 and w(s, 0x9FF) == 777 and len(o) == 0x80 * 4 and w(o, 5) == 0xABCD
print('sections: SaveData %d B [3]=%x [0x9FF]=%d, Options %d B [5]=%x: %s' % (len(s), w(s, 3), w(s, 0x9FF), len(o), w(o, 5), 'OK' if ok else 'MISMATCH'))
PYEOF
cmp -s "$W/mc2.mcd" "$W/mc2_blank.mcd" && echo "card 2 untouched" || echo "card 2 CHANGED"
shasum "$CFG"/memcard*.mcd 2>/dev/null | diff -q - "$W/cards.sha" >/dev/null && echo "player cards unchanged" || echo "player cards CHANGED"
