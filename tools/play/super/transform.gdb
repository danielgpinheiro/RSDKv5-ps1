# docs/32 9l: a scripted Super transformation (shot.sh PRE file): at the first stop Sonic gets 50 rings and the seven
# emeralds; jump is held at the stops 300-304 and Y (Plus: the Super button, Triangle) pressed in the air at 310-313 (pad test mask, set at those
# stops: shot.sh's frame list must hold 290,300,304,310,313): Player_TryTransform. Every stop
# prints the player's state, its sprite group and the missing sprite draws (must not grow from the jump on)
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  if !$super_set
    set var $p->rings = 50
    set var ((SaveRAM *)SaveGame->saveRAM)->collectedEmeralds = 0x7F
    set $super_set = 1
  end
  if g_ps1PerfFrames == 300
    set var g_ps1PadTestMask = 0x10
  end
  if g_ps1PerfFrames == 310
    set var g_ps1PadTestMask = 0x100
  end
  if g_ps1PerfFrames == 304 || g_ps1PerfFrames == 313
    set var g_ps1PadTestMask = 0
  end
  printf "L SUPER f=%u superState=%d anim=%d frame=%d aniFrames=%d rings=%d groupLoads=%u spriteMissing=%u iconShown=%u\n", g_ps1PerfFrames, $p->superState, $p->animator.animationID, $p->animator.frameID, $p->aniFrames, $p->rings, g_ps1AtlasGroupLoads, *(unsigned *)&g_ps1SpriteMissing, g_ps1LoadingShown
end
set $super_set = 0
