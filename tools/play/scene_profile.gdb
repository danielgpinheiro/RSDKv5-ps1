# CPU profile of a scene left idle (docs/32 9e Title / D.A. Garden speed): `make PROFILE=1 BOOT=c,s` (2 MB), make disc,
# tools/play_test.sh tools/play/scene_profile.gdb OUT, then tools/play/prof_report.py OUT.log 300. Frames 300-600, no
# input. F lines every 10 frames: the frame's hblanks, layer hblanks (all, rotozoom, per layer index), rotozoom quads,
# sprites; PROF / PARTS / FRAMES over the 300 frames; PC: per class (update, draw ticks since boot).
break perfTick if g_ps1PerfFrames % 10 == 0 && g_ps1PerfFrames >= 300
commands
silent
printf "F %u hbl=%u layers=%u roto=%u by=%u,%u,%u,%u,%u quads=%u sprites=%u chainWords=%u\n", g_ps1PerfFrames, g_ps1PerfLastHbl, RSDK::g_ps1LayerHbl, g_ps1RotoHbl, g_ps1LayerHblBy[0], g_ps1LayerHblBy[1], g_ps1LayerHblBy[2], g_ps1LayerHblBy[3], g_ps1LayerHblBy[4], g_ps1RotoQuads, *(unsigned *)&g_ps1SpriteDraws, g_ps1ChainLastWords
if g_ps1PerfFrames == 300
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
  set $hb0 = g_ps1ProfHbl
  set $o = 0
  while $o < RSDK::objectClassCount
    if g_ps1ProfClass[$o][0] + g_ps1ProfClass[$o][1] > 0
      printf "P0 %d %u %u %u\n", $o, g_ps1ProfClass[$o][0], g_ps1ProfClass[$o][1], g_ps1ProfCalls[$o]
    end
    set $o = $o + 1
  end
end
if g_ps1PerfFrames >= 600
  printf "PROF frames=%u phases input=%u objects=%u parallax=%u drawlists=%u (hblanks, frames 300-600)\n", g_ps1ProfFrames - $f0, g_ps1ProfPhase[0] - $p0, g_ps1ProfPhase[1] - $p1, g_ps1ProfPhase[2] - $p2, g_ps1ProfPhase[3] - $p3
  printf "PARTS static=%u update=%u typegroups=%u lateupdate=%u entitydraws=%u scanlines=%u (hblanks, frames 300-600)\n", g_ps1ProfPart[0] - $q0, g_ps1ProfPart[1] - $q1, g_ps1ProfPart[2] - $q2, g_ps1ProfPart[3] - $q3, g_ps1ProfPart[4] - $q4, g_ps1ProfPart[5] - $q5
  printf "FRAMES engineHbl=%u frameHbl=%u\n", g_ps1ProfEngine - $en0, g_ps1ProfHbl - $hb0
  set $o = 0
  while $o < RSDK::objectClassCount
    if g_ps1ProfClass[$o][0] + g_ps1ProfClass[$o][1] > 0
      printf "PC %d %u %u %u %08x%08x%08x%08x\n", $o, g_ps1ProfClass[$o][0], g_ps1ProfClass[$o][1], g_ps1ProfCalls[$o], RSDK::objectClassList[$o].hash[0], RSDK::objectClassList[$o].hash[1], RSDK::objectClassList[$o].hash[2], RSDK::objectClassList[$o].hash[3]
    end
    set $o = $o + 1
  end
  quit
end
continue
end
continue
