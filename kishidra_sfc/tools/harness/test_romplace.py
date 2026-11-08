#!/usr/bin/env python3
"""Tests of the shared ROM placer (tools/romplace.py).

usage: test_romplace.py            unit tests of romplace.place() on made-up banks (no build)
       test_romplace.py --build    + a build of a copy of the port (in $TMPDIR) with fake blobs added
                                   through an extra manifest: a 30 KB 'any' blob must land in a FastROM
                                   bank, a 'long' blob in $A0-$FF, a 'fast' one in FastROM; the data the
                                   ROM serves (romdata.py) must equal the normal build's
Exit status 0 when all pass."""
import json
import os
import random
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

    def test_stub_sizes(self):
        es = [whole('a', 10), stream(9 * 100, tracks=[{'name': 'T', 'first': 0, 'blocks': 100, 'loop': 0}])]
        st = romplace.stub(es)
        self.assertIn('bgm_seg_bank: .res 255', st)
        self.assertIn('bgm_table: .res 16', st)
        txt = romplace.place(es, banks({0xa0: 0}))[0]
        self.assertEqual(txt.count(', 0' * 254), 3)        # three tables of 255 entries, one used


def build_test():
    """build a copy of the port with fake blobs; check where they went"""
    tmp = tempfile.mkdtemp(prefix='placetest_', dir=os.environ.get('TMPDIR'))
    dst = os.path.join(tmp, 'kishidra_sfc')
    shutil.copytree(ROOT, dst, ignore=shutil.ignore_patterns('build', '__pycache__', 'runrom'))
    gen = os.path.join(dst, 'data', 'gen')
    os.makedirs(os.path.join(gen, 'fake'), exist_ok=True)
    blobs = {'fake_any30k': (30 * 1024, 'any', False), 'fake_long': (2000, 'fast', True),
             'fake_fast': (5000, 'fast', False)}
    ents = []
    for lab, (size, speed, lg) in blobs.items():
        open(os.path.join(gen, 'fake', lab + '.bin'), 'wb').write(bytes((i * 7 + len(lab)) & 0xff for i in range(size)))
        ents.append({'label': lab, 'file': 'fake/%s.bin' % lab, 'size': size, 'kind': 'whole', 'speed': speed,
                     'long': lg})
    romplace.write_manifest(gen, 'zz_fake', ents)
    r = subprocess.run([sys.executable, os.path.join(dst, 'build.py'), 'rom'], capture_output=True, text=True,
                       cwd=dst, env=dict(os.environ, XDEFS='', PROFILE=''))
    print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr)
    if r.returncode:
        print(r.stdout + r.stderr)
        return False
    where = {}
    for line in open(os.path.join(dst, 'build', 'kishidra.place.txt')):
        p = line.split()
        if p and p[0][2:3] == ':':
            where[p[2]] = (int(p[0][:2], 16), int(p[0][3:], 16), int(p[1]))
    ok = True
    rom = open(os.path.join(dst, 'build', 'kishidra.sfc'), 'rb').read()
    for lab, (size, speed, lg) in blobs.items():
        b, a, n = where[lab]
        want = (0xa0 <= b <= 0xff) if lg else (0x81 <= b <= 0xff)
        o = (b & 0x7f) * 0x8000 + (a & 0x7fff)
        same = rom[o:o + n] == open(os.path.join(gen, 'fake', lab + '.bin'), 'rb').read()
        print('%-12s %5d B %-4s long=%-5s -> $%02X:%04X  %s, bytes %s' % (
            lab, size, speed, lg, b, a, 'OK' if want else 'WRONG BANK', 'OK' if same else 'DIFFER'))
        ok &= want and same
    # every long blob of the real data in $A0+ too, and the served data unchanged
    for lab in ('font_width', 'font_width_s', 'estat_index'):
        b = where[lab][0]
        print('%-12s -> $%02X  %s' % (lab, b, 'OK' if b >= 0xa0 else 'WRONG BANK'))
        ok &= b >= 0xa0
    ref = os.path.join(ROOT, 'build', 'kishidra.sfc')
    if os.path.exists(ref):
        r = subprocess.run([sys.executable, os.path.join(HERE, 'romdata.py'), ref,
                            os.path.join(dst, 'build', 'kishidra.sfc')], capture_output=True, text=True)
        print('romdata vs build/kishidra.sfc: ' + r.stdout.strip().splitlines()[-1])
        ok &= r.returncode == 0
    shutil.rmtree(tmp)
    return ok


if __name__ == '__main__':
    do_build = '--build' in sys.argv
    sys.argv = [a for a in sys.argv if a != '--build']
    res = unittest.main(exit=False, verbosity=1).result
    ok = res.wasSuccessful()
    if do_build:
        ok &= build_test()
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)
