#!/usr/bin/env python3
"""BGM and sound effects for the SFC build (needs ffmpeg and tools/audio/brr).

BGM: mono 11008 Hz BRR streamed from ROM (see tools/audio/driver.py).  Loop points come from
the Safari build's import settings.  Long tracks are shortened to a loop that starts at the Safari
loop point (plus a short crossfade at the join):
  ('short', W)  the first tracks (ids 1-8, kept byte-identical): the end near start + W s whose
                spectrum matches the loop start (short_loop)
  ('bars', W)   the later tracks: a whole number of bars ending near start + W s (bar_loop: the beat
                from the onset envelope, the end refined so the rhythm after the join goes on, the bar
                count picked by harmony / spectrum / rhythm match)
  ('loop',)     the Safari loop itself (loop point to the end of the file)
SE: one variation per sound, trimmed, 10240 Hz BRR inside the SPC image.

Outputs data/gen/audio.s (SPC image, SE tables), data/gen/audio.h, data/gen/audio/*.bin,
data/gen/audio/bgm_loops.json (where each track's loop comes from in the source and how its join
measures: tools/harness/bgmjoin.py) and data/gen/place/bgm.json (the BGM stream: tools/romplace.py
places it and writes bgm_table)."""
import json
import math
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
GEN = os.path.join(ROOT, 'data', 'gen')
OUT = os.path.join(GEN, 'audio')
sys.path.insert(0, os.path.join(HERE, 'extract'))
sys.path.insert(0, os.path.join(HERE, 'audio'))
import webbuild  # noqa: E402
import qoa  # noqa: E402
import driver  # noqa: E402

RATE = 11008
PITCH = round(RATE * 4096 / 32000)
SE_RATE = 10240
SE_PITCH = round(SE_RATE * 4096 / 32000)
# the BGM stream is placed by tools/romplace.py (data/gen/place/bgm.json) in whatever ROM is left
SPC_BANKS = [0xFE, 0xFF]

# id, file, shape: ('full',) intro + loop as in Safari / ('short', seconds) loop only / ('bars', seconds)
# loop only, whole bars / ('loop',) the Safari loop only / ('once', seconds).  The first 8 keep their
# ids, order and bytes (the stream starts with them); later tracks are appended (8 MB ROM: ROM_MB=8)
BGM = [
    ('BGM_OPENING', '01_opening.ogg', ('full',)),
    ('BGM_ZAKO', '02_zako_early.ogg', ('short', 34)),
    ('BGM_BOSS', '08_boss_field.ogg', ('short', 34)),
    ('BGM_MAOU', '13_boss_last.ogg', ('short', 34)),
    ('BGM_MAOU_FINAL', '50_boss_last_final.ogg', ('short', 34)),
    ('BGM_PEKO', '16_boss_secret.ogg', ('full',)),     # 裏ボス: the whole track, its own long loop
    ('BGM_VICTORY', '05_victory.mp3', ('once', 12)),
    ('BGM_GAMEOVER', '06_gameover.mp3', ('once', 9)),
    ('BGM_ZAKO_MID', '03_zako_mid.ogg', ('bars', 38)),
    ('BGM_DRAGON', '04_dragon.ogg', ('bars', 38)),
    ('BGM_B_CAVE', '09_boss_cave.ogg', ('bars', 38)),
    ('BGM_B_DESERT', '10_boss_desert.ogg', ('bars', 38)),
    ('BGM_B_ICE', '11_boss_ice.ogg', ('bars', 38)),
    ('BGM_B_VOLCANO', '12_boss_volcano.ogg', ('bars', 38)),
    ('BGM_ZAKO_LATE', '14_zako_late.ogg', ('bars', 38)),      # plays in 魔王の城 / 魔王城の地下 (web BGM_SWAP)
    ('BGM_ZAKO_CASTLE', '15_zako_castle.ogg', ('bars', 38)),  # plays in 氷の洞窟..灼熱の洞窟 (web BGM_SWAP)
    ('BGM_B_MARGOS', '17_boss_margos.ogg', ('bars', 38)),
    ('BGM_B_OSHALSIAS', '18_boss_oshalsias.ogg', ('bars', 38)),
    ('BGM_B_PICHARUS', '19_boss_picharus.ogg', ('bars', 38)),
    ('BGM_B_MIMIKKUN', '20_boss_mimikkun.ogg', ('loop',)),    # the composer's loop is short: kept whole
    ('BGM_B_NAZOKIMOS', '21_boss_nazokimos.ogg', ('loop',)),
    ('BGM_B_WASHARAS', '22_boss_washaras.ogg', ('bars', 38)),
    ('BGM_B_KIRANOS', '23_boss_kiranos.ogg', ('bars', 38)),
    ('BGM_B_YAMIJAS', '24_boss_yamijas.ogg', ('bars', 38)),
    ('BGM_B_DARKNESS', '25_boss_darkness.ogg', ('bars', 38)),
    ('BGM_B_DOTARK', '26_boss_dotark.ogg', ('bars', 38)),
    ('BGM_ENDING', '27_ending.ogg', ('bars', 38)),
    ('BGM_CASTLE_WAVES', '49_castle_last_waves.ogg', ('bars', 38)),
]

# SE id (sound.h order), source, max seconds, volume (0-127)
SE = [
    ('SE_CURSOR', 'beat_tap', 0.12, 48),
    ('SE_OK', 'beat_tap', 0.12, 80),
    ('SE_CANCEL', 'beat_miss', 0.2, 64),
    ('SE_SLASH', 'slash2', 0.45, 110),
    ('SE_SPEAR', 'spear1', 0.45, 110),
    ('SE_HAMMER', 'hammer1', 0.5, 110),
    ('SE_KICK', 'kick1', 0.4, 110),
    ('SE_SHOUT', 'shout', 0.5, 110),
    ('SE_DAMAGE', 'damage1', 0.45, 110),
    ('SE_GUARD', 'guard4', 0.5, 110),
    ('SE_JUST', 'just1', 0.6, 120),
    ('SE_MIRACLE', 'miracle', 0.85, 120),
    ('SE_EAT', 'eat', 0.45, 100),
    ('SE_CHARGE', 'charge', 0.6, 100),
    ('SE_STUN', 'stun', 0.5, 100),
    ('SE_LUCKY', 'lucky', 0.5, 100),
    ('SE_WARNING', 'warning', 0.8, 110),
    ('SE_BREATH', 'breath', 0.8, 110),
    ('SE_TRANSFORM', 'transform', 0.7, 110),
    ('SE_COIN', 'lucky', 0.3, 80),
    ('SE_DROP', 'beat_tap', 0.12, 100),
    ('SE_DEFEAT', 'kick2', 0.4, 90),
    ('SE_SPECIAL', 'just3', 0.6, 120),
]


def ffmpeg_pcm(data, rate, in_fmt=None, in_rate=None):
    cmd = ['ffmpeg', '-v', 'error']
    if in_fmt:
        cmd += ['-f', in_fmt, '-ar', str(in_rate), '-ac', '1']
    cmd += ['-i', 'pipe:0', '-ac', '1', '-ar', str(rate), '-f', 's16le', 'pipe:1']
    r = subprocess.run(cmd, input=data, capture_output=True, check=True)
    return np.frombuffer(r.stdout, dtype='<i2').astype(np.float64) / 32768.0


def brr(pcm, end=False, f0=()):
    raw = os.path.join(OUT, '_tmp.raw')
    out = os.path.join(OUT, '_tmp.brr')
    n = (len(pcm) + 15) // 16 * 16
    x = np.zeros(n)
    x[:len(pcm)] = pcm
    open(raw, 'wb').write(np.clip(np.round(x * 32767), -32768, 32767).astype('<i2').tobytes())
    cmd = [os.path.join(HERE, 'audio', 'brr'), raw, out] + (['-end'] if end else [])
    if f0:
        cmd += ['-f0'] + [str(b) for b in f0]
    r = subprocess.run(cmd, capture_output=True, text=True, check=True)
    d = open(out, 'rb').read()
    os.remove(raw)
    os.remove(out)
    return d, r.stderr.strip()


def spectra(a, nfft=2048, hop=512):
    n = (len(a) - nfft) // hop
    win = np.hanning(nfft)
    frames = np.stack([a[i * hop:i * hop + nfft] * win for i in range(n)])
    s = np.log1p(np.abs(np.fft.rfft(frames, axis=1))[:, 2:400])
    s -= s.mean(axis=1, keepdims=True)
    s /= np.linalg.norm(s, axis=1, keepdims=True) + 1e-9
    return s, hop


def short_loop(a, start, seconds):
    """loop [start, end): end near start + seconds where the music matches the loop start"""
    s, hop = spectra(a)
    fs = start // hop
    want = int(seconds * RATE)
    lo, hi = (start + int(want * 0.75)) // hop, min((start + want) // hop, len(s) - 8)
    ctx = 6
    ref = s[fs - ctx:fs + ctx]
    best, be = None, -1e9
    for e in range(max(lo, ctx), hi):
        sc = float(np.sum(ref * s[e - ctx:e + ctx]))
        if sc > be:
            be, best = sc, e
    end = best * hop
    # refine on the waveform
    w = 1024
    r0 = a[start - w:start + w]
    bo, bc = end, -1e9
    for d in range(-hop, hop + 1, 4):
        seg = a[end + d - w:end + d + w]
        if len(seg) != len(r0):
            continue
        c = float(np.dot(seg, r0))
        if c > bc:
            bc, bo = c, end + d
    end = bo
    end = start + (end - start) // 16 * 16
    return end, be / (2 * ctx)


OH = 128                     # onset envelope hop (11.6 ms)


def onset(a):
    """onset envelope: positive spectral flux (STFT 1024, hop OH, log1p(10|X|)) minus its 32-frame
    moving mean, clipped at 0"""
    n = 1024
    m = (len(a) - n) // OH
    win = np.hanning(n)
    fr = np.stack([a[i * OH:i * OH + n] * win for i in range(m)])
    mag = np.log1p(10 * np.abs(np.fft.rfft(fr, axis=1)))
    f = np.maximum(0, np.diff(mag, axis=0)).sum(axis=1)
    f = np.concatenate([[0], f])
    f -= np.convolve(f, np.ones(32) / 32, 'same')
    return np.maximum(f, 0)


def chroma(a, hop=512, n=4096):
    """12 pitch classes per frame (STFT n, 55-2500 Hz), log, mean removed, unit norm"""
    m = (len(a) - n) // hop
    win = np.hanning(n)
    freqs = np.fft.rfftfreq(n, 1 / RATE)
    ok = (freqs > 55) & (freqs < 2500)
    pc = np.round(12 * np.log2(freqs[ok] / 440.0)).astype(int) % 12
    M = np.zeros((ok.sum(), 12))
    M[np.arange(ok.sum()), pc] = 1
    fr = np.stack([a[i * hop:i * hop + n] * win for i in range(m)])
    out = np.log1p(np.abs(np.fft.rfft(fr, axis=1))[:, ok] @ M)
    out -= out.mean(axis=1, keepdims=True)
    out /= np.linalg.norm(out, axis=1, keepdims=True) + 1e-9
    return out, hop


def beat_of(o, body):
    """the beat (s): bar = body / nb is 4 beats of 70..200 BPM (the Safari loop body is whole bars);
    the nb whose beat lag has the strongest onset autocorrelation (+ half of 2 and 4 beats)"""
    fps = RATE / OH
    f = o - o.mean()
    ac = np.correlate(f, f, 'full')[len(f) - 1:]
    best, bv = None, -1e18
    for nb in range(1, 400):
        beat = body / nb / 4
        if not (60 / 200 <= beat <= 60 / 70):
            continue
        lag = beat * fps
        k = int(round(lag))
        if k + 1 >= len(ac):
            continue
        v = ac[k] + 0.5 * ac[int(round(2 * lag))] + 0.5 * ac[int(round(4 * lag))]
        if v > bv:
            bv, best = v, beat
    return best


def sim(S, hop, fs, fe, b0, b1):
    """mean frame similarity of S around frame fs and around frame fe, from -b0 s to +b1 s"""
    k0, k1 = -int(b0 * RATE / hop), int(b1 * RATE / hop)
    k0 = max(k0, -fs)
    k1 = min(k1, len(S) - fe)
    return float(np.mean(np.sum(S[fs + k0:fs + k1] * S[fe + k0:fe + k1], axis=1)))


def rhythm(o, start, end):
    """join check: the onset pattern for 2 s after the loop start vs the one after the loop end (what
    the original plays next): (best lag in +-150 ms in ms, correlation at lag 0); lag 0 = the beat goes on"""
    fps = RATE / OH
    n2 = int(2.0 * fps)
    s0, e0 = start // OH, end // OH
    x = o[s0:s0 + n2]
    best_c, best_lag, c0 = -2, 0, 0
    for lag in range(-13, 14):
        y = o[e0 + lag:e0 + lag + n2]
        if len(y) < len(x):
            continue
        c = float(np.corrcoef(x, y)[0, 1])
        if lag == 0:
            c0 = c
        if c > best_c:
            best_c, best_lag = c, lag
    return best_lag * 1000 / fps, c0


def bar_loop(a, start, seconds, o, ch, chop, sp, sphop):
    """loop [start, end) of whole bars, end <= start + seconds: unit = one bar (4 beats, beat_of the
    Safari loop body); for each bar count k (from seconds / bar down to half of it) the end near
    start + k bars (+-0.15 s) where the onset envelope after it matches the one after the loop start
    best (r); score = chroma match + 0.5 spectral match [-1 s, +2 s] + 0.5 r; the longest k within
    0.08 of the best score; then the end refined on the waveform (+-64 samples) and cut to 16 samples.
    Returns (end, bars, bar seconds, score)."""
    body = (len(a) // 16 * 16 - start) / RATE
    unit = 4 * beat_of(o, body)
    fps = RATE / OH
    n2 = int(2.0 * fps)
    s0 = start // OH
    x = o[s0:s0 + n2] - o[s0:s0 + n2].mean()
    fs_c, fs_s = start // chop, start // sphop
    kmax = int(math.floor(seconds / unit + 1e-6))
    kmin = max(1, int(math.ceil(0.5 * seconds / unit)))
    cands = []
    for k in range(kmax, kmin - 1, -1):
        e0 = (start + int(round(k * unit * RATE))) // OH
        best = None
        for d in range(-int(0.15 * fps), int(0.15 * fps) + 1):
            y = o[e0 + d:e0 + d + n2]
            if len(y) < n2:
                continue
            y = y - y.mean()
            c = float(np.dot(x, y) / (np.linalg.norm(x) * np.linalg.norm(y) + 1e-9))
            if best is None or c > best[0]:
                best = (c, (e0 + d) * OH)
        if best is None:
            continue
        r, end = best
        m = sim(ch, chop, fs_c, end // chop, 1.0, 2.0) + 0.5 * sim(sp, sphop, fs_s, end // sphop, 1.0, 2.0) + 0.5 * r
        cands.append((m, k, end, r))
    if not cands:
        raise SystemExit('bar_loop: no loop end in %.1f s (bar %.3f s)' % (seconds, unit))
    top = max(c[0] for c in cands)
    m, k, end, r = max((c for c in cands if c[0] >= top - 0.08), key=lambda c: c[1])
    w = 1024
    r0 = a[start - w:start + w]
    bo, bc = end, -1e9
    for d in range(-64, 65, 2):
        seg = a[end + d - w:end + d + w]
        if len(seg) != len(r0):
            continue
        c = float(np.dot(seg, r0))
        if c > bc:
            bc, bo = c, end + d
    end = start + (bo - start) // 16 * 16
    return end, k, unit, m


def crossfade(seg, a, start, end, xf):
    """fade the last xf samples of a[start:end] into the samples just before start"""
    out = seg.copy()
    xf = min(xf, start, len(out))
    if xf <= 0:
        return out
    t = np.linspace(0, 1, xf)
    out[-xf:] = a[end - xf:end] * (1 - t) + a[start - xf:start] * t
    return out


def source_pcm(wb, src):
    """a BGM file of the web build as mono RATE Hz, normalised to 0.85 (what the loops are cut from)"""
    path = 'res://assets/bgm/' + src
    data = wb.ogg(path) if src.endswith('.ogg') else wb.mp3(path)
    a = ffmpeg_pcm(data, RATE)
    a *= 0.85 / max(1e-6, np.max(np.abs(a)))
    return a


def shape_tag(shape):
    """short name of a shape (bgm_loops.json, tools/harness/bgmjoin.py file names): full, short34, bar38, loop, once"""
    return {'short': 'short', 'bars': 'bar'}.get(shape[0], shape[0]) + ('%d' % shape[1] if shape[0] in ('short', 'bars') else '')


def build_bgm(wb):
    stream = bytearray()
    tracks = []
    report = []
    loops = []
    for name, src, shape in BGM:
        prm = wb.import_params('res://assets/bgm/' + src)
        loop_off = float(prm.get('loop_offset', '0'))
        a = source_pcm(wb, src)
        lp = {'name': name, 'src': src, 'shape': shape_tag(shape), 'safari_loop': loop_off, 'src_len': len(a)}
        if shape[0] == 'full':
            start = int(round(loop_off * RATE)) // 16 * 16
            end = len(a) // 16 * 16
            pcm = crossfade(a[:end], a, start, end, int(0.04 * RATE))
            loop_blk = start // 16
            note = 'intro %.1fs + loop %.1fs (Safari loop)' % (start / RATE, (end - start) / RATE)
            lp.update(begin=0, start=start, end=end, tail=int(0.04 * RATE))
        elif shape[0] == 'short':
            start = int(round(loop_off * RATE)) // 16 * 16
            end, score = short_loop(a, start, shape[1])
            pcm = crossfade(a[start:end], a, start, end, int(0.12 * RATE))
            loop_blk = 0
            note = 'loop %.1fs from %.1fs (match %.2f)' % ((end - start) / RATE, start / RATE, score)
            lp.update(begin=start, start=start, end=end, tail=int(0.12 * RATE))
        elif shape[0] == 'bars':
            start = int(round(loop_off * RATE)) // 16 * 16
            o = onset(a)
            ch, chop = chroma(a)
            sp, sphop = spectra(a)
            end, k, unit, score = bar_loop(a, start, shape[1], o, ch, chop, sp, sphop)
            pcm = crossfade(a[start:end], a, start, end, int(0.12 * RATE))
            loop_blk = 0
            lag, r = rhythm(o, start, end)
            cm = sim(ch, chop, start // chop, end // chop, 1.0, 2.0)
            note = 'loop %.2fs from %.2fs = %d bars of %.3fs (score %.2f; join: lag %+.0f ms, r %.2f, chroma %.2f)' % (
                (end - start) / RATE, start / RATE, k, unit, score, lag, r, cm)
            lp.update(begin=start, start=start, end=end, tail=int(0.12 * RATE), bars=k, bar_s=round(unit, 4),
                      lag_ms=round(lag, 1), r=round(r, 3), chroma=round(cm, 3))
        elif shape[0] == 'loop':
            start = int(round(loop_off * RATE)) // 16 * 16
            end = len(a) // 16 * 16
            pcm = crossfade(a[start:end], a, start, end, int(0.04 * RATE))
            loop_blk = 0
            note = 'loop %.2fs from %.2fs (the Safari loop)' % ((end - start) / RATE, start / RATE)
            lp.update(begin=start, start=start, end=end, tail=int(0.04 * RATE))
        else:
            n = int(shape[1] * RATE) // 16 * 16
            pcm = a[:n].copy()
            f = int(1.5 * RATE)
            pcm[-f:] *= np.linspace(1, 0, f)
            loop_blk = None
            note = 'once %.1fs' % (n / RATE)
            lp.update(begin=0, start=None, end=n, tail=f)
        d, info = brr(pcm, f0=[0] + ([loop_blk] if loop_blk else []))
        first = len(stream) // 9
        stream += d
        nblk = len(d) // 9
        tracks.append((name, first, nblk, loop_blk))
        lp.update(first=first, blocks=nblk, loop=loop_blk)
        loops.append(lp)
        report.append('%-16s %s, %d blocks, %s' % (name, note, nblk, info))
    return bytes(stream), tracks, report, loops


def build_se(wb, scale=1.0):
    lst = []
    cache = {}
    report = []
    for name, src, sec, vol in SE:
        key = (src, sec)
        if key not in cache:
            path = [f for f in wb.files if f.endswith('/' + src + '.wav.import')][0].replace('.import', '')
            rate, ch, chans = qoa.decode(qoa.godot_sample(wb.read(wb.imported('res://' + path))))
            x = np.mean(np.array(chans, dtype=np.float64), axis=0) / 32768.0
            nz = np.nonzero(np.abs(x) > 0.01)[0]
            x = x[nz[0] if len(nz) else 0:]
            x = x[:int(sec * scale * rate)]
            f = min(len(x), int(0.03 * rate))
            x[-f:] *= np.linspace(1, 0, f)
            y = ffmpeg_pcm(np.clip(np.round(x * 32767), -32768, 32767).astype('<i2').tobytes(), SE_RATE, 's16le', rate)
            y *= 0.9 / max(1e-6, np.max(np.abs(y)))
            d, info = brr(y, end=True, f0=[0])
            cache[key] = len(lst)
            lst.append((d, SE_PITCH))
            report.append('%-12s %-10s %.2fs %5d bytes' % (name, src, len(y) / SE_RATE, len(d)))
        else:
            report.append('%-12s (same sample as above)' % name)
    se_dir = []
    for name, src, sec, vol in SE:
        se_dir.append((cache[(src, sec)] + 1, vol))
    return lst, se_dir, report


def main():
    os.makedirs(OUT, exist_ok=True)
    brr_tool = os.path.join(HERE, 'audio', 'brr')
    if not os.path.exists(brr_tool):
        subprocess.run(['gcc', '-O2', '-o', brr_tool, os.path.join(HERE, 'audio', 'brr.c'), '-lm'], check=True)
    wb = webbuild.get()
    scale = 1.0
    while True:
        se_list, se_dir, se_rep = build_se(wb, scale)
        size = sum(len(d) for d, p in se_list)
        room = driver.SE_END - driver.SE_START
        if size <= room:
            break
        scale *= room / size * 0.97
    img, top = driver.image(se_list)
    se_rep.append('SE length scale %.2f' % scale)
    stream, tracks, rep, loops = build_bgm(wb)
    stream = bytearray(stream)
    for i in range(0, len(stream), 9):
        stream[i] &= 0xFC                      # no END/LOOP: the driver flags the last ring block
    stream = bytes(stream)
    open(os.path.join(OUT, 'bgm.brr'), 'wb').write(stream)
    open(os.path.join(OUT, 'spc.bin'), 'wb').write(img)
    with open(os.path.join(OUT, 'bgm_loops.json'), 'w', encoding='utf-8') as f:
        f.write('{"rate": %d, "note": "source samples at rate (after ffmpeg and the 0.85 normalisation); the stream '
                'plays [begin, end) once, then [start, end) again and again (start null: once); its last tail samples are '
                'the crossfade into the loop start (or the fade-out)", "tracks": [\n' % RATE)
        f.write(',\n'.join(json.dumps(lp, ensure_ascii=False) for lp in loops) + ']}\n')
    sys.path.insert(0, HERE)
    import romplace
    romplace.write_manifest(GEN, 'bgm', [{
        'label': 'bgm', 'file': 'audio/bgm.brr', 'size': len(stream), 'kind': 'stream', 'unit': 9, 'min': 64,
        'speed': 'slow', 'hook': 'bgm',
        'tracks': [{'name': name, 'first': first, 'blocks': n, 'loop': loop} for name, first, n, loop in tracks]}])
    s = ['; generated by tools/make_audio.py (BGM stream: data/gen/place/bgm.json, tables by tools/romplace.py)',
         '.export spc_image, se_dir, se_vol',
         '.export SPC_IMAGE_LEN_LO, SPC_IMAGE_LEN_HI, SPC_BOOT_LEN, SPC_SPLIT_END, se_thr']
    # SPC image in two banks
    s.append('.segment "BANK%02X"' % SPC_BANKS[0])
    boot = driver.SE_START - driver.DRIVER
    assert (len(img) - boot) % 9 == 0
    split = boot + (0x8000 - boot) // 9 * 9     # load blocks never cross the bank boundary
    s.append('spc_image:')
    s.append('  .incbin "audio/spc.bin", 0, %d' % min(len(img), split))
    if len(img) > split:
        s.append('.segment "BANK%02X"' % SPC_BANKS[1])
        s.append('  .incbin "audio/spc.bin", %d, %d' % (split, len(img) - split))
    s.append('SPC_BOOT_LEN = %d' % boot)
    s.append('SPC_SPLIT_END = $%04X   ; end of the first bank of the image (address)' % (0x8000 + split))
    s.append('SPC_IMAGE_LEN_LO = %d' % (len(img) & 0xffff))
    s.append('SPC_IMAGE_LEN_HI = %d' % (len(img) >> 16))
    s.append('.segment "BANK%02X"' % SPC_BANKS[1])
    h = ['/* generated by tools/make_audio.py */', '#ifndef AUDIO_H', '#define AUDIO_H',
         '#define BGM_PITCH %d' % PITCH, '#define N_BGM_TRACKS %d' % len(tracks)]
    for k, (name, first, n, loop) in enumerate(tracks):
        h.append('#define TRACK_%s %d' % (name[4:], k))
    s.append('se_dir:')
    s.append('  .byte 0, ' + ', '.join(str(d) for d, v in se_dir))
    s.append('se_vol:')
    s.append('  .byte 0, ' + ', '.join(str(v) for d, v in se_dir))
    # a sound effect can play once its sample is in ARAM: bytes still to load <= threshold
    total = len(img) - boot
    ends, at = [], 0
    for d, p in se_list:
        at += len(d)
        ends.append(at)
    s.append('se_thr:')
    s.append('  .word 0, ' + ', '.join(str(total - (ends[d - 1] + 8) // 9 * 9) for d, v in se_dir))
    h.append('#endif')
    open(os.path.join(GEN, 'audio.s'), 'w').write('\n'.join(s) + '\n')
    open(os.path.join(GEN, 'audio.h'), 'w').write('\n'.join(h) + '\n')
    print('\n'.join(se_rep))
    print('SPC image %d bytes (samples end at $%04X)' % (len(img), top))
    print('\n'.join(rep))
    print('BGM %d KB (%.0f s)' % (len(stream) // 1024, len(stream) / 9 * 16 / RATE))


if __name__ == '__main__':
    main()
