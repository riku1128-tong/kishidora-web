#!/usr/bin/env python3
"""Compare the data two ROMs really serve, wherever the placer put it: every asset of gfx_table, every
string of text_table, the fonts, the enemy stat tables, the SPC image and SE tables, and each BGM
track as the stream reader of src/asm/sound.s walks it (start and loop, block by block).

usage: romdata.py [--gen-a DIR] [--gen-b DIR] [--tracks N] [-v] ROM_A [ROM_B]

  Symbols come from each ROM's .lbl (build.py writes it), placed labels from its .place.txt when
  there is one (build/kishidra.place.txt: their real bank; a label in a code bank $81-$9F has a bank 0
  address in the .lbl).  Both stream formats are read: bgm_seg_bank / _ptr / _len (tools/romplace.py)
  and the older bgm_banks with 3640 blocks per bank.  The counts (assets, strings, glyphs, tracks,
  enemy-stat rows) come from data/gen (or --gen-a / --gen-b for a ROM built from other data).
  With one ROM: the BGM walk is checked against data/gen/audio/bgm.brr and data/gen/place/bgm.json.
  --tracks N: compare only the first N BGM tracks (and not the track counts), e.g. a ROM built from a
  copy whose stream was cut to the first tracks (tools/harness/test_romplace.py).
Exit status 0 when everything compared is identical."""
import argparse
import hashlib
import json
import os
import re
import struct
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
OLD_BLOCKS_PER_BANK = 3640


def foff(bank, addr):
    """file offset of a ROM address (LoROM; ExLoROM banks $00-$7D are the file above 4 MB)"""
    return (bank & 0x7f) * 0x8000 + (addr & 0x7fff) + (0x400000 if bank < 0x80 else 0)


class Rom:
    def __init__(self, path, gen):
        self.path = path
        self.d = open(path, 'rb').read()
        stem = os.path.splitext(path)[0]
        self.sym = {}
        for line in open(stem + '.lbl'):
            p = line.split()
            if len(p) == 3 and p[0] == 'al':
                self.sym[p[2].lstrip('.')] = int(p[1], 16)
        self.placed = {}
        if os.path.exists(stem + '.place.txt'):
            for line in open(stem + '.place.txt'):
                m = re.match(r'^([0-9A-F]{2}):([0-9A-F]{4})\s+\d+\s+(\w+)', line)
                if m:
                    self.placed[m.group(3)] = (int(m.group(1), 16), int(m.group(2), 16))
        self.gen = gen

    def where(self, label):
        if label in self.placed:
            return self.placed[label]
        v = self.sym.get(label)
        if v is None:
            raise KeyError(label)
        if v < 0x10000:
            raise SystemExit('%s: %s has a bank 0 address in the .lbl and is not in the .place.txt' % (self.path, label))
        return v >> 16, v & 0xffff

    def at(self, bank, addr, n):
        o = foff(bank, addr)
        return self.d[o:o + n]

    def label(self, label, n, off=0):
        b, a = self.where(label)
        return self.at(b, a + off, n)


def define(gen, header, name):
    m = re.search(r'#define\s+%s\s+(\d+)' % name, open(os.path.join(gen, header), encoding='utf-8').read())
    return int(m.group(1))


def gfx_assets(r):
    n = define(r.gen, 'gfx.h', 'N_GFX_ASSETS')
    out = []
    for i in range(n):
        lo, bank, flags, size = struct.unpack('<HBBH', r.label('gfx_table', 6, 6 * i))
        out.append(bytes([flags]) + r.at(bank, lo, size))
    return out


def strings(r):
    n = define(r.gen, 'text.h', 'N_TEXT')
    out = []
    for i in range(n):
        lo, bank = struct.unpack('<HB', r.label('text_table', 3, 3 * i))
        o, s = foff(bank, lo), bytearray()
        while True:
            c = r.d[o:o + 2]
            s += c
            o += 2
            if c == b'\0\0':
                break
        out.append(bytes(s))
    return out


def fonts(r):
    g = define(r.gen, 'text.h', 'N_GLYPH')
    return [r.label('font_glyphs', 24 * g), r.label('font_width', g), r.label('font_glyphs_s', 16 * g),
            r.label('font_width_s', g)]


def estat(r):
    ni = os.path.getsize(os.path.join(r.gen, 'estat_index.bin'))
    nd = os.path.getsize(os.path.join(r.gen, 'estat_data.bin'))
    return [r.label('estat_index', ni), r.label('estat_data', nd)]


def spc(r):
    ln = r.sym['SPC_IMAGE_LEN_LO'] | r.sym['SPC_IMAGE_LEN_HI'] << 16
    b, a = r.where('spc_image')
    first = min(ln, r.sym['SPC_SPLIT_END'] - a)
    img = r.at(b, a, first) + r.at(b + 1, 0x8000, ln - first)
    n = r.sym['se_vol'] - r.sym['se_dir']          # se_dir, se_vol: 1 + SEs bytes, se_thr: words
    return [img, r.label('se_dir', n), r.label('se_vol', n), r.label('se_thr', 2 * n)]


def bgm_tracks(r, limit=None):
    """[(name, start blocks, loop blocks)] as sound.s streams them (the first limit tracks)"""
    gen_audio = open(os.path.join(r.gen, 'audio.h')).read()
    names = re.findall(r'#define TRACK_(\w+) \d+', gen_audio)[:limit]
    new = 'bgm_seg_bank' in r.sym

    def seg(k):
        if new:
            bank = r.label('bgm_seg_bank', 1, k)[0]
            ptr, = struct.unpack('<H', r.label('bgm_seg_ptr', 2, 2 * k))
            ln, = struct.unpack('<H', r.label('bgm_seg_len', 2, 2 * k))
            return bank, ptr, ln
        return r.label('bgm_banks', 1, k)[0], 0x8000, OLD_BLOCKS_PER_BANK

    def walk(row):
        k, ptr, left, n0, n1, n2 = struct.unpack('<BHHBBB', row)
        n = n0 | n1 << 8 | n2 << 16
        bank = seg(k)[0]
        out = bytearray()
        for _ in range(n):
            out += r.at(bank, ptr, 9)
            ptr += 9
            left -= 1
            if left == 0:
                k += 1
                bank, ptr, left = seg(k)
        return bytes(out)
    out = []
    for i, name in enumerate(names):
        row = r.label('bgm_table', 16, 16 * i)
        out.append((name, walk(row[:8]), walk(row[8:]) if any(row[13:16]) else None))
    return out


def h(b):
    return hashlib.sha1(b).hexdigest()[:12] if b is not None else '-'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom_a')
    ap.add_argument('rom_b', nargs='?')
    ap.add_argument('--gen-a', default=os.path.join(ROOT, 'data', 'gen'))
    ap.add_argument('--gen-b', default=os.path.join(ROOT, 'data', 'gen'))
    ap.add_argument('--tracks', type=int)
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    ra = Rom(a.rom_a, a.gen_a)
    bad = 0
    if a.rom_b:
        rb = Rom(a.rom_b, a.gen_b)
        for what, fn in (('gfx assets', gfx_assets), ('strings', strings), ('fonts', fonts),
                         ('enemy stats', estat), ('SPC image + SE tables', spc)):
            xa, xb = fn(ra), fn(rb)
            diff = [i for i in range(max(len(xa), len(xb))) if i >= len(xa) or i >= len(xb) or xa[i] != xb[i]]
            print('%-22s %5d / %5d  %s' % (what, len(xa), len(xb), 'same' if not diff else
                                            'DIFF %d (first %s)' % (len(diff), diff[:8])))
            bad += bool(diff)
        ta, tb = bgm_tracks(ra, a.tracks), bgm_tracks(rb, a.tracks)
        for (na, sa, la), (nb, sb, lb) in zip(ta, tb):
            same = na == nb and sa == sb and la == lb
            bad += not same
            print('BGM %-18s start %7d blocks %s  loop %s  %s' % (
                na, len(sa) // 9, h(sa), h(la), 'same' if same else 'DIFF (%s %s / %s %s)' % (h(sa), h(la), h(sb), h(lb))))
        if len(ta) != len(tb) and a.tracks is None:
            print('BGM tracks %d / %d DIFF' % (len(ta), len(tb)))
            bad += 1
    else:
        man = json.load(open(os.path.join(a.gen_a, 'place', 'bgm.json')))['entries'][0]
        brr = open(os.path.join(a.gen_a, man['file']), 'rb').read()
        for (name, s, l), t in zip(bgm_tracks(ra), man['tracks']):
            es = brr[t['first'] * 9:(t['first'] + t['blocks']) * 9]
            el = brr[(t['first'] + t['loop']) * 9:(t['first'] + t['blocks']) * 9] if t['loop'] is not None else None
            same = s == es and l == el
            bad += not same
            print('BGM %-18s %s' % (name, 'same as bgm.brr' if same else 'DIFF from bgm.brr'))
    print('OK: identical' if not bad else '%d difference(s)' % bad)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
