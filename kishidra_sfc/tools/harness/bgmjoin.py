#!/usr/bin/env python3
"""BGM loop joins: a listening snippet of every track's loop join, made from the stream the ROM plays
(data/gen/audio/bgm.brr cut by data/gen/place/bgm.json, decoded with tools/audio/brrdec.py), and checks
of each join against the source it was cut from (data/gen/audio/bgm_loops.json, written by
tools/make_audio.py).

usage: bgmjoin.py [-o DIR] [--sec S] [--check] [--max-lag MS] [--gen DIR] [TRACK ...]

  -o DIR       write DIR/<file>_<shape>.wav (11008 Hz mono, shape = full / short34 / bar38 / loop):
               the last S s of the track as the SPC plays it, then the first S s after its loop point
               (the join is at S s; a 'loop' / 'bars' / 'short' track loops to its first block, a 'full'
               one to the Safari loop point).  'once' tracks (jingles) have no join and get no file.
  --sec S      seconds on each side of the join (default 4)
  --check      for each track (needs the Safari web build, as make_audio.py):
                 - the decoded stream is the source segment bgm_loops.json names (correlation >= 0.99
                   outside the crossfade / fade-out at its end) and its block counts match bgm.json
                 - the rhythm at the join (make_audio.rhythm: the onset pattern after the loop start vs
                   after the loop end; lag 0 = the beat goes on) is re-measured; 'bars' tracks must have
                   |lag| <= --max-lag ms (default 12) and a loop of whole bars (within 0.16 s)
  TRACK        BGM names (BGM_ZAKO_MID), file stems (03_zako_mid) or shapes (bar38); default: all
  --gen DIR    another data/gen (default: this port's)
Exit status 0 when every check passes (without --check: when the files were written)."""
import argparse
import json
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'audio'))
import brrdec  # noqa: E402


def write_wav(path, x, rate):
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(np.clip(np.round(x), -32768, 32767).astype('<i2').tobytes())
    w.close()


def corr(x, y):
    n = min(len(x), len(y))
    x, y = x[:n] - np.mean(x[:n]), y[:n] - np.mean(y[:n])
    return float(np.dot(x, y) / (np.sqrt(np.dot(x, x) * np.dot(y, y)) + 1e-9))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', dest='out')
    ap.add_argument('--sec', type=float, default=4.0)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--max-lag', type=float, default=12.0)
    ap.add_argument('--gen', default=os.path.join(ROOT, 'data', 'gen'))
    ap.add_argument('tracks', nargs='*')
    a = ap.parse_args()
    if not a.out and not a.check:
        ap.error('nothing to do: give -o DIR and / or --check')
    man = json.load(open(os.path.join(a.gen, 'place', 'bgm.json'), encoding='utf-8'))['entries'][0]
    lj = json.load(open(os.path.join(a.gen, 'audio', 'bgm_loops.json'), encoding='utf-8'))
    rate = lj['rate']
    loops = {t['name']: t for t in lj['tracks']}
    brr = open(os.path.join(a.gen, man['file']), 'rb').read()
    want = set(a.tracks)
    sel = [t for t in man['tracks'] if not want or t['name'] in want or
           loops[t['name']]['src'].rsplit('.', 1)[0] in want or loops[t['name']]['shape'] in want]
    if not sel:
        sys.exit('no such track: %s' % ' '.join(a.tracks))
    if a.out:
        os.makedirs(a.out, exist_ok=True)
    ma = wb = None
    if a.check:
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import make_audio as ma  # noqa: E402
        wb = ma.webbuild.get()
    bad = 0
    n = int(a.sec * rate)
    for t in sel:
        lp = loops[t['name']]
        stem = lp['src'].rsplit('.', 1)[0]
        dec = brrdec.decode(brr[t['first'] * 9:(t['first'] + t['blocks']) * 9]).astype(np.float64)
        line = '%-17s %-7s %-22s' % (t['name'], lp['shape'], stem)
        if t['loop'] is not None:
            ls = t['loop'] * 16
            line += ' loop %6.2f s' % ((len(dec) - ls) / rate)
            if a.out:
                f = os.path.join(a.out, '%s_%s.wav' % (stem, lp['shape']))
                write_wav(f, np.concatenate([dec[max(ls, len(dec) - n):], dec[ls:ls + n]]), rate)
        else:
            line += ' once %6.2f s' % (len(dec) / rate)
        if lp.get('bars'):
            line += ' = %2d bars of %.3f s' % (lp['bars'], lp['bar_s'])
        if a.check:
            ok = True
            src = ma.source_pcm(wb, lp['src'])
            if len(src) != lp['src_len']:
                ok = False
                line += '  SOURCE CHANGED (%d samples, was %d)' % (len(src), lp['src_len'])
            else:
                seg = src[lp['begin']:lp['end'] - lp['tail']] * 32767
                c = corr(dec[:len(seg)], seg)
                blocks_ok = t['blocks'] == (lp['end'] - lp['begin'] + 15) // 16 and t['loop'] == lp['loop'] and (
                    lp['start'] is None or lp['start'] - lp['begin'] == (t['loop'] or 0) * 16)
                line += '  stream = source %.4f%s' % (c, '' if blocks_ok else ' BLOCKS DIFFER')
                ok &= c >= 0.99 and blocks_ok
                if lp['start'] is not None and lp['end'] < len(src) - 2 * rate:
                    o = ma.onset(src)
                    lag, r = ma.rhythm(o, lp['start'], lp['end'])
                    ch, chop = ma.chroma(src)
                    cm = ma.sim(ch, chop, lp['start'] // chop, lp['end'] // chop, 1.0, 2.0)
                    line += ', join lag %+4.0f ms r %.2f chroma %.2f' % (lag, r, cm)
                    if lp['shape'].startswith('bar'):
                        off = (lp['end'] - lp['start']) / rate - lp['bars'] * lp['bar_s']
                        if abs(lag) > a.max_lag or abs(off) > 0.16:
                            ok = False
                            line += ' (lag over %g ms or %.3f s off the bars)' % (a.max_lag, off)
                elif lp['start'] is not None:
                    line += ', join = the end of the file (the Safari loop)'
            line += '  OK' if ok else '  FAIL'
            bad += not ok
        print(line, flush=True)
    if a.out:
        print('join snippets (%g s + %g s, %d Hz) in %s' % (a.sec, a.sec, rate, a.out))
    if a.check:
        print('%d track(s), %s' % (len(sel), 'all checks pass' if not bad else '%d FAIL' % bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
