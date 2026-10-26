#!/bin/bash
# Layer evidence (docs/ports/mania/32 3.3): capture frame FRAME's layer inputs and framebuffer in PCSX-Redux (EMU=ds: DuckStation;
# tools/gfx/layer_reference.gdb: 8 MB measuring link, a BOOT= build, e.g. make MEASURE_RAM=8M BOOT=1,0 && make disc), then
# render the layers on the host and compare every pixel (tools/gfx/layer_reference.py).
# Usage: [RIGHT_FROM=F] [PRE_GDB='gdb lines'] tools/gfx/layer_reference.sh FOLDER SCENE_ID FRAME [OUT_DIR]   (RIGHT_FROM: pad 1 holds right
# from frame F, for a running frame; the scripted run holds it from 240)
set -eu
cd "$(dirname "$0")/../.."
FOLDER=$1; SCENE=$2; FRAME=$3; OUT=${4:-${TMPDIR:-/tmp}/ps1_layer_ref}
rm -rf "$OUT"; mkdir -p "$OUT"
# 9b.6: the capture's breakpoints (each frame's layer begin and layer draws, the frame's end) start disabled and are
# enabled one frame before FRAME by the engine's test stop (g_ps1TestStops, ps1/rsdk5_main.cpp): conditions GDB checks
# every frame ran the emulator at a few fps. RIGHT_FROM holds right through the scripted pad (g_ps1PadScript).
{ printf 'tbreak main\ncommands\nsilent\nset var g_ps1TestStops[3] = %d\n' $((FRAME - 1))
  if [ -n "${RIGHT_FROM:-}" ]; then
    printf 'set var g_ps1PadScript[0] = 0xFFFF\nset var g_ps1PadScript[1] = %s\nset var g_ps1PadScript[2] = 0x8\nset var g_ps1PadScript[3] = 60000\nset var g_ps1PadScriptSteps = 1\nset var g_ps1PadScriptStep = 0\n' "$RIGHT_FROM"
  fi
  printf 'continue\nend\nbreak ps1TestStop\nset $refStop = $bpnum\ncommands\nsilent\neval "enable %%d %%d %%d", $refStop + 1, $refStop + 2, $refStop + 3\ncontinue\nend\n'
  sed "s|REF_DIR|$OUT|g; s|REF_FRAME|$FRAME|g; \$d" tools/gfx/layer_reference.gdb
  if [ -n "${PRE_GDB:-}" ]; then printf '%b\n' "$PRE_GDB"; fi
  printf 'eval "disable %%d %%d %%d", $refStop + 1, $refStop + 2, $refStop + 3\ncontinue\n'; } > "$OUT/capture.gdb"
TIMEOUT=${TIMEOUT:-200} tools/play_test.sh "$OUT/capture.gdb" "$OUT/run" > /dev/null 2>&1 || true
grep -E "^(LAYER|FRAME)" "$OUT/run.log" || true
/usr/bin/python3 tools/gfx/layer_reference.py "$OUT" "$FOLDER" "$SCENE" --png "$OUT/cmp"
