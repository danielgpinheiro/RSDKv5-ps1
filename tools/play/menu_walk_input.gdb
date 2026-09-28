# Menu walk input (docs/32 4.2): `make [MEASURE_RAM=8M] BOOT=0,2`. Pad bits as tools/play/ghz1_run.gdb (4 = A, the menu's
# confirm). The presses go through RSDK::ProcessInput (once per frame), so this file combines with scripts that use perfTick
# (tools/gfx/capture_manifests.sh: MANIFEST_INPUT=tools/play/menu_walk_input.gdb).
#   200 A: the first-boot autosave notice (DialogRunner_NotifyAutoSave's dialog)   320 / 480 A: the notices after it
#   640 A: Main Menu -> Mania Mode (the Save Select at ~690, the cursor on the last slot)   (800 A would start a new save:
#   the AIZ intro, not in this walk)
break RSDK::ProcessInput if g_ps1PerfFrames == 200 || g_ps1PerfFrames == 204 || g_ps1PerfFrames == 320 || g_ps1PerfFrames == 324 || g_ps1PerfFrames == 480 || g_ps1PerfFrames == 484 || g_ps1PerfFrames == 640 || g_ps1PerfFrames == 644
commands
silent
if g_ps1PerfFrames % 4 == 0 && (g_ps1PerfFrames == 200 || g_ps1PerfFrames == 320 || g_ps1PerfFrames == 480 || g_ps1PerfFrames == 640)
  set g_ps1PadTestMask = 0x10
else
  set g_ps1PadTestMask = 0
end
continue
end
