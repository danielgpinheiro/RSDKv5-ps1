#!/usr/bin/env python3
"""docs/ports/mania/32 9i: the sprite sheets a boss fight draws. A `make EXTRA_CPPFLAGS=-DPS1_COUNT_SHEETS=1` build counts the frames
asked for per sheet (PS1Sprites.cpp g_ps1SheetDraws); boss.sh with SHEETS=1 dumps the counts at the boss's READY
(OUT.sheets0.bin, with the surface table OUT.surfaces.bin) and when the next scene starts loading (OUT.sheets1.bin).
Prints each loaded sheet with its draws during the fight and after it (to the act's end): 0 = a sheet the fight never asks
for. Usage: fight_sheets.py OUT (build/boss/OUT.*; BOSS_WORK= to move)"""
import glob, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'gfx'))
import build_atlas  # noqa: E402  (md5_words: the engine's hash words of a name)

S = os.environ.get('BOSS_WORK') or os.path.join(HERE, '..', '..', '..', 'build', 'boss')
DATA = os.path.join(HERE, '..', '..', '..', '..', 'SonicMania_Extracted', 'Data', 'Sprites')
out = sys.argv[1]
c0 = struct.unpack('<64I', open(os.path.join(S, out + '.sheets0.bin'), 'rb').read()[:256])
c1 = struct.unpack('<64I', open(os.path.join(S, out + '.sheets1.bin'), 'rb').read()[:256])
surf = open(os.path.join(S, out + '.surfaces.bin'), 'rb').read()
names = {}
for p in glob.glob(os.path.join(DATA, '**', '*.gif'), recursive=True):
    rel = os.path.relpath(p, DATA).replace(os.sep, '/')
    names[tuple(build_atlas.md5_words(rel))] = rel
size = len(surf) // 64  # GFXSurface records
rows = []
for i in range(64):
    rec = surf[i * size:(i + 1) * size]
    h = struct.unpack_from('<4I', rec, 0)
    if not any(h):
        continue
    rows.append((c1[i] - c0[i], c0[i], names.get(h, '?%08x' % h[0]), i))
for d, before, name, i in sorted(rows):
    print('%3d %-34s fight+after %8d  before %8d' % (i, name, d, before))
