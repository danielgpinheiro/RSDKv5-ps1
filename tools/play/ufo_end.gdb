# A UFO stage to its end and out (docs/32 7.5), `make BOOT=3,N` (UFO N+1, 2 MB; replace OVL with the folder): the
# stage's rings set to 0 at frame 300 (TIME OVER a second later), the fade, the results, the next scene's load; the
# guards 300 frames into that scene. L lines at every load and every 120 frames.
add-symbol-file build/gen/ovl/OVL/OVL.elf
set $out = 0
break RSDK::LoadSceneAssets
commands
silent
printf "L LOADASSETS frame=%u cat=%d list=%d folder=%s used=%u limit=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::currentSceneFolder, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit
if g_ps1PerfFrames > 300
  set $out = g_ps1PerfFrames
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
if $f == 300
  set var UFO_Setup->rings = 0
end
if $f % 120 == 0
  printf "L %u cat=%d list=%d state=%d hbl=%u stgUsed=%u stgLimit=%u | sprMissing=%u overflow=%u\n", $f, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, g_ps1PerfLastHbl, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit, *(unsigned *)&g_ps1SpriteMissing, g_ps1ChainOverflow
end
if $out && $f >= $out + 300
  printf "L GUARD fail=%u,%u,%u,%u,%u oom=%u missing=%u stubs=%u heapHeadroom=%u arenaFail=%u ovlErrors=%u meshErr=%u ghostWrites=%u relocations=%u spareFail=%u\n", g_ps1StorageFail[0], g_ps1StorageFail[1], g_ps1StorageFail[2], g_ps1StorageFail[3], g_ps1StorageFail[4], g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, (unsigned)g_ps1UnlinkedCalls, g_ps1HeapHeadroom, g_ps1ChainArenaFail, g_ps1OvlErrors, g_ps1MeshErrors, RSDK::g_ps1EntityGhostWrites, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntitySpareFail
  quit
end
continue
end
continue
