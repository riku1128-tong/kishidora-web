#!/usr/bin/env python3
"""Sprites per scanline from the OAM shadow (_oam, 544 bytes, crt0.s): the PPU shows at most 32
sprites ("range") and 34 8-pixel slivers ("time") on a line and drops the rest.

usage: [ROM=x.sfc] oamlines.py [options] DUMP ...                WRAM dumps (128 KB, or the 544 bytes
                                                                of 'wram FILE oam 544')
       [ROM=x.sfc] oamlines.py [options] --run SCRIPT FRAMES    run SCRIPT, then look at the OAM of
                                                                every frame (--every K) for FRAMES
options: --lines N (visible lines, default 224), --size-select N (OBSEL bits 5-7; the port uses 1:
         8x8 / 32x32), --sprites N / --slivers N (limits, default 32 / 34), -v (every line of the
         worst frame), -o OUTDIR (keep the dumps of --run)
Counting (x and y as the PPU sees them): a sprite is on a line when one of its rows is; it counts for
the 32 when part of it is at x 0..255 (or x = -256, a hardware quirk: listed separately); its 8-px
columns count as slivers when part of them is at x 0..255.  Slots 0-95 are the field, 96-127 the
battle panel.  Exit status 1 when a frame is over a limit."""
import argparse
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hrun  # noqa: E402

SIZES = {0: ((8, 8), (16, 16)), 1: ((8, 8), (32, 32)), 2: ((8, 8), (64, 64)), 3: ((16, 16), (32, 32)),
         4: ((16, 16), (64, 64)), 5: ((32, 32), (64, 64)), 6: ((16, 32), (32, 64)), 7: ((16, 32), (32, 32))}


def lines_of(oam, size_select, nlines):
    """per line: [sprites in range, slivers, field slivers, panel slivers, x=-256 sprites]"""
    small, large = SIZES[size_select]
    L = [[0, 0, 0, 0, 0] for _ in range(nlines)]
    for i in range(128):
        xl, y = oam[4 * i], oam[4 * i + 1]
        hb = (oam[512 + i // 4] >> (2 * (i % 4))) & 3
        x = xl | (hb & 1) << 8
        if x >= 256:
            x -= 512
        w, h = large if hb & 2 else small
        quirk = x == -256
        if not quirk and not (-w < x < 256):
            continue
        cols = sum(1 for k in range(w // 8) if -8 < x + 8 * k < 256)
        for r in range(h):
            ln = (y + r) & 0xff
            if ln >= nlines:
                continue
            e = L[ln]
            if quirk:
                e[4] += 1
                continue
            e[0] += 1
            e[1] += cols
            e[2 if i < 96 else 3] += cols
    return L


def oam_of(path, syms):
    d = open(path, 'rb').read()
    if len(d) == 544:
        return d
    off = hrun.addr(syms, 'oam')
    return d[off:off + 544]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('dumps', nargs='*')
    ap.add_argument('--run', nargs=2, metavar=('SCRIPT', 'FRAMES'))
    ap.add_argument('--every', type=int, default=1)
    ap.add_argument('--lines', type=int, default=224)
    ap.add_argument('--size-select', type=int, default=1)
    ap.add_argument('--sprites', type=int, default=32)
    ap.add_argument('--slivers', type=int, default=34)
    ap.add_argument('-v', action='store_true')
    ap.add_argument('-o', '--out')
    a = ap.parse_args()
    syms = hrun.symbols(hrun.ROM) if (a.run or any(os.path.getsize(f) != 544 for f in a.dumps)) else {}
    files, td = list(a.dumps), None
    if a.run:
        td = os.path.abspath(a.out) if a.out else tempfile.mkdtemp(prefix='oam_')
        os.makedirs(td, exist_ok=True)
        text = hrun.prepare(a.run[0], td)
        for k in range(0, int(a.run[1]), a.every):
            f = os.path.join(td, 'oam_%05d.bin' % k)
            text += 'wram %s oam 544\nwait %d\n' % (f, a.every)
            files.append(f)
        r = hrun.run(hrun.ROM, text, td)
        if r.returncode:
            sys.exit('runrom failed (%d): %s' % (r.returncode, (r.stdout + r.stderr).strip()))
    if not files:
        sys.exit('no dumps (give DUMP files or --run SCRIPT FRAMES)')
    worst = None
    over = 0
    for f in files:
        L = lines_of(oam_of(f, syms), a.size_select, a.lines)
        ms = max(range(a.lines), key=lambda n: (L[n][1], L[n][0]))
        mr = max(range(a.lines), key=lambda n: (L[n][0], L[n][1]))
        bad = L[ms][1] > a.slivers or L[mr][0] > a.sprites
        over += bad
        q = sum(1 for e in L if e[4])
        if bad or len(files) == 1 or a.v:
            print('%s: slivers %d on line %d (field %d, panel %d), sprites %d on line %d%s%s' % (
                os.path.basename(f), L[ms][1], ms, L[ms][2], L[ms][3], L[mr][0], mr,
                ', %d lines with x=-256 sprites' % q if q else '', '  OVER' if bad else ''))
        if worst is None or (L[ms][1], L[mr][0]) > worst[0]:
            worst = ((L[ms][1], L[mr][0]), f, L, ms, mr)
    _, f, L, ms, mr = worst
    print('%d frame(s): worst %d slivers (line %d) / %d sprites (line %d) in %s; %d over the limits %d / %d' % (
        len(files), L[ms][1], ms, L[mr][0], mr, os.path.basename(f), over, a.slivers, a.sprites))
    if a.v:
        print('line sprites slivers field panel  (%s)' % os.path.basename(f))
        for n, e in enumerate(L):
            if e[0] or e[4]:
                print('%4d %7d %7d %5d %5d%s' % (n, e[0], e[1], e[2], e[3], '  x=-256: %d' % e[4] if e[4] else ''))
    if td and not a.out:
        shutil.rmtree(td, ignore_errors=True)
    sys.exit(1 if over else 0)


if __name__ == '__main__':
    main()
