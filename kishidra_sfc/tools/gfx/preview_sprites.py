"""decode converted sprite/boss assets back to PNG (for checking)"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
import snesgfx as G
from PIL import Image
GEN = os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'gen')

def asset(name):
    p = os.path.join(GEN, 'gfx', name.lower() + '.bin')
    b = open(p, 'rb').read()
    h = open(os.path.join(GEN, 'gfx.h')).read()
    import re
    try:
        return G.unlzss(b)
    except Exception:
        return b

def names():
    import re
    h = open(os.path.join(GEN, 'gfx.h')).read()
    return {int(i): n for n, i in re.findall(r'#define ASSET_(\w+)\s+(\d+)', h)}

def raw(name):
    p = os.path.join(GEN, 'gfx', name.lower() + '.bin')
    b = open(p, 'rb').read()
    s = open(os.path.join(GEN, 'gfx.s')).read()
    import re
    m = re.search(r'gfx_%s:.*\n' % name.lower(), s)
    # flags in table are not easy to map here; try both
    try:
        d = G.unlzss(b)
        if len(d) >= len(b):
            return d
    except Exception:
        pass
    return b

def pal(name):
    b = open(os.path.join(GEN, 'gfx', name.lower() + '_pal.bin'), 'rb').read()
    return [G.from555(struct.unpack_from('<H', b, i)[0]) for i in range(0, len(b), 2)]

def decode_tile(t, p, palette, base=0):
    out = []
    for y in range(8):
        row = []
        for x in range(8):
            b = 7 - x
            v = ((t[y*2] >> b) & 1) | (((t[y*2+1] >> b) & 1) << 1) | (((t[16+y*2] >> b) & 1) << 2) | (((t[17+y*2] >> b) & 1) << 3)
            row.append(None if v == 0 else palette[p * 16 + v])
        out.append(row)
    return out

def sprite(name):
    nm = names()
    d = raw(name + '_DESC')
    n = d[0]; o = 1
    palette = pal(name)
    ims = []
    for i in range(n):
        code, aid, ns, npc = struct.unpack_from('<BHBB', d, o); o += 5
        strips = raw(nm[aid])
        im = Image.new('RGBA', (128, 128), (60, 110, 60, 255))
        for k in range(npc):
            dx, dy, ci = struct.unpack_from('<bbB', d, o); o += 3
            st, slot = ci // 4, ci % 4
            for tr in range(4):
                for tc in range(4):
                    off = st * 2048 + (tr * 16 + slot * 4 + tc) * 32
                    t = decode_tile(strips[off:off+32], 0, palette)
                    for y in range(8):
                        for x in range(8):
                            if t[y][x]:
                                im.putpixel((64 + dx + tc*8 + x, 100 + dy + tr*8 + y), t[y][x] + (255,))
        ims.append(im)
    return ims

def boss(name):
    nm = names()
    d = raw(name + '_DESC')
    n = d[0]; o = 1
    palette = [(0,0,0)] * 96 + pal(name)
    ims = []
    for i in range(n):
        code, ta, ma, W, H, ax, ay, nt = struct.unpack_from('<BHHBBbbH', d, o); o += 11
        tiles = raw(nm[ta]); mp = raw(nm[ma])
        im = Image.new('RGBA', (W*8, H*8), (60, 110, 60, 255))
        for j in range(W*H):
            w, = struct.unpack_from('<H', mp, j*2)
            if not w: continue
            t = decode_tile(tiles[(w & 1023)*32:(w & 1023)*32+32], (w >> 10) & 7, palette)
            hf, vf = (w >> 14) & 1, (w >> 15) & 1
            for y in range(8):
                for x in range(8):
                    c = t[y][x]
                    if c:
                        im.putpixel(((j % W)*8 + (7-x if hf else x), (j // W)*8 + (7-y if vf else y)), c + (255,))
        ims.append(im)
    return ims

if __name__ == '__main__':
    out = []
    for n in sys.argv[2:]:
        out += boss(n) if n.startswith('BOSS') else sprite(n)
    W = sum(i.width for i in out) + 4 * len(out)
    H = max(i.height for i in out)
    c = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    x = 0
    for i in out:
        c.paste(i, (x, 0)); x += i.width + 4
    c.resize((W * 2, H * 2), Image.NEAREST).save(sys.argv[1])
