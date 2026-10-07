# PC-sampler profile of a scene left idle (docs/32 9e): `make SAMPLE=1 BOOT=c,s` (2 MB), make disc, then
# EMU_RAM=8mb tools/play_test.sh tools/play/scene_sample.gdb OUT; frames 300-600 land in build/sample_hist.bin:
# tools/play/sample_report.py build/sample_hist.bin 300 --ovl FOLDER.
break perfTick if g_ps1PerfFrames == 300 || g_ps1PerfFrames == 600
commands
silent
if g_ps1PerfFrames == 300
  set g_ps1SampleOn = 1
end
printf "F %u hbl=%u\n", g_ps1PerfFrames, g_ps1PerfLastHbl
if g_ps1PerfFrames >= 600
  set g_ps1SampleOn = 0
  printf "SAMPLES count=%u period=%u\n", g_ps1SampleCount, g_ps1SamplePeriod
  dump binary memory build/sample_hist.bin 0x80400000 0x80600000
  quit
end
continue
end
continue
