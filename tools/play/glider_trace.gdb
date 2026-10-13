add-symbol-file @OVL@
# docs/32 9s: FBZ2's outro (FBZ2Outro_Cutscene_RunToGlider, every logic step while it runs): every 8th call both players
# and the two hang gliders (runtime slots 1941 / 1942: tools/play/boss/slotmap.py FBZ 2 HangGlider); a G line when a
# glider takes a player (HangGlider_State_Glide's first step for it)
set $gn = 0
set $hc = 0
break HangGlider_State_CheckGrab
commands
silent
set $hc = $hc + 1
continue
end
break FBZ2Outro_Cutscene_RunToGlider
commands
silent
set language c++
set $gn = $gn + 1
set $q1 = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
if $gn % 8 == 1 || (($q1->position.x >> 16) > 15640 && ($q1->position.x >> 16) < 15780)
  set $q1 = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  set $q2 = (struct EntityPlayer *)RSDK::objectEntityPtr[1]
  set $h1 = (RSDK::EntityBase *)RSDK::objectEntityPtr[1941]
  set $h2 = (RSDK::EntityBase *)RSDK::objectEntityPtr[1942]
  printf "L GT f=%u n=%d grabChecks=%d p1 %d,%d anim=%d gnd=%d act=%d | p2 cls=%d %d,%d anim=%d gnd=%d act=%d | g1 cls=%d %d,%d act=%d | g2 cls=%d %d,%d act=%d\n", g_ps1PerfFrames, $gn, $hc, $q1->position.x >> 16, $q1->position.y >> 16, $q1->animator.animationID, $q1->onGround, $q1->active, $q2->classID, $q2->position.x >> 16, $q2->position.y >> 16, $q2->animator.animationID, $q2->onGround, $q2->active, $h1->classID, $h1->position.x >> 16, $h1->position.y >> 16, $h1->active, $h2->classID, $h2->position.x >> 16, $h2->position.y >> 16, $h2->active
end
set language auto
continue
end
