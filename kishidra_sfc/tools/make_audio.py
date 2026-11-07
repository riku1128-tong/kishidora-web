#!/usr/bin/env python3
"""BGM and sound effects for the SFC build (needs ffmpeg and tools/audio/brr).

BGM: mono 11008 Hz BRR streamed from ROM (see tools/audio/driver.py).  Loop points come from
the Safari build's import settings; long tracks are shortened to a loop found by comparing
the spectrum at the loop start with candidate ends (plus a short crossfade).
SE: one variation per sound, trimmed, 10240 Hz BRR inside the SPC image.

Outputs data/gen/audio.s, data/gen/audio.h and data/gen/audio/*.bin."""
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
BLOCKS_PER_BANK = 3640                      # 32760 bytes
# banks the stream may use, in order (code banks $90-$9F hold no code)
# (labels must not live in $81-$9F: those memory areas carry a bank attribute, so ^label is 0)
BGM_BANKS = list(range(0xB0, 0xC0)) + list(range(0xE0, 0xFE)) + list(range(0x90, 0xA0))
SPC_BANKS = [0xFE, 0xFF]

# id, file, shape: ('full',) intro + loop as in Safari / ('short', seconds) loop only / ('once', seconds)
BGM = [
    ('BGM_OPENING', '01_opening.ogg', ('full',)),
    ('BGM_ZAKO', '02_zako_early.ogg', ('short', 34)),
    ('BGM_BOSS', '08_boss_field.ogg', ('short', 34)),
    ('BGM_MAOU', '13_boss_last.ogg', ('short', 34)),
    ('BGM_MAOU_FINAL', '50_boss_last_final.ogg', ('short', 34)),
    ('BGM_PEKO', '16_boss_secret.ogg', ('full',)),     # 裏ボス: the whole track, its own long loop
    ('BGM_VICTORY', '05_victory.mp3', ('once', 12)),
    ('BGM_GAMEOVER', '06_gameover.mp3', ('once', 9)),
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


def crossfade(seg, a, start, end, xf):
    """fade the last xf samples of a[start:end] into the samples just before start"""
    out = seg.copy()
    xf = min(xf, start, len(out))
    if xf <= 0:
        return out
    t = np.linspace(0, 1, xf)
    out[-xf:] = a[end - xf:end] * (1 - t) + a[start - xf:start] * t
    return out


def build_bgm(wb):
    stream = bytearray()
    tracks = []
    report = []
    for name, src, shape in BGM:
        path = 'res://assets/bgm/' + src
        prm = wb.import_params(path)
        loop_off = float(prm.get('loop_offset', '0'))
        data = wb.ogg(path) if src.endswith('.ogg') else wb.mp3(path)
        a = ffmpeg_pcm(data, RATE)
        a *= 0.85 / max(1e-6, np.max(np.abs(a)))
        if shape[0] == 'full':
            start = int(round(loop_off * RATE)) // 16 * 16
            end = len(a) // 16 * 16
            pcm = crossfade(a[:end], a, start, end, int(0.04 * RATE))
            loop_blk = start // 16
            note = 'intro %.1fs + loop %.1fs (Safari loop)' % (start / RATE, (end - start) / RATE)
        elif shape[0] == 'short':
            start = int(round(loop_off * RATE)) // 16 * 16
            end, score = short_loop(a, start, shape[1])
            pcm = crossfade(a[start:end], a, start, end, int(0.12 * RATE))
            loop_blk = 0
            note = 'loop %.1fs from %.1fs (match %.2f)' % ((end - start) / RATE, start / RATE, score)
        else:
            n = int(shape[1] * RATE) // 16 * 16
            pcm = a[:n].copy()
            f = int(1.5 * RATE)
            pcm[-f:] *= np.linspace(1, 0, f)
            loop_blk = None
            note = 'once %.1fs' % (n / RATE)
        d, info = brr(pcm, f0=[0] + ([loop_blk] if loop_blk else []))
        first = len(stream) // 9
        stream += d
        nblk = len(d) // 9
        tracks.append((name, first, nblk, loop_blk))
        report.append('%-16s %s, %d blocks, %s' % (name, note, nblk, info))
    return bytes(stream), tracks, report


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
    stream, tracks, rep = build_bgm(wb)
    stream = bytearray(stream)
    for i in range(0, len(stream), 9):
        stream[i] &= 0xFC                      # no END/LOOP: the driver flags the last ring block
    stream = bytes(stream)
    nbanks = (len(stream) // 9 + BLOCKS_PER_BANK - 1) // BLOCKS_PER_BANK
    if nbanks > len(BGM_BANKS):
        raise SystemExit('BGM needs %d banks, only %d free' % (nbanks, len(BGM_BANKS)))
    open(os.path.join(OUT, 'bgm.brr'), 'wb').write(stream)
    open(os.path.join(OUT, 'spc.bin'), 'wb').write(img)
    s = ['; generated by tools/make_audio.py', '.export bgm_table, bgm_banks, spc_image, se_dir, se_vol',
         '.export SPC_IMAGE_LEN_LO, SPC_IMAGE_LEN_HI, SPC_BOOT_LEN, SPC_SPLIT_END, se_thr']
    # stream data, BLOCKS_PER_BANK blocks per bank
    for i in range(nbanks):
        off = i * BLOCKS_PER_BANK * 9
        ln = min(BLOCKS_PER_BANK * 9, len(stream) - off)
        s.append('.segment "BANK%02X"' % BGM_BANKS[i])
        s.append('  .incbin "audio/bgm.brr", %d, %d' % (off, ln))
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
    s.append('bgm_banks:')
    s.append('  .byte ' + ', '.join('$%02x' % b for b in BGM_BANKS[:nbanks]))

    def at(g):
        return g // BLOCKS_PER_BANK, 0x8000 + (g % BLOCKS_PER_BANK) * 9, BLOCKS_PER_BANK - g % BLOCKS_PER_BANK

    s.append('bgm_table:   ; per track: bank index, ptr, left in bank, blocks (3) ; same for the loop (blocks 0 = once)')
    h = ['/* generated by tools/make_audio.py */', '#ifndef AUDIO_H', '#define AUDIO_H',
         '#define BGM_PITCH %d' % PITCH, '#define N_BGM_TRACKS %d' % len(tracks)]
    for k, (name, first, n, loop) in enumerate(tracks):
        bi, p, left = at(first)
        row = [bi, p & 0xff, p >> 8, left & 0xff, left >> 8, n & 0xff, (n >> 8) & 0xff, n >> 16]
        if loop is not None:
            g = first + loop
            lbi, lp, lleft = at(g)
            ln = n - loop
            row += [lbi, lp & 0xff, lp >> 8, lleft & 0xff, lleft >> 8, ln & 0xff, (ln >> 8) & 0xff, ln >> 16]
        else:
            row += [0] * 8
        s.append('  .byte ' + ', '.join('$%02x' % v for v in row) + '   ; %s' % name)
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
    print('BGM %d KB in %d banks (%.0f s)' % (len(stream) // 1024, nbanks, len(stream) / 9 * 16 / RATE))


if __name__ == '__main__':
    main()
