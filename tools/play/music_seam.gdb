# docs/ports/mania/63 M8: the loop seam. GHZ1's track starts 12 rows before its end (row 1038 of 1050), so the loop
# arrives ~38 frames later. Stops go through ps1TestStop (g_ps1TestStops), never a per-frame breakpoint condition:
# the CD-volume fade is ~3 frames each way, and a condition on perfTick runs the emulator at a few fps. S lines: the
# gain (0x7FFF = full), the seam state (1 fade out, 2 hold through the drive's refill, 3 fade in), the drive's
# Setloc+ReadS gap in hblanks.
break RSDK::PS1StreamStart
commands
silent
set var streamStartPos = 1038 * 2352
set var g_ps1TestStops[0] = g_ps1PerfFrames + 36
set var g_ps1TestStops[1] = g_ps1PerfFrames + 38
set var g_ps1TestStops[2] = g_ps1PerfFrames + 40
set var g_ps1TestStops[3] = g_ps1PerfFrames + 42
set var g_ps1TestStops[4] = g_ps1PerfFrames + 44
set var g_ps1TestStops[5] = g_ps1PerfFrames + 46
set var g_ps1TestStops[6] = g_ps1PerfFrames + 48
set var g_ps1TestStops[7] = g_ps1PerfFrames + 52
continue
end
break ps1TestStop
commands
silent
printf "S f=%u off=%d loops=%u mode=%u gain=%u gapHbl=%u\n", g_ps1PerfFrames, g_ps1PerfFrames - g_ps1XaStartFrame, g_ps1XaLoops, g_ps1XaSeamMode, g_ps1XaSeamGainLast, g_ps1XaLoopGapHbl
if g_ps1PerfFrames >= g_ps1XaStartFrame + 52
  printf "DONE loops=%u gapHbl=%u maxGap=%u gain=%u seamOff=%d\n", g_ps1XaLoops, g_ps1XaLoopGapHbl, g_ps1XaLoopGapMaxHbl, g_ps1XaSeamGainLast, g_ps1XaSeamOff
  quit
end
continue
end
continue
