#!/usr/bin/env python3
"""Which BGM track an emulator recording plays: a runrom 'wav' recording against the stream the ROM plays
(data/gen/audio/bgm.brr cut into tracks by data/gen/place/bgm.json, decoded with tools/audio/brrdec.py),
with its loop.  (Port of the bgm plan's wavref.py / wavloop.py.)

usage: wavref.py REC.wav TRACK [T0 [T1]] [--min C]   TRACK in the recording's [T0, T1] s (default 1 s .. end)
       wavref.py REC.wav --which [T0 [T1]]          the best-matching track of all, then its windows
  TRACK   a BGM_ name of audio.h (BGM_ZAKO_LATE), a file stem (14_zako_late) or a song id (15 = BGM_ZAKO_LATE)
  --gen   another data/gen

snes9x records one sample per DSP sample (32 kHz; runrom's header says 32040); the driver plays the stream at
BGM_PITCH, i.e. 1409/4096 stream samples per DSP sample.  The track's position is found by normalised
cross-correlation of a short piece of the recording (resampled to the stream rate) with the whole track (one
pass of intro + loop and one more loop), then each 50 ms window is compared with the stream there, with a small
lag search that follows the recording.  A correct stream gives a median correlation >= 0.99; sound effects
lower single windows (a window is 'low' below 0.9 when the reference is not near silence).
Exit status 0 when the median >= --min (default 0.99) (--which: and the best track was found)."""
import argparse
import json
import os
import re
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'audio'))
import brrdec  # noqa: E402

WIN = 0.05
SEARCH = 0.25          # seconds of recording used to find the position


class Stream:
    """the BGM stream of a data/gen: tracks (name, first block, blocks, loop block or None), decoded on demand"""
    def __init__(self, gen=None):
        self.gen = gen or os.path.join(ROOT, 'data', 'gen')
        ent = json.load(open(os.path.join(self.gen, 'place', 'bgm.json'), encoding='utf-8'))['entries'][0]
        self.tracks = ent['tracks']
        self.data = open(os.path.join(self.gen, 'audio', 'bgm.brr'), 'rb').read()
        h = open(os.path.join(self.gen, 'audio.h')).read()
        self.pitch = int(re.search(r'#define BGM_PITCH (\d+)', h).group(1))
        self.src = {}
        p = os.path.join(self.gen, 'audio', 'bgm_loops.json')
        if os.path.exists(p):
            for t in json.load(open(p, encoding='utf-8'))['tracks']:
                self.src[t['name']] = os.path.splitext(t['src'])[0]
        self._pcm, self._ext, self._cache = {}, {}, {}

    def find(self, key):
        """index of a track by BGM_ name, file stem or song id (1-based, as bgm_play)"""
        key = str(key)
        for i, t in enumerate(self.tracks):
            if key in (t['name'], self.src.get(t['name'])):
                return i
        if key.isdigit() and 1 <= int(key) <= len(self.tracks):
            return int(key) - 1
        raise SystemExit('no track %s' % key)

    def label(self, i):
        t = self.tracks[i]
        return '%s (%s)' % (t['name'], self.src.get(t['name'], '?'))

    def pcm(self, i):
        if i not in self._pcm:
            t = self.tracks[i]
            self._pcm[i] = brrdec.decode(self.data[t['first'] * 9:(t['first'] + t['blocks']) * 9]).astype(np.float64)
        return self._pcm[i]

    def looped(self, i):
        """(the track as played once, its loop start in samples or None)"""
        t = self.tracks[i]
        return self.pcm(i), (None if t['loop'] is None else t['loop'] * 16)

    def ext(self, i):
        """the track once and its loop once more (a position search finds any point of the loop)"""
        if i not in self._ext:
            one, lp = self.looped(i)
            self._ext[i] = np.concatenate([one, one[lp:]]) if lp is not None else np.concatenate([one, np.zeros(4096)])
        return self._ext[i]

    def cache(self, i):
        return self._cache.setdefault(i, {})


def load_wav(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())[:, 0].astype(np.float64)
    return x, 32000          # one sample per DSP sample


def ncc_peak(ref, seg, cache=None):
    """best normalised cross-correlation of seg against ref: (position in ref, fractional; corr).  cache: a dict
    kept for one ref and one segment length (its FFT and running sums)"""
    n = len(seg)
    if len(ref) < n + 1:
        return 0, -1.0
    seg = seg - seg.mean()
    size = 1 << int(np.ceil(np.log2(len(ref) + n)))
    if cache is not None and cache.get('key') == (len(ref), n):
        fr, s1, var = cache['fr'], cache['s1'], cache['var']
    else:
        fr = np.fft.rfft(ref, size)
        c1 = np.concatenate([[0.0], np.cumsum(ref)])
        c2 = np.concatenate([[0.0], np.cumsum(ref * ref)])
        s1 = c1[n:] - c1[:-n]
        var = np.maximum((c2[n:] - c2[:-n]) - s1 * s1 / n, 1e-9)
        if cache is not None:
            cache.update(key=(len(ref), n), fr=fr, s1=s1, var=var)
    cc = np.fft.irfft(fr * np.conj(np.fft.rfft(seg, size)), size)[:len(ref) - n + 1]
    r = cc / np.sqrt(var * np.dot(seg, seg) + 1e-9)
    k = int(np.argmax(r))
    f = 0.0
    if 0 < k < len(r) - 1:                          # the peak between two samples (parabola)
        d = r[k - 1] - 2 * r[k] + r[k + 1]
        f = 0.5 * (r[k - 1] - r[k + 1]) / d if d < 0 else 0.0
    return k + f, float(r[k])


def search_piece(st, x, rate, t0):
    """the recording's SEARCH s from t0, at the stream rate"""
    step = st.pitch / 4096.0
    seg = x[int(t0 * rate):int(t0 * rate) + int(SEARCH * rate)]
    return np.interp(np.arange(int(len(seg) * step)) / step, np.arange(len(seg)), seg)


def match(st, i, x, rate, t0, t1):
    """recording x (DSP rate) against track i in [t0, t1] s -> dict(median, low, windows, at = track seconds at
    t0 (in the first pass: intro + loop), pos = the stream position (samples) at the recording's first sample,
    peak = the position search's corr, lows = [(t, corr)])"""
    one, lp = st.looped(i)
    step = st.pitch / 4096.0                        # stream samples per recorded sample
    a0 = int(t0 * rate)
    pos, peak = ncc_peak(st.ext(i), search_piece(st, x, rate, t0), st.cache(i))
    if lp is not None and pos >= len(one):
        pos -= len(one) - lp                        # found in the second pass of the loop

    def at(p):                                      # a stream position (float) -> index in the track, with the loop
        if lp is None:
            return np.where(p < len(one), p, len(one) - 1)
        body = len(one) - lp
        return np.where(p < len(one), p, lp + np.mod(p - lp, body))
    cs, lows, lag = [], [], 0
    w = int(WIN * rate)
    for a in np.arange(t0, t1 - WIN, WIN):
        s0 = int(a * rate)
        if s0 + w + 64 > len(x):
            break
        p = pos + (s0 - a0) * step + np.arange(w) * step
        y = np.interp(at(p), np.arange(len(one)), one)
        best, bl = -2.0, lag
        for d in range(lag - 40, lag + 41):
            if s0 + d < 0:
                continue
            z = x[s0 + d:s0 + d + w]
            c = float(np.dot(z, y) / np.sqrt(np.dot(z, z) * np.dot(y, y) + 1e-9))
            if c > best:
                best, bl = c, d
        lag = bl
        cs.append(best)
        if best < 0.9 and np.sqrt((y ** 2).mean()) > 300:
            lows.append((round(float(a), 2), round(best, 3)))
    return {'median': float(np.median(cs)) if cs else 0.0, 'windows': len(cs), 'low': len(lows), 'lows': lows,
            'at': pos / (st.pitch / 4096.0 * 32000), 'pos': pos - a0 * step, 'peak': peak}


def which(st, x, rate, t0, only=None):
    """the tracks (all, or the indexes in only) whose stream best matches the recording at t0: [(corr, index)],
    best first"""
    segs = search_piece(st, x, rate, t0)
    out = [(ncc_peak(st.ext(i), segs, st.cache(i))[1], i) for i in (only if only is not None else range(len(st.tracks)))]
    return sorted(out, reverse=True)


def report(st, i, m):
    s = '%s: median corr %.3f over %d windows, %d low, at %.2f s of the track (search %.2f)' % (
        st.label(i), m['median'], m['windows'], m['low'], m['at'], m['peak'])
    if m['lows']:
        s += '; low: ' + ' '.join('%.2f:%.2f' % lw for lw in m['lows'][:12]) + (' ...' if len(m['lows']) > 12 else '')
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rec')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--which', action='store_true')
    ap.add_argument('--min', type=float, default=0.99)
    ap.add_argument('--gen')
    a = ap.parse_args()
    st = Stream(a.gen)
    x, rate = load_wav(a.rec)
    args = list(a.args)
    if not a.which:
        if not args:
            sys.exit('TRACK missing')
        i = st.find(args.pop(0))
    t0 = float(args[0]) if args else 1.0
    t1 = float(args[1]) if len(args) > 1 else len(x) / rate
    if a.which:
        best = which(st, x, rate, t0)
        print('best: ' + ', '.join('%s %.3f' % (st.tracks[k]['name'], c) for c, k in best[:4]))
        i = best[0][1]
    m = match(st, i, x, rate, t0, t1)
    print(report(st, i, m))
    sys.exit(0 if m['median'] >= a.min else 1)


if __name__ == '__main__':
    main()
