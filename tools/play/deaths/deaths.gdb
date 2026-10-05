# 9b.3 (docs/32): in a zone boot, at each load + 300 frames: log the guards and sprite misses, then
#  step 0: a death (the act restarts), step 1: a time over (timer at 9:59.90), step 2: a death with one life (game over),
#  step 3: the scene the game over leads to is logged, then quit. No load within 4000 frames: L TIMEOUT.
set $lf = 0
set $armed = 1
set $step = 0
break RSDK::LoadSceneFolder
commands
silent
printf "L SCENE f=%u cat=%d pos=%d folder=%s name=%s\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].folder, RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].name
set $lf = g_ps1PerfFrames
set $armed = 1
continue
end
break perfTick if $armed && g_ps1PerfFrames == $lf + 300
commands
silent
set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
printf "L AT f=%u pos=%d state=%d x=%d y=%d rings=%d hbl=%u headroom=%u oom=%u missing=%u spriteMissing=%u relocations=%u ghost=%u stgFail=%u acMissing=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, $p->position.x >> 16, $p->position.y >> 16, $p->rings, g_ps1PerfLastHbl, g_ps1HeapHeadroom, g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, *(unsigned *)&g_ps1SpriteMissing, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntityGhostWrites, g_ps1StorageFail[0], g_ps1SpriteOutside
set $i = 0
while $i < 8
  set $e = g_ps1MissingSheet[$i]
  if $e
    set $s = ($e & 0xFF) - 1
    printf "L MISS sheet=%d frames=%u rect=%u,%u hash=%08x%08x\n", $s, $e >> 8, g_ps1MissingRect[$i] & 0xFFFF, g_ps1MissingRect[$i] >> 16, RSDK::gfxSurface[$s].hash[0], RSDK::gfxSurface[$s].hash[1]
  end
  set var g_ps1MissingSheet[$i] = 0
  set $i = $i + 1
end
set $armed = 0
if $step == 3
  printf "L DONE\n"
  quit
end
if $step == 2
  printf "L GAMEOVER lives=%d\n", $p->lives
  set var $p->lives = 1
  set var $p->deathType = 1
  set $step = 3
end
if $step == 1
  printf "L TIMEOVER\n"
  set var RSDK::sceneInfo.minutes = 9
  set var RSDK::sceneInfo.seconds = 59
  set var RSDK::sceneInfo.milliseconds = 90
  set $step = 2
end
if $step == 0
  printf "L DEATH lives=%d\n", $p->lives
  set var $p->deathType = 1
  set $step = 1
end
continue
end
break perfTick if g_ps1PerfFrames == $lf + 4000
commands
silent
printf "L TIMEOUT f=%u step=%d state=%d\n", g_ps1PerfFrames, $step, RSDK::sceneInfo.state
quit
end
break PS1OutOfMemory
commands
silent
printf "L STOP oom %u\n", $a0
bt 8
quit
end
continue
