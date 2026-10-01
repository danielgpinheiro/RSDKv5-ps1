# Music over the new-game walk (docs/32 5.2 / 5.3), `make BOOT=0,2`: menu_newgame_input.gdb's presses (Menu -> Save
# Select -> a new save -> AIZ intro -> GHZCutscene -> GHZ1). L lines every 30 frames: scene, the music track and
# position, forced stops (file reads) and resumes; SFX; the guards at 3000.
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
break RSDK::ProcessInput if g_ps1PerfFrames == 200 || g_ps1PerfFrames == 204 || g_ps1PerfFrames == 320 || g_ps1PerfFrames == 324 || g_ps1PerfFrames == 480 || g_ps1PerfFrames == 484
commands
silent
if g_ps1PerfFrames == 200 || g_ps1PerfFrames == 320 || g_ps1PerfFrames == 480
  set *$ps1pad = 0x10
else
  set *$ps1pad = 0
end
continue
end
break RSDK::LoadSceneAssets
commands
silent
printf "L LOADASSETS frame=%u folder=%s xaTrack=%d forced=%u\n", g_ps1PerfFrames, RSDK::currentSceneFolder, g_ps1XaTrack, g_ps1XaForcedStops
continue
end
break RSDK::PS1StreamStart
commands
silent
printf "L PLAYSTREAM frame=%u %s start=%u loop=%u\n", g_ps1PerfFrames, streamFilePath, streamStartPos, streamLoopPoint
continue
end
break psyqo::Kernel::Internal::abort
commands
silent
printf "L ABORT %s frame=%u\n", (char*)$a0, g_ps1PerfFrames
bt 12
quit
end
break perfTick if g_ps1PerfFrames % 30 == 0
commands
silent
printf "L %u cat=%d list=%d state=%d hbl=%u | mus starts=%u missing=%u resumes=%u forced=%u loops=%u ends=%u track=%d ch0=%d pos=%d | sfx banks=%u plays=%u unl=%u last=%d err=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, g_ps1PerfLastHbl, g_ps1MusicStarts, g_ps1MusicMissing, g_ps1MusicResumes, g_ps1XaForcedStops, g_ps1XaLoops, g_ps1XaEnds, g_ps1XaTrack, RSDK::channels[0].state, RSDK::channels[0].bufferPos, g_ps1SfxBanks, g_ps1SfxPlays, g_ps1SfxUnloaded, g_ps1SfxUnloadedLast, g_ps1SfxBankErrors
if g_ps1PerfFrames >= 3000
  printf "L GUARD fail=%u,%u,%u,%u,%u oom=%u missing=%u stubs=%u heapHeadroom=%u stgLimit=%u arenaFail=%u chainOverflow=%u moved=%u forcedDefrag=%u relocations=%u spareFail=%u ghostWrites=%u createOverwrites=%u dormantFail=%u dormant=%u\n", g_ps1StorageFail[0], g_ps1StorageFail[1], g_ps1StorageFail[2], g_ps1StorageFail[3], g_ps1StorageFail[4], g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, (unsigned)g_ps1UnlinkedCalls, g_ps1HeapHeadroom, g_ps1StgLimit, g_ps1ChainArenaFail, g_ps1ChainOverflow, RSDK::g_ps1EntityArenaMoved, g_ps1StorageForcedDefrags[0], RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntitySpareFail, RSDK::g_ps1EntityGhostWrites, RSDK::g_ps1EntityCreateOverwrites, RSDK::g_ps1DormantFail, RSDK::g_ps1DormantRecords
  quit
end
continue
end
continue
