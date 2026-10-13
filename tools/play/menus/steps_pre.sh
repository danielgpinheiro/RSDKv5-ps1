#!/bin/bash
# docs/32 phase 13b: steps_pre.sh STEPS > PRE.gdb: a menus/*.steps file as GDB lines for tools/play/vshot.sh's PRE (the
# scripted pad set at the first scene load, after the EXE's start-up cleared BSS; ps1_onstop prints the Video options)
echo 'set $script = 0'
echo 'break RSDK::LoadSceneFolder'
echo 'commands'
echo 'silent'
echo 'if !$script'
awk 'BEGIN{k=0} !/^#/ && NF>=4 {printf "  set var g_ps1PadScript[%d] = %s\n  set var g_ps1PadScript[%d] = %s\n  set var g_ps1PadScript[%d] = %s\n  set var g_ps1PadScript[%d] = %s\n", 4*k, $1, 4*k+1, $2, 4*k+2, $3, 4*k+3, $4; k++} END{printf "  set var g_ps1PadScriptSteps = %d\n  set var g_ps1PadScriptStep = 0\n", k}' $1
echo '  set $script = 1'
echo 'end'
echo 'printf "L SCENE f=%u pos=%d\n", *(unsigned *)&g_ps1PerfFrames, RSDK::sceneInfo.listPos'
echo 'continue'
echo 'end'
echo 'define ps1_onstop'
echo '  printf "L OPT f=%u fullResOff=%u frameskip=%d extras=%u vram2MB=%u cardSaves=%u\n", *(unsigned *)&g_ps1PerfFrames, *(unsigned *)&g_ps1FullResOff, *(int *)&g_ps1Frameskip, *(unsigned *)&g_ps1Extras, *(unsigned *)&g_ps1Vram2MB, *(unsigned *)&g_ps1CardSaves'
echo 'end'
