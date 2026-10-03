#!/usr/bin/env python3
"""Stage-local copies and the disc load order for Mania (docs/32 phase 9.2; Sonic 2's tool, docs/30 9.2).

A scene's load (tools/load_profile.sh) opens, in this order: its code overlay (Data/PS1Ovl/<unit>.bin), the folder's
TileConfig.bin and StageConfig.bin, the static-variable files of its classes (Data/Objects/Static/<hash>.bin: the
GameConfig's objects, then the StageConfig's), 16x16Tiles.pal, Scene<N>.ptl / .pda / .atl, PS1Sfx<N>.bin, Scene<N>.bin,
the strings, then its sprite files in the order the captured manifest lists them (tools/gfx/manifests/<folder><N>.txt;
a mesh right after the sprite file that loads it). The folder's own files sit together; everything else (the shared
Global / Players / UI sprites, the static files, the strings, the meshes) was a seek away each. Each folder gets an
exact copy of those in Data/Stages/<folder>/PS1Local/<FNV-1a 32 of the lowercase path>.<ext>, and ps1/cdrom_fs.cpp
reads from there while that folder loads. tools/disc/load_order.txt lists every scene's files in that order (game
order: GameConfig's categories), copies included, so a load reads a few continuous runs.

Writes the copies under ISO_ROOT and the load order (paths only, no game data).
Usage: stage_local.py ISO_ROOT [OUT=tools/disc/load_order.txt]
"""
import hashlib, os, shutil, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, '..', '..')
sys.path.insert(0, os.path.join(REPO, 'tools'))
import rsdk5_formats as F  # noqa: E402

MESH_AFTER = {'Global/SpecialRing.bin': 'Data/Meshes/Global/SpecialRing.bin'}  # LoadMesh right after the sprites


def fnv1a(s):
    h = 0x811C9DC5
    for c in s.lower().encode():
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


def local_name(rel):
    """the copy's name: FNV-1a 32 of the path as the game opens it (lowercase), its extension kept (ps1/cdrom_fs.cpp)"""
    return '%08X%s' % (fnv1a(rel), os.path.splitext(rel)[1])


def static_file(name):
    """Data/Objects/Static/<hash>.bin as RSDK::LoadStaticVariables names it: md5 words little-endian, low nibble first"""
    words = struct.unpack('<4I', hashlib.md5(name.encode()).digest())
    return 'Data/Objects/Static/%s.bin' % ''.join(''.join('0123456789ABCDEF'[(w >> i) & 15] for i in range(0, 32, 4)) for w in words)


def scenes(data):
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    for cat in g['categories']:
        for s in cat['scenes']:
            yield s['folder'], s['id']


def units():
    out = {}
    path = os.path.join(REPO, 'build', 'gen', 'ovl', 'units.txt')
    for line in open(path):
        p = line.split()
        if len(p) == 3:
            out[(p[1], None if p[2] == '-' else p[2])] = p[0]
    return out


def manifest(folder, sid):
    p = os.path.join(REPO, 'tools', 'gfx', 'manifests', folder + sid + '.txt')
    if not os.path.exists(p):
        return []
    return [l.split()[1] for l in open(p) if l.startswith('SPR ')]


def main():
    iso = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'load_order.txt')
    data = os.path.join(iso, 'Data')
    exists = lambda rel: os.path.isfile(os.path.join(iso, rel))
    ovl = units()
    gobjs = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))['objects']
    order, seen = [], set()

    def add(rel):  # (paths compare case-insensitively, as the disc index: manifests spell RattleKiller / Rattlekiller)
        if rel.lower() not in seen and exists(rel):
            seen.add(rel.lower())
            order.append(rel)

    for rel in ('Data/Game/GameConfig.bin', 'Data/Music/Music.bin', 'Data/Strings/StringsEN.txt'):
        add(rel)
    copies = {}  # folder -> {rel: local}
    first = {}   # folder -> where its files start in the order (its PS1Local.idx goes there)
    for folder, sid in scenes(data):
        stage = 'Data/Stages/%s/' % folder
        if not exists(stage + 'StageConfig.bin'):
            continue
        loc = copies.setdefault(folder, {})
        first.setdefault(folder, len(order))

        def shared(rel):
            if not exists(rel):
                return
            key = rel.lower()
            if key not in loc:
                loc[key] = stage + 'PS1Local/' + local_name(rel)
                dst = os.path.join(iso, loc[key])
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(os.path.join(iso, rel), dst)
            add(loc[key])

        unit = ovl.get((folder, sid)) or ovl.get((folder, None))
        if unit:
            add('Data/PS1Ovl/%s.bin' % unit)
        add(stage + 'TileConfig.bin')
        add(stage + 'StageConfig.bin')
        sobjs = F.read_stageconfig(os.path.join(data, 'Stages', folder, 'StageConfig.bin'))['objects']
        for name in gobjs + sobjs:
            shared(static_file(name))
        for n in ('16x16Tiles.pal', 'Scene%s.ptl' % sid, 'Scene%s.pda' % sid, 'Scene%s.atl' % sid, 'PS1Sfx%s.bin' % sid,
                  'Scene%s.bin' % sid):
            add(stage + n)
        shared('Data/Strings/StringsEN.txt')
        for spr in manifest(folder, sid):
            shared('Data/Sprites/' + spr)
            if spr in MESH_AFTER:
                shared(MESH_AFTER[spr])
        mesh_dir = os.path.join(data, 'Meshes', folder)
        if os.path.isdir(mesh_dir):
            for n in sorted(os.listdir(mesh_dir)):
                shared('Data/Meshes/%s/%s' % (folder, n))
    # each folder's index of its copies (tools/disc/build_disc.py fills it with their LBAs after the first mastering
    # pass; the disc index leaves the copies out: it lives in RAM from boot, the folders' tables only during a load)
    for folder in sorted(first, key=lambda f: -first[f]):
        n = len(copies[folder])
        if not n:
            continue
        rel = 'Data/Stages/%s/PS1Local.idx' % folder
        open(os.path.join(iso, rel), 'wb').write(bytes(16 + 12 * n))
        order.insert(first[folder], rel)
        seen.add(rel.lower())
    # a copy's name must be unique in its folder
    for folder, loc in copies.items():
        names = list(loc.values())
        if len(names) != len(set(names)):
            dup = [r for r in loc if list(loc.values()).count(loc[r]) > 1]
            raise SystemExit('stage_local: name collision in %s: %s' % (folder, dup))
    with open(out, 'w') as f:
        f.write('# Disc files in the order the game loads them (tools/disc/stage_local.py, docs/32 phase 9.2): scenes in\n'
                '# GameConfig order, each one\'s files as its load opens them, its folder\'s PS1Local copies of the shared\n'
                '# files between them. Paths only (no game data). tools/disc/build_disc.py lays these out first.\n')
        for rel in order:
            f.write(rel + '\n')
    n = sum(len(v) for v in copies.values())
    b = sum(os.path.getsize(os.path.join(iso, p)) for v in copies.values() for p in v.values())
    print('stage_local: %d folders, %d copies (%.1f MB), load order %d files' % (len(copies), n, b / 1048576, len(order)))


if __name__ == '__main__':
    main()
