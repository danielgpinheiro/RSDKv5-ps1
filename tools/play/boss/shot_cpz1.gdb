# docs/ports/mania/32 9b.6b: CPZ1's AmoebaDroid with its big blob on screen (shot.sh PRE file, BOOT=1,2): the boss hook
# warps Sonic as the cpz1 recipe does (40 px right, 100 px up of the droid), for pictures of the blob over the droid
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  set var $p->rings = 50
  if !$warp_set
    set var g_ps1TestBossClass = *(short *)AmoebaDroid
    set var g_ps1TestBossNth = 0
    set var g_ps1TestBossPin = -1
    set var g_ps1TestBossDX = 40
    set var g_ps1TestBossDY = -100
    set var g_ps1TestBossWarp = 1
    set $warp_set = 1
  end
  printf "L SHOTB f=%u slot=%d missing=%u fxtodo=%u\n", g_ps1PerfFrames, g_ps1TestBossSlot, *(unsigned *)&g_ps1SpriteMissing, *(unsigned *)&g_ps1SpriteFxTodo
end
set $warp_set = 0
