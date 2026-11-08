#!/usr/bin/env python3
"""Save data v2 tests (docs/plan/PLAN.md §2, step S5; shared_infra spec §3.6): the v1 -> v2 migration,
the slot rotation, the fallback to the v1 slot, mksave's v2 output, full bags, the title with only a
v1 slot, a new game, and the compile-time guard that keeps Save below the tower record ($3E00).

usage: savetest.py [-o OUTDIR] [--old OLD_ROM] [--no-guard] [ROM]

  ROM        a SAVE_VER 2 ROM (default build/kishidra.sfc; its .lbl / .map next to it)
  --old      a SAVE_VER 1 ROM (built before S5, with its .lbl / .map): it loads the same v1 saves, and
             its sv (the v1 part and the bag), title and map shots must equal the new ROM's; a v2
             save is no save there (its title = the title without a save)
  --no-guard skip the guard check (it compiles src/c/save.c with cc65 and SAVE_SPARE = 64 / 6636 /
             6637 / 7000: sizeof(Save) 9300 / 15872 compile, 15873 / 16236 must fail)
  -o OUTDIR  keep the saves, scripts, dumps and shots there (default: a temporary folder, removed
             when every check passed)

The saves are made with mksave.py (--v1 for the old layout).  The game is driven like a player:
title A (つづきから), map A, A, A = STAGE 1 of the cursor area -> the battle start writes the save
(map.c), so the SRAM dump after it shows which slot was written; a full bag cannot sortie, so its
write is the menu's タイトルへ (Y, UP = the last row, A, はい).
Exit status 0 when every check passes."""
import argparse
import concurrent.futures as cf
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hrun  # noqa: E402
import mksave  # noqa: E402

V1_SIZE, V1_NINV, V2_NINV, V2_SIZE = mksave.V1_SIZE, mksave.V1_NINV, mksave.V2_NINV, mksave.V2_SIZE
HDR, SLOT_B = 12, 0x4000
SHOPS_SEEN, GUIDES = 636, 628
GDB_NAV_FORGE = 16

TITLE = 'loadsram {srm}\nwait 200\nshot {out}/title.ppm\npress A\nwait 150\nwram {out}/map.bin\nshot {out}/map.ppm\n'
BATTLE = 'press A\nwait 60\npress A\nwait 60\npress A\nwait 240\nwram {out}/battle.bin\nsram {out}/after.srm\n'
# a full bag cannot sortie: menu (Y), UP = the last row タイトルへ, A, はい (A) writes the save
TO_TITLE = 'press Y\nwait 60\npress UP\nwait 10\npress A\nwait 30\npress A\nwait 60\nwram {out}/battle.bin\nsram {out}/after.srm\n'

fails = []


def check(ok, what):
    print('  %s %s' % ('ok  ' if ok else 'FAIL', what))
    if not ok:
        fails.append(what)
    return ok


def mk(path, *args):
    r = subprocess.run([sys.executable, os.path.join(hrun.HERE, 'mksave.py'), path] + list(args),
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit('mksave failed: %s\n%s' % (' '.join(args), r.stdout + r.stderr))
    return open(path, 'rb').read()


def play(rom, out, srm, then=BATTLE):
    """title -> map (WRAM, shots) [-> THEN: a save write (WRAM, SRAM)] on ROM; returns out"""
    os.makedirs(out, exist_ok=True)
    text = TITLE.format(srm=srm, out=out) + (then.format(out=out) if then else '')
    r = hrun.run(rom, text, out)
    if r.returncode:
        sys.exit('runrom failed on %s (%s):\n%s' % (rom, out, r.stdout + r.stderr))
    return out


def sv_of(rom, dump, size=V2_SIZE):
    d = open(dump, 'rb').read()
    o = hrun.addr(hrun.symbols(rom), 'sv')
    return d[o:o + size]


def u16(d, o):
    return struct.unpack_from('<H', d, o)[0]


def hdr(d):
    """magic, seq, sum, size, ver"""
    return (bytes(d[0:4]),) + struct.unpack_from('<HHHH', d, 4)


def body_sum(slot, ninv_off, size):
    n = u16(slot, ninv_off)
    used = size - HDR - (1009 - n) * 8
    return sum(slot[HDR:HDR + used]) & 0xffff


def used_len(slot, ninv_off):
    """bytes of a slot that are written (header + body up to the last item)"""
    return ninv_off + 2 + u16(slot, ninv_off) * 8


def migrated(v1):
    """the v2 image the migration must make from a v1 file (ver / size and the v1_init fields)"""
    n = u16(v1, V1_NINV)
    img = bytearray(v1[:V1_NINV]) + bytes(V2_NINV - V1_NINV) + v1[V1_NINV:V1_NINV + 2 + n * 8]
    struct.pack_into('<HH', img, 8, V2_SIZE, 2)
    area_open = v1[12]
    seen = 0
    for a in range(min(area_open, 15) + 1):
        if a in (1, 3, 5, 8):            # area_has_shop: AR_PLAIN, AR_POTOS, AR_OASIS, AR_FISHING
            seen |= 1 << a
    struct.pack_into('<H', img, SHOPS_SEEN, seen)
    img[GUIDES] = GDB_NAV_FORGE if area_open >= 1 else 0
    return bytes(img)


def check_write(after, slot_off, seq, sv_now, what):
    """a slot written as v2 with SEQ, its body = the game's sv"""
    s = after[slot_off:slot_off + V2_SIZE]
    h = hdr(s)
    check(h[0] == b'KSFC' and h[1] == seq and h[3] == V2_SIZE and h[4] == 2,
          '%s: slot %s v2 header, seq %d (got seq %d, size %d, ver %d)' % (what, 'AB'[slot_off // SLOT_B], seq, h[1], h[3], h[4]))
    check(h[2] == body_sum(s, V2_NINV, V2_SIZE), '%s: slot sum matches the body' % what)
    n = used_len(sv_now, V2_NINV)
    check(s[:n] == sv_now[:n], '%s: slot = WRAM sv (%d bytes)' % (what, n))


def guard():
    print('guard: Save must end before the tower record at slot + $3E00')
    tmp = tempfile.mkdtemp(prefix='savetest_guard_')
    cc65 = os.path.join(os.environ['CC65_HOME'], 'bin', 'cc65') if os.environ.get('CC65_HOME') else 'cc65'
    for spare, size, want in ((64, 9300, 0), (6636, 15872, 0), (6637, 15873, 1), (7000, 16236, 1)):
        r = subprocess.run([cc65, '-O', '-Oirs', '-Cl', '-t', 'none', '--cpu', '65sc02',
                            '-I', os.path.join(hrun.ROOT, 'src', 'c'), '-I', os.path.join(hrun.ROOT, 'data', 'gen'),
                            '-D', 'SAVE_SPARE=%d' % spare, '-o', os.path.join(tmp, 'save.s'),
                            os.path.join(hrun.ROOT, 'src', 'c', 'save.c')], capture_output=True, text=True)
        msg = (r.stdout + r.stderr).strip()
        if want:
            check(r.returncode != 0 and "'save_fits_slot' is invalid" in msg,
                  'spare %d (Save %d B): the compile fails on the guard (%s)' % (spare, size, msg.splitlines()[0] if msg else 'no message'))
        else:
            check(r.returncode == 0, 'spare %d (Save %d B): compiles%s' % (spare, size, '' if not msg else ' (%s)' % msg))
    shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', nargs='?', default=hrun.ROM)
    ap.add_argument('--old')
    ap.add_argument('--no-guard', action='store_true')
    ap.add_argument('-o', '--out')
    a = ap.parse_args()
    out = os.path.abspath(a.out) if a.out else tempfile.mkdtemp(prefix='savetest_')
    os.makedirs(out, exist_ok=True)
    rom = os.path.abspath(a.rom)
    P = lambda *p: os.path.join(out, *p)  # noqa: E731

    # ---------------------------------------------------------------- saves
    base = ['--open', '5', '--clear-all', '--coins', '4242', '--items', '3', '17', '40']
    v1 = mk(P('v1.srm'), '--v1', *base)
    v2 = mk(P('v2.srm'), '--shops-seen', '0x2a', '--guides', '16', *base)   # = what the migration makes
    full = ['--items'] + ['%d:%d,%d' % (i % 197, i % 50, (i * 7) % 50) for i in range(997)]
    big1 = mk(P('v1_999.srm'), '--v1', '--open', '5', '--clear-all', *full)
    big2 = mk(P('v1_1009.srm'), '--v1', '--open', '5', *(full + ['%d:%d' % (i, i) for i in range(10)]))
    allf = mk(P('v2_all.srm'), '--open', '5', '--clear-all', '--items', '3:1,2', '17', '40', '--guides', '0',
              '--solo', '7', '--mirage-cards', '0x2a5a5', '--shops-seen', '0x22', '--boss-wins', '3,1',
              '--boss-rec', '0:2:40:12:1:2,3,-,1', '3:4:99:50:7:-,-,4,1',
              '--set', '1:ドラゴンたいじ:2,3,-,1', '15::gone,-,4,1')
    open(P('empty.srm'), 'wb').write(bytes(32768))

    jobs = {
        'mig': (rom, P('mig'), P('v1.srm')),
        'mk2': (rom, P('mk2'), P('v2.srm')),
        'big1': (rom, P('big1'), P('v1_999.srm'), TO_TITLE),
        'big2': (rom, P('big2'), P('v1_1009.srm'), TO_TITLE),
        'allf': (rom, P('allf'), P('v2_all.srm')),
    }
    if a.old:
        jobs['old'] = (os.path.abspath(a.old), P('old'), P('v1.srm'), None)
        jobs['old_big'] = (os.path.abspath(a.old), P('old_big'), P('v1_999.srm'), None)
        jobs['old_v2'] = (os.path.abspath(a.old), P('old_v2'), P('v2.srm'), None)
    with cf.ThreadPoolExecutor(max_workers=8) as ex:
        futs = {k: ex.submit(play, *v) for k, v in jobs.items()}
        ngf = ex.submit(new_game, rom, P('newgame'), P('empty.srm'))
        res = {k: f.result() for k, f in futs.items()}
        ngf.result()

    # ---------------------------------------------------------------- 1. migration
    print('1. a v1 save (--v1 --open 5 --clear-all --coins 4242 --items 3 17 40) on the v2 ROM')
    sv = sv_of(rom, P('mig', 'map.bin'))
    n = u16(v1, V1_NINV)
    h = hdr(sv)
    check(h[0] == b'KSFC' and h[3] == V2_SIZE and h[4] == 2, 'sv: ver 2, size 9300 (got ver %d, size %d)' % (h[4], h[3]))
    check(sv[12:V1_NINV] == v1[12:V1_NINV], 'sv bytes 12..627 = the v1 save')
    check(u16(sv, V2_NINV) == n and sv[V2_NINV:V2_NINV + 2 + n * 8] == v1[V1_NINV:V1_NINV + 2 + n * 8],
          'bag moved to 1226 / 1228 and identical (%d items)' % n)
    blk = sv[V1_NINV:V2_NINV]
    nz = [(V1_NINV + i, x) for i, x in enumerate(blk) if x]
    check(nz == [(GUIDES, GDB_NAV_FORGE), (SHOPS_SEEN, 0x2a)],
          'v2 block zero except guides 16 and shops_seen 0x2A (areas 1, 3, 5): %s' % nz)
    check(sv[:used_len(sv, V2_NINV)] == migrated(v1), 'sv = the expected migrated image')
    if a.old:
        old = sv_of(a.old, P('old', 'map.bin'), V1_SIZE)
        check(old[12:V1_NINV] == sv[12:V1_NINV], 'the v1 part equals the old ROM\'s sv')
        check(old[V1_NINV:V1_NINV + 2 + n * 8] == sv[V2_NINV:V2_NINV + 2 + n * 8], 'the bag equals the old ROM\'s bag')
        check(open(P('old', 'title.ppm'), 'rb').read() == open(P('mig', 'title.ppm'), 'rb').read(),
              'title shot (v1 slot only) = the old ROM\'s')
        check(open(P('old', 'map.ppm'), 'rb').read() == open(P('mig', 'map.ppm'), 'rb').read(),
              'map shot = the old ROM\'s')
    after = open(P('mig', 'after.srm'), 'rb').read()
    check(after[:V1_SIZE] == v1[:V1_SIZE], 'battle start: slot A still the v1 save')
    check_write(after, SLOT_B, 2, sv_of(rom, P('mig', 'battle.bin')), 'battle start')

    # ---------------------------------------------------------------- 2./3. reload, fallback
    s2 = bytearray(after)
    bad = bytearray(after)
    bad[SLOT_B + 16] ^= 0x55                      # coins: a summed byte of the v2 slot
    open(P('reload.srm'), 'wb').write(s2)
    open(P('bad.srm'), 'wb').write(bad)
    with cf.ThreadPoolExecutor(max_workers=2) as ex:
        f1 = ex.submit(play, rom, P('reload'), P('reload.srm'))
        f2 = ex.submit(play, rom, P('bad'), P('bad.srm'))
        f1.result(), f2.result()
    print('2. reload that SRAM')
    sv2 = sv_of(rom, P('reload', 'map.bin'))
    nb = used_len(after[SLOT_B:], V2_NINV)
    check(sv2[:nb] == after[SLOT_B:SLOT_B + nb], 'loads slot B (the v2 slot, seq 2)')
    a2 = open(P('reload', 'after.srm'), 'rb').read()
    check_write(a2, 0, 3, sv_of(rom, P('reload', 'battle.bin')), 'next write')
    check(a2[SLOT_B:] == after[SLOT_B:], 'slot B unchanged')
    print('3. one byte of the v2 slot corrupted')
    sv3 = sv_of(rom, P('bad', 'map.bin'))
    check(sv3[:used_len(sv3, V2_NINV)] == migrated(v1), 'falls back to the v1 slot and migrates it')
    a3 = open(P('bad', 'after.srm'), 'rb').read()
    check(a3[:V1_SIZE] == v1[:V1_SIZE], 'slot A still the v1 save')
    check_write(a3, SLOT_B, 2, sv_of(rom, P('bad', 'battle.bin')), 'slot B rewritten')

    # ---------------------------------------------------------------- 4. mksave v2
    print('4. mksave v2 output')
    sv4 = sv_of(rom, P('mk2', 'map.bin'))
    nv = used_len(v2, V2_NINV)
    check(sv4[:nv] == v2[:nv], 'loads as written (%d bytes)' % nv)
    mig = bytearray(migrated(v1))
    mig[6:8] = v2[6:8]                            # sum: the migrated sv keeps the v1 sum until its first write
    check(v2[:nv] == bytes(mig), 'mksave v2 = the migrated v1 save (same options; header sum aside)')
    check(open(P('mk2', 'map.ppm'), 'rb').read() == open(P('mig', 'map.ppm'), 'rb').read(), 'map shot = the migrated save\'s')
    check_write(open(P('mk2', 'after.srm'), 'rb').read(), SLOT_B, 2, sv_of(rom, P('mk2', 'battle.bin')), 'battle start')
    sva = sv_of(rom, P('allf', 'map.bin'))
    na = used_len(allf, V2_NINV)
    check(sva[:na] == allf[:na], 'every v2 option (guides, tickets, cards, shops, wins, records, sets) loads as written')
    aa = open(P('allf', 'after.srm'), 'rb').read()
    check(aa[SLOT_B + V1_NINV:SLOT_B + V2_NINV] == allf[V1_NINV:V2_NINV], 'and the v2 block is written back unchanged')
    check_write(aa, SLOT_B, 2, sv_of(rom, P('allf', 'battle.bin')), 'battle start')

    # ---------------------------------------------------------------- 5. full bags
    for k, img in (('big1', big1), ('big2', big2)):
        n = u16(img, V1_NINV)
        print('5. a v1 save with %d items' % n)
        svb = sv_of(rom, P(k, 'map.bin'))
        check(u16(svb, V2_NINV) == n and svb[V2_NINV:V2_NINV + 2 + n * 8] == img[V1_NINV:V1_NINV + 2 + n * 8],
              'bag identical at 1228 (%d B moved)' % (2 + n * 8))
        check(svb[:used_len(svb, V2_NINV)] == migrated(img), 'sv = the expected migrated image')
        if k == 'big1' and a.old:
            ob = sv_of(a.old, P('old_big', 'map.bin'), V1_SIZE)
            check(ob[V1_NINV:V1_NINV + 2 + n * 8] == svb[V2_NINV:V2_NINV + 2 + n * 8], 'the bag equals the old ROM\'s')
        check_write(open(P(k, 'after.srm'), 'rb').read(), SLOT_B, 2, sv_of(rom, P(k, 'battle.bin')), 'menu タイトルへ')

    # ---------------------------------------------------------------- 6. title, new game
    print('6. title with only a v1 slot; a new game')
    t_v1 = open(P('mig', 'title.ppm'), 'rb').read()
    check(t_v1 == open(P('mk2', 'title.ppm'), 'rb').read(), 'title (v1 slot only) = title with a v2 save (つづきから)')
    check(t_v1 != open(P('newgame', 'title.ppm'), 'rb').read(), 'and differs from the title without a save')
    ng = open(P('newgame', 'after.srm'), 'rb').read()
    check_write(ng, SLOT_B, 1, sv_of(rom, P('newgame', 'map.bin')), 'new game')
    check(not any(ng[:HDR]), 'slot A empty')

    if a.old:
        check(open(P('old_v2', 'title.ppm'), 'rb').read() == open(P('newgame', 'title.ppm'), 'rb').read(),
              'downgrade: the old ROM shows a v2 save as no save (README: copy the .srm first)')

    if not a.no_guard:
        guard()
    print('%d check(s) failed: %s' % (len(fails), '; '.join(fails)) if fails else 'all checks passed')
    if not fails and not a.out:
        shutil.rmtree(out, ignore_errors=True)
    elif fails:
        print('files in', out)
    sys.exit(1 if fails else 0)


def new_game(rom, out, srm):
    """empty SRAM: はじめから -> difficulty (A) -> the first write (main.c), then a WRAM / SRAM dump"""
    os.makedirs(out, exist_ok=True)
    text = ('loadsram %s\nwait 200\nshot %s/title.ppm\npress A\nwait 60\npress A\nwait 60\n'
            'wram %s/map.bin\nsram %s/after.srm\n') % (srm, out, out, out)
    r = hrun.run(rom, text, out)
    if r.returncode:
        sys.exit('runrom failed (new game):\n' + r.stdout + r.stderr)


if __name__ == '__main__':
    main()
