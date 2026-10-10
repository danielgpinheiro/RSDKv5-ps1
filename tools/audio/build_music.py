#!/usr/bin/env python3
"""Build-time CD-XA music for the Mania PS1 port (docs/32 phase 5.2; Sonic 2's tool, RSDKv4-ps1):
Data/Music/*.ogg -> Data/Music/Music.xa + Music.bin.

Format (as the earlier ports): XA-ADPCM 37.8 kHz stereo 4-bit, 18.75 sectors/s per channel; at 2x CD speed one channel
takes 1 sector in 8, so the file interleaves 8 channels: physical sector r*8 + k carries row r of channel k. The drive
plays only the channel selected with Setfilter (file 1, channel k). Each channel holds its tracks back to back
(longest first into the least filled channel). Each track in its channel:
  - its audio rows. The PCM is resampled to 37,800 Hz by ffmpeg (swr, 64-tap filter) and **prefixed with silence** so
    the track's loop point lands on a row boundary; a row holds 2,016 stereo samples. 37,800 / 44,100 = 6 / 7 exactly,
    so the engine maps any 44.1 kHz sample position the PC uses (PlayStream's startPos / loopPoint, GetChannelPos) to
    a row in 32 bits: row = (round(pos * 6 / 7) + pad) / 2016. Pad = (-L') mod 2016, L' = round(L * 6 / 7), L = the
    track's loop point (at most 53 ms). The loop point: Music_SetMusicTrack / RSDK.PlayStream constants in the game's
    sources, then the scenes' Music entities (trackFile / trackLoop); a track given several keeps the most common (the
    others land on the nearest row at run time, listed in the output).
  - one Form-1 data row, the end marker: file 1, channel k, payload b'RSDKXAEND' + channel + u16 track id. Data sectors
    reach the CPU in XA mode (ps1/xa_music): it loops (Setloc to the loop row) or ends.
  - GUARD silent audio rows: they play while the loop Setloc / Pause takes effect, so the next track in the channel is
    never heard.
Rows past a channel's end are filler (silent XA audio on channel 31, never selected). psxavenc's EOF submode bits are
cleared.

Output: Data/Music/Music.xa (2336-byte sectors, mkpsxiso type="mixed") and Data/Music/Music.bin:
  'XAM5', u16 count, u16 interleave (8), u32 file sectors;
  count x { u32 name hash (FNV-1a of the lower-case name as PlayStream gets it, "greenhill1.ogg"), u16 start row,
            u16 audio rows, u16 pad (samples), u8 channel, u8 track id }                        (12 B, kept in RAM)
Self-check: every track's rows are read back from the written file (channel, submode, marker, guard) and its first
audio row is decoded (xa_adpcm) to confirm the leading pad is silent.

Usage: build_music.py SRC_DATA_DIR OUT_DATA_DIR   (needs psxavenc, ffmpeg)
"""
import collections, glob, os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import xa_adpcm  # noqa: E402
import rsdk5_formats as F  # noqa: E402

PSXAVENC = os.environ.get('PSXAVENC') or next((p for p in [os.path.join(HERE, '../../../psxavenc/build/psxavenc')] if os.path.exists(p)), 'psxavenc')  # (phase 11: else PATH)
FFMPEG = os.environ.get('FFMPEG') or 'ffmpeg'
GAME = os.path.join(HERE, '..', '..', 'SonicMania')
SECTOR = 2336
INTERLEAVE = 8
FILE_NO = 1
FILLER_CHANNEL = 31
RATE = 37800
ROW_SAMPLES = 2016
GUARD = 4
# Base Mania's scope (docs/32 user decisions): Encore / competition music is not built
OUT_OF_SCOPE = {'encoremenu.ogg', 'competition.ogg', 'buddybeat.ogg', 'pinball.ogg'}


def fnv1a(name):
    h = 0x811C9DC5
    for b in name.lower().encode():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def rows_of(pos44, pad):
    """the engine's mapping (PS1AudioDevice.cpp): 44.1 kHz sample -> row from the track's start"""
    return ((pos44 * 6 + 3) // 7 + pad) // ROW_SAMPLES


def subheader(channel, submode, coding):
    return bytes([FILE_NO, channel, submode, coding] * 2)


def end_sector(channel, track_id):
    payload = (b'RSDKXAEND' + bytes([channel]) + struct.pack('<H', track_id)).ljust(2048, b'\0')
    return (subheader(channel, 0x08, 0) + payload).ljust(SECTOR, b'\0')  # Form 1 data; EDC/ECC by mkpsxiso


def silent_sector(channel, coding):
    return (subheader(channel, 0x64, coding)).ljust(SECTOR, b'\0')  # XA audio, all-zero sound groups


def loop_points(data):
    """{lower-case name: Counter(loop point)}: code constants (Music_SetMusicTrack(name, track, loop), RSDK.PlayStream(
    name, slot, start, loop)) and the Music entities of every scene (trackFile, trackLoop). false / 0 = no loop."""
    out = collections.defaultdict(collections.Counter)
    for root, _, fs in os.walk(os.path.join(GAME, 'Objects')):
        for f in fs:
            if not f.endswith('.c'):
                continue
            src = open(os.path.join(root, f), errors='replace').read()
            for name, loop in re.findall(r'Music_SetMusicTrack\(\s*"([^"]+)"\s*,\s*\w+\s*,\s*(\w+)\s*\)', src):
                out[name.lower()][0 if loop == 'false' else int(loop)] += 100  # code first
            for name, loop in re.findall(r'RSDK\.PlayStream\(\s*"([^"]+)"\s*,[^,]+,\s*\w+\s*,\s*(\w+)', src):
                out[name.lower()][0 if loop == 'false' else int(loop)] += 100
    H = {F.name_hash(n): n for n in ('trackFile', 'trackLoop')}
    mh = F.name_hash('Music')
    for folder in sorted(os.listdir(os.path.join(data, 'Stages'))):
        d = os.path.join(data, 'Stages', folder)
        for f in sorted(os.listdir(d)):
            if not (f.startswith('Scene') and f.endswith('.bin')):
                continue
            for o in F.read_scene(os.path.join(d, f))['objects']:
                if o['hash'] != mh:
                    continue
                for e in o['entities']:
                    v = {H[h]: x for h, x in e['vars'].items() if h in H}
                    if v.get('trackFile'):
                        out[v['trackFile'].lower()][int.from_bytes(v['trackLoop'], 'little')] += 1
    return out


def encode(src, loop44, tmp, channel):
    """-> (audio sectors, pad) for one track."""
    d = open(src, 'rb').read()
    if d[:4] != b'OggS':
        d = bytes(b ^ 0xFF for b in d)
    ogg, wav, xa = os.path.join(tmp, 'in.ogg'), os.path.join(tmp, 'in.wav'), os.path.join(tmp, 'out.xa')
    open(ogg, 'wb').write(d)
    loop = (loop44 * 6 + 3) // 7 if loop44 else 0
    pad = (-loop) % ROW_SAMPLES
    subprocess.run([FFMPEG, '-v', 'error', '-y', '-threads', '1', '-i', ogg, '-af',
                    'aresample=%d:resampler=swr:filter_size=64,adelay=%dS:all=1' % (RATE, pad),
                    '-ac', '2', '-ar', str(RATE), '-c:a', 'pcm_s16le', wav], check=True)
    subprocess.run([PSXAVENC, '-q', '-t', 'xa', '-f', str(RATE), '-c', '2', '-b', '4', '-F', str(FILE_NO), '-C', str(channel), wav, xa],
                   check=True)
    x = open(xa, 'rb').read()
    assert len(x) % SECTOR == 0, src
    secs = [bytearray(x[i:i + SECTOR]) for i in range(0, len(x), SECTOR)]
    for s in secs:
        assert s[0] == FILE_NO and s[1] == channel and s[2] & 0x04, (src, s[:8].hex())
        s[2] &= 0x7F  # clear EOF
        s[6] &= 0x7F
    return [bytes(s) for s in secs], pad


def main():
    src_data, out_data = sys.argv[1], sys.argv[2]
    root = os.path.join(src_data, 'Music')
    out_root = os.path.join(out_data, 'Music')
    os.makedirs(out_root, exist_ok=True)
    loops = loop_points(src_data)
    tracks = sorted(os.path.basename(p) for p in glob.glob(os.path.join(root, '*.ogg')) if os.path.basename(p).lower() not in OUT_OF_SCOPE)
    assert len(tracks) < 256
    hashes = [fnv1a(t) for t in tracks]
    assert len(set(hashes)) == len(hashes), 'name hash collision'
    primary, others = {}, []
    for t in tracks:
        c = loops.get(t.lower())
        primary[t] = max(c.items(), key=lambda kv: (kv[1], -kv[0]))[0] if c else 0
        if c and len(c) > 1:
            others.append('%s %d (+ %s)' % (t, primary[t], ', '.join(str(k) for k in sorted(c) if k != primary[t])))
    enc = {}
    with tempfile.TemporaryDirectory() as tmp:
        for t in tracks:
            enc[t] = encode(os.path.join(root, t), primary[t], tmp, 0)
    coding = enc[tracks[0]][0][0][3]
    # Channels: longest track first into the least filled channel.
    chans = [[] for _ in range(INTERLEAVE)]
    fill = [0] * INTERLEAVE
    for t in sorted(tracks, key=lambda t: (-len(enc[t][0]), t)):
        k = fill.index(min(fill))
        chans[k].append(t)
        fill[k] += len(enc[t][0]) + 1 + GUARD
    rows = max(fill)
    assert rows < 0x10000, 'rows per channel exceed the u16 table fields'
    placed, columns = {}, []
    for k in range(INTERLEAVE):
        col = []
        for t in chans[k]:
            tid = tracks.index(t)
            secs, pad = enc[t]
            start = len(col)
            col += [s[:1] + bytes([k]) + s[2:5] + bytes([k]) + s[6:] for s in secs]
            col.append(end_sector(k, tid))
            col += [silent_sector(k, coding)] * GUARD
            placed[t] = (k, tid, start, len(secs), pad)
        col += [silent_sector(FILLER_CHANNEL, coding)] * (rows - len(col))
        columns.append(col)
    out = bytearray()
    for r in range(rows):
        for k in range(INTERLEAVE):
            out += columns[k][r]
    out = xa_adpcm.clear_unused(out)  # reproducible build
    open(os.path.join(out_root, 'Music.xa'), 'wb').write(out)
    table = struct.pack('<4sHHI', b'XAM5', len(tracks), INTERLEAVE, len(out) // SECTOR)
    for t in tracks:
        k, tid, start, n, pad = placed[t]
        table += struct.pack('<IHHHBB', fnv1a(t), start, n, pad, k, tid)
    open(os.path.join(out_root, 'Music.bin'), 'wb').write(table)
    # Self-check from the written file.
    for t in tracks:
        k, tid, start, n, pad = placed[t]
        sec = lambda r: out[(r * INTERLEAVE + k) * SECTOR:(r * INTERLEAVE + k + 1) * SECTOR]
        for r in (start, start + n - 1):
            assert sec(r)[1] == k and sec(r)[2] & 0x04 and not sec(r)[2] & 0x80, (t, r)
        m = sec(start + n)
        assert m[1] == k and m[2] & 0x08 and m[8:17] == b'RSDKXAEND' and m[17] == k and struct.unpack_from('<H', m, 18)[0] == tid, t
        for g in range(GUARD):
            assert sec(start + n + 1 + g)[1] == k and sec(start + n + 1 + g)[2] & 0x04, t
        if primary[t]:
            assert rows_of(primary[t], pad) * ROW_SAMPLES == (primary[t] * 6 + 3) // 7 + pad, t  # the loop on a row
            assert rows_of(primary[t], pad) < n, t
        if pad >= 64:
            left, right = xa_adpcm.decode_sectors([sec(start)])
            assert abs(left[:pad - 32]).max() < 64 and abs(right[:pad - 32]).max() < 64, (t, 'lead pad not silent')
    for k in range(INTERLEAVE):
        print('ch%d %2d tracks %6d rows %5.1f min' % (k, len(chans[k]), fill[k], fill[k] / 18.75 / 60))
    print('Music.xa: %d sectors (%.1f MB on disc), %d tracks, 37.8 kHz stereo 4-bit | loops: %d, none %d | Music.bin %d B' % (
        len(out) // SECTOR, len(out) / SECTOR * 2352 / 1048576, len(tracks), sum(1 for t in tracks if primary[t]),
        sum(1 for t in tracks if not primary[t]), len(table)))
    for o in others:
        print('  several loop points: ' + o)


if __name__ == '__main__':
    main()
