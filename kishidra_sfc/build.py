#!/usr/bin/env python3
"""キシドラ！ SFC 版のビルド。

    python build.py            データの作り直し（必要なときだけ）+ ROM を作る
    python build.py rom        ROM だけ作る（data/gen にあるデータを使う）
    python build.py data       data/gen のデータだけ作り直す（Safari 版のファイルから）
    python build.py clean

必要なもの: Python 3.9+、cc65 (cc65 / ca65 / ld65)。データの作り直しには Pillow と
zstandard（pip install pillow zstandard）、BGM の変換には ffmpeg も使う。

出力: build/kishidra.sfc（LoROM / FastROM / 4MB / SRAM 32KB。ROM_MB=6 / 8 で ExLoROM）、build/kishidra.map と
build/kishidra.lbl（リンクの記号。tools/harness の runrom などが読む）、build/kishidra.place.txt
（データの置き場所）

データの置き場所: 変換スクリプトはバンクを決めない。data/gen のファイルと data/gen/place/*.json
（tools/romplace.py）を書き、ここで 2 回リンクする。1 回目はデータなしでコードと固定の領域
（C のコード、ENGINE、TABLES など）の大きさを測り、tools/romplace.py が残りの場所にデータを置いて
2 回目をリンクする。ROM のデータのバンクは .bank(label) で書く（8 ビットなら #<.bank(x) /
.byte <.bank(x)、^label は不可）。lda f:label で読むデータは TABLES か固定の領域に置くか、
manifest で "long": true にする（ビルドのとき check_far_reads() が調べる）

ROM の大きさ（環境変数 ROM_MB）:
    ROM_MB=4                 既定。LoROM 4MB（$80-$FF、FastROM）
    ROM_MB=6 / ROM_MB=8      ExLoROM 6MB / 8MB。ファイルの先頭 4MB は 4MB の ROM と同じ並び（$80-$FF）、
                             その先が $00-$3F / $00-$7D:8000-FFFF（SlowROM。データと BGM のストリームだけ、
                             コードは置かない）。$00 は $80 の写し（リセット・割り込みの入口とヘッダー）、
                             ヘッダーは 2 か所（$FFD7 = $0D）、チェックサムは snes9x / NSRT の折り返し和。
                             $81:FFB0-FFFF と $01:FFB0-FFFF は 0（ほかの形のヘッダーと読まれないように）。
                             動く: snes9x 1.54 以降、bsnes v115 / ares、FXPak Pro / SD2SNES 1.8.0 以降、MiSTer。
                             動かない: Mesen2、Super EverDrive、snes9x 1.50-1.53（6MB なら可）、bsnes-mercury。
                             できた ROM は tools/harness/romcheck.py と同じ検査を通す（通らなければビルドの失敗）

テスト用の ROM（環境変数）:
    PROFILE=1                計測用（tools/harness の prof.py / textcheck.py）
    XDEFS="TEST_HOOKS FORCE_WAVE=0,0,6"
                             テストだけの定義（空白で区切る。cc65 には全部、ca65 には値が数か
                             値のないものだけ = 1 で渡す）。TEST_HOOKS: sys.c のテスト用の命令箱
"""
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, 'src')
GEN = os.path.join(ROOT, 'data', 'gen')
OBJ = os.path.join(ROOT, 'build', 'obj')
OUT = os.path.join(ROOT, 'build', 'kishidra.sfc')

ROM_BANKS = 128          # 4MB LoROM: banks $80-$FF
# ROM_MB=4 (LoROM) | 6 | 8 (ExLoROM).  ExLoROM: file 0-4 MB = banks $80-$FF (FastROM, the same as the
# 4 MB ROM), file 4 MB+ = banks $00-$3F / $00-$7D:8000-FFFF (SlowROM: placed data and the BGM stream
# only, never code).  Bank $00 is a copy of bank $80 (the CPU takes the vectors and runs the reset /
# NMI / IRQ / BRK entry code there, crt0.s STARTUP), so nothing is placed in it.  File 0x7F0000-0x7FFFFF
# (8 MB) is not reachable (banks $7E / $7F are WRAM): $FF.
ROM_MB = os.environ.get('ROM_MB', '4') or '4'
if ROM_MB not in ('4', '6', '8'):
    sys.exit('ROM_MB は 4 / 6 / 8')
ROM_SIZE = int(ROM_MB) << 20
SLOW_BANKS = list(range(0x01, {'4': 0x01, '6': 0x40, '8': 0x7e}[ROM_MB]))
ZERO_AT = 0xffb0         # $81:FFB0-FFFF and $01:FFB0-FFFF: 80 zero bytes (no HiROM / ExHiROM header there)
TABLE_BANK = 0xC0        # segment TABLES (first in $C0): tables read with f: that point at placed data
CC65_HOME = os.environ.get('CC65_HOME', '')


def tool(name):
    if CC65_HOME:
        p = os.path.join(CC65_HOME, 'bin', name)
        if os.path.exists(p) or os.path.exists(p + '.exe'):
            return p
    return name


def none_lib():
    cands = [os.path.join(CC65_HOME, 'lib', 'none.lib')] if CC65_HOME else []
    cands += ['/usr/share/cc65/lib/none.lib', '/usr/local/share/cc65/lib/none.lib', r'C:\cc65\lib\none.lib']
    for c in cands:
        if os.path.exists(c):
            return c
    out = subprocess.run([tool('cl65'), '--print-target-path'], capture_output=True, text=True).stdout.strip()
    p = os.path.join(os.path.dirname(out), 'lib', 'none.lib')
    if os.path.exists(p):
        return p
    sys.exit('none.lib (cc65) が見つからない。CC65_HOME を設定して。')


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.stderr.write(' '.join(cmd) + '\n' + r.stdout + r.stderr)
        sys.exit(1)
    return r.stdout


# C files and the ROM bank (1..) their code goes to.  Bank N = $80+N.
# Code that calls each other often should share a bank (cross-bank calls go through
# a small trampoline in low WRAM).
C_BANKS = {
    'main.c': 1, 'sys.c': 1, 'text.c': 1, 'ui.c': 1,
    'title.c': 2, 'story.c': 2, 'names.c': 2,
    'battle_core.c': 3, 'battle_hit.c': 4,
    'battle_view.c': 5, 'battle_fx.c': 6,
    'adventure.c': 7, 'rewards.c': 7, 'save.c': 8, 'inventory.c': 8,
    'map.c': 9, 'menu.c': 10, 'equip.c': 10, 'gacha.c': 11, 'forge.c': 12, 'debug.c': 13, 'dex.c': 14,
    'sound.c': 1, 'tables.c': 15,
}
C_BANK_LAST = 31          # banks $81..$9F can hold C code
DBG = ['-g'] if os.environ.get('PROFILE') else []   # symbols in build/kishidra.dbg for the profiler
CDBG = ['--debug-info'] if os.environ.get('PROFILE') else []   # ... and C source lines (prof.py LINES=1)


def c_functions(path):
    """Names of the non-static functions a C file defines (style: one definition per line,
    'type name(args) {' at column 0)."""
    names = []
    txt = open(path, encoding='utf-8').read()
    for m in re.finditer(r'^(?!static\b)(?!typedef\b)[A-Za-z_][\w \*]*?\b([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*\{', txt, re.M):
        if m.group(1) in ('if', 'for', 'while', 'switch', 'return', 'sizeof'):
            continue
        names.append(m.group(1))
    return names


def xdefs():
    """XDEFS="A B=1 C=0,0,6": test-only defines -> (cc65 args, ca65 args).  ca65 takes only
    numbers (and its -D without a value is 0, so a bare name gets 1 there, as in cc65)."""
    cdefs, adefs = [], []
    for x in os.environ.get('XDEFS', '').split():
        name, eq, val = x.partition('=')
        if not re.match(r'^[A-Za-z_]\w*$', name):
            sys.exit('XDEFS: %s は名前=値 にして' % x)
        cdefs += ['-D', x]
        if not eq:
            adefs += ['-D', name + '=1']
        elif re.match(r'^-?(\d+|\$[0-9A-Fa-f]+|0x[0-9A-Fa-f]+)$', val):
            adefs += ['-D', x]
    return cdefs, adefs


def build_rom():
    os.makedirs(OBJ, exist_ok=True)
    xc, xa = xdefs()
    cfiles = sorted(glob.glob(os.path.join(SRC, 'c', '*.c'))) + sorted(glob.glob(os.path.join(GEN, '*.c')))
    sfiles = sorted(glob.glob(os.path.join(SRC, 'asm', '*.s'))) + sorted(glob.glob(os.path.join(GEN, '*.s')))
    banks = {}
    funcs = {}
    for c in cfiles:
        b = C_BANKS.get(os.path.basename(c))
        if b is None:
            sys.exit('build.py の C_BANKS に %s のバンクを書いて' % os.path.basename(c))
        banks[c] = b
        funcs[c] = c_functions(c)
    # per bank: -D redirect so calls inside the bank skip the trampoline
    bank_funcs = {}
    for c, b in banks.items():
        bank_funcs.setdefault(b, []).extend(funcs[c])
    allf = [f for fs in bank_funcs.values() for f in fs]
    dup = {f for f in allf if allf.count(f) > 1}
    if dup:
        sys.exit('同じ名前の関数がある: %s' % ', '.join(sorted(dup)))
    objs = []
    compiled = set()          # functions cc65 really defined (a definition may sit in an #ifdef)
    inc = ['-I', os.path.join(SRC, 'c'), '-I', GEN]
    for c in cfiles:
        b = banks[c]
        base = os.path.splitext(os.path.basename(c))[0]
        s = os.path.join(OBJ, base + '.s')
        o = os.path.join(OBJ, base + '.o')
        defs = (['-D', 'PROFILE=1'] if os.environ.get('PROFILE') else []) + xc
        for f in bank_funcs[b]:
            if f != 'main':
                defs += ['-D', '%s=%s__b' % (f, f)]
        run([tool('cc65'), '-O', '-Oirs', '-Cl', '-t', 'none', '--cpu', '65sc02', '--code-name', 'CODE%d' % b] + DBG + CDBG + [
             '--rodata-name', 'RODATA', '--bss-name', 'BSS', '--data-name', 'DATA', '-o', s] + inc + defs + [c])
        compiled.update(re.findall(r'^\s*\.export\s+_(\w+)__b\s*$', open(s).read(), re.M))
        run([tool('ca65'), '--cpu', '65816'] + DBG + ['-o', o, s])
        objs.append(o)
    # trampolines: _f (low WRAM) -> jsl f__t (bank) -> jsr _f__b -> rtl
    tr = ['.p816', '.smart -']
    for b in sorted(bank_funcs):
        for f in bank_funcs[b]:
            if f == 'main' or f not in compiled:
                continue
            tr += ['.import _%s__b' % f, '.export _%s' % f,
                   '.segment "STUBS"', '_%s:' % f, '  .byte $22', '  .word .loword(%s__t)' % f, '  .byte $%02x' % (0x80 + b), '  rts',
                   '.segment "CODE%d"' % b, '%s__t:' % f, '  jsr _%s__b' % f, '  rtl']
    tp = os.path.join(OBJ, 'trampolines.s')
    open(tp, 'w').write('\n'.join(tr) + '\n')
    sfiles.append(tp)
    asm_inc = ['-I', os.path.join(SRC, 'asm'), '-I', GEN, '--bin-include-dir', GEN]
    if os.environ.get('PROFILE'):
        asm_inc += ['-D', 'PROFILE=1']   # PC sampler (tools/harness/prof.py)
    asm_inc += xa
    for s in sfiles:
        o = os.path.join(OBJ, 'a_' + os.path.splitext(os.path.basename(s))[0] + '.o')
        run([tool('ca65'), '--cpu', '65816'] + DBG + asm_inc + ['-o', o, s])
        objs.append(o)
    cfg = os.path.join(OBJ, 'kishidra.cfg')
    open(cfg, 'w').write(linker_cfg(sorted(set(banks.values()))))
    mp = os.path.join(ROOT, 'build', 'kishidra.map')
    # data placement (tools/romplace.py): pass 1 links without the placed blobs (their labels in the
    # unwritten area PLSTUB), the placer fills the space left in every bank, pass 2 links the result
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import romplace
    entries = romplace.load(GEN)
    pp = os.path.join(OBJ, 'place.s')
    po = os.path.join(OBJ, 'a_place.o')
    open(pp, 'w').write(romplace.stub(entries))
    check_far_reads(sfiles + [pp] + sorted(glob.glob(os.path.join(SRC, 'asm', '*.inc'))) +
                    [os.path.join(OBJ, os.path.splitext(os.path.basename(c))[0] + '.s') for c in cfiles], entries)
    run([tool('ca65'), '--cpu', '65816'] + asm_inc + ['-o', po, pp])
    run([tool('ld65'), '-C', cfg, '-o', OUT, '-m', mp] + objs + [po, none_lib()])
    used = area_used(mp, cfg)
    # long blobs (read with lda f:label) never go into the code banks $81-$9F (bank 0 addresses)
    pbanks = [{'bank': b, 'base': 0x8000, 'used': used.get(b, 0), 'speed': 'fast', 'long': b > 0x80 + C_BANK_LAST,
               'size': bank_size(b)} for b in range(0x81, 0x80 + ROM_BANKS)]
    pbanks += [{'bank': b, 'base': 0x8000, 'used': used.get(b, 0), 'speed': 'slow', 'size': bank_size(b)}
               for b in SLOW_BANKS]
    txt, rep, where = romplace.place(entries, pbanks)
    open(pp, 'w').write(txt)
    open(os.path.join(ROOT, 'build', 'kishidra.place.txt'), 'w').write(romplace.listing(where))
    run([tool('ca65'), '--cpu', '65816'] + DBG + asm_inc + ['-o', po, pp])
    run([tool('ld65'), '-C', cfg, '-o', OUT, '-m', mp, '--dbgfile', os.path.join(ROOT, 'build', 'kishidra.dbg'),
         '-Ln', os.path.join(ROOT, 'build', 'kishidra.lbl')] + objs + [po, none_lib()])
    print('\n'.join(rep))
    check_layout(cfg)
    finish_rom(OUT)
    usage(mp)
    tags = (['PROFILE'] if os.environ.get('PROFILE') else []) + os.environ.get('XDEFS', '').split()
    print('OK', OUT, os.path.getsize(OUT), 'bytes, %s' % ('LoROM 4MB' if not SLOW_BANKS else 'ExLoROM %sMB' % ROM_MB) +
          (' (test ROM: %s)' % ' '.join(tags) if tags else ''))


def bank_size(bank):
    """bytes of the memory area of a ROM bank (CPU bank byte): $81 and $01 end at $FFB0 (zero area)"""
    return ZERO_AT - 0x8000 if bank in (0x81, 0x01) else 0x8000


def linker_cfg(cbanks):
    """Every ROM memory area carries its bank attribute, so .bank(label) is right in all of them (in
    the code banks $81-$9F the area starts at $8000: cc65 code uses 16-bit jsr, and there ^label is
    0).  TABLES is the first segment of $C0; in every area the placed segment PLxx is the last one.
    The areas are written in file order: ROM00-ROM7F (banks $80-$FF), then for ROM_MB=6 / 8 EXT00-EXTnn
    (banks $00-$nn at file 4 MB+; EXT00 stays empty: finish_rom copies bank $80 into it, and the slow
    banks hold only the placed segments PLnn).  ZERO81 / ZERO01 are the 80 zero bytes at $FFB0."""
    mem = ['MEMORY {',
           '  ZP: start=$0000, size=$80, type=rw;',
           '  ENGZP: start=$80, size=$80, type=rw;',
           '  LOWRAM: start=$0200, size=$1e00, type=rw;',
           '  CRAM: start=$2000, size=$d700, type=rw, bank=$7e;',
           '  ROM00: start=$808000, size=$8000, fill=yes, fillval=$ff, file=%O, bank=$80;',
           '  PLSTUB: start=$0000, size=$10000, file="", bank=$00;']
    seg = ['SEGMENTS {',
           '  ZEROPAGE: load=ZP, type=zp;',
           '  ENGZP: load=ENGZP, type=zp, optional=yes;',
           '  STARTUP: load=ROM00, type=ro;',
           '  ENGINE: load=ROM00, type=ro;',
           '  CODE: load=ROM00, run=LOWRAM, type=rw, define=yes;',
           '  STUBS: load=ROM00, run=LOWRAM, type=rw, define=yes;',
           '  LOWBSS: load=LOWRAM, type=bss, define=yes;',
           '  RODATA: load=ROM00, run=CRAM, type=rw, define=yes;',
           '  DATA: load=ROM00, run=CRAM, type=rw, define=yes;',
           '  BSS: load=CRAM, type=bss, define=yes;',
           '  HEADER: load=ROM00, start=$80ffb0, type=ro;',
           '  VECTORS: load=ROM00, start=$80ffe0, type=ro;',
           '  PLSTUB: load=PLSTUB, type=ro, optional=yes;']
    for b in range(1, ROM_BANKS):
        bank = 0x80 + b
        if 1 <= b <= C_BANK_LAST:
            mem.append('  ROM%02X: start=$8000, size=$%x, fill=yes, fillval=$ff, file=%%O, bank=$%02x;' % (b, bank_size(bank), bank))
            seg.append('  CODE%d: load=ROM%02X, type=ro, optional=yes;' % (b, b))
            seg.append('  ENG%d: load=ROM%02X, type=ro, optional=yes;' % (b, b))
        else:
            mem.append('  ROM%02X: start=$%02x8000, size=$%x, fill=yes, fillval=$ff, file=%%O, bank=$%02x;' % (b, bank, bank_size(bank), bank))
        if bank_size(bank) < 0x8000:
            mem.append('  ZERO%02X: start=$%04x, size=$%x, fill=yes, fillval=$00, file=%%O, bank=$%02x;' % (
                bank, ZERO_AT if b <= C_BANK_LAST else (bank << 16) | ZERO_AT, 0x10000 - ZERO_AT, bank))
        if bank == TABLE_BANK:
            seg.append('  TABLES: load=ROM%02X, type=ro;' % b)
        seg.append('  BANK%02X: load=ROM%02X, type=ro, optional=yes;' % (bank, b))
        seg.append('  PL%02X: load=ROM%02X, type=ro, optional=yes;' % (bank, b))
    for xb in ([0x00] + SLOW_BANKS) if SLOW_BANKS else []:
        mem.append('  EXT%02X: start=$%06x, size=$%x, fill=yes, fillval=$ff, file=%%O, bank=$%02x;' % (
            xb, (xb << 16) | 0x8000, bank_size(xb), xb))
        if bank_size(xb) < 0x8000:
            mem.append('  ZERO%02X: start=$%06x, size=$%x, fill=yes, fillval=$00, file=%%O, bank=$%02x;' % (
                xb, (xb << 16) | ZERO_AT, 0x10000 - ZERO_AT, xb))
        if xb:
            seg.append('  PL%02X: load=EXT%02X, type=ro, optional=yes;' % (xb, xb))
    mem.append('}')
    seg.append('}')
    return '\n'.join(mem + seg) + '\n'


def area_used(mp, cfg):
    """bytes used in each ROM bank (CPU bank byte: $81-$FF, and $01-$7D for ROM_MB=6 / 8) by the segments
    of a link (from its map)"""
    txt = open(mp).read()
    m = re.search(r'Segment list:.*?\n\n', txt, re.S)
    seg2mem = dict(re.findall(r'^\s+(\w+): load=(\w+)', open(cfg).read(), re.M))
    used = {}
    for line in m.group(0).splitlines():
        p = line.split()
        if len(p) >= 4 and re.match(r'^[0-9A-F]{6}$', p[1]):
            area = re.match(r'^(ROM|EXT)([0-9A-F]{2})$', seg2mem.get(p[0], ''))
            if not area or area.group(0) in ('ROM00', 'EXT00'):
                continue
            bank = int(area.group(2), 16) + (0x80 if area.group(1) == 'ROM' else 0)
            used[bank] = used.get(bank, 0) + int(p[3], 16)
    return used


# a 24-bit read (lda f:label) or ^label of a label in one of these segments gets bank 0
# (their memory areas start at $8000): such data must be in TABLES, a fixed data segment or
# a placed blob marked "long" (README / tools/romplace.py)
BANK0_SEGS = re.compile(r'^(CODE\d+|ENG\d+|BANK[89][0-9A-F]|PL[89][0-9A-F]|PLSTUB)$')


def check_far_reads(asm_files, entries):
    """Build-time check of the f: / ^ rule: every label read as 'f:label' or used as '^label' in the
    assembler sources (src/asm, data/gen, the cc65 output) must be a WRAM / ROM00 label, a constant,
    a label of TABLES or of a fixed segment in $A0-$FF, or a placed blob marked long ('^' never for
    placed data: use <.bank())."""
    placed = {e['label']: e for e in entries}
    seg_of, consts = {}, set()
    for f in asm_files:
        cur = 'CODE'
        for line in open(f, encoding='utf-8', errors='replace'):
            line = line.split(';')[0]
            m = re.match(r'\s*\.segment\s+"(\w+)"', line)
            if m:
                cur = m.group(1)
                continue
            m = re.match(r'\s*([A-Za-z_]\w*)\s*:?=', line)
            if m:
                consts.add(m.group(1))
                continue
            m = re.match(r'\s*([A-Za-z_]\w*):', line)
            if m:
                seg_of.setdefault(m.group(1), cur)
    bad = []
    for f in asm_files:
        params = set()
        for k, line in enumerate(open(f, encoding='utf-8', errors='replace'), 1):
            code = line.split(';')[0]
            m = re.match(r'\s*\.macro\s+\w+\s*(.*)', code)
            if m:
                params = set(re.findall(r'\w+', m.group(1)))
                continue
            if re.match(r'\s*\.endmacro', code):
                params = set()
            for kind, name in re.findall(r'(?<![\w@.])(f:|\^)\s*([A-Za-z_]\w*)', code):
                if name in consts or name in params or name.startswith('__'):
                    continue            # constants, macro arguments, linker symbols (__X_LOAD__)
                where = '%s:%d: %s%s' % (os.path.relpath(f, ROOT), k, kind, name)
                if name in placed:
                    if kind == '^' or not placed[name]['long']:
                        bad.append('%s is placed data (%s): %s' % (
                            where, placed[name]['_from'],
                            'use <.bank(%s)' % name if kind == '^' else 'mark it "long" in the manifest'))
                    continue
                seg = seg_of.get(name)
                if seg is None:
                    bad.append('%s: label not found' % where)
                elif BANK0_SEGS.match(seg):
                    bad.append('%s is in segment %s (bank 0 address): put it in TABLES or a long blob' % (where, seg))
    if bad:
        sys.exit('f: / ^ の規則（build.py check_far_reads）:\n  ' + '\n  '.join(bad))


def check_layout(cfg):
    """the slow banks hold placed data only: no segment but PLnn loads into EXTnn, none into EXT00 (the
    copy of bank $80); code, the engine and the fixed segments stay in the FastROM areas ROMxx"""
    bad = []
    for name, mem in re.findall(r'^\s+(\w+): load=(\w+)', open(cfg).read(), re.M):
        if (mem.startswith('EXT') and (mem == 'EXT00' or name != 'PL' + mem[3:])) or (
                re.match(r'^(CODE\d*|ENG\d+|STARTUP|ENGINE|STUBS|RODATA|DATA|HEADER|VECTORS|TABLES|BANK[0-9A-F]{2})$', name)
                and not re.match(r'^ROM[0-9A-F]{2}$', mem)):
            bad.append('%s in %s' % (name, mem))
    if bad:
        sys.exit('build.py: $00-$7D (SlowROM) holds only placed data: ' + ', '.join(bad))


def mirror_sum(d, start, length, mask=0x800000):
    """SNES checksum (snes9x Checksum_Calculate / NSRT): a size that is not a power of two is summed as
    if its tail were mirrored up to the next power of two (6 MB = sum(0-4 MB) + 2 * sum(4-6 MB))"""
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


def finish_rom(path):
    """pad to ROM_MB; ExLoROM: header byte $FFD7 = $0D (8 MB class, also for 6 MB), bank $00 = a copy of
    bank $80 (entry code, header, vectors at file 4 MB); the checksum (mirror sum) in every header copy;
    then the checks of tools/harness/romcheck.py (zero areas, copy, header, checksum, the layout each
    emulator / flash cart detector picks)"""
    d = bytearray(open(path, 'rb').read())
    if len(d) > ROM_SIZE:
        sys.exit('build.py: the link wrote %d bytes, more than ROM_MB=%s' % (len(d), ROM_MB))
    d += b'\xff' * (ROM_SIZE - len(d))
    hdr = 0x7fc0
    if d[ZERO_AT:0x10000] != bytes(0x10000 - ZERO_AT):
        sys.exit('build.py: $81:FFB0-FFFF must be zero')
    heads = [hdr]
    if SLOW_BANKS:
        if d[0x400000:0x408000] != b'\xff' * 0x8000:
            sys.exit('build.py: bank $00 must stay empty (it becomes the copy of bank $80)')
        if d[0x400000 + ZERO_AT:0x410000] != bytes(0x10000 - ZERO_AT):
            sys.exit('build.py: $01:FFB0-FFFF must be zero')
        d[hdr + 0x17] = 0x0d                      # $FFD7: 8 MB class (header.s has $0C for 4 MB)
        d[0x400000:0x408000] = d[0:0x8000]        # bank $00 = bank $80
        heads.append(0x400000 + hdr)
    for h in heads:
        d[h + 0x1c:h + 0x20] = b'\xff\xff\x00\x00'   # these 4 bytes sum to $1FE whatever the checksum is
    s = mirror_sum(d, 0, len(d)) & 0xffff
    for h in heads:
        d[h + 0x1c] = (s ^ 0xffff) & 0xff
        d[h + 0x1d] = (s ^ 0xffff) >> 8
        d[h + 0x1e] = s & 0xff
        d[h + 0x1f] = s >> 8
    open(path, 'wb').write(d)
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'harness'))
    import romcheck
    bad = romcheck.problems(bytes(d))
    if bad:
        sys.exit('build.py: the ROM fails tools/harness/romcheck.py:\n  ' + '\n  '.join(bad))


def usage(mp):
    txt = open(mp).read()
    m = re.search(r'Segment list:.*?\n\n', txt, re.S)
    rows = []
    for line in (m.group(0) if m else '').splitlines():
        p = line.split()
        if len(p) >= 4 and re.match(r'^[0-9A-F]{6}$', p[1] or ''):
            rows.append((p[0], int(p[3], 16)))
    per = {}
    for name, size in rows:
        per[name] = per.get(name, 0) + size
    used = sum(v for k, v in per.items() if k not in ('BSS', 'LOWBSS', 'ZEROPAGE', 'ENGZP'))
    big = sorted(per.items(), key=lambda kv: -kv[1])[:8]
    print('ROM used %.1f KB / %d KB;' % (used / 1024, ROM_SIZE // 1024), ', '.join('%s %.1fK' % (k, v / 1024) for k, v in big))
    bss = per.get('BSS', 0) + per.get('RODATA', 0) + per.get('DATA', 0)
    print('WRAM $7E C data %.1f KB / 54 KB, low WRAM %.1f KB / 7.5 KB' %
          (bss / 1024, (per.get('CODE', 0) + per.get('STUBS', 0) + per.get('LOWBSS', 0)) / 1024))


def build_data():
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import make_data
    make_data.main()


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'all'
    if what == 'clean':
        shutil.rmtree(os.path.join(ROOT, 'build'), ignore_errors=True)
    elif what == 'data':
        build_data()
    elif what == 'audio':
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'make_audio.py')], check=True)
    elif what == 'rom':
        build_rom()
    else:
        if not os.path.exists(os.path.join(GEN, 'stamp.txt')):
            build_data()
        if not os.path.exists(os.path.join(GEN, 'audio.s')):
            subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'make_audio.py')], check=True)
        build_rom()
