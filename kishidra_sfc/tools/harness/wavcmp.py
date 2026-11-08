#!/usr/bin/env python3
"""Compare two runrom recordings window by window, allowing a small time shift (the stream / SE start
can move by a few samples when the CPU timing inside a frame changes; see PLAN S1 / S2 notes).

usage: wavcmp.py [--win SEC] [--max-shift N] A.wav B.wav

For each window: the shift of B (in samples, -N..N) that makes it closest to A, and the largest sample
difference left at that shift (0 = identical after the shift).  Summary: windows identical at shift 0,
identical at another shift, and different at every shift (in a mix of music and a sound effect that
moved relative to each other no single shift fits).  Exit status 0 when every window is identical at
some shift."""
import argparse
import sys
import wave

import numpy as np


def read(path):
    w = wave.open(path)
    d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, w.getnchannels())
    return d.astype(np.int32), w.getframerate()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--win', type=float, default=0.25)
    ap.add_argument('--max-shift', type=int, default=64)
    o = ap.parse_args()
    a, rate = read(o.a)
    b, _ = read(o.b)
    n, m = int(o.win * rate), o.max_shift
    same0 = shifted = differ = 0
    rows = []
    for s in range(m, min(len(a), len(b)) - n - m, n):
        x = a[s:s + n]
        best = None
        for sh in [0] + [k for r in range(1, m + 1) for k in (-r, r)]:
            e = int(np.abs(x - b[s + sh:s + sh + n]).max())
            if best is None or e < best[1]:
                best = (sh, e)
            if e == 0:
                break
        sh, e = best
        if e == 0 and sh == 0:
            same0 += 1
        elif e == 0:
            shifted += 1
        else:
            differ += 1
        rows.append('%7.2f s  shift %4d  max diff %6d  rms %6.0f' % (s / rate, sh, e, np.sqrt((x.astype(float) ** 2).mean())))
    print('\n'.join(rows))
    print('%d windows of %.2f s: %d identical, %d identical after a shift, %d differ at every shift (|shift| <= %d)'
          % (len(rows), o.win, same0, shifted, differ, m))
    sys.exit(0 if not differ else 1)


if __name__ == '__main__':
    main()
