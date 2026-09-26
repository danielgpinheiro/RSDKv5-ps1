#!/bin/bash
# Capture the sprite manifests of the scenes the current EXE loads within END_FRAME frames (docs/32 3.1):
# tools/gfx/manifests/<Folder><Id>.txt, one "SPR path" / "SHT path" line per load (first occurrence), in load order.
# Usage: tools/gfx/capture_manifests.sh END_FRAME [LABEL]   (a BOOT= build starts at that scene; the natural one at Logos)
set -eu
cd "$(dirname "$0")/../.."
END="${1:-600}"
LABEL="${2:-$(date +%F)}"
OUT=${TMPDIR:-/tmp}/ps1_manifest
{ echo "set \$end_frame = $END"; cat tools/gfx/capture_manifest.gdb; } > "$OUT.in.gdb"
TIMEOUT=${TIMEOUT:-300} tools/play_test_ds.sh "$OUT.in.gdb" "$OUT" > /dev/null 2>&1 || true
/usr/bin/python3 - "$OUT.log" "$LABEL" <<'PYEOF'
import sys, os
log, label = sys.argv[1], sys.argv[2]
scenes, cur = {}, None
for line in open(log, errors='replace'):
    t = line.split()
    if not t:
        continue
    if t[0] == 'SCENE' and len(t) >= 3:
        cur = t[1] + t[2]
        scenes.setdefault(cur, [])
    elif t[0] in ('SPR', 'SHT') and cur and len(t) >= 2 and (t[0], t[1]) not in scenes[cur]:
        scenes[cur].append((t[0], t[1]))
os.makedirs('tools/gfx/manifests', exist_ok=True)
for name, items in scenes.items():
    with open('tools/gfx/manifests/%s.txt' % name, 'w') as f:
        f.write('# %s: the sprite files (SPR) and sheets (SHT) the scene loads, in load order (tools/gfx/capture_manifests.sh, %s)\n' % (name, label))
        for k, p in items:
            f.write('%s %s\n' % (k, p))
    print('manifest %s: %d SPR, %d SHT' % (name, sum(k == 'SPR' for k, _ in items), sum(k == 'SHT' for k, _ in items)))
PYEOF
