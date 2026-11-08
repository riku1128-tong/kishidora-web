# Surface BGM (group `bgm`): implementation spec

Status: designed **and prototyped** in a work copy. The prototype is an 8 MB ExLoROM with all 28 surface tracks, the new
stream tables and the new BGM selection. It plays correctly on snes9x: title, dex cards and a money-stage battle were
checked against the decoded BRR stream. The prototype is a patch, not a finished implementation. The main repo was not
touched.

All paths below are relative to `/home/user/kishidora-web/kishidra_sfc` unless they are absolute.
Scratch: `/tmp/claude-0/-home-user-kishidora-web/bfba2c3a-727d-51a5-9268-545e956e9958/scratchpad/plan/bgm/`
(= `$P` below).

| file in `$P` | what |
|---|---|
| `proto.patch` | the prototype diff (src/, build.py, tools/make_audio.py, new tools/pack_bgm.py, tools/song_tables.py) |
| `work/` | the work copy the patch was built in (8 MB ROM in `work/build/kishidra.sfc` is a PROFILE=1 build) |
| `shipped/*.gd` | the **shipped** web scripts, decoded from index.pck with `webbuild.gdscript_source` (see section 0) |
| `songtable.txt` | the web rules evaluated for every area/men/stage, mirage wave and dex monster (`scripts/songtable.py`) |
| `measure.json`, `grid11008.json`, `grid8000.json` | measured loop sizes (make_audio.short_loop) at many windows |
| `loopq2.json`, `barloop2.json` | musical checks of the current loop finder, and of the proposed bar-aware loop finder |
| `joins/*.wav` | 8 s listening snippets around every loop join (`_cur34` = today's finder, `_bar38` = proposed) |
| `scripts/` | all measurement / check scripts (measure.py, measure2.py, loopq2.py, barloop.py, wavref.py, wavloop.py, budget.py, bankfree.py, joins.py) |
| `run/` | harness scripts and recordings used for the tests |

---------------------------------------------------------------------------------------------------------------

## 0. Facts that change the plan (read first)

1. **The decompiled sources in `scratchpad/src/*.gd` are older than the shipped web build.** The shipped `index.pck`
   (read by `tools/extract/webbuild.py`) differs in `tuning.gd` (58 diff lines), `battle_core.gd`, `map.gd`, etc.
   For BGM the difference matters: the shipped `Tuning.BOSS_BGM` has `"MON_114": 8, "MON_089": 17` (the scratch src
   says `"MON_114": 17` and has no MON_089). So 龍騎士マーゴス (アーケディア城, castle floor 1) plays `08_boss_field`
   and 翼龍ドラゴス (平原の村, castle floor 2) plays `17_boss_margos`. The shipped map.gd autotest also expects this:
   `want = { "MON_114": 8, "MON_089": 17, "MON_101": 13, ...}`. Everything below uses the shipped build. I put all the
   shipped scripts in `$P/shipped/`, and other groups should check their rules there too (tuning.gd also gained
   `SPIN_POSES`/`SPIN_FRAMES` for ランページ, and a `CHAMPAGNE` boss FX).
2. **Regenerating data from today's web build changes more than BGM.** `python build.py data` on the current web
   build changes `gdata.h` (new tuning constants, and `N_BOSSFX` 23→24 because `BFX_CHAMPAGNE` is inserted, which
   shifts every BFX id after it), `tables.c` and many `gfx/*.bin` files (e.g. the boss_007 art). Running `make_audio.py`
   also changes `spc.bin`: 3306 bytes of SE sample data are different, while the SE tables are the same. The prototype
   kept the old `spc.bin` and the old data/gen and added only the BGM. A full data regen should be its own step.
3. **Today's loop finder often makes musically wrong loops.** `make_audio.short_loop` matches 0.28 s of spectrum and
   ignores bars. At the 34 s window, about half of the 24 loop tracks join with a beat skip. Examples: 08
   lag −81 ms, 13 lag −105 ms, 09 +116 ms, 26 +140 ms, 22 −105 ms, 10 +70 ms, 15 +58 ms. 08 and 13 are already in
   the shipped port. A bar-aware finder (section 2.3) gives a rhythm-continuous join (lag 0–12 ms, one onset frame)
   for all 20 new tracks.
4. **The relayed user request differs from the computed task on the loops.**
   - The user said **ダメです** to making 16_boss_secret *and the opening* loop-only. So **01_opening stays `('full',)`
     exactly as now**, even though the computed task lists it among the tracks that may be re-cut.
   - The phrase "短いループでいいです" is not in the relayed request. So I do not shorten the 4 existing loop tracks
     (02/08/13/50) without the user's OK. The 8 MB plan does not need it: the new tracks get loops as long as the
     existing ones or longer.
   - 07_worldmap: the user said 不要. The map keeps 01_opening.
5. **4 MB is not possible.** It would need loops of about 6 s, or 8 kHz with about 8 s loops (section 3). A ROM over
   4 MB is needed. **8 MB ExLoROM** is recommended: it runs in snes9x (verified here), current bsnes and FXPak Pro
   (firmware ≥ v1.8.0). It does **not** run on Mesen2 or on old bsnes forks (bsnes_mercury shows a black screen,
   verified). Everdrive is unverified.
6. **Existing bug, not caused by this work:** while the SE samples upload at boot (the first ~3 s of the title), the BGM
   stream has short glitches. In the current repo ROM, windows at 1.20–1.25 s and 1.60–1.65 s do not match the decoded
   stream (corr 0.27–0.83). The 4 MB build with the new tables has one at 1.95 s, the 8 MB build has a few between
   1.7 and 3.1 s. From 3.2 s on there are none in any build. Their position changes with timing, so this is a race in
   the upload/stream hand-off (sound.s `stream_idle`/`refresh`, driver burst loop). It is worth its own fix.

---------------------------------------------------------------------------------------------------------------

## 1. Web rules: which track plays where (shipped build)

### 1.1 Sources

- `battle_core.gd` `stage_bgm()` (line 1648 in `$P/shipped/battle_core.gd`), `stage_boss_kind()` (1427),
  `stage_count_for()` (396), `is_castle/is_money/is_secret/is_treasure`.
- `battle.gd` `_spawn_stage()` → `_play_bgm(_stage_bgm(...))` (1267), victory `_play_bgm("05_victory", false)` (2430),
  game over `_play_bgm("06_gameover", false)` (2469). `_play_bgm` returns early when the name is the same, so the song
  does not restart between waves.
- `dex_data.gd` `monster_bgm()` (388); `dex_screens.gd` `_open_card` emits `bgm_requested(no)` for known monsters,
  and -1 on close. `map.gd` `_dex_bgm` saves the map song position and resumes it.
- `map.gd` `_king_return()` (1779) → `_play_bgm(Tuning.ENDING_BGM)` for 王さま talk + EPILOGUE + CREDITS, then title.
- `title.gd` → `BGM_TITLE` = 1. `map.gd` `_map_bgm()` → `BGM_MAP` = 7 (not used on SFC: the map keeps 01).
- `game_data.gd` loads `assets/bgm/bgm.json` (1–16) + `bgm_phone.json` (17–72) into `bgm_files`, then applies
  `Tuning.BGM_SWAP = {14: 15, 15: 14}`. So id 14 (`BGM_ZAKO_LATE`) plays file `15_zako_castle`, and id 15
  (`BGM_ZAKO_CASTLE`) plays file `14_zako_late`.

### 1.2 Constants (params.json `defines` + shipped tuning.gd)

```
BGM_TITLE 1  BGM_MAP 7  BGM_BOSS 4 (mid-boss = 04_dragon)  BGM_VICTORY 5  BGM_GAMEOVER 6  BGM_KING 1
BGM_ZAKO_EARLY 2  BGM_ZAKO_MID 3  BGM_ZAKO_LATE 14  BGM_ZAKO_CASTLE 15  BGM_B_SECRET 16
AREA_BOSS_BGM = [8, 8, 9, 8, 10, 10, 10, 8, 8, 8, 11, 11, 9, 12, 13, 16]   (index = AREA_TABLE order)
Tuning.BOSS_BGM (surface) = { MON_114: 8, MON_089: 17, MON_096: 18, MON_091: 19, MON_115: 20, MON_099: 21,
                              MON_092: 22, MON_093: 23, MON_094: 24, MON_113: 25, MON_102: 26, MON_119: 50 }
Tuning.CASTLE_LAST_WAVES_BGM = 49   Tuning.ENDING_BGM = 27   Tuning.BGM_DB = -3.0
CASTLE_BOSSES = [MON_114, MON_089, MON_095, MON_096, MON_097, MON_092, MON_093, MON_091, MON_102]
CASTLE_FINAL  = [MON_113, MON_094, MON_099, MON_100, MON_101]  MAOU_FORMS = [MON_101, MON_118, MON_119]
TREASURE_GUARD MON_115  TREASURE_MEN 6  TREASURE_STAGES 3  TREASURE_TABLE areas: AR_ISLET, AR_ISLAND
Tuning.MONEY_MEN 20  MONEY_STAGES 5  MONEY_NAME "魔王城の地下"   CASTLE_MEN 10  CASTLE_STAGES 5
AREA_MEN 5  MEN_STAGES 3  SECRET_MEN 1  SECRET_STAGES 1  AWAKEN_MON MON_117  TUTORIAL_AREA AR_ARCADIA  TUTORIAL_MON MON_116
```

### 1.3 stage_bgm (surface part, exact order)

```
if mirage: evaluate stage_bgm() as the wave's home stage (entry home area, men 5, its last stage; forms: castle men 10 last stage; peko: secret 1/1)
kind = stage_boss_kind():   0 if stage != stage_count and not (castle and men == 10)
                             2 if castle or secret or men == AREA_MEN(5) or treasure or money
                             1 if men == AREA_MEN-1 (4) else 0
kind 0                    -> area["bgm"] (via bgm_files after BGM_SWAP)
treasure or money         -> BOSS_BGM[guard = MON_115] = 20
kind 1                    -> BGM_BOSS = 4 (04_dragon)
castle, men 1..9          -> BOSS_BGM[CASTLE_BOSSES[men-1]], else AREA_BOSS_BGM[first area whose boss is it]
castle, men 10, stage < 5 -> 49 (CASTLE_LAST_WAVES_BGM)
castle, men 10, stage 5   -> the MAOU_FORMS member on the field: MON_101/MON_118 -> AREA_BOSS_BGM[castle] = 13, MON_119 -> 50
secret                    -> AREA_BOSS_BGM[secret] = 16
else (area boss)          -> BOSS_BGM[area boss] else AREA_BOSS_BGM[area index]
tutorial / training: area = TUTORIAL_AREA, men 0 -> kind 0 -> 02
```

### 1.4 Full table: every surface area / men / stage

The file name is what plays (after the swap). Full listing: `$P/songtable.txt`.

| area | men 1–3 (all stages) | men 4 (st 1–2 / st 3) | men 5 (st 1–2 / st 3 boss) | men 6 宝の部屋 (st 1–2 / 3) |
|---|---|---|---|---|
| アーケディア城 | 02 | 02 / 04 (MON_107) | 02 / **08** (MON_114 龍騎士マーゴス) | – |
| 平原の村 | 02 | 02 / 04 | 02 / **17** (MON_089 翼龍ドラゴス) | – |
| いにしえの洞窟 | 02 | 02 / 04 | 02 / 09 (MON_095) | – |
| 港町ポトス | 02 | 02 / 04 | 02 / 18 (MON_096) | – |
| 砂漠の遺跡群 | 02 | 02 / 04 | 02 / 10 (MON_097) | – |
| 砂漠のオアシス | 03 | 03 / 04 | 03 / 22 (MON_092) | – |
| 砂漠の宮殿 | 03 | 03 / 04 | 03 / 23 (MON_093) | – |
| 小島 | 03 | 03 / 04 | 03 / 19 (MON_091) | 03 / 20 (ミミッくん) |
| 漁師の里 | 03 | 03 / 04 | 03 / 20 (MON_115 ミミッくん) | – |
| 孤島 | 03 | 03 / 04 | 03 / 21 (MON_099) | 03 / 20 (ミミッくん) |
| 氷の洞窟 | 15 | 15 / 04 | 15 / 11 (MON_098) | – |
| 氷の宮殿 | 15 | 15 / 04 | 15 / 24 (MON_094) | – |
| 深淵の観測所 | 15 | 15 / 04 | 15 / 25 (MON_113) | – |
| 灼熱の洞窟 | 15 | 15 / 04 | 15 / 12 (MON_090) | – |

(“15” = file `15_zako_castle`, which is BGM id 14 after the swap. The tutorial and training (カカシ, only in
アーケディア城) play 02.)

| 魔王の城 floor | stages 1–4 | stage 5 |
|---|---|---|
| 1 | 14 | 08 (MON_114) |
| 2 | 14 | 17 (MON_089) |
| 3 | 14 | 09 (MON_095) |
| 4 | 14 | 18 (MON_096) |
| 5 | 14 | 10 (MON_097) |
| 6 | 14 | 22 (MON_092) |
| 7 | 14 | 23 (MON_093) |
| 8 | 14 | 19 (MON_091) |
| 9 | 14 | 26 (MON_102 魔龍ドットアーク) |
| 10 | 49 (bosses MON_113, MON_094, MON_099, MON_100 in waves 1–4) | 13 (魔王, 魔王第2形態) → 50 (魔王最終形態) |
| 魔王城の地下 (men 20) | 14 | 20 (ミミッくん) |
| 月見の島 | – | 16 (one stage, ペコラータ; the song keeps its full intro + loop) |

(“14” = file `14_zako_late`, which is BGM id 15 after the swap.) On the form change the web plays
`["49", "13", "13", "50"]` (autotest `_auto_maou`): form 2 keeps 13 playing, the final form switches to 50.

**蜃気楼の斜塔** (18 waves; each wave plays the song of its home stage, which equals the dex rule for that boss):
MON_114→08, MON_089→17, MON_095→09, MON_096→18, MON_097→10, MON_092→22, MON_093→23, MON_091→19, MON_115→20,
MON_099→21, MON_098→11, MON_094→24, MON_113→25, MON_090→12, MON_101→13, MON_118→13, MON_119→50, MON_117→16.

**Other scenes:** title 01; map and all menus 01 (instead of the web's 07); victory 05 once; game over 06 once;
王さまの帰還 talk + エピローグ + クレジット 27, then the title (01).

### 1.5 Dex card song (DexData.monster_bgm, the shipped order of rules)

1. `Tuning.BOSS_BGM[m]` if present.
2. m in MAOU_FORMS → AREA_BOSS_BGM[castle] = 13.
3. m in CASTLE_FINAL → 49 (this only reaches MON_100 魔王のペット; the others hit rules 1 or 2 first).
4. m == AWAKEN_MON → AREA_BOSS_BGM[secret] = 16.
5. First area whose boss is m → AREA_BOSS_BGM[i].
6. Otherwise the zako song of the area where m has the largest share of the zako pool. The share is
   pool.count(m)/pool.size, the strictly greatest wins, so on a tie the earlier area wins.
7. Mid-boss (`areas[].mid`) → BGM_BOSS 4.
8. EK_LUCKY → areas[0] bgm = 02.
9. TUTORIAL_MON → the tutorial area song = 02.
10. Otherwise -1: no change, the map song keeps playing.

Result (`$P/tableDex.md`): 02 ×28, 03 ×23, file 15_zako_castle ×14 (includes MON_107 王宮騎士オージーン and MON_112, mid-bosses that
also sit in a late zako pool), file 14_zako_late ×3 (MON_071, MON_109, MON_111), 04 ×5 (MON_063, 064, 076, 078, 080),
one boss song each for MON_089..MON_102/113–119 as in 1.4, MON_100 → 49, カカシ → -1. In the SFC an unknown card or -1 plays
BGM_OPENING (= the map song, as now). The web resumes the map song at its saved position, while the SFC restarts the
opening when you go back to the list. That is unchanged.

---------------------------------------------------------------------------------------------------------------

## 2. Conversion: measured sizes and loop quality

### 2.1 Method

I ran make_audio in the work copy: ffmpeg → mono 11008 Hz (and 8000 Hz), normalised to 0.85, start = the Safari
`loop_offset` rounded to 16 samples, `short_loop(a, start, W)`. BRR bytes are ceil(n/16)·9.

I validated this: the current 02/08/13/50 come out at exactly the port's block counts (163377/188757/169686/209187 B), and
the full 28-track prototype stream starts byte-identical to today's `bgm.brr` (first 1,925,793 B). Rates: 11008 Hz =
6192 B/s, 8000 Hz = 4500 B/s. Converting all 28 tracks takes 43 s.

### 2.2 Today's finder (`short_loop`), all surface tracks

Each cell is the loop length, the loop start and the bytes. The last column is the rhythm check at the 34 s join:

- bars = loop length / bar.
- lag = offset between the onset pattern after the join and the one that originally followed. 0 means the beat goes on.
- r = their correlation.

| track | Safari loop at / body | 34 s | 24 s | 20 s | 16 s | 12 s | 8k 20 s | 8k 16 s | rhythm at the 34 s join (current finder) |
|---|---|---|---|---|---|---|---|---|---|
| 01_opening | 16.0 s / 40.0 s | 31.5 s from 16.0 / 195039 B | 23.8 s from 16.0 / 147600 B | 19.0 s from 16.0 / 117648 B | 13.2 s from 16.0 / 82044 B | 10.0 s from 16.0 / 61920 B | 19.0 s from 16.0 / 85500 B | 12.8 s from 16.0 / 57375 B | 15.75 bars, lag +0 ms, r 0.84 |
| 02_zako_early | 56.7 s / 102.4 s | 26.4 s from 56.7 / 163377 B | 22.8 s from 56.7 / 141273 B | 15.5 s from 56.7 / 96165 B | 12.0 s from 56.7 / 74304 B | 11.6 s from 56.7 / 71874 B | 15.3 s from 56.7 / 68706 B | 13.6 s from 56.7 / 60993 B | 8.25 bars, lag +12 ms, r 0.49 |
| 03_zako_mid | 64.9 s / 76.8 s | 32.0 s from 64.9 / 198135 B | 22.4 s from 64.9 / 138600 B | 15.9 s from 64.9 / 98568 B | 12.8 s from 64.9 / 79146 B | 9.0 s from 64.9 / 55836 B | 16.0 s from 64.9 / 72009 B | 12.8 s from 64.9 / 57600 B | 10.00 bars, lag +0 ms, r 0.45 |
| 04_dragon | 84.9 s / 67.2 s | 25.6 s from 84.9 / 158499 B | 18.3 s from 84.9 / 113139 B | 16.0 s from 84.9 / 98883 B | 14.5 s from 84.9 / 89775 B | 9.4 s from 84.9 / 58203 B | 15.9 s from 84.9 / 71793 B | 14.5 s from 84.9 / 65241 B | 8.00 bars, lag -12 ms, r 0.54 |
| 08_boss_field | 37.4 s / 64.0 s | 30.5 s from 37.4 / 188757 B | 20.6 s from 37.4 / 127476 B | 15.7 s from 37.4 / 97281 B | 14.4 s from 37.4 / 89163 B | 10.0 s from 37.4 / 61821 B | 15.8 s from 37.4 / 71109 B | 14.4 s from 37.4 / 64791 B | 9.53 bars, lag -81 ms, r -0.04 |
| 09_boss_cave | 34.6 s / 64.8 s | 27.8 s from 34.6 / 171828 B | 22.9 s from 34.6 / 141507 B | 15.3 s from 34.6 / 94482 B | 12.2 s from 34.6 / 75321 B | 11.4 s from 34.6 / 70893 B | 15.3 s from 34.6 / 68706 B | 12.2 s from 34.6 / 54738 B | 14.57 bars, lag +116 ms, r 0.26 |
| 10_boss_desert | 32.0 s / 76.8 s | 28.5 s from 32.0 / 176625 B | 22.4 s from 32.0 / 138555 B | 19.8 s from 32.0 / 122598 B | 13.8 s from 32.0 / 85473 B | 9.6 s from 32.0 / 59310 B | 19.7 s from 32.0 / 88731 B | 13.0 s from 32.0 / 58500 B | 8.91 bars, lag +70 ms, r -0.14 |
| 11_boss_ice | 34.0 s / 64.1 s | 25.5 s from 34.0 / 157608 B | 18.7 s from 34.0 / 115758 B | 18.7 s from 34.0 / 115758 B | 12.2 s from 34.0 / 75654 B | 11.9 s from 34.0 / 73863 B | 18.6 s from 34.0 / 83529 B | 12.1 s from 34.0 / 54513 B | 7.94 bars, lag +46 ms, r -0.24 |
| 12_boss_volcano | 32.9 s / 64.0 s | 27.2 s from 32.9 / 168453 B | 23.5 s from 32.9 / 145674 B | 19.9 s from 32.9 / 123417 B | 12.0 s from 32.9 / 74241 B | 11.7 s from 32.9 / 72495 B | 19.9 s from 32.9 / 89685 B | 12.0 s from 32.9 / 53955 B | 8.50 bars, lag -12 ms, r 0.34 |
| 13_boss_last | 86.0 s / 63.3 s | 27.4 s from 86.0 / 169686 B | 23.6 s from 86.0 / 146457 B | 15.0 s from 86.0 / 93024 B | 12.4 s from 86.0 / 76572 B | 9.6 s from 86.0 / 59589 B | 15.0 s from 86.0 / 67473 B | 12.3 s from 86.0 / 55557 B | 14.28 bars, lag -105 ms, r -0.01 |
| 14_zako_late | 11.9 s / 153.6 s | 27.2 s from 11.9 / 168201 B | 18.4 s from 11.9 / 113985 B | 18.4 s from 11.9 / 113985 B | 15.8 s from 11.9 / 97893 B | 9.1 s from 11.9 / 56421 B | 18.3 s from 11.9 / 82548 B | 12.8 s from 11.9 / 57438 B | 8.49 bars, lag +46 ms, r -0.13 |
| 15_zako_castle | 69.6 s / 47.1 s | 28.1 s from 69.6 / 174033 B | 19.1 s from 69.6 / 118242 B | 15.0 s from 69.6 / 92916 B | 14.6 s from 69.6 / 90234 B | 11.6 s from 69.6 / 71703 B | 19.0 s from 69.6 / 85743 B | 14.5 s from 69.6 / 65376 B | 9.54 bars, lag +58 ms, r -0.14 |
| 16_boss_secret | 64.7 s / 51.2 s | 26.8 s from 64.7 / 165735 B | 21.1 s from 64.7 / 130671 B | 17.1 s from 64.7 / 106074 B | 12.8 s from 64.7 / 79416 B | 10.8 s from 64.7 / 67167 B | 17.4 s from 64.7 / 78372 B | 13.4 s from 64.7 / 60372 B | (kept full: 717876 B) |
| 17_boss_margos | 55.3 s / 77.0 s | 30.8 s from 55.3 / 190692 B | 21.8 s from 55.3 / 135009 B | 15.0 s from 55.3 / 92691 B | 12.8 s from 55.3 / 79407 B | 10.0 s from 55.3 / 62154 B | 16.5 s from 55.3 / 74268 B | 12.8 s from 55.3 / 57753 B | 18.79 bars, lag +23 ms, r 0.13 |
| 18_boss_oshalsias | 39.1 s / 53.3 s | 34.0 s from 39.1 / 210267 B | 23.1 s from 39.1 / 143100 B | 17.2 s from 39.1 / 106605 B | 14.3 s from 39.1 / 88281 B | 11.0 s from 39.1 / 68229 B | 17.2 s from 39.1 / 77472 B | 14.4 s from 39.1 / 64683 B | 20.37 bars, lag -12 ms, r 0.63 |
| 19_boss_picharus | 7.4 s / 51.2 s | 28.8 s from 7.4 / 178326 B | 18.4 s from 7.4 / 113760 B | 18.4 s from 7.4 / 113760 B | 14.6 s from 7.4 / 90405 B | 11.2 s from 7.4 / 69165 B | 18.4 s from 7.4 / 82800 B | 14.6 s from 7.4 / 65700 B | 9.00 bars, lag +12 ms, r 0.65 |
| 20_boss_mimikkun | 15.0 s / 43.6 s | 26.5 s from 15.0 / 163998 B | 22.6 s from 15.0 / 139860 B | 18.9 s from 15.0 / 116865 B | 13.2 s from 15.0 / 82026 B | 10.1 s from 15.0 / 62586 B | 18.9 s from 15.0 / 84933 B | 13.3 s from 15.0 / 59724 B | 8.50 bars, lag +12 ms, r 0.41 |
| 21_boss_nazokimos | 47.0 s / 38.4 s | 29.2 s from 47.0 / 180774 B | 20.4 s from 47.0 / 126063 B | 17.4 s from 47.0 / 107712 B | 13.4 s from 47.0 / 83205 B | 11.8 s from 47.0 / 72882 B | 18.0 s from 47.0 / 81036 B | 13.3 s from 47.0 / 59985 B | 9.12 bars, lag +0 ms, r 0.51 |
| 22_boss_washaras | 2.0 s / 59.2 s | 30.5 s from 2.0 / 188910 B | 20.2 s from 2.0 / 125271 B | 18.0 s from 2.0 / 111537 B | 14.6 s from 2.0 / 90459 B | 10.4 s from 2.0 / 64467 B | 19.9 s from 2.0 / 89784 B | 14.6 s from 2.0 / 65808 B | 9.28 bars, lag -105 ms, r 0.01 |
| 23_boss_kiranos | 38.7 s / 102.4 s | 33.4 s from 38.7 / 206595 B | 18.6 s from 38.7 / 115164 B | 16.0 s from 38.7 / 99018 B | 12.8 s from 38.7 / 79236 B | 11.9 s from 38.7 / 73710 B | 16.0 s from 38.7 / 71991 B | 12.8 s from 38.7 / 57402 B | 10.43 bars, lag +23 ms, r 0.03 |
| 24_boss_yamijas | 56.9 s / 51.2 s | 27.8 s from 56.9 / 172440 B | 21.4 s from 56.9 / 132507 B | 15.0 s from 56.9 / 93060 B | 15.0 s from 56.9 / 93060 B | 9.0 s from 56.9 / 55719 B | 15.2 s from 56.9 / 68391 B | 15.2 s from 56.9 / 68391 B | 8.70 bars, lag -35 ms, r -0.08 |
| 25_boss_darkness | 42.1 s / 58.2 s | 33.5 s from 42.1 / 207612 B | 18.1 s from 42.1 / 112383 B | 18.1 s from 42.1 / 112383 B | 13.1 s from 42.1 / 81288 B | 9.6 s from 42.1 / 59130 B | 17.7 s from 42.1 / 79614 B | 12.7 s from 42.1 / 57339 B | 12.67 bars, lag +0 ms, r 0.46 |
| 26_boss_dotark | 59.0 s / 48.0 s | 30.6 s from 59.0 / 189576 B | 22.5 s from 59.0 / 139347 B | 16.5 s from 59.0 / 102105 B | 14.2 s from 59.0 / 88263 B | 10.9 s from 59.0 / 67716 B | 16.5 s from 59.0 / 74205 B | 14.2 s from 59.0 / 64125 B | 10.21 bars, lag +140 ms, r -0.05 |
| 27_ending | 5.1 s / 141.3 s | 32.0 s from 5.1 / 198189 B | 18.7 s from 5.1 / 115533 B | 17.0 s from 5.1 / 105255 B | 12.8 s from 5.1 / 78984 B | 10.6 s from 5.1 / 65970 B | 17.0 s from 5.1 / 76581 B | 12.9 s from 5.1 / 57933 B | 12.00 bars, lag +0 ms, r 0.80 |
| 49_castle_last_waves | 2.9 s / 46.2 s | 29.0 s from 2.9 / 179334 B | 20.9 s from 2.9 / 129681 B | 15.8 s from 2.9 / 97821 B | 14.7 s from 2.9 / 91017 B | 11.1 s from 2.9 / 68814 B | 15.8 s from 2.9 / 71100 B | 14.6 s from 2.9 / 65466 B | 18.18 bars, lag +35 ms, r -0.17 |
| 50_boss_last_final | 75.3 s / 76.8 s | 33.8 s from 75.3 / 209187 B | 21.4 s from 75.3 / 132237 B | 17.6 s from 75.3 / 108873 B | 14.0 s from 75.3 / 86724 B | 9.8 s from 75.3 / 60588 B | 17.5 s from 75.3 / 78858 B | 14.0 s from 75.3 / 62928 B | 10.56 bars, lag +12 ms, r 0.17 |

Totals for the 24 loop tracks (02, 08, 13, 50 + the 20 new ones), uniform window, today's finder: 11008 Hz: 6 s 760,941 · 8 s 1,036,368 ·
10 s 1,256,607 · 12 s 1,563,138 · 16 s 2,020,131 · 20 s 2,514,762 · 22 s 2,857,401 · 24 s 3,100,581 · 28 s 3,762,693 ·
34 s 4,371,102 · 38 s 4,952,286 · 40 s 5,333,103 B. 8000 Hz: 10 s 912,798 · 16 s 1,455,939 · 20 s 1,861,065 ·
24 s 2,243,880 B. The finder picks the best match in [0.75 W, W], so the real loops are on average about 0.86 W.

Bars: the Safari loop bodies are whole numbers of bars. For example 02 = 102.4 s = 32.00 × 3.2 s, 08 = 20.00,
14 = 48.00, 15 = 16.00. This confirms the tempo estimates, so loops of whole bars are possible.

### 2.3 Proposed finder: `bar_loop` (replace `short_loop` for new tracks)

Prototype: `$P/scripts/barloop.py` (`bar_loop`) and `$P/scripts/loopq2.py` (`beat_of`, `onset`, `chroma`).

1. Onset envelope: STFT n=1024, hop 128 (11.6 ms), log1p(10|X|), positive spectral flux summed, minus a 32-frame
   moving mean, clipped at 0.
2. Beat: for nb = 1..399, bar = body/nb, beat = bar/4 within 70–200 BPM. Pick the nb that maximises
   ac[beat] + 0.5·ac[2·beat] + 0.5·ac[4·beat] (ac = autocorrelation of the onset envelope). unit = 4·beat.
3. For k = floor(W/unit) down to ceil(0.5·W/unit): the candidate end is start + k·unit. Refine it within ±0.15 s
   to the position where the 2 s of onset envelope after the end best matches the 2 s after the start (normalised dot
   product = r). Score = chroma match [−1 s, +2 s] + 0.5·spectral match (make_audio.spectra, same window) + 0.5·r.
   Chroma: STFT n=4096, hop 512, 55–2500 Hz folded to 12 pitch classes, log, mean-removed, unit norm.
4. Take the **longest** k whose score ≥ best − 0.08.
5. Waveform refine: ±64 samples (not ±hop as today), then align to 16 samples. Use the same 0.12 s crossfade as today.

Results at W = 38 s for the new tracks. All joins are rhythm-continuous (lag 0–12 ms). r and chroma say how similar the
music after the join is to what followed in the original. For 20/21 the composer's own loop body is used (it is
short, so it costs little):

| track | shape | loop | bytes | join check |
|---|---|---|---|---|
| 03_zako_mid | ('bars', 38) | 35.20 s from 64.92 s = 11 bars of 3.200 s | 217953 | lag +0 ms, r 0.82, chroma 0.42 |
| 04_dragon | ('bars', 38) | 25.59 s from 84.89 s = 8 bars of 3.200 s | 158445 | lag +0 ms, r 0.67, chroma 0.54 |
| 09_boss_cave | ('bars', 38) | 36.19 s from 34.64 s = 19 bars of 1.905 s | 224082 | lag +12 ms, r 0.63, chroma 0.57 |
| 10_boss_desert | ('bars', 38) | 25.59 s from 32.04 s = 8 bars of 3.200 s | 158445 | lag +0 ms, r 0.84, chroma 0.51 |
| 11_boss_ice | ('bars', 38) | 35.30 s from 33.99 s = 11 bars of 3.205 s | 218592 | lag +0 ms, r 0.74, chroma 0.49 |
| 12_boss_volcano | ('bars', 38) | 35.20 s from 32.93 s = 11 bars of 3.200 s | 217926 | lag +0 ms, r 0.64, chroma 0.24 |
| 14_zako_late | ('bars', 38) | 31.99 s from 11.89 s = 10 bars of 3.200 s | 198081 | lag +12 ms, r 0.38, chroma 0.57 |
| 15_zako_castle | ('bars', 38) | 35.33 s from 69.61 s = 12 bars of 2.945 s | 218781 | lag +0 ms, r 0.22, chroma 0.56 |
| 17_boss_margos | ('bars', 38) | 26.35 s from 55.26 s = 16 bars of 1.639 s | 163143 | lag +12 ms, r 0.17, chroma 0.55 |
| 18_boss_oshalsias | ('bars', 38) | 33.33 s from 39.10 s = 20 bars of 1.667 s | 206361 | lag +12 ms, r 0.55, chroma 0.54 |
| 19_boss_picharus | ('bars', 38) | 28.80 s from 7.43 s = 9 bars of 3.200 s | 178326 | lag +12 ms, r 0.65, chroma 0.48 |
| 20_boss_mimikkun | ('loop',) Safari loop body | 43.64 s from 15.05 s (the web loop itself) | 270189 | seamless by design |
| 21_boss_nazokimos | ('loop',) Safari loop body | 38.40 s from 47.04 s (the web loop itself) | 237771 | seamless by design |
| 22_boss_washaras | ('bars', 38) | 19.71 s from 2.04 s = 6 bars of 3.288 s | 122067 | lag +0 ms, r 0.42, chroma 0.51 |
| 23_boss_kiranos | ('bars', 38) | 31.99 s from 38.73 s = 10 bars of 3.200 s | 198063 | lag +12 ms, r 0.70, chroma 0.19 |
| 24_boss_yamijas | ('bars', 38) | 35.20 s from 56.89 s = 11 bars of 3.200 s | 217944 | lag +0 ms, r 0.60, chroma 0.60 |
| 25_boss_darkness | ('bars', 38) | 34.40 s from 42.07 s = 13 bars of 2.647 s | 213012 | lag +12 ms, r 0.29, chroma 0.60 |
| 26_boss_dotark | ('bars', 38) | 36.00 s from 59.02 s = 12 bars of 3.000 s | 222939 | lag +12 ms, r 0.31, chroma 0.52 |
| 27_ending | ('bars', 38) | 29.33 s from 5.11 s = 11 bars of 2.667 s | 181602 | lag +12 ms, r 0.73, chroma 0.75 |
| 49_castle_last_waves | ('bars', 38) | 19.00 s from 2.92 s = 12 bars of 1.593 s | 117648 | lag +0 ms, r 0.66, chroma 0.61 |
| **20 new tracks** | | | **3,941,370 B (3849 KB)** | |

If the user OKs a re-cut of the existing loops with `bar_loop` at 38 s: 02 = 28.80 s (9 bars, 178,299 B),
08 = 19.19 s (6 bars, 118,836 B), 13 = 28.78 s (15 bars, 178,200 B), 50 = 28.79 s (9 bars, 178,236 B). Total 653,571 B
instead of 731,007 B. The joins become rhythm-continuous, while today 08 is −81 ms and 13 is −105 ms.

`bar_loop` totals for all 24 loop tracks: 16 s 1,946,781 · 20 s 2,409,354 · 24 s 2,621,043 · 28 s 3,225,429 ·
34 s 4,081,275 · 38 s 4,477,554 · 40 s 5,002,479 B. For the 20 new tracks only: 16 s 1,669,941 · 20 s 1,954,701 · 24 s 2,166,390 ·
28 s 2,711,349 · 34 s 3,427,704 · 38 s 3,823,983 · 40 s 4,170,609 B.

The "musically sane" verdict is by measurement only. I could not listen. `$P/joins/<track>_bar38.wav` and
`_cur34.wav` are the last 4 s of each loop followed by its first 4 s (11008 Hz) for a listening check.

---------------------------------------------------------------------------------------------------------------

## 3. Budget

Measured from the current map (`$P/scripts/bankfree.py`). Free 558,409 B, current BGM 1,925,793 B. Bank $80 is
excluded: it has HEADER/VECTORS at fixed addresses. So the space BGM could use in 4 MB is 2,473,337 B. The non-music
additions take 340 KB (348,160 B), which leaves **BGM capacity 2,125,177 B (4 MB) · 4,189,561 B (6 MB ExLoROM, +63 banks) ·
6,221,177 B (8 MB ExLoROM, +125 banks)**.

The fixed part stays as it is: 01_opening full 346,878 + 16_boss_secret full 717,876 + 05_victory 74,304 + 06_gameover 55,728 =
**1,194,786 B**.

| ROM | budget for the 24 loop tracks | existing 4 kept (731,007) → room for 20 new | longest uniform window (today's finder / bar_loop) |
|---|---|---|---|
| (a) 4 MB | 930,391 B | 199,384 B (≈10 KB = 1.6 s per track) | all 24 re-cut: 6 s @11008 (avg loop 5.1 s) or 10 s @8000 (8.5 s); with the existing kept: impossible |
| (b) 6 MB | 2,994,775 B | 2,263,768 B | all 24: 22 s / 24 s; existing kept: 20 s / 24 s (new avg ≈ 17.5 s) |
| (c) 8 MB | 5,026,391 B | 4,295,384 B | all 24: 38 s / 40 s; existing kept: 38 s / 40 s |

Per-track options:
- 6 MB, existing kept: zako 03/14/15 + 04 + 49 + 27 at `bars 34`, the 14 boss songs at `bars 16` → 2,850,156 B (145 KB
  spare). Or everything new at `bars 24` → 2,897,397 B (97 KB spare).
- **8 MB (recommended):** existing kept, 18 new at `bars 38`, 20/21 composer loops → loop tracks 4,672,377 B, whole
  stream **5,867,163 B**, **354 KB spare** after the 340 KB of other additions. With the 4 existing re-cut: 431 KB spare.
  At `bars 40` for all new: 124 KB spare, which is too tight.
- 4 MB at 8 kHz would also need a driver change (timer 2 divisor per track, see 4.6). Not recommended.

---------------------------------------------------------------------------------------------------------------

## 4. Stream format and code changes (prototype: `$P/proto.patch`)

### 4.1 tools/make_audio.py

- BGM list: keep the first 8 entries and their order. This keeps ids 1–8 and BGM_PITCH. Append the new ones:

```
('BGM_ZAKO_MID','03_zako_mid.ogg',('bars',38)), ('BGM_DRAGON','04_dragon.ogg',('bars',38)),
('BGM_B_CAVE','09_boss_cave.ogg',('bars',38)), ('BGM_B_DESERT','10_boss_desert.ogg',('bars',38)),
('BGM_B_ICE','11_boss_ice.ogg',('bars',38)), ('BGM_B_VOLCANO','12_boss_volcano.ogg',('bars',38)),
('BGM_ZAKO_LATE','14_zako_late.ogg',('bars',38)),      # plays in 魔王の城 / 魔王城の地下 (BGM_SWAP)
('BGM_ZAKO_CASTLE','15_zako_castle.ogg',('bars',38)),  # plays in 氷の洞窟..灼熱の洞窟 (BGM_SWAP)
('BGM_B_MARGOS','17_boss_margos.ogg',('bars',38)), ('BGM_B_OSHALSIAS','18_boss_oshalsias.ogg',('bars',38)),
('BGM_B_PICHARUS','19_boss_picharus.ogg',('bars',38)), ('BGM_B_MIMIKKUN','20_boss_mimikkun.ogg',('loop',)),
('BGM_B_NAZOKIMOS','21_boss_nazokimos.ogg',('loop',)), ('BGM_B_WASHARAS','22_boss_washaras.ogg',('bars',38)),
('BGM_B_KIRANOS','23_boss_kiranos.ogg',('bars',38)), ('BGM_B_YAMIJAS','24_boss_yamijas.ogg',('bars',38)),
('BGM_B_DARKNESS','25_boss_darkness.ogg',('bars',38)), ('BGM_B_DOTARK','26_boss_dotark.ogg',('bars',38)),
('BGM_ENDING','27_ending.ogg',('bars',38)), ('BGM_CASTLE_WAVES','49_castle_last_waves.ogg',('bars',38)),
```

  The prototype used `('short', 34)` for all of these (5,565,888 B stream). The two new shapes:
  - `('bars', W)` = `bar_loop` above, with loop_blk 0.
  - `('loop',)` = start at loop_offset, end = len//16·16, 0.04 s crossfade (as `full`), loop_blk 0.
- Stop emitting BANKxx segments, `bgm_banks` and `bgm_table`. Write `data/gen/audio/bgm.brr` (as now) and
  `data/gen/audio/bgm_tracks.json` = `[{name, first, blocks, loop}]`. `audio.s` keeps the SPC image and the se_* tables.
  `audio.h` keeps `BGM_PITCH`, `N_BGM_TRACKS`, `TRACK_*`.
- Recommended: also write the C enum into audio.h (`#define BGM_<NAME> (TRACK_<NAME>+1)`), so that sound.h cannot drift.
  cc65 rejects an enum compare in a typedef array size, so a compile-time check in C does not work.
- `BGM_BANKS` and `BLOCKS_PER_BANK` go away.

### 4.2 tools/pack_bgm.py (new) + build.py: link-time placement

- build.py pass 1: assemble `build/obj/bgm_place.s` = `pack_bgm.stub(n)`. It defines `bgm_seg_bank` .res 255,
  `bgm_seg_ptr` .res 510, `bgm_seg_len` .res 510 and `bgm_table` .res 16·n in segment BANKFF, at their final size and
  without stream data. Then link.
- `area_used(map, cfg)` sums the segment sizes per memory area. `pack_bgm.place()` fills the free bytes of each bank in
  the order **$01..$7D (extended), $81..$9F (code banks), $A0..$FF (data banks)** (the prototype used ext, $A0..$FF,
  $81..$9F; the recommended order leaves the $A0–$FF tails to GFX/text, which need non-attribute banks).
  - Each bank gets one segment `BGMxx`. In the linker cfg it is the **last** segment of its memory area, so nothing
    linked before it moves between the passes.
  - Each chunk is `.incbin "audio/bgm.brr", off, len` with `.assert .loword(bgm_cN) = $xxxx` to catch drift.
  - Skip chunks under 64 blocks: the prototype made 188 segments, many of them 1–3 blocks in nearly full banks.
  - Error out if more than 255 segments are needed, or `"BGM: N bytes do not fit"` (the prototype prints exactly this
    for 4 MB: 3,094,839 B short; 6 MB: 1,030,959 B short at 34 s windows).
- bgm_table rows are the same 16 bytes as today, but the first byte is the **segment index** and "left" is the blocks
  left in that segment: `[seg, ptr lo, ptr hi, left lo, left hi, n0, n1, n2] ×2` (start, loop; loop n = 0 → once).
- Pass 2: assemble the real `bgm_place.s` and link again. Cost: one more ca65 + ld65 run (the build stays under 2 s).
- BGM is placed automatically after everything else, so other groups can add code/GFX/text to any bank and the
  BGM yields. The build fails only when the total does not fit.

### 4.3 src/asm/sound.s

- `.import bgm_table, bgm_seg_bank, bgm_seg_ptr, bgm_seg_len, ...` replaces `bgm_banks`. Remove `BLOCKS_PER_BANK`.
- `load_pos`: `s_ptr+2 = bgm_seg_bank[s_bi]` (drop the dead `ldx #0`).
- `stream_one` at `s_left == 0`:

```
  inc s_bi
  lda s_bi / A16 / and #$00ff / tax / A8
  lda f:bgm_seg_bank,x
  sta s_ptr+2
  A16 / txa / asl / tax
  lda f:bgm_seg_ptr,x
  sta s_ptr
  lda f:bgm_seg_len,x
  sta s_left
```

  The engine grows +2 bytes. The CPU cost is the same: it only runs at a segment change, about every 47 s of music for
  a full bank.
- Measured: the money battle profile (PROFILE=1, 600 frames) shows send_block 14.8 % vs 14.4 %, stream_idle 11.2 vs
  10.9 %, wait_frame 6.4 vs 6.2 % (repo ROM vs 8 MB prototype with BGM in slow banks $01–$7D). No measurable cost.

### 4.4 C side

- `src/c/sound.h` enum (same order as make_audio):
  `BGM_NONE, BGM_OPENING, BGM_ZAKO, BGM_BOSS, BGM_MAOU, BGM_MAOU_FINAL, BGM_PEKO, BGM_VICTORY, BGM_GAMEOVER,
  BGM_ZAKO_MID, BGM_DRAGON, BGM_B_CAVE, BGM_B_DESERT, BGM_B_ICE, BGM_B_VOLCANO, BGM_ZAKO_LATE, BGM_ZAKO_CASTLE,
  BGM_B_MARGOS, BGM_B_OSHALSIAS, BGM_B_PICHARUS, BGM_B_MIMIKKUN, BGM_B_NAZOKIMOS, BGM_B_WASHARAS, BGM_B_KIRANOS,
  BGM_B_YAMIJAS, BGM_B_DARKNESS, BGM_B_DOTARK, BGM_ENDING, BGM_CASTLE_WAVES, N_BGM`.
  The prototype edited it by hand; generating it is better (4.1).
- Generated tables (make_data.py; prototype in `tools/song_tables.py`, which appends to tables.c):
  - `const u8 mon_song[N_MON]` = DexData.monster_bgm mapped to SFC ids by file name. Unknown or no song = 0.
    Mapping: the web id goes through `bgm_files` with the swap applied, then the file stem is looked up in make_audio.BGM.
  - `const u8 area_song[N_AREA]` = the area's `bgm` the same way (mirage = 0).
  - Declare both in `gtypes.h`. Remove `mon_bgm` (and `mon_track` in dex.c).
  - Prototype values: area_song = {2,2,2,2,2,9,9,9,9,9,16,16,16,16,15,6,0}. mon_song[MON_114] = BGM_BOSS(3),
    mon_song[MON_089] = BGM_B_MARGOS(17).
- `battle_core.c battle_bgm()` (replaces the current one; +18 B code):

```c
u8 battle_bgm(void) {
  u8 kind = stage_boss_kind(), i, m;
  if (b_mirage) return b_ne && mon_song[b_e[0].mon] ? mon_song[b_e[0].mon] : BGM_BOSS;
  if (!kind) return area_song[b_area];            /* tutorial / training are in アーケディア城: 02 */
  if (is_treasure() || is_money()) return mon_song[MON_115];
  if (kind == 1) return BGM_DRAGON;
  if (is_secret()) return BGM_PEKO;
  if (is_castle()) {
    if (b_men != D_CASTLE_MEN) return mon_song[castle_bosses[b_men - 1]];
    if (b_stage < b_stage_count) return BGM_CASTLE_WAVES;
    for (i = 0; i < b_ne; ++i) { m = b_e[i].mon; if (forms_index(m) != 255) return mon_song[m]; }
    return BGM_MAOU;
  }
  return mon_song[areas[b_area].boss];
}
```

  The callers are unchanged: battle_view.c EV_STAGE_START and EV_FORM_SWAP call `bgm_play(battle_bgm())`, and
  `bgm_play` ignores the same song, so waves and form 1→2 keep playing and the final form switches to 50.
- `dex.c mon_card`: `bgm_play(known && mon_song[m] ? mon_song[m] : BGM_OPENING);` (−19 B).
- `story.c ending()`: `bgm_play(BGM_ENDING);` as the first statement (+5 B). The title then plays OPENING as now.
  Other groups adding the web's second trigger (choosing アーケディア城 while `king_return_due`) must also go through
  ending().
- No save fields. The existing `sv.bgm_vol` belongs to the volume-settings group.
- WRAM: mon_song 94 + area_song 17 = +111 B of RODATA copied to $7E, −94 B for mon_bgm = **+17 B**. No LOWBSS, VRAM or
  OAM change.
- ROM (non-stream): BANKFF tables 255 + 510 + 510 + 28·16 = 1,723 B (was 59 + 128). Code +6 B net, ENGINE +2 B.

### 4.5 ROM layout: 8 MB ExLoROM (build.py)

- `ROM_MB` = 4/6/8 (default 8 once adopted). `EXT_BANKS` = {6: $40, 8: $7E}.
- Linker cfg: after ROM7F (bank $FF), add memory areas `EXTnn` for nn = $00..EXT_BANKS−1 with
  `start=$nn8000, size=$8000, fill=yes, fillval=$ff, file=%O` (no bank attribute, so `^label` = nn works). Segments
  `BANKnn` + `BGMnn` for nn ≥ 1. EXT00 holds no segments.
- After linking, `exlorom()`:
  - Copy file[0:$8000] (bank $80: STARTUP, ENGINE, HEADER, VECTORS) to file[$400000:$408000]. The CPU reads the reset
    and NMI vectors from $00:FFxx, which is file $407Fxx in ExLoROM, and the reset/NMI stubs run there before
    `jml reset_fast`/`jml nmi_fast`.
  - Set the ROM size byte $FFD7 = $0D in both headers. The map mode $30 stays.
  - Pad to ROM_MB MB.
- `fix_checksum()`: put the placeholder in both headers, then sum = sum(first 4 MB) + sum(rest) × (4 MB / len(rest)).
  Write it to both headers.
- SRAM stays at $70:0000 (verified with the probe ROM in snes9x).
- Banks $00–$7D are SlowROM. That is fine for the stream (measured). For other data, DMA speed is unchanged, while CPU
  reads are about 25 % slower.
- Compatibility:

  | target | result |
  |---|---|
  | snes9x_libretro (here, prints `Map_JumboLoROMMap`) | 8 MB and 6 MB prototypes run, verified |
  | bsnes master | EXLOROM board, from its source |
  | FXPak Pro | from its changelog (fw ≥ 1.8.0) |
  | Mesen2 | not supported |
  | bsnes_mercury | black screen, verified |
  | Everdrive | unknown |

  ExHiROM would run everywhere, but it remaps every bank (LoROM $C0–$FF would collide with $80–$BF mirrors, and SRAM
  moves to $A0–$A3:6000) and touches every asset path. Effort L+, so it is not recommended unless the user needs
  Mesen2 or Everdrive.
- README: update the ROM line and the BGM row (see new_text_ja).

### 4.6 Only if 4 MB with 8 kHz were chosen (not recommended)

The driver's timer 2 runs at TIMER_DIV 93 = one tick per BRR block at 11008 Hz. An 8000 Hz track would make the ring
accounting run 37.6 % fast. That over-reports free space, and unplayed blocks get overwritten. It would need a per-track
divisor (128 for 8000 Hz) sent with START (new command 6 or a third parameter) and a per-track pitch in bgm_table.
sound.c passes ea1 = BGM_PITCH today.

---------------------------------------------------------------------------------------------------------------

## 5. Test plan (harness; all of these were run on the prototype)

Tools: `$P/scripts/wavref.py rec.wav bgm.brr first_block t0 t1` compares a recording with the decoded stream,
aligned per 50 ms window. `$P/scripts/wavloop.py rec.wav bgm.brr bgm_tracks.json NAME t0 t1` does the same with the
loop taken into account. One recorded sample = one DSP sample in snes9x. A correct stream gives median corr ≥ 0.99.
Sound effects lower single windows, so look for runs of low windows.

1. **Build:** `ROM_MB=8 python build.py rom`. It prints the segment list, the ROM is 8,388,608 B and snes9x logs
   `Map_JumboLoROMMap`. Also check `python build.py rom` (4 MB) fails with "BGM: N bytes do not fit".
2. **Title:** `run/title.txt` (wav 1500 frames). `wavref.py title.wav bgm.brr 0 3.5 24` → 0 low windows, median 0.996
   (prototype: OK). The first 3 s may show the existing boot glitch (0.6).
3. **Dex:** make a save with `mksave.py all.srm --open 15 --clear-all --dex 1000 --ending`, then title → A → Y → DOWN×4 →
   A → A → card RIGHT×13 (No.014 翼龍ドラゴス) / ×20 (No.021 鈍龍タトルノドン). Going LEFT from No.001 wraps
   (No.090 ペコラータ, 089 魔王最終形態, 086 魔王のペット).
   - Expect B_MARGOS, B_CAVE, PEKO, MAOU_FINAL and CASTLE_WAVES. The prototype got corr 0.996–0.999 on all five, and
     CASTLE_WAVES also through its loop point at 28.6 s.
   - Unknown cards (No.001 with --dex 1000) must play OPENING.
   - Scripts: `run/dex/dex2.txt`, `run/dex/dex3.txt`.
4. **Battle routing:** for each row of table 1.4, start the stage and record 4 s after EV_STAGE_START. Use the
   L×3 debug unlock or mksave `--clear AREA:MASK`. At least check: アーケディア城 5-3 (08), 平原の村 5-3 (17),
   小島 6-3 (20), 魔王の城 1-1 (14_zako_late), 魔王の城 10-1 (49), 10-5 forms (13 → 13 → 50 by forcing HP 0 on each
   form), 魔王城の地下 1 (14_zako_late) and 5 (20), 月見の島 (16), 蜃気楼の斜塔 waves 1/2/15/17/18 (08/17/13/50/16),
   チュートリアル and カカシ (02). The prototype money battle (`run/money/m.txt`, 36 s of attacks) matched
   BGM_ZAKO_LATE with median corr 0.998, and the scattered low windows were the same as on the repo ROM (sound
   effects).
5. **Ending:** a save with the castle floors 1–9 cleared, win floor 10 (debug or a strong save). ending() must start
   BGM_ENDING. Then wavref against TRACK_ENDING.
6. **Performance:** `PROFILE=1 ROM_MB=8 python build.py rom; python tools/harness/prof.py run/money/pre.txt 600`. The
   sound.s share and wait_frame must stay within ±1 % of the 4 MB build. Also check battle lag and nmi_vmax as the
   README describes.
7. **textcheck:** `python tools/harness/textcheck.py <scripts>` works on the 8 MB PROFILE ROM (prototype: 0 findings
   on dex3.txt).
8. **Listening:** play `$P/joins/*_bar38.wav` (and the final ROM in snes9x) for every new track. Re-run `barloop.py`
   on any track that sounds off, with a different window.

---------------------------------------------------------------------------------------------------------------

## 6. Effort, risks

- Effort: conversion + finder M, packer + sound.s M (prototype exists), ExLoROM S, C selection S. About 1.5–2 days
  including the listening and routing tests.
- Risks:
  1. ExLoROM is less widely supported (4.5). The user must test on snes9x (OK). Mesen2 and old bsnes forks do not run it.
  2. The loop quality is checked by measurement. Some joins have low r (17: 0.17, 15: 0.22, 25: 0.29), which means a
     section change. A by-ear review is needed.
  3. Two-pass link: if another group adds a fixed-address segment in a bank that BGM also uses, the BGM chunk must
     still be last. Keep BGMxx after everything in each memory area, and the `.assert` catches drift.
  4. A data/audio regen also changes unrelated things (0.2). Keep `spc.bin` unless the SE change is wanted.
  5. The existing boot glitch (0.6) may be more audible with 8 MB timings.
  6. The bgm_table index is 1 byte (`ea[0] = song-1`, `asl ×4` in 16 bits), so up to 255 tracks. Segments ≤ 255.

## 7. Shared needs (other groups)

- Use `$P/shipped/*.gd`, not `scratchpad/src`: the shipped web build is newer (MON_114/MON_089 songs, SPIN_POSES for
  ランページ, CHAMPAGNE boss FX, ...).
- Under ExLoROM, BGM no longer owns banks $B0–$BF, $E0–$FD and $90–$9F. It fills what is left after linking, in the
  order ext → code → data banks. GFX (make_gfx GFX_BANKS) can use $A0–$FF and the extended banks $01–$7D (SlowROM; no
  bank attribute, so `^label` works there). C code can use $81–$9F.
- Total ROM after the recommended BGM in 8 MB: 354 KB spare beyond the 340 KB of non-music additions. If those grow
  past about 690 KB, shorten new-track windows (bars 34 frees 396 KB).
- The ending/king-return group must call `ending()` (which starts BGM_ENDING) from every trigger.
- Mirage suspend/resume and 再挑戦: nothing to do. EV_STAGE_START picks the song.
