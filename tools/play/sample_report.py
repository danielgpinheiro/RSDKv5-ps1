#!/usr/bin/env python3
"""Ranks the functions of a PC-sampler dump (`make SAMPLE=1`, ps1/pc_sampler.cpp, tools/play/ghz1_sample.gdb).
Usage: sample_report.py HIST.bin FRAMES [PERIOD] [--top N] [--lines FUNC_SUBSTRING ...]
HIST.bin: one little-endian word per instruction of 0x80000000..0x80200000 (GDB dump of 0x80400000). FRAMES: the frames
the sampler ran. PERIOD: cycles between samples (ps1/pc_sampler.cpp). Symbols: rsdkv5.elf and the overlays' ELFs
(build/gen/ovl/*/*.elf, their .ovl range only). --lines: per source line of the matching functions (addr2line)."""
import bisect, glob, os, re, struct, subprocess, sys
from collections import defaultdict

HBL = 2152  # cycles per NTSC hblank (33.8688 MHz / 15,734 Hz)
args = sys.argv[1:]
lines_for = []
top = 40
if '--lines' in args:
    i = args.index('--lines')
    lines_for = args[i + 1:]
    args = args[:i]
if '--top' in args:
    i = args.index('--top')
    top = int(args[i + 1])
    del args[i:i + 2]
hist, frames = args[0], int(args[1])
period = int(args[2]) if len(args) > 2 else 9973
repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def symbols(elf, lo=0, hi=1 << 32):
    out = subprocess.run(['mipsel-none-elf-nm', '-n', '-S', '-C', '--defined-only', elf], capture_output=True, text=True).stdout
    syms = []
    for l in out.splitlines():
        m = re.match(r'([0-9a-f]{8}) (?:([0-9a-f]{8}) )?([tTwW]) (.*)', l)
        if m:
            a = int(m.group(1), 16)
            if lo <= a < hi:
                syms.append((a, int(m.group(2), 16) if m.group(2) else 0, m.group(4), elf))
    return syms


syms = symbols(os.path.join(repo, 'rsdkv5.elf'))
for ovl in glob.glob(os.path.join(repo, 'build', 'gen', 'ovl', '*', '*.elf')):
    nm = subprocess.run(['mipsel-none-elf-nm', ovl], capture_output=True, text=True).stdout
    s = re.search(r'([0-9a-f]+) . __ps1_ovl_start', nm)
    e = re.search(r'([0-9a-f]+) . __ps1_ovl_end', nm)
    if s and e:
        tag = os.path.basename(ovl)[:-4]
        syms += [(a, z, '%s [%s]' % (n, tag), f) for a, z, n, f in symbols(ovl, int(s.group(1), 16), int(e.group(1), 16))]
syms.sort()
starts = [s[0] for s in syms]

data = open(hist, 'rb').read()
counts = struct.unpack('<%dI' % (len(data) // 4), data)
total = sum(counts)
by_fn = defaultdict(int)
pcs_of = defaultdict(list)
for i, c in enumerate(counts):
    if not c:
        continue
    pc = 0x80000000 + i * 4
    k = bisect.bisect_right(starts, pc) - 1
    name = '?'
    if k >= 0:
        a, z, n, f = syms[k]
        if not z or pc < a + z:
            name = n
    by_fn[name] += c
    pcs_of[name].append((pc, c))
cyc = total * period
print('%d samples over %d frames: %.1f hblanks a frame sampled (period %d cycles)' % (total, frames, cyc / HBL / frames, period))
for n, c in sorted(by_fn.items(), key=lambda x: -x[1])[:top]:
    print('%6.2f%% %6.1f hbl/frame  %s' % (100.0 * c / total, c * period / HBL / frames, n))
for want in lines_for:
    for n in [n for n in by_fn if want in n]:
        elf = next((s[3] for s in syms if s[2] == n), os.path.join(repo, 'rsdkv5.elf'))
        pcs = sorted(pcs_of[n])
        out = subprocess.run(['mipsel-none-elf-addr2line', '-e', elf] + ['%x' % p for p, _ in pcs], capture_output=True, text=True).stdout.splitlines()
        by_line = defaultdict(int)
        for (p, c), l in zip(pcs, out):
            by_line[l.split('/')[-1].split(' (')[0]] += c
        print('== %s: %d samples, %.1f hbl/frame' % (n, by_fn[n], by_fn[n] * period / HBL / frames))
        for l, c in sorted(by_line.items(), key=lambda x: -x[1])[:25]:
            print('  %6.2f%% %5.1f hbl/frame  %s' % (100.0 * c / by_fn[n], c * period / HBL / frames, l))
