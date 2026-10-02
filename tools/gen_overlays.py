#!/usr/bin/env python3
"""Code overlays (docs/32 2.4).

Resident EXE: the engine, the port layer, the GameConfig's global objects, the helpers and the objects whose static
variables Game.c registers. Every other object's code lives in the overlay of each stage folder that lists it (a
StageConfig object not resident), loaded by the engine at the scene load (ps1/overlay.cpp) at the top of STG: linked
at STG's fixed top (__stack_start - 16 KB, RSDK/Storage/Storage.cpp) less its size.

What stays resident for the overlays:
  - every non-resident object's pointer variable (`ObjectBridge *Bridge`): global code tests them (`if (Water) ...`);
  - a jump stub for every function of a non-resident object that resident code, or another overlay, names: it jumps
    (lui / lw / nop for the R3000's load delay / jr) through ps1OvlTable[k], which the loaded overlay fills and the loader resets to a counted "missing" handler (the
    old unlinked stubs). The overlay's own references bind to the stub as well (its link takes the resident's symbols
    first, --allow-multiple-definition), so `player->state == Current_PlayerState_Down` compares one address.
The overlay links against a NOBITS image of the resident symbols (section-relative: GNU ld refuses R_MIPS_26 to
absolute --just-symbols addresses).

Commands:
  plan [--stages Logos,Title,Menu,AIZ,GHZCutscene,GHZ]
      ps1/rsdk5_objects.inc  Game.c's registration list for the resident objects (conditionals, static variables)
      build/gen/game_srcs.txt  resident object sources
      build/gen/ovl/folders.txt, build/gen/ovl/<F>.srcs  the overlays and their sources
  stubs OUT.c RESIDENT_OBJ...   (after compiling every object)
      OUT.c  pointer variables, ps1OvlTable, jump stubs, missing handlers
      build/gen/ovl/<F>_reg.c  the overlay's registrations, stub-table fills and header
      build/gen/ovl/<F>.syms  per object file: the stubbed functions it defines (for `link`)
  resimg RESIDENT.elf           build/gen/ovl/resimg.o: the resident's symbols as a NOBITS image (once per resident link)
  link F RESIDENT.elf CC...     build/ovl/<F>.bin (two links: size, then at the top)
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import rsdk5_formats as F

GEN = os.path.join(REPO, 'build', 'gen')
OVL = os.path.join(GEN, 'ovl')
NM, OBJCOPY = 'mipsel-none-elf-nm', 'mipsel-none-elf-objcopy'
STACK_RESERVE = 16 * 1024  # RSDK/Storage/Storage.cpp PS1_STACK_RESERVE
MAGIC = 0x314C564F         # 'OVL1'
# Optional engine parts linked into the overlays of the folders whose objects need them (not the resident EXE): their
# objects, and the function the overlay's register calls to plug them in. docs/32 6.2: the video player (Sonic CD's,
# docs/31) for the objects that call RSDK.LoadVideo; 7.3: the 3D draw for the UFO stages' objects (7.4: with their floor)
EXTRAS = {'video': ({'TitleSetup', 'UIVideo'}, ['ps1/fmv.o', 'ps1/video.o', 'ps1/mdec.o'], 'PS1VideoLink'),
          's3d': ({'UFO_Setup', 'UFO_Player', 'UFO_Circuit'}, ['RSDKv5/RSDK/Graphics/PS1/PS1Scene3D.o', 'RSDKv5/RSDK/Graphics/PS1/PS1UfoFloor.o',
                                                             'ps1/gte3d.o'],
                  'PS1Scene3DLink'),
          'deform': ({'UFO_Plasma', 'Smog'}, ['RSDKv5/RSDK/Graphics/PS1/PS1Deform.o'], 'PS1DeformLink')}  # 7.5


# docs/32 7b: folders whose acts get an overlay each (<folder>_<scene id>.bin): an act's overlay leaves out the folder's
# classes that only another act's scene places and that no code of the act's overlay or of the resident names (their
# pointer variable or a function). The scene load takes the act's overlay when the resident lists it (ps1OvlUnits); a
# change of overlay between two acts of one folder is a full stage load (ps1/overlay.cpp PS1OverlayUnitChanges).
SPLIT = {'CPZ', 'FBZ', 'SSZ2', 'HCZ', 'MSZ'}
# 7b.3c: classes an act's overlay leaves out although kept code names them, each reviewed: every such use is in code
# the act never runs or behind a check of the class's pointer (a left-out class's pointer stays NULL in the resident):
# FBZ2: FBZSetup finds FBZ1Outro only when CutsceneRules_IsAct1(); Ring.c tests `if (BigSqueeze)`; FBZ1Outro, BigSqueeze
# and FBZTrash name only each other. Their StageLoads load their own sprites / sounds only (BigSqueeze's Soundboard
# rumble plays while it crushes, never in act 2).
FORCE_DROP = {('FBZ', '2'): {'FBZ1Outro', 'BigSqueeze', 'FBZTrash'}}
# 7b.5: a class an act leaves out loses its StageLoad / StaticUpdate there: never left out are the zone setups and the
# classes whose StageLoad reaches past their own statics (random numbers: the stock sequence; zone / trigger
# callbacks; palettes, tiles, draw groups; an ACTIVE_ALWAYS / NORMAL object with a StaticUpdate). SAFE_DROP: reviewed
# ones whose effect is nil without their entities: CPZBoss (sets objectDrawGroup[1] / +1 to the defaults nothing else
# changes in CPZ), FBZStorm (stops a rain sound only its entities start), DNARiser (draw-list refs of its entities),
# BigSqueeze (an empty StaticUpdate), LaundroMobile / ScrewMobile (their loop sounds and the stage wrap only run on
# timers / flags their entities set).
STAGELOAD_EFFECTS = re.compile(r'\bRand\(|ZONE_RAND|callbacks\[|\bZone->\w+(\[[^]]*\])?\s*=[^=]|SetLimitedFade|LoadPalette|'
                               r'SetActivePalette|CopyTileLayer|SetTile\(|SetDrawGroupProperties|'
                               r'stageFinishCallback|Soundboard_LoadSfx\([^;]*Music')
# 7b.10: objects a folder's overlay links for their helper functions without registering the class (reviewed: the
# helpers touch no state of their own object): LRZ1Setup checks the player's lava / conveyor tiles and LRZ1's conveyor
# objects find their buttons through LRZ2Setup_GetTileInfo / LRZ2Setup_SetupTagLink (LRZ2Setup is no LRZ1 object: its
# functions were unlinked stubs there)
LINK_ONLY = {'LRZ1': ['LRZ2Setup']}
SAFE_DROP = {'CPZBoss', 'FBZStorm', 'DNARiser', 'BigSqueeze', 'LaundroMobile', 'ScrewMobile'}


def keeps_effects(cfile, cls):
    """The reason a class must stay in every act's overlay (None: its StageLoad / StaticUpdate act only on its own)."""
    if cls.endswith('Setup'):
        return 'setup'
    if cls in SAFE_DROP:
        return None
    try:
        src = open(cfile, errors='replace').read()
    except OSError:
        return None

    def body(fn):
        m = re.search(r'\nvoid %s_%s\(void\)\s*\{' % (cls, fn), src)
        if not m:
            return ''
        depth, i = 1, m.end()
        while depth and i < len(src):
            depth += {'{': 1, '}': -1}.get(src[i], 0)
            i += 1
        return src[m.end():i - 1]
    sl, su = body('StageLoad'), body('StaticUpdate')
    if STAGELOAD_EFFECTS.search(sl):
        return 'StageLoad: ' + STAGELOAD_EFFECTS.search(sl).group(0)
    if re.search(r'->active\s*=\s*ACTIVE_(ALWAYS|NORMAL)', sl) and su.strip():
        return 'StaticUpdate'
    return None


def units():
    """-> [(unit, folder, scene id or None)] (build/gen/ovl/units.txt, written by `plan`)"""
    p = os.path.join(OVL, 'units.txt')
    out = []
    for line in open(p).read().splitlines() if os.path.exists(p) else []:
        u, fo, sid = line.split()
        out.append((u, fo, None if sid == '-' else sid))
    return out


def unit_folder(u):
    for un, fo, _ in units():
        if un == u:
            return fo
    return u


def extra_folders(name):
    p = os.path.join(OVL, '%s_folders.txt' % name)
    return open(p).read().split() if os.path.exists(p) else []


def video_folders():
    return extra_folders('video')


def run(args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def sources():
    srcs = {}
    for root, _, files in sorted(os.walk(os.path.join(REPO, 'SonicMania', 'Objects'))):
        for f in sorted(files):
            if f.endswith('.c') and f != 'All.c':
                srcs[f[:-2]] = os.path.relpath(os.path.join(root, f), REPO)
    return srcs


def game_c_region():
    game_c = open(os.path.join(REPO, 'SonicMania', 'Game.c')).read()
    region = game_c[game_c.index('#include "../ps1/rsdk5_objects.inc"'):]
    return region[region.index('#else') + len('#else'):region.index('#endif // RETRO_PS1_BUILD')].strip('\n').split('\n')


def resident_set(data, srcs):
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    res = [o for o in g['objects'] if o in srcs]
    res += [os.path.basename(p)[:-2] for p in sorted(srcs.values()) if '/Helpers/' in p]
    # objects whose functions work without the object in the scene (tools/gen_objects.py history: 2.2, 2.3j)
    res += ['MenuParam', 'CutsceneRules', 'UIButtonPrompt']
    game_c = open(os.path.join(REPO, 'SonicMania', 'Game.c')).read()
    res += re.findall(r'RSDK_REGISTER_STATIC_VARIABLES\((\w+)\)', game_c)
    out = []
    for o in res:
        if o in srcs and o not in out:
            out.append(o)
    return out


def plan(argv):
    stages = ['Logos', 'Title', 'Menu', 'AIZ', 'GHZCutscene', 'GHZ']
    if '--stages' in argv:
        i = argv.index('--stages'); stages = argv[i + 1].split(','); del argv[i:i + 2]
    data = argv[0] if argv else os.path.join(REPO, '..', 'SonicMania_Extracted', 'Data')
    srcs = sources()
    res = resident_set(data, srcs)
    os.makedirs(OVL, exist_ok=True)
    for f in os.listdir(OVL):
        if f.endswith('.srcs'):
            os.remove(os.path.join(OVL, f))
    folders = []
    for st in stages:
        p = os.path.join(data, 'Stages', st, 'StageConfig.bin')
        objs = [o for o in F.read_stageconfig(p)['objects'] if o in srcs and o not in res] if os.path.exists(p) else []
        if objs:
            folders.append(st)
            link_only = [srcs[o] for o in LINK_ONLY.get(st, []) if o in srcs and o not in objs and o not in res]
            open(os.path.join(OVL, st + '.srcs'), 'w').write('\n'.join([srcs[o] for o in objs] + link_only) + '\n')
            open(os.path.join(OVL, st + '.objs'), 'w').write('\n'.join(objs) + '\n')
    open(os.path.join(OVL, 'folders.txt'), 'w').write('\n'.join(folders) + '\n')
    with open(os.path.join(OVL, 'units.txt'), 'w') as f:
        for st in folders:
            ids = sorted(n[5:-4] for n in os.listdir(os.path.join(data, 'Stages', st)) if re.match(r'Scene.+\.bin$', n))
            if st in SPLIT and len(ids) > 1:
                for sid in ids:
                    f.write('%s_%s %s %s\n' % (st, sid, st, sid))
            else:
                f.write('%s %s -\n' % (st, st))
    # the folders whose objects need an optional engine part (EXTRAS) get it linked into their overlay
    for name, (objects, _, _) in EXTRAS.items():
        ef = [st for st in folders if os.path.exists(os.path.join(data, 'Stages', st, 'StageConfig.bin'))
              and objects & set(F.read_stageconfig(os.path.join(data, 'Stages', st, 'StageConfig.bin'))['objects'])]
        open(os.path.join(OVL, '%s_folders.txt' % name), 'w').write('\n'.join(ef) + '\n')
    region = game_c_region()
    kept = 0
    with open(os.path.join(REPO, 'ps1', 'rsdk5_objects.inc'), 'w') as f:
        f.write('// generated by tools/gen_overlays.py: Game.c\'s registration list for the resident objects (the stage\n'
                '// objects register from their overlay, build/gen/ovl/<folder>_reg.c)\n')
        for line in region:
            m = re.match(r'\s*RSDK_REGISTER_OBJECT\((\w+)\);', line)
            if m and m.group(1) not in res:
                continue
            kept += bool(m)
            f.write(line + '\n')
    open(os.path.join(GEN, 'game_srcs.txt'), 'w').write('\n'.join(srcs[o] for o in res) + '\n')
    print('gen_overlays: %d resident objects (%d registrations); overlays: %s' % (
        len(res), kept, ', '.join('%s (%d)' % (st, len(open(os.path.join(OVL, st + '.objs')).read().split())) for st in folders)))


def nm_syms(objs, opt):
    out = {}
    for line in run([NM, opt] + objs).splitlines() if objs else []:
        p = line.split()
        if opt == '-u' and len(p) == 2 and p[0] == 'U':
            out.setdefault(p[1], 'U')
        elif opt != '-u' and len(p) == 3:
            out[p[2]] = p[1]
    return out


def objs_of(folder):
    return [s[:-2] + '.o' for s in open(os.path.join(OVL, folder + '.srcs')).read().split()]


def resident_game_objs():
    return [s[:-2] + '.o' for s in open(os.path.join(GEN, 'game_srcs.txt')).read().split()]


def light_folders(data=os.path.join(REPO, '..', 'SonicMania_Extracted', 'Data')):
    """docs/32 4.2a: the folders only the GameConfig's Presentation category lists (Logos, Title, Menu, Credits, ...):
    their scenes have STG to spare, zones do not. A resident object's function only light overlays reach is linked into
    them (overlay_reach); what any other folder reaches stays resident, shared by every zone."""
    g = F.read_gameconfig(os.path.join(data, 'Game', 'GameConfig.bin'))
    light, heavy = set(), set()
    for c in g['categories']:
        for sc in c['scenes']:
            (light if c['name'] == 'Presentation' else heavy).add(sc['folder'])
    return light - heavy


DEBUG_SECTIONS = ('.pdr', '.debug', '.mdebug', '.eh_frame', '.comment', '.gnu', '.reginfo', '.MIPS', '.note')
MUTABLE = ('.data', '.bss', '.sdata', '.sbss')


def parse_obj(o):
    """-> (symbols: name -> (section, kind 'F'/'O'/' ', bind 'l'/'g'/'w'), relocations: section -> [(type, target)])"""
    syms = {}
    for line in run(['mipsel-none-elf-objdump', '-t', o]).splitlines():
        m = re.match(r'^[0-9a-f]{8} (.)(.)(.)(.)(.)(.)(.) (\S+)\s+[0-9a-f]{8} (.*)$', line)
        if m:
            bind = 'w' if m.group(2) == 'w' else m.group(1)
            syms[m.group(9).strip()] = (m.group(8), m.group(7), bind)
    relocs, cur = {}, None
    for line in run(['mipsel-none-elf-objdump', '-r', o]).splitlines():
        m = re.match(r'RELOCATION RECORDS FOR \[(.*)\]:', line)
        if m:
            cur = None if m.group(1).startswith(DEBUG_SECTIONS) else m.group(1)
            continue
        q = line.split()
        if cur and len(q) == 3 and q[1].startswith('R_MIPS'):
            relocs.setdefault(cur, []).append((q[1], re.sub(r'[+-]0x[0-9a-f]+$', '', q[2])))
    return syms, relocs


def overlay_reach(folders, ro_objs):
    """docs/32 4.2a: the resident objects' functions the overlays reach. A function only CALLED from an overlay (and from
    what that call reaches) need not be resident: each overlay link appends the resident objects, and the linker takes a
    function from there only when the resident does not have it (the resident image comes first). A function stays
    resident (-u) when an overlay or a copied function takes its ADDRESS (a state or callback may outlive the overlay,
    and two copies would compare unequal), or when a copy would be wrong: it touches its file's static (mutable) data
    or takes a static function's address (directly or through the static functions it calls).
    -> (keep: resident-object functions to keep resident, refs: every other symbol a copied function names)"""
    info = {o: parse_obj(o) for o in ro_objs}
    gfun = {}                       # global function -> (object, section)
    for o, (syms, _) in info.items():
        for n, (sec, kind, bind) in syms.items():
            if kind == 'F' and bind in 'gw':
                gfun[n] = (o, sec)
    keep, refs = set(), set()
    for fo in folders:
        work = []
        for o in objs_of(fo):
            for sec, lst in parse_obj(o)[1].items():
                for ty, t in lst:
                    if t in gfun:
                        if ty == 'R_MIPS_26' and sec.startswith('.text'):
                            work.append(gfun[t])
                        else:
                            keep.add(t)
        seen, bad, callers = set(), set(), {}
        while work:
            node = work.pop()
            if node in seen:
                continue
            seen.add(node)
            o, sec = node
            syms = info[o][0]
            for ty, t in info[o][1].get(sec, []):
                if t in gfun:  # a global function (of any resident object)
                    if ty == 'R_MIPS_26':
                        work.append(gfun[t])
                    else:
                        keep.add(t)
                    continue
                tsec, kind, bind = syms.get(t, (t, ' ', 'l'))
                if tsec.startswith('.text') and (t.startswith('.text') or kind == 'F'):  # a static function
                    if tsec == sec:
                        continue  # its own jump table / labels
                    if ty == 'R_MIPS_26':
                        work.append((o, tsec))
                        callers.setdefault((o, tsec), set()).add(node)
                    else:
                        bad.add(node)
                elif tsec.startswith(MUTABLE) and bind == 'l':
                    bad.add(node)  # the file's own static data: a copy would have its own
                elif bind != 'l' or tsec == '*UND*':
                    refs.add(t)    # global data, engine / library functions: bound to the resident
        # a static function that is bad makes its callers bad, up to a global one (kept resident)
        todo = list(bad)
        while todo:
            node = todo.pop()
            for c in callers.get(node, ()):
                if c not in bad:
                    bad.add(c)
                    todo.append(c)
        for o, sec in bad:
            for n, (s2, kind, bind) in info[o][0].items():
                if s2 == sec and kind == 'F' and bind in 'gw':
                    keep.add(n)
    return keep, refs, set(gfun)


def stubs(argv):
    out, res_objs = argv[0], [o for o in argv[1:] if o.endswith('.o')]
    folders = open(os.path.join(OVL, 'folders.txt')).read().split()
    classes = set(sources())
    libs = [os.path.join(REPO, '..', 'nugget', 'psyqo', 'libpsyqo.a')]
    res_def = set(nm_syms(res_objs + libs, '--defined-only'))
    res_names = set(open(os.path.join(GEN, 'game_srcs.txt')).read().split())
    resident = {os.path.basename(p)[:-2] for p in res_names}

    def wanted(sym, defined):
        return not sym.startswith('_') and sym not in defined and sym not in res_def and sym not in classes

    stubbed = set(u for u in nm_syms([o for o in res_objs if o.startswith('SonicMania/')], '-u') if wanted(u, set()))
    ovl_def = {}
    for fo in folders:
        objs = objs_of(fo)
        d = nm_syms(objs, '--defined-only')
        ovl_def[fo] = {o: nm_syms([o], '--defined-only') for o in objs}
        for u in nm_syms(objs, '-u'):
            if wanted(u, d):
                stubbed.add(u)
        # data a resident object would need from an overlay: not supported (pointer variables are resident)
    stubbed = sorted(stubbed)
    index = {s: i for i, s in enumerate(stubbed)}
    # resident symbols only overlays use: --gc-sections would drop them from the resident link; they become roots
    # (ld reads build/gen/ovl_keep.txt as `-u symbol` options)
    # 4.2a: minus the resident objects' functions only light overlays call (overlay_reach: linked into those overlays)
    light = [fo for fo in folders if fo in light_folders()]
    must, copy_refs, ro_funcs = overlay_reach(light, resident_game_objs())
    keep = set()
    for fo in folders:
        for u in nm_syms(objs_of(fo), '-u'):
            if u in res_def and (fo not in light or u not in ro_funcs or u in must):
                keep.add(u)
    for u in copy_refs:
        if u in res_def and (u not in ro_funcs or u in must):
            keep.add(u)
    keep |= must & res_def
    # docs/32 6.2: the resident symbols the video player (linked into video folders' overlays) uses stay resident (one
    # CD stream, one chain: shared state), and the resident's allocator for its buffers (PS1VideoMalloc / Free: STG's
    # tail) must win over the player's weak psyqo defaults
    for name, (_, eobjs, _) in EXTRAS.items():
        if extra_folders(name):
            vobjs = [os.path.join(REPO, o) for o in eobjs]
            vdef = nm_syms(vobjs, '--defined-only')
            keep |= {u for u in nm_syms(vobjs, '-u') if u in res_def and u not in vdef}
    keep |= {'PS1VideoMalloc', 'PS1VideoFree'} & res_def
    open(os.path.join(GEN, 'ovl_keep.txt'), 'w').write(''.join('-u %s\n' % k for k in sorted(keep)))
    others = sorted(c for c in classes if c not in resident)
    with open(out, 'w') as f:
        f.write('// generated by tools/gen_overlays.py: %d non-resident object pointers, %d overlay functions behind stubs\n'
                % (len(others), len(stubbed)))
        f.write('volatile unsigned g_ps1UnlinkedCalls = 0; // stubs reached with no overlay function behind them (GDB)\n')
        f.write('volatile unsigned g_ps1UnlinkedLast  = 0; // 1 + index of the last one (ps1OvlNames)\n')
        f.write('volatile unsigned g_ps1UnlinkedSeen[8];   // the first 8 distinct ones reached (1 + index)\n')
        f.write('static unsigned s_scratch[1024];\n')
        f.write('static void *hit(unsigned id) {\n')
        f.write('    g_ps1UnlinkedCalls++; g_ps1UnlinkedLast = id;\n')
        f.write('    for (int i = 0; i < 8; i++) { if (g_ps1UnlinkedSeen[i] == id) break; if (!g_ps1UnlinkedSeen[i]) { g_ps1UnlinkedSeen[i] = id; break; } }\n')
        f.write('    for (int i = 0; i < 1024; i++) s_scratch[i] = 0;\n')
        f.write('    return s_scratch;\n}\n\n')
        for c in others:
            f.write('void *%s = 0;\n' % c)
        f.write('\n')
        for i, s in enumerate(stubbed):
            f.write('static void *miss%d(void) { return hit(%d); } // %s\n' % (i, i + 1, s))
        f.write('\nconst unsigned ps1OvlCount = %d;\n' % len(stubbed))
        f.write('void *const ps1OvlMiss[%d] = {%s};\n' % (max(len(stubbed), 1), ', '.join('(void *)miss%d' % i for i in range(len(stubbed))) or '0'))
        f.write('void *ps1OvlTable[%d] = {%s};\n\n' % (max(len(stubbed), 1), ', '.join('(void *)miss%d' % i for i in range(len(stubbed))) or '0'))
        f.write('__asm__(".section .text.ps1ovlstubs,\\"ax\\",@progbits\\n"\n        ".set noreorder\\n.global ps1OvlStubsBase\\nps1OvlStubsBase:\\n"\n')
        for i, s in enumerate(stubbed):
            f.write('        ".global %s\\n.type %s,@function\\n%s:\\n lui $t9, %%hi(ps1OvlTable+%d)\\n lw $t9, %%lo(ps1OvlTable+%d)($t9)\\n nop\\n jr $t9\\n nop\\n"\n'
                    % (s, s, s, 4 * i, 4 * i))
        f.write('        ".set reorder\\n.previous\\n");\n')
        f.write('// the loader reads this: it keeps the stub section in a --gc-sections link (overlays may name stubs no\n'
                '// resident code does)\nextern char ps1OvlStubsBase[];\nvoid *const ps1OvlStubsRef = ps1OvlStubsBase;\n')
    region = game_c_region()
    res_undef = set(nm_syms([o for o in res_objs if o.startswith('SonicMania/')], '-u'))
    for un, fo, sid in units():
        all_names = open(os.path.join(OVL, fo + '.objs')).read().split()
        objs_names, dropped = unit_members(fo, sid, all_names, res_undef)
        open(os.path.join(OVL, un + '.objs'), 'w').write('\n'.join(objs_names) + '\n')
        keep_o = {o for o in ovl_def[fo] if os.path.basename(o)[:-2] in objs_names or os.path.basename(o)[:-2] in LINK_ONLY.get(fo, [])}
        fills, syms = [], []
        for o, d in ovl_def[fo].items():
            if o not in keep_o:
                continue
            mine = [s for s in d if s in index and d[s] in 'Tt']
            syms.append('%s %s' % (o, ' '.join(mine)))
            fills += mine
        open(os.path.join(OVL, un + '.syms'), 'w').write('\n'.join(syms) + '\n')
        with open(os.path.join(OVL, un + '_reg.c'), 'w') as f:
            f.write('// generated by tools/gen_overlays.py: overlay %s (%d objects%s)\n'
                    % (un, len(objs_names), '; left out: ' + ' '.join(dropped) if dropped else ''))
            f.write('#include "Game.h"\n\n')
            f.write('extern void *ps1OvlTable[];\n')
            # docs/32 7b: the StageConfig classes this act's overlay leaves out (not counted as missing)
            f.write('extern const char *const *g_ps1OvlLeftOut;\n')
            f.write('static const char *const s_leftOut[] = { %s0 };\n' % ''.join('"%s", ' % d for d in dropped))
            for s in fills:
                f.write('extern void __ovl_%s(void);\n' % s)
            f.write('\nstatic void PS1OverlayRegister(void)\n{\n')
            for line in region:
                m = re.match(r'\s*RSDK_REGISTER_OBJECT\((\w+)\);', line)
                if m:
                    if m.group(1) in objs_names:
                        f.write(line + '\n')
                elif 'RSDK_REGISTER_STATIC_VARIABLES' not in line:
                    f.write(line + '\n')
            for s in fills:
                f.write('    ps1OvlTable[%d] = (void *)__ovl_%s;\n' % (index[s], s))
            f.write('    g_ps1OvlLeftOut = s_leftOut;\n')
            for name, (_, _, fn) in EXTRAS.items():
                if fo in extra_folders(name):
                    f.write('    { extern void %s(void); %s(); } // docs/32: %s\n' % (fn, fn, name))
            f.write('}\n\n')
            f.write('typedef struct { unsigned magic, size; void (*reg)(void); unsigned base; } PS1OverlayHeader;\n')
            f.write('extern char __ps1_ovl_start[], PS1_OVL_SIZE[];\n')
            f.write('__attribute__((section(".ovl_header"), used)) const PS1OverlayHeader ps1OvlHeader = {\n')
            f.write('    0x%08X, (unsigned)PS1_OVL_SIZE, PS1OverlayRegister, (unsigned)__ps1_ovl_start };\n' % MAGIC)
    with open(out, 'a') as f: # docs/32 7b: the act overlays (ps1/overlay.cpp)
        f.write('const char *const ps1OvlUnits[] = { %s0 };\n' % ''.join('"%s", ' % un for un, _, sid in units() if sid))
    print('gen_overlays: %d pointers, %d stubbed functions -> %s; %d resident symbols kept for the overlays; overlays %s' % (
        len(others), len(stubbed), out, len(keep), ' '.join(un for un, _, _ in units())))


def unit_members(fo, sid, names, res_undef):
    """docs/32 7b: an act overlay's classes. Kept: what the act's scene places, and every class that code reaches from
    those or from the resident (a reference to its pointer variable or one of its functions), and the classes no act
    places that only that unknown use explains. Left out: the classes only other acts' scenes place, and the classes no
    act places that only those reach (their helpers: the Mean Bean Machine's beans). -> (kept names, left-out names)"""
    if not sid:
        return names, []
    data = os.path.join(REPO, '..', 'SonicMania_Extracted', 'Data', 'Stages', fo)
    placed = {}
    for n in os.listdir(data):
        m = re.match(r'Scene(.+)\.bin$', n)
        if m:
            sc = F.read_scene(os.path.join(data, n), names)
            placed[m.group(1)] = {o['name'] for o in sc['objects'] if o['entities']}
    anywhere = set().union(*placed.values())
    obj = {os.path.basename(p)[:-2]: p[:-2] + '.o' for p in open(os.path.join(OVL, fo + '.srcs')).read().split()}
    undef = {c: set(nm_syms([obj[c]], '-u')) for c in names}
    defs = {c: set(nm_syms([obj[c]], '--defined-only')) | {c} for c in names}

    force = FORCE_DROP.get((fo, sid), set())

    def reach(roots, extra_refs):
        got, refs, changed = set(roots), set(extra_refs), True
        while changed:
            changed = False
            for k in list(got):
                refs |= undef[k]
            for c in names:
                if c not in got and c not in force and defs[c] & refs:
                    got.add(c)
                    changed = True
        return got

    keep = reach({c for c in names if c in placed[sid]}, res_undef)
    other = {c for c in names if c in anywhere and c not in keep}
    helpers = reach(other, set()) - keep - anywhere
    # 7b.5: the classes no act places and no other act's class explains (setups: SSZ2Setup writes GenericTrigger's
    # callbacks in both acts) are roots too, so what they name stays
    keep = reach({c for c in names if c in placed[sid]} | ({c for c in names if c not in anywhere} - helpers), res_undef)
    other = {c for c in names if c in anywhere and c not in keep}
    helpers = helpers - keep
    drop = other | helpers
    cfile = {os.path.basename(p)[:-2]: p for p in open(os.path.join(OVL, fo + '.srcs')).read().split()}
    for c in sorted(drop - force):
        why = keeps_effects(cfile.get(c, ''), c)
        if why:
            drop.discard(c)
            print('gen_overlays: %s_%s keeps %s (%s)' % (fo, sid, c, why))
    drop |= force & set(names)
    keep = reach({c for c in names if c not in drop}, res_undef) - force
    drop = {c for c in names if c not in keep}
    keep = {c for c in names if c not in drop}
    return [c for c in names if c in keep], sorted(drop)


def resimg(elf, out_s):
    syms = []
    for line in run([NM, '-n', '--defined-only', elf]).splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in 'TBDRWVG' and not p[2].startswith('__ps1_ovl'):
            syms.append((int(p[0], 16), p[2]))
    lo = min(a for a, _ in syms if a >= 0x80000000)
    hi = run([NM, elf])
    bss_end = int(re.search(r'([0-9a-f]+) . __bss_end', hi).group(1), 16)
    syms = [(a, n) for a, n in syms if lo <= a <= bss_end]
    with open(out_s, 'w') as f:
        f.write('.section .resimg,"aw",@nobits\n')
        for a, n in sorted(syms):
            f.write('.org 0x%x\n.global %s\n%s:\n' % (a - lo, n, n))
    return lo


def resimg_cmd(argv):
    elf = argv[0]
    img_s, img_o = os.path.join(OVL, 'resimg.s'), os.path.join(OVL, 'resimg.o')
    base = resimg(elf, img_s)
    open(os.path.join(OVL, 'resimg.base'), 'w').write('0x%x\n' % base)
    run(['mipsel-none-elf-as', '-march=mips1', '-EL', img_s, '-o', img_o])


def link(argv):
    un, elf, cc = argv[0], argv[1], argv[2:]
    fo = unit_folder(un)  # docs/32 7b: an act overlay links its folder's objects (those its .syms lists)
    work = os.path.join(OVL, un)
    os.makedirs(work, exist_ok=True)
    img_o = os.path.join(OVL, 'resimg.o')  # `resimg` (make builds it once, before the links)
    base = int(open(os.path.join(OVL, 'resimg.base')).read(), 16)
    stack = int(re.search(r'([0-9a-f]+) . __stack_start', run([NM, elf])).group(1), 16)
    top = stack - STACK_RESERVE
    objs = []
    for line in open(os.path.join(OVL, un + '.syms')).read().splitlines():
        p = line.split()
        if not p:
            continue
        o, mine = p[0], p[1:]
        dst = os.path.join(work, os.path.basename(o))
        args = [OBJCOPY]
        for s in mine:
            args += ['--add-symbol', '__ovl_%s=.text.%s:0,global,function' % (s, s)]
        run(args + [o, dst])
        objs.append(dst)
    libc = [os.path.join(REPO, 'ps1', 'libc', n) for n in ('string.o', 'stdlib.o', 'math.o', 'stdio.o')]
    # 4.2a: a light overlay's copies of the resident objects' functions it only calls (overlay_reach); the resident's
    # own come first (the image), so only what the resident lacks is taken from these
    ro = [os.path.join(REPO, o) for o in resident_game_objs()] if fo in light_folders() else []
    reg = os.path.join(OVL, un + '_reg.o')
    for name, (_, eobjs, _) in EXTRAS.items():  # optional engine parts (not in the resident: nothing there calls them)
        if fo in extra_folders(name):
            objs += [os.path.join(REPO, o) for o in eobjs]

    def do_link(at):
        ld = os.path.join(work, 'ovl.ld')
        open(ld, 'w').write('''SECTIONS {
  .resimg 0x%x (NOLOAD) : { *(.resimg) }
  .ovl 0x%x : {
    __ps1_ovl_start = .;
    KEEP(*(.ovl_header))
    *(.text .text.*)
    *(.rodata .rodata.* .rdata .rdata.*)
    *(.data .data.* .sdata .sdata.*)
    *(.sbss .sbss.* .bss .bss.* COMMON)
    . = ALIGN(16);
    __ps1_ovl_end = .;
  }
  PS1_OVL_SIZE = __ps1_ovl_end - __ps1_ovl_start;
  /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.note .note.*) *(.mdebug.*) }
}
''' % (base, at))
        out = os.path.join(work, '%s.elf' % un)
        subprocess.run(cc + ['-nostdlib', '-Wl,-T,' + ld, '-Wl,--allow-multiple-definition', '-Wl,--gc-sections',
                             '-Wl,-wrap,memcpy', '-Wl,-wrap,memset', '-Wl,-wrap,_Znwj', '-Wl,-wrap,_Znaj',
                             '-Wl,-e,ps1OvlHeader', '-o', out, img_o, reg] + objs + ro + libc + ['-lgcc'], check=True)
        nmout = run([NM, out])
        s = int(re.search(r'([0-9a-f]+) . __ps1_ovl_start', nmout).group(1), 16)
        e = int(re.search(r'([0-9a-f]+) . __ps1_ovl_end', nmout).group(1), 16)
        return out, e - s
    _, size = do_link(top)  # size probe: above the resident image on every link (an 8 MB build's BSS may pass 0x80100000)
    at = (top - size) & ~15
    out, size2 = do_link(at)
    assert size2 == size, (size, size2)
    os.makedirs(os.path.join(REPO, 'build', 'ovl'), exist_ok=True)
    binf = os.path.join(REPO, 'build', 'ovl', un + '.bin')
    run([OBJCOPY, '-O', 'binary', '-j', '.ovl', out, binf])
    data = open(binf, 'rb').read()
    magic, hsize, _, hbase = (int.from_bytes(data[i:i + 4], 'little') for i in (0, 4, 8, 12))
    assert magic == MAGIC and hsize == size == len(data) and hbase == at, (hex(magic), hsize, size, len(data), hex(hbase), hex(at))
    print('gen_overlays: %s.bin %d bytes at 0x%08x (top 0x%08x)' % (un, size, at, top))


if __name__ == '__main__':
    cmd, argv = sys.argv[1], sys.argv[2:]
    {'plan': plan, 'stubs': stubs, 'resimg': resimg_cmd, 'link': link}[cmd](argv)
