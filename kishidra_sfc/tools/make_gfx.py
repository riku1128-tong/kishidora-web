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

POSE_CODE = {'normal': 0, 'idle2': 1, 'attack': 2, 'hurt': 3, 'defeat': 4, 'special': 5, 'guard': 6}
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


HUD_FONT = {
    '0': ['.###.', '#...#', '#..##', '#.#.#', '##..#', '#...#', '.###.'],
    '1': ['..#..', '.##..', '..#..', '..#..', '..#..', '..#..', '.###.'],
    '2': ['.###.', '#...#', '....#', '..##.', '.#...', '#....', '#####'],
    '3': ['####.', '....#', '....#', '.###.', '....#', '....#', '####.'],
    '4': ['...#.', '..##.', '.#.#.', '#..#.', '#####', '...#.', '...#.'],
    '5': ['#####', '#....', '####.', '....#', '....#', '#...#', '.###.'],
    '6': ['.###.', '#....', '#....', '####.', '#...#', '#...#', '.###.'],
    '7': ['#####', '....#', '...#.', '..#..', '..#..', '..#..', '..#..'],
    '8': ['.###.', '#...#', '#...#', '.###.', '#...#', '#...#', '.###.'],
    '9': ['.###.', '#...#', '#...#', '.####', '....#', '....#', '.###.'],
    '/': ['....#', '...#.', '...#.', '..#..', '.#...', '.#...', '#....'],
    'H': ['#...#', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    'P': ['####.', '#...#', '#...#', '####.', '#....', '#....', '#....'],
    'S': ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'],
    'T': ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
}
HUD_GLYPH0 = 39


class Gfx:
    def __init__(self):
        self.wb = webbuild.get()
        self.a = Assets('gfx', GFX_BANKS)

    def img(self, path):
        return self.wb.image(path)

    def bg(self, name, im, npal=4, pal_base=2, tile_base=0, max_tiles=1024):
        data, words, pal, n, size = G.bg_image(im, npal=npal, pal_base=pal_base, tile_base=tile_base, max_tiles=max_tiles)
        if n > max_tiles:
            raise SystemExit('%s: %d tiles (max %d)' % (name, n, max_tiles))
        # backgrounds are stored raw: DMA from ROM is much faster than unpacking (ROM space is plentiful)
        self.a.add(name + '_TILES', data, compress=False)
        self.a.add(name + '_MAP', b''.join(struct.pack('<H', w) for w in words), compress=False)
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
            self.bg('BG_' + n.upper(), im.crop((0, top, 256, top + 128)), npal=3, pal_base=2, tile_base=0, max_tiles=512)
        self.a.const('BG_LIST', '"%s"' % ','.join(names))

    # ------------------------------------------------------------ sprites
    def mob_meta(self):
        if not hasattr(self, '_mh'):
            mh = json.loads(self.wb.read('assets/mobs_hd/mobs_hd.json'))
            ex = json.loads(self.wb.read('assets/mobs_hd/extra.json'))
            for k, v in ex.items():
                if k not in mh:
                    mh[k] = v
                else:
                    for p in v:
                        mh[k].setdefault(p, v[p])
            self._mh = mh
        return self._mh

    def mob_pose(self, key, pose, k):
        info = self.mob_meta()[key][pose]
        im = self.img('assets/mobs_hd/%s_%s.png' % (key, pose))
        im = G.native(im, info['w'], info['h'])
        ax, ay = info['anchor']
        sm = G.scale(im, k)
        return sm, (ax * sm.width / im.width, ay * sm.height / im.height)

    @staticmethod
    def cells_of(im, anchor, max_cells):
        """cover the opaque pixels with 32x32 cells -> [(dx, dy, image)] relative to the anchor"""
        bb = im.getbbox()
        if bb is None:
            return []
        x0, y0, x1, y1 = bb
        cols = (x1 - x0 + 31) // 32
        rows = (y1 - y0 + 31) // 32
        out = []
        for r in range(rows):
            for c in range(cols):
                box = (x0 + c * 32, y0 + r * 32, x0 + c * 32 + 32, y0 + r * 32 + 32)
                cell = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
                cell.paste(im.crop(box), (0, 0))
                if cell.getbbox():
                    out.append((round(box[0] - anchor[0]), round(box[1] - anchor[1]), cell))
        if len(out) > max_cells:
            return None
        return out

    def sprite_set(self, name, poses, max_cells, npal=1):
        """poses: [(pose_name, image, anchor)] sharing one palette -> assets + descriptor"""
        cells = []
        for pn, im, anc in poses:
            c = self.cells_of(im, anc, max_cells)
            if c is None:
                return None
            cells.append((pn, c))
        blocks = []
        for pn, cl in cells:
            for dx, dy, cim in cl:
                px = [p[:3] for p in G.snap(cim).getdata() if p[3]]
                blocks.append(px)
        pals, _ = G.fit_palettes(blocks, 1)
        pal = pals[0]
        cache = G.Cache(pal)
        desc = bytearray([len(cells)])
        for pi, (pn, cl) in enumerate(cells):
            nstrips = (len(cl) + 3) // 4
            strips = bytearray(2048 * nstrips)
            pieces = []
            for ci, (dx, dy, cim) in enumerate(cl):
                cim = G.snap(cim)
                px = cim.load()
                st, slot = ci // 4, ci % 4
                for tr in range(4):
                    for tc in range(4):
                        idx = []
                        for y in range(8):
                            for x in range(8):
                                c = px[tc * 8 + x, tr * 8 + y]
                                idx.append(0 if c[3] == 0 else 1 + cache(c[:3]))
                        t = G.tile4(idx)
                        o = st * 2048 + (tr * 16 + slot * 4 + tc) * 32
                        strips[o:o + 32] = t
                pieces.append((dx, dy, ci))
            aid = self.a.add('%s_%s' % (name, pn), strips)
            desc += struct.pack('<BHBB', POSE_CODE.get(pn, 0), aid, nstrips, len(pieces))
            for dx, dy, ci in pieces:
                desc += struct.pack('<bbB', max(-128, min(127, dx)), max(-128, min(127, dy)), ci)
        self.a.add(name + '_PAL', G.palette_bytes(pal), compress=False)
        self.a.add(name + '_DESC', bytes(desc), compress=False)
        return [pn for pn, _ in cells]

    def boss_set(self, name, poses):
        """boss poses -> BG1 tiles (palettes 6-7), map rect, anchor ; shared palettes"""
        blocks = []
        imgs = []
        for pn, im, anc in poses:
            bb = im.getbbox()
            x0, y0 = bb[0] & ~7, bb[1] & ~7
            w = (bb[2] - x0 + 7) // 8 * 8
            h = (bb[3] - y0 + 7) // 8 * 8
            c = Image.new('RGBA', (w, h), (0, 0, 0, 0))
            c.paste(im.crop((x0, y0, x0 + w, y0 + h)), (0, 0))
            c = G.snap(c)
            imgs.append((pn, c, (round(anc[0] - x0), round(anc[1] - y0))))
            px = c.load()
            for ty in range(h // 8):
                for tx in range(w // 8):
                    blocks.append([px[tx * 8 + x, ty * 8 + y][:3] for y in range(8) for x in range(8) if px[tx * 8 + x, ty * 8 + y][3]])
        pals, _ = G.fit_palettes(blocks, 2)
        caches = [G.Cache(p) for p in pals]
        desc = bytearray([len(imgs)])
        for pn, c, anc in imgs:
            W, H = c.width // 8, c.height // 8
            px = c.load()
            tiles = [tuple([0] * 64)]
            index = {tiles[0]: 0}
            words = []
            for ty in range(H):
                for tx in range(W):
                    raw = [px[tx * 8 + x, ty * 8 + y] for y in range(8) for x in range(8)]
                    best = None
                    for g in range(2):
                        e = 0
                        for p in raw:
                            if p[3]:
                                e += G._dist(p[:3], pals[g][caches[g](p[:3])])
                        if best is None or e < best[0]:
                            best = (e, g)
                    g = best[1]
                    idx = tuple(0 if p[3] == 0 else 1 + caches[g](p[:3]) for p in raw)
                    key = None
                    for hf in (0, 1):
                        for vf in (0, 1):
                            k2 = G.flipped(idx, hf, vf)
                            if k2 in index:
                                key, fl = index[k2], (hf, vf)
                                break
                        if key is not None:
                            break
                    if key is None:
                        key = len(tiles)
                        tiles.append(idx)
                        index[idx] = key
                        fl = (0, 0)
                    if key == 0:
                        words.append(0)
                    else:
                        words.append(key | ((6 + g) << 10) | (fl[0] << 14) | (fl[1] << 15))
            if len(tiles) > 256:
                raise SystemExit('%s %s: %d tiles' % (name, pn, len(tiles)))
            ta = self.a.add('%s_%s_T' % (name, pn), b''.join(G.tile4(t) for t in tiles))
            ma = self.a.add('%s_%s_M' % (name, pn), b''.join(struct.pack('<H', w) for w in words))
            desc += struct.pack('<BHHBBbbH', POSE_CODE.get(pn, 0), ta, ma, W, H, -anc[0] if anc[0] < 128 else -127, -anc[1] if anc[1] < 128 else -127, len(tiles))
        pb = b''.join(G.palette_bytes(p) for p in pals)
        self.a.add(name + '_PAL', pb, compress=False)
        self.a.add(name + '_DESC', bytes(desc), compress=False)
        return [pn for pn, _, _ in imgs]

    def monsters(self):
        import safari
        sf = safari.get()
        mh = self.mob_meta()
        rows = sf.tables['ENEMY_TABLE'] + [{'id': 'MON_KAKASHI', 'kind': 'EK_ZAKO'}]   # training dummy
        zako_poses = ['normal', 'idle2', 'attack', 'hurt', 'defeat']
        boss_poses = ['normal', 'idle2', 'attack', 'hurt', 'defeat', 'special', 'guard']
        lst = []
        for i, r in enumerate(rows):
            key = r['id'].lower()
            if key not in mh:
                print('no art', r['id'])
                lst.append('-')
                continue
            boss = r['kind'] == 'EK_BOSS'
            want = [p for p in (boss_poses if boss else zako_poses) if p in mh[key]]
            if boss:
                poses = [(p,) + self.mob_pose(key, p, SCALE) for p in want]
                self.boss_set('BOSS_%03d' % i, poses)
                lst.append('B')
            else:
                k = SCALE
                while True:
                    poses = [(p,) + self.mob_pose(key, p, k) for p in want]
                    if self.sprite_set('MON_%03d' % i, poses, 4) is not None:
                        break
                    k -= 0.03
                lst.append('Z%.2f' % k)
        self.mon_kinds = lst
        print('monsters:', ' '.join(lst))

    # ------------------------------------------------------------ hero
    def hero(self):
        wb = self.wb
        hj = json.loads(wb.read('assets/hero_v4/hero.json'))
        art = json.loads(wb.read('assets/items_v4/items.json'))
        rep = {'S': 'WP_001', 'P': 'WP_124', 'H': 'WP_070'}

        def weapon_img(wid, angle):
            a = art[wid]
            im = G.native(self.img('assets/items_v4/%s.png' % wid), a['w'], a['h'])
            gx, gy = a['grip']
            if a.get('flip'):
                im = im.transpose(Image.FLIP_LEFT_RIGHT)
                gx = a['w'] - gx
            rot = angle - a.get('aim', -45.0)
            pad = 40
            big = Image.new('RGBA', (im.width + pad * 2, im.height + pad * 2), (0, 0, 0, 0))
            big.paste(im, (pad, pad))
            big = big.rotate(-rot, resample=Image.NEAREST, center=(gx + pad, gy + pad))
            return big, (gx + pad, gy + pad)

        def compose(pose, wt):
            info = hj[pose]
            tex = info.get('tex', pose)
            body = G.native(self.img('assets/hero_v4/%s.png' % tex), info['w'], info['h'])
            ax, ay = info['anchor']
            canvas = Image.new('RGBA', (200, 200), (0, 0, 0, 0))
            ox, oy = 100 - ax, 140 - ay
            hand = info.get('hand')
            layers = [(0, body, (ox, oy))]
            if hand and wt:
                w, g = weapon_img(rep[wt], hand[2])
                z = hand[3] if len(hand) > 3 else 1
                layers.append((z, w, (round(100 + hand[0] - g[0]), round(140 + hand[1] - g[1]))))
            for z, im, at in sorted(layers, key=lambda t: t[0]):
                canvas.alpha_composite(im, at)
            sm = G.scale(canvas, SCALE)
            return sm, (100 * SCALE, 140 * SCALE)
        plain = ['idle1', 'idle2', 'blink', 'guard', 'parry', 'recoil', 'recoil_big', 'down', 'eat1', 'eat2', 'eat3',
                 'walk1', 'walk2', 'walk3', 'walk4', 'kick_ready', 'kick', 'shout_ready', 'shout']
        armed = [('sword_windup', 'S'), ('sword_swing', 'S'), ('sword_ready2', 'S'), ('sword_thrust', 'S'),
                 ('spear_ready', 'P'), ('spear_thrust', 'P'), ('hammer_up', 'H'), ('hammer_down', 'H'),
                 ('charge', 'S'), ('charge', 'P'), ('charge', 'H'), ('victory', 'S'), ('victory', 'P'), ('victory', 'H')]
        sp_wt = {'jumpgiri': 'S', 'moaclimb': 'S', 'cyclone': 'S', 'sworddance': 'S', 'kabutowari': 'H', 'shouryuu': 'H',
                 'megaroll': 'H', 'heavybomb': 'H', 'kantsuu': 'P', 'skydive': 'P', 'senpuu': 'P', 'multilance': 'P'}
        poses = []
        for p in plain:
            poses.append((p,) + compose(p, None))
        for p, wt in armed:
            poses.append(('%s_%s' % (p, wt),) + compose(p, wt))
        for sp, wt in sp_wt.items():
            for k in range(1, 9):
                p = 'sp_%s_%d' % (sp, k)
                if p in hj:
                    poses.append((p,) + compose(p, wt))
        names = self.sprite_set('HERO', poses, 8)
        if names is None:
            raise SystemExit('hero pose too large')
        h = ['/* hero pose numbers (order of HERO_DESC) */']
        for i, n in enumerate(names):
            h.append('#define HP_%s %d' % (n.upper(), i))
        h.append('#define N_HERO_POSE %d' % len(names))
        # shield position per pose (scaled, relative to the anchor), front/back
        sh = []
        for n in names:
            base = n
            for suf in ('_S', '_P', '_H'):
                if base.endswith(suf) and base[:-2] in hj:
                    base = base[:-2]
            v = hj.get(base, {}).get('shield', [12, -22, 1])
            sh.append('{ %d, %d, %d }' % (round(v[0] * SCALE), round(v[1] * SCALE), 1 if v[2] > 0 else 0))
        h.append('static const signed char hero_shield_pos[N_HERO_POSE][3] = { %s };' % ', '.join(sh))
        self.hero_h = h
        print('hero poses:', len(names))

    def shields_icons(self):
        """16x16 item icons (4 shared palettes), shield sprites for the battle hero"""
        import safari
        sf = safari.get()
        sys.path.insert(0, os.path.join(HERE, 'data'))
        import make_data as MD
        g = MD.Gen.__new__(MD.Gen)
        g.s = sf
        g.T = sf.tuning
        weapons = [k for k, w in sf.weapons.items() if w['atk'] > 1 and w['rarity'] in MD.RARITIES]
        shields = [k for k in sf.shields if sf.shields[k]['rarity'] in MD.RARITIES]
        ids = weapons + shields + [sf.num('FUUIN_ID')]
        mats = [r['item'] for r in sf.tables['MAT_TABLE'] if r['item']]
        icons = []
        art = json.loads(self.wb.read('assets/items_v4/items.json'))
        for k in ids + ['MAT:' + m for m in MD.MATS]:
            im = None
            src = k
            if k.startswith('MAT:'):
                m = k[4:]
                row = next((r for r in sf.tables['MAT_TABLE'] if r['id'] == m), None)
                src = row['item'] if row and row['item'] else ('WP_215' if m == 'MAT_HKYOUKA' else ('WP_216' if m.startswith('IT_') else 'WP_214'))
            path = 'assets/icons/%s.png' % src
            if self.wb.has(path + '.import'):
                im = self.img(path)
            elif src in art:
                a = art[src]
                im = G.scale(G.native(self.img('assets/items_v4/%s.png' % src), a['w'], a['h']), 16.0 / max(a['w'], a['h']))
            else:
                im = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
            c = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
            c.paste(im, ((16 - im.width) // 2, (16 - im.height) // 2))
            if k.startswith('MAT:') and k[4:] == 'MAT_HKYOUKA':
                c = c.point(lambda v: v)  # same art, recoloured below
                px = c.load()
                for y in range(16):
                    for x in range(16):
                        r, gg, b, a = px[x, y]
                        if a:
                            px[x, y] = (min(255, b + 40), gg, r, a)
            icons.append(G.snap(c))
        blocks = [[p[:3] for p in im.getdata() if p[3]] for im in icons]
        pals, assign = G.fit_palettes(blocks, 4)
        caches = [G.Cache(p) for p in pals]
        data = bytearray()
        palsel = bytearray()
        for im, g_ in zip(icons, assign):
            px = im.load()
            for ty in (0, 1):
                for tx in (0, 1):
                    idx = [0 if px[tx * 8 + x, ty * 8 + y][3] == 0 else 1 + caches[g_](px[tx * 8 + x, ty * 8 + y][:3]) for y in range(8) for x in range(8)]
                    data += G.tile4(idx)
            palsel.append(g_)
        self.a.add('ICONS16', bytes(data))
        self.a.add('ICONS16_PAL', b''.join(G.palette_bytes(p) for p in pals), compress=False)
        self.a.add('ICONS16_SEL', bytes(palsel), compress=False)
        self.a.const('ICON_MAT0', len(ids))
        # shield sprites for the hero (one palette each)
        for i, k in enumerate(shields):
            a = art[k]
            im = G.scale(G.native(self.img('assets/items_v4/%s.png' % k), a['w'], a['h']), SCALE)
            c = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
            c.paste(im, ((16 - im.width) // 2, (16 - im.height) // 2))
            c = G.snap(c)
            pal = G.make_palette([p[:3] for p in c.getdata() if p[3]], 15) or [(0, 0, 0)]
            cache = G.Cache(pal)
            px = c.load()
            d = bytearray()
            for ty in (0, 1):
                for tx in (0, 1):
                    d += G.tile4([0 if px[tx * 8 + x, ty * 8 + y][3] == 0 else 1 + cache(px[tx * 8 + x, ty * 8 + y][:3]) for y in range(8) for x in range(8)])
            self.a.add('SHIELD_%02d' % i, bytes(d), compress=False)
            self.a.add('SHIELD_%02d_PAL' % i, G.palette_bytes(pal), compress=False)
        self.a.const('N_SHIELD_GFX', len(shields))

    def skill_icons(self):
        import safari
        sf = safari.get()
        icons = []
        for r in sf.tables['SKILL_TABLE']:
            p = 'assets/skills/%s.png' % r['id']
            im = self.img(p) if self.wb.has(p + '.import') else Image.new('RGBA', (16, 16))
            sm = im.resize((16, 16), Image.LANCZOS)
            a = sm.split()[3].point(lambda v: 255 if v > 100 else 0)
            sm.putalpha(a)
            icons.append(G.snap(sm))
        blocks = [[p[:3] for p in im.getdata() if p[3]] for im in icons]
        pals, assign = G.fit_palettes(blocks, 2)
        caches = [G.Cache(p) for p in pals]
        data = bytearray()
        for im, g_ in zip(icons, assign):
            px = im.load()
            for ty in (0, 1):
                for tx in (0, 1):
                    data += G.tile4([0 if px[tx * 8 + x, ty * 8 + y][3] == 0 else 1 + caches[g_](px[tx * 8 + x, ty * 8 + y][:3]) for y in range(8) for x in range(8)])
        self.a.add('SKILL_ICONS', bytes(data))
        self.a.add('SKILL_ICONS_PAL', b''.join(G.palette_bytes(p) for p in pals), compress=False)
        self.a.add('SKILL_ICONS_SEL', bytes(assign), compress=False)

    def fx(self):
        """battle effect sprites (strip 7): digits, bubbles, cursor, sparks ; palette = FX_PAL"""
        from PIL import ImageFont
        ttf = os.path.join(ROOT, 'build', 'mplus.ttf')
        if not os.path.exists(ttf):
            webbuild.font_ttf(self.wb, ttf)
        font = ImageFont.truetype(ttf, 14)
        pal = [(16, 8, 8), (248, 248, 248), (248, 224, 64), (248, 128, 40), (248, 64, 64), (120, 200, 248), (96, 224, 96),
               (160, 160, 176), (88, 56, 40), (248, 176, 72), (40, 40, 56), (200, 120, 248), (255, 240, 180), (64, 128, 232), (232, 232, 120)]
        sheet = Image.new('RGBA', (128, 32), (0, 0, 0, 0))     # one strip = 128x32
        d = ImageDraw.Draw(sheet)
        d.fontmode = '1'
        # 0..9 white digits (8x16) in row 0 cols 0-9, yellow digits row 2 (y=16) cols 0-9 -> tiles r*16+c
        for i in range(10):
            for col, y0 in ((pal[1], 0), (pal[2], 16)):
                g = Image.new('RGBA', (8, 16), (0, 0, 0, 0))
                gd = ImageDraw.Draw(g)
                gd.fontmode = '1'
                for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1), (1, 1)):
                    gd.text((ox, -2 + oy), str(i), font=font, fill=pal[0] + (255,))
                gd.text((0, -2), str(i), font=font, fill=col + (255,))
                sheet.alpha_composite(g, (i * 8, y0))
        # count bubble digits 1..9 (8x8 orange box) at row 0/1 cols 10..15 and row 2/3 cols 10..15
        small = ImageFont.truetype(ttf, 9)
        for i in range(10):
            g = Image.new('RGBA', (8, 8), (0, 0, 0, 0))
            gd = ImageDraw.Draw(g)
            gd.fontmode = '1'
            gd.rectangle((0, 0, 7, 7), fill=pal[3] + (255,), outline=pal[0] + (255,))
            gd.text((2, -2), str(i), font=small, fill=pal[1] + (255,))
            x, y = (10 + i % 6) * 8, (i // 6) * 8
            sheet.alpha_composite(g, (x, y))
        # mini HP bar tiles 0..8 px (row 1 col 14,15 + row 3 cols 10..15 ...) -> place at row 3 (y=24) cols 0..8
        for i in range(9):
            g = Image.new('RGBA', (8, 8), (0, 0, 0, 0))
            gd = ImageDraw.Draw(g)
            gd.rectangle((0, 2, 7, 5), fill=pal[10] + (255,))
            if i:
                gd.rectangle((0, 3, i - 1, 4), fill=pal[6] + (255,))
            sheet.alpha_composite(g, (i * 8, 24))
        # target arrow (row 3 col 9), stun star (col 10), poison (11), paralyze (12), blind (13), coin (14), plus (15)
        g = Image.new('RGBA', (8, 8), (0, 0, 0, 0)); gd = ImageDraw.Draw(g)
        gd.polygon([(0, 1), (7, 1), (3, 6)], fill=pal[2] + (255,), outline=pal[0] + (255,)); sheet.alpha_composite(g, (72, 24))
        g = Image.new('RGBA', (8, 8), (0, 0, 0, 0)); gd = ImageDraw.Draw(g)
        gd.polygon([(3, 0), (4, 3), (7, 3), (5, 5), (6, 7), (3, 6), (1, 7), (2, 5), (0, 3), (3, 3)], fill=pal[2] + (255,)); sheet.alpha_composite(g, (80, 24))
        g = Image.new('RGBA', (8, 8), (0, 0, 0, 0)); gd = ImageDraw.Draw(g)
        gd.ellipse((1, 2, 6, 7), fill=pal[11] + (255,)); gd.polygon([(3, 0), (1, 4), (6, 4)], fill=pal[11] + (255,)); sheet.alpha_composite(g, (88, 24))
        g = Image.new('RGBA', (8, 8), (0, 0, 0, 0)); gd = ImageDraw.Draw(g)
        gd.polygon([(4, 0), (1, 4), (3, 4), (2, 7), (6, 3), (4, 3), (5, 0)], fill=pal[14] + (255,)); sheet.alpha_composite(g, (96, 24))
        g = Image.new('RGBA', (8, 8), (0, 0, 0, 0)); gd = ImageDraw.Draw(g)
        gd.ellipse((0, 2, 7, 6), fill=pal[10] + (255,), outline=pal[7] + (255,)); gd.point((3, 4), fill=pal[1] + (255,)); sheet.alpha_composite(g, (104, 24))
        g = Image.new('RGBA', (8, 8), (0, 0, 0, 0)); gd = ImageDraw.Draw(g)
        gd.ellipse((0, 0, 7, 7), fill=pal[9] + (255,), outline=pal[8] + (255,)); gd.line((3, 2, 3, 5), fill=pal[12] + (255,)); sheet.alpha_composite(g, (112, 24))
        sheet = G.snap(sheet)
        cache = G.Cache(pal)
        px = sheet.load()
        data = bytearray()
        for tr in range(4):
            for tc in range(16):
                data += G.tile4([0 if px[tc * 8 + x, tr * 8 + y][3] == 0 else 1 + cache(px[tc * 8 + x, tr * 8 + y][:3]) for y in range(8) for x in range(8)])
        self.a.add('FX_STRIP', bytes(data))
        self.a.add('FX_PAL', G.palette_bytes(pal), compress=False)
        os.makedirs(PREVIEW, exist_ok=True)
        sheet.resize((512, 128), Image.NEAREST).save(os.path.join(PREVIEW, 'fx.png'))
        # spark / slash effects: second strip (4 cells of 32x32)
        cells = Image.new('RGBA', (128, 32), (0, 0, 0, 0))
        cd = ImageDraw.Draw(cells)
        import math
        for k in range(8):   # cell 0: star burst
            a = k * math.pi / 4
            r = 15 if k % 2 == 0 else 9
            cd.line((16, 16, 16 + r * math.cos(a), 16 + r * math.sin(a)), fill=pal[12] + (255,), width=2)
        cd.ellipse((12, 12, 20, 20), fill=pal[1] + (255,))
        cd.arc((36, 2, 92 - 30, 58 - 26), 200, 340, fill=pal[1] + (255,), width=3)       # cell 1: slash arc
        cd.arc((38, 4, 60, 26), 200, 340, fill=pal[5] + (255,), width=1)
        cd.polygon([(66, 14), (94, 16), (66, 18)], fill=pal[1] + (255,))                  # cell 2: thrust
        cd.line((64, 16, 95, 16), fill=pal[5] + (255,), width=1)
        for k in range(10):                                                              # cell 3: impact
            a = k * math.pi / 5
            cd.line((112, 16, 112 + 14 * math.cos(a), 16 + 14 * math.sin(a)), fill=pal[3] + (255,), width=2)
        cd.ellipse((106, 10, 118, 22), fill=pal[2] + (255,))
        cells = G.snap(cells)
        px = cells.load()
        data = bytearray(2048)
        for slot in range(4):
            for tr in range(4):
                for tc in range(4):
                    t = G.tile4([0 if px[slot * 32 + tc * 8 + x, tr * 8 + y][3] == 0 else 1 + cache(px[slot * 32 + tc * 8 + x, tr * 8 + y][:3]) for y in range(8) for x in range(8)])
                    o = (tr * 16 + slot * 4 + tc) * 32
                    data[o:o + 32] = t
        self.a.add('FX_CELLS', bytes(data))
        cells.resize((512, 128), Image.NEAREST).save(os.path.join(PREVIEW, 'fx_cells.png'))

    def hud(self):
        """BG1 tiles for the HP/ST bars (palette 5)"""
        pal = [(8, 8, 12), (40, 32, 24), (72, 200, 72), (232, 208, 64), (232, 72, 56), (72, 144, 240), (216, 168, 64),
               (24, 24, 32), (248, 248, 248)]
        tiles = []

        def tile(fn):
            im = Image.new('RGBA', (8, 8), (0, 0, 0, 0))
            fn(ImageDraw.Draw(im))
            tiles.append(im)
        # 0 empty, 1-9 HP bar fill 0..8 (green), 10-18 yellow, 19-27 red, 28-36 ST blue, 37 bar left cap, 38 right cap
        tile(lambda d: None)
        for colour in (pal[2], pal[3], pal[4], pal[5]):
            for i in range(9):
                def f(d, i=i, colour=colour):
                    d.rectangle((0, 1, 7, 6), fill=pal[7] + (255,))
                    d.line((0, 0, 7, 0), fill=pal[6] + (255,))
                    d.line((0, 7, 7, 7), fill=pal[6] + (255,))
                    if i:
                        d.rectangle((0, 2, i - 1, 5), fill=colour + (255,))
                        d.line((0, 2, i - 1, 2), fill=pal[8] + (255,))
                tile(f)
        tile(lambda d: (d.rectangle((6, 0, 7, 7), fill=pal[6] + (255,))))
        tile(lambda d: (d.rectangle((0, 0, 1, 7), fill=pal[6] + (255,))))
        # 39.. small HUD glyphs (5x7, white with dark shadow): 0-9 / H P S T
        for ch in '0123456789/HPST':
            rows = HUD_FONT[ch]
            def f(d, rows=rows):
                for y, r in enumerate(rows):
                    for x, c in enumerate(r):
                        if c == '#':
                            d.point((x + 2, y + 1), fill=pal[7] + (255,))
                            d.point((x + 1, y + 1), fill=pal[7] + (255,))
                            d.point((x + 1, y + 2), fill=pal[7] + (255,))
                for y, r in enumerate(rows):
                    for x, c in enumerate(r):
                        if c == '#':
                            d.point((x + 1, y), fill=pal[8] + (255,))
            tile(f)
        cache = G.Cache(pal)
        data = bytearray()
        for im in tiles:
            im = G.snap(im)
            px = im.load()
            data += G.tile4([0 if px[x, y][3] == 0 else 1 + cache(px[x, y][:3]) for y in range(8) for x in range(8)])
        self.a.add('HUD_TILES', bytes(data))
        self.a.const('HUD_GLYPH0', HUD_GLYPH0)
        self.a.const('HUD_NTILES', len(tiles))
        self.a.add('HUD_PAL', G.palette_bytes(pal), compress=False)

    def menu_obj(self):
        """menu / map sprites (OBJ $6000): tiles 0-63 icons (palette MENU_OBJ_PAL), 64.. king chibi (32x32 cells, KING_PAL)"""
        pal = [(16, 8, 8), (248, 248, 248), (248, 216, 64), (232, 64, 48), (160, 160, 176), (88, 56, 40), (120, 200, 248),
               (96, 224, 96), (40, 32, 48), (255, 240, 180), (200, 120, 248), (248, 160, 40)]
        sheet = Image.new('RGBA', (128, 32), (0, 0, 0, 0))
        def tile(col, row, fn):
            g = Image.new('RGBA', (8, 8), (0, 0, 0, 0))
            fn(ImageDraw.Draw(g))
            sheet.alpha_composite(g, (col * 8, row * 8))
        c = lambda i: pal[i] + (255,)
        # 0 cursor arrow (right), 1 cursor arrow (down), 2 path dot, 3 new star
        tile(0, 0, lambda d: d.polygon([(1, 0), (6, 4), (1, 7)], fill=c(2), outline=c(0)))
        tile(1, 0, lambda d: d.polygon([(0, 1), (7, 1), (4, 6)], fill=c(2), outline=c(0)))
        tile(2, 0, lambda d: (d.rectangle((2, 2, 5, 5), fill=c(0)), d.rectangle((3, 3, 4, 4), fill=c(9))))
        tile(3, 0, lambda d: d.polygon([(3, 0), (4, 3), (7, 3), (5, 5), (6, 7), (3, 6), (1, 7), (2, 5), (0, 3), (3, 3)], fill=c(2), outline=c(0)))
        # 4 marker: new (red), 5 open (white), 6 cleared (yellow), 7 tower (purple)
        for i, col in enumerate((3, 1, 2, 10)):
            tile(4 + i, 0, lambda d, col=col: (d.ellipse((0, 0, 7, 7), fill=c(0)), d.ellipse((1, 1, 6, 6), fill=c(col)), d.ellipse((3, 3, 4, 4), fill=c(0))))
        # 8-9 / 24-25: 16x16 selection ring (yellow)
        g = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
        d = ImageDraw.Draw(g)
        d.ellipse((0, 0, 15, 15), outline=c(0), width=3)
        d.ellipse((1, 1, 14, 14), outline=c(2), width=1)
        sheet.alpha_composite(g, (64, 0))
        # 10 hand-shaped down pointer is the down arrow; 11 coin
        tile(11, 0, lambda d: (d.ellipse((0, 0, 7, 7), fill=c(11), outline=c(5)), d.line((3, 2, 3, 5), fill=c(9))))
        sheet = G.snap(sheet)
        cache = G.Cache(pal)
        px = sheet.load()
        data = bytearray()
        for tr in range(4):
            for tc in range(16):
                data += G.tile4([0 if px[tc * 8 + x, tr * 8 + y][3] == 0 else 1 + cache(px[tc * 8 + x, tr * 8 + y][:3]) for y in range(8) for x in range(8)])
        self.a.add('MENU_OBJ', bytes(data))
        self.a.add('MENU_OBJ_PAL', G.palette_bytes(pal), compress=False)
        # king chibi: 64x96 -> 2x3 cells of 32x32 in a 16-tile wide block (tiles 64..255)
        im = self.img('assets/story/king_chibi.png')
        w = 64
        h = round(im.height * w / im.width)
        im = im.resize((w, h), Image.LANCZOS)
        h = min(96, h)
        canvas = Image.new('RGBA', (64, 96), (0, 0, 0, 0))
        canvas.alpha_composite(im.crop((0, 0, 64, h)), (0, 96 - h))
        canvas = G.snap(canvas)
        cp = canvas.load()
        cols = [cp[x, y][:3] for y in range(96) for x in range(64) if cp[x, y][3]]
        kp = G.make_palette(cols, 15)
        kc = G.Cache(kp)
        block = Image.new('RGBA', (128, 96), (0, 0, 0, 0))
        block.alpha_composite(canvas, (0, 0))
        bp = block.load()
        kd = bytearray()
        for tr in range(12):
            for tc in range(16):
                kd += G.tile4([0 if bp[tc * 8 + x, tr * 8 + y][3] == 0 else 1 + kc(bp[tc * 8 + x, tr * 8 + y][:3]) for y in range(8) for x in range(8)])
        self.a.add('KING_OBJ', bytes(kd))
        self.a.add('KING_PAL', G.palette_bytes(kp), compress=False)
        os.makedirs(PREVIEW, exist_ok=True)
        canvas.save(os.path.join(PREVIEW, 'king.png'))

    def mon_table(self):
        """gfx.h: per monster index -> descriptor / palette asset, is boss"""
        import re
        names = {n: i for i, (n, b, f) in enumerate(self.a.items)}
        rows = []
        for i, k in enumerate(self.mon_kinds):
            if k == '-':
                rows.append((0xffff, 0xffff, 0))
            elif k == 'B':
                rows.append((names['BOSS_%03d_DESC' % i], names['BOSS_%03d_PAL' % i], 1))
            else:
                rows.append((names['MON_%03d_DESC' % i], names['MON_%03d_PAL' % i], 0))
        self.mon_rows = rows

    def write(self):
        self.mon_table()
        self.a.write()
        h = ['/* generated by tools/make_gfx.py */', '#ifndef GFXTAB_H', '#define GFXTAB_H']
        h += self.hero_h
        h.append('static const unsigned short mon_gfx[][3] = { %s };' % ', '.join('{ %d, %d, %d }' % r for r in self.mon_rows))
        bgs = self.bg_names
        names = {n: i for i, (n, b, f) in enumerate(self.a.items)}
        T = webbuild.script_constants(self.wb, 'scripts/tuning.gdc')
        import safari
        sf = safari.get()
        area_ids = [r['id'] for r in sf.tables['AREA_TABLE']] + [T['MIRAGE']['surface']['id']]
        row = []
        for a in area_ids:
            n = T['AREA_BG'].get(a, T['MIRAGE']['surface']['bg'] if a == T['MIRAGE']['surface']['id'] else 'field')
            row.append(str(names['BG_%s_TILES' % n.upper()]))
        h.append('static const unsigned short area_bg[] = { %s };' % ', '.join(row))
        h.append('#define BG_CASTLE_FINAL %d' % names['BG_%s_TILES' % T['CASTLE_FINAL_BG'].upper()])
        h.append('#endif')
        open(os.path.join(ROOT, 'data', 'gen', 'gfxtab.h'), 'w').write('\n'.join(h) + '\n')


def main():
    g = Gfx()
    g.title()
    g.worldmap()
    g.battle_bgs()
    g.monsters()
    g.hero()
    g.shields_icons()
    g.skill_icons()
    g.fx()
    g.hud()
    g.menu_obj()
    g.write()


if __name__ == '__main__':
    main()
