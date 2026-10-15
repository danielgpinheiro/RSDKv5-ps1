#!/bin/bash
# Task 3 (docs/32 9f; tools/play/menus/freeze_loop.sh): N new-game starts on a fresh card (DuckStation: a non-persistent card; tools/emu.sh) from the
# menu_nat snapshot, each with its own button timings (DuckStation is deterministic: the same script replays the same
# run): the menu presses +-0..40 frames, the AIZ intro's skip 30..400 frames after its load (the save's card write runs
# in the background then). One line per run: DONE (GHZ1 + 600 frames) or STUCK (where).
cd "$(dirname "$0")/../../.."
# The run scripts delete their snapshot when done: make menu_nat first with KEEP_SNAP=1 (any tools/play/menus/run.sh run)
[ -d build/boss/snaps/menu_nat ] || { echo "no menu_nat snapshot: run tools/play/menus/run.sh once with KEEP_SNAP=1"; exit 1; }
N=${N:-24}; TAG=${TAG:-jit}
for i in $(seq 1 $N); do
  st=build/perf9f/ns_${TAG}_$i.steps
  awk -v seed=$((i * 7919 + ${SEED:-1})) 'BEGIN{srand(seed)} /^#/ {next} NF>=4 { d=$2; if ($1=="0xFFFE") d=30+int(rand()*370); else d=$2+int(rand()*40); print $1, d, $3, $4 }' ${STEPS:-tools/play/menus/newsave.steps} > $st
  EMU=${EMU:-ds} NOBUILD=1 TIMEOUT=${TIMEOUT:-700} tools/play/menus/run.sh ${TAG}_ng_$i $st 9 1 600 > /dev/null 2>&1
  L=build/boss/mn_${TAG}_ng_$i.log
  if grep -q "^L DONE" $L; then r=DONE; else r="STUCK $(grep -a '^L STUCK' $L | head -1) $(grep -a -A3 '^#0' $L | head -4 | tr '\n' ' ')"; fi
  echo "$i $r skip=$(tail -1 $st | awk '{print $2}') $(grep -a '^L SCENE' $L | tail -1 | cut -c1-40) $(grep -ao 'cardErrors=[0-9]*' $L | tail -1)" | tee -a build/perf9f/freeze_${TAG}.txt
done
