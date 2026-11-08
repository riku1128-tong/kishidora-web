# Spec: menus, settings and records (SNES port of キシドラ！)

Group "Menus, settings and records". Nine items. Everything below was checked against the decompiled web
source (`scratchpad/src/*.gd`, `scratchpad/src/ui/*.gd`) and the port (`kishidra_sfc/src`). Measurements were made
in a copy of the port: `scratchpad/plan/menus_settings/work` (prototype code, do not ship as is: it has
`dbg[]` timing hooks).

Files in `scratchpad/plan/menus_settings/`:

| file | what |
|---|---|
| `new_ui.txt` | every new / changed UI string, in `src/text/ui.txt` format (94 strings, 2152 B, 15 new glyphs) |
| `proto_src.patch` | prototype diff vs the port: settings rows + volume (sound.c/sound.s/main.c/menu.c), `sp_reach()` (adventure.c) |
| `proto_make_gfx.patch` | title logo baked into the title picture (make_gfx.py `title()`) |
| `proto_sp_reach.c` | the working, verified `sp_reach()` C code (copy of the prototype) |
| `reach_sim.py` | Python model of the web BFS (`adventure.gd reachable_specials`) and of the SNES algorithm; 0 mismatches in 8500 random loadouts |
| `fx_state_gen.py` | prototype of the make_data generator for 特殊効果のようす (`desc.gd fx_state`) |
| `measure_logo.py`, `measure_keys.py`, `measure_keys2.py` | ROM size measurements with the port's own converter (`tools/gfx/snesgfx.py`) |
| `title_baked.png`, `title_sprites.png`, `keys_conv.png` | previews |
| `t/*.txt`, `t/*.png` | harness scripts and screenshots of the prototype |

Conventions used below: "box y" = pixel y inside a BG3 box (`say(box, x, y, ...)`); window tile coords are
`win_open(id, tx, ty, tw, th, tile, pal)`. Lines of different colours in the same tile columns must be 16 px
apart (`LINE_C`); lines of one colour may be 14 px apart (`LINE_H`). Inner text area of a window: x 8..(tw*8-8),
y 8..(th*8-8).

---------------------------------------------------------------------------------------------------------------

## 0. Shared prerequisites (needed by this group; most are shared with other groups)

1. **Save format v2 with a migration** (`src/c/save.c`, `save.h`, `tools/harness/mksave.py`). `save.c` accepts only
   `ver == SAVE_VER && size == sizeof(Save)`, so any new field makes every existing save unreadable (the user
   plays on snes9x with a real save). All groups' new fields must go into ONE new block placed just before
   `u16 ninv;` (inv[] must stay last because `body_len()` drops the unused inventory tail), and SAVE_VER goes
   1 -> 2 once, with a v1 reader:
   - v1 layout (measured): header 12 B, `ninv` at offset 628, `inv` at 630, size 8702 (`mksave.py` reports 8702).
   - `read_slot()`: also accept `ver == 1 && size == 8702`.
   - `load_slot()` for v1: read 8702-12 bytes into `&sv + 12`, checksum over the v1 body
     (`8702 - 12 - (1009 - ninv) * 8` bytes, ninv read at offset 628), then
     `memmove(&sv.ninv, (u8 *)&sv + 628, 2 + ninv * 8)`, `memset(<new block>, 0, <its size>)`, set
     `ver = 2`, `size = sizeof(Save)`, then run the per-feature "v1 init" hooks (this group's: see 7 and 6).
   - `mksave.py` must write v2 (insert the new block before `ninv`) and keep a `--v1` switch to make v1 saves for
     the migration test.
2. **Text and font bank room** (`tools/make_data.py write_text`). Measured now: strings in $C1 with 3878 B free,
   fonts in $C2 with 3158 B free (each glyph 24 + 16 + 2 = 42 B: about 75 more glyphs). This group alone adds
   ~2.2 KB of fixed strings + ~1.2-2.2 KB of generated effect notes and 15 glyphs (630 B). Needed: put the fonts
   in their own bank (text.s addresses `font_glyphs` / `font_glyphs_s` by `^bank`, so only make_data's layout
   changes) and let strings use $C1..$C2 fully. Cheap extra win: content-dedupe in `write_text` (identical code
   sequences share one body; the table entry just points to it).
3. **Regenerating data pulls in the newer web build.** `python build.py data` against the current
   `/home/user/kishidora-web` (version.json "2026-11-07 17:40") changes 355 files in `data/gen` (boss / hero
   gfx, `gdata.h`: e.g. `N_BOSSFX 23 -> 24`, `BFX_CHAMPAGNE` inserted at 21 so `BFX_DARKRAY` moves 21 -> 22,
   `tables.c sp_fx` columns change). Text and font come out byte-identical. Either accept the regeneration
   once for everyone (visual check) or regenerate only text: running the `Gen` steps without `make_gfx.main()`
   works (verified):
   ```
   python3 -c "import sys; sys.path.insert(0,'tools'); import make_data as m; g=m.Gen(); g.build_digits(); g.build_ids(); g.build_consts(); g.build_tables(); g.build_estats(); g.build_dex(); g.build_story(); g.build_name_grid(); g.build_difficulty_text(); g.build_tutorial(); g.build_ui_text(); g.write_text(); g.write()"
   ```
   (it still rewrites gdata.h / tables.c from the newer web data; a `build.py text` target that runs only the
   text part would be cleaner).
4. **GFX bank space**: this group adds 5.5 KB of key-item art and saves 0.47 KB on the title (net +5.0 KB of the
   ~56 KB free in GFX_BANKS).
5. **cc65 code generation hazard (found while prototyping).** With the port's flags (`-O -Oirs -Cl`, 65sc02), a
   small static helper `static u8 f(u8 x) { ... rs_have[x >> 3] ... }` called with a byte read through a pointer
   (`f(sp->sk1)`) used a stale X register as the high byte of the index; the same expression written inline with
   u8 temporaries (`e = x >> 3; n = x & 7; rs_have[e] & rs_bit[n]`) is correct. Rule for new code: compute byte
   indexes into u8 temporaries first, and compare results against a Python model (as done for `sp_reach`).
6. **`sp_reach()` is shared**: the status screen (3), the dex "今の装備で出せる！" mark (today an approximation in
   dex.c `sp_ready`), and other groups' items that the web builds on `reachable_specials` (list filter
   必殺/必殺のみ/必殺以外 = `solo_ok`, おすすめ, そうび詳細 "この武器 1 本で出せる必殺技", gacha "必殺" tags, the
   必殺10連 gacha). Give it a generic signature (see 3).
7. **The root menu** (`menu.c menu_scene`) gets new rows from this group (ステータス rename, 大切なもの, 討伐記録,
   NEW tags, gray 鍛冶屋). Other groups adding root rows must use the same row table (see 3/7 below).

---------------------------------------------------------------------------------------------------------------

## 1. Settings: BGM / SE volume, and settings from the title

### Web rules (settings_screen.gd, game.gd, title.gd)
- `Game.settings = { "bgm": 8, "se": 8, "guard_late": Tuning.GUARD_LATE_FRAMES (4), ... }`, clamped `bgm`/`se`
  0..10, `guard_late` 0..`GUARD_LATE_MAX` (10). Kept in `user://settings.json`, separate from the save:
  "冒険の記録を全部消して、タイトルにもどる（設定は消えない）".
- `_apply_settings`: per bus `v = settings[k] / 10.0`; `set_bus_mute(v <= 0)`;
  `set_bus_volume_db(linear_to_db(max(v, 0.001)))` -> the volume is LINEAR amplitude v/10.
- Screen: title band "設定"; for each of BGM and SE: label ("BGM の音量" / "効果音の音量", gold), "−" and "+"
  buttons, 10 cells (filled `#58c8f0` for i < v, else dark), value text under the cells: "なし" when 0 else
  `v * 10` (gray). Changing SE plays SE "guard1" (`SE_SAMPLE`).
- Guard: label "ガードのタイミング", "%d フレーム（%s）" with ふつう/早め/遅め vs 4, a long touch-latency help text.
  (The port already shows "ガードの遅れ補正 {n} フレーム"; keep the port's wording, no change needed.)
- "データの初期化" (3-step confirm) and "バージョン": not needed (the port's はじめから confirm replaces the reset).
- Title: a small "設定" button opens the same screen (`_open_settings`).

### Port today
- `save.h`: `u8 bgm_vol, se_vol` exist, `save_new()` sets 8/8; nothing reads them.
- `ui.txt`: `T_OPT_BGM "BGM の音量 {n}"`, `T_OPT_SE "効果音の音量 {n}"` exist, unused.
- `sound.s`: `bgm_volume` (driver command 4 = BGM voice VOL L/R, also used by every later START) exists, never
  called; `se_start` sends `se_vol[x]` (per-sample level from make_audio) as the voice volume; its comment
  mentions "ea1 = volume scale" but the code ignores ea1.
- `menu.c options_scene`: one row (guard) + もどる.

### SNES design (prototyped and measured)
- `sound.s`: new `_se_scale` (LOWBSS, 1 B, exported). In `se_start`, after `lda f:se_vol,x`:
  `sta WRMPYA; lda _se_scale; sta WRMPYB; nop x4; lda RDMPYH; beq @out; sta s_p3` (level * scale / 256; 0 =
  no sound). +18 B (measured).
- `sound.c`: `void sound_volume(u8 bgm, u8 se)` with tables
  `bgm_lv[11] = {0,13,25,38,51,64,76,89,102,114,127}` (127 v/10 -> `ea[0]`, `bgm_volume()`) and
  `se_lv[11] = {0,26,51,77,102,128,154,179,205,230,255}` (256 v/10 -> `se_scale`). +70 B (measured). Declare in
  sound.h.
- `main.c`: right after `if (!save_load()) save_new();` call `sound_volume(sv.bgm_vol, sv.se_vol);`
  (s_ready is already 1 there: spc_boot runs in crt0). Also call it after `save_load()` in the SC_TITLE branch.
- Loudness: the port plays BGM at VOL 127 today. With the linear map the default 8 gives VOL 101 (-2 dB).
  Recommended: raise the DSP master volume in `tools/audio/driver.py` init from `(0x0C, 0x60), (0x1C, 0x60)` to
  `0x78` (x1.25) so setting 8 sounds exactly as today and 10 is +2 dB (needs `python build.py audio`; coordinate
  with the BGM group, who re-run it anyway). Risk: at 10 the mix has less headroom (possible clipping with many
  SEs). Without the driver change the default is simply 2 dB quieter than today.
- Measured with the harness (wav capture of the menu with the prototype): BGM 8 -> 5: RMS 3204 -> 1953 (ratio
  0.61, expected 0.625); SE 8 -> 4 with BGM 0: RMS 811.5 -> 405.8 (0.50).
- Settings screen (`menu.c options_scene(u8 from_title)`; make it non-static so title.c can call it):
  window `win_open(BX_LIST, 2, 4, 28, 22, T_LIST, PAL_W)`; cursor `mk_cursor(16 + 8, 32 + row_y + 2)`.

  | row | box y | content |
  |---|---|---|
  | 0 BGM | 8 | `T_OPT_BGM` at x 20; value at x 160: `TXT_N_GRAY` with v*10, or `TXT_GRAY_S` + `T_DEX_NONE` ("なし") when 0 |
  |  | 24 | 10 cells at x = 24 + 16 i: `T_OPT_CELL_ON` "{blue}●" for i < v, `T_OPT_CELL_OFF` "{gray}●" (16 px pitch: each cell owns 2 tiles, so blue/gray never share a tile) |
  | 1 SE | 48 / 64 | the same with `T_OPT_SE` |
  | 2 guard | 88 | `T_OPT_GUARD` (existing) |
  | 3 rename | 112 | `T_OPT_RENAME` "名前の変更" (row absent when from_title) |
  | 4 back | 136 | `T_OPT_BACK` |
  | keys | 160 | `T_OPT_KEYS` "{gray}←→:かえる A:けってい B:もどる" |

  Controls: ↑↓ row (skip the rename row from the title), ←→ on rows 0-2 change by 1 (0..10), call
  `sound_volume()` at once, play `SE_GUARD` for the SE row (web "guard1"), `SE_CURSOR` otherwise; A on 名前の変更
  -> `name_entry(1)` (item 5) then redraw; A on もどる or B -> `if (!from_title || save_exists()) save_write();`
  then `fade_out()`. (From the title with no save the values stay in RAM and are carried into the new game, see
  below; nothing is written, so つづきから does not light up.) From the title the head box shows only "設定"
  (no coins: menu.c `head()` prints `sv.coins`, add a `head_plain()`).
  The prototype used `TXT_ORB_2`/`TXT_ORB_0` for the cells (same glyphs): screenshot `t/opt4.png`.
- Settings survive はじめから (web: settings are not part of the save): `save_new()` keeps `bgm_vol`, `se_vol`,
  `guard_late` when `sv.magic == "KSFC"` (RAM holds a save), clamped to 10; otherwise 8 / 8 / 0 (0 = today's
  `save_new` value for guard_late). Then `sound_volume()` is unchanged.
- Title (`title.c`): box `box(0, 9, 17, 14, 8, 1, PAL_W, BOX_WINDOW)` (was height 6) with three rows at box y 9,
  25, 41: `T_CONTINUE` / `T_CONTINUE_GRAY`, `T_NEWGAME`, `T_MENU_OPTION` ("設定"); copyright box moves to tile
  `1 + 14 * 8 = 113` (row 25 unchanged). `box_pal(0, 1, 1 + 2 i, 12, 2, ...)` for i = 0..2. ↑↓ cycle the enabled
  rows (row 0 only with a save). A/START on 設定: `fade_out(); options_scene(1);` then rebuild the title screen
  (put the screen set-up of `title_scene` in a local `title_draw()` and loop); BGM keeps playing (bgm_play of the
  same song is a no-op).
- Data: none (strings only). No save fields (already exist).

### Costs
- ROM: code measured +465 B (menu.c options, prototype) + 70 (sound.c) + 18 (sound.s); title rows and save_new
  ~+250 B estimated; text ~100 B; 0 new glyphs. **~0.9 KB**.
- WRAM: 1 B (`_se_scale`) + 22 B of tables (RODATA runs in WRAM $7E and loads from bank $80).

### Risks
- Calling `bgm_volume()` while the SE samples are still being uploaded after boot: command 4 does not touch
  the LOAD pointer (driver.py `c_vol4`), safe; verified (the prototype calls it before the title).
- A save with bgm_vol/se_vol > 10 (none exist; mksave writes 8/8) is clamped in `sound_volume`.

### Tests
1. `t/opt.txt` style script (load a save, title A, map Y, menu ↑↑ = 設定, A): `shot`, `wav` 180 frames, LEFT x4,
   `wav` again; RMS ratio ≈ new/old setting (prototype: 0.61 for 8 -> 5).
2. SE: set BGM 0, record cursor SEs at SE 8 and SE 4 (`t/se.txt`): RMS ratio 0.5; SE 0: no SE energy.
3. Title: power on with no SRAM, ↓↓ 設定, change BGM to 3, back, はじめから: `wram` dump, `sv.bgm_vol == 3`
   (Save offset 108 = `_sv` address from build/kishidra.map + 108; se_vol 109, guard_late 106) and no save in SRAM
   until the first `save_write`.
4. textcheck.py on scripts 1 and 3 (PROFILE=1 build): clean.

---------------------------------------------------------------------------------------------------------------

## 2. Title logo

### Web rules (title.gd)
`LOGO_PATH = res://assets/title/logo.png` (186 x 74, 15 opaque colours, pixel art drawn with NEAREST at scale
`floor((360 - 24) / 186 * 4) / 4 = 1.75`), centred, top at `H * 0.12`. (Also `credit.png` at the bottom; the
port already prints its © line.)

### SNES design: bake the logo into the title picture (measured; recommended)
`tools/make_gfx.py title()` (patch in `proto_make_gfx.patch`):
```python
im = self.img('assets/title/title_bg.png').resize((256, 224), Image.NEAREST).convert('RGBA')
logo = self.img('assets/title/logo.png').convert('RGBA')        # its own pixel size (1x)
im.alpha_composite(logo, ((256 - logo.width) // 2, 24))         # x 35, y 24..97 (12 % of 224 = 27)
self.bg('TITLE', im, npal=6, pal_base=2)
```
- Measured: TITLE 897 -> 882 tiles, tiles 28704 -> 28224 B (stored raw): **-480 B ROM**. No code, no VRAM, no
  OAM, no palette change. Screenshot of the built ROM: `t/title_logo.png` (from the harness), preview
  `title_baked.png`.
- 1x keeps the pixel art exact (73 % of the screen width; the web shows 90 %). The menu box (rows 17-24) and the
  © line are below the logo. `difficulty_select()` also uses the TITLE picture, under a full window: the logo is
  hidden there (fine).
- Alternative (if the baked palettes ever look worse): the logo as OBJ: 12 32x32 + 10 8x8 sprites, tiles 64-255
  (king area, free on the title) + 10 in 12-63, OBJ palette 1, 3.3 KB LZSS, max 24 slivers per line; hide it
  while `mk_confirm` is open (sprites are above the low-priority dialog). Measured in `measure_logo.py`.

### Tests
`t/title.txt` (wait 300, shot) on the rebuilt ROM; compare with `title_baked.png`; textcheck on the title.

---------------------------------------------------------------------------------------------------------------

## 3. Status screen (ステータス)

### Web rules (menu_screens.gd `_build_status`, `_fx_page`; desc.gd `fx_state`; battle_core.gd; adventure.gd)
- Title "ステータス". First line: `"HP %d   ST %d   ガード %d%%" % [hp, st_max, guard]` where
  - `hp = calc_hp_max() = mul_pct(HERO_HP_BASE (100) + shield.hp + shield_plus, 100 + Σ FK_HEART.v1)` over
    `fx_list()` = the MAIN weapon's effect and the shield's effect (`equipped_rows`); `shield_plus = enh_sum *
    ENH_SHIELD_HP (10)`; mirage buffs are 0 outside battle.
  - `st_max = HERO_ST_MAX` (10).
  - `guard = min(shield.def * GUARD_PCT_PER_DEF (5), GUARD_PCT_MAX (95))`.
- Four slot panels (メイン/サブ/レンタル/盾; port: MAIN/SUB/THIRD/盾): name + rarity label; stat text
  (weapon: "ATK %d" (atk + enh_sum) + "  " + element kanji 炎/水/風/光/闇 (none for EL_NONE); shield:
  "DEF %d  HP+%d" (hp + enh_sum * 10)); weapons: skills joined with "・", or "スキルなし"; "くわしく ›" opens the
  item info.
- "今の装備で出せる必殺技" (gold), then one line per special `"%s　%s" % [name, " > ".join(skill names)]`, or
  "なし（武器のスキルの組み合わせで出る）" (gray). The list is `adv.specials_possible(lo)` =
  `reachable_specials`: an exact search (below), not "has the skills".
- "特殊効果のようす" (gold), per slot k: slot name (gold); `st = Desc.fx_state(lo, k)`:
  - NONE: "武器なし" (weapon slot, no weapon) or "特殊効果なし" (gray). (The web's rental text
    "借りた武器の効果は働かない（メインだけ）" does not apply: THIRD is an owned weapon, treat it like SUB.)
  - else effect name (yellow; gray when OFF), state word right-aligned in `STATE_COLORS`:
    `STATE_WORD = ["", "発動", "条件つき", "発動中", "条件待ち", "発動しない"]` (outside battle only 1, 2, 5 occur;
    colours: 発動 #ffe060 yellow, 条件つき white, 発動しない gray), the effect body lines (`Desc.effect_body` =
    the port's `FXD_n`, white, gray when OFF), then the note `st[1]` (cream).
  - fx_state rules: k = 1, 2 (SUB, THIRD) with an effect -> [OFF, "メインに装備すると発動"]. For MAIN and the
    shield, by effect kind: FK_RESONANCE: weapons of element v1 among the 3 slots -> [ON, "対象 " + their slot
    names] else [OFF, "%s武器を装備していない" % _elem(v1)]; FK_TYPE_UP: same by weapon type ("%sを装備していない");
    FK_ALL_SR: all 3 weapons and the shield rarity >= SR -> ON else [OFF, "全部 SR・SR+ ではない"]; FK_BOOST:
    weapon of element v1 -> [COND, "%s敵がいるとき" % _elem(strong of v1)] else OFF (as RESONANCE); FK_BERSERK_UP:
    any equipped weapon has a skill with buff BF_CRIT -> [COND, "ベルセルクをためたとき"] else
    [OFF, "ベルセルクのスキルがない"]; FK_KILLER [COND, "%sがいるとき" (races joined with "・")]; FK_PANIC
    [COND, "%sサイズの敵がいるとき"]; FK_FULLHP_UP [COND, "HP が満タンのとき"]; FK_PINCH_UP / FK_HAISUI
    [COND, "HP が最大の %d%% 以下のとき" % v1]; FK_COMBO_UP [COND, "コンボが %d 以上のとき" % v1];
    FK_ROUND_POWER [COND, "WAVE 2から"]; FK_KILL_POWER [COND, "敵を倒すほど"]; FK_GUARD_CUT: GC_ALL -> ON,
    GC_ELEMENT [COND, "%s敵の攻撃のとき" % _elem(v3)], GC_RACE [COND, "%sの攻撃のとき"], scene
    [COND, "洞窟の戦闘のとき" / "草原の戦闘のとき"]; every other kind [ON, ""]. `_elem(e)` = "無属性の" for
    EL_NONE else kanji + "の". All 86 surface effects with their class and texts are listed by `fx_state_gen.py`
    (38 COND, 27 ON, 10 RESONANCE, 5 BOOST, 3 TYPE, 1 NONE, 1 BERSERK, 1 ALL_SR; 53 distinct texts, 1162 B).
- `reachable_specials(lists)`: each weapon cycles its skills in order; pressing a weapon uses its current skill
  and advances it; an attacking skill (hits > 0) is appended to the history (max `COMBO_HISTORY_MAX` = 3),
  buffs (hits 0) are not; the longest special whose sequence equals the end of the history fires (ties: the
  first in SPECIAL_TABLE) and clears the history, otherwise the history keeps its last 2. A special is
  reachable if some press sequence from all positions 0 fires it. The port's battle code does exactly the same
  (`battle_hit.c find_special / hist_push`).

### SNES design
**Data (make_data.py, next to the FXN_/FXD_ texts in `build_tables`)**:
- `const u8 fx_rule[N_EFFECT]`: high nibble class `FR_NONE 0, FR_ON 1, FR_COND 2, FR_RESO 3, FR_TYPE 4,
  FR_ALLSR 5, FR_BOOST 6, FR_BERSERK 7`; low nibble parameter (element index for RESO/BOOST, weapon type index for
  TYPE). Logic: `fx_state_gen.py` (port of desc.gd fx_state; uses `descs._elem`, `descs._race`, `descs.SIZE`,
  `descs.WTYPE`). 86 B (RODATA = WRAM).
- Texts `FXS_n` (note for COND / BOOST / BERSERK; " " if none) and `FXO_n` (OFF reason for RESO / TYPE / ALLSR /
  BOOST / BERSERK; " " if none), consecutive families so `FXS_0 + fx` works. ~2.2 KB as 172 strings, ~1.7 KB with
  content dedupe (shared need 2).
- UI strings (see `new_ui.txt`): `T_MENU_STATUS` becomes "ステータス" (was "必殺技"; this also makes the tutorial's
  "「ステータス」で確かめよう" point at a real menu row), `T_ST_TAB_0..2` 装備/必殺技/特殊効果, `T_ST_HPST`
  "HP {n}   ST {m}", `T_ST_GUARD` "ガード {n}%", `T_ST_ATK`, `T_ST_SHIELD` "DEF {n}  HP+{m}", `T_ST_NOSKILL`,
  `T_ST_SP_HEAD` "{gold}今の装備で出せる必殺技", `T_ST_SP_NONE`, `T_ST_FX_HEAD` "{gold}特殊効果のようす",
  `T_ST_FX_NOWEAPON`, `T_ST_FX_NONE`, `T_ST_STATE_1` "{yellow}発動", `T_ST_STATE_2` "条件つき",
  `T_ST_STATE_3` "{gray}発動しない", `T_ST_FX_MAINONLY`, `T_ST_FX_TARGET` "対象", `T_ST_KEYS`
  "{gray}L/R:ページ A:くわしく B:もどる". Element kanji: existing `ELN_n`; slot names: `T_SLOT_MAIN..`.
- New glyphs: 件 動 対 象 条 組.

**`sp_reach` (adventure.c, bank 7; shared)** — verified prototype in `proto_sp_reach.c`:
- Signature for sharing: `u8 sp_reach(const Inv *const *w, u8 nw, u8 *out, u8 *scratch);` (w = up to 3
  weapons, NULL = empty slot; out = 2 bytes, bit n = special n; returns the count). The prototype reads
  `sv.equip[0..2]` directly; wrap that as `sp_reach_equipped(out, scratch)`.
- Algorithm (exact, matches the web BFS on 8500 random and targeted loadouts in `reach_sim.py`, and on 5 loadouts
  run in the emulator vs the Python model): candidates = specials whose skills are all on the weapons (others can
  never fire). History is kept as its longest suffix that is a proper prefix of a candidate ("prefix", 0-2
  skills; at most 28, measured max 8). State = prefix * 32 + p0 + 3 p1 + 9 p2. A transition table
  `tr[prefix * 9 + weapon * 3 + pos]` (0x80 | special when one fires and the history clears, else next prefix;
  buffs keep the prefix) is built first; then bit sets seen/done are swept byte by byte until no new state.
- Scratch: `2 * 112 + 28 * 9 = 476` bytes lent by the caller (menu.c passes `(u8 *)order`, 2018 B, free while the
  status screen runs; dex.c can pass `pmap` (640 B) while no card is open).
- Measured: code 2235 B (CODE7 9680 -> 11915), static arrays 206 B BSS + 35 B RODATA, run time 2-9 frames
  (worst seen 9 frames = 0.15 s with 5 candidates) on snes9x; the first naive versions took 92 / 32 / 20 frames
  (u16 division, struct indexing and sweeps over every state are what cost).
- dex.c: replace `sp_ready() == 2` by the `sp_reach` bit (web dex uses `specials_possible` too).

**Screen (`menu.c status_scene()`, replaces `specials_scene`)**: head box with `T_MENU_STATUS` and the three tab
names (current tab yellow, others gray, each on its own tiles, like `dex.c head()`); window
`win_open(BX_LIST, 0, 2, 32, 26, T_LIST, PAL_W)`; `T_ST_KEYS` at box y 186. L/R switch tabs (`list_run(&l,
BTN_L | BTN_R)`), B leaves. Computed once on entry: hp, guard, `sp_reach` bits, fx states.
- Tab 装備: y 8: `T_ST_HPST` (targ_num hp, targ_num2 10) at x 10, `T_ST_GUARD` at x 150. Then a `List` of the 4
  slots, `l.y = 24`, `l.lh = 40` (row bases 24, 64, 104, 144: on tile rows), 4 rows: line 1 (base): slot label
  `TXT_YELLOW_S` + `T_SLOT_*` at x 14, name with `mk_item_name(BX_LIST, 56, y, it, 1)` (max 108 px + the +N column
  at 168), rarity `TXT_GRAY_S` + `RARN_*` at x 200; line 2 (base + 14, white, x 56): `T_ST_ATK` (atk + enh_sum)
  and the element kanji `ELN_n` 8 px after it (weapons, none for EL_NONE), `T_ST_SHIELD` (def, hp + enh_sum * 10)
  (shield); line 3 (base + 26, white, weapons): the skill names from x 10, 8 px apart like `mk_item_detail`
  (worst case 3 x 72 + 16 = 232 px, ends at 242 <= 248; the web joins them with "・", which would need 250 px),
  or `T_ST_NOSKILL`. Empty slot: `T_SLOT_EMPTY` at x 56. Line 3 ends at base + 37, before the next row's yellow
  label on a new tile row (base + 40); the yellow label (x 14-44) and the gray rarity (x 200+) share tile rows
  only with empty columns of line 2. Shield row ends at 170; keys at 186.
  A on a slot with an item: the item card `info(idx)` window (rows 17-27, `mk_item_detail`), B closes it.
- Tab 必殺技: y 8 `T_ST_SP_HEAD`; a `List` of the reachable specials (`l.y = 24`, `l.lh = 32`, 5 visible):
  name yellow (`SPN_n`), chain on the next line (y + 16) like today's `sp_row` (gray `TXT_NEXT_ARROW` on its own
  tile between names). None: `T_ST_SP_NONE` at y 24. (The full 13-special list with "skills missing" stays in the
  dex's 必殺技 tab.)
- Tab 特殊効果: y 8 `T_ST_FX_HEAD`; a `List` of the 4 slots (`l.y = 24`, `l.lh = 56`: bases 24, 80, 136 on tile
  rows; 3 visible, ↓ scrolls to 盾): base: slot label yellow x 14, effect name (`FXN_n`, yellow; gray when OFF)
  x 56, state word x 196 (`T_ST_STATE_1/2/3`); base + 16: `FXD_n` at x 24 (white; `TXT_GRAY_S` when OFF; up to 2
  lines 14 px apart, longest FXD_83 218 px -> ends 242); base + 44 (base + 30 when FXD has 1 line): the note
  `FXS_n` / `FXO_n` / `T_ST_FX_MAINONLY`, or for an ON RESO/TYPE effect `T_ST_FX_TARGET` followed by the slot
  names whose weapon matches. The note takes the FXD's colour (white, or gray when OFF: the web's cream note
  would share a tile row with a gray FXD line). NONE: `T_ST_FX_NOWEAPON` / `T_ST_FX_NONE` at base. A: item card.
- Fx state at run time (`fx_state(k)` in menu.c): `e = items[id].effect`; FX_NONE -> NONE; k 1/2 -> OFF +
  T_ST_FX_MAINONLY; else switch `fx_rule[e] >> 4` as in the web rules (RESO: `items[w].element == param` over
  the 3 equipped weapons; TYPE: `items[w].type == param`; ALLSR: 3 weapons present and every rarity >= R_SR and
  the shield's rarity >= R_SR; BOOST: like RESO -> COND; BERSERK: any skill of the 3 weapons with
  `skills[s].buff == BF_CRIT` -> COND).

### Costs
- ROM: sp_reach 2.2 KB (measured) + screen ~3.5 KB (estimate: menu.c averages 31 B per source line; ~110 lines)
  + texts ~0.6 KB fixed + 1.7-2.2 KB FXS/FXO + 6 glyphs 252 B. **~8.5 KB**.
- WRAM: 206 B + 35 B (sp_reach) + 86 B (`fx_rule`) + ~30 B of screen statics. Scratch borrowed.
- VRAM/OAM: none new (menu layout; BG3 tiles: head 1-64, list 65-896 like today).

### Tests
1. Saves from `t/mk3.py` (mksave + equip 3 weapons; see the prototype): open ステータス on each of the 5 loadouts
   in `reach_sim.py`'s check, read the listed specials from a WRAM dump of the out bits or a screenshot; must
   equal `web()` of `reach_sim.py`: `[0,1,2,11]`, `[3]`, `[8,9,10,11]`, `[4,6,7,12]`, `[0,1,3]`.
2. Timing: PROFILE build, `prof.py` or frame stamps: < 10 frames from A to the screen for the worst loadout.
3. HP: equip a shield with HP 30 + 2 enhancements and a FK_HEART MAIN (e.g. FX グランハート): HP line equals
   `calc_hp_max()` at the start of a battle (wram dump of `_b_hp_max` after a sortie).
4. 特殊効果: one effect of each class on MAIN (e.g. 炎の共鳴 with/without a fire SUB, 王者の輝き with all SR, ベルセルカー
   with/without ベルセルク, ドラキラー); SUB with an effect shows 発動しない + メインに装備すると発動.
5. textcheck.py over the three tabs (and the item card from each): clean.

---------------------------------------------------------------------------------------------------------------

## 4. Key items (大切なもの): 4 orbs + ウサギのバッヂ, list screen, orb / badge scene

### Web rules (menu_screens.gd `_build_keys`, `_key_row`, `key_art`; map.gd `_orb_scene`, `_badge_scene`;
ui/orb_scene.gd; adventure.gd `commit_win`; save_data.gd `orb_owned`, `badge_owned`; Tuning)
- `ORB_NAMES`: 炎の宝玉 "赤く燃える炎の力が宿る宝玉。", 水の宝玉 "透き通った水の力が宿る宝玉。", 風の宝玉
  "命を育てる風の力が宿る宝玉。", 光の宝玉 "闇を払う光の力が宿る宝玉。" (port: `ORBN_n`, `ORBD_n`).
  `BADGE = ["ウサギのバッヂ", "うさぎの騎士に勝った記念のバッヂ。", "ペコラータに勝ったあかし！"]` (port has
  `BADGE_NAME`, `BADGE_DESC` = [1] + "\n" + [2]; add `BADGE_DESC1` = [1] and `BADGE_MORE` = [2]).
- Owned: `orb_owned = is_cleared(orb.area, orb.men)`; `badge_owned = is_cleared(AREA_SECRET, 1)`
  (`D_BADGE_MEN` = 1 exists in gdata.h). **No save field is needed.**
- List (title "大切なもの"): the 4 orbs always (owned: name yellow + text white; not owned: "？？？" gray +
  "まだ見つけていない。" gray, art darkened `modulate (0.16, 0.14, 0.2, 0.9)`); the badge row only when owned.
- Scene (`commit_win`: `res.badge = area == AREA_SECRET and men == 1` on the first clear; map shows the orb scene
  if `res.orb`, else the badge scene, then the rest of the after-battle flow): OrbScene: screen dimmed 75 %
  black; the art (96 px) falls from above to 36 % of the height in FALL = 0.8 s (`lerp(top - 40, land, k²)`),
  then bobs `sin(t * 2.4) * 4`; at landing a white flash (alpha 0.8 -> 0 in 0.25 s), a ring and a burst; a glow
  circle pulses `0.5 + 0.5 sin(3t)`. Texts from 52 % down, centred: title `name + "を手に入れた！"` (yellow),
  description (white), "more" (cream): orbs `"残りの宝玉も探そう！"` or, when all 4 are owned,
  `"4つの宝玉がそろった！\nアーケディア城へ戻ろう！"`; badge `BADGE[2]`; then "タップで次へ" (gray). Texts appear
  at FALL + 0.4 s, the help at + 0.9 s; a tap before that skips to the end, a tap after it closes.
  Colours: orbs `#ff6030 #40a0ff #50d060 #fff080`, badge `#ff90c8`.
- Art: `res://assets/keys/ORB_FIRE|ORB_WATER|ORB_WOOD|ORB_LIGHT.png` (104 px, 4x pixel art of 24 px, 9-10
  colours), `BADGE.png` (160 px illustration, 13097 colours).

### SNES design
**Art (make_gfx.py, new `keys()` step; measured in `measure_keys2.py`)**: order = `ORB_TABLE` order (fire, water,
wood, light) then BADGE.
- `KEYS_OBJ`: the 5 arts at 32 px (orbs native 24 px centred, badge LANCZOS to 30 px), 15 colours each, laid out
  as OBJ table rows 4-11 (tiles 64-191; art k at block (k & 3, k >> 2), i.e. tiles 64, 68, 72, 76, 128): 4096 B
  raw, **1357 B LZSS**. `KEYS_PAL`: 5 palettes + a silhouette palette (15 x RGB(41, 36, 51)) = 192 B raw.
- `KEY_BIG_0..4`: each art at 64 px (orbs 2x = 48 px, badge 62 px), 64 tiles row-major (8 x 8), LZSS:
  562 / 552 / 549 / 535 / 1777 = **3975 B** (laid out as 16-wide rows instead: 5190 B, one upload).
  `KEY_BIG_PAL_0..4`: 32 B each.
- Total **5.5 KB** of GFX bank space.

**List (`menu.c keys_scene()`, root row 大切なもの)**: `mk_screen()`, head `T_MENU_KEYS`; while blanked:
`load_vram(ASSET_KEYS_OBJ, MK_OBJ_VRAM + 64 * 16)`, `load_pal(ASSET_KEYS_PAL, 128 + 16)` (OBJ palettes 1-6).
Window `win_open(BX_LIST, 0, 2, 32, 26, T_LIST, PAL_W)`. Row i (0..3 orbs, 4 badge if owned) at box y
`8 + 36 i`: name x 48 (`TXT_YELLOW_S` + `ORBN_i` / `BADGE_NAME`, or `T_DEX_UNKNOWN_GRAY`); description at
y + 16 (`ORBD_i`; badge `say_wrap(BX_LIST, 48, y + 16, BADGE_DESC1, 240)` = 2 lines, it is 204 px wide; not owned
`T_KEY_NOT_YET`). Sprites every frame (`mk_sprites`): `spr_add(12, 16 + 8 + 36 i - 2, MK_ATTR(64 + (i & 3) * 4 +
(i >> 2) * 64, owned ? 1 + i : 6, 3), 1)` (32x32). B: back. 5 large sprites, at most 1 per line.

**Scene (`story.c key_scene(u8 k)`, k = 0..3 orb, 4 badge; replaces `orb_scene`)**: called from `map.c
after_battle()`: `if (res.orb != 255) key_scene(res.orb); else if (res.badge) key_scene(4);` (label hidden
around it as today).
- Set up (display on): `unpack(ASSET_KEY_BIG_0 + k, 0xA000)`; 8 x `queue_vram(MK_OBJ_VRAM + (64 + r * 16) * 16,
  0xA000 + r * 256, 0x7f, 256)` (tiles 64-71, 80-87, ... 176-183: the 64x64 picture = 2 x 2 large sprites at
  tiles 64, 68, 128, 132); `load_pal(ASSET_KEY_BIG_PAL_0 + k, 128 + 4 * 16)` (OBJ palette 4; the king's 1-3 are
  reloaded by `king_load` when a talk follows).
- Dim: colour math subtract on BG1 only: `cgwsel = 0; cgadsub = 0x81; coldata[] = 20` (BG3 text and OBJ stay
  bright). Text box: transparent high-priority BG3 `box(BX_KEY, 0, 14, 32, 12, 185, PAL_W, 0)` (map tiles 185+ are
  the closed area window's), all lines `say_center`: y 6 `T_KEY_GET` (targ_str = ORBN_k / BADGE_NAME), y 26 desc
  (`ORBD_k` / `BADGE_DESC1`), y 46 more (`T_KEY_MORE` / `T_KEY_ALL` (2 lines, 14 apart) / `BADGE_MORE`), y 82
  `T_KEY_NEXT` "{gray}A:つぎへ".
- Timeline (frames): 0-47 fall: `y = land - (land + 104) * (1 - (t/48)²)` with land = 48 (top-left of the 64x64;
  centre 80 = 36 % of 224); 48: `se_play(SE_MIRACLE)`, flash: `cgadsub = 0x3f; coldata = 25 -> 0` over 15 frames,
  then back to the BG1 subtract; 8 `MK_T_STAR` sprites fly out from the centre for 36 frames (burst); from 48 on bob
  `y = land + bob[(t >> 1) & 15]` (16-entry table of `sin * 4`); 72: flush the first three lines; 102: the help
  line. A before 102 jumps to 102 (all text shown); A after closes: `box_hide`, `cgadsub = 0`, `coldata = 0`.
- OAM: 4 large + 8 small; VRAM: 2 KB OBJ upload in 2 NMIs (queue). `mk_sprites = 0` while it runs.
- Strings: `T_KEY_GET` "{yellow}{s}を手に入れた！", `T_KEY_MORE`, `T_KEY_ALL`, `BADGE_DESC1`, `BADGE_MORE`,
  `T_KEY_NEXT`, `T_KEY_NOT_YET`, `T_MENU_KEYS` "大切なもの" (no new glyphs). The old `T_ORB_GET`
  ("…を取り戻した！") is no longer used by the scene (the web scene says "を手に入れた！"; the result screen's
  "を取り戻した！" stays).

**Rules (adventure.c)**: `AdvResult` gets `u8 badge`; in `commit_win()`, inside `if (!is_cleared(b_area, b_men))`:
`res.badge = b_area == AR_SECRET && b_men == D_BADGE_MEN;`. `u8 orbs_owned(void)` (count `is_cleared(orbs[i].area,
orbs[i].men)`) for the "all 4" text; `u8 badge_owned(void) { return is_cleared(AR_SECRET, D_BADGE_MEN); }`.

### Costs
ROM: art 5.5 KB (measured) + code ~1.6 KB (estimate) + text ~0.25 KB -> **~7.4 KB**. WRAM ~20 B. SRAM 0.

### Tests
1. mksave `--open 15 --clear AREA:MASK` variants: no orbs (list shows 4 x ？？？ with dark art), 2 orbs, all 4 +
   badge (`--clear 15:1`): screenshots of 大切なもの; textcheck.
2. Scene: a save just before an orb boss / Pekorata with strong gear (debug "武器・盾をもらう"), win, then
   `shot` at frames 20 (falling), 50 (flash), 110 (all text); A closes and the area story / opened notice follow
   as before. For Pekorata (first clear of 月見の島 STAGE 1): the badge scene shows; a second win shows nothing.
3. Orb count text: last orb -> `T_KEY_ALL`.

---------------------------------------------------------------------------------------------------------------

## 5. Rename the hero (名前の変更)

### Web rules (profile_screens.gd `_edit_name`, `_set_name`; ui/name_entry.gd; adventure.gd `set_profile_name`)
NameEntry with title "名前を変える", the current name pre-filled, max `PROFILE_NAME_MAX` = 6; "けってい" is
disabled while the name is empty (`NameKeys.ok`); `set_profile_name`: strip, cut to 6, ignore empty or unchanged,
then save. (The web's rename sits in the excluded プロフィール screen.)

### SNES design
- Place: the settings screen row "名前の変更" (item 1; not on the title).
- `story.c name_entry(u8 rename)` (callers: `map.c` new game `name_entry(0)`, settings `name_entry(1)`):
  title `rename ? T_NAME_RENAME ("名前を変える") : T_NAME_TITLE`; pre-fill from `sv.name`, or from `default_name`
  when `sv.name[0] == 0` (so the current shown name is edited); in rename mode START with `nlen == 0` plays
  `SE_CANCEL` and stays (web: けってい disabled); after START, if the buffer equals the old name nothing changes,
  else store, `name_sync()`, `save_write()`. New-game mode unchanged (an empty name still means the default).
- Controls unchanged (`T_NAME_KEYS`: A 入力, B けす, L/R 文字, START けってい).
- Strings: `T_NAME_RENAME`, `T_OPT_RENAME`. No glyphs.

### Costs
ROM ~0.3 KB code + 30 B text. WRAM 0. SRAM 0 (`sv.name` exists).

### Tests
Settings -> 名前の変更, B x6 to empty, START (stays, cancel SE), type 3 letters, START: map top-left shows the new
name; `sram` dump: name codes (u16 glyph codes) at Save offset 88 of the newest slot; reboot -> つづきから keeps
it. textcheck.

---------------------------------------------------------------------------------------------------------------

## 6. Boss records (討伐記録) for 魔王 and the secret boss

### Web rules (adventure.gd `boss_record_kind`, `defeat_record`, `record_loadout`, `commit_win`;
save_data.gd `BOSS_RECORD_KEYS`; profile_screens.gd `beaten`, `_build_boss_records`, `_record_card`,
`_record_row`, `_difficulty_chip`)
- Kinds: "maou" = AREA_CASTLE, men CASTLE_MEN (10); "secret" = AREA_SECRET, men SECRET_MEN (1); never for the
  tutorial, classic or treasure battles. In `commit_win` (before the clear is recorded): key `kind + ("_last" if
  is_cleared(area, men) else "_first")` = `defeat_record(c)` = `{ date, turns: c.turns_total, boss_turns:
  c.turns_stage, difficulty, weapons: copies of the 3 equipped items, shield: copy }`. So `_first` is written once,
  `_last` on every later win.
- `turns_total` +1 per enemy turn of the run; `turns_stage` +1 per enemy turn, reset at each WAVE start (and on
  a mirage form change); for 魔王 it is the turns of the last WAVE (all three forms) = "魔王戦".
- Screen (tab "討伐記録", shown only when `beaten("maou") or beaten("secret")`; `beaten(maou)` = a record exists or
  `ending_seen` or `is_cleared(castle, 10)`; `beaten(secret)` = a record exists or `is_cleared(secret, 1)`):
  intro `who + "を倒したときの装備とスキル、かかったターン（主人公が行動した回数）。2 回目からの記録は、倒すたびに
  新しくなる。品の絵をタップすると、くわしい説明"` with who = "魔王と裏ボス" if beaten(secret) else "魔王"; per
  beaten boss a heading ("魔王", "裏ボス  " + name of AWAKEN_MON = MON_117 兎騎士ペコラータ) and two cards:
  "初めての討伐" / "2 回目からの討伐（最新）" (yellow) + date (gray, right); missing card: "まだ倒していない", or if
  the boss is cleared "この記録をつけはじめる前に倒したので、記録はない" (first) / "2 回目の討伐はまだ" (latest);
  "かかったターン  %d" + ("（魔王戦 %d）" for maou), difficulty chip (`DIFFICULTY_NAMES`, colours lunatic
  #7a4f96, extra #9a4f5c, hard #a8703f, normal #4f68b0, easy #4f9a5c); 4 rows メイン/サブ/レンタル/盾 with art,
  type chip, name (yellow), shield "防御力 %d" "HP +%d" (hp + enh * 10), weapons "スキル" + names, empty "なし";
  tapping an item opens its info.

### Replacement for the date (no clock)
"**{n} 回目**" = how many times this boss had been beaten (counted by the port since v2) when the record was
made. Shown only when the count is exact (the first-kill record exists, i.e. counting started at the first kill);
an old save that had already beaten the boss shows no number.

### SNES design
**Save (v2 block, see 0.1)**, 164 B:
```c
typedef struct {
  u8 valid;            /* 1 = recorded */
  u8 difficulty;       /* sv.difficulty at the kill */
  u16 turns;           /* b_turns_total */
  u16 boss_turns;      /* b_turns_stage (魔王戦) */
  u16 nth;             /* sv.boss_wins[k] after this win: "n 回目" */
  Inv w[3];            /* MAIN, SUB, THIRD as they were; id 255 = empty */
  Inv shield;
} BossRec;             /* 40 bytes */
u16 boss_wins[2];      /* 0 魔王, 1 裏ボス: wins counted since v2 */
BossRec rec[4];        /* 0 魔王 first, 1 魔王 latest, 2 裏ボス first, 3 裏ボス latest */
```
v1 migration: everything 0 (no records, counters 0).

**Battle (battle_core.c, bank 3)**: `u16 b_turns_stage;` (battle.h extern): `= 0` in `start_stage()`, `++` in
`enemies_turn()` next to `++b_turns_total`. ~20 B, no per-frame cost.

**Commit (adventure.c `commit_win`, first statement after `res.win = 1` etc., before `men_clear`)**:
```c
static void boss_record(void) {
  u8 k, s; BossRec *r;
  if (b_mode != BM_NORMAL || is_treasure() || is_money()) return;
  if (b_area == AR_CASTLE && b_men == D_CASTLE_MEN) k = 0;
  else if (b_area == AR_SECRET && b_men == D_SECRET_MEN) k = 1;
  else return;
  if (sv.boss_wins[k] < 0xffff) ++sv.boss_wins[k];
  r = &sv.rec[k * 2 + is_cleared(b_area, b_men)];
  r->valid = 1; r->difficulty = sv.difficulty; r->turns = b_turns_total; r->boss_turns = b_turns_stage;
  r->nth = sv.boss_wins[k];
  for (s = 0; s < 3; ++s) if (sv.equip[s] == NO_ITEM) { memset(&r->w[s], 255, sizeof(Inv)); r->w[s].slots = 0; }
                          else r->w[s] = sv.inv[sv.equip[s]];
  r->shield = sv.inv[sv.equip[EQ_SHIELD]];
}
```
**Screen (`menu.c records_scene()`, root row 討伐記録, present only when beaten(0) || beaten(1))**:
- List screen: head `T_MENU_RECORDS`; window (0, 2, 32, 26); intro `say_wrap(BX_LIST, 8, 8, T_REC_INTRO_0 or _1,
  240)` (4 lines, y 8-50, white); `List` at `l.y = 72`, `l.lh = 16`, rows = for each beaten boss k: (k, first),
  (k, latest): x 14 `T_REC_BOSS_k` (gold), x 62 `T_REC_FIRST`/`T_REC_LAST` (yellow when the record exists, else
  `T_REC_FIRST_GRAY`/`T_REC_LAST_GRAY`). A: card; B: back. Keys `T_REC_KEYS` at y 186.
- Card screen (window (0, 2, 32, 26)): y 8 `T_REC_BOSS_0` or `T_REC_SECRET` (targ_str `MONN_0 + MON_117`);
  y 24 the card title (yellow) and, if `rec.valid && sv.rec[k*2].valid`, `T_REC_NTH` (n) at x 200;
  record missing: y 40 `T_REC_NONE` / `T_REC_BEFORE` (2 lines) / `T_REC_NOT2` by the web rule, nothing else;
  record present: y 40 `T_REC_TURNS` or `T_REC_TURNS_M` (n = turns, m = boss_turns), `T_REC_DIFF_0 + difficulty`
  at x 200 (green / blue / gold / red / purple for easy..lunatic); 4 gear rows at y 56, 88, 120, 152 (tile rows; the
  last ends at 180): line 1 slot
  label gray (`TXT_GRAY_S` + `T_SLOT_*`) x 14, type `WTN_n` x 50, name `mk_item_name` x 66 (white with yellow
  +N; the web name is yellow - use `TXT_YELLOW_S` + `ITEMN` and the +N on its own tile if preferred); line 2
  (y + 16): weapon `T_REC_SKILL` x 24 + skill names (x 62, "・" between), shield `T_REC_DEF` x 24 and `T_REC_HP`
  x 110; empty slot `TXT_NONE_GRAY`. Cursor on the rows with items (↑↓), A: item card (`mk_item_detail` on the
  stored `Inv`, window rows 17-27), B: back to the list. Keys `T_REC_KEYS` y 186.
- Strings: `new_ui.txt` section 6 (T_MENU_RECORDS, T_REC_*); glyphs 主 伐 公 明 目 討 説.

### Costs
ROM: code ~3.8 KB (estimate: screens ~110 lines + commit 25 + battle 3) + text ~0.9 KB + 7 glyphs 294 B ->
**~5 KB**. SRAM/WRAM: 164 B. CPU in battle: one increment per enemy turn.

### Tests
1. Save before 魔王 (mksave `--open 14 --clear 14:0x1ff`) + debug gear: win STAGE 10 -> 討伐記録 appears; card
   "初めての討伐": turns = WRAM `_b_turns_total` read just before the result (wram dump), boss turns plausible, gear
   = the equipped items; "2 回目からの討伐（最新）" = まだ. Win again with other gear: latest updated, first kept,
   "2 回目" shown.
2. Debug 地上クリア (item 9) on a fresh save: 討伐記録 lists both bosses with "この記録をつけはじめる前に倒したので、
   記録はない" and no "n 回目".
3. v1 save with the castle cleared -> migrate -> same as 2 for 魔王.
4. textcheck on the list, both card kinds and the item card.

---------------------------------------------------------------------------------------------------------------

## 7. NEW marks on menu entries and shops, and the forge unlock rule

### Web rules (adventure.gd `nav_open`, `nav_new`, `see_nav`, `new_shops`, `see_shops`, `_seen_init`;
map.gd `_nav_refresh`, `_nav`, `_go_shop`, `_open_screen`; ui/new_badge.gd; Tuning.NAV_OPEN_AREA)
- `NAV_OPEN_AREA = { "enhance": "AR_PLAIN", "friend": "AR_PLAIN" }`: the 鍛冶屋 nav button is dim and disabled
  until `save.area_open >= index(AR_PLAIN)` (= 1, 平原の村, the first village). `nav_new(enhance)` = open and
  `guides["nav_enhance"]` not set; `see_nav(enhance)` sets it when the enhance / evolve / items screens open.
- Shops: `shop_areas()` = reachable areas with a shop; `new_shops()` = those not in `save.shops_seen`;
  `nav_new(shop)` = any new shop; `_go_shop` (the ショップ nav, opens the newest shop town) calls `see_shops()`
  which marks ALL fresh shops seen. Shop nav when none: notice "店のある町に、まだ行けない" / "STAGEを進めよう".
- A NEW nav button glows and carries "NEW!!" (red box `#e83040`, border `#fff0a0`, white text, bobbing
  `sin(4t) * 1.5`).
- `_seen_init` for old saves (no `shops_seen` key): marks all current shops and open navs as seen (no NEW
  flood after an update).

### SNES design
- Save (v2 block): `u8 nav_seen;` (bit 0 = forge seen) `u16 shops_seen;` (bit = area index) = 3 B. v1
  migration ("seen init"): `shops_seen` = bits of every `area_has_shop(a) && a <= area_open`;
  `nav_seen |= 1` if `forge_open()`.
- adventure.c: `u8 forge_open(void) { return sv.area_open >= AR_PLAIN; }` (the debug "open all" sets area_open
  = AR_SECRET, so it opens too); `u8 forge_new(void) { return forge_open() && !(sv.nav_seen & 1); }`;
  `u8 shop_new(u8 a) { return area_has_shop(a) && a <= sv.area_open && !(sv.shops_seen & (1 << a)); }`;
  `u8 shops_new(void)` (any); `void shops_see_all(void)`; `void shop_see(u8 a)`; `void forge_see(void)`.
- Root menu (`menu.c root_row`): 鍛冶屋 row: `T_MENU_FORGE_GRAY` when `!forge_open()`; `T_NEW_TAG` "{red}NEW!!"
  at x 72 when `forge_new()`; ショップ row: `T_NEW_TAG` at x 72 when `shops_new()`. Choosing gray 鍛冶屋:
  `mk_notice(T_FORGE_LOCKED, T_FORGE_LOCKED2)` with targ_str = `AREAN_0 + AR_PLAIN` ("鍛冶屋は、まだ使えない" /
  "平原の村に着くと使えるようになる"; SNES-only text: the web just dims the button). No shop: add the web's second
  line `T_NO_SHOP2` "STAGEを進めよう" to the existing notice. Choosing ショップ: `shops_see_all()` then
  `shop_scene(last_shop_area(), 0)`.
- `forge_scene()` entry and `items_scene()` entry call `forge_see()` (web: enhance / evolve / items screens).
  `item_actions`: the 鍛冶屋 row shows `T_ACT_FORGE_GRAY` and gives the locked notice when `!forge_open()`.
- Area window (`map.c row_draw`, RK_GACHA): `T_NEW_TAG` at `ROW_LBL` (x 128) when `shop_new(map_area)`; opening
  the shop from that row calls `shop_see(map_area)`.
- Map key help (`map.c map_setup`): when `forge_new() || shops_new()`, `say(BX_HELP, 208, 2, T_NEW_TAG)`
  (T_MAP_KEYS ends at x 198), redrawn by `map_setup` after the menu.
- Text "NEW!!" is static red (no bob; optional: blink by re-flushing the tag every 32 frames).
- Root menu rows (proposal, one column, `win_open(BX_LIST, 8, 2, 16, 25)`, `l.lh = 16`, up to 11 rows from
  y 8): 装備 / 持ち物 / 鍛冶屋 / ショップ / 図鑑 / ステータス / 大切なもの / 討伐記録 (only when a boss is beaten) /
  難易度 xx / 設定 / タイトルへ. Build the visible rows into an array each time the menu opens; `menu_text[]`
  and the switch use the row ids.

### Costs
ROM ~0.7 KB code + ~0.2 KB text. SRAM/WRAM 3 B. No new glyphs.

### Tests
1. New game, win the Arcadia stages until 平原の村 opens: the map help shows NEW!!; the menu shows 鍛冶屋 NEW!! and
   ショップ NEW!!; before that, 鍛冶屋 is gray and gives the notice (also from 持ち物 -> 鍛冶屋で強化・進化).
2. Open 鍛冶屋: its NEW goes; open the 平原の村 ガチャ row: shops NEW goes; reaching 港町ポトス brings it back.
3. v1 save with area_open 5 -> migrate: no NEW anywhere.
4. textcheck on the menu and the area window with NEW tags.

---------------------------------------------------------------------------------------------------------------

## 8. Dex count colours (銅・銀・金・虹)

### Web rules (Tuning `DEX_GLOW = [10, 30, 50, 100]`, `DEX_GLOW_NAMES = ["", "銅", "銀", "金", "虹"]`,
`dex_glow(n)`; ui/dex_card.gd; ui/card_view.gd; dex_screens.gd `entries`)
- count = kills for monsters, `got` (number ever obtained) for weapons and shields. `lv = dex_glow(n)` = number
  of thresholds reached (0..4). The card's "×n" tag uses `GLOW_COLORS[lv].lightened(0.35)` (lv 0: cream):
  `GLOW_COLORS = [#6a6a7a, #c87838, #b8c8e0, #e8b030, #e070e0]`, plus a glow shader on the card (tier) and
  sparkles in the big card view. The names 銅/銀/金/虹 are defined but not shown.

### SNES design
- Thresholds: existing `T_DEX_GLOW_0..3` (10, 30, 50, 100) in gdata.h; `static u8 tier(u16 n)` in dex.c.
- Colours: on the dex screens only, after each `mk_screen()` in dex.c, recolour three BG3 palettes the dex does
  not use (checked: no dex text uses {green}/{blue}/{purple}): `cgram[4*4+3] = 0x3E9B` (bronze = #c87838 +35 %
  -> (219,167,126)), `cgram[5*4+3] = 0x777A` (silver (209,219,235)), palette 7 colour 3 = rainbow; gold uses the
  existing {gold} palette 6 (0x229b, the frame colour). Rainbow: every 6 frames step through 8 hues
  `{0x211F,0x1A9F,0x23FF,0x2BEA,0x7F88,0x7E0C,0x7D58,0x619F}` (red, orange, yellow, green, cyan, blue, violet,
  pink) and set `cgram_dirty |= 1` (256 B CGRAM upload, within the NMI budget) - in the list (`mk_sprites` hook)
  and in the card loops.
- Text: rename `T_DEX_COUNT` -> `T_DEX_CNT_0`, `T_DEX_KILLS` -> `T_DEX_KILLS_0`, `T_DEX_GOT` -> `T_DEX_GOT_0` and add
  `_1.._4` (consecutive): list rows `sayn(BX_LIST, 196, y, T_DEX_CNT_0 + tier(sv.kills[m]), sv.kills[m])` (items:
  x 200, `sv.got[it]`); monster card `T_DEX_KILLS_0 + tier`, item card `T_DEX_GOT_0 + tier(sv.got)` (only the
  "手に入れた数" number coloured, `{c0}` after it).
- No glow shader on the SNES (optional: colour the card's frame palette by tier - not specified).

### Costs
ROM ~0.4 KB code + ~0.35 KB text; 0 glyphs; WRAM ~10 B (rainbow table in RODATA 16 B).

### Tests
mksave `--dex 3` gives kills `(i*7) % 130` and got `i % 5`; patch a few kills to 9, 10, 30, 50, 100, 999 and got
to 10/30 (mksave option); dex list screenshots at 3 frames 6 apart (rainbow changes), card screenshots; WRAM
`cgram` check of entries 19, 23, 31; textcheck (no colour mixing: the ×n starts its own tile at x 196/200).

---------------------------------------------------------------------------------------------------------------

## 9. Debug menu additions: 地上クリア and skill editing of owned weapons

### Web rules (debug_screens.gd `_build_open` card "地上クリア", `_ask_surface_clear`, tab "スキル" `_build_owned`,
`_pick_owned`, `_open_editor`, `_ed_ok`; adventure.gd `debug_surface_clear`, `debug_set_skills`, `debug_skills`)
- 地上クリア: every AREA_TABLE area gets all its STAGE bits (`(1 << men_count) - 1`), `area_open` = last area,
  `ending_seen = true`, `debug_open = true` (also `chitei_preview`: not in the port), save. Card text
  "地上のエリアを全部クリアして、エンディングを" / "見たことにする（王さまの話のあと、地図に穴）" / underground note;
  state "いま：地上クリア済み" (green) / "いま：まだ"; button disabled when done; confirm
  ["地上を全部クリアしたことにする？", "（元に戻せない）"]; message "地上をクリアした。地図にもどると…（地底）".
  Orbs and the badge count as owned automatically (they are "cleared" checks); no boss records are made.
- スキル tab: "スキルを変える武器をタップ", the owned weapons list (filters), pick -> editor with the 3 slots
  (`WEAPON_SLOTS_MAX`), skill list, buttons やめる / 決める (no おまかせ when editing); 決める with no skill ->
  "スキルを 1 つは選んで"; `debug_set_skills(uid, skills)` (weapons only, skills that exist, max 3) -> "スキルを変えた"
  or "変えられなかった".

### SNES design (debug.c bank 13, adventure.c)
- Root rows: `DB_GIVE, DB_SKILL, DB_GOODS, DB_ALL, DB_DEX, DB_OPEN, DB_SURF, DB_BACK` with `T_DBG_SKILL`
  "スキルを変える" / `T_DBG_SKILL_D` and `T_DBG_SURF` "地上クリア" / `T_DBG_SURF_D`. 8 rows: list window
  `win_open(BX_LIST, 4, 2, 24, 15)`, `l.lh = 14` (one colour), 7 visible (the kit scrolls), info window unchanged
  (rows 17-27). DB_SURF info line at y 54: `T_DBG_SURF_DONE_NOW` (green) if `sv.ending_seen` else `T_DBG_SURF_NOW`.
- 地上クリア: if `sv.ending_seen` -> `mk_notice(T_DBG_SURF_ALREADY)`; else `mk_confirm(T_DBG_SURF_ASK,
  T_DBG_NOUNDO)` -> `debug_surface_clear()` -> `mk_notice(T_DBG_SURF_DONE)`.
  `void debug_surface_clear(void)` (adventure.c): `for (a = 0; a < AR_MIRAGE; ++a) sv.cleared[a] |= (1UL <<
  men_count(a)) - 1;` (castle bits 0-9 only, the money STAGE bit 19 untouched like the web), `sv.area_open =
  AR_SECRET; sv.ending_seen = 1; sv.story |= ST_DEBUG_OPEN; save_write();`. Back on the map `map_setup()` shows
  every area cleared and the mirage tower.
- スキルを変える: `i = pick_item(1, T_DBG_SK_HEAD, NO_ITEM)` (menu.c, weapons only, SELECT sorts, the item card
  below the list), then `editor_edit(i)`: the existing `editor()` with a mode flag: pre-fill `ed_sk` from
  `sv.inv[i].sk`, rows スキル1..3 / `T_DBG_DECIDE` "決める" / `T_DBG_CANCEL` "やめる" (no おまかせ, as the web);
  決める with no skill -> `T_DBG_NEED_SKILL`; else `debug_set_skills(i, sk, n)` (adventure.c: weapons only; pack
  the chosen skills, fill 255; `save_write()`), notice `T_DBG_SK_DONE` / `T_DBG_SK_FAIL`. The skill picker keeps
  the port's filter (same weapon type + 特殊). Equipped weapons may be edited (as the web).
- Glyphs: 書 済.

### Costs
ROM ~1.5 KB code + ~0.45 KB text + 84 B glyphs. WRAM ~5 B.

### Tests
1. Fresh save: R x3 -> 地上クリア -> confirm: map all cleared, 蜃気楼の斜塔 open, 大切なもの shows 4 orbs + badge,
   討伐記録 visible (item 6 test 2), no ending replay (ending_seen), SRAM `cleared[]` = 0x7 for normal areas,
   0x3ff castle, 0x1 secret.
2. スキルを変える: pick an equipped MAIN, set skills ジャンプギリ order (斬り上げ, スラッシュ), 決める: ステータス
   必殺技 tab lists ジャンプギリ; dex skill marks update.
3. textcheck on the debug root (8 rows, scroll), the picker and the editor.

---------------------------------------------------------------------------------------------------------------

## Totals for the group (ROM, WRAM, SRAM)

| item | ROM | WRAM | SRAM | glyphs |
|---|---|---|---|---|
| 1 settings | 0.9 KB | 23 B | 0 | 0 |
| 2 logo | -0.47 KB | 0 | 0 | 0 |
| 3 status (+ shared sp_reach 2.2 KB) | 8.5 KB | ~360 B | 0 | 6 |
| 4 key items (art 5.5 KB) | 7.4 KB | 20 B | 0 | 0 |
| 5 rename | 0.3 KB | 0 | 0 | 0 |
| 6 boss records | 5.0 KB | 164 B (save) | 164 B | 7 |
| 7 NEW + forge lock | 0.9 KB | 3 B (save) | 3 B | 0 |
| 8 dex colours | 0.75 KB | 26 B | 0 | 0 |
| 9 debug | 2.0 KB | 5 B | 0 | 2 |
| **total** | **~25 KB** (code ~16 KB in banks 1/2/3/7/10/13/14, GFX +5.0 KB, text/font ~5 KB) | **~0.6 KB** of 7.2 KB free | **167 B** (v2 block) | **15** (630 B) |

Code bank room (measured, build map): CODE7 9.7 KB, CODE10 10.7 KB, CODE13 7.4 KB, CODE14 12.7 KB, CODE2 6.9 KB of
32 KB each: everything fits in the existing banks. Low WRAM trampolines: +~25 new non-static functions x 7 B.
RODATA (const tables) is copied to WRAM $7E and loaded from bank $80 (10.6 KB free there): keep new const tables
small (this group: ~200 B).
