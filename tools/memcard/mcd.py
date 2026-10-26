#!/usr/bin/env python3
"""Memory card images on the host (phase 8): make test cards and check what the game wrote.

Sony format (psx-spx "Memory Card Data Format", the same layout psyqo's MemoryCardFileSystem
writes): 128 KB = 16 blocks x 64 frames x 128 B. Block 0 frame 0 = "MC" header, frames 1-15 =
directory (0x51 first / 0x52 middle / 0x53 last block in use, 0xA0 free; +4 size, +8 next block
index or 0xFFFF, +0x0A name; 0x7F = XOR checksum), 16-35 broken-sector list, 63 = "MC". A file's
first block: frame 0 = title ("SC", 0x11..0x13 icon frames, Shift-JIS title at +4, CLUT at +0x60),
then the icon frames, then the payload. The port's payload (ps1/memcard.cpp) starts with a
16-byte header: "RSDK", u16 version, u16 0, u32 length, u32 CRC-32 of the data.

Usage:
  mcd.py format OUT.mcd                 blank formatted card
  mcd.py raw OUT.mcd                    unformatted card (all zero)
  mcd.py fill OUT.mcd N                 formatted card with N one-block files of another game (docs/ports/mania/32 9f: full cards)
  mcd.py list IMAGE                     directory + titles
  mcd.py extract IMAGE NAME [--icon OUT.png] [--data OUT.bin]
  mcd.py check IMAGE NAME [--title T] [--icon SYMBOL] [--pattern N]
                                        verify title, icon (vs tools/memcard/build_icons.py) and
                                        payload (header, CRC, N bytes of (i*7+3)&0xFF)
  mcd.py absent IMAGE NAME              succeed if NAME is not on the card
  mcd.py msv IMAGE NAME [--dump DIR]    Mania's payload sections (docs/ports/mania/32 phase 8), each unpacked to DIR/<name>
  mcd.py mania IMAGE SAVEDATA.bin [--out OUT.mcd]
                                        a PC Mania save RAM (65,536 B) as the port's card file (BAHBRW-00006MANIA,
                                        SaveData.bin = its first 0xA00 words); the file's blocks reused, else a new
                                        one-block file; the rest of the card untouched (manual tests: DuckStation cards)
"""
import os, re, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FRAME, BLOCK = 128, 8192


def cksum(f):
    c = 0
    for b in f[:0x7f]:
        c ^= b
    return c


def frame(data, n):
    return data[n * FRAME:(n + 1) * FRAME]


def fmt_card():
    card = bytearray(128 * 1024)

    def put(n, f):
        f[0x7f] = cksum(f)
        card[n * FRAME:(n + 1) * FRAME] = f
    for n in (0, 63):
        f = bytearray(FRAME); f[0:2] = b'MC'; put(n, f)
    for n in range(1, 16):
        f = bytearray(FRAME); struct.pack_into('<IIH', f, 0, 0xA0, 0, 0xFFFF); put(n, f)
    for n in range(16, 36):
        f = bytearray(FRAME); struct.pack_into('<I', f, 0, 0xFFFFFFFF); struct.pack_into('<H', f, 8, 0xFFFF); put(n, f)
    return card


def sjis_table():
    """ASCII <- fullwidth Shift-JIS, from psyqo's own encoder table."""
    src = open(os.path.join(HERE, '..', '..', '..', 'nugget', 'common', 'util', 'sjis-fullwidth-ascii.cc')).read()
    codes = [int(v, 16) for v in re.findall(r'0x([0-9a-f]{4}),\s*//', src)][:95]
    return {c: chr(0x20 + i) for i, c in enumerate(codes)}


def decode_title(raw):
    t, out = sjis_table(), []
    for i in range(0, 64, 2):
        c = (raw[i] << 8) | raw[i + 1]
        if c == 0:
            break
        out.append(t.get(c, '?'))
    return ''.join(out).rstrip()


def directory(data):
    if frame(data, 0)[:2] != b'MC':
        raise SystemExit('not a formatted card')
    files = []
    for n in range(1, 16):
        f = frame(data, n)
        if cksum(f) != f[0x7f]:
            raise SystemExit('directory frame %d: bad checksum' % n)
        state, size, nxt = struct.unpack_from('<IIH', f, 0)
        if state == 0x51:
            name = f[0x0a:0x0a + 20].split(b'\0')[0].decode()
            chain, b = [n], nxt
            while b != 0xFFFF:
                chain.append(b + 1)
                b = struct.unpack_from('<H', frame(data, b + 1), 8)[0]
            files.append({'name': name, 'size': size, 'blocks': chain})
    return files


def read_file(data, name):
    for f in directory(data):
        if f['name'] == name:
            body = b''.join(data[b * BLOCK:(b + 1) * BLOCK] for b in f['blocks'])
            t = body[:FRAME]
            if t[:2] != b'SC':
                raise SystemExit('%s: title frame magic %r' % (name, t[:2]))
            frames = t[2] & 0x0f
            clut = list(struct.unpack_from('<16H', t, 0x60))
            icon = [body[(1 + i) * FRAME:(2 + i) * FRAME] for i in range(frames)]
            return {'title': decode_title(t[4:0x44]), 'clut': clut, 'icon': icon,
                    'payload': body[(1 + frames) * FRAME:], 'blocks': f['blocks']}
    return None


def payload_data(p):
    magic, ver, _, length, crc = struct.unpack_from('<4sHHII', p, 0)
    if magic != b'RSDK':
        raise SystemExit('payload magic %r' % magic)
    d = p[16:16 + length]
    if len(d) != length or (zlib.crc32(d) & 0xFFFFFFFF) != crc:
        raise SystemExit('payload CRC mismatch (length %d)' % length)
    return ver, d


def msv_sections(d):
    """Mania's payload (docs/ports/mania/32 phase 8, RSDK/User/Core/UserStorage.cpp): 'MSV1', u16 count, u16 0, then per section
    name[16], u32 raw, u32 packed, the packed words (u32 tokens: bit 31 = that many zero words, else that many literal
    words follow). -> [(name, raw bytes, packed bytes, unpacked data)]"""
    magic, count = struct.unpack_from('<4sH', d, 0)
    if magic != b'MSV1':
        raise SystemExit('not an MSV1 payload: %r' % magic)
    out, p = [], 8
    for _ in range(count):
        name = d[p:p + 16].split(b'\0')[0].decode()
        raw, packed = struct.unpack_from('<II', d, p + 16)
        p += 24
        words = struct.unpack_from('<%dI' % (packed // 4), d, p)
        p += packed
        data, i = bytearray(), 0
        while i < len(words):
            t = words[i]; i += 1
            if t & 0x80000000:
                data += bytes(4 * (t & 0x7FFFFFFF))
            else:
                for v in words[i:i + t]:
                    data += struct.pack('<I', v)
                i += t
        if len(data) < raw:
            raise SystemExit('%s: unpacks to %d of %d bytes' % (name, len(data), raw))
        out.append((name, raw, packed, bytes(data[:raw])))
    if p != len(d):
        raise SystemExit('MSV1: %d bytes after the sections' % (len(d) - p))
    return out


def icon_png(f, path):
    from PIL import Image
    im = Image.new('RGBA', (16, 16))
    for y in range(16):
        for x in range(16):
            i = (f['icon'][0][y * 8 + x // 2] >> (4 * (x & 1))) & 0xF
            v = f['clut'][i]
            im.putpixel((x, y), (0, 0, 0, 0) if v == 0 else ((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3, 255))
    im.resize((128, 128), Image.NEAREST).save(path)


def expected_icon(symbol):
    """(clut, pixels) of a symbol in ps1/memcard_icons.h."""
    src = open(os.path.join(HERE, '..', '..', 'ps1', 'memcard_icons.h')).read()
    m = re.search(re.escape(symbol) + r' = \{\s*1,\s*\{([^}]*)\},\s*\{\{([^}]*)\}\}', src)
    if not m:
        raise SystemExit('icon %s not in ps1/memcard_icons.h' % symbol)
    return [int(v, 16) for v in m[1].split(',') if v.strip()], bytes(int(v, 16) for v in m[2].split(',') if v.strip())


MANIA_FILE, MANIA_TITLE, MANIA_KEEP = 'BAHBRW-00006MANIA', 'SONIC MANIA', 0xA00 * 4


def msv_pack(src):
    """UserStorage.cpp PS1CardPack: zero runs -> 0x80000000 | n; literal runs (which go on over a single zero word
    between non-zero ones) -> n, then the n words"""
    w = struct.unpack('<%dI' % (len(src) // 4), src)
    out, i = [], 0
    while i < len(w):
        z = i
        while z < len(w) and not w[z]:
            z += 1
        if z > i:
            out.append(0x80000000 | (z - i)); i = z; continue
        lit = i
        while lit < len(w) and (w[lit] or (lit + 1 < len(w) and w[lit + 1])):
            lit += 1
        out.append(lit - i); out += w[i:lit]; i = lit
    return struct.pack('<%dI' % len(out), *out)


def mania_payload(save):
    """ps1/memcard's header + Mania's MSV1 payload with one section, SaveData.bin"""
    raw = save[:MANIA_KEEP]
    packed = msv_pack(raw)
    d = struct.pack('<4sHH', b'MSV1', 1, 0) + b'SaveData.bin'.ljust(16, b'\0') + struct.pack('<II', len(raw), len(packed)) + packed
    return struct.pack('<4sHHII', b'RSDK', 1, 0, len(d), zlib.crc32(d) & 0xFFFFFFFF) + d


def mania_card(card, save):
    """card with the save in MANIA_FILE (in place)"""
    clut, pixels = expected_icon('s_ps1McIconMania')
    payload = mania_payload(save)
    f = read_file(card, MANIA_FILE)
    if f is None:  # a new one-block file in the first free block: directory frame, title frame, icon frame
        free = [n for n in range(1, 16) if struct.unpack_from('<I', frame(card, n), 0)[0] & 0xF0 == 0xA0]
        if not free:
            raise SystemExit('no free block on the card')
        n = free[0]
        d = bytearray(FRAME)
        struct.pack_into('<IIH', d, 0, 0x51, BLOCK, 0xFFFF)
        d[0x0a:0x0a + len(MANIA_FILE)] = MANIA_FILE.encode()
        d[0x7f] = cksum(d)
        card[n * FRAME:(n + 1) * FRAME] = d
        t = bytearray(FRAME)
        t[0:4] = b'SC\x11\x01'
        enc = {v: k for k, v in sjis_table().items()}
        for i, ch in enumerate(MANIA_TITLE):
            struct.pack_into('>H', t, 4 + 2 * i, enc[ch])
        struct.pack_into('<16H', t, 0x60, *clut)
        body = bytearray(BLOCK)
        body[0:FRAME] = t
        body[FRAME:2 * FRAME] = pixels
        card[n * BLOCK:(n + 1) * BLOCK] = body
        f = read_file(card, MANIA_FILE)
    start = (1 + len(f['icon'])) * FRAME  # the payload after the title and icon frames
    room = len(f['blocks']) * BLOCK - start
    if len(payload) > room:
        raise SystemExit('payload %d B > the file\'s room %d B' % (len(payload), room))
    body = bytearray(b''.join(card[b * BLOCK:(b + 1) * BLOCK] for b in f['blocks']))
    body[start:] = payload + bytes(room - len(payload))
    for k, b in enumerate(f['blocks']):
        card[b * BLOCK:(b + 1) * BLOCK] = body[k * BLOCK:(k + 1) * BLOCK]
    # self-check: the card reads back as the save's kept part
    r = read_file(card, MANIA_FILE)
    secs = msv_sections(payload_data(r['payload'])[1])
    if [(s[0], s[3]) for s in secs] != [('SaveData.bin', save[:MANIA_KEEP])]:
        raise SystemExit('self-check failed')
    return card, len(payload), r['blocks']


def main():
    a = sys.argv[1:]
    if not a:
        raise SystemExit(__doc__)
    cmd = a[0]
    opt = lambda k: a[a.index(k) + 1] if k in a else None
    if cmd == 'format':
        open(a[1], 'wb').write(fmt_card()); print('formatted card %s' % a[1]); return
    if cmd == 'raw':
        open(a[1], 'wb').write(bytes(128 * 1024)); print('unformatted card %s' % a[1]); return
    if cmd == 'fill':  # N one-block files ("BASLUS-99999FILLnn", a plain title frame) in blocks 1..N
        card, n = fmt_card(), int(a[2])
        for k in range(n):
            f = bytearray(FRAME)
            struct.pack_into('<IIH', f, 0, 0x51, BLOCK, 0xFFFF)
            nm = ('BASLUS-99999FILL%02d' % k).encode()
            f[0x0a:0x0a + len(nm)] = nm
            f[0x7f] = cksum(f)
            card[(1 + k) * FRAME:(2 + k) * FRAME] = f
            body = bytearray(BLOCK)
            body[0:4] = b'SC\x11\x01'
            card[(1 + k) * BLOCK:(2 + k) * BLOCK] = body
        open(a[1], 'wb').write(card); print('card %s: %d one-block file(s), %d free' % (a[1], n, 15 - n)); return
    data = open(a[1], 'rb').read()
    if cmd == 'mania':
        save = open(a[2], 'rb').read()
        if len(save) < MANIA_KEEP:
            raise SystemExit('%s: %d B, not a Mania save RAM' % (a[2], len(save)))
        card, n, blocks = mania_card(bytearray(data), save)
        out = opt('--out') or a[1]
        open(out, 'wb').write(card)
        print('%s: %s blocks %s, payload %d B (SaveData.bin %d of %d B, %d B dropped past it non-zero)' % (
            out, MANIA_FILE, blocks, n, MANIA_KEEP, len(save), sum(1 for b in save[MANIA_KEEP:] if b)))
        return
    if cmd == 'list':
        fs = directory(data)
        print('%d file(s), %d free block(s)' % (len(fs), 15 - sum(len(f['blocks']) for f in fs)))
        for f in fs:
            r = read_file(data, f['name'])
            print('  %-20s blocks %s  title "%s"  icon frames %d' % (f['name'], f['blocks'], r['title'], len(r['icon'])))
        return
    f = read_file(data, a[2])
    if cmd == 'absent':
        print('%s: %s' % (a[2], 'absent' if f is None else 'PRESENT'))
        sys.exit(0 if f is None else 1)
    if f is None:
        raise SystemExit('%s: not on the card' % a[2])
    if cmd == 'extract':
        if opt('--icon'):
            icon_png(f, opt('--icon'))
        if opt('--data'):
            open(opt('--data'), 'wb').write(payload_data(f['payload'])[1])
        print('%s: title "%s", blocks %s' % (a[2], f['title'], f['blocks'])); return
    if cmd == 'msv':  # Mania's sections; --dump DIR writes each section's bytes
        ver, d = payload_data(f['payload'])
        secs = msv_sections(d)
        print('%s: title "%s", blocks %s, payload v%d %d B, CRC ok, %d section(s)' % (a[2], f['title'], f['blocks'], ver, len(d), len(secs)))
        for name, raw, packed, data in secs:
            print('  %-16s raw %6d B  packed %6d B' % (name, raw, packed))
            if opt('--dump'):
                os.makedirs(opt('--dump'), exist_ok=True)
                open(os.path.join(opt('--dump'), name), 'wb').write(data)
        return
    if cmd == 'check':
        ok = True
        if opt('--title') is not None and f['title'] != opt('--title'):
            print('title "%s" != "%s"' % (f['title'], opt('--title'))); ok = False
        if opt('--icon'):
            clut, pixels = expected_icon(opt('--icon'))
            if f['clut'] != clut or bytes(f['icon'][0]) != pixels:
                print('icon differs from %s' % opt('--icon')); ok = False
        ver, d = payload_data(f['payload'])
        if opt('--pattern'):
            n = int(opt('--pattern'))
            if d != bytes((i * 7 + 3) & 0xFF for i in range(n)):
                print('payload differs from the %d-byte pattern' % n); ok = False
        print('%s: %s (title "%s", blocks %s, payload v%d %d B, CRC ok)' % (
            a[2], 'OK' if ok else 'MISMATCH', f['title'], f['blocks'], ver, len(d)))
        sys.exit(0 if ok else 1)
    raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
