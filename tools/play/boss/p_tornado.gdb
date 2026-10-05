set var $p->rings = 50
set var $p->blinkTimer = 240
set $t = (struct EntityTornado *)RSDK::objectEntityPtr[361]
if $ph == 10 && $t->state == Tornado_State_FlyAway_Left && $p->position.x < 0x24B10000
  printf "L TRAIN f=%u x=%d y=%d -> catch node\n", g_ps1PerfFrames, $p->position.x >> 16, $p->position.y >> 16
  set var $p->position.x = 0x24EA0000
  set var $p->position.y = 0x05140000
  set var $p->velocity.x = 0
  set var $p->velocity.y = -0x68000
  set var $p->groundVel = 0
  set var $p->state = Player_State_Air
end
