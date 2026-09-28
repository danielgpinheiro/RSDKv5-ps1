set $n = 0
break PS1LayerDebugHook if g_ps1PerfFrames == REF_FRAME
commands
silent
set $li = $a0
printf "LAYER call=%d index=%d\n", $n, $li
eval "dump binary memory REF_DIR/sl_%d_%d.bin (char*)RSDK::scanlines (char*)RSDK::scanlines+3840", $n, $li
eval "dump binary memory REF_DIR/lb_%d.bin &RSDK::gfxLineBuffer[0] &RSDK::gfxLineBuffer[240]", $n
if $n == 0
  dump binary memory REF_DIR/pal.bin &RSDK::fullPalette[0][0] &RSDK::fullPalette[0][256]
  set $tb = (char*)'(anonymous namespace)::s_ps1TileBlob'
  dump binary memory REF_DIR/tiletable.bin $tb $tb+2048
end
set $n = $n + 1
continue
end
break perfTick if g_ps1PerfFrames == REF_FRAME
commands
silent
printf "FRAME bufferY=%u layers=%d bands=%u pieces=%u misses=%u sprites=%u spriteMissing=%u culled=%u inkTodo=%u fxTodo=%u unbound=%u rotated=%u tiles=%u tileMissing=%u flats=%u primTodo=%u primApprox=%u fades=%u fadeOverflow=%u drawLog=%u clutFull=%u clutRows=%u splices=%u fadeSpans=%u fadeSpanBad=%u\n", RSDK::g_ps1FrameBufferY, $n, RSDK::g_ps1LayerBands, RSDK::g_ps1LayerPieces, RSDK::g_ps1LayerMisses, *(unsigned *)&g_ps1SpriteDraws, *(unsigned *)&g_ps1SpriteMissing, *(unsigned *)&g_ps1SpriteCulled, *(unsigned *)&g_ps1SpriteInkTodo, *(unsigned *)&g_ps1SpriteFxTodo, *(unsigned *)&g_ps1SpriteUnbound, *(unsigned *)&g_ps1SpriteRotated, *(unsigned *)&g_ps1TileDraws, *(unsigned *)&g_ps1TileMissing, *(unsigned *)&g_ps1FlatDraws, *(unsigned *)&g_ps1PrimTodo, *(unsigned *)&g_ps1PrimApprox, *(unsigned *)&g_ps1FadeCalls, *(unsigned *)&g_ps1FadeOverflow, *(unsigned *)&g_ps1DrawLogCount, *(unsigned *)&g_ps1ClutFullUploads, *(unsigned *)&g_ps1ClutRowUploads, *(unsigned *)&g_ps1ClutSplices, *(unsigned *)&g_ps1FadeSpanChecked, *(unsigned *)&g_ps1FadeSpanBad
set $dl = (char *)&g_ps1DrawLog
eval "dump binary memory REF_DIR/drawlog.bin $dl $dl+%u", *(unsigned *)&g_ps1DrawLogCount * 16
set g_ps1VramDumpX = 0
set g_ps1VramDumpY = RSDK::g_ps1FrameBufferY
set g_ps1VramDumpW = 320
set g_ps1VramDumpH = 240
set g_ps1VramDumpReq = 1
continue
end
break PS1VramDumpHook
commands
silent
dump binary memory REF_DIR/fb.bin &g_ps1VramDumpBuf[0] &g_ps1VramDumpBuf[76800]
printf "DONE\n"
detach
quit
end
continue
