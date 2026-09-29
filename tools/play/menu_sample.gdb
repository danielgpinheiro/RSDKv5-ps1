# Menu CPU profile (docs/32 4.2e): `make SAMPLE=1 BOOT=0,2` (2 MB), `make disc`, then
# EMU_RAM=8mb tools/play_test.sh tools/play/menu_sample.gdb OUT; the Main Menu's frames 300-600 (A at 200 closes the
# autosave notice) land in build/sample_hist.bin: tools/play/sample_report.py build/sample_hist.bin 300 --ovl Menu.
set $ps1pad = (unsigned short *)&g_ps1PadTestMask
break RSDK::ProcessInput if g_ps1PerfFrames == 200 || g_ps1PerfFrames == 204
commands
silent
if g_ps1PerfFrames == 200
  set *$ps1pad = 0x10
else
  set *$ps1pad = 0
end
continue
end
break perfTick if g_ps1PerfFrames == 300 || g_ps1PerfFrames == 600 || g_ps1PerfFrames % 50 == 0
commands
silent
if g_ps1PerfFrames == 300
  set g_ps1SampleOn = 1
end
printf "F %u hbl=%u flats=%u sprites=%u chainWords=%u\n", g_ps1PerfFrames, g_ps1PerfLastHbl, *(unsigned *)&g_ps1FlatDraws, *(unsigned *)&g_ps1SpriteDraws, g_ps1ChainLastWords
if g_ps1PerfFrames >= 600
  set g_ps1SampleOn = 0
  printf "SAMPLES count=%u period=%u\n", g_ps1SampleCount, g_ps1SamplePeriod
  dump binary memory build/sample_hist.bin 0x80400000 0x80600000
  quit
end
continue
end
continue
