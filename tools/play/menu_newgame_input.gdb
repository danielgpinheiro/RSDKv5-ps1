# Menu walk into a new game (docs/32 4.2d): tools/play/menu_walk_input.gdb's presses (200 A: the autosave notice, 320 A:
# Mania Mode -> Save Select, the cursor on a blank slot) + 480 A: a new save (Sonic) -> the AIZ intro (~570) ->
# GHZCutscene -> GHZ1 (the cutscenes run without input). `make [MEASURE_RAM=8M] BOOT=0,2`.
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
break RSDK::ProcessInput if g_ps1PerfFrames == 200 || g_ps1PerfFrames == 204 || g_ps1PerfFrames == 320 || g_ps1PerfFrames == 324 || g_ps1PerfFrames == 480 || g_ps1PerfFrames == 484
commands
silent
if g_ps1PerfFrames == 200 || g_ps1PerfFrames == 320 || g_ps1PerfFrames == 480
  set *$ps1pad = 0x10
else
  set *$ps1pad = 0
end
continue
end
