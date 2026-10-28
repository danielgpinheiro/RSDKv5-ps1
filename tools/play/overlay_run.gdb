# The debug overlay's values (docs/ports/mania/32 "Debug overlay"): a `make DEBUG_OVERLAY=1 BOOT=c,s` build, make disc,
# EMU_LOG=OUT.emu.log tools/play_test.sh tools/play/overlay_run.gdb OUT (the [PERF] lines land in OUT.emu.log on
# PCSX-Redux; these F lines come through GDB on both emulators). Stops at frames 600, 900 and 1200 (g_ps1TestStops: no
# per-frame breakpoint). Options, set before this script (a wrapper that sources it): $cap = 1 starts capped
# (g_ps1Uncapped 0: 30 fps, two logic steps; BOOT= builds start uncapped); $hide = 1 hides the overlay from frame 900
# (the on / off A/B: frames 600-900 against 900-1200 of the same run).
define ps1_ovl
  printf "F %u fps=%u/%u logic=%u prims=%u bad=%u hbl=%u max=%u rebuild=%u log=%u vis=%u cpu=%u flip=%u\n", *(unsigned *)&::g_ps1PerfFrames, *(unsigned *)&::g_ps1DbgFps, *(unsigned *)&::g_ps1DbgTarget, *(unsigned *)&::g_ps1DbgLogic, *(unsigned *)&::g_ps1DbgPrims, *(unsigned *)&::g_ps1DbgBadPrims, *(unsigned *)&::g_ps1DbgHbl, *(unsigned *)&::g_ps1DbgHblMax, *(unsigned *)&::g_ps1DbgHblRebuild, *(unsigned *)&::g_ps1DbgHblLog, *(unsigned *)&::g_ps1DbgVisible, *(unsigned *)&::g_ps1PerfLastHbl, *(unsigned *)&::g_ps1PerfFlipHbl
end
tbreak main
commands
silent
set var g_ps1TestStops[0] = 600
set var g_ps1TestStops[1] = 900
set var g_ps1TestStops[2] = 1200
if !$_isvoid($cap)
  set var g_ps1Uncapped = 0
end
continue
end
break ps1TestStop
commands
silent
ps1_ovl
if !$_isvoid($hide) && *(unsigned *)&::g_ps1PerfFrames == 900
  set var g_ps1DbgVisible = 0
end
if *(unsigned *)&::g_ps1PerfFrames >= 1200
  printf "GUARDS stack=%u chainOverflow=%u\n", *(unsigned *)&::g_ps1StackOverflow, *(unsigned *)&::g_ps1ChainOverflow
  quit
end
continue
end
continue
