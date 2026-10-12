# docs/32 phase 13 M1: what the boot detected (natural build: the Logos overlay runs the probes; or a
# `make BOOT=... HWDETECT=1` build). One H line at drawn frame $f (default 200), then the scene it reached.
set $f = 200
tbreak main
commands
silent
set var g_ps1TestStops[0] = $f
continue
end
break ps1TestStop
commands
silent
printf "H detected=%u ramMB=%u vram2MB=%u ramCtrl=%08x faults=%u extras=%u mods=%u frames=%u listPos=%d\n", *(unsigned *)&g_ps1HwDetected, *(unsigned *)&g_ps1RamMB, *(unsigned *)&g_ps1Vram2MB, *(unsigned *)&g_ps1RamCtrl, *(unsigned *)&g_ps1HwFaults, *(unsigned *)&g_ps1Extras, *(unsigned *)&g_ps1ModsLoaded, *(unsigned *)&g_ps1PerfFrames, RSDK::sceneInfo.listPos
quit
end
break psyqo::Kernel::Internal::abort
commands
silent
printf "ABORT %s\n", (char*)$a0
quit
end
continue
# a stop that is not the H line (the harness's timeout interrupt, a trap): where the game was, then quit (never resume:
# a resumed run outlives the harness)
printf "T frames=%u listPos=%d state=%d detected=%u ramMB=%u mods=%u\n", *(unsigned *)&g_ps1PerfFrames, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, *(unsigned *)&g_ps1HwDetected, *(unsigned *)&g_ps1RamMB, *(unsigned *)&g_ps1ModsLoaded
bt 8
quit
