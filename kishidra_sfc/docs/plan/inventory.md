# Inventory and equipment: implementation spec (SFC port of キシドラ！, surface world)

Group items: (1) セット装備, (2) list filters + compare, (3) まとめて売る + N・R 一括売却,
(4) そうび詳細 (full item detail), (5) おすすめ (recommended gear) on the sortie screen.

Everything below was checked against the decompiled web source (`scratchpad/src/*.gd`) and the port
source at commit 807245d. Numbers marked **measured** come from a prototype built in
`plan/inventory/work` (a copy of the port) and run on snes9x through `tools/harness/runrom`.

Files written for this spec (all under `/tmp/claude-0/-home-user-kishidora-web/bfba2c3a-727d-51a5-9268-545e956e9958/scratchpad/plan/inventory/`):

| file | what |
|---|---|
| `proto/equip_proto.c` | C prototype: `solo_mask`, the optimised おすすめ (`reco_run`, `reco_shield`), `inv_remove_marked`, the bench |
| `scripts/reco_ref.py` | Python port of `men_detail` / `_reco_targets` / `recommend_loadout` / `_reco_shield` / `_reco_why` (float, exactly as the web) |
| `scripts/reco_int.py` | the integer algorithm of this spec, compared with the float reference on random inventories |
| `scripts/gen_reco.py` | prototype of `make_data.py build_reco()` (writes `data/gen/reco.s`) |
| `scripts/solo_check.py` | checks `solo_mask` against the web BFS (`Adventure.reachable_specials`) on every 1-3 skill list |
| `scripts/check_bench.py` | reads a save, runs the float reference on it (to compare with the ROM's picks) |
| `scripts/textcost.py` | new glyphs, widths and bytes of the new strings |
| `text/ui_inventory.txt` | every new `ui.txt` line of this group (156 strings) |

---------------------------------------------------------------------------------------------------

## 0. Common ground

### 0.1 Where the code goes (banks)

The C compiler emits every function of a file, static or not, and `menukit.h` is all static
(each screen file gets its own copy of `list_run`, `mk_notice`, ...; function pointers must stay in
their bank). New code is therefore split so that bank 10 (menu.c, 10.7 KB used) does not overflow
when the menus group also adds screens there:

| file | bank (build.py `C_BANKS`) | contents |
|---|---|---|
| `src/c/inventory.c` (new; `C_BANKS` already lists `'inventory.c': 8`) | 8 ($88, with save.c; 3.1 KB used) | solo cache, `inv_remove` / `inv_remove_marked` / `inv_new_index`, set data ops, `equip_put`, `sell_picked`, `bulk_pick` |
| `src/c/equip.c` (new; change its `C_BANKS` entry from 10 to **16**) | 16 ($90, empty) | `sets_scene`, `sell_scene`, `item_detail`, `full_bag_dialog`; its own copies of the menukit statics |
| `src/c/reco.c` (new; add `'reco.c': 15`) | 15 ($8F: tables.c there is RODATA only, so the bank's code area is empty) | おすすめ |
| `src/c/menu.c` | 10 | `pick_item` tab line, keys, compare sprites; equip 5th row; 持ち物 START |
| `src/c/map.c` | 9 | sortie おすすめ button and reasons; the full-bag dialog call |
| `src/c/rewards.c`, `src/c/gacha.c` | 7, 11 | N・R 一括売却 keys |
| `src/c/story.c` | 2 | `name_input` (generalised name entry) |

New helpers that other banks need (`mk_confirm3`, `mk_num_spr`) go into `menukit.h` behind
`#ifdef MK_WANT_CONFIRM3` / `#ifdef MK_WANT_NUMSPR`, so only the files that define the macro before
the include get a copy (each copy is about 400 B).

### 0.2 Shared prerequisites (other groups need them too)

1. **Save layout v2 with migration** (save.c). `read_slot` today rejects any save whose `size`
   differs from `sizeof(Save)`, so adding a field silently drops the player's save. One owner must
   add `SAVE_VER 2`, keep `ninv`/`inv[]` last (body_len trims the unused inventory), and convert a
   v1 slot on load: read the v1 header, copy the v1 fixed part, move `ninv`+`inv` to the v2 offset,
   zero the new fields. This group adds `EquipSet sets[15]` (360 B) and the `ST_SOLO` story bit.
2. **Text and font banks.** This group adds 156 strings = 3,032 B of glyph codes in the string
   bank (+468 B of `text_table` in $C0) and 20 glyphs (820 B). $C1 has 3,878 B free and the font
   bank $C2 3,158 B. With the other groups, `write_text` will spill into $C2 and push the fonts to
   $C3, a GFX bank. `make_data.py` needs an explicit layout (strings in $C1-$C2 or a free bank
   tail, fonts in their own bank) before these strings land.
3. **`name_input`** (story.c): the hero-name entry generalised (buffer, max length, title,
   cancel). The 名前の変更 item of another group needs the same function.
4. **`_hero_name` 16 → 18 bytes** (text.s `.res 18`, ui.h `u16 hero_name[9]`): `{name}` then holds
   an 8-glyph set name (sets are shown by copying their name into `hero_name`; `name_sync()`
   restores the hero's name when the screen closes).
5. **math.s exports** `mul_now` (pa = pa[0] × pp, 16×16→32 with the hardware multiplier) and
   `div_now` (pa /= pp, divisor 2-255) — measured in the prototype, 40 lines of asm reusing the
   existing `mul16`/`div8`.
6. **Per-STAGE target data** (`data/gen/reco.s`). The 詳細 (area/STAGE detail) item needs a
   per-STAGE wave table too; if that group builds one with zako ranges, escorts and wave bosses,
   `reco.s` can be derived from it. Otherwise this group's table stands alone (1.7 KB).

### 0.3 Inventory indices, and why the sets need renumbering

The web identifies items by a permanent `uid`; a sold item simply disappears and a set slot whose
uid is gone shows ×. The port stores inventory *indices*, and `inv_remove` compacts `sv.inv` with
`memmove`, renumbering `sv.equip`. Items only ever change position through `inv_remove`
(evolution changes `id` in place, `inv_add` appends, sorting is a view in `order[]`). So every
reference to an index must be renumbered there:

* `sv.equip[4]` (already), `sv.sets[s].slot[4]` (new): an entry equal to the removed index becomes
  `SET_GONE` (0xFFFE) for sets / `NO_ITEM` for equip; entries above it are decremented;
  `NO_ITEM`/`SET_GONE` stay.
* Arrays held by a screen while it removes items (`res.idx[]` on the result screen,
  `gacha_out[].idx`, the forge's current item) are fixed by that screen with `inv_new_index()`.

### 0.4 Controls (no clash with README controls)

Menus today: A decide, B back, SELECT sort, ←→ page, L/R tabs in the dex. New, used consistently
in every item list:

| key | item lists (持ち物, equip pick, forge picks, sell) |
|---|---|
| L / R | type tab: 全部 → 剣 → 槍 → 槌 (→ 盾), wraps |
| Y | 必殺 filter: 必殺 → 必殺のみ → 必殺以外 |
| SELECT | sort (unchanged) |
| X | くわしく (そうび詳細) — in the sell list: まとめて売る |
| START | 持ち物: まとめて売る (opens the sell list) — sell list: 全部外す |

On the detail screen: L/R page, ←→ next/previous item of the list, START ♥ toggle, A/B close.

---------------------------------------------------------------------------------------------------

## 1. Equipment sets (セット装備)

### 1.1 Web rules (adventure.gd 1796-1860, save_data.gd 423-451, menu_screens.gd 870-996)

* `Tuning.EQUIP_SETS = 15`, `Tuning.EQUIP_SET_NAME_MAX = 8`.
* A set = `{ name, uids: [equip[0] (メイン), equip[1] (サブ), equip[SHIELD] (盾)] }` — the rental
  slot is not stored. Empty set = `{}`.
* `set_register(i, name)`: name = `name.strip_edges().left(8)`, empty → `"セット%d" % (i+1)`; stores
  the current uids; `save.write()`.
* `set_rename(i, name)`: same name rule; only for a registered set.
* `set_delete(i)`: back to `{}`.
* `set_recall(i)`: for k in メイン, サブ, 盾: uid < 0 (slot was empty when registered) → only for
  サブ: `equip[1] = -1`; uid whose item no longer exists → `missing += 1`, the slot keeps what it
  has; else `equip_set(slot, uid)` (which swaps when the item sits in another slot). Afterwards, if
  メイン is empty and サブ is not, サブ moves to メイン. Returns `{ok, missing}`; `save.write()`.
* `set_is_current(i)`: the three uids equal the current メイン, サブ, 盾.
* `sets_with(uid)`: names of the sets that hold the uid (for the sell / enhance confirmations).
* Items in a set are **not** protected: they can be sold or used as enhancement material; the
  confirmation adds `（セット装備「%s」に入っている）` (names joined by `・`). After a sale the set
  shows × for that slot; recall reports `（なくなった品 %d）` (web autotest
  "a sold item: the set shows ×, recall keeps that slot").
* UI strings: section `セット装備`, `今の装備を登録して、すぐ呼び出せる`; per row number, name or
  `未登録`, badge `装備中` when current, three icons (× red for a gone item, － gray for an empty
  slot), buttons `呼び出す` (disabled when empty or current), `登録` / `編集`.
  * `_ask_set_register`: empty → name entry; registered → confirm `セット%d「%s」を` /
    `今の装備で上書きする？` button `上書き` → name entry.
  * name entry title `セット%dの名前（%d字まで）`, cancelable, starts from the old name; toast
    `セット%d「%s」に登録した`.
  * 編集 popup: title `セット%d「%s」`, buttons `今の装備で上書き`, `名前を変える`, `セットを消す`,
    `やめる`.
  * rename title `セット%dの新しい名前（%d字まで）`; toast `セット%dの名前を「%s」にした`.
  * delete confirm `セット%d「%s」を消す？` / `（今の装備はそのまま）` button `消す`; toast
    `セット%dを消した`.
  * recall toast `「%s」を呼び出した` + `（なくなった品 %d）` when missing > 0.

### 1.2 SNES design

**Port decision: a set stores all four port slots (MAIN, SUB, THIRD, 盾).** THIRD replaces the web's
rental slot but is a real owned slot in the port, so it belongs in the set. Recall treats THIRD
exactly like SUB (empty in the set → THIRD cleared). If MAIN is empty after the recall, SUB moves
to MAIN (web), then THIRD (port addition, so MAIN is never left empty when the set had a weapon).

Save fields (save.h; `Save` before `ninv`):

```c
#define EQUIP_SETS 15                 /* Tuning.EQUIP_SETS */
#define SET_NAME_MAX 8                /* Tuning.EQUIP_SET_NAME_MAX */
#define SET_GONE 0xfffe               /* the item of this slot was sold or used up (web ×) */
typedef struct {
  u16 slot[4];                        /* inventory index, NO_ITEM = empty slot, SET_GONE = gone */
  u16 name[SET_NAME_MAX];             /* glyph codes, 0-terminated when shorter; name[0] == 0: unregistered */
} EquipSet;                           /* 24 B */
  ...
  EquipSet sets[EQUIP_SETS];          /* 360 B, zeroed by save_new and by the v1 -> v2 migration */
  u16 ninv;
  Inv inv[INV_SLOTS];
```

Default name: when the entered name is empty, store "セット" + the number, as glyph codes:
make_data emits `const u16 set_name0[3]` (セ, ッ, ト) like `default_name`, digits are
`FONT_DIGIT0 + d`. (Strip: the name grid has a space cell; trailing spaces are removed before
storing, leading ones too — web `strip_edges`.)

Screens:

* **equip_scene** (menu.c): the slot list gets a 5th row `セット装備` (row 4, 16 px pitch at
  y = 8 + 4×16 = 72; the key line stays at 96). A on it → `sets_scene()`; X on it is ignored; its
  card shows `T_SETS_SUB`.
* **sets_scene** (equip.c), layout in 8×8 tiles (BG3 tile ranges in brackets):
  * head BX 0 (0,0,32,2) [1-64]: `セット装備` + coins (as `head()`).
  * list BX 1 (0,2,32,16) [65-576]: y 8 = `T_SETS_SUB` in the 8-dot font; rows from y 16, 16 px
    apart (colours differ per row), 6 visible, 15 in all (list_run scrolls); key line y 112 in the
    8-dot font `T_SETS_KEYS` `A:呼び出す・登録 X:編集 B:もどる`.
  * row: x 14 `{yellow}{n}` (1-15); x 40 the name (`TXT_NAME` after copying the name into
    `hero_name`) or `{gray}未登録`; x 144 `{green}装備中` when `set_is_current`; x 200 `{red}×{n}`
    when `set_missing > 0`.
  * card BX 2 (0,18,32,10) [577-896]: the 4 slots of the cursor set, 16 px apart: `T_SLOT_MAIN..`
    at x 14, at x 70 the item name (`mk_item_name`), `{gray}－` for NO_ITEM, `{red}×（なくなった）`
    for SET_GONE; empty set: `{gray}A で今の装備を登録できる`.
  * A: empty set → register flow; current set → `SE_CANCEL` (web: 呼び出す disabled); else recall →
    notice `「{name}」を呼び出した` / `（なくなった品 {n}）` (second line only when missing > 0).
  * X: empty → register flow; registered → popup BX 11 (6,7,20,12) title `セット{n}「{name}」`,
    rows 16 px apart `今の装備で上書き` / `名前を変える` / `セットを消す` / `やめる`; B = やめる.
  * Register / overwrite: overwrite asks `mk_confirm3(T_SET_OVER1, T_SET_OVER2, 0, T_BTN_OVER)`;
    then `name_input(buf, 8, T_SET_NAME_TITLE, NI_CANCEL)` prefilled with the old name; on decide →
    `set_register` → notice `セット{n}「{name}」に` / `登録した`.
  * Rename: `name_input(..., T_SET_RENAME_TITLE, NI_CANCEL)` → notice `セット{n}の名前を` /
    `「{name}」にした`.
  * Delete: `mk_confirm3(T_SET_OVER1 (セット{n}「{name}」を), T_SET_DEL1 (消す？), T_SET_DEL2, T_BTN_DELETE)`
    → `set_delete` → notice `セット{n}を消した`.
  * Dialogs larger than 128 tiles overlap the card's tiles (`MK_DLG_TILE` rule): close the card
    window before such a dialog and redraw it after (same rule for the sell list and the detail).
  * On exit: `name_sync()` (hero name back into `hero_name`), `save_write()` was already done by
    each operation, `fade_out()`.
* **name_input** (story.c, generalised `name_entry`):
  `u8 name_input(u16 *buf, u8 max, u16 title, u8 flags)` — `targ_num` carries the set number for
  the title; `flags & NI_CANCEL`: SELECT cancels (returns 0), key bar `T_NAME_KEYS2` in the 8-dot
  font (204 px); START decides (returns 1). `name_entry()` becomes `name_input(sv.name, 6,
  T_NAME_TITLE, 0)` + the existing default-name rule. `name_draw` copies `max` glyphs (up to 8)
  into `hero_name`.

### 1.3 C changes

* inventory.c: `equip_put(slot, i)` (moved from menu.c `equip_set`, same swap rule), `set_used`,
  `set_is_current` (4 compares), `set_missing`, `set_register(s, name, len)`, `set_rename`,
  `set_delete`, `set_recall(s)` (returns missing, 255 for an empty set; calls `save_write`),
  `sets_with(i)` (count). `inv_remove(i)`: after the existing equip loop, the same loop over
  `sv.sets[*].slot[*]` (60 entries; == i → SET_GONE, > i and not NO_ITEM/SET_GONE → −1).
* forge.c `do_enhance`: the item-source confirm becomes
  `mk_confirm(T_CONFIRM, sets_with(src) ? T_SELL_IN_SET : 0)` (web `_ask_enhance` adds the line).
* tools/harness/mksave.py: write the v2 layout (`--set N:NAME:MAIN,SUB,THIRD,SHIELD`, name in kana
  converted with font.bin.chars).

### 1.4 Costs

* ROM: inventory.c set ops ≈ 1.2 KB, set renumbering in `inv_remove` ≈ 0.2 KB, sets_scene + popup
  ≈ 4.5 KB, name_input generalisation ≈ 0.3 KB (estimates: cc65 emits ≈ 0.8-1.0 B of code per
  byte of source in this code base: menu.c 11,462 B source → 10,727 B code). Text ≈ 0.9 KB.
* SRAM/WRAM: 360 B in `Save` (both). sets_scene state ≈ 20 B.
* VRAM: BG3 tiles 1-896 + dialogs; OBJ: the menu cursor only.

### 1.5 Risks

* A forgotten `inv_remove` caller that bypasses it (none today) would desynchronise sets — keep
  all removals in inventory.c.
* Old saves: without the v2 migration, every player loses the save on the update.
* Set names in sentences rely on `hero_name`; a `{name}` text drawn on these screens must expect
  the set name (none today).

### 1.6 Test plan

1. `mksave.py --open 5 --items ...` (20 weapons, 3 shields). Script: menu → 装備 → セット装備 (row
   4) → A on set 1 → type 「ドラゴンたいじ」 (8 presses on the grid) → START → shot. WRAM dump:
   `sv.sets[0].slot == equip` and the name codes.
2. Change MAIN and SUB in 装備, back to セット装備: no 装備中 on set 1; A → recall: equip equals the
   set again, `装備中` shows (web autotest `menu_equip_sets`).
3. Register set 2 with an extra weapon as SUB, then sell that weapon (持ち物 → 売る): set 2 shows
   `×1`, card `×（なくなった）`; recall → notice second line `（なくなった品 1）`, SUB unchanged.
4. Enhance with an item of a set as material: the confirm shows `（セット装備に入っている）`; after
   it the set entry is SET_GONE and the entries above it moved down (WRAM).
5. 編集 → 名前を変える → old name prefilled → new name; → セットを消す → confirm → 未登録; → SELECT in
   the name entry cancels without change.
6. textcheck.py on all these scripts (PROFILE=1 ROM): no overflow, no colour sharing (the 16 px
   rows; the 8-dot sub-title and key line on their own tile rows).
7. Load a v1 save (today's mksave output) on the new ROM: progress kept, 15 sets 未登録.

---------------------------------------------------------------------------------------------------

## 2. List filters (type / 必殺) and compare

### 2.1 Web rules

* `FILTER_ALL = [all, WT_SWORD, WT_SPEAR, WT_HAMMER, WT_SHIELD]`, `FILTER_WEAPONS` without the
  shield; names `全部 剣 槍 槌 盾`. `SOLO_TEXT = ["必殺", "必殺のみ", "必殺以外"]`
  (`SOLO_ALL 0, SOLO_ONLY 1, SOLO_NOT 2`); the chip is "off" in state 0. Sort button `並べ方：` +
  `入手順 種類 レア度 強さ 属性`.
* `sorted_items(mode, filter, solo, guard)`: drops shields for `weapons`, weapons for `shields`,
  any other type for a `WT_*` filter, and items failing `solo_match(solo, it)` =
  `solo == ALL or solo_ok(it) == (solo == ONLY)` (a shield is never solo_ok: it passes
  必殺以外 and fails 必殺のみ).
* `solo_ok(it)`: a weapon whose own skill list alone reaches a special:
  `reachable_specials([it.skills])` is not empty. `reachable_specials` is a BFS over (position in
  each weapon's skill cycle, history): a skill with `hits > 0` is appended to the history (kept to
  `COMBO_HISTORY_MAX = 3`), the longest special sequence (2 or 3 skills) equal to the tail of the
  history fires and clears it, otherwise the last `3 − 1 = 2` entries are kept; a skill with
  `hits == 0` (精神統一, パワーため, ベルセルク) leaves the history as it is.
* Where the bar is shown: equip pick (weapons: 全部/剣/槍/槌 + 必殺; shields: sort only — the
  `盾のスキル` guard chips only appear with sub-world shields), the forge item list and its material
  pick (`FILTER_ALL` + 必殺), the shop sell list (`FILTER_ALL` + 必殺). Filters are not saved
  (they live in the screen); `sort_mode` is saved (already in the port).
* Every item row shows the yellow `必殺` chip (`Deco.SOLO_TEXT`, fill ffcc38, ink 8a1000) after
  the type chip when `solo_ok`.
* Compare in the equip pick (`_build_equip`): `v0 = item_value(cur)` of the item in the slot being
  changed (0 when empty); for each other row `v = item_value(it)`, note `ATK %+d` (weapons) /
  `DEF %+d` (shields) of `v − v0`, green (Px.GREEN) when greater, ff8080 when smaller, cream when
  equal; the item in the slot shows `装備中`. `item_value` = `def` for a shield, `atk` for a weapon —
  **without** the enhancement bonus (the そうび詳細 compare includes it; both are kept as in the web).

### 2.2 SNES design

**Solo cache.** Computing solo for 999 items takes 1.37 s (**measured**: 82 frames), too slow for
every list build, so the result is cached in the item: `Inv.slots` bit 6 (`INV_SOLO 0x40`; bits 0-3
are the slot count ≤ 9, bit 7 the ♥, bits 4-6 unused). It depends only on the skills, so it is set
in `inv_add`, in the debug skill editor (other group: call `inv_solo_update(i)`), and once for the
whole bag when a save without `ST_SOLO` (story bit 8) is loaded (`inv_solo_all()`, ≤ 1.4 s, then
`sv.story |= ST_SOLO; save_write()`). mksave.py leaves the bits 0 and ST_SOLO clear, so harness
saves are fixed on load too.

`u16 solo_mask(const u8 *sk)` (inventory.c; proto/equip_proto.c lines 8-46, 728 B **measured**):
1. P = the skills with `hits > 0`, in order (m ≤ 3); m = 0 → 0.
2. candidates = specials all of whose skills are in P (others can never match); none → 0.
3. simulate 6·m pushes of P cyclically: shift the history to 2 when it holds 3, push, find the
   longest candidate equal to the tail (ties: the first in table order), on a match set its bit and
   clear the history.
6·m pushes visit every state: a state is (position, length since the last clear ∈ 0..2), at most
3·m of them. **Checked** against the web BFS for all 127,550 one-, two- and three-skill lists of
the 50 surface skills: 1,691 lists reach a special, 0 mismatches (`scripts/solo_check.py`).

**Filters in `pick_item` / `make_order`** (menu.c): statics `list_type` (0 全部, 1 剣, 2 槍, 3 槌, 4 盾)
and `list_solo` (0-2), reset to 0 by `menu_scene`, `forge_scene`, `shop_scene` and `sell_scene` on
entry (web: per screen). `make_order(filter)` additionally skips `items[id].type != list_type − 1`
(when `list_type`) and items failing the solo rule (`inv_solo(it)`). Tabs available: filter 0, 3
and sell: 5; filter 1 (equip weapons): 4 (a stale 盾 becomes 全部); filter 2 (shields): none and no
必殺 (Y, L, R ignored). When the start item is filtered out, the cursor goes to row 0.

**List window layout** (BX_LIST 0,2,32,15 as today; inner rows 1-13):
* tile row 1 (y 8), 8-dot font (`text_small = 8`), each colour run starting on a tile:
  `{gray}L` x 8 · tabs at x 16 (`全部`, 16 px), 40 `剣`, 56 `槍`, 72 `槌`, 88 `盾` — the current one
  `TXT_YELLOW_S`, the others `TXT_GRAY_S` · `{gray}R` x 104 · `{gray}Y:` x 120 · solo state x 128:
  `必殺` gray in state 0, `必殺のみ` / `必殺以外` yellow · `{gray}SELECT:{c0}` + `T_SORT_n` from x 168
  (ends ≤ 224). Total 180 px.
* item rows: `l.y = 16`, 6 rows 14 px apart (16-97), unchanged columns: x 14 name (+N yellow in its
  own column at ≥ 128), **new x 144 `TXT_SP_TAG` (`{yellow}必殺`, 8-dot, at row y + 2)** when
  `inv_solo`, x 176 rarity (gray), x 200 slot tag, x 216 ♥. (Each of these columns has a single
  colour, so 14 px rows may share tile rows.)
* tile row 13 (y 104), 8-dot key line: 持ち物 `T_KEYS_ITEMS` (188 px), equip pick `T_KEYS_EQUIP`,
  forge picks `T_KEYS_PICK`, sell `T_KEYS_SELL`.

**Compare in the equip pick: sprites.** Per-row colours cannot be BG3 text here: the rows are 14 px
apart and an 8×8 tile has one palette. The difference is drawn with OBJ digits instead:
* make_gfx.py `menu_obj()`: tiles 12-23 = `0`-`9`, `+`, `-` from the misaki 8-dot font in colour 1
  with a 1-px shadow in colour 8; tile 26 = a red ✓ (used by the sell list). Free today: 10, 12-23,
  26-63 (8, 9, 24, 25 are the selection ring). ROM ≈ 0.4 KB raw, less after LZ.
* OBJ palettes 4/5/6 = the menu palette with colour 1 = green (96,224,96) / red (232,64,48) /
  cream (248,240,180), set by `mk_screen` when `MK_WANT_NUMSPR`.
* `mk_num_spr(x, y, s16 v, pal)`: `+`/`-` then the digits, 5 px apart (sprites may overlap).
* In the equip pick the rarity column is not drawn; `mk_sprites` draws, for each visible row
  except the item already in that slot, `v − v0` at x = box + 168, y = row + 3 (green > 0, red < 0,
  cream `+0`). v = `items[id].def` for shields, `items[id].atk` for weapons (no enhancement, web
  `item_value`). ≤ 24 sprites, ≤ 4 per scanline band.

`X` (くわしく) in every pick list opens `item_detail(order[cur], DF_BROWSE, order, norder, cur)`
(§4) and redraws the list on return.

### 2.3 C changes

* inventory.c: `solo_mask`, `inv_solo`, `inv_solo_update`, `inv_solo_all`; `inv_add` sets the bit.
* save.c `save_load`: `if (!(sv.story & ST_SOLO))` → `inv_solo_all()` (show nothing; ≤ 1.4 s
  during the title's load).
* menu.c: `list_type`, `list_solo`; `make_order` filter; `item_header` draws the tab line and
  the key line; `pick_item` handles L/R/Y (rebuild order, keep the cursor item), X (detail);
  `item_row` draws `TXT_SP_TAG`; a `list_slot`-aware `mk_sprites` for the compare digits.
* make_gfx.py: the 13 OBJ tiles and 3 palettes (`MENU_OBJ_PAL` grows from 1 to 4 palettes, or a
  separate `MENU_NUM_PAL` asset).

### 2.4 Costs

ROM: solo 0.73 KB (**measured**), filters + tab line + compare sprites ≈ 2.2 KB, OBJ tiles ≈ 0.3 KB,
text ≈ 0.4 KB. WRAM 2 B (the Inv bit costs nothing). CPU: `make_order` gains two compares per
item; `inv_add` + ≤ 1.4 ms per weapon (≤ 28 ms for 20 battle rewards, on the result screen, never
in a battle frame).

### 2.5 Risks

The 8-dot tab line must stay on tile row 1 and the first item row must start at y 16, or the
textcheck colour test fails; the scroll marks are drawn at the first and last *item* rows
(list_draw uses `l->y`), not on the tab line.

### 2.6 Test plan

1. Save with 30 items of all types, 4 of them solo-capable (e.g. skills `0,4,5` = スラッシュ,
   2れんげき, 3れんげき → ソードダンス; `24,25` → カンツウスロー; `39,39,39` would be ランページ but
   3 identical skills is a valid list too). Press L/R through the tabs, screenshot each; WRAM
   `norder` equals the Python count from the save.
2. Y cycles: 必殺のみ shows exactly the solo items (and `必殺` badges on them), 必殺以外 the rest
   including shields.
3. Equip pick on MAIN (ATK 40 sword): rows show green/red digits = atk − 40; on 盾 = def − def0;
   the equipped item shows no digits. Shot + OAM dump.
4. Load a save without ST_SOLO: after the title, every INV_SOLO bit equals `solo_check.py`'s result
   for the same skill lists (dump WRAM, compare in Python).
5. textcheck.py on 持ち物, equip pick (weapons and shields), forge item pick, forge material pick.

---------------------------------------------------------------------------------------------------

## 3. まとめて売る and N・R 一括売却

### 3.1 Web rules

* Protected: **equipped** items and **★ (fav)** items (`sell`, `sell_many`, `sellable_of`,
  `_sell_pick`, `_sell_prune`). Items in sets are **not** protected (warning line only). There is
  no other lock.
* Price: `sell_price = PRICE_TABLE[rarity] × SELL_PCT / 100`, `PRICE_TABLE = [300, 1000, 2500,
  6000, 15000, 30000]` (N N+ R R+ SR SR+), `SELL_PCT = 25` → 75, 250, 625, 1500, 3750, 7500
  coins. Coins are capped at `COIN_MAX` (port `COIN_CAP` 999,999,999).
* `sell_many(uids)`: skips gone / equipped / fav, removes each, adds the price, one `save.write()`;
  returns `{count, coins}`.
* Shop 売る tab (`_build_sell`): header `持ち物 %d/%d` + `（%d 個売るとSTAGEに行ける）` when
  `n >= INV_MAX` (red), hint `タップで選ぶ（何個でも）・★の品は売れない（長押しで★）`, the list bar
  (`FILTER_ALL` + 必殺 + sort), rows = sorted items **without equipped ones**, each with the price
  `%d コイン` (yellow, gray when ★) and a red ✓ when picked; empty: `売れる品はない`. Bottom bar:
  `全部外す` (disabled when none) and `まとめて売る` with `行をタップして選ぶ` or
  `%d 個・%d コイン`. Picks survive filter changes (a dictionary of uids; the total counts all).
  Tapping a ★ row: toast `★の品は売れない（長押しで★を外す）`.
* Confirm (`_ask_sell_picks`): one pick → `<name+N>を売る？`, `%d コイン`, and
  `（セット装備「%s」に入っている）` when in sets; several → `選んだ品 %d 個を売る？`, `%d コイン`,
  `（%d 個はセット装備に入っている）` when k > 0. Buttons `やめる` / `売る`. Toast
  `%d 個売った（+%d コイン）`.
* `inv_over() = max(0, n − INV_MAX + 1)`; a STAGE cannot start while `n >= INV_MAX`
  (`can_start`). The full-bag window: title `持ち物がいっぱい`, `持ち物 %d/%d` (red),
  `持ち物が上限より少なくなるまで、STAGEに挑めない。あと %d 個、売るか強化の素材にして減らそう`,
  buttons `売る` (opens the sell screen) and `強化` (opens the forge's enhance page).
* N・R 一括売却 (`treasure_view._bulk_buttons`, `gacha_view._sell_row`):
  `BULK_RARITIES = ["RARE_N", "RARE_R"]` — **exactly** N and exactly R (not N+, not R+);
  `sellable_of(uids, rar)` = the still-owned, not ★, not equipped items of this result with that
  rarity. Buttons `N一括売却` / `R一括売却` (disabled when none). Confirm on the result screen
  `%s の品 %d 個を売る？` / `%d コイン`; on the gacha screen
  `%s の品 %d 個を売る？` / `%d コイン（★の品は売らない）`. Sold cards get the tag `売った`; the
  result's gold box adds `売った品  +%d コイン`. Both screens also have a `売却モード` (pick any)
  — that is part of the treasure / gacha presentation items (other groups), using the same API.

### 3.2 SNES design

API (inventory.c, bank 8):

```c
extern u8 inv_pick[(INV_SLOTS + 7) / 8];   /* 127 B, bit i = item i picked for selling */
u16 inv_remove_marked(void);  /* one compacting pass over sv.inv (copy runs of kept items with
                                 mem_copy), renumbers sv.equip and sv.sets; returns the count */
u16 inv_new_index(u16 old);   /* valid until inv_pick is cleared: NO_ITEM when old was removed,
                                 else old - (picked bits below old) (popcount by nibble table) */
u16 sell_picked(u32 *coins);  /* clears the bits of equipped / ♥ items, adds the prices
                                 (coins_add), inv_remove_marked, save_write; returns the count */
u8  bulk_pick(const u16 *idx, u8 n, u8 rarity, u32 *coins);   /* sets the bits of idx[] items
                                 that exist, have exactly this rarity, are not ♥ / equipped */
```

The single-item `sell_item` stays (it uses `inv_remove`).

**Sell screen** `sell_scene()` (equip.c; title `売る`), the same list window as §2 with filter 4 =
all items except equipped ones (+ type and 必殺 filters):
* row: x 14 name, x 144 `必殺`, x 164 price `{yellow}{n}` (all rows yellow; the web's gray price
  for ★ rows cannot be per-row here — the ♥ at x 216 marks them), ✓ sprite (OBJ tile 26) at x 200
  for picked rows (the slot-tag column is free: equipped items are not listed).
* card BX 2 (0,17,32,11): y 8 `{yellow}まとめて売る {n} 個・{m} コイン` or
  `{gray}A で売る品を選ぶ（何個でも）`; y 24 `持ち物 {n}/{m}` (red `T_FULL_COUNT` when
  `ninv >= D_INV_MAX`); y 40 `{red}あと {n} 個売るとSTAGEに行ける` when full, else
  `{gray}♥（お気に入り）の品は売れない`; y 56 the cursor item's first card line (rarity, type,
  element, stars, ATK — `mk_item_detail` line 1).
* keys: A toggles the pick (♥ item → `mk_notice(T_SELL_FAV)`, web toast); X = まとめて売る
  (nothing picked → `SE_CANCEL`); START = 全部外す; L/R/Y/SELECT filters and sort (picks kept);
  B leaves (picks cleared, as the web discards them with the screen).
* The count and total are kept incrementally (u16, u32) as rows are toggled.
* X → `mk_confirm3` (28×10 window; the card is closed first and redrawn after, see §1.2):
  one pick: `{s}を売る？` (targ_str = ITEMN), `{yellow}{n} コイン`, `（セット装備に入っている）`
  when `sets_with(i)`; several: `選んだ品 {n} 個を売る？`, `{yellow}{n} コイン`,
  `（{n} 個はセット装備に入っている）` (k = distinct picked items referenced by any set: collect the
  ≤ 60 set entries, test each one's pick bit, dedupe). Buttons `売る` / `やめる` (web labels).
  **Port deviation:** the set names are left out of the line — `（セット装備「12345678」に入っている）`
  is 276 px, wider than any window.
* Yes → `sell_picked` → `SE_COIN`, notice `{n} 個売った` / `{yellow}+{n} コイン` (web's one-line
  toast in two lines), clear the picks, rebuild `order[]`, reopen the list and card.
* Entry points: 持ち物 START; the full-bag dialog; optional `持ち物を売る` row in the shop's gacha
  body (4 rows instead of 3).

**Full-bag dialog** `full_bag_dialog()` (equip.c), replacing `mk_notice(T_INV_FULL, T_INV_FULL2)` in
map.c `area_window` and `mirage_window`: window 30×14, `持ち物がいっぱい` (centred), `{red}持ち物 {n}/{m}`,
`T_FULL_TEXT` with `say_wrap` (3 lines at 224 px), cursor rows `売る` / `強化` / `もどる`. 売る →
`sell_scene()`; 強化 → `forge_scene(NO_ITEM)`; then `map_setup()`.

**N・R 一括売却 on today's port screens** (minimal; the chest / gacha presentation items rebuild these
screens and must keep the same calls):
* rewards.c result_scene (win with items): key line `{gray}A:つぎへ X:N一括売却 Y:R一括売却`
  (only when `bulk_pick` finds something; compute with a dry run); X/Y → `bulk_pick(res.idx,
  res.nitems, R_N / R_R, &c)` → `mk_confirm3(T_BULK_ASK (targ_str RARN), T_SELL_COINS, 0, T_BTN_SELL)`
  → `sell_picked` → `res.idx[k] = inv_new_index(res.idx[k])`, `res.sold[k] = 1` where it became
  NO_ITEM through the sale, `res.sold_sum += c` → redraw: sold rows `{gray}売った` at x 184, line
  `{yellow}売った品 +{n} コイン`.
* gacha.c show_results: the same keys and flow over `gacha_out[k].idx`, confirm line 2
  `T_BULK_COINS` (`{n} コイン（♥の品は売らない）`), `GachaPull.sold`.
* New fields: `AdvResult.sold[ITEMS_RUN_MAX]` (u8) and `sold_sum` (u32); `GachaPull.sold` (u8).

### 3.3 Costs

* ROM: `inv_remove_marked` 395 B (**measured**, without the set remap) + remap/`inv_new_index` ≈ 0.4 KB,
  `sell_picked` + `bulk_pick` ≈ 0.6 KB, sell_scene ≈ 3.5 KB, full-bag dialog ≈ 0.6 KB, result/gacha
  keys ≈ 1.2 KB; text ≈ 1.1 KB.
* WRAM: `inv_pick` 127 B, `AdvResult.sold` + `sold_sum` 24 B, `GachaPull.sold` 10 B, sell state ≈ 10 B.
* CPU (**measured**): compacting a 999-item bag removing 498 items = 15 frames (0.25 s) with
  per-item struct copies; copying runs with `mem_copy` should cut that further. `inv_new_index`
  ≈ 4 k cycles per call (≤ 125 byte popcounts); 64 set/equip entries + 20 result entries < 0.1 s.

### 3.4 Risks

* `res.idx` and `gacha_out[].idx` must be remapped right after `sell_picked` and before
  `inv_pick` is cleared, or the result screen points at the wrong items.
* The ✓ sprite and the compare digits share the menu OBJ tile area with nothing else today; the
  dex picture uses OBJ tiles 64-127 (no conflict).

### 3.5 Test plan

1. 999-item save (`plan/inventory/t/s999.srm`, made by the script in `t/items999.txt`): open the
   map → STAGE → full-bag dialog appears (shot) → 売る → pick 3 rows with A, one ♥ row (notice),
   X → confirm text, yes → WRAM: `ninv` −3, coins + Σ prices, equip indices still point at the
   same item ids, `inv_pick` all 0.
2. Pick a weapon that is in set 1: confirm line 3 appears; after the sale `sets[0].slot[k] ==
   SET_GONE`, the other set entries above it moved down by the number of removed items below them
   (Python check from the dumps before/after).
3. Filter 剣 + 必殺のみ, pick, change the filter, pick more, X: the total counts all picks.
4. Result screen after a win with N and R items: X sells exactly the N (not N+) unprotected ones;
   rows show 売った; the map afterwards has the right equipment.
5. Gacha 10 pulls → Y (R一括売却) → confirm says `（♥の品は売らない）`.
6. textcheck.py on the sell list (empty / some picks / full bag), the confirm windows, the
   full-bag dialog, the result and gacha screens with the new key line.

---------------------------------------------------------------------------------------------------

## 4. そうび詳細 (full item detail)

### 4.1 Web rules (ui/info_box.gd, desc.gd)

Title band `そうび詳細`, ☆/★ toggle at the right (own items). Content:
1. `"%s  %sタイプ" % [dex_no_text, 剣/槍/槌/盾]` — `No.%03d` = position in the world's weapon or
   shield dex order + 1 (`No.---` when unknown).
2. name + `+%d` (number of enhanced slots), rarity label (big, rarity colour), the picture;
   with a browse list: arrows and `%d / %d`.
3. band `スロット` + one tile per slot (empty / success / great / miracle star), right side
   `攻撃力 +%d` (weapon) or `最大 HP +%d` (shield) where plus = `enh_sum × (ENH_SHIELD_HP 10 for
   shields, else 1)`; no slots: `なし`; never-owned items: `手に入れたときに決まる`.
4. stats box: weapon `攻撃力 atk+plus`, `スタン stun`, `会心率 crit%`, `属性 炎`/`なし`;
   shield `HP hp+plus`, `防御力 def`, `ガード min(def × GUARD_PCT_PER_DEF 5, GUARD_PCT_MAX 95)%`.
5. effect box: `特殊効果なし`, or the effect name (yellow) + `Desc.effect_body` lines + the state
   line: without a loadout slot (item not equipped) `盾の欄で働く` (shield) / `メインのときだけ働く`
   (weapon); equipped: `"いま：" + STATE_WORD[state] + "（reason）"` with
   `STATE_WORD = ["", "発動", "条件つき", "発動中", "条件待ち", "発動しない"]` and colours
   NONE gray, ON yellow (ffe060), COND white, OFF gray (NOW/WAIT only in battle).
   `Desc.fx_state(loadout, k)`:
   * FX_NONE → NONE; k = サブ or レンタル → OFF `メインに装備すると発動`;
   * FK_RESONANCE: equipped weapons (メイン/サブ/レンタル) of element v1 → ON `対象 メイン サブ…`,
     else OFF `%s武器を装備していない` (`_elem`: `無属性の` / `炎の`…);
   * FK_TYPE_UP: same with the type → ON `対象 …` / OFF `剣を装備していない`;
   * FK_ALL_SR: shield and all three weapons rarity ≥ SR → ON, else OFF `全部 SR・SR+ ではない`;
   * FK_BOOST: weapon of element v1 equipped → COND `%s敵がいるとき` (`_elem` of the element that
     v1 is strong against) else OFF `%s武器を装備していない`;
   * FK_BERSERK_UP: an equipped weapon has a BF_CRIT skill → COND `ベルセルクをためたとき` else OFF
     `ベルセルクのスキルがない`;
   * FK_KILLER → COND `%sがいるとき` (race, `・` race v3); FK_PANIC → COND `%sサイズの敵がいるとき`;
     FK_FULLHP_UP → COND `HP が満タンのとき`; FK_PINCH_UP / FK_HAISUI → COND
     `HP が最大の %d%% 以下のとき`; FK_COMBO_UP → COND `コンボが %d 以上のとき`;
     FK_ROUND_POWER → COND `WAVE 2から`; FK_KILL_POWER → COND `敵を倒すほど`;
   * FK_GUARD_CUT: GC_ALL → ON; GC_ELEMENT → COND `%s敵の攻撃のとき`; GC_RACE → COND
     `%sの攻撃のとき`; GC_SCENE → COND `洞窟の戦闘のとき` / `草原の戦闘のとき`;
   * everything else → ON.
6. skill rows (3): icon, name, `Desc.skill(sk)[1:]` + `レア度：★★★☆☆` (star of the skill, 5 marks);
   shield: one row `ガード` / `盾を構えて攻撃を防ぐ。当たる直前ならジャスト、当たる瞬間ならミラクル`;
   never-owned weapon: `？？？` / `スキルは手に入れたときに決まる`.
7. compare (only with `compare`, i.e. from the treasure result): heading
   `%sの装備とくらべると` (slot name of `compare_slot`: shield → 盾; else the first of メイン/サブ
   holding the same type; else メイン), line `攻撃力 a → b（%+d）` / `防御力 …`, green / ff8080 /
   cream. v = `def` for shields, `atk + enh_sum` for weapons.
8. weapons: heading `この武器 1 本で出せる必殺技`; per special of `reachable_specials([skills])`:
   name (yellow), `Desc.special` line 2 (`A > B > C`), line 3 (`威力 %d%% ×%d回 %s`); none: `ない`.
   (`Desc.special` also has `違う武器のスキルでもつながる。コンボが多いほど強い`; InfoBox shows lines
   1-2 of the body = sequence and power.)
9. footer for own items (from 持ち物 / forge): `強化`, `進化` (disabled when no target), `売る`
   (disabled when equipped or ★) with notes `空き %d マス` / `スロットなし`, `→ 進化先` /
   `進化しない`, `装備中は売れない` / `★は売れない` / `%d コイン`.

### 4.2 SNES design

`void item_detail(u16 i, u8 flags, const u16 *list, u16 n, u16 pos)` (equip.c): full screen in the
menu layout. flags: `DF_BROWSE` (←→ through `list`), `DF_COMPARE` (block 7; set by the result and
gacha screens, as the web), `DF_FAV` (START toggles ♥ via `fav_toggle`). The port keeps its
existing action menu (持ち物 A → お気に入り / 売る / 鍛冶屋) instead of the web footer.

Windows: head BX 0 (0,0,32,2) `そうび詳細` + `{gray}{page}/{pages}` at x 208 (and `{n}/{m}` of the
browse list at x 150); body BX 1 (0,2,32,26) [65-896]. Key line inside the body at y 192 (8-dot):
`T_DT_TABS` `L/R:ページ ←→:となり START:♥ B:もどる` (160 px). Pages (L/R): weapons 3, shields 2.

Page 1 「性能」 (y in the body):
* 8: `T_DT_NO` `{gray}No.{n3}{c0}  {s}タイプ` (n = 1 + position in `dex_item` within the weapon or
  shield part; `T_DEX_NO` style), s = `WTN_0 + type`.
* 24: name (`mk_item_name`) — item icon sprite 16×16 at screen (224, 40): `PANEL_ICONS` (raw,
  128 B per item, DMA from ROM via `asset_where`) to OBJ tiles 64/65/80/81, `PANEL_PAL` to OBJ
  palette 2.
* 40: rarity `TXT_YELLOW_S` + `{gray}スロット` + the 8-dot stars (as `mk_item_detail`) or `{gray}なし`;
  right x 168 `{yellow}攻撃力 +{n}` / `{yellow}最大 HP +{n}` when plus > 0.
* 56: weapon `攻撃力 {n}` (x 10), `スタン {n}` (x 90), `会心率 {n}%` (x 170); shield `HP {n}`,
  `防御力 {n}`, `ガード {n}%`.
* 72 (weapons): `属性 {s}` (ELN) or `属性 なし`.
* 88: `{yellow}{s}` effect name, or `特殊効果なし`; 102 / 116: `FXD_n` lines (≤ 2 lines,
  **measured** max over all effects).
* 136: state line on its own tile rows (colour per state): equipped → `いま：` + `T_ST_n` +
  `（reason）`; not equipped → `{gray}メインのときだけ働く` / `{gray}盾の欄で働く`. Reasons are
  composed at run time from templates + `RACEN_` / `SZN_` / `ELN_` / `WTN_` (`T_FXS_*`, list in
  §A); the slot list of `対象` uses the port's slot labels `MAIN SUB THIRD` (web: メイン サブ
  レンタル).
* 160 / 176 (`DF_COMPARE` only): `{yellow}{s}の装備とくらべると` (s = `T_SLOT_MAIN + cs`, port
  labels) and `攻撃力 {n} → {m}` + `{green}（+{n}）` / `{red}（-{n}）` / `（+0）`.
  `compare_slot`: shield → 盾; else the first of MAIN, SUB, THIRD whose item has the same type; else
  MAIN.

Page 2 「スキル」: three blocks of 60 px from y 8: skill icon sprite 16×16 at (8, block + 16 − 2)
(`SKILL_ICONS` raw, `SKILL_ICONS_PAL` 2 palettes → OBJ palettes 3-4 by `SKILL_ICONS_SEL`), name
(white) at x 30, `T_DEX_ST` (`{gray}ST {n}`) at x 200, `SKILLD_n` lines (≤ 2, **measured**) at +14 /
+28, `T_DT_SK_RARE` (`レア度：{s}` with `T_SKSTAR_star`) at +42; all white 12-dot, 14 px apart.
Shield: `ガード` + `T_DT_GUARD_TEXT` with `say_wrap`. Never-owned (not used in the port today):
`{gray}スキルは手に入れたときに決まる`.

Page 3 「必殺技」 (weapons): `{yellow}この武器 1 本で出せる必殺技` at y 8; for each bit of
`solo_mask(it->sk)`: `SPN_n` yellow (y), the sequence like menu.c `sp_row` (gray ▷ on its own
tile, y + 16), `T_DEX_SP_POWER` (`威力 {n}% ×{m}回 {s}`, y + 30), 48 px per special; none:
`{gray}ない`. The 8-dot `{gray}違う武器のスキルでもつながる。コンボが多いほど強い` (200 px) at y 180.

Input: L/R page (redraw the body), ←→ browse when `DF_BROWSE` (wraps; the page stays), START ♥
(redraws the head's ♥ marker), A/B close (`fade_out`). The callers redraw their own screen after.

### 4.3 C changes

* equip.c: `item_detail`, `fx_state(const Inv *it, u8 slot, u16 *reason_text)` (switch on
  `effects[fx].kind` as in §4.1, using `sv.equip` as the loadout), `dex_no_of(id)` (scan
  `dex_item`), the three page drawers, icon upload (`queue_vram` 2 × 64 B per 16×16 icon).
* menu.c: X in `pick_item` → `item_detail(order[l.cur], DF_BROWSE | DF_FAV, order, norder, l.cur)`.
* rewards.c / gacha.c (when those screens get a cursor): `DF_COMPARE`.

### 4.4 Costs

ROM ≈ 4.5 KB screen + ≈ 1.2 KB `fx_state`, text ≈ 1.3 KB (templates instead of 85 per-effect
strings); art 0 B (PANEL_ICONS and SKILL_ICONS are already in ROM, raw). WRAM ≈ 30 B. VRAM: OBJ tiles
64-127 (4 + 12 tiles used), OBJ palettes 2-4; NMI: 4 × 128 B of icons per page change (well under
4,864 B).

### 4.5 Risks

* `PANEL_ICONS` are quantised to the battle panel palette (`PANEL_PAL`); if that palette changes
  per battle, load the menu copy explicitly. Fallback: `ICONS16` (4 shared palettes) but it is LZ
  packed (one 32 KB unpack) — avoid.
* The page-1 effect state must use the same rule as the battle (`b_fx[0]` = MAIN weapon,
  `b_fx[1]` = shield): SUB/THIRD effects never work, as in the web.

### 4.6 Test plan

1. 持ち物 → X on a MAIN weapon with FX_KILL_GOBLIN: page 1 `いま：条件つき（ゴブリンがいるとき）`
   (white); equip it as SUB → `いま：発動しない（メインに装備すると発動）` (gray); unequipped →
   `メインのときだけ働く`.
2. FX_RES_FIRE weapon as MAIN with a fire SUB: `いま：発動（対象 MAIN SUB）`; without fire:
   `発動しない（炎の武器を装備していない）`. FX_OUJA with non-SR gear: `全部 SR・SR+ ではない`.
3. Shield FX_GUARD_DRAGON equipped: `条件つき（ドラゴンの攻撃のとき）`; FX_NANAIRO: `発動`.
4. Page 2 for SK_SEVENCROSS (2 lines) and a buff skill; page 3 for skills 0,4,5 → ソードダンス with
   `スラッシュ ▷ 2れんげき ▷ 3れんげき` and `威力 150% ×4回 ひとりに`; a non-solo weapon → `ない`.
5. ←→ walks the filtered list; START toggles ♥ (WRAM `slots` bit 7).
6. textcheck.py over all pages for: a weapon with 3 skills, a shield, the longest item name, an
   item with 9 stars.

---------------------------------------------------------------------------------------------------

## 5. おすすめ (recommended gear) on the sortie screen

### 5.1 Web rules (adventure.gd 1590-1780, 3450-3614; sortie_screens.gd 255-352; tuning.gd 1423-1431)

Constants: `RECO_BOSS 4.0`, `RECO_MID 2.5`, `RECO_WEAK_STUN 2.0`, `RECO_TOP 12`, `RECO_DEF 20.0`,
`RECO_NO_HIT 0.3`; `CRIT_PCT 200`, `ELEMENT_ADV_PCT 200`, `WEAPON_WEAK_PCT 150`,
`ENH_SHIELD_HP 10`.

**Targets** `_reco_targets(area, men)` from `men_detail`:
* `zn = Σ_waves ((zako_min + zako_max)/2 + escort/2)`; every zako of the STAGE (`zako_pool(area)`
  minus the mid, minus this STAGE's bosses, only if some wave has zako or escorts) gets weight
  `zn / count`; every wave with a boss adds that boss with `RECO_MID` when the wave role is `mid`
  or `guard`, else `RECO_BOSS` (boss, maou, secret). No target → `["", 1.0]`.
* Waves (`men_detail`, surface): `MEN_STAGES 3` waves (castle `CASTLE_STAGES 5`, secret
  `SECRET_STAGES 1`, treasure `TREASURE_STAGES 3`, money `MONEY_STAGES 5`). Normal waves:
  zako `[MEN_ZAKO_MIN[s], MEN_ZAKO_MAX[s]]` = [1,2], [2,3], [2,3] (castle [1,2], [1,2], [2,2],
  [2,3], [2,3]), capped by `AREA_ENEMY_MAX` (AR_PLAIN 2); AR_ARCADIA (ONE_ENEMY_AREA) [1,1]. Last
  wave of STAGE 4 = mid boss (role mid), of STAGE 5 = area boss (boss); castle STAGE 1-9 last wave
  `CASTLE_BOSSES[men−1]`, STAGE 10 every wave `CASTLE_FINAL[s]` (maou on the last); secret = the
  boss (secret). Escort of a boss wave: secret `SECRET_ESCORT_MAX 0`, a `MAOU_FORMS` boss
  `MAOU_ESCORT_MAX 0`, else `BOSS_ESCORT_MAX 1`; 0 for `NO_ESCORT` bosses and AR_ARCADIA; ≤ cap−1.
  Treasure: zako `TREASURE_ZAKO` = 1, 1, 0 and the guard `MON_115` (role guard). Money (castle 20):
  zako `MONEY_ZAKO` = 1, 1, 1, 1, 0 and the guard (role guard). Mirage: every rush boss (boss).
* Lucky monsters are not targets.

**Weapon value** `_reco_eff(it, m, main)` (float):
`v = (atk + enh_sum) × (1 + crit/100 × (CRIT_PCT/100 − 1))`; if the item has skills,
`v *= Σ_s skill(s) / n` with `skill = hits == 0 ? 0.3 : power/100 × hits × (race_pct/100 if the
skill's race is the target's race)`; no target → return v; `×2` (ELEMENT_ADV_PCT) when the weapon
element's `strong` is the target's element (and not EL_NONE); `×1.5×2.0` (WEAPON_WEAK_PCT ×
RECO_WEAK_STUN) when the target's weak type is the weapon type; as MAIN only, FK_KILLER whose v1 or
v3 is the target's race → `× v2/100`.

**Choice** `recommend_loadout`: for each owned weapon: `me[i] = eff(main)`, `se[i] = eff(sub)`,
`tot = Σ me[i] × w[i]`; sort by tot descending; `top` = first 12 + for each target the item with
the best `se[i]` (first on ties) if not in top yet. Pairs: for m in top, s in top + [none], s ≠ m:
`v = Σ_i max(m.me[i], s.se[i]) × w[i]`, `× 1.0001` if m is the current メイン, `× 1.0001` if s is
the current サブ; the first strictly better pair wins.

**Shield** `_reco_shield`: for each shield: `base = hp + enh_sum × 10 + def × 20`; `v = Σ_t w_t ×
base × 100 / max(10, 100 − cut_t)` where `cut_t = v1` of an FK_GUARD_CUT effect whose condition
holds for the target (GC_ELEMENT: target element == v3; GC_RACE: race == v3; GC_SCENE: the area's
scenery == v3; **GC_ALL gives no cut here**); `× 1.0001` for the current shield.

**Apply** `apply_recommend`: for メイン, サブ, 盾 in that order, skip a negative or unchanged uid,
else `equip_set(slot, uid)`, count; `save.write()` if any. (A recommended "no SUB" therefore leaves
the current SUB.) Toast `おすすめ装備にした` / `もうおすすめの装備になっている`.

**Reason** `_reco_why`: the non-zako target with the largest weight (first on ties); its weak type
kanji (剣/槍/槌) and the element whose `strong` is its element (not EL_NONE), joined by `・` →
`ボスの弱点：…` (target kind boss) / `中ボスの弱点：…` (mid); among zako targets the weak type with
the largest summed weight (first seen on ties) → `雑魚の弱点：剣`. Shown on the sortie card under
`おすすめの理由` (gold), one line per part, colour 9ed6f0.

### 5.2 Integer version (port)

**Port decision: THIRD is chosen too.** After MAIN and SUB, the THIRD candidate t (from the same
candidate list plus "none", t ≠ m, s) maximising `Σ_i max(m.me, s.se, t.se) × w` is applied, ties →
the current THIRD (+1/8192 bonus) or none (no change). This is an addition (the web leaves the
rental slot to the friend list); see the user question.

Units: values in 1/4 ATK, u16 (realistic maximum 720 ATK × ×6 × killer ×2 = 34,560 → fits; values
are clamped at 10,922 before ×1/2/3/6 and at 65,535).

Per STAGE (from `reco.s`): rows (mon, role) with the zako rows first; `zn2 = Σ_waves (min + max +
escort)` (= 2·zn exactly); nz = zako rows. Weights scaled by 2·nz (relative weights unchanged):
zako `zn2` each, mid `5·nz`, boss `8·nz` (nz ≥ 1); in every one of the 84 surface blocks the zako
rows come first and all non-zako rows have the same weight (**checked** with `reco_ref.py`; castle
10's boss and maou waves both weigh RECO_BOSS), so `wb` is one number.

Per item (one pass over `sv.inv`; skip shields):
```
b   = (atk + enh_total) × (100 + crit × (CRIT_PCT − 100) / 100)      (u16 ≤ 31,680)
rs  = Σ_skills (hits ? power × hits : 30)    n = number of skills (0 → rs 100, n 1)
e0  = (b × rs / 100) / (25 n)                                         (pct_now, div_now: 1/4 ATK)
k_t = (weapon element strong == target element ? 2 : 1) × (target weak == weapon type ? 3 : 1)
se_t = e_t × k_t,  me_t = se_t × v2/100 when MAIN-killer race matches (mul_pct)
e_t  = e0, or recomputed with race_pct for skills whose race is the target's race
tot = Σ_zako me_t × zn2 + Σ_boss me_t × wb   (u16 saturating group sums, mul_now)
```
Fast path (no race skill whose race is among the targets, no killer race among the targets):
`tot = e0 × KW[type][element]` with `KW = Σ_t k_t × w_t` precomputed per STAGE (18 values); the
per-target best as SUB comes from the best `e0` per (type, element) class × `k_t` (exact, since
se_t = e0 × k_t for these items). Slow path items compute every target. Then candidates = top 12
(stable insertion) + per-target bests, `c_se/c_me[cand][target]` (u16), pair search with group
sums, bonus `v + (v >> 13) × nb` (≈ ×1.0001 per slot already holding the item). Shield: as the web
with `base × 100 / (100 − cut)` by `mul_now`/`div_now`, bonus `v >> 13`.

**Checked:** `scripts/reco_int.py` (this integer algorithm, line for line) vs `scripts/reco_ref.py`
(float, as the web): 200 random inventories of 20-300 items on random STAGEs of all 84 surface
blocks — MAIN 200/200, SUB 200/200, shield 200/200 identical. On the ROM (**measured**,
proto/equip_proto.c, 999-item save `t/s999.srm`, 砂漠の宮殿 STAGE 5): picks MAIN 930, SUB 744,
shield 666 = the float reference on the same save (`check_bench.py`).

Speed (**measured**, 999 items = 830 weapons + 169 shields, snes9x):

| version | 砂漠の宮殿 5 | 魔王の城 10 |
|---|---|---|
| straightforward C (eff() per item × target, u32 mul/div) | 3,690 frames (61.5 s) | — |
| + per-STAGE tables, hardware mul/div | 683 (11.4 s) | 847 |
| + fast path / slow path, group sums | 337 (5.6 s) | 388 |
| + enh_total, u16 ×k, no u32 division in loops | **248 (4.1 s)**: scan 183, candidates 7, pairs 27, shield 31 | — |

cc65 code costs ≈ 13 k cycles per weapon in the scan even so. Plan: keep the C version (it is the
reference) and **move the scan loop (`item_prep` + fast/slow path + top-12 insertion) to 65816 asm**
(estimated 1.5-2 k cycles per weapon → ≈ 0.4 s for 830 weapons), and show
`おすすめの装備をさがしている…` in a one-line window while it runs (any bag size). With 300 items the
C version takes ≈ 1.5 s.

### 5.3 Data (make_data.py `build_reco`, prototype `scripts/gen_reco.py`)

`data/gen/reco.s` in a ROM bank tail (BANKC0 in the prototype; any bank: the copy routine takes
`^reco_data`):
* `reco_index[area][men 0..20]`: `.word offset, .byte n, .byte zn2` (prototype, 1,428 B) — the
  spec layout `.word offset` (714 B) with `n, zn2` at the start of the block is smaller.
* `reco_data`: n × (mon index, role 0 zako / 1 mid-guard / 2 boss); 85 blocks (14 areas × 5 STAGEs,
  6 treasure rooms, castle 1-10 + 20, secret, mirage 18 bosses), 1,000 B, ≤ 11 rows except the
  mirage (18).
* asm `reco_block` in asset.s (next to `estat_block`, sharing `fc_src`/`fc_dst`; 60 B): ea0 area,
  ea1 men, ea3 destination → ea2 = n | zn2 << 8.
Only RK_MEN, RK_TREASURE (men 6) and RK_MONEY (men 20) rows get the button; RK_TRAIN and the
tutorial do not (no block). The mirage has no sortie in the port (its block is there for later; the
candidate arrays are sized for 12 targets, so a mirage sortie would need `RECO_T 18`).

### 5.4 SNES UI (map.c)

* sortie window: `win_open(BX_SORTIE, 2, 5, 28, 19, T_WIN, …)` — **T_SORTIE moves to T_WIN (185)**:
  the area window is closed while the sortie shows, and 28×19 = 532 tiles from 625 would pass 1024.
  Rows: 4 slots (y 8-56), buttons at y 72: `出撃する` (x 16, 48 px), `装備をかえる` (x 80, 72 px),
  `おすすめ` (x 168, 48 px, ends at the inner edge 216); cursor at x − 11 — the port's order with
  おすすめ appended so the default cursor stays on 出撃する (web order: おすすめ / そうび変更 / 開始).
* おすすめ: one-line window `おすすめの装備をさがしている…`, `reco_run(map_area, men)`, close it,
  `n = reco_apply()` (MAIN, SUB, THIRD, shield via `equip_put`; `save_write` when n > 0), notice
  `おすすめ装備にした` / `もうおすすめの装備になっている`, redraw the sortie with the reason block:
  y 96 `{yellow}おすすめの理由`, y 112 `{blue}ボスの弱点：` / `{blue}中ボスの弱点：` + `TXT_BLUE_S`
  (WTN) + `TXT_BLUE_DOT` + `TXT_BLUE_S` (ELN), y 128 `{blue}雑魚の弱点：` + WTN. The block stays
  until the sortie window is closed (web `_reco_why` lives with the sortie page).
* reco.c globals for the reason: `reco_why_role` (255 none / 1 mid / 2 boss), `reco_why_weak`,
  `reco_why_el`, `reco_why_zako` (255 = none).

### 5.5 Costs

* ROM: reco ≈ 6.5 KB C (**measured**: `reco_run` 2,954 B, `reco_shield` 903 B, statics 2.7 KB) + the
  asm scan ≈ 0.8 KB + UI ≈ 1 KB + data 1.7 KB + `reco_block` 60 B + `mul_now`/`div_now` 60 B; text
  ≈ 0.3 KB.
* WRAM: 1.9 KB of scratch (`c_se`/`c_me` 1,152 B, `kmul` 216 B, skill tables 150 B, top/best/class
  arrays ≈ 250 B, targets ≈ 60 B) — **overlay it on menu.c's `order[]`** (2,018 B, exported as
  `inv_order`; no list is open while the sortie computes); persistent ≈ 16 B (`reco_out`, reasons).
  The prototype kept them separate: equip.o BSS 2,355 B **measured**.

### 5.6 Risks

* Time: 4.1 s (C) for a full bag is long; without the asm scan the wait window is mandatory.
* The THIRD choice is a port addition (question below).
* `reco.s` must be regenerated whenever the area or enemy tables change (make_data does it).

### 5.7 Test plan

1. Saves with 20, 150 and 999 items (`t/items999.txt` generator, seeds fixed). For each of
   AR_PALACE 5, AR_CASTLE 10, AR_ISLET 6 (treasure), AR_CASTLE 20 (money), AR_SECRET 1: script map →
   area → STAGE → sortie → おすすめ → WRAM dump `sv.equip`; `check_bench.py` (float reference on the
   same save) must give the same MAIN/SUB/shield.
2. Press おすすめ twice: the second notice is `もうおすすめの装備になっている`, nothing changes.
3. Reason text: AR_PALACE 5 → `ボスの弱点：槍・闇` / `雑魚の弱点：剣` (reference output); AR_ISLET 4
   → `中ボスの弱点：…`.
4. Time: PROFILE=1 `PROF_MARK` before/after `reco_run`; target ≤ 30 frames for 999 items with the
   asm scan.
5. textcheck.py on the sortie window with and without the reason block.

---------------------------------------------------------------------------------------------------

## A. New strings (ui.txt additions; full list in `text/ui_inventory.txt`)

Totals: 156 strings, 3,032 B of codes in the string bank + 468 B of `text_table`; new glyphs (20):
登 呼 編 書 括 却 限 詳 細 働 動 条 件 違 対 象 理 由 雑 魚 (+820 B font).

Widths (12-dot, `{n}` = 4 digits, names 96 px): the widest one-line texts are
`T_SELL_IN_SETS` 208 px and `T_SELL_PICKED` 192 px (+18 px for 7-digit coins) → 28-tile windows;
`T_SET_CALLED`, `T_SET_REG1`, `T_SET_OVER1` 96 px + the 96 px name = 192 px → 28-tile notices;
8-dot lines: tab line 180 px, `T_KEYS_ITEMS` 188, `T_NAME_KEYS2` 204, `T_DT_SP_NOTE` 200.

Web strings that the port changes, and why:
* `★` (fav) → `♥` (the port marks favourites with ♥ and uses ★ for slot stars).
* `（セット装備「%s」に入っている）` → `（セット装備に入っている）` (too wide with names).
* one-line toasts → two-line notices (`セット%d「%s」に` / `登録した`, `%d 個売った` / `+%d コイン`, ...).
* slot names inserted in sentences use the port labels MAIN / SUB / THIRD / 盾 (`MAINの装備と
  くらべると`, `対象 MAIN SUB`); fixed sentences keep the web words (`メインのときだけ働く`,
  `メインに装備すると発動`).
* new (no web text): `おすすめの装備をさがしている…`, the key lines, `A で今の装備を登録できる`.

## B. Totals for this group

* ROM ≈ 36 KB: code ≈ 29 KB (reco 6.5 measured + solo 0.7 measured + removal 0.8 + sets 5.9 +
  filters/compare 2.2 + sell 5.9 + detail 5.7 + sortie 1 + asm 0.9), data 1.7, text 3.5,
  font 0.8, OBJ tiles 0.3. All code in banks 8, 9, 10, 15, 16 (code-bank tails: 276 KB free; no GFX
  bank needed).
* WRAM ≈ 0.65 KB permanent (sets 360 in Save, `inv_pick` 127, result/gacha sold flags 34, states
  ≈ 120, `hero_name` +2) + 1.9 KB reco scratch overlaid on `order[]`.
* SRAM: +360 B per slot (16 KB slots, 8.7 KB used today).
* VRAM: BG3 tiles 1-896 per screen as today (+ dialogs with the card hidden); OBJ tiles 12-23, 26,
  64-127; OBJ palettes 2-6.
* No battle-time cost.
