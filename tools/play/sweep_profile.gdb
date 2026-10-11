# docs/32 9q: sweep_timing.gdb with a per-class CPU profile at every point (`make PROFILE=1 BOOT=1,s [CAP=1]`): "K" lines
# (P0 = the class counters at the window's start, PC = at its end, per point k) for tools/play/sweep_profile.py.
# Frame timing along a whole act (docs/32 9f): from frame 300, every 150 frames player 1 (invulnerable, no death
# boundary) is warped to the next point x = $swX0 + k * $swStep px, y = $swY (Zone.c's test warp, mode 2: player and
# camera, the layer's whole bounds; the class is set only until the timing starts: Zone_PS1TestBoss scans every slot
# each frame), drops onto the terrain, and frames 90-149 after the warp are timed:
# "S x=... vsyncs x100 slow worstV cpu max gpuWait flip sprites". `make BOOT=c,s`, make disc,
# [EMU=ds] tools/play_test.sh <a copy with $swX0 / $swStep / $swEnd / $swY set first> OUT.
set $swK = 0
set $swActive = 0
break perfTick if g_ps1PerfFrames >= 300 && (g_ps1PerfFrames % 150 == 0 || g_ps1PerfFrames % 150 == 90)
commands
silent
set $swP = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
set var $swP->blinkTimer = 240
set var $swP->rings = 50
if g_ps1PerfFrames % 150 == 90
  set var g_ps1TestBossClass = 0
  set $winVs = g_ps1PerfVsyncs
  set $winIw = g_ps1ChainIdleWaitHbl
  set $winF = g_ps1PerfFrames
  set var g_ps1PerfSumHbl = 0
  set var g_ps1PerfSumFlip = 0
  set var g_ps1PerfSlowFrames = 0
  set var g_ps1PerfWorstVsyncs = 0
  set var g_ps1PerfMaxHbl = 0
  set $swSpr = 0
  set $swActive = 1
  set $o = 0
  while $o < RSDK::objectClassCount
    if g_ps1ProfClass[$o][0] + g_ps1ProfClass[$o][1] > 0
      printf "K0 %d %d %u %u %u\n", $swK, $o, g_ps1ProfClass[$o][0], g_ps1ProfClass[$o][1], g_ps1ProfCalls[$o]
    end
    set $o = $o + 1
  end
  printf "KP0 %d %u %u %u %u %u\n", $swK, g_ps1ProfPart[0], g_ps1ProfPart[1], g_ps1ProfPart[2], g_ps1ProfPart[3], g_ps1ProfPart[4]
else
  if $swActive
    set $winN = g_ps1PerfFrames - $winF
    set $o = 0
    while $o < RSDK::objectClassCount
      if g_ps1ProfClass[$o][0] + g_ps1ProfClass[$o][1] > 0
        printf "K1 %d %d %u %u %u %08x%08x%08x%08x\n", $swK, $o, g_ps1ProfClass[$o][0], g_ps1ProfClass[$o][1], g_ps1ProfCalls[$o], RSDK::objectClassList[$o].hash[0], RSDK::objectClassList[$o].hash[1], RSDK::objectClassList[$o].hash[2], RSDK::objectClassList[$o].hash[3]
      end
      set $o = $o + 1
    end
    printf "KP1 %d %u %u %u %u %u\n", $swK, g_ps1ProfPart[0], g_ps1ProfPart[1], g_ps1ProfPart[2], g_ps1ProfPart[3], g_ps1ProfPart[4]
    printf "S x=%d px=%d py=%d vsyncs x100=%u slow=%u worstV=%u cpu=%u max=%u gpuWait=%u flip=%u sprites=%u\n", $swX0 + ($swK - 1) * $swStep, $swP->position.x >> 16, $swP->position.y >> 16, (g_ps1PerfVsyncs - $winVs) * 100 / $winN, g_ps1PerfSlowFrames, g_ps1PerfWorstVsyncs, g_ps1PerfSumHbl / $winN, g_ps1PerfMaxHbl, (g_ps1ChainIdleWaitHbl - $winIw) / $winN, g_ps1PerfSumFlip / $winN, *(unsigned *)&g_ps1SpriteDraws
  end
  if $swX0 + $swK * $swStep > $swEnd
    quit
  end
  set var g_ps1TestBossDX = $swX0 + $swK * $swStep
  set var g_ps1TestBossDY = $swY
  set var g_ps1TestBossWarp = 2
  set var g_ps1TestBossClass = 0xFFF0
  set $swK = $swK + 1
end
continue
end
continue
