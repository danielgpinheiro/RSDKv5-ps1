add-symbol-file @OVL@
break RSDK::PS1EntityRelocate
commands
silent
printf "L RELOC f=%u slot=%d size=%u\n", g_ps1PerfFrames, $a0, $a1
bt 6
continue
end
# docs/32 9l: Super Sonic through a boss fight (boss.sh EXTRA): right after the warp the player turns Super from GDB
# (no transformation, no Super music: the player group must follow the game alone), 200 rings so Super lasts the fight;
# the L B lines' miss= (missing sprite draws) must stay 0 through the boss track's start
break perfTick if $ph == 1 && g_ps1PerfFrames == $pf + 7 && g_ps1PerfFrames % 60 != 0
commands
silent
set var $p->rings = 200
set var $p->superState = 2
set var $p->aniFrames = Player->superFrames
printf "L FORCESUPER f=%u aniFrames=%d loads=%u\n", g_ps1PerfFrames, $p->aniFrames, g_ps1AtlasGroupLoads
continue
end
