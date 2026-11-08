# Wing flaps and mirage dex cards (group `wings_mirage`): implementation spec

Port: `/home/user/kishidora-web/kishidra_sfc` (read-only). Web source: the decompiled
`scratchpad/src/tuning.gd`, `battle.gd`, `ui/fighter.gd`, `ui/pose_tween.gd`, `ui/dex_card.gd`, `ui/card_view.gd`,
`adventure.gd`, `save_data.gd`, `debug_screens.gd`. Everything below was checked against that source and the port
code; every number marked "measured" was produced by the scripts in `scratchpad/plan/wings_mirage/scripts/` with the
port's own converter (`tools/make_gfx.py`, a copy in `scratchpad/plan/wings_mirage/work`) or by running ROMs built from
that copy with `tools/harness/runrom` (snes9x libretro core).

Units: web pixels x 0.73 = SFC pixels (`make_gfx.SCALE`); web seconds x 60 = frames.

---------------------------------------------------------------------------------------------------

## 0. Summary

| # | item | ROM (asset banks) | code | WRAM ($7E C data) | other |
|---|---|---|---|---|---|
| 1a | Wing flaps, petit zako MON_001-007 (OBJ) | **+37.7 KB** measured (flap strips stored raw so they are DMA'd from ROM with no unpack; +21.5 KB if stored as tile runs, but that costs +2.8 % lag, measured) | ~0.5 KB C + 24 B asm | +312 B (`zdesc` 3x96 -> 3x200) +45 B Actor | OAM <= 9 per petit (4 today) |
| 1b | Wing flaps, 13 dragon bosses (BG1) | **+26.4 KB** measured net (+44.3 KB of idle sets minus 18.0 KB of the four `idle2` poses Safari never shows when it flaps) | ~0.8 KB C + ~140 B asm | ~20 B | 2.3 KB in bank $7F ($7F:C000-$C8FF) |
| 2 | Dex mirage cards, 18 surface bosses | **+79.4 KB** measured (+18 B table, ~90 B text, 1 new glyph 常) | ~0.6 KB C | ~25 B | SRAM +3 B |
| | total | **~143.5 KB** | **~2 KB** | **~0.4 KB** (+104 B shared with boss_fx for `bdesc`) | |

Naive conversion (each flap pose converted like today's boss poses and uploaded with today's code) would cost
118.5 KB for the dragons alone and **46 % lag** in a dragon fight (measured); the design below costs 26.4 KB and
**0.0 % lag** (measured with a prototype of its expensive path).

No new screens. New UI text: 4 strings + 1 changed string; glyph 常 (for 通常) is the only new glyph.

---------------------------------------------------------------------------------------------------

## 1. Item 1: Wing-flap idle animation (petit monsters and dragon bosses)

### 1.1 Web rules (tuning.gd lines 288-317, battle.gd `_set_idle` 1257-1300, ui/fighter.gd `_process`, `set_pose`, `_frame_start`, ui/pose_tween.gd `loop_frame`)

Frame tables, `[pose, seconds, lift]`:

```
FLAP_PETIT = [["flap_up", 0.19, 0.0], ["flap_mid", 0.05, 0.02], ["flap_down", 0.11, 0.04], ["flap_mid", 0.07, 0.03]]
FLAP_STONE = [["flap_up", 0.26, 0.0], ["normal", 0.08, 0.015], ["flap_down", 0.16, 0.03], ["normal", 0.22, 0.015]]
FLAP_HOTARU (MON_243-248, underground only: not ported)
boss_flap(has_up): FLAP_STONE if has_up, else FLAP_STONE with "flap_up" replaced by "normal"
ENEMY_IDLE_FRAMES = { MON_001..MON_007: FLAP_PETIT, MON_089, MON_090, MON_091, MON_093, MON_094: FLAP_STONE, (MON_243-248 HOTARU) }
IDLE_FRAMES_SWAY = 0.4   IDLE_LIFT_RATE = 25.0   BOSS_IDLE_SLOW = 1.3 (slows breath/sway/float only, not the frames)
```

`_set_idle(f, e)`:
1. `frames = ENEMY_IDLE_FRAMES.get(e.mon, [])`; if empty and the fighter has `flap_down` -> `boss_flap(has flap_up)`;
   if still empty and it has `idle2` -> `[["normal", half, 0], ["idle2", half, 0]]` (half = race breath period x slow / 2).
2. Frames whose pose the fighter lacks are dropped; the table is used only if >= 2 frames remain.
3. With frames: `f.idle_frames = frames`, `f.frame_time = f.idle_time = randf() * 10` (random phase per enemy),
   `lift_rate = 25`, breath/sway amplitudes x 0.4 (all flapping races have amplitude 0, so nothing changes).
   **When idle frames exist, `idle2` is never shown.**

`Fighter._process` while `pose == "normal"` and no tween is running:
* `fr = loop_frame(idle_frames, frame_time)` (cycle by time), `_show(fr[0])` (instant texture swap, no tween poses).
* lift target = `fr[2] * pose_size("normal").y` (a fraction of the normal pose's height), smoothed every frame:
  `_lift = lerp(_lift, target, 1 - exp(-25 * delta))`, `position.y -= _lift` (the whole fighter, HUD included).
* Not idle (attack, hurt, guard, defeat ...): target 0.
* `set_pose("normal")` after another pose: `frame_time = _frame_start("normal")` = the start time of the first
  "normal" entry: FLAP_STONE 0.26 s; boss_flap(no up) 0; FLAP_PETIT has no "normal" entry -> 0 (flap_up).

Which monsters flap (from `assets/mobs_hd/mobs_hd.json` + `extra.json`, measured with `scripts/flap_list.py`):

| monster | kind | poses | table | made by |
|---|---|---|---|---|
| MON_001 プチドラ | zako | flap_up/mid/down | FLAP_PETIT | redrawn frames |
| MON_002 プチトマト, 003 プチタマ, 004 プチッコリ, 005 プチェロン, 006 プチャーク | zako | flap_up/mid/down | FLAP_PETIT | `made: make_flaps` (001's wings on each body; body pixels identical to normal) |
| MON_007 ゴールデンプチ | lucky | flap_up/mid/down | FLAP_PETIT | make_flaps |
| MON_089 翼龍ドラゴス, 091 水龍ピチャルス, 093 光龍キラノス, 094 闇龍ヤミジャス | boss | flap_up, flap_down | FLAP_STONE | make_flaps (body identical) |
| MON_090 炎龍ゴーゴモス | boss | flap_up, flap_down | FLAP_STONE | redrawn |
| MON_092 樹龍ワシャラス, 097 熱龍マグードワイバル, 098 光龍ゴルデピカライア, 099 奇龍ナーゾキーモス, 102 魔龍ドットアーク, 119 魔王最終形態 | boss | flap_up, flap_down | boss_flap(true) = FLAP_STONE | redrawn |
| MON_100 魔王のペット, 114 龍騎士マーゴス | boss | flap_down only | boss_flap(false): normal 0.26, normal 0.08, flap_down 0.16, normal 0.22 | redrawn |

### 1.2 How the web makes the flap frames
They are pre-drawn PNGs in the web build (`assets/mobs_hd/<mon>_flap_up|flap_mid|flap_down.png`, 4x scale,
anchors in the json). The runtime only swaps textures on the timings above and lifts the fighter. Some frames were
generated offline by the developer's `make_flaps` tool (wings composited on the normal body: body pixels identical,
`scripts/flapdiff.py`: e.g. MON_089 flap_up 11,248 of 11,895 shared opaque pixels identical); the others are full
redraws in which almost every body pixel differs slightly (median colour difference 9-17 of 765 summed RGB,
`scripts/flapdiff2.py`), so on the SFC they share no tiles with the normal pose unless the converter locks the body
(1.4.2).

### 1.3 SFC timing (60 fps)
Boundaries at the rounded cumulative times:

| program | frames per segment (total) | codes | restart after another pose |
|---|---|---|---|
| PETIT | flap_up 11, flap_mid 3, flap_down 7, flap_mid 4 (25) | 18, 19, 20, 19 | frame `slot * 8` (see below) |
| STONE (with up) | flap_up 16, normal 4, flap_down 10, normal 13 (43) | 18, 0, 20, 0 | frame 16 (web `_frame_start`) |
| STONE (no up: 100, 114) | normal 16, normal 4, flap_down 10, normal 13 (43) | 0, 0, 20, 0 | frame 0 |

Lift (px up, integer, the steady state of the web's smoothing, `scripts/lift.py`; the converter computes it per
monster from h = round(normal height x 0.73)):
* PETIT h 45: `1 1 0 0 0 0 0 0 0 0 0 0 1 1 1 1 1 2 2 2 2 2 2 1 1` (h 43, MON_006: one frame later).
* STONE h 95: `1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 1 2 2 2 3 3 3 3 3 3 3 2 2 2 2 2 2 2 1 1 1 1 1 1`.

Phase: the web picks a random phase per enemy; the SFC starts zako slot s at frame `s * 8` of the 25 (and restarts
there after an attack/hurt) so that three petits never open their wings together: this keeps the scanline sprite load
within the PPU limit (1.6.4). Bosses start at frame 0 (web: random; one boss per field).

### 1.4 Data conversion (`tools/make_gfx.py`)

`POSE_CODE` += `'flap_up': 18, 'flap_mid': 19, 'flap_down': 20` (boss_fx takes 7-17 for Pekorata's rest poses).

#### 1.4.1 Petits (`monsters()`, zako branch, keys `mon_001`..`mon_007`)
* Poses: `normal, attack, hurt, defeat` (as today; **drop `idle2`**, never shown when flap frames exist) + `flap_up,
  flap_mid, flap_down`, all at k = 0.73 (today's k for these seven; the scale loop must test only the base poses).
* One palette over all 7 poses (as `sprite_set` does).
* Base poses: unchanged (`cells_of` 32x32 cells, tile runs).
* Flap poses: **mixed cover** of the opaque 8x8 tiles (grid = the pose's bbox origin) with 32x32 cells and 8x8 pieces
  inside one 64-tile strip. Exhaustive search (`scripts/proto_flap.py mixed_cover`, < 1 s for all 21 poses):
  candidate big cells = every 4x4-tile window (offsets -3..rows/cols) covering >= 6 opaque tiles; choose 0-3
  non-overlapping big cells; the remaining opaque tiles are 8x8 pieces; constraint `16 * big + small <= 64`;
  minimise `(max(max_slivers_per_tile_row, budget), big + small, total slivers)` with budget
  **flap_up 8, flap_mid 10, flap_down 9** (8 = today's 2x2 cells; 10/9 = the exact visible width).
  Big cells take strip slots 0..big-1 (tiles `slot*4 + c + 16*r`); 8x8 pieces fill the free tiles of the remaining
  slots in order.
  Measured result (all seven alike): flap_up 3 big + 1-2 small (8 slivers/line, 4-5 OAM), flap_mid 3 big + 5-6 small
  (10, 8-9 OAM), flap_down 3 big + 3-4 small (9, 6-7 OAM); 49-54 tiles of 64.
* **Flap strips stored raw** (`self.a.add(name, strip_2048_bytes, compress=False)`, asset flags 0), so
  `pose_tiles()` DMAs them straight from ROM (1.5.4). Measured cost of the alternative (tile runs + unpack): 3 zako
  changing pose at the PETIT rate in the money stage raised lag from 1.6 % to 4.4 %; without the unpack: 0.0 %.
* Descriptor (`MON_%03d_DESC`): as today, plus
  * piece byte 3 (`cell`): **bit 7 = 8x8 piece, bits 0-5 = tile number inside the strip**; bit 7 clear = 32x32 cell (today);
  * pose byte 3 (`nstrips`): **bit 7 = raw asset (DMA from ROM)**;
  * byte 0 (`npose`): **bit 7 = an idle program follows the poses** (tail below).
  Measured sizes 132-141 B before the tail.
* Idle-program tail (petits 52 B, bosses 88 B): `ncyc` (25/43), `restart` (0 / 16 / 0), `ncyc` pose codes (one per
  frame: 18/19/20/0), `ncyc` lift values (u8, px).

#### 1.4.2 Dragons (`monsters()`, boss branch, the 13 keys with `flap_down`)
* **Idle set** = normal + flap_up (if any) + flap_down on **one canvas**: paste the native images so the anchors
  coincide (canvas = union of the extents around the anchor), scale each with `G.scale` on that same canvas size
  (identical resampling where the pixels are identical).
* Grid: 8-px grid with the **phase of the normal pose's own bbox**, extended by whole tiles to cover the wings
  (`scripts/proto_flap.py dragon`). Each idle pose keeps **its own sub-rect** on that grid (W, H, anchor) so the dex,
  which centres the picture by W, shows normal exactly as today.
* Palettes: the 2 palettes are fitted on the base poses (normal from the common canvas + attack, hurt, defeat,
  special, guard), not on the flap poses (same rule as boss_fx uses for Pekorata); flap tiles map to them. The dark
  aura's 8 tinted palette versions (boss_fx) therefore also fit the flap frames.
* **Body lock**: a flap tile whose alpha mask equals normal's tile at the same grid position and whose mean colour
  distance (`G._dist`, 2dr²+4dg²+3db² per pixel) is <= **800** (about 9 of 255 per channel, below one RGB555 step on
  average) takes normal's tile. Then tiles are deduplicated with flips as `boss_set` does.
  Escalation: if `N_idle + max_action_tiles > 448` the converter retries the boss with limits 1500 and 3000; if it
  still overflows it marks the boss **`wings_volatile`** (1.5.3). Measured: MON_097 fits at 3000 (274 + 174 = 448);
  MON_098 stays volatile (310 at 800 / 303 at 3000, + 165).
* Assets: `BOSS_%03d_NORMAL_T` = **T0** (normal's tiles, tile 0 = blank, LZSS), `BOSS_%03d_WINGS_T` = **T1** (flap-only
  tiles, numbered on from T0, LZSS), one map per idle pose (`_NORMAL_M`, `_FLAP_UP_M`, `_FLAP_DOWN_M`, LZSS,
  words reference T0 / T0+T1 numbering), actions as today. **Drop `idle2`** for 089/091/093/094.
* Descriptor (`BOSS_%03d_DESC`, 11 B per pose as today): normal: `ta = T0, ntiles = n0`; flap poses: code 18/20,
  `ta = T1`, `ma` its map, its own W/H/anchor, `ntiles = n0 + n1`. Byte 0 bit 7 + the 88-B tail as for petits; one
  extra tail byte `flags` (bit 0 `wings_volatile`).
* Previews of every converted idle pose to `build/preview/boss_<n>_<pose>.png` (as `preview_bg` does for BGs)
  (`scripts/proto_flap.py out` already writes them; checked: `out/flap_sheet.png`).

Measured idle sets (`scripts/proto_flap.py`, body lock 800):

| boss | ROM now -> new | T0 + T1 = idle tiles | grid | max action pose | fits 448? |
|---|---|---|---|---|---|
| MON_089 翼龍ドラゴス | 28,030 -> 26,451 (-1,579) | 136 + 73 = 209 | 16x13 | 154 | yes |
| MON_090 炎龍ゴーゴモス | 23,129 -> 26,193 (+3,064) | 140 + 77 = 217 | 16x13 | 155 | yes |
| MON_091 水龍ピチャルス | 27,861 -> 26,459 (-1,402) | 136 + 78 = 214 | 16x13 | 161 | yes |
| MON_092 樹龍ワシャラス | 24,088 -> 27,242 (+3,154) | 143 + 81 = 224 | 17x13 | 170 | yes |
| MON_093 光龍キラノス | 28,974 -> 27,404 (-1,570) | 140 + 77 = 217 | 16x13 | 169 | yes |
| MON_094 闇龍ヤミジャス | 27,666 -> 26,060 (-1,606) | 138 + 73 = 211 | 16x13 | 152 | yes |
| MON_097 熱龍マグードワイバル | 25,895 -> 30,543 (+4,648) | 167 + 123 = 290 (274 at lock 3000) | 19x13 | 174 | at lock 3000 |
| MON_098 光龍ゴルデピカライア | 24,293 -> 29,224 (+4,931) | 149 + 161 = 310 | 23x13 | 165 | **no: volatile** |
| MON_099 奇龍ナーゾキーモス | 22,816 -> 27,313 (+4,497) | 122 + 125 = 247 | 19x13 | 149 | yes |
| MON_100 魔王のペット | 24,984 -> 26,820 (+1,836) | 125 + 48 = 173 | 15x12 | 188 | yes |
| MON_102 魔龍ドットアーク | 18,644 -> 23,201 (+4,557) | 113 + 131 = 244 | 20x12 | 132 | yes |
| MON_114 龍騎士マーゴス | 29,788 -> 31,478 (+1,690) | 182 + 38 = 220 | 18x13 | 191 | yes |
| MON_119 魔王最終形態 | 21,882 -> 26,025 (+4,143) | 124 + 116 = 240 | 14x13 | 161 | yes |
| **13 dragons** | 328,050 -> 354,413 **+26,363 B** | | | | |

(Map bytes above are for full-grid maps; per-pose sub-rect maps are a little smaller, so these are upper bounds.)
Petits, measured: 31,957 -> 69,616 B (**+37,659**) with raw flap strips; 53,445 (+21,488) with tile runs.

### 1.5 Engine changes

#### 1.5.1 `src/asm/sprite.s` `meta_add` (+~24 B)
After loading the piece's `cell` byte: if bit 7 is set, `o_ta = (cell & $3f) + ea2 | ea8`, `o_bits = 0` (small),
`jsr put_spr`; else today's 32x32 path. (`dex.c pic_sprites` gets the same 2-line test.)

#### 1.5.2 New asm (`src/asm/bossmap.s`, segment ENGINE, bank $80 has 10.6 KB free; ~140 B)
* `void boss_blit(void)`: ea0 = source map address, ea6 = its bank ($7E or $7F), ea1 = destination in `bg1map`,
  ea2 = columns (clipped), ea3 = rows (clipped), ea4 = source stride (bytes), ea5 = tile add. Clears `bg1map` rows 2-15
  (448 words, `stz` loop, DB = $7E), then copies the rows: `w ? w + add : 0`. The measured prototype
  (`bossmap.proto.s`, reading $7E) took 4.0 % of the samples in a fight with 4 changes per 43 frames and no lag.
* `void asset_where(void)`: ea0 = asset id -> ea0 = address, ea1 = bank, ea2 = stored size (wraps `asset_find`).

#### 1.5.3 `src/c/battle_view.c` (bank $85; ~1.3 KB; keep it lean: the bank has ~5.5 KB left and boss_fx adds hooks)
* Buffers: `zdesc[3][96]` -> `[3][200]`; `bdesc[96]` -> `[200]` (shared with boss_fx); `npose = d[0] & 0x7f`;
  `parse_desc` stops at the tail and sets `a->idle = (d[0] & 0x80) ? d + o : 0`.
* `Actor`: `pcode[7]` -> `pcode[21]` (pose index per code, `pose_fill` loops 0..20; codes >= 7 fall back to 0 in
  `pose_find`); new `u8 pf, busy, lift;` and `const u8 *idle`.
* **Idle choice in `animate()`** (replaces `else want = pose_by_code(a, (ph & 64) ? 1 : 0);`, and is the "idle"
  case in boss_fx's boss branch):

  ```c
  else if (a->idle && (!a->boss || idle_ready)) {        /* Tuning.FLAP_PETIT / FLAP_STONE */
    const u8 *t = a->idle; u8 n = t[0];
    if (a->busy) { a->busy = 0; a->pf = t[1] + (a->boss ? 0 : a->slot * 8); while (a->pf >= n) a->pf -= n; }
    else if (++a->pf >= n) a->pf = 0;
    want = pose_by_code(a, t[2 + a->pf]);
    a->lift = t[2 + n + a->pf];
  } else { want = pose_by_code(a, (ph & 64) ? 1 : 0); a->lift = 0; }
  ```
  and every non-idle branch sets `a->busy = 1; a->lift = 0;`. At stage start `busy = 1` (petit slot phases 0/8/16).
* Zako: `draw_meta(..., FOOT_Y + 4 - a->lift, ...)`. Nothing else changes for zako (`a->cur = want`, tiles go with the
  OAM in `pose_tiles`).
* **`pose_tiles()`**: if the pose's `nstrips & 0x80` (raw): `asset_where(rd16(d+1))`,
  `spr_put(k, OBJ_VRAM(STRIP_ZAKO + slot), ea[0], (u8)ea[1], 2048)` (DMA from ROM, no unpack); the room test is the
  same `2048 + 80`. Tile-run poses as today.
* **Boss idle set** (only bosses with `a->idle`; one boss at a time, globals `idle_state, n0, n_idle, wings_bad,
  idle_map_at[3]`):
  * VRAM (BG1 tiles; HUD keeps 0-63): **T0 at 64..64+n0-1, T1 at 64+n0..64+n_idle-1** (resident for the whole
    fight); **action poses top-aligned at 512 - ntiles** (one action slot instead of buffers A/B). Non-flapping
    bosses keep the A/B scheme unchanged (Pekorata etc.).
  * Loader (`load_stage_gfx`, the form swap `EV_FORM_SWAP` and the mirage next wave all call one `boss_load(k)`):
    unpack T0 -> $7F:A000, `queue_vram_big(64*16, ...)`; `idle_state = 1`. In `animate()`: state 1 and
    `!vq_busy(0xa000, 0xc000)` -> unpack T1 -> $7F:A000, queue to `(64+n0)*16`, state 2; state 2 and not busy ->
    state 3 (`idle_ready`). Normal is shown from state 2 (its map blitted when T0 has arrived), flap frames from
    state 3. Idle maps: `unpack(ma, 0xC000 + k * 0x300)` once at load (3 x 768 B, $7F:C000-$C8FF; checked unused in
    battle: $7F:A000-$BFFF is the tile buffer, $7F:D000 the map buffer, $7F:DA00-$DFFF and $7F:E000-$FFFF are the
    PROFILE debug rings / text-check log, so $7F:E000 must **not** be used).
  * `boss_upload(k, p)` for a flapping boss:
    * p is an idle-set pose (code 0/18/20): if `p` is a flap pose and (`idle_state < 3` or `wings_bad`) -> show normal
      instead. Else **no tile upload**: `boss_show(p)` = blit its cached map (bank $7F) with add 64, `a->cur = p`.
    * p is an action pose: if the shown pose is an action pose, or it is a flap pose and `512 - ntiles(p) < 64 + n_idle`,
      first `boss_show(normal)` and return (the next frame uploads; at most 1-2 frames of normal between two actions,
      e.g. hurt -> defeat). Else unpack its tiles -> $7F:A000, queue to `(512 - ntiles)*16`, map -> `boss_map`,
      `boss_pending = 1` (map blitted when the tiles are in, as today). If `512 - ntiles < 64 + n_idle` -> `wings_bad = 1`.
    * Returning to idle with `wings_bad` (only `wings_volatile` bosses, i.e. MON_098): show normal, unpack T1 and
      queue it again; when it is in, `wings_bad = 0` and flapping resumes. CPU like today's pose change (one LZSS
      unpack of ~4 KB), only after MON_098's actions.
  * `boss_place()` keeps its position maths and calls `boss_blit()` (no C loops; measured: the C loops alone gave
    31.7 % lag at the flap rate). For idle-set poses `boss_top` and boss_fx's `FxGeo` keep the **normal** pose's rect
    (the HP bubble and effect anchors do not jump with the wings; web: the HUD is placed from `pose_size("normal")`).
  * **Lift via HDMA** (table 1, BG1 scroll), only while an idle-set pose shows: lines 0..127-L: VOFS = by + L;
    lines 128-L..127: VOFS = 136 (BG1 row 0, always empty in battle; otherwise BG1 row 16, the panel's HP bar, would
    show in the last L lines of the field); line 128+: 0 as today. The table is rebuilt only when L, bx or by change
    (<= 6 times per 43-frame cycle). `hdma_tab1` is 32 B (sfc.h), enough for the extra 5-byte segment. BG1 row 0
    is cleared by `sys_init()` and nothing in battle writes rows 0-1 (`boss_place` clips at row 2, the HUD uses
    rows 16-27).
* Visual deviations from Safari (all small): the overlay HP bar / count bubble of zako do not bob with the 1-2 px
  lift; phases are spread evenly instead of random; MON_119's 6-px RC_MAOU float is still not ported (a separate,
  pre-existing gap; the same HDMA lift path would make it cheap).

#### 1.5.4 `src/c/dex.c`
`pose0()` masks `pdesc[0] & 0x7f`; `pic_sprites()` handles cell bit 7 (only base poses are shown, so no visible change).
The dragon's normal pose is T0 + its own sub-rect map: `pic_load` works unchanged (n0 <= 182 tiles, W <= 20).

### 1.6 Budgets

#### 1.6.1 ROM: +37.7 KB (petits) + 26.4 KB (dragons) = **+64.0 KB** of asset data, measured. Does not fit the
~56 KB free inside today's `GFX_BANKS` together with item 2: needs the shared bank-table change (section 3).

#### 1.6.2 WRAM: +312 B `zdesc`, +104 B `bdesc` (shared with boss_fx's 200), +45 B Actor (3 x 15), ~20 B globals:
**~0.4 KB** of the ~7 KB headroom (`hdma_tab1` already has 32 B); plus 2,304 B in bank $7F ($7F:C000-$C8FF).

#### 1.6.3 VRAM: no new areas. Petits: the slot's existing 2-KB OBJ strip. Dragons: the existing BG1 tiles 64-511
re-used as T0+T1 (resident) + a top-aligned action slot.

#### 1.6.4 OAM and scanlines
* OAM: petit flap poses 4-9 sprites (today 4); 3 petits <= 27 of the 96 field slots.
* PPU limit 34 sprite slivers (8-px tile slices) / 32 sprites per line. Today, measured from the descriptors
  (`scripts/lines_now.py`): 3 petits (2x2 cells) 24 + hero idle 4 + shield 2 = 30; hero attack poses 8, specials 12.
  With flaps and the 0/8/16 phases (`scripts/linesim2.py`): 3 petits <= **28** -> 34 with the hero idle (at the
  limit, never over); during the hero's attack frames up to 38 on a few lines (today's specials already reach 37).
  In-phase (random) flapping would reach 30 + 6 = 36 even while idle; that is why the phases are spread.

#### 1.6.5 NMI / DMA per frame (cap 4864 B; OAM 544 B; `SPR_ROOM` 4320 B)
* Petit pose change = 2048 B (+80 set-up) with its OAM. PETIT = 4 changes / 25 frames / petit: 1 petit ~340 B/frame,
  3 petits ~1,020 B/frame on average; at most 2 strips fit one NMI (4,256 B), a third waits a frame (no tearing, the
  existing rule); text and its BG3 map still go first.
* Dragon flap change = the BG1 map (2,048 B, `bgmap_dirty |= 1`, sent when 2 KB are left), 4 per 43 frames:
  ~190 B/frame average, no tile DMA.
* Boss load: T0+T1 = 5.4-9.9 KB through `queue_vram_big` (2-KB entries): 1-2 NMIs more than today, once per boss
  (stage start, form swap, mirage wave). MON_098 wing re-upload after an action: 5.2 KB (2-3 NMIs).
* Measured `nmi_vmax` (last line of the NMI transfer, PROFILE builds) stayed at 258-259 in every run below.

#### 1.6.6 CPU (measured, PROFILE ROMs from the work copy, `scripts/lag.py` = 1 - loop iterations / frames)

| run (window) | lag | note |
|---|---|---|
| mirage wave 1, MON_114 idle, today's ROM (600 f) | 0.0 % | baseline |
| same, poses cycled at FLAP_STONE timing with today's code (LZSS tiles + C `boss_place`) (600 f) | **46.0 %** | profile: `lz` 22.9 %, `_boss_place` 9.1 % + its runtime helpers ~8 % |
| same, map only (no tile upload) + C `boss_place` (600 f) | 31.7 % | the C loops are the problem |
| same, map only, maps resident, asm `boss_blit` (= this design) (600 f) | **0.0 %** | `boss_blit` 4.0 % of samples |
| money stage, 3 zako idle, today's ROM (900 f) | 1.6 % | baseline |
| same, 3 zako cycling at FLAP_PETIT timing, tile-run unpack (900 f) | 4.4 % | `tr_copy` 2.8 + `tr_zero` 2.6 + `pose_tiles` 3.1 % |
| same, no unpack (raw strips from ROM, = this design) (900 f) | **0.0 %** | |
| 魔王の城 9 wave 1, 2 zako cycling, tile runs (600 f) | 0.0 % | |

Prototype patches: `scratchpad/plan/wings_mirage/battle_view.proto.c` (`FLAPTEST*`, `FASTPLACE` defines) and
`bossmap.proto.s`; the work copy is restored to the original sources.

### 1.7 Risks
* **Sprite time-over** (34 slivers): at the limit while idle with 3 petits; check with the harness (1.8 test 3).
  The order in which the PPU drops slivers on an overloaded line decides whether a damage number or a wing tip
  flickers; verify in snes9x (its default 34-tile limit is on).
* `wings_volatile` path (MON_098 only) is the one new state machine with a visible failure mode (garbage wing
  tiles if a flap frame is shown before T1 is back): guarded by `wings_bad`; covered by test 6.
* Body lock alters redrawn flap frames slightly (the body is normal's instead of the redraw's noise); check the
  previews (`out/flap_sheet.png`, `build/preview/boss_*`). MON_097 needs lock 3000 (~18/255 per channel average).
* Palettes fitted on the base poses only: flap-only colours (wing membranes) map to the nearest of 30 colours.
* ROM: 64 KB in the asset banks requires the shared bank-table work first (GFX_BANKS has ~56 KB free).
* `battle_view.c` bank space ($85 ~5.5 KB left, shared with boss_fx hooks): if it runs out, move the boss idle-set
  loader into `battle_fx.c` (bank $86, empty) behind two exported calls.

### 1.8 Test plan (harness; build a PROFILE ROM; add `XDEFS` to build.py for test-only defines, as in the work copy)
1. `python build.py data && PROFILE=1 python build.py rom`; `textcheck.py` on the usual scripts: unchanged.
2. **Petit cycle**: test define `FORCE_WAVE="0,0,6"` (3 petits: プチドラ, プチドラ, ゴールデンプチ) in いにしえの洞窟
   (`mksave --open 2 --clear 2:0`). `shot` every frame for 50 frames: each petit shows up 11 / mid 3 / down 7 / mid 4
   frames, slots 8 frames apart, feet 0-2 px higher in the mid/down frames; no torn sprite (tiles always match the OAM).
3. **Scanlines**: new `tools/harness/oamlines.py WRAM.bin` (reads `_oam` from the map file, counts slivers and
   sprites per line, x 0..255 only): <= 34 / <= 32 on every frame of test 2 with the hero idle; report the attack frames.
4. **Lag**: `scripts/lag.py` on test 2's wave, 900 frames, idle: <= baseline + 0.5 % (prototype: 0.0 %); same in the
   money stage (unchanged enemies): within 0.5 % of today.
5. **Dragon**: mirage tower wave 2 (`mksave --open 14 --clear-all`, map RIGHT, A, A; test define
   `FORCE_MIRAGE_WAVE=2` for MON_089, `=1` for MON_114 (flap_down only), `=5` MON_097): shots at cycle frames
   0, 16, 20, 30 -> flap_up / normal / flap_down / normal, lift 0/1/3/2 px; `vram` dump: BG1 tiles 64..64+n_idle equal
   T0+T1; lag 600 frames idle 0.0 %; `nmi_vmax` <= 259; lines 125-127 under the boss show no HP-bar pixels.
6. **Volatile wings**: wave 11 (MON_098): let it attack and hit it 3 times (hurt) -> after each action normal shows
   until T1 is back (`vram` dump equal again) and then it flaps; no wrong tiles in any `shot` of 120 frames.
7. **Transitions**: hurt -> defeat (kill blow) on a flapping boss shows at most 2 frames of normal between them;
   the form swap 魔王第2形態 -> 魔王最終形態 (castle 10) loads the idle set and MON_119 flaps.
8. **Dex**: MON_089 / MON_098 / MON_114 cards: picture identical to today's build (pixel diff of the picture area).
9. **Dark aura** (boss_fx): 魔王の城 STAGE 1 MON_114: the tint pulses on the flap_down frame too.

---------------------------------------------------------------------------------------------------

## 2. Item 2: Dex mirage cards (surface, 18 cards)

### 2.1 Web rules
* Storage: `SaveData.mirage_cards` (set of monster ids), saved as `"mirage": {"cards": [...]}`.
* Award (`adventure.gd commit_mirage`, at the end of a run, win or lose):
  `wave = n if clear else clamp(c.stage, 1, n)`; `for i in (n if clear else wave - 1): m = rush[i].mon; if new -> add`
  -> every boss beaten in that run gets its card ("負けてももらえる"); a clear gives all 18. (AutoTest: losing at
  wave 16 gives 15 cards.) The result note: **`"蜃気楼カード +%d" % cards.size()`** (only new cards), after the
  record line, before the coins (`battle.gd _mirage_rewards`; colour Px.CREAM; "記録の更新なし" only if there is no note).
* Rules text (`mirage_rules_text`): **`"クリアで %d コイン。初めてのクリアで %s x%d。倒したボスの蜃気楼カード（負けてももらえる）"`**;
  pause info (other group): **`"倒したボス %d 体（カードは負けてももらえる）"`**.
* Rush order (surface, 18): MON_114, 089, 095, 096, 097, 092, 093, 091, 115, 099, 098, 094, 113, 090, 101, 118,
  119, 117 (`tools/data/enemies.mirage_rush`).
* Dex (`ui/card_view.gd`, `ui/dex_card.gd`): in the opened monster card, if known and the card is owned, a button
  **`"蜃気楼"`** (colour #6a3a9a, bottom left) switches the card to the mirage version; it then reads **`"通常"`**.
  Mirage card: frame **#c070ff**, art background **#5a2a8a** (radial gradient: lightened 35 % at the centre ->
  darkened 55 %), mark **`"蜃"`** in the top-right badge (instead of the element kanji), art =
  `assets/mobs_hd/<mon>_mirage.png` (if missing: normal art tinted (0.82, 0.66, 1.0)). Name, text lines and the rest
  are the normal card's. The dex list shows nothing extra. The debug "dex all" does not give cards.

### 2.2 SFC design
* **Save**: `u8 mirage_cards[3];` in `Save` after `mirage_turns` (bit w = the boss of mirage wave w+1, rush order).
  SAVE_VER 1 -> 2 with the shared migration (new fields zero). `mksave.py --mirage-cards MASK` (writes the 3 bytes).
* **Award**: `adventure.c commit_mirage(clear)`, after its `memset(&res, ...)`:
  `n = clear ? MIRAGE_WAVES : wave - 1; for (w = 0; w < n; ++w) if (!bit_get(sv.mirage_cards, w)) { bit_set(sv.mirage_cards, w); ++res.mirage_cards; }`
  (`wave` as computed there; new `u8 mirage_cards` in the result struct, `adventure.h`). If the other group adds
  mirage suspend/resume, cards are given only here, when the run ends (web: same).
* **Result** (`rewards.c`, mirage branch): after the BEST/NOREC line `if (res.mirage_cards) sayn(BX_RES, 24, y, T_RES_CARDS, res.mirage_cards)`;
  show `T_RES_NOREC` only when there is no other note (`!mirage_up && !mirage_cards && !coins && !first`), as Safari.
* **Rules text** (`map.c mirage_window`, `T_MIRAGE_RULES`): last line becomes
  `クリアで 500000 コイン。倒したボスの\n蜃気楼カード（負けてももらえる）` (7 lines from y 42 at 14 px: ends at 138,
  above 挑戦する at 150; widths 212 and 192 px, the window's widest line is 216).
* **Dex card** (`dex.c mon_card`), monsters with a card (`mirage_mon[w] == m` and the bit set) and known:
  * Hint box `BX_MIRT` (BG3, transparent, tiles 833-848, screen tiles x 1-8, rows 2-3 = top-left of the picture area,
    above every picture: the tallest card starts at row 4): **`{purple}A:蜃気楼`** (normal view) /
    **`{purple}A:通常`** (mirage view). Purple = BG3 palette 7 (0x6d36, the port's `{purple}`).
  * **A** toggles (free in the card view; B back, ←→ / ↑↓ neighbours as today), `se_play(SE_OK)`, then
    `fade_out()` -> redraw the same card with the other picture (the music continues: `bgm_play` skips the same
    track). ←→ always opens the neighbour in the normal view.
  * Mirage view: picture = the card asset (below) through the same `pic_load` path (BG1 tiles 64+, palettes 6-7),
    backdrop `cgram[0] = 0x44AB` (#5a2a8a, flat), window frame colour `cgram[27] = 0x7DD8` (#c070ff; BG3 palette 6
    colour 3 = the gold frames), mark box `BX_MARK` (tiles 849-852, screen tile x 29-30, rows 2-3): **`{purple}蜃`**.
    `mk_screen()` restores all colours on the next screen.
  * No card -> no hint, A does nothing. Zako never have cards (all 18 are EK_BOSS).
* Not done: the radial gradient (a vertical HDMA gradient on CGRAM 0 would be possible, ~0.2 KB; not needed) and the
  card tilt/holo effects (the SFC dex has no card frame view).

### 2.3 Data conversion
* `make_gfx.py`: after `monsters()`, `mirage_cards()`: for w, r in enumerate(`enemies.mirage_rush(sf)`):
  `im = G.native(img('assets/mobs_hd/%s_mirage.png' % key), w/4, h/4)`, `boss_set('MIRAGE_%02d' % w, [('normal', G.scale(im, SCALE), (0, 0))])`
  (anchor unused: the dex centres by W and stands the picture on PIC_FOOT). Assets are added in order T, M, PAL,
  DESC, so `ASSET_MIRAGE_00_DESC + 4 * w` / `- 1` = desc / palette of wave w; emit `#define MIRAGE_ASSET_STEP 4` and
  assert the ids are consecutive.
* `make_data.py`: `const u8 mirage_mon[MIRAGE_WAVES]` (monster index per wave, 18 B) in `tables.c`.
* Text (`src/text/ui.txt`): `T_RES_CARDS {yellow}蜃気楼カード +{n}` (94 px), `T_DEX_MIR_ON {purple}A:蜃気楼`,
  `T_DEX_MIR_OFF {purple}A:通常`, `T_DEX_MIR_MARK {purple}蜃`, and the changed `T_MIRAGE_RULES`. Only 常 is a new
  glyph (12-dot font, ~24 B).

Measured cards (`scripts/mirage_geo.py`, port `boss_set`, scale 0.73, 2 palettes, LZSS):

| wave | boss | W x H | tiles | bytes |
|---|---|---|---|---|
| 1 | MON_114 龍騎士マーゴス | 18x13 | 184 | 6,014 |
| 2 | MON_089 翼龍ドラゴス | 14x12 | 138 | 4,661 |
| 3 | MON_095 鈍龍タトルノドン | 18x12 | 171 | 5,583 |
| 4 | MON_096 海龍オシャルシアス | 17x12 | 161 | 5,399 |
| 5 | MON_097 熱龍マグードワイバル | 16x12 | 164 | 5,511 |
| 6 | MON_092 樹龍ワシャラス | 15x14 | 151 | 4,955 |
| 7 | MON_093 光龍キラノス | 14x14 | 148 | 4,885 |
| 8 | MON_091 水龍ピチャルス | 14x12 | 135 | 4,534 |
| 9 | MON_115 ミミッくん | 11x9 | 80 | 2,675 |
| 10 | MON_099 奇龍ナーゾキーモス | 13x12 | 122 | 4,019 |
| 11 | MON_098 光龍ゴルデピカライア | 15x12 | 151 | 4,851 |
| 12 | MON_094 闇龍ヤミジャス | 15x12 | 134 | 4,371 |
| 13 | MON_113 闇のダルクネス | 9x12 | 90 | 3,092 |
| 14 | MON_090 炎龍ゴーゴモス | 14x12 | 140 | 4,736 |
| 15 | MON_101 魔王 | 13x12 | 115 | 3,997 |
| 16 | MON_118 魔王第2形態 | 10x12 | 108 | 3,772 |
| 17 | MON_119 魔王最終形態 | 13x12 | 116 | 4,004 |
| 18 | MON_117 兎騎士ペコラータ | 7x12 | 72 | 2,361 |
| | **total** | all <= 20x16 (`pmap`), <= 256 tiles | | **79,420 B** |

A delta against the battle normal pose (reuse matching tiles) was measured at ~59 KB (-19 KB) but needs 4 palettes
and a two-asset picture load; not worth it (`scripts/mirage_cards.py`). Previews: `out/mirage_sheet.png`.

### 2.4 Code changes
* `save.h` / `save.c` (field + SAVE_VER, migration with the shared work), `adventure.h` / `adventure.c`
  (`res.mirage_cards`, award), `rewards.c` (note line), `map.c` (no code: text only), `dex.c`:
  `pic_load(m, known)` -> `pic_load(m, known, desc_asset, pal_asset)`; `static u8 mir_view;` `mir_wave(m)` (loop over
  `mirage_mon`); the A toggle in `mon_card`; hint / mark boxes; the two CGRAM writes. ~0.6 KB in bank $8E (dex.c
  12.7 KB used).

### 2.5 Budgets
ROM 79.4 KB (assets) + ~0.1 KB (text, glyph, table). WRAM ~25 B (`mirage_mon` RODATA 18 B, state). SRAM +3 B per
slot. VRAM: none new (the picture area of today's card: BG1 tiles 64-247 max). No battle-time cost.

### 2.6 Risks
* ROM placement (bank-table work); SAVE_VER bump (shared migration, or old saves become "no save").
* `BX_MIRT` / `BX_MARK` must not share tiles with the picture's colours: they sit on rows 2-3 where no card picture
  reaches (`by = 144 - H*8 >= 32` for H <= 14); textcheck verifies.
* Purple `{purple}` is the port's dark-element colour (#b048d8), close to the web's #c070ff.
* The frame colour entry: `ui.h` says BG3 palette 6 (`PAL_GOLD`) is the window frames, so colour 3 of it is CGRAM
  27; confirm with a shot of the mirage view (if the ring tiles use another index, change that one).

## 5. Open question (default chosen)
* Petit flap strips: stored raw (+16.2 KB ROM, no CPU, 0.0 % lag measured) is the default; tile runs save 16.2 KB but
  measured +2.8 % lag with three flapping zako (typical waves have one petit: about +1 %). 16 KB is about 2.7 s of
  music spread over all tracks.

### 2.7 Test plan
1. `mksave t.srm --open 14 --clear-all --dex 99 --mirage-cards 0x3ffff`: 図鑑 (menu 5th row) -> モンスター ->
   No.007 龍騎士マーゴス -> A: hint `A:蜃気楼` at top-left; A -> purple backdrop, purple info frame, `蜃` top-right,
   the mirage picture (crown/cape art; compare with `out/mirage/mon_114_mirage_sfc.png`); A -> back to normal;
   → -> neighbour opens in normal view; music never restarts (`wav` capture continuous).
2. `--mirage-cards 0`: no hint, A does nothing. `--mirage-cards 0x2` (wave 2 = MON_089 only): only No.014 has it.
3. Mirage run: lose on wave 3 (`FORCE_MIRAGE_WAVE` not used; or a test define that loses at once) -> result shows
   `蜃気楼カード +2`; `sram` dump: bits 0-1 set. Second run losing on wave 3: no card line, `記録の更新なし`.
   Clear (test define): `+16` (the remaining ones) with coins and the first-clear material.
4. `textcheck.py` on the card screens (all 18 in both views), the result screen and the mirage window: no overflow,
   no colour mixing.
5. Old save (SAVE_VER 1 image from today's `mksave.py`) loads through the migration with no cards.

---------------------------------------------------------------------------------------------------

## 3. Shared needs (for the integrator)
1. **Asset banks**: +143.5 KB of assets from this group (flaps 64.0 KB, cards 79.4 KB) against ~56 KB free in
   `GFX_BANKS`: needs the shared bank-table change (assets in code-bank tails / $86 / $8F / $9D-$9F; `Assets` taking a
   per-bank free size; `.bank(label)` instead of `^label` for labels in $81-$9F). Raw petit strips are DMA'd from
   ROM: any bank works.
2. **SAVE_VER 2 + migration** (3 bytes here).
3. **WRAM bank $7F map** for battle: this group takes $7F:C000-$C8FF; $7F:E000-$EFFF is the PROFILE text-check log
   and $7F:F000- the profiler ring (do not use).
4. **Battle pose codes**: boss_fx 7-17, this group 18-20; `pcode[21]`, `bdesc[200]`, `zdesc[3][200]`.
5. **build.py `XDEFS`** (test-only defines: `FORCE_WAVE`, `FORCE_MIRAGE_WAVE`) and `tools/harness/oamlines.py` help
   every battle group.
6. **BG1 / HDMA table 1** is changed (lift segments); boss_fx uses HDMA ch 4/5 and colour math, no overlap.
7. Mirage suspend/resume (other group) must award cards only through `commit_mirage`; the pause line
   "倒したボス %d 体（カードは負けてももらえる）" belongs to the pause-contents item.

## 4. Scripts and outputs (all under `scratchpad/plan/wings_mirage/`)
* `scripts/flap_list.py` (which monsters have flap poses), `flapdiff.py` / `flapdiff2.py` (body identical vs redrawn),
  `measure_zako.py`, `cover.py` / `cover2.py` (mixed covers), `lines_now.py` / `linesim2.py` (scanlines),
  `measure_boss.py` (naive cost), `bodylock.py` (lock limits), **`proto_flap.py`** (reference conversion + ROM),
  `lift.py` (lift tables), `lag.py` (lag from two WRAM dumps), `mirage_cards.py`, `mirage_geo.py`.
* `out/flap_sheet.png`, `out/mirage_sheet.png`, `out/*.png` (converted idle poses), `out/mirage/*`.
* `t/*.txt` harness scripts (`mir_pre.txt` mirage wave 1, `money_pre.txt` money stage, `c9_pre.txt`), `t/*.srm`.
* `battle_view.proto.c`, `bossmap.proto.s`: the measurement prototypes.
