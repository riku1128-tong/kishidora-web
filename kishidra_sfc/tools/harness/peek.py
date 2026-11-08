#!/usr/bin/env python3
"""Run the ROM N frames (optionally with a script) and print WRAM symbols from the ld65 map.
usage: peek.py frames sym[:len] ...   (sym may be a hex address like 7e2000)"""
import os, re, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hrun  # noqa: E402
ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
CORE = os.environ.get('CORE', '/usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so')
def syms():
    m = open(os.path.join(ROOT, 'build', 'kishidra.map')).read()
    out = {}
    for name, addr in re.findall(r'(\S+)\s+([0-9A-F]{6}) R[LE]A', m):
        out[name] = int(addr, 16)
    return out
def main():
    frames = sys.argv[1]
    td = tempfile.mkdtemp()
    sc = os.path.join(td, 's.txt')
    w = os.path.join(td, 'w.bin')
    pre = os.environ.get('SCRIPT', '')
    open(sc, 'w').write((open(pre).read() if pre else '') + 'wait %s\nwram %s\n' % (frames, w))
    hrun.ensure_runrom()                          # runrom is not in git: built here when missing / old
    subprocess.run([os.path.join(ROOT, 'tools', 'harness', 'runrom'), CORE, os.path.join(ROOT, 'build', 'kishidra.sfc'), sc], check=True, capture_output=True)
    d = open(w, 'rb').read()
    s = syms()
    for a in sys.argv[2:]:
        n, _, ln = a.partition(':')
        ln = int(ln or 2)
        addr = int(n, 16) if re.match(r'^[0-9a-fA-F]{4,6}$', n) and n not in s else s.get(n, s.get('_' + n))
        if addr is None:
            print(n, '?'); continue
        off = (addr & 0x1ffff) if addr >= 0x7e0000 else (addr & 0xffff) | (0x10000 if (addr >> 16) == 0x7f else 0)
        if addr < 0x2000: off = addr
        print('%-16s %06x %s' % (n, addr, d[off:off + ln].hex()))
main()
