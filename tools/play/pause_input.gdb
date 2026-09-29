# Pause in GHZ1 (docs/32 4.3): `make [MEASURE_RAM=8M] BOOT=1,0`. Start (pad bit 10) held on frames 300-303 opens the
# PauseMenu; the presses go through RSDK::ProcessInput (once per frame), so this file combines with scripts that use
# perfTick (tools/gfx/layer_reference.sh: PRE_GDB="$(cat tools/play/pause_input.gdb)").
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
break RSDK::ProcessInput if g_ps1PerfFrames == 300 || g_ps1PerfFrames == 304
commands
silent
if g_ps1PerfFrames == 300
  set *$ps1pad = 0x400
else
  set *$ps1pad = 0
end
continue
end
