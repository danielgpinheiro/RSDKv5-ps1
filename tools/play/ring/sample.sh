#!/bin/bash
# docs/ports/mania/32 9o: the PC sampler next to GHZ1's giant ring (frames 500-900), the ring used (ENABLED=0: wireframe, the default)
# or live (ENABLED=1). Builds SAMPLE=1 BOOT=1,0 (2 MB) under the tree lock, EMU_RAM=8mb (the histogram at 0x80400000).
# Output build/perf9o/ring<E>_hist.bin, then the report (top 30). Read the report before rebuilding (it maps through
# the current ELFs).
set -u
cd "$(dirname "$0")/../../.."
E=${ENABLED:-0}; OUT=build/perf9o; mkdir -p $OUT
until mkdir build/boss/tree.lock 2>/dev/null; do sleep 5; done
( make -j8 SAMPLE=1 BOOT=1,0 > $OUT/b.log 2>&1 && make disc > $OUT/d.log 2>&1 ); ok=$?
rmdir build/boss/tree.lock
[ $ok = 0 ] || { echo BUILD FAIL; exit 1; }
cat > $OUT/ring$E.gdb <<G
tbreak main
commands
silent
set var g_ps1TestStops[0] = 300
set var g_ps1TestStops[1] = 500
set var g_ps1TestStops[2] = 900
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
  set var *(int *)((char *)RSDK::objectEntityPtr[g_ps1TestBossSlot] + 140) = $E
  set g_ps1SampleOn = 1
end
if g_ps1PerfFrames == 900
  set g_ps1SampleOn = 0
  dump binary memory $OUT/ring${E}_hist.bin 0x80400000 0x80600000
  quit
end
continue
end
continue
G
rm -f $OUT/ring${E}_hist.bin
EMU_RAM=8mb TIMEOUT=${TIMEOUT:-1200} tools/play_test.sh $OUT/ring$E.gdb $OUT/ring$E > /dev/null 2>&1
/usr/bin/python3 tools/play/sample_report.py $OUT/ring${E}_hist.bin 400 --ovl GHZ --top ${TOP:-30}
