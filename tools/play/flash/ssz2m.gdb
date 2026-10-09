# docs/32 9m: Metal Sonic's hit flash (shot.sh PRE file; BOOT=1,12 = SSZ2M, the race and the fight). Sonic holds right
# with 50 rings; the first stop finds Metal Sonic's slot; at each later stop where he is visible and on screen his
# invincibilityTimer (EntityMetalSonic + 148, the SSZ2 overlays' DWARF; visible +85, onScreen +86) is set to 48: the next
# frame's update makes it 47 and MetalSonic_Draw copies bank 2's entries 240-247 over bank 0's around his draws
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  set var $p->rings = 50
  set var g_ps1PadTestMask = 8
  if $ms < 0
    set $i = 0
    while $i < 2368 && $ms < 0
      set $e = (char *)RSDK::objectEntityPtr[$i]
      if $e && *(unsigned short *)($e + 54) == *(short *)MetalSonic
        set $ms = $i
      end
      set $i = $i + 1
    end
  end
  if $ms >= 0
    set $e = (char *)RSDK::objectEntityPtr[$ms]
    if $flash_last != g_ps1PerfFrames - 1 && *(unsigned char *)($e + 85) && *(unsigned char *)($e + 86)
      set var *(int *)($e + 148) = 48
    end
    printf "L MS f=%u slot=%d x=%d y=%d visible=%d onScreen=%d timer=%d\n", g_ps1PerfFrames, $ms, *(int *)$e >> 16, *(int *)($e + 4) >> 16, *(unsigned char *)($e + 85), *(unsigned char *)($e + 86), *(int *)($e + 148)
  end
  set $flash_last = g_ps1PerfFrames
end
set $ms = -1
set $flash_last = 0
