# UFO stage boot at 2 MB (docs/32 7.2), `make BOOT=3,0` (UFO1): prints the STG left after the load (with no meshes
# until the PS1 mesh format: the meshes' budget), the frame cost, and the guards.
break RSDK::LoadSceneAssets
commands
silent
printf "L LOADASSETS frame=%u cat=%d list=%d folder=%s used=%u limit=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::currentSceneFolder, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit
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
printf "L EXHAUSTED set=%d size=%d frame=%u used=%u limit=%u\n", $a0, $a1, g_ps1PerfFrames, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit
bt 12
quit
end
break perfTick if g_ps1PerfFrames % 60 == 0
commands
silent
printf "L %u cat=%d list=%d state=%d hbl=%u stgUsed=%u stgLimit=%u free=%u meshes=%u meshBytes=%u meshErr=%u | s3d draws=%u faces=%u culled=%u overflow=%u todo=%u matrix=%u gteFlag=%u | hbl xform=%u faces=%u sort=%u emit=%u | sprMissing=%u primTodo=%u inkTodo=%u overflow=%u high=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, g_ps1PerfLastHbl, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit, RSDK::dataStorage[0].storageLimit - RSDK::dataStorage[0].usedStorage * 4, g_ps1MeshLoads, g_ps1MeshBytes, g_ps1MeshErrors, g_ps1S3DDraws, g_ps1S3DFaces, g_ps1S3DCulled, g_ps1S3DOverflow, g_ps1S3DTodo, g_ps1S3DMatrix, g_ps1GteFlagged, g_ps1S3DHbl[0], g_ps1S3DHbl[1], g_ps1S3DHbl[2], g_ps1S3DHbl[3], *(unsigned *)&g_ps1SpriteMissing, *(unsigned *)&g_ps1PrimTodo, *(unsigned *)&g_ps1SpriteInkTodo, g_ps1ChainOverflow, g_ps1ChainHighWater
if g_ps1PerfFrames >= 600
  printf "L GUARD fail=%u,%u,%u,%u,%u oom=%u missing=%u stubs=%u heapHeadroom=%u arenaFail=%u ovlErrors=%u\n", g_ps1StorageFail[0], g_ps1StorageFail[1], g_ps1StorageFail[2], g_ps1StorageFail[3], g_ps1StorageFail[4], g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, (unsigned)g_ps1UnlinkedCalls, g_ps1HeapHeadroom, g_ps1ChainArenaFail, g_ps1OvlErrors
  quit
end
continue
end
continue
