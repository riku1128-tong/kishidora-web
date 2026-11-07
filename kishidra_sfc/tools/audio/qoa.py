"""Decoder for QOA ("Quite OK Audio"), the format Godot uses for compressed AudioStreamWAV."""
import struct

SCALE = [round((s + 1) ** 2.75) for s in range(16)]
DQT = [0.75, -0.75, 2.5, -2.5, 4.5, -4.5, 7, -7]


def _rha(x):  # round half away from zero
    return int(x + 0.5) if x >= 0 else -int(-x + 0.5)


DEQ = [[_rha(SCALE[s] * q) for q in DQT] for s in range(16)]


def decode(blob):
    """-> (rate, channels, [samples per channel])"""
    assert blob[:4] == b'qoaf', 'not qoa'
    total, = struct.unpack_from('>I', blob, 4)
    p = 8
    rate = ch = 0
    out = None
    blob = blob[:8 + _payload_len(blob)] if False else blob
    while p + 8 <= len(blob):
        hdr, = struct.unpack_from('>Q', blob, p)
        fch = hdr >> 56
        frate = (hdr >> 32) & 0xffffff
        fs = (hdr >> 16) & 0xffff
        fsize = hdr & 0xffff
        if fch == 0 or fsize == 0 or frate == 0 or p + fsize > len(blob):
            break
        ch, rate = fch, frate
        if out is None:
            out = [[] for _ in range(ch)]
        q = p + 8
        lms = []
        for c in range(ch):
            h = list(struct.unpack_from('>4h', blob, q))
            w = list(struct.unpack_from('>4h', blob, q + 8))
            lms.append((h, w))
            q += 16
        nslices = (fs + 19) // 20
        done = [0] * ch
        for s in range(nslices):
            for c in range(ch):
                sl, = struct.unpack_from('>Q', blob, q)
                q += 8
                sf = sl >> 60
                h, w = lms[c]
                n = min(20, fs - s * 20)
                for j in range(n):
                    qv = (sl >> (57 - 3 * j)) & 7
                    pred = (h[0] * w[0] + h[1] * w[1] + h[2] * w[2] + h[3] * w[3]) >> 13
                    r = DEQ[sf][qv]
                    v = max(-32768, min(32767, pred + r))
                    out[c].append(v)
                    d = r >> 4
                    for k in range(4):
                        w[k] += -d if h[k] < 0 else d
                    h[0], h[1], h[2], h[3] = h[1], h[2], h[3], v
        p += fsize
    return rate, ch, out


def godot_sample(res):
    """QOA payload inside an imported AudioStreamWAV resource."""
    i = res.find(b'qoaf')
    if i < 0:
        raise ValueError('no qoa data')
    return res[i:]
