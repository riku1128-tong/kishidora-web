# Spec: treasure chests, gacha presentation + もう1回, 必殺10連武器ガチャ (group `chests_gacha`)

SNES port of キシドラ！ (`/home/user/kishidora-web/kishidra_sfc`, commit 807245d). Three items:

1. Treasure chests: the row of chests in battle (with the drops flying to it), the opening after a win
   (TREASURE view), and the materials counted by kind (gold page).
2. Gacha presentation (summon circle, a chest that drops, shakes, ranks up and opens; 10 boxes that open
   one by one) and the もう1回 / もう10連 key. Same chest art as item 1.
3. 必殺10連武器ガチャ: 10 weapons, each re-rolled until it alone can fire a special. Tickets come from
   clearing ペコラータ (月見の島).

Everything was checked against the decompiled web source (`scratchpad/src/*.gd`, `src/ui/*.gd`) and the
port. Numbers marked **measured** come from a prototype in `plan/chests_gacha/work` (a copy of the port)
run on snes9x (`tools/harness/runrom`), or from the port's own converters (`tools/gfx/snesgfx.py`,
`assets.py`, `cc65`/`od65`).

Files in `/tmp/claude-0/-home-user-kishidora-web/bfba2c3a-727d-51a5-9268-545e956e9958/scratchpad/plan/chests_gacha/`:

| file | what |
|---|---|
| `work/tools/gfx/chests_gfx.py` | prototype of the new make_gfx part: CHEST32 (+PAL), CHEST16, CHEST_ROW, MAT_ICONS, GACHA_CIRCLE (run by `work/tools/make_gfx.py`) |
| `work/src/c/solo_proto.inc`, `work/src/asm/math.s` (end) | solo pull prototypes + benches, the asm `rnd` / `rnd_mul8` / `rnd_below` |
| `work/src/c/tv_proto.inc` | static mock of the treasure view (closed / open / sell pages) |
| `work/src/c/battle_view.c` (`chest_row*`) | the BG3 chest row, working in battle |
| `proto/solo.c` | production-shaped solo pull (1,206 B measured) |
| `scripts/solo_table.py` | builds `solo_cls` / `solo_tab` and checks them against a port of the web BFS (0 mismatches / 120,100 lists) |
| `scripts/solo_sim2.py` | acceptance rates and tries per ticket with the port's skill lots |
| `scripts/solo_pool.py` | the solo pool; proves the port's `dex_item` weapon order == web `DexData.weapon_order` |
| `scripts/measure_chests*.py`, `measure_small.py`, `small_manual.py`, `bg3_chest.py`, `measure_circle.py`, `chestlib.py` | art measurements and previews |
| `scripts/tables.py` | frame tables (drop flight) and BGR555 colours |
| `scripts/textcost.py`, `text/ui_chests_gacha.txt` | every new UI string (`ui.txt` format), widths, new glyphs |
| `out/m/*.png` | previews (`chests32_km6.png` chosen chest art, `small16_manual.png`, `bg3_chest.png`, `circle.png`) |
| `t/tv_sheet.png`, `t/battle_row_sheet.png`, `t/row_zoom.png` | emulator shots of the mocks |
| `proto_battle_row.patch`, `proto_make_gfx.patch`, `proto_math.patch`, `proto_sys.patch` | diffs of the prototype against the port (battle row, make_gfx hook, asm rnd/rnd_mul8/rnd_below, sys.c) |

Conventions: "box y" = pixel y inside a BG3 box; tile rows are 8 px; text of different colours in the same
tile columns must sit on different tile rows; the 8-dot font (`text_small = 8`) uses one tile row.
Frames are 60 fps (web seconds × 60).

---------------------------------------------------------------------------------------------------

## 0. Shared needs (other groups / integrator)

1. **Save v2 block** (owned by the menus/inventory groups): this group adds **`u8 solo_tickets`** (1 B,
   0..99). v1 → v2 migration sets it to 0. `mksave.py`: `--solo N`.
2. **Text/font**: 77 new strings (2,134 B codes + 231 B table), **10 new glyphs `⇒作再却引択括獲計超`**
   (420 B; 再 括 却 are also in the sortie / inventory lists). Strings that are wider than their window
   are in the 8-dot font (widths in §A).
3. **Faster, bit-identical random numbers** (`src/asm/math.s`, `sys.c`): `rnd()` as 16-bit asm (same
   xorshift, same sequence), `rnd_range()` uses the hardware multiplier when the range has < 256 values
   (`rnd_mul8`, exact: `(r·n)>>16 = (rh·n + (rl·n>>8))>>8`), and `u8 rnd_below(u8 n)` (rnd + multiply in
   one call). **Measured**: 500 `rnd_range(0,99)` 27 → 8 frames; same RNG sequence (the solo bench made
   the same number of tries before and after). Every caller in the game gets faster; the battle calls
   `rnd()` every frame in `frame_wait`. ENGINE +234 B, CODE1 −52 B.
4. **`solo_ok_sk()` table** (this group, also for the inventory group's `INV_SOLO` cache and おすすめ):
   one-weapon "can fire a special" by a 1,000-B bit table, exact (§3.4). `inv_solo_all()` (999 items,
   1.37 s with `solo_mask`) becomes a few frames; `solo_mask()` stays for *which* specials.
5. **Menu OBJ tiles** (MENU_OBJ, `make_gfx.py menu_obj()`): this group adds tiles **27-42** (particles,
   16 tiles). The inventory group has 12-23 and 26. 43-63 stay free.
6. **Battle OBJ**: misc strip row 2 (tiles `5·64+32..47`, empty today) = CHEST16 (flying chests/pouch);
   misc row 1 col 14 (empty) = the 5×5 spark. Tell the boss-effects group.
7. **Battle BG3 tiles 484-502** = chest row patterns. The sortie group's windows use 87-502 while open:
   `modal_open` hides the row, `modal_close` re-uploads the 304 B and redraws it.
8. **Key map on the rebuilt result / gacha screens** (changes the inventory group's X/Y contract):
   A つぎへ/OK, **X くわしく** (as in every pick list), **Y 売却モード**, R 再挑戦 / もう1回 (sortie
   contract kept), START = A. N・R 一括売却 moves into 売却モード (X = N一括売却, Y = R一括売却 there).
   Reason: with a cursor on the items, X must be くわしく like everywhere else, and the web puts the bulk
   buttons beside 売却モード anyway. The inventory API is used unchanged (`bulk_pick`, `sell_picked`,
   `inv_new_index`, `item_detail(i, DF_BROWSE|DF_COMPARE|DF_FAV, list, n, pos)`).
9. **Bank 16 ($90) is not empty**: it holds BGM (`BANK90` 32,760 B in the map). The inventory spec puts
   `equip.c` there; it must move (e.g. bank 13 tail or 8).
10. **Port bug**: `battle_core.c meat_gift()` stops at `ITEMS_RUN_MAX` (20); the web stops at
    `ITEM_MAX + MEAT_GIFT_EXTRA_MAX` = 10 + 5 = **15** (`battle_core.gd meat_gift`). Fix to
    `D_ITEM_MAX + T_MEAT_GIFT_EXTRA_MAX`; the treasure grid is sized for 15 (3 rows of 5).
11. **Data regeneration**: running `python build.py data` here (Pillow 12) changed 348 existing gfx files
    (same sizes, other bytes). New assets should be appended at the end of `make_gfx.main()` (ids stay)
    and the integrator should regenerate in one known environment.
12. **GFX space**: this group's art is 11.1 KB (§1.7). It is raw/LZSS data that can live in any data
    bank (the `^label` bank-byte pitfall of `$81-$9F` applies if it goes to code-bank tails).

---------------------------------------------------------------------------------------------------

## 1. Treasure chests (battle row, opening after a win, materials by kind)

### 1.1 Web rules

**Chest art** (`ui/chests.gd`, `rarity.gd`): `Chests.tex(rank, open)` loads
`assets/chests/<key>_<closed|open>.png`, keys `n n_plus r r_plus sr sr_plus …` by rarity rank
(N 0, N+ 1, R 2, R+ 3, SR 4, SR+ 5). Closed 164×150, open 164×178 (content 160×146 / 160×174), plus
`pouch.png` 150×184. Fallback pixel art (used only when a PNG is missing): `CHEST`/`CHEST_OPEN` 16×14,
`POUCH` 12×12, tiers N/R colours `f4f6fa c8d0dc a8b4c4 727e92` / `d8b8ff a078e8 7a50c8 4e2c94`, SR rainbow
by x, plus = gold band `d8a030`, outline `1a1220`, lock `e8c050`/`fff0a0`.

**Battle top bar** (`battle.gd _update_chests`, `_chest_layout`, `_bag_total`, `_drop_target`,
`_on_reward`, `_on_drop_arrived`, `ui/drops.gd`): only in adventure battles (not tutorial / training;
the tower drops nothing). Row at x 92 of the 360-px bar, room = 360 − 92 − 112 = 156 px; when bags > 0 the
pouch takes `BAG_W` 44 (icon `BAG_ICON` 20 + `×%d` 12 px text) at the right end. Chest size
`min(24, floor(room / n) − 2)`, pitch size + 2, in drop order. Shown chests = `items.size() −
drops.chests_pending`; bags = Σ mats − `bags_pending`; coins text = `coins − coins_pending`.
Each reward (`_on_reward`): coin drop (left, `+%d` popup), item drop (right, delay 0.12 s, its chest,
32 px, glow when lucky or rank ≥ R), material pouch (delay 0.24, 24 px, glow unless 強化の素材), goods
pouch (0.36). Meat gift: chest after `MEAT_FLY_TIME + MEAT_GIFT_DELAY` = 0.3 + 0.55 s.
Flight (`drops.gd`): x eases (sine out) to `from.x ± rand(34..58)` over `POP_TIME` 0.78 s (no overlap with
other landed drops); y: up 44 px (0.18 quad out) → land (0.24 quad in) → bounce 14 px (0.11 + 0.11) →
5 px (0.07 + 0.07); rest `REST_TIME` 0.25; then lift 10 px (`LIFT_TIME` 0.12) and glide to the target
(`GLIDE_TIME` 0.5, sine in-out, shrinking to the target size); arrival updates the row and bumps the icon
(scale 1.35 → 1, 0.18 s). Glow = a 5×5 white cross circling the 4 corners every 8 frames. A new WAVE
(`_spawn_stage`) glides every landed drop at once (`collect_all`).

**Win** (`_show_result` → `_open_treasure` after 1.6 s, adventure and not the tower) → `TreasureView`
(`ui/treasure_view.gd`). Pages `closed` → `open` → (`sell`) → `gold`:
- Title = `_place_text(false)`: `<エリア><全角数字>`, `<エリア>のたからのSTAGE`, `魔王城の地下`.
- `closed`: heading `TREASURE` (34 pt), the board with one closed chest per item (5 per row, rows
  centred, cell 68×130, chest fitted to 48 px at the cell bottom), button `あける`.
  Items empty → straight to `gold`; `_skip_gold()` (= leave or coins 0 and no mats and nothing sold and
  overflow 0) → finish.
- `あける` (`_open`): `opening`; flash 0.3 s (`ffe6a8`-ish α0.3), 6-particle burst at every chest base,
  SE `charge`. Schedule: `t = OPEN_SHAKE 0.5`; per cell in order `at = t + wait(rank)`, `t = at +
  OPEN_GAP 0.13`; `wait` = 0.42 (rank ≥ 4, `OPEN_SUPER_WAIT`), 0.18 (rank ≥ 2, `OPEN_RARE_WAIT`), else 0;
  end = last at + `OPEN_TAIL` 0.45.
  Unopened chests wobble: rotation `sin(t·42 + i·1.7) · amp`, `amp = (0.04 + 0.1·near)·(1 + 0.6[rank≥2]
  + 0.8[rank≥4])`, `near = clamp(1 − (at − t)/0.6)`; hop `−|sin(t·21 + i)|·3·near`; brightness
  `1 + lit·k` with `lit = 0.35·near`, or 0.9 inside the wait window, `k = 0.5 + 0.5 sin(t·38 + i)`.
  Entering the wait window: two rings at the chest mouth (rarity colour) and SE `just3` (rank ≥ 4) /
  `just2`.
  Pop (`_pop_cell`) at `at`: open texture, white 2.2 → 1 and squash 1.3×0.72 → 1 (0.4 s elastic), a beam
  up to the card, ring, burst (10 + 6·lv), the item card flies from the mouth to its place (0.34 s,
  0.3 → 1.25 → 1), NEW badge pops, `レア！` (rank 2-3) / `超レア！` (rank ≥ 4) pops after 0.22 s
  (red `ff4a30`, white outline), a glow burst behind the card (rank ≥ 2). Rank ≥ 4: screen flash
  (rarity colour lightened, α0.7, 0.4 s), 14 rays, ring, 36-particle burst, confetti (45; 80 for SR+),
  screen shake 0.42 s amp 7, SE `miracle` (`transform` for rank ≥ 6, not on the surface). Rank 2-3:
  10 rays, ring, shake 0.2 s amp 3, SE `just2`. Else SE `just1`. A tap anywhere skips to the end state.
- `open`: each cell = card (item icon in a rarity frame) + tags: `売った` (`ffb060`, sold here),
  `コインに` (gray, the bag was full: `got[i] < 0`), the ★ fav mark, NEW (`NEW!!`, fresh item), the
  label. Hint `品をタップすると、くわしく見られる`, or when any item is solo:
  `タップでくわしく。「必殺」＝この 1 本で必殺技が出せる`. Note (red `ff8080`):
  `持ち物 %d/%d：減らすまで次のSTAGEに行けない` (bag ≥ INV_MAX) or `チケットがないので再挑戦できない`
  (retry shown, treasure STAGE, no ticket). Bulk row `N一括売却` / `R一括売却` (exactly N / R, disabled
  when none). Buttons `売却モード` | `再挑戦` (only on the last page: `retry_shown and _skip_gold()`) |
  `つぎへ`. Tap a card → InfoBox with `compare` and browse over the result; long press → ★ toggle
  (toast `★を付けた（お気に入り）` / `★を外した`).
- `sell`: heading `売却モード` (28 pt), `売る品をタップして選ぶ（もう一度タップでやめる）`, picked cards
  get a red frame and `✓`, ★ cards toast `★の品は売れない（長押しで★を外す）`, line
  `選んだ品 %d　合計 %d コイン`, buttons `やめる` / `売る` (disabled when none) → confirm
  `%d 個の品を売る？` / `%d コイン`, `やめる` / `売る`; bulk confirm `%s の品 %d 個を売る？` / `%d コイン`.
- `gold`: section `獲得報酬`: `獲得コイン` (left) `合計コイン` (right), coin icons, `str(coins)` `⇒`
  `str(save.coins)`, then `売った品  +%d コイン` and `持ち物がいっぱい → コイン +%d` (`ffc070`) when
  non-zero; section `素材`: one entry per material kind in 2 columns, the material's item icon 28 px +
  `%s ×%d` (`Desc.mat_name`), or `なし`; note; `再挑戦` (if shown) and `つぎへ`.
- **Loss / leave** (`_show_rewards`): `失った報酬`, `コイン %d`（+ `　素材の袋 ×%d`) in `a09070`, the row of
  closed chests (`_chest_row`, 32 px, 7 per row, pitch 42) or `宝箱なし`.

### 1.2 Art (measured with the port's converter)

| asset | content | bytes |
|---|---|---|
| `CHEST32` | 12 images, image = 2·rank + open (rank 0-5 = N N+ R R+ SR SR+); content crop (2,2)-(162,148/176), scale 32/174 (`G.scale`, `G.snap`) → closed 29×27, open 29×32, bottom-centred in a 32×32 cell; 16 tiles in row order | **6,144 raw** (LZSS as one asset 5,344: not worth it, images are DMA'd one by one) |
| `CHEST32_PAL` | 6 palettes, one per rank, shared by closed + open; k-means (farthest-point seeds, √count weights, port colour metric). `make_palette` (median cut) lost the rainbow and the inner glow (`out/m/chests32_6pal.png` vs `chests32_km6.png`) | **192** |
| `CHEST16` | battle: closed N / R / SR + pouch, 16×16 each, the web's own 16×14 `CHEST` / `POUCH` pixel art drawn **in FX_PAL indices** (battle OBJ palette 5, no new palette): N white/gray/dark, R cream/purple/blue, SR rainbow, lock gold (`out/m/small16_manual.png`). Downscaling the PNG into FX_PAL was unreadable (`small16.png`) | **512 raw** |
| `CHEST_ROW` | battle row on BG3 (2bpp): chest 2×2, pouch 2×2, `×` and `0-9` (misaki 8-dot + shadow); colour 1 = 0x0421, 2 = 0x0866, 3 = the palette's text colour | **304** |
| `MAT_ICONS` + `_SEL` | 16 material icons (16×16, from the ICONS16 conversion in `shields_icons()`: `data[len(ids)*128:]`), palette index per mat into `ICONS16_PAL` (already in ROM) | **2,048 raw + 16** |
| `GACHA_CIRCLE_T` / `_M` (item 2) | summon circle for BG1, 256×96 (r 92, y × 0.42): 2 rings, 48 ticks (4 colour phases), inner rings, 2 triangles; 130 tiles with flips | **1,394 + 417 (LZSS)** |
| MENU_OBJ +16 tiles (items 1/2) | particles (dot, small star, spark, confetti square) in white / yellow / purple / red-green-blue | 512 raw, ≈ 200 LZSS (estimate) |

Total **≈ 11.1 KB** (11,027 B measured + MENU_OBJ growth + 8 table entries). Previews:
`out/m/chests32_km6.png`, `small16_manual.png`, `bg3_chest.png`, `circle.png`; emulator shots
`t/tv_sheet.png`, `t/row_zoom.png`.

### 1.3 Battle: the chest row (BG3, no OAM)

Why BG3: the battle uses all 8 OBJ palettes (hero 0, shield 1, zako 2-4, FX 5, skill icons 6-7) and
almost all OBJ VRAM; OAM 0-95 peaked at 43 in a 3-zako battle (**measured**, OAM dump) before the effects
groups add theirs; 15 chests as 16×16 OBJ would be 60 more entries. The top bar row itself is full on the
SNES (WAVE 6-68, boss name + HP bar 88-204, coins ~196-252), so the row goes **just under it, BG3 rows 2-3
(y 16-31), columns 13-31 (x 104-255)**; the combo box owns columns 0-11 of those rows.

- Patterns: `CHEST_ROW` (19 tiles) at BG3 tiles `TL_CHEST` = 484-502, uploaded by `chest_row_load()` at
  the end of `hud_boxes()` (304 B, queue).
- Map cells only (no canvas): chest = tiles +0 +1 / +2 +3, palette N/N+ `PAL_GRAY` (3), R/R+ purple (7),
  SR/SR+ `PAL_YELLOW` (1); pouch +4..+7 `PAL_GOLD` (6); `×` +8, digits +9.. (palette 0); priority bit
  set (above the boss picture, like the top bar).
- Layout (web order: drop order, pouch at the right end): `room` = 14 columns (13-26) when bags > 0,
  else 19. `pitch` = 2 columns when `2n ≤ room`, else 1 (each earlier chest shows its left half, the last
  is whole: the web shrinks them; on BG3 the overlap keeps one palette per tile). If `n + 1 > room` the
  newest `room − 1` are shown. Pouch at columns 27-28, `×n` from column 29 (`×` dropped at ≥ 100).
- Shown chests `n = b_nitems − chests_pending`, bags `Σ b_mats − bags_pending` (`b_mats` includes the
  goods, as the web's `core.mats`).
- Redrawn only when n or bags change (`cr_n`, `cr_bags`), from `draw_top()`; not while `modal_on`;
  `modal_open` clears rows 2-3 columns 13-31; `modal_close` calls `chest_row_load()` + forces a redraw.
- **Prototype measured**: works in battle (`t/battle_row_sheet.png`, forced drops), CODE5 +967 B.

### 1.4 Battle: drops that fly to the row (OBJ, transient)

`static Drop drops[DROP_MAX 6]` in battle_view.c: `u8 kind (CHEST/BAG/COIN), rank, glow; s8 t` (frame,
starts at −delay); `s16 x0, y0` (spawn), `s8 dx` (landing offset), `u8 phase` (POP 47 f, REST 15 f, LIFT
7 f, GLIDE 30 f), `s16 gx, gy` (glide start), `u8 tx` (target column), `u16 coins` (COIN value).
- `EV_REWARD` (`e->v` coins, `e->a` item, `e->b` mat, `e->c` goods): coin drop (left, delay 0); item →
  chest of `items[id].rarity`, right, delay 7 f, glow = lucky or rarity ≥ R; mat → pouch, delay 14 f,
  glow = mat ≠ `MAT_KYOUKA`; goods → pouch, delay 22 f, glow. `EV_MEAT_GIFT` with an item → chest, delay
  51 f. Counters `chests_pending`, `bags_pending`, `coins_pending` (u32) go up at spawn.
- Spawn `x0 = foe_x(enemy)`, `y0 = FOOT_Y − 30`; landing `dx = ±(24 + rnd_below(18))` (web 34-58 px ×
  0.711), clamped to x 8..232; no overlap search (with ≤ 6 drops a ±8 px nudge away from the previous
  drop's landing x is enough).
- Motion tables (make_data or const, `scripts/tables.py`): `POP_UP[47]` = px above the landing line
  `35 40 45 49 52 55 57 59 60 61 61 61 60 58 56 53 50 46 41 36 30 23 16 8 0 3 5 7 8 9 10 10 10 9 8 7 5 3 0 2
  3 3 4 3 3 2 0` (spawn 30 px above the feet), `POP_X[47]` (0..255 sine-out), `GLIDE[30]` (0..255 sine
  in-out); lift = −7 px over 7 frames.
- Targets at glide start: chest → column `13 + slot·pitch` of the row after it arrives (x = col·8, y = 16);
  pouch → (216, 16); coin → the coin mark (`((252 − msg_width(TXT_N_WHITE)) & ~7) − 16`, y 2).
- Sprites: chest/pouch = 4 × 8×8 from CHEST16 (misc strip row 2: N 32-35 … pouch 44-47, arranged
  TL/TR/BL/BR), palette `OBJ_FX`, via `ent_add`; spark = misc r1c14 when glow (4 corner positions, 8-frame
  steps, hidden every other 2 frames for the alpha flicker); coin = misc r3c14. **No scaling**: the
  sprite keeps 16 px and lands at the target (web shrinks 32 → 24/≤24 px).
- Arrival: pending−−; chest/pouch → `chest_row()`; coin → `top_dirty` (the top bar shows
  `b_coins − coins_pending`, as the web). Bump = none (BG3).
- `EV_STAGE_START` (new WAVE): every drop still in POP/REST goes to LIFT (web `collect_all`). Battle end:
  the pool is cleared; nothing pending matters (`b_nitems` is authoritative).
- OAM: ≤ 5 per chest/pouch in flight, 1-2 per coin; typically 5-15 for ~1.5 s after a kill.
- CPU: ~0.5 k cycles per drop per frame (tables + 4-5 `ent_add`); `draw_top` runs once more per coin
  arrival (it already runs on each EV_REWARD). Check with `prof.py` (§1.9).

### 1.5 After a win: the treasure view (`treasure_scene`)

Entered from `result_scene(outcome)` when `outcome == 1 && b_mode == BM_NORMAL && !b_mirage` (normal,
treasure, money and secret STAGEs), after `commit_win()`. Returns `RES_MAP` / `RES_RETRY` (sortie
contract). Menu layout (`mk_screen`), backdrop `cgram[0]` = board colour (web BG `3a2925` → 0x10A7).

**State**: `n = res.nitems` (≤ 15 after fix 0.10), per cell `{u8 rank, open, x, y, tags}`, `page`,
`cur`, `sold_sum` (u32), `picks` (via the inventory group's `inv_pick[]` bits), `retry_shown = !res.king`,
`retry_ticket = is_treasure()`. Overflow items have `res.idx[i] == NO_ITEM` from `commit_rewards`
("コインに"); items sold here get `res.idx[i] = NO_ITEM` and a `sold` bit.

**Layout** (tile rows): head box rows 0-1 (`BOX_OPAQUE`): `{yellow}TREASURE` / `{red}売却モード` at x 8,
the place text (sortie group's `place_text()` = `<エリア><全角数字>` / `…のたからのSTAGE` / `魔王城の地下`)
at x 120. Board rows 3-17 (transparent box, `BOX_LOW` so the sprites are above it, tiles 65-544): 3 cell
rows × 5 tile rows, cell 48×40 px: `x0 = 8 + (5 − in_row)·24 + col·48`, `y0 = 24 + row·40`. Inside a
cell (box coordinates): row 0 `{yellow}NEW!` (`T_GACHA_NEWTAG`, fresh and not sold); rows 1-3 the card
frame (24×24 rect at x0+12, y0+8 by `tb_rect`, palette by rarity: N/N+ gold 6, R/R+ purple 7, SR yellow 1,
SR+ red 2); row 4 `{red}レア！` / `{red}超レア！` (rank 2-3 / ≥ 4, centred), or `{gold}売った`,
`{gray}コインに`. Sprites: closed/opening chest 32×32 at (x0+8, y0+8); after the pop the item icon
16×16 at (x0+16, y0+12) (PANEL_ICONS, OBJ palette 7 = PANEL_PAL, as the inventory group's そうび詳細).
Foot window rows 19-27 (tiles 545-832), inner rows 20-26:
row 20 note (red 8-dot), rows 21-22 cursor item line (12-dot: rarity label in its colour at x 8,
`mk_item_name` at x 40, `NEW!`/`売った`/`コインに` at x 200), row 23 `{gray}{WTN}・{ELN}属性  攻撃力 {n}`
(shield `防御力`/`HP`), row 24 `{gray}スキル` + skill names + `{yellow}必殺` when `solo_ok_sk`,
row 25 hint, row 26 keys (8-dot). **Mock measured**: `t/tv_sheet.png` (15 chests, icons, frames, labels).

**OBJ VRAM** (menu layout, tiles at $6000, 8×8/32×32): slot s (0-11) = tile `64 + (s>>2)·64 + (s&3)·4`.
Slots 0-5 = chest images N closed/open, R closed/open, SR closed/open (only ranks present are needed;
plus ranks never drop); icon k (0-23) = 2×2 tiles in slots 6-11: `T(6 + k/4) + ((k>>1)&1)·32 + (k&1)·2`
(TL, TR at t, t+1; BL, BR at t+16, t+17). OBJ palettes: 0 menu, 1-6 chest by rank (`CHEST32_PAL` loaded
at 128+16), 7 PANEL_PAL; the "lit" chest uses palette 2·r+2 (the unused plus slot) holding a brightened
copy (each channel + (31 − c)·3/8).

**Pages and keys**
- `CLOSED`: foot rows 21-22 `宝箱 {n} 個`, row 26 `{gray}A:あける`. A / START → opening.
- Opening (§ timeline below); A / B / START during it → all cells open at once (web tap = skip); row 26
  `{gray}A でスキップ`.
- `OPEN`: cursor = the menu arrow at the cell's left (x0+4, y0+18); ←→ ±1, ↑↓ ±5 (clamped). Hint row 25
  `T_TV_HINT` or `T_TV_HINT_SOLO` (any item `solo_ok_sk`); keys row 26 `T_TV_KEYS` / `T_TV_KEYS_R`
  (R only when `last = retry_shown && skip_gold()` and the retry is allowed). Note row 20 as the sortie
  spec (`sv.ninv >= D_INV_MAX` → `持ち物 {n}/{m}：減らすまで次のSTAGEに行けない`; retry shown, treasure,
  no ticket → `チケットがないので再挑戦できない`).
  - A / START: → `GOLD` (or finish when `skip_gold()`).
  - X: `item_detail(res.idx[cur], DF_BROWSE|DF_COMPARE|DF_FAV, list, m, pos)` over the owned result
    items (`NO_ITEM` ones skipped); on return redraw everything (`tv_redraw`: VRAM, palettes, boxes).
    ♥ is toggled inside item_detail (START), so the web's long press needs no key here.
  - Y: → `SELL`. R: `RES_RETRY` when allowed (else `SE_CANCEL`).
- `SELL`: head `{red}売却モード`; row 20 `T_TV_SELL_HINT`; rows 21-22 `T_TV_SELL_SUM` (n picks, total);
  rows 23-24 cursor item; row 26 `T_TV_SELL_KEYS`. Picked cells: frame palette red (2) + `✓` sprite
  (MENU_OBJ tile 26, inventory group) at (x0+30, y0+22).
  - A: toggle the cursor item (♥ → `mk_notice(T_SELL_FAV)`; equipped cannot happen for new items).
  - X / Y: `bulk_pick(res.idx, n, R_N / R_R, &c)` → `mk_confirm3(T_BULK_ASK {s}=RARN, T_SELL_COINS, 0,
    T_BTN_SELL)` (web `%s の品 %d 個を売る？` / `%d コイン`). START: confirm the picks `T_TV_ASK_PICKS` /
    `T_SELL_COINS`. Yes → `sell_picked(&c)` → `res.idx[k] = inv_new_index(res.idx[k])`, cells whose idx
    became NO_ITEM through the sale get `sold`, `sold_sum += c`, `SE_COIN`, back to `OPEN`.
  - B: → `OPEN` (picks cleared).
- `GOLD`: board replaced by one window (0,2,32,22): y 8 `{yellow}獲得報酬` centred; y 24 `獲得コイン`
  (x 16) and `合計コイン` (right-aligned to x 232); y 40 coin mark sprite (MK_T_COIN) + `{n}` (b_coins,
  x 30..110), `⇒` x 120, coin mark + `{n}` (sv.coins) right-aligned to 232; y 56 `T_TV_SOLDSUM` if
  `sold_sum`; next row `T_TV_OVER` if `res.overflow`; next row (8-dot) `T_TV_SOLO` / `T_TV_SOLO_MAX`
  (item 3); then `{yellow}素材` and the materials **by kind**: every m with `b_mats[m] > 0` in index
  order, 2 columns (x 16 / 128), 18 px rows: icon sprite (MAT_ICONS, OBJ palettes 4-7 =
  `ICONS16_PAL`, chests are not on this page) + `T_TV_MAT` (`{s}=MATN_m ×{n}`); none → `{gray}なし`.
  Keys row 26 `T_TV_KEYS_GOLD` / `_R`. A / B / START → `RES_MAP`; R → `RES_RETRY` when allowed.
  `skip_gold()` = `!b_coins && !Σb_mats && !sold_sum && !res.overflow && !res.solo`.
- Dialogs (`mk_confirm3`, `mk_notice`) take BG3 tiles from 1024 − w·h down (28×10 → 744-1023), which
  overlaps the foot window's canvas (545-832): after every dialog the foot (and the board labels) are
  drawn again (`tv_text()`), as after `item_detail`.

**Opening timeline** (frames; t from あける):
- t 0: flash (colour math add, coldata (31,29,23)·0.3 fading over 18 f), 6 particles from each chest
  base (life 27 f), `SE_CHARGE`. Schedule `at_i` = 30 + Σ(wait + 8): wait 25 (rank ≥ 4), 11 (2-3), 0.
- Each frame for unopened cells: `near = clamp(36 − (at − t), 0, 36)`; jitter `dx = SIN16[(t·7 + i·4)
  & 15] · amp / 3` with `amp = 1 + 2·near/36` × (1, 1.6, 2.4 by rank) capped at 4 px; hop
  `dy = −(|SIN16[(t·4 + i·4) & 15]| · near) / 36`; lit palette when `at − t ≤ wait` (blink on
  `(t >> 1) & 1`) — `SIN16 = 0 1 2 3 3 3 2 1 0 -1 -2 -3 -3 -3 -2 -1`.
- Entering the wait window: `SE_JUST` and 8 particles on a ring around the chest mouth (rarity colour).
- Pop at `at`: chest → open image (slot of rank·2+1); 2 frames lit palette (white flash), 3 frames
  y+2 (squash), back; icon sprite appears at the mouth (x0+16, y0+16) and moves to (x0+16, y0+12) over
  10 f; BG3 frame / label / NEW / tags of the cell are drawn and flushed (label 13 f later); 10 + 6·lv
  particles (lv = min(rank, 4)); after 15 f the open chest blinks out over 8 f (the web's tight mode:
  the card stays, the chest goes). Rank ≥ 4: flash in the rarity colour lightened (SR 0x22DD, SR+
  0x65FF; α 0.7 → 0 over 24 f), shake 25 f ±4 px (BG/OBJ scroll), 24 confetti particles falling for
  120 f, `SE_MIRACLE`. Rank 2-3: shake 12 f ±2 px, `SE_JUST`. N: `SE_JUST`.
- End = last at + 27 f → `OPEN` (foot redrawn).
SE map (port has one of each): charge → `SE_CHARGE`, just1/2/3/4 → `SE_JUST`, miracle → `SE_MIRACLE`,
transform → `SE_TRANSFORM`, guard1/2 → `SE_GUARD`.

**Particles** (chestfx.c, shared with item 2): `static Part p[24]` `{s16 x, y; s8 vx, vy (1/4 px);
u8 life, tile}`; tiles MENU_OBJ 27-42, palette 0 (menu) — colour by tile (white / yellow / purple
dots and sparks, confetti red/green/blue/yellow). Spawned rings: 8 or 16 particles at angle steps
(`SIN16`-based velocity), bursts: random angles. ≤ 24 at once, oldest replaced.

### 1.6 Loss / leave result (rewards.c)

Keep the text page; replace the item-name list by the web's closed-chest row: `T_RES_LOST`,
`T_RES_COINS_GRAY` + `T_RES_BAGS`, then rows of closed 32-px chests (7 per row, pitch 34, centred, at
y 64 / 100), `T_RES_NONE` when none. Chest images of the ranks present (≤ 3 slots) + their palettes;
the win page of rewards.c is replaced by `treasure_scene` (the tutorial / training / tower pages stay).

### 1.7 Code changes (files)

- `tools/make_gfx.py`: `shields_icons()` keeps `mat_icon_data` / `mat_icon_sel`; new `chests()` (port of
  `work/tools/gfx/chests_gfx.py`: CHEST32 + PAL with `kmeans_pal`, CHEST16, CHEST_ROW, MAT_ICONS +
  SEL) called last in `main()`; `menu_obj()` + 16 particle tiles (27-42).
- `tools/make_data.py`: the strings (`src/text/ui.txt`), `T_*` constants already exist
  (`T_MEAT_GIFT_EXTRA_MAX`); motion tables (or C consts).
- `src/c/battle_view.c` (bank 5, 4.6 KB free after the row): `chest_row_load`, `chest_row`, drop pool
  (`drop_add`, `drops_step` in the sprite pass, `drops_collect`), EV_REWARD / EV_MEAT_GIFT /
  EV_STAGE_START hooks, `modal_open` / `modal_close` hooks, coins shown minus pending in `draw_top`.
  If bank 5 gets tight, the drops can live in the sortie group's `battle_ui.c` (bank 6).
- `src/c/battle_core.c`: `meat_gift` limit fix (0.10).
- `src/c/chestfx.c` (new, **bank 11** with gacha.c; non-static, called from bank 7 through the
  trampolines): `cf_chest(slot, img)` (4 × 128 B `queue_vram` from `CHEST32` via `asset_where`),
  `cf_icon(k, item)` (2 × 64 B from `PANEL_ICONS`), `cf_mat_icon(k, m)`, `cf_pals(mask)`, `cf_lit(rank)`,
  particles (`cf_ring`, `cf_burst`, `cf_confetti`, `cf_parts_draw`), `cf_flash(bgr, frames)`,
  `cf_shake(frames, amp)`, `cf_tick()` (per frame), and the ×2 scaler (item 2).
- `src/c/treasure.c` (new, **bank 7** with rewards.c / adventure.c): `u8 treasure_scene(void)` and its
  pages. `rewards.c result_scene`: calls it for wins; loss/leave chest row; returns `RES_*`.
- `build.py C_BANKS`: `'chestfx.c': 11, 'treasure.c': 7`.

### 1.8 Costs

ROM: art 11.1 KB (§1.2; ≈ 9.3 KB of it is item 1); code: chest row 0.97 KB (**measured**), drops ≈ 1.2 KB,
treasure.c ≈ 11 KB (≈ 390 lines at the port's ≈ 28 B/line), chestfx.c ≈ 3 KB, rewards.c ≈ 0.6 KB;
text ≈ 1.4 KB (item 1's 31 strings) + glyphs. **≈ 27 KB** for item 1 including its share of the art.
WRAM: drops 6 × 14 = 84 B, row 2 B, treasure state ≈ 160 B, particles 192 B (shared) → ≈ 0.45 KB BSS.
VRAM: battle BG3 484-502, OBJ misc row 2 + r1c14; menu OBJ 27-42 and 64-255; BG3 canvas ≈ 830 tiles in
the treasure view. OAM: treasure view ≤ 15 chests + 60 icon pieces + cursor + ≤ 24 particles ≤ 100;
per scanline a row of 5 (chests 20 slivers + icons 10) ≤ 34. NMI: ≤ 512 B per chest image change, 128 B
per icon, BG3 rows by `tb_flush` (queued).

### 1.9 Risks

- OAM/scanline limits during the burst + confetti over a cell row: cap particles at 16 while chests are
  visible on the same lines (snes9x drops slivers past 34 per line).
- `draw_top` per coin arrival in busy waves (money stage): measure lag with `prof.py`; if it rises,
  redraw only every other frame or let coins arrive in groups.
- BG3 row tiles 484-502 inside the sortie group's window range: the re-upload in `modal_close` must run
  after the window's last canvas upload (`canvas_wait(TL_WIN, …)` already precedes it).
- `res.idx[]` must be remapped right after `sell_picked` (inventory API), before `inv_pick` is cleared.
- The 3-colour BG3 chests are flatter than the web's PNG chests; the flying chests (OBJ) are the
  detailed web pixel art. If the effects groups leave OAM room, the row could later use CHEST16 sprites
  (60 entries at 15 chests).

### 1.10 Tests (harness)

1. Battle row: a test build with every enemy dropping (prototype patch in `battle_hit.c`:
   `pct = 100; b_drop_pct = 100;` + a forced 強化の素材) — `t/battle.txt` (save `mksave.py t/b1.srm
   --open 2 --items 71:0,1 --main 2`, いにしえの洞窟 1, Y presses): shots every 4 hits show chests
   appearing after their flight, `×n` growing; OAM dump (slots 0-95 visible ≤ 96); `prof.py` on the
   money stage (lag ≤ today's 4-6 %).
2. Pause during a flight (START): row hidden, drops frozen; resume: row back, drops continue.
3. Treasure STAGE (guard always drops): `mksave.py --open 7 --tickets 3`, win → closed page shot →
   A → shots at +30, +45, +60 f (wobble, lit, first pop) → open page; cursor ←→↑↓ shots; X → そうび詳細,
   B → page redrawn identically (compare shots); Y → sell page; X with N items → confirm text; yes →
   `sv.ninv` −k, cells `売った`; START → gold page: coins, `売った品 +{n} コイン`, materials by kind
   (compare with `b_mats` from a WRAM dump).
4. Full bag (999 items save from the inventory group's `t/items999.txt`): overflow items show
   `コインに`, gold page `持ち物がいっぱい → コイン +{n}`, red note, no R.
5. Retry contract: R on the last page returns `RES_RETRY` (sortie test `t/retry.txt` passes).
6. Loss: lose with items → chest row on the result.
7. `textcheck.py` (PROFILE build) over closed / open / sell / gold / loss pages: no overflow, no colour
   mix, nothing over a frame.

---------------------------------------------------------------------------------------------------

## 2. Gacha presentation and もう1回

### 2.1 Web rules (`ui/gacha_view.gd`, `menu_screens.gd _ask_gacha`, `_pull`, `_gacha_result`)

- Before a pull: `_ask_gacha`: coins < price·n → `コインが足りない` (n = 1) or
  `コインが足りない\n%d連は %d コイン`; room < n → `持ち物がいっぱい` (room ≤ 0) or
  `持ち物の空きが %d 個しかない\n%d連には %d 個の空きがいる`; else confirm
  `[label + ("を引く？" | "を%d連で引く？"), "%d コイン"]`, buttons `やめる` / `引く`; label =
  `装備ガチャ` (`GACHA_TYPES[0][0]`).
- View: fade in 0.18 s; dark backdrop (gradient `161c40` → `05060d`, 70 twinkling stars), summon circle
  (`_Circle`: r 130, y × 0.42, grows 0.5 → 1 at 2.2/s ease-out-cubic, 48 ticks spinning 0.9 rad/s,
  hexagram), skip label `タップでスキップ` at the bottom. `rank` = the item's rank (10 pulls: the best).
- Phase 0 (summon, `SUMMON_T` 2.45 s): motes fly to the centre every 0.06 s until 2.15 s; at `DROP_AT`
  0.35 the chest drops from above (`DROP_T` 0.42, bounce) to the centre (+40 px), landing: SE `guard1`
  + ring. `SHAKES` at 1.05 / 1.5 / 1.95 s (`SHAKE_T` 0.3 wobble, 0.22 rad·k·(1 + i·0.25)): the chest
  becomes `_stage_rank(k)` = min(rank,1), min(rank,3), rank; tier = clamp(r/2, 0, 2); tier up → flash
  (colour c α0.75, 0.35 s) + 12 rays + SE `transform` (tier ≥ 2) / `miracle`; else SE
  `just1/just2/just3` by k; ring + burst (10 + 6k); circle colour = `COLORS[chest rank]`.
- Open (at 2.45 s or a tap): final rank, open texture fading out ×1.3 in 0.35 s; circle `COLORS[rank]`;
  beam, white flash 0.45 s, 46-particle burst, 16 rays (rainbow ≥ 4); SE `charge`.
- Single reveal: backdrop tint, sunburst (rainbow ≥ 5), twinkles (10; 18 at rank ≥ 4) when rank ≥ 2,
  item art pops (0.1 → 1.18 → 1) and bobs (±4 px, 2.4 rad/s); headline: rarity label (44 pt, scale
  2.2 → 1) at the top, word below the item (`WORDS`: `GET!` `GET!` `RARE!` `RARE!` `SUPER RARE!!`
  `ULTRA RARE!!!`), NEW badge; rank ≥ 4 confetti + SE `miracle` (+0.18 s). After `CARD_T` 0.8 s (or a
  tap) the card: name, rarity / type chip / `無属性`|`<属性>属性`, stats (`攻撃力` `スタン` `会心率` `スロット`
  or `HP +n` `防御力` `ガード n%` `スロット`), `スキル` (tiles + descriptions), `特殊効果` (+ body + `盾の欄で
  働く`/`メインに付けたときだけ働く`) or `なし`, `この 1 本で出せる必殺技` (+ list), `たからのチケットももらった！`;
  buttons `くわしく` / `もう1回` (when `can_again`) / `OK`; ☆/★ fav button.
- 10 pulls: pulls **sorted by rank, then index**; `_reveal_multi`: grid 5 × 2 (tile 60, gap 8, at
  area.y + 64), boxes (`BOX_W` 50) of the teaser rank (`_teaser_rank`: rk < 2 → rk; rk < 4 → 0 or rk
  (50 %); else 0 or 2 (50 %)) drop in (stagger 0.045 s, 0.3 s bounce from −90 px), SE `guard2`. After
  0.8 s, per box: rk ≥ 4: tense (dim 0.5, aura, shake 9°, SE `charge`), 0.5 s, promote steps
  (`_promote_steps`: 2/4/6/8 between shown and rk, then rk; each: new box rank, `UP!`, ring, burst, SE
  `just3` (< 4) / `just4`), 0.32 s each, 0.12 s, open, 0.6 s; rk 2-3: if shown < rk shake 0.26 s 7°,
  promote, 0.22 s; open, 0.26 s; else open, 0.14 s. Open: open box, squash, fade; tile pops; NEW; SE
  `miracle` / `just2` / `just1`; rk ≥ 4 also white flash 0.35 s, beam, rays, confetti, view shake 7 px.
  Then `_multi_done` (headline with the best rank, twinkles, confetti + `miracle` at ≥ 4), 1.0 s, list.
- List (`_show_list`): `タップでくわしく・長押しで★（お気に入り）`, `＝この 1 本で必殺技が出せる` (chip), rows
  (icon, name, 必殺 chip, skill tiles, `売った` / `✓` / NEW, ★), `たからのチケットを %d 枚もらった！`; sell
  row `N一括売却` `R一括売却` `売却モード`; buttons `もう%d連` / `OK`. Sell mode: `売る品をタップして選ぶ
  （★の品は売れない）`, `やめる`, `一括選択` / `全部はずす`, `売る（%d 個）` + `%d コイン`; toasts
  `★の品は売れない（長押しで★を外す）`, `装備している品は売れない`, `%d コインで売った`; confirms
  `%s の品 %d 個を売る？` + `%d コイン（★の品は売らない）`, `選んだ品 %d 個を売る？` + `%d コイン`.
- **もう1回**: `can_again` = true for the normal gacha (false for the solo gacha and a closed pickup);
  `again` → `_ask_gacha(type, label, price, n)` again (the confirm, then a new view).

### 2.2 SNES design (`gacha.c`, bank 11)

Flow: shop body rows (`gacha_run`): A on `1回` / `10回` → checks (`T_GACHA_NO_COIN` [+
`T_GACHA_NO_COIN10`], `T_GACHA_FULL` or `T_GACHA_ROOM` + `T_GACHA_ROOM10`) → `mk_confirm3(T_GACHA_ASK1|10
({s}=T_GACHA_NAME), T_GACHA_ASK_COINS, 0, T_BTN_PULL)` → `gacha_pull(n)` (unchanged rules) →
`gacha_view(n, can_again = 1)` → R → the same checks + confirm → loop; A/B → back to the shop body.

Screen: menu layout; BG1 = the summon circle (`GACHA_CIRCLE_T` at BG1 tiles 1.., map rows 13-24,
palette 2: colours 1-3 = hint colour shades, 4-7 = tick phases rotated every 4 frames to fake the spin;
"grow" = colours faded in over 27 f); `win_base()` after loading. Backdrop `cgram[0]` = 0x2062
(`161c40`); 12 star sprites (MK_T_STAR, fixed pseudo-random spots, visible when `((frame + i·5) & 31)
< 24`). Head box rows 0-1 (BG3) for the headline; foot window rows 19-27 for the card / list.

Big pieces without OBSEL changes: the chest is **pixel-doubled** to 64×64 (4 × 32×32 sprites, OBJ
slots 0, 1, 4, 5) and the item icon **quadrupled** (16 → 32 → 64, 4 sprites, slots 2, 3, 6, 7; PANEL_PAL
palette 7). Scaler (chestfx.c `cf_double(src, dst, w_tiles, h_tiles)`): 4bpp planar, `u16 dbl[256]`
bit-doubling table (built at first use, 512 B BSS), per source row 4 planes × lookup → 2 tiles × 2 rows;
32×32 → 64×64 ≈ 50 k cycles (≈ 0.85 frame, estimate), done in the gap before a rank change; buffers
$7F:A000 (64×64, 2 KB) and $7F:A800 (32×32, 512 B) (free outside battle and the dex); upload 2 KB in one
NMI (≤ 4,864).

Timeline (frames; `SE_*` map as §1.5):
- 0-8 fade in. Motes (dot particles, hint colour) from a radius of 105-163 px to the centre every 4 f
  until f 129, life 30 f.
- f 21: chest (rank `stage(0)`, closed) drops from y −64 to its landing (bottom y 122, x 96..159) over
  25 f with a bounce table; landing: `SE_GUARD`, 8-particle ring.
- f 63, 90, 117: shake 18 f (jitter `SIN16 × amp`, amp 2 + k px), chest image → `stage(k)` (new doubled
  image; palette 1 + r); tier up → `cf_flash(COLORS[r], 21 f, α 0.75)` + `SE_TRANSFORM` (tier ≥ 2) /
  `SE_MIRACLE`; else `SE_JUST`; ring + 10 + 6k burst particles; circle colours = `COLORS[r]`.
- f 147 or A/B/START: open: final rank, open image for 6 f, blink 12 f, hide; circle `COLORS[rank]`;
  white flash 27 f; 24-particle burst; `SE_CHARGE`. (Beam and rays: not drawn; the flash + burst stand
  for them.)
- Single reveal: backdrop `cgram[0]` = blend(0x2062, `COLORS[rank]`, 0.3); twinkles (10 / 18 star
  particles within 43-107 px) when rank ≥ 2; item 64×64 appears with 2 white frames at centre
  (x 96, y 36) and bobs ±3 px (period 160 f); head row: `{rarity colour}RARN` centred; under the item
  (y 104) `T_GV_WORD_<rank>` (colour: N gold, R purple, SR yellow, SR+ red); `NEW!` at the item's
  top-right when fresh; rank ≥ 4: 24 confetti + `SE_MIRACLE` at +11 f. After 48 f (or a key) → card.
- Single card (window rows 15-27, inner y 8..95; the item stays above it at y 36-99, the word at
  y 104): y 8 `mk_item_name` (12-dot); y 24 `RARN` (its colour) + `WTN` + `T_GV_EL` ({s}=ELN, `無属性`
  for none; weapons) (12-dot); then 8-dot rows: y 40 `T_GV_ST_W` + `T_GV_ST_W2` on one row (220 px; shields
  `T_GV_ST_S` + `_S2`); y 48 `T_GV_SKILLS` + skill names (`SKILLN_`) joined by `・`; y 56 `T_GV_FX` +
  `FXN_` (yellow) or `T_GV_NONE`; y 64 `T_GV_SOLO_HEAD` + `SPN_` names from `solo_mask` (or `なし`;
  weapons); y 72 `T_GV_TICKET` if `gacha_out[0].ticket`; y 88 keys `T_GV_KEYS1`. A/B/START OK, X `item_detail(idx, DF_COMPARE|DF_FAV,
  0, 0, 0)` then redraw, R もう1回. (Full skill / effect descriptions are in そうび詳細.)
- 10 pulls: `gacha_out[]` stably sorted by rank. After the open: grid cells 48×40 (the same cell code
  as §1.5) at x0 = 8 + col·48, y0 = 40 + row·40 (2 rows, bottom y 120); boxes 32×32 (slots 0-8 = closed/open images of the ranks that appear;
  icons in slots 9-11, 12 places) drop in from y0 − 64 over 18 f, staggered 3 f, `SE_GUARD`; after 48 f
  the sequence per box (frames): rk ≥ 4: tense 30 f (dim = colour math subtract (6,6,6) on BG1/BG3/
  backdrop, OBJ not affected; box jitter ±3; 12-particle aura ring; `SE_CHARGE`), each promote step 19 f
  (box image + palette → step rank, `T_GV_UP` text above the cell for 19 f, ring + 14 particles,
  `SE_JUST`), 7 f, open, 36 f; rk 2-3: if shown < rk: jitter ±2 for 16 f, promote, 13 f; open, 16 f;
  else open, 8 f. Open = open image 6 f, hop, hide after 14 f; the icon (16×16, PANEL_PAL) pops at the
  cell (from +6 px over 10 f) inside its BG3 frame (as §1.5, cell rows 1-3), label row 4 (`RARN` in its
  colour), `NEW!` row 0; rk ≥ 4: white flash 21 f, 16 confetti, shake 19 f ±4 px. Backdrop tint follows
  the best opened rank. Then the headline (best rank: RARN in the head row + `T_GV_WORD_<best>` at y 128),
  twinkles, confetti + `SE_MIRACLE` at ≥ 4; 60 f; list.
- List (= the open page of §1.5 on this grid): cursor over the 10 cells; foot window rows 19-27: cursor
  item lines (as §1.5), `T_GV_SOLO_NOTE` if any is solo, `T_GV_TICKETS` (count of `ticket`), keys
  `T_GV_KEYS10` (`T_GV_KEYS_SOLO` for the solo gacha). A/B/START OK; X くわしく (browse over the 10);
  Y 売却モード (sell page as §1.5 with `T_GV_SELL_HINT`, `T_GV_SELL_KEYS` split on two 8-dot rows;
  SELECT = 一括選択 / 全部はずす over the sellable ones; X/Y = N/R 一括売却 with `T_GV_BULK_COINS`;
  START = 売る with `T_TV_ASK_PICKS`-style confirm; toast `T_GV_SOLD_TOAST`); R もう10連.
- Skip: A/B/START in phase 0 → open now; during the box sequence → all boxes open at once, list.

Colours: `COLORS` BGR555 (rank 0-5): `0x7359 0x7FBC 0x7DF6 0x7E99 0x233F 0x69FF`.

### 2.3 Code changes

`gacha.c`: `gacha_run` (confirm, checks, solo row §3), `gacha_view(u8 n, u8 can_again)` (phases above),
`gv_list` (shares the grid/foot/sell code with treasure.c through chestfx.c functions: `cf_cell_frame`,
`cf_cell_tags`, `cf_item_lines`, `cf_sell_page` — non-static, bank 11), `show_results` removed.
`chestfx.c`: scaler, circle palette cycling, stars, dim, as above. MENU_OBJ particles. Strings §A.

### 2.4 Costs

ROM: circle 1.8 KB (measured), chest art shared; code ≈ 10.5 KB (gacha_view ≈ 330 lines + confirm/checks
≈ 40 lines + scaler ≈ 30 lines; estimate) + text ≈ 0.9 KB → **≈ 13 KB**. WRAM: view state ≈ 180 B (10
boxes × 10 B + phase vars), `dbl[256]` 512 B (or ROM const), scale buffers 2.5 KB in $7F scratch.
VRAM: OBJ 64-255 + MENU_OBJ 27-42, BG1 130 tiles + 12 map rows, BG3 ≈ 600 tiles. OAM: big chest 4 + item
4 + stars 12 + ≤ 24 particles; grid 10 boxes + 40 icon pieces + cursor + ≤ 16 particles ≤ 71.
CPU: trivial except the scaler (≤ 1 frame per rank change).

### 2.5 Risks

- Colour-math dim must exclude OBJ (CGADSUB bit 4 off) and be cleared before windows/notices open.
- The doubled 64-px chest is chunky next to the web's smooth 112-px chest (pixel art doubled, intended).
- Re-entering the view from もう1回 must reload VRAM/palettes (the confirm window is drawn over it).
- With the confirm added, every pull takes one more A (web behaviour; today's port pulls at once).

### 2.6 Tests

1. `mksave.py t/g1.srm --open 1 --clear-all --coins 100000`; shop (`t/solo.txt` navigation): A on 1回 →
   confirm shot (`装備ガチャを引く？` / `1000 コイン`), yes → shots at f 10, 30, 64, 91, 118, 150, 200
   (drop, three shakes, open, reveal, card); textcheck on the card; R → confirm again; A → shop.
2. 10回: shots during boxes (every 8 f), the list, cursor moves, X → そうび詳細 and back, Y → sell page,
   X (N一括) confirm → coins up, `売った`; R → もう10連 confirm.
3. Skip: A at f 40 → reveal immediately; A during the boxes → all open.
4. Coins short (`--coins 500`) → `コインが足りない`; 10回 with 5,000 → plus `10連は 10000 コイン`;
   bag 995 → `持ち物の空きが 4 個しかない` / `10連には 10 個の空きがいる`.
5. Determinism of the rank promote: dump `gacha_out` and compare the final box ranks with the item
   rarities; teaser ranks follow `_teaser_rank`.

---------------------------------------------------------------------------------------------------

## 3. 必殺10連武器ガチャ

### 3.1 Web rules (`tuning.gd`, `adventure.gd`, `menu_screens.gd`)

- Names: ticket `必殺10連武器チケット` (`SOLO_TICKET_NAME`), gacha `必殺10連武器ガチャ`
  (`SOLO_GACHA_NAME`); `SOLO_GACHA_N` 10; `SOLO_TICKET_MAX` 99; `SOLO_ROLL_TRIES` 4000. Cost: **1 ticket
  per 10 weapons**, no coins, no shields, no たからのチケット drops.
- Shown (`solo_gacha_shown`): tickets > 0 and the area has a shop (`solo_gacha_world` ≠ "": surface).
  Port shop areas: 平原の村, 港町ポトス, オアシスの町, 釣り人の村.
- Rates (`SOLO_GACHA_RATES.surface`): R 30, R+ 30, SR 25, SR+ 15 (%); `roll = randf()·100`, the first
  row whose cumulative pct exceeds it.
- Pool (`solo_gacha_cands`): `DexData.weapon_order` (WEAPON_TABLE minus material items, sword/spear/
  hammer, rarity < SSR, sorted by type, rarity rank, atk, id) filtered by rarity: **R 33 (sword 13,
  spear 10, hammer 10), R+ 24 (8/8/8), SR 52 (21/18/13, includes フウイン WP_017 atk 1), SR+ 40
  (14/13/13) = 149**, uniform inside a rarity: 0.909 / 1.25 / 0.481 / 0.375 % each
  (`scripts/solo_pool.py`). Includes evolved and plus weapons.
- Skills (`solo_skills`): `roll_skills(type, world)` repeated up to 4000 times until `not kick_only(sk)`
  and `reachable_specials(gd, [sk])` is not empty (the BFS over that weapon's skill cycle with the 3-skill
  combo history: this weapon alone can fire a special). Fail → whole pull fails `武器を作れなかった`.
- Pull (`solo_gacha_pull`): errors `このお店では引けない`, `%sがない`, `持ち物がいっぱい`
  (inv + 10 > INV_MAX), `出る武器がない`; all 10 picked first, then `solo_tickets −= 1`, 10 × `inv_add`,
  `new` = not found before, `save.write()`.
- UI: gold panel `★ %s ★`, `全部その 1 本で必殺技が出せる武器！ %d 本（盾は出ない）`,
  `%sのレア度　%s` (`地上`, `R 30%・R+ 30%・SR 25%・SR+ 15%`), buttons `出る武器を見る` (excluded: 排出一覧
  不要) and `%d連で引く` + `チケット 1 枚`, line `%s %d 枚`. `_ask_solo`: none → `%sがない`; room < 10 →
  `持ち物の空きが %d 個しかない\n%sには %d 個の空きがいる`; confirm `%sを引く？` /
  `%s 1 枚（のこり %d 枚）`, `やめる` / `引く`. Result = the 10-pull view, `can_again` false. The result list
  and そうび詳細 show the 必殺 chip.
- Tickets in the web come only from online gifts (`GIFT_KINDS ["solo10"]`, `receive_gifts`, note
  `%sを %d 枚受け取った！`), i.e. never in ordinary play.

### 3.2 Ticket = reward for clearing ペコラータ (月見の島)

Web secret boss rewards (`adventure.gd commit_win`, `battle_core.gd give_reward`, `roll_item`,
`reward_first`): 月見の島 has one STAGE (`SECRET_MEN` 1, one wave, ペコラータ / 本気を出した兎騎士ペコラータ).
**Every** win: `SECRET_COINS` 100,000 × `coin_pct`/100 coins. **First** win: the reward shield
`SECRET_REWARD_SHIELD` SH_302 (SR, all `ENH_SLOT_MAX` slots), `res.badge` (ウサギのバッヂ, menus group).
**Repeat** wins: a sure drop at `SECRET_REPEAT_RARITY` SR (`sure = peko and men_cleared`). So the web
already pays every clear.

**Recommendation (default): 1 ticket on every clear of 月見の島 1 (cap 99)**:
- the web treats the secret boss as a repeatable fight with a per-clear reward (100,000 coins + a sure
  SR); the ticket fits that slot;
- first-clear-only means one ticket per save: the gold panel appears once and the whole gacha (≈ 1.2 KB
  code + tables + UI) is used one time;
- value: one ticket = 10 weapons of R..SR+ that can each fire a special; the same win already pays
  100,000 coins (= 100 normal pulls, mostly N). Pekorata is the hardest fight on the surface, so the
  pace stays slow.
This is asked to the user (§5) because 「クリア報酬」 can mean either.

Port: `commit_win()` (adventure.c), in the normal branch: `if (is_secret() && b_men == D_SECRET_MEN)
{ if (sv.solo_tickets < SOLO_TICKET_MAX) { ++sv.solo_tickets; res.solo = 1; } else res.solo = 2; }`
(first-only variant: also `&& res.first`). `res.solo` (new `AdvResult` byte) → gold page line
`T_TV_SOLO` (`必殺10連武器チケットを 1 枚受け取った！`, the web's gift note text) or `T_TV_SOLO_MAX`; and
`skip_gold()` is false when `res.solo`. Not in the tower (the tower's secret boss is not 月見の島).
Debug: `debug_goods()` also sets `sv.solo_tickets = SOLO_TICKET_MAX` (no new string).

### 3.3 Shop panel (gacha.c)

When `sv.solo_tickets > 0` (all 4 shop areas): window BX_SOLO (2,2,28,8) above the normal body (which
moves to (2,10,28,13)): y 8 `T_SOLO_TITLE` centred (yellow); y 24 (8-dot) `T_SOLO_DESC` (220 px); y 33
(8-dot) `T_SOLO_RATES` (172 px); y 44 cursor row `T_SOLO_PULL`; y 60 (8-dot, yellow) `T_SOLO_HAVE`
({n} = tickets). Cursor rows: solo, 1回, 10回, もどる (sel 0-3). A on solo: `sv.ninv + 10 >
D_INV_MAX` → `mk_notice(T_GACHA_ROOM ({n} = room), T_SOLO_ROOM2)`; else `mk_confirm3(T_SOLO_ASK,
T_SOLO_ASK2 ({n} = tickets − 1, 8-dot), 0, T_BTN_PULL)`; yes → fade out, `solo_pull()` (screen
black, §3.4 timing), `gacha_view(10, can_again = 0)` with `T_GV_KEYS_SOLO`; `ERR_MAX` →
`mk_notice(T_SOLO_FAIL, 0)` (never seen). Back in the shop the panel is redrawn (hidden at 0 tickets).

### 3.4 The pull on the SNES (adventure.c, bank 7)

`u8 solo_pull(void)` (`proto/solo.c`, **1,206 B measured**, 70 B BSS): checks; for each of 10: rarity
`x = rnd_below(100)`: < 30 R, < 60 R+, < 85 SR, else SR+; `id = solo_pool[solo_off[r] +
rnd_below(solo_cnt[r])]`; repeat `solo_roll(items[id].type, sk)` until `solo_ok_sk(sk, &kick) && !kick`
(≤ 4000, `sound_service()` every 32 tries); then `--sv.solo_tickets`, 10 × `inv_add(id, sk, nsk, -1, 0)`
into `gacha_out[]` (`fresh`, `ticket` 0), `save_write()`. フウイン gets rolled skills like any sword (web
does the same; the port's normal drops give it none — that difference stays as it is).

- `solo_roll` = `roll_skills` (same draws, same result) using `rnd_below` and `solo_cls[s] == 255` for
  "hits == 0" (no `skills[]` struct indexing: cc65 calls mul8 for every struct-array index).
- `solo_ok_sk(sk, &kick)`: hit skills in order → classes c0..c2 (`solo_cls`: 255 buff, 1..18 the 18
  skills used by the 13 specials, 19 = any other hit skill — they all behave alike: a history place, no
  match); `kick` = the list is exactly `[SK_KICK]` (web `kick_only`: kick + buffs only; `SP_RAMPAGE` =
  KICK×3 makes it reachable, the web rejects it explicitly); index `c0 + 20·c1 + 400·c2` (tables
  `solo_mul1/2`), bit in `solo_tab` (8,000 bits = **1,000 B**). **Checked** (`scripts/solo_table.py`,
  port of the web BFS) on all 120,100 ordered lists of 1-3 distinct surface skills: 1,459 reach a
  special, **0 mismatches**; in the emulator every accepted list also has `solo_mask != 0` (0 / 500
  weapons).
- make_data: `solo_cls[N_SKILL]` (50 B), `solo_tab[1000]`, `solo_mul1[20]`, `solo_mul2[20]` (u16, 80 B),
  `solo_pool[149]` in dex order (= the port's `dex_item` weapon part, verified identical to the web
  order), `solo_off[4]`, `solo_cnt[4]`; defines `SOLO_N 10`, `SOLO_TICKET_MAX 99`, `T_SOLO_ROLL_TRIES
  4000`, rate thresholds 30/60/85 from `SOLO_GACHA_RATES` (cumulative). 1,287 B; as RODATA they are
  copied to WRAM (CRAM 46.8 → ≈ 48.1 KB of 54) — or keep `solo_tab` as an asset copied when used.
- **Measured speed** (50 tickets, snes9x, `t/solo.txt`): tries per weapon 12.3 (sim 11.9; acceptance
  sword 7.6 %, spear 6.9 %, hammer 12.9 %, `scripts/solo_sim2.py`), tries per ticket mean 123.
  | version | frames per ticket (mean / worst of 50) |
  |---|---|
  | port `roll_skills` + `solo_mask` + pool by scanning `items[]` | 48.4 / 93 |
  | + `rnd_range` by the multiplier | 31.3 / 54 |
  | + asm `rnd` | 29.1 / 50 |
  | + class table instead of `solo_mask` | 23.2 / 38 |
  | + precomputed pool (no `items[id]` scans) | 15.9 / 32 |
  | + `rnd_below` | **12.1 / 23** |
  Simulated p99.9 = 288 tries ≈ 29 frames (0.5 s). The pull runs while the screen is black after the
  confirm (BGM ring 2.6 s, `sound_service()` keeps it fed).

### 3.5 Costs

ROM: code 1.2 KB (measured) + shop panel ≈ 0.8 KB + commit/debug 0.1 KB; tables 1.29 KB; asm rnd 0.23 KB
(shared need); text ≈ 0.6 KB → **≈ 4.2 KB**. WRAM: 70 B BSS (+ 1.29 KB if the tables are RODATA). Save
+1 B. Presentation = item 2 (no extra art).

### 3.6 Risks

- `solo_tab` depends on the skill and special tables: make_data must regenerate it (and the check in
  `solo_table.py` should run in make_data, failing the build on a mismatch).
- 4000-try failure path: unreachable in practice (acceptance ≥ 6.9 %: p(fail) < 1e-120), kept for
  parity.
- Ticket on every clear: if the user wants first-clear-only, one condition changes.

### 3.7 Tests

1. `t/solo.txt` bench (prototype hook in the shop: SELECT = 50 pulls into `dbg[]`): mean ≤ 13 frames,
   max ≤ 30; 0 lists accepted with `solo_mask == 0`.
2. Statistics: a debug build that pulls 500 tickets into a scratch area (not the bag) and dumps
   counts: rarity shares 30/30/25/15 ± 2 %, every type present, per-rarity uniform (χ² over the ids),
   every weapon `solo_ok_sk` and not kick-only; compare with `scripts/solo_sim2.py`.
3. Ticket: `mksave.py --open 15 --clear-all --items 71:0,1 --main 2` (secret open) → win 月見の島 1 →
   gold page `必殺10連武器チケットを 1 枚受け取った！`, WRAM `sv.solo_tickets == 1`; win again → 2;
   `--solo 99` → `…もう持てない（99 枚まで）`, stays 99. Loss / leave → no ticket.
4. Shop: `--solo 2`: panel in 平原の村 (shot, textcheck), not in an area without a shop; bag 990 →
   notice; confirm `のこり 1 枚`; pull → ninv +10, tickets 1; 10-box view (all ranks R..SR+, all with
   the 必殺 tag), no R key; tickets 0 → panel gone.
5. Save v1 → v2: `solo_tickets` 0.

---------------------------------------------------------------------------------------------------

## 4. Totals (this group)

| part | ROM | WRAM | save |
|---|---|---|---|
| art (§1.2) | 11.1 KB | – | – |
| item 1 code + text | ≈ 17.8 KB | ≈ 0.45 KB | – |
| item 2 code + text | ≈ 11.4 KB | ≈ 0.7 KB (+2.5 KB $7F scratch) | – |
| item 3 code + tables + text | ≈ 4.2 KB | 70 B (+1.29 KB if RODATA) | 1 B |
| glyphs (10) | 0.42 KB | – | – |
| **total** | **≈ 45 KB** | **≈ 1.2-2.5 KB** | **1 B** |

Code banks: 5 (battle row + drops, ≈ 2.2 KB; bank 5 has 5.5 KB free today), 7 (treasure.c, solo_pull:
≈ 13 KB of 23 KB free), 11 (gacha view, chestfx: ≈ 14 KB of 29 KB free).

## 5. Question for the user

- ペコラータ（月見の島）に勝ったときの「必殺10連武器チケット」は、**勝つたびに 1 枚（おすすめ。最大 99 枚）**
  にしますか、それとも**初めて勝ったときだけ 1 枚**にしますか。Safari 版の裏ボスは、勝つたびに 10 万コインと
  SR の品が必ずもらえるので、「毎回 1 枚」がその形に合います。初回だけだと、このガチャは 1 回しか引けません。

---------------------------------------------------------------------------------------------------

## A. New strings (`text/ui_chests_gacha.txt`, widths by `scripts/textcost.py`)

77 strings, 2,134 B + 231 B table; glyphs `⇒作再却引択括獲計超`. Strings wider than their window are
drawn with `text_small = 8` (8-dot): `T_TV_HINT_SOLO` 180, `T_TV_KEYS_R` 176, `T_TV_SELL_HINT` 160,
`T_TV_SELL_KEYS` 184, `T_TV_SOLO` 156, `T_TV_SOLO_MAX` 188, `T_SOLO_DESC` 220, `T_SOLO_RATES` 172,
`T_SOLO_ASK2` 176, `T_SOLO_ROOM2` (12-dot 236 → 8-dot 160), `T_GV_KEYS10` 168, `T_GV_SELL_KEYS` 252 →
two rows (`X:N一括売却  Y:R一括売却  SELECT:一括選択` / `START:売る  B:やめる`). Web text kept exactly
except: `タップ` → `A` / `X` (`X でくわしく見られる`, `売る品を A で選ぶ（…）`, `A でスキップ`), ★ → ♥ (the
port's favourite mark), the ticket lines (new reward), and the key lines. Families: `T_GV_WORD_0..5`
(make_data needs contiguous numbers).

## B. Prototype notes

- `work/` contains prototype-only code (benches on SELECT/X/Y in the shop, `dbg[]` writes, the mock);
  do not merge it as is. The battle row code and `chests_gfx.py` are close to final.
- Bench numbers above are from `work/build/kishidra.sfc` built on 2026-11-08 with cc65 from /usr/bin.
