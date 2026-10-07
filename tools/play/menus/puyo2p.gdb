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
# 9f: pad 2 for Puyo Puyo's 2P: from the Puyo scene's load (listPos 114) it holds A (one press: input slot 2 assigned to
# it, its level confirmed), then nothing (its board fills). Through menus.gdb.in's load hook (a second breakpoint on
# LoadSceneFolder would take the harness's own commands' place)
define ps1_onload
  if RSDK::sceneInfo.listPos == 114
    set var ::g_ps1Pad2TestMask = 0x10
  else
    set var ::g_ps1Pad2TestMask = 0
  end
end
