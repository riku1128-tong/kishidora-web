#!/usr/bin/env python3
"""Graphics for the SFC build, converted from the Safari build's art.

Writes data/gen/gfx.s, data/gen/gfx.h and data/gen/gfx/*.bin (see tools/gfx/assets.py).
Preview PNGs of every converted screen go to build/preview/.
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(HERE, 'gfx'))
sys.path.insert(0, os.path.join(HERE, 'extract'))
sys.path.insert(0, os.path.join(HERE, 'data'))
from PIL import Image, ImageDraw  # noqa: E402
import snesgfx as G  # noqa: E402
from assets import Assets  # noqa: E402
import webbuild  # noqa: E402

PREVIEW = os.path.join(ROOT, 'build', 'preview')
GFX_BANKS = list(range(0xC3, 0xE0)) + list(range(0xA0, 0xC0))

SCALE = 0.73          # HD native pixel art -> SFC size (same as the Safari "pitch" ratio)


def preview_bg(path, data, words, pal, size, pal_base=2):
    """Decode tiles+map+palettes into a PNG to check the conversion."""
    W, H = size
    tiles = [data[i:i + 32] for i in range(0, len(data), 32)]
    cols = {}
    for p in range(len(pal) // 32):
        for c in range(16):
            v, = struct.unpack_from('<H', pal, p * 32 + c * 2)
            cols[(pal_base + p, c)] = G.from555(v)
    im = Image.new('RGB', (W * 8, H * 8), (255, 0, 255))
    px = im.load()
    for i, w in enumerate(words):
        t = tiles[w & 1023]
        p = (w >> 10) & 7
        hf, vf = (w >> 14) & 1, (w >> 15) & 1
        tx, ty = i % W, i // W
        for y in range(8):
            for x in range(8):
                b = 7 - x
                v = ((t[y * 2] >> b) & 1) | (((t[y * 2 + 1] >> b) & 1) << 1) | (((t[16 + y * 2] >> b) & 1) << 2) | (((t[17 + y * 2] >> b) & 1) << 3)
                xx = 7 - x if hf else x
                yy = 7 - y if vf else y
                if v:
                    px[tx * 8 + xx, ty * 8 + yy] = cols.get((p, v), (255, 0, 255))
                else:
                    px[tx * 8 + xx, ty * 8 + yy] = (0, 0, 0)
    os.makedirs(PREVIEW, exist_ok=True)
    im.save(os.path.join(PREVIEW, path))


class Gfx:
    def __init__(self):
        self.wb = webbuild.get()
        self.a = Assets('gfx', GFX_BANKS)

    def img(self, path):
        return self.wb.image(path)

    def bg(self, name, im, npal=4, pal_base=2, tile_base=0, max_tiles=1024):
        data, words, pal, n, size = G.bg_image(im, npal=npal, pal_base=pal_base, tile_base=tile_base)
        if n > max_tiles:
            raise SystemExit('%s: %d tiles (max %d)' % (name, n, max_tiles))
        self.a.add(name + '_TILES', data)
        self.a.add(name + '_MAP', b''.join(struct.pack('<H', w) for w in words))
        self.a.add(name + '_PAL', pal, compress=False)
        self.a.const('%s_NTILES' % name, n)
        self.a.const('%s_W' % name, size[0])
        self.a.const('%s_H' % name, size[1])
        preview_bg(name.lower() + '.png', data, words, pal, size, pal_base)
        return n

    # ------------------------------------------------------------ screens
    def title(self):
        im = self.img('assets/title/title_bg.png').resize((256, 224), Image.NEAREST)
        self.bg('TITLE', im, npal=6, pal_base=2)

    def worldmap(self):
        im = self.img('assets/map/worldmap.png').resize((256, 256), Image.LANCZOS)
        self.bg('WORLDMAP', im, npal=6, pal_base=2)

    def battle_bgs(self):
        T = webbuild.script_constants(self.wb, 'scripts/tuning.gdc')
        names = sorted(set(T['AREA_BG'].values()) | {T['CASTLE_FINAL_BG'], T['MIRAGE']['surface']['bg'], 'field', 'cave'})
        self.bg_names = names
        for n in names:
            path = 'assets/bg/%s_hd.png' % n
            if not self.wb.has(path + '.import'):
                print('no bg', n)
                continue
            im = self.img(path)
            h = round(im.height * 256 / im.width)
            im = im.resize((256, h), Image.NEAREST)
            top = max(0, min(h - 128, int(h * 0.22)))
            self.bg('BG_' + n.upper(), im.crop((0, top, 256, top + 128)), npal=4, pal_base=2, tile_base=0, max_tiles=512)
        self.a.const('BG_LIST', '"%s"' % ','.join(names))

    def write(self):
        self.a.write()


def main():
    g = Gfx()
    g.title()
    g.worldmap()
    g.battle_bgs()
    g.write()


if __name__ == '__main__':
    main()
