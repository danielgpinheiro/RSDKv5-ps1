#!/bin/bash
# Scripted play (docs/32 2.5): a GDB script drives pad 1 through g_ps1PadTestMask at chosen frames (break on perfTick)
# and prints the player, rings, timer, logic hblanks and live temp slots; the guards at the end.
# PCSX-Redux by default, EMU=ds for DuckStation (tools/emu.sh; both shared with other sessions).
# Usage: tools/play_test.sh SCRIPT.gdb [OUT_PREFIX]   (TIMEOUT=seconds, default 500). The EXE must be a BOOT= build.
set -u
cd "$(dirname "$0")/.."
SCRIPT="$1"
OUT="${2:-/tmp/ps1_play}"
RUN="$OUT.run.gdb" # never the input script: cat into itself grows without end
[ "$(cd "$(dirname "$SCRIPT")" && pwd)/$(basename "$SCRIPT")" = "$(cd "$(dirname "$RUN")" && pwd)/$(basename "$RUN")" ] && { echo "OUT would overwrite SCRIPT"; exit 1; }
. tools/emu.sh
{ emu_gdb_header; cat "$SCRIPT"; } > "$RUN"
emu_start "$OUT"
timeout -s INT ${TIMEOUT:-500} /opt/homebrew/bin/gdb --batch -x "$RUN" > "$OUT.log" 2>&1
emu_stop
grep -E "^(F |L |GUARDS|DORMANT|SAMPLES|CLUT )" "$OUT.log" | tee "$OUT.txt"
echo "(log: $OUT.log)"
