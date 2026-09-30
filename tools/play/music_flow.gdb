# SFX (docs/32 5.1) and music (5.2) over the pause flow at 2 MB: the uploads read back (g_ps1SfxVerify), plays / unloaded per 20 frames.
# Pause flow at 2 MB (docs/32 4.3): `make BOOT=1,0`; tools/play_test.sh tools/play/pause_flow.gdb OUT. Start (300) pauses,
# A (400) continues, Start (500) + Down (560) + A (600) opens Restart, Right (680) + A (720) restarts GHZ1; Start (1100) +
# Down x2 + A (1240) opens Exit, Right + A (1340) returns to the Menu. L lines every 20 frames, the guards at 1800.
break RSDK::AudioDevice::Init
commands
silent
set var g_ps1SfxVerify = 1
continue
end
break Object.cpp:372 if RSDK::sceneInfo.entity->classID >= RSDK::sceneInfo.classCount
commands
silent
printf "L BADCLASS slot=%d class=%d classCount=%d entity=%08x ptr0=%08x\n", RSDK::sceneInfo.entitySlot, RSDK::sceneInfo.entity->classID, RSDK::sceneInfo.classCount, RSDK::sceneInfo.entity, RSDK::objectEntityPtr[RSDK::sceneInfo.entitySlot]
x/8wx RSDK::sceneInfo.entity
quit
end
break psyqo::Kernel::Internal::abort
commands
silent
printf "L ABORT %s frame=%u\n", (char*)$a0, g_ps1PerfFrames
bt 12
quit
end
break RSDK::LoadSceneAssets
commands
silent
printf "L LOADASSETS frame=%u used=%u limit=%u entries=%d\n", g_ps1PerfFrames, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit, RSDK::dataStorage[0].entryCount
continue
end
break PS1StorageExhausted
commands
silent
printf "L EXHAUSTED set=%d size=%d frame=%u used=%u limit=%u entries=%d stgLimit=%u\n", $a0, $a1, g_ps1PerfFrames, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit, RSDK::dataStorage[0].entryCount, g_ps1StgLimit
bt 12
quit
end
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
break RSDK::ProcessInput if (g_ps1PerfFrames % 20 == 0 || g_ps1PerfFrames % 20 == 4) && g_ps1PerfFrames >= 300 && g_ps1PerfFrames <= 1500
commands
silent
set $f = g_ps1PerfFrames
set *$ps1pad = 0
if $f == 300 || $f == 500 || $f == 1100
  set *$ps1pad = 0x400
end
if $f == 400 || $f == 600 || $f == 720 || $f == 1240 || $f == 1340
  set *$ps1pad = 0x10
end
if $f == 560 || $f == 1160 || $f == 1200
  set *$ps1pad = 0x2
end
if $f == 680 || $f == 1300
  set *$ps1pad = 0x8
end
continue
end
break perfTick if g_ps1PerfFrames % 20 == 0
commands
silent
printf "L %u cat=%d list=%d state=%d hbl=%u tint=%u moved=%u forced=%u sfx banks=%u bytes=%u err=%u plays=%u unl=%u last=%d vb=%u vf=%u | mus ready=%u starts=%u missing=%u resumes=%u forced=%u loops=%u track=%d xaStarts=%u pos=%d\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, g_ps1PerfLastHbl, *(unsigned*)&g_ps1TintGrey, RSDK::g_ps1EntityArenaMoved, g_ps1StorageForcedDefrags[0], g_ps1SfxBanks, g_ps1SfxBankBytes, g_ps1SfxBankErrors, g_ps1SfxPlays, g_ps1SfxUnloaded, g_ps1SfxUnloadedLast, g_ps1SfxVerifyBytes, g_ps1SfxVerifyFail, g_ps1MusicReady, g_ps1MusicStarts, g_ps1MusicMissing, g_ps1MusicResumes, g_ps1XaForcedStops, g_ps1XaLoops, g_ps1XaTrack, g_ps1XaStarts, RSDK::channels[0].bufferPos
if g_ps1PerfFrames >= 1800
  printf "L GUARD fail=%u,%u,%u,%u,%u oom=%u missing=%u stubs=%u heapHeadroom=%u stgLimit=%u stgUsed=%u stgPeak=%u tmpPeak=%u strPeak=%u arenaFail=%u chainOverflow=%u primTodo=%u inkTodo=%u dbOverflow=%u ta=%d missing=%u atlDropped=%d\n", g_ps1StorageFail[0], g_ps1StorageFail[1], g_ps1StorageFail[2], g_ps1StorageFail[3], g_ps1StorageFail[4], g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, (unsigned)g_ps1UnlinkedCalls, g_ps1HeapHeadroom, g_ps1StgLimit, RSDK::dataStorage[0].usedStorage * 4, g_ps1StorageLivePeak[0], g_ps1StorageLivePeak[4], g_ps1StorageLivePeak[3], g_ps1ChainArenaFail, g_ps1ChainOverflow, *(unsigned *)&g_ps1PrimTodo, *(unsigned *)&g_ps1SpriteInkTodo, *(unsigned *)&g_ps1UserDBOverflow, globals->taTableLoaded, *(unsigned *)&g_ps1SpriteMissing, ps1AtlDropped
  quit
end
continue
end
continue
printf "L STOPPED pc=%08x cause=%08x bad=%08x ra=%08x sp=%08x frame=%u\n", $pc, $cause, $bad, $ra, $sp, g_ps1PerfFrames
info symbol $ra
bt 12
