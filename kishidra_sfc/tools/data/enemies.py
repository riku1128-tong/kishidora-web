"""Enemy stats of the surface world, computed the way BattleCore.make_enemy() does.

Everything that make_enemy() derives from (area, 面, WAVE, monster) is computed here
once; the SFC game only applies the run-time parts (difficulty, turn jitter, lucky
rolls, awakening, forms).  The rules follow scripts/battle_core.gd (surface branch:
not classic, not directional/sustain, not tutorial).
"""
import math


def idiv(a, b):
    """GDScript integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


class Core:
    def __init__(self, s):
        self.s = s
        self.T = s.tuning
        self.area = ''
        self.men = 0
        self.stage = 1
        self.stage_count = 3
        self.hp_pct = self.atk_pct = self.boss_pct = 100
        self.mid_def = ''
        self.mid_placed = False
        self.mirage = False

    def num(self, k):
        return self.s.num(k)

    def mul_pct(self, v, p):
        if p == 100:
            return v
        return min(idiv(v * p, 100), self.num('DAMAGE_MAX'))

    def scale_pct(self, v, p):
        return min(idiv(v * p, 100), 65535)

    # ------------------------------------------------------------ area / 面
    def stage_count_for(self, area, men):
        if area == self.num('AREA_CASTLE'):
            return self.num('CASTLE_STAGES')
        if area == self.num('AREA_SECRET'):
            return self.num('SECRET_STAGES')
        if men == self.num('TREASURE_MEN') and self.treasure_row(area):
            return self.num('TREASURE_STAGES')
        return self.s.areas[area].get('waves', self.num('MEN_STAGES'))

    def treasure_row(self, area):
        for t in self.s.tables['TREASURE_TABLE']:
            if t['area'] == area:
                return t
        return {}

    def start_men(self, area, men):
        self.area = area
        self.men = men
        self.stage = 1
        self.stage_count = self.stage_count_for(area, men)
        self.area_pcts()
        self.mid_def = ''
        self.mid_placed = False

    def is_castle(self):
        return self.area == self.num('AREA_CASTLE')

    def is_secret(self):
        return self.area == self.num('AREA_SECRET')

    def is_treasure(self):
        return self.men == self.num('TREASURE_MEN') and bool(self.treasure_row(self.area))

    def area_pcts(self):
        a = self.s.areas[self.area]
        lv = self.num('AREA_MEN') if self.is_treasure() else self.men
        step = (lv - 1) * (self.num('CASTLE_STEP_PCT') if self.is_castle() else self.num('MEN_STEP_PCT'))
        self.hp_pct = self.scale_pct(a['hp_pct'], 100 + step)
        self.atk_pct = self.scale_pct(a['atk_pct'], 100 + step)
        self.boss_pct = a['boss_pct']

    def boss_hp_up(self):
        return not self.is_treasure() and not self.is_secret()

    def stage_boss_kind(self):
        if self.stage != self.stage_count and not (self.is_castle() and self.men == self.num('CASTLE_MEN')):
            return 0
        if self.is_castle() or self.is_secret() or self.men == self.num('AREA_MEN'):
            return 2
        if self.is_treasure():
            return 2
        return 1 if self.men == self.num('AREA_MEN') - 1 else 0

    def forms_of(self, mon):
        f = self.num('MAOU_FORMS')
        return f if mon in f else []

    def fixed_boss(self):
        if self.is_castle():
            if self.men == self.num('CASTLE_MEN'):
                return [self.num('CASTLE_FINAL_HP')[self.stage - 1], self.num('CASTLE_FINAL_ATK')[self.stage - 1]]
            if 1 <= self.men < self.num('CASTLE_MEN'):
                return [self.num('CASTLE_BOSS_HP')[self.men - 1], self.num('CASTLE_BOSS_ATK')[self.men - 1]]
        elif self.is_secret():
            return [self.num('SECRET_BOSS_HP'), self.num('SECRET_BOSS_ATK')]
        return [0, 0]

    def pool_max(self):
        top = [0, 0]
        for m in self.s.pools.get(self.area, []):
            r = self.s.enemies[m]
            sz = self.s.sizes[r['size']]
            rc = self.s.races[r['race']]
            top[0] = max(top[0], self.scale_pct(idiv(sz['hp'] * rc['hp_pct'], 100), self.hp_pct))
            top[1] = max(top[1], self.scale_pct(idiv(sz['atk'] * rc['atk_pct'], 100), self.atk_pct))
        return top

    def midboss_atk(self, mon):
        return self.scale_pct(self.pool_max()[1], self.num('MIDBOSS_ATK_PCT'))

    def boss_atk_floor(self):
        return idiv(idiv(self.pool_max()[1] * self.T['BOSS_ATK_FLOOR_PCT'], 100) * self.T['ENEMY_ATK_PCT'].get(self.area, 100), 100)

    def boss_home(self, mon):
        for a in self.s.tables['AREA_TABLE']:
            if a.get('boss', '') == mon and a.get('mid', 'MON_NONE') != 'MON_NONE':
                return a['id']
        return ''

    def mid_ref(self, home):
        c = Core(self.s)
        c.start_men(home, self.num('AREA_MEN') - 1)
        c.mid_def = self.s.areas[home]['mid']
        return c.make_enemy(c.mid_def)

    @staticmethod
    def dmg_per_count(atk, sp, every, turns, combo_pct, hits):
        per = float(atk)
        if sp > 0 and every > 0:
            per = (every * atk + sp) / float(every + 1)
        return per * (1.0 + combo_pct / 100.0 * max(0, hits - 1)) / max(1.0, turns)

    def reboss_home(self, e):
        if e['kind'] != 'EK_BOSS' or not self.is_castle():
            return ''
        last = self.num('CASTLE_MEN')
        if self.men < 1 or self.men >= last:
            return ''
        return self.boss_home(e['mon'])

    @staticmethod
    def reboss_pct(v, pct):
        return idiv(v * pct + 50, 100)

    def balance_hp(self, e, zako_base):
        t = self.s.hp_balance
        if e['kind'] == 'EK_LUCKY':
            return -1
        if e['kind'] != 'EK_BOSS' and not e['mid']:
            pct = t['zako_pct'].get(self.area, 0)
            return max(1, idiv(zako_base * pct, 100)) if pct > 0 else -1
        if e['mid']:
            return t['mid'].get(self.area, -1)
        if self.is_treasure():
            return -1
        if self.is_castle():
            last = self.num('CASTLE_MEN')
            if self.men >= last:
                fin = t['final'].get(self.area, {})
                forms = self.forms_of(e['mon'])
                if forms:
                    fl = fin.get('forms', [])
                    fi = forms.index(e['mon'])
                    return fl[fi] if fi < len(fl) else -1
                return fin.get('rush', -1)
            cl = t['castle'].get(self.area, [])
            v = cl[self.men - 1] if 1 <= self.men <= len(cl) else -1
            rb = self.reboss_home(e)
            if rb:
                b = v if self.men in self.T['REBOSS_FROM_TABLE'].get(self.area, []) else t['boss'].get(rb, -1)
                if b > 0:
                    return self.reboss_pct(b, self.T['REBOSS_HP_PCT'])
            return v
        return t['boss'].get(self.area, -1)

    def balance_atk(self, e, zako_atk, zako_sp):
        t = self.s.atk_balance
        if e['kind'] == 'EK_LUCKY':
            return []
        if e['kind'] != 'EK_BOSS' and not e['mid']:
            pct = t['zako_pct'].get(self.area, 0)
            cz = t['castle_zako_pct'].get(self.area, [])
            if self.is_castle() and 1 <= self.men <= len(cz):
                pct = cz[self.men - 1]
            return [max(1, idiv(zako_atk * pct, 100)), max(1, idiv(zako_sp * pct, 100))] if pct > 0 else []
        if e['mid']:
            return t['mid'].get(self.area, [])
        if self.is_treasure():
            return []
        if self.is_castle():
            last = self.num('CASTLE_MEN')
            if self.men >= last:
                fin = t['final'].get(self.area, {})
                forms = self.forms_of(e['mon'])
                if forms:
                    fl = fin.get('forms', [])
                    fi = forms.index(e['mon'])
                    return fl[fi] if fi < len(fl) else []
                rl = fin.get('rush', [])
                return rl[self.stage - 1] if 1 <= self.stage <= len(rl) else []
            cl = t['castle'].get(self.area, [])
            v = cl[self.men - 1] if 1 <= self.men <= len(cl) else []
            rb = self.reboss_home(e)
            if rb:
                b = v if self.men in self.T['REBOSS_FROM_TABLE'].get(self.area, []) else t['boss'].get(rb, [])
                if len(b) >= 2:
                    return [self.reboss_pct(b[0], self.T['REBOSS_ATK_PCT']), self.reboss_pct(b[1], self.T['REBOSS_ATK_PCT'])]
            return v
        return t['boss'].get(self.area, [])

    def slow_boss(self, e, turns):
        return e['kind'] == 'EK_BOSS' and turns > self.T['SLOW_BOSS_TURNS'] and not self.forms_of(e['mon'])

    def is_mid_species(self, mon):
        return any(a.get('mid', 'MON_NONE') == mon for a in self.s.tables['AREA_TABLE'])

    def stun_resist_pct(self, mon):
        if mon in self.T['MON_STUN_PCT']:
            return self.T['MON_STUN_PCT'][mon]
        hard = mon == self.num('AWAKEN_MON') or mon in self.num('MAOU_FORMS')
        return self.T['SECRET_STUN_PCT'] if hard else 100

    def make_enemy(self, mon):
        s, T = self.s, self.T
        r = s.enemies[mon]
        sz = s.sizes[r['size']]
        rc = s.races[r['race']]
        e = {'mon': mon, 'kind': r['kind'], 'mid': False, 'ability': ''}
        lucky = r['kind'] == 'EK_LUCKY'
        boss = r['kind'] == 'EK_BOSS'
        base_hp = idiv(sz['hp'] * rc['hp_pct'], 100)
        base_atk = 0 if lucky else idiv(sz['atk'] * rc['atk_pct'], 100)
        hp_v = base_hp
        atk_v = base_atk
        sp_v = sz['sp_atk']
        turns = self.num('LUCKY_ESCAPE_TURNS') if lucky else max(1, sz['turns'] + rc['turns'])
        hp_v = self.scale_pct(hp_v, self.hp_pct)
        atk_v = self.scale_pct(atk_v, self.atk_pct)
        sp_v = self.scale_pct(sp_v, self.atk_pct)
        if boss:
            hp_v = self.scale_pct(self.scale_pct(hp_v, self.num('BOSS_HP_PCT')), self.boss_pct)
            atk_v = self.scale_pct(self.scale_pct(atk_v, self.num('BOSS_ATK_PCT')), self.boss_pct)
            sp_v = self.scale_pct(self.scale_pct(self.scale_pct(sp_v, self.num('BOSS_ATK_PCT')), self.boss_pct), self.num('BOSS_SP_PCT'))
            fixed = self.fixed_boss()
            if fixed[0] > 0:
                hp_v = fixed[0]
                atk_v = fixed[1]
                if base_atk > 0:
                    sp_base = self.num('SECRET_SP_BASE_ATK') if mon == self.num('AWAKEN_MON') else base_atk
                    sp_v = self.scale_pct(self.scale_pct(sz['sp_atk'], idiv(fixed[1] * 100, sp_base)), self.num('BOSS_SP_PCT'))
        elif mon == self.mid_def and not self.mid_placed:
            top = self.pool_max()
            hp_v = self.scale_pct(top[0], self.num('MIDBOSS_HP_PCT'))
            atk_v = self.midboss_atk(mon)
            turns = turns - self.num('MIDBOSS_TURNS') if turns > self.num('MIDBOSS_TURNS') + 2 else 2
            e['mid'] = True
        if boss or e['mid']:
            e['ability'] = s.boss_abilities.get(mon, '')
        forms = self.forms_of(mon)
        if forms:
            turns = self.num('MAOU_FORM_TURNS')[forms.index(mon)]
        hp_cap = T['ENEMY_HP_CAP']
        hp_max = max(min(hp_v, self.num('DAMAGE_MAX')), 1)
        if lucky:
            hp_max = max(1, idiv(hp_max * T['LUCKY_HP_PCT'], 100))
        if not lucky:
            hp_max = min(idiv(hp_max * T['ENEMY_HP_PCT'].get(self.area, 100), 100), hp_cap)
            atk_v = idiv(atk_v * T['ENEMY_ATK_PCT'].get(self.area, 100), 100)
            sp_v = idiv(sp_v * T['ENEMY_ATK_PCT'].get(self.area, 100), 100)
        if boss and self.boss_hp_up():
            hp_max = min(idiv(hp_max * T['BOSS_HP_AREA_PCT'].get(self.area, T['BOSS_HP_PCT']), 100), hp_cap)
            fa = self.boss_atk_floor()
            atk_v = max(atk_v, fa)
            sp_v = max(sp_v, idiv(fa * T['BOSS_SP_FLOOR_PCT'], 100))
            bap = T['BOSS_ATK_AREA_PCT'].get(self.area, 100)
            atk_v = idiv(atk_v * bap, 100)
            sp_v = idiv(sp_v * bap, 100)
        home = self.boss_home(mon) if boss and self.boss_hp_up() and self.fixed_boss()[0] == 0 else ''
        if home:
            m = self.mid_ref(home)
            hp_max = min(max(hp_max, idiv(m['hp_max'] * T['BOSS_OVER_MID_HP_PCT'] + 99, 100)), hp_cap)
            want = self.dmg_per_count(m['atk'], m['sp_atk'], m['sp_every'], m['base'], 0, 2) * T['BOSS_OVER_MID_DMG_PCT'] / 100.0
            have = self.dmg_per_count(atk_v, sp_v, sz['sp_every'], turns, 0, 2)
            if 0.0 < have < want:
                atk_v = math.ceil(atk_v * want / have)
                sp_v = math.ceil(sp_v * want / have)
        bal = self.balance_hp(e, self.scale_pct(base_hp, self.hp_pct))
        if bal > 0:
            hp_max = min(bal, hp_cap)
        ba = self.balance_atk(e, self.scale_pct(base_atk, self.atk_pct), self.scale_pct(sz['sp_atk'], self.atk_pct))
        if ba:
            atk_v, sp_v = ba[0], ba[1]
        if self.slow_boss(e, turns):
            slow = turns - T['SLOW_BOSS_TURNS']
            hp_max = min(idiv(hp_max * (100 + T['SLOW_BOSS_HP_PCT'] * slow), 100), hp_cap)
            atk_v = idiv(atk_v * (100 + T['SLOW_BOSS_ATK_PCT'] * slow), 100)
            sp_v = idiv(sp_v * (100 + T['SLOW_BOSS_ATK_PCT'] * slow), 100)
        lock_v = -1
        if mon == self.num('AWAKEN_MON'):
            atk_v = T['SECRET_ATK']
            sp_v = T['SECRET_SP_ATK']
            lock_v = T['SECRET_LOCK_ATK']
        e['hp_max'] = hp_max
        e['atk'] = atk_v
        e['sp_atk'] = sp_v
        e['lock_atk'] = lock_v
        e['sp_every'] = sz['sp_every']
        e['base'] = turns
        st = sz['stun']
        if lucky:
            st = max(1, idiv(st * T['LUCKY_STUN_PCT'], 100))
        resist = self.stun_resist_pct(mon)
        if resist != 100:
            st = self.mul_pct(st, resist)
        e['stun_need'] = st
        e['stun_step'] = self.mul_pct(st, self.num('STUN_GROW_PCT'))
        e['stun_cap'] = self.mul_pct(st, self.num('STUN_GROW_MAX_PCT'))
        e['miracle_next'] = mon == self.num('AWAKEN_MON')
        return e


def mirage_rush(s):
    """The tower's waves: every area boss (areas with a mid-boss), the 魔王 forms, then ペコリータ."""
    c = Core(s)
    out = []
    men_boss = s.num('AREA_MEN')
    for r in s.tables['AREA_TABLE']:
        if r['id'] in (s.num('AREA_CASTLE'), s.num('AREA_SECRET')):
            continue
        b = r.get('boss', 'MON_NONE')
        if b in ('MON_NONE', '') or b not in s.enemies:
            continue
        out.append({'mon': b, 'home': r['id'], 'men': men_boss, 'stage': c.stage_count_for(r['id'], men_boss), 'kind': 'boss'})
    cid = s.num('AREA_CASTLE')
    last_men = s.num('CASTLE_MEN')
    last_wave = c.stage_count_for(cid, last_men)
    for f in s.num('MAOU_FORMS'):
        out.append({'mon': f, 'home': cid, 'men': last_men, 'stage': last_wave, 'kind': 'form'})
    peko = s.num('AWAKEN_MON')
    out.append({'mon': peko, 'home': s.num('AREA_SECRET'), 'men': 1, 'stage': 1, 'kind': 'secret'})
    return out


def mirage_enemies(s):
    T = s.tuning
    rush = mirage_rush(s)
    c = Core(s)

    def entry_enemy(ent):
        c.start_men(ent['home'], ent['men'])
        c.stage = ent['stage']
        if c.stage_boss_kind() == 1:
            c.mid_def = s.areas[ent['home']]['mid']
        return c.make_enemy(ent['mon'])
    last = [r for r in rush if r['kind'] in ('form', 'boss')][-1]
    anc = entry_enemy(last)
    anchor = [max(1, anc['hp_max']), max(1, anc['atk'])]
    cap = T['UNDER_HP_CAP']

    def mp(v, p):
        return min(max(round_half_away(v * p / 100.0), 1), cap)
    out = []
    for i, ent in enumerate(rush):
        e = entry_enemy(ent)
        if ent['kind'] == 'secret':
            hp_p, atk_p = T['MIRAGE_SECRET_HP_PCT']['surface'], T['MIRAGE_SECRET_ATK_PCT']['surface']
            e['hp_max'] = mp(e['hp_max'], hp_p)
            e['atk'] = mp(e['atk'], atk_p)
            e['sp_atk'] = mp(e['sp_atk'], atk_p)
            if e['lock_atk'] > 0:
                e['lock_atk'] = mp(e['lock_atk'], atk_p)
            e['base'] = max(1, e['base'] - T['MIRAGE_SECRET_FASTER'])
        else:
            t = min(max(i / float(max(len(rush) - 2, 1)), 0.0), 1.0)
            hpc = T['MIRAGE_HP_PCT']['surface']
            atkc = T['MIRAGE_ATK_PCT']['surface']
            hp_p = round_half_away(hpc[0] + (hpc[1] - hpc[0]) * t)
            atk_p = round_half_away(atkc[0] + (atkc[1] - atkc[0]) * t)
            ratio = e['sp_atk'] / float(max(e['atk'], 1))
            e['hp_max'] = mp(anchor[0], hp_p)
            if e['atk'] > 0:
                e['atk'] = mp(anchor[1], atk_p)
                e['sp_atk'] = round_half_away(e['atk'] * ratio)
        e['home'] = ent['home']
        e['wave_kind'] = ent['kind']
        out.append(e)
    return out


def round_half_away(x):
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))
