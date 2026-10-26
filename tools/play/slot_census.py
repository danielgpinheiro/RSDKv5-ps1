#!/usr/bin/env python3
"""Counts a slot census (tools/play/slot_census.gdb: OUT.log + its RAM dump OUT.ram) by class: live slots, by active
kind, in range. Usage: tools/play/slot_census.py OUT.log [OVERLAY_UNIT] (class names from the update functions' symbols:
rsdkv5.elf + build/gen/ovl/<unit>/<unit>.elf). Writes OUT.rows (slot class active inRange live x y rx ry)."""
import os, struct, subprocess, sys, collections

ACT = {0: 'NEVER', 1: 'ALWAYS', 2: 'NORMAL', 3: 'PAUSED', 4: 'BOUNDS', 5: 'XBOUNDS', 6: 'YBOUNDS', 7: 'RBOUNDS', 255: 'DISABLED'}


def syms(elf):
    out = {}
    if not os.path.exists(elf):
        return out
    for l in subprocess.run(['mipsel-none-elf-nm', elf], capture_output=True, text=True).stdout.splitlines():
        p = l.split()
        if len(p) == 3 and p[1] in 'tT':
            out[int(p[0], 16)] = p[2]
    return out


def main():
    log, unit = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else None)
    names = syms('rsdkv5.elf')
    if unit:
        names.update(syms('build/gen/ovl/%s/%s.elf' % (unit, unit)))
    O = {}
    for l in open(log, errors='replace'):
        if l.startswith('O '):
            O.update({k: int(v) for k, v in (t.split('=') for t in l.split()[1:])})
        elif l.startswith('F ') or l.startswith('CAM'):
            print(l.strip())
    ram = open(log[:-4] + '.ram', 'rb').read()
    rd = lambda a, f='<I': struct.unpack_from(f, ram, a - 0x80000000)[0]
    n = O['n']
    tstart = n - 0x100  # TEMPENTITY_START (TEMPENTITY_COUNT 0x100)
    ids = [rd(O['ids'] + 4 * c) for c in range(O['ccount'])]
    cls_name = {}
    for c in range(O['ccount']):
        a = rd(O['ocl'] + ids[c] * O['osz'] + O['oup'])
        cls_name[c] = names.get(a, '?%08x' % a).replace('_Update', '') if a else '(no update)'
    print('R scene_end=%d temp_end=%d n=%d classes=%d' % (O['send'], O['tend'], n, O['ccount']))
    rows = []
    for s in list(range(O['send'])) + list(range(tstart, O['tend'])):
        e = rd(O['ptrs'] + 4 * s)
        if not e:
            continue
        cls = rd(e + O['oC'], '<H')
        lv = (rd(O['live'] + 4 * (s >> 5)) >> (s & 31)) & 1
        if cls or lv:
            rows.append((s, cls, ram[e + O['oA'] - 0x80000000], rd(e + O['oI']), lv, rd(e, '<i') >> 16, rd(e + 4, '<i') >> 16,
                         rd(e + O['oR'], '<i') >> 16, rd(e + O['oR'] + 4, '<i') >> 16))
    open(log[:-4] + '.rows', 'w').write(''.join('%d %s %d %d %d %d %d %d %d\n' % ((r[0], cls_name.get(r[1], r[1])) + r[2:]) for r in rows))
    by = collections.defaultdict(collections.Counter)
    for s, c, a, i, lv, x, y, rx, ry in rows:
        k = by[cls_name.get(c, '#%d' % c)]
        k['live'] += 1
        k[ACT.get(a, str(a))] += 1
        k['inRange'] += 1 if i else 0
    tot = collections.Counter()
    for k in by.values():
        tot.update(k)
    print('TOTAL', dict(tot))
    for name, k in sorted(by.items(), key=lambda t: -t[1]['live']):
        print('%-24s %5d  in %4d  %s' % (name, k['live'], k['inRange'], ' '.join('%s=%d' % (a, k[a]) for a in ACT.values() if k[a])))


main()
