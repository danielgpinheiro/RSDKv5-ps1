#!/usr/bin/env python3
"""Phase 1.3 feasibility reports for the Mania PS1 port (docs/32 appendix 1): per scene, what the PS1 would
have to hold, from the data (tools/rsdk5_formats.py) and the MIPS code sizes (build/codesize/sizes.txt,
build/codesize/measure.sh: every SonicMania/Objects/**/*.c at -Os).

Per scene of every GameConfig category:
- objects: the global set (GameConfig, loaded in every scene) + the stage set (StageConfig);
- entities in the scene file, split by filter (1 = both modes, 2 = Mania only, 4 = Encore only);
- layers: layout bytes (u16 per tile), line-scroll bytes; tiles actually used by the layouts (of 1024);
- code: bytes of the global objects (resident) and of the stage objects (the scene's overlay);
- sprites: the sprite files each object loads (LoadSpriteAnimation("X.bin") in its source; Player's per
  character files listed apart), their sheets (GIF size, colours) and the VRAM they need (4-bit if <= 16
  colours, else 8-bit, in 16-bit VRAM halfwords);
- SFX: the global + stage sets as SPU ADPCM bytes at the source rate and at 22.05 kHz (16 B per 28 samples).
Usage: feasibility.py [DATA_DIR] [SIZES] [DETAIL_SCENE e.g. GHZ/1] [--scenes Mania Mode,Presentation,...]
"""
import os, re, struct, sys, wave
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsdk5_formats as F
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))
ENTITY_BYTES = 0x58 + 0x400 + 8  # Entity + data[0x100] + REV0U pointers (Object.hpp)


def object_sources(repo):
    """object name -> source path (SonicMania/Objects.cfg: Game/Objects/<Zone>/<Name>)."""
    m = {}
    for line in open(os.path.join(repo, 'SonicMania', 'Objects.cfg')):
        line = line.strip().rstrip('\\').strip()
        if line.startswith('Game/Objects/'):
            rel = 'SonicMania/' + line[len('Game/'):] + '.c'
            m[os.path.basename(line)] = rel
    return m


def code_sizes(path):
    s = {}
    for line in open(path):
        f, text, data, bss = line.split()
        if f.endswith('/All.c'):
            continue
        s[f] = (int(text), int(data), int(bss))
    return s


def sprite_calls(repo, src):
    """sprite .bin names an object's source loads (constant strings; Player etc. build names at runtime)."""
    try:
        t = open(os.path.join(repo, src)).read()
    except OSError:
        return set()
    return set(re.findall(r'LoadSpriteAnimation\("([^"]+)"', t))


class Sheets:
    def __init__(self, data):
        self.data, self.cache = data, {}

    def info(self, name):
        if name not in self.cache:
            p = os.path.join(self.data, 'Sprites', name)
            try:
                im = Image.open(p)
                colours = len(im.getcolors(256) or [0] * 257)
                self.cache[name] = (im.size[0], im.size[1], colours)
            except OSError:
                self.cache[name] = None
        return self.cache[name]


def vram_halfwords(w, h, colours):
    return w * h // 4 if colours <= 16 else w * h // 2  # 4-bit: 4 texels per halfword; 8-bit: 2


def sfx_bytes(data, name, rate=None):
    p = os.path.join(data, 'SoundFX', name)
    try:
        with wave.open(p, 'rb') as w:
            n, r = w.getnframes(), w.getframerate()
    except (OSError, wave.Error):
        return 0, 0
    r2 = rate or r
    samples = n * r2 // r
    return (samples + 27) // 28 * 16, r


def main():
    a = sys.argv[1:]
    cats = None
    if '--scenes' in a:
        i = a.index('--scenes'); cats = a[i + 1].split(','); del a[i:i + 2]
    data = a[0] if a else F.data_root()
    sizes_path = a[1] if len(a) > 1 else os.path.join(REPO, 'build', 'codesize', 'sizes.txt')
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    srcs = object_sources(REPO)
    sizes = code_sizes(sizes_path)
    sheets = Sheets(data)
    sprites = {}
    # sizeof(Entity<Name>) / sizeof(Object<Name>) for MIPS (build/codesize/entsizes.txt, from entsize.c + nm -S)
    ent_size, static_size = {}, {}
    try:
        for line in open(os.path.join(os.path.dirname(sizes_path), 'entsizes.txt')):
            n, e, o = line.split(); ent_size[n] = int(e); static_size[n] = int(o)
    except OSError:
        pass
    detail = a[2] if len(a) > 2 else None

    def obj_code(name):
        return sizes.get(srcs.get(name, ''), (0, 0, 0))[0]

    def obj_sprites(name):
        if name not in sprites:
            sprites[name] = sprite_calls(REPO, srcs.get(name, ''))
        return sprites[name]

    global_code = sum(obj_code(o) for o in g['objects'])
    global_sfx = sum(sfx_bytes(data, n, 22050)[0] for n, _ in g['sfx'])
    print('GLOBAL objects %d, code %d KB (MIPS -Os); global SFX %d files, %d KB SPU at 22.05 kHz' % (
        len(g['objects']), global_code // 1024, len(g['sfx']), global_sfx // 1024))
    for o in sorted(g['objects'], key=obj_code, reverse=True)[:12]:
        print('  %-16s %6d B' % (o, obj_code(o)))
    print()
    hdr = '%-22s %4s %5s %5s %5s | %6s %5s %5s | %6s %6s | %6s %5s %6s | %6s %5s | %5s %6s' % (
        'scene', 'objs', 'ents', 'mania', 'enc', 'layKB', 'chkKB', 'tiles', 'codeKB', 'ovlKB', 'sheetKB', 'sheets', 'frmKB', 'sfxKB', 'sfx', 'entKB', 'statKB')
    stage_cache = {}
    for c in g['categories']:
        if cats and c['name'] not in cats:
            continue
        print('== %s' % c['name'])
        print(hdr)
        for s in c['scenes']:
            folder = s['folder']
            d = os.path.join(data, 'Stages', folder)
            scf = os.path.join(d, 'StageConfig.bin')
            if folder not in stage_cache:
                stage_cache[folder] = F.read_stageconfig(scf) if os.path.exists(scf) else {'objects': [], 'sfx': [], 'use_global_objects': True}
            sc = stage_cache[folder]
            scene_path = os.path.join(d, 'Scene%s.bin' % s['id'])
            if not os.path.exists(scene_path):
                print('%-22s (no scene file)' % (folder + '/' + s['id']))
                continue
            sn = F.read_scene(scene_path, g['objects'] + sc['objects'])
            ents = [e for o in sn['objects'] for e in o['entities']]
            n_mania = sum(1 for e in ents if e['filter'] & 3)  # FILTER_BOTH | FILTER_MANIA
            n_enc = sum(1 for e in ents if e['filter'] & 5)    # FILTER_BOTH | FILTER_ENCORE
            lay = sum(2 * L['xsize'] * L['ysize'] + L['line_scroll_bytes'] for L in sn['layers'])
            used = set()
            for L in sn['layers']:
                used.update(t & 0x3FF for t in L['tiles'] if t != 0xFFFF)
            used_objs = [o['name'] for o in sn['objects'] if o['entities']]
            stage_objs = [o for o in sc['objects']]
            ovl = sum(obj_code(o) for o in stage_objs)
            # The strings an object's source names are an upper bound: many objects pick one file per stage
            # (CheckSceneFolder), so keep the files whose folder is Global, the scene's zone (folder without its
            # act digit), Players (Sonic only: one character at a time), or named after the zone; drop UI /
            # Editor / Menu files in stages. Phase 2's host run of StageLoad replaces this heuristic.
            zone = re.sub(r'\d+$', '', folder)
            in_stage = c['name'] in ('Mania Mode', 'Encore Mode', 'Special Stage', 'Blue Spheres', 'Pinball', 'Extras', 'Cutscenes')

            def keep(sp):
                top = sp.split('/')[0]
                if not in_stage:
                    return top != 'Editor'
                if top in ('Editor', 'UI', 'Menu'):
                    return False
                if top == 'Players':
                    return sp in ('Players/Sonic.bin', 'Players/SuperSonic.bin')
                if top in ('Global', folder, zone):
                    return True
                return zone.lower() in sp.lower() or folder.lower() in sp.lower()
            spr_names = set()
            for o in g['objects'] + stage_objs:
                spr_names |= {sp for sp in obj_sprites(o) if keep(sp)}
            # layouts as an 8x8-tile chunk dictionary (Sonic CD style): distinct chunks x 128 B + u16 index per chunk
            chunks, idx = set(), 0
            for L in sn['layers']:
                w, h = L['xsize'], L['ysize']
                for cy in range(0, h, 8):
                    for cx in range(0, w, 8):
                        chunks.add(tuple(L['tiles'][(cy + y) * w + cx + x] if cy + y < h and cx + x < w else 0xFFFF for y in range(8) for x in range(8)))
                        idx += 1
            chunk_kb = (len(chunks) * 128 + idx * 2) // 1024
            hw, nsheets, frame_hw = 0, 0, 0
            seen = set()
            for sp in sorted(spr_names):
                p = os.path.join(data, 'Sprites', sp)
                if not os.path.exists(p):
                    continue
                spr = F.read_sprite(p)
                for sh in spr['sheets']:
                    if sh in seen:
                        continue
                    seen.add(sh)
                    info = sheets.info(sh)
                    if info:
                        hw += vram_halfwords(*info)
                        nsheets += 1
                # the frames actually referenced (an atlas of used frames, as Sonic CD's): distinct rects per sheet
                rects = set()
                for an in spr['anims']:
                    for f in an['frames']:
                        rects.add((f['sheet'], f['x'], f['y'], f['w'], f['h']))
                for sheet_i, x, y, w, h in rects:
                    info = sheets.info(spr['sheets'][sheet_i]) if sheet_i < len(spr['sheets']) else None
                    if info and w > 0 and h > 0:
                        frame_hw += vram_halfwords(w, h, info[2])
            sfx = sum(sfx_bytes(data, n, 22050)[0] for n, _ in sc['sfx'])
            # entity RAM with the real struct sizes (Mania-mode entities), static Object structs of the loaded classes
            ent_ram = sum(ent_size.get(o['name'], 1112) for o in sn['objects'] for e in o['entities'] if e['filter'] & 3)
            stat_ram = sum(static_size.get(o, 0) for o in g['objects'] + stage_objs)
            print('%-22s %4d %5d %5d %5d | %6d %5d %5d | %6d %6d | %6d %5d %6d | %6d %5d | %5d %6d' % (
                folder + '/' + s['id'], len(stage_objs), len(ents), n_mania, n_enc, lay // 1024, chunk_kb, len(used),
                (global_code + ovl) // 1024, ovl // 1024, hw * 2 // 1024, nsheets, frame_hw * 2 // 1024, (global_sfx + sfx) // 1024, len(sc['sfx']),
                ent_ram // 1024, stat_ram // 1024))
            if detail == folder + '/' + s['id']:
                print('  -- sprite files by used-frame area (KB of VRAM halfwords; 8-bit unless the sheet has <= 16 colours)')
                rows = []
                for sp in sorted(spr_names):
                    p = os.path.join(data, 'Sprites', sp)
                    if not os.path.exists(p):
                        continue
                    spr = F.read_sprite(p)
                    rects = set((f['sheet'], f['x'], f['y'], f['w'], f['h']) for an in spr['anims'] for f in an['frames'])
                    area = 0
                    for sheet_i, x, y, w, h in rects:
                        info = sheets.info(spr['sheets'][sheet_i]) if sheet_i < len(spr['sheets']) else None
                        if info and w > 0 and h > 0:
                            area += vram_halfwords(w, h, info[2])
                    who = [o for o in g['objects'] + stage_objs if sp in obj_sprites(o)]
                    rows.append((area * 2 // 1024, sp, len(rects), ','.join(who[:3])))
                for r in sorted(rows, reverse=True)[:25]:
                    print('  %6d KB %-32s %4d frames  %s' % r)
                print('  -- statics: %s' % ', '.join('%s %dK' % (o, static_size.get(o, 0) // 1024) for o in sorted(g['objects'] + stage_objs, key=lambda o: -static_size.get(o, 0))[:8]))
                print('  -- entity RAM by object: %s' % ', '.join('%s %dK' % (o['name'], sum(ent_size.get(o['name'], 1112) for e in o['entities'] if e['filter'] & 3) // 1024) for o in sorted(sn['objects'], key=lambda o: -len(o['entities']))[:8]))
        print()


if __name__ == '__main__':
    main()
