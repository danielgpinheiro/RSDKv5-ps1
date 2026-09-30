# UFO1 with the PC sampler (docs/32 7.3): `make SAMPLE=1 BOOT=3,0` (2 MB), `make disc`, then
# EMU_RAM=8mb tools/play_test.sh tools/play/ufo_sample.gdb OUT; frames 60-360 sampled into build/sample_hist.bin:
# tools/play/sample_report.py build/sample_hist.bin 300 --ovl UFO1.
break perfTick if g_ps1PerfFrames == 60 || g_ps1PerfFrames == 360
commands
silent
if g_ps1PerfFrames == 60
  set g_ps1SampleOn = 1
else
  set g_ps1SampleOn = 0
  dump binary memory build/sample_hist.bin 0x80400000 0x80600000
  quit
end
continue
end
continue
