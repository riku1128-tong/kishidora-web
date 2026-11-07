"""Description lines for skills and effects (a port of scripts/desc.gd for the surface effects)."""

ELEMENT = {'EL_NONE': '無', 'EL_FIRE': '炎', 'EL_WATER': '水', 'EL_WOOD': '風', 'EL_LIGHT': '光', 'EL_DARK': '闇'}
WTYPE = {'WT_SWORD': '剣', 'WT_SPEAR': '槍', 'WT_HAMMER': '槌', 'WT_SHIELD': '盾'}
TARGET = {'TGT_ONE': 'ひとりに', 'TGT_TWO': 'ふたりに', 'TGT_ALL': '全員に'}
AIL = {'AIL_POISON': '毒', 'AIL_PARALYZE': 'しびれ', 'AIL_BLIND': '暗闇'}
SIZE = {'SZ_S': 'S', 'SZ_M': 'M', 'SZ_L': 'L'}


def _elem(el):
    return '無属性の' if el == 'EL_NONE' else ELEMENT.get(el, el) + 'の'


def _race(s, rc):
    return s.races.get(rc, {}).get('name', rc)


def skill(s, sk):
    r = s.skills[sk]
    lines = ['%s  ST %d' % (r['name'], r['cost'])]
    if r['hits'] == 0:
        b = r['buff']
        if b == 'BF_FREE':
            lines.append('次のスキルの ST が 0 になる')
        elif b == 'BF_POWER':
            lines.append('次の攻撃の威力 %d%%' % s.buffs['BF_POWER']['value'])
        else:
            lines.append('次の攻撃の会心率 %d%%' % s.buffs.get(b, {}).get('value', 0))
        lines.append('攻撃はしない。必殺技の順番に入らない')
        return lines
    if r['power'] == 100:
        lines.append('%d回 %s' % (r['hits'], TARGET.get(r['target'], '')))
    else:
        lines.append('威力 %d%% ×%d回 %s' % (r['power'], r['hits'], TARGET.get(r['target'], '')))
    l = '' if r['stun_pct'] == 100 else 'スタン 武器の %d%%' % r['stun_pct']
    if r['crit_pct'] > 0:
        l += ('  ' if l else '') + '会心 %d%%' % r['crit_pct']
    if l:
        lines.append(l)
    if r.get('crit_mult', 0) > 0:
        lines.append('会心のとき %d%%' % r['crit_mult'])
    if r.get('race', 'RC_NONE') != 'RC_NONE':
        lines.append('%sに 威力 %d%%' % (_race(s, r['race']), r['race_pct']))
    if r.get('reset_pct', 0) > 0:
        lines.append('%d%% で敵のカウントを戻す' % r['reset_pct'])
    for x in s.tables.get('SKILL_AIL_TABLE', []):
        if x['skill'] == sk:
            for a in s.tables['AIL_TABLE']:
                if a['id'] == x['ail']:
                    lines.append('%d%% で%sにする（%dターン）' % (a['pct'], AIL.get(a['id'], a['id']), a['turns']))
    return lines


def effect_body(s, fx):
    e = s.effects.get(fx, {})
    if not e or e['kind'] == 'FK_NONE':
        return ['特殊効果はない']
    v1, v2, v3 = e['v1'], e['v2'], e['v3']
    k = e['kind']
    if k == 'FK_HEART':
        return ['最大 HP %d%%' % (100 + int(v1))]
    if k == 'FK_RESONANCE':
        return ['%s武器で攻撃すると 威力 %d%%' % (_elem(v1), v2)]
    if k == 'FK_BOOST':
        return ['%s武器で、有利な属性の敵を' % _elem(v1), '攻撃すると さらに %d%%' % v2]
    if k == 'FK_TYPE_UP':
        return ['%sで攻撃すると 威力 %d%%' % (WTYPE.get(v1, v1), v2)]
    if k == 'FK_KILLER':
        t = _race(s, v1)
        if str(v3) != 'RC_NONE':
            t += '・' + _race(s, v3)
        return ['%sに 威力 %d%%' % (t, v2)]
    if k == 'FK_SPECIAL_UP':
        return ['必殺技の威力 %d%%' % v1]
    if k == 'FK_FULLHP_UP':
        return ['HP が満タンのとき 威力 %d%%' % v1]
    if k == 'FK_PINCH_UP':
        return ['HP が最大の %d%% 以下のとき' % v1, '威力 %d%%' % v2]
    if k == 'FK_COMBO_UP':
        return ['コンボが %d 以上のとき 威力 %d%%' % (v1, v2)]
    if k == 'FK_POWER':
        return ['いつも威力 %d%%' % v1, '肉で戻る ST は %d%%' % v2]
    if k == 'FK_ROUND_POWER':
        return ['WAVE が 1 つ進むごとに', '威力 +%d%%（WAVE 2で %d%%）' % (v1, 100 + int(v1))]
    if k == 'FK_PANIC':
        return ['%sサイズの敵へのスタン %d%%' % (SIZE.get(v1, v1), v2)]
    if k == 'FK_CRIT_UP':
        return ['会心率 %d%%' % v1]
    if k == 'FK_BERSERK_UP':
        return ['ベルセルクの会心率をさらに %d%%' % v1]
    if k == 'FK_CRIT_ST':
        return ['会心のたびに ST +%d' % v1]
    if k == 'FK_ROUND_ST':
        return ['WAVE 2 からの始めに ST +%d' % v1]
    if k == 'FK_TURNS':
        t = ''
        if int(v1) > 0:
            t += '+%d' % v1
        if int(v2) > 0:
            t += '-%d' % v2
        return ['敵が攻撃するまでのターンが %s' % t]
    if k == 'FK_GUARD_CUT':
        who = '敵の攻撃を'
        if v2 == 'GC_ELEMENT':
            who = '%s敵の攻撃を' % _elem(v3)
        elif v2 == 'GC_RACE':
            who = '%sの攻撃を' % _race(s, v3)
        elif v2 == 'GC_SCENE':
            who = '%sの戦闘で攻撃を' % ('洞窟' if v3 == 'SCN_CAVE' else '草原')
        return [who, 'ガードすると さらに %d%% カット' % v1]
    if k == 'FK_JUST_WIDE':
        return ['ジャストガードの受付が %d%%' % v1]
    if k == 'FK_REFLECT':
        return ['ジャストガードで敵の ATK の %d%%' % v1, 'ミラクルガードで %d%% を返す' % v2]
    if k == 'FK_FUNBARI':
        return ['倒れるダメージを受けても', '%d%% の確率で HP 1 で耐える' % v1]
    if k == 'FK_GUARD_STUN':
        return ['ジャストガードで %d%%・ミラクルで %d%%' % (v1, v2), 'の確率で、攻撃した敵がスタン']
    if k == 'FK_COMBO_ALL':
        return ['ふつうのスキルにもコンボの', '倍率がかかる' + ((' (%d%%)' % v1) if int(v1) != 100 else '')]
    if k == 'FK_GREED':
        return ['倒したときのコイン %d%%' % v1, 'アイテムのドロップ率 %d%%' % v2]
    if k == 'FK_ALL_SR':
        return ['武器3本と盾が全部 SR・SR+ なら', '威力 %d%%' % v1]
    if k == 'FK_HIT_STUN':
        return ['当てるたびに %d%% の確率で' % v1, '敵がスタン']
    if k == 'FK_ST_POWER':
        return ['スキルで使った ST 1つにつき', '威力 +%d%%' % v1]
    if k == 'FK_HP_POWER':
        return ['威力 %d%%' % v1, '攻撃のたびに最大 HP の %d%% 減る' % v2]
    if k == 'FK_COST_POWER':
        return ['攻撃の消費 ST +%d' % v1, '威力 %d%%' % v2]
    if k == 'FK_HIT_ST':
        return ['当てると %d%% の確率で' % v1, 'ST が %d 回復' % v2]
    if k == 'FK_ST_HEAL_UP':
        return ['肉を食べたときの ST の回復 +%d' % v1]
    if k == 'FK_HIT_SLOW':
        return ['当てるたびに %d%% の確率で' % v1, '敵のカウントが +1']
    if k == 'FK_KILL_POWER':
        return ['このSTAGEで倒した敵 1体につき', '威力 +%d%%（+%d%% まで）' % (v1, v2)]
    if k == 'FK_HAISUI':
        return ['HP が最大の %d%% 以下のとき' % v1, '受けるダメージ -%d%%  威力 %d%%' % (v2, v3)]
    if k == 'FK_REVENGE':
        return ['ガードすると %d%% の確率で' % v1, '敵の ATK の %d%% を返す' % v2]
    if k == 'FK_AUTO_MG':
        return ['ガード・ジャストガードのとき', '%d%% の確率でミラクルガードになる' % v1]
    if k == 'FK_MIRACLE_X':
        return ['ミラクルで回復する HP %d%%' % v1]
    return ['まだ働かない効果']
