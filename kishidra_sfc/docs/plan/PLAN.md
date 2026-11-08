# キシドラ！ SFC port — integrated implementation plan (surface additions)

Lead-engineer integration of the 10 design specs. Read this first. The per-item rules (web numbers, every
Japanese string, screen coordinates, data conversion) stay in the group specs, which are listed in §9;
this plan says **what wins when they disagree, where everything lives, and in what order to build it**.

Base: `/home/user/kishidora-web/kishidra_sfc` at `807245d` (4 MB LoROM FastROM, SRAM 32 KB, SAVE_VER 1).
Web reference: the **shipped** scripts decoded from `index.pck` in
`/tmp/claude-0/-home-user-kishidora-web/bfba2c3a-727d-51a5-9268-545e956e9958/scratchpad/plan/bgm/shipped/`
(the decompile in `scratchpad/src` is older; checked differences that matter: `BOSS_BGM` MON_114 → 8 and
MON_089 → 17, `BOSS_FX` gains `CHAMPAGNE` (a sea special, not used on the surface, but it shifts generated
`BFX_*` ids), `SPIN_POSES` is for sub-world enemy SUSTAIN specials, **not** the hero's ランページ (the hero still
uses `ATTACK_POSES.RAMPAGE`, shipped `battle.gd:56`, so the boss_fx spin spec stands), mirage `form_swap` does
`stage += 1; turns_stage = 0` (`battle_core.gd:5802-5804`), and **every lucky in 魔王城の地下 drops one
material** (`roll_mat` → `_money_mat`, `battle_core.gd:3758-3840`; the decompile has no such branch, so no group
spec had it: conflict 33 gives it to A1 / E2 / D2)).

---------------------------------------------------------------------------------------------------------------

## 0. Decisions

### 0.1 The user's decisions (not re-opened)

- Add every listed surface screen and rule, the effects, the 18 surface mirage cards, and the 必殺10連武器ガチャ
  with tickets as the **secret-boss (ペコラータ, 月見の島) clear reward**.
- Not wanted: underground / sea / sky, pickup / ad / free gacha, 排出一覧 (so the solo gacha has no
  「出る武器を見る」), NPC friends, rankings, the profile summary, save transfer, online news / gifts.
- BGM: every missing **surface** track except `07_worldmap` (the map keeps `01_opening`).
- **`16_boss_secret` stays the full 116 s and `01_opening` stays as it is (intro + loop)**: the user answered
  「ダメです」 to cutting them to loop-only. The computed task text also allowed re-cutting `01_opening`; that
  conflicts with the user's words, so it is **not** done. The other existing loops (02, 08, 13, 50) and the
  jingles 05 / 06 are also left byte-identical: at 8 MB nothing existing has to be shortened.
- The user plays on snes9x.

### 0.2 Decisions taken here (resolving group questions that do not need the user)

| question raised by | decision | why |
|---|---|---|
| menus: raise SPC master volume? | yes: `MVOL 0x60 → 0x78` so setting 8 sounds as today; B1 measures the peak at BGM 10 / SE 10 in the busiest battle and falls back to `0x70` if it clips | the web default 8 = 0.8 × full; without it every save gets 2 dB quieter |
| menus: 討伐記録 date | `n 回目` (kill count when the record was made); no date | no clock on the SFC |
| menus: where 名前の変更 / 討伐記録 go | 名前の変更 = a row of 設定 (not from the title); 討伐記録 = a root-menu row shown once a boss is beaten | the web hosts both in the unwanted プロフィール screen |
| inventory: sets store THIRD? | yes, all four slots (MAIN / SUB / THIRD / 盾) | THIRD is an owned weapon in the port |
| inventory: おすすめ picks THIRD? | yes, greedily after MAIN / SUB (ties keep current) | otherwise おすすめ can steal the THIRD item and leave the slot inconsistent |
| inventory: 4 s おすすめ wait | ship C6 with the wait window, C7 moves the scan to asm (target ≤ 30 frames for 999 items) | testable in two steps |
| inventory: compare outside the treasure result | as the web: only from the treasure view (`DF_COMPARE`); the equip pick shows the OBJ digits | fidelity |
| forge: ◆ / ♥ wording | texts describe what the SFC shows (◆ = miracle, ♥ = favourite) | a text must match the screen |
| forge: guides for old saves | shown once (zeroed v2 block), as the web | web `guides` key absent → guide runs |
| sortie: info window stops the battle | yes (SELECT held = pause-like) | BG3 has 49 free tiles in the fight; START already freezes |
| sortie: 再挑戦 key | R | A / START = つぎへ, X / Y used by the treasure view |
| sortie: とばす key in tutorial windows | START (pause cannot open there) | free |
| sortie: mirage autosave | every calm turn, as the web (power-off resume) | fidelity; cost measured ≈ 2 frames once per turn after the asm gather |
| shared: battle keys | **SELECT held = info window, L = pet ペコラータ** (L does nothing on other enemies) | only free battle buttons |
| wings: petit flap strips | uncompressed (+16 KB ROM, 0.0 % lag measured) | 8 MB has room |

---------------------------------------------------------------------------------------------------------------

## 1. Conflicts between the specs and their resolution

| # | conflict | resolution |
|---|---|---|
| 1 | **Save fields placed separately**: forge `u8 guides` (bits 1/2/4), sortie `u8 guides` (bit 0) and `mirage_run*`, menus `nav_seen` + `shops_seen` + records, wings `mirage_cards` "after mirage_turns", chests `solo_tickets`, inventory `sets` | one v2 block (§2), one `guides` byte: `GDB_LUCKY 1, GDB_ENH 2, GDB_EVO 4, GDB_GIFT 8, GDB_NAV_FORGE 16`; every group uses these names |
| 2 | **Battle buttons**: sortie opens info with "L or SELECT (hold)", boss_fx pets with L | SELECT (hold) = info, L = pet; both specs follow this |
| 3 | **Test hooks on buttons**: boss_fx adds SELECT / SELECT+R hooks in PROFILE builds — they collide with the SELECT info window in the very builds used for textcheck / prof | no button hooks. S1 adds a `poke` command to `runrom` and a `dbg_cmd` WRAM mailbox polled in `frame_wait` only when `XDEFS=TEST_HOOKS`; commands: special now, clear wave, set HP, force wave (`FORCE_WAVE`, `FORCE_MIRAGE_WAVE` are XDEFS too), skip boredom time, unit-test entry points |
| 4 | **Result-screen keys**: inventory puts N / R 一括売却 on X / Y of `result_scene` and `show_results`; sortie puts 再挑戦 on R; chests rebuilds the result as the TREASURE view with A つぎへ, X くわしく, Y 売却モード, R 再挑戦 | chests' map wins. Inside 売却モード: X N一括売却, Y R一括売却, START 売る, B やめる. Wins go to `treasure_scene` (F1); inventory keeps only its API (`bulk_pick`, `sell_picked`, `inv_new_index`); its result_scene / show_results key changes are dropped. Gacha: A OK, X くわしく, Y 売却モード (10 pulls), R もう1回 / もう10連 (none for 必殺10連) |
| 5 | **gacha.c owned twice**: inventory (bulk keys, 4th row 持ち物を売る), chests (new view, solo panel) | chests' `gacha_view` owns it; shop body rows: [必殺10連 panel when tickets] / 1回 / 10回 / 持ち物を売る / もどる |
| 6 | **Three implementations of "which specials can a loadout make"**: menus `sp_reach` (1-3 weapons, exact BFS, 2.2 KB), inventory `solo_mask` (1 weapon), chests `solo_ok_sk` (1 weapon, 1 KB class table) | one module `loadout.c` (S7): `sp_reach()` for lists of specials (status 必殺技 tab, dex mark, そうび詳細 page 3 with nw = 1, gacha card) and `solo_ok_sk()` for the yes/no (INV_SOLO cache, 必殺10連 accept loop, filters). `solo_mask` is dropped. Test: `solo_ok_sk == (sp_reach(nw=1) != 0)` on all 127,550 ordered 1-3 skill lists (Python models) + 2,000 in the emulator |
| 7 | **sp_reach scratch**: menus lends `menu.c order[]`, but そうび詳細 opened with X from a list browses that same `order[]` → corrupt list | scratch = battle_view's `held[]` (896 B, only used while a battle window opens), exported as `bv_scratch`; `sp_reach` never runs in battle. おすすめ keeps `order[]` (lists rebuild on entry) |
| 8 | **Three effect-state evaluators**: menus (generated FXS_n / FXO_n + `fx_rule[]`), inventory (21 template strings T_FXS_*), sortie battle 状況 (switch on kind + FXO_n) | one generator (menus `fx_state_gen.py` ported into make_data, S7) and one `fx_state()` in `loadout.c` for ステータス and そうび詳細; battle_ui.c keeps a live evaluator (今の HP / コンボ / WAVE notes) on the same `fx_rule` classes and FXO_n. Inventory's T_FXS_* templates are not added |
| 9 | **Text-id clashes**: `T_DT_HEAD` (inventory / sortie), `T_ST_TAB_0..2`, `T_ST_KEYS`, `T_ST_SP_HEAD`, `T_ST_STATE_3` (menus / sortie) | sortie renames: battle 状況 → `T_SIT_*`, area / STAGE detail → `T_SD_*`; identical duplicates defined once; each lane writes `src/text/ui_<lane>.txt` (make_data reads `sorted(ui*.txt)` with one duplicate check) |
| 10 | **Item-card API**: status / records / STAGE-detail show cards for `Inv` copies; inventory's `item_detail` takes an inventory index | `item_detail(const Inv *it, u16 idx, u8 flags, const u16 *list, u16 n, u16 pos)`; ♥ and browse only when `idx != NO_ITEM`; status A and records A open it (no browse) |
| 11 | **Name entry**: menus `name_entry(u8 rename)`, inventory `name_input(buf, max, title, flags)` | `name_input()` with `NI_CANCEL` (SELECT cancels) and `NI_NONEMPTY` (START on an empty buffer plays SE_CANCEL); rename = `NI_CANCEL|NI_NONEMPTY`, set names = `NI_CANCEL` (empty → 「セット{n}」); `name_entry()` (new game) unchanged; `_hero_name` 16 → 18 B so `{name}` can show 8-glyph set names; `name_sync()` restores it |
| 12 | **pick_item** changed by inventory (tabs, Y 必殺, X くわしく, compare digits, START まとめて売る) and forge (`pick_evo`, hint title, START とばす) | inventory refactor first (C2); START mode per call: `PK_START_SELL` (持ち物 only), `PK_START_GUIDE` (forge while a guide waits), else ignored |
| 13 | **map.c area window touched by five groups**: forge (focus row, `MA_FARM`, `open_area`), sortie (focus + go for 再挑戦, X / Y detail, mirage window), inventory (full-bag dialog, おすすめ in the sortie window), menus (NEW tags, `shop_see`, badge after_battle) | E1 refactor first, no visual change: `open_area()`, `area_window(u8 focus_men, u8 flags /*AW_GO, AW_FARM*/)`, a row-tag callback, extra keys for `list_run`, a sortie button table; then each feature is a local change |
| 14 | **Three copies of the web men_detail wave rules**: sortie `plan.s` (1,321 B), inventory `reco.s` (1.7 KB), forge farm (its own Python port) | one Python port `tools/data/mendetail.py` (from sortie `gen_plan.py`); `plan.s` in TABLES + asm `plan_block`; おすすめ derives its targets from `plan_block` at run time (no `reco.s`, −1.7 KB); forge's farm table is generated from the same module |
| 15 | **`b_turns_stage` added twice** (menus records, sortie tower), reset rules differ | one counter (A1): 0 in `start_stage`, ++ with `turns_total` in `enemies_turn`, reset in `form_swap` **only in the tower** (web 5802-5804); 魔王戦 counts all forms. Same step fixes the port bug that tower forms keep form 0's stage (`form_swap` lacks `++b_stage`, verified in `battle_core.c:982`) |
| 16 | **CODE5 (battle_view.c) nearly full**: 27,195 B today, claims → ≈ 31.4 KB | new battle code in new files: `battle_ui.c` + `battle_fx.c` (bank 6), `battle_drop.c` (chest row + drops, bank 18); gate: CODE5 ≤ 30 KB after every battle step |
| 17 | `'equip.c': 10` in today's C_BANKS; inventory wants bank 16, which is a BGM bank today | after S2 BGM is placed by romplace; `equip.c` → 16, `reco.c` → 15, `status.c` → 17, `battle_drop.c` → 18, `loadout.c` → 8, `treasure.c` → 7, `chestfx.c` → 11 |
| 18 | **Battle OBJ strips / palettes** claimed by boss_fx (strip 4 + pal 4 in boss stages; strips 2-4 + pals 2-4 in the intro), chests (misc strip row 2 = CHEST16, r1c14 spark, r3c14 coin), sortie (FX strip row 2 cols 10-13 sparkle), wings (petit flaps inside each slot strip) | §5.1 table; verified: zako slots are assigned 0,1,2 to non-boss actors in order (`battle_view.c:297`), so with a boss on the field slot 2 / strip 4 / pal 4 is free; misc row 2 is empty today |
| 19 | **Battle BG3 tiles**: sortie windows 87-502 (416 tiles) overlap the combo (439-462) and the chest row (484-502) | `bv_modal_close()` re-uploads both; the row is hidden while a window (pause, 状況, info, tip, talk) is open |
| 20 | **Colour math owners**: existing special flash and WARNING write CGWSEL / CGADSUB / COLDATA directly; boss_fx adds the `cm_req` arbiter; menus (key scene dim), chests (treasure flash, gacha dim) use colour math on menu screens | in battle everything goes through `cm_req` (A6, port of the two existing users first, A/B identical); on menu screens `mk_screen` / `sys_init` reset CGWSEL, CGADSUB, COLDATA, MOSAIC, WOBJSEL / WOBJLOG and HDMA ch 4/5 |
| 21 | **Pose codes / descriptor buffers**: boss_fx codes 7-17, `bdesc[200]`, Actor arrays [18]; wings codes 18-20, `pcode[21]`, `zdesc[3][200]` | A5 reserves all codes and buffer sizes once (no visual change) |
| 22 | **Mirage snapshot vs. later battle state**: boss_fx adds `Foe.asleep` and view state, wings adds lift / flap state, PH_INTRO | A15 comes last in the battle lane; `Foe` copied whole and `asleep` cleared on restore (view-owned); PH_INTRO is never calm; a restore posts `EV_STAGE_START c=1`, which suppresses intro, slide and WARNING |
| 23 | **RNG state**: sortie adds `rnd_state` in sys.c for the snapshot; chests moves the RNG into asm (bit-identical, faster) | S7 moves it to math.s with an exported `_rnd_state`; A15's table includes it |
| 24 | **WRAM $7E budget**: 7,146 B free; claims ≈ 6.0 KB **plus** chests' solo tables as RODATA (1,287 B) = overflow | big const tables (`solo_tab` 1,000 B, BFX recipes, reco/plan/farm) are ROM blobs (romplace, `long` + small asm accessors); screen-local and battle-local state share one arena `ovl[1536]` (§5.4) |
| 25 | **Sprite lines in the money stage**: three flapping golden petits (lucky) + idle hero = 34 slivers on some lines (wings, at the limit), and the lucky sparkle (sortie) and flying drops (chests) add more on the same lines | sparkle adapts: with ≥ 2 flapping petits on the field, 1 star per lucky and the glint only when that petit is not in `flap_mid`; drops fly above the field rows of the enemies' feet; A16 runs `oamlines.py` on the money stage and on `FORCE_WAVE=0,0,6` |
| 26 | **Dex palettes**: menus recolours BG3 palettes 4 / 5 / 7 colour 3 on dex screens (bronze, silver, rainbow); wings uses `{purple}` for the mirage hint / mark (would cycle rainbow) and recolours `cgram[27]` (palette 6 colour 3 = frame **and** `{gold}` text) to #c070ff on the mirage view | wings' hint and mark use `{gold}` (gold on the normal card, purple on the mirage card = frame colour in each view); on the mirage view the tier-3 count uses `T_DEX_CNT_3M` (`{yellow}×{n}`) |
| 27 | rom_compat's `tools/banks.py` (fixed bank lists per kind) vs shared_infra's `romplace` (automatic placement); bgm's `pack_bgm.py` | romplace for everything (BGM = its `stream` kind, same `bgm_table` format as bgm's packer); rom_compat contributes `finish_rom()` (bank $00 copy, both headers, mirror checksum), the 80-byte zero areas at `$81:FFB0` and `$01:FFB0`, `romcheck.py` and the probe ROMs |
| 28 | rom_compat's budget counted `01_opening` as a 34 s loop (5287.2 KB total) | superseded by the bgm group's numbers with `01` kept full (§4) |
| 29 | **audio regeneration**: menus' MVOL change (driver.py → spc.bin), bgm's new tracks (bgm.brr), S3's full regeneration (SE samples change 3,306 B) | S3 regenerates once; B1 and H1 each re-run `build.py audio`; whichever lands second rebases and regenerates (outputs are deterministic) |
| 30 | **Pause chest icons**: chests suggests CHEST16 sprites in the pause; sortie shows text only (OBJ under a BG3 window cannot show) | text only: 「宝箱 ×n」 |
| 31 | menus names the status tabs 装備 / 必殺技 / 特殊効果, shared_infra wrote ステータス / 必殺技 / 特殊効果 | menus' names (the owner); the screen title is ステータス |
| 32 | sortie's bag-full check before 再挑戦 uses `mk_notice(T_INV_FULL…)`; inventory replaces those notices with `full_bag_dialog` | `full_bag_dialog` everywhere once C5 lands (E3 calls it when present) |
| 33 | **A web rule with no owner** (found in the S3 review): the shipped build's `roll_mat` (`battle_core.gd:3758-3760`) starts with `if is_money() and e.is_lucky(): return _money_mat()`, and the money STAGE detail (`map.gd:1279-1282`) and the forge farm model (`adventure.gd:3302-3329` → `mat_odds(…, money)`, `battle_core.gd:3845-3853`; the detail's material line is `money_mat_text`, `adventure.gd:3332-3337`) follow it. The decompile predates it, so no spec has it; the port's `battle_hit.c roll_mat` gives a money lucky the plain lucky odds (ベル 3 %, スーパー強化鉄 2.91 %, 強化鉄 47.05 %, nothing 47.05 %) | split over the steps that own the code: **A1** the battle rule, **E2** the two STAGE-detail lines, **D2** the farm model (rows A1 / E2 / D2 of §6; detail in `sortie_battle.md` §2 and `forge.md` §3). Surface rule (`money_mat_steps(g, w)` 3820-3829 with `World.built(w)` false, `_money_mat` 3831-3840): steps in this order, the first hit wins, each through `mat_roll(miner(pct))` (miner is the identity on the surface: no surface effect has `FK_MINER`): ミラクルベル `MAT_BELL_LUCKY_PCT` 3, スーパー強化鉄 `MONEY_MAT_SKYOUKA_PCT` 15 (`T_MONEY_MAT_SKYOUKA_PCT`, generated since S3), 強化鉄 100. Odds 3 / 14.55 / 82.45 %, never nothing (FX_YOKUBARI's drop 150 % gives 4 / 21.12 / 74.88 %, still never nothing). No ツメ step and no `MAT_LATE_AREA` step for these luckies; zako and the guard keep the normal `roll_mat`; `roll_goods` (フエルスロット) unchanged. `T_MONEY_MAT_COMMON_PCT` and the world `rare` step are sub-world only |

---------------------------------------------------------------------------------------------------------------

## 2. Save data v2 (one layout for every group)

v1 (today, verified in `src/c/save.h`): 8,702 B, `ninv` at 628, `inv[1009]` at 630, slots `$0000` / `$4000`
(16 KB), header 12 B (`KSFC`, seq, sum, size, ver), body first, header last, `read_slot` accepts only
`ver == 1 && size == 8702`. Existing fields already cover: BGM / SE volume (108 / 109, default 8), guard delay
(106), hero name (88, u16 glyph codes), tutorial, debug-open (`story` bit 4). Orbs and the badge are derived
from `cleared[]`.

### 2.1 Layout (SAVE_VER 2, `sizeof(Save)` = 9,300 B, measured in the shared prototype)

| offset | field | bytes | owner / meaning |
|---|---|---|---|
| 0-627 | all v1 fields, unchanged offsets | 628 | — |
| 628 | `u8 guides` | 1 | `GDB_LUCKY 1` lucky tip (sortie), `GDB_ENH 2` / `GDB_EVO 4` forge guides, `GDB_GIFT 8` 強化鉄 gift, `GDB_NAV_FORGE 16` forge NEW seen (menus); bits 5-7 free |
| 629 | `u8 solo_tickets` | 1 | 必殺10連武器チケット 0..99 (chests) |
| 630 | `u8 mirage_run` | 1 | 1 = a tower run is going / suspended (sortie) |
| 631 | `u8 mirage_cards[3]` | 3 | bit w = boss of WAVE w+1 (18 bits) (wings) |
| 634 | `u16 mirage_run_id` | 2 | ++ per run; the snapshot record must match (sortie) |
| 636 | `u16 shops_seen` | 2 | bit = area index of a shop town (1, 3, 5, 8) (menus) |
| 638 | `u16 boss_wins[2]` | 4 | 魔王 / 裏ボス kills since v2 (menus) |
| 642 | `BossRec rec[4]` | 160 | 0 魔王 first, 1 魔王 latest, 2 裏ボス first, 3 裏ボス latest; `BossRec` = `u8 valid, difficulty; u16 turns, boss_turns, nth; Inv w[3]; Inv shield` (40 B) |
| 802 | `EquipSet sets[15]` | 360 | `u16 slot[4]` (index, `NO_ITEM`, `SET_GONE 0xFFFE`), `u16 name[8]` (glyph codes; `name[0]==0` unregistered) (inventory) |
| 1162 | `u8 spare[64]` | 64 | zero; later fields whose zero value is right for older saves need no version bump |
| 1226 | `u16 ninv` | 2 | — |
| 1228 | `Inv inv[1009]` | 8,072 | must stay last (`body_len` writes only `ninv` items) |

Existing bytes gain meanings: `story` bit 3 = `ST_SOLO` (INV_SOLO bits are valid); `Inv.slots` bit 6 =
`INV_SOLO` (bits 0-3 slots, bit 7 ♥; bits 4-6 were unused — verified).

Outside `Save`: the tower snapshot record at slot + `$3E00` (SRAM `$3E00` and `$7E00`, 512 B each): header
14 B (`KMSN`, u16 seq, sum, len, run id, reach turns), body ≤ 498 B (measured 296-332 B), body first, header last.
Compile-time guard in save.c: `extern char save_fits_slot[0x3E00 / sizeof(Save)];` (fails when Save > 15,872 B).
Free per slot after v2: 6,572 B.

### 2.2 Migration (save.c, prototyped and run by shared_infra)

`read_slot()` returns 2 (v2), 1 (v1: ver 1 and size 8702), 0. `load_slot()` for v1: read 8,690 B, check the v1
sum over `SAVE_V1_SIZE − HDR − (1009 − ninv)·8`, `memmove` ninv + bag from 628 to 1226, zero 628..1225, set
ver 2 / size 9300, `v1_init()`: `shops_seen` |= every shop town ≤ `area_open` (web `_seen_init`),
`guides |= GDB_NAV_FORGE` if `area_open ≥ AR_PLAIN`. Everything else starts at zero, which is the right v1 state:
guides and the lucky tip show once, no boss records (records screen says 「この記録をつけはじめる前に倒したので、
記録はない」 for bosses already cleared), no cards, no suspended run, 0 tickets, `ST_SOLO` clear →
`inv_solo_all()` on load (≤ 10 frames with `solo_ok_sk`) then `save_write`. The v1 slot stays as the fallback
until the second v2 write. Downgrade: an older ROM sees v2 as "no save" — README note: copy the `.srm` first.

`tools/harness/mksave.py`: v2 by default; `--v1` writes today's layout byte-identically; new options
`--guides N` (default `0x1f`, so existing scripts see no tip / guide), `--solo N`, `--mirage-cards MASK`,
`--shops-seen MASK` (default `0xffff`), `--set N:NAME:MAIN,SUB,THIRD,SHIELD`, `--boss-rec K:...`, `--mat IDX:N`,
`--kills MON:N`, `--got ID:N`. The v2 block length is asserted (598 B).

---------------------------------------------------------------------------------------------------------------

## 3. Menu tree and key map after the change

### 3.1 Tree (new parts marked ＋; owner step in brackets)

```
タイトル        つづきから / はじめから / ＋設定 [B1]            (＋ロゴ [B1])
  設定          BGM の音量 / 効果音の音量 / ガードの遅れ補正 / もどる   (from the title: no 名前の変更)

地図 (map)      ←→↑↓ エリア, A 開く, Y/X/START メニュー, L×3 / R×3 デバッグ;  ＋help line NEW!! [B2]
  エリアの窓     [アーケディア城: チュートリアル, カカシ] STAGE 1..n, 宝の部屋, 金策, ガチャ(＋NEW!!) [B2], 道具
                 ＋X: STAGE の詳細 / ＋Y: エリアの詳細 [E2]   (月見の島: only after its clear)
                 bag full → ＋持ち物がいっぱい dialog: 売る / 強化 / もどる [C5]
    出撃の窓     出撃する / 装備をかえる / ＋おすすめ (+ おすすめの理由) [C6]
  蜃気楼の窓     挑戦する / ＋続きから（WAVE n / 18） / ＋はじめから (confirm) [A15]

バトル          ＋宝箱の列 + 飛ぶ宝箱 [A14]; ＋ラッキー演出 + はじめての説明 [A4]; ＋SELECT 長押し: 情報 [A3]
                ＋L: ペコラータをなでる [A9]; effects [A6-A13]; チュートリアル窓: ＋START とばす [A3]
  ポーズ (START) 続ける / ＋状況 (敵・武器・盾と必殺技・効果) / 離脱する  [A2]
                 塔: 続ける / 状況 / やめる / ＋セーブして中断 [A2, A15]
  結果 (勝ち)    ＋TREASURE: あける → 開いた宝箱 (X くわしく, Y 売却モード) → 獲得報酬 (素材は種類ごと) [F1]
                 ＋R 再挑戦 (last page) [E3 contract, F1]
  結果 (負け)    失った報酬 + ＋宝箱の列 [F1]
  塔の結果       + ＋蜃気楼カード +n [G1]

メニュー (one column, rows built per open, window (8,2,16,25), 16 px) [B2]
   1 装備          MAIN / SUB / THIRD / 盾 / ＋セット装備 [C4]
   2 持ち物        ＋L/R 種類, ＋Y 必殺, SELECT 並べ方, ＋X そうび詳細 [C2, C3], ＋START まとめて売る [C5]
                   action: 鍛冶屋 (＋gray until 平原の村) [B2] / 売る (＋set warning) [C4]
   3 鍛冶屋        ＋gray + notice until 平原の村, ＋NEW!! [B2]
                   強化・進化する品を選ぶ / ＋進化できる品だけ [D1] / ＋素材 (説明・落ちる場所・作る) [D2] / ＋使い方 [D3] / もどる
   4 ショップ      ＋NEW!! [B2]; opens the newest shop town (all marked seen)
                   ＋必殺10連 panel (tickets > 0) [F3] / 1回 / 10回 (＋確認・演出・もう1回) [F2] / ＋持ち物を売る [F2] / もどる
   5 図鑑          ＋数の色 銅・銀・金・虹 [B6]; card: ＋A 蜃気楼 / 通常 [G1]
   6 ＋ステータス  (replaces 必殺技) tabs 装備 / 必殺技 / 特殊効果 [B3]
   7 ＋大切なもの  4 宝玉 + ウサギのバッヂ [B4]
   8 ＋討伐記録    only once 魔王 or 裏ボス is beaten [B5]
   9 難易度
  10 設定          BGM の音量 / 効果音の音量 / ガードの遅れ補正 / ＋名前の変更 [B1]
  11 タイトルへ
デバッグ (R×3)    ＋地上クリア, ＋スキルを変える [B7] (rows scroll, 7 visible)
```

11 rows is the one-column maximum (last row at y 168, text ends at 180 of 192). `menu_text[]` and the
switch use row ids, not positions.

### 3.2 Key map

| screen | keys |
|---|---|
| title | ↑↓ row, A / START decide |
| map | unchanged |
| area window | A decide, B back, X STAGE の詳細, Y エリアの詳細 |
| sortie window | ←→ buttons (出撃する / 装備をかえる / おすすめ), A, B |
| battle | Y / X / A skills, B guard, R meat, ←→ target, START pause, **SELECT held = info** (←→ target, Y/X/A skill card, B hero states), **L = pet** (ペコラータ only) |
| tutorial window | A next / try, START とばす (confirm: ←→, A) |
| Pekorata entrance | A / B close the talk only |
| treasure view | closed: A あける; opening: A / B / START skip; open: ←→↑↓ cursor, A / START つぎへ, X くわしく, Y 売却モード, R 再挑戦 (last page); 売却モード: A pick, X N一括, Y R一括, START 売る, B やめる; gold page: A つぎへ, R 再挑戦 |
| gacha view | A / B / START skip, then A OK, X くわしく, Y 売却モード (10), R もう1回 / もう10連 (not 必殺10連); sell page adds SELECT 一括選択 / 全部はずす |
| item lists | L / R type tab, Y 必殺 filter, SELECT sort, X くわしく, START まとめて売る (持ち物) / とばす (forge guide waiting) |
| そうび詳細 | L / R page, ←→ next item (browse), START ♥, A / B close |
| sell list | A pick, X まとめて売る, START 全部外す, L/R/Y/SELECT filters, B back |
| sets | A 呼び出す・登録, X 編集, B back |
| forge | guide pages A つぎへ / やってみる / おわる, START とばす; count window ←→ ±1, Y 最大, A 作る, B やめる |
| dex | L / R tab, SELECT kind, A くわしく; card ←→ next, A 蜃気楼 / 通常, B back |
| status / records / keys | L / R tab, ↑↓ row, A そうび詳細, B back |
| settings | ↑↓ row, ←→ value (0..10, applied at once), A on 名前の変更, B back |
| name entry | A input, B erase, L / R page, START decide, SELECT cancel (rename, set names) |

---------------------------------------------------------------------------------------------------------------

## 4. ROM budget, layout and BGM loop windows

### 4.1 Per item (KB; "m" = measured with the port's converters / a built prototype, "e" = estimate)

| item | KB | | item | KB |
|---|---|---|---|---|
| Settings + title 設定 (m) | 0.9 | | Retry (m) | 1.0 |
| Title logo baked (m) | −0.47 | | Area / STAGE detail (m) | 10.4 |
| Status screen incl. sp_reach (m) | 8.5 | | Mirage suspend / resume (e) | 3.2 |
| Key items + badge (art 5.5 m) | 7.4 | | Pause contents + 状況 (m) | 10.7 |
| Rename (e) | 0.3 | | Battle info window (m) | 2.1 |
| Boss records (e) | 5.0 | | Lucky presentation + tip (m) | 1.2 |
| NEW marks + forge unlock (e) | 0.9 | | Tutorial skip + advice (m) | 1.1 |
| Dex count colours (e) | 0.75 | | Treasure chests (art 11.1 m) | 27.0 |
| Debug additions (e) | 2.0 | | Gacha presentation (circle 1.8 m) | 13.0 |
| Equipment sets (e) | 7.1 | | 必殺10連 (tables m) | 4.2 |
| List filters + compare (e) | 3.6 | | Pekorata entrance (art m) | 3.5 |
| Bulk sell + N・R (e) | 7.8 | | Pekorata idle poses (art 22.7 m) | 23.6 |
| そうび詳細 (e) | 7.0 | | Boss specials 24 kinds (art m) | 11.4 |
| おすすめ (m, −1.7 with plan.s) | 8.7 | | Dark aura (m) | 1.8 |
| Forge guides (m) | 6.8 | | Hero spins (art 2.5 m) | 2.8 |
| Evolve auto-materials (m) | 6.6 | | Wing flaps (art m, raw petits) | 64.0 |
| Materials + farm (m) | 5.0 | | Mirage cards 18 (art m) | 79.6 |
| Shared: glyphs +59 (m), save v2, menu table, string dedupe | +2.0 | | BGM routing + stream tables | 1.7 |
| consolidation (fx templates dropped, solo_mask dropped) | −1.0 | | | |

**Non-music total ≈ 341 KB.** New BGM (20 tracks, 8 MB windows below): **3,941,370 B = 3,849 KB** (m).
ExLoROM bank `$00` (copy of `$80`): 32 KB. **Total added ≈ 4,222 KB.**

Capacity (romplace, tail packing): today 545.3 KB free in 4 MB; ExLoROM adds 125 usable 32 KB banks
(`$01-$7D`; `$00` is the copy, file `0x7F0000-0x7FFFFF` is unreachable) = 4,000 KB.
- **4 MB**: 545 − 341 = ≈ 204 KB for 20 new tracks → ≈ 1.6 s each. Not usable. (Even re-cutting the existing
  02/08/13/50 too would give ≈ 6 s windows.) All non-music items fit in 4 MB (used during development).
- **6 MB ExLoROM**: ≈ 2,219 KB for the new tracks → **bars-24 windows** (2,166 KB, loops ≈ 17.5 s on
  average), ≈ 50 KB spare.
- **8 MB ExLoROM** (recommended): new 18 tracks at **bars-38 windows** (loops 03 35.2, 04 25.6, 09 36.2,
  10 25.6, 11 35.3, 12 35.2, 14 32.0, 15 35.3, 17 26.4, 18 33.3, 19 28.8, 22 19.7, 23 32.0, 24 35.2, 25 34.4,
  26 36.0, 27 29.3, 49 19.0 s — average 30.8 s), `20_boss_mimikkun` and `21_boss_nazokimos` with their composer
  loops (43.6 s, 38.4 s); existing tracks unchanged; **≈ 354 KB spare** (shared simulation of every group's
  additions: 357 KB, 180 BGM segments of 255). If non-music growth passes ≈ 690 KB, bars-34 frees ≈ 396 KB.
- (ExHiROM fallback, only if asked: 8 MB ≈ bars-38; 7.5 MB for Super EverDrive ≈ bars-36, estimate.)

All new tracks: mono 11,008 Hz BRR (6,192 B/s), normalised 0.85, from the Safari `loop_offset`, loop only,
bar-aware loop finder (bgm spec §2.3; every new join rhythm-continuous, lag ≤ 12 ms). 07_worldmap not added.

### 4.2 Layout recommendation

**ExLoROM 8 MB, FastROM for `$80-$FF`** (the first 4 MB byte-for-byte today's LoROM), extended banks
`$01-$7D` SlowROM holding only DMA-read data and the BGM stream, bank `$00` = copy of `$80`, header `$FFD5=$30`,
`$FFD7=$0D` in both copies, mirror checksum, SRAM unchanged at `$70:0000` (saves keep working).
Runs on: snes9x 1.54+ (PC 1.6x, RetroArch "Snes9x", the harness core 1.61: verified `Map_JumboLoROMMap`),
snes9x2010 / 2005 cores, bsnes v115 / ares, FXPak Pro / SD2SNES firmware ≥ 1.8.0, MiSTer. Does **not** run on:
Mesen2 (no ExLoROM mapping), Super EverDrive X5/X6 (7.5 MB load limit, ExLoROM undocumented), snes9x
Wii / GameCube builds and 1.50-1.53 (6 MB max: use `ROM_MB=6` with bars-24 windows), bsnes-mercury (black,
verified). The build keeps `ROM_MB=4|6|8`. ExHiROM (runs on Mesen2 too, ≤ 7.5 MB for EverDrive) is a
separate L-size step after romplace, only on request (§7).

---------------------------------------------------------------------------------------------------------------

## 5. Shared resource tables (every step must respect these)

### 5.1 Battle VRAM / OAM / palettes / HDMA

| resource | use |
|---|---|
| OBJ strips 0-1 | hero (ランページ spins fit in one strip each) |
| OBJ strips 2/3/4, pals 2/3/4 | zako slots 0/1/2 (petit flap poses inside the slot's strip). **Boss stages: strip 4 + pal 4 = boss FX** (BFX cells, Z / ! / hearts, aura flames). **Pekorata entrance: strips 2-4 + pals 2-4 = king + orbs** (月見の島 and tower WAVE 18 have no other enemy) |
| OBJ strip 5 (misc) | shield (0,1,16,17), cost badges (row 0-1 cols 8-15), row 2 = CHEST16 (chests, empty today), r1c14 spark, r3c14 coin, row 3 marks |
| OBJ strip 6 (FX), pal 5 | FX + lucky sparkle stars row 2 cols 10-13 (sortie) |
| OBJ strip 7 (cells) | FX cells (GIGA fallback for mid-bosses in zako stages) |
| OBJ pals 6-7 | skill icons |
| OAM | 0-95 field (peak 43 today), 96-127 panel; effects ≤ 20, petits ≤ 9 each, sparkle ≤ 5 per lucky, drops ≤ 4 × 6; per-line ≤ 34 slivers checked with `oamlines.py` |
| BG3 tiles | 1-64 top, 65-80 HP, 81-86 labels, 87-406 panel columns, **87-502 windows (416)**, 439-462 combo, 484-502 chest row, 896-991 banner, 992-1021 hero label |
| BG1 | 0-60 HUD; bosses 64-287 / 288-511 (A/B); flapping dragons: idle set from 64, one action slot top-aligned at 512 − n |
| HDMA | ch 6/7 today (scroll tables 1/2; wings adds the lift segment to table 1); **ch 4 CGADSUB per line, ch 5 WH0-3 per line** (boss_fx); tables in LOWBSS |
| colour math | battle: only through `cm_req(prio,…)` (4 cut-in, 3 WARNING, 2 effect flash/band/darken, 1 bang tint, 0 aura ring); menus: reset by `mk_screen` / `sys_init` |
| CGRAM upload | full 512 B today; A13 adds a dirty range (64 B for palettes 6-7 when only they changed) for the aura pulse |

### 5.2 WRAM bank $7F in battle

`0000-37FF` BG3 canvas (tiles 0-895; hero pose unpack at `2000`), `3800-3FFF` canvas 896-1023, `4800-5FFF`
zako unpack, `6000-6BFF` shield / misc, `7400-8BFF` king (entrance only; `asset_vram`'s `8000` buffer is used
with the display off only), `A000-BFFF` boss tile unpack, `C000-C8FF` flapping-dragon idle map cache (wings),
`D000-D7FF` boss map unpack, `DA00-DBFF` NMI debug (`DBFA` nmi_vmax), `E000-EFFF` textcheck log,
`F000-FFFF` profiler. Menu screens: `A000-AFFF` unpack (keys, gacha scaler `A000`/`A800`).

### 5.3 Code banks (gate: ≤ 30 KB each after every step; bank $80 ≤ 30 KB; low WRAM ≥ 0.5 KB free)

| bank | files | today | after (est.) |
|---|---|---|---|
| $81 | main, sys, text, ui, sound | 7.5 | 8.5 |
| $82 | title, story, names | 6.9 | 10 |
| $83 | battle_core | 20.3 | 23 |
| $84 | battle_hit | 14.4 | 14.5 |
| $85 | battle_view | 27.2 | ≤ 30 |
| $86 | battle_ui, battle_fx | 0 | 24 |
| $87 | adventure, rewards, treasure | 9.7 | 25 |
| $88 | save, inventory, loadout | 3.5 | 10 |
| $89 | map | 10.0 | 20 |
| $8A | menu | 10.7 | 16 |
| $8B | gacha, chestfx | 3.7 | 18 |
| $8C | forge | 9.0 | 19 |
| $8D | debug | 7.5 | 9 |
| $8E | dex | 12.7 | 15 |
| $8F | reco (+ tables.c, data only) | 0 | 6.5 |
| $90 | equip | 0 | 17.5 |
| $91 | status | 0 | 12 |
| $92 | battle_drop | 0 | 2.5 |

Bank `$80` (STARTUP, ENGINE, CODE / STUBS / RODATA / DATA load images): 21.9 KB → ≈ 26 KB (STUBS 5 B per
non-static function, new asm `snap.s`, `bossmap.s`, math, crt0). Low WRAM: 2,421 B free → ≈ 0.9 KB (STUBS,
HDMA tables). Prefer `static` functions; each exported one costs 5 B of both.

### 5.4 WRAM $7E budget and the overlay arena

Today 47,894 / 55,040 B (7,146 free). Static claims after consolidation ≈ 6.0 KB (save v2 598; boss_fx static
≈ 800; wings ≈ 500 incl. `zdesc` 3×200; chests drops / row ≈ 90; menus ≈ 430; inventory ≈ 240 + `inv_pick` 127;
sortie ≈ 230; forge ≈ 0 static; bgm 17; RODATA growth ≈ 0.4 KB). `ovl[1536]` (sys.c, `ovl.h`) is shared by
**either** the battle (particles 336, BFX recipe buffer 128, intro / rest / aura scalars ≈ 150) **or** the current
menu screen (treasure / gacha state ≈ 1,070, forge evolve rows + guide ≈ 540, STAGE detail lines ≈ 200);
access as `((T *)ovl)->field` (absolute addressing, no runtime cost). Expected use ≈ 5.0 KB → ≈ 2 KB free.
Rule: no arena state survives a screen change; the treasure view starts after `battle_scene` returned.

---------------------------------------------------------------------------------------------------------------

## 6. Implementation steps

Each step is one sitting for one engineer, ends with a green build, its tests, `textcheck.py` clean on the
screens it touches (PROFILE=1 build) and the budget gates of §5.3 / §5.4. Steps in **different lanes** touch
disjoint files except the shared, append-only ones (`make_data.py`, `make_gfx.py`, `adventure.c/h`, `game.h`,
`ui_<lane>.txt` is per lane); within a lane steps are sequential. Lanes may run in parallel worktrees as soon
as their `depends_on` are merged. `map.c` and `dex.c` are touched by several lanes: after E1 the edits are local
(callbacks), rebase before merging.

### Foundation (F)

| id | step | files | depends | tests |
|---|---|---|---|---|
| **S1** | Harness: `runrom poke ADDR VAL` / `pokew`; `build.py` `XDEFS` (test-only `-D` for cc65 + ca65); `dbg_cmd` mailbox in `sys.c` (`#ifdef TEST_HOOKS`, polled in `frame_wait`); `tools/harness/abtest.py` (from shared_infra `regress.py`), `oamlines.py`, `lag.py` (wings) | tools/harness/runrom.c (+binary), build.py, src/c/sys.c, tools/harness/*.py | – | poke `sv.coins` (address from the map) on the map, open the menu: shown; `abtest` base vs base = identical; an `XDEFS=TEST_HOOKS` build answers a ping command (WRAM result byte) |
| **S2** | Shared placer + two-pass link: `tools/romplace.py`, bank attributes on every area, `PLSTUB`, `TABLES` first in `$C0`, `PLxx` last; fonts / strings / estat as blobs (`long` for `f:`-read tables); `gfx_table`, `text_table`, BGM stream segment tables (`bgm_seg_bank/ptr/len`, `bgm_table`) in TABLES; `#<.bank()` rule; build-time grep check of `f:` labels (shared_infra §1, bgm §4.2-4.3) | build.py, tools/romplace.py, tools/gfx/assets.py, tools/make_gfx.py, tools/make_data.py, tools/make_audio.py, src/asm/{sound,text,asset}.s, data/gen (converted, no regeneration) | S1 | `python build.py rom` links; `abtest` shared_infra `t/reg1` (all screens) and `t/reg2` (title from frame 400, battle audio) byte-identical; PROFILE textcheck 0; map: `font_width*`, `estat_index` in `$A0+`; a fake 30 KB `any` and a `long` blob land as specified |
| **S5** | Save v2 + migration (§2) + mksave v2 | src/c/save.h, src/c/save.c, tools/harness/mksave.py | S1 (parallel with S2-S4) | shared_infra §3.6 tests 1-4; + a 999-item v1 save migrates (bag identical at 1228); title shows つづきから with only a v1 slot; build fails when `spare` is set to 7,000 |
| **S3** | One full regeneration from the current web build (`build.py data` + `audio`), reviewed | data/gen/**, src/c fixes for shifted generated ids (`BFX_*` by name) | S2 | `abtest` of every existing harness script old vs new: each differing shot listed and explained by changed web art; textcheck 0; first 1,925,793 B of `bgm.brr` identical; 10 SEs recorded and compared; reviewed diff of gdata.h / tables.c |
| **S4** | ExLoROM option `ROM_MB=4|6|8` (default 4): `EXTnn` areas with bank attribute, zero areas `$81:FFB0` / `$01:FFB0`, `finish_rom()` (bank `$00` copy, both headers `$FFD7=$0D`, mirror checksum, asserts: no CODE/ENG below `$80`, `$00` empty); `romcheck.py`, probe ROMs | build.py, src/asm/header.s (comment), tools/harness/romcheck.py, tools/harness/probe/* | S2 (parallel with S3) | `ROM_MB=8` → 8,388,608 B, snes9x logs `Map_JumboLoROMMap`; `abtest` 4 vs 8 MB on all scripts: shots identical, audio identical after the sample shift; probes PASS on snes9x, FAIL on mercury (expected); PROFILE 8 MB textcheck 0, prof within 1 % |
| **S6** | Text / UI kit: `ui_<lane>.txt` split; `{k}` (code 15) in text.s / ui.h / text.py / textwidth.py; `text_lh` line step (named labels); `ui.c win_close` save-stack compaction; `menukit.h` `mk_confirm3`, `mk_num_spr` behind `#ifdef`; colour-math / window / HDMA-4-5 reset in `mk_screen` + `sys_init`; `name_input(buf,max,title,flags)` + `_hero_name` 18 B + `name_sync` | tools/make_data.py, tools/data/text.py, tools/harness/textwidth.py, src/asm/text.s, src/c/{ui.h,ui.c,menukit.h,story.c,story.h,sys.c}, src/text/ui*.txt | S3 | `abtest` all scripts identical; forge out-of-order close repro (forge spec §1 g7) shows no stale text; `{k}` string drawn via a test command; new-game name entry unchanged (shot); textcheck 0 |
| **S7** | Shared rules: `loadout.c` (bank 8) `sp_reach`, `solo_ok_sk` (+ make_data `build_solo`: `solo_cls`, `solo_tab` as a `long` blob read by an asm accessor, pool / offsets; self-check against the BFS model), `fx_state` + make_data `build_fx_state` (`fx_rule`, FXS_n, FXO_n); `INV_SOLO` in `inv_add`, `inv_solo_all` + `ST_SOLO` on load; math.s `rnd` (moved, `_rnd_state`), `rnd_mul8`, `rnd_below`, `mul_now`, `div_now`; `ovl[1536]` + `ovl.h` | src/c/loadout.{c,h}, src/c/save.c, src/c/sys.c, src/asm/math.s, tools/make_data.py, build.py (C_BANKS) | S5, S6 | Python: `reach_sim.py` (8,500 loadouts) and the 5 emulator loadouts of menus §3 through test commands; `solo_table.py` (120,100) and `solo_ok_sk == (sp_reach(nw=1)≠0)` on 127,550 lists; `fx_state` C vs `fx_state_gen.py` for 86 effects × 4 slots × 6 loadouts; RNG: 500 values equal the old C, battle `abtest` identical; v1 999-item save: `inv_solo_all` ≤ 10 frames and bits = Python |

### Lane battle (sequential; battle_view.c is shared by all of it)

| id | step | files | depends | tests |
|---|---|---|---|---|
| **A1** | Core fixes + battle test commands: tower `form_swap` `++b_stage`; `b_turns_stage` (conflict 15); meat gift cap `D_ITEM_MAX + T_MEAT_GIFT_EXTRA_MAX` (15, was 20); `EV_ENEMY_BANG a` = special next; test commands special-now / clear-wave / set-HP; `FORCE_WAVE`, `FORCE_MIRAGE_WAVE`; money-stage lucky material (conflict 33): `roll_mat` starts with `if (is_money() && foe_lucky(e))` → first of `mat_roll(D_MAT_BELL_LUCKY_PCT)` ベル, `mat_roll(T_MONEY_MAT_SKYOUKA_PCT)` スーパー強化鉄, `mat_roll(100)` 強化鉄, `++b_mats[m]` (cap 250), return | battle_core.c, battle_hit.c, battle.h, battle_view.c | S1, S3 | `FORCE_MIRAGE_WAVE=15`, clear form 0 → WRAM `b_stage` 16 and est[15] stats, after form 2 → ペコラータ (18); meat gifts stop at 15 items; `b_turns_stage` equals turns since the WAVE start (castle 10-5 counts all forms); 魔王城の地下 with test commands, every foe of the 5 WAVEs killed, luckies first (the web autotest `_auto_adv_money`) → Σ `b_mats` ≥ 10 at the victory; a TEST_HOOKS entry point rolling the money-lucky branch 10,000 times → none 0, ベル 300 ± 70, スーパー強化鉄 1,455 ± 141, 強化鉄 8,245 ± 152 (4σ); `abtest` other battles identical (the money stage may differ from its first lucky kill on: its materials, then the RNG) |
| **A2** | `battle_ui.c` (bank 6): modal API (`bv_modal_open/show/close`, 416-tile windows, re-upload hook), pause contents, 状況 4 tabs; results 2 / 3 / 4 in main.c; `place_text` in adventure.c | battle_ui.{c,h}, battle_view.c, main.c, adventure.c, ui_battle.txt, build.py | A1, S6, S7 | sortie §4 (`t/battle.txt`): pause, 4 tabs, paging; one MAIN effect per class + a SUB effect; treasure note; tower buttons; tutorial / training やめる twice → map, no result; textcheck all pages; prof unchanged when closed |
| **A3** | Info window (SELECT held) + tutorial とばす / timing advice (make_data `build_tutorial` TUTX_*) | battle_ui.c, battle_view.c, tools/make_data.py, main.c | A2 | sortie §5 + §7 (`t/battle.txt` end, `t/tut.txt`): enemy / skill + NEXT / hero states; release fires nothing; START → confirm → B back → 飛ばす → map without result; just lesson never gives up, miracle lesson gives up at 5 |
| **A4** | Lucky entry burst + sparkle + first tip (`GDB_LUCKY`) + FX-strip star tiles | battle_view.c, battle_ui.c, tools/make_gfx.py fx() | A3, S5 | sortie §6 (`t/lucky.txt`); second treasure battle: no tip (`--guides 0` then the SRAM bit); money stage 60 s prof lag ≤ today + 0.5 % |
| **A5** | Pose-table extension (no visual change): Actor `off/code[18]`, `pcode[21]`, `bdesc[200]`, `zdesc[3][200]`, `parse_desc` limit, pose codes 7-20 reserved in make_gfx, `fit_n` in `sprite_set` / `boss_set`, dex `npose & 0x7f` | battle_view.c, tools/make_gfx.py, dex.c | A4 | `abtest` every battle and dex script identical; WRAM gate |
| **A6** | `battle_fx.c` engine: `cm_req` (port the special flash and WARNING red onto it), HDMA ch4 / ch5 + crt0 shadows (`_wobjsel`, `_wobjlog`, `_wh[4]`, `_hdma_on` bits 4/5), particle engine in `ovl`, `FxGeo`, hook stubs, shake amplitude | battle_fx.{c,h}, battle_view.c, src/asm/crt0.s | A5 | `abtest` battle scripts identical (flashes unchanged); a test command draws a ch4 band and a ch5 ring (shots); nmi_vmax unchanged |
| **A7** | Boss special effects, 24 kinds (recipes as ROM blobs keyed by name; `BFX_CHAMPAGNE` unused) | battle_fx.c, battle_view.c, tools/make_gfx.py boss_fx(), tools/make_data.py | A6 | boss_fx §3 tests 1-6 with the test commands (all 18 tower bosses, MON_100 / 102, a human mid-boss with zako for GIGA); prof lag ≤ +1 % on fire / bolt / arma / shine; oamlines with an escort |
| **A8** | Hero spin poses for ランページ | tools/make_gfx.py hero(), tools/make_data.py, battle_view.c | A7 | boss_fx §5: キック-only MAIN, third press → spin_front / side / back at +1/+6/+10/+16/+22/+40, landing at +44; plain キック and ジャンプギリ unchanged |
| **A9** | Pekorata idle poses (bored / sit / sleep / pet / stun / wake), guard pose on block, sleeping boss does not guard, L ×10 petting | tools/make_gfx.py (11 poses, `fit_n` 7), battle.h `Foe.asleep`, battle_hit.c `boss_guard`, battle_view.c, battle_fx.c | A8 | boss_fx §2 tests (test command skips boredom time): bored at 600 f, sit 3600, sleep 7200 + Z; Y on a sleeper: damage on the first hit, `miracle_next` 0; L ×9 fast = no pet, 10th = pet; wake at her turn |
| **A10** | Pekorata entrance (PH_INTRO, king walk, talk, WARNING, 4 orbs, spin, flash, mosaic) | battle_core.c, battle.h, battle_view.c, battle_fx.c, tools/make_gfx.py intro_fx() / KING_PAL_FX | A9 | boss_fx §1 tests: shots every 10 f; `b_phase` PH_INTRO until done then PH_FIGHT; exactly one TALK_START; tower WAVE 18 plays it; prof no lag frames in the orbit |
| **A11** | Petit wing flaps (mixed cover, raw strips from ROM, `meta_add` 8×8 pieces, idle program, lift, phases 0/8/16) + adaptive lucky sparkle (conflict 25) | tools/make_gfx.py, src/asm/sprite.s, src/asm/asset.s (`asset_where`), battle_view.c, dex.c | A10, A4 | wings §1 tests 2-4 (`FORCE_WAVE=0,0,6`): 11/3/7/4-frame program, phases, lift 0-2 px; oamlines ≤ 34 slivers and ≤ 32 sprites per line incl. sparkle; money-stage lag within +0.5 %; petit dex cards identical |
| **A12** | Dragon wing flaps (idle sets, body lock, `bossmap.s boss_blit`, idle map cache, action slot, MON_098 volatile wings, HDMA lift) | tools/make_gfx.py, src/asm/bossmap.s, battle_view.c | A11 | wings §1 tests 5-8: flap frames / lifts on MON_089 / 114 / 097, VRAM = T0+T1, lag 0 %, nmi_vmax ≤ 259, no HP-bar leak lines 125-127; MON_098 after attacks; kill blow ≤ 2 frames of normal; form swap to MON_119; Pekorata (non-flapping) unchanged; boss_fx specials still anchored |
| **A13** | Dark aura on MON_114 in 魔王の城 (palette pulse from the final palette, flames, HDMA floor ring) + CGRAM dirty range | battle_fx.c, battle_view.c, src/asm/crt0.s, tools/make_gfx.py | A12 | boss_fx §4 tests: 4 points of the 64-frame cycle, flames on the outline, ring behind the feet; no aura as tower WAVE 1; defeat fades it; DIVE flash wins then the ring returns; works on the flap_down frame; lag ≤ +1 % |
| **A14** | Battle chest row (BG3) + flying drops (`battle_drop.c`, bank 18), coin text minus pending | battle_drop.c, battle_view.c, tools/make_gfx.py (CHEST16 into FX_MISC row 2, CHEST_ROW), tools/make_data.py (POP_UP / POP_X / GLIDE) | A13 | chests §1 tests 1-2 (`XDEFS=FORCE_DROPS`): chests appear after their flight, ×n grows; pause hides / restores the row; OAM ≤ 96; money stage prof ≤ today + 0.5 % |
| **A15** | 蜃気楼の斜塔 中断・続きから: `battle_calm`, asm gather / scatter (`snap.s`, every battle variable classified saved / derived / empty), SRAM records, autosave split over 2 frames, resume without intro / slide / WARNING, pause buttons, mirage window rows + confirm, MS_SUSPEND notice | battle_core.c, src/asm/snap.s, save.c, adventure.c, battle_view.c, battle_ui.c, map.c, main.c | A14, E1, S5 | sortie §3 tests (`t/mirage.txt`): suspend → notice → 続きから → identical WRAM tables; determinism (same pad script twice); power-off mid attack; suspend during the chest choice; resume at WAVE 18 after the entrance (no replay) and with Pekorata asleep (wakes clean); flapping dragon resume; autosave ≤ 40 lines gather, SRAM write in the next frame |
| **A16** | Battle performance + sprite-line pass at 8 MB (stream from SlowROM): prof / lag on money stage, 魔王の城 1 (MON_114 aura + flap + escort + lucky), ペコラータ (sleep + specials), all 18 tower waves; asm particle loop if lag > +1 % | (fixes as found) | A15, H1 | lag ≤ today + 1 % everywhere, nmi_vmax ≤ 259, oamlines ≤ 34 slivers per line (report any drops), no audio dropouts (wav) |

### Lane menus

| id | step | files | depends | tests |
|---|---|---|---|---|
| **B1** | Title 設定 + logo, settings rewrite (`options_scene(from_title)`), BGM / SE volume (`_se_scale`, `sound_volume`, tables), 名前の変更 row (`name_input`), `save_new` keeps settings, driver MVOL 0x78 | src/asm/sound.s, src/c/{sound.c,sound.h,main.c,menu.c,title.c,save.c}, tools/audio/driver.py, data/gen/audio/spc.bin, tools/make_gfx.py title(), ui_menus.txt | S5, S6 | menus §1, §2, §5 tests (RMS ratios, SRAM 108 / 109, title with no save, rename SRAM at 88); peak at BGM 10 / SE 10 over 60 s of the money stage < full scale (else MVOL 0x70); logo shot |
| **B2** | Root menu as a row-id table (11 rows), NEW!! marks, forge unlock (平原の村), shops seen, ショップ opens the newest shop | menu.c, adventure.{c,h}, map.c (row-tag callback, help), forge.c (entry) | B1, E1 | menus §7 tests; v1 save with area_open 5 migrated → no NEW anywhere |
| **B3** | ステータス screen (`status.c`, bank 17): 装備 / 必殺技 / 特殊効果 tabs, HP / guard rule; dex 「今の装備で出せる！」 via `sp_reach`; `specials_scene` removed | status.c, menu.c, dex.c, build.py | B2, S7 | menus §3 tests (HP line == `b_hp_max`, one effect per class, SUB effect, dex marks == status list); textcheck all tabs |
| **B4** | 大切なもの list + orb / badge scene (`key_scene` replaces `orb_scene`), art | tools/make_gfx.py keys(), status.c, story.{c,h}, map.c after_battle, adventure.{c,h}, tools/make_data.py | B3, E1 | menus §4 tests (0 / 2 / 4 orbs, badge, scene shots at 20 / 50 / 110, second Pekorata win no scene, king chibi intact after) |
| **B5** | 討伐記録 (records at the top of `commit_win`, list + cards, root row when beaten) | adventure.c, status.c, menu.c | B4, A1 | menus §6 tests (first / latest, 2 回目, before-records text after debug 地上クリア and after v1 migration, Pekorata) |
| **B6** | Dex count colours (tiers, palette set-up, rainbow tick; `T_DEX_CNT_3M` for G1) | dex.c, ui_menus.txt, mksave `--kills` / `--got` | B3 | menus §8 tests (CGRAM 19 / 23 / 31 on dex vs elsewhere, rainbow over 3 shots) |
| **B7** | Debug: 地上クリア, スキルを変える (calls `inv_solo_update`) | debug.c, adventure.{c,h} | B5, C2, S7 | menus §9 tests (SRAM cleared[] masks, orbs + badge, records visible, ステータス 必殺技 after editing) |

### Lane inventory

| id | step | files | depends | tests |
|---|---|---|---|---|
| **C1** | `inventory.c` core API: `equip_put` (moved), set ops, `inv_remove` renumbers sets, `inv_pick`, `inv_remove_marked`, `inv_new_index`, `sell_picked`, `bulk_pick`; `AdvResult.sold`, `GachaPull.sold` | src/c/inventory.{c,h}, save.c, menu.c, adventure.h, tools/make_data.py (`set_name0`) | S5, S7 | test commands run the ops on crafted saves; WRAM dumps equal a Python model (renumbering, `SET_GONE`, compaction of 999 items ≤ 15 frames, `inv_new_index`) |
| **C2** | `pick_item` refactor: type tabs (L/R), 必殺 filter (Y), 必殺 column, X hook, compare digits (OBJ, `mk_num_spr`), per-screen key lines, START modes; MENU_OBJ digits + palettes 4-6 | menu.c, menukit.h, tools/make_gfx.py menu_obj(), ui_inventory.txt | C1, S6 | inventory §2 tests 1-5 |
| **C3** | そうび詳細 `item_detail` (equip.c, bank 16), 3 pages, `fx_state` + `sp_reach(nw=1)`; X from every pick list; status A → detail | equip.c, menu.c, status.c (if B3 landed), build.py | C2, S7 | inventory §4 tests 1-6 |
| **C4** | セット装備 (sets scene + edit popup, `name_input` names, set warnings in sell / enhance confirms) | equip.c, menu.c (equip_scene 5th row, single sell), forge.c (`do_enhance` confirm line) | C3 | inventory §1 tests 1-7 |
| **C5** | まとめて売る (sell scene) + 持ち物がいっぱい dialog (area / mirage windows) | equip.c, menu.c (items_scene START), map.c | C4, E1 | inventory §3 tests 1-3, 6 (result / gacha bulk is tested in F1 / F2) |
| **C6** | おすすめ (reco.c, bank 15; targets from `plan_block`; THIRD greedy; wait window; reason block) | reco.c, map.c (sortie window), build.py | E2, C1 | inventory §5 tests 1-3, 5-6 (`check_bench.py` same MAIN / SUB / shield on 20 / 150 / 999 items; C time ≤ 250 frames) |
| **C7** | おすすめ asm scan (`reco_scan`) | src/asm/reco.s, reco.c | C6 | same picks as C6 on 200 random inventories; 999 items ≤ 30 frames (PROF_MARK) |

### Lane forge

| id | step | files | depends | tests |
|---|---|---|---|---|
| **D1** | 進化: `evolve_plan` (auto-make lower materials), 進化できる品だけ list, scrolling evolve page, in-page confirm | adventure.{c,h}, menu.c (`pick_evo`), forge.c, tools/make_data.py (FCR_*), ui_forge.txt | C2, S6 | forge §2: host `evoplan.c` vs `cmp_plan.py` 6,000 cases; harness g3 / g4 / g6 / g8 |
| **D2** | 素材 screen: descriptions, where to farm (generated from `mendetail.py`; `mat_odds` with the money-lucky steps of conflict 33; `farm.s` in TABLES), merge window, jump to the STAGE (`MA_FARM`) | forge.c, adventure.c (`mat_farm`), map.c (focus), game.h, menu.c, tools/make_data.py `build_farm` | D1, E2 | forge §3: `farm.py` reproduces the web autotests; with the money-lucky steps (conflict 33, forge §3.1) スーパー強化鉄 and ハイパー強化鉄 (via) point to 魔王城の地下 once the castle is cleared, 強化鉄 / ミラクルベル / ツメ / 剛爪 unchanged, table 26 unique lists / 224 entries / `FARM_LIST_MAX` 7 / 553 B; g5 / g9 / g4 jumps; make_data asserts |
| **D3** | 使い方 guides (強化 11 / 進化 9 steps), auto start, 強化鉄 gift | forge.c, tools/make_data.py `build_guides`, tools/data/guide_snes.py | D2, B2 | forge §1: g1 / g2 / g6 / g7 (`--guides 0 / 1 / 7`), WRAM `sv.guides`, START ends a guide, reopening does not restart |

### Lane map / sortie

| id | step | files | depends | tests |
|---|---|---|---|---|
| **E1** | map.c refactor, no visual change: `open_area()`, `area_window(focus, AW_GO|AW_FARM)`, row-tag callback, extra list keys, sortie button table, `MA_*` codes | map.c, game.h | S3 | `abtest` map / area / sortie / mirage-window scripts identical |
| **E2** | エリア / STAGE の詳細: `tools/data/mendetail.py` (shared web men_detail port), `plan.s` in TABLES + asm `plan_block`, `detail_scene` (X / Y), `dex_card_mon`; the money block's material lines (conflict 33) | tools/data/mendetail.py, tools/make_data.py, src/asm/asset.s, map.c, dex.c, ui_sortie.txt (T_SD_*) | E1, S6 | sortie §2: all 84 blocks + 16 areas vs `plan_expected.txt` (WRAM `dl[]`; regenerated with the two money lines of conflict 33, `T_SD_MONEY_MAT` / `T_SD_MONEY_MATS`, which the prototype's copy lacks), 月見の島 before clear inert, monster card walk, reward card; textcheck on the listed screens |
| **E3** | 再挑戦 (result R, red notes, list cap 5; map returns to the area and opens the sortie) | rewards.c, main.c, map.c, game.h | E1 | sortie §1 (`t/retry.txt`); treasure room with 1 ticket, bag 997, area-opening win, king / loss / 離脱 = no retry |

### Lane shop (chests / gacha)

| id | step | files | depends | tests |
|---|---|---|---|---|
| **F1** | TREASURE view after a win (closed / opening / open / 売却モード / 獲得報酬 with materials by kind), loss / leave chest row; art CHEST32 + palettes, MAT_ICONS, menu particles 27-42 | tools/make_gfx.py chests(), src/c/chestfx.{c,h} (bank 11), src/c/treasure.c (bank 7), rewards.c, ui_shop.txt, build.py | C3, C5, E3 | chests §1 tests 3-7 (treasure STAGE win sequence, X detail + identical redraw, N一括 confirm, gold page == `b_mats`, 999-bag コインに, R → RES_RETRY) |
| **F2** | Gacha presentation (summon circle, chest drop / shakes / rank-up / open, single card, 10 boxes with teasers and promotions, list + sell page), confirm, もう1回 / もう10連, 持ち物を売る row | gacha.c, chestfx.c, tools/make_gfx.py (GACHA_CIRCLE) | F1 | chests §2 tests 1-5 |
| **F3** | 必殺10連 (pool, 30 / 30 / 25 / 15, skills until `solo_ok_sk` and not kick-only, ≤ 4000 tries), shop panel, ticket from ペコラータ (rule per §7 Q1; default every win, max 99, gold-page line), debug 99 | adventure.{c,h}, gacha.c, debug.c | F2, S7 | chests §3 tests 1-5 (≤ 13 / 30 frames per ticket, 500-ticket statistics, ticket counts, panel only with tickets) |

### Lane dex cards

| id | step | files | depends | tests |
|---|---|---|---|---|
| **G1** | Dex mirage cards (18 boss_set pictures, award in `commit_mirage`, result line 蜃気楼カード +n, A toggle, purple backdrop / frame, 蜃 mark, rules text) | tools/make_gfx.py mirage_cards(), tools/make_data.py (`mirage_mon`), dex.c, rewards.c, adventure.c, ui_dex.txt | B6, F1 | wings §2 tests 1-5 (+ the tier-3 count on a mirage card is yellow, conflict 26). Cards are awarded only inside `commit_mirage`; A15 (whichever lands second) keeps that call and its tests (a lost run after a suspend / はじめから still awards the beaten WAVEs) |

### Lane BGM

| id | step | files | depends | tests |
|---|---|---|---|---|
| **H1** | 20 new surface tracks with the bar-aware finder (bars-38; 20 / 21 composer loops), stream manifest; **default `ROM_MB=8`** | tools/make_audio.py, data/gen/audio/*, build.py | S3, S4 | bgm §5: first 1,925,793 B identical; joins rhythm lag ≤ 12 ms; listen to `joins/*_bar38.wav`; 8 MB links with ≥ 300 KB free; all scripts `abtest` vs the S4 8 MB build: screens identical |
| **H2** | BGM routing (`mon_song`, `area_song` with BGM_SWAP; `battle_bgm()`; dex card song; ending BGM_ENDING; enum generated into audio.h) | tools/make_data.py, src/c/{sound.h,battle_core.c,dex.c,story.c,gtypes.h} | H1 | bgm §5 routing tests (`wavref.py` corr ≥ 0.99 for the listed areas / waves / dex cards; unknown card plays OPENING; money stage BGM_ZAKO_LATE) |

### Final

| id | step | files | depends | tests |
|---|---|---|---|---|
| **Z1** | Integration regression at 8 MB: every lane's scripts, textcheck sweep of every new screen, SRAM migration from a real v1 `.srm`, save write time, WRAM / bank report | (fixes as found) | all steps | all group test plans green; budgets of §5 hold; spare ROM reported |
| **Z2** | README: contents table, controls, ROM line「ExLoROM / FastROM / 8MB / SRAM 32KB」, compatibility list, save v2 note (copy the .srm before switching ROMs), not-included list | README.md | Z1 | review |

Order of the critical path: S1 → S2 → S3 → S6 → S7 → (A1 … A16) → Z1. Everything else hangs off it in
parallel lanes: S5 with S2; S4 with S3; H1 right after S4; E1 after S3; B / C / D / F / G lanes after S7.

---------------------------------------------------------------------------------------------------------------

## 7. Questions for the user

1. 「必殺10連武器チケット」は、ペコラータに勝つたびに 1 枚（最大 99 枚）にしますか、初めて勝ったときだけ 1 枚にしますか？
   （初回だけだと、このガチャは 1 回しか引けません。決まるまでは「勝つたびに 1 枚」で作ります）
2. ROM は 8MB（ExLoROM）にします。お使いの snes9x は PC 版 1.54 以降か RetroArch の snes9x コアですか？
   Wii・ゲームキューブ版や 1.53 以前は 6MB までなので、その場合は 6MB にして新しい曲のループが平均約 17 秒
   （8MB なら約 31 秒）になります。
3. Mesen2 や Super EverDrive でも遊ぶ予定はありますか？ あれば ExHiROM という別の形にします（作り直しが増え、
   EverDrive 向けの 7.5MB ではループが少し短くなります）。なければ 8MB ExLoROM のままにします。

---------------------------------------------------------------------------------------------------------------

## 8. Risks to watch

- WRAM `$7E` and bank `$80`: report both after every step; big tables must be ROM blobs.
- A blob read with `f:` but not marked `long` breaks only when it lands in `$81-$9F` (grep check in S2).
- Battle CPU / sprite lines: A16 measures everything together; the money stage is the reference.
- The mirage snapshot must classify every battle variable added by A1-A14 (A15 is last on purpose).
- 255 stream segments: 180 at full load.
- Regeneration pulls in web changes: S3 is the only full regeneration; later steps regenerate their own outputs.
  A new web build can also change **rules** in the scripts (not in the data): check the shipped scripts' diff
  for surface rules and give each one an owner (S3 found one: conflict 33).
- Downgrading a save to an older ROM loses it (README note).

## 9. Group specs (full per-item detail: web rules, strings, coordinates, measurements)

- menus / settings / records: `scratchpad/plan/menus_settings/spec.md`
- inventory: `scratchpad/plan/inventory/spec.md`
- forge: `scratchpad/plan/forge/spec.md`
- sortie / battle UI: `scratchpad/plan/sortie_battle/spec.md` (rename its ids per conflict 9)
- chests / gacha: `scratchpad/plan/chests_gacha/spec.md`
- boss effects: `scratchpad/plan/boss_fx/spec.md` (replace its SELECT hooks per conflict 3)
- wings / mirage cards: `scratchpad/plan/wings_mirage/spec.md`
- BGM: `scratchpad/plan/bgm/spec.md` (packer replaced by romplace, conflict 27)
- ROM compatibility: `scratchpad/plan/rom_compat/spec.md` (banks.py replaced by romplace)
- shared infrastructure (placer, text, save v2, menu): `scratchpad/plan/shared_infra/spec.md`

(`scratchpad` = `/tmp/claude-0/-home-user-kishidora-web/bfba2c3a-727d-51a5-9268-545e956e9958/scratchpad`.)
