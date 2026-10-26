#!/bin/bash
# 9b.7 (docs/ports/mania/32): tools/play/wholegame/run.sh PID [NAME]: the whole Mania-mode scene list in one 2 MB boot (BOOT=1,0) as
# PID (1 Sonic, 2 Tails, 4 Knuckles), through tools/play/menus/run.sh; output build/boss/mn_NAME.log
H=$(cd "$(dirname "$0")" && pwd)
pid=$1; name=${2:-whole_$1}
BOOT=1,0 PID=$pid PRE_EXTRA=$H/wholegame.gdb TIMEOUTF=4000 TIMEOUT=${TIMEOUT:-14400} \
  $H/../menus/run.sh $name $H/../menus/none.steps 37 1 600
