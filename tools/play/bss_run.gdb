# Blue Spheres (docs/32 7.1), `make BOOT=4,0` (layout 1): the stage runs forward by itself; Left at 400, Right at 700,
# A (jump) at 900 and 1100. L lines every 30 frames: state, logic + draw hblanks, missing sprites / unsupported draws,
# SFX / music; the guards at 1500.
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
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
break RSDK::ProcessInput if g_ps1PerfFrames % 100 == 0 || g_ps1PerfFrames % 100 == 8
commands
silent
set $f = g_ps1PerfFrames
set *$ps1pad = 0
if $f == 400
  set *$ps1pad = 0x4
end
if $f == 700
  set *$ps1pad = 0x8
end
if $f == 900 || $f == 1100
  set *$ps1pad = 0x10
end
continue
end
break perfTick if g_ps1PerfFrames % 30 == 0
commands
silent
printf "L %u cat=%d list=%d state=%d hbl=%u | sprMissing=%u inkTodo=%u primTodo=%u atlDropped=%d | sfx plays=%u unl=%u jump=%u | mus starts=%u missing=%u track=%d\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, g_ps1PerfLastHbl, *(unsigned *)&g_ps1SpriteMissing, *(unsigned *)&g_ps1SpriteInkTodo, *(unsigned *)&g_ps1PrimTodo, ps1AtlDropped, g_ps1SfxPlays, g_ps1SfxUnloaded, BSS_Player->sfxJump, g_ps1MusicStarts, g_ps1MusicMissing, g_ps1XaTrack
if g_ps1PerfFrames >= 1500
  printf "L GUARD fail=%u,%u,%u,%u,%u oom=%u missing=%u stubs=%u heapHeadroom=%u stgLimit=%u stgUsed=%u arenaFail=%u chainOverflow=%u ovlErrors=%u moved=%u forcedDefrag=%u\n", g_ps1StorageFail[0], g_ps1StorageFail[1], g_ps1StorageFail[2], g_ps1StorageFail[3], g_ps1StorageFail[4], g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, (unsigned)g_ps1UnlinkedCalls, g_ps1HeapHeadroom, g_ps1StgLimit, RSDK::dataStorage[0].usedStorage * 4, g_ps1ChainArenaFail, g_ps1ChainOverflow, g_ps1OvlErrors, RSDK::g_ps1EntityArenaMoved, g_ps1StorageForcedDefrags[0]
  quit
end
continue
end
continue
