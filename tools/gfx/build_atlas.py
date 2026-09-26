#!/usr/bin/python3
"""Scene sprite atlas for the Mania PS1 port (docs/32 3.1b; golden rule: build time only).

Input: the scene's manifest (tools/gfx/manifests/<Folder><N>.txt: the sprite files `SPR` and sheets `SHT` the scene
loads, captured from the running game with tools/gfx/capture_manifest.gdb), the sprite files as the disc has them
(build/iso/Data/Sprites: SmallFont and SuperButtons trimmed by tools/data_disc.py) and the source sheets (GIFs).

- Every frame of every animation of the loaded files is placed, as its full rect (Mania's frames are tight: 96% of
  GHZ1's rect area is inside the opaque bounds); identical rects once; a frame inside another frame's rect is drawn
  from it; frames that only overlap are stored apart.
- Player files (Players/*.bin; user decision 2026-09-26, docs/32 3.1 fit): each character's frames reduced to <= 15
  colours per frame with one merge map for all of that character's sheets (a colour folded into its nearest colour in
  RGB, least pixels x distance first, until every frame has <= 15): shades can't flicker between frames and palette
  effects (Super) stay coherent. Every other sprite is exact.
- Big sprites at half resolution (user decision 2026-09-26: bosses, big effects, UI art), only where the scene needs
  it: the fewest HALF_PLAN steps that fit (bosses first, then big effects, presentation art, the HUD last, each from a
  side of 64 / 48 / 32 px down); such a frame is stored halved (each 2x2 block -> its most common opaque index) and
  drawn 2x (binding bit 6). Badniks, springs, rings, platforms… are never halved.
- Character art (players, the title screen's Sonic: PLAYER_LIKE) is 15 colours per frame at full size.
- A cluster with <= 15 colours is 4-bit (clusters share 16-entry CLUTs of palette indices, entry 0 transparent),
  otherwise 8-bit on the palette bank's CLUT.
- Clusters are packed (skyline, tallest first) into the texture columns the scene's tiles leave (tools/gfx/vram_plan.py;
  an 8-bit page takes two adjacent columns, or one with u < 128).
Self-check: every frame region is decoded back from the VRAM image + CLUT maps and compared with its source pixels
(after the players' colour merge); the merge's changes are reported.

Output <out>/<Folder>/Scene<N>.atl (little-endian):
  header : 'ATL5', u16 fileCount, clutCount, blockCount, pad, u32 bindOffset, u32 bindBytes, u32 dataOffset,
           u32 pixelBytes                                                                       (28 B)
  file   : u32 md5[4] of the path LoadSpriteAnimation gets, u32 bindingOffset (from bindOffset), u16 frameCount, pad
                                                                                                (24 B)
  clut   : u16 x, y, u8 map[16]                                                                 (20 B)
  block  : u16 x, y, w, h (VRAM halfwords), u32 offset at dataOffset                            (12 B)
  bindings at bindOffset: per file, per frame record in the file's order (animations, then frames): u8 u, v,
           u16 attr = texture column (bits 0-3) | page row (bit 4) | 8-bit (bit 5) | half resolution (bit 6) |
           CLUT slot << 7 (0x1FF = the bank's CLUT); u, v = 0xFF, attr 0xFFFF: not in the atlas
  pixels : at dataOffset (a sector boundary)

Usage: build_atlas.py DATA_DIR DISC_DATA_DIR OUT_DIR FOLDER SCENE_ID [--report]
"""
import hashlib, os, struct, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import vram_plan as V          # noqa: E402
import build_tiles as T        # noqa: E402
import rsdk5_formats as F      # noqa: E402

PAGE = V.PAGE
BANK_CLUT = 0x1FF
HALF_RULES = {}  # file -> side (px) from which its frames are stored at half resolution; set per scene by build_fit


def halve(px):
    """Half resolution for indexed pixels (they can't be averaged): each 2x2 block -> its most common opaque index when
    at least two of the four texels are opaque (ties: the first in the block's raster order), else transparent."""
    h, w = px.shape
    p = np.pad(px, ((0, h % 2), (0, w % 2)))
    blocks = np.stack([p[0::2, 0::2], p[0::2, 1::2], p[1::2, 0::2], p[1::2, 1::2]], -1)
    out = np.zeros(blocks.shape[:2], np.uint8)
    for yy in range(blocks.shape[0]):
        for xx in range(blocks.shape[1]):
            b = [int(v) for v in blocks[yy, xx] if v]
            if len(b) >= 2:
                out[yy, xx] = max(b, key=lambda v: (b.count(v), -b.index(v)))
    return out


class AtlasError(Exception):
    pass


def md5_words(text):
    """GEN_HASH_MD5 (RSDK/Storage/Text.cpp): the MD5 digest as 4 little-endian uint32s."""
    return struct.unpack('<4I', hashlib.md5(text.encode()).digest())


def read_manifest(folder, scene_id):
    path = os.path.join(HERE, 'manifests', '%s%s.txt' % (folder, scene_id))
    out = []
    for line in open(path):
        t = line.split()
        if t and not t[0].startswith('#'):
            out.append((t[0], t[1]))
    return out


class Sheets:
    def __init__(self, data):
        self.data, self.px, self.pal = data, {}, {}

    def get(self, name):
        if name not in self.px:
            im = Image.open(os.path.join(self.data, 'Sprites', name))
            self.px[name] = np.array(im)
            self.pal[name] = im.getpalette()
        return self.px[name]


def merge_colours(regions, pal):
    """Merge map (index -> index) making every region <= 15 colours: fold the colour with the least pixels x squared
    RGB distance to its nearest remaining colour, until no region has more than 15."""
    counts = {}
    sets = []
    for r in regions:
        u, c = np.unique(r, return_counts=True)
        s = set()
        for v, n in zip(u.tolist(), c.tolist()):
            if v:
                counts[v] = counts.get(v, 0) + n
                s.add(v)
        sets.append(s)
    rgb = {i: np.array(pal[i * 3:i * 3 + 3], np.int64) for i in counts}
    mapping = {}

    def target(i):
        while i in mapping:
            i = mapping[i]
        return i
    while True:
        cur = [{target(v) for v in s} for s in sets]
        over = [s for s in cur if len(s) > 15]
        if not over:
            break
        alive = set().union(*cur)
        cands = set().union(*over)
        best = None
        for a in cands:
            for b in alive:
                if b == a:
                    continue
                d = int(((rgb[a] - rgb[b]) ** 2).sum())
                cost = counts[a] * d
                if best is None or cost < best[0]:
                    best = (cost, a, b)
        _, a, b = best
        mapping[a] = b
        counts[b] = counts.get(b, 0) + counts[a]
    return {i: target(i) for i in counts if target(i) != i}


def outer_rects(rects):
    """The rects not inside another one (largest first): a frame inside another frame's rect is drawn from it. Frames
    that only overlap stay separate (merging them into bounding boxes made clusters wider than a texture page, and one
    colour set for unrelated frames)."""
    out = []
    for r in sorted(rects, key=lambda r: (-r[2] * r[3], r)):
        if not any(o[0] <= r[0] and o[1] <= r[1] and r[0] + r[2] <= o[0] + o[2] and r[1] + r[3] <= o[1] + o[3] for o in out):
            out.append(r)
    return out


class Bin:
    """One texture page area: skyline packer over `width` texels x 256 lines."""
    def __init__(self, col, row, depth, width):
        self.col, self.row, self.depth, self.width = col, row, depth, width
        self.sky = [0] * width

    def peek(self, w, h, split=False):
        """split: the item may not cross a column boundary (a 4-bit item in a two-column bin)."""
        best = None
        for u in range(0, self.width - w + 1):
            if split and u // V.COL_HW != (u + w - 1) // V.COL_HW:
                continue
            v = max(self.sky[u:u + w])
            if v + h <= PAGE and (best is None or v < best[1] or (v == best[1] and u < best[0])):
                best = (u, v)
        return best

    def place(self, w, h, split=False):
        best = self.peek(w, h, split)
        if best:
            for i in range(best[0], best[0] + w):
                self.sky[i] = best[1] + h
        return best


# --- selection (docs/32 3.1: user decisions: single characters, players 4-bit; groups swapped where the music changes)
PLAYER_FILES = {'sonic': {'Players/Sonic.bin': 'normal', 'Players/SuperSonic.bin': 'super'},
                'tails': {'Players/Tails.bin': 'normal', 'Players/TailSprite.bin': 'normal'},
                'knux': {'Players/Knux.bin': 'normal'}}
PLAYER_ANIMATED = {'Players/Sonic.bin', 'Players/SuperSonic.bin', 'Players/Tails.bin', 'Players/Knux.bin'}  # ANI_* order
# the player region's alternates beyond the character's own files (Super's sparkles swap in with Super Sonic)
PLAYER_EXTRA = {'Global/SuperSparkle.bin': 'super'}
# per folder: event groups, one at a time in the event region: the title card at the act start, the boss where the boss
# music starts, the sign post / prison where the act-clear music starts
EVENT_GROUPS = {'GHZ': {'start': [('Global/TitleCard.bin', None)],
                        'boss': [('GHZ/DDWrecker.bin', None), ('GHZ/DERobot.bin', None), ('Eggman/EggmanAll.bin', None),
                                 ('Global/Explosions.bin', {2, 3})],
                        'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]}}
# Character art: 15 colours per frame, full resolution (user decision: players lossy; the title screen's big Sonic is
# character art too: 687 KB at 8-bit, more than all the texture VRAM)
PLAYER_LIKE = {'Title/Sonic.bin'}
# What may be stored at half resolution (user decision 2026-09-26: bosses, big effects, UI art); everything else (badniks,
# springs, rings, platforms…) stays full size. HALF_PLAN degrades the least visible first: bosses (on screen at an act's
# end), then big effects, then presentation art, the always-visible HUD last; each step adds to the previous ones and
# build_fit stops at the first that fits.
HALF_BOSS = {'GHZ/DDWrecker.bin', 'GHZ/DERobot.bin', 'Eggman/EggmanAll.bin'}
HALF_EFFECT = {'Global/Explosions.bin', 'Global/Shields.bin', 'Global/Water.bin', 'Global/SpecialRing.bin',
               'Global/Invincible.bin', 'Global/SuperSparkle.bin', 'Global/Dust.bin', 'Global/Announcer.bin'}
HALF_PRESENT = {'Global/TitleCard.bin', 'Global/SignPost.bin', 'Global/EggPrison.bin', 'UI/TextEN.bin', 'UI/SaveSelect.bin',
                'UI/UIElements.bin', 'UI/SmallFont.bin', 'Title/Logo.bin', 'Title/PlusLogo.bin', 'Title/Background.bin',
                'Title/Electricity.bin', 'Logos/Logos.bin'}
HALF_HUD = {'Global/HUD.bin'}
HALF_PLAN = [(HALF_BOSS, 64), (HALF_BOSS, 48), (HALF_BOSS, 32), (HALF_EFFECT, 64), (HALF_PRESENT, 96), (HALF_EFFECT, 48),
             (HALF_PRESENT, 64), (HALF_PRESENT, 48), (HALF_EFFECT, 32), (HALF_PRESENT, 32), (HALF_HUD, 48), (HALF_HUD, 32)]
HALVABLE = HALF_BOSS | HALF_EFFECT | HALF_PRESENT | HALF_HUD
# reviewed by hand (the literal analysis can't follow a handle stored in an entity): in a stage, UIButton_Create sets
# list 0 before PauseMenu (10) / UIDialog (9) set theirs; 7 = a disabled button
ANIM_OVERRIDES = {'UI/TextEN.bin': {0, 7, 9, 10}}
SCOPE_DROP = {'Global/SignPost.bin': {3, 4}}  # Mighty, Ray (base Mania)


def literal_anims(path, names, active, handles_of):
    """Animation IDs the active code passes with this file's handles; None = all (a non-literal ID, or the handle is
    used some other way than SetSpriteAnimation / its load)."""
    import re
    import scene_usage as U
    code = U.active_code(names, active)
    out = set()
    for h in sorted(handles_of.get(path, ())):
        uses = len(re.findall(r'\b' + re.escape(h) + r'\b', code))
        loads = len(re.findall(re.escape(h) + r'\s*=\s*RSDK\.LoadSpriteAnimation', code))
        calls = re.findall(r'SetSpriteAnimation\(\s*' + re.escape(h) + r'\s*,\s*([^,]+?)\s*,', code)
        if uses - loads - len(calls) or any(not c.isdigit() for c in calls):
            return None
        out |= {int(c) for c in calls}
    return out if handles_of.get(path) else None


def select(folder, scene_id, character, files):
    """For each file, each animation: None (not placed) or its placement: 'resident', ('player', alt), ('event', g)."""
    import scene_usage as U
    names, placed, active, _ = U.active_objects(folder, scene_id)
    need = U.needed_files(names, active, [p for p, _, _ in files])
    keep_player, _ = U.player_anims(active, names)
    handles_of = U.file_handles(names)
    groups = EVENT_GROUPS.get(folder, {})
    all_players = set().union(*[set(v) for v in PLAYER_FILES.values()])
    sel, why = {}, {}
    for path, spr, _ in files:
        n = len(spr['anims'])
        if path in all_players:
            if path not in PLAYER_FILES[character]:
                sel[path], why[path] = [None] * n, 'another character'
                continue
            alt = PLAYER_FILES[character][path]
            sel[path] = [('player', alt) if (path not in PLAYER_ANIMATED or ai in keep_player) else None for ai in range(n)]
            why[path] = 'player %s' % alt
            continue
        if need.get(path) is None:
            sel[path], why[path] = [None] * n, 'no active object uses it'
            continue
        anims = ANIM_OVERRIDES.get(path, literal_anims(path, names, active, handles_of))
        drop = SCOPE_DROP.get(path, set())
        place = []
        for ai in range(n):
            if (anims is not None and ai not in anims) or ai in drop:
                place.append(None)
                continue
            where = 'resident'
            if path in PLAYER_EXTRA:
                where = ('player', PLAYER_EXTRA[path])
            for g, members in groups.items():
                for fp, fa in members:
                    if fp == path and (fa is None or ai in fa):
                        where = ('event', g)
            place.append(where)
        sel[path] = place
        why[path] = 'animations %s' % ('all' if anims is None else sorted(anims))
    return sel, why


def pack(clusters, columns, what, bands=None):
    """Skyline-pack clusters (tallest first) into the columns, in order, in halfword units: a 4-bit item is w/4
    halfwords wide, an 8-bit one w/2 (both texture pages start at the column, so depths can share a column). An 8-bit
    item wider than a column (128 texels) takes two adjacent columns (a "wide" bin holds 8-bit items only: a 4-bit page
    is one column). `bands`: (column, row, first free line) areas below other regions, used before whole columns.
    Sets c['bin'], c['u'] (texels in the item's page), c['v']; returns the columns used, or raises AtlasError."""
    free = list(columns)
    bins, over = [], []
    for (col, row, v0) in bands or ():
        if v0 < PAGE:
            b = Bin(col, row, 0, V.COL_HW)
            b.sky = [v0] * V.COL_HW
            b.band = True
            bins.append(b)

    def hw_of(c):
        w = c['px'].shape[1]
        return (w + 3) // 4 if c['depth'] == 4 else (w + 1) // 2

    def new_bin(wide):
        if wide:
            for a in free:
                b = (a[0] + 1, a[1])
                if b in free:
                    free.remove(a); free.remove(b)
                    return Bin(a[0], a[1], 0, 2 * V.COL_HW)
            return None
        if free:
            a = free.pop(0)
            return Bin(a[0], a[1], 0, V.COL_HW)
        return None

    for c in sorted(clusters, key=lambda c: (-c['px'].shape[0], -hw_of(c), c['sheet'], c['rect'], str(c['where']))):
        h, hw = c['px'].shape[0], hw_of(c)
        wide = hw > V.COL_HW
        # best fit: the open bin where the item's top ends lowest (ties: the earlier bin)
        spot, best = None, None
        split = c['depth'] == 4
        for b in bins:
            if hw <= b.width:
                cand = b.peek(hw, h, split and b.width > V.COL_HW)
                if cand and (best is None or cand[1] < best[1][1]):
                    best = (b, cand)
        if best:
            b = best[0]
            spot = b.place(hw, h, split and b.width > V.COL_HW)
        if not spot:
            b = new_bin(wide)
            if b is None:
                over.append(c)
                continue
            bins.append(b)
            spot = b.place(hw, h)
        c['bin'] = b
        c['hw0'] = spot[0]  # halfwords from the bin's left edge
        # texels in the item's page: a 4-bit page is one column (the half of a two-column bin the item is in), an
        # 8-bit page starts at the bin
        c['page_col'] = b.col + (spot[0] // V.COL_HW if c['depth'] == 4 else 0)
        c['u'] = (spot[0] % V.COL_HW) * 4 if c['depth'] == 4 else spot[0] * 2
        c['v'] = spot[1]
    if over:
        kb = sum(c['px'].size // (4 if c['depth'] == 4 else 2) for c in over) * 2 // 1024
        raise AtlasError('%s: %d clusters (%d KB) do not fit %d columns' % (what, len(over), kb, len(columns)))
    return [cp for cp in columns if cp not in free]


def used_lines(clusters):
    """(column, row) -> lines used from the top by these clusters (a wide bin's items count for both columns)."""
    out = {}
    for c in clusters:
        b = c['bin']
        hw0 = c['hw0']
        hw1 = hw0 + ((c['px'].shape[1] + 3) // 4 if c['depth'] == 4 else (c['px'].shape[1] + 1) // 2)
        for k in range(hw0 // V.COL_HW, (hw1 - 1) // V.COL_HW + 1):
            key = (b.col + k, b.row)
            out[key] = max(out.get(key, 0), c['v'] + c['px'].shape[0])
    return out


def kb_of(clusters):
    return sum(c['px'].size // (4 if c['depth'] == 4 else 2) for c in clusters) * 2 / 1024


def render(clusters, work, clut_maps):
    vram = np.zeros((V.VRAM_H, V.VRAM_W), np.uint16)
    lines = {}
    for c in clusters:
        b = c['bin']
        h, w = c['px'].shape
        idx = c['px'].astype(np.uint16)
        X0, Y0 = b.col * V.COL_HW, b.row * PAGE + c['v']
        if c['depth'] == 8:
            trail = w % 2
            idx = np.pad(idx, ((0, 0), (0, trail)))
            hx = X0 + c['hw0']
            words = idx[:, 0::2] | (idx[:, 1::2] << 8)
        else:
            lut = np.zeros(256, np.uint16)
            for n, p in enumerate(clut_maps[c['clut']]):
                if n:
                    lut[p] = n
            q = lut[idx]
            trail = (-w) % 4
            q = np.pad(q, ((0, 0), (0, trail)))
            hx = X0 + c['hw0']
            words = q[:, 0::4] | (q[:, 1::4] << 4) | (q[:, 2::4] << 8) | (q[:, 3::4] << 12)
        vram[Y0:Y0 + h, hx:hx + words.shape[1]] |= words
        span = (hx + words.shape[1] - X0 + V.COL_HW - 1) // V.COL_HW
        for k in range(span):
            key = (b.col + k, b.row)
            lines[key] = max(lines.get(key, 0), c['v'] + h)
    blocks = [(col * V.COL_HW, row * PAGE, V.COL_HW, n) for (col, row), n in sorted(lines.items())]
    return vram, blocks


def decode_check(clusters, vram, work, clut_maps, name):
    n = 0
    for c in clusters:
        b = c['bin']
        h, w = c['px'].shape
        X0, Y0 = b.col * V.COL_HW, b.row * PAGE + c['v']
        # decode through the binding's view: page column + u, as the GPU will read it
        if c['depth'] == 8:
            hx = c['page_col'] * V.COL_HW + c['u'] // 2
            wd = vram[Y0:Y0 + h, hx:hx + (w + 1) // 2].astype(np.int32)
            tex = np.stack([wd & 0xFF, wd >> 8], -1).reshape(h, -1)[:, :w]
        else:
            hx = c['page_col'] * V.COL_HW + c['u'] // 4
            wd = vram[Y0:Y0 + h, hx:hx + (w + 3) // 4].astype(np.int32)
            nib = np.stack([(wd >> s) & 0xF for s in (0, 4, 8, 12)], -1).reshape(h, -1)[:, :w]
            tex = np.array(clut_maps[c['clut']], np.int32)[nib]
        if not np.array_equal(tex, c['px'].astype(np.int32)):
            raise AtlasError('%s: %s %s does not decode back' % (name, c['sheet'], c['rect']))
        n += w * h
    return n


def build(data, disc_data, folder, scene_id, character='sonic', columns=None):
    name = '%s%s/%s' % (folder, scene_id, character)
    man = read_manifest(folder, scene_id)
    sheets = Sheets(data)
    files = []                                  # (path, sprite, [(anim, key)] in record order)
    for kind, path in man:
        if kind == 'SPR':
            s = F.read_sprite(os.path.join(disc_data, 'Sprites', path))
            keys = [(ai, (s['sheets'][f['sheet']] if f['sheet'] < len(s['sheets']) else None, f['x'], f['y'], f['w'], f['h']))
                    for ai, a in enumerate(s['anims']) for f in a['frames']]
            files.append((path, s, keys))
    sel, why = select(folder, scene_id, character, files)

    # the unique sheet rects of every placement (a rect used by two placements is stored in each); a rect is halvable
    # only if every frame using it comes from a HALVABLE file, character art if any does from a PLAYER_LIKE one
    rects = {}                                  # (placement, sheet) -> set(rect)
    halvable, charart = {}, set()
    for path, _, keys in files:
        for ai, (sh, x, y, w, h) in keys:
            where = sel[path][ai]
            if where is not None and sh is not None and w > 0 and h > 0:
                im = sheets.get(sh)
                H, W = im.shape
                x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
                if x1 > x0 and y1 > y0:
                    k = (where, sh, (x0, y0, x1 - x0, y1 - y0))
                    t = HALF_RULES.get(path)
                    prev = halvable.get(k, 0)
                    halvable[k] = None if (t is None or prev is None) else max(prev, t)
                    if path in PLAYER_LIKE or path.startswith('Players/'):
                        charart.add(sh)
            if where is None or sh is None or w <= 0 or h <= 0:
                continue
            im = sheets.get(sh)
            H, W = im.shape
            x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
            if x1 > x0 and y1 > y0:
                rects.setdefault((where, sh), set()).add((x0, y0, x1 - x0, y1 - y0))

    # players: one merge map per character over all its placed frames (normal + Super share the palette layout)
    used_sheets = {sh for (_, sh) in rects}
    work = {sh: sheets.get(sh).copy() for sh in used_sheets}
    by_char = {}
    for (where, sh), rs in rects.items():
        if sh in charart:
            char = sh.split('/')[0] + ''.join(ch for ch in os.path.basename(sh).split('.')[0] if not ch.isdigit())
            by_char.setdefault(char, []).append((sh, rs))
    lossy = {}
    for char, items in sorted(by_char.items()):
        shs = sorted({sh for sh, _ in items})
        regions = [work[sh][y:y + h, x:x + w] for sh, rs in items for (x, y, w, h) in rs]
        mp = merge_colours(regions, sheets.pal[shs[0]])
        changed = total = 0
        lut = np.arange(256, dtype=np.uint8)
        for a_, b_ in mp.items():
            lut[a_] = b_
        for sh in shs:
            after = lut[work[sh]]
            for sh2, rs in items:
                if sh2 == sh:
                    for (x, y, w, h) in rs:
                        changed += int((work[sh][y:y + h, x:x + w] != after[y:y + h, x:x + w]).sum())
                        total += w * h
            work[sh] = after
        lossy[char] = (mp, changed, total)

    clusters = []
    for (where, sh) in sorted(rects, key=str):
        im = work[sh]
        for r in outer_rects(rects[(where, sh)]):
            x, y, w, h = r
            inner = [k for k in halvable if k[0] == where and k[1] == sh and r[0] <= k[2][0] and r[1] <= k[2][1]
                     and k[2][0] + k[2][2] <= r[0] + r[2] and k[2][1] + k[2][3] <= r[1] + r[3]]
            ts = [halvable[k] for k in inner]
            half = bool(ts) and all(t is not None for t in ts) and max(w, h) >= max(ts)
            px = halve(im[y:y + h, x:x + w]) if half else im[y:y + h, x:x + w]
            if px.shape[0] > PAGE or px.shape[1] > PAGE:
                raise AtlasError('%s %s is larger than a texture page' % (sh, r))
            cols = set(np.unique(px).tolist()) - {0}
            clusters.append(dict(where=where, sheet=sh, rect=r, px=px, half=half, colors=cols, depth=4 if len(cols) <= 15 else 8))

    # CLUTs shared by every placement
    groups = []
    for c in sorted((c for c in clusters if c['depth'] == 4), key=lambda c: (-len(c['colors']), c['sheet'], c['rect'], str(c['where']))):
        best = None
        for gi, g in enumerate(groups):
            u = len(g | c['colors'])
            if u <= 15 and (best is None or u - len(g) < best[1]):
                best = (gi, u - len(g))
        if best:
            groups[best[0]] |= c['colors']
            c['clut'] = best[0]
        else:
            groups.append(set(c['colors']))
            c['clut'] = len(groups) - 1
    if len(groups) > len(V.SPRITE_CLUT_SLOTS):
        raise AtlasError('%s: %d CLUTs > %d sprite CLUT slots' % (name, len(groups), len(V.SPRITE_CLUT_SLOTS)))
    clut_maps = [[0] + sorted(g) + [0] * (15 - len(g)) for g in groups]

    # regions: resident, then the player region (its alternates share it), then the event region
    if columns is None:
        _, _, _, tblocks = T.build(data, folder, scene_id)
        tcols = {(x // V.COL_HW, y // V.PAGE) for (x, y, w, h) in tblocks}
        columns = [(c, r) for r in (0, 1) for c in V.TEX_COLUMNS if (c, r) not in tcols]
    places = {}
    for c in clusters:
        places.setdefault(c['where'], []).append(c)
    sizes = {w: kb_of(cs) for w, cs in places.items()}
    summary = '%s: resident %.0f KB; player %s; events %s; %d columns (%d KB)' % (
        name, sizes.get('resident', 0), ', '.join('%s %.0f KB' % (w[1], k) for w, k in sizes.items() if w != 'resident' and w[0] == 'player'),
        ', '.join('%s %.0f KB' % (w[1], k) for w, k in sizes.items() if w != 'resident' and w[0] == 'event'), len(columns), len(columns) * 32)
    try:
        used = pack(places.get('resident', []), columns, name + ' resident')
        rest = [cp for cp in columns if cp not in used]
        taken = used_lines(places.get('resident', []))
        rbands = [(col, row, n) for (col, row), n in sorted(taken.items()) if n < PAGE]
        # event groups (large frames: bosses, sign posts) first, into whole columns and the resident's free lines; each
        # group alone, the region = the most any group needs
        ev = sorted((k for k in places if k != 'resident' and k[0] == 'event'), key=str)
        event_cols = []
        for w in ev:
            event_cols = max(event_cols, pack(places[w], rest, '%s event %s' % (name, w[1]), rbands), key=len)
        region_event = rest[:len(event_cols)] if event_cols else []
        for w in ev:
            pack(places[w], region_event, '%s event %s' % (name, w[1]), rbands)
        rest2 = [cp for cp in rest if cp not in region_event]
        # the player region (normal / Super take turns; frames of ~48 px) in the lines left under both, then columns
        taken2 = used_lines([c for w in ['resident'] + ev for c in places.get(w, [])])
        pbands = [(col, row, n) for (col, row), n in sorted(taken2.items()) if n < PAGE]
        pl = sorted((k for k in places if k != 'resident' and k[0] == 'player'), key=str)
        player_cols = []
        for w in pl:
            player_cols = max(player_cols, pack(places[w], rest2, '%s player %s' % (name, w[1]), pbands), key=len)
        region_player = rest2[:len(player_cols)] if player_cols else []
        for w in pl:
            pack(places[w], region_player, '%s player %s' % (name, w[1]), pbands)
    except AtlasError as e:
        return None, 'DOES NOT FIT: %s\n  %s' % (e, summary), clusters, lossy, sel, why

    # the default state (loaded with the scene): resident + the player's normal frames + the title card
    default = [c for c in clusters if c['where'] in ('resident', ('player', 'normal'), ('event', 'start'))]
    vram, blocks = render(default, work, clut_maps)
    checked = decode_check(default, vram, work, clut_maps, name)
    group_images = {}
    for w in sorted((k for k in places if k != 'resident'), key=str):
        gv, gb = render(places[w], work, clut_maps)
        checked += decode_check(places[w], gv, work, clut_maps, name)
        group_images[w] = (gv, gb)

    # bindings: each frame record -> its cluster in a placement
    by = {}
    for c in clusters:
        by.setdefault((c['where'], c['sheet']), []).append(c)

    def binding(where, sh, x, y, w, h):
        for c in by.get((where, sh), ()):
            cx, cy, cw, ch = c['rect']
            if cx <= x and cy <= y and x + w <= cx + cw and y + h <= cy + ch:
                b = c['bin']
                d = 2 if c['half'] else 1
                u, v = c['u'] + (x - cx) // d, c['v'] + (y - cy) // d
                clut = c['clut'] if c['depth'] == 4 else BANK_CLUT
                if u > 255 or v > 255:
                    raise AtlasError('%s: binding past its page: %s %s' % (name, sh, (x, y)))
                return struct.pack('<BBH', u, v, (c['page_col'] & 15) | (b.row & 1) << 4 | (1 if c['depth'] == 8 else 0) << 5
                                   | (1 if c['half'] else 0) << 6 | clut << 7)
        return None
    MISSING = struct.pack('<BBH', 0xFF, 0xFF, 0xFFFF)
    bind, file_tab = bytearray(), bytearray()
    patches = {w: bytearray() for w in group_images}
    counts = {'placed': 0, 'unplaced': 0}
    for fi, (path, s, keys) in enumerate(files):
        file_tab += struct.pack('<4IIH2x', *md5_words(path), len(bind), len(keys))
        for k, (ai, (sh, x, y, w, h)) in enumerate(keys):
            where = sel[path][ai]
            if where is None or sh is None or w <= 0 or h <= 0:
                bind += MISSING
                counts['unplaced'] += 1
                continue
            bd = binding(where, sh, max(0, x), max(0, y), w, h)
            if bd is None:
                raise AtlasError('%s: no cluster for %s %s' % (name, path, (sh, x, y, w, h)))
            counts['placed'] += 1
            bind += bd if where in ('resident', ('player', 'normal'), ('event', 'start')) else MISSING
            if where != 'resident':
                patches[where] += struct.pack('<HH', fi, k) + bd

    clut_tab = b''.join(struct.pack('<HH', *V.SPRITE_CLUT_SLOTS[i]) + bytes(m) for i, m in enumerate(clut_maps))

    def blocks_bytes(img, bl):
        tab, px = bytearray(), bytearray()
        for (x, y, w, h) in bl:
            tab += struct.pack('<HHHHI', x, y, w, h, len(px))
            px += img[y:y + h, x:x + w].astype('<u2').tobytes()
        return tab, px
    blk_tab, pixels = blocks_bytes(vram, blocks)
    gnames = sorted(group_images, key=str)
    g_tabs = []
    for w in gnames:
        gv, gb = group_images[w]
        tab, px = blocks_bytes(gv, gb)
        g_tabs.append((w, tab, px, len(gb)))
    # layout: header, files, CLUTs, default blocks, group records, group block tables, patches, bindings, pixels
    head = 32 + len(file_tab) + len(clut_tab) + len(blk_tab) + 32 * len(gnames)
    tables = bytearray()
    grec = bytearray()
    off = head
    g_offsets = []
    for (w, tab, px, nb) in g_tabs:
        g_offsets.append((off, len(tables)))
        tables += tab
        off += len(tab)
    p_offsets = []
    for (w, tab, px, nb) in g_tabs:
        p_offsets.append(off)
        tables += patches[w]
        off += len(patches[w])
    bind_off = off
    data_off = (bind_off + len(bind) + 2047) // 2048 * 2048
    pix_off = data_off + ((len(pixels) + 2047) // 2048) * 2048
    body_px = bytearray(pixels) + bytes((-len(pixels)) % 2048)
    for i, (w, tab, px, nb) in enumerate(g_tabs):
        region = 0 if w[0] == 'player' else 1
        grec += struct.pack('<8sBBHHHIIII', w[1].encode()[:8], region, 1 if w in (('player', 'normal'), ('event', 'start')) else 0,
                            nb, len(patches[w]) // 8, 0, g_offsets[i][0], p_offsets[i], pix_off, len(px))
        body_px += px + bytes((-len(px)) % 2048)
        pix_off += (len(px) + 2047) // 2048 * 2048
    out = bytearray(struct.pack('<4sHHHHIIII4x', b'ATL5', len(files), len(clut_maps), len(blocks), len(gnames), bind_off,
                                len(bind), data_off, len(pixels)))
    out += file_tab + clut_tab + blk_tab + grec + tables + bind
    assert len(out) == bind_off + len(bind), (len(out), bind_off, len(bind))
    out += bytes(data_off - len(out)) + body_px

    rep = ('%s atlas: %d files, %d frame records (%d placed), %d clusters, CLUTs %d; VRAM default %d KB, regions: resident %d '
           'columns, player %d, events %d (of %d); bindings %d B; file %d B; self-check %d texels\n  %s' % (
               name, len(files), len(bind) // 4, counts['placed'], len(clusters), len(clut_maps),
               sum(w * h for _, _, w, h in blocks) * 2 // 1024, len(used), len(region_player), len(region_event), len(columns),
               len(bind), len(out), checked, summary))
    for char, (mp, changed, total) in sorted(lossy.items()):
        rep += '\n  players %s: %d colours merged (%s), %d of %d texels changed (%.2f%%)' % (
            char, len(mp), ', '.join('%d->%d' % ab for ab in sorted(mp.items())), changed, total, 100.0 * changed / max(1, total))
    return bytes(out), rep, clusters, lossy, sel, why


def build_fit(data, disc_data, folder, scene_id, character='sonic'):
    """build() with the fewest HALF_PLAN steps that fit (full resolution when it all fits)."""
    global HALF_RULES
    last = None
    for n in range(len(HALF_PLAN) + 1):
        HALF_RULES = {}
        for files_, side in HALF_PLAN[:n]:
            for f in files_:
                HALF_RULES[f] = side
        res = build(data, disc_data, folder, scene_id, character)
        if res[0] is not None:
            desc = 'none' if n == 0 else '; '.join(
                '%s >= %d px' % (lab, min((t for f, t in HALF_RULES.items() if f in grp), default=0))
                for lab, grp in (('bosses', HALF_BOSS), ('effects', HALF_EFFECT), ('presentation', HALF_PRESENT), ('HUD', HALF_HUD))
                if any(f in HALF_RULES for f in grp))
            return res[0], res[1] + '\n  big sprites at half resolution: %s' % desc, *res[2:]
        last = res
    return last


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    if len(a) not in (5, 6):
        sys.exit(__doc__)
    data, disc, outdir, folder, sid = a[:5]
    character = a[5] if len(a) > 5 else 'sonic'
    blob, rep, clusters, _, sel, why = build_fit(data, disc, folder, sid, character)
    if '--why' in sys.argv:
        for p in sorted(why):
            print('  %-28s %s' % (p, why[p]))
    print(rep)
    if blob is None:
        sys.exit(1)
    if '--report' not in sys.argv:
        os.makedirs(os.path.join(outdir, folder), exist_ok=True)
        open(os.path.join(outdir, folder, 'Scene%s%s.atl' % (sid, '' if character == 'sonic' else '_' + character)), 'wb').write(blob)


if __name__ == '__main__':
    main()
