#!/usr/bin/env python3
"""docs/32 7b.10: per overlay unit, the stubbed functions its objects CALL (R_MIPS_26) that no object of the unit defines:
at run time they reach an unfilled stub (g_ps1UnlinkedCalls). Review each: a call guarded by the class pointer or a scene
check is fine; otherwise link the defining object (gen_overlays LINK_ONLY). Run after a build: tools/ovl_stub_check.py"""
import os, re, subprocess, collections
OVL='build/gen/ovl'
stubbed=set(re.findall(r'// (\w+)\n', open('build/gen/ovl_resident.c').read()))
def nm(o, flag):
    out=subprocess.run(['mipsel-none-elf-nm', flag, o], capture_output=True, text=True).stdout
    return [l.split()[-1] for l in out.splitlines() if l.strip()]
def calls_of(o):
    out=subprocess.run(['mipsel-none-elf-objdump', '-r', o], capture_output=True, text=True).stdout
    return set(m.group(1) for m in re.finditer(r'R_MIPS_26\s+(\w+)', out))
units=[l.split()[0] for l in open(os.path.join(OVL,'units.txt')) if l.strip()]
for u in units:
    syms=os.path.join(OVL, u+'.syms')
    if not os.path.exists(syms): continue
    objs=[l.split()[0] for l in open(syms) if l.strip()]
    defined=set(); calls=collections.defaultdict(set)
    for o in objs:
        defined |= set(nm(o,'--defined-only'))
        for s in calls_of(o):
            if s in stubbed: calls[s].add(os.path.basename(o)[:-2])
    missing={s:c for s,c in calls.items() if s not in defined}
    if missing:
        print(u, ':', '; '.join('%s <- %s' % (s, ','.join(sorted(c))) for s,c in sorted(missing.items())))
