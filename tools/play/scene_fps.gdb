# Speed of a scene left idle (docs/32 9e): vsyncs per engine frame over frames 300-600 (1.00 = 60 fps), two stops only
# (no per-frame condition: GDB's would slow the emulator). `make BOOT=c,s` (2 MB), make disc, tools/play_test.sh this OUT.
break perfTick if g_ps1PerfFrames == 300 || g_ps1PerfFrames == 600
commands
silent
if g_ps1PerfFrames == 300
  set $vsStart = g_ps1PerfVsyncs
end
if g_ps1PerfFrames >= 600
  printf "F %u vsyncs=%u per frame x100=%u hbl=%u\n", g_ps1PerfFrames, g_ps1PerfVsyncs - $vsStart, (g_ps1PerfVsyncs - $vsStart) * 100 / 300, g_ps1PerfLastHbl
  quit
end
continue
end
continue
