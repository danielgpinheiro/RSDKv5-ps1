#!/usr/bin/env python3
"""Extract a Retro Engine v5 datapack ("RSDKv5": Sonic Mania) into a loose Data/ tree (build time).

Format (RSDKv5/RSDK/Storage/Storage.cpp, and the Dreamcast port's dreamcast/rsdkv5.py, which agree):
  "RSDKv5", u16 fileCount, fileCount x { MD5 of the lower-case path as 4 big-endian words (the digest with
  each 4-byte group reversed), u32 offset, u32 size (bit 31 = encrypted) }.
Names are only hashes: the paths come from a file list (default tools/rsdkv5_filelist.txt, the Dreamcast
port's 5,288 known paths); every hash no listed path produces is reported.
Encrypted files (v5 differs from v4 here) are XORed with a stream keyed by the file NAME and its size:
  A = MD5(upper-case path), B = MD5(str(size)), each as 4 big-endian words, no = (size >> 2) & 0x7F,
  posA = 0, posB = 8, swap = 0; per byte: b ^= B[posB] ^ no; if swap: b = nybble-swapped b; b ^= A[posA];
  then posA++, posB++; if posA > 15 and posB > 8: no = (no + 2) & 0x7F, and the positions restart from
  no (swap ? (no % 7, no % 12 + 2) : (no % 12 + 3, no % 7)), swap ^= 1; else posA > 15 -> posA = 0,
  swap ^= 1; posB > 12 -> posB = 0, swap ^= 1.
Every extracted file can be checked against a reference tree (--check DIR).

Usage: extract_rsdk.py DATA.rsdk OUT_DIR [--list FILELIST] [--check REF_DIR]
"""
import hashlib, os, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))


def be_words_md5(text):
    """MD5 with each 4-byte group reversed (the engine's hash words, big-endian)."""
    d = hashlib.md5(text.encode('latin1')).digest()
    return b''.join(d[i:i + 4][::-1] for i in range(0, 16, 4))


def name_hash(path):
    return be_words_md5(path.lower())


def entries(d):
    assert d[:6] == b'RSDKv5', 'not an RSDKv5 datapack: %r' % d[:6]
    n, = struct.unpack_from('<H', d, 6)
    out, p = [], 8
    for _ in range(n):
        h = d[p:p + 16]
        off, size = struct.unpack_from('<II', d, p + 16)
        out.append((h, off, size & 0x7FFFFFFF, bool(size & 0x80000000)))
        p += 24
    return out


def keystream(path, size):
    """(xorB, swap, xorA) per byte for the file `path` of `size` bytes."""
    ka, kb = be_words_md5(path.upper()), be_words_md5(str(size))
    no = (size >> 2) & 0x7F
    pa, pb, swap = 0, 8, 0
    xb = np.empty(size, np.uint8); sw = np.empty(size, np.bool_); xa = np.empty(size, np.uint8)
    for i in range(size):
        xb[i] = kb[pb] ^ no
        sw[i] = swap
        xa[i] = ka[pa]
        pa += 1
        pb += 1
        if pa > 15 and pb > 8:
            no = (no + 2) & 0x7F
            if swap:
                pa, pb = no % 7, no % 12 + 2
            else:
                pa, pb = no % 12 + 3, no % 7
            swap ^= 1
        else:
            if pa > 15:
                swap ^= 1
                pa = 0
            if pb > 12:
                swap ^= 1
                pb = 0
    return xb, sw, xa


def decrypt(path, data):
    xb, sw, xa = keystream(path, len(data))
    b = np.frombuffer(data, np.uint8) ^ xb
    b = np.where(sw, ((b << 4) | (b >> 4)) & 0xFF, b).astype(np.uint8)
    return (b ^ xa).tobytes()


def main():
    a = sys.argv[1:]
    src, out = a[0], a[1]
    listf = a[a.index('--list') + 1] if '--list' in a else os.path.join(HERE, 'rsdkv5_filelist.txt')
    ref = a[a.index('--check') + 1] if '--check' in a else None
    d = open(src, 'rb').read()
    ents = entries(d)
    known = {}
    for line in open(listf):
        line = line.strip()
        if line and not line.startswith('#'):
            known[name_hash(line)] = line
    done, missing, bad, enc_count = 0, [], [], 0
    for h, off, size, enc in ents:
        path = known.get(h)
        if path is None:
            missing.append((h.hex(), size, enc))
            continue
        data = d[off:off + size]
        if enc:
            data = decrypt(path, data)
            enc_count += 1
        dst = os.path.join(out, path)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, 'wb').write(data)
        done += 1
        if ref:
            r = os.path.join(ref, path)
            if not os.path.exists(r) or open(r, 'rb').read() != data:
                bad.append(path)
    print('%d files in the pack: %d extracted (%d decrypted), %d names unknown%s' % (
        len(ents), done, enc_count, len(missing), '' if not ref else ', %d differ from %s' % (len(bad), ref)))
    for m in missing[:20]:
        print('  unknown hash %s size %d%s' % (m[0], m[1], ' encrypted' if m[2] else ''))
    for b in bad[:20]:
        print('  DIFFERS', b)
    return 1 if missing or bad else 0


if __name__ == '__main__':
    sys.exit(main())
