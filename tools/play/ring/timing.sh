#!/bin/bash
# docs/32 9o: frame timing next to GHZ1's giant ring on a 2 MB BOOT=1,0 build (snapshot ring_t): MODE hidden (visible 0),
# solid (enabled 1) or wire (enabled 0); EMU pcsx | ds (DuckStation, retail BIOS). Frames 500-800 after the boss hook's
# warp (frame 300): avg vblanks a frame x100 and avg CPU hblanks. NOBUILD=1 reuses the snapshot.
# Usage: tools/play/ring/timing.sh EMU MODE...
set -u
cd "$(dirname "$0")/../../.."
R=$PWD; S=$R/build/boss; mkdir -p $S
emu=$1; shift
if [ "${NOBUILD:-0}" != 1 ]; then
  until mkdir $S/tree.lock 2>/dev/null; do sleep 5; done
  ( make -j8 BOOT=1,0 > $S/b_ring_t.log 2>&1 && make disc > $S/d_ring_t.log 2>&1 && tools/play/boss/snap.sh ring_t > /dev/null ); ok=$?
  rmdir $S/tree.lock
  [ $ok = 0 ] || { echo "BUILD FAIL"; exit 1; }
fi
for mode in "$@"; do
  case $mode in
    hidden) set_="set var *(unsigned char *)((char *)RSDK::objectEntityPtr[g_ps1TestBossSlot] + 85) = 0" ;;
    solid)  set_="set var *(int *)((char *)RSDK::objectEntityPtr[g_ps1TestBossSlot] + 140) = 1" ;;
    wire)   set_="set var *(int *)((char *)RSDK::objectEntityPtr[g_ps1TestBossSlot] + 140) = 0" ;;
  esac
  cat > $S/ring_t_$mode.gdb <<G
tbreak main
commands
silent
set var g_ps1TestStops[0] = 300
set var g_ps1TestStops[1] = 500
set var g_ps1TestStops[2] = 800
continue
end
break ps1TestStop
commands
silent
if g_ps1PerfFrames == 300
  set var g_ps1TestBossClass = *(short *)SpecialRing
  set var g_ps1TestBossNth = 0
  set var g_ps1TestBossPin = -1
  set var g_ps1TestBossDX = -60
  set var g_ps1TestBossDY = 0
  set var g_ps1TestBossWarp = 1
end
if g_ps1PerfFrames == 500
  $set_
  set \$tv0 = g_ps1PerfVsyncs
  set \$tf0 = g_ps1PerfFrames
  set \$th0 = g_ps1PerfSumHbl
end
if g_ps1PerfFrames == 800
  printf "L RINGT mode=$mode emu=$emu slot=%d avgV100=%u avgHbl=%u\n", g_ps1TestBossSlot, (g_ps1PerfVsyncs - \$tv0) * 100 / (g_ps1PerfFrames - \$tf0), (g_ps1PerfSumHbl - \$th0) / (g_ps1PerfFrames - \$tf0)
  quit
end
continue
end
continue
G
  e=""; [ $emu = ds ] && e="EMU=ds"
  while [ $emu = ds ] && pgrep -f "DuckStation.app/Contents/MacOS" > /dev/null; do sleep 10; done
  ( cd $S/snaps/ring_t && eval "$e TIMEOUT=${TIMEOUT:-900} tools/play_test.sh $S/ring_t_$mode.gdb $S/ring_t_${emu}_$mode" ) > /dev/null 2>&1
  grep -a "^L RINGT\|No symbol\|rror" $S/ring_t_${emu}_$mode.log | cut -c1-160
done
