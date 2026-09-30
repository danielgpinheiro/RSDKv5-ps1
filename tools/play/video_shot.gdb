# (replace SHOT_DIR before running) Intro video picture + skip (docs/32 6.3), 8 MB measuring link (VRAM read-back): `make MEASURE_RAM=8M BOOT=0,1`. Both
# framebuffers 450 frames into the video (SHOT_DIR/v6_0.bin, v6_256.bin: 320x240 BGR555), A pressed at +600 (the skip:
# fade, the Title again). L lines every 30 frames around it.
add-symbol-file build/gen/ovl/Title/Title.elf
set $v = 0
break RSDK::LoadVideo
commands
silent
set $v = g_ps1PerfFrames
printf "L LOADVIDEO frame=%u %s\n", g_ps1PerfFrames, $a0
continue
end
break PS1VramDumpHook
commands
silent
printf "L DUMP y=%d frame=%u\n", g_ps1VramDumpY, g_ps1PerfFrames
eval "dump binary memory SHOT_DIR/v6_%d.bin g_ps1VramDumpBuf ((char*)g_ps1VramDumpBuf)+153600", g_ps1VramDumpY
continue
end
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
break RSDK::ProcessInput if $v && (g_ps1PerfFrames == $v + 600 || g_ps1PerfFrames == $v + 604)
commands
silent
set *$ps1pad = (g_ps1PerfFrames == $v + 600) ? 0x10 : 0
continue
end
break perfTick if $v && (g_ps1PerfFrames - $v) % 30 == 0
commands
silent
set $d = g_ps1PerfFrames - $v
if $d == 450
  set var g_ps1VramDumpX = 0
  set var g_ps1VramDumpY = 0
  set var g_ps1VramDumpW = 320
  set var g_ps1VramDumpH = 240
  set var g_ps1VramDumpReq = 1
end
if $d == 480
  set var g_ps1VramDumpY = 256
  set var g_ps1VramDumpReq = 1
end
printf "L +%u state=%d done=%u pics=%u skipped=%u faults=%u mode=%u tail=%u\n", $d, RSDK::sceneInfo.state, g_ps1VideoDone, g_ps1VideoPictures, g_ps1FmvSkipped, g_ps1MdecFaults, g_ps1MdecMode, g_ps1VideoTailBytes
if $d >= 780
  quit
end
continue
end
continue
