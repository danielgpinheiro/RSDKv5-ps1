# 9b.7 (docs/32): one 2 MB boot through every Mania-mode scene, GHZ1 (listPos 9) to ERZ (37): each runs 600 frames, then
# the next is loaded as SetScene + LoadScene do (listPos + 1, state ENGINESTATE_LOAD = 0). The loads' guards and heap
# come from menus.gdb.in (L SCENE / L AT); this file only advances (ps1_onload / ps1_onstop, g_ps1TestStops[4]).
define ps1_onload
  if RSDK::sceneInfo.listPos >= 9 && RSDK::sceneInfo.listPos < 37
    set var g_ps1TestStops[4] = $lf + 600
  else
    set var g_ps1TestStops[4] = 0
  end
end
define ps1_onstop
  if RSDK::sceneInfo.listPos >= 9 && RSDK::sceneInfo.listPos < 37 && g_ps1PerfFrames == $lf + 600
    printf "L NEXT f=%u from=%d hbl=%u headroom=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.listPos, g_ps1PerfLastHbl, g_ps1HeapHeadroom
    set var RSDK::sceneInfo.listPos = RSDK::sceneInfo.listPos + 1
    set var RSDK::sceneInfo.state = 0
  end
end
