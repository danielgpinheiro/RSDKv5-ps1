# docs/32 9i: PSZ1's Shiversaw on screen (shot.sh PRE file; BOOT=1,8): the boss hook warps Sonic as the psz1 recipe
# (80 px left, 40 px down of the boss) and holds right a moment, for pictures of the boss at the size the atlas gives it
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  set var $p->rings = 50
  if !$warp_set
    set var g_ps1TestBossClass = *(short *)Shiversaw
    set var g_ps1TestBossNth = 0
    set var g_ps1TestBossPin = -1
    set var g_ps1TestBossDX = -80
    set var g_ps1TestBossDY = 40
    set var g_ps1TestBossWarp = 1
    set $warp_set = 1
  end
  printf "L SHOTB f=%u slot=%d missing=%u loads=%u\n", g_ps1PerfFrames, g_ps1TestBossSlot, *(unsigned *)&g_ps1SpriteMissing, g_ps1AtlasGroupLoads
end
set $warp_set = 0
