# GHZ1's scripted run (tools/play/ghz1_run.gdb's pad: right from 240, jumps at 316 and 412) with the frame timing of
# frames 300-900 (docs/32 9f; tools/play/scene_timing.gdb's counters). `make BOOT=1,0`, make disc, then
# [EMU=ds] tools/play_test.sh tools/play/ghz1_timing.gdb OUT.
break perfTick if g_ps1PerfFrames == 240 || g_ps1PerfFrames == 316 || g_ps1PerfFrames == 330 || g_ps1PerfFrames == 412 || g_ps1PerfFrames == 426 || g_ps1PerfFrames == 300 || g_ps1PerfFrames == 900
commands
silent
if g_ps1PerfFrames == 240 || g_ps1PerfFrames == 330 || g_ps1PerfFrames == 426
  set var g_ps1PadTestMask = 0x8
end
if g_ps1PerfFrames == 316 || g_ps1PerfFrames == 412
  set var g_ps1PadTestMask = 0x18
end
if g_ps1PerfFrames == 300
  set $winF = g_ps1PerfFrames
  set $winVs = g_ps1PerfVsyncs
  set $winIw = g_ps1ChainIdleWaitHbl
  set var g_ps1PerfSumHbl = 0
  set var g_ps1PerfSumFlip = 0
  set var g_ps1PerfSlowFrames = 0
  set var g_ps1PerfWorstVsyncs = 0
  set var g_ps1PerfMaxHbl = 0
  set var g_ps1PerfFlipMax = 0
end
if g_ps1PerfFrames >= 900
  set $winN = g_ps1PerfFrames - $winF
  printf "T frames=%u vsyncs/frame x100=%u slow=%u worstV=%u cpu=%u max=%u gpuWait=%u flip=%u flipMax=%u scene=%d\n", $winN, (g_ps1PerfVsyncs - $winVs) * 100 / $winN, g_ps1PerfSlowFrames, g_ps1PerfWorstVsyncs, g_ps1PerfSumHbl / $winN, g_ps1PerfMaxHbl, (g_ps1ChainIdleWaitHbl - $winIw) / $winN, g_ps1PerfSumFlip / $winN, g_ps1PerfFlipMax, RSDK::sceneInfo.listPos
  printf "P x=%d rings=%d\n", ((struct EntityPlayer *)RSDK::objectEntityPtr[0])->position.x >> 16, ((struct EntityPlayer *)RSDK::objectEntityPtr[0])->rings
  quit
end
continue
end
continue
