# docs/32 phase 13 M5: Sonic & Tails through a BOOT= act (`make BOOT=... HWDETECT=1`, a console with 8 MB+ and the VRAM
# mod): playerID = Sonic + Tails at the stage load, pad 1 holds right from frame 300, frames 600-1500 timed; at 1500:
# player 2, the atlas / entity guards and STG's use.
tbreak main
commands
silent
set var g_ps1PadScript[0] = 0xFFFF
set var g_ps1PadScript[1] = 300
set var g_ps1PadScript[2] = 0x8
set var g_ps1PadScript[3] = 60000
set var g_ps1PadScriptSteps = 1
set var g_ps1PadScriptStep = 0
set var g_ps1TestStops[0] = 600
set var g_ps1TestStops[1] = 1500
continue
end
break Player_StageLoad
commands
silent
set var *(int *)((char *)globals + 4) = $duo
continue
end
break ps1TestStop
commands
silent
if *(unsigned *)&g_ps1PerfFrames == 600
  set $vsA = *(unsigned *)&g_ps1PerfVsyncs
  set var g_ps1PerfSumHbl = 0
  set var g_ps1PerfMaxHbl = 0
else
  set $p1 = (RSDK::EntityBase *)RSDK::objectEntityPtr[0]
  set $p2 = (RSDK::EntityBase *)RSDK::objectEntityPtr[1]
  printf "D duo=%x vsyncs/frame x100=%u cpu=%u max=%u p1x=%d p2class=%d p2x=%d missing=%u relocations=%u ghostWrites=%u dormantFail=%u stgUsed=%u stgLimit=%u\n", $duo, (*(unsigned *)&g_ps1PerfVsyncs - $vsA) * 100 / 900, *(unsigned *)&g_ps1PerfSumHbl / 900, *(unsigned *)&g_ps1PerfMaxHbl, $p1->position.x >> 16, $p2->classID, $p2->position.x >> 16, *(unsigned *)&g_ps1SpriteMissing, *(unsigned *)&RSDK::g_ps1EntityRelocations, *(unsigned *)&RSDK::g_ps1EntityGhostWrites, *(unsigned *)&RSDK::g_ps1DormantFail, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit
  quit
end
continue
end
# (no abort / storage breakpoints: redux16mb's GDB stub failed to resume with five; the D line shows STG)
continue
printf "T stopped f=%u\n", *(unsigned *)&g_ps1PerfFrames
quit
