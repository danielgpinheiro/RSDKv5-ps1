# docs/32 phase 13 M0.2: the CPU cost of a sidekick (Sonic & Tails). Set $st first (1: Sonic with Tails, 0: Sonic
# alone): Player_StageLoad then finds globals->playerID (offset 4) set and keeps it. Pad 1 holds right from frame
# $right; frames $winA..$winB timed as scene_timing.gdb (T line), plus where player 2 is (P2 line). Needs a build whose
# reserved slot 1 takes a Player (the M0 measuring edit of ps1ReservedSpecs).
set $winA = 360
set $winB = 660
set $right = 300
tbreak main
commands
silent
set var g_ps1PadScript[0] = 0xFFFF
set var g_ps1PadScript[1] = $right
set var g_ps1PadScript[2] = 0x8
set var g_ps1PadScript[3] = 60000
set var g_ps1PadScriptSteps = 1
set var g_ps1PadScriptStep = 0
set var g_ps1TestStops[0] = $winA
set var g_ps1TestStops[1] = $winB
continue
end
break Player_StageLoad
commands
silent
if $st
  set var *(int *)((char *)globals + 4) = 0x201
end
continue
end
break ps1TestStop
commands
silent
if g_ps1PerfFrames == $winA
  set $winF = g_ps1PerfFrames
  set $winVs = g_ps1PerfVsyncs
  set var g_ps1PerfSumHbl = 0
  set var g_ps1PerfSlowFrames = 0
  set var g_ps1PerfMaxHbl = 0
end
if g_ps1PerfFrames >= $winB
  set $n = g_ps1PerfFrames - $winF
  set $p1 = (RSDK::EntityBase *)RSDK::objectEntityPtr[0]
  set $p2 = (RSDK::EntityBase *)RSDK::objectEntityPtr[1]
  printf "T st=%d frames=%u vsyncs/frame x100=%u slow=%u cpu=%u max=%u steps=%d scene=%d\n", $st, $n, (g_ps1PerfVsyncs - $winVs) * 100 / $n, g_ps1PerfSlowFrames, g_ps1PerfSumHbl / $n, g_ps1PerfMaxHbl, g_ps1LogicSteps, RSDK::sceneInfo.listPos
  printf "P2 class=%d p1x=%d p2x=%d p2y=%d playerID=%x\n", $p2->classID, $p1->position.x >> 16, $p2->position.x >> 16, $p2->position.y >> 16, *(int *)((char *)globals + 4)
  quit
end
continue
end
break psyqo::Kernel::Internal::abort
commands
silent
printf "ABORT %s\n", (char*)$a0
quit
end
break PS1StorageExhausted
commands
silent
printf "STOP storage exhausted\n"
quit
end
continue
