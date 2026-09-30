# Title intro video (docs/32 6.3), `make BOOT=0,1`: the Title idles into its intro (TitleSetup: 800 frames, the fade,
# LoadScene + PlayStream + LoadVideo), the STR sequence plays to its end and the Title loads again. The player lives in
# the Title's overlay: its symbols come from build/gen/ovl/Title/Title.elf. L lines every 150 frames; the guards at END.
add-symbol-file build/gen/ovl/Title/Title.elf
set $end = 6600
break RSDK::LoadVideo
commands
silent
printf "L LOADVIDEO frame=%u %s state=%d\n", g_ps1PerfFrames, $a0, RSDK::sceneInfo.state
continue
end
break psyqo::Kernel::Internal::abort
commands
silent
printf "L ABORT %s frame=%u\n", (char*)$a0, g_ps1PerfFrames
bt 12
quit
end
break perfTick if g_ps1PerfFrames % 150 == 0
commands
silent
printf "L %u state=%d list=%d hbl=%u | video done=%u missing=%u tail=%u fmvPlays=%u fails=%u frames=%u pics=%u dropped=%u mdecFaults=%u mode=%u timeouts=%u | xa forced=%u starts=%u track=%d\n", g_ps1PerfFrames, RSDK::sceneInfo.state, RSDK::sceneInfo.listPos, g_ps1PerfLastHbl, g_ps1VideoDone, g_ps1VideoMissing, g_ps1VideoTailBytes, g_ps1FmvPlays, g_ps1FmvFails, g_ps1FmvFrames, g_ps1VideoPictures, g_ps1VideoDropped, g_ps1MdecFaults, g_ps1MdecMode, g_ps1FmvTimeouts, g_ps1XaForcedStops, g_ps1XaStarts, g_ps1XaTrack
if g_ps1PerfFrames >= $end
  printf "L GUARD fail=%u,%u,%u,%u,%u oom=%u stubs=%u heapHeadroom=%u stgLimit=%u arenaFail=%u chainOverflow=%u ovlErrors=%u\n", g_ps1StorageFail[0], g_ps1StorageFail[1], g_ps1StorageFail[2], g_ps1StorageFail[3], g_ps1StorageFail[4], g_ps1OutOfMemory, (unsigned)g_ps1UnlinkedCalls, g_ps1HeapHeadroom, g_ps1StgLimit, g_ps1ChainArenaFail, g_ps1ChainOverflow, g_ps1OvlErrors
  quit
end
continue
end
continue
