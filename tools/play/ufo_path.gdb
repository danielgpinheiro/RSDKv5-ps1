# The UFO stage in and out (docs/32 7.4), `make BOOT=1,0` (GHZ1, 2 MB): at frame 240 GHZ1's first giant ring (scene
# slot 853 = entity 917) is moved in front of the player, who holds right into it: the flash, the warp, the special
# stage (UFO1: no input; its rings set to 0 at +300, so the time-over comes a second later), the results, then GHZ1 again. L lines at every load and every 120 frames;
# the guards when GHZ1 has run 300 frames after coming back.
add-symbol-file build/gen/ovl/UFO1/UFO1.elf
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
set $ufo = 0
set $back = 0
break RSDK::LoadSceneAssets
commands
silent
printf "L LOADASSETS frame=%u cat=%d list=%d folder=%s used=%u limit=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::currentSceneFolder, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit
if RSDK::sceneInfo.activeCategory == 3
  set $ufo = g_ps1PerfFrames
end
if $ufo && RSDK::sceneInfo.activeCategory == 1
  set $back = g_ps1PerfFrames
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
if RSDK::sceneInfo.activeCategory == 1 && !$ufo
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  set $r = (struct EntitySpecialRing *)RSDK::objectEntityPtr[853 + 64]
  if $f == 240
    set $r->position.x = $p->position.x + (100 << 16)
    set $r->position.y = $p->position.y - (20 << 16)
    set *$ps1pad = 0x8
  end
end
if $ufo && !$back && $f == $ufo + 2
  set *$ps1pad = 0
end
if $ufo && !$back && $f == $ufo + 300
  set var UFO_Setup->rings = 0
  printf "L RINGS0 frame=%u\n", $f
end
if $f % 120 == 0
  printf "L %u cat=%d list=%d state=%d hbl=%u stgUsed=%u stgLimit=%u | s3d draws=%u faces=%u todo=%u | floor draws=%u | sprMissing=%u overflow=%u\n", $f, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, g_ps1PerfLastHbl, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit, g_ps1S3DDraws, g_ps1S3DFaces, g_ps1S3DTodo, g_ps1FloorDraws, *(unsigned *)&g_ps1SpriteMissing, g_ps1ChainOverflow
end
if $back && $f >= $back + 300
  printf "L GUARD fail=%u,%u,%u,%u,%u oom=%u missing=%u stubs=%u heapHeadroom=%u arenaFail=%u ovlErrors=%u meshErr=%u ghostWrites=%u relocations=%u spareFail=%u\n", g_ps1StorageFail[0], g_ps1StorageFail[1], g_ps1StorageFail[2], g_ps1StorageFail[3], g_ps1StorageFail[4], g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, (unsigned)g_ps1UnlinkedCalls, g_ps1HeapHeadroom, g_ps1ChainArenaFail, g_ps1OvlErrors, g_ps1MeshErrors, RSDK::g_ps1EntityGhostWrites, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntitySpareFail
  quit
end
continue
end
continue
