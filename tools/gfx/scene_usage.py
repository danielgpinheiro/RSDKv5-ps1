#!/usr/bin/python3
"""What a scene can show (docs/32 3.1): the static analysis the sprite atlas selects frames with.

- Objects of the scene: GameConfig's global objects + the folder's StageConfig objects.
- Active objects: those with Mania-mode entities in the scene file (filter & 3: FILTER_BOTH | FILTER_MANIA), then every
  object an active object's code can create (CREATE_ENTITY(X, …), X->classID passed to CreateEntity / ResetEntity /
  ResetEntitySlot, or assigned to a classID), repeated until nothing is added. An object's code is its own file plus
  the files of every object whose functions it calls (PauseMenu calls UIDialog_CreateActiveDialog, which creates the
  dialog), transitively, plus the helpers (BadnikHelpers creates a badnik's Animals). An object that is never active draws
  nothing (Draw callbacks run for entities), so the sprite files only it loads can be left out.
- Sprite file owners: the objects whose source loads the file (LoadSpriteAnimation("path") literals; Player's files
  and UIWidgets' language text are built at runtime and listed here).
- Player animations: every ANI_* constant an active object's code names (the Plus enum of Player.h; aliases resolve to
  their value). A gimmick animation (hang bars, pole swings, turntables…) no active object sets is never shown.
- Shields a zone can give: the ItemBox types of the folder's scenes (the player keeps a shield across acts).
The runtime guard for anything this misses is the atlas miss counter (phase 3.2 renderer).

Usage: scene_usage.py FOLDER SCENE_ID   (prints the analysis)
"""
import glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..'))
import rsdk5_formats as F  # noqa: E402

DATA = os.path.join(REPO, '..', 'SonicMania_Extracted', 'Data')
RUNTIME_OWNERS = {'Players/Sonic.bin': {'Player'}, 'Players/SuperSonic.bin': {'Player'}, 'Players/Tails.bin': {'Player'},
                  'Players/TailSprite.bin': {'Player'}, 'Players/Knux.bin': {'Player'}, 'Players/Mighty.bin': {'Player'},
                  'Players/Ray.bin': {'Player'}, 'UI/TextEN.bin': {'UIWidgets'}}


def sources():
    m = {}
    for p in glob.glob(os.path.join(REPO, 'SonicMania', 'Objects', '*', '*.c')):
        m.setdefault(os.path.basename(p)[:-2], p)
    return m


def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def scene_objects(folder, scene_id, data=DATA):
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    sc = F.read_stageconfig(os.path.join(data, 'Stages', folder, 'StageConfig.bin'))
    names = g['objects'] + sc['objects']
    s = F.read_scene(os.path.join(data, 'Stages', folder, 'Scene%s.bin' % scene_id), names)
    placed = {o['name'] for o in s['objects'] if any(e['filter'] & 3 for e in o['entities'])}
    return names, placed, s


def creation_edges(names, src):
    edges = {}
    pat_create = re.compile(r'CREATE_ENTITY\(\s*(\w+)')
    pat_classid = re.compile(r'(?:CreateEntity|ResetEntity|ResetEntitySlot)\([^;]*?\b(\w+)->classID')
    pat_assign = re.compile(r'classID\s*=\s*(\w+)->classID')
    for n in names:
        p = src.get(n)
        if not p:
            continue
        t = strip_comments(open(p).read())
        edges[n] = set(pat_create.findall(t)) | set(pat_classid.findall(t)) | set(pat_assign.findall(t))
    return edges


def helper_sources():
    """SonicMania/Objects/Helpers: shared functions objects call (BadnikHelpers creates the Animals of a destroyed
    badnik); counted as code of every active set."""
    return sorted(glob.glob(os.path.join(REPO, 'SonicMania', 'Objects', 'Helpers', '*.c')))


_code_cache = {}


def code_of(path):
    if path not in _code_cache:
        _code_cache[path] = strip_comments(open(path).read())
    return _code_cache[path]


def called_objects(text, known):
    """Objects whose functions the code calls (Name_Function(...)): their code runs in the caller's context."""
    return {m for m in re.findall(r'\b([A-Z]\w*?)_[A-Z]\w*\s*\(', text) if m in known}


def code_closure(objs, known, src):
    """The source files that run for these objects: their own, every object's whose functions they call (repeated),
    and the helpers."""
    files, todo, seen = set(helper_sources()), list(objs), set()
    for p in helper_sources():
        todo += list(called_objects(code_of(p), known))
    while todo:
        o = todo.pop()
        if o in seen or o not in src:
            continue
        seen.add(o)
        files.add(src[o])
        todo += list(called_objects(code_of(src[o]), known) - seen)
    return files


def function_body(code, fname):
    """The text of `void fname(...) { ... }` in code (brace-matched), or ''."""
    m = re.search(r'\b' + re.escape(fname) + r'\s*\([^)]*\)\s*\{', code)
    if not m:
        return ''
    depth, i = 1, m.end()
    while i < len(code) and depth:
        depth += {'{': 1, '}': -1}.get(code[i], 0)
        i += 1
    return code[m.end():i]


# 7b.14 (reviewed): stage objects with no entity whose StaticUpdate runs the scene (and creates what it draws): roots.
# CreditsSetup_StaticUpdate loads the credits and creates their UICreditsText lines. (A general "every StaticUpdate is
# a root" rule would change the zones' atlases: phase 9b reviews it.)
STATIC_ROOTS = {'Credits': {'CreditsSetup'}}


def active_objects(folder, scene_id, data=DATA):
    names, placed, scene = scene_objects(folder, scene_id, data)
    src = sources()
    known = set(names)
    placed = set(placed) | (set(STATIC_ROOTS.get(folder, ())) & known)
    pat_create = re.compile(r'CREATE_ENTITY\(\s*(\w+)')
    pat_classid = re.compile(r'(?:CreateEntity|ResetEntity|ResetEntitySlot)\([^;]*?\b(\w+)->classID')
    pat_assign = re.compile(r'classID\s*=\s*(\w+)->classID')
    active = set(placed) & known
    # every scene object's StageLoad runs at the load, entities or not: what it creates is active (TitleSetup creates
    # its own entity: RSDK.ResetEntitySlot(0, TitleSetup->classID, NULL))
    for n in names:
        p = src.get(n)
        body = function_body(code_of(p), n + '_StageLoad') if p else ''
        active |= (set(pat_create.findall(body)) | set(pat_classid.findall(body)) | set(pat_assign.findall(body))) & known
    # 9b.2b: the act's stage-finish callback (ActClear runs it: PSZ2Setup's creates PSZ2Outro, whose cutscene animates
    # PSZEggman's turn: a dropped animation there hung the ending) is a root like a StageLoad
    # (a folder with both acts' scenes: only this act's callbacks, by their EndAct<N> name)
    acts = {f[5] for f in os.listdir(os.path.join(data, 'Stages', folder)) if re.match(r'Scene\d', f)}
    for n in names:
        p = src.get(n)
        for cb in set(re.findall(r'stageFinishCallback\s*=\s*(\w+)\s*;', code_of(p) if p else '')):
            m = re.search(r'EndAct(\d)', cb)
            if m and {'1', '2'} <= acts and scene_id[:1] in ('1', '2') and m.group(1) != scene_id[0]:
                continue
            owner = src.get(cb.split('_', 1)[0])
            body = function_body(code_of(owner), cb) if owner else ''
            active |= (set(pat_create.findall(body)) | set(pat_classid.findall(body)) | set(pat_assign.findall(body))) & known
    while True:
        code = ''.join(code_of(f) for f in code_closure(active, known, src))
        made = (set(pat_create.findall(code)) | set(pat_classid.findall(code)) | set(pat_assign.findall(code))) & known
        if made <= active:
            break
        active |= made
    return names, placed, active, scene


def active_code(names, active):
    src = sources()
    return ''.join(code_of(f) for f in code_closure(active, set(names), src))


def file_handles(names):
    """sprite path -> the handles (Owner->field) it is loaded into, from `X->f = RSDK.LoadSpriteAnimation("path", …)`."""
    src = sources()
    handles = {}
    pat = re.compile(r'(\w+->\w+)\s*=\s*RSDK\.LoadSpriteAnimation\("([^"]+)"')
    for n in names:
        p = src.get(n)
        if p:
            for h, path in pat.findall(strip_comments(open(p).read())):
                handles.setdefault(path, set()).add(h)
    return handles


def needed_files(names, active, paths):
    """The sprite files an active object can draw: one of its handles is named by an active object's code (or a
    helper's), or it has no handle found (kept)."""
    code = active_code(names, active)
    handles = file_handles(names)
    out = {}
    for path in paths:
        hs = handles.get(path)
        if not hs:
            out[path] = 'kept (no handle found)'
        else:
            users = [h for h in hs if re.search(r'\b' + re.escape(h) + r'\b', code)]
            out[path] = ('used via ' + ', '.join(sorted(users))) if users else None
    return out


def file_owners(names):
    src = sources()
    owners = {}
    for n in names:
        p = src.get(n)
        if not p:
            continue
        for path in re.findall(r'LoadSpriteAnimation\("([^"]+)"', strip_comments(open(p).read())):
            owners.setdefault(path, set()).add(n)
    for path, o in RUNTIME_OWNERS.items():
        owners.setdefault(path, set()).update(o)
    return owners


def player_anim_values():
    """ANI_* name -> animation index (Player.h, Plus enum: the data has Flume at 46)."""
    t = open(os.path.join(REPO, 'SonicMania', 'Objects', 'Global', 'Player.h')).read()
    body = t[t.index('ANI_IDLE'):]
    body = body[:body.index('}')]
    body = re.sub(r'#if MANIA_USE_PLUS|#endif|//[^\n]*', '', body)
    val, cur = {}, -1
    for m in re.finditer(r'(ANI_\w+)\s*(=\s*(ANI_\w+))?\s*,', body):
        if m.group(3):
            val[m.group(1)] = val[m.group(3)]
        else:
            cur += 1
            val[m.group(1)] = cur
    return val


def player_anim_owners():
    """9b.1: ANI_* alias name -> the character whose ability poses it names (Player.h's '<Char> Ability Anim Aliases'
    sections: the same ABILITY_n slots are Tails' swimming and Knuckles' climbing poses)"""
    t = open(os.path.join(REPO, 'SonicMania', 'Objects', 'Global', 'Player.h')).read()
    owners, cur = {}, None
    for line in t[t.index('ANI_IDLE'):t.index('} PlayerAnimationIDs')].split('\n'):
        m = re.search(r'//\s*(\w+) Ability Anim Aliases', line)
        if m:
            cur = m.group(1).lower()
            continue
        m = re.match(r'\s*(ANI_\w+)\s*=', line)
        if m and cur:
            owners[m.group(1)] = cur
    return owners


# 7b: an ANI_ name a `case` label or a comparison names sets no animation (Player.c's switch on Tails' tail poses)
NONSET = re.compile(r'case\s+ANI_\w+\s*:|[!=<>]=\s*ANI_\w+|ANI_\w+\s*[!=<>]=|[<>]\s*ANI_\w+|ANI_\w+\s*[<>](?!=)')
# 7b: poses one object's code sets only for some entities or with some other object (reviewed in the code):
# (object, ANI) -> ('never',) / ('active', {objects}) / ('var', editable variable, {values of some scene entity})
POSE_GATES = {('Player', 'ANI_HANG'): ('never',),                   # a sidekick Tails carrying the leader (one character)
              ('Player', 'ANI_DROWN'): ('active', {'Water'}),       # Water.c sets PLAYER_DEATH_DROWN
              # 9b.1: Tails' swimming poses need player->underwater, which only Water.c sets (MSZ1's Tails atlas fits)
              ('Player', 'ANI_SWIM'): ('active', {'Water'}),
              ('Player', 'ANI_SWIM_TIRED'): ('active', {'Water'}),
              ('Player', 'ANI_SWIM_LIFT'): ('active', {'Water'}),
              ('Player', 'ANI_FLUME'): ('active', {'HCZSetup'}),    # Player_State_WaterSlide: HCZSetup's slides
              ('Platform', 'ANI_TWISTER'): ('var', 'collision', {8}),    # PLATFORM_C_SOLID_HOLD
              ('Platform', 'ANI_TURNTABLE'): ('var', 'collision', {14})}  # PLATFORM_C_SOLID_BARREL


def gate_open(gate, obj, active, scene):
    if gate[0] == 'never':
        return False
    if gate[0] == 'active':
        return bool(gate[1] & set(active))
    if scene is None:
        return True
    h = F.name_hash(gate[1])
    for o in scene['objects']:
        if o['name'] == obj:
            for e in o['entities']:
                v = e['vars'].get(h)
                if v is not None and not isinstance(v, (tuple, str)) and int.from_bytes(v, 'little', signed=True) in gate[2]:
                    return True
    return False


def boss_only_poses(active, names, boss_files):
    """7b: the player poses (IDs) that only objects loading a boss-group sprite file set (their code, NONSET aside)"""
    owners = file_owners(names)  # path -> objects loading it
    boss_objs = {o for p, objs in owners.items() if p in boss_files for o in objs}
    setters = {}
    for f in code_closure(active, set(names), sources()):
        obj = os.path.basename(f)[:-2]
        for a in set(re.findall(r'\bANI_\w+\b', NONSET.sub(' ', code_of(f)))):
            setters.setdefault(a, set()).add(obj)
    val = player_anim_values()
    return {val[a] for a, objs in setters.items() if a in val and objs and objs <= boss_objs}


def player_anims(active, names, scene=None, character=None):
    """the player animation IDs the scene's code can set; with `character` (9b.1: Tails' / Knuckles' atlases), another
    character's ability aliases don't count (their slots are this character's other poses)"""
    owners = player_anim_owners() if character else {}
    used = set()
    for f in code_closure(active, set(names), sources()):
        obj = os.path.basename(f)[:-2]
        for a in set(re.findall(r'\bANI_\w+\b', NONSET.sub(' ', code_of(f)))):
            if owners.get(a, character) != character:
                continue
            g = POSE_GATES.get((obj, a))
            if g is None or gate_open(g, obj, active, scene):
                used.add(a)
    val = player_anim_values()
    return {v for n, v in val.items() if n in used}, val


def itembox_types(folder, data=DATA):
    """ItemBox 'type' values of every scene of the folder (Mania-mode entities)."""
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    sc = F.read_stageconfig(os.path.join(data, 'Stages', folder, 'StageConfig.bin'))
    th = F.name_hash('type')
    types = set()
    for p in sorted(glob.glob(os.path.join(data, 'Stages', folder, 'Scene*.bin'))):
        s = F.read_scene(p, g['objects'] + sc['objects'])
        for o in s['objects']:
            if o['name'] == 'ItemBox':
                for e in o['entities']:
                    if e['filter'] & 3 and th in e['vars']:
                        types.add(int.from_bytes(e['vars'][th][:4], 'little', signed=True))
    return types


def main():
    folder, sid = sys.argv[1], sys.argv[2]
    names, placed, active, _ = active_objects(folder, sid)
    print('%s%s: %d objects, %d placed, %d active' % (folder, sid, len(names), len(placed), len(active)))
    print('  never active:', ' '.join(sorted(set(names) - active)))
    keep, val = player_anims(active, names)
    print('  player animations kept %d of %d; dropped: %s' % (len(keep), max(val.values()) + 1,
                                                            ' '.join(sorted(n for n, v in val.items() if v not in keep))))
    print('  ItemBox types in the folder:', sorted(itembox_types(folder)))
    man = [l.split() for l in open(os.path.join(HERE, 'manifests', '%s%s.txt' % (folder, sid))) if l[:3] == 'SPR']
    need = needed_files(names, active, [p for _, p in man])
    print('  sprite files not needed:', ' '.join(p for p, why in need.items() if why is None))
    print('  kept without a handle:', ' '.join(p for p, why in need.items() if why and why.startswith('kept')))


if __name__ == '__main__':
    main()
