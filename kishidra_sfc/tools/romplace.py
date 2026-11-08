#!/usr/bin/env python3
"""Shared ROM placer: puts every generated data blob into the ROM space that is left after the code
and the fixed segments are linked (build.py links twice: pass 1 with stub(), pass 2 with place()).

Generators (tools/gfx/assets.py, make_data.py, make_audio.py and any later one) do not choose banks.
They write their bytes to files under data/gen/ and a manifest data/gen/place/<name>.json
(write_manifest()):

    {"entries": [
      {"label": "gfx_title_tiles", "file": "gfx/title_tiles.bin", "off": 0, "size": 14336,
       "kind": "whole", "speed": "any"},
      {"label": "font_width", "file": "font.bin.wid", "size": 705, "kind": "whole", "speed": "fast",
       "long": true},
      {"label": "bgm", "file": "audio/bgm.brr", "size": 1925793, "kind": "stream", "unit": 9, "min": 64,
       "speed": "slow", "hook": "bgm", "tracks": [{"name": "BGM_OPENING", "first": 0, "blocks": 38542,
       "loop": 11023}, ...]}
    ]}

  kind  "whole"   the blob sits in one bank (DMA sources, LZSS / tile-run art, fonts, strings, anything
                  read with a 16-bit offset).  At most $8000 bytes.
        "stream"  may be cut into chunks of `unit` bytes (BGM: 9-byte BRR blocks), each chunk at least
                  `min` units except the last.  The placer writes <label>_seg_bank[255] (.byte),
                  <label>_seg_ptr[255] and <label>_seg_len[255] (.word, length in units) in segment
                  TABLES and, for "hook": "bgm", bgm_table: 16 B per track = [segment, ptr lo, ptr hi,
                  left lo, left hi, blocks 0, 1, 2] for the start and the same for the loop (blocks 0 =
                  played once); "left" = units left in that segment.
  speed "fast"    CPU-read data (fonts, strings, LZSS / tile runs): FastROM banks only.
        "any"     DMA-read data: FastROM first, the slow banks (ExLoROM $01-$7D) when FastROM is full.
        "slow"    the stream: slow banks first, then what is left anywhere (placed last).
  long  true      the code reads it with 24-bit absolute addresses (lda f:label,x): never in the code
                  banks $81-$9F (their memory areas start at $8000, so the address there has bank 0 and
                  only .bank(label) is right).  Data read through a (.loword, .bank) pointer can go
                  anywhere.

Every blob gets an exported label.  Refer to it with .loword(label) and <.bank(label) (an 8-bit
context needs the '<'), never ^label.  Every ROM memory area carries its bank attribute, so .bank()
is right everywhere (build.py linker_cfg).

Placement (place()):
  1. whole blobs, biggest first (ties by label), best fit (the bank with the least free space that
     still holds it) over the FastROM banks (for long ones only the banks that allow long reads);
     'any' blobs fall back to the slow banks.
  2. streams last: slow banks first, then every tail that is left in bank order; tails under `min`
     units are skipped (except for the last chunk); more than 255 segments is an error.
In every memory area the placed segment PLxx is the last one, so nothing linked before it moves
between the passes; place() writes an .assert per label that fails the link if one did.
"""
import glob
import json
import os

MAXSEG = 255                 # entries of a stream's segment tables (s_bi is one byte)
BANK = 0x8000                # bytes of a LoROM bank
KINDS = ('whole', 'stream')
SPEEDS = ('fast', 'any', 'slow')


def load(gen):
    """every entry of data/gen/place/*.json (sorted by file name), checked"""
    entries = []
    for p in sorted(glob.glob(os.path.join(gen, 'place', '*.json'))):
        for e in json.load(open(p, encoding='utf-8'))['entries']:
            e.setdefault('off', 0)
            e.setdefault('speed', 'fast')
            e.setdefault('kind', 'whole')
            e.setdefault('long', False)
            e['_from'] = os.path.basename(p)
            entries.append(e)
    seen, dup = set(), set()
    for e in entries:
        if e['label'] in seen:
            dup.add(e['label'])
        seen.add(e['label'])
    if dup:
        raise SystemExit('romplace: label defined twice: %s' % ', '.join(sorted(dup)))
    for e in entries:
        where = '%s (%s)' % (e['label'], e['_from'])
        if e['kind'] not in KINDS or e['speed'] not in SPEEDS:
            raise SystemExit('romplace: %s: kind %r / speed %r' % (where, e['kind'], e['speed']))
        f = os.path.join(gen, e['file'])
        if not os.path.exists(f) or os.path.getsize(f) < e['off'] + e['size']:
            raise SystemExit('romplace: %s: %s has fewer than %d bytes' % (where, e['file'], e['off'] + e['size']))
        if e['kind'] == 'whole' and e['size'] > BANK:
            raise SystemExit('romplace: %s is %d bytes (more than a bank)' % (where, e['size']))
        if e['kind'] == 'stream' and (e['size'] % e['unit'] or e.get('min', 1) < 1):
            raise SystemExit('romplace: %s: size %d is not a multiple of unit %d' % (where, e['size'], e['unit']))
    return entries


def write_manifest(gen, name, entries):
    """data/gen/place/<name>.json"""
    d = os.path.join(gen, 'place')
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name + '.json'), 'w', encoding='utf-8') as f:
        json.dump({'entries': entries}, f, ensure_ascii=False, indent=0)
        f.write('\n')


def _stream_tables(e, segs):
    """segment tables of a stream (+ the BGM track table) in segment TABLES; segs = [] gives the
    same sizes (pass 1)"""
    lab = e['label']
    pad = MAXSEG - len(segs)
    s = ['.export %s_seg_bank, %s_seg_ptr, %s_seg_len' % (lab, lab, lab), '.segment "TABLES"']
    if segs:
        s.append('%s_seg_bank: .byte ' % lab + ', '.join('<.bank(%s)' % c for c, b, p, f, n in segs) + ', 0' * pad)
        s.append('%s_seg_ptr: .word ' % lab + ', '.join('.loword(%s)' % c for c, b, p, f, n in segs) + ', 0' * pad)
        s.append('%s_seg_len: .word ' % lab + ', '.join('%d' % n for c, b, p, f, n in segs) + ', 0' * pad)
    else:
        s += ['%s_seg_bank: .res %d' % (lab, MAXSEG), '%s_seg_ptr: .res %d' % (lab, 2 * MAXSEG),
              '%s_seg_len: .res %d' % (lab, 2 * MAXSEG)]
    if e.get('hook') == 'bgm':
        s.append('.export bgm_table')
        if not segs:
            s.append('bgm_table: .res %d' % (16 * len(e['tracks'])))
        else:
            def at(g):
                for k, (c, b, p, first, n) in enumerate(segs):
                    if first <= g < first + n:
                        return k, (p + (g - first) * e['unit']) & 0xffff, n - (g - first)
                raise SystemExit('romplace: %s: block %d is not in the stream' % (lab, g))
            s.append('bgm_table:   ; per track: segment, ptr, left in segment, blocks (3); the same for the loop (0 = once)')
            for t in e['tracks']:
                first, n, loop = t['first'], t['blocks'], t['loop']
                k, p, left = at(first)
                row = [k, p & 0xff, p >> 8, left & 0xff, left >> 8, n & 0xff, (n >> 8) & 0xff, n >> 16]
                if loop is not None:
                    k, p, left = at(first + loop)
                    ln = n - loop
                    row += [k, p & 0xff, p >> 8, left & 0xff, left >> 8, ln & 0xff, (ln >> 8) & 0xff, ln >> 16]
                else:
                    row += [0] * 8
                s.append('  .byte ' + ', '.join('$%02x' % v for v in row) + '   ; %s' % t['name'])
    return s


def stub(entries):
    """pass 1: every label in the unwritten area PLSTUB, the stream tables at their final size"""
    s = ['; generated by build.py / tools/romplace.py (pass 1: no data)', '.segment "PLSTUB"']
    for e in entries:
        if e['kind'] == 'whole':
            s += ['.export %s' % e['label'], '%s:' % e['label']]
    for e in entries:
        if e['kind'] == 'stream':
            s += _stream_tables(e, [])
    return '\n'.join(s) + '\n'


def place(entries, banks):
    """banks: [{bank (CPU bank byte), base (16-bit address of the area start), used (bytes of the fixed
    segments, from the pass 1 map), speed ('fast' / 'slow'), long (may hold long blobs)}].
    Returns (asm text of pass 2, report lines, {label: (bank, 16-bit address, size, entry)})."""
    free = {b['bank']: BANK - b['used'] for b in banks}
    at = {b['bank']: b['base'] + b['used'] for b in banks}
    fast = [b['bank'] for b in banks if b['speed'] == 'fast']
    slow = [b['bank'] for b in banks if b['speed'] == 'slow']
    longok = {b['bank'] for b in banks if b.get('long', True)}
    chunks = {}                     # bank -> [(label, entry, file offset, size, address)]
    where = {}

    def put(bank, label, e, off, size):
        chunks.setdefault(bank, []).append((label, e, off, size, at[bank]))
        where[label] = (bank, at[bank], size, e)
        at[bank] += size
        free[bank] -= size

    def best_fit(cands, size):
        best = None
        for b in cands:
            if free[b] >= size and (best is None or free[b] < free[best]):
                best = b
        return best

    # 1. whole blobs: biggest first, ties by label (the result does not depend on the manifest order)
    for e in sorted([e for e in entries if e['kind'] == 'whole'], key=lambda e: (-e['size'], e['label'])):
        ok = [b for b in fast if b in longok] if e['long'] else fast
        b = best_fit(ok, e['size'])
        if b is None and e['speed'] != 'fast':
            b = best_fit([x for x in slow if x in longok] if e['long'] else slow, e['size'])
        if b is None:
            raise SystemExit('ROM が足りない: %s (%d bytes, %s)' % (e['label'], e['size'], e['_from']))
        put(b, e['label'], e, e['off'], e['size'])
    # 2. streams: slow banks first, then every tail in bank order
    segs_of = {}
    for e in [e for e in entries if e['kind'] == 'stream']:
        unit, mn = e['unit'], e.get('min', 1)
        n = e['size'] // unit
        g = 0
        segs = []
        order = slow + fast if e['speed'] == 'slow' else fast + slow
        for b in [x for x in order if not e['long'] or x in longok]:
            if g >= n:
                break
            k = min(free[b] // unit, n - g)
            if k <= 0 or (k < mn and k < n - g):
                continue
            c = '%s_c%d' % (e['label'], len(segs))
            segs.append((c, b, at[b], g, k))
            put(b, c, e, e['off'] + g * unit, k * unit)
            g += k
        if g < n:
            raise SystemExit('ROM が足りない: %s, %d bytes do not fit' % (e['label'], (n - g) * unit))
        if len(segs) > MAXSEG:
            raise SystemExit('romplace: %s needs %d segments, the tables hold %d' % (e['label'], len(segs), MAXSEG))
        segs_of[e['label']] = segs
    s = ['; generated by build.py / tools/romplace.py (pass 2)']
    for b in sorted(chunks):
        s.append('.segment "PL%02X"' % b)
        for label, e, off, size, addr in chunks[b]:
            s.append('.export %s' % label)
            s.append('%s: .incbin "%s", %d, %d' % (label, e['file'], off, size))
            s.append('.assert .loword(%s) = $%04X && .bank(%s) = $%02X, error, "romplace: %s moved between the link passes"'
                     % (label, addr & 0xffff, label, b, label))
    for e in entries:
        if e['kind'] == 'stream':
            s += _stream_tables(e, segs_of[e['label']])
    tot = {}
    for label, (b, addr, size, e) in where.items():
        tot[e['_from']] = tot.get(e['_from'], 0) + size
    rep = ['placed: ' + ', '.join('%s %.1fK' % (k, v / 1024) for k, v in sorted(tot.items()))]
    rep.append('ROM free after placing: %.1f KB (%s)' % (sum(free.values()) / 1024, ', '.join(
        '$%02X:%d' % (b, free[b]) for b in sorted(free) if free[b] >= 1024)))
    for e in entries:
        if e['kind'] == 'stream':
            rep.append('%s: %d segments' % (e['label'], len(segs_of[e['label']])))
    return '\n'.join(s) + '\n', rep, where


def listing(where):
    """placement listing (build/kishidra.place.txt): bank:address, size, label, manifest"""
    out = ['; bank:addr  size  label  (manifest)   -- tools/romplace.py']
    for label, (b, addr, size, e) in sorted(where.items(), key=lambda kv: (kv[1][0], kv[1][1])):
        out.append('%02X:%04X %6d %s (%s%s)' % (b, addr & 0xffff, size, label, e['_from'], ', long' if e['long'] else ''))
    return '\n'.join(out) + '\n'
