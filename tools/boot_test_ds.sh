#!/bin/bash
# Phase 2.2 headless boot test in DuckStation (docs/32): boots build/game.cue (tools/data_disc.sh) with the
# current rsdkv5.elf and reports through GDB (DuckStation's stub on port 2345; docs/31 §6):
#   SCENE  every scene load (list position, folder, frame / vsync count when it starts)
#   SNAP   at frames 60, 300, 600, END: frames vs vsyncs, CPU hblanks, engine state, class counts, the guards
#          (dropped entities, compressed blocks, unlinked stubs reached) and each dataset's live peak / entries /
#          failures
#   STOP   where the CPU stopped if it was not our breakpoint (crash / abort), with a backtrace
# An 8 MB debug EXE (header SP 0x807FFF00) needs DuckStation's 8 MB RAM: the script turns Enable8MBRAM on for the
# run and restores settings.ini afterwards (the retail BIOS's RAM_SIZE value 0xB88 already maps an 8 MB window).
# Usage: tools/boot_test_ds.sh [OUT_PREFIX] [END_FRAME]   (TIMEOUT=seconds, default 150)
cd "$(dirname "$0")/.."
OUT=${1:-/tmp/rsdkv5_boot_ds}
END=${2:-900}
DS=/Applications/DuckStation.app/Contents/MacOS/DuckStation
SET="$HOME/Library/Application Support/DuckStation/settings.ini"

SP=$(python3 -c "import struct;print(hex(struct.unpack_from('<I',open('rsdkv5.ps-exe','rb').read(0x34),0x30)[0]))")
cp "$SET" "$OUT.settings.bak"
restore() { cp "$OUT.settings.bak" "$SET"; }
trap restore EXIT
if [ "$SP" != "0x801fff00" ]; then
  sed -i '' 's/^Enable8MBRAM = false/Enable8MBRAM = true/' "$SET"
  echo "8 MB debug EXE (SP $SP): DuckStation Enable8MBRAM on for this run"
fi

EXC_BREAK="# "
[ "${EXC:-0}" = 1 ] && EXC_BREAK=""
cat > "$OUT.gdb" <<GDBEOF
set pagination off
set confirm off
set print pretty off
file rsdkv5.elf
set architecture mips:3000
target remote localhost:2345
define snap
  printf "SNAP %s frames=%u vsyncs=%u hbl=%u maxhbl=%u over=%u init=%u state=%d listPos=%d cat=%d classes=%d objs=%d heapTotal=%u headroom=%u\n", \$arg0, g_ps1PerfFrames, g_ps1PerfVsyncs, g_ps1PerfLastHbl, g_ps1PerfMaxHbl, g_ps1PerfOverHbl, g_ps1InitDone, RSDK::sceneInfo.state, RSDK::sceneInfo.listPos, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.classCount, RSDK::objectClassCount, g_ps1HeapTotal, g_ps1HeapHeadroom
  printf "ENTITIES arena=%u peak=%u relocations=%u spareFail=%u ghostWrites=%u createSkips=%u records=%u recordsDropped=%u\n", RSDK::g_ps1EntityArenaBytes, RSDK::g_ps1EntityArenaPeak, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntitySpareFail, RSDK::g_ps1EntityGhostWrites, RSDK::g_ps1EntityCreateSkips, RSDK::g_ps1SceneRecordBytes, RSDK::g_ps1SceneRecordDropped
  printf "GUARD dropped=%u compressed=%u unlinkedCalls=%u unlinkedLast=%u globalVarsDropped=%u globalsSize=%d oom=%u\n", RSDK::g_ps1EntitiesDropped, RSDK::g_ps1CompressedMismatch, (unsigned)g_ps1UnlinkedCalls, (unsigned)g_ps1UnlinkedLast, RSDK::g_ps1GlobalVarsDropped, RSDK::g_ps1GlobalVarsSize, g_ps1OutOfMemory
  printf "STUBS seen %u %u %u %u %u %u %u %u (1 + index in build/gen/unlinked_stubs.c)\n", ((unsigned*)&g_ps1UnlinkedSeen)[0], ((unsigned*)&g_ps1UnlinkedSeen)[1], ((unsigned*)&g_ps1UnlinkedSeen)[2], ((unsigned*)&g_ps1UnlinkedSeen)[3], ((unsigned*)&g_ps1UnlinkedSeen)[4], ((unsigned*)&g_ps1UnlinkedSeen)[5], ((unsigned*)&g_ps1UnlinkedSeen)[6], ((unsigned*)&g_ps1UnlinkedSeen)[7]
  set \$d = 0
  while \$d < 5
    printf "DATASET %d live=%u entries=%u largest=%u fail=%u used=%u limit=%u\n", \$d, g_ps1StorageLivePeak[\$d], g_ps1StorageEntryPeak[\$d], g_ps1StorageLargest[\$d], g_ps1StorageFail[\$d], RSDK::dataStorage[\$d].usedStorage * 4, RSDK::dataStorage[\$d].storageLimit
    set \$d = \$d + 1
  end
end
# EXC=1: a real exception (not an IRQ, not a BIOS syscall) at the BIOS vector 0x80000080 with its cause code.
# Off by default: the breakpoint stops the emulator on every IRQ (runs ~20x slower).
${EXC_BREAK}break *0x80000080 if ((\$cause >> 2) & 0x1f) != 0 && ((\$cause >> 2) & 0x1f) != 8
${EXC_BREAK}commands
${EXC_BREAK}silent
${EXC_BREAK}printf "STOP exception code=%u cause=%08x bad=%08x ra=%08x sp=%08x frames=%u\n", (\$cause >> 2) & 0x1f, \$cause, \$bad, \$ra, \$sp, g_ps1PerfFrames
${EXC_BREAK}info symbol \$ra
${EXC_BREAK}info registers
${EXC_BREAK}snap "exception"
${EXC_BREAK}quit
${EXC_BREAK}end
break PS1StorageExhausted
commands
silent
printf "STOP dataset exhausted: set=%u size=%u\n", \$a0, \$a1
bt 12
snap "exhausted"
quit
end
break PS1OutOfMemory
commands
silent
printf "STOP out of memory: new(%u)\n", \$a0
bt 12
snap "oom"
quit
end
break RSDK::LoadSceneFolder
commands
silent
printf "SCENE listPos=%d folder=%s name=%s frames=%u vsyncs=%u\n", RSDK::sceneInfo.listPos, RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].folder, RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].name, g_ps1PerfFrames, g_ps1PerfVsyncs
continue
end
break perfTick if g_ps1PerfFrames == 60 || g_ps1PerfFrames == 300 || g_ps1PerfFrames == 600 || g_ps1PerfFrames == $END
commands
silent
snap "periodic"
if g_ps1PerfFrames == $END
    quit
end
continue
end
continue
printf "STOP pc=%08x frames=%u\n", \$pc, g_ps1PerfFrames
bt 12
snap "stop"
GDBEOF

pkill -f "DuckStation.app/Contents/MacOS" 2>/dev/null; sleep 1
"$DS" -batch -fastboot -nofullscreen -- "$PWD/build/game.cue" >/dev/null 2>&1 &
EMU=$!
sleep 6
timeout ${TIMEOUT:-150} /opt/homebrew/bin/gdb --batch -x "$OUT.gdb" > "$OUT.log" 2>&1
grep -E "^(SCENE|SNAP|GUARD|ENTITIES|STUBS|DATASET|STOP|#[0-9])|in section|^ R[0-9]| sr " "$OUT.log" | tee "$OUT.txt"
kill $EMU 2>/dev/null; wait $EMU 2>/dev/null
echo "(log: $OUT.log)"
