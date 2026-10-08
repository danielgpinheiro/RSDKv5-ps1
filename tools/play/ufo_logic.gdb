# docs/32 9f (UFO 60 fps plan): a UFO stage's logic state for A/B checks of non-visual changes. Run after the pad script
# lines of tools/play/ufo_min.sh (SETS); every 120 frames from 120: player 1's entity (64 words) and, over every slot,
# a checksum of each entity's first 8 words (position, scale, velocity, update range). Two builds: diff the L lines.
break perfTick if g_ps1PerfFrames % 120 == 0 && g_ps1PerfFrames >= 120
commands
silent
set $e = (unsigned *)RSDK::objectEntityPtr[0]
printf "L P %u", g_ps1PerfFrames
set $i = 0
while $i < 64
  printf " %x", $e[$i]
  set $i = $i + 1
end
printf "\n"
set $sum = 0
set $s = 0
while $s < 2224
  set $b = (unsigned *)RSDK::objectEntityPtr[$s]
  set $k = 0
  while $k < 8
    set $sum = ($sum * 31 + $b[$k]) & 0xFFFFFFFF
    set $k = $k + 1
  end
  set $s = $s + 1
end
printf "L SUM %u %x\n", g_ps1PerfFrames, $sum
if g_ps1PerfFrames >= 1320
  quit
end
continue
end
