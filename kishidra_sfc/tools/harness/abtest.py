#!/usr/bin/env python3
"""A/B test: run harness scripts on two ROMs with the same input and compare what they write
(screenshots, audio, dumps) byte for byte.

usage: abtest.py [-o OUTDIR] [-j N] [--check KINDS] [--wav-tol PCT] ROM_A ROM_B SCRIPT_OR_DIR ...

  SCRIPT_OR_DIR  runrom scripts (a folder: every *.txt in it).  Each runs from power-on on both ROMs;
                 its output files go to OUTDIR/<script>/a and /b as NNN_name (NNN = script line),
                 '#!mksave' lines make their saves first (hrun.py), relative 'loadsram' paths are
                 relative to the script (else to the current directory).  Symbols in poke / peek /
                 waitfor are looked up in each ROM's own .lbl / .map (so they follow code that
                 moved).  A script with a line '#!needs SYMBOL ...' is skipped when a ROM lacks
                 one of them (e.g. '#!needs dbg_cmd' for XDEFS=TEST_HOOKS ROMs).
  --check KINDS  what must be identical: shot, wav, peek (the values runrom's peek lines print),
                 dump (wram / sram / vram files); default shot,wav,peek.  Dumps are always
                 compared and listed.  A script runrom fails on (an unknown command, a bad
                 poke, a missing save, a waitfor timeout) is an error.  runrom's output is kept
                 in <side>/_stdout.txt.
  --wav-tol PCT  a wav that differs still passes when its RMS is within PCT % of the other's
                 (default: byte-identical).
  -o OUTDIR      default: a new folder under $TMPDIR.  Differing screenshots get a side-by-side
                 cmp_NNN_name.png (A left, B right) when Pillow is installed.
Exit status 0 when everything checked is identical."""
import argparse
import concurrent.futures as cf
import glob
import math
import os
import re
import shutil
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hrun  # noqa: E402


def scripts_of(args):
    out = []
    for a in args:
        if os.path.isdir(a):
            out += sorted(glob.glob(os.path.join(a, '*.txt')))
        elif os.path.exists(a):
            out.append(a)
        else:
            sys.exit('no such script: %s' % a)
    names, res = {}, []
    for s in out:
        n = os.path.splitext(os.path.basename(s))[0]
        if n in names:            # same name in two folders: prefix the folder
            n = os.path.basename(os.path.dirname(os.path.abspath(s))) + '_' + n
        names[n] = s
        res.append((n, s))
    return res


def ppm(path):
    d = open(path, 'rb').read()
    parts, i = [], 0
    while len(parts) < 4:         # P6 w h 255
        while d[i:i + 1].isspace():
            i += 1
        j = i
        while not d[j:j + 1].isspace():
            j += 1
        parts.append(d[i:j])
        i = j
    return int(parts[1]), int(parts[2]), d[i + 1:]


def shot_diff(pa, pb, out):
    (wa, ha, da), (wb, hb, db) = ppm(pa), ppm(pb)
    if (wa, ha) != (wb, hb):
        return 'size %dx%d vs %dx%d' % (wa, ha, wb, hb)
    n, x0, y0, x1, y1 = 0, wa, ha, -1, -1
    for k in range(0, len(da), 3):
        if da[k:k + 3] != db[k:k + 3]:
            n += 1
            x, y = (k // 3) % wa, (k // 3) // wa
            x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    try:
        from PIL import Image
        ia, ib = Image.frombytes('RGB', (wa, ha), da), Image.frombytes('RGB', (wb, hb), db)
        s = Image.new('RGB', (wa * 2 + 4, ha), (255, 0, 255))
        s.paste(ia, (0, 0))
        s.paste(ib, (wa + 4, 0))
        s.save(out)
    except ImportError:
        pass
    return '%d pixels in (%d,%d)-(%d,%d)' % (n, x0, y0, x1, y1)


def wav_rms(path):
    d = open(path, 'rb').read()[44:]
    n = len(d) // 2
    s = struct.unpack('<%dh' % n, d[:2 * n]) if n else ()
    return math.sqrt(sum(v * v for v in s) / n) if n else 0.0


def rom_symbols(rom):
    try:
        return hrun.symbols(rom)
    except SystemExit:          # no .lbl / .map: no symbols
        return {}


def run_one(job):
    name, script, tag, rom, outdir = job
    wd = os.path.join(outdir, name, tag)
    os.makedirs(wd, exist_ok=True)
    text = hrun.prepare(script, wd, wd)
    try:
        r = hrun.run(rom, text, wd)
        open(os.path.join(wd, '_stdout.txt'), 'w').write(r.stdout)
        return name, tag, r.returncode, (r.stdout + r.stderr).strip()
    except Exception as e:      # timeout and the like
        return name, tag, -1, str(e)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('rom_a')
    ap.add_argument('rom_b')
    ap.add_argument('scripts', nargs='+')
    ap.add_argument('-o', '--out')
    ap.add_argument('-j', '--jobs', type=int, default=min(8, os.cpu_count() or 2))
    ap.add_argument('--check', default='shot,wav,peek')
    ap.add_argument('--wav-tol', type=float)
    a = ap.parse_args()
    check = set(a.check.split(','))
    if check - {'shot', 'wav', 'peek', 'dump'}:
        sys.exit('--check takes shot, wav, peek, dump')
    out = os.path.abspath(a.out or tempfile.mkdtemp(prefix='abtest_'))
    syms = None
    todo, skipped = [], []
    for n, s in scripts_of(a.scripts):
        shutil.rmtree(os.path.join(out, n), ignore_errors=True)    # nothing left from an earlier run
        need = [w for l in open(s).read().splitlines() if l.startswith('#!needs') for w in l.split()[1:]]
        if need and syms is None:
            syms = [rom_symbols(a.rom_a), rom_symbols(a.rom_b)]
        miss = [w for w in need if not all(w in y or '_' + w in y for y in syms)]
        (skipped if miss else todo).append((n, s, miss))
    jobs = [(n, s, t, r, out) for n, s, _ in todo for t, r in (('a', a.rom_a), ('b', a.rom_b))]
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(run_one, jobs))
    status = {(n, t): (rc, msg) for n, t, rc, msg in res}
    tot = {'same': 0, 'diff': 0, 'fail': 0}
    kinds = {}
    for name, _, miss in skipped:
        print('%-24s skipped (a ROM has no %s)' % (name, ', '.join(miss)))
    for name, _, _ in todo:
        bad = []
        for t in 'ab':
            rc, msg = status[(name, t)]
            if rc:
                bad.append('runrom on %s exit %d: %s' % (t.upper(), rc, msg.splitlines()[-1] if msg else ''))
        da, db = os.path.join(out, name, 'a'), os.path.join(out, name, 'b')
        files = sorted(f for f in set(os.listdir(da)) | set(os.listdir(db)) if re.match(r'^\d{3}_', f))   # the script's outputs
        nsame = 0
        for f in files:
            pa, pb = os.path.join(da, f), os.path.join(db, f)
            kind = 'shot' if f.endswith('.ppm') else 'wav' if f.endswith('.wav') else 'dump'
            k = kinds.setdefault(kind, [0, 0])
            if not (os.path.exists(pa) and os.path.exists(pb)):
                bad.append('%s: only in %s' % (f, 'A' if os.path.exists(pa) else 'B'))
                k[1] += 1
                continue
            xa, xb = open(pa, 'rb').read(), open(pb, 'rb').read()
            if xa == xb:
                nsame += 1
                k[0] += 1
                continue
            if kind == 'shot':
                why = shot_diff(pa, pb, os.path.join(out, name, 'cmp_' + f[:-4] + '.png'))
            elif kind == 'wav':
                ra, rb = wav_rms(pa), wav_rms(pb)
                why = 'rms %.0f vs %.0f' % (ra, rb)
                if a.wav_tol is not None and abs(ra - rb) <= a.wav_tol / 100.0 * max(ra, rb, 1.0):
                    nsame += 1
                    k[0] += 1
                    print('  %s %s: rms within %.1f%% (%s)' % (name, f, a.wav_tol, why))
                    continue
            else:
                why = '%d bytes differ' % (sum(1 for x, y in zip(xa, xb) if x != y) + abs(len(xa) - len(xb)))
            k[1] += 1
            bad.append('%s: DIFF %s%s' % (f, why, '' if kind in check else ' (not checked)'))
        pk = [[' '.join(l.split()[:2] + l.split()[3:]) for l in open(os.path.join(d, '_stdout.txt')).read().splitlines()
               if l.startswith('peek ')] if os.path.exists(os.path.join(d, '_stdout.txt')) else [] for d in (da, db)]
        if pk[0] or pk[1]:        # 'peek NAME OFFSET: bytes (frame n)' without the offset (code moves)
            k = kinds.setdefault('peek', [0, 0])
            if pk[0] == pk[1]:
                nsame += len(pk[0])
                k[0] += len(pk[0])
            else:
                k[1] += 1
                n = max(len(pk[0]), len(pk[1]))
                pa_, pb_ = pk[0] + ['-'] * (n - len(pk[0])), pk[1] + ['-'] * (n - len(pk[1]))
                bad += ['peek DIFF: A %s / B %s%s' % (x, y, '' if 'peek' in check else ' (not checked)')
                        for x, y in zip(pa_, pb_) if x != y][:8]
        failing = [b for b in bad if not b.endswith('(not checked)')]
        tot['fail' if any(b.startswith('runrom') for b in bad) else 'diff' if failing else 'same'] += 1
        print('%-24s %s  (%d files identical)' % (name, 'FAIL' if failing else 'same', nsame))
        for b in bad:
            print('    ' + b)
    print('kinds: ' + ', '.join('%s %d identical / %d differ' % (k, v[0], v[1]) for k, v in sorted(kinds.items())))
    print('%d script(s): %d identical, %d differ, %d failed to run, %d skipped; output in %s' %
          (sum(tot.values()) + len(skipped), tot['same'], tot['diff'], tot['fail'], len(skipped), out))
    sys.exit(0 if tot['diff'] == tot['fail'] == 0 else 1)


if __name__ == '__main__':
    main()
