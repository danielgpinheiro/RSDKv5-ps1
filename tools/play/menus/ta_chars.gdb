add-symbol-file build/gen/ovl/Menu/Menu.elf
# docs/ports/mania/63 M15: which Time Attack control the Menu opens (BOOT=0,2). Down then A on the main menu reaches
# Time Attack; T prints both controls' button counts, the selected character button and the sprite-missing counter.
set $lf = 0
break RSDK::LoadSceneFolder
commands
silent
if RSDK::sceneInfo.listPos == 2 && $lf == 0
  set $lf = g_ps1PerfFrames
  set var g_ps1TestStops[0] = $lf + 140
  set var g_ps1TestStops[1] = $lf + 146
  set var g_ps1TestStops[2] = $lf + 160
  set var g_ps1TestStops[3] = $lf + 166
  set var g_ps1TestStops[4] = $lf + 240
  set var g_ps1TestStops[5] = 0
  set var g_ps1TestStops[6] = 0
  set var g_ps1TestStops[7] = 0
end
continue
end
break ps1TestStop
commands
silent
if g_ps1PerfFrames == $lf + 140
  set var g_ps1PadTestMask = 2
end
if g_ps1PerfFrames == $lf + 146
  set var g_ps1PadTestMask = 0
end
if g_ps1PerfFrames == $lf + 160
  set var g_ps1PadTestMask = 16
end
if g_ps1PerfFrames == $lf + 166
  set var g_ps1PadTestMask = 0
end
printf "T f=%u off=%d mask=%u pos=%d five=%d legacy=%d sel=%d missing=%u\n", g_ps1PerfFrames, g_ps1PerfFrames - $lf, g_ps1PadTestMask, RSDK::sceneInfo.listPos, ((ObjectTimeAttackMenu *)TimeAttackMenu)->timeAttackControl->buttonCount, ((ObjectTimeAttackMenu *)TimeAttackMenu)->timeAttackControl_Legacy->buttonCount, ((ObjectTimeAttackMenu *)TimeAttackMenu)->timeAttackControl->buttonID, g_ps1SpriteMissing
if g_ps1PerfFrames == $lf + 240
  quit
end
continue
end
continue
