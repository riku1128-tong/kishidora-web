#!/usr/bin/env python3
"""Tests of the shared loadout rules and the RNG (plan step S7): src/c/loadout.c (sp_reach, solo_ok_sk /
the INV_SOLO cache, fx_state), src/asm/loadout.s (the solo table), src/asm/math.s (rnd, rnd_mul8,
rnd_below, mul_now, div_now), against the Python models of tools/data/loadout_model.py and the web rules.

usage: loadtest.py [-o OUTDIR] [--python-only] [ROM]   (an XDEFS=TEST_HOOKS ROM, default build/kishidra.sfc)

Python (no ROM):
  reach   snes_reach (the algorithm of sp_reach) == web_reach (adventure.gd reachable_specials) on
          8,500 loadouts (the generators of the menus group's reach_sim.py: random, built from special
          sequences, mixed)
  solo    the solo table == the web BFS on all 120,100 lists of 1-3 distinct skills and all 127,550 with
          repeats, and == (snes_reach of that one weapon != 0); kick-only == battle_core.gd kick_only
ROM (sys.c test commands DC_SOLO .. DC_POOL, records in ovl):
  solo    every list that fires a special (1,691) + 400 others + kick lists: solo_ok_sk (fires, kick-only),
          sp_reach(nw = 1) (its specials) and the INV_SOLO bit inv_add gives a sword / a weapon / a shield
          with that list == the models
  reach   5 hand-made loadouts (the menus group's expected lists) + 300 random ones: sp_reach == web_reach,
          the most frames of one call (<= 10); sp_reach_equipped on saves that equip the 5 loadouts
  fx      fx_state_of for the 86 effects x 4 slots x 6 loadouts + fx_state (the items' own effects):
          state, note text, target slots == desc.gd fx_state (loadout_model.fx_state)
  rnd     500 rnd(), rnd_range of 6 ranges (both paths), rnd_below, rnd_pct == the C of before S7
  math    mul_now, div_now, pct_now, rnd_mul8 on 100 random arguments each == Python
  pool    solo_pool_id (the 必殺10連 pool in the ROM blob) == the web solo_gacha_cands per rarity
  bag     a v1 save of 999 items: after the load every INV_SOLO bit == the model, ST_SOLO set, the
          save written back (seq 2 in slot B); inv_solo_all on that bag (DC_SOLOALL) <= 10 frames
Exit status 0 when every check passes."""
import argparse
import itertools
import os
import random
import re
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'data'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'extract'))
import hrun  # noqa: E402
import mksave  # noqa: E402
import safari  # noqa: E402
import loadout_model as LM  # noqa: E402
import textwidth  # noqa: E402

fails = []
DBG_IO = 512
DC_SOLO, DC_REACH, DC_REACH_EQ, DC_FX, DC_SOLOALL, DC_RND, DC_MATH, DC_POOL = 8, 9, 10, 11, 12, 13, 14, 15
ST_SOLO, INV_SOLO = 8, 0x40
STORY = 107                  # offset of sv.story (save.h)


def check(ok, what):
    print('  %s %s' % ('ok  ' if ok else 'FAIL', what))
    if not ok:
        fails.append(what)
    return ok


class Ctx:
    def __init__(self):
        import make_data
        self.s = safari.get()
        self.m = LM.Model(self.s)
        self.st = LM.SoloTable(self.m)
        g = make_data.Gen()
        g.build_ids()
        self.item_ids, self.skill_ids, self.effect_ids = g.item_ids, g.skill_ids, g.effect_ids
        assert self.skill_ids == self.m.skill_ids
        self.n_sk = len(self.skill_ids)
        s = self.s
        self.weapons = [i for i, k in enumerate(self.item_ids) if k in s.weapons]
        self.shields = [i for i, k in enumerate(self.item_ids) if k in s.shields and k not in s.weapons]
        self.bytype = {}
        for i, k in enumerate(self.skill_ids):
            self.bytype.setdefault(s.skills[k]['wtype'], []).append(i)


# ---------------------------------------------------------------- Python checks
def gen_loadouts(c, n_rand, n_target, n_mixed, seed):
    """reach_sim.py's three generators (port skill indexes)"""
    sps = c.m.sps
    pool = {t: c.bytype[t] + c.bytype.get('SKT_SPECIAL', []) for t in ('WT_SWORD', 'WT_SPEAR', 'WT_HAMMER')}
    r = random.Random(seed)
    out = []
    for _ in range(n_rand):
        out.append([[r.choice(pool[r.choice(list(pool))]) for _ in range(r.randint(1, 3))] for _ in range(3)])
    for _ in range(n_target):
        ws = [[], [], []]
        for _ in range(3):
            for x in r.choice(sps)[1]:
                ws[r.randrange(3)].append(x)
        out.append([w[:3] for w in ws if w])
    for t in range(n_mixed):
        if t % 2:
            out.append([[r.choice(pool[r.choice(list(pool))]) for _ in range(r.randint(1, 3))] for _ in range(r.randint(1, 3))])
        else:
            ws = [[], [], []]
            for _ in range(4):
                for x in r.choice(sps)[1]:
                    ws[r.randrange(3)].append(x)
            out.append([w[:3] for w in ws if w])
    return out


def python_checks(c):
    print('python: sp_reach algorithm vs the web BFS')
    los = gen_loadouts(c, 3000, 1500, 4000, 1)
    bad = 0
    mx = {'prefixes': 0, 'states': 0, 'sweeps': 0}
    for ws in los:
        st = {}
        if c.m.snes_reach(ws, st) != c.m.web_reach(ws):
            bad += 1
        for k in mx:
            mx[k] = max(mx[k], st.get(k, 0))
    check(bad == 0, '%d loadouts: snes_reach == web_reach (%d differ; most prefixes %d, states %d, sweeps %d)' % (
        len(los), bad, mx['prefixes'], mx['states'], mx['sweeps']))
    print('python: the one-weapon table')
    bad = n = 0
    for k in (1, 2, 3):
        for l in itertools.permutations(range(c.n_sk), k):
            n += 1
            if c.st.lookup(l)[0] != bool(c.m.web_reach([list(l)])):
                bad += 1
    check(bad == 0 and n == 120100, '%d ordered lists of 1-3 distinct skills: table == web BFS (%d differ)' % (n, bad))
    bad = bad2 = badk = n = pos = 0
    for k in (1, 2, 3):
        for l in itertools.product(range(c.n_sk), repeat=k):
            n += 1
            ref = c.m.web_reach([list(l)])
            ok, kick = c.st.lookup(l)
            pos += bool(ref)
            bad += ok != bool(ref)
            bad2 += ok != bool(c.m.snes_reach([list(l)]))
            badk += kick != c.m.kick_only(l)
    check(bad == 0 and n == 127550, '%d lists with repeats: table == web BFS (%d fire a special, %d differ)' % (n, pos, bad))
    check(bad2 == 0, 'solo_ok_sk == (sp_reach(nw=1) != 0) on the %d lists (%d differ)' % (n, bad2))
    check(badk == 0, 'kick-only == kick_only on the %d lists (%d differ)' % (n, badk))


# ---------------------------------------------------------------- ROM checks
class Script:
    """one runrom script: batches of records through the test commands"""
    def __init__(self):
        self.lines = []
        self.seq = 0
        self.outs = []

    def poke_bytes(self, sym, off, data, w=1):
        vals = list(data)
        cmd = {1: 'poke', 2: 'pokew', 4: 'pokel'}[w]
        for k in range(0, len(vals), 48):
            self.lines.append('%s %s+%d %s' % (cmd, sym, off + k * w, ' '.join(str(v) for v in vals[k:k + 48])))

    def cmd(self, c, args, wait=3000):
        self.seq += 1
        self.lines.append('pokew dbg_arg %s' % ' '.join(str(a & 0xffff) for a in (list(args) + [0, 0, 0, 0])[:4]))
        self.lines.append('poke dbg_cmd %d' % c)
        self.lines.append('waitfor dbg_seq %d %d' % (self.seq & 0xff, wait))
        self.lines.append('peek dbg_ack')
        self.lines.append('peek dbg_res 8')

    def dump(self, name, addr, n):
        self.lines.append('wram OUT/%s %s %d' % (name, addr, n))
        self.outs.append(name)


def parse_peeks(stdout):
    res = []
    for line in stdout.splitlines():
        m = re.match(r'peek (\S+) [0-9a-f]+: ((?:[0-9a-f]{2} ?)+)', line)
        if m:
            res.append((m.group(1), [int(x, 16) for x in m.group(2).split()]))
    return res


def u16s(b):
    return [b[i] | b[i + 1] << 8 for i in range(0, len(b) - 1, 2)]


def to_map(srm, out):
    """boot, title, つづきから (the map)"""
    return ['loadsram %s' % srm, 'wait 200', 'press A', 'wait 150', 'wram %s/after_load.bin' % out,
            'sram %s/after_load.srm' % out]


def solo_lists(c):
    r = random.Random(3)
    pos, neg = [], []
    for k in (1, 2, 3):
        for l in itertools.product(range(c.n_sk), repeat=k):
            (pos if c.st.lookup(l)[0] else neg).append(list(l))
    lists = pos + r.sample(neg, 400)
    K, S = c.m.kick, c.bytype
    buf = [i for i in range(c.n_sk) if c.m.hits[i] == 0]
    lists += [[K], [K, buf[0]], [buf[1], K, buf[2]], [K, K], [K, K, K], [K, 0], [buf[0]], [buf[0], buf[1]]]
    return [(l + [255, 255, 255])[:3] for l in lists]


def menus_loadouts(c):
    """five hand-made loadouts (3 weapons, distinct skills each) whose web result is the list of the menus
    spec §3 test 1: [0,1,2,11], [3], [8,9,10,11], [4,6,7,12], [0,1,3]"""
    ix = c.m.sk_index
    L = lambda *names: [ix['SK_' + n] for n in names]  # noqa: E731
    return [
        ([L('KIRIAGE', 'SLASH', 'FENCING'), L('RENGEKI2', 'MEGABREAK'), L('TSUKI', 'NIDANZUKI')], [0, 1, 2, 11]),
        ([L('SLASH', 'RENGEKI2', 'RENGEKI3'), L('SEISHIN', 'TSUKI'), L('TATAKI', 'JIWARE')], [3]),
        ([L('TSUKI', 'JAVELIN', 'TOSSHIN'), L('YARIBARAI', 'MEGATHRUST'), L('NIDANZUKI', 'TSUKI')], [8, 9, 10, 11]),
        ([L('UPPER', 'TATAKI', 'KICK'), L('JIWARE', 'MEGAHIT', 'GIGAHIT'), L('KICK', 'POWERTAME')], [4, 6, 7, 12]),
        ([L('KIRIAGE', 'SLASH'), L('FENCING', 'RENGEKI2', 'RENGEKI3'), L('BERSERK')], [0, 1, 3]),
    ]


def fx_loadouts(c):
    """six loadouts for the effect states: (MAIN, SUB, THIRD, shield) as (item, [skills]) or None"""
    s, iid = c.s, c.item_ids
    W = lambda i: s.weapons[iid[i]]  # noqa: E731
    def pick(pred, k=0):
        got = [i for i in c.weapons if pred(W(i))]
        return got[k % len(got)]
    def sh(pred, k=0):
        got = [i for i in c.shields if pred(s.shields[iid[i]])]
        return got[k % len(got)]
    ix = c.m.sk_index
    ber = ix['SK_BERSERK']
    sr = lambda r: r in ('RARE_SR', 'RARE_SRP')  # noqa: E731
    r = random.Random(5)
    rnd_w = lambda: (r.choice(c.weapons), [r.randrange(c.n_sk) for _ in range(r.randint(0, 3))])  # noqa: E731
    return [
        [None, None, None, None],
        [(pick(lambda w: w['element'] == 'EL_FIRE' and w['wtype'] == 'WT_SWORD' and sr(w['rarity'])), [0, 1]),
         (pick(lambda w: w['element'] == 'EL_FIRE' and w['wtype'] == 'WT_SPEAR' and sr(w['rarity'])), [24]),
         (pick(lambda w: w['element'] == 'EL_WATER' and w['wtype'] == 'WT_HAMMER' and sr(w['rarity'])), [12, 13]),
         (sh(lambda x: sr(x['rarity'])), [])],
        [(pick(lambda w: w['element'] == 'EL_NONE' and w['rarity'] == 'RARE_N'), [0]), None,
         (pick(lambda w: w['element'] == 'EL_WOOD' and w['wtype'] == 'WT_SPEAR'), [24, ber]),
         (sh(lambda x: x['rarity'] == 'RARE_N'), [])],
        [(pick(lambda w: w['element'] == 'EL_LIGHT' and w['wtype'] == 'WT_SWORD'), [2, 3, 4]),
         (pick(lambda w: w['element'] == 'EL_DARK' and w['wtype'] == 'WT_SPEAR'), [ber, 25]),
         (pick(lambda w: w['element'] == 'EL_NONE' and w['wtype'] == 'WT_HAMMER'), [14]),
         (sh(lambda x: x['rarity'] == 'RARE_SRP'), [])],
        [None, (pick(lambda w: w['element'] == 'EL_WATER'), [1]), (pick(lambda w: w['element'] == 'EL_WOOD'), [26]),
         (sh(lambda x: x['rarity'] == 'RARE_R'), [])],
        [rnd_w(), rnd_w(), rnd_w(), (r.choice(c.shields), [])],
    ]


def web_lo(c, lo):
    s, iid, sk = c.s, c.item_ids, c.skill_ids
    ws = [(iid[w[0]], [sk[x] for x in w[1]]) if w else None for w in lo[:3]]
    return {'weapons': ws, 'shield': iid[lo[3][0]] if lo[3] else None}


def rec_lo(lo):
    b = []
    for w in lo:
        if w is None:
            b += [255, 255, 255, 255]
        else:
            b += [w[0]] + (list(w[1]) + [255, 255, 255])[:3]
    return b


def xs(v):
    v ^= (v << 7) & 0xffff
    v ^= v >> 9
    v ^= (v << 8) & 0xffff
    return v


def rnd_model(state, n, mode, a, b):
    out = []
    for _ in range(n):
        if mode == 0:
            state = xs(state)
            v = state
        elif mode in (1, 4):
            lo, hi = (a & 255, b) if mode == 1 else (0, b)
            if hi <= lo:
                v = lo
            else:
                state = xs(state)
                v = lo + ((state * ((hi - lo + 1) & 0xffff)) >> 16)
        elif mode == 2:
            state = xs(state)
            v = (state * (a & 255)) >> 16
        else:
            if a > 0:
                state = xs(state)
                v = 1 if (state * 100) >> 16 < a else 0
            else:
                v = 0
        out.append(v & 0xffff)
    return out, state


def rom_checks(c, rom, out):
    syms = hrun.symbols(rom)
    for need in ('dbg_cmd', 'ovl', 'rnd_state', 'sv'):
        if need not in syms and '_' + need not in syms:
            try:
                hrun.addr(syms, need)
            except Exception:
                sys.exit('loadtest: %s has no %s (build it with XDEFS=TEST_HOOKS)' % (rom, need))
    texts, wid, chars = textwidth.load()
    names = {}
    for mm in re.finditer(r'#define\s+(\w+)\s+(\d+)', open(os.path.join(ROOT, 'data', 'gen', 'text.h')).read()):
        names.setdefault(int(mm.group(2)), mm.group(1))
    import text as textmod

    def decode(tid):
        return textwidth.measure(texts[names[tid]], wid, chars)[3]

    def subst(t):
        return ''.join(textmod.SUBST.get(ch, ch) for ch in t)

    # ---- saves: the menus loadouts equipped (bag 2, 3, 4 = MAIN, SUB, THIRD), the 999-item v1 bag
    mls = menus_loadouts(c)
    eq_saves = []
    for k, (ws, want) in enumerate(mls):
        p = os.path.join(out, 'eq%d.srm' % k)
        items = ['%d:%s' % (c.weapons[3 + k * 7 + j], ','.join(str(x) for x in w)) for j, w in enumerate(ws)]
        mksave_run(p, '--open', '3', '--clear-all', '--items', *items, '--equip', '2,3,4,1')
        eq_saves.append(p)
    r = random.Random(11)
    pos = [l for l in solo_lists(c) if c.st.lookup(l)[0]]
    bag = []
    for i in range(997):
        iid = r.choice(c.weapons) if i % 9 else r.choice(c.shields)
        sk = r.choice(pos) if i % 3 == 0 else [r.randrange(c.n_sk) for _ in range(r.randint(0, 3))]
        bag.append((iid, [x for x in sk if x != 255]))
    big = os.path.join(out, 'v1_999.srm')
    mksave_run(big, '--v1', '--open', '5', '--clear-all', '--items', *['%d:%s' % (i, ','.join(map(str, s))) for i, s in bag])

    # ---- the main script (on the map of the 999-item save)
    S = Script()
    S.lines += to_map(big, out)
    # solo lists
    lists = solo_lists(c)
    per = (1024 // 3)
    sb = []
    adds = [c.weapons[7], c.weapons[100], c.shields[3]]     # inv_add of these items (sword, spear?, a shield)
    for k in range(0, len(lists), per):
        part = lists[k:k + per]
        add = adds[len(sb) % len(adds)]
        S.poke_bytes('ovl', DBG_IO, [x for l in part for x in l])
        S.cmd(DC_SOLO, [len(part), add + 1])
        S.dump('solo_%03d.bin' % len(sb), 'ovl+%d' % DBG_IO, 3 * len(part))
        sb.append((part, add))
    # loadouts
    los = [ws for ws, _ in mls] + gen_loadouts(c, 100, 100, 100, 9)
    rb = []
    per = 1024 // 9
    for k in range(0, len(los), per):
        part = los[k:k + per]
        rec = []
        for ws in part:
            ws = (list(ws) + [[], [], []])[:3]
            for w in ws:
                rec += (list(w) + [255, 255, 255])[:3]
        S.poke_bytes('ovl', DBG_IO, rec)
        S.cmd(DC_REACH, [len(part)], wait=20000)
        S.dump('reach_%03d.bin' % len(rb), 'ovl+%d' % DBG_IO, 9 * len(part))
        rb.append(part)
    # effects
    flo = fx_loadouts(c)
    cases = [(k, fx, lo) for lo in flo for k in range(4) for fx in range(len(c.effect_ids))]
    cases += [(k, 255, lo) for lo in flo for k in range(4)]
    fb = []
    per = 1024 // 18
    for k in range(0, len(cases), per):
        part = cases[k:k + per]
        rec = []
        for kk, fx, lo in part:
            rec += [kk, fx] + rec_lo(lo)
        S.poke_bytes('ovl', DBG_IO, rec)
        S.cmd(DC_FX, [len(part)])
        S.dump('fx_%03d.bin' % len(fb), 'ovl+%d' % DBG_IO, 18 * len(part))
        fb.append(part)
    # rnd
    rmodes = [(500, 0, 0, 0), (200, 1, 0, 99), (200, 1, 7, 7), (200, 1, 3, 200), (200, 1, 0, 1), (200, 1, 10, 3),
              (200, 4, 0, 255), (200, 4, 0, 999), (200, 4, 0, 65535), (300, 2, 100, 0), (100, 2, 1, 0), (200, 2, 255, 0),
              (300, 3, 30, 0), (100, 3, 0, 0), (100, 3, 100, 0)]
    for k, (n, mode, a, b) in enumerate(rmodes):
        S.cmd(DC_RND, [n, mode, a, b])
        S.dump('rnd_%03d.bin' % k, 'ovl+%d' % DBG_IO, 2 * n)
    # math
    mr = random.Random(13)
    mcases = []
    for mode in range(4):
        recs = []
        for _ in range(100):
            x = mr.choice([mr.randrange(1 << 32), mr.randrange(1 << 16), mr.randrange(1 << 24), 0xffffffff, 0])
            p = mr.randrange(2, 256) if mode in (1, 3) else mr.choice([mr.randrange(1 << 16), mr.randrange(2, 400), 100, 0xffff])
            if mode == 3:
                p = mr.randrange(0, 256)
            recs.append((x, p))
        mcases.append(recs)
        S.poke_bytes('ovl', DBG_IO, [v for x, p in recs for v in (x & 0xffff, x >> 16, p, 0)], w=2)
        S.cmd(DC_MATH, [len(recs), mode])
        S.dump('math_%d.bin' % mode, 'ovl+%d' % DBG_IO, 8 * len(recs))
    # the 必殺10連 pool
    gdefs = {mm.group(1): int(mm.group(2)) for mm in re.finditer(
        r'#define\s+(SOLO_\w+)\s+(\d+)', open(os.path.join(ROOT, 'data', 'gen', 'gdata.h')).read())}
    S.cmd(DC_POOL, [gdefs['SOLO_POOL_N']])
    S.dump('pool.bin', 'ovl+%d' % DBG_IO, gdefs['SOLO_POOL_N'])
    # the bag again, timed
    S.cmd(DC_SOLOALL, [])
    S.dump('bag_after.bin', 'sv', mksave.V2_SIZE)
    wd = os.path.join(out, 'main')
    os.makedirs(wd, exist_ok=True)
    txt = '\n'.join(S.lines) + '\n'
    rr = hrun.run(rom, hrun.prepare(os.path.join(wd, 's.txt'), wd, text=txt), wd, timeout=3600)
    open(os.path.join(wd, '_stdout.txt'), 'w').write(rr.stdout + rr.stderr)
    if rr.returncode:
        check(False, 'runrom: exit status %d: %s' % (rr.returncode, (rr.stdout + rr.stderr)[-300:]))
        return
    pk = [v for n, v in parse_peeks(rr.stdout)]
    acks = pk[0::2]
    res = [u16s(v) for v in pk[1::2]]
    cmds = [int(l.split()[2]) for l in S.lines if l.startswith('poke dbg_cmd')]
    check([a[0] for a in acks] == cmds, 'every test command answered (%d)' % len(cmds))
    rd = lambda name: open(os.path.join(wd, name), 'rb').read()  # noqa: E731
    ci = 0

    print('rom: one weapon (DC_SOLO)')
    bad_f = bad_k = bad_r = bad_a = n = 0
    for k, (part, add) in enumerate(sb):
        d = rd('solo_%03d.bin' % k)
        for j, l in enumerate(part):
            f, o0, o1 = d[3 * j:3 * j + 3]
            ok, kick = c.st.lookup(l)
            web = c.m.web_reach([l])
            n += 1
            bad_f += bool(f & 1) != ok
            bad_k += bool(f & 2) != kick
            bad_r += ((f >> 2) & 15) != len(web) or (o0 | o1 << 8) != sum(1 << x for x in web)
            bad_a += bool(f & 0x40) or bool(f & 0x80) != (bool(web) and add in c.weapons)
        ci += 1
    check(bad_f == 0, '%d lists: solo_ok_sk == the web BFS (%d differ)' % (n, bad_f))
    check(bad_k == 0, 'kick-only == kick_only (%d differ)' % bad_k)
    check(bad_r == 0, 'sp_reach(nw=1): the specials == web_reach (%d differ)' % bad_r)
    check(bad_a == 0, 'inv_add of a sword / a weapon / a shield with each list: INV_SOLO == web solo_ok (%d differ)' % bad_a)

    print('rom: loadouts (DC_REACH)')
    bad = n = 0
    worst = 0
    for k, part in enumerate(rb):
        d = rd('reach_%03d.bin' % k)
        worst = max(worst, res[ci][0])
        for j, ws in enumerate(part):
            cnt, o0, o1, t0, t1 = d[9 * j:9 * j + 5]
            web = c.m.web_reach(ws)
            n += 1
            if cnt != len(web) or (o0 | o1 << 8) != sum(1 << x for x in web):
                bad += 1
        ci += 1
    check(bad == 0, '%d loadouts: sp_reach == web_reach (%d differ)' % (n, bad))
    d = rd('reach_000.bin')
    got5 = [[x for x in range(16) if (d[9 * j + 1] | d[9 * j + 2] << 8) >> x & 1] for j in range(5)]
    check(got5 == [w for _, w in mls] and [c.m.web_reach(ws) for ws, _ in mls] == [w for _, w in mls],
          'the 5 loadouts of the menus spec: %s' % got5)
    check(worst <= 10, 'the slowest sp_reach call: %d frames (<= 10)' % worst)

    print('rom: effect states (DC_FX)')
    bad = n = 0
    examples = []
    for k, part in enumerate(fb):
        d = rd('fx_%03d.bin' % k)
        for j, (kk, fx, lo) in enumerate(part):
            stv, n0, n1, slots = d[18 * j:18 * j + 4]
            note = n0 | n1 << 8
            wl = web_lo(c, lo)
            if fx == 255:
                if lo[kk] is None:
                    want = (LM.FS_NONE, '')
                else:
                    want = LM.fx_state(c.s, wl, kk)
            else:
                want = LM.fx_state(c.s, wl, kk, c.effect_ids[fx])
            if want[1].startswith('対象 '):
                got = (stv, '対象 ' + ' '.join(LM.SLOT[x] for x in range(3) if slots >> x & 1)) if names.get(note) == 'T_ST_FX_TARGET' else (stv, '?')
            elif note == 0:
                got = (stv, '')
            else:
                got = (stv, decode(note))
            n += 1
            if got != (want[0], subst(want[1])):
                bad += 1
                if len(examples) < 5:
                    examples.append('k %d fx %s: %s, want %s' % (kk, c.effect_ids[fx] if fx != 255 else 'own', got, want))
        ci += 1
    check(bad == 0, '%d cases (86 effects x 4 slots x 6 loadouts + the items\' own): state and note == desc.gd '
          'fx_state (%d differ) %s' % (n, bad, '; '.join(examples)))

    print('rom: random numbers (DC_RND)')
    state = None
    bad = n = 0
    for k, (cnt, mode, a, b) in enumerate(rmodes):
        before, after = res[ci][0], res[ci][1]
        if state is not None and before != state:
            bad += 1
        vals = u16s(rd('rnd_%03d.bin' % k))
        want, st2 = rnd_model(before, cnt, mode, a, b)
        n += cnt
        if vals != want or st2 != after:
            bad += 1
            print('    mode %d (%d, %d): %s... want %s...' % (mode, a, b, vals[:6], want[:6]))
        state = None        # frames pass between the commands (frame_wait calls rnd)
        ci += 1
    check(bad == 0, '%d values (rnd, rnd_range 1-65536 values, rnd_below, rnd_pct) == the old C (%d runs differ)' % (n, bad))

    print('rom: multiplier / divider helpers (DC_MATH)')
    for mode, recs in enumerate(mcases):
        d = rd('math_%d.bin' % mode)
        bad = 0
        for j, (x, p) in enumerate(recs):
            v = struct.unpack_from('<I', d, 8 * j)[0]
            if mode == 0:
                want = (x & 0xffff) * p
            elif mode == 1:
                want = x // (p & 0xff)
            elif mode == 2:
                want = ((x * p) & 0xffffffff) // 100
            else:
                want = ((x & 0xffff) * (p & 0xff)) >> 16
            bad += v != want
        check(bad == 0, '%s on %d arguments == Python (%d differ)' % (['mul_now', 'div_now', 'pct_now', 'rnd_mul8'][mode], len(recs), bad))
        ci += 1

    print('rom: the 必殺10連 pool (DC_POOL)')
    got = rd('pool.bin')
    want, ok_defs = [], True
    for r, (rar, w) in enumerate(c.s.tuning['SOLO_GACHA_RATES']['surface']):
        ids = [c.item_ids.index(k) for k in solo_cands(c.s, rar)]
        ok_defs &= (gdefs['SOLO_OFF_%d' % r], gdefs['SOLO_CNT_%d' % r]) == (len(want), len(ids))
        want += ids
    check(ok_defs and list(got) == want and len(want) == gdefs['SOLO_POOL_N'],
          'solo_pool_id(0 .. %d) == the web solo_gacha_cands of each rarity in order (SOLO_OFF_r / SOLO_CNT_r %s)' % (
              len(want) - 1, 'right' if ok_defs else 'WRONG'))
    ci += 1

    print('rom: the bag of a v1 save (999 items)')
    sv = open(os.path.join(out, 'after_load.bin'), 'rb').read()
    o = hrun.addr(syms, 'sv')
    svb = sv[o:o + mksave.V2_SIZE]
    ninv = struct.unpack_from('<H', svb, mksave.V2_NINV)[0]
    shield = set(c.shields)
    bad = n_solo = 0
    for j in range(ninv):
        iid, s0, s1, s2, slots = svb[mksave.V2_NINV + 2 + 8 * j:mksave.V2_NINV + 2 + 8 * j + 5]
        want = iid not in shield and c.st.lookup([s0, s1, s2])[0] and bool(c.m.web_reach([[s0, s1, s2]]))
        n_solo += want
        bad += bool(slots & INV_SOLO) != want or (slots & 0xbf) != (3 if j > 1 else (0 if j == 0 else 2))
    check(ninv == 999 and bad == 0, 'after the load: %d items, the INV_SOLO bits == the web BFS (%d solo, %d differ), '
          'the other slot bits as written' % (ninv, n_solo, bad))
    check((svb[STORY] & ST_SOLO) != 0, 'sv.story has ST_SOLO')
    sram = open(os.path.join(out, 'after_load.srm'), 'rb').read()
    hb = sram[0x4000:0x4000 + 12]
    check(hb[:4] == b'KSFC' and struct.unpack_from('<HHHH', hb, 4)[0] == 2 and
          sram[0x4000 + 12:0x4000 + 1228 + 8 * ninv] == svb[12:1228 + 8 * ninv],
          'the load wrote the fixed bag back (slot B, seq 2, = WRAM sv)')
    check(sram[:12] == open(big, 'rb').read()[:12], 'slot A still the v1 save')
    t, nsolo = res[ci][0], res[ci][1]
    after = rd('bag_after.bin')
    check(t <= 10 and nsolo == n_solo and after[mksave.V2_NINV:mksave.V2_NINV + 2 + 8 * ninv] ==
          svb[mksave.V2_NINV:mksave.V2_NINV + 2 + 8 * ninv],
          'inv_solo_all on the 999 items: %d frames (<= 10), %d solo, the bag unchanged' % (t, nsolo))

    print('rom: sp_reach_equipped (saves that equip the 5 loadouts)')
    for k, p in enumerate(eq_saves):
        S2 = Script()
        S2.lines += to_map(p, os.path.join(out, 'eq%d' % k))
        S2.cmd(DC_REACH_EQ, [], wait=600)
        wd2 = os.path.join(out, 'eq%d' % k)
        os.makedirs(wd2, exist_ok=True)
        rr = hrun.run(rom, '\n'.join(S2.lines) + '\n', wd2)
        pk2 = [v for nn, v in parse_peeks(rr.stdout)]
        r2 = u16s(pk2[1]) if len(pk2) > 1 else [0, 0, 0]
        want = mls[k][1]
        got = [x for x in range(16) if r2[1] >> x & 1]
        check(rr.returncode == 0 and got == want and r2[0] == len(want),
              'save %d: %s (%d frames)' % (k, got, r2[2]))


WEB_RARITY_IDS = ['RARE_N', 'RARE_NP', 'RARE_R', 'RARE_RP', 'RARE_SR', 'RARE_SRP', 'RARE_SSR', 'RARE_SSRP', 'RARE_UR',
                  'RARE_URP', 'RARE_USR', 'RARE_USRP', 'RARE_LR', 'RARE_LRP', 'RARE_LSR', 'RARE_LSRP', 'RARE_MR']   # rarity.gd IDS


def solo_cands(s, rarity):
    """adventure.gd solo_gacha_cands('surface', rarity): DexData.weapon_order (WEAPON_TABLE without the
    material items, sword / spear / hammer, below SSR, by type, rarity, atk, id) of that rarity"""
    mats = {r['item'] for r in s.tables['MAT_TABLE'] if r.get('item')}
    tord = {'WT_SWORD': 0, 'WT_SPEAR': 1, 'WT_HAMMER': 2}
    ranks = WEB_RARITY_IDS
    rows = [w for w in s.tables['WEAPON_TABLE'] if w['id'] not in mats and w['wtype'] in tord
            and ranks.index(s.weapons[w['id']]['rarity']) < ranks.index('RARE_SSR')]
    rows.sort(key=lambda w: (tord[w['wtype']], ranks.index(w['rarity']), int(w['atk']), w['id']))
    return [w['id'] for w in rows if s.weapons[w['id']]['rarity'] == rarity]


def mksave_run(path, *args):
    import subprocess
    r = subprocess.run([sys.executable, os.path.join(HERE, 'mksave.py'), path] + list(args), capture_output=True, text=True)
    if r.returncode:
        sys.exit('mksave failed: %s' % (r.stdout + r.stderr))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', nargs='?', default=hrun.ROM)
    ap.add_argument('-o', '--out')
    ap.add_argument('--python-only', action='store_true')
    a = ap.parse_args()
    out = os.path.abspath(a.out) if a.out else tempfile.mkdtemp(prefix='loadtest_')
    os.makedirs(out, exist_ok=True)
    c = Ctx()
    python_checks(c)
    if not a.python_only:
        rom_checks(c, os.path.abspath(a.rom), out)
    print('%d check(s) failed: %s' % (len(fails), '; '.join(fails)) if fails else 'all checks passed')
    print('files in', out)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
