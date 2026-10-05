set var $p->rings = 50
set var $p->blinkTimer = 240
set $t = (struct EntityTornado *)RSDK::objectEntityPtr[361]
set $dx = $p->position.x - ($t->position.x + 0x1E0000)
set $dy = $p->position.y - $t->position.y
if $ph == 10 && ($dx > 0x600000 || $dx < -0x600000 || $dy > 0x600000 || $dy < -0x600000)
  printf "L BACK f=%u x=%d y=%d tx=%d ty=%d\n", g_ps1PerfFrames, $p->position.x >> 16, $p->position.y >> 16, $t->position.x >> 16, $t->position.y >> 16
  set var $p->position.x = $t->position.x + 0x1E0000
  set var $p->position.y = $t->position.y - 0x400000
  set var $p->velocity.x = 0
  set var $p->velocity.y = 0
  set var $p->groundVel = 0
  set var $p->state = Player_State_Air
end
