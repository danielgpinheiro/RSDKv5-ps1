# docs/32 9n / docs/40 (the v3/v4 ports' fix 75ddf41): the XA pause wait is bounded. GHZ1 (`make BOOT=1,0`): Start at frame
# 300 pauses (PauseSound -> PS1XaPause); with $force (set first) the CD device is put back to PLAYING (5) at its wait, as if
# the drive's PAUSE answer never came, so the action never ends by itself. Before the fix the game stopped there; now
# waitIdle aborts after ~3 s (g_ps1XaAborts 1), the abandoned answer is drained (g_ps1CdIrqOrphans), the pause menu runs;
# Start at 600 resumes the music (g_ps1XaStarts grows). Stops only at the frames asked for (g_ps1TestStops).
set $forced = 0
break xa_music.cpp:412
commands
silent
if $force && !$forced
  set $forced = 1
  set var g_ps1CDRom->m_state = 5
  printf "L FORCED frame=%u\n", g_ps1PerfFrames
end
continue
end
tbreak main
commands
silent
set var g_ps1TestStops[0] = 300
set var g_ps1TestStops[1] = 306
set var g_ps1TestStops[2] = 400
set var g_ps1TestStops[3] = 500
set var g_ps1TestStops[4] = 600
set var g_ps1TestStops[5] = 606
set var g_ps1TestStops[6] = 800
set var g_ps1TestStops[7] = 1000
continue
end
break ps1TestStop
commands
silent
if g_ps1PerfFrames == 300 || g_ps1PerfFrames == 600
  set var g_ps1PadTestMask = 0x400
end
if g_ps1PerfFrames == 306 || g_ps1PerfFrames == 606
  set var g_ps1PadTestMask = 0
end
printf "L F %u state=%d aborts=%u giveUps=%u orphans=%u stale=%u xaStarts=%u forcedStops=%u vbl=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.state, g_ps1XaAborts, g_ps1XaGiveUps, g_ps1CdIrqOrphans, g_ps1CdIrqStale, g_ps1XaStarts, g_ps1XaForcedStops, g_ps1VblankCount
if g_ps1PerfFrames >= 1000
  quit
end
continue
end
break psyqo::Kernel::Internal::abort
commands
silent
printf "L ABORT %s frame=%u\n", (char*)$a0, g_ps1PerfFrames
bt 8
quit
end
continue
