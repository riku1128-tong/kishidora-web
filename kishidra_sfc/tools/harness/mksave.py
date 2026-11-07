#!/usr/bin/env python3
"""Write a 32KB SRAM image with a chosen progress, for emulator tests.

usage: mksave.py out.srm [--open AREA] [--clear-all] [--coins N] [--items ID:SK,SK ...] [--mats N]
  --open N      areas 0..N open (default 0)
  --clear-all   every stage of the open areas cleared (castle cleared -> mirage tower)
Layout = struct Save in src/c/save.h (cc65 packs structs, little endian)."""
import argparse
import os
import re
import struct

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))


def consts():
    out = {}
    for f in ('gdata.h', 'text.h'):
        for m in re.finditer(r'#define\s+(\w+)\s+(-?\d+)L?\b', open(os.path.join(ROOT, 'data', 'gen', f)).read()):
            out[m.group(1)] = int(m.group(2))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--open', type=int, default=0)
    ap.add_argument('--clear-all', action='store_true')
    ap.add_argument('--coins', type=int, default=1000)
    ap.add_argument('--tickets', type=int, default=0)
    ap.add_argument('--ending', action='store_true', help='the ending has been seen')
    ap.add_argument('--main', type=int, default=0, help='inventory index equipped as MAIN')
    ap.add_argument('--dex', type=int, default=0, help='dex: every Nth monster / item / special unknown (0 = all unknown)')
    ap.add_argument('--mats', type=int, default=0)
    ap.add_argument('--items', nargs='*', default=[])
    ap.add_argument('--clear', nargs='*', default=[], help='AREA:MASK (stage bits)')
    a = ap.parse_args()
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
    name = [C.get('T_NAME_TITLE', 0)] * 0
    body = bytearray()
    body += struct.pack('<BBBB', a.open, C['DIFF_NORMAL'], a.open, a.tickets)
    body += struct.pack('<I', a.coins)
    body += b''.join(struct.pack('<I', c) for c in cleared)
    body += b'\0' * 14                                # name (default name is used)
    body += struct.pack('<8B', 1 if a.ending else 0, 1, 1, 0, 6, 1, 8, 8)  # ending, tut, tut_clears, sort, guard_late, story, vols
    body += struct.pack('<4H', a.main, 0xffff, 0xffff, 1)
    body += b''.join(struct.pack('<H', a.mats) for _ in range(n_mat))
    def bits(n):
        b = bytearray((n + 7) // 8)
        for i in range(n):
            if a.dex and i % a.dex:
                b[i >> 3] |= 1 << (i & 7)
        return bytes(b)
    body += bits(n_item) + bytes(32 - (n_item + 7) // 8) + bits(n_sp) + bits(n_mon)
    body += b''.join(struct.pack('<H', (i * 7) % 130 if a.dex and i % a.dex else 0) for i in range(n_mon))
    body += bytes((i % 5) if a.dex and i % a.dex else 0 for i in range(n_item))
    body += struct.pack('<BBH', 0, 0, 0)
    body += struct.pack('<H', len(inv))
    for iid, sk, slots in inv:
        body += struct.pack('<B3BB3B', iid, *sk, slots, 0, 0, 0)
    body += b'\0' * (8 * (inv_slots - len(inv)))
    size = 12 + len(body)
    used = len(body) - 8 * (inv_slots - len(inv))
    s = sum(body[:used]) & 0xffff
    hdr = b'KSFC' + struct.pack('<HHHH', 1, s, size, 1)
    img = bytearray(32768)
    img[0:12] = hdr
    img[12:12 + len(body)] = body
    open(a.out, 'wb').write(img)
    print('save: %d bytes, %d items, areas open 0..%d' % (size, len(inv), a.open))


main()
