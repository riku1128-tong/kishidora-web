#!/usr/bin/env python3
"""Tests of the battle core fixes and the battle test commands (plan step A1): src/c/battle_core.c
(the tower form change, b_turns_stage, the meat gift cap, FORCE_WAVE / FORCE_MIRAGE_WAVE), src/c/battle_hit.c
(魔王城の地下: every lucky character drops a material) and the sys.c / battle_core.c test commands
DC_SPECIAL, DC_CLEAR, DC_SETHP, DC_FOE, DC_CALM, DC_TALK, DC_MONEYMAT and the event ring dbg_evlog, against
the web rules (battle_core.gd).

usage: battletest.py [-o OUTDIR] [--only NAME,...] [--xdefs] [ROM]
       (an XDEFS=TEST_HOOKS ROM, default build/kishidra.sfc; --xdefs: the ROM was built with
       XDEFS="TEST_HOOKS FORCE_WAVE=0,0,6 FORCE_MIRAGE_WAVE=15" and the tests use those values as
       compiled, without poking force_wave / force_mwave; it runs boot, tower, gift and force)

  boot    force_wave / force_mwave as compiled (off: MON_NONE / 0; --xdefs: 0,0,6,255 / 15); a command
          of the battle screen on the map is DC_UNKNOWN
  tower   蜃気楼の斜塔 from WAVE 15 (force_mwave): 魔王 form 0 = est[14]; DC_CLEAR -> the form change makes
          WAVE 16 (b_stage 16, form 1 with the stats of est[15]), b_turns_stage 0 then +1 per enemy turn;
          form 2 = WAVE 17 (est[16]); its kill -> the chest -> WAVE 18 ペコラータ (est[17])
          (battle_core.gd form_swap: stage += 1, turns_stage = 0 in the tower)
  castle  魔王の城 STAGE 10, waves 1-4 cleared with enemy turns in between; WAVE 5 (the 3 forms of the
          魔王): b_stage stays 5 and b_turns_stage counts every turn since the WAVE started, through
          both form changes (records: 魔王戦), b_turns_total the whole run
  gift    魔王城の地下 with every wave the guard ミミッくん (force_wave = MON_115): a guarded meat ask gives a
          weapon (1 item); at 14 items one more (15); at 15 none (D_ITEM_MAX + T_MEAT_GIFT_EXTRA_MAX,
          was 20); DC_SPECIAL makes it ask again (its special: EV_ENEMY_BANG a = 1, EV_ENEMY_WINDUP a = 1)
  money   魔王城の地下, the 5 WAVEs cleared with DC_CLEAR (lucky ones first, the web autotest
          _auto_adv_money): every lucky character drops a material -> sum of b_mats >= 2 per WAVE,
          >= 10 at the victory
  roll    DC_MONEYMAT: 10,000 rolls of the money-lucky material at drop 100 % and at 150 %
          (FX_YOKUBARI): the counts == a Python model of the rule run on the ROM's RNG from the same
          state (rnd_pct / mul_pct16 / xorshift), no roll comes out empty, and the counts are inside 4 sigma
          of 3 / 14.55 / 82.45 % (100 %) and 4 / 21.12 / 74.88 % (150 %)
  force   いにしえの洞窟 STAGE 1 with force_wave 0,0,6: every wave is プチドラ, プチドラ, ゴールデンプチ
          (DC_FOE); DC_SPECIAL makes the target attack at once, EV_ENEMY_BANG's a = special next (0 for a
          monster without a special; the gift test checks 1 on the guard), b_special_now at the windup;
          DC_SETHP on an enemy, on the hero (capped at the max), at 0 a kill; DC_CLEAR the rest
Exit status 0 when every check passes."""
import argparse
import math
import os
import re
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
GEN = os.path.join(ROOT, 'data', 'gen')
sys.path.insert(0, HERE)
import hrun  # noqa: E402

DC_UNKNOWN = 0xff
DS_BATTLE = 1
DC_SPECIAL, DC_CLEAR, DC_SETHP, DC_FOE, DC_MONEYMAT, DC_CALM, DC_TALK = 16, 17, 18, 19, 20, 21, 22
PH_FIGHT, PH_VICTORY, PH_TALK, PH_REWARD = 2, 4, 7, 9
ST_WINDUP = 4
HERO = 255
BIG_HP = 9999                  # the hero survives every hit of the tests (b_hp_max poked; 4 digits fit the HP box)
fails = []


def check(ok, what):
    print('  %s %s' % ('ok  ' if ok else 'FAIL', what))
    if not ok:
        fails.append(what)
    return ok


# ---------------------------------------------------------------- the game's data (data/gen)
class Data:
    def __init__(self):
        g = open(os.path.join(GEN, 'gdata.h')).read()
        self.d = {m.group(1): int(m.group(2)) for m in re.finditer(r'#define\s+(\w+)\s+(-?\d+)\b', g)}
        t = open(os.path.join(GEN, 'tables.c')).read()
        self.maou_forms = [int(x) for x in re.search(r'maou_forms\[3\] = \{([^}]*)\}', t).group(1).split(',')]
        m = re.search(r'diff_rules\[5\] = \{(.*?)\};', t, re.S)
        self.diff_atk = [int(r.split(',')[0]) for r in re.findall(r'\{([^}]*)\}', m.group(1))]
        m = re.search(r'enum \{\s*(EV_STAGE_START = 1,.*?)\};', open(os.path.join(ROOT, 'src', 'c', 'battle.h')).read(), re.S)
        names = re.findall(r'\b(EV_\w+)', re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S))
        self.ev = {n: i + 1 for i, n in enumerate(names)}
        idx = open(os.path.join(GEN, 'estat_index.bin'), 'rb').read()
        dat = open(os.path.join(GEN, 'estat_data.bin'), 'rb').read()
        size = self.d['ESTAT_SIZE']
        self.est = {}
        for area in range(self.d['AR_MIRAGE'] + 1):
            for men in range(self.d['ESTAT_MEN_MAX'] + 1):
                off, n = struct.unpack_from('<HH', idx, (area * (self.d['ESTAT_MEN_MAX'] + 1) + men) * 4)
                rows = []
                for i in range(n):
                    e = struct.unpack_from('<BBBBIHHhHHHBBB', dat, off + size * i)
                    rows.append(dict(zip(('mon', 'stage', 'role', 'base', 'hp', 'atk', 'sp_atk', 'lock_atk', 'stun_need',
                                          'stun_step', 'stun_cap', 'sp_every', 'flags', 'ability'), e)))
                if rows:
                    self.est[(area, men)] = rows

    def mirage(self, wave):
        return self.est[(self.d['AR_MIRAGE'], 1)][wave - 1]

    def atk(self, atk, difficulty, lucky=False):
        dp = self.diff_atk[difficulty]
        return atk if dp == 100 or lucky else atk * dp // 100


# ---------------------------------------------------------------- runrom scripts
class Script:
    """a runrom script whose peeks are named (in order) and read back after the run"""
    def __init__(self, save):
        self.lines = ['#!mksave a1.srm %s' % save, 'loadsram a1.srm', 'wait 200']
        self.names = []
        self.seq = 0

    def raw(self, *lines):
        self.lines += lines

    def peek(self, name, sym, n=1):
        self.lines.append('peek %s %d' % (sym, n))
        self.names.append(name)

    def cmd(self, name, c, args=(), wait=3000):
        self.seq += 1
        self.lines.append('pokew dbg_arg %s' % ' '.join(str(a & 0xffff) for a in (list(args) + [0, 0, 0, 0])[:4]))
        self.lines.append('poke dbg_cmd %d' % c)
        self.lines.append('waitfor dbg_seq %d %d' % (self.seq & 0xff, wait))
        self.peek(name + '.ack', 'dbg_ack')
        self.peek(name, 'dbg_res', 8)

    def foe(self, name, i):
        self.cmd(name, DC_FOE, [i])
        self.peek(name + '.buf', 'dbg_buf', 26)

    def hero_hp(self, name='hp'):
        self.cmd(name, DC_SETHP, [HERO, BIG_HP & 0xffff, BIG_HP >> 16])

    def battle(self):
        """after the press that starts a battle: wait until battle_scene takes commands"""
        self.raw('waitfor dbg_scr %d 900' % DS_BATTLE)

    def talk(self, name='talk'):
        """the next talk window: wait for it, then close it as A does (DC_TALK waits until A would)"""
        self.cmd(name, DC_TALK)

    def turn(self, name):
        """one enemy turn: the hero eats the meat (R) at a calm moment; -> the counters after it"""
        self.hero_hp(name + '.hp')
        self.cmd(name + '.c0', DC_CALM)
        self.raw('press R', 'wait 10')
        self.cmd(name, DC_CALM)

    def evlog(self, name):
        """the battle_core.c event ring (TEST_HOOKS): the last 32 events posted"""
        self.peek(name + '.n', 'dbg_evn')
        self.peek(name, 'dbg_evlog', 128)

    def big_hero(self):
        self.raw('pokel b_hp_max %d' % BIG_HP)
        self.hero_hp('big')

    def run(self, rom, wd):
        r = hrun.run(rom, hrun.prepare(os.path.join(wd, '_src.txt'), wd, text='\n'.join(self.lines) + '\n'), wd, timeout=3600)
        open(os.path.join(wd, '_stdout.txt'), 'w').write(r.stdout + r.stderr)
        vals = []
        for line in r.stdout.splitlines():
            m = re.match(r'peek (\S+) [0-9a-f]+: ((?:[0-9a-f]{2} ?)+)', line)
            if m:
                vals.append([int(x, 16) for x in m.group(2).split()])
        out = dict(zip(self.names, vals))
        if r.returncode or len(vals) != len(self.names):
            check(False, 'runrom exit %d, %d of %d peeks (%s)' % (r.returncode, len(vals), len(self.names),
                                                                   ' / '.join(l for l in r.stderr.splitlines() if 'runrom' in l)[:300]))
        return out


def u16s(b):
    return [b[i] | b[i + 1] << 8 for i in range(0, len(b) - 1, 2)]


def res(out, name):
    """dbg_res of a command as 4 u16 (None when it did not run)"""
    return u16s(out[name]) if name in out else None


def foe(out, name):
    """DC_FOE -> dict (None when it did not run)"""
    if name not in out or name + '.buf' not in out:
        return None
    b, r = u16s(out[name + '.buf']), u16s(out[name])
    return {'mon': b[0], 'kind': b[1], 'hp': b[2] | b[3] << 16, 'hp_max': b[4] | b[5] << 16, 'atk': b[6], 'sp_atk': b[7],
            'sp_every': b[8], 'attacks': b[9], 'count': b[10], 'dying': b[11] & 255, 'gone': b[11] >> 8,
            'guarded': b[12] & 255, 'just': b[12] >> 8, 'stage': r[0], 'ne': r[1], 'turns_stage': r[2], 'turns_total': r[3]}


def events(out, name):
    """the event ring read by Script.evlog -> [(type, enemy, a, b)], oldest first"""
    if name not in out:
        return []
    n, b = out[name + '.n'][0], out[name]
    k = min(n, 32)
    return [tuple(b[((n - k + i) & 31) * 4:((n - k + i) & 31) * 4 + 4]) for i in range(k)]


def last_ev(ctx, out, name, ev):
    l = [e for e in events(out, name) if e[0] == ctx.data.ev[ev]]
    return l[-1] if l else None


def acked(out, name, c):
    return out.get(name + '.ack') == [c]


# ---------------------------------------------------------------- the tests
def t_boot(ctx):
    s = Script('--open 14 --clear-all')
    s.peek('fw', 'force_wave', 4)
    s.peek('fm', 'force_mwave')
    s.raw('press A', 'wait 150')
    s.cmd('map', DC_FOE, [0])
    s.cmd('roll0', DC_MONEYMAT, [0, 0])
    out = s.run(ctx.rom, ctx.wd('boot'))
    want = ([0, 0, 6, 255], [15]) if ctx.xdefs else ([255, 255, 0, 0], [0])
    check(out.get('fw') == want[0] and out.get('fm') == want[1],
          'force_wave %s / force_mwave %s as compiled (%s)' % (out.get('fw'), out.get('fm'), 'XDEFS' if ctx.xdefs else 'off'))
    check(acked(out, 'map', DC_UNKNOWN), 'DC_FOE on the map: DC_UNKNOWN (the battle screen only)')
    check(acked(out, 'roll0', DC_MONEYMAT) and res(out, 'roll0') == [0, 0, 0, 0], 'DC_MONEYMAT works on any screen (0 rolls)')


def t_tower(ctx):
    D, d = ctx.data, ctx.data.d
    s = Script('--open 14 --clear-all')
    if not ctx.xdefs:
        s.raw('poke force_mwave 15')
    s.raw('press A', 'wait 150', 'press RIGHT', 'wait 30', 'press A', 'wait 60', 'press A')
    s.battle()
    s.talk()                                   # 魔王: 「...」 (TALK_MAOU1)
    s.big_hero()
    s.peek('diff', 'b_difficulty')
    s.foe('w15', 0)
    s.turn('t15')
    s.cmd('clear0', DC_CLEAR)                  # form 0 -> the form change
    s.raw('waitfor b_phase %d 1500' % PH_TALK)    # TALK_MAOU2 (after the form change)
    s.foe('w16', 0)
    s.raw('shot OUT/tower_w16.ppm')
    s.talk()
    s.turn('t16a')
    s.turn('t16b')
    s.foe('w16b', 0)
    s.cmd('clear1', DC_CLEAR)
    s.raw('waitfor b_phase %d 1500' % PH_TALK)    # TALK_MAOU3
    s.foe('w17', 0)
    s.raw('shot OUT/tower_w17.ppm')
    s.talk()
    s.hero_hp('hp17')
    s.cmd('calm17', DC_CALM)                   # after the final form's opening attack
    s.cmd('clear2', DC_CLEAR)
    s.talk()                                   # TALK_MAOU_DEFEAT
    s.raw('waitfor b_phase %d 1500' % PH_REWARD, 'wait 90', 'press A')
    s.talk()                                   # ペコラータ: TALK_START
    s.foe('w18', 0)
    s.raw('shot OUT/tower_w18.ppm')
    out = s.run(ctx.rom, ctx.wd('tower'))
    diff = out.get('diff', [0])[0]
    f = [foe(out, n) for n in ('w15', 'w16', 'w17', 'w18')]
    for k, (e, wave, mon) in enumerate(zip(f, (15, 16, 17, 18), D.maou_forms + [d['MON_117']])):
        x = D.mirage(wave)
        ok = e is not None and e['stage'] == wave and e['mon'] == mon == x['mon'] and e['hp_max'] == x['hp'] \
            and e['atk'] == D.atk(x['atk'], diff) and e['sp_atk'] == D.atk(x['sp_atk'], diff) and e['sp_every'] == x['sp_every']
        check(ok, 'WAVE %d: b_stage %s, %s (mon %d), HP %s / atk %s / sp %s == est[%d] (%d / %d / %d)' % (
            wave, e and e['stage'], ['form 0', 'form 1', 'form 2', 'ペコラータ'][k], mon, e and e['hp_max'], e and e['atk'],
            e and e['sp_atk'], wave - 1, x['hp'], D.atk(x['atk'], diff), D.atk(x['sp_atk'], diff)))
    t15, w16, t16a, t16b, w17 = res(out, 't15'), f[1], res(out, 't16a'), res(out, 't16b'), f[2]
    if None in (t15, w16, t16a, t16b, w17):
        check(False, 'tower: a step did not run')
        return
    check(t15[2] == t15[0] - res(out, 't15.c0')[0] + res(out, 't15.c0')[2] and t15[0] > res(out, 't15.c0')[0],
          'WAVE 15: b_turns_stage %d after %d turn(s)' % (t15[2], t15[0] - res(out, 't15.c0')[0]))
    check(w16['turns_stage'] == 0 and w16['turns_total'] == t15[0],
          'form change in the tower: b_turns_stage 0 (b_turns_total %d goes on)' % w16['turns_total'])
    check(t16b[2] == t16b[0] - w16['turns_total'] and t16b[2] >= 2,
          'WAVE 16: b_turns_stage %d == turns since the form change (%d)' % (t16b[2], t16b[0] - w16['turns_total']))
    check(w17['turns_stage'] == 0 and w17['turns_total'] == t16b[0], 'WAVE 17: b_turns_stage 0 again')
    check(f[3] is not None and f[3]['turns_stage'] == 0, 'WAVE 18: b_turns_stage 0 at the start')


def t_castle(ctx):
    d = ctx.data.d
    s = Script('--open 14 --clear-all')
    s.raw('press A', 'wait 120', 'press A', 'wait 60', 'press A', 'wait 60', 'press A')
    s.battle()
    s.big_hero()
    s.peek('men', 'b_men')
    for w in range(1, 5):
        s.cmd('s%d' % w, DC_CALM)
        if w in (1, 3):
            s.turn('t%d' % w)
        s.cmd('c%d' % w, DC_CLEAR)
    s.talk()                                   # 魔王 form 0 (TALK_MAOU1)
    s.cmd('s5', DC_CALM)
    s.turn('t5a')
    s.turn('t5b')
    s.cmd('c5a', DC_CLEAR)
    s.raw('waitfor b_phase %d 1500' % PH_TALK)
    s.foe('f1', 0)
    s.talk()
    s.turn('t5c')
    s.cmd('c5b', DC_CLEAR)
    s.raw('waitfor b_phase %d 1500' % PH_TALK)
    s.foe('f2', 0)
    s.talk()
    s.hero_hp('hp5')
    s.turn('t5d')
    s.cmd('c5c', DC_CLEAR)
    s.talk()                                   # TALK_MAOU_DEFEAT
    s.raw('waitfor b_phase %d 1500' % PH_VICTORY)
    s.peek('vs', 'b_stage')
    s.peek('vts', 'b_turns_stage', 2)
    s.peek('vtt', 'b_turns_total', 2)
    out = s.run(ctx.rom, ctx.wd('castle'))
    check(out.get('men') == [d['D_CASTLE_MEN']], '魔王の城 STAGE %s' % out.get('men'))
    st = [res(out, 's%d' % w) for w in range(1, 6)]
    if None in st:
        check(False, 'castle: a wave did not start')
        return
    check([r[1] for r in st] == [1, 2, 3, 4, 5] and all(r[2] == 0 for r in st),
          'b_turns_stage 0 at the start of each WAVE (WAVEs %s, turns_total %s)' % ([r[1] for r in st], [r[0] for r in st]))
    t0 = st[4][0]
    check(t0 >= 2, 'turns before WAVE 5: %d' % t0)
    f1, f2, t5c, t5d = foe(out, 'f1'), foe(out, 'f2'), res(out, 't5c'), res(out, 't5d')
    if None in (f1, f2, t5c, t5d):
        check(False, 'castle: a form step did not run')
        return
    check(f1['stage'] == 5 and f1['mon'] == ctx.data.maou_forms[1] and f1['turns_stage'] == f1['turns_total'] - t0 >= 2,
          'form 1: b_stage %d, b_turns_stage %d == turns since WAVE 5 started (%d)' % (f1['stage'], f1['turns_stage'], f1['turns_total'] - t0))
    check(f2['stage'] == 5 and f2['mon'] == ctx.data.maou_forms[2] and f2['turns_stage'] == f2['turns_total'] - t0 >= 3,
          'form 2: b_stage %d, b_turns_stage %d == turns since WAVE 5 started (%d)' % (f2['stage'], f2['turns_stage'], f2['turns_total'] - t0))
    vts, vtt = u16s(out.get('vts', [0, 0]))[0], u16s(out.get('vtt', [0, 0]))[0]
    check(out.get('vs') == [5] and vts == vtt - t0 and vts == t5d[2],
          'victory: b_turns_stage %d (魔王戦, all forms) == b_turns_total %d - %d' % (vts, vtt, t0))


def t_gift(ctx):
    d = ctx.data.d
    cap = d['D_ITEM_MAX'] + d['T_MEAT_GIFT_EXTRA_MAX']
    s = Script('--open 14 --clear-all')
    s.raw('poke force_wave %d 255' % d['MON_115'])
    s.raw('press A', 'wait 150', 'press A', 'wait 40', 'press DOWN', 'wait 20', 'press A', 'wait 40', 'press A')
    s.battle()
    s.peek('area', 'b_area')
    s.peek('men', 'b_men')
    # 1: its opening ask (the first attack of the wave), guarded with the meat
    s.raw('waitfor b_step_kind %d 1500' % ST_WINDUP, 'press R')
    s.cmd('g1.calm', DC_CALM)
    s.foe('g1', 0)
    s.peek('g1.n', 'b_nitems')
    s.peek('g1.m', 'b_meat_gifts')
    for k, n in ((2, cap - 1), (3, cap)):
        s.hero_hp('g%d.hp' % k)
        s.raw('poke b_nitems %d' % n)
        s.cmd('g%d.sp' % k, DC_SPECIAL, [255])
        s.raw('waitfor b_step_kind %d 1500' % ST_WINDUP)
        s.evlog('g%d.ev' % k)
        s.raw('press R')
        s.cmd('g%d.calm' % k, DC_CALM)
        s.foe('g%d' % k, 0)
        s.peek('g%d.n' % k, 'b_nitems')
        s.peek('g%d.m' % k, 'b_meat_gifts')
    s.raw('shot OUT/gift.ppm')
    out = s.run(ctx.rom, ctx.wd('gift'))
    check(out.get('area') == [d['AR_CASTLE']] and out.get('men') == [d['T_MONEY_MEN']], '魔王城の地下, every wave the guard (MON_115)')
    rows = [(foe(out, 'g%d' % k), out.get('g%d.n' % k), out.get('g%d.m' % k)) for k in (1, 2, 3)]
    want = [(1, 1), (cap, 2), (cap, 2)]
    for k, ((e, n, m), (wn, wm)) in enumerate(zip(rows, want), 1):
        before = [0, cap - 1, cap][k - 1]
        check(e is not None and e['mon'] == d['MON_115'] and e['guarded'] == 1 and n == [wn] and m == [wm],
              'ask %d (guarded %s) with %d items: b_nitems %s, b_meat_gifts %s (want %d, %d)' % (
                  k, e and e['guarded'], before, n, m, wn, wm))
    for k in (2, 3):
        r = res(out, 'g%d.sp' % k)
        bang, wind = last_ev(ctx, out, 'g%d.ev' % k, 'EV_ENEMY_BANG'), last_ev(ctx, out, 'g%d.ev' % k, 'EV_ENEMY_WINDUP')
        check(r is not None and r[0] == 1 and r[1] == 1 and bang is not None and bang[1:3] == (0, 1) and wind is not None
              and wind[2] == 1, 'DC_SPECIAL on the guard: done, special next %s; EV_ENEMY_BANG enemy / a (special next) = %s, '
              'EV_ENEMY_WINDUP a = %s' % (r and r[1], bang and bang[1:3], wind and wind[2]))


def t_money(ctx):
    d = ctx.data.d
    s = Script('--open 14 --clear-all')
    s.raw('press A', 'wait 150', 'press A', 'wait 40', 'press DOWN', 'wait 20', 'press A', 'wait 40', 'press A')
    s.battle()
    for w in range(1, 6):
        s.hero_hp('hp%d' % w)
        s.cmd('c%d' % w, DC_CLEAR)
        s.peek('m%d' % w, 'b_mats', d['N_MAT'])
    s.raw('waitfor b_phase %d 1500' % PH_VICTORY)
    s.peek('mats', 'b_mats', d['N_MAT'])
    s.peek('coins', 'b_coins', 4)
    s.raw('wait 60', 'shot OUT/money_clear.ppm')
    out = s.run(ctx.rom, ctx.wd('money'))
    lucky = [d['T_MONEY_LUCKY_%d' % i] for i in range(d['T_MONEY_STAGES'])]
    killed = [res(out, 'c%d' % w) for w in range(1, 6)]
    sums = [sum(out.get('m%d' % w, [])) for w in range(1, 6)]
    check(None not in killed, 'DC_CLEAR killed %s enemies per WAVE' % [k and k[0] for k in killed])
    need = 0
    for w in range(5):
        need += lucky[w]
        check(sums[w] >= need, 'after WAVE %d: %d materials (>= %d: one per lucky character so far)' % (w + 1, sums[w], need))
    mats = out.get('mats', [])
    names = {d[k]: k for k in ('MAT_BELL', 'MAT_SKYOUKA', 'MAT_KYOUKA')}
    got = ', '.join('%s %d' % (names.get(i, str(i)), n) for i, n in enumerate(mats) if n)
    coins = struct.unpack('<I', bytes(out.get('coins', [0, 0, 0, 0])))[0]
    check(sum(mats) >= sum(lucky) == 10, 'victory: %d materials >= %d lucky characters (%s; %d coins)' % (sum(mats), sum(lucky), got, coins))


def xs(v):
    v ^= (v << 7) & 0xffff
    v ^= v >> 9
    v ^= (v << 8) & 0xffff
    return v


def money_model(state, n, drop):
    """battle_hit.c money_mat on the RNG of math.s: rnd_pct(p) = p > 0 and rnd_below(100) < p,
    mat_roll(pct) = rnd_pct(min(pct * drop / 100, 100))"""
    cnt = [0, 0, 0, 0]
    steps = [(min(p * drop // 100, 100), k) for p, k in ((3, 1), (15, 2), (100, 3))]
    for _ in range(n):
        m = 0
        for p, k in steps:
            if p > 0:
                state = xs(state)
                if (state * 100) >> 16 < p:
                    m = k
                    break
        cnt[m] += 1
    return cnt, state


def t_roll(ctx):
    d = ctx.data.d
    assert (d['D_MAT_BELL_LUCKY_PCT'], d['T_MONEY_MAT_SKYOUKA_PCT']) == (3, 15), 'the model assumes ベル 3 %, スーパー強化鉄 15 %'
    N = 10000
    s = Script('--open 14 --clear-all')
    s.raw('press A', 'wait 150')
    for drop in (100, 150):
        s.cmd('r%d' % drop, DC_MONEYMAT, [N, drop], wait=6000)
        s.peek('r%d.buf' % drop, 'dbg_buf', 4)
    out = s.run(ctx.rom, ctx.wd('roll'))
    for drop, p in ((100, (0, .03, .97 * .15, .97 * .85)), (150, (0, .04, .96 * .22, .96 * .78))):
        r = res(out, 'r%d' % drop)
        b = u16s(out.get('r%d.buf' % drop, [0, 0, 0, 0]))
        if r is None:
            check(False, 'DC_MONEYMAT %d %% did not run' % drop)
            continue
        model, after = money_model(b[0], N, drop)
        check(r == model and after == b[1], 'drop %d %%: none / ベル / スーパー強化鉄 / 強化鉄 = %s == the model from rnd_state $%04x (%s)' % (
            drop, r, b[0], model))
        band = all(abs(r[k] - N * p[k]) <= 4 * math.sqrt(N * p[k] * (1 - p[k])) for k in (1, 2, 3))
        check(r[0] == 0 and band and sum(r) == N, 'drop %d %%: no roll empty; inside 4 sigma of %s' % (
            drop, ' / '.join('%.0f' % (N * x) for x in p[1:])))


def t_force(ctx):
    d = ctx.data.d
    s = Script('--open 2 --clear 2:0')
    if not ctx.xdefs:
        s.raw('poke force_wave 0 0 6 255')
    s.raw('press A', 'wait 150', 'press A', 'wait 60', 'press A', 'wait 60', 'press A')
    s.battle()
    s.peek('area', 'b_area')
    s.cmd('calm1', DC_CALM)
    for i in range(3):
        s.foe('a%d' % i, i)
    s.cmd('sp', DC_SPECIAL, [0])
    s.raw('waitfor b_step_kind %d 1500' % ST_WINDUP)
    s.evlog('spev')
    s.peek('spnow', 'b_special_now')
    s.peek('att', 'b_attacker')
    s.cmd('calm2', DC_CALM)
    s.cmd('hpe', DC_SETHP, [1, 5, 0])
    s.foe('e1', 1)
    s.cmd('hph', DC_SETHP, [HERO, 7, 0])
    s.peek('hero', 'b_hp', 4)
    s.raw('shot OUT/force_hp.ppm')
    s.cmd('kill', DC_SETHP, [2, 0, 0])
    s.foe('e2', 2)
    s.cmd('hpf', DC_SETHP, [HERO, BIG_HP & 0xffff, BIG_HP >> 16])
    s.peek('hpmax', 'b_hp_max', 4)
    s.peek('hero2', 'b_hp', 4)
    s.cmd('clr', DC_CLEAR)
    s.cmd('calm3', DC_CALM)
    for i in range(3):
        s.foe('b%d' % i, i)
    out = s.run(ctx.rom, ctx.wd('force'))
    check(out.get('area') == [d['AR_ANCIENT']], 'いにしえの洞窟 (b_area %s)' % out.get('area'))
    for w, p in ((1, 'a'), (2, 'b')):
        fs = [foe(out, '%s%d' % (p, i)) for i in range(3)]
        mons = [f and f['mon'] for f in fs]
        check(mons == [0, 0, 6] and fs[0]['ne'] == 3 and fs[0]['stage'] == w,
              'WAVE %s: the monsters %s (want プチドラ 0, 0, ゴールデンプチ 6)' % (fs[0] and fs[0]['stage'], mons))
    a0 = foe(out, 'a0')
    sp = res(out, 'sp')
    bang = last_ev(ctx, out, 'spev', 'EV_ENEMY_BANG')
    check(sp is not None and sp[0] == 1 and sp[1] == (a0['sp_every'] > 0) and out.get('spnow') == [sp[1]] and out.get('att') == [0]
          and bang is not None and bang[1:3] == (0, sp[1]),
          'DC_SPECIAL: enemy 0 attacks at once, special next %s (sp_every %d), EV_ENEMY_BANG a %s, b_special_now %s at the windup' % (
              sp and sp[1], a0['sp_every'], bang and bang[2], out.get('spnow')))
    e1, e2 = foe(out, 'e1'), foe(out, 'e2')
    check(e1 is not None and e1['hp'] == 5 and res(out, 'hpe')[0] == 5, 'DC_SETHP enemy 1 -> HP %s' % (e1 and e1['hp']))
    hero = struct.unpack('<I', bytes(out.get('hero', [0, 0, 0, 0])))[0]
    check(hero == 7, 'DC_SETHP hero -> b_hp %d' % hero)
    check(e2 is not None and e2['hp'] == 0 and e2['dying'] > 0, 'DC_SETHP enemy 2 at 0: killed (dying %s)' % (e2 and e2['dying']))
    hm, h2 = (struct.unpack('<I', bytes(out.get(k, [0, 0, 0, 0])))[0] for k in ('hpmax', 'hero2'))
    check(h2 == hm and res(out, 'hpf')[0] | res(out, 'hpf')[1] << 16 == hm, 'DC_SETHP hero above the max: capped at %d' % hm)
    clr = res(out, 'clr')
    check(clr is not None and clr[0] == 2, 'DC_CLEAR: the 2 enemies left (%s)' % (clr and clr[0]))


XDEFS_TESTS = ['boot', 'tower', 'gift', 'force']   # (castle and money need the normal formations)
TESTS = {'boot': t_boot, 'tower': t_tower, 'castle': t_castle, 'gift': t_gift, 'money': t_money, 'roll': t_roll, 'force': t_force}


class Ctx:
    def __init__(self, rom, out, xdefs):
        self.rom, self.out, self.xdefs = rom, out, xdefs
        self.data = Data()

    def wd(self, name):
        p = os.path.join(self.out, name)
        os.makedirs(p, exist_ok=True)
        return p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', nargs='?', default=hrun.ROM)
    ap.add_argument('-o', dest='out')
    ap.add_argument('--only')
    ap.add_argument('--xdefs', action='store_true')
    a = ap.parse_args()
    syms = hrun.symbols(a.rom)
    for need in ('dbg_cmd', 'force_wave', 'b_turns_stage'):
        hrun.addr(syms, need)
    out = a.out or tempfile.mkdtemp(prefix='battletest_')
    ctx = Ctx(a.rom, out, a.xdefs)
    names = a.only.split(',') if a.only else (XDEFS_TESTS if a.xdefs else list(TESTS))
    for n in names:
        print('%s:' % n)
        TESTS[n](ctx)
    print('%d check(s) failed' % len(fails) if fails else 'all checks passed', '(output in %s)' % out)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
