#!/usr/bin/env python3
"""Render BG1/BG2/BG3/OBJ separately from a WRAM + VRAM dump (mode 1 layout of this game).
usage: layers.py wram.bin vram.bin out.png
Uses the WRAM shadows (_cgram, _bg1map.._bg3map, _oam) located via build/kishidra.map."""
import os, re, sys
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
VR = {'bg1': 0x0000, 'bg2': 0x2000, 'bg3': 0x6000, 'obj': 0x4000}   # word addresses


def syms():
    m = open(os.path.join(ROOT, 'build', 'kishidra.map')).read()
    return {n: int(a, 16) for n, a in re.findall(r'(\S+)\s+([0-9A-F]{6}) R[LE]A', m)}


def wram_off(a):
    return a & 0x1ffff if a >= 0x7e0000 else a & 0xffff


def tile(vram, base_word, n, bpp):
    off = base_word * 2 + n * 8 * bpp
    px = [[0] * 8 for _ in range(8)]
    for y in range(8):
        planes = []
        for p in range(0, bpp, 2):
            planes.append(vram[(off + p * 8 + y * 2) & 0xffff])
            planes.append(vram[(off + p * 8 + y * 2 + 1) & 0xffff])
        for x in range(8):
            v = 0
            for b, pl in enumerate(planes):
                v |= ((pl >> (7 - x)) & 1) << b
            px[y][x] = v
    return px


def colour(c):
    return ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31)


def render_bg(vram, cg, tmap, base, bpp, palbase):
    im = Image.new('RGB', (256, 224), (255, 0, 255))
    pix = im.load()
    for ty in range(28):
        for tx in range(32):
            e = tmap[(ty * 32 + tx) * 2] | tmap[(ty * 32 + tx) * 2 + 1] << 8
            t = tile(vram, base, e & 0x3ff, bpp)
            pal = (e >> 10) & 7
            hf, vf = e & 0x4000, e & 0x8000
            for y in range(8):
                for x in range(8):
                    v = t[7 - y if vf else y][7 - x if hf else x]
                    if v:
                        pix[tx * 8 + x, ty * 8 + y] = colour(cg[palbase + pal * (1 << bpp) + v])
                    elif (e >> 13) & 1:
                        pix[tx * 8 + x, ty * 8 + y] = (60, 0, 60)  # transparent, priority 1
    return im


def render_obj(vram, cg, oam):
    im = Image.new('RGB', (256, 224), (255, 0, 255))
    pix = im.load()
    for i in range(127, -1, -1):
        x = oam[i * 4]
        y = oam[i * 4 + 1]
        t = oam[i * 4 + 2]
        a = oam[i * 4 + 3]
        hb = (oam[512 + i // 4] >> ((i & 3) * 2)) & 3
        if hb & 1:
            x -= 256
        size = 32 if hb & 2 else 8
        if y >= 224 and y < 240:
            continue
        tn = t | (a & 1) << 8
        pal = (a >> 1) & 7
        hf, vf = a & 0x40, a & 0x80
        n = size // 8
        for cy in range(n):
            for cx in range(n):
                sub = (tn & 0x1f0) | ((tn + cx) & 0x0f)
                sub = (sub + cy * 16) & 0x1ff
                tt = tile(vram, VR['obj'], sub, 4)
                for yy in range(8):
                    for xx in range(8):
                        v = tt[yy][xx]
                        if not v:
                            continue
                        px = (n - 1 - cx) * 8 + 7 - xx if hf else cx * 8 + xx
                        py = (n - 1 - cy) * 8 + 7 - yy if vf else cy * 8 + yy
                        sx, sy = x + px, (y + py) & 255
                        if 0 <= sx < 256 and sy < 224:
                            pix[sx, sy] = colour(cg[128 + pal * 16 + v])
    return im


def main():
    w = open(sys.argv[1], 'rb').read()
    v = open(sys.argv[2], 'rb').read()
    s = syms()
    cgo = wram_off(s['_cgram'])
    cg = [w[cgo + i * 2] | w[cgo + i * 2 + 1] << 8 for i in range(256)]
    maps = {k: w[wram_off(s['_bg%dmap' % k]):wram_off(s['_bg%dmap' % k]) + 2048] for k in (1, 2, 3)}
    oo = wram_off(s['_oam'])
    oam = w[oo:oo + 544]
    ims = [render_bg(v, cg, maps[1], VR['bg1'], 4, 0), render_bg(v, cg, maps[2], VR['bg2'], 4, 0),
           render_bg(v, cg, maps[3], VR['bg3'], 2, 0), render_obj(v, cg, oam)]
    S = Image.new('RGB', (512 * 2, 448 * 2))
    for i, im in enumerate(ims):
        S.paste(im.resize((512, 448), Image.NEAREST), ((i % 2) * 512, (i // 2) * 448))
    S.save(sys.argv[3])


main()
