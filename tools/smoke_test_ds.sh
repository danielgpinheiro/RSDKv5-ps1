#!/bin/bash
# Phase 0 smoke test in DuckStation (the PCSX-Redux one is tools/smoke_test.sh; this port started while
# another session held PCSX-Redux). DuckStation: GDB stub on port 2345 (settings.ini [Debug]
# EnableGDBServer), `file` the ELF before `target remote`, `set architecture mips:3000` (docs/31 §6).
# Checks: the 6 files' CRCs vs the host, the SPU read-back, the loading icon ticks, fps. Not checked
# here: the VRAM pixels of the icon (no VRAM dump API; see the screenshot) and the pad (no input
# injection in DuckStation; the same layer is verified on the Sonic CD disc).
# Needs rsdkv5.ps-exe (make PLATFORM_TEST=1) + build/game.cue (tools/smoke_disc.sh).
# Usage: tools/smoke_test_ds.sh [OUT_PREFIX]
cd "$(dirname "$0")/.."
OUT=${1:-/tmp/rsdkv5_smoke_ds}
DS=/Applications/DuckStation.app/Contents/MacOS/DuckStation
cat > "$OUT.gdb" <<GDBEOF
set pagination off
set confirm off
file rsdkv5.elf
set architecture mips:3000
target remote localhost:2345
break PS1LoadingDebugHook if tick == 3
commands
silent
printf "ICON tick=%u frame=%u\n", tick, g_ps1LoadingFrame
continue
end
break PS1LoadingDoneHook
commands
silent
printf "LOAD us=%u requests=%u sectors=%u\n", us, g_ps1FsRequests, g_ps1FsSectors
continue
end
tbreak PS1ChainEnd if g_ps1PerfFrames == 120
continue
printf "RESULT done=%u files=%u spuOk=%u spuBytes=%u frames=%u vsyncs=%u\n", g_ps1TestDone, g_ps1TestFilesOk, g_ps1TestSpuOk, g_ps1TestSpuBytes, g_ps1PerfFrames, g_ps1PerfVsyncs
set \$i = 0
while \$i < 6
  printf "FILE %d size=%u crc=%08x\n", \$i, g_ps1TestSize[\$i], g_ps1TestCrc[\$i]
  set \$i = \$i + 1
end
detach
GDBEOF
pkill -f "DuckStation.app/Contents/MacOS" 2>/dev/null; sleep 1
"$DS" -batch -fastboot -nofullscreen -- "$PWD/build/game.cue" >/dev/null 2>&1 &
sleep 6
timeout ${TIMEOUT:-180} /opt/homebrew/bin/gdb --batch -x "$OUT.gdb" 2>&1 | grep -E "^(ICON|LOAD|RESULT|FILE) " | tee "$OUT.txt"
python3 - "$OUT.txt" <<'PYEOF'
import re, sys, zlib
files = ['Data/Game/GameConfig.bin', 'Data/Game/RSDKConfig.bin', 'Data/Stages/GHZ/StageConfig.bin',
         'Data/Stages/GHZ/Scene1.bin', 'Data/Stages/GHZ/TileConfig.bin', 'Data/Stages/GHZ/Scene2.bin']
ok = 0
for l in open(sys.argv[1]):
    m = re.match(r'FILE (\d) size=(\d+) crc=([0-9a-f]+)', l)
    if m:
        d = open('build/iso/' + files[int(m[1])], 'rb').read()
        good = int(m[2]) == len(d) and int(m[3], 16) == zlib.crc32(d) & 0xFFFFFFFF
        ok += good
        print('%-38s %s' % (files[int(m[1])], 'CRC OK' if good else 'MISMATCH'))
print('%d/6 files match the host' % ok)
PYEOF
echo "(DuckStation left running for a screenshot; stop it with: pkill -f DuckStation.app/Contents/MacOS)"
