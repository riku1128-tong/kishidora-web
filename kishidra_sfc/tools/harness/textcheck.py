#!/usr/bin/env python3
"""Text check: run runrom scripts on a profiling ROM (PROFILE=1 python build.py rom) and list the text
that leaves its box, sits on a window frame, shares an 8x8 tile with text of another colour, or is
drawn over other text.  (text.s logs glyphs and lines at $7F:E000; see text_check there.)

usage: textcheck.py SCRIPT [SCRIPT ...]      (each script is run from power-on; WRAM snapshots are
                                               taken every 10 frames while it runs; the log ring
                                               holds 512 entries, an overflow is reported)
Prints one line per distinct finding: script, the step (last 'shot' name or press before it), kind,
box, text id name, x, y."""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
CORE = os.environ.get('CORE', '/usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so')
KIND = {1: 'right', 2: 'bottom', 3: 'colour', 4: 'left', 5: 'top', 8: 'overlap'}
PAL = ['white', 'yellow', 'red', 'gray', 'green', 'blue', 'gold', 'pal7']
STEP = 10


def text_names():
    names = {}
    for m in re.finditer(r'#define\s+(\w+)\s+(\d+)', open(os.path.join(ROOT, 'data', 'gen', 'text.h')).read()):
        names.setdefault(int(m.group(2)), m.group(1))
    return names


def bg3map_addr():
    m = re.search(r'^_bg3map\s+([0-9A-F]{6})', open(os.path.join(ROOT, 'build', 'kishidra.map')).read(), re.M)
    return int(m.group(1), 16) & 0xffff


def shared_tiles(d, at):
    """BG3 tiles shown at two places of the screen (two boxes on the same tiles): [(tile, pos1, pos2)]"""
    seen, out = {}, []
    for k in range(32 * 28):
        t = (d[at + 2 * k] | d[at + 2 * k + 1] << 8) & 0x3ff
        if t == 0:
            continue
        if t in seen:
            out.append((t, seen[t], (k % 32, k // 32)))
        else:
            seen[t] = (k % 32, k // 32)
    return out


def expand(script):
    """insert a WRAM dump every STEP frames of waiting; label each dump with the step it belongs to"""
    out, labels = [], []
    step = 'start'
    for line in open(script).read().splitlines():
        p = line.split()
        if p and p[0] == 'shot':
            step = os.path.basename(p[1])
        if p and p[0] in ('press', 'hold'):
            step = ' '.join(p)
        if p and p[0] == 'wait':
            left = int(p[1])
            while left > 0:
                k = min(STEP, left)
                out.append('wait %d' % k)
                out.append('wram @%d' % len(labels))
                labels.append(step)
                left -= k
        else:
            out.append(line)
    return out, labels


def run(script, names):
    td = tempfile.mkdtemp()
    lines, labels = expand(script)
    lines = [l.replace('@', td + '/w') + '.bin' if l.startswith('wram @') else l for l in lines]
    sc = os.path.join(td, 's.txt')
    open(sc, 'w').write('\n'.join(lines) + '\n')
    subprocess.run([os.path.join(ROOT, 'tools', 'harness', 'runrom'), CORE, os.path.join(ROOT, 'build', 'kishidra.sfc'), sc],
                   check=True, capture_output=True)
    seen = {}
    shared = {}
    mapat = bg3map_addr()
    lines_in = {}          # box -> [(x0, x1, y, msg)] drawn since its last clear
    prev = 0

    def name(msg):
        return names.get(msg, '#%d' % msg) if msg != 0xffff else '(codes)'

    def add(key, val):
        if key not in seen:
            seen[key] = val
    for i in range(len(labels)):
        f = '%s/w%d.bin' % (td, i)
        if not os.path.exists(f):
            continue
        d = open(f, 'rb').read()
        for t, p1, p2 in shared_tiles(d, mapat)[:1]:
            shared.setdefault(labels[i], (t, p1, p2))
        idx = d[0x1dbfe] | d[0x1dbff] << 8
        if ((idx - prev) & 0xffff) > 4096:
            print('%s: log overflow at step %s (more than 512 entries in %d frames)' % (os.path.basename(script), labels[i], STEP), file=sys.stderr)
            prev = (idx - 4096) & 0xffff
        j = prev
        while j != idx:
            o = j & 0x0ff8
            e = d[0x1e000 + o:0x1e008 + o]
            kind, box, msg = e[0], e[1], e[2] | e[3] << 8
            x = e[4] | e[5] << 8
            y = e[6]
            j = (j + 8) & 0xffff
            if kind not in KIND and kind not in (6, 7):
                continue                          # not written yet
            if kind == 7:
                lines_in[box] = []
                continue
            if kind == 6:
                x1 = x + e[7]
                for (a0, a1, ay, am) in lines_in.get(box, []):
                    if (a0, a1, ay, am) == (x, x1, y, msg):
                        continue                  # the same line drawn again
                    if x < a1 - 1 and a0 < x1 - 1 and abs(y - ay) < 12:
                        add((8, box, msg, am, y), (labels[i], 'overlap', box, name(msg), x, y, 'over %s (x %d..%d y %d)' % (name(am), a0, a1, ay)))
                        if os.environ.get('TC_DEBUG'):
                            print('overlap', i, labels[i], box, name(msg), x, x1, y, 'old', name(am), a0, a1, ay, file=sys.stderr)
                lines_in.setdefault(box, []).append((x, x1, y, msg))
                continue
            key = (kind, box, msg, x // 8 if kind == 3 else -1, y)
            extra = ''
            if kind == 3:
                extra = '%s over %s' % (PAL[e[7] & 7], PAL[(e[7] >> 4) & 7])
            add(key, (labels[i], KIND.get(kind, kind), box, name(msg), x, y, extra))
        prev = idx
    for step, (t, p1, p2) in shared.items():
        seen[('tiles', step)] = (step, 'tiles', 0, 'BG3 tile %d at %d,%d and %d,%d' % (t, p1[0], p1[1], p2[0], p2[1]), 0, 0, '')
    # one line per (step, kind, box, text, y): the x of the first glyph
    out = {}
    for v in seen.values():
        k = (v[0], v[1], v[2], v[3], v[5], v[6] if v[1] == 'overlap' else '')
        if k not in out or v[4] < out[k][4]:
            out[k] = v
    return list(out.values())


def main():
    names = text_names()
    total = 0
    for sc in sys.argv[1:]:
        res = run(sc, names)
        for step, kind, box, msg, x, y, extra in sorted(res, key=lambda r: (r[0], r[1], r[2], r[3])):
            print('%-10s %-14s %-7s box %-2d %-22s x %3d y %3d %s' % (os.path.basename(sc), step, kind, box, msg, x, y, extra))
        total += len(res)
    print('%d findings' % total)


main()
