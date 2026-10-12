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
printf "H detected=%u ramMB=%u vram2MB=%u ramCtrl=%08x faults=%u extras=%u frames=%u listPos=%d\n", *(unsigned *)&g_ps1HwDetected, *(unsigned *)&g_ps1RamMB, *(unsigned *)&g_ps1Vram2MB, *(unsigned *)&g_ps1RamCtrl, *(unsigned *)&g_ps1HwFaults, *(unsigned *)&g_ps1Extras, *(unsigned *)&g_ps1PerfFrames, RSDK::sceneInfo.listPos
quit
end
break psyqo::Kernel::Internal::abort
commands
silent
printf "ABORT %s\n", (char*)$a0
quit
end
# (redux16mb -8mb pauses on a read of the missing second bank (open bus, 0xFFFFFFFF: the probe expects it): go on)
set $n = 0
while $n < 20
  set $n = $n + 1
  continue
end
