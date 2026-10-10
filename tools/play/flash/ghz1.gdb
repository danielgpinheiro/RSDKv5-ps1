# docs/32 9m: GHZ1's DD Wrecker hit flash (shot.sh PRE file; BOOT=1,0), the bank-1 route: DDWrecker_Draw sets bank 1
# active on every line and, while invincibilityTimer & 2, bank 1's entries 1-27 white. The boss hook warps Sonic into the
# arena as tools/play/boss's ghz1 recipe (absolute 15830,1550, the balls pinned in slots 386 / 387); from frame 700, at
# each stop where a ball is visible its timer (EntityDDWrecker + 112; visible +85) is set to 4: the next two frames
# (3, 2) draw it white
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  set var $p->rings = 50
  if !$warp_set
    set var g_ps1TestBossClass = *(short *)DDWrecker
    set var g_ps1TestBossNth = 0
    set var g_ps1TestBossPin = 386
    set var g_ps1TestBossDX = 15830
    set var g_ps1TestBossDY = 1550
    set var g_ps1TestBossWarp = 2
    set $warp_set = 1
  end
  set $s = 386
  while $s <= 387
    set $e = (char *)RSDK::objectEntityPtr[$s]
    if g_ps1PerfFrames >= 700 && $flash_last != g_ps1PerfFrames - 1 && *(unsigned short *)($e + 54) == *(short *)DDWrecker && *(unsigned char *)($e + 85)
      set var *(int *)($e + 112) = 4
    end
    printf "L DDW f=%u slot=%d class=%d x=%d y=%d visible=%d timer=%d\n", g_ps1PerfFrames, $s, *(unsigned short *)($e + 54), *(int *)$e >> 16, *(int *)($e + 4) >> 16, *(unsigned char *)($e + 85), *(int *)($e + 112)
    set $s = $s + 1
  end
  set $flash_last = g_ps1PerfFrames
end
set $warp_set = 0
set $flash_last = 0
