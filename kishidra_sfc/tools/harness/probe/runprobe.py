#!/usr/bin/env python3
"""Run the probe ROMs of mkprobe.py on libretro cores and check each verdict.

usage: runprobe.py [DIR] [CORE.so ...]
  DIR    the probe folder (default build/probe; made with mkprobe.py when it has no meta.json)
  CORE   libretro cores (default: the snes9x and bsnes-mercury cores installed in
         /usr/lib/x86_64-linux-gnu/libretro)

Per probe and core: PASS / FAIL (every block read and SRAM byte as expected, WRAM $7E:01FE = $A5), the
on-screen colour (GREEN / RED) and the core's map log line.  Known results (docs/plan/rom_compat.md §2):
snes9x passes all four probes; bsnes-mercury fails the ExLoROM ones (it maps $80 to file 4 MB and leaves
$C0-$FF open) and passes the ExHiROM ones.  Exit status 0 when every result of a known core is the
known one (other cores are only reported)."""
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import hrun  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
CORES = '/usr/lib/x86_64-linux-gnu/libretro'
KNOWN = {'snes9x': {'exlorom6': True, 'exlorom8': True, 'exhirom6': True, 'exhirom8': True},
         'bsnes_mercury_accuracy': {'exlorom6': False, 'exlorom8': False, 'exhirom6': True, 'exhirom8': True}}


def run_probe(core, rom, wd, m):
    """(passed, text lines)"""
    os.makedirs(wd, exist_ok=True)
    sp = os.path.join(wd, 's.txt')
    open(sp, 'w').write('wait 30\nshot %s/screen.ppm\nwram %s/wram.bin\nsram %s/sram.bin\n' % (wd, wd, wd))
    r = subprocess.run([hrun.RUNROM, core, rom, sp], capture_output=True, text=True, timeout=300)
    log = (r.stdout + r.stderr).strip().splitlines()
    try:
        w = open(os.path.join(wd, 'wram.bin'), 'rb').read()
    except OSError:
        return False, ['no WRAM dump: ' + ' / '.join(log[-3:])]
    try:
        s = open(os.path.join(wd, 'sram.bin'), 'rb').read()
    except OSError:
        s = b''
    scr = open(os.path.join(wd, 'screen.ppm'), 'rb').read()[-3:]
    ok_all = w[0x1FE] == 0xA5
    rows = []
    for i, (a, e) in enumerate(zip(m['reads'], m['expect_blocks'])):
        g = struct.unpack_from('<H', w, 2 * i)[0]
        ok_all &= e == g
        rows.append('%s->%d%s' % (a, g, '' if e == g else '(want %d)' % e))
    sram = []
    for i, (k, v) in enumerate(m['sram'].items()):
        k = int(k)
        rb, infile = w[0x100 + i], (s[k] if len(s) > k else None)
        good = rb == v and infile == v
        ok_all &= good
        sram.append('sram[%04X] read %02X file %s%s' % (k, rb, '%02X' % infile if infile is not None else '--',
                                                       '' if good else ' BAD'))
    colour = 'GREEN' if scr[1] > 128 and scr[0] < 64 else ('RED' if scr[0] > 128 else '?')
    maps = ' '.join(x for x in log if 'Map' in x)
    return ok_all, ['%s | screen %s (flag %02X, last pixel %s) %s' % ('PASS' if ok_all else 'FAIL', colour, w[0x1FE],
                                                                       scr.hex(), maps),
                    '    ' + ' '.join(rows), '    ' + '; '.join(sram)]


def main():
    args = sys.argv[1:]
    d = args[0] if args and not args[0].endswith('.so') else os.path.join(ROOT, 'build', 'probe')
    cores = [a for a in args if a.endswith('.so')] or [os.path.join(CORES, c + '_libretro.so') for c in KNOWN]
    if not os.path.exists(os.path.join(d, 'meta.json')):
        subprocess.run([sys.executable, os.path.join(HERE, 'mkprobe.py'), d], check=True)
    hrun.ensure_runrom()
    meta = json.load(open(os.path.join(d, 'meta.json')))
    good = True
    for core in cores:
        if not os.path.exists(core):
            print('%s: not installed' % core)
            continue
        cn = os.path.basename(core).replace('_libretro.so', '')
        for name, m in meta.items():
            ok, lines = run_probe(core, os.path.abspath(os.path.join(d, name + '.sfc')),
                                  os.path.join(d, 'run', cn + '_' + name), m)
            want = KNOWN.get(cn, {}).get(name)
            note = '' if want is None else ('  (as known)' if ok == want else '  ** expected %s **' % (
                'PASS' if want else 'FAIL'))
            good &= want is None or ok == want
            print('%-24s %-9s %s%s' % (cn, name, lines[0], note))
            for x in lines[1:]:
                print(x)
    print('probes: ' + ('all results as known' if good else 'a result differs from the known one'))
    sys.exit(0 if good else 1)


if __name__ == '__main__':
    main()
