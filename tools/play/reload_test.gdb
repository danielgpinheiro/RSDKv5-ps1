# docs/32 phase 13 M2: the CD sector cache, measured. The BOOT= scene is loaded again at drawn frames $a and $b (the
# engine's load state, as a retry does); an R line at each and at $end: CD requests / sectors / microseconds in CD
# reads, vblanks, the cache's hits and bytes (Mods overlay, 0 on a stock console), XA stops. Each load's cost is the
# difference between two lines. Needs `make BOOT=... HWDETECT=1` for the cache.
set $a = 300
set $b = 600
set $end = 900
# ($mh / $mb: the addresses of g_ps1ModsHits / g_ps1ModsHitBytes in build/gen/ovl/Mods/Mods.elf, set before this file;
# meaningful only with modsLoaded=1: on a stock console that RAM is the low 2 MB's mirror)
define ps1_r
  printf "R f=%u req=%u sec=%u us=%u vsyncs=%u modsLoaded=%u hits=%u hitBytes=%u xaStops=%u ramMB=%u\n", *(unsigned *)&g_ps1PerfFrames, *(unsigned *)&g_ps1FsRequests, *(unsigned *)&g_ps1FsSectors, *(unsigned *)&g_ps1FsReadUs, *(unsigned *)&g_ps1PerfVsyncs, *(unsigned *)&g_ps1ModsLoaded, *(unsigned *)$mh, *(unsigned *)$mb, *(unsigned *)&g_ps1XaForcedStops, *(unsigned *)&g_ps1RamMB
end
tbreak main
commands
silent
set var g_ps1TestStops[0] = $a
set var g_ps1TestStops[1] = $b
set var g_ps1TestStops[2] = $end
continue
end
break ps1TestStop
commands
silent
ps1_r
if *(unsigned *)&g_ps1PerfFrames >= $end
  quit
end
set var RSDK::sceneInfo.state = 0
continue
end
break psyqo::Kernel::Internal::abort
commands
silent
printf "ABORT %s\n", (char*)$a0
quit
end
continue
