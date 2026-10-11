# docs/32 9q: PC-sampler profile at one point of an act (`make SAMPLE=1 BOOT=1,s [CAP=1]`, make disc, EMU_RAM=8mb
# tools/play_test.sh <a copy with $sX / $sY set first> OUT): player 1 (invulnerable) warped to x = $sX, y = $sY at frame
# 300 (Zone.c's test warp, as sweep_timing.gdb), frames 400-700 sampled into build/sample_hist.bin;
# tools/play/sample_report.py build/sample_hist.bin 300 --ovl FOLDER.
break perfTick if g_ps1PerfFrames == 300 || g_ps1PerfFrames == 330 || g_ps1PerfFrames == 400 || g_ps1PerfFrames == 550 || g_ps1PerfFrames == 700
commands
silent
set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
set var $p->blinkTimer = 240
set var $p->rings = 50
if g_ps1PerfFrames == 300
  set var g_ps1TestBossDX = $sX
  set var g_ps1TestBossDY = $sY
  set var g_ps1TestBossWarp = 2
  set var g_ps1TestBossClass = 0xFFF0
end
if g_ps1PerfFrames == 330
  set var g_ps1TestBossClass = 0
end
if g_ps1PerfFrames == 400
  set g_ps1SampleOn = 1
end
printf "F %u hbl=%u x=%d y=%d\n", g_ps1PerfFrames, g_ps1PerfLastHbl, $p->position.x >> 16, $p->position.y >> 16
if g_ps1PerfFrames >= 700
  set g_ps1SampleOn = 0
  printf "SAMPLES count=%u period=%u\n", g_ps1SampleCount, g_ps1SamplePeriod
  dump binary memory build/sample_hist.bin 0x80400000 0x80600000
  quit
end
continue
end
continue
