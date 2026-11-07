#!/usr/bin/env python3
"""キシドラ！ SFC 版のビルド。

    python build.py            データの作り直し（必要なときだけ）+ ROM を作る
    python build.py rom        ROM だけ作る（data/gen にあるデータを使う）
    python build.py data       data/gen のデータだけ作り直す（Safari 版のファイルから）
    python build.py clean

必要なもの: Python 3.9+、cc65 (cc65 / ca65 / ld65)。データの作り直しには Pillow と
zstandard（pip install pillow zstandard）、BGM の変換には ffmpeg も使う。

出力: build/kishidra.sfc（LoROM / FastROM / 4MB / SRAM 32KB）
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


def build_rom():
    os.makedirs(OBJ, exist_ok=True)
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
    inc = ['-I', os.path.join(SRC, 'c'), '-I', GEN]
    for c in cfiles:
        b = banks[c]
        base = os.path.splitext(os.path.basename(c))[0]
        s = os.path.join(OBJ, base + '.s')
        o = os.path.join(OBJ, base + '.o')
        defs = ['-D', 'PROFILE=1'] if os.environ.get('PROFILE') else []
        for f in bank_funcs[b]:
            if f != 'main':
                defs += ['-D', '%s=%s__b' % (f, f)]
        run([tool('cc65'), '-O', '-Oirs', '-Cl', '-t', 'none', '--cpu', '65sc02', '--code-name', 'CODE%d' % b] + DBG + [
             '--rodata-name', 'RODATA', '--bss-name', 'BSS', '--data-name', 'DATA', '-o', s] + inc + defs + [c])
        run([tool('ca65'), '--cpu', '65816'] + DBG + ['-o', o, s])
        objs.append(o)
    # trampolines: _f (low WRAM) -> jsl f__t (bank) -> jsr _f__b -> rtl
    tr = ['.p816', '.smart -']
    for b in sorted(bank_funcs):
        for f in bank_funcs[b]:
            if f == 'main':
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
    for s in sfiles:
        o = os.path.join(OBJ, 'a_' + os.path.splitext(os.path.basename(s))[0] + '.o')
        run([tool('ca65'), '--cpu', '65816'] + DBG + asm_inc + ['-o', o, s])
        objs.append(o)
    cfg = os.path.join(OBJ, 'kishidra.cfg')
    open(cfg, 'w').write(linker_cfg(sorted(set(banks.values()))))
    mp = os.path.join(ROOT, 'build', 'kishidra.map')
    run([tool('ld65'), '-C', cfg, '-o', OUT, '-m', mp, '--dbgfile', os.path.join(ROOT, 'build', 'kishidra.dbg')] + objs + [none_lib()])
    fix_checksum(OUT)
    usage(mp)
    print('OK', OUT, os.path.getsize(OUT), 'bytes')


def linker_cfg(cbanks):
    mem = ['MEMORY {',
           '  ZP: start=$0000, size=$80, type=rw;',
           '  ENGZP: start=$80, size=$80, type=rw;',
           '  LOWRAM: start=$0200, size=$1e00, type=rw;',
           '  CRAM: start=$2000, size=$d700, type=rw, bank=$7e;',
           '  ROM00: start=$808000, size=$8000, fill=yes, fillval=$ff, file=%O;']
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
           '  VECTORS: load=ROM00, start=$80ffe0, type=ro;']
    for b in range(1, ROM_BANKS):
        bank = 0x80 + b
        if 1 <= b <= C_BANK_LAST:
            mem.append('  ROM%02X: start=$8000, size=$8000, fill=yes, fillval=$ff, file=%%O, bank=$%02x;' % (b, bank))
            seg.append('  CODE%d: load=ROM%02X, type=ro, optional=yes;' % (b, b))
            seg.append('  ENG%d: load=ROM%02X, type=ro, optional=yes;' % (b, b))
        else:
            mem.append('  ROM%02X: start=$%02x8000, size=$8000, fill=yes, fillval=$ff, file=%%O;' % (b, bank))
        seg.append('  BANK%02X: load=ROM%02X, type=ro, optional=yes;' % (bank, b))
    mem.append('}')
    seg.append('}')
    return '\n'.join(mem + seg) + '\n'


def fix_checksum(path):
    d = bytearray(open(path, 'rb').read())
    hdr = 0x7fc0
    d[hdr + 0x1c:hdr + 0x20] = b'\xff\xff\x00\x00'
    s = sum(d) & 0xffff
    d[hdr + 0x1c] = (s ^ 0xffff) & 0xff
    d[hdr + 0x1d] = (s ^ 0xffff) >> 8
    d[hdr + 0x1e] = s & 0xff
    d[hdr + 0x1f] = s >> 8
    open(path, 'wb').write(d)


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
    print('ROM used %.1f KB / 4096 KB;' % (used / 1024), ', '.join('%s %.1fK' % (k, v / 1024) for k, v in big))
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
