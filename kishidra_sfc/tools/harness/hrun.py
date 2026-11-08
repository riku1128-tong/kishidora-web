#!/usr/bin/env python3
"""Shared helpers of the harness tools (abtest.py, lag.py, oamlines.py): run a runrom script on a ROM,
read the link's symbols, turn SNES addresses into WRAM dump offsets.

Script conventions on top of runrom's commands (runrom itself reads these lines as comments):
  #!mksave NAME.srm ARGS...   make the save with tools/harness/mksave.py before the run; a later
                              'loadsram NAME.srm' loads it (so no .srm file has to be kept)
  #!needs SYMBOL ...          abtest.py skips the script on a ROM without these symbols (dbg_cmd:
                              an XDEFS=TEST_HOOKS ROM)
Relative paths in 'loadsram' and 'map' are relative to the script's folder.

    python3 hrun.py SCRIPT DIR    write DIR/<script> with the saves made and the paths resolved (for
                                  tools that take a plain runrom script: textcheck.py, prof.py)"""
import os
import re
import shlex
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
RUNROM = os.environ.get('RUNROM', os.path.join(HERE, 'runrom'))
CORE = os.environ.get('CORE', '/usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so')
ROM = os.environ.get('ROM', os.path.join(ROOT, 'build', 'kishidra.sfc'))
OUTPUTS = ('shot', 'wav', 'wram', 'sram', 'vram')


def sym_file(rom, path=None):
    """the symbol file runrom uses for ROM: PATH, else ROM's .lbl, else its .map"""
    if path:
        return path
    stem = os.path.splitext(rom)[0]
    for ext in ('.lbl', '.map'):
        if os.path.exists(stem + ext):
            return stem + ext
    sys.exit('no %s.lbl / .map next to the ROM' % stem)


def symbols(rom, path=None):
    """name -> 24-bit value, from an ld65 VICE label file (all exports) or map (exports used by
    another module); both build.py outputs"""
    out = {}
    for line in open(sym_file(rom, path)):
        p = line.split()
        if len(p) == 3 and p[0] == 'al' and re.match(r'^[0-9A-F]{6}$', p[1]):
            out.setdefault(p[2].lstrip('.'), int(p[1], 16))
            continue
        for i in range(len(p) - 2):
            if re.match(r'^[0-9A-F]{6}$', p[i + 1]) and re.match(r'^[A-Z]{2,4}$', p[i + 2]):
                out.setdefault(p[i], int(p[i + 1], 16))
    return out


def wram_off(v):
    """offset in a 128 KB WRAM dump of a link value / SNES address (as runrom: a value below $10000
    is low WRAM under $2000, else bank $7E, the C data)"""
    if 0x7e0000 <= v <= 0x7fffff:
        return v - 0x7e0000
    if v < 0x10000:
        return v
    if ((v >> 16) < 0x40 or 0x80 <= (v >> 16) < 0xc0) and (v & 0xffff) < 0x2000:
        return v & 0x1fff
    raise ValueError('%06x is not in WRAM' % v)


def addr(syms, name):
    """WRAM offset of a symbol (with or without the leading _ of C names)"""
    for n in (name, '_' + name):
        if n in syms:
            return wram_off(syms[n])
    raise KeyError('symbol %s not in the link (rebuild the ROM?)' % name)


def u16(d, off):
    return d[off] | d[off + 1] << 8


def prepare(script, workdir, outdir=None, text=None):
    """runrom script text for SCRIPT run from WORKDIR: '#!mksave' saves made in WORKDIR, relative
    'loadsram' / 'map' paths resolved, output files (OUT/name or a relative path) put in WORKDIR,
    or with OUTDIR every output file in OUTDIR as NNN_name (NNN = the script line, so a name
    written twice stays two files)"""
    sdir = os.path.dirname(os.path.abspath(script))
    lines = (open(script).read() if text is None else text).splitlines()
    saves, out = {}, []
    for n, line in enumerate(lines, 1):
        p = line.split()
        if not p:
            continue
        if p[0] == '#!mksave':
            a = shlex.split(line)[1:]
            dst = os.path.join(workdir, os.path.basename(a[0]))
            r = subprocess.run([sys.executable, os.path.join(HERE, 'mksave.py'), dst] + a[1:], capture_output=True, text=True)
            if r.returncode:
                sys.exit('%s:%d: mksave failed\n%s' % (script, n, r.stdout + r.stderr))
            saves[a[0]] = dst
            continue
        if p[0].startswith('#'):
            continue
        if p[0] in ('loadsram', 'map') and len(p) > 1:
            f = saves.get(p[1]) or (p[1] if os.path.isabs(p[1]) else os.path.join(sdir, p[1]))
            out.append('%s %s' % (p[0], f))
        elif p[0] in OUTPUTS and len(p) > 1:
            f = p[1][4:] if p[1].startswith('OUT/') else p[1]
            if outdir:
                f = os.path.join(outdir, '%03d_%s' % (n, os.path.basename(f)))
            elif not os.path.isabs(f):
                f = os.path.join(workdir, f)
            os.makedirs(os.path.dirname(f), exist_ok=True)
            out.append(' '.join([p[0], f] + p[2:]))
        else:
            out.append(line)
    return '\n'.join(out) + '\n'


_build_lock = threading.Lock()


def ensure_runrom():
    """runrom is built locally (.gitignore): (re)build it when runrom.c is newer"""
    src = os.path.join(HERE, 'runrom.c')
    if RUNROM != os.path.join(HERE, 'runrom') or not os.path.exists(src):
        return
    with _build_lock:
        if not os.path.exists(RUNROM) or os.path.getmtime(RUNROM) < os.path.getmtime(src):
            subprocess.run(['gcc', '-O2', '-I/usr/include/libretro-common', '-o', RUNROM, src, '-ldl'],
                           check=True, capture_output=True)


def run(rom, text, workdir, timeout=1800):
    """run runrom script TEXT on ROM (in WORKDIR); returns the CompletedProcess"""
    ensure_runrom()
    os.makedirs(workdir, exist_ok=True)
    sp = os.path.join(workdir, '_script.txt')
    open(sp, 'w').write(text)
    return subprocess.run([RUNROM, CORE, os.path.abspath(rom), sp], capture_output=True, text=True,
                          cwd=workdir, timeout=timeout)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    wd = os.path.abspath(sys.argv[2])
    os.makedirs(wd, exist_ok=True)
    dst = os.path.join(wd, os.path.basename(sys.argv[1]))
    open(dst, 'w').write(prepare(sys.argv[1], wd))
    print(dst)
