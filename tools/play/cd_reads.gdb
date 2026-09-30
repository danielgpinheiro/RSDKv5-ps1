# One CD action at a time (docs/32 5.3): every file read that stops the music (xa_music.cpp PS1XaStopForFileRead),
# with the file and the callers. `make BOOT=7,0` (AIZ intro, runs without input; 7,2 GHZCutscene) or any BOOT=; L lines every 60 frames.
break xa_music.cpp:356
commands
silent
up 1
printf "L FORCED frame=%u path=%s\n", g_ps1PerfFrames, path
bt 10
continue
end
break RSDK::PS1StreamStart
commands
silent
printf "L PLAYSTREAM frame=%u %s\n", g_ps1PerfFrames, streamFilePath
continue
end
break perfTick if g_ps1PerfFrames % 60 == 0
commands
silent
printf "L %u cat=%d list=%d hbl=%u forced=%u resumes=%u track=%d\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, g_ps1PerfLastHbl, g_ps1XaForcedStops, g_ps1MusicResumes, g_ps1XaTrack
if g_ps1PerfFrames >= 2000
  quit
end
continue
end
continue
