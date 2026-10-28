#!/bin/bash
# The debug overlay's A/B (docs/ports/mania/32 "Debug overlay"): two runs of one `make DEBUG_OVERLAY=1 BOOT=c,s` build
# (make disc first) on PCSX-Redux, the overlay shown (g_ps1DbgVisible 1) and hidden (0) from boot; the same frames in
# both (the emulation is deterministic and the hidden overlay still counts and logs, so only its drawing differs):
#   - frames 300-900: CPU hblanks, the flip gap (the GPU's tail + the vblank wait), slow frames, vblanks (F AB lines);
#   - frame 900: a VRAM dump of each (the web server, tools/vramshot.py), compared by overlay_check.py vram: outside both
#     framebuffers not one halfword may differ, inside them only the overlay's box (zero VRAM);
#   - frames 1000-1010: L2 + R2 held through pad 1's real path (tools/pad.lua); at 1100 the overlay must be toggled
#     (F TOGGLE lines).
# Usage: EMU_SLOT=N tools/play/overlay_ab.sh OUT   (port 8080, the web server's, must be free; TIMEOUT= per run)
set -u
cd "$(dirname "$0")/../.."
OUT=$1
if nc -z localhost 8080 2>/dev/null; then echo "port 8080 busy (another emulator's web server)"; exit 2; fi
for vis in 1 0; do
  G=$OUT.v$vis.gdb
  cat > "$G" <<EOF
tbreak main
commands
silent
set var g_ps1TestStops[0] = 300
set var g_ps1TestStops[1] = 900
set var g_ps1TestStops[2] = 1000
set var g_ps1TestStops[3] = 1010
set var g_ps1TestStops[4] = 1100
set var g_ps1DbgVisible = $vis
continue
end
break ps1TestStop
commands
silent
set \$ab_f = *(unsigned *)&::g_ps1PerfFrames
if \$ab_f == 300
  set \$ab_cpu = *(unsigned *)&::g_ps1PerfSumHbl
  set \$ab_flip = *(unsigned *)&::g_ps1PerfSumFlip
  set \$ab_slow = *(unsigned *)&::g_ps1PerfSlowFrames
  set \$ab_vbl = *(unsigned *)&::g_ps1PerfVsyncs
end
if \$ab_f == 900
  printf "F AB vis=$vis frames=600 cpu=%u flip=%u slow=%u vsyncs=%u prims=%u bad=%u ovl_hbl_max=%u box=%u,%u,%u,%u\n", *(unsigned *)&::g_ps1PerfSumHbl - \$ab_cpu, *(unsigned *)&::g_ps1PerfSumFlip - \$ab_flip, *(unsigned *)&::g_ps1PerfSlowFrames - \$ab_slow, *(unsigned *)&::g_ps1PerfVsyncs - \$ab_vbl, *(unsigned *)&::g_ps1DbgPrims, *(unsigned *)&::g_ps1DbgBadPrims, *(unsigned *)&::g_ps1DbgHblMax, ((unsigned short *)&g_ps1DbgBox)[0], ((unsigned short *)&g_ps1DbgBox)[1], ((unsigned short *)&g_ps1DbgBox)[2], ((unsigned short *)&g_ps1DbgBox)[3]
  shell python3 tools/vramshot.py $OUT.v$vis > /dev/null
end
if \$ab_f == 1000
  shell curl -s 'http://localhost:8080/api/v1/lua/pad?press=L2,R2'
end
if \$ab_f == 1010
  shell curl -s 'http://localhost:8080/api/v1/lua/pad?release=L2,R2'
end
if \$ab_f >= 1100
  printf "F TOGGLE from=$vis now=%u\n", *(unsigned *)&::g_ps1DbgVisible
  quit
end
continue
end
continue
EOF
  EMU_WEB=1 EMU_LOG="$PWD/$OUT.v$vis.emu.log" TIMEOUT=${TIMEOUT:-600} bash tools/play_test.sh "$G" "$OUT.v$vis"
done
box=$(grep -ao "box=[0-9,]*" "$OUT.v1.log" | head -1 | cut -d= -f2 | tr , ' ')
python3 tools/play/overlay_check.py vram "$OUT.v1_vram.bin" "$OUT.v0_vram.bin" $box
