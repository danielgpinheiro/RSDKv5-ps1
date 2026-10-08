#!/bin/bash
# docs/32 9f (UFO 60 fps plan): a UFO stage's worst moments, timed in PCSX-Redux and DuckStation. The scripted pad steers
# a chase (left / right turns of 50-90 frames, jumps) so the course's busy parts (the UFO, spheres, rings, shadows,
# decorations) pass on screen, from frame FROM to TO (tools/play/scene_timing.gdb's window: worstV = the slowest frame,
# min fps = 60 / worstV). Usage: tools/play/ufo_min.sh N [FROM TO] (N = 1-7, BOOT=3,N-1); EMUS / NOBUILD as perf_scene.sh.
# Output: build/perf9f/ufoN_min_<emu>.txt (the T line).
set -u
cd "$(dirname "$0")/../.."
N=$1; FROM=${2:-180}; TO=${3:-1500}
sets=""; k=0; f=60
lens=(70 55 90 50 80 65 60 85 75 50 90 70)
while [ $k -lt 44 ] && [ $f -lt $TO ]; do
  len=${lens[$(( (k / 2) % 12 ))]}
  mask=$(( k % 2 == 0 ? 0x4 : 0x8 ))
  [ $(( k % 3 )) = 2 ] && mask=$(( mask | 0x10 ))   # a jump with every third turn
  sets="${sets}set var g_ps1PadScript[$((4*k))] = 0xFFFF\nset var g_ps1PadScript[$((4*k+1))] = $f\nset var g_ps1PadScript[$((4*k+2))] = $mask\nset var g_ps1PadScript[$((4*k+3))] = $len\n"
  f=$((f + len + 4)); k=$((k + 1))
done
sets="${sets}set var g_ps1PadScriptSteps = $k\nset var g_ps1PadScriptStep = 0\n${EXTRA_SETS:-}"
STOP_AT_LOAD=1 SETS="$sets" tools/play/perf_scene.sh ufo${N}_min 3,$((N - 1)) $FROM $TO
