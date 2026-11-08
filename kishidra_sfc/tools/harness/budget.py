#!/usr/bin/env python3
"""Memory budgets of a link (docs/plan/PLAN.md §5.3 / §5.4) from its ld65 map.

usage: budget.py [MAP]      (default build/kishidra.map; exit status 1 when a gate fails)

  bank $80     STARTUP + ENGINE + the load images of CODE / STUBS / RODATA / DATA + header / vectors
               (gate: <= 30 KB)
  code banks   CODEn + ENGn of each bank $81-$9F with code (gate: <= 30 KB each; + the data in the
               rest of the bank: BANKxx + PLxx, placed by tools/romplace.py)
  low WRAM     CODE + STUBS + LOWBSS in $0200-$1FFF (gate: >= 0.5 KB free)
  WRAM $7E     RODATA + DATA + BSS in $7E:2000-$F6FF (the C data; reported)
  slow banks   ROM_MB=6 / 8 (ExLoROM): the placed data in $01-$7D (PL01..PL7D: DMA-read art and the BGM
               stream only; reported)"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
KB = 1024
BANK_MAX, LOW_FREE_MIN = 30 * KB, 512
LOW_SIZE, CRAM_SIZE = 0x1e00, 0xd700          # build.py linker_cfg: LOWRAM, CRAM


def segments(path):
    txt = open(path).read()
    m = re.search(r'Segment list:.*?\n\n', txt, re.S)
    out = {}
    for line in (m.group(0) if m else '').splitlines():
        p = line.split()
        if len(p) >= 4 and re.match(r'^[0-9A-F]{6}$', p[1]):
            out[p[0]] = out.get(p[0], 0) + int(p[3], 16)
    return out


def main():
    mp = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'kishidra.map')
    seg = segments(mp)
    g = seg.get
    b80 = sum(g(k, 0) for k in ('STARTUP', 'ENGINE', 'CODE', 'STUBS', 'RODATA', 'DATA', 'HEADER', 'VECTORS'))
    low = g('CODE', 0) + g('STUBS', 0) + g('LOWBSS', 0)
    cram = g('RODATA', 0) + g('DATA', 0) + g('BSS', 0)
    fails = []
    print('bank $80: %d B (%.1f KB) of %d' % (b80, b80 / KB, BANK_MAX))
    if b80 > BANK_MAX:
        fails.append('bank $80')
    rows = []
    for b in range(1, 32):
        code = g('CODE%d' % b, 0) + g('ENG%d' % b, 0)
        data = g('BANK%02X' % (0x80 + b), 0) + g('PL%02X' % (0x80 + b), 0)   # fixed + placed data
        if code:
            rows.append('$%02X %6d%s' % (0x80 + b, code, ' (+%d data)' % data if data else ''))
            if code > BANK_MAX:
                fails.append('bank $%02X' % (0x80 + b))
    print('code per bank (B): ' + ', '.join(rows))
    print('low WRAM: CODE %d + STUBS %d + LOWBSS %d = %d of %d, free %d' %
          (g('CODE', 0), g('STUBS', 0), g('LOWBSS', 0), low, LOW_SIZE, LOW_SIZE - low))
    if LOW_SIZE - low < LOW_FREE_MIN:
        fails.append('low WRAM')
    print('WRAM $7E: RODATA %d + DATA %d + BSS %d = %d of %d, free %d' %
          (g('RODATA', 0), g('DATA', 0), g('BSS', 0), cram, CRAM_SIZE, CRAM_SIZE - cram))
    slow = [(b, g('PL%02X' % b, 0)) for b in range(0x01, 0x7e) if g('PL%02X' % b, 0)]
    if slow:
        print('slow banks $01-$7D: %d B (%.1f KB) placed in %d banks ($%02X-$%02X)' % (
            sum(n for b, n in slow), sum(n for b, n in slow) / KB, len(slow), slow[0][0], slow[-1][0]))
    if fails:
        print('GATE FAILED: ' + ', '.join(fails))
        sys.exit(1)
    print('gates OK (banks <= 30 KB, low WRAM >= 0.5 KB free)')


if __name__ == '__main__':
    main()
