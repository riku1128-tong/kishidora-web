# Spec: sortie flow and battle info (group `sortie_battle`)

SNES port of キシドラ！ (`/home/user/kishidora-web/kishidra_sfc`). Seven items:

1. 再挑戦 (retry the same STAGE after a win)
2. エリア / STAGE の詳細 (enemies and drops before a sortie)
3. 蜃気楼の斜塔: 中断・続きから (suspend / resume, no clock)
4. ポーズの中身と「状況」 (rewards so far, enemy turns, weakness, states, gear, effects)
5. 戦闘中の情報 (the phone's long press → hold L or SELECT)
6. ラッキーキャラの演出とはじめての説明
7. チュートリアル: とばす確認と、ガードのタイミングの細かい助言

Everything below was checked against the decompiled web source (`scratchpad/src/*.gd`, `src/ui/*.gd`)
and the port. **All seven items were prototyped, built and run** in a copy of the port
(`plan/sortie_battle/work`, snes9x libretro core): every screen below was seen in the emulator, ROM sizes are
from the linker map of that build, and `textcheck.py` was run over the new screens (4 findings, all layout
details; the fixes are written into this spec, §8).

Files in `plan/sortie_battle/`:

| file | what |
|---|---|
| `proto_src.patch` | diff of the prototype vs the port (src/, build.py, tools/make_data.py, mksave.py). Prototype quality: it has `PROF_LINE` timing hooks, a `proto_text.h` stub, and the C snapshot that §3 replaces with asm |
| `text/ui_sortie_battle.txt` | every new UI string in `src/text/ui.txt` format (177 strings) |
| `scripts/gen_plan.py` | prototype of `make_data.py build_plan()` (per-STAGE wave table, port of `adventure.gd men_detail` / `area_detail` / `drop_text`) |
| `plan.s`, `plan_expected.txt` | the generated table (1,321 B) and the web's detail text of all 84 STAGE blocks + 16 areas (reference for tests) |
| `scripts/measure_text.py`, `scripts/measure_spark.py`, `scripts/segs.py`, `scripts/skilld.py` | width / glyph / ROM measurements |
| `t/*.txt`, `t/*.png` | harness scripts and screenshots (`battle_sheet.png`, `mirage_sheet.png`, `lucky_sheet.png`, `tut_sheet.png`, `retry_sheet.png`, `d_*.png`) |

Conventions: "box y" = pixel y inside a BG3 box (`say(box, x, y, ...)`); window inner area is x 8..(w·8−8),
y 8..(h·8−8). Lines of different colours in the same tile columns must be on different tile rows (16 px
apart, starting on a tile row); same colour may be 14 px apart. 8-dot text (`text_small = 8`) is 8 px tall
and gets its own tile row.

------------------------------------------------------------------------------------------------------------

## 0. Shared needs (with the other groups)

1. **Save v2 block + migration** (owned by one group, see menus/inventory specs). This group adds 4 bytes:
   `u8 guides` (bit 0 `GD_LUCKY_TIP`), `u8 mirage_run`, `u16 mirage_run_id`. Plus two **SRAM records outside
   `Save`**: mirage snapshot copies at `$3E00` and `$7E00` (512 B each, the tails of the two save slots).
   Add a compile-time check that `sizeof(Save) <= 0x3E00` (now 8,702 B).
2. **Text and font banks.** This group: 177 UI strings (3,744 B codes + 531 B table) + 12 `TUTX_*` (428 B)
   + 86 `FXO_n` (≈1 KB, shared with the menus group's status screen) and **29 new glyphs**
   `飛待再詳細絵＋雑魚落目張先断捨状況↑↓組動条件働対象優的狙` (1,218 B; `件動対象条組` are also in the
   menus group's list). Measured in the prototype: the strings overflow `$C1` by 1,024 B. Strings can go to
   any bank because `text_table` stores the bank byte (the prototype put the overflow into `$8F`); **fonts
   cannot move to a C-code bank** (`$81-$9F` segments start at `$8000` with bank 0 in the address, and
   `text.s` uses `^font_glyphs` — tried, all text became garbage). Keep the fonts in `$C2` (30.8 KB with the
   new glyphs, fits) or another `$A0+` bank.
3. **Effect texts / rules** (menus group §3): `FXO_n` (OFF reason) and the effect classes. The battle's 状況
   reuses `FXO_n` and `FXD_n`, and switches on `effects[fx].kind` itself (no `fx_rule` table needed, see §4).
4. **Per-STAGE plan table** (`data/gen/plan.s`, 1,321 B, §2). The inventory group's おすすめ needs
   per-STAGE targets (`reco.s`, 1.7 KB): derive them from this table instead (zako rows = the area's pool
   minus the STAGE's bosses when any wave has zako or escorts; boss rows = each wave's boss, weight
   mid/guard vs boss by the wave role; `zn2 = Σ(zako_min + zako_max + escort)`).
5. **Result screen contract** (treasure-chest group, inventory group's N・R 一括売却 on X / Y):
   `u8 result_scene(u8 outcome)` returns `RES_MAP` / `RES_RETRY`; R is the retry key; §1 gives the rules.
6. **Bank 6** (`$86`, empty) gets the new `battle_ui.c` (10.0 KB measured). `build.py` already reserves
   `'battle_fx.c': 6` for battle effects: both fit in the one bank (32 KB).
7. **Battle window size**: windows over the field may now use 416 BG3 tiles (87..502; the combo box's tiles
   439-462 and the free 463-502 are joined while a window is open). Only `modal_open`'s `canvas_wait`
   changes (352 → 416).
8. `dex.c` exports `u16 dex_card_mon(u8 m)` (the monster card by monster id) for §2; the dex mirage-card
   group may use it too.
9. **Port bug found (fix needed for §3):** in the tower the 魔王's forms do not advance the WAVE.
   `battle_core.c form_swap()` keeps `b_stage`, so form 1 is made with form 0's mirage stats
   (`make_enemy` reads `est[b_stage-1]`), and after form 2 the next waves spawn form 1 and form 2 again.
   The web (`battle_core.gd form_swap`) does `if is_mirage() and stage < stage_count: stage += 1;
   turns_stage = 0`. Fix: the same line before `make_enemy(e, next)` and `top_dirty = 1` on
   `EV_FORM_SWAP` in battle_view.c (verified in the prototype).

------------------------------------------------------------------------------------------------------------

## 1. 再挑戦 (retry the same STAGE)

### Web rules (battle.gd `_open_treasure`, `_retry_men`; ui/treasure_view.gd `_retry_ok`, `_retry_button`, `_over_note`; map.gd `_retry_after`)
- Shown only after a **win** of an adventure battle that is not the tower: normal STAGE, treasure STAGE,
  money STAGE (`retry_shown = not leave and not after_battle.king`). Not after a loss, a retreat, the
  tutorial, the training or the tower, and not when the win starts the king's return (ending).
- Button `再挑戦` (yellow) on the last page of the treasure view, left of `つぎへ`. Disabled when
  `not adv.can_start()` (bag `inv.size() >= INV_MAX` = 999) or (treasure STAGE and `tickets <= 0` after the
  win used one). The red (`#ff8080`) note line: `持ち物 %d/%d：減らすまで次のSTAGEに行けない` (shown whenever the
  bag is full) else `チケットがないので再挑戦できない` (retry shown, treasure, no ticket).
- Press: `after_battle.retry = {area, men, rental}` → map. The usual after-battle chain runs first (orb
  scene, area story, gift notice, `新しい場所に行けるようになった！`), then `_retry_after`: select that area
  (world cursor = it, `save.write()`), find its row (`men` / `treasure` / `money` with that men);
  row missing or closed → open the stage list; bag full → stage list + the bag-full notice; else open the
  **sortie screen** of that STAGE (`_open_sortie(men, name, rental, again=true)`; the port has no rental).
  Back from the sortie → the stage list.

### SNES design
- Result screen (`rewards.c`, today `A:マップへ`): `retry = outcome == 1 && b_mode == BM_NORMAL && !b_mirage &&
  !res.king`. `note = sv.ninv >= D_INV_MAX ? T_RES_NOTE_FULL : (retry && is_treasure() && !sv.tickets ?
  T_RES_NOTE_TICKET : 0)`; `rok = retry && !note`.
- Keys line y 192: `rok ? T_RES_KEYS_R ({gray}A:マップへ  R:再挑戦) : T_RES_KEYS`. **R** because X / Y are the
  inventory group's N一括売却 / R一括売却 keys on this screen and A / START keep meaning マップへ.
- Note: 8-dot red at box y 176 (`text_small = 8`, targ_num = ninv, targ_num2 = 999). To keep rows clear of
  it, the item list shows at most 5 items when a note is drawn (else 6): rows 32 head, 48 coins, 64-128
  items, 144 `ほか n 個`, 160 overflow, 176 note, 192 keys.
- `result_scene` returns `RES_RETRY` when R was pressed and `rok`; main.c sets `map_retry`.
- map.c `map_scene(MS_AFTER)`: after `after_battle()` (orb, story, opened notice; returns MA_TITLE for the
  king), if `map_retry`: `map_retry = 0; sel = map_area; sv.cursor = sel; make_nodes(); set_scroll();
  draw_top(); save_write(); label_hide(); label_off = 1; a = area_window(map_men, 1);` then the same
  handling as an A press on the map.
- `area_window(u8 focus, u8 go)`: the list cursor goes to the row whose `men == focus` among RK_MEN /
  RK_TREASURE / RK_MONEY; with `go` (first pass only): row open → bag full ? `mk_notice(T_INV_FULL,
  T_INV_FULL2)` : `sortie()` → 1 = MA_BATTLE with `map_men = rows[r].men`; row closed → just the list.

### Code
`rewards.c result_scene` (u8 return, note, cap, R key); `game.h` (`RES_MAP`, `RES_RETRY`, `extern u8
map_retry`); `main.c` (`map_retry = result_scene(r) == RES_RETRY`); `map.c` (`map_retry`, `area_window`
args, `map_scene`).

### Costs (measured in the prototype)
ROM ≈ 1.0 KB (rewards.c ≈ 0.35 KB, map.c ≈ 0.5 KB, main.c 30 B, 3 strings 150 B, glyph 再). WRAM 1 B.
No save field.

### Risks
- The treasure-chest group rebuilds the result pages: keep the contract (R on the last page, the note
  rule, `RES_RETRY`).
- `after_battle()` moves `sel` to a newly opened area; the retry must set it back (done above).

### Tests
1. `t/retry.txt` (prototype, passes): save `mksave.py t/sr.srm --open 2 --items 71:0,1 --main 2` (デンセツ,
   ATK 218), sortie いにしえの洞窟 1, press Y until the win, R on the result → the sortie window of the same
   STAGE opens on the map (`retry_sheet.png`).
2. Treasure room with `--tickets 1`: win → no `R:再挑戦`, red `チケットがないので再挑戦できない`.
3. 999-item bag (`--items` × 997): win → red `持ち物 999/999：…`, R does nothing.
4. Win that opens a new area: after the story and `新しい場所に…`, the cursor is back on the old area and its
   sortie opens. Win of 魔王の城 10 (king): no retry. Loss / 離脱: no retry.
5. textcheck over the result screen with 6 items + overflow + note.

------------------------------------------------------------------------------------------------------------

## 2. エリア / STAGE の詳細

### Web rules (map.gd `_open_men` 詳細 buttons, `_open_detail`, `_open_men_detail`, `_wave_text`, `_boss_title`, `_mon_grid`, `_item_line`; adventure.gd `area_detail`, `men_detail`, `_money_detail`, `drop_text`, `treasure_odds`, `money_lucky_coins`, `money_mat_text`, `secret_hidden`)
- Buttons: the area page has `詳細` (area detail); every STAGE row of kind men / treasure / money has `詳細`.
  Both are disabled while `secret_hidden(area)` (月見の島 not cleared). Tutorial / training rows have none.
- **STAGE detail** (title `<STAGE名>の詳細`):
  - Section `WAVE（n）`, one line per wave: `WAVE i` + `_wave_text`: parts joined by `・`: role name
    (`中ボス` / `ボス` / `魔王` / `裏ボス` / `番人`) + ` ＋ お供 0〜%d 体` when escort > 0; `ラッキーキャラ %d 体`;
    `雑魚 %d 体` (min == max) or `雑魚 %d〜%d 体`; no part → `ー`.
  - `敵の強さ：STAGE 1 の %d%%` or `敵の強さ：このエリアのふつう` (strength 100); castle: `雑魚の強さ`.
    strength = 100 + (lv−1)·step, lv = AREA_MEN (5) for the treasure STAGE else men; step = CASTLE_STEP_PCT 8
    (castle) or MEN_STEP_PCT 15. Money: (100 + (MONEY_LV−1)·8)·MONEY_HP_PCT/100 = 50.
  - Bosses (if any): title `_boss_title`: several roles → `ボス（WAVE ごとに 1 体。最後は魔王）` if 魔王 among
    them else `ボス`; one role → its name; then the monster grid. Zako (if any): `出る雑魚` + `・お供` when
    there are bosses; grid. `ときどきラッキーキャラも出る（倒すとレアな品）` when `lucky`.
  - `手に入るもの`: `drop_text` lines, `ボス・中ボスはレア度が上がりやすい` (if bosses), the reward line, the
    orb line `ボスを倒すと <宝玉>`, treasure `番人は必ず品を落とす：R %d%%・SR %d%%`, money lines.
  - Wave rules (`men_detail`, surface): waves = `stage_count_for` (MEN_STAGES 3, castle 5, secret 1,
    treasure 3, money 5). Treasure: zako = TREASURE_ZAKO [1,1,0] (min = max), lucky = TREASURE_LUCKY [2,2,0],
    last wave guard MON_115 (role 番人). Castle 10: every wave CASTLE_FINAL[s], role 魔王 on the last, ボス
    before. Else last wave of castle / secret / men ≥ 4: castle → CASTLE_BOSSES[men−1], secret or men 5 →
    area boss, men 4 → area mid; role 裏ボス (secret) / ボス (castle, men 5) / 中ボス; MON_NONE → no boss.
    Boss waves get escort: secret SECRET_ESCORT_MAX 0, a MAOU_FORMS boss MAOU_ESCORT_MAX 0, else
    BOSS_ESCORT_MAX 1; 0 for NO_ESCORT bosses and ONE_ENEMY_AREA (アーケディア城); ≤ AREA_ENEMY_MAX−1
    (平原の村 2 → 1). Other waves: zako [MEN_ZAKO_MIN[s], MEN_ZAKO_MAX[s]] = [1,2],[2,3],[2,3] (castle
    [1,2],[1,2],[2,2],[2,3],[2,3]) capped by AREA_ENEMY_MAX; アーケディア城 [1,1]. Money (castle men 20):
    zako MONEY_ZAKO [1,1,1,1,0], lucky MONEY_LUCKY [2,2,2,2,2], last wave guard MON_115.
    `lucky = not (secret or one or treasure)` (false for money). Zako list: zako_pool(area) (pool minus mid,
    unique) minus this STAGE's bosses, only if some wave has zako or escort.
  - Reward: 月見の島 1 → SECRET_REWARD_SHIELD (SH_302 イージスの盾), once; 平原の村 5 → REWARD_SHIELD_ID (SH_279);
    other areas men 5 (not treasure) → BOSS_REWARD_TABLE. Line `ボスを初めて倒すと必ず`; once cleared:
    `初めて倒したときだけ（もうもらった）` (secret) or `2 回目からは、ときどき（もうもらった）`; then the item
    name `<名前>（<レア度>）` (yellow) and `くわしく` (item info with "スキルは手に入れたときに決まる").
  - drop_text (surface): `落とす品のレア度（雑魚）　N %d%%　R %d%%　SR %d%%` with rarity [n, r, sr] of the area;
    areas up to PHASE_EARLY_LAST (砂漠の遺跡群) show [n, r+sr, 0]. Treasure / money first add
    `落とす品　武器 %d%%　盾 %d%%` (95 / 5: TREASURE_SHIELD_PCT 5, MONEY_SHIELD_PCT 5).
  - Money lines (surface, shipped `map.gd:1263-1284`): `ラッキーキャラ 1 体 %d コイン（コイン %d%%）` with EK_LUCKY
    coin_min 500 × castle coin_pct 400 / 100 × MONEY_COIN_PCT 160 / 100 = 3200, `%d 体全部で %d コイン` (10 体,
    32000), `ラッキーキャラは素材を必ず 1 つ落とす`, `money_mat_text` = `強化鉄・スーパー強化鉄 など`
    (`"・".join([mat_name(MAT_KYOUKA), mat_name(MAT_SKYOUKA)]) + " など"`; the sub worlds add their common material),
    `見張りを先に倒すと、ターンの終わりにラッキーキャラは逃げる`. The two material lines (12 px yellow on the web) are
    in the shipped build only, not in the decompile this spec was first written from (PLAN conflict 33: the battle
    rule is A1's, these lines E2's).
- **Area detail** (`<エリア名>の詳細`): `出るモンスター（雑魚）` grid (zako_pool), `STAGE 4の中ボス`,
  `STAGE 5のボス` (`裏ボス` for 月見の島), castle `ボス（STAGE 1〜9・STAGE 10）` (CASTLE_BOSSES + CASTLE_FINAL,
  14), `手に入るもの` drop_text, `ボス・中ボスはレア度が上がりやすい`, reward `STAGE %dのボスを初めて倒すと必ず`
  (reward_men 5; 平原の村 REWARD_SHIELD_MEN 5; 月見の島 SECRET_MEN 1) + item, orb
  `<STAGE名>のボスを倒すと <宝玉>`, treasure `たからのSTAGE：番人が必ず R %d%%・SR %d%%` (小島 50/50, 孤島 10/90).
- Monster grid cell: picture (silhouette until met), name or `？？？`, and **the weakness even when unknown**:
  `弱点 なし` (gray) or `弱点` + weapon kanji (both #ffb060) + `<属性>属性` (element colour).
- `plan_expected.txt` lists the exact text of every block (84 STAGEs + 16 areas) as the web makes it.

### SNES design (prototyped; screenshots `t/d_*.png`)
- **Keys** in the stage list (`area_window`): `X` = this row's STAGE detail (RK_MEN / RK_TREASURE / RK_MONEY;
  other rows ignore it), `Y` = the area detail. `list_run(&l, secret ? 0 : BTN_X | BTN_Y)` with
  `secret = sel == AR_SECRET && !area_done(AR_SECRET)` (secret_hidden). Keys line (win_header, box y 138,
  8-dot gray): `T_WIN_KEYS_D` `A:けってい B:もどる X:STAGEの詳細 Y:エリアの詳細` (192 px); the old
  `T_WIN_KEYS` for 月見の島 until cleared. Y / X were free here (Y / X / START open the menu only on the map
  itself).
- **Screen** (map.c `detail_scene(area, men)`, men 0 = area): full screen (`fade_out`, `mk_screen`), head box
  `box(BX_TOP, 0, 0, 32, 2, T_TOP=1, …)`: title at x 6 (`T_DT_HEAD` `{s}の詳細` / `T_DT_HEAD_MEN`
  `{s} {n}の詳細` / `T_DT_HEAD_TR` `{s}の宝の部屋の詳細` / money: `T_ROW_MONEY` via `T_DT_HEAD`), keys at x 180
  `{gray}A:くわしく B:もどる`. Body: `win_open(BX_WIN, 0, 2, 32, 26, 65, PAL_W)` (tiles 65-896) and a `List`
  of up to 40 lines, `l.y = 8`, `l.lh = 16` (rows on tile rows, any colours), 12 visible, ↑↓ / ←→ pages
  (list kit). Row kinds and layout (x in box px):
  - DK_TEXT: section titles yellow at x 14 (`T_DT_SEC_*`, `T_DT_SR_1..5` for the one-role titles), gray
    notes at x 14 (`T_DT_STR*`, `T_DT_BOSS_UP`, `T_DT_RW_*`).
  - DK_WAVE: `{gray}WAVE n` x 14; the parts from x 56 (white, put() running x): role `T_DT_ROLE_1..5`,
    `T_DT_ESCORT` (4 px gap), `T_DEX_DOT`, `T_DT_LUCKYN`, `T_DT_ZAKO1/2`, or `T_DT_NONE`. Widest
    `ラッキーキャラ 2 体・雑魚 1 体` 172 px → ends at 228.
  - DK_MON: name `MONN_m` (white) or `{gray}？？？` (not in `sv.found_mons`) at x 14 (widest 熱龍マグードワイバル
    120 px → 134); weakness from **x 136**: `{yellow}弱点`, +4 px, weapon kanji yellow (web #ffb060),
    +4 px, next tile, element `ELN_e` + `属性` in the element's colour (炎 red, 水 blue, 風 green, 光 yellow,
    闇 purple = `TXT_RED_S/BLUE_S/GREEN_S/YELLOW_S/PURPLE_S`); `{gray}弱点 なし`. Must end by x 228 (the ▼
    scroll mark sits at 236: textcheck found the prototype's x 140 column touching it).
  - DK_RATES: `N {n}%　R {m}%` + `SR {n}%` at x 30 (white), under the gray `落とす品のレア度（雑魚）` line
    (the web's one line is 234 px).
  - DK_WS `落とす品　武器 {n}%　盾 {m}%`; DK_TR `{gray}番人は必ず品を落とす：R {n}%・SR {m}%` (230 px → ends 244 ✓);
    DK_ORB `{yellow}ボスを倒すと {s}` (ORBN_n); DK_ORB_AREA `{yellow}{s} {n}のボスを倒すと` + orb name;
    DK_ITEM `{yellow}<名前>` + `{yellow}（<RARN>）` at x 30.
  - 8-dot lines (gray, x 10, y+2): `T_DT_LUCKY` (184 px), `T_DT_TR_AREA` (140), `T_DT_MONEY_1` (156),
    `T_DT_MONEY_3` (232 px → x 10..242). `T_DT_MONEY_2` 12-dot yellow.
  - Money material lines (PLAN conflict 33), between `T_DT_MONEY_2` and `T_DT_MONEY_3` as on the web, 12-dot
    yellow like `T_DT_MONEY_2`: `T_DT_MONEY_MAT` `{yellow}ラッキーキャラは素材を必ず 1 つ落とす` (218 px with 落, which
    this group adds anyway → at x 10 it ends at 228, clear of the ▼ at 236) and `T_DT_MONEY_MATS`
    `{yellow}強化鉄・スーパー強化鉄 など` (160 px). make_data asserts `T_DT_MONEY_MATS` equals the web's
    `money_mat_text` for the surface (from `MAT_NAMES`). The money block grows by 2 rows (about 26 of the 40 the list holds).
  - A on DK_MON → **the monster's dex card** (`dex_card_mon(m)`: dex.c `mon_card` found by monster id: the
    battle picture, a silhouette while unknown, race/element/size, weakness, places, kills, its battle BGM);
    ←→ / ↑↓ on the card step to the previous / next monster line of this detail (wrapping), B back to the
    list (`bgm_play(BGM_OPENING)`, the screen is set up again with the cursor kept). This is the "one picture
    at a time" of the dex.
  - A on DK_ITEM → item card: close the list window (its tiles are reused), `win_open(MK_BOX_DLG, 0, 15, 32,
    13, …)`, `mk_item_detail` of a temporary `Inv {id, sk 255×3, slots 0, enh 0}` + `{gray}スキルは手に入れたとき
    に決まる` (weapons) at box y TOP_Y+LINE_C+2·LINE_H; A / B close; reopen the list. (The prototype opened the
    dialog over the list: 32×13 = 416 tiles from 608 overlaps the list's 65-896 — must close first.)
  - B → `fade_out`; area_window calls `map_setup()` and reopens the list where it was.
- **Data** (`make_data.py build_plan()`, port of `scripts/gen_plan.py`): `data/gen/plan.s` in a bank tail
  (prototype: BANKC0), `.export plan_data`. Per block: `area, men, waves | 0x20 lucky | 0x40 treasure |
  0x80 money, strength (u8), reward item (255 none), 1 = reward once`, then per wave 3 bytes `boss mon (255
  none), role | escort << 3 | lucky << 5, zmin | zmax << 2` (role 0 none, 1 中ボス, 2 ボス, 3 魔王, 4 裏ボス,
  5 番人); `$FF` ends the table. 84 blocks (14 areas × 5, 2 treasure rooms, castle 1-10 + 20, secret 1) =
  1,321 B. Generated from the web rules (not from the port's C), so the screen matches the web by
  construction; the zako list, area rarities, orbs, treasure odds, castle bosses come from the existing
  `areas[]`, `orbs[]`, `castle_bosses[]`, `castle_final[]` tables at run time.
- **asm** `plan_block` (asset.s, ENGINE, 113 B, prototyped): `ea0 = area, ea1 = men, ea3 = dst` → copies the
  block (≤ 21 B) and returns `ea2 = length` (0 = none) by scanning the blocks.

### Code
map.c: `detail_scene`, `build_men`, `build_area`, `dl_drops`, `dl_mons_pool`, `dl_draw`, `item_window`,
`dl_add`, helpers; `area_window` keys; `win_header` keys. dex.c: `dex_card_mon`. asset.s: `plan_block`.
make_data.py: `build_plan`. The detail stays in map.c (bank 9, 22.7 KB free) to share its menukit copy.

### Costs (measured)
ROM: map.c detail code ≈ 6.6 KB (`dl_draw` 1.9 KB, `build_men` 1.1 KB, `area_window` +0.4 KB,
`build_area` 0.6 KB, `detail_scene` 0.6 KB, rest 0.8 KB), table 1,321 B, asm 113 B, dex 41 B, ≈ 60 strings
≈ 1.9 KB, glyphs `詳細雑魚落目張先＋絵` ≈ 0.4 KB → **≈ 10.4 KB**. WRAM ≈ 200 B (`dl[40]` 160 B, `plan[24]`,
locals). VRAM / OAM: menu layout only (BG3 tiles 1-896); the dex card is its own screen.

### Risks
- The table must be regenerated with the data (one more `make_data` step).
- The cursor stops on every line (needed for A on monsters / the reward). The web just scrolls.

### Tests
1. `t/detail.txt` (prototype, passes): 孤島 5 / 3 STAGE detail and the area detail (`d_men1.png`, `d_men2.png`,
   `d_tre1.png`, `d_area2.png`).
2. Compare, for every block, the SNES lines with `plan_expected.txt`: a harness script that opens every
   STAGE's detail (save `--open 15 --clear-all --dex 0` and one with all monsters known) and pages down;
   OCR-free check: dump WRAM `dl[]` + `ndl` and compare kinds / monsters / numbers with a Python model
   from `gen_plan.py`.
3. 月見の島 before the clear: X / Y do nothing and the old keys line shows; after: both work.
4. A on a monster → its card (picture, BGM), ←→ walks the monsters, B back on the same line. A on the
   reward → card with `スキルは手に入れたときに決まる`.
5. textcheck on: アーケディア城 4, 魔王の城 10 (5 bosses), 魔王城の地下 (money lines, including the two material
   lines), 小島 6 (treasure), 魔王の城 area (27 lines). Must be clean (the prototype had one: ▼ vs the weakness at
   x 140 → x 136).
6. `plan_expected.txt` (and `gen_plan.py`, then `tools/data/mendetail.py`) carries the two money material lines
   after `10 体全部で 32000 コイン`; the prototype's copy lacks them (PLAN conflict 33).

------------------------------------------------------------------------------------------------------------

## 3. 蜃気楼の斜塔: 中断・続きから

### Web rules (battle.gd `_mirage_autosave`, `_mirage_mark`, `_mirage_suspend`, `_pause_info`, `_retry_text`; adventure.gd `mirage_run_get/put`, `mirage_record`, `mirage_note`, `mirage_better`, `mirage_reach_turns`, `commit_mirage`; map.gd `_open_mirage`, `_mirage_resume`, `_mirage_restart`, `after_battle mirage_suspend`; battle_core.gd `snapshot`, `restore`, `mirage_calm`)
- **Calm point** `mirage_calm()`: phase REWARD, or phase FIGHT with turn PLAYER, step NONE, hero NORMAL, no
  attacker, empty attack queue, no pending hits, no popups, no talk / talk request, no enemy dying, and at
  least one targetable enemy.
- **Autosave**: every frame in the tower; at the first frame of a calm period `_mirage_last =
  core.snapshot()`; whenever `mark = [stage, turns_total, phase == REWARD]` differs from the last saved one,
  `mirage_run_put(core, _mirage_last)` and `save.write()`. So closing the app resumes from the start of the
  last player turn (or the chest choice).
- `mirage_run_put` also records the progress as a record (`mirage_note(world, stage, false, gear, week,
  reach_turns(total, turns_stage, false))`).
- **Pause in the tower**: `倒したボス %d 体（カードは負けてももらえる）` (stage−1), buttons `続ける`,
  `状況`, `やめる` (sub `そこまでの WAVE を記録`), `セーブして中断` (sub `続きはまた入ると再開`, visible when calm or a
  last calm snapshot exists), note `やめると続きはない（中断なら続きから）`. やめる needs a second press
  (`本当にやめる？` / `もう一度押すとマップへ`) → `commit_mirage(false)`.
- **中断**: snapshot = `core.snapshot()` if calm else `_mirage_last`; `mirage_run_put`; map → the tower page
  and the notice `中断した` / `また入ると続きから（週が変わると続きは消える）`.
- **Tower page** with a run: big button `続きから（WAVE %d / %d）` and `はじめから`. 続きから: `save.write()`,
  start the battle with `resume` (adv.prepare then `core.restore(snapshot)`, `_spawn_stage()`, reopen the
  chest panel when the phase is REWARD; no WARNING, no slide). はじめから: window
  `中断した続きを捨てて、WAVE 1 からはじめる？（そこまでの WAVE は記録に残る）` with `やめる` / `はじめから` →
  erase the run, `save.write()`, `_mirage_go()`.
- Kills, specials and the record are committed only when the run ends (`commit_mirage`: win / loss /
  やめる). A dropped (はじめから) run keeps only the recorded WAVE.
- Week rules (`week` key, run erased when the week changes, 今週の記録) — **not on the SNES** (no clock):
  only the all-time best, and a suspended run never expires (user decision).
- Turns of a record: `reach_turns = clear ? turns_total : max(0, turns_total − turns_stage)` (the turns of
  the wave you are in do not count). Score `wave·2 + clear`, ties to fewer turns.

### SNES design (prototyped: `t/mirage.txt`, `mirage_sheet.png` — suspend, map notice, 続きから, resumed
battle with HP / combo / skills as before)
- **State saved** at a calm point (`battle_core.c battle_snapshot`, 296 B measured for one boss; max 332 B):

  | field | bytes | note |
  |---|---|---|
  | b_phase, b_stage, b_target, b_focus | 4 | phase is PH_FIGHT or PH_REWARD |
  | b_hp, b_hp_max | 8 | (hp_max includes the MAXHP chests) |
  | b_st, b_combo | 3 | |
  | b_w[3] (Slot: item, nsk, pos, sk[3], plus) | 24 | the gear of the run, even if the bag changes later |
  | b_shield, b_shield_plus | 3 | |
  | b_buff_free/power/crit | 3 | |
  | b_lock_slot, b_lock_slot2, b_lock_turns | 3 | |
  | b_guard_cool, b_guard_meat | 3 | |
  | b_mirage_offer_n, b_mirage_offer[3][2] | 7 | the rolled chests (same chests after resume) |
  | b_mirage_buff[8] (u16) | 16 | |
  | mirage_stock[8][3] | 24 | static in battle_core.c |
  | b_turns_total, b_turns_stage (new) | 4 | |
  | b_meat_gifts, b_history[3], b_nhist, b_last_special | 6 | the special-move history |
  | b_seen[12], b_sp_done[2] | 14 | dex |
  | b_men_kills, b_boss_specials, b_stair | 4 | |
  | b_time, guard_press, hit_time | 6 | timers the guard code compares |
  | attack_over, lock_open_now, beg_said, attack_sab | 4 | statics |
  | RNG seed (`rnd_state()`, new in sys.c) | 2 | |
  | b_ne, b_e[3] (Foe 52 B) | 157 | |
  | kills: n, then n × (mon, count) | 1 + 2n | ≤ 18 monsters in the tower |

  Not saved (derived or empty at a calm point): b_area/b_men/b_mirage/b_stage_count/b_st_max,
  `est[]` (reloaded by `load_estats`), `b_fx`, `b_coin_pct`, `b_drop_pct` (`greed()` from the gear),
  b_difficulty (adv_prepare: lunatic), b_coins / items / mats (the tower drops nothing: `give_reward`
  returns early), the pending / popup / event queues, attacker, talk, forms (`b_form_who` 255), inputs.
- **Restore** (`battle_restore`, after `battle_start(AR_MIRAGE, 1, lo)`): read back in the same order, then
  `b_fx`, `greed()`, `b_turn = TURN_PLAYER`, hero NORMAL, step NONE, attacker 255, queues empty, talk 0,
  `b_form_who = 255`, `after_talk = -1`, `opening_lock = opening_beg = 255`, events cleared, then post
  `EV_STAGE_START` with `c = 1` (battle_view: load the stage gfx, `bgm_play(battle_bgm())`, no WARNING /
  WAVE banner / slide) and `EV_MIRAGE_REWARD` (phase REWARD: the chest window reopens) or `EV_FIGHT`.
- **Calm check** `battle_calm()` (battle_core.c): the web conditions plus `b_form_who == 255` (a pending
  form change starts at the next talk_ready frame).
- **Autosave** (battle_view.c main loop, after `battle_step` + events): `if (b_mirage && battle_calm() &&
  mark != snap_mark)` → snapshot into a buffer and write the SRAM record. Buffer: battle_view's `held[]`
  (896 B, only used inside `modal_show` / `modal_close`), so no new WRAM.
  **Measured cost of the C prototype: 201 scanlines for the snapshot (55 small `memcpy` calls) + 87 lines for
  the SRAM write = 1.1 frames once per player turn — too slow.** Required implementation:
  - snapshot / restore in asm: a ROM table of `(address, length)` pairs (`.word _b_hp, 4` …; the 4 statics
    become non-static `bc_*` globals so the table can name them) walked with `MVN` or a byte loop:
    296 B ≈ 6-8 k cycles ≈ 30-40 scanlines (estimate);
  - the SRAM write in the **next** frame (keep the gathered bytes in `held` and a "dirty" flag): ≈ 87 lines.
  So at most ≈ 90 extra scanlines (≈ 1/3 frame) in any one frame, once per player turn, and only in the tower
  (one boss, light frames). Re-measure with `PROF_LINE` marks (see Tests).
- **SRAM record** (save.c `msnap_write`, `msnap_read`, `msnap_ready`, `msnap_wave`; prototyped):
  two copies at `$3E00` / `$7E00`: header 12 B `"KMSN", u16 seq, u16 sum (byte sum of the body), u16 len,
  u16 run id` + body ≤ 500 B. Write: the copy that is not the newest, body first, header last (a reset in
  between leaves the older copy valid). Read: the newest copy whose magic, `run == sv.mirage_run_id`,
  `len ≤ MSNAP_MAX` and sum are good. `msnap_ready()` = `sv.mirage_run && a valid copy`.
- **Run bookkeeping** (adventure.c): `mirage_start()` on 挑戦する: `++sv.mirage_run_id; sv.mirage_run = 1`
  (map_scene's `save_write()` before the battle stores it). `commit_mirage` sets `sv.mirage_run = 0` and uses
  `reach_turns` (needs `b_turns_stage`: `++` next to `++b_turns_total` in `enemies_turn`, reset in
  `start_stage` and in the mirage `form_swap`). `commit_suspend()` (中断): dex only
  (`found_mons |= b_seen`, `commit_specials`), no kills / record, `save_write()`. `mirage_drop()`
  (はじめから): `mirage_note(wave, 0, reach)` with the snapshot's WAVE and reach turns (the reached WAVE goes
  into the record, as the web's run_put already did), `sv.mirage_run = 0`, `save_write()`. For the exact
  turns the record header carries `u16 reach = b_turns_total − b_turns_stage` of the snapshot (header
  12 → 14 B; the prototype passed 0).
  The record is not updated during the run (the web does it at every autosave); the tower window shows
  `max(sv.mirage_best, msnap_wave())` while a run exists, so the player sees the same number.
- **Pause** (§4): button 4 `セーブして中断` (gray and refused with SE_CANCEL until `msnap_ready()`). Pressed:
  if `battle_calm()` write a fresh snapshot (it differs from the autosave only in target / focus), then
  `battle_scene` returns **4**; main.c: `commit_suspend(); map_scene(MS_SUSPEND)` → map with
  `mk_notice(T_MIR_SUSPENDED 中断した, T_MIR_SUSPENDED2 また入ると続きから)` (the web's week part dropped).
- **Tower window** (map.c `mirage_window`) with a run: lines at box y 136 `続きから（WAVE {n} / {m}）` and y 152
  `はじめから` (cursor ↑↓, A, B = もどる). (The prototype's y 150 / 166 overflowed the 22-row window and
  shared a tile row: textcheck.) 続きから → `map_area = AR_MIRAGE; map_men = 1; b_resume = 1` → MA_BATTLE
  (no inventory check: the web resumes regardless). はじめから → confirm (3 lines, buttons `やめる` /
  `はじめから`; use the inventory group's `mk_confirm3` or a 28×10 window): `T_MIR_RESTART_1`
  `中断した続きを捨てて、`, `T_MIR_RESTART_2` `WAVE 1 からはじめる？`, `T_MIR_RESTART_3`
  `{gray}（そこまでの WAVE は記録に残る）` → `mirage_drop()` then start a new run directly (web `_mirage_go`:
  bag check, `mirage_start()`, MA_BATTLE).
- `battle_scene`: `if (b_resume) { b_resume = 0; n = msnap_read(held); if (n) battle_restore(held); }`
  right after `battle_start`, before `hero_setup` (the panel then shows the restored gear).

### Code
battle_core.c (`battle_calm`, `battle_snapshot`, `battle_restore`, `b_turns_stage`, form_swap fix),
asm gather / scatter + table (new, e.g. `src/asm/snap.s`), sys.c `rnd_state`, save.c (record),
adventure.c (`mirage_start`, `commit_suspend`, `mirage_drop`, `commit_mirage` turns + flag), map.c
(`mirage_window`, `MS_SUSPEND`), main.c (result 4), battle_view.c (autosave, resume, EV_STAGE_START c=1,
top_dirty on EV_FORM_SWAP), battle_ui.c (pause button).

### Costs
Measured prototype (C snapshot): battle_core +1.9 KB, save.c +0.7 KB, adventure +0.2 KB, map
mirage_window +0.7 KB, texts ≈ 0.6 KB, glyphs 断捨 ≈ 0.1 KB → 4.2 KB. With the asm gather: battle_core
≈ +0.5 KB, asm ≈ 0.2 KB + table 0.2 KB → **≈ 3.2 KB**. WRAM ≈ 30 B (snap_mark 4, msnap header 14, flags);
the buffer is borrowed. SRAM: 4 B in Save + 2 × 512 B records. CPU: see above.

### Risks
- A missed field makes a resumed battle differ. The table must list every battle variable that can be
  non-default at a calm point; add a test that compares the full state (below). New battle variables added
  later (other groups' effects) must be classified (saved / derived / empty at calm).
- The run id ties the record to the save; if the save falls back to the older slot, a newer snapshot with
  the same id could belong to a later moment — harmless (still the same run).
- If the user resets during the 1-frame SRAM write, the older copy stays valid (header written last).

### Tests
1. `t/mirage.txt` (prototype, passes): tower → fight → Y twice → START → セーブして中断 → map notice →
   tower `続きから（WAVE 1 / 18）` → resumed battle (`mirage_sheet.png`).
2. State equality: on a PROFILE build, at the resume, dump WRAM right after `battle_restore` and compare
   every field of the table and `b_e[]` with a WRAM dump taken at the autosave frame before the suspend
   (two runs of the same script; addresses from `build/kishidra.map`). Must be byte-identical (except
   `b_turn`-type fields set by restore, which must be equal anyway at a calm point).
3. Determinism: from the same snapshot, the same inputs give the same outcome (RNG saved): run the resume
   twice with identical pad scripts, dump `b_hp`, `b_e[0].hp`, `b_turns_total` after 600 frames: equal.
4. Power-off resume: in the middle of an enemy attack (not calm) take `sram` and reload it into a fresh run:
   続きから restarts at the start of that player turn.
5. Reward phase: suspend while the chest window is open → resume shows the same three chests.
6. 魔王 forms: reach WAVE 15 (debug: start a run with a strong save, or a crafted snapshot with `b_stage = 15`)
   → kill form 0 → top bar `WAVE 16/18` and the form-1 stats of `est[15]`; after form 2 the next wave is
   ペコラータ (18).
7. Timing: PROFILE build, `PROF_LINE` before the gather, after it, and after the SRAM write (next frame);
   read `prof_marks` (prototype: 4 → 205 → 30 of the next frame, n = 296). Target ≤ 40 lines gather and the
   write alone in its frame; `prof.py` over 600 frames of a tower fight shows no frame over budget.
8. Records: lose at WAVE 7 after a suspend / resume: record `WAVE 7` with the reach turns (total minus the
   turns of WAVE 7). はじめから on a WAVE 9 run with record WAVE 7: record becomes WAVE 9 with the snapshot's
   `reach` turns (as the web's last `mirage_run_put`); the tower shows it at once.

------------------------------------------------------------------------------------------------------------

## 4. ポーズの中身と「状況」

### Web rules (battle.gd `_build` pause_panel, `_pause_info`, `_retry_text`, `_leave`, `_toggle_pause`, `_place_text`; ui/battle_status.gd; desc.gd `fx_state`, `fx_state_battle`, `STATE_WORD`, `effect_body`; battle_core.gd `shield_guard_pct`)
- Pause panel (dim 0.72): `メニュー` (22pt), place `_place_text()` = `<エリア><全角数字>` / `<エリア>のたからのSTAGE` /
  `魔王城の地下` / `チュートリアル` / `カカシ（ガードの練習）` + `  WAVE %d/%d`.
  - Adventure (not tower): `ここまでの報酬` (gold), `コイン %d` + `　素材の袋 ×%d` (bags > 0) (yellow), then the chest
    row (one chest per item, its rarity's chest) and `中身はクリアしたあとに開けられる` (gray), or `宝箱なし` (gray).
  - Tower: `倒したボス %d 体（カードは負けてももらえる）` (gold), stage − 1.
  - Tutorial / training: nothing more.
  - Buttons: `続ける`; `状況` (sub `敵・武器・盾・効果`); adventure `離脱する` (sub `報酬なしでマップへ`), tower
    `やめる` (sub `そこまでの WAVE を記録`), tutorial / training `やめる` (sub `マップへもどる`); tower
    `セーブして中断` (§3). Note line: adventure `報酬は入らない・クリアにならない` + `・チケットは減らない` (treasure),
    tower `やめると続きはない（中断なら続きから）`, tutorial `アーケディア城でまたできる`, training
    `カカシはアーケディア城でいつでもできる` (`<訓練名>は<場所>でいつでもできる`).
  - Leaving needs a second press: `本当に離脱する？` / `もう一度押すと離脱`, tutorial / training `本当にやめる？` /
    `もう一度押すとマップへ`. Tutorial / training leave → straight to the map (`_tutorial_quit`, no result).
- 状況 (`BattleStatus`, tabs `敵`, `武器`, `盾と必殺技`, `効果`, `戻る`):
  - 敵 (each living enemy): name (yellow), race (`GameData.race_label`, 月見の島 boss `人間？`), element kanji;
    `HP %d / %d`; right: `スタン %dターン` (yellow) when stunned, else `逃げるまで %dターン` (lucky) /
    `攻撃まで %dターン` (red #ff8080 when `bang` or count ≤ 1); `スタン %d / %d` (yellow when stunned) or
    `スタン なし` (gray, stun_need 0); `弱点` (gray) + weapon kanji (yellow) + `<属性>属性` (element colour);
    `状態` + `毒 %dターン` (#c070ff) `しびれ %dターン` (#f8e050) `暗闇 %dターン` (#8090c0). None: `敵はいない`.
  - 武器 (MAIN, SUB, THIRD): slot name (gold); `なし` (gray) for an empty slot; name + `+%d` (yellow), rarity
    label (rarity colour); `ロック中` / `%dターン` (red) when locked; `ATK %d  <属性 or 無属性>  会心 %d%%  スタン %d`
    (atk + plus); `効果 <名前>` (green #90e0a0; gray for none); `スキル` + names (the next one, `pos`, yellow);
    footer `黄色のスキル：次に使うスキル`.
  - 盾と必殺技: `盾` + name + rarity; `DEF %d  ガード %d%%  HP+%d` (guard = min((def + mirage DEF)·5, 95),
    HP = shield hp + shield_plus); `効果 …`; `今の武器で出せる必殺技` (gold), per special whose skills are all on
    the 3 weapons ("have" test, not the reach search): name (yellow) and `skill > skill > skill`; none:
    `なし（武器のスキルの組み合わせで出る）`.
  - 効果 (MAIN, SUB, THIRD, 盾): `fx_state_battle`: NONE → `武器なし` / `特殊効果なし` (gray); else effect name
    (gray when OFF), state word `["", "発動", "条件つき", "発動中", "条件待ち", "発動しない"]` with colours
    `[gray, #ffe060, white, #ffe060, white, gray]`, `effect_body` lines (gray when OFF), then the note (cream).
    Footer `武器の特殊効果は、メインに付けた武器だけ働く`.
    `fx_state` (menus group §3) then for COND: FK_BOOST `any foe element == strong(v1)`, note
    `対象の敵がいない/いる`; FK_BERSERK_UP `buffs.BF_CRIT`, `ベルセルクをためていない/ためている`; FK_KILLER race in
    (v1, v3), FK_PANIC size == v1 (foe notes); FK_FULLHP_UP hp ≥ max, FK_PINCH_UP / FK_HAISUI hp ≤
    mul_pct(max, v1), note `今の HP %d/%d`; FK_COMBO_UP combo ≥ v1 `今のコンボ %d`; FK_ROUND_POWER stage ≥ 2
    `今はWAVE %d`; FK_KILL_POWER men_kills > 0 `このSTAGEで倒した敵 %d`; FK_GUARD_CUT GC_ELEMENT / GC_RACE foe
    notes, scene `この戦闘は洞窟/草原`. NOW → 発動中, else 条件待ち.

### SNES design (prototyped: `battle_sheet.png`)
- New file **`src/c/battle_ui.c` (bank 6)** with `ui_pause()`, the 状況 pages, §5 `ui_info()`, §6
  `ui_lucky_tip()`, §7 `ui_tut_skip()`. battle_view.c exports `bv_modal_open(x, y, w, h)`,
  `bv_modal_show()` (modal_show + draw_sprites) and `bv_modal_close()`; `battle_ui.h` holds `BX_PANEL 2`,
  `TOP_Y_B 8`. The old `pause_menu` (battle_view.c) is removed. All of it runs while `battle_step` is not
  called: no cost in the battle loop.
- Pause window `modal_open(0, 2, 32, 13)` (rows 2-14, 416 tiles; shared need 7). Rows (box y): 8, 24, 40,
  56, 72 (12-dot) and 88 (8-dot note row).
  - y 8: `{yellow}メニュー` x 10; place x 74 (white, put()): `T_ROW_TUTORIAL` / `T_ROW_TRAINING` (no WAVE) /
    `AREAN_16` (tower) / `T_ROW_MONEY` / `T_PLACE_TREASURE` `{s}の宝の部屋` / `T_PLACE_MEN` `{s} {n}`, 8 px,
    `T_PLACE_WAVE` `WAVE {n}/{m}`. (Port names: the stage list calls the room 宝の部屋 and STAGEs
    `エリア n`; the web's 全角 digits / たからのSTAGE would not match the port's own lists.) Widest
    `蜃気楼の斜塔 WAVE 18/18` ends at 212.
  - y 24: adventure `{yellow}ここまでの報酬` (x 10), +12 `{yellow}コイン {n}`, +12 `{yellow}素材の袋 ×{n}` (bags >
    0) — one line (≤ 242 px); tower `{yellow}倒したボス {n} 体` + 8-dot gray `（カードは負けてももらえる）` on the next
    tile (y + 2).
  - y 40: `宝箱 ×{n}` (n = `b_nitems`) + 8-dot gray `中身はクリアしたあとに開けられる` at x 80; or `{gray}宝箱なし`.
    (Sprites cannot show over the BG3 window, so no chest icons; the chest group's chest art would need BG3
    glyphs here.)
  - y 56 / 72: a 2 × 2 button grid, cursor `TXT_CURSOR` at x 16 / 112, labels at x 30 / 126: `続ける`,
    `状況`, `離脱する` / `やめる` (→ `{red}本当に離脱する？` / `{red}本当にやめる？` after the first press),
    tower only `セーブして中断` (gray until `msnap_ready()`). ←→ toggles the column, ↑↓ the row (pz_sel ^ 1 /
    ^ 2, clamped to the buttons that exist); moving clears the "asked" state.
  - y 88 (8-dot gray): the sub label of the button under the cursor (`敵・武器・盾・効果`, `報酬なしでマップへ`,
    `そこまでの WAVE を記録`, `マップへもどる`, `続きはまた入ると再開`, red `もう一度押すと離脱` /
    `もう一度押すとマップへ` after the first press), and on `続ける` the pause note (`T_PZ_NOTE`, `T_PZ_NOTE_TKT`
    208 px, `T_PZ_NOTE_MIR`, `T_PZ_NOTE_TUT`, `T_PZ_NOTE_TRAIN`). This keeps the web's sub labels and the note
    in one row.
  - A on 続ける / B / START: resume. A on 状況: the status view in the same window, B back. A on 離脱 twice:
    adventure → result 2 (as today); tutorial / training → **result 3** (main.c: no result screen, straight
    to the map, as `_tutorial_quit`); tower やめる twice → result 2 → `commit_mirage(0)` (as today). 中断 →
    result 4 (§3).
- 状況 view (same window): y 8 `{yellow}状況` + the 4 tab names (current yellow, others gray, each from a new
  tile, 8 px apart), page `{gray}{n}/{m}` at x 220 on y 24. L / R tabs, ↑↓ / ←→ page within a tab (wraps),
  B / START back. y 88 8-dot: the tab's footer, else the keys `L/R:タブ ↑↓:つぎ B:もどる`. On 武器 both fit
  (footer x 10, keys x 140); **on 効果 only the footer** (textcheck: footer 176 px overlapped the keys).
  - 敵 (one living enemy per page, in b_e order): y 24 name (yellow) + 8 + race (`RACEN_n`, `T_RACE_SECRET` for
    MON_117) + 8 + `ELN_n` or `無属性`; y 40 `HP {n} / {m}` x 10, x 128 `{yellow}スタン {n}ターン` /
    `逃げるまで {n}ターン` / `攻撃まで {n}ターン` (red variants when `bang || count <= 1`); y 56 `スタン {n} / {m}`
    (yellow when stunned) or `{gray}スタン なし` x 10, x 128 `{gray}弱点` + weapon kanji (yellow) + element (its
    colour) + `属性`; y 72 `{gray}状態` + `{purple}毒 {n}ターン` `{yellow}しびれ {n}ターン` `{blue}暗闇 {n}ターン`
    (each from a new tile). No enemy: `{gray}敵はいない`.
  - 武器 (MAIN / SUB / THIRD): y 24 `{yellow}MAIN` + item name + `TXT_PLUS` + rarity (yellow ≥ SR, else
    gray, as the port's lists); empty `なし`; y 40 `ATK {n}  {s}  会心 {m}%` + `スタン {n}` at x 170, or
    `{red}ロック中 {n}ターン` at x 150 when locked; y 56 `{green}効果 {s}` / `{gray}効果 なし`; y 72 the skills
    from x 10, 8 px apart, the one at `pos` yellow (3 × 72 + 16 = 232 → ends 242; the web's `スキル` label is
    dropped for room); 8-dot footer `黄色のスキル：次に使うスキル`.
  - 盾と必殺技: page 1: y 24 `{yellow}盾` + name + rarity, y 40 `DEF {n}  ガード {m}%` + `HP+{n}` at x 150, y 56
    effect, y 72 `{yellow}今の武器で出せる必殺技` when there are some (they follow on pages 2.., two per page:
    name yellow at y 24/56, chain at y 40/72 x 22 with `{gray}>` between, each on its own tile), or
    `{gray}なし（武器のスキルの組み合わせで出る）` (228 px) when none (the prototype forgot this line on page 1).
  - 効果 (MAIN / SUB / THIRD / 盾, 4 pages): y 24 slot (yellow) + effect name (yellow, gray when OFF) + state word
    at x 180 (`T_ST_STATE_1..5`: `{yellow}発動`, `条件つき`, `{yellow}発動中`, `条件待ち`, `{gray}発動しない`);
    y 40 `FXD_n` (2 lines 14 px apart, `TXT_GRAY_S` when OFF); y 72 the note: `T_ST_TARGET 対象` + slot names
    (ON resonance / type), `FXO_n` (OFF), `T_ST_MAINONLY メインに装備すると発動` (SUB / THIRD), or the NOW / WAIT
    notes (`T_ST_NOTE_*`, numbers hp / hp_max, combo, stage, men_kills). NONE: `武器なし` / `特殊効果なし`.
  - `fx_state(k)` switches on `effects[fx].kind` (FK_RESONANCE / FK_TYPE_UP / FK_BOOST: matching slots by
    `items[w].element / type == v1`; FK_ALL_SR: 3 weapons and the shield ≥ R_SR; FK_BERSERK_UP: a skill with
    `buff == BF_CRIT` on the weapons, then `b_buff_crit`; COND kinds as the web; others ON). `any_foe(what, v1,
    v3)` over living foes (element / race / size).

### Code
battle_ui.c (new, bank 6; `build.py C_BANKS 'battle_ui.c': 6`), battle_ui.h, battle_view.c (exports,
remove `pause_menu`, call `ui_pause`, results 3 / 4, `canvas_wait(TL_WIN, 416)`), main.c (results 3 / 4).

### Costs (measured, battle_ui.c functions)
Pause: `pause_draw` 662, `ui_pause` 247, `btn_text` + `btn_note` 258, `place` 224, `bags` 37 ≈ 1.4 KB.
状況: `fx_state` 1,545, `page_shield` 1,091, `page_foe` 1,073, `page_weapon` 922, `page_fx` 706,
`status_draw` + `status_view` 294, `sp_have` 252, `weak_flow` 162, `any_foe` 150, helpers ≈ 0.5 KB
≈ 6.7 KB. Texts ≈ 2.2 KB (+ FXO_n shared ≈ 1 KB), glyphs `状況件動対象条待働組` ≈ 0.4 KB.
**≈ 10.7 KB** (+1 KB shared). WRAM ≈ 60 B (+ cc65 `-Cl` locals). VRAM: BG3 tiles 87-502 while open; no OAM
(the panel sprites are hidden by the modal as today).

### Risks
- Bank 6 also hosts battle effects later (32 KB is enough for both: 10 KB here).
- The window covers the field like the web's dim; the HP bar / top bar stay visible.
- 8-dot note row lengths: all measured ≤ 232 px (table in `text/`).

### Tests
1. `t/battle.txt` (prototype, passes): pause, 状況 4 tabs, pages, back (`battle_sheet.png`).
2. One effect of each class on MAIN (e.g. 炎の共鳴 with / without a fire SUB, 王者の輝き all SR, ベルセルク
   + ベルセルカー before / after charging, ドラキラー vs a dragon / not, ピンチ at low HP): state word and note
   per the web rules; SUB with an effect → `発動しない` + `メインに装備すると発動`.
3. Enemy page with poison + paralysis (a skill with AIL), a stunned boss, a lucky enemy (逃げるまで, red at 1).
4. Treasure STAGE note `…・チケットは減らない`; tower buttons and note; tutorial / training やめる twice → map
   directly (no result screen).
5. textcheck (prototype: 1 finding on 効果, fixed above): all pages, all pause variants.

------------------------------------------------------------------------------------------------------------

## 5. 戦闘中の情報 (long press → hold L or SELECT)

### Web rules (battle.gd `touch_down` / `touch_up`, `_hold_check`, `_show_weak`, `_show_hero_states`, `_skill_hold_check`, `_show_skill_info`; battle_core.gd `hero_states`; desc.gd `skill`)
- Long press (WEAK_POPUP_HOLD 0.4 s) on an enemy (also retargets it): popup above it: name (15pt white),
  `<種族>・<属性>属性` / `・無属性` (cream), `弱点` (#ffb060) + weapon kanji (white) + `<属性>属性` (element
  colour) or `なし` (gray). On the hero: `hero_states()` lines `name：value` (name #ffd060): `精神統一：スキル消費ST0`,
  `パワーため：ダメージ2倍` (BF_POWER 200 → "2"), `ベルセルク：会心率%d%%` (fx_mult(FK_BERSERK_UP, 150)),
  `スキルロック：<武器名・武器名>が使えない（あと%dターン）`; empty → no popup. The popup closes on release and
  follows the hero's states while held.
- Long press on a skill card (SKILL_INFO_HOLD 0.4 s): `"%s  ST %d" % [name, c.cost()]` (15pt) + `Desc.skill`
  lines (12pt cream; = the port's `SKILLD_n`); the skill is not used (a short tap uses it on release).
- The battle does not stop.

### SNES design (prototyped: `b_info0/1/2.png`)
- L and SELECT are unused in battle today (battle_view.c reads Y / X / A / B / R / ←→ / START in PH_FIGHT,
  A / B in talks and tutorial windows, ↑↓ / A in the tower's chest choice; the L×3 debug is map-only).
- Press L or SELECT in PH_FIGHT → `info_req`; it opens with the pause's gate (`!flash_t && !(banner_sp &&
  banner_t) && warn_t <= 66`) if the button is still held. **While it is held the battle waits** (like the
  pause): `ui_info()` loops on `frame_wait()` until `!(pad & (BTN_L | BTN_SELECT))`, then
  `bv_modal_close()`. No 0.4 s delay (the button is dedicated). Reasons for waiting instead of a live popup:
  the BG3 tiles left during the fight are 49 (463-511), too few for the skill text; drawing text while the
  battle runs costs frames; and the panel text must hide while the window uses its tiles. START can already
  freeze the battle at any time, so this adds no advantage.
- Window `modal_open(0, 2, 32, 10)` (rows 2-11). Modes:
  - enemy (default; ←→ retarget and show the new target, `battle_set_target`): y 8 name, y 24
    `RACEN` / `人間？` + `・` + `ELN` + `属性` or `無属性`, y 40 `{yellow}弱点` + weapon kanji (**white**, as
    the battle popup) + element in its colour, or `なし`; y 60 8-dot gray `←→:ねらい Y/X/A:スキル B:勇者`.
  - Y / X / A: the skill on that column's card (`skill_of(c)`): y 8 `T_IP_SKILL` `{s}  ST {n}` with
    `skill_cost(sk)` (0 under 精神統一, as `c.cost()`), y 24 `SKILLD_n` (≤ 2 lines, ≤ 216 px), y 56 `{gray}NEXT
    {s}  ST {n}` (the NEXT card under it — an addition: the port's panel shows NEXT). Empty column: `なし`.
  - B: hero states (`T_IP_HS_0..7`), one per row y 8 / 24 / 40 / 56 (yellow name, white value from the next
    tile): `精神統一：` `スキル消費ST0`; `パワーため：` `ダメージ{n}倍` (buff_value[BF_POWER] / 100); `ベルセルク：`
    `会心率{n}%` (buff_value[BF_CRIT] × each FK_BERSERK_UP v1 of MAIN and the shield, `mul_pct16`);
    `スキルロック：` `<ITEMN>・<ITEMN>` + `が使えない（あと{n}ターン）`. None: `{gray}いまの状態はない` (the web shows
    nothing; on a button-driven screen a line is clearer).
  - Pressing Y / X / A / B in info mode does not act (the web's long press does not fire the skill either).

### Code
battle_ui.c `ui_info`, `info_foe`, `info_skill`, `info_hero`, `weak_flow`; battle_view.c main loop
(`info_req`).

### Costs (measured)
≈ 1.6 KB code (`info_hero` 640, `info_skill` 282, `ui_info` 264, `info_foe` 257, shared `weak_flow`) +
≈ 0.5 KB text (`↑↓` etc. shared) → **≈ 2.1 KB**. WRAM ≈ 10 B. Battle loop: one pad test per frame.

### Risks
- Holding L while an attack winds up freezes the timing — same as pausing (accepted for the pause already).

### Tests
1. `t/battle.txt` end (passes): hold L → enemy; L + Y → skill + NEXT; L + B → hero states (`b_info*.png`).
2. With 精神統一 charged: L + Y shows `ST 0` and the hero page `精神統一：スキル消費ST0`; after a lock:
   `スキルロック：<名前>が使えない（あと3ターン）`.
3. 3 enemies: L + ←→ walks the targets (the arrow sprite moves after release).
4. Release L → the battle continues the same frame; no skill fired; textcheck clean.

------------------------------------------------------------------------------------------------------------

## 6. ラッキーキャラの演出とはじめての説明

### Web rules (battle.gd `_lucky_entry`, `_lucky_tip`, `_spawn_stage` sparkle; ui/lucky_sparkle.gd; Tuning.LUCKY_TIP)
- At the `fight` event of each wave, for each lucky enemy that is targetable: its sparkle glints, a ring
  (#fff0a0, 8 → 60 px, 0.45 s, width 3) and a burst (16 particles #ffe680, 170 px/s, 0.55 s); if any: SE
  `lucky`, then the tip.
- Sparkle (on every lucky enemy until it is not targetable): small 4-point stars every 0.14-0.28 s at random
  points of the body (rect shrunk by 4 px, upper 85 %), life 0.55 s, radius 5.5-9.5 px, growing and fading
  (sin); a big glint every 1.8 s at 60-80 % x, 18-35 % y of the body, radius 15, life 0.6 s, with 4 rays.
- Tip (once per save: `save.guides["lucky_tip"]`, written at once; only in adventure, not tutorial /
  training): the battle waits (`tut_hold`) under a window: title `レアキャラ出現！` (yellow 18pt), lines
  `お宝の気配…。優先的に狙おう！`, `（逃げられる前に倒そう）`, button `OK`.

### SNES design (prototyped: `lucky_sheet.png`)
- EV_FIGHT → `lucky_entry()`: for each lucky targetable foe `fx_add(foe_x(i) − 16, FOOT_Y − 44, 0)` (the
  existing 32×32 star-burst cell, 10 frames), and if any `se_play(SE_LUCKY)` (exists) and, when `b_mode ==
  BM_NORMAL && !(sv.guides & GD_LUCKY_TIP)`, `tip_due = 1`.
- Tip opens in the main loop when `battle_can_command() && !b_npending && !b_npopups` (like `tut_due`):
  `sv.guides |= GD_LUCKY_TIP; save_write(); ui_lucky_tip();` — window `modal_open(0, 2, 32, 9)`: y 8
  `T_LUCKY_TITLE` x 10, `T_LUCKY_OK {gray}A:OK` x 216, y 24 `T_LUCKY_LINES` (say_wrap, 2 lines, 180 px). A
  closes. (save_write takes a few frames while the battle waits.)
- Sparkle in `overlay_sprites` (cached sprite lists, rebuilt only when the signature changes): signature byte
  7 = `0x80 | ((frame_ct >> 3) & 15) | (((frame_ct & 127) < 32) << 4)` for EK_LUCKY. Stars: 3 per lucky
  enemy from a 16-spot table `spark_xy` (dx, dy from the foe x / feet, inside its 64×56 box), spot
  `(ph + 5k) & 15`, tile small / medium alternating → they move every 8 frames (web 0.14-0.28 s; life
  0.55 s ≈ 33 frames: each spot lives 8 frames × 2 sizes). Glint: 2×2 sprites (16×16) at (x + 4, feet − 48)
  for 32 of every 128 frames (web 0.6 s every 1.8 s). Power-of-two periods keep it free of divisions.
  Stops with `foe_targetable` (the overlay is not drawn then).
- Tiles: FX strip (`make_gfx.py fx()`, OBJ strip 6, palette FX_PAL) free cells row 2 cols 10-15: col 10
  small star (r 2.6, pal[12] cream + white core), col 11 medium star (r 3.6, yellow + white), cols 12-13 rows
  2-3 the glint (4 rays cream / white + star). Measured with the port's converter (`measure_spark.py`,
  `spark_tiles.png`): 8 tiles 256 B raw, 169 B LZSS alone → the FX strip asset grows by ≈ 150 B
  (741 B now).
- `OV_MAX` 9 → 13, `ov_sig[3][7]` → `[3][8]`.

### Costs (measured)
`lucky_entry` 143 B, overlay additions ≈ 0.3 KB, `ui_lucky_tip` 165 B, tip check ≈ 0.1 KB, `spark_xy` 32 B
(RODATA = WRAM), tiles ≈ 0.15 KB, texts 0.1 KB, glyphs `優的狙` (先 shared) ≈ 0.15 KB → **≈ 1.2 KB**.
WRAM ≈ 130 B (ov_spr +96, spark_xy 32, tip_due). OAM: + 3 (stars) or + 4 (glint) sprites per lucky enemy
(≤ 2 lucky per wave → ≤ 8). CPU: the list is rebuilt every 8 frames (≈ 1 k cycles), emitting costs ≈ 200
cycles per frame per lucky enemy (asm `spr_list`). Save: 1 bit.

### Risks
- The money stage (2 lucky + 1 zako, the heaviest battle) gets ≤ 8 more sprites: check sprites per scanline
  (34 tiles) and `prof.py` lag there.

### Tests
1. `t/lucky.txt` (prototype, passes): 小島 宝の部屋 → the tip with A:OK, burst + glint on the golden ones
   (the stars need the new tiles).
2. Second treasure battle: no tip (SRAM dump: the `guides` byte has bit 0).
3. Money stage 60 s: `prof.py` (PROFILE build) — lag % not above today's (4-6 %); OAM dump: ≤ 96 slots used.
4. Lucky flees / dies → its sparkles stop at once.

------------------------------------------------------------------------------------------------------------

## 7. チュートリアル: とばす確認と、ガードのタイミングの助言

### Web rules (ui/tutorial_guide.gd `_show`, `_ok`, `_guard_result`, `_ask_skip`, `_restore`, `_do_skip`; battle.gd `_tutorial_quit`; Tuning.TUTORIAL_TEXT)
- Every tutorial window has two buttons: ok `つぎへ` (info steps and the give-up) or `やってみる` (goal steps,
  retry, hint), and `とばす`. とばす → window `チュートリアルを飛ばす？` (#ff7070) with `飛ばして冒険に出かける`,
  `（アーケディア城でまたできる）`, buttons `もどる` (back to the previous window) / `飛ばす` → `_tutorial_quit`:
  straight to the map, no reward, no clear, no result screen.
- Guard lessons: at the next player turn `_guard_result(goal)`: ok if guard > NONE / just ≥ JUST / miracle ==
  MIRACLE → next step. Else `fails += 1`; miracle and `fails >= TUT_MIRACLE_TRIES` (5) → give-up window
  (`むずかしいね！` / `ミラクルガードは戦いの中で` / `ねらってみよう`, `つぎへ`); else retry window `もう一度！`
  (#ff7070) with the advice by the last result `r`:
  goal guard → `guard_ng` (`ガードが遅かったか、早すぎた` / `攻撃のポーズになったらガードを押そう`);
  r JUST → `near_miracle` (`おしい！ ジャストガードだった` / `もう少し遅くガードを押そう`);
  r GUARD → `early_just` (`ガードできたけど、少し早い` / `当たる直前まで待ってガード`) for the just goal,
  `early_miracle` (`…` / `当たる瞬間ぴったりにガード`) for miracle;
  r NONE → `miss_just` (`タイミングがずれた` / `攻撃が当たる直前にガード`) / `miss_miracle` (`…` /
  `当たる瞬間ぴったりにガード`).

### Port today (battle_view.c `tut_show` / `tut_event`)
Retry body = `TUTX_GUARD_NG` for the guard goal else the step's hint (`TUTH_n`); the title is yellow; the key
is always `A:つぎへ`; no とばす; **give-up after 4 fails for both just and miracle** (web: miracle only, 5).

### SNES design (prototyped: `tut_sheet.png`)
- make_data `build_tutorial`: also `TUTX_NEXT/TRY/SKIP/SKIP_TITLE/SKIP_YES/SKIP_NO` and (fit_lines)
  `TUTX_SKIP_LINES`, `TUTX_NEAR_MIRACLE`, `TUTX_EARLY_JUST`, `TUTX_EARLY_MIRACLE`, `TUTX_MISS_JUST`,
  `TUTX_MISS_MIRACLE` (exact web text, 428 B).
- `tut_event` EV_ENEMY_HIT: `tut_last = e->a` (GD_*); `if (e->a >= need) tut_done(); else if (++tut_fails >=
  D_TUT_MIRACLE_TRIES && g == TG_MIRACLE) give-up; else retry`.
- `tut_show` retry body: guard → `TUTX_GUARD_NG`; `tut_last == GD_JUST` → `TUTX_NEAR_MIRACLE`; `GD_GUARD` →
  just ? `TUTX_EARLY_JUST` : `TUTX_EARLY_MIRACLE`; else just ? `TUTX_MISS_JUST` : `TUTX_MISS_MIRACLE`. Retry
  title `TXT_RED_S`. Key line 8-dot gray at box y 72, x 150: `T_TUT_KEYS` `A:{s}  START:とばす` with `{s}` =
  `TUTX_NEXT` (info steps, give-up) or `TUTX_TRY` (goal steps, retry, hint) (replaces `T_TALK_NEXT`).
- In the tutorial-window loop: START → `ui_tut_skip()` (same window): y 8 `{red}` `TUTX_SKIP_TITLE`, y 24
  `TUTX_SKIP_LINES` (2 lines), y 56 cursor + `TUTX_SKIP_NO` x 46 / `TUTX_SKIP_YES` x 126 (←→, default もどる),
  y **64** 8-dot `T_TUT_SKIP_KEYS` `←→:えらぶ A:けってい` (the prototype's y 74 overflowed the 11-row window:
  textcheck). B or もどる → `tut_show(tut_msg)` again; 飛ばす → `battle_scene` returns 3 → main.c skips
  `result_scene` (no `commit_lose`, so the tutorial's practice enemy is not counted, as the web) → map.
- START is otherwise the pause; while a tutorial window is open the pause cannot open, so there is no clash.

### Costs (measured)
`ui_tut_skip` 317 B, `tut_show` / `tut_event` +≈ 0.25 KB, TUTX 428 B, `T_TUT_KEYS` / `T_TUT_SKIP_KEYS` 70 B,
glyph 飛 42 B → **≈ 1.1 KB**. WRAM 1 B.

### Tests
1. `t/tut.txt` (prototype, passes): first window `A:つぎへ START:とばす`, START → confirm, B → back, START →
   → → A → map without a result screen.
2. Guard lesson 2/4 (just goal): press B early (GUARD) → `ガードできたけど、少し早い / 当たる直前まで待ってガード`;
   no press → `タイミングがずれた / 攻撃が当たる直前にガード`; never a give-up on the just lesson.
3. Miracle lesson: a JUST → `おしい！ ジャストガードだった…`; the 5th failure → `むずかしいね！`.
4. textcheck over all tutorial windows (prototype finding fixed: y 64).

------------------------------------------------------------------------------------------------------------

## 8. textcheck findings of the prototype (all fixed in the text above)

```
tc_detail.txt  colour  box 3 TXT_MORE_DOWN  x 236 y 184 gray over yellow      -> weakness column x 136, end <= 228
tc_battle.txt  overlap box 2 T_ST_KEYS      x 140 y 88 over T_ST_FX_FOOT       -> 効果 tab: footer only
tc_tut.txt     bottom  box 2 T_TUT_SKIP_KEYS x 10 y 74                          -> y 64
tc_mirage.txt  bottom  box 3 T_MIR_RESTART  x 40 y 166                          -> rows y 136 / 152
```
Also seen in the screenshots: the one-role boss title used `{s}` that was gone by draw time (showed the area
name) → `T_DT_SR_1..5`; the 盾と必殺技 page 1 missed the `なし（…）` line.

## 9. Totals (measured prototype; asm snapshot estimated)

| | ROM | WRAM |
|---|---|---|
| code (battle_ui 10,043; map +7,815; battle_core +1,912; save +688; battle_view +673; adventure/rewards +570; asm +113; low-WRAM stubs +120; misc +128) | 22.1 KB (21.1 KB with the asm snapshot) | BSS +560, RODATA +42, low WRAM +120 |
| plan table | 1.3 KB | – |
| texts: 177 UI + 12 TUTX strings + table | 4.7 KB (+ FXO_n ≈ 1 KB shared with menus) | – |
| 29 glyphs (6 shared with menus) | 1.2 KB | – |
| sparkle tiles | ≈ 0.15 KB (GFX bank) | – |
| **total** | **≈ 29.5 KB** (+1 KB shared) | **≈ 0.72 KB** (6.6 KB free after) |

SRAM: +4 B in Save (v2 block), + 2 × 512 B snapshot records at $3E00 / $7E00.
VRAM: none new (BG3 window tiles 87-502 while a battle window is open; FX strip cells already in VRAM).
OAM: ≤ 8 more sprites with lucky enemies. CPU in the battle loop: lucky ≈ 200 cycles / frame / lucky enemy;
tower autosave ≈ 40 + 90 scanlines in two frames once per player turn (after the asm change; C prototype
1.1 frames).

## 10. Questions for the user

1. 戦闘中の情報 (L / SELECT): the battle waits while the button is held (proposed). The web's long press does
   not stop the battle. OK to wait?
2. 再挑戦 is **R** on the result screen (A / START stay マップへ; X / Y are the N・R 一括売却 keys). OK?
3. Tutorial: **START** = とばす in the tutorial windows. OK?
4. 蜃気楼の斜塔: like the web, the run is saved to SRAM at every player turn, so turning the power off resumes
   from the start of that turn (続きから). Keep this, or save only when 中断 is chosen?
