# docs/32 9m: SSZ1's HotaruHiWatt on its hit-flash frames (shot.sh PRE file; BOOT=1,10). At the first stop the boss hook
# warps Sonic to the boss (as tools/play/boss's ssz1 recipe) and holds right; from frame 500, at each stop the boss's
# invincibilityTimer (EntityHotaruHiWatt + 108, from the SSZ1 overlay's DWARF) is set to 48 while it is visible (+85; in
# the Hotaru minions' phases the boss is hidden): the next frame's update makes
# it 47 (odd: HotaruHiWatt_Draw copies bank 2's entries 240-255 over bank 0's around its draws), the one after 46
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  if !$flash_set
    set var $p->rings = 50
    set var g_ps1TestBossClass = *(short *)HotaruHiWatt
    set var g_ps1TestBossNth = 0
    set var g_ps1TestBossPin = -1
    set var g_ps1TestBossDX = -120
    set var g_ps1TestBossDY = -80
    set var g_ps1TestBossWarp = 1
    set var g_ps1PadTestMask = 8
    set $flash_set = 1
  end
  if g_ps1PerfFrames >= 500 && g_ps1TestBossSlot >= 0 && $flash_last != g_ps1PerfFrames - 1 && *(unsigned char *)((char *)RSDK::objectEntityPtr[g_ps1TestBossSlot] + 85)
    set var *(int *)((char *)RSDK::objectEntityPtr[g_ps1TestBossSlot] + 108) = 48
    set var g_ps1PadTestMask = 0
    set var $p->rings = 50
  end
  set $flash_last = g_ps1PerfFrames
  printf "L FLASH f=%u slot=%d timer=%d pal0=%04x pal1=%04x pal2=%04x draw=%x visible=%d\n", g_ps1PerfFrames, g_ps1TestBossSlot, g_ps1TestBossSlot >= 0 ? *(int *)((char *)RSDK::objectEntityPtr[g_ps1TestBossSlot] + 108) : -1, RSDK::fullPalette[0][244], RSDK::fullPalette[1][244], RSDK::fullPalette[2][244], g_ps1TestBossSlot >= 0 ? *(unsigned *)((char *)RSDK::objectEntityPtr[g_ps1TestBossSlot] + 96) : 0, g_ps1TestBossSlot >= 0 ? *(unsigned char *)((char *)RSDK::objectEntityPtr[g_ps1TestBossSlot] + 85) : -1
end
set $flash_set = 0
set $flash_last = 0
