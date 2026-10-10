#!/usr/bin/env python3
"""The phase 2 data disc tree (docs/32 2.2): ../SonicMania_Extracted/Data -> build/iso/Data, in the forms the PS1
engine reads without decoding anything at runtime (golden rule):

  Stages/*/Scene*.bin, Stages/*/TileConfig.bin
      every zlib block stored raw: same header (u32 size field = raw length + 4, u32 big-endian raw length), the
      bytes uncompressed (RSDKv5/RSDK/Core/Reader.hpp ReadCompressed on PS1 copies them)
  Stages/*/16x16Tiles.gif -> 16x16Tiles.pal
      the GIF's colour table as ImageGIF::Load reads it (256 x RGB, entries past the table 0); LoadStageGIF on PS1
      applies it to the palette rows. The tile pixels go to VRAM at build time in phase 3.
  Game/GameConfig.bin
      the global-variable blocks remapped to the PS1 GlobalVariables (shorter menuParam / competitionSession / replay
      buffers; SonicMania/GameVariables.h): the same values land in the same fields (self-checked)
  everything else in the kept folders: copied as is

Not on the disc (converted by later phases or not used on the PS1): .gif sprite sheets (VRAM atlases, phase 3),
.wav (SPU ADPCM, phase 5), .ogg (CD-XA, phase 5), .ogv (STR, phase 6), .png/.tga images (phase 3/6), shaders.

Self-check: each rewritten file is read back with the raw rule and its blocks compared with the zlib originals;
the rest of the file must be byte-identical.
Usage: data_disc.py [SRC_DATA] [ISO_ROOT]   (defaults ../SonicMania_Extracted/Data build/iso)
"""
import os, shutil, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import rsdk5_formats as F

SKIP_EXT = ('.gif', '.wav', '.ogg', '.ogv', '.png', '.tga', '.vso', '.fso')


class Rewriter(F.R):
    """Reads like the engine and records where each compressed block is, so the file can be re-emitted with the
    blocks raw."""

    def __init__(self, data):
        super().__init__(data)
        self.blocks = []  # (start, end, raw)

    def compressed(self):
        start = self.p
        raw = super().compressed()
        self.blocks.append((start, self.p, raw))
        return raw

    def emit(self):
        out, last = bytearray(), 0
        for start, end, raw in self.blocks:
            out += self.d[last:start]
            out += struct.pack('<I', len(raw) + 4) + struct.pack('>I', len(raw)) + raw
            last = end
        out += self.d[last:]
        return bytes(out)


def raw_blocks(data):
    """Parse helper for the self-check: a compressed() that reads the raw form."""
    class RawR(F.R):
        def compressed(self):
            size = self.u32() - 4
            be = struct.unpack('>I', struct.pack('<I', self.u32()))[0]
            assert size == be, (size, be)
            return self.bytes(size)
    return RawR(data)


def walk_scene(r):
    """Scene.cpp LoadSceneAssets' read order (as tools/rsdk5_formats.read_scene), calling r.compressed() for the
    blocks."""
    assert r.bytes(4) == b'SCN\0'
    r.bytes(16); r.string(); r.u8()
    for _ in range(r.u8()):
        r.u8(); r.string(); r.u8(); r.u8(); r.u16(); r.u16(); r.u16(); r.u16()
        for _ in range(r.u16()):
            r.u16(); r.u16(); r.u8(); r.u8()
        r.compressed(); r.compressed()
    for _ in range(r.u8()):
        r.bytes(16)
        var_count = r.u8()
        vars_ = [(r.bytes(16), r.u8()) for _ in range(var_count - 1)]
        for _ in range(r.u16()):
            r.u16(); r.i32(); r.i32()
            for _, vt in vars_:
                if vt == 8:
                    n = r.u16(); r.bytes(2 * n)
                elif vt == 9:
                    r.i32(); r.i32()
                else:
                    r.bytes(F.VAR_SIZES[vt])
    assert r.p == len(r.d), 'scene: read %d of %d' % (r.p, len(r.d))


# Scene layouts on the PS1 (docs/32 2.3g, choice 29): each layer's layout block becomes a chunk index (u16 per 4x4 tiles,
# row-major over the layer's own size in chunks: pitch ceil(xsize / 4), ceil(ysize / 4) rows, then one row of chunk 0;
# 7b: it was the stock power-of-two padded size, 64 KB a layer for CPZ1's 1200x192 where 29 KB hold it: the engine reads
# columns / rows past the layer as chunk 0, the stock padding), and one pool of the scene's distinct chunks (16 u16 each, chunk 0 = all 0xFFFF, the stock
# padding value) follows the last layer as one more raw block. The engine reads tiles through the index (Scene.hpp
# PS1TilePtr); writes copy a shared chunk first.
# 7b.3: two levels: a chunk is 4 u16 row indices (8 bytes) into one pool of the scene's distinct 4-tile rows (8 bytes; row 0 =
# all 0xFFFF, chunk 0 = four row 0s): SPZ2's pool 218 -> 146 KB, SPZ1's 171 -> 103 KB. The chunk pool block, then the row
# pool block; writes copy a shared chunk, then a shared row.
# 7b.3c: a layer's chunk index is a table of segments (u16 each: SEG chunk indices in a row, the row padded with chunk 0)
# into one pool of the scene's distinct segments (SEG u16 each; segment 0 = chunk 0 everywhere), the third block after the
# layers: FBZ2's three 1008x496 layers 187 -> ~94 KB (mostly empty or repeated spans). Writes copy a shared segment first.
CHUNK = 4
SEG = 8


def layer_shift(size):
    """Scene.cpp's loop: the smallest shift >= 1 with 1 << shift >= size."""
    shift = 1
    while (1 << shift) < size:
        shift += 1
    return shift


def raw_block(data):
    return struct.pack('<I', len(data) + 4) + struct.pack('>I', len(data)) + data


def chunk_scene(data):
    """The raw-block scene (convert_blocks' output) with chunked layouts. Returns (bytes, chunk count)."""
    r = raw_blocks(data)
    assert r.bytes(4) == b'SCN\0'
    r.bytes(16); r.string(); r.u8()
    out = bytearray(data[:r.p])
    pool, pool_ids = [(0xFFFF,) * (CHUNK * CHUNK)], {}
    pool_ids[pool[0]] = 0
    segs, seg_ids = [(0,) * SEG], {(0,) * SEG: 0}
    layer_count = r.u8()
    out.append(layer_count)
    for _ in range(layer_count):
        start = r.p
        r.u8(); r.string(); r.u8(); r.u8()
        xsize, ysize = r.u16(), r.u16()
        r.u16(); r.u16()
        for _ in range(r.u16()):
            r.u16(); r.u16(); r.u8(); r.u8()
        line_scroll = r.compressed()
        out += data[start:r.p - len(line_scroll) - 8] + raw_block(line_scroll)
        layout = r.compressed()
        tiles = struct.unpack('<%dH' % (xsize * ysize), layout)
        index = b''
        if xsize or ysize:
            cw, ch = -(-xsize // CHUNK), -(-ysize // CHUNK)
            idx = []
            for cy in range(ch):
                for cx in range(cw):
                    k = tuple(tiles[(cy * CHUNK + y) * xsize + cx * CHUNK + x]
                              if cy * CHUNK + y < ysize and cx * CHUNK + x < xsize else 0xFFFF
                              for y in range(CHUNK) for x in range(CHUNK))
                    if k not in pool_ids:
                        pool_ids[k] = len(pool)
                        pool.append(k)
                    idx.append(pool_ids[k])
            idx += [0] * cw  # the row past the last: chunk 0 (rows beyond the layer read it)
            sw, tab = -(-cw // SEG), []
            for cy in range(ch + 1):
                row = idx[cy * cw:(cy + 1) * cw] + [0] * (sw * SEG - cw)
                for i in range(sw):
                    k = tuple(row[i * SEG:(i + 1) * SEG])
                    if k not in seg_ids:
                        seg_ids[k] = len(segs)
                        segs.append(k)
                    tab.append(seg_ids[k])
            assert sw < 0x100, 'segment stride is a u8 (TileLayer.ps1SegStride)'
            index = struct.pack('<%dH' % len(tab), *tab)
        out += raw_block(index)
    assert len(pool) < 0x8000, 'chunk pool too big for u16 indices with room for private chunks'
    rows, row_ids, chunks = [(0xFFFF,) * CHUNK], {(0xFFFF,) * CHUNK: 0}, []
    for c in pool:
        for y in range(CHUNK):
            k = c[y * CHUNK:(y + 1) * CHUNK]
            if k not in row_ids:
                row_ids[k] = len(rows)
                rows.append(k)
            chunks.append(row_ids[k])
    assert chunks[:CHUNK] == [0] * CHUNK and len(rows) < 0xFF00, 'row pool too big for u16 indices with room for private rows'
    out += raw_block(struct.pack('<%dH' % len(chunks), *chunks))
    out += raw_block(b''.join(struct.pack('<4H', *k) for k in rows))
    assert len(segs) < 0xFF00, 'segment pool too big for u16 indices with room for private segments'
    out += raw_block(b''.join(struct.pack('<%dH' % SEG, *k) for k in segs))
    out += data[r.p:]
    return bytes(out), len(pool), len(rows), len(segs)


def objects_first(data):
    """7b: a chunked scene with its objects section moved before the layers (the PS1 loader places the entities before
    any layout is in STG: Scene.cpp LoadSceneAssets)."""
    r = raw_blocks(data)
    assert r.bytes(4) == b'SCN\0'
    r.bytes(16); r.string(); r.u8()
    head = r.p
    for _ in range(r.u8()):
        r.u8(); r.string(); r.u8(); r.u8(); r.u16(); r.u16(); r.u16(); r.u16()
        for _ in range(r.u16()):
            r.u16(); r.u16(); r.u8(); r.u8()
        r.compressed(); r.compressed()
    r.compressed(); r.compressed(); r.compressed()  # the chunk, row and segment pools
    return data[:head] + data[r.p:] + data[head:r.p]


# 7b: object relations the PS1 bakes into the scene file (the engine may keep a class's entities as records, which another
# object's Create can no longer reach): each one appends a PS1-only variable the game registers on the PS1.
# CircleBumper.ps1Low: ShopWindow_Create (SPZ) sets the CircleBumpers its hitboxItem touches to the low object draw group
# (RSDK.CheckObjectCollisionTouchBox: pixel positions, the bumper's hitbox -7,-6,7,6, not flipped); a bumper's own Create
# sets the high group: the window wins when it comes later in slot order (the scene entities' file-slot order).
def objects_section(data):
    """(offset of the objects section, parsed objects) of a raw-block scene (convert_blocks' output)."""
    r = raw_blocks(data)
    assert r.bytes(4) == b'SCN\0'
    r.bytes(16); r.string(); r.u8()
    for _ in range(r.u8()):
        r.u8(); r.string(); r.u8(); r.u8(); r.u16(); r.u16(); r.u16(); r.u16()
        for _ in range(r.u16()):
            r.u16(); r.u16(); r.u8(); r.u8()
        r.compressed(); r.compressed()
    start = r.p
    objs = []
    for _ in range(r.u8()):
        h = r.bytes(16)
        var_count = r.u8()
        vars_ = [(r.bytes(16), r.u8()) for _ in range(var_count - 1)]
        ents = []
        for _ in range(r.u16()):
            slot, x, y = r.u16(), r.i32(), r.i32()
            vals = []
            for _, vt in vars_:
                if vt == 8:
                    n = r.u16(); vals.append(struct.pack('<H', n) + r.bytes(2 * n))
                else:
                    vals.append(r.bytes(8 if vt == 9 else F.VAR_SIZES[vt]))
            ents.append([slot, x, y, vals])
        objs.append([h, vars_, ents])
    assert r.p == len(r.d)
    return start, objs


def bake_relations(data, scene_filter=3):
    """The scene with the PS1's baked relations (see above); unchanged if it has none."""
    start, objs = objects_section(data)
    by = {o[0]: o for o in objs}
    bumper, window = by.get(F.name_hash('CircleBumper')), by.get(F.name_hash('ShopWindow'))
    if not bumper:
        return data, 0

    def var(o, e, name):
        for (vh, vt), v in zip(o[1], e[3]):
            if vh == F.name_hash(name):
                return v
        return None

    def present(o, e):
        f = var(o, e, 'filter')
        return (f[0] if f is not None else 0xFF) & scene_filter

    wins = []
    for e in (window[2] if window else []):
        if not present(window, e):
            continue
        sx, sy = struct.unpack('<ii', var(window, e, 'size'))
        sx, sy = sx >> 16, sy >> 16
        wins.append((e[0], e[1] >> 16, e[2] >> 16, (-8 - sx, -8 - sy, sx + 8, sy + 8)))
    low = 0
    bumper[1].append((F.name_hash('ps1Low'), 0))  # VAR_UINT8
    for e in bumper[2]:
        bx, by_ = e[1] >> 16, e[2] >> 16
        v = 0
        for slot, wx, wy, (l, t, r, b) in wins:
            if slot > e[0] and bx - 7 < wx + r and bx + 7 > wx + l and by_ - 6 < wy + b and by_ + 6 > wy + t:
                v = 1
        e[3].append(bytes([v]))
        low += v
    out = bytearray(data[:start])
    out.append(len(objs))
    for h, vars_, ents in objs:
        out += h + bytes([len(vars_) + 1])
        for vh, vt in vars_:
            out += vh + bytes([vt])
        out += struct.pack('<H', len(ents))
        for slot, x, y, vals in ents:
            out += struct.pack('<Hii', slot, x, y) + b''.join(vals)
    return bytes(out), low


def unchunk_check(orig_raw, chunked):
    """Self-check: every layer's tiles read back through index + pool equal the original layout (padding = 0xFFFF)."""
    r0, r1 = raw_blocks(orig_raw), raw_blocks(chunked)
    for r in (r0, r1):
        assert r.bytes(4) == b'SCN\0'
        r.bytes(16); r.string(); r.u8()
    n = r0.u8(); assert r1.u8() == n
    pending = []
    for _ in range(n):
        dims = []
        for r in (r0, r1):
            r.u8(); r.string(); r.u8(); r.u8()
            dims.append((r.u16(), r.u16()))
            r.u16(); r.u16()
            for _ in range(r.u16()):
                r.u16(); r.u16(); r.u8(); r.u8()
        assert dims[0] == dims[1]
        assert r0.compressed() == r1.compressed()
        pending.append((dims[0], r0.compressed(), r1.compressed()))
    pool, rows, segs = r1.compressed(), r1.compressed(), r1.compressed()
    assert r0.d[r0.p:] == r1.d[r1.p:], 'objects section changed'
    for (xsize, ysize), layout, index in pending:
        if not (xsize or ysize):
            assert index == b''
            continue
        cw, ch = -(-xsize // CHUNK), -(-ysize // CHUNK)
        sw = -(-cw // SEG)
        assert len(index) == 2 * sw * (ch + 1)
        for y in range(1 << layer_shift(ysize)):
            for x in range(1 << layer_shift(xsize)):
                cx, cy = x >> 2, min(y >> 2, ch)  # the engine: a column past the layer reads chunk 0, a row the zero row
                si = struct.unpack_from('<H', index, 2 * ((cx >> 3) + cy * sw))[0] if cx < cw else 0
                ci = struct.unpack_from('<H', segs, 2 * ((si << 3) + (cx & 7)))[0]
                ri = struct.unpack_from('<H', pool, 2 * ((ci << 2) + (y & 3)))[0]
                t = struct.unpack_from('<H', rows, 2 * ((ri << 2) + (x & 3)))[0]
                want = struct.unpack_from('<H', layout, 2 * (y * xsize + x))[0] if x < xsize and y < ysize else 0xFFFF
                assert t == want, (x, y, t, want)


def walk_tileconfig(r):
    assert r.bytes(4) == b'TIL\0'
    r.compressed()
    assert r.p == len(r.d)


def convert_blocks(src, walk):
    data = open(src, 'rb').read()
    rw = Rewriter(data)
    walk(rw)
    out = rw.emit()
    # self-check: the raw form reads back to the same blocks, and nothing else changed
    chk = raw_blocks(out)
    raws = []
    orig = chk.compressed
    chk.compressed = lambda: (raws.append(orig()), raws[-1])[1]
    walk(chk)
    assert raws == [b[2] for b in rw.blocks], src
    return out, len(rw.blocks)


# GlobalVariables on the PS1 (SonicMania/GameVariables.h, RETRO_PS1_BUILD) is the PC struct with some arrays shorter:
# (PC int offset, PC length, PS1 length). GameConfig.bin's global-variable blocks (u32 int offset, u32 count, values) are
# written for the PC layout: offsets after a shortened array move down, writes into dropped elements go. SonicMania/Game.c
# checks the same numbers against the compiled layout (_Static_assert).
GV_REGIONS = [
    (40, 0x4000, 0xA40),                    # atlEntityData: 8 stored entities + SaveGame's recall table (Zone.c)
    (16425, 0x4000, 0xD00),                 # saveRAM: the offsets the game indexes (slots, time attack, progress, encore)
    (33834, 0x4000, 0x80),                  # menuParam: only MenuParam_GetParam's EntityMenuParam (432 B)
    (50221, 0x4000, 0x80),                  # competitionSession: only CompetitionSession_GetSession (380 B)
    (67037 + 0 * 0x40000, 0x40000, 1),      # replayWriteBuffer  (no replays in scope)
    (67037 + 1 * 0x40000, 0x40000, 1),      # replayReadBuffer
    (67037 + 2 * 0x40000, 0x40000, 1),      # replayTempWBuffer
    (67037 + 3 * 0x40000, 0x40000, 1),      # replayTempRBuffer
]
GV_PC_INTS = 1115625             # sizeof(GlobalVariables) / 4 on the PC (4,462,500 bytes, measured in 2.2)


def gv_remap(off):
    shift = 0
    for start, pc_len, ps1_len in GV_REGIONS:
        if off < start:
            return off - shift
        if off < start + pc_len:
            i = off - start
            return start - shift + i if i < ps1_len else None
        shift += pc_len - ps1_len
    assert off < GV_PC_INTS, off
    return off - shift


def convert_gameconfig(src):
    """GameConfig.bin with its global-variable blocks remapped (everything before them is copied as is)."""
    d = open(src, 'rb').read()
    r = F.R(d)
    assert r.bytes(4) == b'CFG\0'
    r.string(); r.string(); r.string(); r.u8(); r.u16()
    [r.string() for _ in range(r.u8())]
    r.palettes()
    [(r.string(), r.u8()) for _ in range(r.u8())]
    r.u16()
    for _ in range(r.u8()):
        r.string()
        for _ in range(r.u8()):
            r.string(); r.string(); r.string(); r.u8()
    head = d[:r.p]
    writes, dropped = [], 0
    for _ in range(r.u8()):
        off, n = r.u32(), r.u32()
        for v in range(n):
            val = r.i32()
            m = gv_remap(off + v)
            if m is None:
                dropped += 1
            else:
                writes.append((m, val))
    r.done(src)
    # coalesce consecutive offsets into blocks (the loader writes them in file order, as the PC does)
    blocks = []
    for off, val in writes:
        if blocks and blocks[-1][0] + len(blocks[-1][1]) == off:
            blocks[-1][1].append(val)
        else:
            blocks.append((off, [val]))
    assert len(blocks) < 256
    out = bytearray(head) + bytes([len(blocks)])
    for off, vals in blocks:
        out += struct.pack('<II', off, len(vals)) + b''.join(struct.pack('<i', v) for v in vals)
    # self-check: the same values land in the same fields
    pc = {}
    for off, vals in F.read_gameconfig(src)['global_vars']:
        for v, val in enumerate(vals):
            if gv_remap(off + v) is not None:
                pc[gv_remap(off + v)] = val
    ps1 = {}
    for off, vals in blocks:
        for v, val in enumerate(vals):
            ps1[off + v] = val
    assert pc == ps1
    return bytes(out), len(writes), dropped


# Sprites/UI/SmallFont.bin (docs/32 2.3e): 7,375 glyphs, one animation, for every language the PC ships. The PS1 keeps
# the glyphs of ASCII and the Latin-script string tables (EN FR GE IT SP): 133 frames. The engine maps characters to
# frames at run time (SetSpriteString: the first frame with that unicodeChar), so keeping the order keeps the result for
# every kept character; a dropped one (CJK) maps to -1 as a character the font lacks.
FONT_TRIM = {'Sprites/UI/SmallFont.bin': ('EN', 'FR', 'GE', 'IT', 'SP')}
# Animations the PS1 never shows, their frames dropped (the animation entries stay, so every animation ID keeps its
# meaning): Sprites/Global/SuperButtons.bin (3.1, docs/32): HUD_GetButtonFrame picks the prompt animation from
# UIButtonPrompt_GetGamepadType, and the PS1 pad reports DEVICE_TYPE_CONTROLLER / DEVICE_PS4 (PS1InputDevice.cpp), so
# the five PC keyboard layouts (97 frames each: 485 of the file's 530) are unreachable.
ANIM_TRIM = {'Sprites/Global/SuperButtons.bin': {1: 'PC EN/JP', 9: 'PC FR', 10: 'PC IT', 11: 'PC GE', 12: 'PC SP'}}


# docs/32 7.1: frames wider than any screen position the PS1's 320 columns can show, cropped to what they can show
# (x + x0, width w, pivot x + x0: the same pixels land in the same screen columns). Sprites/SpecialBS/Globe.bin's turn
# animation: 512 x 240 frames drawn at the screen centre (pivot -256) by BSS_Setup, whose camera never scrolls sideways
# (ScreenInfo->position.x = 0x100 - center.x every frame): columns 96..415 only (symmetric: a mirrored draw too)
FRAME_CROP = {'Sprites/SpecialBS/Globe.bin': {0: (96, 320)}}


def crop_sprite(data, rules):
    """The sprite file with rules[animation] = (x0, w) applied to every frame of those animations."""
    r = F.R(data)
    assert r.bytes(4) == b'SPR\0'
    r.u32()
    for _ in range(r.u8()):
        r.string()
    hitboxes = r.u8()
    for _ in range(hitboxes):
        r.string()
    out = bytearray(data)
    for ai in range(r.u16()):
        r.string()
        n = r.u16(); r.i16(); r.u8(); r.u8()
        for _ in range(n):
            r.u8(); r.i16(); r.u16()
            at = r.p
            x, y, w, h, px, py = struct.unpack_from('<6h', data, at)
            if ai in rules:
                x0, w2 = rules[ai]
                assert x0 + w2 <= w, 'crop past the frame'
                struct.pack_into('<6h', out, at, x + x0, y, w2, h, px + x0, py)
            r.bytes(12 + 8 * hitboxes)
    r.done('sprite')
    return bytes(out)


def font_charset(src_data, langs):
    chars = set(range(0x20, 0x7F))
    for lang in langs:
        text = open(os.path.join(src_data, 'Strings', 'Strings%s.txt' % lang), 'rb').read().decode('utf-16')
        chars |= set(ord(c) for c in text if ord(c) >= 0x20)
    return chars


def trim_sprite(data, keep):
    """The sprite file (Animation.cpp LoadSpriteAnimation's order) with only the frames keep(unicodeChar, animation)
    accepts, each kept frame's bytes unchanged; frame counts rewritten."""
    r = F.R(data)
    assert r.bytes(4) == b'SPR\0'
    r.u32()
    for _ in range(r.u8()):
        r.string()
    hitboxes = r.u8()
    for _ in range(hitboxes):
        r.string()
    head_end = r.p
    anims = []
    for ai in range(r.u16()):
        a0 = r.p
        r.string()
        count_at = r.p
        n = r.u16(); r.i16(); loop = r.u8(); r.u8()
        frames = []
        for _ in range(n):
            f0 = r.p
            r.u8(); r.i16(); uc = r.u16()
            r.bytes(12 + 8 * hitboxes)
            if keep(uc, ai):
                frames.append(data[f0:r.p])
        anims.append((data[a0:count_at], data[count_at + 2:count_at + 6], loop, frames))
    r.done('sprite')
    out = bytearray(data[:4]) + struct.pack('<I', sum(len(a[3]) for a in anims)) + data[8:head_end]
    out += struct.pack('<H', len(anims))
    for name, speed_loop_rot, loop, frames in anims:
        assert loop < max(1, len(frames)), 'loop index past the kept frames'
        out += name + struct.pack('<H', len(frames)) + speed_loop_rot + b''.join(frames)
    return bytes(out)


def gif_palette(src):
    """ImageGIF::Load (Sprite.cpp): packed byte at 10, table size 1 << ((packed & 7) + 1) from byte 13; entries
    past it stay 0 (the palette buffer is allocated cleared). A local colour table is not expected in the stage
    tilesets (its engine path writes palette[0x81..0x100]); the tool stops if one appears."""
    d = open(src, 'rb').read()
    assert d[:3] == b'GIF', src
    size = 1 << ((d[10] & 7) + 1)
    pal = bytearray(0x100 * 3)
    pal[:size * 3] = d[13:13 + size * 3]
    p = 13 + size * 3
    p = d.index(b',', p)  # the engine scans byte by byte for the image separator
    packed = d[p + 1 + 8]
    assert not packed >> 7, '%s: local colour table' % src
    return bytes(pal)


def main():
    a = sys.argv[1:]
    src = os.path.abspath(a[0] if a else F.data_root())
    root = os.path.abspath(a[1] if len(a) > 1 else os.path.join(REPO, 'build', 'iso'))
    dst = os.path.join(root, 'Data')
    if os.path.exists(dst):
        shutil.rmtree(dst)
    n = {'copied': 0, 'scene': 0, 'tileconfig': 0, 'blocks': 0, 'pal': 0, 'skipped': 0}
    size_in = size_out = 0
    for dirpath, _, files in os.walk(src):
        rel = os.path.relpath(dirpath, src)
        for f in sorted(files):
            if f.startswith('.'):
                continue
            s = os.path.join(dirpath, f)
            d = os.path.join(dst, rel, f)
            ext = os.path.splitext(f)[1].lower()
            top = rel.split(os.sep)[0]
            if top == 'Stages' and f.lower() == '16x16tiles.gif':
                os.makedirs(os.path.dirname(d), exist_ok=True)
                open(d[:-4] + '.pal', 'wb').write(gif_palette(s))
                n['pal'] += 1
                size_out += 0x300
                continue
            if ext in SKIP_EXT or top == 'Shaders':
                n['skipped'] += 1
                continue
            os.makedirs(os.path.dirname(d), exist_ok=True)
            size_in += os.path.getsize(s)
            if top == 'Stages' and f.startswith('Scene') and ext == '.bin':
                out, b = convert_blocks(s, walk_scene)
                out, baked = bake_relations(out)
                if baked:
                    print('data_disc: %s: %d CircleBumpers baked low (ShopWindow)' % (os.path.relpath(s, src), baked))
                chunked, chunks, nrows, nsegs = chunk_scene(out)
                unchunk_check(out, chunked)
                ordered = objects_first(chunked)
                assert len(ordered) == len(chunked)
                open(d, 'wb').write(ordered); n['scene'] += 1; n['blocks'] += b
                n['chunks'] = max(n.get('chunks', 0), chunks)
                n['pool'] = max(n.get('pool', 0), 8 * (chunks + nrows) + 2 * SEG * nsegs)
            elif top == 'Game' and f == 'GameConfig.bin':
                out, kept, dropped = convert_gameconfig(s)
                open(d, 'wb').write(out); n['copied'] += 1
                print('data_disc: GameConfig global variables remapped to the PS1 layout: %d kept, %d dropped (shortened arrays)' % (kept, dropped))
            elif os.path.relpath(s, src).replace(os.sep, '/') in FONT_TRIM:
                chars = font_charset(src, FONT_TRIM[os.path.relpath(s, src).replace(os.sep, '/')])
                data = open(s, 'rb').read()
                out = trim_sprite(data, lambda uc, ai: uc in chars)
                # self-check: read back; the kept frames are the source's in order, nothing else changed
                a, b = F.read_sprite(s), None
                open(d, 'wb').write(out)
                b = F.read_sprite(d)
                for aa, bb in zip(a['anims'], b['anims']):
                    assert [x for x in aa['frames'] if x['unicode'] in chars] == bb['frames']
                    assert {k: v for k, v in aa.items() if k not in ('frames', 'frame_count')} == \
                        {k: v for k, v in bb.items() if k not in ('frames', 'frame_count')}
                assert (a['sheets'], a['hitboxes'], len(a['anims'])) == (b['sheets'], b['hitboxes'], len(b['anims']))
                n['copied'] += 1
                print('data_disc: %s: %d of %d glyphs kept (%s + ASCII), self-checked' % (
                    f, b['frame_count'], a['frame_count'], ' '.join(FONT_TRIM[os.path.relpath(s, src).replace(os.sep, '/')])))
            elif os.path.relpath(s, src).replace(os.sep, '/') in FRAME_CROP:
                rules = FRAME_CROP[os.path.relpath(s, src).replace(os.sep, '/')]
                open(d, 'wb').write(crop_sprite(open(s, 'rb').read(), rules))
                a, b = F.read_sprite(s), F.read_sprite(d)  # self-check: only the cropped fields changed
                for ai, (aa, bb) in enumerate(zip(a['anims'], b['anims'])):
                    for fa, fb in zip(aa['frames'], bb['frames']):
                        if ai in rules:
                            x0, w2 = rules[ai]
                            fa = dict(fa, x=fa['x'] + x0, w=w2, pivot_x=fa['pivot_x'] + x0)
                        assert fa == fb
                n['copied'] += 1
                print('data_disc: %s: frames of animations %s cropped to the PS1 screen' % (f, sorted(rules)))
            elif os.path.relpath(s, src).replace(os.sep, '/') in ANIM_TRIM:
                drop = ANIM_TRIM[os.path.relpath(s, src).replace(os.sep, '/')]
                out = trim_sprite(open(s, 'rb').read(), lambda uc, ai: ai not in drop)
                open(d, 'wb').write(out)
                a, b = F.read_sprite(s), F.read_sprite(d)  # self-check: the kept animations unchanged, the others empty
                for ai, (aa, bb) in enumerate(zip(a['anims'], b['anims'])):
                    assert bb['frames'] == ([] if ai in drop else aa['frames'])
                    assert {k: v for k, v in aa.items() if k not in ('frames', 'frame_count')} == \
                        {k: v for k, v in bb.items() if k not in ('frames', 'frame_count')}
                assert (a['sheets'], a['hitboxes'], len(a['anims'])) == (b['sheets'], b['hitboxes'], len(b['anims']))
                n['copied'] += 1
                print('data_disc: %s: %d of %d frames kept (dropped: %s), self-checked' % (
                    f, b['frame_count'], a['frame_count'], ', '.join(drop.values())))
            elif top == 'Stages' and f == 'TileConfig.bin':
                out, b = convert_blocks(s, walk_tileconfig)
                open(d, 'wb').write(out); n['tileconfig'] += 1; n['blocks'] += b
            else:
                shutil.copyfile(s, d); n['copied'] += 1
            size_out += os.path.getsize(d)
    print('data_disc: scene layouts as 4x4-tile chunk indices in segments + segment, chunk and row pools per scene (largest %d chunks, '
          'largest pools %d bytes, every tile self-checked)' % (n.get('chunks', 0), n.get('pool', 0)))
    print('data_disc: %(scene)d scenes + %(tileconfig)d tile configs rewritten (%(blocks)d blocks raw, self-checked), '
          '%(pal)d tileset palettes, %(copied)d files copied, %(skipped)d source/media files left out' % n)
    print('data_disc: %.1f MB of kept source -> %.1f MB on the disc tree (%s)' % (size_in / 1048576, size_out / 1048576, dst))


if __name__ == '__main__':
    main()
