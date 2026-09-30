# Blue Spheres in and out (docs/32 7.1), `make BOOT=1,0` (GHZ1): at frame 240 the player gets 50 rings and stands before
# the star post in scene slot 44 (entity 44 + RESERVE_ENTITY_COUNT 64; x 4580, y 544) holding right; once the post has its stars up (bonusStageID set, starTimer >= 60)
# the player is placed in them: the zone is saved, the Blue Spheres layout loads (FROZEN + fade), it ends (a red
# sphere: Left at +300 after the stage starts), and the game goes back to GHZ1 at the star post. L lines every 30
# frames; the guards at the end.
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
set $bss = 0
set $placed = 0
break RSDK::LoadSceneAssets
commands
silent
printf "L LOADASSETS frame=%u cat=%d list=%d folder=%s\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::currentSceneFolder
if RSDK::sceneInfo.activeCategory == 4
  set $bss = g_ps1PerfFrames
end
continue
end
break psyqo::Kernel::Internal::abort
commands
silent
printf "L ABORT %s frame=%u\n", (char*)$a0, g_ps1PerfFrames
bt 12
quit
end
break PS1StorageExhausted
commands
silent
printf "L EXHAUSTED set=%d size=%d frame=%u\n", $a0, $a1, g_ps1PerfFrames
bt 12
quit
end
break perfTick
commands
silent
set $f = g_ps1PerfFrames
if RSDK::sceneInfo.activeCategory == 1 && !$bss
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  set $post = (struct EntityStarPost *)RSDK::objectEntityPtr[44 + 64]
  if $f == 240
    set $p->rings = 50
    set $p->position.x = $post->position.x - (48 << 16)
    set $p->position.y = $post->position.y - (16 << 16)
    set *$ps1pad = 0x8
  end
  if $f > 240 && !$placed && $post->bonusStageID && $post->starTimer >= 60
    set *$ps1pad = 0
    set $p->position.x = $post->position.x
    set $p->position.y = $post->position.y - (44 << 16)
    set $placed = $f
    printf "L PLACED frame=%u bonus=%d rings=%d\n", $f, $post->bonusStageID, $p->rings
  end
end
if $bss && ($f == $bss + 300)
  set *$ps1pad = 0x4
end
if $bss && ($f == $bss + 308)
  set *$ps1pad = 0
end
if $f % 30 == 0 && RSDK::sceneInfo.activeCategory == 1 && $f >= 240
  printf "L POS %u x=%d y=%d gv=%d rings=%d | post x=%d y=%d act=%d inter=%d bonus=%d starTimer=%d\n", $f, $p->position.x >> 16, $p->position.y >> 16, $p->groundVel, $p->rings, $post->position.x >> 16, $post->position.y >> 16, $post->active, $post->interactedPlayers, $post->bonusStageID, $post->starTimer
end
if $f % 10 == 0
  printf "L %u cat=%d list=%d state=%d hbl=%u sprMissing=%u inkTodo=%u primTodo=%u | mus track=%d starts=%u | sfx unl=%u | chain overflow=%u high=%u\n", $f, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, g_ps1PerfLastHbl, *(unsigned *)&g_ps1SpriteMissing, *(unsigned *)&g_ps1SpriteInkTodo, *(unsigned *)&g_ps1PrimTodo, g_ps1XaTrack, g_ps1MusicStarts, g_ps1SfxUnloaded, g_ps1ChainOverflow, g_ps1ChainHighWater
end
if $f >= 1200 && !$bss || $f >= 2400
  printf "L GUARD fail=%u,%u,%u,%u,%u oom=%u missing=%u stubs=%u heapHeadroom=%u stgLimit=%u arenaFail=%u chainOverflow=%u ovlErrors=%u moved=%u forcedDefrag=%u\n", g_ps1StorageFail[0], g_ps1StorageFail[1], g_ps1StorageFail[2], g_ps1StorageFail[3], g_ps1StorageFail[4], g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, (unsigned)g_ps1UnlinkedCalls, g_ps1HeapHeadroom, g_ps1StgLimit, g_ps1ChainArenaFail, g_ps1ChainOverflow, g_ps1OvlErrors, RSDK::g_ps1EntityArenaMoved, g_ps1StorageForcedDefrags[0]
  quit
end
continue
end
continue
