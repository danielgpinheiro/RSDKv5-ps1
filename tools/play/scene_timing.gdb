# Frame timing of a scene left idle or a scripted run window (docs/32 9f), in either emulator: `make BOOT=c,s` (2 MB),
# make disc, then [EMU=ds] tools/play_test.sh tools/play/scene_timing.gdb OUT. Two stops only (frames 300 and 600 by default
# 300 and 600): over the window, frames, vsyncs (100 = 60 fps), frames slower than one
# vblank, the worst frame's vblanks, CPU hblanks per frame (average / max: inside frame(), the GPU-arena wait included),
# the GPU-arena wait (G) and the flip gap (F: the GPU finishing, then vblank) per frame; phase 13b: the game speed (logic
# steps a vblank x100: 100 = full speed).
set $winA = 300
set $winB = 600
break perfTick if g_ps1PerfFrames == 300 || g_ps1PerfFrames == 600
commands
silent
if g_ps1PerfFrames == $winA
  set $winF = g_ps1PerfFrames
  set $winVs = g_ps1PerfVsyncs
  set $winSt = *(unsigned *)&g_ps1StepsTotal
  set $winIw = g_ps1ChainIdleWaitHbl
  set var g_ps1PerfSumHbl = 0
  set var g_ps1PerfSumFlip = 0
  set var g_ps1PerfSlowFrames = 0
  set var g_ps1PerfWorstVsyncs = 0
  set var g_ps1PerfMaxHbl = 0
  set var g_ps1PerfFlipMax = 0
end
if g_ps1PerfFrames >= $winB
  set $n = g_ps1PerfFrames - $winF
  printf "T frames=%u vsyncs/frame x100=%u slow=%u worstV=%u cpu=%u max=%u gpuWait=%u flip=%u flipMax=%u scene=%d speed x100=%u\n", $n, (g_ps1PerfVsyncs - $winVs) * 100 / $n, g_ps1PerfSlowFrames, g_ps1PerfWorstVsyncs, g_ps1PerfSumHbl / $n, g_ps1PerfMaxHbl, (g_ps1ChainIdleWaitHbl - $winIw) / $n, g_ps1PerfSumFlip / $n, g_ps1PerfFlipMax, RSDK::sceneInfo.listPos, (*(unsigned *)&g_ps1StepsTotal - $winSt) * 100 / (g_ps1PerfVsyncs - $winVs)
  quit
end
continue
end
continue
