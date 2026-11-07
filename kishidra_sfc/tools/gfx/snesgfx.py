"""SNES graphics helpers: colour reduction, 4bpp/2bpp tiles, tile maps, LZSS.

Colours are BGR555.  Pixel art from the Safari build is drawn at 4x, so most
images are first reduced to their native size and then fitted to a few
16-colour palettes (colour 0 = transparent).
"""
import struct
from PIL import Image


def rgb555(c):
    r, g, b = c[:3]
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def from555(v):
    return ((v & 31) << 3 | (v & 31) >> 2, (v >> 5 & 31) << 3 | (v >> 5 & 31) >> 2, (v >> 10 & 31) << 3 | (v >> 10 & 31) >> 2)


def snap(img):
    """RGBA image with colours snapped to 5 bits and alpha to 0/255."""
    img = img.convert('RGBA')
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a < 128:
                px[x, y] = (0, 0, 0, 0)
            else:
                px[x, y] = (r & 0xf8, g & 0xf8, b & 0xf8, 255)
    return img


def native(img, w, h):
    """4x pixel art -> native size (sample cell centres)."""
    return img.resize((w, h), Image.NEAREST)


def scale(img, k):
    """Shrink pixel art by k (< 1) keeping hard alpha."""
    w = max(1, round(img.width * k))
    h = max(1, round(img.height * k))
    rgb = img.convert('RGBA')
    # premultiplied resize to avoid dark fringes
    pre = Image.new('RGBA', rgb.size)
    pre.paste(rgb, (0, 0), rgb)
    small = pre.resize((w, h), Image.LANCZOS)
    a = rgb.split()[3].resize((w, h), Image.BOX)
    small.putalpha(a.point(lambda v: 255 if v >= 110 else 0))
    return small


def _dist(a, b):
    dr = a[0] - b[0]
    dg = a[1] - b[1]
    db = a[2] - b[2]
    return 2 * dr * dr + 4 * dg * dg + 3 * db * db


def make_palette(pixels, n):
    """n colours for a list of (r,g,b) pixels (median cut via PIL)."""
    uniq = list(dict.fromkeys(pixels))
    if len(uniq) <= n:
        return uniq
    w = min(len(pixels), 4096)
    step = max(1, len(pixels) // w)
    sample = pixels[::step]
    im = Image.new('RGB', (len(sample), 1))
    im.putdata(sample)
    q = im.quantize(colors=n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:n * 3]
    out = []
    for i in range(n):
        c = (pal[3 * i] & 0xf8, pal[3 * i + 1] & 0xf8, pal[3 * i + 2] & 0xf8)
        if c not in out:
            out.append(c)
    return out


def nearest(c, pal):
    best = 0
    bd = 1 << 30
    for i, p in enumerate(pal):
        d = _dist(c, p)
        if d < bd:
            bd = d
            best = i
    return best


class Cache:
    def __init__(self, pal):
        self.pal = pal
        self.m = {}

    def __call__(self, c):
        v = self.m.get(c)
        if v is None:
            v = self.m[c] = nearest(c, self.pal)
        return v


def fit_palettes(blocks, npal, ncol=15, iters=3):
    """blocks: list of pixel lists (opaque (r,g,b) only).  Returns (palettes, assignment)."""
    if npal == 1:
        allpx = [p for b in blocks for p in b]
        pal = make_palette(allpx, ncol) if allpx else [(0, 0, 0)]
        return [pal], [0] * len(blocks)
    # initial grouping by mean colour (k-means on means)
    means = []
    for b in blocks:
        if b:
            means.append(tuple(sum(p[i] for p in b) / len(b) for i in range(3)))
        else:
            means.append((0, 0, 0))
    order = sorted(range(len(blocks)), key=lambda i: means[i][0] * 0.3 + means[i][1] * 0.6 + means[i][2] * 0.1)
    assign = [0] * len(blocks)
    for k, i in enumerate(order):
        assign[i] = k * npal // max(1, len(order))
    pals = []
    for _ in range(iters):
        pals = []
        for g in range(npal):
            px = [p for i, b in enumerate(blocks) if assign[i] == g for p in b]
            pals.append(make_palette(px, ncol) if px else [(0, 0, 0)])
        caches = [Cache(p) for p in pals]
        changed = False
        for i, b in enumerate(blocks):
            if not b:
                continue
            best = assign[i]
            be = None
            for g in range(npal):
                c = caches[g]
                e = 0
                for p in b:
                    e += _dist(p, pals[g][c(p)])
                if be is None or e < be:
                    be = e
                    best = g
            if best != assign[i]:
                assign[i] = best
                changed = True
        if not changed:
            break
    return pals, assign


def tile4(idx8x8):
    """64 colour indices (0-15) -> 32 bytes of SNES 4bpp."""
    out = bytearray(32)
    for y in range(8):
        for x in range(8):
            v = idx8x8[y * 8 + x]
            bit = 7 - x
            if v & 1:
                out[y * 2] |= 1 << bit
            if v & 2:
                out[y * 2 + 1] |= 1 << bit
            if v & 4:
                out[16 + y * 2] |= 1 << bit
            if v & 8:
                out[16 + y * 2 + 1] |= 1 << bit
    return bytes(out)


def tile2(idx8x8):
    out = bytearray(16)
    for y in range(8):
        for x in range(8):
            v = idx8x8[y * 8 + x]
            bit = 7 - x
            if v & 1:
                out[y * 2] |= 1 << bit
            if v & 2:
                out[y * 2 + 1] |= 1 << bit
    return bytes(out)


def flipped(idx, h, v):
    out = []
    for y in range(8):
        yy = 7 - y if v else y
        for x in range(8):
            xx = 7 - x if h else x
            out.append(idx[yy * 8 + xx])
    return tuple(out)


def palette_bytes(pal, ncol=16, zero=(0, 0, 0)):
    """palette list (without colour 0) -> 32 bytes"""
    out = struct.pack('<H', rgb555(zero))
    for c in pal[:ncol - 1]:
        out += struct.pack('<H', rgb555(c))
    out += b'\0\0' * (ncol - 1 - len(pal[:ncol - 1]))
    return out


def bg_image(img, npal=4, pal_base=2, tile_base=0, dedup=True, opaque_color0=None):
    """Convert an image (multiple of 8) to (tiles bytes, map words, palettes bytes, ntiles).

    opaque_color0: if set, colour index 0 is a real colour (the backdrop) and all pixels
    may use 15 colours + it.  Otherwise index 0 = transparent (alpha 0)."""
    img = snap(img)
    W, H = img.width // 8, img.height // 8
    px = img.load()
    blocks = []
    raw = []
    for ty in range(H):
        for tx in range(W):
            b = []
            r = []
            for y in range(8):
                for x in range(8):
                    c = px[tx * 8 + x, ty * 8 + y]
                    r.append(c)
                    if c[3]:
                        b.append(c[:3])
            blocks.append(b)
            raw.append(r)
    pals, assign = fit_palettes(blocks, npal)
    caches = [Cache(p) for p in pals]
    tiles = []
    index = {}
    words = []
    for i, r in enumerate(raw):
        g = assign[i]
        idx = tuple(0 if c[3] == 0 else 1 + caches[g](c[:3]) for c in r)
        key = None
        hv = (0, 0)
        if dedup:
            for h in (0, 1):
                for v in (0, 1):
                    k = flipped(idx, h, v)
                    if k in index:
                        key = index[k]
                        hv = (h, v)
                        break
                if key is not None:
                    break
        if key is None:
            key = len(tiles)
            tiles.append(idx)
            index[idx] = key
            hv = (0, 0)
        words.append((tile_base + key) | ((pal_base + g) << 10) | (hv[0] << 14) | (hv[1] << 15))
    data = b''.join(tile4(t) for t in tiles)
    paldata = b''.join(palette_bytes(p) for p in pals)
    return data, words, paldata, len(tiles), (W, H)


# ---------------------------------------------------------------- LZSS
# format: control byte (LSB first), 1 = literal, 0 = match (2 bytes: llll oooo oooo oooo,
# length = l + 3 (3..18), distance = o + 1 (1..4096)); stream starts with the raw size (u16).
def lzss(data):
    out = bytearray(struct.pack('<H', len(data)))
    i = 0
    n = len(data)
    head = {}
    while i < n:
        ctrl_pos = len(out)
        out.append(0)
        ctrl = 0
        for bit in range(8):
            if i >= n:
                break
            best_l = 0
            best_d = 0
            if i + 3 <= n:
                key = data[i:i + 3]
                for j in reversed(head.get(key, [])[-48:]):
                    d = i - j
                    if d > 4096:
                        continue
                    l = 3
                    while l < 18 and i + l < n and data[j + l] == data[i + l]:
                        l += 1
                    if l > best_l:
                        best_l = l
                        best_d = d
                        if l == 18:
                            break
            if best_l >= 3:
                v = ((best_l - 3) << 12) | (best_d - 1)
                out += struct.pack('<H', v)
                for k in range(best_l):
                    if i + k + 3 <= n:
                        head.setdefault(data[i + k:i + k + 3], []).append(i + k)
                i += best_l
            else:
                ctrl |= 1 << bit
                out.append(data[i])
                if i + 3 <= n:
                    head.setdefault(data[i:i + 3], []).append(i)
                i += 1
        out[ctrl_pos] = ctrl
    return bytes(out)


def unlzss(blob):
    n, = struct.unpack_from('<H', blob, 0)
    p = 2
    out = bytearray()
    while len(out) < n:
        ctrl = blob[p]
        p += 1
        for bit in range(8):
            if len(out) >= n:
                break
            if ctrl & (1 << bit):
                out.append(blob[p])
                p += 1
            else:
                v, = struct.unpack_from('<H', blob, p)
                p += 2
                l = (v >> 12) + 3
                d = (v & 0xfff) + 1
                for _ in range(l):
                    out.append(out[-d])
    return bytes(out)
