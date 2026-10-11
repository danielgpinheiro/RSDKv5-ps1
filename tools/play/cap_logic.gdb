# docs/32 choice 182: the 30 fps zone cap keeps the game's logic. GHZ1 (BOOT=1,0) with the GHZ1 run's inputs (right from
# logic frame 240, jumps at 316 and 412: ghz1_run.gdb) through the scripted pad, which counts logic steps; one line per
# 30 logic steps (a stop at every RSDK::ProcessObjects; in OUT.log). Run uncapped (`make BOOT=1,0`) and capped (`make BOOT=1,0 CAP=1`):
# the S lines must be identical (the V line: the drawn frames, half as many capped).
set $n = 0
tbreak main
commands
silent
set var g_ps1PadScript[0] = 0xFFFF
set var g_ps1PadScript[1] = 240
set var g_ps1PadScript[2] = 0x8
set var g_ps1PadScript[3] = 76
set var g_ps1PadScript[4] = 0xFFFF
set var g_ps1PadScript[5] = 316
set var g_ps1PadScript[6] = 0x18
set var g_ps1PadScript[7] = 14
set var g_ps1PadScript[8] = 0xFFFF
set var g_ps1PadScript[9] = 330
set var g_ps1PadScript[10] = 0x8
set var g_ps1PadScript[11] = 82
set var g_ps1PadScript[12] = 0xFFFF
set var g_ps1PadScript[13] = 412
set var g_ps1PadScript[14] = 0x18
set var g_ps1PadScript[15] = 14
set var g_ps1PadScript[16] = 0xFFFF
set var g_ps1PadScript[17] = 426
set var g_ps1PadScript[18] = 0x8
set var g_ps1PadScript[19] = 2000
set var g_ps1PadScriptSteps = 5
set var g_ps1PadScriptStep = 0
continue
end
break RSDK::ProcessObjects
commands
silent
set $n = $n + 1
if $n % 30 == 0
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  printf "S %d state=%d x=%d y=%d gv=%d vx=%d vy=%d ground=%d rings=%d score=%d time=%u:%02u.%02u anim=%d frame=%d rand=%u\n", $n, RSDK::sceneInfo.state, $p->position.x, $p->position.y, $p->groundVel, $p->velocity.x, $p->velocity.y, $p->onGround, $p->rings, $p->score, RSDK::sceneInfo.minutes, RSDK::sceneInfo.seconds, RSDK::sceneInfo.milliseconds, $p->animator.animationID, $p->animator.frameID, RSDK::randSeed
end
if $n >= 1500
  printf "V drawn=%u steps=%d logicSteps=%d\n", g_ps1PerfFrames, $n, g_ps1LogicSteps
  quit
end
continue
end
continue
