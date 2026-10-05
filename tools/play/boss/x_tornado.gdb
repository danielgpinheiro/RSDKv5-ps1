add-symbol-file @OVL@
break Player_State_Death
commands
silent
set $q = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
set $t = (struct EntityTornado *)RSDK::objectEntityPtr[361]
printf "L SAVE f=%u x=%d y=%d tx=%d ty=%d\n", g_ps1PerfFrames, $q->position.x >> 16, $q->position.y >> 16, $t->position.x >> 16, $t->position.y >> 16
set var $q->deathType = 0
set var $q->visible = 1
set var $q->position.x = $t->position.x + 0x1E0000
set var $q->position.y = $t->position.y - 0x400000
set var $q->velocity.x = 0
set var $q->velocity.y = 0
set var $q->groundVel = 0
set var $q->state = Player_State_Air
continue
end
