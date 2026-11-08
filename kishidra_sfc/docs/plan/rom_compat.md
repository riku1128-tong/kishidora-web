# ROM expansion compatibility (group `rom_compat`)

Scope: which >4 MB cartridge layout the SNES port of キシドラ！ should use so that the 20 new surface
BGM tracks (all surface tracks except `07_worldmap`) plus the current ones fit, which emulators and
flash carts run each layout, and what the port has to change.  No game rule of the web build is
involved (the web build has no ROM limit); this group only provides the space.

Everything below was checked in source code (emulators, flash-cart firmware/FPGA) and, where an
emulator is installed here, by running ROMs in the harness.  Web pages of krikzz.com, nesdev and
FuSoYa could not be opened from this session (hosts not allowed); for those, search-result snippets
are quoted and marked as such.

---------------------------------------------------------------------------------------------------

## 0. Decision

| target | layout to use | why |
|---|---|---|
| **snes9x only** (the user's emulator) | **ExLoROM 8 MB** | Works in snes9x 1.54 to master (desktop 1.63, the harness core 1.61, libretro "Snes9x", snes9x2010, snes9x2005). Smallest change to the port (LoROM stays LoROM; prototype done and passes every harness script). Room for 34 s loops (today's length) for every track. |
| **FXPak Pro / SD2SNES only** | **ExLoROM 8 MB** (same ROM) | Firmware v1.8.0 and later (current v1.11.2) map LoROM > 4 MB as ExLoROM; SRAM at $70:0000. |
| **Super EverDrive X5 / X6** | **ExHiROM ≤ 7.5 MB** (6 MB is the safe choice) | Cart limit 7.5 MB (krikzz spec, via retailer/search). ExLoROM support is not documented anywhere reachable; ExHiROM is the layout of commercial games (Tales of Phantasia 6 MB). Needs the HiROM refactor (§5). Untested on the cart: run `probe/exhirom6.sfc` first (green = OK). |
| **emulators only (snes9x + bsnes/ares + Mesen2)** | ExHiROM 8 MB | Mesen2 (latest master) has no ExLoROM mapping. |
| **everything** (snes9x, bsnes/ares, Mesen2, FXPak Pro, MiSTer, Super EverDrive X5/X6, ZSNES) | ExHiROM 6 MB (7.5 MB if the EverDrive loads it) | Broadest, but loops shrink to ≈ 20 s (6 MB) / ≈ 31 s (7.5 MB) and the port needs the HiROM refactor (effort XL). |

**Recommendation now: ExLoROM 8 MB** (`ROM_LAYOUT=exlorom8`).  It is the layout snes9x runs best,
it also runs on FXPak Pro/SD2SNES, MiSTer and bsnes/ares, and it costs the port about one day.  It
does **not** run on Mesen2 or on a Super EverDrive; if the user wants either, switch later to ExHiROM
(§5) — nothing done for ExLoROM is wasted (the bank table, the converters' bank lists and the harness
checks carry over).

---------------------------------------------------------------------------------------------------

## 1. How much music fits (per layout)

BGM need (sizes from the BGM group's measured loops, `plan/bgm/tableC.md`, 11008 Hz BRR, 9-byte
blocks; loops only, no intros, as the port stores them today):

* fixed part: 16_boss_secret full 117 s = 717,876 B (kept as is), 05 victory 12 s = 74,304 B,
  06 gameover 9 s = 55,728 B → **847,908 B (828.0 KB)**
* 25 loop tracks (20 new: 03 04 09 10 11 12 14 15 17 18 19 20 21 22 23 24 25 26 27 49; 5 current:
  01 02 08 13 50):

| loop window | 25 loops | BGM total |
|---|---|---|
| 34 s (today's cap) | 4459.1 KB | **5287.2 KB** (5.16 MB) |
| 24 s | 3172.1 KB | 4000.1 KB |
| 20 s | 2570.7 KB | 3398.7 KB |
| 16 s | 2052.9 KB | 2880.9 KB |
| 12 s | 1587.0 KB | 2415.0 KB |

Space (measured on today's `build/kishidra.sfc` + map): BGM banks 59 × 32 KB, 1880.7 KB used;
whole ROM 545.3 KB free (code banks $81-$8F 340.7 KB free incl. empty $86/$8F, GFX banks 27.8 KB,
$9D-$9F 96 KB, text 17.4 KB, $AF 28 KB, SPC 17.5 KB, engine 10.6 KB).  The other groups add about
300-350 KB (≈ 250 KB of art: wing flaps 64 KB + mirage cards 79 KB + boss effects 31.5 KB + chests,
menus, forge…; ≈ 60-80 KB of code; some text).  Art must sit in data banks, not in code-bank tails.
The BGM stream stores 3640 blocks (32,760 B) per bank.

| layout | extra banks | BGM banks possible (with ≈ 300 KB new art placed) | BGM capacity | loop window that fits |
|---|---|---|---|---|
| LoROM 4 MB (today) | 0 | ≈ 50 | ≈ 1.6 MB | ≈ 5 s (useless) |
| ExLoROM 6 MB | $01-$3F (63; $00 is the vector copy) | ≈ 108 | ≈ 3.45 MB | **≈ 20 s** |
| ExHiROM 6 MB | 2 MB (64 × 32 KB windows) | ≈ 110 | ≈ 3.5 MB | ≈ 20-21 s |
| ExHiROM 7.5 MB | 3.5 MB | ≈ 158 | ≈ 5.05 MB | ≈ 31 s |
| **ExLoROM 8 MB** | $01-$7D (125) | **170** ($02-$7D, $E0-$FD, $90-$9F) | **5438.7 KB** | **34 s for every track, 151 KB spare** |
| ExHiROM 8 MB | ≈ 4 MB − 64 KB | ≈ 170 | ≈ 5.4 MB | 34 s |

With ExLoROM 8 MB the art still gets $B0-$BF (512 KB, freed by the BGM move) plus bank $01
(32,688 B) on top of today's 27.8 KB: ≈ 572 KB for new art, about twice what the groups ask for.

---------------------------------------------------------------------------------------------------

## 2. Compatibility matrix

Legend: **yes (run)** = ROM run here in the harness; **yes (src)** = read in the source at the commit
given; **no** = fails (run or src); **?** = could not be verified.

| target (version) | ExLoROM 6 MB | ExLoROM 8 MB | ExHiROM 6 MB | ExHiROM 8 MB | source |
|---|---|---|---|---|---|
| snes9x 1.61 libretro (harness core, Debian `libretro-snes9x 1.61+dfsg-1`) | **yes (run: game + probe)** | **yes (run: game + probe)** | **yes (run: probe)** | **yes (run: probe)** | `Map_JumboLoROMMap` / `Map_ExtendedHiROMMap` printed by the core |
| snes9x 1.54 … 1.63 … master (desktop, libretro `Snes9x - Current`) | yes (src) | yes (src) | yes (src) | yes (src) | snes9xgit/snes9x memmap.cpp, identical `Map_JumboLoROMMap` in 1.54, 1.61, 1.63, master 1bcc369 (2026-09-24); libretro/snes9x fae2fea (2026-09-19) the same |
| snes9x 1.50 – 1.53 | yes (src) | **no** ($40-$7D mirror $00-$3F) | yes (src) | yes (src) | tag 1.53 memmap.cpp: `map_lorom_offset(0x40, 0x7f, …, CalculatedSize - 0x400000, 0x400000)` |
| libretro snes9x2010 (fe690dd, 2026-09-21) | yes (src) | yes (src); **6 MB max in the Wii/GameCube build** (`HW_DOL`: `MAX_ROM_SIZE 0x600000`) | yes | yes (not on HW_DOL) | src/memmap.c `MAP_JUMBOLOROMMAP`, src/memmap.h |
| libretro snes9x2005 (a79dfe9, 2026-09-26) | yes (src) | yes (src) | yes (src) | yes (src) | source/memmap.c `JumboLoROMMap`, `MAX_ROM_SIZE = 0x800000` |
| bsnes v115 master (78474e5, 2026-10-07) | yes (src) | yes (src) | yes (src) | yes (src) | bsnes/heuristics/super-famicom.cpp L46-56 (ExLoROM header 0x407FB0 +4), L255-279 (board); target-bsnes/resource/system/boards.bml L706-744 (`EXHIROM-RAM`, `EXLOROM-RAM`) |
| ares master (dd73766, 2026-11-06) | yes (src) | yes (src) | yes (src) | yes (src) | mia/medium/super-famicom.cpp L236-244, L558-582; ares/System/Super Famicom/boards.bml L701-732 (same boards) |
| bsnes-mercury accuracy (v094 fork, `094+git20220807`, 2nd core installed here) | **no (run)** | **no (run)** | **yes (run: probe)** | **yes (run: probe)** | probe: $80:8100 read file block 128, $C0-$FF open bus; game: black screen, silence |
| higan | ? (discontinued; ares is its successor and supports both) | ? | yes (ToP) | ? | — |
| Mesen2 master (b9fa69d, 2026-06-04 — latest; `git fetch` returns the same head) | **no (src)** | **no (src)** | yes (src) | yes (src) | Core/SNES/BaseCartridge.cpp L181-226: header bases {0,0x200,0x8000,0x8200,0x408000,0x408200}, no 0x400000; the ExLoRom flag (map mode $22) is only logged (L803-810); `RegisterHandlers` L461-463 maps both $00-$7D:8000 and $80-$FF:8000 from ROM page 0 → banks $00-$7D show the first 4 MB. ExHiROM L486-505. |
| ZSNES 1.51 | no | no | yes (48 Mbit) | no | FuSoYa Lunar Expand page (search snippet): "ZSNES does not support ExLoROM or any ROM that is larger than 48 Mbits" |
| **FXPak Pro / SD2SNES** (firmware ≥ v1.8.0; current v1.11.2, master b081ff9 2026-11-01) | **yes (src)** | **yes (src)** | yes (src) | yes (src) | CHANGELOG v1.8.0 "add support for ExLoROM (LoROM > 32 Mbits)"; src/smc.c L41 header list, L63-94 (best score, ties → later candidate), L133-293 (map $20 → mapper 1 LoROM, $25 → mapper 2 ExHiROM), L348-439 (score, +4 above 4 MB); verilog/sd2snes_base/address.v L196-208: LoROM ROM addr = `{0, ~A23, A22:16, A14:0} & ROM_MASK`, ExHiROM `{0, !A23, A21:0}`; SRAM LoROM $70-$7D/$F0-$FF:0000-7FFF (ROM ≥ 4 MB), HiROM/ExHiROM $20-$3F/$A0-$BF:6000-7FFF; src/memory.c L383-400 ROM mask from header size doubled to the file size. Same `sd2snes_base` FPGA for Mk2 (Xilinx .ucf) and Mk3/FXPak Pro (Quartus .qsf). Max 8 MB with these mappers. |
| MiSTer SNES core (SNES_MiSTer 3ac236a 2026-11-05, Main_MiSTer cc47bad) | yes (src) | yes (src) | yes (src) | yes (src) | rtl/chip/DSP/DSP_LHRomMap.vhd L189-190 `CART_ADDR <= '0' & not CA(23) & CA(22..16) & CA(14..0)` (LoROM/ExLoROM), L232-233 ExHiROM; support/snes/snes.cpp L113-127 headers 0x7FC0/0xFFC0/0x40FFC0, L204-214 ROM size from the file size |
| Super EverDrive X5 / X6 (OS 1.05) | ? (no documentation found; community posts call ExLoROM unofficial) | **no** (7.5 MB max) | **probably** (commercial ExHiROM: Tales of Phantasia 6 MB, Daikaijuu Monogatari II 5 MB) | **no** (7.5 MB max) | krikzz X5 product page / retailer listings via search: "max ROM size up to 7.5MByte", save RAM up to 128 KB |
| Super EverDrive v1 / v2 (legacy) | ? | no | v1: 48 Mbit (6 MB) max (VGCollect listing via search) | no | — |
| Super EverDrive "X7" / "Pro" | — | — | — | — | no such SNES product found (X7 and Pro exist for N64 / NES / Mega Drive only) |

Notes:
* The game itself (not just a probe) was run as ExLoROM 6 MB and 8 MB in the harness core with the
  BGM stream moved to banks $02-$3C / $40-$7A and GFX banks (including the asset table) moved to
  $01, $3D-$3F / $01, $7B-$7D: 61 scripts of the other groups (86,628 frames = 24 min of play per ROM),
  **323 of 323 screenshots byte-identical** to the 4 MB build, WAV RMS equal, the music identical
  sample for sample after a 3-8 sample (0.1-0.25 ms) shift (slow-ROM reads move the APU handshake a
  little).  SRAM dumps identical.
* bsnes v115/ares and Mesen2 could not be built here (building outside code was not permitted), so
  they are covered by source reading plus `scripts/romcheck.py` (a Python port of each detector,
  which agrees with the two installed cores on every probe ROM).

---------------------------------------------------------------------------------------------------

## 3. Layout reference

### 3.1 ExLoROM (recommended)

**Memory map** (bsnes/ares `EXLOROM-RAM`, snes9x `Map_JumboLoROMMap`, sd2snes/MiSTer LoROM mapper):

| CPU address | file offset | speed |
|---|---|---|
| $80-$FF:8000-FFFF | 0x000000-0x3FFFFF (bank & $7F) × 32 KB — **exactly today's 4 MB LoROM** | FastROM with MEMSEL ($420D) = 1 |
| $00-$3F:8000-FFFF | 0x400000-0x5FFFFF | always SlowROM (2.68 MHz, 8 master cycles) |
| $40-$7D:8000-FFFF | 0x600000-0x7EFFFF (8 MB file only) | SlowROM |
| $7E-$7F | WRAM; file 0x7F0000-0x7FFFFF is not reachable (pad with $FF) | — |
| $70-$7D:0000-7FFF (+ $F0-$FF:0000-7FFF) | battery SRAM (32 KB, linear at $70:0000-7FFF) | — |
| $40-$7D:0000-7FFF | snes9x: a mirror of the upper half; bsnes/ares: open bus → **never use** | — |
| $C0-$FF:0000-7FFF | snes9x: mirror; bsnes/ares EXLOROM: not mapped → **never use** (the port does not today) | — |

Formula used by the tools: `file = (bank & 0x7F) * 0x8000 + (addr & 0x7FFF) + (0x400000 if bank < 0x80 else 0)`.

**Vectors and header.**  The CPU fetches the reset/NMI/IRQ/BRK/COP vectors from $00:FFE0-FFFF =
file 0x407FE0, and the handlers start in bank $00 (PBR = 0) = file 0x400000+.  The port's handlers
(`STARTUP` segment in crt0.s: `reset`, `nmi_vec`, `nmi_dummy`/`irq_dummy`, `irq_prof`, `brk_trap`)
are at the start of bank $80 and jump to bank $80 at once (`jml reset_fast`, `jml nmi_fast`) or use
long addressing only.  So **bank $00 = a byte copy of bank $80** (file 0x000000-0x007FFF copied to
0x400000-0x407FFF after linking): identical code, header and vectors in both places.  Cost 32 KB.

Header bytes (both copies, file 0x7FB0 and 0x407FB0):
* $FFD5 map mode = **$30** (LoROM + FastROM).  Never $32/$22: bsnes/ares read $22/$32 as S-DD1,
  snes9x excludes $4332/$4532 from the extended format, sd2snes treats $22 with cart type $43/$45 as S-DD1.
* $FFD6 = $02 (ROM + RAM + battery), $FFD7 = **$0D** (8 MB class; a 6 MB ROM also uses $0D),
  $FFD8 = $05 (32 KB SRAM), $FFD9 = $00, $FFDA = $33, $FFB0-$FFBF maker/game code in ASCII ("LP", "KSDR")
  (snes9x takes 1 point off for non-ASCII).
* Checksum: snes9x/NSRT mirror sum: a power-of-two size = plain byte sum; 6 MB = sum(0-4 MB) +
  2 × sum(4-6 MB).  Write checksum and complement into **both** copies.  (Prefill both with
  $FFFF/$0000 before summing; the 4 bytes of each copy always sum to $1FE, so the result does not
  depend on the order.)  Only detection scores and snes9x's "bad checksum" message use it.
* **file 0x40FFB0-0x40FFFF = zeros** ($01:FFB0-FFFF) and **file 0xFFB0-0xFFFF = zeros**
  ($81:FFB0-FFFF): the reset vector there reads $0000, so no detector can take random data there for an
  ExHiROM / HiROM header (sd2snes gives ties to later candidates and +4 above 4 MB; MiSTer, Mesen2,
  bsnes and sd2snes all reject a reset vector < $8000).  Cost 2 × 80 B.

**How each detector picks ExLoROM** (and what the prototype ROM scores, `romcheck_results.txt`):
* snes9x: file > 4 MB and the map/type pair not SuperFX/SA-1/S-DD1/SPC7110 → "extended".  It scores
  LoROM/HiROM at 0x7F00/0xFF00 and again at +0x400000; if the +4 MB scores are ≥ the normal ones →
  BIGFIRST (file used as is), else SMALLFIRST (**the halves are swapped**).  Then LoROM if lo ≥ hi →
  `Map_JumboLoROMMap`.  So the header copy at 0x407FB0 is required.  Prototype: lo 11, hi −2, BIGFIRST.
* bsnes/ares: scores 0x7FB0, 0xFFB0, 0x407FB0, 0x40FFB0 (opcode at the reset vector, checksum, mode
  bonus); the two Ex candidates get +4; header at 0x407FB0 with mode $30 → board `EXLOROM-RAM`.
  Prototype: lo 14, exlo 16.
* sd2snes/FXPak Pro: scores {0xFFB0, 0x101B0, 0x7FB0, 0x81B0, 0x40FFB0, 0x4101B0} (ties → later,
  +4 above 4 MB); map $20 → LoROM mapper, which is ExLoROM by its address formula; ROM mask = header
  size ($0D → 8 MB).  It reads the header at **0x7FB0**.  Prototype: 21 at 0x7FB0, everything else ≤ 1.
* MiSTer: headers 0x7FC0/0xFFC0/0x40FFC0 → type 0 (LoROM/ExLoROM), size from the file.
* Mesen2: picks the LoROM header at 0x7FB0 and maps it as plain LoROM → the extended banks read the
  first 4 MB.  Not supported.

### 3.2 ExHiROM (fallback for Mesen2 / EverDrive)

| CPU address | file offset | speed |
|---|---|---|
| $C0-$FF:0000-FFFF | 0x000000-0x3FFFFF (64 KB banks) | FastROM |
| $80-$BF:8000-FFFF | upper halves of the same (file bank × 0x10000 + 0x8000) | FastROM |
| $40-$7D:0000-FFFF | 0x400000-0x7DFFFF | SlowROM |
| $00-$3F:8000-FFFF | upper halves of 0x400000-0x7FFFFF ($3E/$3F reach file 0x7E8000/0x7F8000) | SlowROM |
| $A0-$BF (and $20-$3F):6000-7FFF | SRAM, 8 KB per bank: $A0:6000 = 0x0000, $A1:6000 = 0x2000, $A2 = 0x4000, $A3:7FFF = 0x7FFF | — |

* Header only at file **0x40FFB0** ($00:FFB0), map mode **$35** (ExHiROM + FastROM), $FFD7 $0D,
  $FFD8 $05; vectors at 0x40FFE0; reset code at $00:8000+ = file 0x408000+.  Mesen2 needs the header
  at 0x40FFB0 (a $25/$35 header at 0xFFB0 takes its HiROM branch first).
* SRAM: use $A0-$A3 only.  Mesen2 maps $80-$BF:6000-7FFF and, with "strict board mappings", not
  $20-$3F; bsnes/ares mask=$E000 on 20-3F,a0-bf; snes9x `map_HiROMSRAM` 20-3F,A0-BF; sd2snes
  `{A20:16, A12:0}`; all agree that $A0:6000 is SRAM offset 0 (probe: 5 bytes at 4 windows correct in
  both installed cores).
* Detection: snes9x scores +3 for $FFD7 = 13 on > 4 MB and +4 for > 3 MB on HiROM → BIGFIRST
  ExtendedHiROM (probe: lo −3, hi 15); bsnes/ares header 0x40FFB0 +4 → `EXHIROM-RAM`; sd2snes 25 at
  0x40FFB0 → mapper 2; Mesen2 base 0x408000 → ExHiRom; MiSTer 0x40FFC0 → type 2.

### 3.3 Other layouts considered

* "LoROM with banks $40-$7D / $C0-$FF:0000-7FFF, no mapper": not extra storage.  In a 4 MB LoROM
  those addresses are mirrors of the upper halves (or open bus on bsnes); 4 MB is the LoROM ceiling
  because the board ignores A23.  Any > 4 MB LoROM must decode A23, and that decode is ExLoROM.
* SA-1 (Super MMC, up to 8 MB in 1 MB pages): supported by snes9x, bsnes, Mesen2, FXPak Pro, MiSTer;
  **not** by Super EverDrive X5/X6.  It moves SRAM to BW-RAM and changes the whole map; no gain over
  ExLoROM for this port.
* MSU-1 (BGM as `<rom>-N.pcm` files, 44.1 kHz stereo, outside the ROM): FXPak Pro, snes9x (has
  msu1.cpp since 1.55-ish; present in 1.61), bsnes/ares, Mesen2.  Full-length tracks, ROM stays 4 MB,
  but needs dozens of extra files next to the ROM, is not on Super EverDrive X5/X6, and still needs
  the SPC700 music as the fallback.  Not recommended for this request.
* S-DD1 / SPC7110 mappers: no.

---------------------------------------------------------------------------------------------------

## 4. Port changes for ExLoROM 8 MB

The prototype is `work/` (a copy of the port) + `proto_build.patch`.  `python build.py rom` with no
`ROM_LAYOUT` still gives a 4 MB LoROM (84 bytes differ from today: the two 80-byte zero areas and the
checksum).

### 4.1 build.py (prototype done; ≈ 70 lines)
* `ROM_LAYOUT = os.environ.get('ROM_LAYOUT', 'lorom')` — make the default `exlorom8` once the BGM needs
  it.  `XBANKS` = [] / $00-$3F / $00-$7D; `ROM_SIZE` = 4/6/8 MB.
* `linker_cfg()`: after the 128 LoROM areas, one area per extended bank:
  `XROMnn: start=$nn8000, size=$8000, fill=yes, fillval=$ff, file=%O;` and segment
  `BANKnn: load=XROMnn, type=ro, optional=yes;` (nn = 01…7D; **no segment for bank $00**).
  Bank $01: `XROM01 size=$7fb0` + `XHDR01: start=$01ffb0, size=$50, fill=yes, fillval=$00`.
  Bank $81: `ROM01 size=$7fb0` + `HDR81: start=$ffb0, size=$50, fillval=$00, bank=$81` (both layouts).
  No bank attribute on XROMnn, so `^label` gives the real bank ($01-$7D) and `.loword(label)` $8000+.
* `finish_rom()` (replaces `fix_checksum`): pad to `ROM_SIZE` with $FF; assert $81:FFB0 zeros; if
  extended: assert bank $00 empty, set $FFD7 = $0D, copy file[0:0x8000] → file[0x400000:0x408000],
  assert $01:FFB0 zeros; mirror-sum checksum into the header at 0x7FC0 (+0x407FC0).
* `usage()`: "ROM used … / 8192 KB".
* Add (not in the prototype): fail the build if a `CODEn`/`ENGn` segment or `STARTUP`/`ENGINE` is in
  banks $00-$7D (they cannot be by construction; keep the assert for future edits), and if two bank
  lists of §4.2 overlap.

### 4.2 tools/banks.py (new) — one bank table for every tool
```python
LAYOUT  = os.environ.get('ROM_LAYOUT', 'exlorom8')
EX      = {'lorom': [], 'exlorom6': range(0x01, 0x40), 'exlorom8': range(0x01, 0x7E)}[LAYOUT]
ENGINE  = [0x80]                         # STARTUP/ENGINE/header; copied to $00 in ExLoROM
CODE    = range(0x81, 0x90)              # C code (C_BANK_LAST may drop to 15: $90-$9F go to BGM)
TEXT    = [0xC0, 0xC1, 0xC2, 0xC3, 0xC4] # estat, strings, font (today $C0-$C2 + spill; room to grow)
SHIFT   = [0xAF]                         # text.s shift tables (4 KB) — never GFX
SPC     = [0xFE, 0xFF]
BGM     = [b for b in EX if b >= 0x02] + list(range(0xE0, 0xFE)) + list(range(0x90, 0xA0))   # 170 banks
GFX     = list(range(0xC5, 0xE0)) + list(range(0xA0, 0xAF)) + list(range(0xB0, 0xC0)) + [0x01]
CAP     = {0x01: 0x7FB0}                 # usable bytes when not 0x8000
def file_offset(bank, addr): return (bank & 0x7F) * 0x8000 + (addr & 0x7FFF) + (0x400000 if bank < 0x80 else 0)
```
(For `lorom` keep today's lists.)  This also removes two existing traps: `make_gfx.GFX_BANKS` today
lists $AF (text.s) and $B0-$BF (BGM), and the text/font can spill into $C3 where the GFX table lives.
Assets in GFX banks $C3/$C4 move to the new GFX banks (`gfx_table` is read with long addressing, so
its bank is free; it was moved to $7B in the prototype without any code change).

### 4.3 make_audio.py
* `BGM_BANKS = banks.BGM` (stream in $02-$7D first, then $E0-$FD, $90-$9F).  The stream is read only
  by `sound.s` through `bgm_banks` (1 byte per bank) and `[s_ptr]` long reads, so SlowROM banks are
  fine: 103 B per frame × 2 extra master cycles = 0.06 % of a frame (measured: battle profile
  `send_block` 16.6 % → 16.8 %, within noise).
* Capacity 170 × 32,760 B = 5,569,200 B (5438.7 KB) ≥ 5287.2 KB for 34 s windows.  Keep the
  `nbanks > len(BGM_BANKS)` error.  If the BGM group adds a per-bank length table, bank $01 (32,688 B)
  can join the stream; otherwise leave $01 to GFX.
* `SPC_BANKS` unchanged ($FE-$FF).

### 4.4 make_gfx.py and tools/gfx/assets.py
* `GFX_BANKS = banks.GFX`; `Assets(prefix, banks, cap=banks.CAP)`: start with
  `free = {b: cap.get(b, 0x8000) for b in banks}` and place at offset `cap.get(b, 0x8000) - free[b]`
  (today `0x8000 - free[b]`).  Nothing else: the table stores `.loword(lab)` and `^lab`.
* DMA from $00-$7D:8000-FFFF is as fast as from FastROM (DMA is always 8 cycles/byte).  Tile-run and
  LZSS unpacking read the source with the CPU: about 1/3 slower per source byte in SlowROM; only at
  load time.  Rule for the art groups: things read by the CPU every battle frame (none today) belong in
  $80-$FF; anything DMA'd can go anywhere.

### 4.5 make_data.py
* Text banks from `banks.TEXT`; check at build time that the last text/font bank < `banks.GFX[0]`
  (today the font silently spills into $C3).  Text stays in $C0-$C4 (FastROM, `textwidth.py` reads the
  generated asm, not the ROM, so it needs no change).

### 4.6 65816 code
* `src/asm/sound.s`: **no change** (bank crossing takes the next entry of `bgm_banks` and restarts at
  $8000 — right for every ExLoROM bank).  `s_bi` is one byte: up to 256 banks.
* `src/asm/sram.s`, `src/c/save.c`, `save.h`: **no change** (SRAM stays $70:0000-7FFF, 32 KB, 2 slots).
* `src/asm/crt0.s`: no change (vectors come from the bank $00 copy; `STARTUP` must stay at the start
  of bank $80 and keep using `jml` / long addressing — add a comment saying so).
* `src/asm/header.s`: comment "ExLoROM 8MB: $FFD7 is set to $0D by build.py".  (Or
  `.ifdef EXROM` `.byte $0d`.)
* `src/asm/asset.s`, `text.s`: no change (long addressing; `^label` banks).
* Never place code in $00-$7D (SlowROM; $00 is a copy).

### 4.7 Harness
* `tools/harness/runrom`: no change (tested with 6/8 MB).  `textcheck.py`, `prof.py`, `mksave.py`:
  no change (tested: PROFILE=1 ExLoROM 8 MB build, textcheck 0 findings on the same tour as the 4 MB
  build; prof.py runs).
* New `tools/harness/romcheck.py` (= `scripts/romcheck.py`): prints the layout snes9x, bsnes/ares,
  sd2snes/FXPak and Mesen2 would pick.  Run it after every build: it must say JumboLoROM BIGFIRST /
  EXLOROM-RAM / LoROM mapper mask $7FFFFF.
* New `tools/harness/probe/` (= `scripts/mkprobe.py`): tiny ExLoROM/ExHiROM 6/8 MB ROMs that check 7-13
  bank reads and the SRAM windows and turn the screen **green (pass) or red (fail)** — for trying an
  emulator or a flash cart before loading the game.
* New `tools/harness/abtest.py` (= `scripts/regress.py` + `regcmp.py`): run a set of scripts on two
  ROMs and compare every screenshot; WAV by RMS.
* Any script that turns a bank into a file offset (the survey's `bankuse.py`, `rom_free.py`) must use
  `banks.file_offset`.

### 4.8 README
ROM line: "ExLoROM / FastROM / 8MB / SRAM 32KB".  Add the compatibility list of §2 (works: snes9x
1.54+, bsnes v115+, ares, FXPak Pro/SD2SNES fw 1.8.0+, MiSTer; does not work: Mesen2, Super EverDrive,
ZSNES, snes9x 1.50-1.53, Wii/GameCube snes9x).

### 4.9 Effort and cost
* Effort **M** (about one day): build.py S (done), banks.py + converters S, harness S, data rebuild and
  full A/B test S.  Everything else is the BGM/art groups' own work.
* ROM: +32 KB (bank $00 copy) + 160 B (two zero areas) of overhead; **adds 4000 KB**
  ($01-$7D) of data space.  WRAM 0, VRAM/OAM 0, SRAM 0, CPU ≈ 0.

---------------------------------------------------------------------------------------------------

## 5. ExHiROM fallback (only if Mesen2 or a Super EverDrive is wanted)

What changes (effort **XL**, 3-5 days + full retest):
1. build.py: 64 KB HiROM banks.  Code banks become the upper halves $81-$9F:8000-FFFF (file
   n × 0x10000 + 0x8000); every data bank now in $C0-$FF:8000 must move to a lower half
   $C0-$FF:0000-7FFF (base **$0000**) or to an upper half $A0-$BF:8000.  Extended data in $40-$7D
   (64 KB) or 32 KB windows.  Reset/vectors/STARTUP copy at file 0x408000 / header at 0x40FFB0.
2. Converters: every "$8000 + offset" becomes "window base + offset" (assets.py, make_audio.py
   `pos()`, `SPC_SPLIT_END`, make_data.py text pointers).
3. sound.s: bank crossing restarts at the window base, not $8000 (L101-105 SPC upload, L421-428
   stream); the SPC image split.
4. sram.s: SRAM in four 8 KB windows $A0-$A3:6000-7FFF (split `sram_read/write/wram_sum` loops at
   8 KB; `save.c` unchanged).
5. Harness: file offsets, romcheck, probe.
Sizes: 6 MB → ≈ 20 s loops; **7.5 MB** (largest that fits the EverDrive limit) → ≈ 31 s; 8 MB → 34 s
(no EverDrive).  The 7.5 MB file has not been tried on an EverDrive.

---------------------------------------------------------------------------------------------------

## 6. Test plan (for the implementer)

1. `python build.py rom` (lorom) → 4,194,304 B; `ROM_LAYOUT=exlorom8 python build.py rom` →
   8,388,608 B.  `python tools/harness/romcheck.py build/kishidra.sfc` → snes9x "JumboLoROM, BIGFIRST",
   bsnes "EXLOROM-RAM", sd2snes "LoROM mapper … mask $7FFFFF".
2. Byte checks (in finish_rom, plus a test): file[0x400000:0x408000] == file[0:0x8000]; headers at
   0x7FB0 and 0x407FB0 equal; 0xFFB0-0xFFFF and 0x40FFB0-0x40FFFF all zero; mirror-sum checksum
   matches; the map file shows BGM segments in BANK02… and no CODE/ENG segment below $80.
3. A/B regression: build the same data/gen as lorom (BGM forced back into $B0-… for the A side, or
   simply the old commit) and exlorom8; run every script in `plan/*/t/*.txt` (abtest.py) → all
   screenshots identical, WAV RMS within 2 %, SRAM dumps identical.  (Measured with the prototype:
   323/323 identical, 4/4 WAV.)
4. Music: for each of the 25 loop tracks + 16, play it from the sound test / dex (dex has the battle
   songs) or a battle, record 2 loops with `wav`, check the loop point is seamless and that the track
   that sits across a bank boundary ($7D → $E0, $FD → $90) plays through (decode the WAV and compare
   with `tools/audio/brrdec.py` output of the same blocks).
5. Saves: write a save on exlorom8, dump SRAM, load it into a 4 MB build (same `save.h`) → same
   progress (SRAM layout unchanged).
6. PROFILE=1 exlorom8 build: `textcheck.py` on all scripts → no new findings; `prof.py` battle 900
   frames → within 1 % of the 4 MB build.
7. Probe ROMs on the user's setup (snes9x desktop and, if present, FXPak Pro): `exlorom8.sfc` must turn
   green.  bsnes-mercury must turn it red (shows the probe works).
8. Real FXPak Pro (optional, by the user): firmware ≥ 1.8.0, load the game, play the title, a battle,
   save, power off, load.

---------------------------------------------------------------------------------------------------

## 7. Evidence produced here

* `probe/` probe ROMs + `probe/results.txt`: snes9x 1.61 passes ExLoROM 6/8 MB and ExHiROM 6/8 MB
  (all bank reads and SRAM windows); bsnes-mercury fails ExLoROM 6/8 ($80 → file 4 MB, $C0-$FF open
  bus) and passes ExHiROM 6/8.
* `exlorom6.sfc`, `exlorom8.sfc` (game, first prototype) and `exlorom6z.sfc`, `exlorom8z.sfc`,
  `base4z.sfc` (final prototype with the zero areas), `base4.sfc` (today's ROM).
* `regress/compare.txt`, `regress/compare_z.txt`: A/B results (315/315 and 323/323 screenshots).
* `t/`: title/map/battle A/B run, WAV lag analysis (`scripts/wavlag.py`: +3…+8 samples, residual 0),
  mercury screenshots (`t/compare.png`: ExLoROM title + battle in snes9x, black in mercury).
* `t/tc/prof_base4.txt`, `t/tc/prof_exlorom8.txt`: battle CPU profiles (same within 1 %).
* `romcheck_results.txt`: detector simulation for all ROMs.
* `src/`: sparse clones used for the source reading (sd2snes, snes9x tags 1.53/1.54/1.61/1.63, libretro
  snes9x/snes9x2010/snes9x2005, ares, MiSTer core + Main, Lil' Manster README showing a 6 MB ExLoROM
  translation patch in use).

## 8. Risks

* Mesen2 users and Super EverDrive owners cannot run an ExLoROM build (they get wrong music or a crash,
  not a clear message).  Mitigation: README list; keep `ROM_LAYOUT=lorom` buildable (music cut to fit)
  or do §5.
* snes9x picks SMALLFIRST (halves swapped) if the 0x407FB0 header copy is missing or scores lower than
  random data at 0xFFB0/0x40FFB0 → guarded by the copy and the two zero areas; romcheck in the build.
* Someone later puts code or a CPU-read hot table in $00-$7D → slower (SlowROM).  Assert in build.py.
* Someone edits `STARTUP` to use absolute (DBR-relative) ROM reads before `jml` → still fine because bank
  $00 is a full copy of bank $80; only data in other banks would differ.
* The 8 MB file needs 8 MB of cart RAM: FXPak Pro/SD2SNES (16 MB) and MiSTer are fine.
* EverDrive and higan facts are not from primary documents (sites blocked); verify with the probe ROMs.

## 9. Sources

Source code (read at the commits given in §2):
* https://github.com/snes9xgit/snes9x/blob/1bcc369e89f08243e0a462882fb1f3e42e51de3a/memmap.cpp
  (LoadROMInt L1350, ScoreHiROM L1075, ScoreLoROM L1129, map_LoROMSRAM L2626, map_HiROMSRAM L2640,
  Map_JumboLoROMMap L2782, Map_ExtendedHiROMMap L3120, Checksum_Calculate L3259); tags 1.53, 1.54,
  1.61, 1.63 (memmap.h MAX_ROM_SIZE 0x800000 → 0xC00000 in 1.63)
* https://github.com/libretro/snes9x (fae2fea), https://github.com/libretro/snes9x2010 (fe690dd,
  src/memmap.c, src/memmap.h HW_DOL), https://github.com/libretro/snes9x2005 (a79dfe9, source/memmap.c)
* https://github.com/bsnes-emu/bsnes (78474e5) bsnes/heuristics/super-famicom.cpp,
  bsnes/target-bsnes/resource/system/boards.bml
* https://github.com/ares-emulator/ares (dd73766) mia/medium/super-famicom.cpp,
  ares/System/Super Famicom/boards.bml
* https://github.com/SourMesen/Mesen2 (b9fa69d) Core/SNES/BaseCartridge.cpp,
  Core/SNES/MemoryMappings.cpp
* https://github.com/mrehkopf/sd2snes (b081ff9, v1.11.2) CHANGELOG, src/smc.c, src/memory.c,
  verilog/sd2snes_base/address.v; https://raw.github.com/mrehkopf/sd2snes/v1.8.0/CHANGELOG
* https://github.com/MiSTer-devel/SNES_MiSTer (3ac236a) rtl/chip/DSP/DSP_LHRomMap.vhd, SNES.sv;
  https://github.com/MiSTer-devel/Main_MiSTer (cc47bad) support/snes/snes.cpp
* https://github.com/Miacidae/Lil-Manster-Public README (uses "48 Mbit ExLoROM 6 MB 2" from Lunar Expand)
Web (search snippets only; pages not reachable from this session):
* https://krikzz.com/our-products/cartridges/spedx5.html (Super EverDrive X5: max ROM 7.5 MB),
  https://stoneagegamer.com/super-everdrive-x5-base-gray.html, https://dragonbox.de/en/everdrives-flashcarts/snes-super-everdrive-x6
* https://krikzz.com/pub/support/super-everdrive/x-series/OS/ (OS up to v1.05, 2021-03-31)
* https://fusoya.eludevisibility.org/le/index.html (snes9x 1.39a+ 48 Mbit ExLoROM / 64 Mbit ExHiROM,
  1.50-1.53 ExLoROM limited to 48 Mbit, ZSNES no ExLoROM)
* https://forums.nesdev.org/viewtopic.php?t=14808 (ExLoROM/ExHiROM address mapping thread)
* https://www.timeextension.com/guides/best-flash-carts-everdrive-carts-and-odes,
  https://vgcollect.com/item/40022 (Super EverDrive v1 48 Mbit)
