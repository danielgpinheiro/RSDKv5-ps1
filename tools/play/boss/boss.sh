#!/bin/bash
# 9b.2b boss recipe: boss.sh OUT CLASS_EXPR HIT_FN HITS DX DY WAIT PRISON  (the EXE must be the scene's BOOT= build;
# HIT_FN = name in the scene's overlay ELF (OVL=folder) or the resident ELF). EMU=ds for DuckStation (from a snapshot: R=)
H=$(cd "$(dirname "$0")" && pwd)                       # these scripts, recipes, PER / EXTRA snippets
S=${BOSS_WORK:-$(cd "$H/../../.." && pwd)/build/boss}  # generated GDB scripts, logs, snapshots
mkdir -p $S
R=${R:-$(cd "$H/../../.." && pwd)}
OUT=$1; CLASS=$2; FN=$3; HITS=$4; DX=$5; DY=$6; WAIT=$7; PRISON=$8
ADDR=$(mipsel-none-elf-nm $R/build/gen/ovl/$OVL/$OVL.elf 2>/dev/null | awk -v f=$FN '$3==f && $2=="T"{print $1}')
[ -z "$ADDR" ] && ADDR=$(mipsel-none-elf-nm $R/rsdkv5.elf | awk -v f=$FN '$3==f && $2=="T"{print $1}')
[ -z "$ADDR" ] && { echo "no symbol $FN"; exit 1; }
ADDR2=$ADDR   # FN2: the second hit round's function (CLASS2: its class), e.g. ERZ's King then the Klepto Mobile
if [ -n "${FN2:-}" ]; then
  ADDR2=$(mipsel-none-elf-nm $R/build/gen/ovl/$OVL/$OVL.elf 2>/dev/null | awk -v f=$FN2 '$3==f && $2=="T"{print $1}')
  [ -z "$ADDR2" ] && ADDR2=$(mipsel-none-elf-nm $R/rsdkv5.elf | awk -v f=$FN2 '$3==f && $2=="T"{print $1}')
  [ -z "$ADDR2" ] && { echo "no symbol $FN2"; exit 1; }
fi
cat > $S/$OUT.in.gdb <<G
set \$lf = 0
set \$armed = 1
set \$n = 0
set \$ph = 0
set \$pf = 0
set \$seen = 0
set \$psl = -1
set \$second = 0
break RSDK::LoadSceneFolder
commands
silent
printf "L SCENE f=%u cat=%d pos=%d folder=%s name=%s\n", g_ps1PerfFrames, RSDK::sceneInfo.activeCategory, RSDK::sceneInfo.listPos, RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].folder, RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].name
printf "L SCENEMISS total=%u\n", g_ps1SpriteMissing
set \$i = 0
while \$i < 8
  set \$e = g_ps1MissingSheet[\$i]
  if \$e
    set \$s = (\$e & 0xFF) - 1
    printf "L MISS sheet=%d frames=%u rect=%u,%u hash=%08x%08x w=%d h=%d\n", \$s, \$e >> 8, g_ps1MissingRect[\$i] & 0xFFFF, g_ps1MissingRect[\$i] >> 16, RSDK::gfxSurface[\$s].hash[0], RSDK::gfxSurface[\$s].hash[1], RSDK::gfxSurface[\$s].width, RSDK::gfxSurface[\$s].height
  end
  set \$i = \$i + 1
end
set \$i = 0
while \$i < 8
  set var g_ps1MissingSheet[\$i] = 0
  set \$i = \$i + 1
end
set var g_ps1SpriteMissing = 0
set \$lf = g_ps1PerfFrames
set \$armed = 1
continue
end
break perfTick if \$armed && g_ps1PerfFrames == \$lf + 300
commands
silent
set \$p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
printf "L AT f=%u pos=%d state=%d x=%d y=%d rings=%d hbl=%u headroom=%u oom=%u missing=%u spriteMissing=%u relocations=%u ghost=%u stgFail=%u acMissing=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, \$p->position.x >> 16, \$p->position.y >> 16, \$p->rings, g_ps1PerfLastHbl, g_ps1HeapHeadroom, g_ps1OutOfMemory, RSDK::g_ps1StageObjectsMissing, *(unsigned *)&g_ps1SpriteMissing, RSDK::g_ps1EntityRelocations, RSDK::g_ps1EntityGhostWrites, g_ps1StorageFail[0], g_ps1SpriteOutside
set \$armed = 0
set \$i = 0
while \$i < 8
  set \$e = g_ps1MissingSheet[\$i]
  if \$e
    set \$s = (\$e & 0xFF) - 1
    printf "L MISS sheet=%d frames=%u rect=%u,%u hash=%08x%08x w=%d h=%d\n", \$s, \$e >> 8, g_ps1MissingRect[\$i] & 0xFFFF, g_ps1MissingRect[\$i] >> 16, RSDK::gfxSurface[\$s].hash[0], RSDK::gfxSurface[\$s].hash[1], RSDK::gfxSurface[\$s].width, RSDK::gfxSurface[\$s].height
  end
  set \$i = \$i + 1
end
set \$n = \$n + 1
if \$n > 1
  quit
end
set var \$p->rings = 50
set var g_ps1TestBossClass = $CLASS
set var g_ps1TestBossNth = ${NTH:-0}
set var g_ps1TestBossPin = ${PIN:--1}
set var g_ps1TestBossDX = $DX
set var g_ps1TestBossDY = $DY
set var g_ps1TestBossWarp = ${WARPMODE:-1}
set var g_ps1PadTestMask = ${PAD:-0}
${WARPACTION:-echo}
set \$ph = 1
set \$pf = g_ps1PerfFrames
continue
end
break perfTick if \$ph == 1 && g_ps1PerfFrames == \$pf + $WAIT
commands
silent
printf "L HIT f=%u slot=%d class=%d bx=%d by=%d x=%d y=%d\n", g_ps1PerfFrames, g_ps1TestBossSlot, g_ps1TestBossClass, ((struct EntityPlayer *)RSDK::objectEntityPtr[g_ps1TestBossSlot])->position.x >> 16, ((struct EntityPlayer *)RSDK::objectEntityPtr[g_ps1TestBossSlot])->position.y >> 16, \$p->position.x >> 16, \$p->position.y >> 16
set var g_ps1PadTestMask = ${HOLDPAD:-0}
set \$ph = 10
continue
end
break perfTick if \$ph >= 1 && \$ph <= 11 && g_ps1PerfFrames % ${TICK:-60} == 0
commands
silent
printf "L B f=%u ph=%d slot=%d hits=%d x=%d y=%d rings=%d st=%d hbl=%u miss=%u\n", g_ps1PerfFrames, \$ph, g_ps1TestBossSlot, g_ps1TestBossHits, \$p->position.x >> 16, \$p->position.y >> 16, \$p->rings, RSDK::sceneInfo.state, g_ps1PerfLastHbl, g_ps1SpriteMissing
@PER@
if (\$ph == 10 || \$ph == 2) && ${FJUMP:-0}
  set var g_ps1PadTestMask = ((g_ps1PerfFrames / ${TICK:-60}) & 1) * 16
end
if \$ph >= 4 && \$ph <= 5 && ${EPAD:-0}
  set var g_ps1PadTestMask = ${EPAD:-0} | ((g_ps1PerfFrames / ${TICK:-60}) & ${EJUMP:-0}) * 16
end
if \$ph == 3 && ${PPAD:-0}
  set var g_ps1PadTestMask = ${PPAD:-0} | ((g_ps1PerfFrames / ${TICK:-60}) & 1) * 16
end
if \$ph == 4 && ${PPAD:-0}
  set var g_ps1PadTestMask = 0
end
if \$ph == 10 && (${READY:-1})
  printf "L READY f=%u\n", g_ps1PerfFrames
  ${ACTION:-echo}
  set var \$p->rings = 50
  set var g_ps1TestBossHit = (void (*)(void))0x$ADDR
  set var g_ps1TestBossHits = $HITS
  set \$ph = 2
end
if \$ph == 11 && (${READY2:-1})
  printf "L READY2 f=%u\n", g_ps1PerfFrames
  set var g_ps1TestBossClass = ${CLASS2:-$CLASS}
  set var g_ps1TestBossHit = (void (*)(void))0x$ADDR2
  set var g_ps1TestBossNth = ${NTH2:-${NTH:-0}}
  set var g_ps1TestBossPin = ${PIN2:-${PIN:--1}}
  set var g_ps1TestBossHits = ${HITS2:-0}
  set \$ph = 2
end
if \$ph == 2 && g_ps1TestBossHits == 0 && ${HITS2:-0} && !\$second
  set \$second = 1
  set \$ph = 11
end
if \$ph == 2 && g_ps1TestBossHits == 0
  ${DONEACTION:-echo}
  if $PRISON
    set var g_ps1TestBossClass = *(short *)EggPrison
    set var g_ps1TestBossNth = ${PNTH:-0}
    set var g_ps1TestBossPin = -1
    set var g_ps1TestBossHit = EggPrison_State_Opened
    set \$ph = 3
  else
    set var g_ps1TestBossClass = 0
    set \$ph = 4
  end
else
  if \$ph == 3 && g_ps1TestBossSlot >= 0 && (${PREADY:-1})
    set \$seen = \$seen + 1
    if \$seen == ${PSEEN:-4}
      printf "L PRISONWARP f=%u slot=%d\n", g_ps1PerfFrames, g_ps1TestBossSlot
      set \$psl = g_ps1TestBossSlot
      set var g_ps1TestBossDX = ${PDX:-0}
      set var g_ps1TestBossDY = ${PDY:--64}
      set var g_ps1TestBossWarp = ${PWARP:-1}
    end
    if \$seen == ${PSEEN:-4} + 3
      if ((struct EntityEggPrison *)RSDK::objectEntityPtr[\$psl])->state == EggPrison_State_Idle
        printf "L PRISON remote open f=%u slot=%d\n", g_ps1PerfFrames, \$psl
        set var g_ps1TestBossHits = 1
      else
        printf "L PRISON opened by the player f=%u\n", g_ps1PerfFrames
      end
      set \$ph = 4
    end
  else
    if \$ph == 4 && g_ps1TestBossHits == 0
      set var g_ps1TestBossClass = 0
      set \$ph = 5
    end
  end
end
if \$ph >= 4
  set \$e = (struct EntityEggPrison *)RSDK::objectEntityPtr[\$psl]
  printf "L P cls=%d act=%d st=%p x=%d y=%d timer=%d actclear=%d sstate=%d\n", \$e->classID, \$e->active, \$e->state, \$e->position.x >> 16, \$e->position.y >> 16, \$e->timer, ((struct EntityPlayer *)RSDK::objectEntityPtr[16])->classID, RSDK::sceneInfo.state
end
continue
end
break perfTick if g_ps1PerfFrames % 60 == 31 && g_ps1PerfFrames >= ${MAXF:-9000}
commands
silent
printf "L TIMEOUT f=%u pos=%d state=%d ph=%d miss=%u\n", g_ps1PerfFrames, RSDK::sceneInfo.listPos, RSDK::sceneInfo.state, \$ph, g_ps1SpriteMissing
set \$i = 0
while \$i < 8
  set \$e = g_ps1MissingSheet[\$i]
  if \$e
    set \$s = (\$e & 0xFF) - 1
    printf "L MISS sheet=%d frames=%u rect=%u,%u hash=%08x%08x w=%d h=%d\n", \$s, \$e >> 8, g_ps1MissingRect[\$i] & 0xFFFF, g_ps1MissingRect[\$i] >> 16, RSDK::gfxSurface[\$s].hash[0], RSDK::gfxSurface[\$s].hash[1], RSDK::gfxSurface[\$s].width, RSDK::gfxSurface[\$s].height
  end
  set \$i = \$i + 1
end
quit
end
break PS1OutOfMemory
commands
silent
printf "L STOP oom %u\n", \$a0
bt 8
quit
end
G
if [ -n "$PER" ]; then python3 $H/perins.py $S/$OUT.in.gdb $PER; else sed -i '' '/@PER@/d' $S/$OUT.in.gdb; fi
[ -n "$EXTRA" ] && sed "s|@OVL@|$R/build/gen/ovl/$OVL/$OVL.elf|g" $EXTRA >> $S/$OUT.in.gdb
echo continue >> $S/$OUT.in.gdb
# a run the timeout interrupts (SIGINT) lands here: where the game was (a hang never reaches perfTick)
printf 'printf "L STUCK f=%%u\\n", g_ps1PerfFrames\nbt 12\ninfo registers pc ra\n' >> $S/$OUT.in.gdb
pgrep -f "DuckStation.app/Contents/MacOS" >/dev/null && [ "$EMU" = ds ] && { echo "DS busy"; exit 1; }
cd $R; TIMEOUT=${TIMEOUT:-1500} tools/play_test.sh $S/$OUT.in.gdb $S/$OUT > /dev/null 2>&1
grep -a "^L \|No symbol\|rror" $S/$OUT.log | cut -c1-260
