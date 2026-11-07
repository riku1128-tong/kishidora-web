#!/usr/bin/env python3
"""Pixel widths of game texts (from data/gen/textdata.s and the font widths), for laying out boxes.

usage: textwidth.py REGEX [REGEX ...]   text id names to show (e.g. 'T_DIFF_.*' 'SPN_\\d+')
Each line: name, widest line in pixels, number of lines, the text.  {n} counts as N_DIGITS digits,
{s} (an inserted text) as the widest text of S_WIDTH pixels (env), shown with a '+s'."""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
N_DIGITS = int(os.environ.get('N_DIGITS', '4'))
S_WIDTH = int(os.environ.get('S_WIDTH', '0'))


def load():
    src = open(os.path.join(ROOT, 'data', 'gen', 'textdata.s')).read()
    # text_table: .word addr / .byte bank per text
    tab = src[src.index('text_table:'):]
    ptrs = re.findall(r'\.word \$([0-9a-f]+)\s*\n\s*\.byte \$([0-9a-f]+)', tab)
    # banks of bytes: segment name -> bytes in order
    data = {}
    cur = None
    for line in src[:src.index('text_table:')].splitlines():
        m = re.match(r'\.segment "BANK([0-9A-F]+)"', line)
        if m:
            cur = int(m.group(1), 16)
            data.setdefault(cur, bytearray())
            continue
        if line.strip().startswith('.byte') and cur is not None:
            data[cur] += bytes(int(v, 16) for v in re.findall(r'\$([0-9a-f]+)', line))
    wid = open(os.path.join(ROOT, 'data', 'gen', 'font.bin.wid'), 'rb').read()
    chars = open(os.path.join(ROOT, 'data', 'gen', 'font.bin.chars'), encoding='utf-8').read().split('\n')
    names = {}
    for m in re.finditer(r'#define\s+(\w+)\s+(\d+)', open(os.path.join(ROOT, 'data', 'gen', 'text.h')).read()):
        names.setdefault(int(m.group(2)), m.group(1))
    texts = {}
    for i, (a, b) in enumerate(ptrs):
        bank, addr = int(b, 16), int(a, 16) - 0x8000
        d = data.get(bank & 0x7f) or data.get(bank)
        codes = []
        while True:
            c = d[addr] | d[addr + 1] << 8
            addr += 2
            if c == 0:
                break
            codes.append(c)
        texts[names.get(i, '#%d' % i)] = codes
    return texts, wid, chars


def measure(codes, wid, chars):
    lines, w, s, txt = [], 0, False, ''
    for c in codes:
        if c == 1:
            lines.append(w); w = 0; txt += '\\n'
        elif c in (3, 12, 11):
            w += 6 * N_DIGITS; txt += '{n}'
        elif c == 4:
            w += S_WIDTH; s = True; txt += '{s}'
        elif c == 2:
            w += 48; txt += '{name}'
        elif 5 <= c <= 10:
            w = (w + 7) & ~7 if w & 7 else w; txt += '{c%d}' % (c - 5)
        elif c >= 16:
            w += wid[c - 16] & 0x7f; txt += chars[c - 16] if c - 16 < len(chars) else '?'
    lines.append(w)
    return max(lines), len(lines), s, txt


def main():
    texts, wid, chars = load()
    for pat in sys.argv[1:]:
        for name, codes in texts.items():
            if re.fullmatch(pat, name):
                w, n, s, txt = measure(codes, wid, chars)
                print('%-24s %4d%s %d  %s' % (name, w, '+s' if s else '  ', n, txt))


if __name__ == '__main__':
    main()
