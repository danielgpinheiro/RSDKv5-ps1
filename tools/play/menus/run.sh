#!/bin/bash
# 9b.5 (docs/32): run.sh NAME STEPS ENDPOS ENDN ENDF [CARD]: the natural build (snapshot menu_nat: NOBUILD=1 skips the
# build) with the scripted pad. STEPS = a file of "scene delay buttons hold" lines (scene = listPos, 0xFFFF any, 0xFFFE the next scene loaded; delay =
# frames after that scene's load; buttons: bit 0 Up, 1 Down, 2 Left, 3 Right, 4 A (Cross), 5 B, 6 C, 7 X (Triangle),
# 10 Start); ends ENDF frames after the ENDN-th load of scene ENDPOS. CARD = a card image kept across runs (EMU_MC1;
# made blank if missing), never the player's. PRE_EXTRA = a file of more GDB lines (@R@ = the build snapshot). ENDCMDS = extra GDB lines at the end (prints). BOOT=cat,scene: start at that scene (a BOOT= build); PID=1/2/4: play
# as Sonic / Tails / Knuckles (globals->playerID at the first load); PLUS=1 with PID=8 / 16: Mighty / Ray (9g). Output build/boss/mn_NAME.log.
H=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$H/../../.." && pwd)
S=${BOSS_WORK:-$R/build/boss}
name=$1 steps=$2 endpos=$3 endn=$4 endf=$5 card=${6:-}
n=menu_nat
[ -n "${BOOT:-}" ] && n=scene_${BOOT/,/_}   # BOOT=cat,scene: a BOOT= build (its own snapshot) instead of the natural one
if [ "${NOBUILD:-0}" != 1 ]; then
  until mkdir $S/tree.lock 2>/dev/null; do sleep 5; done
  ( cd $R && make -j8 ${BOOT:+BOOT=$BOOT} > $S/b_$n.log 2>&1 && make disc > $S/d_$n.log 2>&1 && $R/tools/play/boss/snap.sh $n > /dev/null ); ok=$?
  rmdir $S/tree.lock
  [ $ok = 0 ] || { echo "BUILD FAIL"; exit 1; }
fi
set_lines=$(awk 'BEGIN{k=0} !/^#/ && NF>=4 {printf "set var g_ps1PadScript[%d] = %s\\nset var g_ps1PadScript[%d] = %s\\nset var g_ps1PadScript[%d] = %s\\nset var g_ps1PadScript[%d] = %s\\n", 4*k, $1, 4*k+1, $2, 4*k+2, $3, 4*k+3, $4; k++} END{printf "set var g_ps1PadScriptSteps = %d\\nset var g_ps1PadScriptStep = 0", k}' $steps)
SNAP=$S/snaps/$n python3 - "$H/menus.gdb.in" "$S/mn_$name.in.gdb" "$set_lines" "$endpos" "$endn" "$endf" "${TIMEOUTF:-5000}" "${ENDCMDS:-}" <<'PY'
import sys, os
src, out, steps, endpos, endn, endf, to, endcmds = sys.argv[1:9]
s = open(src).read().replace('@STEPS@', steps.replace('\\n', '\n')).replace('@ENDPOS@', endpos).replace('@ENDN@', endn)
s = s.replace('@PRE@', open(os.environ['PRE_EXTRA']).read().replace('@R@', os.environ['SNAP']) if os.environ.get('PRE_EXTRA') else '')
s = s.replace('@PIDSET@', (('set var globals->playerID = %s' % os.environ['PID']) if os.environ.get('PID') else '')
              + ('\nset var g_ps1PlusChars = 1' if os.environ.get('PLUS') else ''))  # 9g: Mighty / Ray (PID=8 / 16)
s = s.replace('@ENDF@', endf).replace('@TIMEOUT@', to).replace('@ENDCMDS@', endcmds.replace('\\n', '\n'))
open(out, 'w').write(s)
PY
[ -n "$card" ] && export EMU_MC1=$card
( cd $S/snaps/$n && TIMEOUT=${TIMEOUT:-2400} tools/play_test.sh $S/mn_$name.in.gdb $S/mn_$name ) > /dev/null 2>&1
[ "${NOBUILD:-0}" = 1 ] || $R/tools/play/boss/snap.sh -d $n   # the snapshot this run made (KEEP_SNAP=1 keeps it)
echo "== $name"; grep -a "^L \|rror\|No symbol" $S/mn_$name.log | cut -c1-250
