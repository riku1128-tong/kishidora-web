#!/usr/bin/env python3
"""Lag over a window: run SCRIPT (it leads to the moment to measure), then FRAMES idle frames (or the
runrom commands of --window FILE), and compare two WRAM dumps taken at the start and the end.

usage: [ROM=x.sfc] lag.py [-o OUTDIR] SCRIPT FRAMES
       [ROM=x.sfc] lag.py [-o OUTDIR] SCRIPT --window FILE

Prints
  frames      NMIs in the window (_frame)
  lag frames  NMIs that found no finished frame (_lag, crt0.s): the main loop ran late; any screen
  loops       PROFILE ROM, battle: battle-loop iterations (prof_marks[6]) -> lag = 1 - loops / frames
  nmi_vmax    PROFILE ROM: the latest scanline an NMI's uploads ended on since power-on ($7F:DBFA;
              262 + n = n lines into the next frame)
SCRIPT may use '#!mksave' (hrun.py).  The first / last screenshots and dumps are kept in OUTDIR
(default: a temporary folder, removed)."""
import argparse
import os
import re
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hrun  # noqa: E402

NMI_VMAX = 0x1dbfa             # $7F:DBFA, crt0.s (PROFILE)


def lag_addr(syms):
    """_lag: in the .lbl; a map lists only symbols another module imports, and crt0.s keeps _lag
    right after _frame (checked here)"""
    if '_lag' in syms:
        return hrun.wram_off(syms['_lag'])
    crt0 = open(os.path.join(hrun.ROOT, 'src', 'asm', 'crt0.s')).read()
    if not re.search(r'^_frame:\s+\.res 2[^\n]*\n_lag:', crt0, re.M):
        sys.exit('no _lag symbol and crt0.s no longer has _lag right after _frame')
    return hrun.addr(syms, 'frame') + 2


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('script')
    ap.add_argument('frames', nargs='?', type=int)
    ap.add_argument('--window')
    ap.add_argument('-o', '--out')
    a = ap.parse_args()
    if (a.frames is None) == (a.window is None):
        sys.exit('give FRAMES or --window FILE')
    rom = hrun.ROM
    syms = hrun.symbols(rom)
    fr, lg = hrun.addr(syms, 'frame'), lag_addr(syms)
    pm = hrun.addr(syms, 'prof_marks') if '_prof_marks' in syms else None
    td = os.path.abspath(a.out) if a.out else tempfile.mkdtemp(prefix='lag_')
    os.makedirs(td, exist_ok=True)
    win = 'wait %d\n' % a.frames if a.window is None else open(a.window).read()
    text = hrun.prepare(a.script, td) + ('wram %s/l0.bin\nshot %s/l0.ppm\n%swram %s/l1.bin\nshot %s/l1.ppm\n'
                                         % (td, td, win.rstrip('\n') + '\n', td, td))
    r = hrun.run(rom, text, td)
    if r.returncode:
        sys.exit('runrom failed (%d): %s' % (r.returncode, (r.stdout + r.stderr).strip()))
    d0, d1 = open(td + '/l0.bin', 'rb').read(), open(td + '/l1.bin', 'rb').read()
    nf = (hrun.u16(d1, fr) - hrun.u16(d0, fr)) & 0xffff
    nl = (hrun.u16(d1, lg) - hrun.u16(d0, lg)) & 0xffff
    out = 'frames %d, lag frames %d (%.1f%%)' % (nf, nl, 100.0 * nl / max(nf, 1))
    if pm is not None:
        loops = (hrun.u16(d1, pm + 12) - hrun.u16(d0, pm + 12)) & 0xffff
        if loops:
            out += ', battle loops %d: lag %.1f%%' % (loops, 100.0 * (1 - loops / max(nf, 1)))
        out += ', nmi_vmax line %d' % hrun.u16(d1, NMI_VMAX)
    print(out)
    if not a.out:
        shutil.rmtree(td, ignore_errors=True)


if __name__ == '__main__':
    main()
