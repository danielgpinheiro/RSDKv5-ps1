# Slot census at one point of an act (the update scan's work): player 1 (invulnerable) warped to x = $sX, y = $sY at
# frame 300 (Zone.c's test warp, as sample_at.gdb); at frame 400 the RAM is dumped to $ramfile (8 MB link: EMU_RAM=8mb)
# with the layout lines tools/play/slot_census.py needs ("O ..."). Copy with $sX / $sY / $ramfile set first.
break perfTick if g_ps1PerfFrames == 300 || g_ps1PerfFrames == 330 || g_ps1PerfFrames == 400
commands
silent
set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
set var $p->blinkTimer = 240
set var $p->rings = 50
if g_ps1PerfFrames == 300
  set var g_ps1TestBossDX = $sX
  set var g_ps1TestBossDY = $sY
  set var g_ps1TestBossWarp = 2
  set var g_ps1TestBossClass = 0xFFF0
end
if g_ps1PerfFrames == 330
  set var g_ps1TestBossClass = 0
end
printf "F %u hbl=%u x=%d y=%d\n", g_ps1PerfFrames, g_ps1PerfLastHbl, $p->position.x >> 16, $p->position.y >> 16
if g_ps1PerfFrames >= 400
  printf "O ptrs=%u n=%u send=%u tend=%u live=%u ids=%u ocl=%u osz=%u oup=%u ccount=%u\n", &RSDK::objectEntityPtr[0], sizeof(RSDK::objectEntityPtr) / 4, RSDK::ps1SceneSlotEnd, RSDK::ps1TempSlotEnd, &RSDK::ps1LiveBits[0], &RSDK::stageObjectIDs[0], &RSDK::objectClassList[0], sizeof(RSDK::objectClassList[0]), &((RSDK::ObjectClass *)0)->update, RSDK::sceneInfo.classCount
  printf "O oA=%u oC=%u oI=%u oR=%u oInt=%u\n", &((RSDK::Entity *)0)->active, &((RSDK::Entity *)0)->classID, &((RSDK::Entity *)0)->inRange, &((RSDK::Entity *)0)->updateRange, &((RSDK::Entity *)0)->interaction
  printf "CAMS n=%d\n", RSDK::cameraCount
  set $ci = 0
  while $ci < RSDK::cameraCount
    printf "CAM %d pos=%d,%d off=%d,%d\n", $ci, RSDK::cameras[$ci].position.x >> 16, RSDK::cameras[$ci].position.y >> 16, RSDK::cameras[$ci].offset.x >> 16, RSDK::cameras[$ci].offset.y >> 16
    set $ci = $ci + 1
  end
  eval "dump binary memory %s 0x80000000 0x80800000", $ramfile
  quit
end
continue
end
continue
