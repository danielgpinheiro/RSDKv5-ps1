# GHZ1 scripted run with the CPU profile (docs/32 phase 4 preview): build with
# `make MEASURE_RAM=8M BOOT=1,0 PROFILE=1`, run tools/play_test.sh tools/play/ghz1_profile.gdb OUT, then
# tools/play/prof_report.py OUT.log. PROF: hblanks per engine phase over frames 240-900; PC: per objectClassList index,
# update and draw ticks of root counter 2 (8 cycles, summed since boot), calls, the class hash.
# GHZ1 scripted run (docs/32 2.5): `make MEASURE_RAM=8M BOOT=1,0`, then tools/play_test_ds.sh tools/play/ghz1_run.gdb OUT.
# Pad bits (g_ps1PadTestMask, ORed into pad 1): 0 Up 1 Down 2 Left 3 Right 4 A (jump) 5 B 6 C 7 X 8 Y 9 Z 10 Start 11 Select.
# Right from frame 240; jumps at 316 (the ring row at x 324-372) and 412 (the Motobug at x 832). Samples every 15 frames.
set $peakTemp = 0
break perfTick if g_ps1PerfFrames % 15 == 0 || g_ps1PerfFrames == 240 || g_ps1PerfFrames == 316 || g_ps1PerfFrames == 330 || g_ps1PerfFrames == 412 || g_ps1PerfFrames == 426
commands
silent
if g_ps1PerfFrames == 240
  set g_ps1PadTestMask = 0x8
end
if g_ps1PerfFrames == 316 || g_ps1PerfFrames == 412
  set g_ps1PadTestMask = 0x18
end
if g_ps1PerfFrames == 330 || g_ps1PerfFrames == 426
  set g_ps1PadTestMask = 0x8
end
if g_ps1PerfFrames % 15 == 0
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  set $t = 0
  set $s = 0x840
  while $s < 0x940
    if ((RSDK::EntityBase*)RSDK::objectEntityPtr[$s])->classID != 0
      set $t = $t + 1
    end
    set $s = $s + 1
  end
  if $t > $peakTemp
    set $peakTemp = $t
  end
  printf "F %u state=%d x=%d y=%d gv=%d vx=%d vy=%d ground=%d rings=%d time=%u:%02u.%02u hbl=%u temp=%d peakTemp=%d pad=%x\n", g_ps1PerfFrames, RSDK::sceneInfo.state, $p->position.x >> 16, $p->position.y >> 16, $p->groundVel, $p->velocity.x, $p->velocity.y, $p->onGround, $p->rings, RSDK::sceneInfo.minutes, RSDK::sceneInfo.seconds, RSDK::sceneInfo.milliseconds, g_ps1PerfLastHbl, $t, $peakTemp, g_ps1PadTestMask
end
if g_ps1PerfFrames == 240
  set $f0 = g_ps1ProfFrames
  set $p0 = g_ps1ProfPhase[0]
  set $p1 = g_ps1ProfPhase[1]
  set $p2 = g_ps1ProfPhase[2]
  set $p3 = g_ps1ProfPhase[3]
  set $en0 = g_ps1ProfEngine
  set $q0 = g_ps1ProfPart[0]
  set $q1 = g_ps1ProfPart[1]
  set $q2 = g_ps1ProfPart[2]
  set $q3 = g_ps1ProfPart[3]
  set $q4 = g_ps1ProfPart[4]
  set $q5 = g_ps1ProfPart[5]
  set $fa0 = g_ps1ProfForeachAll[0]
  set $fa1 = g_ps1ProfForeachAll[1]
  set $hb0 = g_ps1ProfHbl
end
if g_ps1PerfFrames >= 900
  printf "PROF frames=%u phases input=%u objects=%u parallax=%u drawlists=%u (hblanks, frames 240-900)\n", g_ps1ProfFrames - $f0, g_ps1ProfPhase[0] - $p0, g_ps1ProfPhase[1] - $p1, g_ps1ProfPhase[2] - $p2, g_ps1ProfPhase[3] - $p3
  printf "PARTS static=%u update=%u typegroups=%u lateupdate=%u entitydraws=%u scanlines=%u (hblanks, frames 240-900)\n", g_ps1ProfPart[0] - $q0, g_ps1ProfPart[1] - $q1, g_ps1ProfPart[2] - $q2, g_ps1ProfPart[3] - $q3, g_ps1ProfPart[4] - $q4, g_ps1ProfPart[5] - $q5
  printf "FOREACHALL loops=%u slots=%u (frames 240-900)\n", g_ps1ProfForeachAll[0] - $fa0, g_ps1ProfForeachAll[1] - $fa1
  printf "SOUNDBOARD check=%u update=%u (ticks of 8 cycles, since boot)\n", ps1ProfSoundboard[0], ps1ProfSoundboard[1]
  printf "FRAMES engineHbl=%u frameHbl=%u\n", g_ps1ProfEngine - $en0, g_ps1ProfHbl - $hb0
  set $o = 0
  while $o < RSDK::objectClassCount
    if g_ps1ProfClass[$o][0] + g_ps1ProfClass[$o][1] > 0
      printf "PC %d %u %u %u %08x%08x%08x%08x\n", $o, g_ps1ProfClass[$o][0], g_ps1ProfClass[$o][1], g_ps1ProfCalls[$o], RSDK::objectClassList[$o].hash[0], RSDK::objectClassList[$o].hash[1], RSDK::objectClassList[$o].hash[2], RSDK::objectClassList[$o].hash[3]
    end
    set $o = $o + 1
  end
end
if g_ps1PerfFrames >= 900
  printf "GUARDS stageObjectsMissing=%u stubs=%u createSkips=%u relocations=%u spareFail=%u ghostWrites=%u chunkOverflow=%u chunkPrivate=%u forcedDefrag=%u atl=%d\n", RSDK::g_ps1StageObjectsMissing, g_ps1UnlinkedCalls, RSDK::g_ps1EntityCreateSkips, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntitySpareFail, RSDK::g_ps1EntityGhostWrites, RSDK::g_ps1ChunkOverflow, RSDK::g_ps1ChunkPrivatePeak, g_ps1StorageForcedDefrags[0], ps1AtlDropped
  quit
end
continue
end
continue
