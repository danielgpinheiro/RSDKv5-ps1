#!/usr/bin/env python3
"""Build-time 3D meshes for the Mania PS1 port (docs/32 phase 7.2): the Dreamcast port's mesh pipeline (user decision
2026-09-30; docs/18: `mania-mesh-optimizer`, its settings from Dreamcast/RSDKv5-Decompilation/dreamcast/
model_process.sh), then a PS1 format sized for 2 MB.

Step 1, the Dreamcast pass: vertices deduplicated; the character, UFO, item box, springboard and decoration meshes
simplified with its per-file thresholds; lighting baked into the vertex colours (its --bake-lighting: the PS1 then
draws no per-frame lighting, as the Dreamcast). Its --stripify is left out (a Dreamcast GPU path: the PS1 draws
triangles).

Step 2, the PS1 format (the same path under Data/Meshes, replacing the MDL; RSDK/Graphics/Scene3D.cpp PS1 LoadMesh):
  'PMD6', u8 flags (bit0 16-bit positions, else 8-bit; bit1 closed: faces turned away may be dropped), u8 face vertex
  count (3, or 4: quads), u16 positions, u16 stored frames,
  u8 frame step (1, or 2: every other keyframe kept, the engine interpolating across twice the span), u8 0,
  u16 scale (8-bit positions: engine units (float x 256) = value * scale >> 8), u16 index count;
  colours (one per face corner, index count x u32: the GPU's colour word, 0x00BBGGRR, from the baked lighting), indices
  (u16 each, into the positions), then per stored frame the positions (positions x 3 x i8 or i16, engine fixed point as
  LoadMesh keeps them: float x 256 for 16-bit).
  docs/32 7.4 (PMD5 -> PMD6): the baked colours had split the vertices (Sonic's jog: 698 vertices, 251 positions): the
  colours now go with the face corners and vertices sharing every stored position are one, so the GTE transforms each
  position once (the emit reads a corner's colour as it read the vertex's).
  Keyframes are halved where the animation has an even frame count of 10 or more (the loop back to frame 0 keeps its
  timing); 8-bit positions for the animated meshes whose steps stay small on screen, 16-bit for the rest (table).
Read back and compared: every stored position within half a quantisation step of the Dreamcast pass's, colours and
indices exact.

Usage: build_meshes.py SRC_DATA_DIR OUT_DATA_DIR   (needs the optimizer: DC_MESH_OPT or ../Dreamcast/mania-mesh-optimizer)
"""
import os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OPT = os.environ.get('DC_MESH_OPT') or os.path.join(HERE, '../../../../Dreamcast/mania-mesh-optimizer/cmake-build-release/mania-mesh-optimizer')

CHAR = ['--simplify', '--bake-lighting']                     # model_process.sh: --simplify --stripify --bake-lighting
# docs/32 7.3: the PS1 draws a tenth of the Dreamcast's triangles in a frame: the always-visible big meshes (the special
# stage's characters and UFO) simplified further by the same tool (Sonic's jog 769 -> 417 faces, the UFO 774 -> 454)
CHAR_PS1 = CHAR + ['--simplify-index-threshold', '0.3', '--simplify-target-error', '0.08']
DECOR_A = ['--simplify', '--bake-lighting', '--simplify-index-threshold', '0.015', '--simplify-target-error', '0.0225']
DECOR_B = ['--simplify', '--bake-lighting', '--simplify-index-threshold', '0.005', '--simplify-target-error', '0.1']
BIG = ['--simplify', '--simplify-index-threshold', '0.05', '--simplify-target-error', '0.1']
# base Mania's meshes (every LoadMesh literal in the game, less Pinball (Encore) and Mighty / Ray) and the Dreamcast's
# settings for each; POS8: 8-bit positions
MESHES = {}
for c in ('Sonic', 'Tails', 'Knux'):
    for a in ('Ball', 'Dash', 'Jog', 'Jump', 'Tumble'):
        MESHES['Special/%s%s.bin' % (c, a)] = CHAR_PS1
for e in ('Blue', 'Cyan', 'Green', 'Grey', 'Purple', 'Red', 'Yellow'):
    MESHES['Special/Emerald%s.bin' % e] = ['--bake-lighting']
MESHES.update({'Special/ItemBox.bin': ['--simplify', '--bake-lighting'], 'Special/Shadow.bin': [],
               'Special/Springboard.bin': CHAR, 'Special/UFOChase.bin': CHAR_PS1,
               'Decoration/Bird.bin': DECOR_A, 'Decoration/Fish.bin': DECOR_A,
               'Global/SpecialRing.bin': [], 'SSZ/EggTower.bin': BIG, 'SSZ/MonarchPlans.bin': BIG,
               'TMZ/MonarchBG.bin': [], 'TMZ/OrbNet.bin': []})
for d in ('Flower1', 'Flower2', 'Flower3', 'Pillar1', 'Pillar2', 'Tree'):
    MESHES['Decoration/%s.bin' % d] = DECOR_B
POS8 = {p for p in MESHES if p.startswith('Special/') and p != 'Special/UFOChase.bin'} | {'Decoration/Bird.bin', 'Decoration/Fish.bin'}
# docs/32 7.3: closed meshes, whose faces turned away from the camera are hidden anyway (the engine drops them: half the
# faces); the decorations are open (palm leaves, wings: the PC shows both sides), as SSZ / TMZ until checked
CULLED = {p for p in MESHES if p.startswith('Special/')} | {'Global/SpecialRing.bin'}


def read_mdl(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'MDL\0', path
    flags, fvc = d[4], d[5]
    vc, fc = struct.unpack_from('<HH', d, 6)
    p = 10
    if flags & 2:
        p += 8 * vc
    cols = None
    if flags & 4:
        cols = struct.unpack_from('<%dI' % vc, d, p)
        p += 4 * vc
    assert not flags & 8, 'stripped: ' + path
    ic = struct.unpack_from('<H', d, p)[0]
    idx = struct.unpack_from('<%dH' % ic, d, p + 2)
    p += 2 + 2 * ic
    stride = 6 if flags & 1 else 3
    vals = struct.unpack_from('<%df' % (vc * fc * stride), d, p)
    frames = [[tuple(int(vals[(f * vc + v) * stride + k] * 0x100) for k in range(3)) for v in range(vc)] for f in range(fc)]
    return fvc, vc, cols, idx, frames


def gpu_bgr(c):
    # MDL colours are stored as u32 (the engine's `color`: 0xAARRGGBB read little-endian) -> the GPU's 0x00BBGGRR
    r, g, b = (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF
    return r | g << 8 | b << 16


def convert(path, src, pos8, culled):
    fvc, vc, cols, idx, frames = read_mdl(src)
    assert fvc in (3, 4), path  # quads (Global/SpecialRing) are drawn as PS1 quads
    n = len(frames)
    step = 2 if n >= 10 and n % 2 == 0 else 1
    kept = frames[::step]
    maxabs = max(max(abs(x) for x in v) for f in kept for v in f) or 1
    if pos8:
        scale = max(1, -(-maxabs * 0x100 // 127))                # value * scale >> 8 = engine units
        q = lambda x: max(-127, min(127, int(round(x * 0x100 / scale))))
        assert scale < 0x10000, path
    else:
        assert maxabs < 0x8000, path
        scale = 0x100
        q = lambda x: x
    colors = [gpu_bgr(c) for c in cols] if cols else [0xFFFFFF] * vc
    # the stored (quantised) positions of each vertex over the kept frames; vertices equal in all of them are welded
    qv = [tuple(tuple(q(x) for x in f[v]) for f in kept) for v in range(vc)]
    pos_of, uniq = {}, []
    for v in range(vc):
        if qv[v] not in pos_of:
            pos_of[qv[v]] = len(uniq)
            uniq.append(qv[v])
    pc = len(uniq)
    cidx = [pos_of[qv[v]] for v in idx]          # corner -> position
    ccol = [colors[v] for v in idx]              # corner -> colour
    out = struct.pack('<4sBBHHBBHH', b'PMD6', (0 if pos8 else 1) | (2 if culled else 0), fvc, pc, len(kept), step, 0, scale, len(idx))
    out += struct.pack('<%dI' % len(idx), *ccol) + struct.pack('<%dH' % len(idx), *cidx)
    fmt = '<%db' % (3 * pc) if pos8 else '<%dh' % (3 * pc)
    for fi in range(len(kept)):
        out += struct.pack(fmt, *[x for u in uniq for x in u[fi]])
    out += b'\0' * (-len(out) % 4)
    # read back: every corner's position (each frame) and colour as the Dreamcast pass left them
    magic, fl, fv, v2, f2, st, _, sc, ic = struct.unpack_from('<4sBBHHBBHH', out, 0)
    assert (magic, v2, f2, st, sc, ic) == (b'PMD6', pc, len(kept), step, scale, len(idx)), path
    p = 16
    cols_b = struct.unpack_from('<%dI' % ic, out, p)
    p += 4 * ic
    idx_b = struct.unpack_from('<%dH' % ic, out, p)
    p += 2 * ic
    worst = 0
    for f in kept:
        vals = struct.unpack_from(fmt, out, p)
        p += (3 if pos8 else 6) * pc
        for c, v in enumerate(idx):
            assert cols_b[c] == colors[v], path
            for k in range(3):
                x = vals[3 * idx_b[c] + k]
                back = x * scale >> 8 if pos8 else x
                worst = max(worst, abs(back - f[v][k]))
    assert worst <= (scale >> 1) + 1 if pos8 else worst == 0, (path, worst, scale)
    return out, (vc, pc, n, len(kept), step, len(idx) // fvc, pos8, scale, worst)


def main():
    src_data, out_data = sys.argv[1], sys.argv[2]
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        for path, args in sorted(MESHES.items()):
            src = os.path.join(src_data, 'Meshes', path)
            dc = os.path.join(tmp, path.replace('/', '_'))
            r = subprocess.run([OPT, '-i', src, '-o', dc] + args, capture_output=True, text=True)
            if 'no changes were made' in r.stdout:
                dc = src  # nothing to dedup / simplify / bake (the optimizer writes no file): the source as it is
            elif r.returncode or not os.path.exists(dc):
                sys.exit('optimizer failed on %s:\n%s%s' % (path, r.stdout[-400:], r.stderr[-400:]))
            out, (vc, pc, n, kept, step, faces, pos8, scale, worst) = convert(path, dc, path in POS8, path in CULLED)
            dst = os.path.join(out_data, 'Meshes', path)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(out)
            total += len(out)
            print('%-26s %4d verts -> %4d positions %3d -> %3d frames (step %d) %4d faces %s | %6d B (MDL %6d B)' % (
                path, vc, pc, n, kept, step, faces, 'pos8 x%.2f' % (scale / 256) if pos8 else 'pos16', len(out),
                os.path.getsize(src)))
    print('meshes: %d files, %d bytes' % (len(MESHES), total))


if __name__ == '__main__':
    main()
