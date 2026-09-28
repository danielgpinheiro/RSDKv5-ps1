# GHZ1 scripted run with the PC sampler (docs/32 3.3e): `make SAMPLE=1 BOOT=1,0` (2 MB), `make disc`, then
# EMU_RAM=8mb tools/play_test.sh tools/play/ghz1_sample.gdb OUT; the counters of frames 240-900 land in
# build/sample_hist.bin: tools/play/sample_report.py build/sample_hist.bin 660 [--lines DrawRowBands].
# The inputs of tools/play/ghz1_run.gdb (the same game state; sampling adds IRQ time, so hbl= are higher).
set $peakTemp = 0
break perfTick if g_ps1PerfFrames % 15 == 0 || g_ps1PerfFrames == 240 || g_ps1PerfFrames == 316 || g_ps1PerfFrames == 330 || g_ps1PerfFrames == 412 || g_ps1PerfFrames == 426
commands
silent
if g_ps1PerfFrames == 240
  set g_ps1PadTestMask = 0x8
  set g_ps1SampleOn = 1
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
  set g_ps1SampleOn = 0
  printf "SAMPLES count=%u period=%u\n", g_ps1SampleCount, g_ps1SamplePeriod
  dump binary memory build/sample_hist.bin 0x80400000 0x80600000
  printf "GUARDS stageObjectsMissing=%u stubs=%u createSkips=%u relocations=%u spareFail=%u ghostWrites=%u chunkOverflow=%u chunkPrivate=%u forcedDefrag=%u atl=%d\n", RSDK::g_ps1StageObjectsMissing, g_ps1UnlinkedCalls, RSDK::g_ps1EntityCreateSkips, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntitySpareFail, RSDK::g_ps1EntityGhostWrites, RSDK::g_ps1ChunkOverflow, RSDK::g_ps1ChunkPrivatePeak, g_ps1StorageForcedDefrags[0], ps1AtlDropped
  quit
end
continue
end
continue
