#!/usr/bin/env python3
"""Probe ROMs for the > 4 MB layouts (docs/plan/rom_compat.md §2, §7): try an emulator or a flash cart
with these before loading the game.  The screen turns GREEN when every check passed, RED when one
failed (an emulator that does not boot the layout at all usually stays black).

usage: mkprobe.py [OUTDIR]      (default build/probe) -> exlorom6.sfc, exlorom8.sfc (the layout of
                                ROM_MB=6 / 8), exhirom6.sfc, exhirom8.sfc and meta.json (what each
                                probe reads and expects; tools/harness/probe/runprobe.py)

Every 32 KB block of the file holds its block number (u16) at block offsets $0100 and $7F00.  The probe
runs from bank $00 at reset, reads a list of CPU addresses into WRAM $7E:0000.. (2 bytes each), writes
test bytes to SRAM, reads them back to $7E:0100.., compares everything with the expected values and
sets $7E:01FE = $A5 (pass, green backdrop) or $5A (fail, red).
ExLoROM probe: $80/$BF/$C0/$FF/$81:8100 = blocks 0/63/64/127/1 (the first 4 MB, FastROM half),
$01/$3F:8100 = blocks 129/191 and, 8 MB, $40/$7D:8100 = 192/253 (file above 4 MB), SRAM $70:0000 and
$70:7FFF.  Headers at 0x7FB0 and 0x407FB0 (map mode $30, $FFD7 $0D), zeros at 0x40FFB0, as the game.
ExHiROM probe (for a later ExHiROM layout, PLAN §7 Q3): header at 0x40FFB0 (mode $35), SRAM in the
8 KB windows $A0-$A3:6000 and $20:6000."""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))


def code(reads, writes, exp):
    """65816 code: reads -> $7E:0000.. (2 B each); SRAM test bytes -> $7E:0100..; then the compare and
    the verdict on screen (green = pass, red = fail; $7E:01FE = $A5 / $5A).  Returns (code, offsets of
    the jmp operands to patch with the fail address, offset of the fail code)"""
    c = bytearray([0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20])          # sei clc xce rep #$10 sep #$20
    c += bytes([0xA9, 0x8F, 0x8D, 0x00, 0x21, 0x9C, 0x2C, 0x21, 0x9C, 0x00, 0x42])  # forced blank, TM=0, NMITIMEN=0
    for i, a in enumerate(reads):
        for k in range(2):
            c += bytes([0xAF]) + struct.pack('<I', a + k)[:3]            # lda long
            c += bytes([0x8F, (i * 2 + k) & 0xff, (i * 2 + k) >> 8, 0x7E])  # sta $7E:xxxx
    for a, v in writes:
        c += bytes([0xA9, v, 0x8F]) + struct.pack('<I', a)[:3]           # lda #v ; sta long
    for i, (a, v) in enumerate(writes):
        c += bytes([0xAF]) + struct.pack('<I', a)[:3] + bytes([0x8F, 0x00 + i, 0x01, 0x7E])
    fails = []
    for i, e in enumerate(exp):
        for k in range(2):
            c += bytes([0xAF, (i * 2 + k) & 0xff, (i * 2 + k) >> 8, 0x7E, 0xC9, (e >> (8 * k)) & 0xff, 0xF0, 0x03, 0x4C])
            fails.append(len(c))
            c += b'\0\0'
    for i, (a, v) in enumerate(writes):
        c += bytes([0xAF, i, 0x01, 0x7E, 0xC9, v, 0xF0, 0x03, 0x4C])
        fails.append(len(c))
        c += b'\0\0'

    def finish(col, flag):
        return bytes([0x9C, 0x21, 0x21, 0xA9, col & 0xff, 0x8D, 0x22, 0x21, 0xA9, col >> 8, 0x8D, 0x22, 0x21,
                      0xA9, 0x0F, 0x8D, 0x00, 0x21, 0xA9, flag, 0x8F, 0xFE, 0x01, 0x7E, 0x80, 0xFE])
    c += finish(0x03E0, 0xA5)                                           # green, pass
    fail_at = len(c)
    c += finish(0x001F, 0x5A)                                           # red, fail
    return c, fails, fail_at


def place(rom, at, cpu_addr, c):
    code_b, fails, fail_at = c
    code_b = bytearray(code_b)
    for f in fails:
        code_b[f:f + 2] = struct.pack('<H', cpu_addr + fail_at)
    rom[at:at + len(code_b)] = code_b


def header(rom, at, mapmode, title):
    h = bytearray(0x50)
    h[0x00:0x02] = b'01'
    h[0x02:0x06] = b'PRBE'
    h[0x10:0x25] = title.encode().ljust(21)
    h[0x25] = mapmode
    h[0x26] = 0x02
    h[0x27] = 0x0D
    h[0x28] = 0x05
    h[0x29] = 0
    h[0x2a] = 0x33
    vec = [0] * 16
    vec[14] = 0x8200                                                    # emulation RESET: the code at block offset $0200
    h[0x30:0x50] = struct.pack('<16H', *vec)
    rom[at:at + 0x50] = h


def mirror_sum(d, start, length, mask=0x800000):
    while mask and not (length & mask):
        mask >>= 1
    p1 = sum(d[start:start + mask])
    rest = length - mask
    p2 = 0
    if rest:
        p2 = mirror_sum(d, start + mask, rest, mask >> 1)
        n = rest
        while n < mask:
            n += n
            p2 += p2
    return p1 + p2


def checksum(rom, heads):
    for h in heads:
        rom[h + 0x2c:h + 0x30] = b'\xff\xff\x00\x00'
    s = mirror_sum(rom, 0, len(rom)) & 0xffff
    for h in heads:
        rom[h + 0x2c:h + 0x30] = struct.pack('<HH', s ^ 0xffff, s)


def markers(rom):
    for blk in range(len(rom) // 0x8000):
        for off in (0x0100, 0x7F00):
            rom[blk * 0x8000 + off:blk * 0x8000 + off + 2] = struct.pack('<H', blk)


def exlorom(size):
    rom = bytearray(b'\xff' * size)
    markers(rom)
    reads = [0x808100, 0xBF8100, 0xC08100, 0xFF8100, 0x818100, 0x018100, 0x3F8100]
    exp = [0, 63, 64, 127, 1, 129, 191]
    if size > 0x600000:
        reads += [0x408100, 0x7D8100]
        exp += [192, 253]
    writes = [(0x700000, 0x11), (0x707FFF, 0x22)]
    c = code(reads, writes, exp)
    for base in (0x000000, 0x400000):          # entry code in bank $80 and its copy in bank $00 (file 4 MB)
        place(rom, base + 0x200, 0x8200, c)
    header(rom, 0x7FB0, 0x30, 'EXLOROM PROBE')
    header(rom, 0x407FB0, 0x30, 'EXLOROM PROBE')
    rom[0x40FFB0:0x410000] = bytes(0x50)
    checksum(rom, [0x7FB0, 0x407FB0])
    return rom, reads, exp, {0x0000: 0x11, 0x7FFF: 0x22}


def exhirom(size):
    rom = bytearray(b'\xff' * size)
    markers(rom)
    reads = [0xC00100, 0xC08100, 0xFF8100, 0x808100, 0xBF8100, 0x400100, 0x408100, 0x5F8100, 0x018100, 0x1F8100]
    exp = [0, 1, 127, 1, 127, 128, 129, 191, 131, 191]
    if size > 0x600000:
        reads += [0x608100, 0x7D8100, 0x3F8100]
        exp += [193, 251, 255]
    writes = [(0xA06000, 0x11), (0xA16000, 0x33), (0xA26000, 0x44), (0xA37FFF, 0x22), (0x206001, 0x55)]
    c = code(reads, writes, exp)
    place(rom, 0x408200, 0x8200, c)             # $00:8200 = file 0x408200
    header(rom, 0x40FFB0, 0x35, 'EXHIROM PROBE')
    checksum(rom, [0x40FFB0])
    return rom, reads, exp, {0x0000: 0x11, 0x2000: 0x33, 0x4000: 0x44, 0x7FFF: 0x22, 0x0001: 0x55}


PROBES = [('exlorom6', exlorom, 0x600000), ('exlorom8', exlorom, 0x800000),
          ('exhirom6', exhirom, 0x600000), ('exhirom8', exhirom, 0x800000)]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'probe')
    os.makedirs(out, exist_ok=True)
    meta = {}
    for name, fn, size in PROBES:
        rom, reads, exp, sram_exp = fn(size)
        open(os.path.join(out, name + '.sfc'), 'wb').write(rom)
        meta[name] = {'reads': ['$%06X' % a for a in reads], 'expect_blocks': exp,
                      'sram': {str(k): v for k, v in sram_exp.items()}}
    json.dump(meta, open(os.path.join(out, 'meta.json'), 'w'), indent=1)
    print('probe ROMs in %s: %s' % (out, ', '.join(n + '.sfc' for n, f, s in PROBES)))


if __name__ == '__main__':
    main()
