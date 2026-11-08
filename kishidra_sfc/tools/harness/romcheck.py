#!/usr/bin/env python3
"""Which memory map would each emulator / flash cart pick for a ROM, and is the ROM built right for
its size?  Python ports of the header detectors of snes9x (memmap.cpp LoadROMInt / ScoreHiROM /
ScoreLoROM, the same in 1.54 .. master), bsnes v115 / ares (heuristics/super-famicom.cpp scoreHeader
and board()), sd2snes / FXPak Pro (src/smc.c smc_id / smc_headerscore, v1.11.2) and Mesen2
(Core/SNES/BaseCartridge.cpp GetHeaderScore / LoadRom).  They agree with the two installed libretro
cores (snes9x 1.61, bsnes-mercury) on every probe ROM of tools/harness/probe (docs/plan/rom_compat.md).

usage: romcheck.py [-q] ROM.sfc ...
       romcheck.py --selftest ROM.sfc     damage copies of a good ROM in memory (each rule below once, the
                                          halves of a 6 / 8 MB file swapped) and check problems() names each;
                                          also build.py's check_layout() on a linker config with code in
                                          a slow bank

The checks (problems(); build.py runs them on every ROM it writes):
  every size     $FFD5 map mode $30 (LoROM + FastROM), $FFD8 $05 (32 KB SRAM), $81:FFB0-FFFF (file
                 0xFFB0) zero, the checksum (snes9x / NSRT mirror sum) and its complement right
  4 MB           $FFD7 $0C; snes9x LoROM, bsnes LOROM-RAM (header 0x7FB0), sd2snes LoROM mask $3FFFFF
  6 / 8 MB       ExLoROM: $FFD7 $0D in both header copies, bank $00 (file 0x400000) = bank $80 (file 0),
                 $01:FFB0-FFFF (file 0x40FFB0) zero; snes9x JumboLoROM BIGFIRST, bsnes EXLOROM-RAM (header
                 0x407FB0), sd2snes LoROM mapper with mask $7FFFFF.  (Mesen2 maps it as plain LoROM:
                 not supported, reported only.)
Exit status 0 when every ROM passes; -q prints only the problems."""
import sys

MB = 0x100000


def u16(d, a):
    return d[a] | d[a + 1] << 8 if a + 1 < len(d) else 0


def allascii(b):
    return all(32 <= c < 127 for c in b)


# ---------------------------------------------------------------- snes9x
def s9x_hi(d, size, off=0):
    b = 0xff00 + off
    if b + 0x100 > len(d):
        return -99
    g = lambda i: d[b + i]
    s = 0
    if g(0xd7) == 13 and size > 0x400000: s += 3
    if g(0xd5) & 1: s += 2
    if g(0xd5) == 0x23: s -= 2
    if g(0xd4) == 0x20: s += 2
    if u16(d, b + 0xdc) + u16(d, b + 0xde) == 0xffff:
        s += 2
        if u16(d, b + 0xde): s += 1
    if g(0xda) == 0x33: s += 2
    if (g(0xd5) & 0xf) < 4: s += 2
    if not (g(0xfd) & 0x80): s -= 6
    if u16(d, b + 0xfc) > 0xffb0: s -= 2
    if size > 0x300000: s += 4
    if g(0xd7) > 12: s -= 1
    if not allascii(d[b + 0xb0:b + 0xb6]): s -= 1
    if not allascii(d[b + 0xc0:b + 0xc0 + 20]): s -= 1
    return s


def s9x_lo(d, size, off=0):
    b = 0x7f00 + off
    if b + 0x100 > len(d):
        return -99
    g = lambda i: d[b + i]
    s = 0
    if not (g(0xd5) & 1): s += 3
    if g(0xd5) == 0x23: s += 2
    if u16(d, b + 0xdc) + u16(d, b + 0xde) == 0xffff:
        s += 2
        if u16(d, b + 0xde): s += 1
    if g(0xda) == 0x33: s += 2
    if (g(0xd5) & 0xf) < 4: s += 2
    if not (g(0xfd) & 0x80): s -= 6
    if u16(d, b + 0xfc) > 0xffb0: s -= 2
    if size <= 0x1000000: s += 2
    if g(0xd7) >= 7 and (1 << (g(0xd7) - 7)) > 48: s -= 1
    if not allascii(d[b + 0xb0:b + 0xb6]): s -= 1
    if not allascii(d[b + 0xc0:b + 0xc0 + 20]): s -= 1
    return s


def snes9x(d):
    """(map, format, text): map LoROM / HiROM / JumboLoROM / ExtendedHiROM, format normal / BIGFIRST /
    SMALLFIRST (snes9x swaps the two halves of the file)"""
    size = (len(d) + 0x1fff) // 0x2000 * 0x2000
    ext = size > 0x400000 and u16(d, 0x7fd5) not in (0x1320, 0x1420, 0x1520, 0x1a20, 0x3423, 0x3523, 0x4332, 0x4532) \
        and u16(d, 0xffd5) not in (0xf93a, 0xf53a)
    hi, lo = s9x_hi(d, size), s9x_lo(d, size)
    fmt = 'normal'
    if ext:
        shi, slo = s9x_hi(d, size, 0x400000), s9x_lo(d, size, 0x400000)
        if max(slo, shi) >= max(lo, hi):
            fmt, hi, lo = 'BIGFIRST', shi, slo
        else:
            fmt = 'SMALLFIRST'
    if lo >= hi:
        m = 'JumboLoROM' if ext else 'LoROM'
    else:
        m = 'ExtendedHiROM' if ext else 'HiROM'
    txt = '%s%s, %s%s (lo %d hi %d)' % (m, ' (=ExLoROM)' if m == 'JumboLoROM' else '', fmt,
                                        ' (halves swapped!)' if fmt == 'SMALLFIRST' else '', lo, hi)
    return m, fmt, txt


# ---------------------------------------------------------------- bsnes / ares
def bs_score(d, a):
    if len(d) < a + 0x50:
        return 0
    comp, chk, rst = u16(d, a + 0x2c), u16(d, a + 0x2e), u16(d, a + 0x4c)
    mode = d[a + 0x25] & ~0x10
    if rst < 0x8000:
        return 0
    op = d[(a & ~0x7fff) | (rst & 0x7fff)]
    s = 0
    if op in (0x78, 0x18, 0x38, 0x9c, 0x4c, 0x5c): s += 8
    if op in (0xc2, 0xe2, 0xad, 0xae, 0xac, 0xaf, 0xa9, 0xa2, 0xa0, 0x20, 0x22): s += 4
    if op in (0x40, 0x60, 0x6b, 0xcd, 0xec, 0xcc): s -= 4
    if op in (0x00, 0x02, 0xdb, 0x42, 0xff): s -= 8
    if chk + comp == 0xffff: s += 4
    if a == 0x7fb0 and mode == 0x20: s += 2
    if a == 0xffb0 and mode == 0x21: s += 2
    return max(0, s)


def bsnes(d):
    """(board, header offset, text)"""
    L, H, XL, XH = (bs_score(d, a) for a in (0x7fb0, 0xffb0, 0x407fb0, 0x40ffb0))
    if XL: XL += 4
    if XH: XH += 4
    if L >= H and L >= XL and L >= XH: h = 0x7fb0
    elif H >= XL and H >= XH: h = 0xffb0
    elif XL >= XH: h = 0x407fb0
    else: h = 0x40ffb0
    mm = d[h + 0x25]
    mode = {0x20: 'LOROM', 0x30: 'LOROM', 0x21: 'HIROM', 0x31: 'HIROM', 0x22: 'SDD1', 0x32: 'SDD1', 0x23: 'SA1', 0x33: 'SA1',
            0x25: 'EXHIROM', 0x35: 'EXHIROM', 0x2a: 'SPC7110', 0x3a: 'SPC7110'}.get(mm)
    if not mode:
        mode = {0x7fb0: 'LOROM', 0xffb0: 'HIROM', 0x407fb0: 'EXLOROM', 0x40ffb0: 'EXHIROM'}[h]
    if mode == 'LOROM' and h == 0x407fb0:
        mode = 'EXLOROM'
    board = mode + ('-RAM' if d[h + 0x28] else '')
    return board, h, '%s board, header $%06X (scores lo %d hi %d exlo %d exhi %d)' % (board, h, L, H, XL, XH)


# ---------------------------------------------------------------- sd2snes / FXPak Pro
def sd_score(d, addr):
    hoff = 0x200 if (addr & 0xfff) == 0x1b0 else 0
    if addr + 0x50 > len(d):
        return 0
    h = d[addr:addr + 0x50]
    licensee, ramsize, carttype, romsize, dest = h[0x2a], h[0x28], h[0x26], h[0x27], h[0x29]
    mapper = h[0x25] & ~0x10
    bsx = ramsize & ~0x10
    rst = u16(h, 0x4c)
    s = 0
    s += 2 * (licensee == 0x33)
    s += 4 * ((u16(h, 0x2c) + u16(h, 0x2e)) == 0xffff)
    s += carttype < 8
    s += romsize < 0x10
    s += ramsize < 8
    s += dest < 0x0e
    s += (not (dest & 0x40) and not (dest & 0xf))
    bsx_adj = 0
    if h[0x02:0x06] == bytes([0, 1, 0, 0]):
        s += 1
        bsx_adj = 2
    if not bsx_adj and rst < 0x8000:
        return 0
    a = addr - hoff
    if a == 0x7fb0 and (mapper == 0x20 or bsx == 0x20): s += 2
    if a == 0xffb0 and (mapper == 0x21 or bsx == 0x21): s += 2
    if a == 0x7fb0 and mapper == 0x22: s += 2
    if a == 0x40ffb0 and mapper == 0x25: s += 2
    fa = ((a & ~0x7fff) | (rst & 0x7fff)) + hoff
    op = d[fa] if fa < len(d) else 0xff
    if op in (0x78, 0x18, 0x38, 0x9c, 0x4c, 0x5c): s += 8
    elif op in (0xc2, 0xe2, 0xad, 0xae, 0xac, 0xaf, 0xa9, 0xa2, 0xa0, 0x20, 0x22): s += 4
    elif op in (0x40, 0x60, 0x6b, 0xcd, 0xec, 0xcc): s -= 4 - bsx_adj
    elif op in (0x00, 0x02, 0xdb, 0x42, 0xff): s -= 8 - bsx_adj
    if s and addr > 0x400000: s += 4
    return max(0, s)


def sd2snes(d):
    """(mapper, header offset, ROM mask, text)"""
    addrs = [0xffb0, 0x101b0, 0x7fb0, 0x81b0, 0x40ffb0, 0x4101b0]
    best, idx = 1, 2
    sc = []
    for i, a in enumerate(addrs):
        s = sd_score(d, a)
        sc.append(s)
        if s >= best:
            best, idx = s, i
    h = d[addrs[idx]:addrs[idx] + 0x50]
    m = h[0x25] & 0xef
    mapper = {0x20: 'LoROM', 0x21: 'HiROM', 0x22: 'LoROM/S-DD1', 0x25: 'ExHiROM', 0x23: 'SA-1'}.get(m, '?')
    romsize = 1024 << h[0x27]
    while len(d) > romsize:
        romsize <<= 1
    txt = '%s mapper%s, header $%06X, ROM mask $%06X (scores %s)' % (
        mapper, ' (ExLoROM above 4 MB: ROM addr = {~A23,A22:16,A14:0})' if mapper == 'LoROM' else '',
        addrs[idx], romsize - 1, sc)
    return mapper, addrs[idx], romsize - 1, txt


# ---------------------------------------------------------------- Mesen2
def me_score(d, base):
    if len(d) < base + 0x7fff:
        return -1
    h = d[base + 0x7fb0:base + 0x8000]
    mode = h[0x25] & ~0x10
    s = 0
    if mode in (0x20, 0x22) and base < 0x8000: s += 1
    elif mode in (0x21, 0x25) and base >= 0x8000: s += 1
    if h[0x26] < 8: s += 1
    if h[0x27] < 0x10: s += 1
    if h[0x28] < 8: s += 1
    chk, comp = u16(h, 0x2e), u16(h, 0x2c)
    if chk + comp == 0xffff and chk and comp: s += 8
    rst = u16(d, base + 0x7ffc)
    if rst < 0x8000:
        return -1
    op = d[base + (rst & 0x7fff)]
    if op in (0x18, 0x78, 0x4c, 0x5c, 0x20, 0x22, 0x9c): s += 8
    elif op in (0xc2, 0xe2, 0xa9, 0xa2, 0xa0): s += 4
    elif op in (0x00, 0xff, 0xcc): s -= 8
    return max(0, s)


def mesen2(d):
    best, pick = -1, 0
    for b in (0, 0x200, 0x8000, 0x8200, 0x408000, 0x408200):
        s = me_score(d, b)
        if s >= 0 and s >= best:
            best, pick = s, b
    if (pick & 0x8000) == 0:
        m = 'LoROM (00-7D and 80-FF:8000 both = file 0-4 MB; ExLoROM NOT supported)' if len(d) > 0x400000 else 'LoROM'
    else:
        m = 'ExHiROM' if pick & 0x400000 else 'HiROM'
    return '%s, header base $%06X' % (m, pick)


# ---------------------------------------------------------------- the port's own rules
def mirror_sum(d, start, length, mask=0x800000):
    """snes9x Checksum_Calculate / NSRT: a tail that is not a power of two counts as mirrored up to the
    next power of two (6 MB = sum(0-4 MB) + 2 * sum(4-6 MB))"""
    while mask and not (length & mask):
        mask >>= 1
    part1 = sum(d[start:start + mask])
    rest = length - mask
    part2 = 0
    if rest:
        part2 = mirror_sum(d, start + mask, rest, mask >> 1)
        n = rest
        while n < mask:
            n += n
            part2 += part2
    return part1 + part2


def problems(d):
    """[] when the ROM is a correct 4 MB LoROM or 6 / 8 MB ExLoROM build of the port, else what is wrong"""
    bad = []
    if len(d) not in (4 * MB, 6 * MB, 8 * MB):
        return ['size %d bytes: not 4, 6 or 8 MB' % len(d)]
    ex = len(d) > 4 * MB
    heads = [0x7fb0] + ([0x407fb0] if ex else [])
    want_size = 0x0d if ex else 0x0c
    for h in heads:
        if d[h + 0x25] != 0x30:
            bad.append('header $%06X: map mode $%02X, want $30 (LoROM + FastROM)' % (h, d[h + 0x25]))
        if d[h + 0x27] != want_size:
            bad.append('header $%06X: ROM size byte $%02X, want $%02X' % (h, d[h + 0x27], want_size))
        if d[h + 0x28] != 0x05:
            bad.append('header $%06X: SRAM size byte $%02X, want $05 (32 KB)' % (h, d[h + 0x28]))
        if u16(d, h + 0x4c) < 0x8000:
            bad.append('header $%06X: reset vector $%04X' % (h, u16(d, h + 0x4c)))
    if d[0xffb0:0x10000] != bytes(0x50):
        bad.append('$81:FFB0-FFFF (file 0xFFB0) is not zero')
    if ex:
        if d[0x400000:0x408000] != d[0:0x8000]:
            bad.append('bank $00 (file 0x400000) is not a copy of bank $80 (file 0)')
        if d[0x40ffb0:0x410000] != bytes(0x50):
            bad.append('$01:FFB0-FFFF (file 0x40FFB0) is not zero')
    tmp = bytearray(d)
    for h in heads:
        tmp[h + 0x2c:h + 0x30] = b'\xff\xff\x00\x00'
    s = mirror_sum(tmp, 0, len(tmp)) & 0xffff
    for h in heads:
        if u16(d, h + 0x2e) != s or u16(d, h + 0x2c) != s ^ 0xffff:
            bad.append('header $%06X: checksum $%04X / $%04X, want $%04X / $%04X' % (
                h, u16(d, h + 0x2e), u16(d, h + 0x2c), s, s ^ 0xffff))
    m, fmt, _ = snes9x(d)
    if (m, fmt) != (('JumboLoROM', 'BIGFIRST') if ex else ('LoROM', 'normal')):
        bad.append('snes9x would map it as %s, %s' % (m, fmt))
    board, h, _ = bsnes(d)
    if (board, h) != (('EXLOROM-RAM', 0x407fb0) if ex else ('LOROM-RAM', 0x7fb0)):
        bad.append('bsnes / ares would pick %s at $%06X' % (board, h))
    mapper, h, mask, _ = sd2snes(d)
    if (mapper, h, mask) != ('LoROM', 0x7fb0, 0x7fffff if ex else 0x3fffff):
        bad.append('sd2snes / FXPak Pro would pick %s at $%06X, mask $%06X' % (mapper, h, mask))
    return bad


def plain_sum(b):
    """(selftest) the plain byte sum in both headers, as a tool that does not mirror would write it"""
    for h in (0x7fb0, 0x407fb0):
        b[h + 0x2c:h + 0x30] = b'\xff\xff\x00\x00'
    v = sum(b) & 0xffff
    for h in (0x7fb0, 0x407fb0):
        b[h + 0x2c:h + 0x30] = bytes([(v ^ 0xffff) & 0xff, (v ^ 0xffff) >> 8, v & 0xff, v >> 8])


def selftest(path):
    import os
    import tempfile
    d = open(path, 'rb').read()
    ok = not problems(d)
    print('%-44s %s' % ('the ROM itself', 'no problem' if ok else 'PROBLEMS: %s' % problems(d)))
    ex = len(d) > 4 * MB
    cases = [('map mode $20', lambda b: b.__setitem__(0x7fd5, 0x20)),
             ('$81:FFB0 not zero', lambda b: b.__setitem__(0xffc0, 0xff)),
             ('one ROM byte changed (checksum)', lambda b: b.__setitem__(0x123456, b[0x123456] ^ 1)),
             ('SRAM byte', lambda b: b.__setitem__(0x7fd8, 0x03))]
    if ex:
        cases += [('$FFD7 $0C in the copy', lambda b: b.__setitem__(0x407fd7, 0x0c)),
                  ('bank $00 differs from $80', lambda b: b.__setitem__(0x401000, b[0x401000] ^ 1)),
                  ('$01:FFB0 not zero', lambda b: b.__setitem__(0x40ffe0, 0x12)),
                  ('no header copy at 0x407FB0', lambda b: b.__setitem__(slice(0x407fb0, 0x408000), b'\xff' * 0x50))]
        if len(d) == 6 * MB:
            cases.append(('plain byte sum as the checksum (6 MB)', plain_sum))
    else:
        cases += [('$FFD7 $0D', lambda b: b.__setitem__(0x7fd7, 0x0d))]
    for name, f in cases:
        b = bytearray(d)
        f(b)
        bad = problems(bytes(b))
        ok &= bool(bad)
        print('%-44s %s' % (name, ('caught: ' + bad[0]) if bad else 'NOT CAUGHT'))
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.path.insert(0, root)
    import build
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as t:
        for name, line in [('code segment in a slow bank', '  CODE5: load=EXT02, type=ro;'),
                           ('data segment in bank $00', '  PL00: load=EXT00, type=ro;'),
                           ('fixed segment BANK01', '  BANK01: load=EXT01, type=ro;')]:
            cfg = os.path.join(t, 'x.cfg')
            open(cfg, 'w').write('SEGMENTS {\n  CODE1: load=ROM01, type=ro;\n  PL02: load=EXT02, type=ro;\n%s\n}\n' % line)
            try:
                build.check_layout(cfg)
                caught = False
            except SystemExit as e:
                caught = str(e)
            ok &= bool(caught)
            print('%-44s %s' % ('build.check_layout: ' + name, ('caught: ' + caught) if caught else 'NOT CAUGHT'))
    print('selftest ' + ('PASS' if ok else 'FAIL'))
    return ok


def main():
    if sys.argv[1:2] == ['--selftest']:
        sys.exit(0 if all(selftest(p) for p in sys.argv[2:]) else 1)
    args = [a for a in sys.argv[1:] if a != '-q']
    quiet = '-q' in sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    ok = True
    for p in args:
        d = open(p, 'rb').read()
        bad = problems(d)
        ok &= not bad
        if not quiet:
            print('== %s (%d bytes = %.1f MB)' % (p, len(d), len(d) / MB))
            print('  snes9x  : ' + snes9x(d)[2])
            print('  bsnes   : ' + bsnes(d)[2])
            print('  sd2snes : ' + sd2snes(d)[3])
            print('  Mesen2  : ' + mesen2(d))
        for b in bad:
            print('  PROBLEM : %s%s' % ('' if not quiet else p + ': ', b))
        if not quiet:
            print('  verdict : ' + ('OK' if not bad else 'FAIL'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
