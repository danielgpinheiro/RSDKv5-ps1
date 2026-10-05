# Pause flow at 2 MB (docs/32 4.3): `make BOOT=1,0`; tools/play_test.sh tools/play/pause_flow.gdb OUT. Start (300) pauses,
# A (400) continues, Start (500) + Down (560) + A (600) opens Restart, Right (680) + A (720) restarts GHZ1; Start (1100) +
# Down x2 + A (1240) opens Exit, Right + A (1340) returns to the Menu. L lines every 20 frames, the guards at 1800.
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
break RSDK::ProcessInput if (g_ps1PerfFrames % 20 == 0 || g_ps1PerfFrames % 20 == 4) && g_ps1PerfFrames >= 1800 && g_ps1PerfFrames <= 3000
commands
silent
set $f = g_ps1PerfFrames
set *$ps1pad = 0
if $f == 1800 || $f == 2000 || $f == 2600
  set *$ps1pad = 0x400
end
if $f == 1900 || $f == 2100 || $f == 2220 || $f == 2740 || $f == 2840
  set *$ps1pad = 0x10
end
if $f == 2060 || $f == 2660 || $f == 2700
  set *$ps1pad = 0x2
end
if $f == 2180 || $f == 2800
  set *$ps1pad = 0x8
end
continue
end
break perfTick if g_ps1PerfFrames % 20 == 0
commands
silent
printf "L %u cat=%d list=%d state=%d hbl=%u tint=%u moved=%u forced=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, g_ps1PerfLastHbl, *(unsigned*)&g_ps1TintGrey, RSDK::g_ps1EntityArenaMoved, g_ps1StorageForcedDefrags[0]
if g_ps1PerfFrames >= 3300
  printf "L GUARD fail=%u,%u,%u,%u,%u oom=%u missing=%u stubs=%u heapHeadroom=%u stgLimit=%u stgUsed=%u stgPeak=%u tmpPeak=%u strPeak=%u arenaFail=%u chainOverflow=%u primTodo=%u inkTodo=%u dbOverflow=%u ta=%d missing=%u atlDropped=%d\n", g_ps1StorageFail[0], g_ps1StorageFail[1], g_ps1StorageFail[2], g_ps1StorageFail[3], g_ps1StorageFail[4], g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, (unsigned)g_ps1UnlinkedCalls, g_ps1HeapHeadroom, g_ps1StgLimit, RSDK::dataStorage[0].usedStorage * 4, g_ps1StorageLivePeak[0], g_ps1StorageLivePeak[4], g_ps1StorageLivePeak[3], g_ps1ChainArenaFail, g_ps1ChainOverflow, *(unsigned *)&g_ps1PrimTodo, *(unsigned *)&g_ps1SpriteInkTodo, *(unsigned *)&g_ps1UserDBOverflow, globals->taTableLoaded, *(unsigned *)&g_ps1SpriteMissing, ps1AtlDropped
  quit
end
continue
end
continue
printf "L STOPPED pc=%08x cause=%08x bad=%08x ra=%08x sp=%08x frame=%u\n", $pc, $cause, $bad, $ra, $sp, g_ps1PerfFrames
info symbol $ra
bt 12
