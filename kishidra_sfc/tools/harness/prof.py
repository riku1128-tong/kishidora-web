#!/usr/bin/env python3
"""Where does the CPU spend its time?  Needs a profiling ROM:  PROFILE=1 python build.py rom
(a V-IRQ every 16 scanlines stores the interrupted PC in a ring at $7F:F000; see crt0.s).

usage: prof.py SCRIPT FRAMES [TOP]
  SCRIPT  runrom commands that lead to the moment to measure (may be empty)
  FRAMES  how many frames to sample after it (about 14 samples per frame)
Prints the functions with the most samples (static C functions and asm labels included)."""
import bisect
import collections
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
CORE = os.environ.get('CORE', '/usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so')


def symbols():
    """region (bank, 'low' for low WRAM code) -> sorted [(addr, function)]; local labels count
    for their function (C scopes, ca65 @cheap labels -> the label before them)"""
    segs, syms, scopes, mods = {}, {}, {}, {}
    for line in open(os.path.join(ROOT, 'build', 'kishidra.dbg')):
        kind, _, rest = line.rstrip('\n').partition('\t')
        if kind not in ('seg', 'sym', 'scope', 'mod'):
            continue
        f = dict(kv.split('=', 1) for kv in rest.split(','))
        if kind == 'seg':
            segs[f['id']] = (f['name'].strip('"'), int(f['start'], 16))
        elif kind == 'sym':
            syms[f['id']] = f
        elif kind == 'scope':
            scopes[f['id']] = f
        else:
            mods[f['id']] = os.path.basename(f['name'].strip('"'))

    def owner(f):
        if 'parent' in f and f['parent'] in syms:
            return owner(syms[f['parent']])
        sc = scopes.get(f.get('scope', ''), {})
        if sc.get('name', '""') != '""':
            return sc['name'].strip('"')
        return f['name'].strip('"') + ' (%s)' % mods.get(sc.get('mod', ''), '?')
    table = collections.defaultdict(list)
    for f in syms.values():
        if f.get('type') != 'lab' or 'seg' not in f or 'val' not in f:
            continue
        sname, start = segs[f['seg']]
        val = int(f['val'], 16)
        m = re.match(r'CODE(\d+)$', sname)
        if m:
            key = 0x80 + int(m.group(1))
        elif sname in ('CODE', 'STUBS'):
            key = 'low'
        elif sname in ('STARTUP', 'ENGINE') or sname.startswith('ENG'):
            key = (val >> 16) | 0x80
        else:
            continue             # data labels
        table[key].append((val & 0xffff, owner(f)))
    # low WRAM code without symbols (the cc65 runtime library): module names from the map
    mp = open(os.path.join(ROOT, 'build', 'kishidra.map')).read()
    code = re.search(r'^CODE\s+([0-9A-F]{6})', mp, re.M)
    if code:
        base = int(code.group(1), 16)
        cur = None
        for line in mp.split('Segment list')[0].splitlines():
            if line and not line.startswith(' '):
                cur = re.sub(r'.*\((.*)\)$', r'\1', line.rstrip(':'))
                continue
            m2 = re.match(r'\s+CODE\s+Offs=([0-9A-F]+)', line)
            if m2 and cur and not cur.startswith('a_crt0'):
                table['low'].append((base + int(m2.group(1), 16), 'runtime ' + cur))
    for k in table:
        table[k].sort()
    return table


def lookup(table, pb, pc):
    key = 'low' if pc < 0x2000 else (pb | 0x80)
    lst = table.get(key, [])
    i = bisect.bisect_right(lst, (pc, '￿')) - 1
    if i < 0:
        return 'bank%02x:%04x' % (pb, pc)
    return lst[i][1]


def main():
    script, frames = sys.argv[1], int(sys.argv[2])
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    td = tempfile.mkdtemp()
    pre = open(script).read() if script and os.path.exists(script) else ''
    chunks = (frames + 59) // 60
    lines = [pre.rstrip('\n'), 'wram %s/w0.bin' % td]
    for i in range(chunks):
        lines += ['wait 60', 'wram %s/w%d.bin' % (td, i + 1)]
    sc = os.path.join(td, 's.txt')
    open(sc, 'w').write('\n'.join(lines) + '\n')
    subprocess.run([os.path.join(ROOT, 'tools', 'harness', 'runrom'), CORE, os.path.join(ROOT, 'build', 'kishidra.sfc'), sc],
                   check=True, capture_output=True)
    table = symbols()
    count = collections.Counter()
    total = 0
    prev = None
    for i in range(chunks + 1):
        d = open('%s/w%d.bin' % (td, i), 'rb').read()
        idx = (d[0x1dbfc] | d[0x1dbfd] << 8) & 0x0ffc
        if prev is not None:
            j = prev
            while j != idx:
                pc = d[0x1f000 + j] | d[0x1f001 + j] << 8
                pb = d[0x1f002 + j]
                count[lookup(table, pb, pc)] += 1
                total += 1
                j = (j + 4) & 0x0ffc
        prev = idx
    print('%d samples' % total)
    for name, n in count.most_common(top):
        print('%5.1f%%  %s' % (100.0 * n / max(1, total), name))


main()
