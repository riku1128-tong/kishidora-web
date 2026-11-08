#!/usr/bin/env python3
"""Write a 32KB SRAM image with a chosen progress, for emulator tests.

usage: mksave.py out.srm [--open AREA] [--clear-all] [--coins N] [--items ID:SK,SK ...] [--mats N] ...
  --open N      areas 0..N open (default 0)
  --clear-all   every stage of the open areas cleared (castle cleared -> mirage tower)
  --v1          write the old SAVE_VER 1 layout (8702 B, byte-identical to the mksave of before v2),
                to test the migration; the v2 options below are then refused
Layout = struct Save in src/c/save.h (cc65 packs structs, little endian): SAVE_VER 2 (9300 B), slot A
($0000), seq 1.  docs/plan/PLAN.md §2.

v1 fields:
  --equip M,S,T,SH         sv.equip: bag indexes of MAIN, SUB, THIRD, 盾 ('-' = empty; default --main, -, -, 1)
  --story N                sv.story (default 1 = ST_OPENING).  Without ST_SOLO (8) the game computes the
                           INV_SOLO bits of the bag on load and writes the save back (save.c save_load);
                           with it (e.g. 9) the bits are taken as written (mksave writes them 0)
  --mat IDX:N ...          one material (after --mats N, which sets all of them)
  --kills MON:N ...        sv.kills[MON] = N (N > 0 also marks the monster found, as a kill does)
  --got ID:N ...           sv.got[ID] = N (N > 0 also marks the item found, as add_got does)
v2 block (offsets 628..1225, length asserted):
  --guides N               GDB_* bits (default 0x1f: every one-time tip / guide already seen;
                           0 = all of them still to come)
  --solo N                 必殺10連武器チケット
  --mirage-cards MASK      bit w = the mirage card of WAVE w+1 (18 bits)
  --shops-seen MASK        bit = area index of a shop town (default 0xffff: no NEW marks)
  --boss-wins A,B          魔王 / 裏ボス wins counted since v2
  --boss-rec K:DIFF:TURNS:BOSS_TURNS:NTH:MAIN,SUB,THIRD,SHIELD ...
                           討伐記録 K (0 魔王 first, 1 魔王 latest, 2 裏ボス first, 3 裏ボス latest);
                           the four entries are bag indexes (0 = the start weapon, 1 = the start
                           shield, 2.. = --items in order) whose Inv is copied, '-' = empty (id 255)
  --set N:NAME:MAIN,SUB,THIRD,SHIELD ...
                           セット装備 N (1..15, sets[N-1]); NAME in kana (glyph codes of
                           data/gen/font.bin.chars, at most 8; empty = 「セットN」); entries are bag
                           indexes, '-' = empty (NO_ITEM), 'gone' = SET_GONE"""
import argparse
import os
import re
import struct
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))

V1_SIZE, V1_NINV = 8702, 628            # SAVE_V1_SIZE, SAVE_V1_NINV (save.h)
V2_NINV, V2_SIZE = 1226, 9300           # offsetof(Save, ninv), sizeof(Save) at SAVE_VER 2
# the v2 block: name, offset, struct format (save.h; checked against V2_NINV below)
V2_FIELDS = [('guides', 628, '<B'), ('solo_tickets', 629, '<B'), ('mirage_run', 630, '<B'),
             ('mirage_cards', 631, '<3s'), ('mirage_run_id', 634, '<H'), ('shops_seen', 636, '<H'),
             ('boss_wins', 638, '<2H'), ('rec', 642, '<160s'), ('sets', 802, '<360s'),
             ('spare', 1162, '<64s')]
BOSSREC, EQUIPSET, EQUIP_SETS, SET_NAME_MAX = 40, 24, 15, 8
NO_ITEM, SET_GONE, GLYPH0 = 0xffff, 0xfffe, 16


def consts():
    out = {}
    for f in ('gdata.h', 'text.h'):
        for m in re.finditer(r'#define\s+(\w+)\s+(-?\d+)L?\b', open(os.path.join(ROOT, 'data', 'gen', f)).read()):
            out[m.group(1)] = int(m.group(2))
    return out


def glyph_codes(text):
    """kana -> glyph codes of the committed font (tools/data/text.py: code = GLYPH0 + line index)"""
    chars = []
    for ch in open(os.path.join(ROOT, 'data', 'gen', 'font.bin.chars'), encoding='utf-8').read().split('\n'):
        if ch:
            chars.append(ch.encode('utf-8').decode('unicode_escape') if ch.startswith('\\') else ch)
    index = {c: i for i, c in enumerate(chars)}
    out = []
    for ch in text:
        if ch not in index:
            sys.exit('mksave: %r is not in the font (data/gen/font.bin.chars)' % ch)
        out.append(GLYPH0 + index[ch])
    return out


def num(s):
    """decimal (leading zeros allowed) or 0x / 0b / 0o"""
    s = s.strip()
    return int(s, 0) if s[:2].lower() in ('0x', '0b', '0o') else int(s, 10)


def pairs(specs, what):
    for spec in specs:
        k, sep, v = spec.partition(':')
        if not sep:
            sys.exit('mksave: %s wants K:N, got %r' % (what, spec))
        yield num(k), num(v)


def bag_ref(s, inv, empty, gone=None):
    """one entry of a --set / --boss-rec list: a bag index, '-' = EMPTY, 'gone' = GONE"""
    s = s.strip()
    if s in ('', '-'):
        return empty
    if s == 'gone' and gone is not None:
        return gone
    i = num(s)
    if not 0 <= i < len(inv):
        sys.exit('mksave: bag index %d out of range (0..%d)' % (i, len(inv) - 1))
    return i


def inv_bytes(item):
    iid, sk, slots = item
    return struct.pack('<B3BB3B', iid, *sk, slots, 0, 0, 0)


def v2_block(a, inv):
    rec = bytearray(4 * BOSSREC)
    for spec in a.boss_rec:
        p = spec.split(':')
        if len(p) != 6:
            sys.exit('mksave: --boss-rec K:DIFF:TURNS:BOSS_TURNS:NTH:MAIN,SUB,THIRD,SHIELD, got %r' % spec)
        k, diff, turns, bturns, nth = (num(x) for x in p[:5])
        ws = p[5].split(',')
        if not 0 <= k < 4 or len(ws) != 4:
            sys.exit('mksave: --boss-rec %r: K is 0..3 and 4 entries are needed' % spec)
        r = struct.pack('<BBHHH', 1, diff, turns, bturns, nth)
        for w in ws:
            i = bag_ref(w, inv, None)
            r += inv_bytes(inv[i]) if i is not None else inv_bytes((255, [255, 255, 255], 0))
        assert len(r) == BOSSREC
        rec[k * BOSSREC:(k + 1) * BOSSREC] = r
    sets = bytearray(EQUIP_SETS * EQUIPSET)
    for spec in a.set:
        p = spec.split(':')
        if len(p) != 3:
            sys.exit('mksave: --set N:NAME:MAIN,SUB,THIRD,SHIELD, got %r' % spec)
        n = num(p[0])
        ws = p[2].split(',')
        if not 1 <= n <= EQUIP_SETS or len(ws) != 4:
            sys.exit('mksave: --set %r: N is 1..%d and 4 entries are needed' % (spec, EQUIP_SETS))
        name = glyph_codes(p[1] or 'セット%d' % n)
        if len(name) > SET_NAME_MAX:
            sys.exit('mksave: --set %r: the name is longer than %d' % (spec, SET_NAME_MAX))
        s = struct.pack('<4H', *(bag_ref(w, inv, NO_ITEM, SET_GONE) for w in ws))
        s += struct.pack('<8H', *(name + [0] * (SET_NAME_MAX - len(name))))
        assert len(s) == EQUIPSET
        sets[(n - 1) * EQUIPSET:n * EQUIPSET] = s
    wins = [num(x) for x in a.boss_wins.split(',')]
    if len(wins) != 2:
        sys.exit('mksave: --boss-wins A,B')
    val = {'guides': (a.guides,), 'solo_tickets': (a.solo,), 'mirage_run': (0,),
           'mirage_cards': (struct.pack('<I', a.mirage_cards)[:3],), 'mirage_run_id': (0,),
           'shops_seen': (a.shops_seen,), 'boss_wins': wins, 'rec': (bytes(rec),), 'sets': (bytes(sets),),
           'spare': (bytes(64),)}
    v2 = bytearray()
    for name, off, fmt in V2_FIELDS:
        assert V1_NINV + len(v2) == off, (name, off, V1_NINV + len(v2))
        v2 += struct.pack(fmt, *val[name])
    assert len(v2) == V2_NINV - V1_NINV == 598, len(v2)
    return bytes(v2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out')
    ap.add_argument('--open', type=int, default=0)
    ap.add_argument('--clear-all', action='store_true')
    ap.add_argument('--coins', type=int, default=1000)
    ap.add_argument('--tickets', type=int, default=0)
    ap.add_argument('--ending', action='store_true', help='the ending has been seen')
    ap.add_argument('--main', type=int, default=0, help='inventory index equipped as MAIN')
    ap.add_argument('--equip', default=None, help='MAIN,SUB,THIRD,SHIELD bag indexes (- = empty); overrides --main')
    ap.add_argument('--story', type=lambda x: int(x, 0), default=1,
                    help='sv.story (default 1 = ST_OPENING; 9 adds ST_SOLO: the INV_SOLO bits are taken as they are, all 0)')
    ap.add_argument('--dex', type=int, default=0, help='dex: every Nth monster / item / special unknown (0 = all unknown)')
    ap.add_argument('--mats', type=int, default=0)
    ap.add_argument('--items', nargs='*', default=[])
    ap.add_argument('--clear', nargs='*', default=[], help='AREA:MASK (stage bits)')
    ap.add_argument('--mat', nargs='*', default=[], help='IDX:N (one material)')
    ap.add_argument('--kills', nargs='*', default=[], help='MON:N')
    ap.add_argument('--got', nargs='*', default=[], help='ID:N')
    ap.add_argument('--v1', action='store_true', help='the old SAVE_VER 1 layout (migration tests)')
    ap.add_argument('--guides', type=lambda x: int(x, 0), default=None,
                    help='GDB_* bits (default 0x1f: every one-time tip already seen)')
    ap.add_argument('--solo', type=int, default=None, help='必殺10連武器チケット')
    ap.add_argument('--mirage-cards', type=lambda x: int(x, 0), default=None, help='bit w = card of WAVE w+1')
    ap.add_argument('--shops-seen', type=lambda x: int(x, 0), default=None, help='bit = area (default: all seen)')
    ap.add_argument('--boss-wins', default=None, help='A,B')
    ap.add_argument('--boss-rec', nargs='*', default=[], help='K:DIFF:TURNS:BOSS_TURNS:NTH:MAIN,SUB,THIRD,SHIELD')
    ap.add_argument('--set', nargs='*', default=[], help='N:NAME:MAIN,SUB,THIRD,SHIELD')
    a = ap.parse_args()
    v2opts = [o for o, v in (('--guides', a.guides), ('--solo', a.solo), ('--mirage-cards', a.mirage_cards),
                             ('--shops-seen', a.shops_seen), ('--boss-wins', a.boss_wins),
                             ('--boss-rec', a.boss_rec or None), ('--set', a.set or None)) if v is not None]
    if a.v1 and v2opts:
        sys.exit('mksave: %s are v2 fields (not with --v1)' % ', '.join(v2opts))
    a.guides = 0x1f if a.guides is None else a.guides
    a.solo = 0 if a.solo is None else a.solo
    a.mirage_cards = 0 if a.mirage_cards is None else a.mirage_cards
    a.shops_seen = 0xffff if a.shops_seen is None else a.shops_seen
    a.boss_wins = '0,0' if a.boss_wins is None else a.boss_wins
    C = consts()
    n_area, n_mat, n_sp, n_mon, n_item = C['N_AREA'], C['N_MAT'], C['N_SPECIAL'], C['N_MON'], C['N_ITEM']
    inv_slots = C['D_INV_MAX'] + C['D_ITEM_MAX']
    men = [C['D_CASTLE_MEN'] if i == C['AR_CASTLE'] else (C['D_SECRET_MEN'] if i == C['AR_SECRET'] else C['D_AREA_MEN'])
           for i in range(n_area)]
    cleared = [0] * n_area
    if a.clear_all:
        for i in range(min(a.open + 1, C['AR_MIRAGE'])):
            cleared[i] = (1 << men[i]) - 1
    else:
        for i in range(a.open):
            cleared[i] = (1 << men[i]) - 1
    for spec in a.clear:
        ar, _, mask = spec.partition(':')
        cleared[int(ar)] = int(mask, 0)
    inv = [(C['ITEM_START_WEAPON'], [C['START_SKILL_0'], C['START_SKILL_1'], 255], 0),
           (C['ITEM_EQUIP_SHIELD'], [255, 255, 255], 2)]
    for spec in a.items:
        iid, _, sk = spec.partition(':')
        sks = [int(x) for x in sk.split(',') if x] if sk else []
        inv.append((int(iid), (sks + [255, 255, 255])[:3], 3))
    if len(inv) > inv_slots:
        sys.exit('mksave: %d items, the bag holds %d' % (len(inv), inv_slots))
    mats = [a.mats] * n_mat
    for k, v in pairs(a.mat, '--mat'):
        mats[k] = v
    def bits(n):
        b = bytearray((n + 7) // 8)
        for i in range(n):
            if a.dex and i % a.dex:
                b[i >> 3] |= 1 << (i & 7)
        return b
    kills = [(i * 7) % 130 if a.dex and i % a.dex else 0 for i in range(n_mon)]
    got = [(i % 5) if a.dex and i % a.dex else 0 for i in range(n_item)]
    found_items, found_mons = bits(n_item), bits(n_mon)
    for k, v in pairs(a.kills, '--kills'):
        kills[k] = v
        if v:
            found_mons[k >> 3] |= 1 << (k & 7)
    for k, v in pairs(a.got, '--got'):
        got[k] = v
        if v:
            found_items[k >> 3] |= 1 << (k & 7)
    body = bytearray()
    body += struct.pack('<BBBB', a.open, C['DIFF_NORMAL'], a.open, a.tickets)
    body += struct.pack('<I', a.coins)
    body += b''.join(struct.pack('<I', c) for c in cleared)
    body += b'\0' * 14                                # name (default name is used)
    body += struct.pack('<8B', 1 if a.ending else 0, 1, 1, 0, 6, a.story, 8, 8)  # ending, tut, tut_clears, sort, guard_late, story, vols
    eq = [a.main, 0xffff, 0xffff, 1]
    if a.equip is not None:
        eq = [bag_ref(x, inv, 0xffff) for x in a.equip.split(',')]
        if len(eq) != 4:
            sys.exit('mksave: --equip MAIN,SUB,THIRD,SHIELD')
    body += struct.pack('<4H', *eq)
    body += b''.join(struct.pack('<H', v) for v in mats)
    body += bytes(found_items) + bytes(32 - (n_item + 7) // 8) + bits(n_sp) + bytes(found_mons)
    body += b''.join(struct.pack('<H', v) for v in kills)
    body += bytes(got)
    body += struct.pack('<BBH', 0, 0, 0)
    assert 12 + len(body) == V1_NINV, 12 + len(body)
    if not a.v1:
        body += v2_block(a, inv)
    body += struct.pack('<H', len(inv))
    for item in inv:
        body += inv_bytes(item)
    body += b'\0' * (8 * (inv_slots - len(inv)))
    size = 12 + len(body)
    assert size == (V1_SIZE if a.v1 else V2_SIZE), size
    used = len(body) - 8 * (inv_slots - len(inv))
    s = sum(body[:used]) & 0xffff
    hdr = b'KSFC' + struct.pack('<HHHH', 1, s, size, 1 if a.v1 else 2)
    img = bytearray(32768)
    img[0:12] = hdr
    img[12:12 + len(body)] = body
    open(a.out, 'wb').write(img)
    print('save: %d bytes, %d items, areas open 0..%d' % (size, len(inv), a.open))


if __name__ == '__main__':
    main()
