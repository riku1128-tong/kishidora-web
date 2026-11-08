# Forge and materials (key `forge`): implementation spec

SFC port of キシドラ！ (`/home/user/kishidora-web/kishidra_sfc`), surface world. Three items:

1. Forge how-to guides 強化の使い方 / 進化の使い方 (first forge visit, 強化鉄 gift, text pages + cursor instead of touch highlights)
2. Evolve: make the missing materials from lower ones automatically (exact web recipe chain), and the 進化できる品だけ list
3. 素材: descriptions, where to farm each material (the web's `mat_farm`, precomputed for every open state), jump to that STAGE

Everything below was **built and run** in a prototype (`plan/forge/work2`, a copy of the port):
the ROM links, all screens were driven in the libretro harness (snes9x core) and
`textcheck.py` reports **0 findings on 9 scripts**. The prototype diff is the reference implementation:

| file | what |
|---|---|
| `plan/forge/proto_src.patch` | all `src/` changes (forge.c rewrite, adventure.c/h, menu.c, map.c, game.h, save.h, ui.c, ui.h, text.s) |
| `plan/forge/proto_tools.patch` | `tools/data/text.py` (`{k}`), `tools/harness/textwidth.py`, `tools/harness/mksave.py` (`--guides`, `--mat`) |
| `plan/forge/proto/forge.c` | the complete new forge.c |
| `plan/forge/farm.py`, `farm_gen.py` | the web's `mat_farm` in Python + the farm table writer (to fold into make_data.py) |
| `plan/forge/evo.py`, `cmp_plan.py`, `proto/evoplan.c` | the web's `evolve_need`/`evolve_plan` in Python, a host build of the C plan, the random comparison (6000 cases, 0 mismatches) |
| `plan/forge/text/guide_snes.py` | SNES overrides of the guide text (applied on top of Tuning.GUIDE_*) |
| `plan/forge/text/ui_forge.txt`, `forge_strings.json`, `strings_table.md`, `fstg.txt` | every new string (173), final form |
| `plan/forge/wrap.py`, `measure.py` | string generation (line breaking `fit_break`, templates) and width checks |
| `plan/forge/t/g1.txt … g9.txt` (also in `proto/`) | harness scripts; `t/*_sheet.png` the screenshots |

---------------------------------------------------------------------------------------------------

## 0. Common ground

### 0.1 Measured cost of the whole group (prototype vs. the current port, `ld65` map)

| segment | before | after | delta | what |
|---|---|---|---|---|
| CODE12 (forge.c, bank $8C) | 8,963 | 17,762 | **+8,799** | guide engine 2.75 KB, evolve page 4.85 KB, 素材 screen 2.25 KB (minus the old evolve/matup code) |
| CODE7 (adventure.c, $87) | 9,680 | 11,301 | +1,621 | evolve_plan, crafting evolve, mat_up_check, mat_farm |
| CODE9 (map.c, $89) | 10,033 | 10,398 | +365 | farm jump, おすすめ label, open_area() |
| CODE10 (menu.c, $8A) | 10,727 | 10,987 | +260 | 進化できる品だけ rows, MA_FARM plumbing |
| CODE1 (ui.c, $81) | 7,528 | 7,897 | +369 | win_close save-stack compaction (bug fix, see 0.4) |
| ENGINE (text.s) | 9,284 | 9,319 | +35 | `{k}` third number, `text_lh` line step |
| BANKC1+C2 (strings, font) | 58,500 | 64,638 | +6,138 | 173 strings (5,466 B) + 16 glyphs (656 B) |
| BANKC0 | 21,993 | 22,953 | +960 | text_table (+519) + farm.s (441) |
| RODATA (ROM $80 and WRAM) | 8,994 | 9,223 | +229 | guide table, row tables, farm_stages |
| BSS (WRAM) | 38,887 | 39,257 | +370 | evolve page rows, Plan, buffers |
| **ROM total** | | | **+18,803 B (18.4 KB)** | |
| **WRAM** | 46.8 KB | 47.4 KB | **+0.6 KB** (+6 B low WRAM) | |
| **SRAM** | | | **+1 B per slot** (`u8 guides`) | |
| VRAM / OAM | | | 0 new tiles; 1 cursor sprite | BG3 tile ranges re-planned inside forge.c |

Per item (code attributed by function, text by string family, glyphs by first use):

| item | code | text | glyphs | data | ROM | WRAM |
|---|---|---|---|---|---|---|
| 1 guides | ~3.0 KB (+0.4 shared ui.c/text.s) | 3,029 B | 8 (328 B) | g_tab 95 B | ~6.8 KB | ~0.1 KB |
| 2 evolve | ~5.2 KB | 1,159 B | 3 (123 B) | row tables 64 B | ~6.6 KB | ~0.3 KB |
| 3 materials | ~2.6 KB | 1,797 B | 5 (205 B) | farm.s 441 + 46 B | ~5.0 KB | ~0.1 KB |

New glyphs (16): 計 作 落 先 ＋ 埋 星 同 説 明 右 動 覧 虹 雑 魚. (`虹` disappears if 3.1 adopts `（◆）` for the bell text: 15 glyphs, 615 B.)

### 0.2 Shared prerequisites (other groups need them too)

1. **Save v2 block** (owner: see the menus_settings / inventory specs): this group adds `u8 guides;` (bits `GDB_ENH 1`,
   `GDB_EVO 2`, `GDB_GIFT 4`; bits 3-7 free for other one-time tips) inside the new block before `u16 ninv`.
   v1 migration: `guides = 0` (the guides show once to existing players, like a web player whose save has no
   `guides` key; the gift still needs "no enhancement material at all"). `mksave.py` gets `--guides N`
   (default **7** so the existing harness scripts that open the forge see no guide) and `--mat IDX:N`.
   The prototype simply inserted the byte (no migration), so it is not save-compatible by itself.
2. **Text and font banks** (measured): with this group alone the strings fill $C1 (32,764 of 32,768 B) and spill
   1.6 KB into $C2; `make_data.write_text` then puts the fonts (30,282 B: font.bin 17,304 + font_s 11,536 + widths)
   into $C3, a full GFX bank → **link error**. The prototype moved the font segment into $C2 after the spilled
   strings (C2 31,874 / 32,768 used: only 0.9 KB left). The integrator must give the fonts their own bank (or
   split `font_glyphs` / `font_glyphs_s`, which text.s addresses by `^bank` separately) before the groups' text lands.
3. **Regenerating data** (`python build.py data`) pulls in the newer web build (see menus_settings 0.3). The prototype
   regenerated text with the text-only `Gen` call; battle code segments came out byte-identical.
4. **ui.c window save stack** (0.4): needed by any screen that closes windows out of order repeatedly.
5. **text.s** `{k}` (third number, control code 15, `targ_num3`) and `text_lh` (line step override, 0 = 14):
   generic, 35 B. Exported from text.s; `ui.h` declares `extern u32 targ_num3; extern u16 text_lh;`;
   `tools/data/text.py` CTRL gets `'{k}': 15`; `textwidth.py` counts code 15 as a number.

### 0.3 Controls (no clash with README / inventory spec)

Forge screens only: A decide, B back, ↑↓ rows, START = **とばす** while a guide waits for an action
(the web's hint-bar button), ←→ in yes/no and in the count picker, Y = 最大 in the count picker.
The item lists keep the inventory group's keys (L/R tabs, Y 必殺, SELECT sort, X くわしく); START is not
handled inside `pick_item` (go back with B, then START).

### 0.4 Bugs found while prototyping (fix them in the implementation)

* **ui.c `win_close` leaks the BG3 save stack** when a window that is not the last one is closed:
  `save_top` only drops when the last window closes. Repeated card close/reopen (guide pages) pushed
  `save_top` past `WIN_SAVE` (2048 cells); `bg3_rect` then saves only part of a window and leaves old text on
  screen when it closes (seen in the prototype on the "おしまい" page). Fix (in the patch): when the closed window
  is not the last, `memmove` the later saves down by `w*h` cells, subtract `w*h` from their `wins_at`, and
  lower `save_top`.
* **ca65 anonymous labels**: inserting `beq :+ / :` into `set_font` captured the existing `bne :+` of the
  8-dot branch and broke every 8-dot text (12 rows read from 8-row glyphs). Use a named local label (`@lh_ok`).
* Text ids and C macros share one namespace: the guide strings are `GD_*`, so the save bits are `GDB_*`.

---------------------------------------------------------------------------------------------------

## 1. Forge how-to guides (強化の使い方 / 進化の使い方)

### 1.1 Web rules (ui/forge_guide.gd, forge_screens.gd 50-157, tuning.gd 1557-1651)

* **When.** `ForgeScreens._ready`: opened on the list (not at an item) and `save.guides` lacks `"enhance"` →
  `start_guide("enhance")`. Opened at an item on its evolve page, or `_set_page("evolve")` later, with no guide
  running and no `"evolve"` key → `start_guide("evolve")`. The **使い方** button (top band, hidden while an item
  band shows) opens a popup `どの使い方を見る？` with `強化の使い方` / `進化の使い方` / `やめる`.
* `start_guide(k)`: goes back to the list (unless k = evolve and the evolve page is open), `_evo_only = (k == evolve)`,
  `save.guides[k] = true` **immediately** (skipping still counts as seen), then runs `ForgeGuide`.
* **hands** (`_ready`): enhance → some owned item has an empty enhancement slot (`enh.has(0)`); evolve →
  `evolve_list()` not empty. Steps flagged `hands` are skipped when hands is false.
* **Gift**: enhance guide, hands, no `ENH_MATS` at all (`MAT_KYOUKA, MAT_SKYOUKA, MAT_HKYOUKA, MAT_BELL` all 0) and
  no `guides.enhance_gift` → `mat_add("MAT_KYOUKA")` (one 強化鉄), `guides.enhance_gift = true`; the line
  `GUIDE_TEXT.gift` = **おためしに強化鉄を 1 つあげる** is appended to the step whose goal is `result`.
* **Steps** (`_go`): a step is skipped if `hands` and not hands, or its `goal` (or `skip_if`) is already done
  (`_done`: `item` = an item is open and has an empty slot; `enh_page` = item open and page enhance;
  `evo_item` = item open and evolvable; `evo_page` = item open and page evolve).
  The window shows title (18 px, yellow), lines, button `つぎへ` (no goal) / `やってみる` (goal) / `おわる`
  (last step) and `とばす`. `やってみる` → hint bar (step `hint`, yellow) + `とばす`; the guide watches the goal:
  * `item`: when an item opens — has an empty slot → next; else window **空いたマスがない** /
    `この品は、空いたマスがない` / `一覧にもどって、ほかの品を選ぼう` + `やってみる` (back to the list, wait again).
  * `enh_page`: next when the enhance page opens; back to `item` if the item closes.
  * `pick`: next when the material picker opens (web only).
  * `result`: next when an enhancement result was shown; if the player leaves, back to `item` / `enh_page` / `pick`.
  * `evo_item`: as `item` with evolvable, window **進化しない品** / `この品は、これ以上進化しない` /
    `一覧にもどって、ほかの品を選ぼう`; switching to the 素材 tab jumps past `mat_tab`.
  * `evo_page`: next when the evolve page opens; 素材 tab → past `mat_tab`; item closed → back to `evo_item`.
  * `mat_tab`: next when the 素材 tab opens.
* The highlight: the screen is dimmed except a pulsing gold rectangle around the step's `mark` (a UI part).
* **Strings** (verbatim, tuning.gd): GUIDE_TEXT `next つぎへ`, `try やってみる`, `end おわる`, `skip とばす`,
  `replay 使い方`, `gift おためしに強化鉄を 1 つあげる`, `no_slot_title 空いたマスがない`,
  `no_slot [この品は、空いたマスがない / 一覧にもどって、ほかの品を選ぼう]`, `no_evo_title 進化しない品`,
  `no_evo [この品は、これ以上進化しない / 一覧にもどって、ほかの品を選ぼう]`.
  GUIDE_ENHANCE (11 steps) and GUIDE_EVOLVE (9 steps): titles, lines and hints exactly as in tuning.gd 1565-1651
  (dumped to `plan/forge/text/guide_web.json`).

### 1.2 SNES design

The touch highlight becomes **text pages** (a window with the step's title and lines) plus a **blinking cursor**
pointing at the menu row the step is about; after `やってみる` the menu's own cursor starts on that row and the
head bar shows the hint. Keys of a page are in the head bar: `A:つぎへ　START:とばす`,
`A:やってみる　START:とばす`, `A:おわる` (gray `A:`/`START:`).

**Forge root (new, forge.c `forge_scene`)**: window (6,2,20,11), rows 14 px apart:
`強化・進化する品を選ぶ` / `進化できる品だけ` / `素材` / `使い方` / `もどる` (replaces 素材の合成; see 2 and 3).
`使い方` → chooser window (6,13,20,9): centred `どの使い方を見る？`, rows `強化の使い方` `進化の使い方` `やめる`.

**Contexts** (where forge.c calls the guide) and the step table (`g_tab`, 5 bytes: context, goal, mark row, flags, lines):

| j | web | title | context (page shown at) | goal | mark | flags |
|---|---|---|---|---|---|---|
| E0 | 0 | 強化のしかた | ROOT (root menu drawn) | – | root row 0 | |
| E1 | 1 | 強化する品を選ぶ | ROOT | item | root row 0 | hands, 16 px |
| E2 | 2 | 強化・進化 (web 強化・進化・売る) | ITEM (item menu drawn) | – | item row 0 | hands, 16 px |
| E3 | 3 | 「強化する」を選ぶ | ITEM | enh_page | item row 0 | hands |
| E4 | 4 | 強化のマス | ENH (強化する chosen, before the material window) | – | – | 16 px |
| – | 5 | 強化してみよう | dropped: the port opens the material window right after 強化する | | | |
| E5 | 6 | 使う素材 | SRC (material window drawn) | – | src row 0 | |
| E6 | 7 | 素材を選ぶ (+ gift line) | SRC | result | src row 0 | hands |
| E7 | 8 | 強化の結果 | RESULT (after the result notice) | – | – | hands |
| E8 | 9 | マスを増やす・消す | ITEM | – | item row 2 | |
| E9 | 10 | おしまい | ANY | – | – | |
| V0 | 0 | 進化のしかた | ANY | – | – | |
| V1 | 1 | 進化できる品 | ROOT | – | root row 1 | hands, skip_if evo_item, 16 px |
| V2 | 2 | 品を選ぶ | ROOT | evo_item | root row 1 | hands |
| V3 | 3 | 「進化する」を選ぶ | ITEM | evo_page | item row 1 | hands |
| V4 | 4 | 要る素材とコイン | EVO (before the evolve page is drawn) | – | – | hands, 16 px |
| V5 | 5 | 素材を見る | ANY | mat_tab | root row 2 | |
| V6 | 6 | 素材の一覧 | MATS (素材 screen, before drawing) | – | – | |
| V7 | 7 | 素材をまとめる | MATS | – | – | |
| V8 | 8 | おしまい | ANY | – | – | |

Rules of `guide_at(ctx, i)` (prototype): loop over the current step; skip it as the web `_skip` does (`goal_done` uses
the context: an item is open in ITEM/ENH/SRC/RESULT/EVO with index `i`; `enh_page` done in ENH/SRC/RESULT;
`evo_page` done in EVO); if hands and the step's context is neither ANY nor `ctx`, stop (its screen comes later;
with hands false every page shows at once, as the web shows them without a hole); show the page; START ends the
guide; a goal step sets `g_wait`. Events `guide_ev` (prototype): PICKED (after `pick_item` returns: item / evo_item
goal, else the no_slot / no_evo page with `やってみる` → `pick_item` again), ENH, RESULT, EVO, MATS (evolve guide
waiting evo_item / evo_page / mat_tab → step V6), LEAVE_ITEM (item menu left: enh_page / result → E1;
evo_page → V2), LEAVE_SRC (material window cancelled, no material, cancelled item source: result → E3).
Leaving the forge ends the guide (`guide_end` in `forge_scene`). Auto start: `forge_scene(NO_ITEM)` with
`!(sv.guides & GDB_ENH)`; `do_evolve` with `!(sv.guides & GDB_EVO) && !g_kind` (then V1-V3 skip because their goals
are done, as on the web). `forge_scene(i)` from 持ち物 does not start the enhance guide (web: opened at an item).

**Page window** (`g_page`): box 4, 32 tiles wide, height `(24 + (n-1)*lh + 12 + 8 + 7) >> 3` rows (lh 14, or 16
when the lines have colour runs: `text_lh = 16`), title at (8, 8) yellow, lines from (8, 24) (x 8: lines may be 240 px),
gift line after them. At the root the page sits at the bottom (y = 28 - h, under the root window); elsewhere at the
top (y = 2). The page uses the card's BG3 tiles: forge.c closes the card (tracked by `card_on`) and the caller
redraws it (`if (guide_at(...)) card(i);`; in SRC also `src_window()` again so the card does not cover its top row).
The mark cursor (MK_T_CURSOR, blinking every 16 frames) is drawn only when the step's context is the current one
and the row is not under the page: root (51, 26 + 14k), item menu (35, 114 + 14k), material window (51, 122 + 14k).
Hint while waiting: forge's head bar shows the hint (`GDx_H_j`, yellow) at x 8 and `{gray}START:とばす` at x 176
instead of title / item / coins; `pick_item` gets the hint as its title (its head shows title + coins).

**BG3 tiles of the forge** (new plan, 1024 tiles): head 1-64; menu / small windows 65-328 (≤ 24×11); card 329-680;
guide page 329-840 (≤ 32×16, card closed); evolve page 65-864 (32×25, card and menu closed); 素材 list 65-512,
素材 text / action / count 513-864; menukit dialogs from 864 up (never open with a page).

**SNES text of the guides.** make_data.py takes title / lines / hint from `Tuning.GUIDE_*` and applies
`tools/data/guide_snes.py` (`ENHANCE_FIX`, `EVOLVE_FIX`, from `plan/forge/text/guide_snes.py`) only where the web
text talks about touch or about UI the port does not have (the port's star marks are ★ white = 成功,
★ yellow = 大成功, ◆ red = 奇跡; favourites are ♥; the port's list has no `スロット 0/3`; 売る / お気に入り are in 持ち物):

| step | web | SNES |
|---|---|---|
| E1 lines | 右の「スロット 0/3」は、 / 埋まったマスの数 / 全部のマスの数 / 空いたマスのある品をタップしよう | 下の欄の星が強化のマス / {gray}☆{c0} は空いたマス、★ と {red}◆{c0} は埋まったマス / 空いたマスのある品を選ぼう |
| E1 hint | 空いたマスのある品をタップ | 空いたマスのある品を選ぶ |
| E2 title / lines | 強化・進化・売る / 品の画面で、強化・進化・売るを選べる / 右上の☆で★（お気に入り）になる / ★の品は売れない・強化の素材にならない | 強化・進化 / 品を選ぶと、強化・進化を選べる / 売る・お気に入り（{red}♥{c0}）は「持ち物」で / {red}♥{c0}の品は売れない・強化の素材にならない |
| E3 | 「強化」を選ぶ / 「強化」をタップしよう / hint 「強化」をタップ | 「強化する」を選ぶ / 「強化する」を選ぼう / hint 「強化する」を選ぶ |
| E4 lines 2-3 | 成功は黄の星、大成功は緑の星、 / 奇跡は虹の星がマスに入る | 成功は ★、大成功は {yellow}★{c0}、 / 奇跡は {red}◆{c0} がマスに入る |
| E5 last line | （★の品と装備中の品は使えない） | （♥の品と装備中の品は使えない） |
| E6 lines / hint | 使う素材をタップして、 / 「強化する」を押そう / 素材をタップして「強化する」 | 使う素材を選んで、 / A を押そう / 素材を選んで A |
| E8 lines 1, 6 | スロット +1：フエルスロットで / スロットリセットは店の「道具」で買える | スロットを増やす：フエルスロットで / スロットリセットは「道具屋」で買える |
| E9, V8 | この説明は、上の「使い方」で | この説明は、鍛冶屋の「使い方」で |
| V1 lines 1, 4 | 「進化できる品だけ」を押すと、 / 黄色は、今すぐ進化できる品 | 「進化できる品だけ」を選ぶと、 / {yellow}黄色{c0}は、今すぐ進化できる品 |
| V2 | 品をタップしてみよう / hint 進化させたい品をタップ | 品を選んでみよう / hint 進化させたい品を選ぶ |
| V3 | 「進化」を選ぶ / 品の画面の「進化」をタップしよう / hint 「進化」をタップ | 「進化する」を選ぶ / 品のメニューの「進化する」を選ぼう / hint 「進化する」を選ぶ |
| V4 lines | 上に、進化したあとの品と強さ、 / … / オレンジは下の素材から自動で作る / 赤は足りない：ボタンで落ちる場所へ / … | 上に、進化したあとの品と効果、 / 下に、要る素材とコインが出る / {gold}オレンジ{c0}は下の素材から自動で作る / {red}赤{c0}は足りない：選んで A で落ちる場所へ / そろったら「進化する」で進化できる |
| V5 lines / hint | 「素材」の札で、持っている数と / hint 「素材」の札をタップ | 鍛冶屋の「素材」で、持っている数と / hint 「素材」を選ぶ |
| V6 line 1 | 素材をタップすると、下に | 素材を選ぶと、下に |

All other titles and lines are the web's verbatim. GUIDE_TEXT strings are verbatim; next/try/end/skip appear in the
key strings above; replay `使い方` is the root row.

### 1.3 Code

* forge.c (prototype `proto/forge.c` lines 28-330): `g_tab`, `g_kind/g_base/g_n/g_step/g_wait/g_hands/g_gift`,
  `g_wait_goal`, `g_hint`, `head` (hint mode), `head_keys`, `guide_end`, `g_start_skip` (START in `menu_rows`, the evolve
  page and the 素材 list), `g_page`, `goal_done`, `g_mark`, `guide_at`, `hands_enh`, `hands_evo`, `guide_start` (sets the
  seen bit at once, gift), `g_back`, `guide_ev`, `g_row` (start row of `menu_rows`), `guide_ask`. `menu_rows(n, sx, sy, sel)`
  gains the start row. Hook calls in `forge_scene`, `pick_and_forge`, `item_forge`, `do_enhance`, `do_evolve`, `mats_scene`.
* make_data.py `build_guides()`: strings `GDE_T_j / GDE_L_j / GDE_H_j` (j 0-9) and `GDV_T_j / GDV_L_j / GDV_H_j` (j 0-8)
  (`{yellow}` + title; lines joined by `\n`; hint `{yellow}` + hint or `' '` so the families stay contiguous),
  `GD_GIFT`, `GD_NOSLOT_T/L`, `GD_NOEVO_T/L`, `GD_KEY_NEXT/TRY/END`; assert that the number of steps and their goals match
  `g_tab` (export `N_GDE`, `N_GDV` and the goal list to gdata.h, or generate `g_tab` itself with the context column
  kept in guide_snes.py).
* save.h `u8 guides` + `GDB_*`; mksave `--guides`.

### 1.4 Risks

* Page text width: lines up to 240 px must start at x 8 (the frame check fails from x 10: last glyph column
  ≥ 248). Measured all pages: widest 240 px (E5 line 3 `（同じ特殊効果の品なら、もっと出やすい）`) — make_data must assert ≤ 240.
* Colour runs start on a tile: lines with `{yellow}`/`{red}`/`{gold}` runs need `text_lh = 16` (E1, E2, E4, V1, V4);
  textcheck verified.
* The pick list cannot take START (the inventory group owns `pick_item`); the hint is still shown there.
* Guide pages upload up to 32×16 canvas tiles (8 KB) through the queue: a page appears 2-4 frames after the key
  press; key presses during drawing are lost (harness scripts must wait ≥ 60 frames between presses).

### 1.5 Tests (harness)

* `g1` (`mksave --open 13 --clear-all --coins 100000 --guides 0 --items 13:0,1 85:2,3`): first forge open →
  E0 at the bottom under the root, E1 (colour page) → list with the hint in its head → pick 火球インフェルノ → E2, E3 →
  強化する → E4 (card hidden), E5, E6 with `おためしに強化鉄を 1 つあげる`, the material window shows `強化鉄 ×1`.
* `g2`: same save, continue: choose 強化鉄 → result notice → E7 → item menu → E8 (rows 2-3 of the menu stay visible) → E9.
  Check `sv.mats[0]` after the gift (WRAM dump, `_sv` from the map) and `sv.guides == 7`.
* `g7` (`--guides 7`): 使い方 → 強化の使い方 → E0, E1 → pick 見習いの剣 (0 slots) → `空いたマスがない` page → list
  again → pick 古びた盾 → E2, E3 → head shows `「強化する」を選ぶ` + `START:とばす`.
* `g6` (`--guides 1`, mats 0:6 4:2): 進化する → auto evolve guide V0, V4, V5 → evolve page with the hint.
* START during a wait ends the guide (head returns to normal); leaving the forge ends it; reopening the forge does
  not restart it (bits set).
* textcheck (PROFILE=1) on g1, g2, g6, g7: 0 findings (measured).
* v1 save migrated → `guides = 0` → the guide starts on the first forge visit; a save with mats gets no gift.

---------------------------------------------------------------------------------------------------

## 2. Evolve: automatic materials and 進化できる品だけ

### 2.1 Web rules (adventure.gd 2899-3160, forge_screens.gd 211-247, 671-811, tuning.gd 1038-1080, 1797)

* **Costs** (`evolve_need`, port `evolve_need` is identical — verified on every evolvable item):
  `EVOLVE_COST` by the item's rarity: N {強化鉄 3, スーパー 1, coins 300}; N+ and R {スーパー 2, ツメ 2, 1,500};
  R+ {スーパー 3, ツメ 3, 剛爪 1, 4,000}; SR and SR+ {スーパー 3, 剛爪 3, 10,000}. Element = the target's
  (weapons) / `SHIELD_EVOLVE_ELEMENT` or the target's guard effect (shields). `EL_NONE`: ツメ → `MAT_SKYOUKA`
  (`EVOLVE_NONE_TSUME`), 剛爪 → `MAT_HKYOUKA` (`EVOLVE_NONE_CLAW`). `EL_ALL`: one ツメ and one 剛爪 of each of the 5
  elements (`min(1, n)`). Specials: `WP_017` フウイン = ミラクルベル ×`FUUIN_BELLS` (3) + SR+ coins (10,000);
  `SH_211` = 光ツメ 4, 光剛爪 2, スーパー 5, 20,000; `SH_302` = ベル 2, 5×剛爪 2, ハイパー 5, 50,000 (`WP_013` needs 地底石:
  it has no target in the port). 115 surface items can evolve; need lists have 1 (8 items), 2 (95), 3 (10), 6 (SH_201),
  7 (SH_302) entries.
* **Merge chains** (`MAT_TABLE.up`): 強化鉄 → スーパー強化鉄 → ハイパー強化鉄, 竜のツメ → 竜の剛爪 (×5 elements);
  3 lower + `MAT_UP_COINS` 200 coins make 1 upper (`MAT_UP_COUNT` 3; `MAT_UP_COINS_OF` only lists 地底石);
  `MAT_MAX` 999.
* **evolve_plan(need)** (exact): `want[m]` = needed counts. For each wanted material, walk down to the bottom of
  its chain (once per chain, in need order); walk up the chain with `carry = 0`:
  `e = have[m] + carry - want[m]`; for a wanted m: row = ok if `have ≥ want`, craft if `e ≥ 0`, else short;
  `e < 0` → `short[m] = -e`; `carry = max(e, 0) / 3`.
  If nothing is short, per chain from the top down: `d = max(0, want[c[i]] + d*3 - have[c[i]])`; `d > 0` → merge
  `[c[i-1], d]` (make d of c[i] from 3d of c[i-1]) pushed to the front, `craft_coins += d * 200`.
  `coins = need.coins + craft_coins`; `ok = no short && coins owned ≥ coins`.
* **evolve_crafting(uid)**: errors `進化できない` / `素材が足りない` / `コインが足りない`; runs every merge with
  `mat_up` (its errors `これより上の素材はない`, `素材が足りない`, `コインが足りない`, `これ以上持てない（999 まで）`), then
  `evolve` (subtract needs and coins, change id, `found_items`, `new` = first time).
* **List filter** `進化できる品だけ` (toggle button, on by default when the screen kind is evolve): rows filtered by
  `evolve_target != ""`, the right note is `→ <target name>` in yellow when `evolve_plan(...).ok`, else gray;
  empty: `進化できる品はない` (gray).
* **Evolve page** (`_evolve_body`): `進化` (yellow 18); before / after panels (icon, name, rarity, stat) with `→`;
  `効果 %s → %s` (or `効果 %s`, green); `スキルと強化のマスは、そのまま`; `要る素材とコイン` (gold); per need: name +
  `%d（持ち %d）` (white ok / orange craft) or `%d（持ち %d・あと %d）` (red short) and, under a short row, the farm
  button (3.1); if merges: `足りない分は、進化するときに作る（コイン %d）` (orange) + rows `%s ×%d（%s %d から）`
  (made ×n (from lower n×3)); elif a craft row: `オレンジは、下の素材から作れる（進化するときに作る）`; coins row
  `コイン（合計）` (when craft_coins > 0) or `コイン` + `%d（持ち %d）` (red if short); `素材が足りない（赤）。ボタンで落ちる場所へ`
  or `コインが足りない`; buttons `くわしく` (info of the evolved item) / `進化する`.
* `_ask_evolve`: short → message `素材が足りない\n赤い素材の下のボタンで、落ちる場所へ行ける`; not ok → `コインが足りない`;
  confirm `%sを進化させる？`, `→ <name>`, [`先に作る：` + `<made>×<n>` joined by `・`, `コイン %d（作る %d をふくむ）`],
  `スキルと強化はそのまま`, button `進化する`; result popup `進化した！`, icon, `<name>  <rarity>`, stats, NEW badge if new.

### 2.2 SNES design

* **Root row `進化できる品だけ`** → `pick_item` with `pick_evo = 1` (menu.c global): `make_order` drops items with
  `evolve_target == 255` (composes with the inventory group's filters); 5 rows 16 px apart (yellow and gray rows must
  not share tiles); row = `mk_item_name(…, 1)` + at x 144 in the **8-dot font** (`text_small = 8`, y + 2)
  `{yellow}→ {s}` (plan ok) or `{gray}→ {s}` (widest target 72 px → ends at 228 < 236 scroll marks). No rarity/tags
  in this list (the card below shows the item). Empty list: header line `{gray}進化できる品はない` at y 8.
* **Evolve page** (`do_evolve`, replaces the small window): full window (0,2,32,25), 10 visible rows 16 px apart,
  keys `{gray}A:えらぶ B:もどる` at y 176, ▲▼ at x 236. Rows (`er_build`, one entry per 16 px; 2-line texts take two):
  1 before (`mk_item_name`, rarity gray at 208), 2 `{yellow}→ {s}` after + rarity yellow, 3 `{green}効果 {s}`
  (+ `{green}→ {s}` at the next tile when it changes; max 84 + 84 px fits), 4 `{gray}スキルと強化のマスは、そのまま`,
  5 `{yellow}要る素材とコイン`, per need: name (x 14) + count (x 104) `{n}（持ち {m}）` / `{gold}{n}（持ち {m}）` /
  `{red}{n}（持ち {m}・あと {k}）` (orange = `{gold}`, the BG3 gold palette 0x229b), under a short need its farm row
  (3.2, x 14, blue, selectable), merges: `{gold}足りない分は、\n進化するときに作る（コイン {n}）` + per merge
  `FCR_<lower>` (`{gold}スーパー強化鉄 ×{n}（強化鉄 {m} から）`; the ハイパー one is 2 lines), else
  `{gold}オレンジは、\n下の素材から作れる（進化するときに作る）`; coins row; `{red}素材が足りない（赤）。\n選んで A で落ちる場所へ`
  (web: ボタンで) or `{red}コインが足りない`; `進化する`, `くわしく` (selectable, cursor at x 28).
  The cursor moves only between selectable rows; past the first / last one ↑/↓ scroll the page (so the top rows
  are reachable); a 2-line entry is not drawn when its second line would be cut by the bottom. Start: cursor on
  `進化する`, page scrolled so it is visible.
* A on 進化する: short → `mk_notice(T_FORGE_SHORT, T_FG_EVO_SHORT2)` = `素材が足りない` / `赤い素材を選ぶと、落ちる場所へ`
  (the web's 2nd line does not fit 192 px); not ok → `コインが足りない`; else the confirm is drawn **inside the page**
  (no free tiles for a dialog): `{s}を進化させる？`, `{yellow}→ {s}`, [`先に作る：` + `{s}×{n}` items joined by `・`,
  wrapping at x 246, `コイン {n}（作る {m} をふくむ）`], `スキルと強化はそのまま`, `はい` / `いいえ` (←→).
  Yes → `evolve_item(i)` (crafting) → `mk_notice(T_FG_EVO_DONE, evolve_fresh ? T_FG_EVO_DONE_NEW : TXT_WHITE_S)`
  = `{yellow}進化した！` / `獄炎ヘルフレイム {yellow}NEW!`. くわしく → the card of the evolved item (temporary Inv) until A/B.
  A farm row → 3.3 jump.

### 2.3 Code

* adventure.h/.c (prototype): `Plan` struct (row[7], lack[7], nshort, ncraft, ok, craft_mat[7], craft_n[7],
  craft_coins, coins), `evolve_plan()` (needs `evolve_need` first), `evolve_item` rewritten (plan, every merge with
  `mat_up_check` + `mat_up_raw`, one `save_write` at the end, `evolve_fresh`), `mat_up_check` (shared by
  `mat_up_do`), `evolve_short` removed. Host-verified: `proto/evoplan.c` vs `cmp_plan.py` → 0 mismatches in 6,000
  random cases (all 115 items, random stocks and coins).
* menu.c: `u8 pick_evo`, `make_order`, `item_row`, `pick_item` (rows/lh), `item_header` (empty text).
* forge.c: `er_*` (page rows), `er_confirm`, `do_evolve`; tables `fcr_text[N_MAT]`, `farmv_text[N_MAT]`
  (make_data can emit them as gdata.h arrays or keep the `FCR_<MAT>` / `FARMV_<MAT>` ids).
* text.s/ui.h: `{k}` / `targ_num3` (the three-number count).
* make_data: `FCR_<lower>` strings (7, `fit_break` splits the ハイパー one) and the long notes split by `fit_break`
  (break after the last `、。）` that fits 240 px, else before a `（`).

### 2.4 Risks

* The evolve page is long (up to ~26 rows for SH_302); scrolling is by rows; tested with 13-15 rows.
* 8-dot target names are small (misaki); a 12-dot alternative does not fit (name 108 + `→ ` 16 + target 108 > 236).
* `evolve_item` merges then evolves; a merge error after an earlier merge keeps the made materials (web does the same).
* `need.mat[7]` caps the need list at 7 (SH_302 has exactly 7).

### 2.5 Tests

* Host: `gcc -DHOST_TEST … proto/evoplan.c data/gen/tables.c` + `cmp_plan.py` (6,000 cases).
* `g3` (`--guides 1 --mat 0:6 --items 13:0,1 85:2,3`): 進化できる品だけ → rows `→ 卒業の剣` yellow, `→ 勇者の盾` gray
  (SH_211 special needs), `→ 獄炎ヘルフレイム` gray; 見習いの剣 → page: 強化鉄 3（持ち 6）, スーパー強化鉄 1（持ち 0） gold,
  merge `スーパー強化鉄 ×1（強化鉄 3 から）`, `コイン（合計） 500`; くわしく shows `N+ 剣 ATK 60`.
* `g4` (`--guides 7 --mat 0:6`, WP_083): スーパー強化鉄 2 gold, 炎竜のツメ `2（持ち 0・あと 2）` red, farm row
  `ドロップおすすめ：砂漠の遺跡群 5`, note `オレンジは、…`, coins 1500, the lack note; 進化する → notice.
* `g6` (`--mat 0:6 4:2`): confirm `先に作る：スーパー強化鉄×2`, `コイン 1900（作る 400 をふくむ）`; yes → `進化した！ /
  獄炎ヘルフレイム NEW!`; WRAM: mats[0] 0, mats[4] 0, coins 98,100, item id 14.
* `g8`: ↑ past the first selectable row scrolls to the top; ↓ back to くわしく.
* textcheck on g3, g4, g6, g8: 0 findings (measured).

---------------------------------------------------------------------------------------------------

## 3. 素材: descriptions, where to farm, jump

### 3.1 Web rules (forge_screens.gd 813-1003, adventure.gd 3166-3265, battle_core.gd 3802-3870, map.gd 418-436, 740-790)

* **List** (`mats_shown`: every surface MAT_TABLE row, 14): row = icon + name (white if owned else gray) + `x%d`
  (yellow / gray). Detail panel: icon, `%s　持ち %d` (yellow), line 1 (usage, white), line 2 (drops, cream), buttons
  `%d つとコイン %d で %sを作る` (when it has an upper material) and the farm button.
* **Texts** (`_mat_texts`, exact):
  強化鉄 `N の品の進化と、強化に使う` / `どのモンスターも落とす`;
  スーパー強化鉄 `どのレア度の品の進化にも、強化にも使う` / `%sから先のモンスターが落とす。強化鉄をまとめても作れる`
  (%s = area name of `MAT_LATE_AREA` = 小島);
  ハイパー強化鉄 `強化に使う（大成功が出やすい）。無属性の武器の R+・SR の進化と、イージスの盾の進化にも` / `スーパー強化鉄をまとめて作る`;
  ミラクルベル `強化に使うと必ず奇跡（虹の星）。フウインとイージスの盾の進化にも` / `ボスとラッキーキャラが、ごくまれに落とす`;
  ツメ `%s属性の R・R+ の品の進化に使う` / `%sのドラゴンのボスが必ず落とす（弱い方のボスほどよく落とす。雑魚もときどき）`;
  剛爪 `%s属性の R+・SR の品の進化に使う（イージスの盾は 5 属性とも）` / `%sのドラゴンのボスが落とす（強い方のボスほどよく落とす）。ツメをまとめても作れる`
  (%s = `Desc.ELEMENT`: 炎 水 風 光 闇).
* **Farm button**: `ドロップおすすめ：%s` (stage name), `%sのおすすめ：%s` (via a lower material), disabled
  `ドロップおすすめ：まだ行ける場所にない`. Stage name `men_name`: area + full-width digits (`小島４`),
  `<area>のたからのSTAGE`, `魔王城の地下`.
* **mat_farm(mat)** (exact): candidates = open rows of `men_rows(area)` of kind men / treasure / money in the
  reachable worlds (port: surface only; AREA_TABLE order = `area_rank`). Open: men = `men_open`; treasure =
  `tickets > 0 && area_open_now`; money = shown when `is_cleared(castle, CASTLE_MEN)`, always open.
  `v = mat_per_wave(area, men, mat)`; if `v > 0`: `score = v × (0.3 if area ∈ {AR_CASTLE, AR_SECRET} and kind ≠ money
  else 1)` (`MAT_FARM_HARD_PCT` 30); `top = max score`; pick the candidate with `score*100 ≥ top*90`
  (`MAT_FARM_NEAR_PCT` 90) and the smallest key `[area_rank, men]`. None → for each material whose `up` is `mat`:
  its `_mat_farm_of`, marked `via`.
* **mat_per_wave** = Σ over the waves of `men_detail`: boss `mat_odds(boss, boss_like=true)`;
  `((zako_lo + zako_hi)/2 + escort/2) × mean(mat_odds(zako, false))`; lucky: `w.lucky × mean(lucky)` or, when the
  stage allows luckies, `LUCKY_APPEAR_PCT/100 × mean(lucky)` (10 %) — divided by the wave count.
  men_detail (surface): waves `MEN_STAGES` 3 / `CASTLE_STAGES` 5 / `SECRET_STAGES` 1 / `TREASURE_STAGES` 3 /
  `MONEY_STAGES` 5; zako per wave `MEN_ZAKO_MIN/MAX` [1,2,2,2,2]/[2,3,3,3,3], castle [1,1,2,2,2]/[2,2,2,3,3],
  `AR_ARCADIA` one enemy, `AREA_ENEMY_MAX` {AR_PLAIN: 2}; last wave boss: men 4 = mid, men 5 = boss, castle men
  1-9 `CASTLE_BOSSES[men-1]`, castle men 10 every wave `CASTLE_FINAL`, secret `boss`, treasure guard `MON_115` with
  `TREASURE_ZAKO` [1,1,0] and `TREASURE_LUCKY` [2,2,0], money `MONEY_ZAKO` [1,1,1,1,0], `MONEY_LUCKY` [2,2,2,2,2], guard
  `MON_115`; escort `BOSS_ESCORT_MAX` 1 (0 for `NO_ESCORT` bosses and the maou forms / secret); zako pool = POOL minus
  the mid boss and the bosses; luckies (10 %) on every stage except 月見の島, Arcadia and treasure.
* **mat_odds(mon, boss_like)** (steps applied in order, `p = pct/100 × left; left -= p`): dragon of an element:
  boss → 剛爪 `claw_boss_pct` (weakest dragon boss of its element `CLAW_WEAK_PCT` 15, strongest `CLAW_STRONG_PCT` 70,
  else `MAT_CLAW_BOSS_PCT` 40) then ツメ 100; zako → ツメ `TSUME_DRAGON_ZAKO_PCT` 50. Other element monster, zako, not
  lucky → ツメ `TSUME_ELEM_ZAKO_PCT` 6. Then ベル (boss 1, lucky 3, else 0); from `AR_ISLET` on スーパー強化鉄 (boss 30,
  else 3); 強化鉄 (boss 50, lucky 50, zako 8).
* **Jump** (`map._go_farm`): only if the area is open; map, select the area, open its STAGE list with that row
  focused (scrolled to) and marked with the material icon + `おすすめ` (yellow).
* **Merge window** (`_ask_mat_up` / `_mat_up_window`): fewer than 3 → `素材が足りない`; coins < 200 → `コインが足りない`;
  `mat_up_max < 1` → `%sはもう持てない（%d まで）`; window `%s %d 個とコイン %d で` / `%sを %d 個作る？`, `－` `n / most` `＋`
  `最大`, `やめる` / `作る` (`mat_up_max = min(have/3, coins/200, 999 - have(up))`).

### 3.2 Conversion (make_data.py `build_farm`, prototype `farm.py` + `farm_gen.py`)

The web computes `mat_farm` at run time from the save's open rows. The port precomputes it for **every open state**:

* `f` = number of leading open men rows in `(area_rank, men)` order (normal play opens them strictly in that order;
  the debug "open all" opens all, f = 81), `t` = tickets > 0, `m` = castle men 10 cleared.
* For each of the 14 materials and each (t, m), Python evaluates the web algorithm in doubles (same operation order
  as GDScript) for f = 0..81 and stores the breakpoints `(f_from, stage, via)`.
* Result (measured): 56 lists, 22 unique, 220 entries, ≤ 6 per list; 23 distinct stages. `data/gen/farm.s`
  (segment `BANKC0`): `farm_index` (14 × 4 × {.word offset, .byte count}, order material × (t·2 + m)), `farm_data`,
  and the absolute symbols `_farm_lo = .loword(farm_index)`, `_farm_bk = ^farm_index`; `data/gen/farm.h`:
  `N_FARM_MAT 14`, `FARM_LIST_MAX 6`, `FARM_DATA_OFF 168`, `N_FSTG 23`, `farm_stages[23][2]` (area, men). 441 B ROM + 46 B.
* Stage names `FSTG_0..22` (port style, like the STAGE list): `<area> <n>` (`小島 4`), `<area> 宝の部屋`, `魔王城の地下`.
  (The web's `小島４` / `小島のたからのSTAGE` would need 10 full-width digit glyphs and does not fit the via rows.)
* Via strings `FARMV_<lower>` = `{blue}<lower name>のおすすめ：{s}` (7). Assert every farm text ≤ 232 px at x 14.
* Examples (`plan/forge/text/farm_breakpoints.txt`): 強化鉄 t0 m0: f1 アーケディア城 1, f4 アーケディア城 4, f9 平原の村 4;
  t1 from f36 小島 宝の部屋; m1 魔王城の地下. スーパー強化鉄 before 小島 via 強化鉄, then 小島 1 / 小島 4. ハイパー強化鉄 always via
  スーパー強化鉄. 炎竜のツメ from f21 砂漠の遺跡群 1, f25 砂漠の遺跡群 5 (= the web autotest `evolve_farm`). 炎竜の剛爪 via ツメ
  until 砂漠の遺跡群 5, then 灼熱の洞窟 5 (f70).

Runtime (`adventure.c mat_farm(mat)`): `st = (tickets ? 2 : 0) | (castle cleared ? 1 : 0)`; `far_copy` the 3-byte
index entry and the list; `farm_f()` counts leading `men_open` rows; the last entry with `f_from ≤ f` gives
`farm_stage` (FSTG index or 255), `farm_via`, `farm_area`, `farm_men`.

### 3.3 SNES design

* **素材 screen** (`mats_scene`, root row `素材`): head `素材` + coins; list (0,2,32,14), 5 rows 16 px apart:
  `MATN` white / `{gray}` when 0 at x 14, `{yellow}x{n}` / `{gray}x{n}` at x 168, keys `{gray}A:えらぶ B:もどる` at y 92.
  Detail (0,16,32,11, `mk_moved`): usage `MATD_n` white from (8, 8), drops `MATW_n` gray from the next tile row; the
  element families are templates `MATD_TSUME`, `MATW_TSUME`, `MATD_CLAW`, `MATW_CLAW` with `targ_str = ELN_<element>`
  (saves ~1 KB). Lines are broken at conversion time (`fit_break`, 240 px): at most 3 + 1 or 2 + 3 lines (fits 80 px).
  SNES change: the bell text `（虹の星）` → `（◆）` (the port shows 奇跡 as a red ◆; also drops the glyph 虹) — optional.
* A → action window (0,16,32,10, framed, over the text): `{yellow}{s}　持ち {n}`; rows (cursor x 3): farm row
  (`{blue}ドロップおすすめ：{s}` / via `FARMV` / `{gray}ドロップおすすめ：まだ行ける場所にない`, disabled), merge row
  `{n} つとコイン {m} で {s}を作る` (3, 200, upper name; only with an upper material), `もどる`.
* Merge → the web checks, then the count window (1,15,30,10, framed): `{s} {n} 個とコイン {m} で` (y 8),
  `{s}を {n} 個作る？` (y 22), `{yellow}{n} / {m}` (y 40), `{gray}←→:－＋ Y:最大 A:作る B:やめる` (y 56);
  ←→ ±1 (1..most), Y = most, A = `mat_up_do(m, n)`, B = やめる (widest line 220 px).
* **Jump** (farm row of the action window or of the evolve page): `farm_go_area / farm_go_men` set, every forge loop
  returns (`fs_exit`), `forge_scene` returns 1, `items_scene` returns 1, `menu_scene` returns `MA_FARM` (new value of
  the map-action enum in game.h). `map_scene`: after `map_setup()`, if `farm_go_area <= sv.area_open`: select it,
  `set_scroll`, `draw_top`, `draw_label`, `focus_men = farm_go_men`, `open_area()` (the old A-press code, now a
  function). `area_window` starts on the focused row; `row_draw` writes `{yellow}おすすめ` at `ROW_WAVE` (168) instead of
  `WAVE n` (men / money rows) or at `ROW_LBL` (128) instead of `チケット n` (treasure row; the tickets are in the top
  bar). The web's `king_return_due` check is not needed (the port shows the ending right after the castle).
* START during a guide wait (V5 waits for the 素材 row) works in the 素材 list (`list_run(&l, BTN_START)`).

### 3.4 Code

adventure.c `farm_f`, `mat_farm`, globals `farm_stage, farm_via, farm_area, farm_men, farm_go_area (=255), farm_go_men`;
forge.c `mat_row`, `mat_header`, `mat_detail`, `mat_count`, `mat_actions`, `mats_scene`, `matd_lines[14]`;
map.c `focus_men`, `row_focus`, `open_area`, `row_draw`, `area_window`, `map_scene`; menu.c `items_scene` /
`menu_scene` return paths; game.h `MA_FARM`, `u8 forge_scene(u16)`; make_data `build_farm`, `MATD/MATW` strings.

### 3.5 Risks

* The precomputed table assumes the open STAGE rows form a prefix (true in normal play and with the debug open;
  a hand-edited save with gaps gets the answer for its leading prefix).
* farm.s sits in $C0 next to `text_table` (9.8 KB left there after this group); any bank works (`far_copy`).
* The web's money-row rule (castle cleared) is used, not the port's debug display rule (shown with ST_DEBUG_OPEN):
  in debug mode without the castle cleared the 金策 row exists but is never recommended (as on the web).

### 3.6 Tests

* Python: `farm.py` must reproduce the web autotests: `forge_farm` (all surface STAGEs through 灼熱の洞窟 cleared:
  炎竜のツメ → 砂漠の遺跡群 5) and `evolve_farm` (same); plus a make_data assertion that every FSTG is a row that exists
  in `make_rows` (treasure areas 小島 / 孤島, money only in the castle).
* `g5` (`--open 13 --clear-all --mat 0:30 4:7`): list, ハイパー強化鉄 detail (3 + 1 lines), 強化鉄 action window
  (`ドロップおすすめ：平原の村 4`, merge row), merge 10 with Y (mats 30 → 0, スーパー 10, coins −2,000), 炎竜の剛爪 detail
  (2 + 3 lines), its farm `灼熱の洞窟 5` → map, 灼熱の洞窟 list, cursor on STAGE 5 with `おすすめ`.
* `g9` (`--open 7 --tickets 1`): 強化鉄 → `小島 宝の部屋`; ハイパー強化鉄 → `スーパー強化鉄のおすすめ：小島 宝の部屋`; jump →
  小島 list, cursor on 宝の部屋 with `おすすめ` in the label column.
* `g4`: evolve page farm row → 砂漠の遺跡群 list with `おすすめ` on STAGE 5.
* textcheck on g4, g5, g9: 0 findings (measured).

---------------------------------------------------------------------------------------------------

## Appendix: all new strings

See `plan/forge/text/strings_table.md` (173 ids with their final text, including the generated families
GDE_*/GDV_*/MATD_*/MATW_*/FSTG_*/FARMV_*/FCR_*). New ui.txt lines are in `plan/forge/text/ui_forge.txt`
(`T_FG_MATS_HEAD` already exists and is reused; add `TXT_WHITE_S {s}`).
