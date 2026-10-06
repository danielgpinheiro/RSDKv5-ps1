#!/usr/bin/env python3
"""docs/32 9b.7: the game's ps1TempLargeClasses (SonicMania/Objects/Global/Zone.c) from the sources and the build.

The classes the game creates as temp entities are the ones CREATE_ENTITY(X, ...) or RSDK.CreateEntity(X->classID, ...)
name (the DebugSpawns are CREATE_ENTITYs too; the act transitions' stored entities are Players, in reserved slots, and
SignPost, which is created this way as well). Their entity sizes come from the ELFs' debug info (the resident and every
overlay: tools/ps1 builds); the list keeps those past a small temp block (PS1_TEMP_SMALL_CAP, 256 B).
Usage: tools/temp_large_classes.py [--check]   (--check: compare with Zone.c's table, exit 1 when it differs)
"""
import glob, os, re, subprocess, sys

R = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SMALL = 256
pat = re.compile(r'CREATE_ENTITY\(\s*(\w+)|CreateEntity\(\s*(\w+)->classID')
names = set()
for f in glob.glob(os.path.join(R, 'SonicMania', 'Objects', '**', '*.c'), recursive=True):
    for m in pat.finditer(open(f, errors='replace').read()):
        names.add(m.group(1) or m.group(2))
gdb = ['file %s' % os.path.join(R, 'rsdkv5.elf')]
gdb += ['add-symbol-file %s' % e for e in sorted(glob.glob(os.path.join(R, 'build', 'gen', 'ovl', '*', '*.elf')))]
gdb += ['python', 'for n in %r:' % sorted(names), '    try:', "        print('SZ %d %s' % (gdb.lookup_type('struct Entity' + n).sizeof, n))",
        '    except Exception:', '        pass', 'end']
open('/tmp/temp_large_classes.gdb', 'w').write('\n'.join(gdb) + '\n')
out = subprocess.run(['/opt/homebrew/bin/gdb', '-batch', '-x', '/tmp/temp_large_classes.gdb'], capture_output=True, text=True).stdout
sizes = {l.split()[2]: int(l.split()[1]) for l in out.splitlines() if l.startswith('SZ ')}
large = sorted((n for n, s in sizes.items() if s > SMALL), key=lambda n: (-sizes[n], n))
if '--check' in sys.argv:
    zone = open(os.path.join(R, 'SonicMania', 'Objects', 'Global', 'Zone.c')).read()
    m = re.search(r'ps1TempLargeClasses\[\] = \{(.*?)NULL \};', zone, re.S)
    have = set(re.findall(r'"(\w+)"', m.group(1))) if m else set()
    if have != set(large):
        print('ps1TempLargeClasses differs: missing %s, extra %s' % (sorted(set(large) - have), sorted(have - set(large))))
        sys.exit(1)
    print('ps1TempLargeClasses: %d classes, up to date' % len(large))
else:
    for n in large:
        print('%5d %s' % (sizes[n], n))
