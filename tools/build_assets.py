#!/usr/bin/env python3
"""docs/32 phase 11: the whole asset pipeline in one run, as the Makefile's data / gfx / sfx / music / video / meshes /
disc recipes do it, into a work folder: the public builder runs it (and the port can, to prove the builder's image equals
`make disc`'s). Golden rule: every conversion is build time; the PS1 only copies.

  build_assets.py --rsdk Data.rsdk --exe PSX.EXE --ovl OVL_DIR --work WORK --out OUT_PREFIX [--license FILE] [--plus]

1. extract Data.rsdk (tools/extract_rsdk.py) -> WORK/src/Data; every later tool reads it through MANIA_DATA
2. the data disc tree (tools/data_disc.py) -> WORK/iso
3. tile images and sprite atlases for every scene of STAGES (tools/gfx/build_tiles.py, build_atlas.py), per character
   (Sonic, Tails, Knuckles; --plus also Mighty and Ray); docs/32 phase 13: the full-resolution atlases for consoles with
   the 2 MB VRAM mod (build_atlas.py --atx, Scene<N>.atx: in parallel, ATX_JOBS, the files the same whatever the count)
4. SFX banks (tools/audio/build_sfx.py; --plus: the Plus characters' variants), music (build_music.py), video
   (tools/fmv/build_str.py), meshes (tools/mesh/build_meshes.py; DC_MESH_OPT: the mesh optimiser)
5. the disc tree as `make disc` lays it out (the prebuilt overlays from OVL_DIR, with its units.txt), the stage-local
   copies (tools/disc/stage_local.py), the disc (tools/disc/build_disc.py; --license or none) and its byte check
   (tools/disc/verify_disc.py)
--plus writes Data/PS1Config.txt (plus_characters=1): Mighty and Ray in the save select. Off by default (the engine's
license: DLC off by default in pre-built executables)."""
import argparse, concurrent.futures, glob, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
BASE_CHARS = ['sonic', 'tails', 'knux']
PLUS_CHARS = ['mighty', 'ray']


def stages():
    """STAGES: tools/stages.txt (the builder's copy) or the port's Makefile."""
    p = os.path.join(HERE, 'stages.txt')
    if os.path.exists(p):
        return open(p).read().split()
    m = re.search(r'^STAGES \?= (.*)$', open(os.path.join(HERE, '..', 'Makefile')).read(), re.M)
    return m.group(1).split(',')


def run(args, env=None):
    print('$ ' + ' '.join(os.path.basename(a) if os.path.isabs(a) else a for a in args), flush=True)
    subprocess.run(args, check=True, env=env)


def tool(*p):
    return os.path.join(HERE, *p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rsdk', required=True)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--ovl', required=True, help='the prebuilt code overlays (<unit>.bin) and their units.txt')
    ap.add_argument('--work', required=True)
    ap.add_argument('--out', required=True, help='the disc image prefix (<prefix>.bin / .cue)')
    ap.add_argument('--license', default='none')
    ap.add_argument('--plus', action='store_true')
    ap.add_argument('--skip-extract', action='store_true', help='reuse WORK/src')
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    src = os.path.join(work, 'src')
    data = os.path.join(src, 'Data')
    iso = os.path.join(work, 'iso')
    env = dict(os.environ, MANIA_DATA=data, PS1_OVL_UNITS=os.path.join(os.path.abspath(a.ovl), 'units.txt'))
    chars = BASE_CHARS + (PLUS_CHARS if a.plus else [])

    if not a.skip_extract:
        shutil.rmtree(src, ignore_errors=True)
        run([PY, tool('extract_rsdk.py'), os.path.abspath(a.rsdk), src], env)
    shutil.rmtree(iso, ignore_errors=True)
    run([PY, tool('data_disc.py'), data, iso], env)

    gfx = os.path.join(work, 'gfx')
    shutil.rmtree(gfx, ignore_errors=True)
    for f in stages():
        ids = sorted(os.path.basename(p)[5:-4] for p in glob.glob(os.path.join(data, 'Stages', f, 'Scene*.bin')))
        run([PY, tool('gfx', 'build_tiles.py'), data, gfx, f] + ids, env)
        for i in ids:
            if os.path.exists(tool('gfx', 'manifests', '%s%s.txt' % (f, i))):
                for c in chars:
                    run([PY, tool('gfx', 'build_atlas.py'), data, os.path.join(iso, 'Data'), gfx, f, i, c], env)
    # phase 13: Scene<N>.atx (presentation scenes print SKIP: their .atl serves every console)
    atx_jobs = [(f, i) for f in stages()
                for i in sorted(os.path.basename(p)[5:-4] for p in glob.glob(os.path.join(data, 'Stages', f, 'Scene*.bin')))
                if os.path.exists(tool('gfx', 'manifests', '%s%s.txt' % (f, i)))]
    with concurrent.futures.ThreadPoolExecutor(int(os.environ.get('ATX_JOBS', min(8, os.cpu_count() or 1)))) as ex:
        list(ex.map(lambda fi: run([PY, tool('gfx', 'build_atlas.py'), data, os.path.join(iso, 'Data'), gfx, fi[0], fi[1],
                                    '--atx'], env), atx_jobs))
    sfx = os.path.join(work, 'sfx')
    shutil.rmtree(sfx, ignore_errors=True)
    run([PY, tool('audio', 'build_sfx.py'), data, sfx] + stages(), env)
    if a.plus:
        for c in PLUS_CHARS:
            run([PY, tool('audio', 'build_sfx.py'), '--char', c, data, sfx] + stages(), env)
    music, video, meshes = (os.path.join(work, d) for d in ('music', 'video', 'meshes'))
    run([PY, tool('audio', 'build_music.py'), data, music], env)
    run([PY, tool('fmv', 'build_str.py'), data, video], env)
    run([PY, tool('mesh', 'build_meshes.py'), data, meshes], env)

    # the disc tree, as the Makefile's `disc` recipe
    D = os.path.join(iso, 'Data')
    shutil.rmtree(os.path.join(D, 'PS1Ovl'), ignore_errors=True)
    os.makedirs(os.path.join(D, 'PS1Ovl'))
    for line in open(env['PS1_OVL_UNITS']):
        u = line.split()[0] if line.split() else None
        if u:
            shutil.copyfile(os.path.join(os.path.abspath(a.ovl), u + '.bin'), os.path.join(D, 'PS1Ovl', u + '.bin'))
    if os.path.exists(os.path.join(os.path.abspath(a.ovl), 'Mods.bin')):  # phase 13: loaded only with 8 MB or more
        shutil.copyfile(os.path.join(os.path.abspath(a.ovl), 'Mods.bin'), os.path.join(D, 'PS1Ovl', 'Mods.bin'))
    for d in sorted(glob.glob(os.path.join(gfx, '*', ''))):
        f = os.path.basename(os.path.dirname(d))
        os.makedirs(os.path.join(D, 'Stages', f), exist_ok=True)
        for p in sorted(glob.glob(d + '*.ptl') + glob.glob(d + '*.atl') + glob.glob(d + '*.pda') + glob.glob(d + '*.atx')):
            shutil.copyfile(p, os.path.join(D, 'Stages', f, os.path.basename(p)))
    for d in sorted(glob.glob(os.path.join(sfx, 'Stages', '*', ''))):
        f = os.path.basename(os.path.dirname(d))
        os.makedirs(os.path.join(D, 'Stages', f), exist_ok=True)
        for p in sorted(glob.glob(d + '*.bin')):
            shutil.copyfile(p, os.path.join(D, 'Stages', f, os.path.basename(p)))
    if os.path.isdir(os.path.join(meshes, 'Meshes')):
        shutil.copytree(os.path.join(meshes, 'Meshes'), os.path.join(D, 'Meshes'), dirs_exist_ok=True)
    cfg = os.path.join(D, 'PS1Config.txt')
    if os.path.exists(cfg):
        os.remove(cfg)
    if a.plus:
        open(cfg, 'w').write('plus_characters=1\n')
    else:
        for pat in ('Stages/*/*_mighty.*', 'Stages/*/*_ray.*', 'Stages/*/PS1Sfx*_mighty.bin', 'Stages/*/PS1Sfx*_ray.bin',
                    'Meshes/Special/Mighty*.bin', 'Meshes/Special/Ray*.bin'):
            for p in glob.glob(os.path.join(D, pat)):
                os.remove(p)
    if os.path.isdir(os.path.join(video, 'Video')):
        os.makedirs(os.path.join(D, 'Video'), exist_ok=True)
        for p in sorted(glob.glob(os.path.join(video, 'Video', '*.str')) + glob.glob(os.path.join(video, 'Video', '*.sti'))
                        + [os.path.join(video, 'Video', 'PS1Video.bin')]):
            shutil.copyfile(p, os.path.join(D, 'Video', os.path.basename(p)))
    if os.path.isdir(os.path.join(music, 'Music')):
        os.makedirs(os.path.join(D, 'Music'), exist_ok=True)
        for n in ('Music.xa', 'Music.bin'):
            shutil.copyfile(os.path.join(music, 'Music', n), os.path.join(D, 'Music', n))
    for p in glob.glob(os.path.join(D, 'Stages', '*', 'PS1Local')):
        shutil.rmtree(p)
    order = os.path.join(work, 'load_order.txt')  # (the work folder's: never the tools' own copy)
    run([PY, tool('disc', 'stage_local.py'), iso, order], env)
    env['PS1_LOAD_ORDER'] = order
    lic = os.path.abspath(a.license) if a.license != 'none' else 'none'
    run([PY, tool('disc', 'build_disc.py'), iso, os.path.abspath(a.exe), lic, os.path.abspath(a.out)], env)
    run([PY, tool('disc', 'verify_disc.py'), os.path.abspath(a.out) + '.bin', iso, os.path.abspath(a.exe), lic], env)


if __name__ == '__main__':
    main()
