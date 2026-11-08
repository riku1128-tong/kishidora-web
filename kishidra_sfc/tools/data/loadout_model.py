"""Python models of the loadout rules shared by make_data.py (generated tables) and the harness tests
(tools/harness/loadtest.py): which specials a loadout can fire, the one-weapon table, and the effect
states.  Everything works on the port's indices (skill / special / effect / item numbers of gdata.h).

  web_reach(lists)       adventure.gd reachable_specials (shipped build): the BFS over (weapon positions,
                         combo history), a straight port.  lists = skill lists, one per weapon (255 / None
                         entries are dropped, an empty list is no weapon).  Returns the special indices.
  snes_reach(lists)      the algorithm of loadout.c sp_reach (history reduced to its longest suffix that
                         is a proper prefix of a candidate, transition table, sweeps): must equal web_reach.
  kick_only(sk)          battle_core.gd kick_only (kick + buffs only)
  SoloTable              the one-weapon table of loadout.c solo_ok_sk: classes, the bit table, lookup
  fx_rule(...) / fx_state(...)
                         desc.gd fx_state: the generated class / parameter / note texts per effect, and
                         the state of a loadout slot (the oracle of the C fx_state)
"""
import itertools

NONE = 255


class Model:
    def __init__(self, s):
        """s = safari.get()"""
        self.s = s
        self.skill_ids = [r['id'] for r in s.tables['SKILL_TABLE']]
        self.sk_index = {k: i for i, k in enumerate(self.skill_ids)}
        self.hits = [s.skills[k]['hits'] for k in self.skill_ids]
        self.buff = [s.skills[k]['buff'] for k in self.skill_ids]
        self.hmax = s.num('COMBO_HISTORY_MAX')
        self.sps = []                   # (special index, sequence of skill indices)
        for i, p in enumerate(s.specials):
            q = [p['sk1'], p['sk2']] + ([p['sk3']] if p['sk3'] != 'SK_NONE' else [])
            self.sps.append((i, tuple(self.sk_index[x] for x in q)))
        self.kick = self.sk_index['SK_KICK']

    # ------------------------------------------------------------ reachable specials
    @staticmethod
    def _clean(lists):
        ws = []
        for l in lists:
            w = [x for x in (l or []) if x is not None and x != NONE]
            if w:
                ws.append(w)
        return ws

    def _match(self, h):
        """adventure.gd _reach_match: the longest special equal to the end of the history (ties: the
        first in the table)"""
        best, bl = None, 0
        for i, q in self.sps:
            n = len(q)
            if n < 2 or len(h) < n or n <= bl:
                continue
            if tuple(h[-n:]) == q:
                best, bl = i, n
        return best

    def web_reach(self, lists):
        ws = self._clean(lists)
        have = {x for w in ws for x in w}
        want = {i for i, q in self.sps if all(x in have for x in q)}
        found = set()
        if want:
            keep = self.hmax - 1
            start = (tuple(0 for _ in ws), ())
            seen = {start}
            queue = [start]
            while queue and len(found) < len(want):
                pos, hist = queue.pop()
                for i, sk in enumerate(ws):
                    p = list(pos)
                    x = sk[p[i]]
                    p[i] = (p[i] + 1) % len(sk)
                    h = hist
                    if self.hits[x] > 0:
                        h2 = (list(hist) + [x])[-self.hmax:]
                        m = self._match(h2)
                        if m is not None:
                            found.add(m)
                            h = ()
                        else:
                            h = tuple(h2[max(0, len(h2) - keep):])
                    nx = (tuple(p), h)
                    if nx not in seen:
                        seen.add(nx)
                        queue.append(nx)
        return sorted(found)

    def snes_reach(self, lists, stats=None):
        """loadout.c sp_reach, step by step (candidates, prefixes, transitions, sweeps)"""
        ws = self._clean(lists)[:3]
        have = {x for w in ws for x in w}
        cand = [(i, q) for i, q in self.sps if all(x in have for x in q)]
        if not cand:
            return []
        pre = [()]
        for i, q in cand:
            for L in range(1, len(q)):
                if q[:L] not in pre:
                    pre.append(q[:L])
        assert len(pre) <= 28

        def step(pid, x):
            h = pre[pid] + (x,)
            best = None
            for i, q in cand:                      # 3 beats 2, ties: the first
                if len(q) == 3 and len(h) >= 3 and h[-3:] == q:
                    best = i
                    break
                if len(q) == 2 and best is None and len(h) >= 2 and h[-2:] == q:
                    best = i
            if best is not None:
                return 0x80 | best
            if len(h) >= 2 and h[-2:] in pre:
                return pre.index(h[-2:])
            return pre.index(h[-1:]) if h[-1:] in pre else 0
        tr = {}
        for pid in range(len(pre)):
            for i, w in enumerate(ws):
                for k, x in enumerate(w):
                    tr[pid, i, k] = step(pid, x) if self.hits[x] > 0 else pid
        n = [len(w) for w in ws]
        vis, done, found = {((0, 0, 0), 0)}, set(), set()
        sweeps = 0
        while True:
            new = sorted(vis - done)
            if not new or len(found) == len(cand):
                break
            sweeps += 1
            for pos, pid in new:
                done.add((pos, pid))
                for i in range(len(ws)):
                    e = tr[pid, i, pos[i]]
                    if e & 0x80:
                        found.add(e & 0x7f)
                        e = 0
                    p = list(pos)
                    p[i] = (p[i] + 1) % n[i]
                    vis.add((tuple(p), e))
        if stats is not None:
            stats.update(prefixes=len(pre), states=len(vis), sweeps=sweeps)
        return sorted(found)

    def kick_only(self, sk):
        """battle_core.gd kick_only: kick and buffs only"""
        kick = False
        for x in sk:
            if x is None or x == NONE:
                continue
            if x == self.kick:
                kick = True
            elif self.hits[x] > 0:
                return False
        return kick


class SoloTable:
    """loadout.c solo_ok_sk: one weapon alone can fire a special, by the classes of its hit skills in
    order.  Class 0 = none, 1..K = the hit skills used by some special, K + 1 = any other hit skill (all
    alike: a history place, never a match); buffs (hits 0) leave the history alone, so with one weapon
    they can be dropped from the list.  Bit c0 + C c1 + C^2 c2 (C = K + 2) of the table."""

    def __init__(self, m):
        self.m = m
        spsk = sorted({x for _, q in m.sps for x in q})
        assert all(m.hits[x] > 0 for x in spsk), 'a special uses a buff skill'
        self.K = len(spsk)
        self.C = self.K + 2
        assert self.C <= 32 and len(m.skill_ids) <= 64
        self.cls = []
        for x in range(len(m.skill_ids)):
            self.cls.append(NONE if m.hits[x] == 0 else (1 + spsk.index(x) if x in spsk else self.K + 1))
        rep = {1 + i: x for i, x in enumerate(spsk)}
        rep[self.K + 1] = next(x for x in range(len(m.skill_ids)) if self.cls[x] == self.K + 1)
        self.rep = rep
        C = self.C
        self.bits = bytearray((C ** 3 + 7) // 8)
        # every class tuple of 1..3 hit skills (repeats too: a list may name a skill twice)
        for n in (1, 2, 3):
            for t in itertools.product(range(1, C), repeat=n):
                if m.web_reach([[rep[c] for c in t]]):
                    i = self.index(list(t))
                    self.bits[i >> 3] |= 1 << (i & 7)

    def classes(self, sk):
        return [self.cls[x] for x in sk if x is not None and x != NONE and self.cls[x] != NONE]

    def index(self, c):
        c = list(c) + [0] * (3 - len(c))
        return c[0] + self.C * c[1] + self.C * self.C * c[2]

    def lookup(self, sk):
        """(can fire, kick-only) as loadout.s solo_eval computes them"""
        c = self.classes(sk)
        if not c:
            return False, False
        i = self.index(c)
        return bool(self.bits[i >> 3] >> (i & 7) & 1), all(x == self.cls[self.m.kick] for x in c)


# ---------------------------------------------------------------- effect states (desc.gd fx_state)
FR = ['FR_NONE', 'FR_ON', 'FR_COND', 'FR_RESO', 'FR_TYPE', 'FR_ALLSR', 'FR_BOOST', 'FR_BERSERK']
FS_NONE, FS_ON, FS_COND, FS_NOW, FS_WAIT, FS_OFF = 0, 1, 2, 3, 4, 5   # desc.gd STATE_WORD index
SLOT = ['MAIN', 'SUB', 'THIRD']        # the port's slot labels (web: メイン サブ レンタル)


def fx_rule(s, k, elements, wtypes):
    """class, parameter, note (FXS: the 条件つき note), off (FXO: why it does not work) of effect k:
    desc.gd fx_state without the loadout (the per-effect part).  elements / wtypes = the port's lists."""
    import descs
    e = s.effects[k]
    kind, v1, v2, v3 = e['kind'], e['v1'], e['v2'], e['v3']
    race = lambda rc: descs._race(s, rc)   # noqa: E731
    if k == 'FX_NONE':                    # (only the id: another effect of kind FK_NONE is 発動)
        return 'FR_NONE', 0, '', ''
    if kind == 'FK_RESONANCE':
        return 'FR_RESO', elements.index(v1), '', '%s武器を装備していない' % descs._elem(v1)
    if kind == 'FK_TYPE_UP':
        return 'FR_TYPE', wtypes.index(v1), '', '%sを装備していない' % descs.WTYPE.get(v1, '')
    if kind == 'FK_ALL_SR':
        return 'FR_ALLSR', 0, '', '全部 SR・SR+ ではない'
    if kind == 'FK_BOOST':
        weak = s.elements.get(v1, {}).get('strong', '')
        return 'FR_BOOST', elements.index(v1), '%s敵がいるとき' % descs._elem(weak), '%s武器を装備していない' % descs._elem(v1)
    if kind == 'FK_BERSERK_UP':
        return 'FR_BERSERK', 0, 'ベルセルクをためたとき', 'ベルセルクのスキルがない'
    note = None
    if kind == 'FK_KILLER':
        note = '%sがいるとき' % (race(v1) + ('・' + race(v3) if str(v3) != 'RC_NONE' else ''))
    elif kind == 'FK_PANIC':
        note = '%sサイズの敵がいるとき' % descs.SIZE.get(v1, '')
    elif kind == 'FK_FULLHP_UP':
        note = 'HP が満タンのとき'
    elif kind in ('FK_PINCH_UP', 'FK_HAISUI'):
        note = 'HP が最大の %d%% 以下のとき' % v1
    elif kind == 'FK_COMBO_UP':
        note = 'コンボが %d 以上のとき' % v1
    elif kind == 'FK_ROUND_POWER':
        note = 'WAVE 2から'
    elif kind == 'FK_KILL_POWER':
        note = '敵を倒すほど'
    elif kind == 'FK_GUARD_CUT':
        if str(v2) == 'GC_ALL':
            return 'FR_ON', 0, '', ''
        if str(v2) == 'GC_ELEMENT':
            note = '%s敵の攻撃のとき' % descs._elem(v3)
        elif str(v2) == 'GC_RACE':
            note = '%sの攻撃のとき' % race(v3)
        else:
            note = '%sの戦闘のとき' % ('洞窟' if str(v3) == 'SCN_CAVE' else '草原')
    if note is not None:
        return 'FR_COND', 0, note, ''
    return 'FR_ON', 0, '', ''


def fx_state(s, lo, k, fx=None):
    """desc.gd fx_state, straight port: lo = {'weapons': [None or (web item id, [web skill ids])] * 3,
    'shield': web id or None}; fx = the effect of slot k (default: the item's).  Returns
    (state, note) with the note as the web text ('対象 MAIN SUB' with the port's slot labels)."""
    import descs
    ws = lo['weapons']
    if fx is None:
        if k == 3:
            fx = s.shields[lo['shield']]['effect'] if lo['shield'] else 'FX_NONE'
        else:
            fx = s.weapons[ws[k][0]]['effect'] if ws[k] else 'FX_NONE'
    if fx == 'FX_NONE':
        return FS_NONE, ''
    if k in (1, 2):
        return FS_OFF, 'メインに装備すると発動'
    e = s.effects[fx]

    def having(t, el):
        return [SLOT[i] for i in range(3) if ws[i] and (t == '' or s.weapons[ws[i][0]]['wtype'] == t)
                and (el == '' or s.weapons[ws[i][0]]['element'] == el)]
    kind = e['kind']
    if kind == 'FK_RESONANCE':
        m = having('', e['v1'])
        return (FS_ON, '対象 ' + ' '.join(m)) if m else (FS_OFF, '%s武器を装備していない' % descs._elem(e['v1']))
    if kind == 'FK_TYPE_UP':
        m = having(e['v1'], '')
        return (FS_ON, '対象 ' + ' '.join(m)) if m else (FS_OFF, '%sを装備していない' % descs.WTYPE.get(e['v1'], ''))
    if kind == 'FK_ALL_SR':
        ranks = [r['id'] for r in s.tables['RARITY_TABLE']]
        sr = 4
        ok = ranks.index(s.shields[lo['shield']]['rarity'] if lo['shield'] else 'RARE_N') >= sr
        for i in range(3):
            if not ws[i] or ranks.index(s.weapons[ws[i][0]]['rarity']) < sr:
                ok = False
        return (FS_ON, '') if ok else (FS_OFF, '全部 SR・SR+ ではない')
    if kind == 'FK_BOOST':
        m = having('', e['v1'])
        weak = s.elements.get(e['v1'], {}).get('strong', '')
        return (FS_COND, '%s敵がいるとき' % descs._elem(weak)) if m else (FS_OFF, '%s武器を装備していない' % descs._elem(e['v1']))
    if kind == 'FK_BERSERK_UP':
        for w in ws:
            for x in (w[1] if w else []):
                if s.skills[x]['buff'] == 'BF_CRIT':
                    return FS_COND, 'ベルセルクをためたとき'
        return FS_OFF, 'ベルセルクのスキルがない'
    race = lambda rc: descs._race(s, rc)   # noqa: E731
    if kind == 'FK_KILLER':
        return FS_COND, '%sがいるとき' % (race(e['v1']) + ('・' + race(e['v3']) if str(e['v3']) != 'RC_NONE' else ''))
    if kind == 'FK_PANIC':
        return FS_COND, '%sサイズの敵がいるとき' % descs.SIZE.get(e['v1'], '')
    if kind == 'FK_FULLHP_UP':
        return FS_COND, 'HP が満タンのとき'
    if kind in ('FK_PINCH_UP', 'FK_HAISUI'):
        return FS_COND, 'HP が最大の %d%% 以下のとき' % e['v1']
    if kind == 'FK_COMBO_UP':
        return FS_COND, 'コンボが %d 以上のとき' % e['v1']
    if kind == 'FK_ROUND_POWER':
        return FS_COND, 'WAVE 2から'
    if kind == 'FK_KILL_POWER':
        return FS_COND, '敵を倒すほど'
    if kind == 'FK_GUARD_CUT':
        v2 = str(e['v2'])
        if v2 == 'GC_ALL':
            return FS_ON, ''
        if v2 == 'GC_ELEMENT':
            return FS_COND, '%s敵の攻撃のとき' % descs._elem(e['v3'])
        if v2 == 'GC_RACE':
            return FS_COND, '%sの攻撃のとき' % race(e['v3'])
        return FS_COND, '%sの戦闘のとき' % ('洞窟' if str(e['v3']) == 'SCN_CAVE' else '草原')
    return FS_ON, ''
