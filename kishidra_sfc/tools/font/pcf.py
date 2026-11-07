"""Minimal reader for X11 PCF bitmap fonts (used for the 東雲 12-dot fonts).

Returns {unicode_char: (width, height, rows)} where rows are ints (MSB = left pixel)
already placed in a cell of the font's full height (ascent+descent).
"""
import gzip
import struct

PCF_PROPERTIES = 1 << 0
PCF_METRICS = 1 << 2
PCF_BITMAPS = 1 << 3
PCF_BDF_ENCODINGS = 1 << 5
PCF_BDF_ACCELERATORS = 1 << 8
PCF_ACCELERATORS = 1 << 1


def _fmt(f):
    return '>' if f & (1 << 2) else '<'


def read_pcf(path):
    data = gzip.open(path).read() if path.endswith('.gz') else open(path, 'rb').read()
    assert data[:4] == b'\x01fcp'
    n, = struct.unpack_from('<I', data, 4)
    tables = {}
    for i in range(n):
        t, f, size, off = struct.unpack_from('<IIII', data, 8 + 16 * i)
        tables[t] = (f, size, off)

    # properties (for CHARSET_REGISTRY)
    f, size, off = tables[PCF_PROPERTIES]
    e = _fmt(struct.unpack_from('<I', data, off)[0])
    nprops, = struct.unpack_from(e + 'I', data, off + 4)
    props = []
    p = off + 8
    for _ in range(nprops):
        name_off, is_str, val = struct.unpack_from(e + 'IbI', data, p)
        props.append((name_off, is_str, val))
        p += 9
    p += (4 - (nprops & 3)) & 3 if nprops & 3 else 0
    strsize, = struct.unpack_from(e + 'I', data, p)
    strs = data[p + 4:p + 4 + strsize]

    def cstr(o):
        return strs[o:strs.index(b'\0', o)].decode('latin-1')
    prop = {}
    for no, is_str, val in props:
        prop[cstr(no)] = cstr(val) if is_str else val

    # accelerators for ascent/descent
    acc = tables.get(PCF_BDF_ACCELERATORS) or tables.get(PCF_ACCELERATORS)
    f, size, off = acc
    e = _fmt(struct.unpack_from('<I', data, off)[0])
    ascent, descent = struct.unpack_from(e + 'ii', data, off + 4 + 8)

    # metrics
    f, size, off = tables[PCF_METRICS]
    fmt, = struct.unpack_from('<I', data, off)
    e = _fmt(fmt)
    metrics = []
    if fmt & 0x100:   # compressed
        cnt, = struct.unpack_from(e + 'H', data, off + 4)
        p = off + 6
        for _ in range(cnt):
            l, r, w, a, d = struct.unpack_from('5B', data, p)
            p += 5
            metrics.append((l - 0x80, r - 0x80, w - 0x80, a - 0x80, d - 0x80))
    else:
        cnt, = struct.unpack_from(e + 'I', data, off + 4)
        p = off + 8
        for _ in range(cnt):
            l, r, w, a, d, attr = struct.unpack_from(e + 'hhhhhH', data, p)
            p += 12
            metrics.append((l, r, w, a, d))

    # bitmaps
    f, size, off = tables[PCF_BITMAPS]
    fmt, = struct.unpack_from('<I', data, off)
    e = _fmt(fmt)
    cnt, = struct.unpack_from(e + 'I', data, off + 4)
    offsets = struct.unpack_from(e + '%dI' % cnt, data, off + 8)
    p = off + 8 + 4 * cnt
    sizes = struct.unpack_from(e + '4I', data, p)
    pad = fmt & 3
    bits = data[p + 16:p + 16 + sizes[pad]]
    msbit = bool(fmt & (1 << 3))
    msbyte = bool(fmt & (1 << 2))
    unit = 1 << ((fmt >> 4) & 3)
    rowbytes_for = lambda w: ((w + 7) // 8 + (1 << pad) - 1) // (1 << pad) * (1 << pad)

    # encodings
    f, size, off = tables[PCF_BDF_ENCODINGS]
    e = _fmt(struct.unpack_from('<I', data, off)[0])
    min2, max2, min1, max1, default = struct.unpack_from(e + '5H', data, off + 4)
    ncodes = (max2 - min2 + 1) * (max1 - min1 + 1)
    idx = struct.unpack_from(e + '%dH' % ncodes, data, off + 14)

    reg = (prop.get('CHARSET_REGISTRY', '') + '-' + prop.get('CHARSET_ENCODING', '')).lower()
    out = {}
    H = ascent + descent
    for b1 in range(min1, max1 + 1):
        for b2 in range(min2, max2 + 1):
            gi = idx[(b1 - min1) * (max2 - min2 + 1) + (b2 - min2)]
            if gi == 0xffff:
                continue
            if reg.startswith('jisx0208'):
                try:
                    ch = bytes([b1 | 0x80, b2 | 0x80]).decode('euc_jp')
                except UnicodeDecodeError:
                    continue
            elif reg.startswith('jisx0201'):
                code = (b1 << 8) | b2
                if code < 0x80:
                    ch = chr(code)
                elif 0xa1 <= code <= 0xdf:
                    ch = bytes([0x8e, code]).decode('euc_jp')
                else:
                    continue
            else:
                ch = chr((b1 << 8) | b2)
            l, r, w, a, d = metrics[gi]
            bw = r - l
            bh = a + d
            rb = rowbytes_for(bw)
            o = offsets[gi]
            rows = []
            for y in range(bh):
                raw = bits[o + y * rb:o + y * rb + rb]
                if not msbyte and unit > 1:
                    raw = b''.join(raw[k:k + unit][::-1] for k in range(0, len(raw), unit))
                v = 0
                for k, byte in enumerate(raw):
                    if not msbit:
                        byte = int('{:08b}'.format(byte)[::-1], 2)
                    v = (v << 8) | byte
                v >>= rb * 8 - bw if bw > 0 else 0
                rows.append(v)
            cell = [0] * H
            for y in range(bh):
                cy = ascent - a + y
                if 0 <= cy < H:
                    sh = l
                    cell[cy] = (rows[y] << max(0, (w - bw - l))) if True else 0
                    cell[cy] = rows[y] << max(0, w - (l + bw)) if l >= 0 else rows[y] >> (-l)
            out[ch] = (w, H, cell)
    return out, ascent, descent
