#!/usr/bin/python3
"""Scene sprite atlas for the Mania PS1 port (docs/ports/mania/32 3.1b; golden rule: build time only).

Input: the scene's manifest (tools/gfx/manifests/<Folder><N>.txt: the sprite files `SPR` and sheets `SHT` the scene
loads, captured from the running game with tools/gfx/capture_manifest.gdb), the sprite files as the disc has them
(build/iso/Data/Sprites: SmallFont and SuperButtons trimmed by tools/data_disc.py) and the source sheets (GIFs).

- Every frame of every animation of the loaded files is placed, as its full rect (Mania's frames are tight: 96% of
  GHZ1's rect area is inside the opaque bounds); identical rects once; a frame inside another frame's rect is drawn
  from it; frames that only overlap are stored apart.
- Player files (Players/*.bin; user decision 2026-09-26, docs/ports/mania/32 3.1 fit): each character's frames reduced to <= 15
  colours per frame with one merge map for all of that character's sheets (a colour folded into its nearest colour in
  RGB, least pixels x distance first, until every frame has <= 15): shades can't flicker between frames and palette
  effects (Super) stay coherent. Every other sprite is exact.
- Big sprites at half resolution (user decision 2026-09-26: bosses, big effects, UI art), only where the scene needs
  it: the fewest HALF_PLAN steps that fit (9c: level art at 15 colours first, then Super Sonic, big effects, pictures,
  presentation art, set pieces, the HUD's art, bosses last, each from a side of 96 / 64 / 48 / 32 px down; the title
  card's letters and the HUD's text never); such a frame is stored halved (each 2x2 block -> its most common opaque index) and
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
           (7.5: then one entry per DEFORM_SHEETS sheet the scene loads, the md5 of the path LoadSpriteSheet gets, one
           binding: the whole 128x128 sheet, 4-bit, 128-aligned in the last free column, for the texture window)
  clut   : u16 x, y, u8 map[16]                                                                 (20 B)
  block  : u16 x, y, w, h (VRAM halfwords), u32 offset at dataOffset                            (12 B)
  bindings at bindOffset: per file, per frame record in the file's order (animations, then frames): u8 u, v,
           u16 attr = texture column (bits 0-3) | page row (bit 4) | 8-bit (bit 5) | half resolution (bit 6) |
           CLUT slot << 7 (0x1FF = the bank's CLUT); u, v = 0xFF, attr 0xFFFF: not in the atlas. 7b.12: quarter
           resolution (QUARTER files, 4-bit only) = bit 6 set and CLUT field = slot + 0x140 (slots < 0xBF)
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
import ps1_text                # noqa: E402  (docs/ports/mania/32 choice 182: the Video page's labels)
import ps1_art                 # noqa: E402  (docs/ports/mania/63 M16: the DualShock 1 drawing)

PAGE = V.PAGE
BANK_CLUT = 0x1FF
HALF_RULES = {}  # file -> side (px) from which its frames are stored at half resolution; set per scene by build_fit
TEXT_KEEP_H = 24  # 4.3: clusters this short stay full resolution (text strips) while a plan with them fits
KEEP_TEXT = True
# 7b: a plan step (LOSSY in HALF_PLAN): level art (every sheet but character art) merged to 15 colours per cluster, as
# the players are, so no cluster needs 8 bits; set per scene by build_fit
LOSSY_LEVEL = False
# 4.3 (user: the pause menu's CONTINUE / RESTART read blocky): animations never halved while the text is kept (the
# pause buttons' text, UI/TextEN.bin list 10; a scene that can't fit them falls back with the other text: GHZ2)
NEVER_HALF_ANIMS = {('UI/TextEN.bin', 10)}
# 9c (user 2026-10-04: the title card's zone name and act number, the HUD's SCORE / TIME / RINGS and its digits read
# blocky): never halved, in any plan step (the text fallback too); ~12 KB in the title card's group, ~3 KB resident
ALWAYS_FULL_ANIMS = {('Global/TitleCard.bin', 1), ('Global/TitleCard.bin', 2), ('Global/TitleCard.bin', 3),
                     ('Global/HUD.bin', 0), ('Global/HUD.bin', 1)}
# 9e: the title's rotozoom pages take 3 columns: of the logo (full since 9c: the user saw its letters blocky) only the
# ribbon's unfurl (animation 1: red / white stripes and a star, no letters; played once as the logo enters) may be halved
ALWAYS_FULL_ANIMS |= {('Title/Logo.bin', a) for a in (0, 2, 3, 4, 5, 6, 7, 8)}
# 9c (MSZ2's title card: the SONIC MANIA strip read garbled): their text strips (<= TEXT_KEEP_H) stay full in the text
# fallback too
ALWAYS_TEXT_FILES = {'Global/TitleCard.bin', 'Global/HUD.bin'}
# 9b.6: animations whose texture the game scrolls by rewriting a frame's sprX (ParallaxSprite's SHIFT attribute: sprX + 0..127,
# CPZ2's masked chemicals): stored as the frame widened by that many texels, drawn from it with PS1SetFrameShift
SHIFT_STRIPS = {('CPZ/CPZParallax.bin', 1): 127, ('CPZ/CPZParallax.bin', 2): 127}
# 7.5: sheets drawn by DrawDeformedSprite (UFO7's plasma, OOZ's smog): whole, in VRAM, 128x128 4-bit
DEFORM_SHEETS = {'SpecialUFO/Plasma.gif', 'OOZ/Smog.gif'}


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


# 9b.1: the sprite files the game loads by the player's character (Player.c's Player_StageLoad, SizeLaser's chibis, the
# Mean Bean boss, Blue Spheres): the manifests were captured as Sonic, so another character's scene takes its own in
# their place (boots as each character check that no frame is missing: g_ps1SpriteMissing)
CHAR_FILES = {
    'Players/Sonic.bin': {'tails': ['Players/Tails.bin', 'Players/TailSprite.bin'], 'knux': ['Players/Knux.bin'],
                          'mighty': ['Players/Mighty.bin'], 'ray': ['Players/Ray.bin']},  # 9g
    'Players/SuperSonic.bin': {'tails': [], 'knux': [], 'mighty': [], 'ray': []},
    'Players/ChibiSonic.bin': {'tails': ['Players/ChibiTails.bin', 'Players/CTailSprite.bin'], 'knux': ['Players/ChibiKnux.bin'],
                               'mighty': ['Players/ChibiMighty.bin'], 'ray': ['Players/ChibiRay.bin']},
    'CPZ/MBMSonic.bin': {'tails': ['CPZ/MBMTails.bin'], 'knux': ['CPZ/MBMKnux.bin'], 'mighty': ['CPZ/MBMMighty.bin'],
                         'ray': ['CPZ/MBMRay.bin']},
    'SpecialBS/Sonic.bin': {'tails': ['SpecialBS/Tails.bin'], 'knux': ['SpecialBS/Knuckles.bin'], 'mighty': ['SpecialBS/Mighty.bin'],
                            'ray': ['SpecialBS/Ray.bin']},
}


def has_ta_gates(folder, scene_id):
    """9b.5: the scene places TimeAttackGate entities (a Time Attack act)."""
    import scene_usage as U
    _, placed, _, _ = U.active_objects(folder, scene_id)
    return 'TimeAttackGate' in placed


def read_manifest(folder, scene_id, character='sonic'):
    path = os.path.join(HERE, 'manifests', '%s%s.txt' % (folder, scene_id))
    data = os.path.join(F.data_root(), 'Sprites')
    out = []
    for line in open(path):
        t = line.split()
        if t and not t[0].startswith('#'):
            # 7b.5: a file the game asks for that the data pack lacks (SSZ's Global/MonarchPlans.bin): the engine's load
            # fails and the game draws nothing from it
            if t[0] == 'SPR' and not os.path.exists(os.path.join(data, t[1])):
                continue
            out.append((t[0], t[1]))
    # 9b.5: Time Attack loads Global/Announcer.bin (Announcer_StageLoad, MODE_TIMEATTACK) for the finish gate's GOAL
    # (Announcer_AnnounceGoal); the manifests were captured in Mania mode: a scene with TimeAttackGate entities gets it
    if ('SPR', 'Global/Announcer.bin') not in out and has_ta_gates(folder, scene_id):
        out.append(('SPR', 'Global/Announcer.bin'))
        out.append(('SHT', 'Global/Display.gif'))
    if character != 'sonic':
        swapped = []
        for k, p in out:
            if k == 'SPR' and p in CHAR_FILES and character in CHAR_FILES[p]:
                swapped += [('SPR', q) for q in CHAR_FILES[p][character] if ('SPR', q) not in out and ('SPR', q) not in swapped]
            swapped.append((k, p)) # (Sonic's stay listed: select() drops another character's frames)
        out = swapped
    return out


class Sheets:
    def __init__(self, data):
        self.data, self.px, self.pal = data, {}, {}

    def get(self, name):
        if name not in self.px:
            im = Image.open(os.path.join(self.data, 'Sprites', name))
            if name == ps1_art.SHEET:  # docs/ports/mania/63 M16: the Options > Controls pad drawing is a DualShock 1
                im = ps1_art.pad_frame(im)
            self.px[name] = np.array(im)
            if name == ps1_text.SHEET:  # docs/ports/mania/32 choice 182: the PS1 Video page's labels in rows below (tools/data_disc.py)
                self.px[name] = ps1_text.extend_sheet(self.data, self.px[name])
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
        self.limit = PAGE  # 7b: a band may end above the page's last line (the sprite CLUT rows' scratch)

    def peek(self, w, h, split=False):
        """split: the item may not cross a texture page boundary: True / COL_HW = a column (4-bit), 2 * COL_HW = two
        (an 8-bit page, from the bin's left edge; 4.2b: bins of several pages)."""
        best = None
        pg = V.COL_HW if split is True else split
        for u in range(0, self.width - w + 1):
            if pg and u // pg != (u + w - 1) // pg:
                continue
            v = max(self.sky[u:u + w])
            if v + h <= self.limit and (best is None or v < best[1] or (v == best[1] and u < best[0])):
                best = (u, v)
        return best

    def place(self, w, h, split=False):
        best = self.peek(w, h, split)
        if best:
            for i in range(best[0], best[0] + w):
                self.sky[i] = best[1] + h
        return best


# --- selection (docs/ports/mania/32 3.1: user decisions: single characters, players 4-bit; groups swapped where the music changes)
PLAYER_FILES = {'sonic': {'Players/Sonic.bin': 'normal', 'Players/SuperSonic.bin': 'super'},
                'tails': {'Players/Tails.bin': 'normal', 'Players/TailSprite.bin': 'normal'},
                'knux': {'Players/Knux.bin': 'normal'},
                'mighty': {'Players/Mighty.bin': 'normal'}, 'ray': {'Players/Ray.bin': 'normal'}}  # 9g: Mania Plus
PLAYER_ANIMATED = {'Players/Sonic.bin', 'Players/SuperSonic.bin', 'Players/Tails.bin', 'Players/Knux.bin',
                   'Players/Mighty.bin', 'Players/Ray.bin'}  # ANI_* order
# the player region's alternates beyond the character's own files (Super's sparkles swap in with Super Sonic)
PLAYER_EXTRA = {'Global/SuperSparkle.bin': 'super'}
# 9b.2b: scenes where the player turns Super without Super's music (ERZ's cutscene transforms him for the final fight,
# under its own boss track): the Super frames sit with the normal ones from the start (the region swaps on Super.ogg)
SUPER_FROM_START = {('ERZ', '1')}
# 9i (user 2026-10-09: pixelated bosses; choice 181): the zone's own sheets a boss fight never draws (measured: the fight
# harness's sheet counts, tools/play/boss/fight_sheets.py, READY to the next scene) go with the act's start group instead
# of the resident region: they share the event region with the boss group, which the boss music swaps in, so the boss
# takes their VRAM at full size. Only zone sheets: the Global ones a fight can show stay resident (Items: rings scattered
# by a hit; Shields; Explosions; Display; Animals; Objects*), and an animation moves only if all its frames are on them
# (as much as the boss needs: the event region is the largest of its groups, in 32 KB columns; SPZ1's Heavy Gunner is
# in its start group already, so it gains nothing)
BOSS_SHARE = {('PSZ1', '1'): {'PSZ1/Enemies.gif'},
              ('PSZ2', '2'): {'PSZ2/Enemies.gif', 'PSZ2/Objects2.gif', 'PSZ2/Objects4.gif', 'PSZ2/Objects-Temp.gif'},
              ('MSZ', '2'): {'MSZ/Enemies.gif', 'MSZ/Objects1.gif', 'MSZ/Train.gif'},
              ('SPZ2', '1'): {'SPZ2/Enemies.gif', 'SPZ2/Objects2.gif', 'SPZ2/Objects3.gif', 'SPZ2/Electric.gif'}}
# per folder: event groups, one at a time in the event region: the title card at the act start, the boss where the boss
# music starts, the sign post / prison where the act-clear music starts
# 9b.2b: the boss explosions (Explosions.bin 2 / 3) stay resident where level objects make them too: FBZ's missiles and
# mines (the puff, 3), SSZ1's flower pods and teleporters, OOZ's OOZSetup, LRZ1's Rexons and intro, TMZ1's crash tests
EVENT_GROUPS = {'GHZ': {'start': [('Global/TitleCard.bin', None)],
                        'boss': [('GHZ/DDWrecker.bin', None), ('GHZ/DERobot.bin', None), ('Eggman/EggmanAll.bin', None),
                                 ('Global/Explosions.bin', {2, 3})],
                        'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                # 7b: CPZ1's Amoeba Droid, CPZ2's Mean Bean Machine (Eggman, the Puyo beans, the player's MBM picture)
                # 9b.2b: CPZ2's Mean Bean Machine is on screen (its control panels) and starts its entrance before the boss
                # music (an 80-frame fade) brings the boss group: its art is in the title card's group too
                'CPZ2': {'start': [('Global/TitleCard.bin', None), ('Eggman/EggmanCPZ.bin', None), ('CPZ/MBMSonic.bin', None),
                                   ('CPZ/MBMTails.bin', None), ('CPZ/MBMKnux.bin', None)],
                         'boss': [('CPZ/AmoebaDroid.bin', None), ('Eggman/EggmanCPZ.bin', None), ('Puyo/PuyoBeans.bin', None),
                                  ('Puyo/Combos.bin', None), ('Puyo/PuyoIndicator.bin', None), ('Puyo/PuyoUI.bin', None),
                                  ('CPZ/MBMSonic.bin', None), ('CPZ/MBMTails.bin', None), ('CPZ/MBMKnux.bin', None),
                                  ('Global/Explosions.bin', {2, 3})],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'CPZ': {'start': [('Global/TitleCard.bin', None)],
                        'boss': [('CPZ/AmoebaDroid.bin', None), ('Eggman/EggmanCPZ.bin', None), ('Puyo/PuyoBeans.bin', None),
                                 ('Puyo/Combos.bin', None), ('Puyo/PuyoIndicator.bin', None), ('Puyo/PuyoUI.bin', None),
                                 ('CPZ/MBMSonic.bin', None), ('CPZ/MBMTails.bin', None), ('CPZ/MBMKnux.bin', None),
                                 ('Global/Explosions.bin', {2, 3})],
                        'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                # 7b: SPZ1's Heavy Gunner, SPZ2's Weather Mobile (Eggman's mobile); the act-2 end's TV picture (the Egg TVs
                # stand in the level: normal group)
                # 9b.2b: the Heavy Gunner's Egg Robos and helicopter are made in the frame its boss music starts fading
                # in (drawn ~400 frames before the boss group's load): in the title card's group too
                'SPZ1': {'start': [('Global/TitleCard.bin', None), ('SPZ1/Boss.bin', None)],
                         'boss': [('SPZ1/Boss.bin', None), ('Global/Explosions.bin', {2, 3})],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                # 9b.2b: the flying prison comes in under the boss music (the Weather Mobile turns into it), and the
                # weather TV (WeatherTV draws from WeatherMobile.bin) stays on screen through the act clear and the outro:
                # both in the boss and the end group
                # 9i: the arena's giant Weather TV (WeatherMobile.bin's Television / Background, anims 0 / 1) is on screen
                # before the boss music brings the boss group (377 missing draws in the fight's harness run): in the
                # title card's group too
                'SPZ2': {'start': [('Global/TitleCard.bin', None), ('SPZ2/WeatherMobile.bin', {0, 1})],
                         'boss': [('SPZ2/WeatherMobile.bin', None), ('Eggman/EggMobile.bin', None),
                                  ('Global/Explosions.bin', {2, 3}), ('Global/EggPrison.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None),
                                 ('SPZ2/TVFlyingBattery.bin', None),
                                 ('SPZ2/WeatherMobile.bin', {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 25})]},
                # 7b: FBZ1's Big Squeeze, FBZ2's Spider Mobile (Eggman in both); act 2's end leaves on the hang glider
                'FBZ': {'start': [('Global/TitleCard.bin', None)],
                        'boss': [('FBZ/BigSqueeze.bin', None), ('FBZ/SpiderMobile.bin', None), ('Eggman/EggmanFBZ.bin', None),
                                 ('Global/Explosions.bin', {2})],
                        # 9b.2b: FBZ1's crushers close in again in the outro, after the act clear
                        'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None), ('FBZ/HangGlider.bin', None),
                                ('FBZ/BigSqueeze.bin', {0})]},
                # 7b: PSZ1's Shiversaw, PSZ2's Heavy Shinobi (PSZ1's hang glider is its intro, under the stage music:
                # level frames; PSZ2's outro art too, unless VRAM needs it)
                # (9i: Shiversaw comes on screen as its music's transition starts: Music.c asks for the boss group then)
                'PSZ1': {'start': [('Global/TitleCard.bin', None)],
                         'boss': [('PSZ1/Shiversaw.bin', None), ('Global/Explosions.bin', {2, 3})],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                # 9b.2b: the outro's Eggman (at his console past the arena: on screen only in the outro, after the act
                # clear) turns round and brings out the Phantom Ruby
                'PSZ2': {'start': [('Global/TitleCard.bin', None)],
                         'boss': [('PSZ2/Shinobi.bin', None), ('Global/Explosions.bin', {2, 3})],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None), ('Eggman/EggmanPSZ.bin', None),
                                 ('PSZ2/ControlPanel.bin', None), ('Global/PhantomRuby.bin', None)]},
                # 7b: SSZ1's Hotaru Hi-Watt; SSZ2M is the Metal Sonic fight from its start (level frames, halvable)
                # 9i: the Hotaru Hi-Watt's heads and bulb (anims 0, 1, 5) form before its boss music brings the boss group (14 draws
                # of nothing in the fight's harness run): in the title card's group too
                'SSZ1': {'start': [('Global/TitleCard.bin', None), ('SSZ1/HotaruHiWatt.bin', {0, 1, 5})],
                         'boss': [('SSZ1/HotaruHiWatt.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'SSZ2': {'start': [('Global/TitleCard.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                # 7b: HCZ1's Screw Mobile / Dive Eggman, HCZ2's Laundro Mobile
                # 9b.2b: HCZ1's Screw Mobile waits parked in view until the player climbs in (which starts the boss
                # music; Dive Eggman swims off with his bomb at once, before the boss group's load); MMZ1's pistons and
                # barrier stand in the arena before Egg Pistons drops in: in the title card's group too
                'HCZ1': {'start': [('Global/TitleCard.bin', None), ('HCZ/ScrewMobile.bin', None), ('HCZ/DiveEggman.bin', None)],
                         'boss': [('HCZ/ScrewMobile.bin', None), ('HCZ/DiveEggman.bin', None), ('Eggman/EggmanHCZ1.bin', None),
                                  ('HCZ/LaundroMobile.bin', None), ('Eggman/EggmanHCZ2.bin', None),
                                  ('Global/Explosions.bin', {2, 3})],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'MMZ1': {'start': [('Global/TitleCard.bin', None), ('MMZ/EggPistonsMKII.bin', None)],
                         'boss': [('MMZ/EggPistonsMKII.bin', None), ('Eggman/EggmanMMZ1.bin', None), ('MMZ/Gachapandora.bin', None),
                                  ('Eggman/EggmanMMZ2.bin', None), ('Global/Explosions.bin', {2, 3})],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'HCZ': {'start': [('Global/TitleCard.bin', None)],
                        'boss': [('HCZ/ScrewMobile.bin', None), ('HCZ/DiveEggman.bin', None), ('Eggman/EggmanHCZ1.bin', None),
                                 ('HCZ/LaundroMobile.bin', None), ('Eggman/EggmanHCZ2.bin', None),
                                 ('Global/Explosions.bin', {2, 3})],
                        'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                # 7b: MSZ's Heavy Magician and the Rogues it summons; the boss explosions stay level frames (MSZ1K's
                # objects use them); Sonic's MSZ1 (the train) shows the Heavy Magician all along: no boss group
                # 9i: the Heavy Magician's magic box (anim 10) stands in view from its Create, before the boss music's
                # transition (72 missing draws in MSZ2's fight run): in the title card's group too
                'MSZ': {'start': [('Global/TitleCard.bin', None), ('MSZ/HeavyMystic.bin', {10})],
                        'boss': [('MSZ/HeavyMystic.bin', None), ('MSZ/Rogues.bin', None)],
                        'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'MSZ1': {'start': [('Global/TitleCard.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                # 7b: OOZ1's Meter Droid, OOZ2's Mega Octus
                'OOZ1': {'start': [('Global/TitleCard.bin', None)],
                         'boss': [('OOZ/MeterDroid.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'OOZ2': {'start': [('Global/TitleCard.bin', None)],
                         'boss': [('OOZ/MegaOctus.bin', None), ('Eggman/EggmanOOZ.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                # 7b: LRZ1's Drillerdroid; LRZ2 has no boss; LRZ3 opens with the Heavies in its cutscene (level frames)
                'LRZ1': {'start': [('Global/TitleCard.bin', None)],
                         'boss': [('LRZ1/Drillerdroid.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'LRZ2': {'start': [('Global/TitleCard.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'LRZ3': {'start': [('Global/TitleCard.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                # 7b: MMZ1's Egg Pistons MKII, MMZ2's Gachapandora (Eggman in each)
                # 7b: TMZ1's Crimson Eye; TMZ2 has no boss; TMZ3 is the Phantom Egg fight from its start (level frames)
                # 9b.2b: the boss's elevator (its arrows) carries the player to act 2 after the act clear
                'TMZ1': {'start': [('Global/TitleCard.bin', None)],
                         'boss': [('TMZ1/CrimsonEye.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None), ('TMZ1/CrimsonEye.bin', None)]},
                'TMZ2': {'start': [('Global/TitleCard.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'TMZ3': {'start': [('Global/TitleCard.bin', None)],
                         'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]},
                'MMZ': {'start': [('Global/TitleCard.bin', None)],
                        'boss': [('MMZ/EggPistonsMKII.bin', None), ('Eggman/EggmanMMZ1.bin', None), ('MMZ/Gachapandora.bin', None),
                                 ('Eggman/EggmanMMZ2.bin', None), ('Global/Explosions.bin', {2, 3})],
                        'end': [('Global/SignPost.bin', None), ('Global/EggPrison.bin', None)]}}
# 7b: art made per character (the zones' intro cutscenes: Players/Cutscene<Zone>.bin draws from <Char>Cutscene<Zone>.gif;
# CPZ2's Mean Bean Machine: CPZ/MBM<Char>.bin): only the atlas's character's (single characters; Mighty / Ray out of
# scope), by file name or by the sheets an animation's frames use
CHAR_NAMES = {'sonic': 'Sonic', 'tails': 'Tails', 'knux': 'Knux', 'mighty': 'Mighty', 'ray': 'Ray'}
ALL_CHAR_NAMES = ('Sonic', 'Tails', 'Knux', 'Mighty', 'Ray')


# 7b.4: files whose animations are per character by name ("<Char> Idle", "Super Sonic Left": Ice.c picks
# ICEANI_<char>IDLE from the player's character and Super state): other characters' animations stay out, Super Sonic's
# go to the player region's Super group
CHAR_ANIM_FILES = {'PSZ1/Ice.bin', 'PSZ2/Ice.bin'}
CHAR_ANIM_PREFIX = {'Sonic': 'Sonic', 'Tails': 'Tails', 'Knux': 'Knux', 'Knuckles': 'Knux', 'Mighty': 'Mighty', 'Ray': 'Ray'}


def char_anim_place(path, anim_name, character):
    """(handled, place) for a per-character animation of a CHAR_ANIM_FILES file."""
    if path not in CHAR_ANIM_FILES:
        return False, None
    if anim_name.startswith('Super Sonic'):
        return True, ('player', 'super') if character == 'sonic' else None
    for word, c in CHAR_ANIM_PREFIX.items():
        if anim_name == word or anim_name.startswith(word + ' '):
            return True, (None if c != CHAR_NAMES[character] else 'resident')
    return False, None


def other_character(name, character):
    """True when a file / sheet name is another character's per-character art (<Char>Cutscene*, MBM<Char>)."""
    base = os.path.basename(name).split('.')[0]
    for c in ALL_CHAR_NAMES:
        if (base.startswith(c + 'Cutscene') or base == 'MBM' + c) and c != CHAR_NAMES[character]:
            return True
    return False
# Character art: 15 colours per frame, full resolution (user decision: players lossy; the title screen's big Sonic is
# character art too: 687 KB at 8-bit, more than all the texture VRAM)
PLAYER_LIKE = {'Title/Sonic.bin', 'SpecialBS/Sonic.bin', 'SpecialBS/Tails.bin', 'SpecialBS/Knuckles.bin'}
# docs/ports/mania/32 7.1: Blue Spheres' own character files (BSS_Player loads every one): single characters, one per atlas
BSS_PLAYER_FILES = {'sonic': 'SpecialBS/Sonic.bin', 'tails': 'SpecialBS/Tails.bin', 'knux': 'SpecialBS/Knuckles.bin',
                    'mighty': 'SpecialBS/Mighty.bin', 'ray': 'SpecialBS/Ray.bin'}
# What may be stored at half resolution (user decision 2026-09-26: bosses, big effects, UI art); everything else (badniks,
# springs, rings, platforms…) stays full size. HALF_PLAN degrades the least visible first: bosses (on screen at an act's
# end), then big effects, then presentation art, the always-visible HUD last; each step adds to the previous ones and
# build_fit stops at the first that fits.
HALF_BOSS = {'GHZ/DDWrecker.bin', 'GHZ/DERobot.bin', 'Eggman/EggmanAll.bin',
             'CPZ/AmoebaDroid.bin', 'Eggman/EggmanCPZ.bin', 'CPZ/MBMSonic.bin', 'CPZ/MBMTails.bin', 'CPZ/MBMKnux.bin',  # 7b
             'SPZ2/WeatherMobile.bin', 'Eggman/EggMobile.bin', 'SPZ2/EggTV.bin',  # 7b
             'FBZ/BigSqueeze.bin', 'FBZ/SpiderMobile.bin', 'Eggman/EggmanFBZ.bin', 'PSZ1/Shiversaw.bin',  # 7b
             'SSZ1/HotaruHiWatt.bin', 'SSZ2/MetalSonic.bin', 'SSZ2/GigaMetal.bin', 'SSZ2/MSFactory.bin', 'Eggman/EggmanSSZ.bin',
             'HCZ/ScrewMobile.bin', 'HCZ/DiveEggman.bin', 'Eggman/EggmanHCZ1.bin', 'HCZ/LaundroMobile.bin', 'Eggman/EggmanHCZ2.bin',
             'OOZ/MeterDroid.bin', 'OOZ/MegaOctus.bin', 'Eggman/EggmanOOZ.bin',
             'LRZ1/Drillerdroid.bin', 'MMZ/EggPistonsMKII.bin', 'Eggman/EggmanMMZ1.bin', 'MMZ/Gachapandora.bin',
             'Eggman/EggmanMMZ2.bin', 'TMZ1/CrimsonEye.bin', 'Phantom/PhantomEgg.bin', 'Phantom/PhantomHand.bin',
             'Phantom/PhantomGunner.bin', 'Phantom/PhantomShinobi.bin', 'Phantom/PhantomMystic.bin', 'Phantom/PhantomRider.bin',
             'Eggman/EggmanTMZ.bin',
             # 4.2d: the Hard-Boiled Heavies (bosses; in the intro cutscenes through CutsceneHBH)
             'SPZ1/Boss.bin', 'PSZ2/Shinobi.bin', 'MSZ/HeavyMystic.bin', 'LRZ3/HeavyRider.bin', 'LRZ3/HeavyKing.bin',
             'MSZ/Rogues.bin', 'Cutscene/DamagedKing.bin', 'Cutscene/HBHPile.bin', 'Cutscene/KingTMZ2.bin'}
HALF_EFFECT = {'Global/Explosions.bin', 'Global/Shields.bin', 'Global/Water.bin', 'Global/SpecialRing.bin',
               'Global/Invincible.bin', 'Global/SuperSparkle.bin', 'Global/Dust.bin', 'Global/Announcer.bin',
               'TMZ1/Portal.bin'}  # 7b.12: the ruby portal (TMZ2 / TMZ3: 20 frames of 126 x 126, 229 KB at 4-bit)
HALF_PRESENT = {'Global/TitleCard.bin', 'Global/SignPost.bin', 'Global/EggPrison.bin', 'UI/TextEN.bin', 'UI/SaveSelect.bin',
                'UI/UIElements.bin', 'UI/SmallFont.bin', 'Title/PlusLogo.bin', 'Title/Background.bin',
                'Title/Electricity.bin', 'Logos/Logos.bin',
                # (9c, user 2026-10-05: the title's SONIC MANIA letters read blocky: Title/Logo.bin stays full size, the
                # title then fits in 19 of 20 columns; 9e: only its ribbon unfurl halvable, ALWAYS_FULL_ANIMS)
                'Title/Logo.bin',
                # 4.2b: the menu's art (the text fonts stay: SmallFont / TextEN only from their plan side up)
                'UI/Diorama.bin', 'UI/Picture.bin', 'UI/MainIcons.bin', 'UI/HeadingsEN.bin', 'UI/Buttons.bin',
                'UI/ButtonLabel.bin', 'UI/MedallionPanel.bin'}
HALF_HUD = {'Global/HUD.bin'}
# 7b (choice 116): Super Sonic (seen only after a transformation) shares the player region with Sonic; at half resolution
# the region is the normal frames' size (Studiopolis: 211 -> 174 KB). Before the always-visible HUD.
HALF_SUPER = {'Players/SuperSonic.bin'}
# 7b (choice 116): a zone's big set pieces and backdrop decorations (Studiopolis: the TV van, shop windows, film
# projector, popcorn machine, signs, parallax props; the cable warp, lotto machine, film reels): their big frames at half
# resolution after every other art but the HUD (the zone's art does not fit VRAM at 15 colours)
HALF_LEVEL = {'SPZ1/TVVan.bin', 'SPZ1/ShopWindow.bin', 'SPZ1/FilmProjector.bin', 'SPZ1/PopcornMachine.bin',
              'SPZ1/Decoration.bin', 'SPZ1/SPZParallax.bin', 'SPZ1/SpinSign.bin',
              'SPZ2/CableWarp.bin', 'SPZ2/LottoMachine.bin', 'SPZ2/FilmReel.bin', 'SPZ2/LottoBall.bin',
              'SPZ2/GreenScreen.bin', 'SPZ2/TVPole.bin',
              # 7b.4: Press Garden's ice (pillars up to 110 px and their 12-frame glint; blocks are ~50 px)
              'PSZ1/Ice.bin', 'PSZ2/Ice.bin',
              'MSZ/Sandworm.bin',  # 7b.8: Mirage Saloon's sandworm (a set piece of big segments)
              'MSZ/Train.bin',  # 9c: MSZ1's train cars (up to 320 px; the act no longer fit with its text kept sharp)
              # 7b.12: Titanic Monarch's mist (shares the ruby portal's sheet clusters) and big set pieces
              'TMZ1/Decoration.bin', 'TMZ1/LargeGear.bin', 'TMZ1/MetalArm.bin', 'TMZ1/CrashTest.bin', 'TMZ1/MagnetSphere.bin',
              'TMZ1/JacobsLadder.bin'}
# 4.2b: pictures (sheets, whatever file draws them): zone previews, the menu dioramas' scenes, controller pictures; halved
# before any presentation file, whose big frames are text (headings, buttons)
HALF_PICTURE = {'UI/Zones.gif', 'UI/Diorama.gif', 'UI/Diorama2.gif', 'UI/Controllers.gif'}
LOSSY = 'lossy'  # 7b: HALF_PLAN's colour step (after the bosses: fewer colours shows less than half resolution)
# 9c (user 2026-10-04: bosses, Hydrocity's Eggman, read blocky; choice 161): fewer colours first, then effects, pictures, presentation, set pieces and the HUD's art; the bosses last (measured over
# the 93 atlases that halve anything: bosses halved in 14 instead of 84)
HALF_PLAN = [(LOSSY, 0), (HALF_SUPER, 32), (HALF_EFFECT, 64), (HALF_PICTURE, 96), (HALF_PRESENT, 96), (HALF_EFFECT, 48),
             (HALF_PRESENT, 64), (HALF_PRESENT, 48), (HALF_EFFECT, 32), (HALF_PRESENT, 32), (HALF_LEVEL, 96), (HALF_LEVEL, 64),
             (HALF_HUD, 48), (HALF_HUD, 32), (HALF_LEVEL, 48), (HALF_LEVEL, 32),
             (HALF_BOSS, 64), (HALF_BOSS, 48), (HALF_BOSS, 32), (HALF_BOSS, 16)]
# 9l (user 2026-10-09: Super Sonic read pixelated; choice 180): Super Sonic full size where the scene then fits by halving
# no more than big effects (>= 48 px), pictures and the big presentation art (>= 96 px: no sign post / egg prison); else
# HALF_PLAN as it was (Super halved first), so no scene halves anything it did not before. Measured over every scene as
# Sonic: GHZ, CPZ, FBZ, OOZ, MMZ, LRZ2, SSZ, TMZ2, the Title keep Super full size
SUPER_FULL_PLAN = [step for step in HALF_PLAN[:6] if step[0] is not HALF_SUPER]
# 7b.12: backdrops far larger than VRAM allows at any plan step, stored at quarter resolution (each texel drawn 4x4) at
# 15 colours whatever the step: Titanic Monarch's sky backdrop (the Monarch, two 1024 x 1024 frames drawn mirrored)
QUARTER = {'TMZ1/MonarchBG.bin'}
QUARTER_CLUT = 0x140
HALVABLE = HALF_BOSS | HALF_EFFECT | HALF_PRESENT | HALF_HUD | HALF_PICTURE | HALF_LEVEL | HALF_SUPER  # files, and (HALF_PICTURE) sheets
# reviewed by hand (the literal analysis can't follow a handle stored in an entity): in a stage, UIButton_Create sets
# list 0 before PauseMenu (10) / UIDialog (9) set theirs; 7 = a disabled button
ANIM_OVERRIDES = {'UI/TextEN.bin': {0, 7, 9, 10},
                  # 9b.5: in Time Attack only GOAL (Announcer_AnnounceGoal); the countdown and player icons are Competition's
                  'Global/Announcer.bin': {0},
                  # 7b: HUD_GetButtonFrame draws the pad's own set (the PS1 pad reports DEVICE_PS4: UIBUTTONPROMPT_PS4 = 3,
                  # RSDK/Input/PS1/PS1InputDevice.cpp) and the icon (0)
                  'Global/SuperButtons.bin': {0, 3},
                  # 7b.14: the credits' fonts and emblems: CreditsSetup picks the animation from each line of Credits.txt
                  # ("[T..": types 0-7, all used)
                  'UI/CreditsText.bin': {0, 1, 2, 3, 4, 5, 6, 7}}
# base Mania (user decision 2026-09-26: no Encore, Mighty, Ray, Competition): animations never placed (None: the whole file)
SCOPE_DROP = {'Global/SignPost.bin': {3, 4},                 # Mighty, Ray
              'Players/Mighty.bin': None, 'Players/Ray.bin': None,
              'SpecialBS/Mighty.bin': None, 'SpecialBS/Ray.bin': None,
              'AIZ/SchrodingersCapsule.bin': None,           # the Encore capsule (Mighty and Ray inside)
              'UI/Diorama.bin': {3, 4, 6, 10, 11}}           # the Plus upsell, Encore, Competition (+ its FBZ platform)
# 9g (Mighty and Ray, a setting off by default): a Plus character's own atlas keeps its files and its sign post face
PLUS_OWN = {'Players/Mighty.bin': 'mighty', 'Players/Ray.bin': 'ray', 'SpecialBS/Mighty.bin': 'mighty',
            'SpecialBS/Ray.bin': 'ray'}
PLUS_SIGNPOST = {'mighty': {4}, 'ray': {3}}  # Global/SignPost.bin: 3 Mighty, 4 Ray (the other one dropped)
SCOPE_DROP_FOLDER = {'Menu': {'Players/KnuxCutsceneHPZ.bin': {4}},  # the Competition diorama's Knuckles
                     # 4.2d: in the Mania-mode intro no Heavy is on screen (HeavyMystic / CutsceneHBH: no entity in
                     # Scene1, only the Encore intro makes them); HeavyMystic's StageLoad loads its sheets regardless
                     'AIZ': {'MSZ/HeavyMystic.bin': None, 'MSZ/Rogues.bin': None}}
# 4.2d: CutsceneHBH loads one file per character into self->aniFrames and animates each with its own IDs (reviewed by
# hand from CutsceneHBH.c: LoadSprites' switch, the *Setup helpers, the states); used where CutsceneHBH is the file's
# only loader (the entity-field analysis would give every file the union)
HBH_ANIMS = {'SPZ1/Boss.bin': {5}, 'PSZ2/Shinobi.bin': {0, 1, 3, 5}, 'MSZ/HeavyMystic.bin': {0},
             'LRZ3/HeavyRider.bin': {0, 2, 3}, 'LRZ3/HeavyKing.bin': {0, 3, 7, 9, 16}, 'MSZ/Rogues.bin': {0, 5, 12},
             'Cutscene/DamagedKing.bin': {0, 1}, 'Cutscene/HBHPile.bin': {0}, 'Cutscene/KingTMZ2.bin': {0, 1, 2}}


def presentation_folders(data):
    """4.2b: folders only the GameConfig's Presentation category lists (Logos, Title, Menu, ...): no player on screen,
    so player files are placed like any other file (the animations the code names), not as a character's region."""
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    light, heavy = set(), set()
    for c in g['categories']:
        for sc in c['scenes']:
            (light if c['name'] == 'Presentation' else heavy).add(sc['folder'])
    return light - heavy


def no_super_folders(data):
    """4.2d: folders only the GameConfig's Presentation / Cutscenes categories list: nobody turns Super there, so the
    player region holds no Super alternate"""
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    light, heavy = set(), set()
    for c in g['categories']:
        for sc in c['scenes']:
            (light if c['name'] in ('Presentation', 'Cutscenes') else heavy).add(sc['folder'])
    return light - heavy


def player_ani_ids():
    """4.2b: the player animation names (Player.h's first enum: ANI_IDLE = 0, ...) -> their IDs"""
    import re
    src = open(os.path.join(HERE, '..', '..', 'SonicMania', 'Objects', 'Global', 'Player.h')).read()
    body = src[src.index('typedef enum {'):]
    body = body[len('typedef enum {'):body.index('}')]
    body = re.sub(r'//[^\n]*', '', body)
    body = re.sub(r'^\s*#.*$', '', body, flags=re.M)  # `#if MANIA_USE_PLUS` (the build's GAME_VERSION has Plus): kept
    out, nxt = {}, 0
    for t in (t.strip() for t in body.split(',')):
        if not t:
            continue
        m = re.match(r'^(ANI_\w+)(?:\s*=\s*(\w+))?$', t)
        assert m, t
        val = nxt if m.group(2) is None else (int(m.group(2), 0) if m.group(2)[0].isdigit() else out[m.group(2)])
        out[m.group(1)] = val
        nxt = val + 1
    return out


def editor_anim_values(code_path, cls, field, scene):
    """7b: the values the scene's `cls` entities give their editable variable `field` (RSDK_EDITABLE_VAR in its own file,
    never assigned there: the ID SetSpriteAnimation gets as `self->field`); None if it is not one"""
    import re
    import scene_usage as U
    code = U.code_of(code_path)
    if not re.search(r'RSDK_EDITABLE_VAR\(\s*' + cls + r'\s*,\s*\w+\s*,\s*' + field + r'\s*\)', code):
        return None
    if re.search(r'self->' + field + r'\s*(?:[-+*/|&^]?=(?!=)|\+\+|--)', code):
        return None  # changed at run time
    h = F.name_hash(field)
    vals = set()
    for o in scene['objects']:
        if o['name'] == cls:
            for e in o['entities']:
                v = e['vars'].get(h)
                if v is None or isinstance(v, (tuple, str)):
                    return None
                vals.add(int.from_bytes(v, 'little', signed=True))
    return vals


def literal_anims(path, names, active, handles_of, never=(), scene=None):
    """Animation IDs the active code passes with this file's handles; None = all (a non-literal ID, or the handle is
    used some other way than SetSpriteAnimation / its load). 7b: an object's editable variable as the ID (`self->type`
    in Decoration) counts the values the scene's entities give it."""
    import re
    import scene_usage as U
    src = U.sources()
    files = U.code_closure(active, set(names), src)
    out = set()
    editor = {}  # call text -> values, per object file that makes the call
    if scene is not None:
        for f in sorted(files):
            cls = os.path.basename(f)[:-2]
            for h in handles_of.get(path, ()):
                for c in re.findall(r'SetSpriteAnimation\(\s*' + re.escape(h) + r'\s*,\s*(self->\w+)\s*,', U.code_of(f)):
                    vals = editor_anim_values(f, cls, c[6:], scene)
                    if vals is None:
                        editor[c] = None
                    elif editor.get(c, set()) is not None:
                        editor[c] = editor.get(c, set()) | vals
    for h in sorted(handles_of.get(path, ())):
        # 4.2b: `never`: objects whose code runs in no entity here (select(): Player in a Presentation scene, the replay
        # recorder anywhere); their files' uses of the handle draw nothing. (Not every entity-less owner: UIWidgets'
        # StageLoad sets static animators that other objects' draws use.)
        skip = {src.get(o) for o in never}
        owner, field = h.split('->', 1)
        ani = player_ani_ids() if path.startswith('Players/') else {}
        if owner[:1].islower():
            # 4.2d: an entity field (`self->aniFrames`, one per entity, the same name in most objects): only the files
            # that load THIS path into it; there any `x->field` passed to SetSpriteAnimation counts (CutsceneHBH loads
            # one of eleven files per character into self->aniFrames and animates it as self-> / hbh->); clearing it
            # (`= 0`) and testing it (`!x->field`) are not draws
            fpat = r'\b[a-z]\w*->' + re.escape(field)  # entity variables (an Object's own field is another handle)
            code = ''.join(U.code_of(f) for f in sorted(files) if f not in skip and re.search(
                re.escape(h) + r'\s*=\s*RSDK\.LoadSpriteAnimation\(\s*"' + re.escape(path) + '"', U.code_of(f)))
            uses = len(re.findall(r'\b' + fpat + r'\b', code))
            benign = len(re.findall(fpat + r'\s*=\s*(?:0\s*;|RSDK\.LoadSpriteAnimation)', code)) + len(re.findall(r'!\s*' + fpat + r'\b', code))
            calls = re.findall(r'SetSpriteAnimation\(\s*' + fpat + r'\s*,\s*([^,]+?)\s*,', code)
            if not code or uses - benign - len(calls) or any(not c.isdigit() and c not in ani for c in calls):
                return None
            out |= {int(c) if c.isdigit() else ani[c] for c in calls}
            continue
        code = ''.join(U.code_of(f) for f in sorted(files) if f not in skip)
        uses = len(re.findall(r'\b' + re.escape(h) + r'\b', code))
        loads = len(re.findall(re.escape(h) + r'\s*=\s*RSDK\.LoadSpriteAnimation', code))
        calls = re.findall(r'SetSpriteAnimation\(\s*' + re.escape(h) + r'\s*,\s*([^,]+?)\s*,', code)
        if uses - loads - len(calls) or any(not c.isdigit() and c not in ani and editor.get(c) is None for c in calls):
            return None
        for c in calls:
            out |= {int(c)} if c.isdigit() else ({ani[c]} if c in ani else editor[c])
    return out if handles_of.get(path) else None


# 9b.2: the act 2 scenes a seamless act end carries its sign post into (Zone_StoreEntities in the act 1 finish keeps the
# players, sign posts and item boxes; OOZ2 shows OOZ1's act clear with its sign post): the sign post is drawn there from
# the act's first frame, before the act-clear music loads the end group, so its frames stay resident there
SIGNPOST_CARRIED = {('GHZ', '2'), ('CPZ', '2'), ('SPZ2', '1'), ('FBZ', '2'), ('PSZ2', '2'), ('HCZ', '2'), ('MSZ', '2'),
                    ('MMZ', '2'), ('LRZ2', '1'), ('TMZ2', '1'), ('OOZ2', '2')}


# 9b.2b: the sign post and the capsules are on screen before the act-clear music loads the end group (a sign post drops
# after a miniboss under the boss group; a capsule shows after the boss, and mid-act duds / springs (FBZ1) under the
# title card's group): resident in every scene (11.6 + 6.7 KB; the end group shrinks by as much)
ALWAYS_RESIDENT = {'Global/SignPost.bin', 'Global/EggPrison.bin'}
# (MSZ1 as Sonic ends in the train cutscene, no act clear: its sign post is never on screen; the atlas is 1 KB short)
ALWAYS_RESIDENT_EXCEPT = {('MSZ', '1', 'sonic')}


def places_of(where):
    """9b.2b: the single placements of an animation's placement (('events', groups): one ('event', g) per group)"""
    return [('event', g) for g in where[1]] if where is not None and where[0] == 'events' else [where]


def select(folder, scene_id, character, files):
    """For each file, each animation: None (not placed) or its placement: 'resident', ('player', alt), ('event', g)."""
    import scene_usage as U
    names, placed, active, _ = U.active_objects(folder, scene_id)
    carried = (folder, scene_id) in SIGNPOST_CARRIED
    if carried and 'SignPost' in names:
        active = set(active) | {'SignPost'}  # 9b.2
    _, _, scene = U.scene_objects(folder, scene_id)  # 7b: editable variables as animation IDs
    need = U.needed_files(names, active, [p for p, _, _ in files])
    keep_player, _ = U.player_anims(active, names, U.scene_objects(folder, scene_id)[2],
                                    None if character == 'sonic' else character)  # 9b.1: its own ability aliases
    handles_of = U.file_handles(names)
    groups = EVENT_GROUPS.get(folder + scene_id, EVENT_GROUPS.get(folder, {}))  # 7b.8: a scene's own entry first
    all_players = set().union(*[set(v) for v in PLAYER_FILES.values()])
    sel, why = {}, {}
    present = folder in presentation_folders(F.data_root())
    no_super = folder in no_super_folders(F.data_root())
    for path, spr, _ in files:
        n = len(spr['anims'])
        drop = SCOPE_DROP_FOLDER.get(folder, {}).get(path, SCOPE_DROP.get(path, set()))
        if PLUS_OWN.get(path) == character:  # 9g
            drop = set()
        elif path == 'Global/SignPost.bin' and character in PLUS_SIGNPOST:
            drop = PLUS_SIGNPOST[character]
        if drop is None:
            sel[path], why[path] = [None] * n, 'out of scope (base Mania)'
            continue
        if other_character(path, character): # 7b
            sel[path], why[path] = [None] * n, 'another character'
            continue
        if path in BSS_PLAYER_FILES.values() and path != BSS_PLAYER_FILES[character]:
            sel[path], why[path] = [None] * n, 'another character'
            continue
        if path in all_players and not present:
            if path not in PLAYER_FILES[character]:
                sel[path], why[path] = [None] * n, 'another character'
                continue
            alt = PLAYER_FILES[character][path]
            if alt == 'super' and (folder, scene_id) in SUPER_FROM_START:
                alt = 'normal'
            if alt == 'super' and no_super:
                sel[path], why[path] = [None] * n, 'no Super in a cutscene'
                continue
            sel[path] = [('player', alt) if (path not in PLAYER_ANIMATED or ai in keep_player) else None for ai in range(n)]
            # 9l (user 2026-10-09: the transformation drew nothing): Player_TryTransform switches Sonic to SuperSonic.bin and
            # plays its ANI_TRANSFORM, but Super.ogg (the super group's load) starts only when that animation ends: its
            # frames go with the normal ones
            if alt == 'super':
                tr = player_ani_ids()['ANI_TRANSFORM']
                if tr < n and sel[path][tr] is not None:
                    sel[path][tr] = ('player', 'normal')
            # 7b: the victory pose shows only after the act clear (ActClear sets it frames after the act-clear music, which
            # loads the end group): with the end group, out of the player region
            if alt == 'normal' and path in PLAYER_ANIMATED and 'end' in groups:
                vic = player_ani_ids()['ANI_VICTORY']
                if vic < n and sel[path][vic] is not None:
                    sel[path][vic] = ('event', 'end')
            # 7b: a pose only the boss's objects set (the Weather Mobile's wind: ANI_FAN) goes with the boss group
            if alt == 'normal' and path in PLAYER_ANIMATED and 'boss' in groups:
                for ai in U.boss_only_poses(active, names, {fp for fp, _ in groups['boss']}):
                    if ai < n and sel[path][ai] is not None:
                        sel[path][ai] = ('event', 'boss')
            why[path] = 'player %s' % alt
            continue
        if need.get(path) is None:
            sel[path], why[path] = [None] * n, 'no active object uses it'
            continue
        # 4.2b: no Player entity exists in a Presentation scene (the code closure counts it active through gameplay
        # helpers): its own uses of the player files draw nothing there; the replay recorder is out of scope (compiled
        # off on the PS1, ReplayRecorder.c REPLAYRECORDER_ENABLED)
        inactive = {'ReplayRecorder'} | ({'Player'} if present else set())
        # ANIM_OVERRIDES were reviewed for stages; a Presentation scene's buttons take their text lists from the scene data
        over = None if present else ANIM_OVERRIDES.get(path)
        if over is None and path in HBH_ANIMS and 'CutsceneHBH' in active and handles_of.get(path) == {'self->aniFrames'}:
            over = HBH_ANIMS[path]
        anims = over if over is not None else literal_anims(path, names, active, handles_of, inactive, scene)
        place = []
        for ai in range(n):
            if (anims is not None and ai not in anims) or ai in drop:
                place.append(None)
                continue
            fr = spr['anims'][ai]['frames']
            if fr and all(f['sheet'] < len(spr['sheets']) and other_character(spr['sheets'][f['sheet']], character) for f in fr):
                place.append(None) # 7b: an animation drawn only from another character's sheets
                continue
            handled, cplace = char_anim_place(path, spr['anims'][ai]['name'], character)
            if handled and cplace != 'resident':
                place.append(cplace)
                continue
            where = 'resident'
            if path in PLAYER_EXTRA:
                where = ('player', PLAYER_EXTRA[path])
                if PLAYER_EXTRA[path] == 'super' and (folder, scene_id) in SUPER_FROM_START:
                    where = ('player', 'normal')
                elif PLAYER_EXTRA[path] == 'super' and no_super:
                    where = None
            gs = sorted({g for g, members in groups.items() for fp, fa in members
                         if fp == path and (fa is None or ai in fa)
                         and (fp not in ALWAYS_RESIDENT or (folder, scene_id, character) in ALWAYS_RESIDENT_EXCEPT)})
            if path == 'Global/Announcer.bin' and not gs and 'start' in groups:
                gs = ['start']  # 9b.5: Time Attack's GOAL comes before any music change (the finish gate): the start group
            if len(gs) == 1:
                where = ('event', gs[0])
            elif gs:  # 9b.2b: in several event groups (boss art still on screen after the act-clear music): a copy in each
                where = ('events', tuple(gs))
            share = BOSS_SHARE.get((folder, scene_id))
            if (where == 'resident' and share and 'boss' in groups and 'start' in groups and fr
                    and all(f['sheet'] < len(spr['sheets']) and spr['sheets'][f['sheet']] in share for f in fr)):
                where = ('event', 'start')  # 9i: level art the boss fight never draws: its VRAM goes to the boss group
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
    for band in bands or ():
        col, row, v0 = band[:3]
        v1 = band[3] if len(band) > 3 else PAGE
        if v0 < v1:
            b = Bin(col, row, 0, V.COL_HW)
            b.sky = [v0] * V.COL_HW
            b.limit = v1
            b.band = True
            bins.append(b)

    def hw_of(c):
        w = c['px'].shape[1]
        return (w + 3) // 4 if c['depth'] == 4 else (w + 1) // 2

    def new_bin(wide, ncols=2):
        if wide:  # ncols adjacent columns of one row (an 8-bit page: 2; 4.2b: a multi-page item: its pages' columns)
            for a in free:
                run = [(a[0] + k, a[1]) for k in range(ncols)]
                if all(r in free for r in run):
                    for r in run:
                        free.remove(r)
                    return Bin(a[0], a[1], 0, ncols * V.COL_HW)
            return None
        if free:
            a = free.pop(0)
            return Bin(a[0], a[1], 0, V.COL_HW)
        return None

    # 4.2b: multi-page items (wider than 256 texels) first, each in its own run of columns from a page's left edge (u 0)
    def multi(c):
        return hw_of(c) > (V.COL_HW if c['depth'] == 4 else 2 * V.COL_HW)

    for c in sorted(clusters, key=lambda c: (not multi(c), -c['px'].shape[0], -hw_of(c), c['sheet'], c['rect'], str(c['where']))):
        h, hw = c['px'].shape[0], hw_of(c)
        pg = V.COL_HW if c['depth'] == 4 else 2 * V.COL_HW  # halfwords per texture page at the item's depth
        spot, best = None, None
        if multi(c):
            b = new_bin(True, -(-hw // pg) * pg // V.COL_HW)
            if b is None:
                over.append(c)
                continue
            bins.append(b)
            spot = b.place(hw, h)
        else:
            wide = hw > V.COL_HW
            # best fit: the open bin where the item's top ends lowest (ties: the earlier bin); never across a page
            for b in bins:
                if hw <= b.width:
                    cand = b.peek(hw, h, pg if b.width > pg else False)
                    # 7b: a capped band (the CLUT rows' free lines: only short strips fit) is taken first, so the
                    # columns keep their lines for the tall frames of the regions packed after
                    if cand and b.limit < PAGE:
                        cand = (cand[0], cand[1] - PAGE)
                    if cand and (best is None or cand[1] < best[1][1]):
                        best = (b, cand)
            if best:
                b = best[0]
                spot = b.place(hw, h, pg if b.width > pg else False)
            if not spot:
                b = new_bin(wide)
                if b is None:
                    over.append(c)
                    continue
                bins.append(b)
                spot = b.place(hw, h)
        c['bin'] = b
        c['hw0'] = spot[0]  # halfwords from the bin's left edge
        # texels in the item's (first) page: pages start at the bin's left edge, every COL_HW (4-bit) / 2 * COL_HW (8-bit)
        c['page_col'] = b.col + (spot[0] // pg) * (pg // V.COL_HW)
        c['u'] = (spot[0] % pg) * (4 if c['depth'] == 4 else 2)
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
            lut = np.zeros(256, np.uint16)  # index 0 stays nibble 0 (transparent): the map's padding entries are 0 too
            for n, p in enumerate(clut_maps[c['clut']]):
                if n and p:
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
            lo, hi = lines.get(key, (PAGE, 0))
            lines[key] = (min(lo, c['v']), max(hi, c['v'] + h))
    # a block covers only the lines its clusters use: a group packed below the resident's lines of a column (bands) must
    # not upload over them (3.4a: the event groups' blocks started at line 0 and would have blanked the HUD's texels)
    blocks = [(col * V.COL_HW, row * PAGE + lo, V.COL_HW, hi - lo) for (col, row), (lo, hi) in sorted(lines.items())]
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
            if ((nib == 0) != (c['px'] == 0)).any():
                raise AtlasError('%s: %s %s: a transparent texel is not nibble 0 (or an opaque one is)' % (name, c['sheet'], c['rect']))
            tex = np.array(clut_maps[c['clut']], np.int32)[nib]
        if not np.array_equal(tex, c['px'].astype(np.int32)):
            raise AtlasError('%s: %s %s does not decode back' % (name, c['sheet'], c['rect']))
        n += w * h
    return n


def build(data, disc_data, folder, scene_id, character='sonic', columns=None):
    name = '%s%s/%s' % (folder, scene_id, character)
    # UFO+ layouts reserve native pause labels even when the rest of the atlas uses the text fallback.
    keep_pause_text = KEEP_TEXT or (folder.startswith('UFO') and scene_id == '2')
    man = read_manifest(folder, scene_id, character)
    sheets = Sheets(data)
    files = []                                  # (path, sprite, [(anim, key)] in record order)
    for kind, path in man:
        if kind == 'SPR':
            s = F.read_sprite(os.path.join(disc_data, 'Sprites', path))
            keys = [(ai, (s['sheets'][f['sheet']] if f['sheet'] < len(s['sheets']) else None, f['x'], f['y'],
                          f['w'] + SHIFT_STRIPS.get((path, ai), 0), f['h']))
                    for ai, a in enumerate(s['anims']) for f in a['frames']]
            files.append((path, s, keys))
    sel, why = select(folder, scene_id, character, files)
    if os.environ.get('ATLAS_FILES'):  # 7b: each file's placed texels (KB at 4-bit; rects it shares are counted per file)
        for path, _, keys in files:
            rs = {}
            for ai, (sh, x, y, w, h) in keys:
                if sel[path][ai] is not None and sh is not None and w > 0 and h > 0:
                    rs.setdefault(sel[path][ai], set()).add((sh, x, y, w, h))
            for where, r in rs.items():
                print('  FILE %-30s %-20s %6.1f KB, largest %s' % (path, where, sum(w * h for _, _, _, w, h in r) / 2048,
                      max((max(w, h) for _, _, _, w, h in r), default=0)))

    # the unique sheet rects of every placement (a rect used by two placements is stored in each); a rect is halvable
    # only if every frame using it comes from a HALVABLE file, character art if any does from a PLAYER_LIKE one
    rects = {}                                  # (placement, sheet) -> set(rect)
    halvable, charart, textish, alltext = {}, set(), {}, {}
    always_halvable = {}                        # every frame using the rect comes from a HALVABLE file (any plan step)
    quarter = {}                                # 7b.12: every frame using the rect comes from a QUARTER file
    for path, _, keys in files:
        for ai, (sh, x, y, w, h), where in ((ai, k, wh) for ai, k in keys for wh in places_of(sel[path][ai])):
            if where is not None and sh is not None and w > 0 and h > 0:
                im = sheets.get(sh)
                H, W = im.shape
                x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
                if x1 > x0 and y1 > y0:
                    k = (where, sh, (x0, y0, x1 - x0, y1 - y0))
                    t = HALF_RULES.get(path, HALF_RULES.get(sh))
                    if (path, ai) in ALWAYS_FULL_ANIMS or ((path, ai) in NEVER_HALF_ANIMS and keep_pause_text):
                        t = None
                    prev = halvable.get(k, 0)
                    halvable[k] = None if (t is None or prev is None) else max(prev, t)
                    # 7b: text lives in presentation / HUD art (the strips KEEP_TEXT protects); a set piece's or a boss's
                    # short strip is not text
                    textish[k] = textish.get(k, False) or path in HALF_PRESENT or path in HALF_HUD
                    alltext[k] = alltext.get(k, False) or path in ALWAYS_TEXT_FILES
                    always_halvable[k] = always_halvable.get(k, True) and (path in HALVABLE or sh in HALVABLE)
                    quarter[k] = quarter.get(k, True) and path in QUARTER
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
    # 7b: the LOSSY plan step: level art's clusters to 15 colours, each cluster its own merge (its nearest colours)
    if LOSSY_LEVEL:
        for (where, sh) in sorted(rects, key=str):
            if sh in charart:
                continue
            for (x, y, w, h) in outer_rects(rects[(where, sh)]):
                region = work[sh][y:y + h, x:x + w]
                if len(set(np.unique(region).tolist()) - {0}) <= 15:
                    continue
                mp = merge_colours([region], sheets.pal[sh])
                lut = np.arange(256, dtype=np.uint8)
                for a_, b_ in mp.items():
                    lut[a_] = b_
                after = lut[region]
                ch, tot = lossy.get(sh, ({}, 0, 0))[1:]
                lossy[sh] = (mp, ch + int((region != after).sum()), tot + w * h)
                work[sh][y:y + h, x:x + w] = after

    clusters = []
    for (where, sh) in sorted(rects, key=str):
        im = work[sh]
        for r in outer_rects(rects[(where, sh)]):
            x, y, w, h = r
            inner = [k for k in halvable if k[0] == where and k[1] == sh and r[0] <= k[2][0] and r[1] <= k[2][1]
                     and k[2][0] + k[2][2] <= r[0] + r[2] and k[2][1] + k[2][3] <= r[1] + r[3]]
            ts = [halvable[k] for k in inner]
            half = bool(ts) and all(t is not None for t in ts) and max(w, h) >= max(ts)
            # 4.3 / 4b (user: readable text): a strip at most TEXT_KEEP_H tall is text (the title's "PRESS ANY BUTTON",
            # headings, buttons): full resolution in every plan step (halved it can't be read)
            if half and h <= TEXT_KEEP_H and (KEEP_TEXT and any(textish.get(k) for k in inner)
                                              or any(alltext.get(k) for k in inner)):
                half = False
            # 4.2b: wider or taller than a texture page (the menu's 294 x 162 diorama backdrop): halved whatever the plan
            # step, when every frame using it may be (else the error below)
            if max(w, h) > PAGE and inner and all(always_halvable[k] for k in inner):
                half = True
            q = bool(inner) and all(quarter[k] for k in inner)
            if q:  # 7b.12: quarter resolution, 15 colours (its own merge)
                half = 'q'
                px = halve(halve(im[y:y + h, x:x + w]))
                if len(set(np.unique(px).tolist()) - {0}) > 15:
                    mp = merge_colours([px], sheets.pal[sh])
                    lut = np.arange(256, dtype=np.uint8)
                    for a_, b_ in mp.items():
                        lut[a_] = b_
                    px = lut[px]  # (the self-check decodes against px; the host reference marks quarter frames approximate)
            else:
                px = halve(im[y:y + h, x:x + w]) if half else im[y:y + h, x:x + w]
            if px.shape[0] > PAGE or px.shape[1] > 4 * PAGE:
                raise AtlasError('%s %s is larger than a texture page' % (sh, r))
            # 4.2b: wider than a page (the menu dioramas' 512 / 1024-texel strips): pack() gives it adjacent pages from
            # a page's left edge and the drawer takes one 256-texel piece per page
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
    if len(groups) > V.SPRITE_CLUT_USABLE:
        raise AtlasError('%s: %d CLUTs > %d sprite CLUT slots' % (name, len(groups), V.SPRITE_CLUT_USABLE))
    clut_maps = [[0] + sorted(g) + [0] * (15 - len(g)) for g in groups]
    # 7b: the sprite CLUT rows this atlas leaves free (under the second framebuffer: page row 1, lines 240-255 of
    # columns 0-4; rows 510-511 are the runtime's scratch) hold short strips
    clut_rows = -(-len(groups) // 20)  # (20 CLUTs a row: vram_plan.SPRITE_CLUT_SLOTS)
    clut_band = [(c, 1, 240 + clut_rows, 254) for c in range(0, 5)] if 240 + clut_rows < 254 else []

    # regions: resident, then the player region (its alternates share it), then the event region
    tbands = []
    if columns is None:
        tbin, _, _, tblocks = T.build(data, folder, scene_id)
        # 7b: the tile CLUT rows the scene leaves free (vram_plan.TILE_CLUT_SLOTS: 32 in rows 240-247, then 20 a row from
        # 248, under the first framebuffer: page row 0, columns 0-4) hold short strips too
        tcluts = int.from_bytes(tbin[6:8], 'little')
        trow = 248 + -(-max(0, tcluts - 32) // 20)
        if trow < 256:
            clut_band = clut_band + [(c, 0, trow, 256) for c in range(0, 5)]
        tcols = {(c, y // V.PAGE) for (x, y, w, h) in tblocks for c in range(x // V.COL_HW, (x + w - 1) // V.COL_HW + 1)}
        columns = [(c, r) for r in (0, 1) for c in V.TEX_COLUMNS if (c, r) not in tcols]
        # 7b: a tile page's lines below its tiles (the .ptl uploads lines 0..n of a page: build_tiles' blocks) take sprites
        tlines = {}
        for (x, y, w, h) in tblocks:
            if w == V.COL_HW and y % V.PAGE == 0:
                tlines[(x // V.COL_HW, y // V.PAGE)] = max(tlines.get((x // V.COL_HW, y // V.PAGE), 0), h)
            else:  # (a whole-page LOD / pattern image)
                tlines[(x // V.COL_HW, y // V.PAGE)] = V.PAGE
        tbands = [(c, r, n) for (c, r), n in sorted(tlines.items()) if n < V.PAGE and c in V.TEX_COLUMNS]
    # 7.5: sheets DrawDeformedSprite draws (whole, 128x128, repeated by the GPU's texture window) get the last free column
    deform = [p for (k, p) in man if k == 'SHT' and p in DEFORM_SHEETS]
    dcol = None
    if deform:
        dcol, columns = columns[-1], columns[:-1]
    places = {}
    for c in clusters:
        places.setdefault(c['where'], []).append(c)
    sizes = {w: kb_of(cs) for w, cs in places.items()}
    if os.environ.get('ATLAS_SIZES'):  # 7b: the KB each sheet takes in each region (fitting a new zone)
        for w, cs in sorted(places.items(), key=lambda kv: str(kv[0])):
            per = {}
            for c in cs:
                per.setdefault(c['sheet'], []).append(c)
            print('  %s: %s' % (w, ', '.join('%s %.1f (8-bit %.1f)' % (sh, kb_of(v), kb_of([c for c in v if c['depth'] == 8]))
                                             for sh, v in sorted(per.items(), key=lambda kv: -kb_of(kv[1])))))
    summary = '%s: resident %.0f KB; player %s; events %s; %d columns (%d KB)' % (
        name, sizes.get('resident', 0), ', '.join('%s %.0f KB' % (w[1], k) for w, k in sizes.items() if w != 'resident' and w[0] == 'player'),
        ', '.join('%s %.0f KB' % (w[1], k) for w, k in sizes.items() if w != 'resident' and w[0] == 'event'), len(columns), len(columns) * 32)
    try:
        used = pack(places.get('resident', []), columns, name + ' resident', tbands + clut_band)
        rest = [cp for cp in columns if cp not in used]
        taken = used_lines(places.get('resident', []))
        lim = {(c, r): v1 for (c, r, _, v1) in clut_band}
        for band in tbands + clut_band:  # 7b: tile pages / CLUT rows the resident left alone
            taken.setdefault(band[:2], band[2])
        rbands = [(col, row, n, lim.get((col, row), PAGE)) for (col, row), n in sorted(taken.items()) if n < lim.get((col, row), PAGE)]
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
        for band in tbands + clut_band:
            taken2[band[:2]] = max(taken2.get(band[:2], band[2]), band[2])
        pbands = [(col, row, n, lim.get((col, row), PAGE)) for (col, row), n in sorted(taken2.items()) if n < lim.get((col, row), PAGE)]
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
    # 7.5: the deform sheets: 4-bit, 128-aligned in their column (two across, two down), each its own CLUT
    deform_bind = {}
    for i, sh in enumerate(deform[:4]):
        im = sheets.get(sh)
        if im.shape != (128, 128):
            raise AtlasError('%s: deform sheet %s is %s' % (name, sh, im.shape))
        cs = sorted(set(np.unique(im).tolist()) - {0})
        if len(cs) > 15 or len(clut_maps) >= V.SPRITE_CLUT_USABLE:
            raise AtlasError('%s: deform sheet %s: %d colours' % (name, sh, len(cs)))
        clut_maps.append([0] + cs + [0] * (15 - len(cs)))
        ci = len(clut_maps) - 1
        lut = np.zeros(256, np.uint16)
        for n, p_ in enumerate(cs):
            lut[p_] = n + 1
        q = lut[im]
        words = q[:, 0::4] | (q[:, 1::4] << 4) | (q[:, 2::4] << 8) | (q[:, 3::4] << 12)
        X, Y = dcol[0] * V.COL_HW + (i % 2) * 32, dcol[1] * PAGE + (i // 2) * 128
        vram[Y:Y + 128, X:X + 32] = words
        w_ = vram[Y:Y + 128, X:X + 32].astype(np.int32)
        nib = np.stack([(w_ >> s_) & 0xF for s_ in (0, 4, 8, 12)], -1).reshape(128, 128)
        if not np.array_equal(np.array(clut_maps[ci], np.int32)[nib], im.astype(np.int32)):
            raise AtlasError('%s: deform sheet %s does not decode back' % (name, sh))
        blocks.append((X, Y, 32, 128))
        deform_bind[sh] = struct.pack('<BBH', (i % 2) * 128, (i // 2) * 128, (dcol[0] & 15) | (dcol[1] & 1) << 4 | ci << 7)
        checked += 128 * 128
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
                d = 4 if c['half'] == 'q' else 2 if c['half'] else 1
                u, v = c['u'] + (x - cx) // d, c['v'] + (y - cy) // d
                clut = c['clut'] if c['depth'] == 4 else BANK_CLUT
                if c['half'] == 'q':
                    if c['depth'] != 4 or clut >= 0x1FF - QUARTER_CLUT:
                        raise AtlasError('%s: quarter-resolution %s needs a 4-bit CLUT slot < %d' % (name, sh, 0x1FF - QUARTER_CLUT))
                    clut += QUARTER_CLUT
                if u > 255 or v > 255:
                    raise AtlasError('%s: binding past its page: %s %s' % (name, sh, (x, y)))
                return struct.pack('<BBH', u, v, (c['page_col'] & 15) | (b.row & 1) << 4 | (1 if c['depth'] == 8 else 0) << 5
                                   | (1 if c['half'] else 0) << 6 | clut << 7)
        return None
    MISSING = struct.pack('<BBH', 0xFF, 0xFF, 0xFFFF)
    OUTSIDE = struct.pack('<BBH', 0xFE, 0xFE, 0xFFFF)  # 4.2d: wholly outside its sheet: the runtime draws nothing, uncounted
    bind, file_tab = bytearray(), bytearray()
    patches = {w: bytearray() for w in group_images}
    counts = {'placed': 0, 'unplaced': 0}
    for fi, (path, s, keys) in enumerate(files):
        # 7b: an animation placed nowhere keeps no frames on the PS1 (Scene<N>.pda): the file's bindings and patches
        # count the stored frames (the kept ones in order, then one placeholder: Animation.cpp LoadSpriteAnimation)
        dropped = {ai for ai in range(len(s['anims'])) if sel[path][ai] is None and s['anims'][ai]['frames']}
        stored = [k for k in range(len(keys)) if keys[k][0] not in dropped]
        file_tab += struct.pack('<4IIH2x', *md5_words(path), len(bind), len(stored) + (1 if dropped else 0))
        for k, (ai, (sh, x, y, w, h)) in enumerate(keys[kk] for kk in stored):
            where = sel[path][ai]
            if where is None or sh is None or w <= 0 or h <= 0:
                bind += MISSING
                counts['unplaced'] += 1
                continue
            H_, W_ = sheets.get(sh).shape
            if x >= W_ or y >= H_ or x + w <= 0 or y + h <= 0:  # 4.2b: wholly outside its sheet (PhantomRuby.bin anim 2)
                bind += OUTSIDE
                counts['unplaced'] += 1
                continue
            counts['placed'] += 1
            default = MISSING  # the binding the scene loads with: its placement in a group loaded from the start, if any
            for wh in places_of(where):
                bd = binding(wh, sh, max(0, x), max(0, y), w, h)
                if bd is None:
                    raise AtlasError('%s: no cluster for %s %s' % (name, path, (sh, x, y, w, h)))
                if wh in ('resident', ('player', 'normal'), ('event', 'start')):
                    default = bd
                if wh != 'resident':
                    patches[wh] += struct.pack('<HH', fi, k) + bd
            bind += default
        if dropped:
            bind += MISSING  # the placeholder frame of the dropped animations

    for sh in deform[:4]: # 7.5: a file entry per deform sheet (its md5 = the path LoadSpriteSheet gets), one binding
        file_tab += struct.pack('<4IIH2x', *md5_words(sh), len(bind), 1)
        bind += deform_bind[sh]
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
    out = bytearray(struct.pack('<4sHHHHIIII4x', b'ATL5', len(files) + len(deform[:4]), len(clut_maps), len(blocks), len(gnames), bind_off,
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
        rep += '\n  %s %s: %d colours merged (%s), %d of %d texels changed (%.2f%%)' % (
            'level art' if '.gif' in char else 'players', char, len(mp), ', '.join('%d->%d' % ab for ab in sorted(mp.items())), changed, total, 100.0 * changed / max(1, total))
    return bytes(out), rep, clusters, lossy, sel, why


def build_fit(data, disc_data, folder, scene_id, character='sonic'):
    """build_fit_share(); 9i: when the scene's BOSS_SHARE leaves it no plan step that fits (Knuckles' larger player
    region in PSZ1), the build without it."""
    res = build_fit_share(data, disc_data, folder, scene_id, character)
    if (res is None or res[0] is None) and (folder, scene_id) in BOSS_SHARE:
        share = BOSS_SHARE.pop((folder, scene_id))
        try:
            res = build_fit_share(data, disc_data, folder, scene_id, character)
            if res is not None and res[0] is not None:
                res = (res[0], res[1] + '\n  (boss share off: it left no plan step that fits)', *res[2:])
        finally:
            BOSS_SHARE[(folder, scene_id)] = share
    return res


def build_fit_share(data, disc_data, folder, scene_id, character='sonic'):
    """build() with the fewest plan steps that fit (full resolution when it all fits): SUPER_FULL_PLAN first (9l), else
    HALF_PLAN."""
    if character == 'sonic':
        res = build_fit_plan(data, disc_data, folder, scene_id, character, SUPER_FULL_PLAN, False)
        if res is not None and res[0] is not None:
            return res
    return build_fit_plan(data, disc_data, folder, scene_id, character, HALF_PLAN, True)


def build_fit_plan(data, disc_data, folder, scene_id, character, plan, last_resort):
    global HALF_RULES, KEEP_TEXT, LOSSY_LEVEL
    last = None
    steps = [(True, n) for n in range(len(plan) + 1)] + ([(False, n) for n in range(len(plan) + 1)] if last_resort else [])
    for keep, n in steps:
        KEEP_TEXT = keep
        HALF_RULES = {}
        LOSSY_LEVEL = False
        for files_, side in plan[:n]:
            if files_ == LOSSY:
                LOSSY_LEVEL = True
                continue
            if files_ is HALF_SUPER and (folder, scene_id) in SUPER_FROM_START:
                continue  # Super is the player for the whole scene there (ERZ's final fight): full size
            for f in files_:
                HALF_RULES[f] = side
        res = build(data, disc_data, folder, scene_id, character)
        if res[0] is not None:
            desc = 'none' if n == 0 else '; '.join(
                ['%s >= %d px' % (lab, min((t for f, t in HALF_RULES.items() if f in grp), default=0))
                 for lab, grp in (('bosses', HALF_BOSS), ('effects', HALF_EFFECT), ('pictures', HALF_PICTURE),
                                  ('presentation', HALF_PRESENT), ('HUD', HALF_HUD), ('set pieces', HALF_LEVEL),
                                  ('Super Sonic', HALF_SUPER))
                 if any(f in HALF_RULES for f in grp)] + (['level art at 15 colours'] if LOSSY_LEVEL else []))
            return res[0], res[1] + '\n  big sprites at half resolution: %s%s' % (desc, '' if keep else ' (text strips too)'), *res[2:]
        last = res
    return last


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    if len(a) not in (5, 6):
        sys.exit(__doc__)
    data, disc, outdir, folder, sid = a[:5]
    character = a[5] if len(a) > 5 else 'sonic'
    # 9b.1: a presentation scene's atlas holds every character (select keeps all player files): one, Sonic's name
    if character != 'sonic' and folder in presentation_folders(F.data_root()):
        print('%s%s/%s: a presentation scene (one atlas for every character)' % (folder, sid, character))
        return
    blob, rep, clusters, _, sel, why = build_fit(data, disc, folder, sid, character)
    if '--why' in sys.argv:
        for p in sorted(why):
            print('  %-28s %s' % (p, why[p]))
    print(rep)
    if blob is None:
        sys.exit(1)
    if '--report' not in sys.argv:
        os.makedirs(os.path.join(outdir, folder), exist_ok=True)
        suffix = '' if character == 'sonic' else '_' + character
        open(os.path.join(outdir, folder, 'Scene%s%s.atl' % (sid, suffix)), 'wb').write(blob)
        pda, dropped = dropped_animations(disc, folder, sid, sel, character)
        open(os.path.join(outdir, folder, 'Scene%s%s.pda' % (sid, suffix)), 'wb').write(pda)
        print('  animations never placed (their frames not loaded): %d frames' % dropped)


def dropped_animations(disc, folder, sid, sel, character='sonic'):
    """7b: Scene<N>.pda: the animations the atlas places nowhere (none of their frames in any region or group), per
    sprite file: the PS1's LoadSpriteAnimation stores only the other animations' frames (one placeholder frame for
    the dropped ones). 'PDA1', u32 count, then per file: u32 md5[4], u16 frames kept, u16 animations dropped, then
    (u16 animation, u16 frames) each."""
    out, n, frames = bytearray(), 0, 0
    for kind, path in read_manifest(folder, sid, character):
        if kind != 'SPR' or path not in sel:
            continue
        s = F.read_sprite(os.path.join(disc, 'Sprites', path))
        drop = [(ai, len(a['frames'])) for ai, a in enumerate(s['anims']) if sel[path][ai] is None and a['frames']]
        if not drop:
            continue
        kept = sum(len(a['frames']) for a in s['anims']) - sum(c for _, c in drop)
        out += struct.pack('<4IHH', *md5_words(path), kept, len(drop))
        for ai, c in drop:
            out += struct.pack('<HH', ai, c)
        n += 1
        frames += sum(c for _, c in drop)
    return b'PDA1' + struct.pack('<I', n) + bytes(out), frames


if __name__ == '__main__':
    main()
