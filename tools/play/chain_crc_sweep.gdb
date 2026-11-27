# docs/perf/80: an act swept for tools/play/chain_crc.sh (sweep_profile.gdb's warps without the profile, and without a
# breakpoint every frame: GDB stops only at the frames g_ps1TestStops asks for). From frame 300, every 150 frames player
# 1 (invulnerable, 50 rings) is warped to x = $swX0 + k * $swStep, y = $swY; the run quits past $swEnd. Frames 90-149 after
# each warp are timed: "S x=... cpu=mean max=" (CPU hblanks a frame; the hashing build's own cost included).
set $swK = 0
set $swW = 0
tbreak main
commands
silent
set var g_ps1TestStops[0] = 300
continue
end
break ps1TestStop
commands
silent
set $swF = g_ps1PerfFrames
set $swP = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
set var $swP->blinkTimer = 240
set var $swP->rings = 50
if $swF % 150 == 90
  set var g_ps1TestBossClass = 0
  set var g_ps1TestStops[0] = $swF + 60
  set var g_ps1PerfSumHbl = 0
  set var g_ps1PerfMaxHbl = 0
  set $swW = 1
else
  if $swW
    printf "S x=%d cpu=%u max=%u\n", $swX0 + ($swK - 1) * $swStep, g_ps1PerfSumHbl / 60, g_ps1PerfMaxHbl
    set $swW = 0
  end
  if $swX0 + $swK * $swStep > $swEnd
    printf "L SWEEPEND f=%u\n", $swF
    quit
  end
  set var g_ps1TestBossDX = $swX0 + $swK * $swStep
  set var g_ps1TestBossDY = $swY
  set var g_ps1TestBossWarp = 2
  set var g_ps1TestBossClass = 0xFFF0
  set $swK = $swK + 1
  set var g_ps1TestStops[0] = $swF + 90
end
continue
end
