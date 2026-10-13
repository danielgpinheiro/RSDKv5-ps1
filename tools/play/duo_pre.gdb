# docs/32 phase 13 M5: Sonic & Tails in a BOOT= build (no menu): globals->playerID (offset 4) = Sonic + Tails assist when
# the stage loads; Player_StageLoad keeps it only with the extras and the 2 MB VRAM mod. ps1_onstop prints player 2.
break Player_StageLoad
commands
silent
set var *(int *)((char *)globals + 4) = 0x201
continue
end
define ps1_onstop
  set $p1 = (RSDK::EntityBase *)RSDK::objectEntityPtr[0]
  set $p2 = (RSDK::EntityBase *)RSDK::objectEntityPtr[1]
  printf "L DUO f=%u playerID=%x p2class=%d p1x=%d p2x=%d p2y=%d missing=%u atx=%u relocations=%u\n", *(unsigned *)&g_ps1PerfFrames, *(int *)((char *)globals + 4), $p2->classID, $p1->position.x >> 16, $p2->position.x >> 16, $p2->position.y >> 16, *(unsigned *)&g_ps1SpriteMissing, *(unsigned *)&g_ps1AtlasAtxLoads, *(unsigned *)&RSDK::g_ps1EntityRelocations
end
