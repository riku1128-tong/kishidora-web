"""Reference BRR decoder (same arithmetic as the DSP) for checking encoded data."""
import numpy as np


def decode(data, nblocks=None):
    n = len(data) // 9 if nblocks is None else nblocks
    out = np.zeros(n * 16, dtype=np.int32)
    p1 = p2 = 0
    k = 0
    for b in range(n):
        h = data[b * 9]
        shift, filt = h >> 4, (h >> 2) & 3
        for i in range(16):
            byte = data[b * 9 + 1 + i // 2]
            nib = (byte >> 4) if i % 2 == 0 else (byte & 15)
            if nib >= 8:
                nib -= 16
            s = ((nib << shift) >> 1) if shift <= 12 else (-2048 if nib < 0 else 0)
            q2 = p2 >> 1
            if filt == 1:
                s += (p1 >> 1) + ((-p1) >> 5)
            elif filt == 2:
                s += p1 - q2 + (q2 >> 4) + ((p1 * -3) >> 6)
            elif filt == 3:
                s += p1 - q2 + ((p1 * -13) >> 7) + ((q2 * 3) >> 4)
            s = max(-32768, min(32767, s))
            s = ((s << 1) & 0xffff)
            if s >= 0x8000:
                s -= 0x10000
            out[k] = s
            k += 1
            p2, p1 = p1, s
    return out
