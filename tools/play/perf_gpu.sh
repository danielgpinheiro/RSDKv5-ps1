#!/bin/bash
# docs/ports/mania/32 9f: the GPU cost of one frame's chain (DuckStation's model, tools/play/gpu_cost.py). Usage:
# tools/play/perf_gpu.sh NAME BOOT FRAME [RIGHT_FROM]   (PCSX-Redux by default: the chain is the same in both; EMU=ds
# works too). NOBUILD=1 reuses the current build. Output: build/perf9f/gpu_NAME.txt (its SUM line: GPU hblanks, pixels,
# overdraw). WARPX= / WARPY= (9u.8): player 1 warped there at frame 300 (FRAME after 330). 9t: gpu_cost.py walks to psyqo's chain tail (m_chainTail / m_chainTailCount, m_chainHeadOversized) and clips
# to the 320 x 240 screen, as the RSDKv3 / v4 copies (docs/perf/43). SETS=: GDB lines run at main (a pad script).
set -u
cd "$(dirname "$0")/../.."
NAME=$1; BOOT=$2; FRAME=$3; RIGHT=${4:-}
OUT=build/perf9f; mkdir -p $OUT
if [ "${NOBUILD:-0}" != 1 ]; then
  make -j8 BOOT=$BOOT > $OUT/b_gpu_$NAME.log 2>&1 && make disc > $OUT/d_gpu_$NAME.log 2>&1 || { echo "BUILD FAIL $NAME"; exit 1; }
fi
{ printf 'tbreak main\ncommands\nsilent\nset var g_ps1TestStops[0] = %d\n' "$FRAME"
  [ -n "${SETS:-}" ] && printf '%b' "$SETS"   # GDB lines at main (e.g. tools/play/ufo_min.sh's chase pad script)
  if [ -n "$RIGHT" ]; then
    printf 'set var g_ps1PadScript[0] = 0xFFFF\nset var g_ps1PadScript[1] = %s\nset var g_ps1PadScript[2] = 0x8\nset var g_ps1PadScript[3] = 60000\nset var g_ps1PadScriptSteps = 1\nset var g_ps1PadScriptStep = 0\n' "$RIGHT"
  fi
  printf 'continue\nend\n'
  if [ -n "${WARPX:-}" ]; then # 9u.8: player 1 (invulnerable) warped to WARPX, WARPY at frame 300 (sample_at.gdb's warp)
    printf 'break perfTick if g_ps1PerfFrames == 300 || g_ps1PerfFrames == 330\ncommands\nsilent\n'
    printf 'set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]\nset var $p->blinkTimer = 240\n'
    printf 'if g_ps1PerfFrames == 300\nset var g_ps1TestBossDX = %d\nset var g_ps1TestBossDY = %d\n' "$WARPX" "${WARPY:-64}"
    printf 'set var g_ps1TestBossWarp = 2\nset var g_ps1TestBossClass = 0xFFF0\nend\n'
    printf 'if g_ps1PerfFrames == 330\nset var g_ps1TestBossClass = 0\nend\ncontinue\nend\n'
  fi
  printf 'break ps1TestStop\ncommands\nsilent\n'
  printf 'printf "HEAD %%08x %%08x %%08x %%d frame %%u\\n", (unsigned)g_ps1GPU->m_chainHead, (unsigned)g_ps1GPU->m_chainTail, (unsigned)g_ps1GPU->m_chainTailCount, (int)g_ps1GPU->m_chainHeadOversized, g_ps1PerfFrames\n'
  printf 'dump binary memory %s/gpu_%s.ram 0x80000000 0x80200000\nquit\nend\ncontinue\n' "$OUT" "$NAME"
} > $OUT/gpu_$NAME.gdb
TIMEOUT=${TIMEOUT:-300} tools/play_test.sh $OUT/gpu_$NAME.gdb $OUT/gpu_${NAME}_run > /dev/null 2>&1
h=$(grep "^HEAD " $OUT/gpu_${NAME}_run.log | head -1 | awk '{print "0x" $2, "0x" $3, "0x" $4, $5}')
[ -n "$h" ] || { echo "NO HEAD $NAME"; exit 1; }
/usr/bin/python3 tools/play/gpu_cost.py $OUT/gpu_$NAME.ram $h --top 12 | tee $OUT/gpu_$NAME.txt
