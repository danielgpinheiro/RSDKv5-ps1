#!/usr/bin/env python3
"""Build-time SFX for the Mania PS1 port (docs/32 phase 5.1): one SPU-ADPCM bank per scene.

The engine registers GameConfig's global SFX and each StageConfig's list by name (LoadSfx); a scene can only play what
the objects it runs ask for (tools/audio/sfx_sets.py). Each scene gets Data/Stages/<Folder>/PS1Sfx<id>.bin with those
samples, uploaded to SPU RAM in one read when the scene loads.

Rates: one per sample per bank (Sonic 2's solver): every sample starts at min(source rate, 44.1 kHz); while the bank is
over its budget the largest sample steps down one rate (RATES), none below FLOOR while another is above it. Budget =
min(SPU sample area, BANK_CAP): the whole SPU sample area by default (docs/32 choice 84: every sound keeps >= 11,025 Hz;
Sonic 2 capped its stage banks at 400 KB for shorter loads, PS1_SFX_BANK_CAP=409600 does the same here). The rates per
scene are logged to build/sfx_rates.txt (the listening check).

Sources: Data/SoundFX/**/*.wav (8 / 16-bit PCM or float, mono or stereo), read here to mono 16-bit PCM; psxavenc encodes
`-t vag` (48-byte header, data padded to 64 bytes with a trailing loop-trap block).

Bank (little endian): 'PSF5', u16 count, u16 pad; count x (char name[40] (NUL-padded SFX path as LoadSfx gets it),
u32 offset, u32 size, u32 rate, u32 frames at 44.1 kHz) (56 B each); then the sample data (each a multiple of 64 bytes:
SPU DMA). Each bank is read back and compared.

Usage: build_sfx.py SRC_DATA_DIR OUT_DATA_DIR [FOLDER ...]   (needs psxavenc; PS1_SFX_BANK_CAP=bytes)
"""
import os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sfx_sets  # noqa: E402

PSXAVENC = os.environ.get('PSXAVENC') or os.path.join(HERE, '../../../psxavenc/build/psxavenc')
FFMPEG = os.environ.get('FFMPEG') or 'ffmpeg'
SPU_SAMPLE_START = 0x1000
SPU_SAMPLE_END = 0x80000 - 32
RATES = (44100, 37800, 32000, 27000, 22050, 18900, 16000, 13000, 11025, 9450, 8000)
FLOOR = 11025
assert all(r % 25 == 0 for r in RATES)  # the engine's 32-bit position math (PS1AudioDevice.cpp PS1SfxAddrAt)
BANK_CAP = int(os.environ.get('PS1_SFX_BANK_CAP') or SPU_SAMPLE_END - SPU_SAMPLE_START)
NAME_LEN = 40
ENTRY = struct.Struct('<40sIIII')


def vag_size(frames, src_rate, rate):
    samples = -(-frames * rate // src_rate)
    blocks = -(-samples // 28) + 2  # + leading dummy block + trailing loop-trap block
    return -(-blocks * 16 // 64) * 64


def choose_rates(names, info, budget):
    rate = {n: next(k for k, r in enumerate(RATES) if r <= info[n][1]) for n in names}

    def size(n):
        return vag_size(info[n][0], info[n][1], RATES[rate[n]])

    while sum(size(n) for n in names) > budget:
        cand = [n for n in names if RATES[rate[n]] > FLOOR] or [n for n in names if rate[n] + 1 < len(RATES)]
        if not cand:
            return None
        n = max(cand, key=lambda n: (size(n), n))
        rate[n] += 1
    return rate


class Encoder:
    def __init__(self, src, tmp):
        self.src, self.tmp, self.pcm, self.vag = src, tmp, {}, {}

    def info(self, path):
        """(frames, rate) of the mono 16-bit PCM decode. The WAV is read as RSDK reads it: by bit depth (8-bit unsigned,
        16-bit signed, 32-bit float), whatever the format tag says (Global/Flying.wav: tag 3 on 16-bit data)."""
        if path not in self.pcm:
            import array
            d = open(os.path.join(self.src, path), 'rb').read()
            assert d[:4] == b'RIFF' and d[8:12] == b'WAVE', path
            i, ch, rate, bits, data = 12, 1, 44100, 16, b''
            while i + 8 <= len(d):
                cid, cs = d[i:i + 4], struct.unpack_from('<I', d, i + 4)[0]
                if cid == b'fmt ':
                    ch, rate = struct.unpack_from('<HI', d, i + 10)
                    bits = struct.unpack_from('<H', d, i + 22)[0]
                elif cid == b'data':
                    data = d[i + 8:i + 8 + cs]
                i += 8 + cs + (cs & 1)
            if bits == 8:
                v = [(b - 128) << 8 for b in data]
            elif bits == 16:
                v = array.array('h', data[:len(data) // 2 * 2]).tolist()
            elif bits == 32:
                v = [max(-32768, min(32767, int(x * 32767))) for x in array.array('f', data[:len(data) // 4 * 4])]
            else:
                raise SystemExit('%s: %d-bit samples' % (path, bits))
            if ch > 1:
                v = [sum(v[k:k + ch]) // ch for k in range(0, len(v) - ch + 1, ch)]
            out = os.path.join(self.tmp, 'pcm%d.wav' % len(self.pcm))
            pcm = array.array('h', v).tobytes()
            hdr = b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16)
            open(out, 'wb').write(hdr + b'data' + struct.pack('<I', len(pcm)) + pcm)
            self.pcm[path] = (out, len(v), rate)
        return self.pcm[path][1:]

    def encode(self, path, rate):
        key = (path, rate)
        if key not in self.vag:
            out = os.path.join(self.tmp, 'v%d.vag' % len(self.vag))
            subprocess.run([PSXAVENC, '-q', '-t', 'vag', '-f', str(rate), self.pcm[path][0], out], check=True)
            v = open(out, 'rb').read()
            assert v[:4] == b'VAGp', path
            size, vrate = struct.unpack_from('>I', v, 12)[0], struct.unpack_from('>I', v, 16)[0]
            body = v[48:]
            assert vrate == rate and len(body) >= size and len(body) % 64 == 0, (path, rate)
            self.vag[key] = body
        return self.vag[key]


def write_bank(path, entries):
    """entries: [(name, rate, frames44, data)] -> the bank file; read back and checked. Returns the data bytes."""
    table, blob = bytearray(), bytearray()
    for name, rate, frames44, data in entries:
        nb = name.encode()
        assert len(nb) < NAME_LEN, name
        table += ENTRY.pack(nb, len(blob), len(data), rate, frames44)
        blob += data
    out = b'PSF5' + struct.pack('<HH', len(entries), 0) + table + blob
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(out)
    back = open(path, 'rb').read()
    n = struct.unpack_from('<H', back, 4)[0]
    base = 8 + ENTRY.size * n
    assert n == len(entries)
    for k, (name, rate, frames44, data) in enumerate(entries):
        nb, off, size, r2, f2 = ENTRY.unpack_from(back, 8 + ENTRY.size * k)
        assert nb.rstrip(b'\0').decode() == name and (r2, f2) == (rate, frames44) and size % 64 == 0, path
        assert back[base + off:base + off + size] == data, path
    return len(blob)


def main():
    a = sys.argv[1:]
    src_data, out_data, folders = a[0], a[1], a[2:] or None
    sets = sfx_sets.sets(src_data, folders)
    src_sfx = os.path.join(src_data, 'SoundFX')
    budget = min(SPU_SAMPLE_END - SPU_SAMPLE_START, BANK_CAP)
    log = []
    with tempfile.TemporaryDirectory() as tmp:
        enc = Encoder(src_sfx, tmp)
        for (folder, sid), names in sets.items():
            # 7b: a name the game asks for that the data pack lacks (CPZ's Staircase: CPZ2HitBlocks.wav): on the PC its
            # GetSfx finds nothing and nothing plays; left out of the bank the same way
            missing = [n for n in names if not os.path.exists(os.path.join(src_sfx, n))]
            for n in missing:
                print('%s%s: %s not in the data (the PC plays nothing either)' % (folder, sid, n))
            names = [n for n in names if n not in missing]
            info = {n: enc.info(n) for n in names}
            rate = choose_rates(names, info, budget)
            if rate is None:
                sys.exit('%s%s: the SFX do not fit %d bytes even at %d Hz' % (folder, sid, budget, RATES[-1]))
            entries = []
            for n in names:
                r = RATES[rate[n]]
                frames44 = -(-info[n][0] * 44100 // info[n][1])
                entries.append((n, r, frames44, enc.encode(n, r)))
            size = write_bank(os.path.join(out_data, 'Stages', folder, 'PS1Sfx%s.bin' % sid), entries)
            hist = {}
            for n in names:
                hist[RATES[rate[n]]] = hist.get(RATES[rate[n]], 0) + 1
            print('%s%s: %d SFX, %d bytes, rates %s' % (folder, sid, len(names), size,
                                                      ' '.join('%d:%d' % (r, c) for r, c in sorted(hist.items(), reverse=True))))
            log.append('%s%s' % (folder, sid))
            log += ['  %6d Hz %s' % (RATES[rate[n]], n) for n in names]
    rates_txt = os.path.join(HERE, '..', '..', 'build', 'sfx_rates.txt')
    os.makedirs(os.path.dirname(rates_txt), exist_ok=True)
    open(rates_txt, 'w').write('\n'.join(log) + '\n')


if __name__ == '__main__':
    main()
