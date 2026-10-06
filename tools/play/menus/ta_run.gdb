# Time Attack run in GHZ1 (listPos 9): the player put 48 px before the start gate (the TimeAttackGate without
# finishLine), the pad runs right through it (ta_run.steps), then 48 px before the finish gate, through it; then the
# results' state every 150 frames (the results set the next scene, the Menu, at once: listPos is 2 from there)
# (ta_unlock.gdb)
add-symbol-file @R@/build/gen/ovl/Menu/Menu.elf
break ManiaModeMenu_HandleUnlocks
commands
silent
set var ((ProgressRAM *)&globals->saveRAM[0x900])->zoneCleared[0] = 1
printf "L UNLOCK f=%u ta=%d\n", g_ps1PerfFrames, ((ProgressRAM *)&globals->saveRAM[0x900])->zoneCleared[0]
continue
end
set $ta = 0
define ps1_onload
  set $ta = RSDK::sceneInfo.listPos == 9
  if $ta
    set var g_ps1TestStops[4] = $lf + 290
    set var g_ps1TestStops[5] = $lf + 450
    set var g_ps1TestStops[6] = $lf + 640
    set var g_ps1TestStops[7] = $lf + 1100
  else
    set var g_ps1TestStops[7] = 0
    set var g_ps1TestStops[4] = 0
    set var g_ps1TestStops[5] = 0
    set var g_ps1TestStops[6] = 0
  end
end
define ps1_onstop
  if $ta && g_ps1PerfFrames >= $lf + 1100
    set $ac = (struct EntityActClear *)RSDK::objectEntityPtr[SLOT_ACTCLEAR]
    printf "L RESULTS f=%u saving=%d state=%p cardSaves=%u asyncDone=%u listPos=%d\n", g_ps1PerfFrames, ActClear->isSavingGame, $ac->state, g_ps1CardSaves, g_ps1CardAsyncDone, RSDK::sceneInfo.listPos
    set var g_ps1TestStops[7] = g_ps1PerfFrames + 150
  end
  if $ta && (g_ps1PerfFrames == $lf + 290 || g_ps1PerfFrames == $lf + 450 || g_ps1PerfFrames == $lf + 640)
    set $s = 0
    set $n = 0
    set $fin = -1
    set $st = -1
    while $s < 0x840
      set $e = (struct EntityTimeAttackGate *)RSDK::objectEntityPtr[$s]
      if $e->classID == *(short *)TimeAttackGate
        if $e->finishLine
          set $fin = $n
        else
          set $st = $n
        end
        set $n = $n + 1
      end
      set $s = $s + 1
    end
    printf "L GATES f=%u start=%d finish=%d started=%d finished=%d timer=%d:%d.%d\n", g_ps1PerfFrames, $st, $fin, TimeAttackGate->started, TimeAttackGate->isFinished, RSDK::sceneInfo.minutes, RSDK::sceneInfo.seconds, RSDK::sceneInfo.milliseconds
    if g_ps1PerfFrames == $lf + 290 || g_ps1PerfFrames == $lf + 450
      set var g_ps1TestBossClass = *(short *)TimeAttackGate
      set var g_ps1TestBossNth = (g_ps1PerfFrames == $lf + 290) ? $st : $fin
      set var g_ps1TestBossDX = -48
      set var g_ps1TestBossDY = 0
      set var g_ps1TestBossWarp = 1
    end
  end
end
