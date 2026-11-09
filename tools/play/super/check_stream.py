#!/usr/bin/env python3
"""prove the streamed music's disc -> RAM half of the playback claim (docs/ports/mania/63 M5).

tools/play/super/stream.gdb dumps, at each stop, the 2,048-byte sector the stream is about to play
(g_ps1MusicStreamSector) together with its index in the body (g_ps1MusicStreamSrcSector). This compares that dump with
Data/Music/Super.adp byte for byte and decodes the sector with a host SPU-ADPCM decoder, so "the theme is playing"
cannot rest on counters alone: the bytes in the ring are the theme's bytes, and they are audio.

Usage: check_stream.py DUMP.bin SECTOR_INDEX [BODY]
Exit status 0 when identical and loud enough; prints what it found either way.
"""
import os, sys

BODY_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            '../../../build/music/Music/Super.adp')


def spu_decode(body):
    pos, step, filt = 0, 0, 0
    coeffs = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
    out = []
    while pos + 16 <= len(body):
        head = body[pos]
        shift, fi = 12 - (head & 0x0F), (head >> 4) & 0x0F
        f0, f1 = coeffs[fi]
        for b in body[pos + 2:pos + 16]:
            for nib in (b & 0x0F, b >> 4):
                if nib >= 8:
                    nib -= 16
                s = (nib << shift) + (step * f0 + filt * f1 + 32) // 64
                s = max(-32768, min(32767, s))
                out.append(s)
                filt, step = step, s
        pos += 16
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit('usage: check_stream.py DUMP.bin SECTOR_INDEX [BODY]')
    dump_path, sector = sys.argv[1], int(sys.argv[2])
    body_path = sys.argv[3] if len(sys.argv) > 3 else BODY_DEFAULT
    dump = open(dump_path, 'rb').read()
    body = open(body_path, 'rb').read()
    off = sector * 2048
    ref = body[off:off + 2048]
    if len(dump) != 2048 or len(ref) != 2048:
        raise SystemExit('short read: dump %d, body %d at sector %d (body %d B)' %
                         (len(dump), len(ref), sector, len(body)))
    same = dump == ref
    dec = spu_decode(dump)
    rms = (sum(s * s for s in dec) / len(dec)) ** 0.5
    peak = max(abs(s) for s in dec)
    print('sector %d of %d (%d B body): bytes %s, decoded rms %.0f peak %d, %.2f s of audio' %
          (sector, len(body) // 2048, len(body), 'IDENTICAL' if same else 'DIFFER', rms, peak, len(dec) / 22050))
    if not same:
        diff = next(i for i in range(2048) if dump[i] != ref[i])
        print('  first difference at byte %d: dump %02x ref %02x' % (diff, dump[diff], ref[diff]))
        raise SystemExit(1)
    if rms < 100:
        print('  the sector decodes to silence: not the theme')
        raise SystemExit(1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
