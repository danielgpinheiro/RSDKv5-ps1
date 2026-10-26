#!/bin/bash
# Load-time profile (docs/ports/mania/32 phase 9.1; Sonic 2's tool, docs/ports/sonic-2/30 9.1): one emulator session over GDB logs
#   REQ   every file-system CD request (PS1FsProfileHook: a blocking read, or one load-stream session = one seek)
#   OPEN  every fopen, LOAD every outermost load end (PS1LoadingDoneHook), GOTO every scene switch below,
# all stamped with the vblank count, then tools/load_profile.py prints a table per load: vblanks, requests, stream
# starts, sectors, heap headroom, and where the time went (files by CD time, fopens by the vblanks until the next one).
# SCHEDULE="frame:category:listPos ...": at that engine frame set the scene (listPos is the GameConfig list's absolute
# index: Mania mode GHZ1 = 9 ... ERZ = 37, Menu = 2) and start a load (sceneInfo.state = ENGINESTATE_LOAD). Default: the
# phase 9 baseline on a BOOT=1,0 build (GHZ2, a restart, every zone's first act, ERZ, the Menu, back to GHZ1).
# PRE="gdb commands": before the game runs (an A/B switch). Runs through tools/play_test.sh (its own card images).
# Usage: make BOOT=1,0 && make disc && tools/load_profile.sh [OUT_PREFIX] [STOP_FRAME]
cd "$(dirname "$0")/.."
OUT=${1:-${TMPDIR:-/tmp}/rsdkv5_load}
DEFAULT="60:1:10 120:1:10 180:1:11 240:1:13 300:1:15 360:1:17 420:1:19 480:1:22 540:1:24 600:1:27 660:1:29 720:1:32 780:1:34 840:1:37 900:0:2 960:1:9"
SCHEDULE=${SCHEDULE:-$DEFAULT}
LAST=$(echo $SCHEDULE | awk '{print $NF}' | cut -d: -f1)
STOP=${2:-$((LAST + 60))}
{
cat <<'GDBEOF'
set pagination off
set confirm off
file rsdkv5.elf
set architecture mips:3000
target remote localhost:3333
@PRE@
break PS1FsProfileHook
commands
silent
printf "REQ lba=%u n=%u us=%u vbl=%u\n", lba, count, us, g_ps1PerfVsyncs
continue
end
break fopen
commands
silent
printf "OPEN vbl=%u %s\n", g_ps1PerfVsyncs, path
continue
end
break PS1LoadingDoneHook
commands
silent
printf "LOAD vbl=%u us=%u frame=%u list=%d pos=%d folder=%s requests=%u sectors=%u streams=%u headroom=%u\n", g_ps1PerfVsyncs, us, g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].folder, g_ps1FsRequests, g_ps1FsSectors, g_ps1FsStreamStarts, g_ps1LoadingHeadroom
continue
end
GDBEOF
for s in $SCHEDULE; do
  F=${s%%:*}; r=${s#*:}; C=${r%%:*}; P=${r#*:}
  cat <<GDBEOF
break perfTick if g_ps1PerfFrames == $F
commands
silent
set var RSDK::sceneInfo.activeCategory = $C
set var RSDK::sceneInfo.listPos = $P
set var RSDK::sceneInfo.state = 0
printf "GOTO vbl=%u frame=%u list=$C pos=$P requests=%u streams=%u\n", g_ps1PerfVsyncs, g_ps1PerfFrames, g_ps1FsRequests, g_ps1FsStreamStarts
continue
end
GDBEOF
done
cat <<GDBEOF
break perfTick if g_ps1PerfFrames == $STOP
commands
silent
printf "DONE vbl=%u frame=%u requests=%u sectors=%u streams=%u localHits=%u sfxBanks=%u sfxKept=%u sfxErrors=%u\n", g_ps1PerfVsyncs, g_ps1PerfFrames, g_ps1FsRequests, g_ps1FsSectors, g_ps1FsStreamStarts, g_ps1FsLocalHits, g_ps1SfxBanks, g_ps1SfxBanksKept, g_ps1SfxBankErrors
quit
end
continue
GDBEOF
} | sed "s|@PRE@|${PRE:-}|" > "$OUT.in.gdb"
TIMEOUT=${TIMEOUT:-3000} tools/play_test.sh "$OUT.in.gdb" "$OUT" > /dev/null 2>&1
grep -aE "^(REQ|OPEN|LOAD|GOTO|DONE) " "$OUT.log" > "$OUT.prof"
/usr/bin/python3 tools/load_profile.py "$OUT.prof" build/game.bin | tee "$OUT.txt"
