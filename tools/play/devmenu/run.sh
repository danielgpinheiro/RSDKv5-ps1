#!/bin/bash
# tools/play/devmenu/run.sh OUT [SHADOW]: the Stage Select dev menu check (docs/ports/mania/32 "Stage Select dev menu") on
# the disc tools/dev_disc.sh built, in its shadow copy (tools/emu.sh runs the EXE and the disc of the current directory;
# its 8 MB header turns the emulator's 8 MB RAM on). GDB drives pad 1 (g_ps1PadTestMask), prints the state (L lines),
# dumps both framebuffers at the picture frames (OUT_f<frame>_y<y>.png) and the guards.
#   default (`tools/dev_disc.sh BOOT=1,0`, GHZ1): 300 Select (the menu opens over the frozen act), 330 Down + 340 Cross
#     (Mania Mode's scenes), 370 Down + 380 Cross (GHZ2: the load), 700 Select + 720 Circle (opened and closed again in
#     GHZ2); pictures 320 / 360 / 640
#   DEVMENU_RUN=title (`tools/dev_disc.sh`, natural boot): 900 Select at the title opens the menu, 940 Select closes it,
#     and the title is still on (the press never reached it: the title takes any button); pictures 870 (the "dev" build
#     stamp) / 915
#   DEVMENU_RUN=2mb (any dev disc, run with 2 MB: EMU_RAM=2mb): the RAM probe's verdict (g_ps1DevMenuBuild 2, init
#     stopped) and a picture of the red screen (frame() clears it red every frame: GDB stops in psyqo's GPU::clear)
# EMU=ds for DuckStation. No snapshot: the shadow copy is the run's tree.
set -u
R=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=$1
SH=${2:-$(cd "$R/../.." && pwd)/work/shadow/mania-devmenu}
mkdir -p "$(dirname "$OUT")"
OUT=$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")
[ -f "$SH/rsdkv5.ps-exe" ] && [ -f "$SH/build/game.cue" ] || { echo "no dev build in $SH: run tools/dev_disc.sh"; exit 1; }
mipsel-none-elf-nm "$SH/rsdkv5.elf" | grep -q g_ps1DevMenuBuild || { echo "$SH/rsdkv5.elf is not a DEVMENU build"; exit 1; }
rm -f "$OUT"_f*.bin "$OUT"_f*.png

if [ "${DEVMENU_RUN:-ghz}" = title ]; then
  STATE="880 910 950 1000"; SEL="900 940"; DOWN=""; CROSS=""; CIRCLE=""; OFF="903 943"; DUMP="870 915"; LAST=1000
else
  STATE="120 306 350 386 600 706 730 790"; SEL="300 700"; DOWN="330 370"; CROSS="340 380"; CIRCLE="720"
  OFF="303 333 343 373 383 703 723"; DUMP="320 360 640"; LAST=790
fi
cond() { local c= f; for f in $1; do c="$c${c:+ || }g_ps1PerfFrames == $f"; done; echo "${c:-0}"; }
DUMP2=$(for f in $DUMP; do printf '%d ' $((f + 1)); done)
ALL=$(echo $STATE $SEL $DOWN $CROSS $CIRCLE $OFF $DUMP $DUMP2 $LAST | tr ' ' '\n' | sort -nu | tr '\n' ' ')

if [ "${DEVMENU_RUN:-ghz}" = 2mb ]; then
cat > "$OUT.in.gdb" <<G
set \$n = 0
break PS1VramDumpHook
commands
silent
eval "dump binary memory ${OUT}_f%u_y%d.bin g_ps1VramDumpBuf ((char*)g_ps1VramDumpBuf)+153600", \$n, g_ps1VramDumpY
printf "L RAM2MB dev=%u initDone=%u clears=%u\\n", g_ps1DevMenuBuild, g_ps1InitDone, \$n
printf "GUARDS devMenuBuild=%u\\n", g_ps1DevMenuBuild
quit
end
break psyqo::GPU::clear
commands
silent
set \$n = \$n + 1
if \$n == 120
  set var g_ps1VramDumpX = 0
  set var g_ps1VramDumpY = 0
  set var g_ps1VramDumpW = 320
  set var g_ps1VramDumpH = 240
  set var g_ps1VramDumpReq = 1
end
continue
end
continue
G
else
cat > "$OUT.in.gdb" <<G
# pad bits: 1 Down, 0x10 A (Cross), 0x20 B (Circle), 0x800 Select
define ps1_state
  printf "L STATE f=%u state=%u devmenu=%u/%u/%u cat=%d listPos=%d scene=%s ch0=%u/%d dev=%u rects=%u dropped=%u overflow=%u loads=%u xaForced=%u musicResumes=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.state, RSDK::devMenu.listPos, RSDK::devMenu.selection, RSDK::devMenu.scrollPos, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].name, RSDK::channels[0].state, RSDK::channels[0].soundID, g_ps1DevMenuBuild, g_ps1DevTextRects, g_ps1DevTextDropped, g_ps1ChainOverflow, g_ps1LoadingLoads, g_ps1XaForcedStops, g_ps1MusicResumes
end
define ps1_dump
  set var g_ps1VramDumpX = 0
  set var g_ps1VramDumpY = \$arg0
  set var g_ps1VramDumpW = 320
  set var g_ps1VramDumpH = 240
  set var g_ps1VramDumpReq = 1
end
tbreak main
commands
silent
continue
end
break PS1VramDumpHook
commands
silent
eval "dump binary memory ${OUT}_f%u_y%d.bin g_ps1VramDumpBuf ((char*)g_ps1VramDumpBuf)+153600", g_ps1PerfFrames, g_ps1VramDumpY
continue
end
break perfTick if $(cond "$ALL")
commands
silent
if $(cond "$STATE")
  ps1_state
end
if $(cond "$SEL")
  set var g_ps1PadTestMask = 0x800
end
if $(cond "$DOWN")
  set var g_ps1PadTestMask = 0x2
end
if $(cond "$CROSS")
  set var g_ps1PadTestMask = 0x10
end
if $(cond "$CIRCLE")
  set var g_ps1PadTestMask = 0x20
end
if $(cond "$OFF")
  set var g_ps1PadTestMask = 0
end
if $(cond "$DUMP")
  ps1_dump 0
end
if $(cond "$DUMP2")
  ps1_dump 256
end
if g_ps1PerfFrames == $LAST
  printf "GUARDS stageObjectsMissing=%u stubs=%u createSkips=%u relocations=%u spareFail=%u ghostWrites=%u chunkOverflow=%u stackOverflow=%u chainOverflow=%u devTextDropped=%u\n", RSDK::g_ps1StageObjectsMissing, g_ps1UnlinkedCalls, RSDK::g_ps1EntityCreateSkips, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntitySpareFail, RSDK::g_ps1EntityGhostWrites, RSDK::g_ps1ChunkOverflow, g_ps1StackOverflow, g_ps1ChainOverflow, g_ps1DevTextDropped
  quit
end
continue
end
continue
G
fi
(cd "$SH" && TIMEOUT=${TIMEOUT:-900} tools/play_test.sh "$OUT.in.gdb" "$OUT")
/usr/bin/python3 - "$OUT" <<'P'
import glob, sys
from PIL import Image
for f in sorted(glob.glob(sys.argv[1] + '_f*_y*.bin')):
    d = open(f, 'rb').read()
    if len(d) < 153600:
        continue
    im = Image.new('RGB', (320, 240))
    px = im.load()
    for i in range(320 * 240):
        v = d[2 * i] | d[2 * i + 1] << 8
        px[i % 320, i // 320] = ((v & 31) << 3, (v >> 5 & 31) << 3, (v >> 10 & 31) << 3)
    im.save(f[:-4] + '.png')
    print('picture', f[:-4] + '.png')
P
