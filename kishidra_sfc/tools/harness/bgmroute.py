#!/usr/bin/env python3
"""BGM routing tests (plan step H2, docs/plan/bgm.md §5): which song each place plays, recorded in the emulator
and matched with the stream of the expected track (tools/harness/wavref.py).

usage: bgmroute.py [-o OUTDIR] [--only NAME,...] [-j N] [ROM]
       (an XDEFS=TEST_HOOKS ROM, default build/kishidra.sfc; -j: runs in parallel, default 4)

The expected songs are the web's (shipped battle_core.gd stage_bgm, dex_data.gd monster_bgm, map.gd
_king_return; bgm.md §1.4 / §1.5), written below by file name, independent of make_data.py.  After
Tuning.BGM_SWAP the castle and 魔王城の地下 play the file 14_zako_late, 氷の洞窟..灼熱の洞窟 15_zako_castle.
A recording passes when the best-matching track of all is the expected one and wavref's median correlation
over its windows is >= 0.99 (sound effects lower single windows only).

  arcadia5 icecave4 plain5 ancient5 islet6 castle1 money secret
          a battle (DC_AREA with AW_GO into the sortie, A): its first WAVE, then the WAVEs after DC_CLEAR
          (zako, mid-boss, area boss, 宝の部屋 guard, 魔王の城 floor 1, 魔王城の地下 1 / 5, 月見の島)
  castle10
          魔王の城 STAGE 10: WAVE 1 (49), WAVE 5 the 魔王 (13), the second form keeps 13 playing (not
          restarted: its position in the track goes on), the final form 50
  tower1 tower2 tower15
          蜃気楼の斜塔 from WAVE 1 / 2 / 15 (force_mwave): 08 / 17; 15 = 13, its second form (WAVE 16) keeps
          13, WAVE 17 50, WAVE 18 ペコラータ 16
  tutorial training
          the two first rows of アーケディア城: 02
  dex     the monster dex (save --dex 1000: only MON_001 unknown): cards with boss songs, a mid-boss
          song, the two swapped zako songs, 49 (魔王のペット), 50, 16; the unknown card plays the map song
          01 again after a card with another song; back in the list 01
  ending  魔王の城 10 won with floors 1-9 cleared: the victory jingle, the map (01) under the orb notice,
          then ending() (the king's talk, the epilogue, the credits) plays 27_ending; then the title 01
Exit status 0 when every check passes."""
import argparse
import os
import re
import sys
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
GEN = os.path.join(ROOT, 'data', 'gen')
sys.path.insert(0, HERE)
import hrun  # noqa: E402
import battletest as bt  # noqa: E402
import wavref  # noqa: E402

D = {m.group(1): int(m.group(2)) for m in re.finditer(r'#define\s+(\w+)\s+(-?\d+)\b', open(os.path.join(GEN, 'gdata.h')).read())}
DC_AREA = bt._C['DC_AREA']
AW_GO = 1
SAVE = '--open %d --clear-all --ending --tickets 3 --coins 5000' % D['AR_SECRET']
REC = 300                  # frames per recording (5 s); checked from 1 s on
lock = threading.Lock()
fails = []


def check(log, ok, what):
    log.append('  %s %s' % ('ok  ' if ok else 'FAIL', what))
    if not ok:
        with lock:
            fails.append(what)
    return ok


class Run:
    """one runrom script with its recordings [(name, expected file stem, t0, t1)]"""
    def __init__(self, save=SAVE):
        self.s = bt.Script(save)
        self.recs = []

    def rec(self, name, expect, frames=REC, t0=1.0):
        self.s.peek('rec_' + name, 'b_stage')       # (runrom prints the frame of a peek: where the recording starts)
        self.s.raw('wav OUT/%s.wav' % name, 'wait %d' % frames, 'wavend')
        self.recs.append((name, expect, t0, frames / 60.0 - 0.2, frames))

    def to_map(self):
        self.s.raw('press A', 'wait 150')

    def sortie(self, area, men):
        """DC_AREA with AW_GO: the sortie window of (area, men); A starts the battle"""
        self.s.cmd('go', DC_AREA, [area, men, AW_GO])
        self.s.raw('wait 60', 'pokew rnd_state 4660', 'press A')
        self.s.battle()
        self.s.big_hero()
        self.s.raw('wait 20')

    def clear_to(self, wave):
        """DC_CLEAR, the next WAVE starts (its EV_STAGE_START picks the song)"""
        self.s.cmd('clear%d' % wave, bt.DC_CLEAR)
        self.s.raw('waitfor b_stage %d 1500' % wave, 'wait 30')


def battle_run(area, men, waves):
    def make():
        r = Run()
        r.to_map()
        r.sortie(D[area], men)
        at = 1
        for w, expect in waves:
            for k in range(at + 1, w + 1):
                r.clear_to(k)
            at = w
            r.rec('w%d' % w, expect)
        return r
    return make


def castle10():
    r = Run()
    r.to_map()
    r.sortie(D['AR_CASTLE'], D['D_CASTLE_MEN'])
    r.rec('w1', '49_castle_last_waves')
    for w in range(2, 6):
        r.clear_to(w)
    r.rec('w5', '13_boss_last')               # under TALK_MAOU1
    r.s.talk()
    r.s.cmd('f0', bt.DC_CLEAR)
    r.s.raw('waitfor b_phase %d 1500' % bt.PH_TALK)
    r.rec('form1', '13_boss_last')            # TALK_MAOU2 after the form change
    r.s.talk()
    r.s.cmd('f1', bt.DC_CLEAR)
    r.s.raw('waitfor b_phase %d 1500' % bt.PH_TALK)
    r.rec('form2', '50_boss_last_final')
    r.cont = [('w5', 'form1')]
    return r


def tower(first):
    def make():
        r = Run()
        r.to_map()
        r.s.raw('poke force_mwave %d' % first)
        r.s.cmd('go', DC_AREA, [D['AR_MIRAGE'], 0, 0])
        r.s.raw('wait 60', 'pokew rnd_state 4660', 'press A')     # 挑戦する
        r.s.battle()
        r.s.big_hero()
        r.s.raw('wait 20')
        if first == 1:
            r.rec('w1', '08_boss_field')
        elif first == 2:
            r.rec('w2', '17_boss_margos')
        else:
            r.rec('w15', '13_boss_last')          # under TALK_MAOU1
            r.s.talk()
            r.s.cmd('f0', bt.DC_CLEAR)
            r.s.raw('waitfor b_phase %d 1500' % bt.PH_TALK)
            r.rec('w16', '13_boss_last')
            r.s.talk()
            r.s.cmd('f1', bt.DC_CLEAR)
            r.s.raw('waitfor b_phase %d 1500' % bt.PH_TALK)
            r.rec('w17', '50_boss_last_final')
            r.s.talk()
            r.s.hero_hp('hp17')
            r.s.cmd('calm17', bt.DC_CALM)
            r.s.cmd('f2', bt.DC_CLEAR)
            r.s.talk()                            # TALK_MAOU_DEFEAT
            r.s.raw('waitfor b_phase %d 1500' % bt.PH_REWARD, 'wait 90', 'press A', 'waitfor b_stage 18 1500', 'wait 30')
            r.rec('w18', '16_boss_secret')
            r.cont = [('w15', 'w16')]
        return r
    return make


def first_rows(up, sortie):
    """アーケディア城's list opened by DC_AREA (no focus: the newest open STAGE, row 6), UP x up, A (A)"""
    def make():
        r = Run()
        r.to_map()
        r.s.cmd('go', DC_AREA, [D['AR_ARCADIA'], 0, 0])
        r.s.raw('wait 60')
        for _ in range(up):
            r.s.raw('press UP', 'wait 8')
        r.s.raw('pokew rnd_state 4660', 'press A')
        if sortie:
            r.s.raw('wait 40', 'press A')
        r.s.battle()
        r.s.raw('wait 20')
        r.rec('battle', '02_zako_early')
        return r
    return make


# in the order visited (No.001 RIGHT .., then LEFT over No.001 to the end of the list); MON_001 (unknown, its card
# plays the map song again) comes after a known card with another song
DEX_CARDS = [('MON_114', '08_boss_field'), ('MON_089', '17_boss_margos'), ('MON_001', '01_opening'),
             ('MON_071', '14_zako_late'), ('MON_095', '09_boss_cave'), ('MON_063', '04_dragon'),
             ('MON_117', '16_boss_secret'), ('MON_119', '50_boss_last_final'), ('MON_100', '49_castle_last_waves'),
             ('MON_005', '15_zako_castle')]
CARD_KEY = 45              # frames between two card keys (a card takes about 30 frames to load and fade in)


def dex():
    t = open(os.path.join(GEN, 'tables.c')).read()
    order = [int(x) for x in re.search(r'dex_mon\[N_DEX_MON\] = \{([^}]*)\}', t).group(1).split(',')]
    n = len(order)
    r = Run(SAVE.replace('--clear-all', '--clear-all --dex 1000'))
    r.to_map()
    r.s.raw('press Y', 'wait 40')
    for _ in range(4):
        r.s.raw('press DOWN', 'wait 6')
    r.s.raw('press A', 'wait 90', 'press A', 'wait 60')       # 図鑑, the card of No.001
    pos = 0
    for mon, expect in DEX_CARDS:
        want = order.index(D[mon])
        d = (want - pos) % n
        key, k = ('RIGHT', d) if d <= n // 2 else ('LEFT', n - d)
        for _ in range(k):
            r.s.raw('press %s' % key, 'wait %d' % CARD_KEY)
        pos = want
        r.s.raw('wait 20', 'shot OUT/No%03d.ppm' % (pos + 1))
        r.rec('No%03d_%s' % (pos + 1, mon), expect, 240)
    r.s.raw('press B', 'wait 30')
    r.rec('list', '01_opening', 240)
    return r


def ending():
    r = Run('--open %d --clear %d:%d --tickets 3' % (D['AR_CASTLE'], D['AR_CASTLE'], (1 << (D['D_CASTLE_MEN'] - 1)) - 1))
    r.to_map()
    r.sortie(D['AR_CASTLE'], D['D_CASTLE_MEN'])
    for w in range(2, 6):
        r.s.cmd('c%d' % w, bt.DC_CLEAR)
    r.s.raw('waitfor b_stage 5 1500')
    r.s.talk()
    for f in range(2):
        r.s.cmd('f%d' % f, bt.DC_CLEAR)
        r.s.raw('waitfor b_phase %d 1500' % bt.PH_TALK)
        r.s.talk()
    r.s.hero_hp('hp')
    r.s.cmd('calm', bt.DC_CALM)
    r.s.cmd('f2', bt.DC_CLEAR)
    r.s.talk()                                    # TALK_MAOU_DEFEAT
    r.s.raw('waitfor b_phase %d 1500' % bt.PH_VICTORY)
    # then A every 3 s: the results, the orb notice on the map, ending() (6 + 6 talk pages, the credits), the title
    r.s.raw('wav OUT/after.wav')
    for _ in range(ENDING_PRESSES):
        r.s.raw('wait %d' % (ENDING_GAP - 3), 'press A')
    r.s.raw('wait %d' % ENDING_GAP, 'wavend')
    r.s.peek('ending_seen', 'sv+%d' % (12 + 4 + 4 + 4 * D['N_AREA'] + 14))    # save.h Save.ending_seen
    r.ending = True
    return r


ENDING_PRESSES = 16
ENDING_GAP = 180           # frames from one A to the next


RUNS = {
    'arcadia5': battle_run('AR_ARCADIA', 5, [(1, '02_zako_early'), (3, '08_boss_field')]),
    'icecave4': battle_run('AR_ICECAVE', 4, [(1, '15_zako_castle'), (3, '04_dragon')]),
    'plain5': battle_run('AR_PLAIN', 5, [(3, '17_boss_margos')]),
    'ancient5': battle_run('AR_ANCIENT', 5, [(3, '09_boss_cave')]),
    'islet6': battle_run('AR_ISLET', D['D_TREASURE_MEN'], [(1, '03_zako_mid'), (3, '20_boss_mimikkun')]),
    'castle1': battle_run('AR_CASTLE', 1, [(1, '14_zako_late'), (5, '08_boss_field')]),
    'money': battle_run('AR_CASTLE', D['T_MONEY_MEN'], [(1, '14_zako_late'), (5, '20_boss_mimikkun')]),
    'secret': battle_run('AR_SECRET', 1, [(1, '16_boss_secret')]),
    'castle10': castle10,
    'tower1': tower(1),
    'tower2': tower(2),
    'tower15': tower(15),
    'tutorial': first_rows(6, False),
    'training': first_rows(5, True),
    'dex': dex,
    'ending': ending,
}


def peek_frames(wd, names):
    """the frame of each peek (runrom's stdout, in the order of the script's peeks)"""
    fr = [int(m.group(1)) for m in re.finditer(r'^peek .*\(frame (\d+)\)', open(os.path.join(wd, '_stdout.txt')).read(), re.M)]
    return dict(zip(names, fr))


def analyse(st, name, r, wd, out):
    log = ['%s:' % name]
    if r.s.names and not all(k in out for k in r.s.names):
        check(log, False, '%s: the script did not run to its end (see %s/_stdout.txt)' % (name, wd))
    frames = peek_frames(wd, r.s.names)
    found = {}
    for rec, expect, t0, t1, nf in r.recs:
        path = os.path.join(wd, rec + '.wav')
        if not os.path.exists(path):
            check(log, False, '%s %s: no recording' % (name, rec))
            continue
        x, rate = wavref.load_wav(path)
        best = wavref.which(st, x, rate, t0)
        i = st.find(expect)
        m = wavref.match(st, i, x, rate, t0, t1)
        m.update(i=i, frame=frames.get('rec_' + rec), spf=len(x) / float(nf))
        found[rec] = m
        check(log, best[0][1] == i and m['median'] >= 0.99, '%s %s: %s (best %s %.3f)' % (
            name, rec, wavref.report(st, i, m), st.tracks[best[0][1]]['name'], best[0][0]))
    for a, b in getattr(r, 'cont', []):
        if a in found and b in found and found[a]['frame'] is not None and found[b]['frame'] is not None:
            ma, mb = found[a], found[b]
            one, lp = st.looped(ma['i'])
            step = st.pitch / 4096.0
            want = ma['pos'] + (mb['frame'] - ma['frame']) * ma['spf'] * step   # had it gone on since a
            if want >= len(one):
                want = lp + (want - lp) % (len(one) - lp)
            sr = step * 32000
            check(log, abs(mb['pos'] - want) < 0.3 * sr, '%s %s -> %s: the song goes on, not restarted (%s at %.2f s of the track, '
                  '%.2f s if it had played on since %s)' % (name, a, b, b, mb['pos'] / sr, want / sr, a))
    if getattr(r, 'ending', False):
        ending_check(st, log, name, wd, out)
    return log


def ending_check(st, log, name, wd, out):
    """the recording from the victory on, one label per second (the candidates VICTORY / OPENING / ENDING, '?'
    when none has a median >= 0.99); the runs must be VICTORY, OPENING (the map), ENDING, OPENING (the title)"""
    path = os.path.join(wd, 'after.wav')
    if not os.path.exists(path):
        check(log, False, '%s: no recording' % name)
        return
    x, rate = wavref.load_wav(path)
    cand = [st.find(k) for k in ('BGM_VICTORY', 'BGM_OPENING', 'BGM_ENDING')]
    seq = []
    for t0 in range(int(len(x) / rate) - 1):
        ms = [(wavref.match(st, i, x, rate, t0 + 0.0, t0 + 1.0)['median'], i) for i in cand]
        c, i = max(ms)
        seq.append(st.tracks[i]['name'][4:] if c >= 0.99 else '?')
    runs = []
    for k, n in enumerate(seq):
        if n != '?' and (not runs or runs[-1][0] != n):
            runs.append([n, k, k])
        elif n != '?':
            runs[-1][2] = k
    log.append('  %s: %s' % (name, ' '.join('%s %d-%d s' % (n, a, b + 1) for n, a, b in runs)))
    order = [n for n, a, b in runs]
    check(log, order == ['VICTORY', 'OPENING', 'ENDING', 'OPENING'],
          '%s: the victory jingle, the map song (the orb notice), BGM_ENDING from ending() (the talk, epilogue, '
          'credits), the title song (%s)' % (name, ' '.join(order)))
    end = [b - a + 1 for n, a, b in runs if n == 'ENDING']
    check(log, bool(end) and end[0] >= (6 + 6) * ENDING_GAP // 60 - 6,
          '%s: BGM_ENDING for %s s (12 talk pages, A every %d frames)' % (name, end[0] if end else 0, ENDING_GAP))
    check(log, out.get('ending_seen') == [1], '%s: sv.ending_seen %s after the credits' % (name, out.get('ending_seen')))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', nargs='?', default=hrun.ROM)
    ap.add_argument('-o', dest='out')
    ap.add_argument('--only')
    ap.add_argument('-j', type=int, default=4)
    a = ap.parse_args()
    syms = hrun.symbols(a.rom)
    for need in ('dbg_cmd', 'force_mwave', 'dbg_map_go'):
        hrun.addr(syms, need)
    out = os.path.abspath(a.out or tempfile.mkdtemp(prefix='bgmroute_'))
    names = a.only.split(',') if a.only else list(RUNS)
    st = wavref.Stream()
    wavref.which(st, [0.0] * 32000, 32000, 0)       # decode every track and cache its search, before the threads

    def one(name):
        r = RUNS[name]()
        wd = os.path.join(out, name)
        os.makedirs(wd, exist_ok=True)
        res = r.s.run(a.rom, wd)
        return analyse(st, name, r, wd, res)
    with ThreadPoolExecutor(a.j) as ex:
        for log in ex.map(one, names):
            print('\n'.join(log))
    fails.extend(bt.fails)              # (Script.run: a runrom that stopped early)
    print('%d check(s) failed' % len(fails) if fails else 'all checks passed', '(output in %s)' % out)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
