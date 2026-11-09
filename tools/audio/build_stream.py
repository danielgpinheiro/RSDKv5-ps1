#!/usr/bin/env python3
"""Build-time streamed music body for the Mania PS1 port (docs/ports/mania/63 M5).

Data/Music/<Name>.ogg -> Data/Music/<Name>.adp: raw SPU-ADPCM, 22,050 Hz mono, sector-padded. The runtime
(ps1/music_stream.cpp) streams it from the CD into a 2 KiB SPU ring on voice 17 and plays it at pitch 0x800, so a theme
that cannot be resident (the Super theme is 500,976 B, no SFX bank can hold it) never loads: only DMA of the bytes the
disc delivers, no software decoding.

Why 22,050 Hz rather than 44,100: psxavenc writes the frames at the requested rate (`-f`), and an ADPCM frame carries
its samples at its encoded rate, so a 22,050 Hz body played at pitch 0x800 decodes at 22,050 Hz - the quality floor of
the docs/ports/mania/63 M5 table, at half the bytes. 44,100 Hz is the fallback if the ring ever proves too short.

The loop point is NOT baked here: the game passes it at PlayStream time (streamLoopPoint, 44.1 kHz samples) and the
runtime restarts the read at that sector. So this file is the whole theme, unmodified.

Self-check (never a blind encode): the body is decoded back with a host SPU-ADPCM decoder, its rate/size header is
asserted, its length against the source's duration is asserted, and the decode's RMS is compared with the source PCM's
so silence or garbage cannot pass.

Usage: build_stream.py SRC_DATA_DIR OUT_DATA_DIR TRACK...   TRACK = "Super" (Data/Music/Super.ogg), default Super.
Needs psxavenc and ffmpeg.
"""
import array, math, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PSXAVENC = os.environ.get('PSXAVENC') or next(
    (p for p in [os.path.join(HERE, '../../../psxavenc/build/psxavenc')] if os.path.exists(p)), 'psxavenc')
RATE = 22050
SECTOR = 2048


def spu_adpcm_decode(body):
    """SPU-ADPCM (16-byte blocks: u8 shift/filter, u8 flags, 28 nibbles) -> list of int16 samples."""
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


def rms(samples):
    return math.sqrt(sum(s * s for s in samples) / max(1, len(samples)))


def build(src, out_dir, name):
    ogg = os.path.join(src, 'Music', name + '.ogg')
    if not os.path.exists(ogg):
        raise SystemExit('build_stream: missing ' + ogg)
    out = os.path.join(out_dir, 'Music', name + '.adp')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, 'in.wav')
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', ogg, '-ar', str(RATE), '-ac', '1',
                        '-c:a', 'pcm_s16le', wav], check=True)
        pcm = array.array('h')
        with open(wav, 'rb') as f:
            data = f.read()
        body_start = data.index(b'data') + 8
        pcm.frombytes(data[body_start:body_start + ((len(data) - body_start) // 2) * 2])
        vag = os.path.join(tmp, 'out.vag')
        subprocess.run([PSXAVENC, '-q', '-t', 'vag', '-f', str(RATE), wav, vag], check=True)
        v = open(vag, 'rb').read()
    assert v[:4] == b'VAGp', 'not a VAG'
    # field 12 counts the samples in bytes and stops 32 B short of the padded body (the encoder's last block); the
    # file's remainder is the 64-byte-padded body, which is what a bank holds (tools/audio/build_sfx.py reads it the
    # same way) and what the ring streams.
    size, vrate = int.from_bytes(v[12:16], 'big'), int.from_bytes(v[16:20], 'big')
    body = v[48:]
    assert vrate == RATE, (vrate, RATE)
    assert len(body) % 64 == 0 and len(body) >= size, (len(body), size)
    # self-check on the encoder's own body (before the sector pad, which decodes as silence): durations and loudness
    dec = spu_adpcm_decode(body)
    body = body + b'\x00' * ((-len(body)) % SECTOR)
    # the encoder pads up to a block (28 samples) at each end, and its 64-byte tail adds a few blocks
    assert abs(len(dec) - len(pcm)) < 28 * 16, (len(dec), len(pcm))
    r_src, r_dec = rms(list(pcm)), rms(dec)
    assert r_src > 200 and 0.7 < r_dec / r_src < 1.3, (r_src, r_dec)
    open(out, 'wb').write(body)
    print('%-8s %8d B = %3d sectors  %.3f s  rms src %5.0f dec %5.0f  %s' %
          (name, len(body), len(body) // SECTOR, len(dec) / RATE, r_src, r_dec, out))
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit('usage: build_stream.py SRC_DATA_DIR OUT_DATA_DIR [TRACK...]')
    src, out_dir = sys.argv[1], sys.argv[2]
    tracks = sys.argv[3:] or ['Super']
    for t in tracks:
        build(src, out_dir, t)


if __name__ == '__main__':
    main()
