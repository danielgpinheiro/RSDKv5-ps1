# Music loop check (docs/32 5.2), `make BOOT=1,0`: GHZ1's track (1,050 rows in Music.bin) starts 100 rows (5.3 s) before its end, so it reaches its
# end marker and loops back to its loop row (GreenHill1.ogg: 776,160 samples). L lines every 20 frames: the channel's
# position (44.1 kHz samples), loops, the XA state.
break RSDK::PS1StreamStart
commands
silent
set var streamStartPos = (1050 - 100) * 2352
printf "L START %s startPos=%u\n", streamFilePath, streamStartPos
continue
end
break perfTick if g_ps1PerfFrames % 20 == 0
commands
silent
printf "L %u pos=%d loops=%u ends=%u starts=%u resumes=%u forced=%u dataSectors=%u loopFrame=%u state=%d\n", g_ps1PerfFrames, RSDK::channels[0].bufferPos, g_ps1XaLoops, g_ps1XaEnds, g_ps1XaStarts, g_ps1MusicResumes, g_ps1XaForcedStops, g_ps1XaDataSectors, g_ps1XaLoopFrame, RSDK::channels[0].state
if g_ps1PerfFrames >= 600
  quit
end
continue
end
continue
