# docs/32 9r: SPZ1 silhouette window 1758 (shot.sh NAME 1,4 300,360,362 tools/play/sil_window.gdb): the player warped onto
# its yellow panel at frame 300 with a blue shield ($shield 0: none); shot frames must include 300 (the warp stop)
set $shield = 1
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  if g_ps1PerfFrames == 300
    set var g_ps1TestBossDX = 880
    set var g_ps1TestBossDY = 780
    set var g_ps1TestBossWarp = 2
    set var g_ps1TestBossClass = 0xFFF0
    if $shield
      set var $p->shield = 1
      set var $p->invincibleTimer = 2
    end
    set var g_ps1TestStops[0] = 304
  end
  if g_ps1PerfFrames == 304
    set var g_ps1TestBossClass = 0
  end
  printf "L P f=%u x=%d y=%d shield=%d todo=%u unmasked=%u\n", g_ps1PerfFrames, $p->position.x >> 16, $p->position.y >> 16, $p->shield, *(unsigned *)&g_ps1PrimTodo, *(unsigned *)&'(anonymous namespace)::g_ps1UnmaskedRects'
end
