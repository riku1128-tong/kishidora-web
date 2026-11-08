#!/usr/bin/env python3
"""Checks of the text / UI kit (plan step S6) on an XDEFS=TEST_HOOKS ROM: runs tools/harness/t/s6_*.txt
and tests what they print and dump.

usage: kitcheck.py [-o OUTDIR] [ROM]      (default build/kishidra.sfc; its .lbl / .map next to it)

  s6_text   {k} (code 15) and the text_lh line step: pairs of BG3 canvas dumps must be identical
  s6_wins   a window closed under a later one (12 rounds each, ui.c dbg_wins; side by side and
            overlapping): the save stack does not grow, the BG1 / BG3 maps are as before; and the
            fallback under more than a screen of windows: the stack does not grow
  s6_name   name_input: NI_NONEMPTY, NI_CANCEL, 8 glyphs, the buffer and {name} afterwards
  s6_kit    mk_confirm3 answers, mk_num_spr's OAM bytes
Exit status 0 when every check passes."""
import argparse
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hrun  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
T = os.path.join(HERE, 't')
fails = []


def check(ok, what):
    print('%s  %s' % ('ok  ' if ok else 'FAIL', what))
    if not ok:
        fails.append(what)


def run(rom, name, outdir):
    script = os.path.join(T, name + '.txt')
    wd = os.path.join(outdir, name)
    os.makedirs(wd, exist_ok=True)
    r = hrun.run(rom, hrun.prepare(script, wd), wd)
    open(os.path.join(wd, '_stdout.txt'), 'w').write(r.stdout + r.stderr)
    if r.returncode:
        check(False, '%s: runrom exit status %d (%s)' % (name, r.returncode, (r.stdout + r.stderr).strip()[-200:]))
        return None, wd
    peeks = []                      # (symbol, [bytes]) in script order
    for line in r.stdout.splitlines():
        m = re.match(r'peek (\S+) [0-9a-f]+: ((?:[0-9a-f]{2} ?)+)', line)
        if m:
            peeks.append((m.group(1), [int(x, 16) for x in m.group(2).split()]))
    return peeks, wd


def words(b):
    return [b[i] | b[i + 1] << 8 for i in range(0, len(b) - 1, 2)]


def same(wd, a, b):
    return open(os.path.join(wd, a), 'rb').read() == open(os.path.join(wd, b), 'rb').read()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', dest='out')
    ap.add_argument('rom', nargs='?', default=os.path.join(hrun.ROOT, 'build', 'kishidra.sfc'))
    a = ap.parse_args()
    out = a.out or tempfile.mkdtemp(prefix='kitcheck_')
    syms = hrun.symbols(a.rom)
    if 'dbg_buf' not in syms and '_dbg_buf' not in syms:
        sys.exit('%s has no dbg_buf: build it with XDEFS=TEST_HOOKS' % a.rom)

    p, wd = run(a.rom, 's6_text', out)
    if p is not None:
        check(same(wd, 'k_codes.bin', 'k_glyphs.bin'), 's6_text: {n}/{m}/{k} = 7/4321/123456 as the digit glyphs')
        check(same(wd, 'lh16.bin', 'lh16_ref.bin'), 's6_text: text_lh 16 puts the next line 16 px down')
        check(same(wd, 'lh14.bin', 'lh14_ref.bin'), 's6_text: text_lh 0 keeps 14 px')
        check(same(wd, 'small16.bin', 'small16_ref.bin'), 's6_text: the 8-dot font keeps 9 px with text_lh 16')
        check(not same(wd, 'lh16.bin', 'lh14.bin') and not same(wd, 'k_codes.bin', 'lh16.bin'),
              's6_text: the dumps show different drawings (the checks above compare something)')
        check(p and p[-1][1][0] == 19, 's6_text: all 19 commands ran')

    p, wd = run(a.rom, 's6_wins', out)
    if p is not None and len(p) == 6:
        for k, (tag, what) in enumerate((('', 'side by side (the forge guide)'), ('_over', 'overlapping'))):
            ack, res = p[2 * k][1], words(p[2 * k + 1][1])[0]
            check(ack == [4] and res == 0, 's6_wins %s: the save stack did not grow in 12 rounds (grew %d)' % (what, res))
            check(same(wd, 'bg1_before.bin', 'bg1_after%s.bin' % tag) and same(wd, 'bg3_before.bin', 'bg3_after%s.bin' % tag),
                  's6_wins %s: BG1 / BG3 maps as before (no window cells left on the screen)' % what)
        check(p[4][1] == [4] and words(p[5][1])[0] == 0, 's6_wins more than a screen of windows over the card '
              '(the fallback): the save stack did not grow in 4 rounds (grew %d)' % words(p[5][1])[0])
    elif p is not None:
        check(False, 's6_wins: expected 6 peeks, got %d' % len(p))

    p, wd = run(a.rom, 's6_name', out)
    if p is not None and len(p) == 7:
        check(p[0][1] == [0], 's6_name: START on an empty name with NI_NONEMPTY stays on the screen')
        check(words(p[1][1])[:2] == [1, 545], 's6_name: decided (1), {name} back to the hero (545 ゆ)')
        check(words(p[2][1]) == [438, 0, 0, 0, 0, 0, 9, 9, 9], 's6_name: buffer = あ, 6 glyphs written, [6..] untouched')
        check(words(p[3][1])[:2] == [0, 545], 's6_name: SELECT with NI_CANCEL returns 0, {name} restored')
        check(words(p[4][1]) == [44, 34, 109, 30, 124, 137, 247, 0, 0], 's6_name: cancelled 8-glyph name leaves the buffer')
        check(words(p[5][1])[0] == 1 and words(p[6][1]) == [0] * 7, 's6_name: new-game mode accepts an empty name')
    elif p is not None:
        check(False, 's6_name: expected 7 peeks, got %d' % len(p))

    p, wd = run(a.rom, 's6_kit', out)
    if p is not None and len(p) == 11:
        r = [words(x[1])[0] for x in p[0:3]]
        check(r == [1, 0, 0], 'mk_confirm3: A on yes = 1, B = 0, LEFT + A = 0 (got %s)' % r)

        def spr(x, y, tiles, pal):
            o = []
            for i, t in enumerate(tiles):
                o += [(x + 5 * i) & 0xff, y, t, pal << 1 | 3 << 4]
            return o
        cases = [(p[3], p[4], spr(100, 50, [22, 13, 14, 15], 4), '+123'),
                 (p[5], p[6], spr(8, 200, [23, 16, 17], 5), '-45'),
                 (p[7], p[8], spr(-3, 7, [22, 12], 6), '0'),
                 (p[9], p[10], spr(40, 60, [23, 15, 14, 19, 18, 20], 4), '-32768')]
        for n, oam, want, what in cases:
            check(words(n[1])[0] == len(want) // 4 and oam[1] == want, 'mk_num_spr %s: %d sprites, OAM %s'
                  % (what, words(n[1])[0], ' '.join('%02x' % v for v in oam[1])))
    elif p is not None:
        check(False, 's6_kit: expected 11 peeks, got %d' % len(p))

    print('output in %s' % out)
    print('%d check(s) failed' % len(fails) if fails else 'all checks passed')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
