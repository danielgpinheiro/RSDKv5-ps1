# Extras unlocked (GameProgress: every silver medal, so Blue Spheres and D.A. Garden) just before the menu checks unlocks
add-symbol-file @R@/build/gen/ovl/Menu/Menu.elf
break ManiaModeMenu_HandleUnlocks
commands
silent
set var ((ProgressRAM *)&globals->saveRAM[0x900])->silverMedalCount = 32
set var ((ProgressRAM *)&globals->saveRAM[0x900])->zoneCleared[0] = 1
printf "L UNLOCK f=%u medals=%d\n", g_ps1PerfFrames, ((ProgressRAM *)&globals->saveRAM[0x900])->silverMedalCount
continue
end
