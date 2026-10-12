# docs/32 phase 13 M4: the atlas counters after a run (pad 1 holds right from frame 300; one A line at drawn frame $f)
set $f = 1500
tbreak main
commands
silent
set var g_ps1PadScript[0] = 0xFFFF
set var g_ps1PadScript[1] = 300
set var g_ps1PadScript[2] = 0x8
set var g_ps1PadScript[3] = 60000
set var g_ps1PadScriptSteps = 1
set var g_ps1PadScriptStep = 0
set var g_ps1TestStops[0] = $f
continue
end
break ps1TestStop
commands
silent
printf "A f=%u atxLoads=%u loads=%u errors=%u unknown=%u bound=%u unplaced=%u missing=%u groupLoads=%u groupErrors=%u hiMask=%x vram2MB=%u listPos=%d\n", *(unsigned *)&g_ps1PerfFrames, *(unsigned *)&g_ps1AtlasAtxLoads, *(unsigned *)&g_ps1AtlasLoads, *(unsigned *)&g_ps1AtlasErrors, *(unsigned *)&g_ps1AtlasUnknownFiles, *(unsigned *)&g_ps1AtlasBound, *(unsigned *)&g_ps1AtlasUnplaced, *(unsigned *)&g_ps1SpriteMissing, *(unsigned *)&g_ps1AtlasGroupLoads, *(unsigned *)&g_ps1AtlasGroupErrors, *(unsigned *)&g_ps1AtlasHiMask, *(unsigned *)&g_ps1Vram2MB, RSDK::sceneInfo.listPos
quit
end
continue
printf "T stopped f=%u\n", *(unsigned *)&g_ps1PerfFrames
quit
