#!/usr/bin/env python3
"""Tests of the shared ROM placer (tools/romplace.py).

usage: test_romplace.py            unit tests of romplace.place() on made-up banks (no build)
       test_romplace.py --build    + builds of a copy of the port (in $TMPDIR) with fake blobs added
                                   through an extra manifest: a 30 KB 'any' blob must land in a FastROM
                                   bank, a 'long' blob in $A0-$FF, a 'fast' one in FastROM; the data the
                                   ROM serves (romdata.py) must equal the normal build's.  Then the same
                                   at ROM_MB=8 (ExLoROM): the fakes where they were, the BGM stream in
                                   the slow banks $01-$7D, romdata equal to the 4 MB build; and once more
                                   with 32 KB 'any' fillers for every empty FastROM bank + 2: some 'any'
                                   blobs must go to slow banks, only when no FastROM bank has room for
                                   them, never a 'fast' or 'long' one, and the ROM still serves the same
                                   data (romdata) with every fake blob's bytes at its ExLoROM file offset
       test_romplace.py --build --keep DIR   keep the three ROMs of the builds (fake4 / fake8 / fill8 with
                                   their .lbl, .map and .place.txt) in DIR, e.g. to run harness scripts on
                                   fill8, whose art partly comes from the slow banks
Exit status 0 when all pass."""
import json
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import romplace  # noqa: E402


def banks(used, slow=(), longok=lambda b: b > 0x9f):
    out = [{'bank': b, 'base': 0x8000, 'used': u, 'speed': 'fast', 'long': longok(b)} for b, u in used.items()]
    out += [{'bank': b, 'base': 0x8000, 'used': u, 'speed': 'slow'} for b, u in slow]
    return out


def whole(label, size, speed='fast', long=False):
    return {'label': label, 'file': 'x.bin', 'off': 0, 'size': size, 'kind': 'whole', 'speed': speed,
            'long': long, '_from': 't.json'}


def stream(size, unit=9, mn=64, tracks=None):
    return {'label': 'bgm', 'file': 'bgm.brr', 'off': 0, 'size': size, 'kind': 'stream', 'unit': unit, 'min': mn,
            'speed': 'slow', 'long': False, 'hook': 'bgm', 'tracks': tracks or [], '_from': 'bgm.json'}


class Place(unittest.TestCase):
    def test_best_fit(self):
        _, _, w = romplace.place([whole('a', 1000)], banks({0x81: 0x8000 - 1500, 0x82: 0x8000 - 1200, 0xa0: 0}))
        self.assertEqual(w['a'][0], 0x82)                    # the fullest bank that still holds it
        self.assertEqual(w['a'][1], 0x8000 + 0x8000 - 1200)  # right after the fixed segments

    def test_long_not_in_code_banks(self):
        _, _, w = romplace.place([whole('l', 100, long=True), whole('s', 100)],
                                 banks({0x81: 0x7f00, 0x9f: 0x7f00, 0xa0: 0, 0xff: 0x4000}))
        self.assertGreaterEqual(w['l'][0], 0xa0)
        self.assertIn(w['s'][0], (0x81, 0x9f))

    def test_any_falls_back_to_slow(self):
        bk = banks({0x81: 0x7000, 0xa0: 0x7800}, slow=[(0x01, 0), (0x02, 0x100)])
        _, _, w = romplace.place([whole('big', 30 * 1024, 'any'), whole('small', 0x800, 'any')], bk)
        self.assertEqual(w['big'][0], 0x02)                  # FastROM full: the fullest slow bank
        self.assertEqual(w['small'][0], 0xa0)                # FastROM first
        with self.assertRaises(SystemExit):                  # 'fast' never goes to a slow bank
            romplace.place([whole('f', 30 * 1024, 'fast')], bk)

    def test_fast_before_any(self):
        # biggest-first alone would put the 'any' 6000 B blob into the only FastROM room and leave the
        # 'fast' 5000 B one nowhere: 'fast' blobs are placed first, the 'any' one goes to the slow bank
        bk = banks({0xa0: 0x8000 - 10000}, slow=[(0x01, 0)])
        _, _, w = romplace.place([whole('any6k', 6000, 'any'), whole('fast5k', 5000, 'fast')], bk)
        self.assertEqual((w['fast5k'][0], w['any6k'][0]), (0xa0, 0x01))

    def test_too_big(self):
        with self.assertRaises(SystemExit):
            romplace.place([whole('a', 0x4000), whole('b', 0x4001)], banks({0xa0: 0x4000, 0xa1: 0x4000}))
        _, _, w = romplace.place([whole('a', 0x4000), whole('b', 0x4000)], banks({0xa0: 0x4000, 0xa1: 0x4000}))
        self.assertEqual({w['a'][0], w['b'][0]}, {0xa0, 0xa1})

    def test_order_independent(self):
        es = [whole('w%d' % i, random.Random(i).randint(1, 9000), long=i % 3 == 0) for i in range(60)]
        bk = banks({b: random.Random(b).randint(0, 0x7000) for b in range(0x81, 0x100)})
        ref = romplace.place(es, bk)[0]
        for seed in range(3):
            sh = list(es)
            random.Random(seed).shuffle(sh)
            self.assertEqual(romplace.place(sh, bk)[0].splitlines()[1:12], ref.splitlines()[1:12])

    def test_stream(self):
        # slow banks first, then the tails in bank order; tails under `min` units skipped
        bk = banks({0x81: 0x8000 - 9 * 100, 0x82: 0x8000 - 9 * 10, 0xa0: 0x8000 - 9 * 500},
                   slow=[(0x01, 0x8000 - 9 * 1000)])
        n = 1000 + 100 + 300
        tracks = [{'name': 'T0', 'first': 0, 'blocks': 1050, 'loop': 1020},
                  {'name': 'T1', 'first': 1050, 'blocks': 350, 'loop': None}]
        txt, rep, w = romplace.place([stream(9 * n, tracks=tracks)], bk)
        segs = [(w[k][0], w[k][2] // 9) for k in sorted((k for k in w if k.startswith('bgm_c')),
                                                       key=lambda k: int(k[5:]))]
        self.assertEqual(segs, [(0x01, 1000), (0x81, 100), (0xa0, 300)])   # $82 (10 blocks) skipped
        row = [l for l in txt.splitlines() if l.endswith('; T0')][0]
        v = [int(x.strip()[1:], 16) for x in row.split(';')[0].split('.byte')[1].split(',')]
        self.assertEqual(v[0], 0)                          # T0 starts in segment 0 ...
        self.assertEqual(v[3] | v[4] << 8, 1000)           # ... with 1000 blocks left in it
        self.assertEqual(v[8], 1)                          # its loop (block 1020) is in segment 1
        self.assertEqual(v[9] | v[10] << 8, (0x8000 + 0x8000 - 900 + 20 * 9) & 0xffff)
        self.assertEqual(v[11] | v[12] << 8, 80)
        self.assertEqual(v[13] | v[14] << 8 | v[15] << 16, 30)
        row = [l for l in txt.splitlines() if l.endswith('; T1')][0]
        v = [int(x.strip()[1:], 16) for x in row.split(';')[0].split('.byte')[1].split(',')]
        self.assertEqual((v[0], v[3] | v[4] << 8), (1, 50))
        self.assertEqual(v[8:], [0] * 8)                   # played once
        # the last chunk may be shorter than `min`
        txt, rep, w = romplace.place([stream(9 * 1003)], bk)
        self.assertEqual(w['bgm_c1'][0:3:2], (0x81, 3 * 9))

    def test_stream_limits(self):
        bk = banks({b: 0x8000 - 9 * 64 for b in range(0x81, 0x100)}, slow=[(b, 0x8000 - 9 * 64) for b in range(1, 0x7e)])
        with self.assertRaises(SystemExit):                # 252 banks of 64 blocks: does not fit
            romplace.place([stream(9 * 64 * 253)], bk)
        bk = banks({b: 0x8000 - 9 * 64 for b in range(0x81, 0x100)},
                   slow=[(b, 0x8000 - 9 * 64) for b in range(1, 0x7e)] + [(0x100 + b, 0x8000 - 9 * 64) for b in range(8)])
        romplace.place([stream(9 * 64 * 255)], bk)        # 255 segments: the tables hold them
        with self.assertRaises(SystemExit):                # fits, but in 256 segments
            romplace.place([stream(9 * 64 * 256)], bk)

    def test_bank_size(self):
        # $81 / $01 end at $FFB0 (build.py zero areas): 'size' is the area's length
        bk = [{'bank': 0x81, 'base': 0x8000, 'used': 0x7000, 'speed': 'fast', 'long': False, 'size': 0x7fb0},
              {'bank': 0xa0, 'base': 0x8000, 'used': 0x7000, 'speed': 'fast', 'long': True},
              {'bank': 0x01, 'base': 0x8000, 'used': 0, 'speed': 'slow', 'size': 0x7fb0}]
        _, _, w = romplace.place([whole('a', 0x1000 - 0x50 + 1)], bk)
        self.assertEqual(w['a'][0], 0xa0)                    # 1 byte more than $81 holds below $FFB0
        _, _, w = romplace.place([whole('a', 0x1000 - 0x50)], bk)
        self.assertEqual(w['a'][0], 0x81)
        txt, rep, w = romplace.place([stream(9 * 4000)], bk)
        self.assertEqual(w['bgm_c0'][0:3:2], (0x01, 9 * (0x7fb0 // 9)))   # 3632 blocks below $01:FFB0
        self.assertTrue(all(a + n <= 0x10000 - (0x50 if b in (0x81, 0x01) else 0) for b, a, n, e in w.values()))

    def test_exlorom_8mb(self):
        # the 8 MB shape: FastROM $81-$FF, slow $01-$7D (bank $00 is the copy of $80, never offered)
        fast = {b: (0x8000 - 0x3000 if b < 0xa0 else 0) for b in range(0x81, 0x100)}
        slow = [(b, 0) for b in range(0x01, 0x7e)]
        bk = banks(fast, slow)
        es = [whole('g%d' % i, 30000, 'any') for i in range(110)] + [whole('t%d' % i, 3000) for i in range(30)]
        _, _, w = romplace.place(es + [stream(9 * 64 * 300)], bk)
        in_slow = sorted(k for k, v in w.items() if v[0] < 0x80 and not k.startswith('bgm_c'))
        self.assertEqual(len(in_slow), 110 - 96)            # 96 empty FastROM banks take 30000 B each
        self.assertTrue(all(k.startswith('g') for k in in_slow))   # never a 'fast' one
        self.assertTrue(all(0x01 <= w[k][0] <= 0x7d for k in w if w[k][0] < 0x80))
        segs = sorted((w[k][0] for k in w if k.startswith('bgm_c')))
        self.assertLess(segs[0], 0x80)                      # the stream starts in the slow banks
        _, _, w = romplace.place([whole('g', 30000, 'any')], bk)
        self.assertGreaterEqual(w['g'][0], 0x81)            # FastROM not full: FastROM

    def test_stub_sizes(self):
        es = [whole('a', 10), stream(9 * 100, tracks=[{'name': 'T', 'first': 0, 'blocks': 100, 'loop': 0}])]
        st = romplace.stub(es)
        self.assertIn('bgm_seg_bank: .res 255', st)
        self.assertIn('bgm_table: .res 16', st)
        txt = romplace.place(es, banks({0xa0: 0}))[0]
        self.assertEqual(txt.count(', 0' * 254), 3)        # three tables of 255 entries, one used


def read_place(dst):
    """label -> (bank, address, size) from a build's kishidra.place.txt"""
    where = {}
    for line in open(os.path.join(dst, 'build', 'kishidra.place.txt')):
        p = line.split()
        if p and p[0][2:3] == ':':
            where[p[2]] = (int(p[0][:2], 16), int(p[0][3:], 16), int(p[1]))
    return where


def foff(bank, addr):
    return (bank & 0x7f) * 0x8000 + (addr & 0x7fff) + (0x400000 if bank < 0x80 else 0)


def romdata(a, b):
    r = subprocess.run([sys.executable, os.path.join(HERE, 'romdata.py'), a, b], capture_output=True, text=True)
    print('  romdata %s vs %s: %s' % (os.path.basename(a), os.path.basename(b), (r.stdout.strip().splitlines() or [r.stderr])[-1]))
    return r.returncode == 0


def build_copy(dst, rom_mb, tag, fakes):
    """write the fake manifest, build at ROM_MB, keep the ROM as build/<tag>.* (with its .lbl / .place.txt)"""
    gen = os.path.join(dst, 'data', 'gen')
    os.makedirs(os.path.join(gen, 'fake'), exist_ok=True)
    ents = []
    for lab, (size, speed, lg) in fakes.items():
        f = os.path.join(gen, 'fake', lab + '.bin')
        if not os.path.exists(f):
            open(f, 'wb').write(bytes((i * 7 + len(lab)) & 0xff for i in range(size)))
        ents.append({'label': lab, 'file': 'fake/%s.bin' % lab, 'size': size, 'kind': 'whole', 'speed': speed,
                     'long': lg})
    romplace.write_manifest(gen, 'zz_fake', ents)
    r = subprocess.run([sys.executable, os.path.join(dst, 'build.py'), 'rom'], capture_output=True, text=True,
                       cwd=dst, env=dict(os.environ, XDEFS='', PROFILE='', ROM_MB=str(rom_mb)))
    out = r.stdout.strip().splitlines()
    print('%s: %s' % (tag, out[-1] if out else r.stderr))
    if r.returncode:
        print(r.stdout + r.stderr)
        return None, None
    b = os.path.join(dst, 'build')
    for ext in ('sfc', 'lbl', 'map', 'place.txt'):
        shutil.copy(os.path.join(b, 'kishidra.' + ext), os.path.join(b, '%s.%s' % (tag, ext)))
    free = [line for line in out if line.startswith('ROM free after placing')]
    return os.path.join(b, tag + '.sfc'), free[0] if free else ''


def check_fakes(dst, rom, fakes):
    where = read_place(dst)
    d = open(rom, 'rb').read()
    ok = True
    for lab, (size, speed, lg) in fakes.items():
        b, a, n = where[lab]
        want = (0xa0 <= b <= 0xff) if lg else (0x81 <= b <= 0xff) if speed == 'fast' else True
        o = foff(b, a)
        same = d[o:o + n] == open(os.path.join(dst, 'data', 'gen', 'fake', lab + '.bin'), 'rb').read()
        ok &= want and same
        if not lab.startswith('fill') or not (want and same):
            print('  %-12s %5d B %-4s long=%-5s -> $%02X:%04X  %s, bytes %s' % (
                lab, size, speed, lg, b, a, 'OK' if want else 'WRONG BANK', 'OK' if same else 'DIFFER'))
    for lab in ('font_width', 'font_width_s', 'estat_index'):
        b = where[lab][0]
        print('  %-12s -> $%02X  %s' % (lab, b, 'OK' if b >= 0xa0 else 'WRONG BANK'))
        ok &= b >= 0xa0
    return ok, where


def build_test(keep=None):
    """build a copy of the port with fake blobs (4 MB, 8 MB, 8 MB with FastROM filled); check where they went"""
    tmp = tempfile.mkdtemp(prefix='placetest_', dir=os.environ.get('TMPDIR'))
    dst = os.path.join(tmp, 'kishidra_sfc')
    shutil.copytree(ROOT, dst, ignore=shutil.ignore_patterns('build', '__pycache__', 'runrom'))
    fakes = {'fake_any30k': (30 * 1024, 'any', False), 'fake_long': (2000, 'fast', True),
             'fake_fast': (5000, 'fast', False)}
    ok = True
    # 4 MB
    rom4, _ = build_copy(dst, 4, 'fake4', fakes)
    if not rom4:
        return False
    k, w4 = check_fakes(dst, rom4, fakes)
    ok &= k
    ref = os.path.join(ROOT, 'build', 'kishidra.sfc')
    if os.path.exists(ref):
        ok &= romdata(ref, rom4)
    # 8 MB: the same placement of the whole blobs, the stream in the slow banks
    rom8, free8 = build_copy(dst, 8, 'fake8', fakes)
    if not rom8:
        return False
    k, w8 = check_fakes(dst, rom8, fakes)
    ok &= k
    same = {l: v for l, v in w4.items() if not l.startswith('bgm_c')} == {l: v for l, v in w8.items() if not l.startswith('bgm_c')}
    segs = sorted(v[0] for l, v in w8.items() if l.startswith('bgm_c'))
    slow_ok = all(0x01 <= b <= 0x7d for b in segs)
    print('  whole blobs where they were at 4 MB: %s; BGM %d segments in $%02X-$%02X: %s' % (
        'OK' if same else 'DIFFER', len(segs), segs[0], segs[-1], 'OK (slow banks)' if slow_ok else 'NOT ALL SLOW'))
    ok &= same and slow_ok and romdata(rom4, rom8)
    # 8 MB with FastROM full: one 32 KB 'any' filler per empty FastROM bank, + 2
    empty = 0
    for a, z, n in re.findall(r'\$([0-9A-F]{2})(?:-\$([0-9A-F]{2}))?:(\d+)', free8.split('slow')[-1]):
        if int(n) == 0x8000 and int(a, 16) >= 0x80:
            empty += int(z or a, 16) - int(a, 16) + 1
    fill = dict(fakes)
    for i in range(empty + 2):
        fill['fill%03d' % i] = (0x8000, 'any', False)
    romf, _ = build_copy(dst, 8, 'fill8', fill)
    if not romf:
        return False
    k, wf = check_fakes(dst, romf, fill)
    ok &= k
    sys.path.insert(0, ROOT)
    import build
    used = build.area_used(os.path.join(dst, 'build', 'fill8.map'), os.path.join(dst, 'build', 'obj', 'kishidra.cfg'))
    fast_free = max(build.bank_size(b) - used.get(b, 0) for b in range(0x81, 0x100))
    ents = {e['label']: e for e in romplace.load(os.path.join(dst, 'data', 'gen'))}
    slow = [(l, v) for l, v in wf.items() if v[0] < 0x80 and not l.startswith('bgm_c')]
    bad = [l for l, v in slow if ents[l]['speed'] != 'any' or ents[l]['long'] or v[2] <= fast_free]
    real = [l for l, v in slow if not l.startswith('fill')]
    print('  %d empty FastROM banks, %d fillers: %d blobs in slow banks (%d of them real gfx, e.g. %s), largest FastROM '
          'gap left %d B: %s' % (empty, empty + 2, len(slow), len(real), ', '.join(sorted(real)[:3]), fast_free,
                                 'OK' if slow and not bad else 'WRONG: %s' % (bad or 'none in slow banks')))
    ok &= bool(slow) and not bad and romdata(rom8, romf)
    if keep:
        os.makedirs(keep, exist_ok=True)
        for tag in ('fake4', 'fake8', 'fill8'):
            for ext in ('sfc', 'lbl', 'map', 'place.txt'):
                shutil.copy(os.path.join(dst, 'build', '%s.%s' % (tag, ext)), keep)
        print('  ROMs kept in %s' % keep)
    shutil.rmtree(tmp)
    return ok


if __name__ == '__main__':
    do_build = '--build' in sys.argv
    keep = None
    if '--keep' in sys.argv:
        i = sys.argv.index('--keep')
        keep = os.path.abspath(sys.argv[i + 1])
        del sys.argv[i:i + 2]
    sys.argv = [a for a in sys.argv if a != '--build']
    res = unittest.main(exit=False, verbosity=1).result
    ok = res.wasSuccessful()
    if do_build:
        ok &= build_test(keep)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)
