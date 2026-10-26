#!/usr/bin/env python3
"""Build-time 3D meshes for the Mania PS1 port (docs/ports/mania/32 phase 7.2): the Dreamcast port's mesh pipeline (user decision
2026-09-30; docs/ports/mania/18: `mania-mesh-optimizer`, its settings from Dreamcast/RSDKv5-Decompilation/dreamcast/
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
  (u16 each, into the positions), 0-2 bytes to a 4-byte boundary, then per stored frame the positions (positions x 3 x
  i8 or i16, engine fixed point as LoadMesh keeps them: float x 256 for 16-bit). 9f (UFO item 1): the position count is
  padded to even (16-bit) or a multiple of 4 (8-bit) with copies of the last one (no corner uses them), so every frame
  starts 4-aligned and the keyframe blend reads whole words (ps1/s3d_blend.s).
  Flag bit3 (7.4, the meshes NORMALS lists: drawn with the scene's lighting, as the special ring): after the LOD (or the
  positions), a normal per face corner: i8 x, y, z (x 127), 0.
  Flag bit4 (9f): the LOD is a detail cut's (drawn only by UFO_LOWPOLY builds), not a distance LOD.
  Flag bit2 (7.4): a distance LOD follows the positions (4-aligned): u16 index count, u16 0, colours (u32 per corner),
  indices (into the same positions): the engine draws it when the model is small on screen.
  docs/ports/mania/32 7.4 (PMD5 -> PMD6): the baked colours had split the vertices (Sonic's jog: 698 vertices, 251 positions): the
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
# docs/ports/mania/32 7.3: the PS1 draws a tenth of the Dreamcast's triangles in a frame: the always-visible big meshes (the special
# stage's characters and UFO) simplified further by the same tool (Sonic's jog 769 -> 417 faces, the UFO 774 -> 454)
CHAR_PS1 = CHAR + ['--simplify-index-threshold', '0.3', '--simplify-target-error', '0.08']
DECOR_A = ['--simplify', '--bake-lighting', '--simplify-index-threshold', '0.015', '--simplify-target-error', '0.0225']
DECOR_B = ['--simplify', '--bake-lighting', '--simplify-index-threshold', '0.005', '--simplify-target-error', '0.1']
BIG = ['--simplify', '--simplify-index-threshold', '0.05', '--simplify-target-error', '0.1']
# base Mania's meshes (every LoadMesh literal in the game, less Pinball (Encore) and Mighty / Ray) and the Dreamcast's
# settings for each; POS8: 8-bit positions
MESHES = {}
for c in ('Sonic', 'Tails', 'Knux', 'Mighty', 'Ray'):  # (9g: Mighty and Ray, the Plus characters setting)
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
# docs/ports/mania/32 7.4: distance LODs (PS1 only: the Dreamcast optimiser stops at its colour / normal seams, the UFO at 368 of 774
# faces): positions clustered on a grid (frame 0), each cluster kept as its member nearest the cluster's centre (an
# existing position: the animation stays), faces with two corners in one cluster dropped, duplicates dropped; the grid
# is sized for about this share of the faces
# by eye (rendered side by side): the UFO and the bird keep their shape; the grid collapsed thin parts elsewhere (the
# tree's trunk, the pillars' caps, the fish's fins, the flowers' stems), so those have none
LOD_SHARE = {'Special/UFOChase.bin': 0.4, 'Decoration/Bird.bin': 0.35}  # UFO 0.3 lost its stripes
# 9f (UFO 60 fps plan, step C): the characters' meshes get a LOD too (about half their faces), flagged bit4 ("a cut's
# LOD"): drawn only by the UFO_LOWPOLY cut (the stock look draws the characters whole at any size)
CUT_LODS = {'Special/%s%s.bin' % (c, a) for c in ('Sonic', 'Tails', 'Knux', 'Mighty', 'Ray') for a in ('Ball', 'Dash', 'Jog', 'Jump', 'Tumble')}
LOD_SHARE.update({p: 0.5 for p in CUT_LODS})
# docs/ports/mania/32 7.4: meshes the game draws lit by the scene (not baked): their normals kept (frame 0, per face corner)
NORMALS = {'Global/SpecialRing.bin', 'Special/ItemBox.bin'}  # 7.5: the item box's lit wireframe
# docs/ports/mania/32 7.3: closed meshes, whose faces turned away from the camera are hidden anyway (the engine drops them: half the
# faces); the decorations are open (palm leaves, wings: the PC shows both sides), as SSZ / TMZ until checked
# 9f: Pillar1 is closed too (every edge shared by two faces, opposite ways: tools/mesh check); the other decorations
# have open parts (leaves, fins, petals)
CULLED = {p for p in MESHES if p.startswith('Special/')} | {'Global/SpecialRing.bin', 'Decoration/Pillar1.bin'}


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
    normals = [tuple(vals[v * stride + 3 + k] for k in range(3)) for v in range(vc)] if flags & 1 else None  # frame 0
    read_mdl.normals = normals
    return fvc, vc, cols, idx, frames


def gpu_bgr(c):
    # MDL colours are stored as u32 (the engine's `color`: 0xAARRGGBB read little-endian) -> the GPU's 0x00BBGGRR
    r, g, b = (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF
    return r | g << 8 | b << 16


def cluster_lod(uniq, cidx, ccol, fvc, share):
    """-> (lod corner positions, lod corner colours) with about `share` of the faces (see LOD_SHARE)"""
    p0 = [u[0] for u in uniq]
    lo = [min(p[k] for p in p0) for k in range(3)]
    hi = [max(p[k] for p in p0) for k in range(3)]
    span = max(h - l for h, l in zip(hi, lo)) or 1
    faces = [tuple(cidx[f * fvc:(f + 1) * fvc]) for f in range(len(cidx) // fvc)]
    target = max(4, int(len(faces) * share))
    best = None
    for steps in range(64, 1, -1):  # the finest grid at or under the target
        cell = span / steps
        key = [tuple(int((p[k] - lo[k]) // cell) for k in range(3)) for p in p0]
        members = {}
        for i, k in enumerate(key):
            members.setdefault(k, []).append(i)
        rep = {}
        for k, ms in members.items():
            c = [sum(p0[i][d] for i in ms) / len(ms) for d in range(3)]
            r = min(ms, key=lambda i: sum((p0[i][d] - c[d]) ** 2 for d in range(3)))
            for i in ms:
                rep[i] = r
        seen, out = set(), []
        for f, fc in enumerate(faces):
            m = tuple(rep[i] for i in fc)
            if len(set(m)) < fvc:
                continue
            k = tuple(sorted(m))
            if k in seen:
                continue
            seen.add(k)
            out.append((m, ccol[f * fvc:(f + 1) * fvc]))
        best = out
        if len(out) <= target:
            break
    return [i for m, _ in best for i in m], [c for _, cs in best for c in cs]


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
    cidx = [pos_of[qv[v]] for v in idx]          # corner -> position
    while len(uniq) % (4 if pos8 else 2):         # (9f: whole-word frames; copies of the last position, never indexed)
        uniq.append(uniq[-1])
    pc = len(uniq)
    ccol = [colors[v] for v in idx]              # corner -> colour
    out = struct.pack('<4sBBHHBBHH', b'PMD6', (0 if pos8 else 1) | (2 if culled else 0), fvc, pc, len(kept), step, 0, scale, len(idx))
    out += struct.pack('<%dI' % len(idx), *ccol) + struct.pack('<%dH' % len(idx), *cidx)
    out += b'\0' * (-len(out) % 4)               # (9f: the positions 4-aligned)
    fmt = '<%db' % (3 * pc) if pos8 else '<%dh' % (3 * pc)
    for fi in range(len(kept)):
        out += struct.pack(fmt, *[x for u in uniq for x in u[fi]])
    out += b'\0' * (-len(out) % 4)
    lod = None
    if path in LOD_SHARE and fvc == 3:
        lod = cluster_lod(uniq, cidx, ccol, fvc, LOD_SHARE[path])
        out = bytearray(out)
        out[4] |= 4 | (16 if path in CUT_LODS else 0)
        out += struct.pack('<HH', len(lod[0]), 0) + struct.pack('<%dI' % len(lod[1]), *lod[1])
        out += struct.pack('<%dH' % len(lod[0]), *lod[0]) + b'\0' * (-2 * len(lod[0]) % 4)
        out = bytes(out)
    nrm = read_mdl.normals if path in NORMALS else None
    if nrm:
        out = bytearray(out)
        q8 = lambda x: max(-127, min(127, int(round(x * 127))))
        corner = [(q8(nrm[v][0]), q8(nrm[v][1]), q8(nrm[v][2])) for v in idx]
        table = sorted(set(corner), key=corner.index)  # first use order
        if len(table) <= 256:
            # 9o: indexed (bit 32): u16 count, u16 0, the unique normals (i8 x, y, z, 0), a u8 index a corner (padded to
            # 4): the runtime lights each unique normal once (the giant ring: 80 for 320 corners), 640 B less
            out[4] |= 8 | 32
            out += struct.pack('<HH', len(table), 0)
            for nv in table:
                out += struct.pack('<bbbb', nv[0], nv[1], nv[2], 0)
            out += bytes(table.index(nv) for nv in corner) + b'\0' * (-len(corner) % 4)
        else:
            out[4] |= 8
            for nv in corner:
                out += struct.pack('<bbbb', nv[0], nv[1], nv[2], 0)
        out = bytes(out)
    # read back: every corner's position (each frame) and colour as the Dreamcast pass left them
    magic, fl, fv, v2, f2, st, _, sc, ic = struct.unpack_from('<4sBBHHBBHH', out, 0)
    assert (magic, v2, f2, st, sc, ic) == (b'PMD6', pc, len(kept), step, scale, len(idx)), path
    p = 16
    cols_b = struct.unpack_from('<%dI' % ic, out, p)
    p += 4 * ic
    idx_b = struct.unpack_from('<%dH' % ic, out, p)
    p = (p + 2 * ic + 3) & ~3
    assert p % 4 == 0 and ((3 if pos8 else 6) * pc) % 4 == 0, path  # every frame 4-aligned
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
    if lod:  # read back the LOD: every corner a stored position, its colour one of the mesh's
        p = (p + 3) & ~3
        n2 = struct.unpack_from('<H', out, p)[0]
        assert n2 == len(lod[0]) and all(i < pc for i in struct.unpack_from('<%dH' % n2, out, p + 4 + 4 * n2)), path
        assert set(struct.unpack_from('<%dI' % n2, out, p + 4)) <= set(colors), path
    return out, (vc, pc, n, len(kept), step, len(idx) // fvc, pos8, scale, worst, len(lod[0]) // 3 if lod else 0)


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
            out, (vc, pc, n, kept, step, faces, pos8, scale, worst, lodf) = convert(path, dc, path in POS8, path in CULLED)
            dst = os.path.join(out_data, 'Meshes', path)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(out)
            total += len(out)
            print('%-26s %4d verts -> %4d positions %3d -> %3d frames (step %d) %4d faces%s %s | %6d B (MDL %6d B)' % (
                path, vc, pc, n, kept, step, faces, ' (LOD %d)' % lodf if lodf else '', 'pos8 x%.2f' % (scale / 256) if pos8 else 'pos16', len(out),
                os.path.getsize(src)))
    print('meshes: %d files, %d bytes' % (len(MESHES), total))


if __name__ == '__main__':
    main()
