# docs/ports/mania/32 63 M3b: Chemical Plant 2's bubble shield over Sonic at 1:1 (shot.sh PRE file, BOOT=1,3): the
# player is warped onto the act's own bubble-shield monitor (Scene2.bin, ItemBox type 2 at 5232, 4352), which the touch
# breaks and grants the shield, so the picture shows the shield's own frames from the atlas
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  if g_ps1PerfFrames == 240
    set var $p->position.x = 5232 << 16
    set var $p->position.y = 4300 << 16
    set var $p->velocity.x = 0
    set var $p->velocity.y = 0
  end
  printf "L SH f=%u shield=%d missing=%u\n", g_ps1PerfFrames, $p->shield, *(unsigned *)&g_ps1SpriteMissing
end
