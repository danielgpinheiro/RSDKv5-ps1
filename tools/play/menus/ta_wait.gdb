# M14: the wait spinner and the Time Attack menu's XA readiness, sampled around the menu switch
add-symbol-file @R@/build/gen/ovl/Menu/Menu.elf
break ManiaModeMenu_HandleUnlocks
commands
silent
set var ((ProgressRAM *)&globals->saveRAM[0x900])->zoneCleared[0] = 1
continue
end
define ps1_onload
  if RSDK::sceneInfo.listPos == 2
    set var g_ps1TestStops[4] = $lf + 545
    set var g_ps1TestStops[5] = $lf + 575
    set var g_ps1TestStops[6] = $lf + 620
    set var g_ps1TestStops[7] = $lf + 680
  end
end
define ps1_onstop
  printf "L WAIT f=%u wait=%u spinnerTimer=%u hasSpinner=%d active=%d xaStarts=%u", g_ps1PerfFrames, ((ObjectManiaModeMenu *)ManiaModeMenu)->timeAttackMusicWait, ((ObjectUIWaitSpinner *)UIWaitSpinner)->timer, ((ObjectUIWaitSpinner *)UIWaitSpinner)->activeSpinner != 0, ((ObjectMusic *)Music)->activeTrack, g_ps1XaStarts
end
