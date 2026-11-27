#!/bin/bash
# docs/perf/80: exact A/B of a renderer change over whole runs. Both sides are built with `make CHAINCRC=1`
# (ps1/chain_crc.cpp: every frame's GPU command words hashed) and run the same jobs; the per-frame hashes are compared.
# Usage: tools/play/chain_crc.sh [REF] -- JOB...
#   REF: the reference commit (default HEAD); the other side is HEAD plus this checkout's uncommitted changes
#        (tracked diff + untracked files), so the checkout itself is never built or touched. REF=cur: both sides are
#        the current code (a determinism check of the jobs themselves). REF_TOOLS="path...": the reference also gets
#        this checkout's changes to those paths (the hashing switch itself, before it is committed).
#   JOB: boss:NAME           a boss recipe (tools/play/boss/recipes.txt, tools/play/boss/runrec.sh ds: DuckStation, both
#                            sides one after the other once the PCSX-Redux jobs are done)
#        act:I[:STEP[:X0[:XEND]]]  category-1 scene I swept like tools/play/prof_all.sh (warps every 150 frames, STEP px,
#                                  default 1536), tools/play/chain_crc_sweep.gdb
#        boot:C,S:FRAMES     a BOOT=C,S build run FRAMES frames with no input (boot::FRAMES: the natural EXE, e.g. the Title)
# The two sides build in their own worktrees, ../../work/worktrees/mania-crA (REF) and mania-crB (current), the same path
# length so the EXEs differ only by the change; each gets an APFS clone of this build/ the first time (kept between
# runs: make rebuilds only what changed). The act / boot jobs run in parallel on EMU_SLOT SLOTA (3) and SLOTB (4), GDB
# stopping only at the frames g_ps1TestStops asks for and every 256 frames for the hashes.
# Output: build/perf9f/crc/<job>.{A,B}.txt ("frame hash words") and a line per job: SAME n frames / DIFF at frame f /
# a side with no frames. KEEP=1 keeps the worktrees (else removed at the end, with their snapshots).
set -u
cd "$(dirname "$0")/../.."
REPO=$(pwd); WS=$(cd ../.. && pwd)
REF=HEAD
if [ "${1:-}" != -- ]; then REF=$1; shift; fi
[ "${1:-}" = -- ] && shift
[ $# -gt 0 ] || { echo "usage: $0 [REF] -- JOB..."; exit 1; }
A=$WS/work/worktrees/mania-crA; B=$WS/work/worktrees/mania-crB
O=$REPO/build/perf9f/crc; mkdir -p $O
refapply=
if [ "$REF" = cur ]; then refsha=$(git rev-parse --verify HEAD); refapply=apply
else refsha=$(git rev-parse --verify "$REF^{commit}") || exit 1; [ -n "${REF_TOOLS:-}" ] && refapply=tools; fi
cursha=$(git rev-parse --verify HEAD) || exit 1

prep() { # DIR SHA [apply]: the worktree at SHA (+ this checkout's changes)
  local d=$1 sha=$2
  if [ ! -d "$d" ]; then
    git worktree add -q --detach "$d" "$sha" || return 1
    cp -c -R build "$d/build" || return 1
    rmdir "$d/build/boss/tree.lock" 2>/dev/null
    rm -rf "$d/build/boss/snaps" "$d/build/perf9f"
  else
    git -C "$d" checkout -q -f --detach "$sha" || return 1
    git -C "$d" clean -fdq || return 1 # untracked, not ignored (objects and build/ stay)
  fi
  if [ "${3:-}" = apply ] || [ "${3:-}" = tools ]; then
    local only=() # tools: only these paths of the checkout's changes (REF_TOOLS), e.g. the hashing switch HEAD lacks
    [ "${3:-}" = tools ] && only=(-- ${REF_TOOLS})
    git diff HEAD --binary ${only[@]+"${only[@]}"} > $O/cur_${3}.diff
    [ -s $O/cur_${3}.diff ] && { git -C "$d" apply $O/cur_${3}.diff || return 1; }
    git ls-files --others --exclude-standard ${only[@]+"${only[@]}"} | while read -r f; do mkdir -p "$d/$(dirname "$f")"; cp -p "$f" "$d/$f"; done
  fi
  mkdir -p "$d/build/perf9f"
}

parse() { # LOG OUT: CRCF blocks -> "frame hash words"
  /usr/bin/python3 - "$1" "$2" <<'PY'
import sys, re
lines = open(sys.argv[1], errors='replace').read().splitlines()
out, i = {}, 0
while i < len(lines):
    t = lines[i].split()
    if t and t[0] == 'CRCF' and len(t) > 1:
        f = int(t[1]); hs, ws = [], []; mode = hs
        i += 1
        while i < len(lines) and not lines[i].startswith('CRCE'):
            if lines[i].startswith('CRCW'):
                mode = ws
            elif ':' in lines[i]:
                mode += [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', lines[i].split(':', 1)[1])]
            i += 1
        if len(hs) == 256 and len(ws) == 256:
            for k in range(256):
                out[f - 255 + k] = (hs[k], ws[k])
    i += 1
with open(sys.argv[2], 'w') as o:
    for f in sorted(out):
        if f > 0:
            o.write('%d %08x %d\n' % (f, out[f][0], out[f][1]))
PY
}

side() { # DIR SLOT TAG
  local d=$1 slot=$2 tag=$3 j kind a b c x0 step xend w
  cat tools/play/boss/x_gen.gdb tools/play/chain_crc.gdb > "$d/build/perf9f/crc_extra.gdb"
  for j in "${JOBS[@]}"; do
    IFS=: read -r kind a b c xend <<< "$j"
    local name=${j//[:,]/_} log=
    if [ "$slot" = ds ]; then [ $kind = boss ] || continue; else [ $kind = boss ] && continue; fi # (bosses: the DuckStation pass)
    case $kind in
      boss)
        ( cd "$d" && CHAINCRC=1 EXTRA=$d/build/perf9f/crc_extra.gdb tools/play/boss/runrec.sh ds $a ) > $O/$name.$tag.run 2>&1
        log=$d/build/boss/bs_$a.log ;;
      act|boot)
        local boot
        if [ $kind = act ]; then boot=1,$a; else boot=$a; fi
        ( cd "$d" && make -j8 CHAINCRC=1 BOOT=$boot > $O/$name.$tag.build 2>&1 && make disc CHAINCRC=1 BOOT=$boot >> $O/$name.$tag.build 2>&1 ) \
          || { echo "BUILD FAIL $tag $j"; continue; }
        if [ $kind = act ]; then
          step=${b:-1536}; x0=${c:-256}
          w=$(cd "$d" && /usr/bin/python3 - $a <<'P'
import sys
sys.path.insert(0, 'tools')
import rsdk5_formats as F
D = '../SonicMania_Extracted/Data/'
s = F.read_gameconfig(D + 'Game/GameConfig.bin')['categories'][1]['scenes'][int(sys.argv[1])]
sc = F.read_scene(D + 'Stages/%s/Scene%s.bin' % (s['folder'], s['id']))
fg = [l for l in sc['layers'] if l.get('name', '').startswith('FG')]
print(max(l['xsize'] for l in fg) * 16 if fg else 4096)
P
)
          [ -z "$xend" ] && { xend=$((w - 256)); [ $xend -lt 1024 ] && xend=1024; }
          { printf 'set $swX0 = %d\nset $swStep = %d\nset $swEnd = %d\nset $swY = 64\n' $x0 $step $xend
            cat tools/play/chain_crc_sweep.gdb tools/play/chain_crc.gdb; echo continue; } > "$d/build/perf9f/crc_$name.gdb"
        else
          { printf 'tbreak main\ncommands\nsilent\nset var g_ps1TestStops[0] = %d\ncontinue\nend\n' "$b"
            printf 'break ps1TestStop\ncommands\nsilent\nprintf "L BOOTEND f=%%u\\n", g_ps1PerfFrames\nquit\nend\n'
            cat tools/play/chain_crc.gdb; echo continue; } > "$d/build/perf9f/crc_$name.gdb"
        fi
        ( cd "$d" && EMU_SLOT=$slot TIMEOUT=${TIMEOUT:-3600} tools/play_test.sh build/perf9f/crc_$name.gdb build/perf9f/crc_$name > /dev/null 2>&1 )
        log=$d/build/perf9f/crc_$name.log ;;
      *) echo "bad job $j"; continue ;;
    esac
    [ -f "$log" ] && cp "$log" $O/$name.$tag.log
    parse $O/$name.$tag.log $O/$name.$tag.txt 2>/dev/null || : > $O/$name.$tag.txt
  done
}

JOBS=("$@")
prep "$A" $refsha $refapply || { echo "prep A failed"; exit 1; }
prep "$B" $cursha apply || { echo "prep B failed"; exit 1; }
side "$A" ${SLOTA:-3} A &
pa=$!
sleep 5
side "$B" ${SLOTB:-4} B &
pb=$!
wait $pa $pb
# boss recipes stop in GDB every frame: on DuckStation (a direct stub; PCSX-Redux's goes through tools/gdbw's relay at
# ~2 s a stop), one side after the other (one DuckStation at a time)
case " ${JOBS[*]}" in *" boss:"*) side "$A" ds A; side "$B" ds B ;; esac
for j in "${JOBS[@]}"; do
  name=${j//[:,]/_}
  /usr/bin/python3 - $O/$name.A.txt $O/$name.B.txt "$j" <<'PY'
import sys
def rd(p):
    try:
        return {int(t[0]): (t[1], int(t[2])) for t in (l.split() for l in open(p)) if len(t) == 3}
    except OSError:
        return {}
a, b = rd(sys.argv[1]), rd(sys.argv[2])
if not a or not b:
    print('%-24s NO FRAMES (A %d, B %d)' % (sys.argv[3], len(a), len(b))); sys.exit()
common = sorted(set(a) & set(b))
diff = [f for f in common if a[f] != b[f]]
words = sum(a[f][1] for f in common)
if diff:
    f = diff[0]
    print('%-24s DIFF %d of %d frames, first at frame %d (A %s %d words, B %s %d words); A %d B %d frames' % (
        sys.argv[3], len(diff), len(common), f, a[f][0], a[f][1], b[f][0], b[f][1], len(a), len(b)))
else:
    print('%-24s SAME %d frames (%d words); A %d B %d frames' % (sys.argv[3], len(common), words, len(a), len(b)))
PY
done | tee -a $O/summary.txt
if [ "${KEEP:-0}" != 1 ]; then
  git worktree remove --force "$A"; git worktree remove --force "$B"
fi
