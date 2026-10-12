# docs/32 choice 182: onScreen across a drawn frame's logic steps (capped build, `make BOOT=1,0 CAP=1`). At drawn frame
# 400 the entry of the next 4 ProcessObjects calls counts the scene slots with onScreen set: the first step of a drawn
# frame sees the last draw's; before the fix the second step saw none (the first step's late pass cleared them).
define ps1_cnt
  set $i = 0
  set $c = 0
  while $i < RSDK::ps1SceneSlotEnd
    if ((RSDK::EntityBase *)RSDK::objectEntityPtr[$i])->onScreen
      set $c = $c + 1
    end
    set $i = $i + 1
  end
  printf "L ONSCREEN f=%u lastStep=%d steps=%d count=%d\n", *(unsigned *)&::g_ps1PerfFrames, *(int *)&::g_ps1LastStep, *(int *)&::g_ps1LogicSteps, $c
end
set $k = 0
tbreak main
commands
silent
set var g_ps1TestStops[0] = 400
continue
end
break ps1TestStop
commands
silent
enable 3
continue
end
break RSDK::ProcessObjects
commands
silent
ps1_cnt
set $k = $k + 1
if $k >= 4
  quit
end
continue
end
disable 3
continue
