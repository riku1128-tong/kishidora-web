# Spec: shared infrastructure (group "Shared infrastructure")

SNES port of キシドラ！ (`/home/user/kishidora-web/kishidra_sfc`). Four items that every other group builds on:

1. ROM bank layout: one shared placer for all generated data (`tools/romplace.py`)
2. Font and text capacity (fonts and strings placed by the same placer; glyph count of all new text)
3. Save data v2 (one consolidated layout with offsets, SAVE_VER 1 -> 2, migration of old saves)
4. Menu structure (where every new screen goes; complete key map, clashes resolved)

**Items 1-3 were prototyped, built and run** in a copy of the port
(`plan/shared_infra/work`, snes9x libretro core). The repo itself was not touched.

Files in `/tmp/claude-0/-home-user-kishidora-web/bfba2c3a-727d-51a5-9268-545e956e9958/scratchpad/plan/shared_infra/`:

| file | what |
|---|---|
| `proto_src.patch` | the whole prototype vs the port: build.py, new tools/romplace.py, tools/gfx/assets.py, make_data.py, make_audio.py, src/asm/{asset,sound,text}.s, src/c/save.{c,h}, tools/harness/mksave.py |
| `work/` | the work copy (data/gen converted with `scripts/convert_gen.py`, so the content is exactly the shipped one) |
| `scripts/convert_gen.py` | prototype helper: feeds today's data/gen through the NEW writers (no web-build regeneration) |
| `scripts/regress.py` | runs one harness script on two ROMs and compares every screenshot / dump / wav byte for byte |
| `scripts/textcost_all.py`, `textcost_all.txt` | every group's new strings encoded against the port's font: strings, bytes, new glyphs, id clashes |
| `scripts/simulate_full.py` | places today's blobs + all groups' additions + the 28-track BGM stream, for 4 / 6 / 8 MB |
| `scripts/bankmap.py`, `bankmap4.txt` | per-bank fixed / placed / free bytes of a link |
| `base.sfc`, `placed4.sfc`, `placed8.sfc` (+ `.map`) | today's ROM, the placer ROM (4 MB), the placer ROM as 8 MB ExLoROM |
| `t/` | harness scripts, saves and results (`t/reg1`, `t/reg2`, `t/reg1x8`, `t/reg2x8`, `t/mig*`, `t/m_v2*`, `t/mk*`) |

Read `plan/bgm/shipped/*.gd` (scripts decoded from the shipped index.pck) rather than `scratchpad/src`, which is older
(bgm spec §0.1). Everything below that quotes the web was checked in the shipped scripts.

---------------------------------------------------------------------------------------------------------------

## 1. ROM bank layout: one shared placer

### 1.1 Today (measured from build/kishidra.map)

- Each generator picks banks itself:
  `make_gfx.py GFX_BANKS = $C3-$DF + $A0-$BF` (first-fit; the table at the start of $C3),
  `make_data.py` estat + text_table in `$C0`, strings from `$C1` on, fonts in "the bank after the last string bank",
  `make_audio.py BGM_BANKS = $B0-$BF + $E0-$FD + $90-$9F` (whole banks, 3640 blocks each), SPC image `$FE-$FF`,
  `text.s` shift tables fixed in `$AF` (4 KB).
- Segments 3,635,895 B, **free 558,409 B (545.3 KB)**, but scattered: 276.7 KB in the C-code bank tails ($81-$8E),
  160 KB in empty banks ($86, $8F, $9D-$9F), 28 KB in $AE/$AF, 17.4 KB in $C0-$C2, 17.5 KB in $FF, 10.6 KB in $80.
  102 banks have < 1 KB free. **The GFX banks have only ~28 KB.**
- Traps: `GFX_BANKS` lists `$AF` (text.s shift tables) and `$B0-$BF` (BGM): the next ~28 KB of art makes a link error.
  More strings spill `$C1 -> $C2`, which pushes the fonts into `$C3`, a full GFX bank (forge spec: link error).
- **Why the code-bank tails cannot be used today:** the C banks `$81-$9F` are memory areas with `start=$8000,
  bank=$XX` (cc65 code uses 16-bit `jsr`; with 24-bit addresses ld65 would report range errors). In those areas a
  label's address has bank 0, so `^label` is 0 (sortie spec: moving the font there turned all text into garbage).
  `.bank(label)` gives the right bank, but ld65 refuses `.bank()` for an area without a bank attribute
  (measured: `ld65: Error: Memory area 'M3' is referenced by .BANK but has no BANK attribute`).
- **Second trap (found in the prototype):** code that reads a label with a 24-bit absolute address
  (`lda f:font_width,x`) also gets bank 0 when the label is in `$81-$9F`. The first placer run put `font_width` in a
  code bank: only the first glyph of each string was drawn (`t/reg1` before the fix). Such data must stay out of
  the code banks.

### 1.2 Design

**One placer, two link passes.** Generators no longer choose banks. They write their bytes to files under
`data/gen/` and a manifest `data/gen/place/<name>.json`. `build.py` links once with every placed label in a stub,
reads the bytes used in each bank from the map, places all blobs into the space that is left, and links again.

Manifest entry (JSON):

```
{"label": "gfx_title_tiles", "file": "gfx/title_tiles.bin", "off": 0, "size": 28704,
 "kind": "whole", "speed": "any", "long": false}
{"label": "bgm", "file": "audio/bgm.brr", "size": 1925793, "kind": "stream", "unit": 9, "min": 64,
 "speed": "slow", "hook": "bgm", "tracks": [{"name": "BGM_OPENING", "first": 0, "blocks": 38542, "loop": 11023}, ...]}
```

| field | meaning |
|---|---|
| `kind: whole` | the blob sits in one bank (DMA sources, LZSS / tile-run art, fonts, strings, anything read with a 16-bit offset). ≤ $8000 bytes |
| `kind: stream` | may be cut into chunks of `unit` bytes, each ≥ `min` units except the last. The placer writes `<label>_seg_bank[255]`, `_seg_ptr[255]`, `_seg_len[255]`, and for `hook: bgm` the per-track `bgm_table` (16 B per track, same format as the bgm group's pack_bgm: segment, ptr, left, blocks ×2) |
| `speed: fast` | CPU-read data (fonts, strings, LZSS): FastROM banks only |
| `speed: any` | DMA-read data: FastROM first, the ExLoROM slow banks `$01-$7D` only when FastROM is full |
| `speed: slow` | the stream: slow banks first, then every tail that is left (placed last) |
| `long: true` | read with 24-bit absolute addresses (`lda f:label,x`): never in `$81-$9F` |

Rules for every asm file and generator (new and old):

- **Use `.bank(label)` for the bank byte of ROM data, never `^label`.** In an 8-bit context write `#<.bank(x)` /
  `.byte <.bank(x)` (measured: `lda #.bank(x)` and `.byte .bank(x)` give a ca65 "Range error").
  `^label` stays correct only for ROM00 / WRAM labels.
- Data read through a `(.loword, .bank)` pointer (asset table, `t_glyph`, `t_src`, `fc_src`) can go anywhere.
  Data read as `f:label` is `long: true`, or lives in the fixed segment `TABLES`.
- Every ROM memory area carries its bank attribute (`ROM00 bank=$80`, `$81-$9F start=$8000 bank=$XX` as today,
  `$A0-$FF start=$XX8000 bank=$XX` **new**, ExLoROM `EXTnn start=$nn8000 bank=$nn`), so `.bank()` works everywhere
  and a missing attribute is a link error, not a silent 0.

**Placement policy** (`romplace.place`):

1. `whole` blobs, biggest first (ties by label), **best fit** (the bank with the least free space that still holds it)
   over the FastROM banks `$81-$FF` (only `$A0-$FF` for `long`); `any` blobs fall back to the slow banks.
2. `stream` blobs last: slow banks first (ExLoROM), then every remaining tail in bank order `$81..$FF`; chunks under
   `min` units are skipped (except the last); more than 255 segments is an error.
3. Bank `$80` (engine, RODATA source, header, vectors) is never used. Each bank is filled after its fixed segments
   (`CODEn`, `ENGn`, `BANKxx`, `TABLES`): in every memory area the placed segment `PLxx` is the last one, so nothing
   linked before it moves between the passes (`.assert .loword(label) = $xxxx` in the generated file catches drift).

**Fixed things that stay where they are:** `ROM00` ($80), C code (`C_BANKS` in build.py), `text.s` shift tables
(`BANKAF`), the SPC image (`BANKFE`/`BANKFF`, split at `SPC_SPLIT_END` as today), and the new segment
**`TABLES`** in `$C0` (first in that area): `gfx_table`, `text_table`, `bgm_seg_*`, `bgm_table` and any other small
table that code reads with `f:` and that points at placed blobs. Measured: TABLES 12,146 B today (gfx 6,522 +
text 4,221 + BGM 1,403); with all groups ≈ 20 KB (text_table +2,604, gfx_table ≈ +1.5 KB, bgm_table +320,
sortie `plan` 1,321, inventory `reco` 1.7 KB, forge `farm` 441). If it ever passes 32 KB, add `TABLES2` in `$C1`.

**Pass 1 stub:** every `whole` label is exported from segment `PLSTUB` in a memory area that is not written
(`PLSTUB: start=$0000, size=$10000, file="", bank=$00`), stream tables at their final size (`.res`). Table sizes do
not depend on placement, so pass 1 and pass 2 have the same fixed layout.

**Identical bodies are stored once** (gfx: same blob bytes -> same label; text: same code sequence -> same body).
Measured: 1 gfx asset (`MON_093_HURT`, 493 B) and **164 of 1,407 strings (1,858 B)**.

### 1.3 Exact changes (all in `proto_src.patch`)

- **`tools/romplace.py`** (new, 230 lines): `load(gen)` (reads `data/gen/place/*.json`, checks duplicate labels and
  `size > $8000`), `write_manifest(gen, name, entries)`, `stub(entries)`, `place(entries, banks) -> (asm, report,
  where)`. `banks` = list of `{bank, base: $8000, used, speed: fast|slow, long: bool}`.
- **`build.py`**:
  - `ROM_MB = int(os.environ.get('ROM_MB', '4'))`, `EXT_BANKS = {4: 0, 6: 0x40, 8: 0x7E}[ROM_MB]`, `TABLE_BANK = 0xC0`
    (the ROM-size decision belongs to the bgm / rom_compat groups; the placer works for all three).
  - `linker_cfg()`: `bank=$80` on ROM00, `bank=$XX` on `ROM20..ROM7F` (`$A0-$FF`), `PLSTUB` area + segment,
    `TABLES: load=ROM40` (before `BANKC0`), `PLxx` after `BANKxx` in every area, `EXTnn` areas for ExLoROM.
  - `build_rom()`: after assembling, `romplace.load(GEN)`, write `build/obj/place.s = stub()`, assemble, link
    (pass 1, no dbg file), `used = area_used(map, cfg)`, `banks` for `$81-$FF` with `long = bank > $9F` (+ ext banks
    `$01..EXT_BANKS-1`, `speed slow`), `place()`, write `place.s`, assemble, link with `--dbgfile` (pass 2), print the
    report, `exlorom()` when `EXT_BANKS`, `fix_checksum()` (both headers for ExLoROM; the part above 4 MB counted as
    mirrored, as in the bgm prototype).
  - `area_used(map, cfg)` and `exlorom(path)` (same as the bgm prototype). Build time 1.1 s (was 1.0 s).
- **`tools/gfx/assets.py` `Assets.write()`**: no bank packing (the `banks` argument is ignored; `make_gfx.py`'s
  `GFX_BANKS` goes away). Writes `gfx/<name>.bin`, `place/gfx.json` (`speed fast` when flags & 3 = LZSS / tile runs,
  else `any`), `gfx.s` = `.import` of every label + `.segment "TABLES"` + `gfx_table` rows
  `.word .loword(l) / .byte <.bank(l), flags / .word size`. `gfx.h` unchanged (verified byte-identical).
- **`tools/make_data.py`**:
  - `write_text()` = `n = tx.write_font()` + new `write_text_tables(n)`: `text.h` (unchanged, verified),
    `text/strings.bin` (all distinct bodies), `place/text.json` (one `whole fast` entry `tx_<k>` per body + `font_glyphs`
    (font.bin), `font_width` (font.bin.wid, **long**), `font_glyphs_s`, `font_width_s` (**long**)), `textdata.s` =
    `.export text_table`, `FONT_DIGIT0`, `.import tx_*`, `.segment "TABLES"`, rows `.word .loword(tx_k) / .byte <.bank(tx_k)`.
    `TEXT_LAST_BANK` (unused) goes away.
  - `build_estats()`: `write_estat(index, data)` writes `estat_index.bin` (**long**: `lda f:estat_index,x`) and
    `estat_data.bin` (via `fc_src`) + `place/estat.json`, and deletes `estat.s`.
- **`tools/make_audio.py`**: `BLOCKS_PER_BANK` and `BGM_BANKS` go away; after writing `bgm.brr` / `spc.bin` it writes
  `place/bgm.json` (the stream entry with `tracks`); `audio.s` keeps the SPC image, `SPC_*` constants and the `se_*`
  tables only (no `bgm_banks` / `bgm_table`). (The bgm group's new track list and `bars` loops go on top of this;
  its `pack_bgm.py` is replaced by the placer's `hook: bgm`, same table format.)
- **asm**: `sound.s` = the bgm group's stream-segment change (`bgm_seg_bank/ptr/len` instead of `bgm_banks`,
  no `BLOCKS_PER_BANK`), and `lda #<.bank(spc_image)`; `text.s` `lda #<.bank(font_glyphs)` / `#<.bank(font_glyphs_s)`;
  `asset.s` `lda #<.bank(estat_data)`. Nothing else reads placed data with `^` or `f:` today (checked: every
  `f:` read in src/asm is a WRAM / ROM00 label, a TABLES table (`gfx_table`, `text_table`, `bgm_table`,
  `bgm_seg_*`), a fixed segment (`se_*` in $FF, `shr_tab`/`shl_tab` in $AF) or a `long` blob (`font_width`,
  `font_width_s`, `estat_index`); the other `^` are ROM00 labels).
- **data/gen** after a regeneration: new `place/*.json`, `text/strings.bin`, `estat_index.bin`, `estat_data.bin`;
  `estat.s` removed; `gfx.s`, `textdata.s`, `audio.s` change format. Commit them as today.

### 1.4 Rules for the other groups

- New art: `Assets.add()` as today — placement is automatic. New binary data of your own: write the file, add a
  manifest `data/gen/place/<yours>.json`, refer to the labels from asm with `.loword()` / `<.bank()`.
  If your asm reads it with `f:label`, mark it `long`. If two blobs must share a bank (one data-bank register for
  both), ship them as one blob with internal offsets.
- Small tables read with `f:` (plan, reco, farm, descriptors): put them in segment `TABLES`, not `BANKC0`.
- Do not add `.segment "BANKxx"` for data any more (the bgm, inventory, forge and boss_fx prototypes do).
- C code banks: any bank `$81-$9F` can hold code now; the data simply moves. Proposed new files (C_BANKS):

  | bank | file(s) | today | + groups (from their specs) | total |
  |---|---|---|---|---|
  | 5 `$85` | battle_view.c | 27,195 | sortie +673, chests ≈ +2.2 K, wings ≈ +0.9 K, boss_fx ≈ +0.4 K | **≈ 31.4 K of 32 K: put new battle code in bank 6 files** |
  | 6 `$86` | battle_ui.c (sortie), battle_fx.c (boss_fx) | 0 | 10.0 K + ≈ 11.6 K | ≈ 21.6 K |
  | 7 `$87` | adventure.c, rewards.c, treasure.c (chests) | 9,680 | chests ≈ 13 K, sortie +0.6 K, records ≈ 0.5 K | ≈ 23.7 K |
  | 8 `$88` | save.c, inventory.c | 3,105 | v2 migration ≈ 0.4 K, sets ops 1.4 K, mirage record 0.7 K | ≈ 5.6 K |
  | 9 `$89` | map.c | 10,033 | sortie +7.8 K, inventory ≈ 1 K, menus/forge ≈ 0.6 K | ≈ 19.5 K |
  | 10 `$8A` | menu.c | 10,727 | menus ≈ 12.7 K (status, keys, records, root, settings), forge 0.3 K, filters 2.2 K | ≈ 25.9 K |
  | 11 `$8B` | gacha.c | 3,708 | chests ≈ 14 K | ≈ 17.7 K |
  | 12 `$8C` | forge.c | 8,963 | forge ≈ 10 K | ≈ 19 K |
  | 15 `$8F` | reco.c (inventory おすすめ) | 0 | 6.5 K | 6.5 K |
  | 16 `$90` | equip.c (inventory: sets, sell, detail) | 0 | ≈ 17.5 K | 17.5 K |

  (`'equip.c': 10` in today's C_BANKS must become 16; bank 16 is no longer BGM once the placer is in.)
  Every non-static function costs 5 B of low WRAM (STUBS); low WRAM has 2,421 B free (CODE 2,314 + STUBS 1,115 +
  LOWBSS 1,830 of 7,680): room for ≈ 480 more.

### 1.5 Measurements (prototype)

- 4 MB, exactly today's content: `placed: bgm 1880.7K, estat 17.4K, gfx 1373.4K, text 55.3K`, free after placing
  **535.8 KB** (all at the top: `$ED` 23.9 KB + `$EE-$FD` whole banks) + 10.6 KB in `$80`. BGM in 60 segments.
  Every bank but `$80`, `$C0` (TABLES) and the top ones ends with ≤ 3 free bytes (`bankmap4.txt`).
- Regression (`scripts/regress.py base.sfc work/build/kishidra.sfc t/reg1.txt t/reg1`): title, map, menu, equip,
  equip list, dex list, dex card (picture + card BGM), stage list, battle (2 frames) — **all screenshots
  byte-identical**. `t/reg2`: title audio 7.5 s from frame 400 and 15 s of battle audio — **byte-identical**. The
  title audio of the first 3.3 s differs (1.29-3.26 s): that is the known boot race between the SE upload and the
  stream (bgm spec §0.6); its position moves with any timing change.
- `textcheck.py` on a PROFILE=1 build over the same screens: **0 findings**.
- 8 MB ExLoROM (`ROM_MB=8`): screenshots identical; audio identical except an 8-sample (0.25 ms) shift
  (stream read from SlowROM banks `$01-$49`; cross-correlation 1.0000, 0 difference after the shift).
- Full load (`scripts/simulate_full.py`: today + every group's art / text / code growth from their specs + the
  bgm group's 28-track stream 5,867,163 B): **8 MB fits, 357 KB spare, BGM 180 segments**; 6 MB 1.67 MB short;
  4 MB 3.73 MB short (consistent with the bgm spec).

### 1.6 Risks

- A blob read with `f:` but not marked `long` breaks only when it lands in `$81-$9F` — maybe after an unrelated code
  change. Mitigation: grep check in build.py: for each `f:<label>` in `src/asm/*.s` and generated asm, the label must
  be in TABLES, a fixed segment, or a `long` manifest entry (≈ 20 lines; recommended).
- Code growth moves data on every build; a ROM built from the same sources is still reproducible (sorting is
  deterministic).
- `PLxx` segments must stay last in their area (`linker_cfg` order); `.assert` in `place.s` fails the build if not.
- 255 stream segments: 180 at the full 8 MB load. `min: 64` blocks keeps tiny chunks out.

### 1.7 Tests

1. `python build.py rom` (4 MB) and `ROM_MB=8 python build.py rom`: both link; the report lists every manifest.
2. `scripts/regress.py` with `t/reg1.txt`, `t/reg2.txt` against the previous ROM: screenshots and late audio identical.
3. PROFILE=1 + `textcheck.py t/tc1.txt`: 0 findings.
4. Put a fake 30 KB `any` blob and a `long` blob in a manifest: map shows the long one in `$A0+`.
5. Every group's own harness tests after rebasing onto the placer.

---------------------------------------------------------------------------------------------------------------

## 2. Font and text capacity

### 2.1 Today

- 1,407 strings = 28,890 B of glyph codes in `$C1` (2 B per code + 2 B end), `text_table` 3 B per string = 4,221 B in
  `$C0`, 705 glyphs: `font.bin` 16,920 (24 B / glyph) + widths 705 + `font_s.bin` 11,280 (16 B / glyph) + widths 705 =
  29,610 B in `$C2` (**3,158 B free = 75 glyphs**).
- `text.s` reads a glyph at `.loword(font_glyphs) + 24·i` (`+16·i` for the 8-dot font) with the bank byte from the
  label; widths with `lda f:font_width,x`. So each glyph table must sit whole in one bank, and the width tables need
  real 24-bit addresses.

### 2.2 New text of all groups (measured, `textcost_all.txt`)

Each group's ui.txt-format file encoded against today's `font.bin.chars` with the port's own `TextDB`:

| group | strings | code bytes | new glyphs (first use) |
|---|---|---|---|
| menus_settings (`new_ui.txt`; 3 change existing ids) | 94 | 2,152 | 組動条件対象討伐主公目説明済書 (15) |
| menus_settings FXS/FXO (`fx_state_gen.py`, 86 effects) | 172 | 1,918 | – |
| inventory (`ui_inventory.txt`) | 156 | 3,032 | 登呼編括却限詳細働違理由雑魚 (14) |
| sortie_battle (`ui_sortie_battle.txt`) | 177 | 3,744 | 再＋落張先断捨状況↑↓待優的狙 (15) |
| sortie TUTX (web `TUTORIAL_TEXT` skip / timing lines) | 12 | 392 | 飛 (1) |
| chests_gacha (`ui_chests_gacha.txt`) | 77 | 2,134 | 超計獲⇒択引作 (7) |
| forge (`forge_strings.json` + `ui_forge.txt` + `fstg.txt`; `{k}` = code 15) | 176 | 5,578 | 埋星同右覧虹 (6) |
| wings_mirage (4 strings) | 4 | 54 | 常 (1) |
| boss_fx, bgm | 0 | 0 | – |
| **total** | **868** | **19,004** (+2,604 table) | **59** |

All 59 new glyphs: `組動条件対象討伐主公目説明済書登呼編括却限詳細働違理由雑魚再＋落張先断捨状況↑↓待優的狙超計獲⇒択引作埋星同右覧虹飛常`
(the sortie spec also lists 絵, which no string of its text file uses: 60 if it stays; 虹 goes if forge adopts `（◆）`).
Identical bodies among the new strings: 1,728 B (stored once by the dedupe).

After all groups: **764 glyphs**: 12-dot 18,336 B + 764, 8-dot 12,224 B + 764 = 32,088 B — that would leave
680 B in a shared bank, so the fonts must not share one bank. Strings: 27,032 B (today, deduped) + ≈ 17.3 KB =
≈ 44.3 KB = two banks; text_table 6,825 B.

### 2.3 Design (prototyped as part of item 1)

- Four font blobs placed separately: `font_glyphs` and `font_glyphs_s` (`whole fast`, anywhere — also the code-bank
  tails), `font_width` and `font_width_s` (`whole fast long`, `$A0+`). Capacity per blob: 1,365 glyphs (12-dot),
  2,048 (8-dot): the font has room for 600 more glyphs.
- Strings: one `whole fast` blob per distinct body (any bank); `text_table` in `TABLES`. No bank limit; a string is
  never split (the placer keeps each body whole). `text_table` index = id·3 (16-bit): up to 21,845 strings.
- `text.s`: only the two `#<.bank()` lines (1.3). No change to drawing, kinsoku or `{s}` nesting.

### 2.4 Text rules for all groups

- **Text id clashes found** (make_data asserts on a duplicate ui.txt key, so the build would stop):
  `T_DT_HEAD` (inventory そうび詳細 / sortie `{s}の詳細`), `T_ST_TAB_0..2`, `T_ST_KEYS`, `T_ST_SP_HEAD`, `T_ST_STATE_3`
  (menus status screen / sortie 状況, different texts). Rename the sortie ones: battle 状況 → `T_SIT_*`
  (`T_SIT_TAB_0 敵`, `T_SIT_TAB_1 武器`, `T_SIT_TAB_2 盾と必殺技`, `T_SIT_KEYS`, `T_SIT_SP_HEAD`, `T_SIT_STATE_3 {yellow}発動中`),
  area / STAGE detail → `T_SD_*` (`T_SD_HEAD {s}の詳細`, ...). Identical duplicates (`T_ST_SP_NONE`, `T_ST_FX_NOWEAPON`,
  `T_ST_FX_NONE`, `T_ST_STATE_1`, `T_ST_STATE_2`, `TXT_BLUE_S`) are defined once (menus / inventory files).
- Split `src/text/ui.txt` into `src/text/ui.txt` + `src/text/ui_<group>.txt` (make_data `build_ui_text` reads
  `sorted(glob('src/text/ui*.txt'))`, the duplicate assertion spans all files): no merge conflicts between groups.
- **Glyph codes must stay stable**: `sv.name` and the new `EquipSet.name` store glyph codes in SRAM. Keep
  `font.bin.chars` append-only (TextDB already does) and never change `GLYPH0 = 16`.
- **Control codes are full**: 1-14 are used, forge takes 15 for `{k}` (third number). A later control code must be an
  escape (15 followed by a sub-code word), not 16 (16 is the first glyph).
- Regenerating text pulls in the newer web build (menus_settings §0.3: gdata.h / tables.c change, `BFX_*` ids shift).
  Do one full regeneration from today's web build **before** the groups' changes land, check it visually, then build
  everything on that.

### 2.5 Tests

1. After regeneration: `text.h` unchanged except the new ids; `textcheck.py` clean on every group's scripts.
2. Draw one string of each new glyph (debug text or a group screen) in both fonts: screenshot.
3. Map check: `font_width`, `font_width_s`, `estat_index` addresses have a bank ≥ `$A0`.

---------------------------------------------------------------------------------------------------------------

## 3. Save data v2

### 3.1 Today (src/c/save.h, save.c, src/asm/sram.s)

SRAM `$70:0000-$7FFF` (32 KB) = two slots `SLOT_A $0000`, `SLOT_B $4000` (16,384 B each), written alternately;
header 12 B (`"KSFC"`, `seq`, `sum` = 16-bit byte sum of the body, `size`, `ver`), body written first, header last;
load = newest valid slot, the other as fallback; only `ninv` items of `inv[]` are written (`body_len`).
`read_slot` accepts only `ver == SAVE_VER (1) && size == sizeof(Save)`: **any new field makes every existing
save "no save"** (the user plays on snes9x with a real save).

v1 layout (cc65 packs structs, little endian), **8,702 B; 7,682 B free per slot**:

| off | field | B | | off | field | B |
|---|---|---|---|---|---|---|
| 0 | magic[4] | 4 | | 110 | equip[4] u16 | 8 |
| 4 | seq, sum, size, ver u16 | 8 | | 118 | mats[16] u16 | 32 |
| 12 | area_open, difficulty, cursor, tickets | 4 | | 150 | found_items[32] | 32 |
| 16 | coins u32 | 4 | | 182 | found_sp[2] | 2 |
| 20 | cleared[17] u32 | 68 | | 184 | found_mons[12] | 12 |
| 88 | name[7] u16 (glyph codes) | 14 | | 196 | kills[94] u16 | 188 |
| 102 | ending_seen, tut_cleared, tut_clears, sort_mode | 4 | | 384 | got[240] | 240 |
| 106 | guard_late, story, bgm_vol, se_vol | 4 | | 624 | mirage_best, mirage_clear | 2 |
| | | | | 626 | mirage_turns u16 | 2 |
| | | | | 628 | ninv u16 | 2 |
| | | | | 630 | inv[1009] × 8 | 8,072 |

Already there (no new field needed): BGM / SE volume (`bgm_vol`, `se_vol`, 0-10, default 8), hero name (`name`),
tutorial (`tut_cleared`, `tut_clears`), key items / badge (derived from `cleared[]`), debug open (`story` bit 4).

### 3.2 New fields of all groups (web `save_data.gd` key → SNES)

| web key | group | SNES field | B |
|---|---|---|---|
| `guides` {lucky_tip, enhance, evolve, enhance_gift, nav_enhance} | sortie, forge, menus | `u8 guides` (one byte for all, bits below) | 1 |
| `solo_tickets` (SOLO_TICKET_MAX) | chests | `u8 solo_tickets` (0..99) | 1 |
| `mirage.run` | sortie | `u8 mirage_run`, `u16 mirage_run_id` + 2 × 512 B records outside `Save` | 3 |
| `mirage.cards` | wings | `u8 mirage_cards[3]` (bit w = WAVE w+1, 18) | 3 |
| `shops_seen` | menus | `u16 shops_seen` (bit = area index; shops are areas 1, 3, 5, 8) | 2 |
| `boss_records` {maou,secret}_{first,last} | menus | `u16 boss_wins[2]`, `BossRec rec[4]` (40 B) | 164 |
| `equip_sets` (EQUIP_SETS 15, name ≤ 8) | inventory | `EquipSet sets[15]` (24 B) | 360 |
| – (port: INV_SOLO cache valid) | inventory | `story` bit 3 `ST_SOLO` (existing byte) | 0 |
| – (port: per-item one-weapon-special cache) | inventory | `Inv.slots` bit 6 `INV_SOLO` (existing byte) | 0 |
| – | (reserve) | `u8 spare[64]`, zero | 64 |

Not stored (not wanted or derived): profile / showcase / friends / online / transfer, ad gacha, gifts, `lr_got`,
`mirage_rec.week*` and gear, sub-world fields, `shops_seen_set` (the migration does its job).

`guides` bits (names `GDB_*`: the guide strings are `GD_*` in text.h, one namespace):
`GDB_LUCKY 1` (lucky_tip), `GDB_ENH 2` (enhance), `GDB_EVO 4` (evolve), `GDB_GIFT 8` (enhance_gift),
`GDB_NAV_FORGE 16` (nav_enhance = the menus group's `nav_seen` bit 0). Bits 5-7 free.
**This replaces** the separate `u8 guides` of forge (bits 1/2/4) and of sortie (bit 0) and menus' `u8 nav_seen`.

### 3.3 v2 layout (offsets; `sizeof(Save)` = 9,300 B, measured in the prototype)

```c
/* save.h (v2) — the v1 part keeps its offsets 0..627 */
#define ST_SOLO 8            /* inventory: the INV_SOLO bits of the bag are up to date */
#define GDB_LUCKY 1
#define GDB_ENH 2
#define GDB_EVO 4
#define GDB_GIFT 8
#define GDB_NAV_FORGE 16
typedef struct {             /* 討伐記録 */
  u8 valid, difficulty;
  u16 turns, boss_turns, nth;
  Inv w[3];                  /* MAIN, SUB, THIRD at the kill; id 255 = empty */
  Inv shield;
} BossRec;                   /* 40 B */
#define EQUIP_SETS 15
#define SET_NAME_MAX 8
#define SET_GONE 0xfffe
typedef struct {
  u16 slot[4];               /* inventory index per EQ_* slot, NO_ITEM empty, SET_GONE gone */
  u16 name[SET_NAME_MAX];    /* glyph codes, 0-terminated when shorter; name[0] == 0: unregistered */
} EquipSet;                  /* 24 B */
#define SAVE_V1_SIZE 8702
#define SAVE_V1_NINV 628
#define SAVE_SPARE 64
typedef struct {
  ... v1 fields unchanged, up to u16 mirage_turns (offset 626) ...
  u8 guides;                 /*  628 */
  u8 solo_tickets;           /*  629 */
  u8 mirage_run;             /*  630 */
  u8 mirage_cards[3];        /*  631 */
  u16 mirage_run_id;         /*  634 */
  u16 shops_seen;            /*  636 */
  u16 boss_wins[2];          /*  638 */
  BossRec rec[4];            /*  642: 0 魔王 first, 1 魔王 latest, 2 裏ボス first, 3 裏ボス latest */
  EquipSet sets[EQUIP_SETS]; /*  802 */
  u8 spare[SAVE_SPARE];      /* 1162 */
  u16 ninv;                  /* 1226 */
  Inv inv[INV_SLOTS];        /* 1228 */
} Save;                      /* 9300 */
```

Compile-time check (cc65 rejects `sizeof` comparisons in array sizes; a division works — verified: a struct of
16,000 B gives `Error: Size of array 'too_big' is invalid`):
`extern char save_fits_slot[0x3E00 / sizeof(Save)];` in save.c (the tower records start at slot + `$3E00`).

`spare`: a later field placed there needs no SAVE_VER bump when 0 is the right value for an older save
(`size` stays 9,300).

### 3.4 Migration (save.c, prototyped and run)

```c
#define SAVE_VER 2
/* header check: 2 = current layout, 1 = a v1 save (migrated by load_slot), 0 = none */
static u8 read_slot(u16 off, Save *hdr_only) {
  ea[0] = (u16)hdr_only; ea[1] = off; ea[2] = HDR;
  sram_read();
  if (memcmp(hdr_only->magic, magic, 4) != 0) return 0;
  if (hdr_only->ver == SAVE_VER && hdr_only->size == sizeof(Save)) return 2;
  if (hdr_only->ver == 1 && hdr_only->size == SAVE_V1_SIZE) return 1;
  return 0;
}
#define V2_BLOCK ((u16)&sv.ninv - (u16)&sv - SAVE_V1_NINV)
static void v1_init(void) {            /* web _seen_init: no NEW flood after the update */
  u8 a;
  for (a = 0; a <= sv.area_open && a < AR_MIRAGE; ++a)
    if (area_has_shop(a)) sv.shops_seen |= (u16)1 << a;
  if (sv.area_open >= AR_PLAIN) sv.guides |= GDB_NAV_FORGE;
}
static u8 load_slot(u16 off) {
  u16 len, n;
  u8 v = read_slot(off, &sv);
  if (!v) return 0;
  ea[0] = (u16)&sv + HDR; ea[1] = off + HDR;
  ea[2] = (v == 2 ? sizeof(Save) : SAVE_V1_SIZE) - HDR;
  sram_read();
  if (v == 1) {
    n = *(u16 *)((u8 *)&sv + SAVE_V1_NINV);
    if (n > INV_SLOTS) return 0;
    ea[0] = (u16)&sv + HDR; ea[2] = SAVE_V1_SIZE - HDR - (INV_SLOTS - n) * sizeof(Inv);
    wram_sum();
    if (ea[3] != sv.sum) return 0;
    memmove(&sv.ninv, (u8 *)&sv + SAVE_V1_NINV, 2 + n * sizeof(Inv));   /* the bag moves up */
    memset((u8 *)&sv + SAVE_V1_NINV, 0, V2_BLOCK);                      /* the v2 block = 0 */
    sv.ver = SAVE_VER;
    sv.size = sizeof(Save);
    v1_init();
    return 1;
  }
  if (sv.ninv > INV_SLOTS) return 0;
  len = body_len();
  ea[0] = (u16)&sv + HDR; ea[2] = len;
  wram_sum();
  return ea[3] == sv.sum;
}
```

`save_exists`, `save_load` and `save_write` are unchanged (`seq` ordering works across versions; the v1 slot stays as
the fallback until the second v2 write). save.c includes `adventure.h` for `area_has_shop`. Per-feature v1 rules
(all already covered by the zeroed block): forge guides show once (guides 0), lucky tip shows once, no boss records
(the records screen says `この記録をつけはじめる前に倒したので、記録はない` for bosses already cleared), no mirage
cards, no suspended run, solo tickets 0, ST_SOLO clear → the inventory group's `inv_solo_all()` on load.
Cost: save.c +≈ 0.4 KB code; WRAM `sv` +598 B (47.4 KB / 54 KB measured).

SRAM map v2 (per slot, base `$0000` / `$4000`): `0..9299` Save, `9300..15871` free (6,572 B), `$3E00..$3FFF` tower
snapshot record (sortie spec: `"KMSN"` header + ≤ 500 B, its own seq / sum / run id).

**Downgrade:** an older ROM sees a v2 save as "no save" (ver 2); starting a new game there overwrites the older
slot. Tell the user to copy the `.srm` before switching ROM versions (README note).

### 3.5 mksave.py (prototyped)

Writes v2 by default; `--v1` writes the old layout (byte-identical to today's mksave output, verified).
New options: `--guides N` (default `0x1f`: every tip seen, so existing harness scripts see no guide / tip),
`--solo N`, `--mirage-cards MASK`, `--shops-seen MASK` (default `0xffff`: no NEW tags). The groups add their own
(`--set N:NAME:MAIN,SUB,THIRD,SHIELD`, `--boss-rec ...`) into the same v2 block. The fixed offsets are asserted
(`len(v2) == 1226 - 628`).

### 3.6 Tests (all run on the prototype)

1. v1 save (`mksave --v1 ... --open 5 --clear-all --coins 4242 --items 3 17 40`) on the v2 ROM: WRAM `sv` (address
   `_sv` from the map, `$7E:80B1` in the prototype) = ver 2, size 9300, v1 part (bytes 12-627) identical to the old
   ROM's `sv`, bag moved to 1228 and identical, block zero except `shops_seen 0x2A` (areas 1, 3, 5) and `guides 16`.
   **Passed.** Starting a battle writes slot B as v2 (seq 2) and keeps slot A v1. **Passed.**
2. Reload that SRAM: loads slot B (v2), the next write goes to slot A as v2 seq 3. **Passed.**
3. Corrupt one byte of the v2 slot: falls back to the v1 slot, migrates, rewrites slot B. **Passed.**
4. `mksave` v2 output loads (`t/mk2`). **Passed.**
5. To add: `textcheck` / screenshots of the title (つづきから offered with only a v1 slot); a v1 save with 999 items
   (the memmove of 8 KB); the 0x3E00 check by temporarily making `spare` 7,000 B (build must fail).

---------------------------------------------------------------------------------------------------------------

## 4. Menu structure

### 4.1 Port today (src/c)

```
title.c   つづきから / はじめから (confirm) -> はじめから: difficulty_select -> map (king_opening, name_entry)
map.c     ←→↑↓ area, A: area_window | mirage_window, Y / X / START: menu, L×3 / R×3: debug
          area_window rows: [アーケディア城: チュートリアル, カカシ] STAGE 1..n, 宝の部屋, 金策, ガチャ (shop), 道具
          A on a STAGE -> sortie (出撃する / 装備をかえる) -> battle
battle    START pause: 続ける / 離脱する -> result_scene (rewards.c) -> map (after_battle: story, orb, ending)
menu.c    装備 / 持ち物 / 鍛冶屋 / ショップ / 図鑑 / 必殺技 / 難易度 xx / 設定 (ガードの遅れ補正) / タイトルへ
debug.c   (R×3) give item / all items / all gear / dex / open all
```

### 4.2 Web (shipped map.gd, menu_screens.gd, profile_screens.gd)

```
nav bar   マップ | 装備 | 鍛冶屋 (dim until 平原の村, NEW) | ショップ (NEW, the newest shop town) | 図鑑 | フレンド | その他
その他    ステータス | 大切なもの | プロフィール (tabs プロフィール / 討伐記録) | 難易度：xx | 設定 | 引き継ぎ | (デバッグ)
title     はじめから / つづきから / 設定
鍛冶屋    強化 / 進化 / 持ち物 (ForgeScreens kinds enhance / evolve / items) + 使い方
ショップ  ガチャ tab (通常, ピックアップ, 広告, 必殺10連 when tickets) / 売る / 盾 / 道具
area      STAGE list + 詳細 + sortie (おすすめ / そうび変更 / 開始); result: 再挑戦, 売却モード, N・R 一括売却
battle    pause 続ける / 状況 / 離脱 (tower: やめる / セーブして中断); long press = info popups
```

### 4.3 Proposed SNES tree (owner group in brackets; everything not wanted stays out)

```
title.c   つづきから / はじめから / 設定 [menus]                       (↑↓ over 3 rows; 設定 without the rename row)
map.c     unchanged keys; help line + NEW!! when forge_new() || shops_new() [menus]
  area_window   rows as today; ガチャ row NEW!! [menus]; X = STAGE の詳細, Y = エリアの詳細 [sortie]
                (月見の島 before its clear: no X / Y)
    sortie      出撃する / 装備をかえる / おすすめ [inventory] (+ おすすめの理由 block)
  mirage_window 続きから（WAVE n / m） / はじめから (confirm) [sortie]
battle    chest row + opening after a win [chests]; pause 続ける / 状況 / 離脱する [sortie];
          tower: やめる / セーブして中断 [sortie]; Pekorata entrance, idle poses, petting [boss_fx]
  result  treasure view: A つぎへ, X くわしく, Y 売却モード (X N一括 / Y R一括 inside), R 再挑戦 [chests + sortie + inventory]
menu.c    (one column, win_open(BX_LIST, 8, 2, 16, 25), lh 16, rows built per open)
  1 装備          slots + 5th row セット装備 -> sets_scene [inventory]
  2 持ち物        filters / compare / そうび詳細 (X) / まとめて売る (START) [inventory]; item actions 鍛冶屋 gray until open [menus]
  3 鍛冶屋        gray + notice until 平原の村, NEW!! [menus]; root 強化・進化する品を選ぶ / 進化できる品だけ / 素材 / 使い方 / もどる,
                  auto guides, farm jump to the map [forge]
  4 ショップ      NEW!! [menus]; gacha presentation + もう1回, 必殺10連 panel when solo_tickets > 0 [chests]
  5 図鑑          count colours 銅・銀・金・虹 [menus]; mirage card toggle A [wings]
  6 ステータス    renamed from 必殺技: tabs ステータス / 必殺技 / 特殊効果 [menus]
  7 大切なもの    4 orbs + ウサギのバッヂ [menus]
  8 討伐記録      only when 魔王 or 裏ボス is beaten (web: a tab of the unwanted プロフィール) [menus]
  9 難易度 xx
 10 設定          BGM の音量 / 効果音の音量 / ガードの遅れ補正 / 名前の変更 [menus]
 11 タイトルへ
debug.c   + 地上クリア, スキル編集 [menus]
```

11 rows × 16 px from y 8: the last row at y 168, its text ends at 180 (inner limit 192). Rows that may be absent (討伐記録) are skipped when the
row array is built; `menu_text[]` and the switch use row ids, not positions.

### 4.4 Key map (all screens; README controls + the groups' additions)

| screen | keys |
|---|---|
| title | ↑↓ row, A / START decide |
| map | ←→↑↓ area, A open, Y / X / START menu, L×3 / R×3 debug (unchanged) |
| area window | A decide, B back, **X STAGE の詳細, Y エリアの詳細** |
| sortie | ←→ / ↑↓ buttons, A, B |
| battle | Y / X / A skills, B guard, R meat, ←→ target, START pause, **SELECT (hold) info**, **L pet ペコラータ** |
| battle chest choice (tower) | ↑↓, A (unchanged) |
| result / gacha result | A つぎへ, X くわしく, Y 売却モード, R 再挑戦 / もう1回, START = A |
| item lists | L / R type tab, Y 必殺 filter, SELECT sort, X くわしく, START まとめて売る (sell list: 全部外す) |
| item detail | L / R page, ←→ next item, START ♥, A / B close |
| forge pages | START とばす (guide waiting), Y 最大 (count picker) |
| dex | L / R tab, SELECT kind, A くわしく; card: ←→ next, A 蜃気楼 / 通常 |
| status / records / keys | L / R tab, ↑↓, A card, B back |
| settings | ↑↓ row, ←→ value |
| name entry | A input, B erase, L / R page, START decide, SELECT cancel (rename / set names) |

**Clash resolved:** the boss_fx spec pets Pekorata with **L**; the sortie spec opens the battle info with
"L or SELECT (hold)". Final: **SELECT (hold) = info, L = pet** (L does nothing against other enemies; the info
window then never opens while a pet is counted). Both specs must be changed to this.
No other clash: Y / X in the area window are free (the menu keys work on the map only), R on the result is free.

---------------------------------------------------------------------------------------------------------------

## 5. Shared risks the integrator must watch

1. **WRAM `$7E`** (C BSS + RODATA): 47,894 B used of 55,040 (`$2000-$F6FF`), 7,146 B free. Claims: menus ≈ 0.6 K,
   sortie ≈ 0.72 K, chests ≈ 1.2 K, forge ≈ 0.6 K, inventory ≈ 0.65 K (+1.9 K overlaid on `order[]`), boss_fx ≈ 1.26 K,
   wings ≈ 0.4 K, save v2 0.6 K → ≈ 6.0 KB: **≈ 1.1 KB left**. Overlay screen-local buffers (one `union` of the
   screens' statics; dex `pdesc/pmap`, forge lists, reco scratch, held[]) and keep big scratch in `$7F`.
2. **CODE5** (battle_view.c) at ≈ 31.4 of 32 KB with the claims: new battle code goes into bank 6 files.
3. **Regeneration**: one full `python build.py data` + `audio` from today's web build first (gdata.h ids shift,
   e.g. `BFX_CHAMPAGNE`), checked visually, before any group lands.
4. Every group prototype that wrote `.segment "BANKxx"` for data, a separate `u8 guides`, or the old save layout
   must be rebased onto 1.3 / 3.3.

## 6. Question for the user

- 戦闘中のボタン: 「SELECT を押している間 = 敵・技・勇者の情報」「L = ペコラータをなでる」に分けます（ほかの敵には
  L は何もしない）。この割り当てでよいですか。
