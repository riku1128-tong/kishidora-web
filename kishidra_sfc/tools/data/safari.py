"""The Safari version's game data for the surface world, as GameData builds it at run time.

GameData.load_all() starts from data/params.json (generated from the original SFC
params.h) and then applies the phone/web rules in scripts/tuning.gd (names with kanji,
stat fixes, extra effects ...).  This module repeats those steps for the surface world.
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'extract'))
import webbuild  # noqa: E402

RARITY_IDS = ['RARE_N', 'RARE_NP', 'RARE_R', 'RARE_RP', 'RARE_SR', 'RARE_SRP']


class Safari:
    def __init__(self):
        wb = self.wb = webbuild.get()
        p = json.loads(wb.read('data/params.json'))
        self.tables = p['tables']
        self.defines = p['defines']
        self.list_names = p.get('list_names', {})
        self.tuning = T = webbuild.script_constants(wb, 'scripts/tuning.gdc')
        self.hp_balance = json.loads(wb.read('data/hp_balance.json'))
        self.atk_balance = json.loads(wb.read('data/atk_balance.json'))
        t = self.tables
        idx = lambda name: {r['id']: r for r in t[name]}
        self.skills = idx('SKILL_TABLE')
        self.weapons = idx('WEAPON_TABLE')
        self.shields = idx('SHIELD_TABLE')
        self.enemies = idx('ENEMY_TABLE')
        self.races = idx('RACE_TABLE')
        self.sizes = idx('SIZE_TABLE')
        self.kinds = idx('KIND_TABLE')
        self.elements = idx('ELEMENT_TABLE')
        self.buffs = idx('BUFF_TABLE')
        self.effects = idx('EFFECT_TABLE')
        self.areas = idx('AREA_TABLE')
        self.specials = t['SPECIAL_TABLE']
        self.pools = {r['area']: r['pool'] for r in t['POOL_TABLE']}
        d = self.defines
        # ---- GameData._phone_rules (surface part)
        mt = t['MAT_TABLE']
        for r in mt:
            if r['id'] == 'MAT_SKYOUKA':
                r['up'] = 'MAT_HKYOUKA'
        for i, r in enumerate(mt):
            if r['id'] == 'MAT_SKYOUKA':
                mt.insert(i + 1, {'id': 'MAT_HKYOUKA', 'item': '', 'element': 'EL_NONE', 'up': 'MAT_NONE'})
                break
        d['MAT_BELL_BOSS_PCT'] = T['MAT_BELL_BOSS_PCT']
        d['INV_MAX'] = T['INV_MAX']
        d['MAT_MAX'] = T['MAT_MAX']
        d['TUTORIAL_STAGES'] = T['TUTORIAL_STAGES']
        d['MAT_TSUME_ZAKO_PCT'] = T['TSUME_DRAGON_ZAKO_PCT']
        d['MAT_BELL_LUCKY_PCT'] = T['MAT_BELL_LUCKY_PCT']
        d['KICK_ONLY_FIX_PCT'] = T['KICK_ONLY_FIX_PCT']
        d['COMBO_TIER_PCT'] = T['COMBO_TIER_PCT']
        d['COMBO_TIER_TEXT'] = T['COMBO_TIER_TEXT']
        for k, v in T['MON_NAMES'].items():
            if k in self.enemies:
                self.enemies[k]['name'] = v
        for k, v in T['RACE_NAMES'].items():
            if k in self.races:
                self.races[k]['name'] = v
        self.enemies[T['TRAINING_MON']] = {'id': T['TRAINING_MON'], 'name': T['TRAINING_NAME'], 'kind': 'EK_ZAKO', 'race': 'RC_HUMAN',
                                           'element': 'EL_NONE', 'weak': 'WEAK_NONE', 'size': 'SZ_S'}
        for k, (df, hp) in T['SHIELD_STATS'].items():
            if k in self.shields:
                self.shields[k]['def'] = df
                self.shields[k]['hp'] = hp
        for k, v in T['RARITY_FIX'].items():
            row = self.weapons.get(k) or self.shields.get(k)
            if row:
                row['rarity'] = v
        for k, v in T['ATK_FIX'].items():
            if k in self.weapons:
                self.weapons[k]['atk'] = v
        for k, (a, c, s) in T['WEAPON_STATS'].items():
            if k in self.weapons:
                self.weapons[k].update(atk=a, crit=c, stun=s)
        for e in T['EXTRA_EFFECTS']:
            self.effects[e['id']] = dict(e)
        for k, v in T['EFFECT_FIX'].items():
            if k in self.weapons:
                self.weapons[k]['effect'] = v
        for k, v in T['EFFECT_ROW_FIX'].items():
            if k in self.effects:
                self.effects[k].update(v)
        for k, v in T['FX_NAMES'].items():
            if k in self.effects:
                self.effects[k]['name'] = v
        for k, v in T['SKILL_NAMES'].items():
            if k in self.skills:
                self.skills[k]['sfc_name'] = self.skills[k]['name']
                self.skills[k]['name'] = v
                self.skills[k]['short'] = v
        for k, v in self.list_names.items():
            row = self.weapons.get(k) or self.shields.get(k)
            if row:
                row['sfc_name'] = row['name']
                row['name'] = v
        for k, v in T['AREA_NAMES'].items():
            if k in self.areas:
                self.areas[k]['name'] = v
        self.boss_abilities = {r['mon']: r['ability'] for r in t['BOSS_ABILITY_TABLE']}
        self.boss_sp = {r['mon']: r for r in t['BOSS_SP_TABLE']}

    def num(self, k, default=0):
        return self.defines.get(k, default)

    def area_list(self):
        return t_ids(self.tables['AREA_TABLE'])

    def zako_pool(self, area):
        pool = self.pools.get(area, [])
        mid = self.areas[area]['mid']
        return [m for m in pool if m != mid]


def t_ids(rows):
    return [r['id'] for r in rows]


_s = None


def get():
    global _s
    if _s is None:
        _s = Safari()
    return _s
