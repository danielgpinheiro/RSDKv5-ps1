# docs/ports/mania/32 63 M3b: Chemical Plant 1's fire shield over Sonic at 1:1 (shot.sh PRE file, BOOT=1,2): the player is
# warped onto the act's own fire-shield monitor (Scene1.bin, ItemBox type 3 at 11248, 1264), which the touch breaks and
# grants the shield, so the picture shows the shield's own frames from the atlas
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  if g_ps1PerfFrames == 240
    set var $p->position.x = 11248 << 16
    set var $p->position.y = 1212 << 16
    set var $p->velocity.x = 0
    set var $p->velocity.y = 0
  end
  printf "L SH f=%u shield=%d missing=%u\n", g_ps1PerfFrames, $p->shield, *(unsigned *)&g_ps1SpriteMissing
end
