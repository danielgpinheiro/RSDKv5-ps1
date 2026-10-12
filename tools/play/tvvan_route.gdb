# SPZ1 TV vans (`make BOOT=1,4`): player 1 warped into a van's entry box at frame 300 ($vanX / $vanY set first: the van's
# position + 16 px toward its door), then the radio's route to its exit TV. P lines every 30 frames: player position,
# state, visible; END at frame $endF: where the player came out (docs/32: van 1214's route skipped TV 1216 on the PS1).
set $warped = 0
break perfTick if g_ps1PerfFrames >= 300 && (g_ps1PerfFrames <= 304 || g_ps1PerfFrames % 30 == 0)
commands
silent
set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
if g_ps1PerfFrames == 300
  set var g_ps1TestBossDX = $vanX
  set var g_ps1TestBossDY = $vanY
  set var g_ps1TestBossWarp = 2
  set var g_ps1TestBossClass = 0xFFF0
  set var $p->blinkTimer = 600
end
if g_ps1PerfFrames == 304
  set var g_ps1TestBossClass = 0
end
printf "P %u x=%d y=%d vis=%d state=%x list=%d steps=%d\n", g_ps1PerfFrames, $p->position.x >> 16, $p->position.y >> 16, $p->visible, $p->state, RSDK::sceneInfo.listPos, g_ps1LogicSteps
if g_ps1PerfFrames >= $endF
  printf "END x=%d y=%d vis=%d state=%x\n", $p->position.x >> 16, $p->position.y >> 16, $p->visible, $p->state
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
continue
