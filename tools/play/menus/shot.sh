#!/bin/bash
# 9b.5 (docs/32): shot.sh NAME STEPS FRAMES [CARD]: tools/play/shot.sh's pictures (8 MB natural build, both framebuffers
# at each frame of FRAMES) with the scripted pad of STEPS (as run.sh), on CARD (EMU_MC1, kept) if given; PRE_EXTRA = a
# file of more GDB lines (@R@ = the build snapshot).
H=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$H/../../.." && pwd)
name=$1 steps=$2 frames=$3 card=${4:-}
pre=$R/build/shots/$name.pre.gdb; mkdir -p $R/build/shots
{
echo 'define ps1_onload'
echo 'end'
[ -n "${PRE_EXTRA:-}" ] && sed "s|@R@|$R/build/boss/snaps/shot_$name|g" $PRE_EXTRA
echo 'set $script = 0'
echo 'set $lf = 0'
echo 'break RSDK::LoadSceneFolder'
echo 'commands'
echo 'silent'
echo 'if !$script'
awk 'BEGIN{k=0} !/^#/ && NF>=4 {printf "  set var g_ps1PadScript[%d] = %s\n  set var g_ps1PadScript[%d] = %s\n  set var g_ps1PadScript[%d] = %s\n  set var g_ps1PadScript[%d] = %s\n", 4*k, $1, 4*k+1, $2, 4*k+2, $3, 4*k+3, $4; k++} END{printf "  set var g_ps1PadScriptSteps = %d\n  set var g_ps1PadScriptStep = 0\n", k}' $steps
echo '  set $script = 1'
echo 'end'
echo 'set $lf = g_ps1PerfFrames'
echo 'printf "L SCENE f=%u pos=%d step=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.listPos, g_ps1PadScriptStep'
echo 'ps1_onload'
echo 'continue'
echo 'end'
} > $pre
[ -n "$card" ] && export EMU_MC1=$card
$R/tools/play/shot.sh $name '' $frames $pre
