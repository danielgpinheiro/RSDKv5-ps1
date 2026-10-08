#!/usr/bin/env python3
"""Per-scene SFX sets for the Mania PS1 port (docs/32 phase 5.1).

The engine loads GameConfig's global SFX at boot and each StageConfig's list per stage folder; objects take IDs with
RSDK.GetSfx("Path.wav") (in StageLoad, sometimes inline) and play them. A scene can only play what an object it links
asks for, so its set is: the objects it links (GameConfig's global objects when the StageConfig uses them, plus the
StageConfig's own), the GetSfx / Soundboard_LoadSfx string literals in those objects' sources, intersected with the
SFX the engine loads there (GameConfig's globals + the StageConfig's list). A GetSfx call without a literal fails the
build unless reviewed (REVIEWED_CALLS).

Usage: sfx_sets.py DATA_DIR [FOLDER ...] [--sizes]
  prints per folder: objects linked, SFX in the set, their size as SPU ADPCM at 44.1 / 22.05 kHz (--sizes).
As a module: sets(data_dir, folders) -> {folder: [sfx path, ...]} (GameConfig order, then the StageConfig's).
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import rsdk5_formats as F

GAME = os.path.join(HERE, '..', '..', 'SonicMania')
LITERAL = re.compile(r'(?:RSDK\.GetSfx|Soundboard_LoadSfx)\s*\(\s*"([^"]+)"')
CALL = re.compile(r'RSDK\.GetSfx\s*\(')
# calls reviewed by hand: Soundboard_LoadSfx(name) passes its literal on (its callers' literals are collected)
REVIEWED_CALLS = {'Soundboard.c'}
SFX_SLOTS = 127

_sources = None


def object_sources():
    """{object name: source text} for every SonicMania/Objects/*/<Name>.c"""
    global _sources
    if _sources is None:
        _sources = {}
        for d, _, fs in os.walk(os.path.join(GAME, 'Objects')):
            for f in fs:
                if f.endswith('.c'):
                    _sources[f[:-2]] = open(os.path.join(d, f), errors='replace').read()
    return _sources


def object_sfx(name):
    """The SFX literals an object's source asks for (a non-literal GetSfx call raises unless reviewed)."""
    src = object_sources().get(name)
    if src is None:
        return set()
    lits = set(LITERAL.findall(src))
    calls = len(CALL.findall(src))
    lit_calls = len(re.findall(r'RSDK\.GetSfx\s*\(\s*"', src))
    if calls != lit_calls and name + '.c' not in REVIEWED_CALLS:
        raise SystemExit('%s.c: %d RSDK.GetSfx calls without a literal: review them' % (name, calls - lit_calls))
    return lits


# Base Mania's scope (docs/32 user decisions: no Encore, Mighty, Ray, Competition): what those alone play
OUT_OF_SCOPE = {'Global/RaySwoop.wav', 'Global/RayDive.wav', 'Global/MightyDrill.wav', 'Global/MightyLand.wav',
                'Global/MightyUnspin.wav', 'Global/MightyDeflect.wav', 'Global/SignPost2p.wav', 'VO/SonicWins.wav',
                'VO/TailsWins.wav', 'VO/KnuxWins.wav', 'VO/MightyWins.wav', 'VO/RayWins.wav', 'VO/ItsADraw.wav',
                'VO/ItsADraw_Set.wav'}
# 9g: a Plus character's own sounds (its bank variant, PS1Sfx<id>_<char>.bin: the Plus characters setting)
PLUS_OWN_SFX = {'mighty': {'Global/MightyDrill.wav', 'Global/MightyLand.wav', 'Global/MightyUnspin.wav',
                           'Global/MightyDeflect.wav'},
                'ray': {'Global/RaySwoop.wav', 'Global/RayDive.wav'}}
# objects every scene with a player runs without an entity in the scene file (the engine / Zone / Player create them)
ALWAYS = {'Player', 'Camera', 'HUD', 'PauseMenu', 'Zone', 'Music', 'Soundboard', 'SaveGame', 'Localization', 'UIWidgets',
          'UIButton', 'UIControl', 'UIDialog', 'DialogRunner', 'Explosion', 'Shield', 'ImageTrail', 'Dust', 'Debris',
          'Animals', 'ScoreBonus', 'InvincibleStars', 'SuperSparkle', 'TitleCard', 'GameOver', 'DebugMode',
          'CutsceneRules'}  # (placed ones -- rings, item boxes, sign posts, water -- come from the scene files)
CREATES = re.compile(r'(?:CREATE_ENTITY\s*\(\s*|->classID|RSDK_GET_ENTITY\([^)]*,\s*)(\w+)')
CLASS_REF = re.compile(r'\b(\w+)->classID')
CALLS = re.compile(r'\b([A-Z]\w*?)_[A-Z]\w*\s*\(')
STATIC_SFX = re.compile(r'\b(\w+)->sfx\w*')


def links(name, linked):
    """the linked objects this object's code creates, resets into, calls or whose sounds it plays"""
    src = object_sources().get(name, '')
    # creation only: CREATE_ENTITY(X, ...), RSDK.CreateEntity(X->classID, ...), RSDK.ResetEntity*(..., X->classID, ...)
    # (X->classID elsewhere is a type check)
    out = set(re.findall(r'CREATE_ENTITY\s*\(\s*(\w+)', src))
    out |= set(re.findall(r'(?:CreateEntity|ResetEntity\w*)\s*\([^;]*?(\w+)->classID', src))
    out |= set(STATIC_SFX.findall(src)) # (helper calls into another object's functions are not followed: its own
    # sounds are played through its statics, found here; a miss shows at run time as g_ps1SfxUnloaded)
    return {o for o in out if o in linked and o != name}


def scene_objects(data, folder, names):
    """{scene id: objects with entities in Scene<id>.bin}"""
    out = {}
    d = os.path.join(data, 'Stages', folder)
    for f in sorted(os.listdir(d)):
        if f.startswith('Scene') and f.endswith('.bin'):
            out[f[5:-4]] = {o['name'] for o in F.read_scene(os.path.join(d, f), names)['objects'] if o['entities']}
    return out


def sets(data, folders=None, character=None):
    """{(folder, scene id): [sfx path, ...]}: per scene, what the objects it runs ask for (9g: `character` mighty / ray:
    that character's own sounds kept too)"""
    gc = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    if folders is None:
        folders = sorted({s['folder'] for c in gc['categories'] for s in c['scenes']})
    out = {}
    for folder in folders:
        sc = F.read_stageconfig(os.path.join(data, 'Stages', folder, 'StageConfig.bin'))
        # the engine's PS1 SFX_COUNT (Audio.hpp): 128 slots, the last one the stream's
        assert (len(gc['sfx']) if sc['use_global_objects'] else 0) + len(sc['sfx']) <= SFX_SLOTS, folder
        objs = (gc['objects'] if sc['use_global_objects'] else []) + sc['objects']
        linked = set(objs)
        scenes = scene_objects(data, folder, objs)
        # base Mania's scenes only: GameConfig's entries with filter 0 (presentation) or the Mania bit (2)
        # (ids compare case-insensitively: GameConfig says MSZ "1k", the file is Scene1K.bin; the disc index ignores case)
        inscope = {sc_['id'].lower() for c in gc['categories'] for sc_ in c['scenes']
                   if sc_['folder'] == folder and (sc_['filter'] == 0 or sc_['filter'] & 2)}
        scenes = {k: v for k, v in scenes.items() if k.lower() in inscope}
        placed_any = set().union(*scenes.values()) if scenes else set()
        # stage code that runs without entities: the folder's *Setup objects (the rest are created by live code)
        setup = {o for o in set(sc['objects']) - placed_any if o.endswith('Setup')}
        for sid, placed in scenes.items():
            live = (placed | setup | (ALWAYS if 'Player' in placed_any else set())) & linked
            todo = list(live)
            while todo:
                for o in links(todo.pop(), linked):
                    if o not in live:
                        live.add(o)
                        todo.append(o)
            wanted = set()
            for o in live:
                wanted |= object_sfx(o)
            wanted -= OUT_OF_SCOPE - PLUS_OWN_SFX.get(character, set())
            loaded = [p for p, _ in gc['sfx']] + [p for p, _ in sc['sfx']]
            seen, lst = set(), []
            for p in loaded:
                if p in wanted and p not in seen:
                    seen.add(p)
                    lst.append(p)
            out[(folder, sid)] = lst
    return out


def wav_info(path):
    """(frames, rate) from a RIFF WAVE header (PCM or float)"""
    d = open(path, 'rb').read()
    assert d[:4] == b'RIFF' and d[8:12] == b'WAVE', path
    p, ch, rate, bits, frames = 12, 1, 44100, 16, 0
    while p + 8 <= len(d):
        cid, size = d[p:p + 4], int.from_bytes(d[p + 4:p + 8], 'little')
        if cid == b'fmt ':
            ch = int.from_bytes(d[p + 10:p + 12], 'little')
            rate = int.from_bytes(d[p + 12:p + 16], 'little')
            bits = int.from_bytes(d[p + 22:p + 24], 'little')
        elif cid == b'data':
            frames = size // (ch * bits // 8)
        p += 8 + size + (size & 1)
    return frames, rate


def adpcm_bytes(path, rate):
    frames, src = wav_info(path)
    n = frames * rate // src
    return (n + 27) // 28 * 16


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    data = a[0]
    res = sets(data, a[1:] or None)
    for (folder, sid), lst in res.items():
        line = '%-14s %3d SFX' % (folder + sid, len(lst))
        if '--sizes' in sys.argv:
            b44 = sum(adpcm_bytes(os.path.join(data, 'SoundFX', p), 44100) for p in lst)
            b22 = sum(adpcm_bytes(os.path.join(data, 'SoundFX', p), 22050) for p in lst)
            line += '  %4d KB @44.1k  %4d KB @22k' % (b44 // 1024, b22 // 1024)
        print(line)


if __name__ == '__main__':
    main()
