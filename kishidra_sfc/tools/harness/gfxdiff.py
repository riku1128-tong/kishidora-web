#!/usr/bin/env python3
"""Compare the graphics of two data/gen folders asset by asset (after a regeneration, or a make_gfx change
that should leave the old art alone), and show what changed in the sprite sets.

usage: gfxdiff.py [-o DIR] GEN_A GEN_B

  Every asset of gfx_table is unpacked (LZSS / tile runs, as src/asm/asset.s does) and compared by name
  (gfx.h ASSET_* ids; an asset only in one folder is listed too).  For each changed sprite set (HERO,
  MON_nnn: OBJ strips + one palette; BOSS_nnn: BG1 tiles + map + two palettes) every pose is drawn from
  both folders and compared: pixels that became transparent / opaque, pixels whose colour changed, the
  largest colour step (one SNES step = 8 levels), and the palette entries that moved.
  -o DIR writes DIR/<set>.png per changed set (each pose: A | B | changed pixels in red, 2x).
Exit status 0 when every asset is identical."""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'gfx'))
import snesgfx as G  # noqa: E402
from assets import untile_runs  # noqa: E402


class Gen:
    def __init__(self, d):
        self.d = d
        h = open(os.path.join(d, 'gfx.h')).read()
        self.ids = {n: int(i) for n, i in re.findall(r'#define ASSET_(\w+)\s+(\d+)', h)}
        self.names = {i: n for n, i in self.ids.items()}
        import json
        files = {e['label']: e['file'] for e in json.load(open(os.path.join(d, 'place', 'gfx.json')))['entries']}
        s = open(os.path.join(d, 'gfx.s')).read()
        rows = re.findall(r'\.word \.loword\((\w+)\)\s*\n\s*\.byte <\.bank\(\w+\), (\d+)\s*\n\s*\.word (\d+)', s)
        self.rows = [(files[lab], int(fl)) for lab, fl, _ in rows]
        self.cache = {}

    def get(self, name):
        if name not in self.cache:
            f, fl = self.rows[self.ids[name]]
            b = open(os.path.join(self.d, f), 'rb').read()
            if fl & 2:
                b = untile_runs(b)
            elif fl & 1:
                b = G.unlzss(b)
            self.cache[name] = b
        return self.cache[name]


def pal(blob):
    return [G.from555(struct.unpack_from('<H', blob, i)[0]) for i in range(0, len(blob), 2)]


def tile(t, base, palette):
    out = []
    for y in range(8):
        row = []
        for x in range(8):
            b = 7 - x
            v = ((t[y * 2] >> b) & 1) | (((t[y * 2 + 1] >> b) & 1) << 1) | (((t[16 + y * 2] >> b) & 1) << 2) | \
                (((t[17 + y * 2] >> b) & 1) << 3)
            row.append(None if v == 0 else palette[base + v])
        out.append(row)
    return out


def sprite_poses(g, name):
    """{pose code + index: {(x, y): rgb}} of a sprite_set (make_gfx.py), anchor at (0, 0)"""
    d = g.get(name + '_DESC')
    palette = pal(g.get(name + '_PAL'))
    n, o, out = d[0], 1, {}
    for i in range(n):
        code, aid, ns, npc = struct.unpack_from('<BHBB', d, o)
        o += 5
        strips = g.get(g.names[aid])
        px = {}
        for k in range(npc):
            dx, dy, ci = struct.unpack_from('<bbB', d, o)
            o += 3
            st, slot = ci // 4, ci % 4
            for tr in range(4):
                for tc in range(4):
                    off = st * 2048 + (tr * 16 + slot * 4 + tc) * 32
                    t = tile(strips[off:off + 32], 0, palette)
                    for y in range(8):
                        for x in range(8):
                            if t[y][x]:
                                px[(dx + tc * 8 + x, dy + tr * 8 + y)] = t[y][x]
        out['%d:%s' % (i, g.names[aid][len(name) + 1:].lower())] = px
    return out, palette


def boss_poses(g, name):
    d = g.get(name + '_DESC')
    palette = [(0, 0, 0)] * 96 + pal(g.get(name + '_PAL'))
    n, o, out = d[0], 1, {}
    for i in range(n):
        code, ta, ma, W, H, ax, ay, nt = struct.unpack_from('<BHHBBbbH', d, o)
        o += 11
        tiles, mp = g.get(g.names[ta]), g.get(g.names[ma])
        px = {}
        for j in range(W * H):
            w, = struct.unpack_from('<H', mp, j * 2)
            if not w:
                continue
            t = tile(tiles[(w & 1023) * 32:(w & 1023) * 32 + 32], ((w >> 10) & 7) * 16, palette)
            hf, vf = (w >> 14) & 1, (w >> 15) & 1
            for y in range(8):
                for x in range(8):
                    if t[y][x]:
                        px[(ax + (j % W) * 8 + (7 - x if hf else x), ay + (j // W) * 8 + (7 - y if vf else y))] = t[y][x]
        out['%d:%s' % (i, g.names[ta][len(name) + 1:-2].lower())] = px
    return out, palette[96:]


def compare_set(ga, gb, name, outdir):
    fn = boss_poses if name.startswith('BOSS_') else sprite_poses
    (pa, la), (pb, lb) = fn(ga, name), fn(gb, name)
    moved = [(i, a, b) for i, (a, b) in enumerate(zip(la, lb)) if a != b]
    step = max([max(abs(x - y) for x, y in zip(a, b)) for _, a, b in moved] or [0]) // 8
    lines = ['%-10s palette: %d of %d colours moved (largest step %d)' % (name, len(moved), len(la), step)]
    tot = [0, 0, 0, 0]
    rows = []
    for k in sorted(set(pa) | set(pb), key=lambda s: int(s.split(':')[0])):
        a, b = pa.get(k, {}), pb.get(k, {})
        gone = [p for p in a if p not in b]
        new = [p for p in b if p not in a]
        col = [p for p in a if p in b and a[p] != b[p]]
        big = max([max(abs(x - y) for x, y in zip(a[p], b[p])) for p in col] or [0])
        if gone or new or col:
            tot[0] += 1
            tot[1] += len(gone)
            tot[2] += len(new)
            tot[3] += len(col)
            lines.append('    pose %-22s %4d px transparent now, %4d opaque now, %5d recoloured (max %3d levels)' % (
                k, len(gone), len(new), len(col), big))
            rows.append((k, a, b))
    lines.append('    %d poses changed: %d px transparent now, %d opaque now, %d recoloured' % tuple(tot))
    if outdir and rows:
        from PIL import Image
        ims = []
        for k, a, b in rows:
            pts = list(a) + list(b)
            x0, y0 = min(p[0] for p in pts), min(p[1] for p in pts)
            w, h = max(p[0] for p in pts) - x0 + 1, max(p[1] for p in pts) - y0 + 1
            im = Image.new('RGB', (w * 3 + 8, h), (60, 110, 60))
            for p, c in a.items():
                im.putpixel((p[0] - x0, p[1] - y0), c)
            for p, c in b.items():
                im.putpixel((w + 4 + p[0] - x0, p[1] - y0), c)
            for p in set(a) | set(b):
                c = (255, 40, 40) if a.get(p) != b.get(p) else (30, 30, 30)
                im.putpixel((2 * w + 8 + p[0] - x0, p[1] - y0), c)
            ims.append(im)
        W, H = max(i.width for i in ims), sum(i.height + 4 for i in ims)
        sheet = Image.new('RGB', (W, H), (0, 0, 0))
        y = 0
        for i in ims:
            sheet.paste(i, (0, y))
            y += i.height + 4
        sheet.resize((W * 2, H * 2), Image.NEAREST).save(os.path.join(outdir, name.lower() + '.png'))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gen_a')
    ap.add_argument('gen_b')
    ap.add_argument('-o', dest='out')
    a = ap.parse_args()
    ga, gb = Gen(a.gen_a), Gen(a.gen_b)
    if a.out:
        os.makedirs(a.out, exist_ok=True)
    only = sorted(set(ga.ids) ^ set(gb.ids))
    changed = [n for n in sorted(set(ga.ids) & set(gb.ids), key=lambda n: ga.ids[n]) if ga.get(n) != gb.get(n)]
    print('%d assets / %d assets: %d changed, %d only in one folder' % (len(ga.ids), len(gb.ids), len(changed), len(only)))
    for n in only:
        print('  only in %s: %s' % ('A' if n in ga.ids else 'B', n))
    sets = sorted({n[:-5] for n in ga.ids if n.endswith('_DESC') and re.match(r'^(HERO|MON_\d+|BOSS_\d+)_DESC$', n)})
    done = set()
    for s in sets:
        mine = [n for n in changed if n == s or n.startswith(s + '_')]
        if not mine:
            continue
        done.update(mine)
        print('\n'.join(compare_set(ga, gb, s, a.out)))
    rest = [n for n in changed if n not in done]
    if rest:
        print('other changed assets: ' + ', '.join(rest))
    sys.exit(1 if changed or only else 0)


if __name__ == '__main__':
    main()
