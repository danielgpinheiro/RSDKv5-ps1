# Time Attack unlocked (GameProgress: zone 0 cleared) just before the Mania Mode menu checks its unlocks
add-symbol-file @R@/build/gen/ovl/Menu/Menu.elf
break ManiaModeMenu_HandleUnlocks
commands
silent
set var ((ProgressRAM *)&globals->saveRAM[0x900])->zoneCleared[0] = 1
printf "L UNLOCK f=%u ta=%d\n", g_ps1PerfFrames, ((ProgressRAM *)&globals->saveRAM[0x900])->zoneCleared[0]
continue
end
