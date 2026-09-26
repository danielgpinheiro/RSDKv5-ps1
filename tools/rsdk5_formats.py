#!/usr/bin/env python3
"""RSDKv5 (Sonic Mania) data-file readers on the host (phase 1.2), each transcribed from the engine's own
loader in dependencies/RSDKv5/RSDKv5/RSDK:
  GameConfig.bin   RetroEngine.cpp LoadGameConfig: 'CFG', title/subtitle/version strings, u8 category,
                   u16 start scene, u8 N + N object names, 8 palette banks (u16 row mask + 16x3 bytes per set
                   row), u8 N + N x (sfx path, u8 max plays), u16 total scenes, u8 categories x (name, u8 N,
                   N x (name, folder, id, u8 filter)), u8 N global-variable blocks (u32 offset, u32 count, ...)
  StageConfig.bin  Scene.cpp LoadSceneFolder: 'CFG', u8 useGlobalObjects, u8 N + N object names, 8 palette
                   banks (as above), u8 N + N x (sfx path, u8 max plays)
  Scene*.bin       Scene.cpp LoadSceneAssets: 'SCN\\0', 16 bytes + a string + 1 byte of editor metadata,
                   u8 layers x (u8 visible, name, u8 type, u8 drawGroup, u16 xsize, u16 ysize, u16 parallax,
                   u16 scrollSpeed, u16 N scroll infos x (u16, u16, u8, u8), compressed lineScroll,
                   compressed layout (u16 tiles)), u8 objects x (16-byte name hash, u8 varCount, (varCount-1) x
                   (16-byte var hash, u8 type), u16 entities x (u16 slot, i32 x, i32 y, the vars by type));
                   compressed = u32 (cSize + 4), u32 big-endian size, zlib (Reader.hpp ReadCompressed)
  TileConfig.bin   Scene.cpp LoadTileConfig: 'TIL\\0', one compressed buffer: 2 planes x 1024 tiles x 38 bytes
  Sprites/*.bin    Animation.cpp LoadSpriteAnimation: 'SPR\\0', u32 frames, u8 N sheets (strings), u8 N hitbox
                   names, u16 anims x (name, u16 frames, u16 speed, u8 loop, u8 rotation, frames x (u8 sheet,
                   u16 duration, u16 unicode, i16 x, y, w, h, pivotX, pivotY, hitboxes x 4 x i16))
Strings are u8 length + bytes (sheet names carry a NUL inside the length). Name hashes inside the files are the
raw MD5 digest of the name (GEN_HASH_MD5, read as 4 little-endian words), unlike the pack's file-name hashes.
Every reader checks it consumed the whole file (self-check: the format is read exactly as the engine does).
"""
import hashlib, os, struct, zlib

FILTER_HASH = None  # set below
VAR_SIZES = {0: 1, 1: 2, 2: 4, 3: 1, 4: 2, 5: 4, 6: 4, 7: 4, 9: 8, 10: 4, 11: 4}  # 8 = string (u16 len + u16 chars)


def name_hash(name):
    """Object / variable / layer name hashes inside the files: the raw MD5 digest, read back by the engine as 4
    little-endian words (GEN_HASH_MD5 / ReadInt32 x 4). The pack's file-name hashes reverse each word
    (tools/extract_rsdk.py); these do not."""
    return hashlib.md5(name.encode('latin1')).digest()


FILTER_HASH = name_hash('filter')


class R:
    def __init__(self, data):
        self.d, self.p = data, 0

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v, = struct.unpack_from('<H', self.d, self.p); self.p += 2; return v

    def i16(self):
        v, = struct.unpack_from('<h', self.d, self.p); self.p += 2; return v

    def u32(self):
        v, = struct.unpack_from('<I', self.d, self.p); self.p += 4; return v

    def i32(self):
        v, = struct.unpack_from('<i', self.d, self.p); self.p += 4; return v

    def bytes(self, n):
        v = self.d[self.p:self.p + n]; self.p += n; return v

    def string(self):  # sprite sheet names carry a terminating NUL inside their length
        n = self.u8(); return self.bytes(n).decode('latin1').rstrip('\0')

    def compressed(self):
        csize = self.u32() - 4
        size_be = self.u32()
        size = struct.unpack('>I', struct.pack('<I', size_be))[0]
        out = zlib.decompress(self.bytes(csize))
        assert len(out) == size, (len(out), size)
        return out

    def palettes(self):
        banks = []
        for b in range(8):
            mask = self.u16()
            rows = {}
            for r in range(16):
                if mask >> r & 1:
                    rows[r] = [tuple(self.bytes(3)) for _ in range(16)]
            banks.append(rows)
        return banks

    def done(self, what):
        assert self.p == len(self.d), '%s: read %d of %d bytes' % (what, self.p, len(self.d))


def read_gameconfig(path):
    r = R(open(path, 'rb').read())
    assert r.bytes(4) == b'CFG\0'
    g = {'title': r.string(), 'subtitle': r.string(), 'version': r.string(), 'category': r.u8(), 'start_scene': r.u16()}
    g['objects'] = [r.string() for _ in range(r.u8())]
    g['palettes'] = r.palettes()
    g['sfx'] = [(r.string(), r.u8()) for _ in range(r.u8())]
    total = r.u16()
    g['categories'] = []
    for _ in range(r.u8()):
        name = r.string()
        scenes = [{'name': r.string(), 'folder': r.string(), 'id': r.string(), 'filter': r.u8()} for _ in range(r.u8())]
        g['categories'].append({'name': name, 'scenes': scenes})
    assert sum(len(c['scenes']) for c in g['categories']) == total
    g['global_vars'] = []
    for _ in range(r.u8()):
        off, n = r.u32(), r.u32()
        g['global_vars'].append((off, [r.i32() for _ in range(n)]))
    r.done(path)
    return g


def read_stageconfig(path):
    r = R(open(path, 'rb').read())
    assert r.bytes(4) == b'CFG\0'
    s = {'use_global_objects': r.u8() != 0}
    s['objects'] = [r.string() for _ in range(r.u8())]
    s['palettes'] = r.palettes()
    s['sfx'] = [(r.string(), r.u8()) for _ in range(r.u8())]
    r.done(path)
    return s


def read_scene(path, names=()):
    """names: object names whose hashes may appear (global + stage objects), to label the objects."""
    r = R(open(path, 'rb').read())
    assert r.bytes(4) == b'SCN\0'
    r.bytes(16)
    r.string()
    r.u8()
    by_hash = {name_hash(n): n for n in names}
    layers = []
    for _ in range(r.u8()):
        r.u8()
        L = {'name': r.string(), 'type': r.u8(), 'draw_group': r.u8(), 'xsize': r.u16(), 'ysize': r.u16(),
             'parallax': r.u16(), 'scroll_speed': r.u16()}
        L['scroll_infos'] = [(r.u16(), r.u16(), r.u8(), r.u8()) for _ in range(r.u16())]
        line_scroll = r.compressed()
        layout = r.compressed()
        assert len(layout) == 2 * L['xsize'] * L['ysize'], (path, L['name'], len(layout), L['xsize'], L['ysize'])
        L['line_scroll_bytes'] = len(line_scroll)
        L['tiles'] = struct.unpack('<%dH' % (L['xsize'] * L['ysize']), layout)
        layers.append(L)
    objects = []
    for _ in range(r.u8()):
        h = r.bytes(16)
        var_count = r.u8()
        vars_ = [(r.bytes(16), r.u8()) for _ in range(var_count - 1)]
        ents = []
        for _ in range(r.u16()):
            slot, x, y = r.u16(), r.i32(), r.i32()
            vals = []
            for vh, vt in vars_:
                if vt == 8:
                    n = r.u16(); r.bytes(2 * n); vals.append(None)
                elif vt == 9:
                    vals.append((r.i32(), r.i32()))
                else:
                    vals.append(r.bytes(VAR_SIZES[vt]))
            # the "filter" variable (registered by the engine, REV02) selects the entity per game mode
            filt = 0xFF
            for (vh, vt), v in zip(vars_, vals):
                if vh == FILTER_HASH and vt == 0:
                    filt = v[0]
            ents.append({'slot': slot, 'x': x, 'y': y, 'filter': filt, 'vars': {vh: v for (vh, _), v in zip(vars_, vals)}})
        objects.append({'hash': h, 'name': by_hash.get(h, h.hex()[:8]), 'var_types': [t for _, t in vars_], 'entities': ents})
    r.done(path)
    return {'layers': layers, 'objects': objects}


def read_tileconfig(path):
    r = R(open(path, 'rb').read())
    assert r.bytes(4) == b'TIL\0'
    buf = r.compressed()
    r.done(path)
    assert len(buf) == 2 * 1024 * 38, len(buf)
    return {'planes': 2, 'tiles': 1024, 'bytes': len(buf)}


def read_sprite(path):
    r = R(open(path, 'rb').read())
    assert r.bytes(4) == b'SPR\0'
    frame_count = r.u32()
    sheets = [r.string() for _ in range(r.u8())]
    hitboxes = [r.string() for _ in range(r.u8())]
    anims = []
    frames_read = 0
    for _ in range(r.u16()):
        a = {'name': r.string(), 'frame_count': r.u16(), 'speed': r.i16(), 'loop': r.u8(), 'rotation': r.u8(), 'frames': []}
        for _ in range(a['frame_count']):
            f = {'sheet': r.u8(), 'duration': r.i16(), 'unicode': r.u16(), 'x': r.i16(), 'y': r.i16(), 'w': r.i16(),
                 'h': r.i16(), 'pivot_x': r.i16(), 'pivot_y': r.i16()}
            f['hitboxes'] = [(r.i16(), r.i16(), r.i16(), r.i16()) for _ in hitboxes]
            a['frames'].append(f)
            frames_read += 1
        anims.append(a)
    assert frames_read == frame_count, (path, frames_read, frame_count)
    r.done(path)
    return {'sheets': sheets, 'hitboxes': hitboxes, 'anims': anims, 'frame_count': frame_count}


def check_all(data_dir):
    """Parse every file of each kind; print the counts (the self-check: every file read to its last byte)."""
    n = {}
    g = read_gameconfig(os.path.join(data_dir, 'Game', 'GameConfig.bin'))
    n['GameConfig'] = 1
    stages = os.path.join(data_dir, 'Stages')
    n['StageConfig'] = n['Scene'] = n['TileConfig'] = 0
    for folder in sorted(os.listdir(stages)):
        d = os.path.join(stages, folder)
        if not os.path.isdir(d):
            continue
        sc = {'objects': []}
        if os.path.exists(os.path.join(d, 'StageConfig.bin')):
            sc = read_stageconfig(os.path.join(d, 'StageConfig.bin')); n['StageConfig'] += 1
        if os.path.exists(os.path.join(d, 'TileConfig.bin')):  # (DAGarden has no tiles)
            read_tileconfig(os.path.join(d, 'TileConfig.bin')); n['TileConfig'] += 1
        for f in sorted(os.listdir(d)):
            if f.startswith('Scene') and f.endswith('.bin'):
                read_scene(os.path.join(d, f), g['objects'] + sc['objects']); n['Scene'] += 1
    n['Sprite'] = 0
    for root, _, files in os.walk(os.path.join(data_dir, 'Sprites')):
        for f in files:
            if f.endswith('.bin'):
                read_sprite(os.path.join(root, f)); n['Sprite'] += 1
    return n


if __name__ == '__main__':
    import sys
    print(check_all(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..', 'SonicMania_Extracted', 'Data')))
