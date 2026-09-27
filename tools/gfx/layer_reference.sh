#!/bin/bash
# Layer evidence (docs/32 3.3): capture frame FRAME's layer inputs and framebuffer in PCSX-Redux (EMU=ds: DuckStation;
# tools/gfx/layer_reference.gdb: 8 MB measuring link, a BOOT= build, e.g. make MEASURE_RAM=8M BOOT=1,0 && make disc), then
# render the layers on the host and compare every pixel (tools/gfx/layer_reference.py).
# Usage: [RIGHT_FROM=F] tools/gfx/layer_reference.sh FOLDER SCENE_ID FRAME [OUT_DIR]   (RIGHT_FROM: pad 1 holds right
# from frame F, for a running frame; the scripted run holds it from 240)
set -eu
cd "$(dirname "$0")/../.."
FOLDER=$1; SCENE=$2; FRAME=$3; OUT=${4:-${TMPDIR:-/tmp}/ps1_layer_ref}
rm -rf "$OUT"; mkdir -p "$OUT"
{ if [ -n "${RIGHT_FROM:-}" ]; then
    printf 'break perfTick if g_ps1PerfFrames == %s\ncommands\nsilent\nset g_ps1PadTestMask = 0x8\ncontinue\nend\n' "$RIGHT_FROM"
  fi
  sed "s|REF_DIR|$OUT|g; s|REF_FRAME|$FRAME|g" tools/gfx/layer_reference.gdb; } > "$OUT/capture.gdb"
TIMEOUT=${TIMEOUT:-200} tools/play_test.sh "$OUT/capture.gdb" "$OUT/run" > /dev/null 2>&1 || true
grep -E "^(LAYER|FRAME)" "$OUT/run.log" || true
/usr/bin/python3 tools/gfx/layer_reference.py "$OUT" "$FOLDER" "$SCENE" --png "$OUT/cmp"
