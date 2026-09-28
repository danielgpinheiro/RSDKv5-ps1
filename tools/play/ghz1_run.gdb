# GHZ1 scripted run (docs/32 2.5): `make MEASURE_RAM=8M BOOT=1,0`, then tools/play_test.sh tools/play/ghz1_run.gdb OUT
# (compare OUT.txt with tools/play/ghz1_run.ref.txt: the F lines without hbl=; L lines = layer cost and chain use).
# Pad bits (g_ps1PadTestMask, ORed into pad 1): 0 Up 1 Down 2 Left 3 Right 4 A (jump) 5 B 6 C 7 X 8 Y 9 Z 10 Start 11 Select.
# Right from frame 240; jumps at 316 (the ring row at x 324-372) and 412 (the Motobug at x 832). Samples every 15 frames.
set $peakTemp = 0
break perfTick if g_ps1PerfFrames % 15 == 0 || g_ps1PerfFrames == 240 || g_ps1PerfFrames == 316 || g_ps1PerfFrames == 330 || g_ps1PerfFrames == 412 || g_ps1PerfFrames == 426
commands
silent
if g_ps1PerfFrames == 240
  set g_ps1PadTestMask = 0x8
end
if g_ps1PerfFrames == 316 || g_ps1PerfFrames == 412
  set g_ps1PadTestMask = 0x18
end
if g_ps1PerfFrames == 330 || g_ps1PerfFrames == 426
  set g_ps1PadTestMask = 0x8
end
if g_ps1PerfFrames % 15 == 0
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  set $t = 0
  set $s = 0x840
  while $s < 0x940
    if ((RSDK::EntityBase*)RSDK::objectEntityPtr[$s])->classID != 0
      set $t = $t + 1
    end
    set $s = $s + 1
  end
  if $t > $peakTemp
    set $peakTemp = $t
  end
  printf "F %u state=%d x=%d y=%d gv=%d vx=%d vy=%d ground=%d rings=%d time=%u:%02u.%02u hbl=%u temp=%d peakTemp=%d pad=%x\n", g_ps1PerfFrames, RSDK::sceneInfo.state, $p->position.x >> 16, $p->position.y >> 16, $p->groundVel, $p->velocity.x, $p->velocity.y, $p->onGround, $p->rings, RSDK::sceneInfo.minutes, RSDK::sceneInfo.seconds, RSDK::sceneInfo.milliseconds, g_ps1PerfLastHbl, $t, $peakTemp, g_ps1PadTestMask
  printf "L %u layerHbl=%u bands=%u pieces=%u solidRuns=%u rowTables=%u uniform=%u misses=%u chainWords=%u chainHigh=%u overflow=%u idleWaits=%u idleWaitHbl=%u arenaFail=%u sprites=%u spriteMissing=%u inkTodo=%u fxTodo=%u unbound=%u rotated=%u tiles=%u tileMissing=%u flats=%u primTodo=%u primApprox=%u fades=%u fadeOverflow=%u\n", g_ps1PerfFrames, RSDK::g_ps1LayerHbl, RSDK::g_ps1LayerBands, RSDK::g_ps1LayerPieces, RSDK::g_ps1LayerSolidRuns, RSDK::g_ps1LayerRowTables, RSDK::g_ps1LayerUniform, RSDK::g_ps1LayerMisses, g_ps1ChainLastWords, g_ps1ChainHighWater, g_ps1ChainOverflow, g_ps1ChainIdleWaits, g_ps1ChainIdleWaitHbl, g_ps1ChainArenaFail, *(unsigned *)&g_ps1SpriteDraws, *(unsigned *)&g_ps1SpriteMissing, *(unsigned *)&g_ps1SpriteInkTodo, *(unsigned *)&g_ps1SpriteFxTodo, *(unsigned *)&g_ps1SpriteUnbound, *(unsigned *)&g_ps1SpriteRotated, *(unsigned *)&g_ps1TileDraws, *(unsigned *)&g_ps1TileMissing, *(unsigned *)&g_ps1FlatDraws, *(unsigned *)&g_ps1PrimTodo, *(unsigned *)&g_ps1PrimApprox, *(unsigned *)&g_ps1FadeCalls, *(unsigned *)&g_ps1FadeOverflow
end
if g_ps1PerfFrames >= 900
  printf "GUARDS stageObjectsMissing=%u stubs=%u createSkips=%u relocations=%u spareFail=%u ghostWrites=%u chunkOverflow=%u chunkPrivate=%u forcedDefrag=%u atl=%d\n", RSDK::g_ps1StageObjectsMissing, g_ps1UnlinkedCalls, RSDK::g_ps1EntityCreateSkips, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntitySpareFail, RSDK::g_ps1EntityGhostWrites, RSDK::g_ps1ChunkOverflow, RSDK::g_ps1ChunkPrivatePeak, g_ps1StorageForcedDefrags[0], ps1AtlDropped
  quit
end
continue
end
continue
