# Menu walk input (docs/32 4.2): `make [MEASURE_RAM=8M] BOOT=0,2`. Pad bits as tools/play/ghz1_run.gdb (4 = A, the menu's
# confirm). The presses go through RSDK::ProcessInput (once per frame), so this file combines with scripts that use perfTick
# (tools/gfx/capture_manifests.sh: MANIFEST_INPUT=...; tools/gfx/layer_reference.sh: PRE_GDB="$(cat ...)").
#   200 A: closes the first-boot autosave notice (DialogRunner_NotifyAutoSave; the Main Menu from ~270)
#   320 A: Main Menu -> Mania Mode (the Save Select from ~360, the cursor on the last slot)
#   (an A at 480 would start a new save there: the AIZ intro, ~570)
# the address taken here, at global scope: inside RSDK::ProcessInput GDB looks for RSDK::g_ps1PadTestMask (the header
# declares it inside the namespace) and stops the script
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
break RSDK::ProcessInput if g_ps1PerfFrames == 200 || g_ps1PerfFrames == 204 || g_ps1PerfFrames == 320 || g_ps1PerfFrames == 324
commands
silent
if g_ps1PerfFrames == 200 || g_ps1PerfFrames == 320
  set *$ps1pad = 0x10
else
  set *$ps1pad = 0
end
continue
end
