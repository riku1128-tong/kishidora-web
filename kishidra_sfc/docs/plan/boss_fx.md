# Boss effects and special poses (group `boss_fx`): implementation spec

Port: `/home/user/kishidora-web/kishidra_sfc` (read-only). Web source: the decompiled `scratchpad/src/battle.gd`,
`battle_core.gd`, `tuning.gd`, `ui/fx.gd`, `ui/dark_aura.gd`, `ui/fighter.gd`.
Measurement scripts, previews and run logs: `scratchpad/plan/boss_fx/scripts/`, `out/`, `t/`.

Units in this spec: web pixels x 0.73 = SFC pixels (the port's `SCALE`), web seconds x 60 = frames.
SFC screen positions used below (measured from the port):

| name | value | where it comes from |
|---|---|---|
| hero feet | (56, 123) | `D_HERO_X`, `FOOT_Y` |
| H = hero centre | (55, 98) | idle1 body rect x 42..67, y 77..124, centre at 0.5 w / 0.45 h (web `Fighter.center()`) |
| HF = hero feet + 26 | (55, 124) | web `hero.center() + (0, 36)` |
| boss feet | (172, 127) | `b_e[k].sx = D_BOSS_POS_X + 84`, `boss_place` py = `FOOT_Y + 4` |
| boss body rect | x0 = sx - ax, y0 = 127 - ay, w = W*8, h = H*8 | the current pose's descriptor (W, H, -ax, -ay) |
| M = boss mouth | (x0 + w/5, y0 + 2h/5) | web `_mouth()` |
| BC = boss centre | (x0 + w/2, y0 + 0.45h) | web `center()` |
| field | lines 16..127 | BG rows 2-15; the panel is lines 128-223 |

---------------------------------------------------------------------------------------------------

## 0. Summary

| # | item | ROM (asset banks) | code | WRAM | effort |
|---|---|---|---|---|---|
| 1 | Pekorata's entrance (king -> orbs -> Pekorata) | ~1.0 KB (orbs/sparkles 0.45 + king tint palettes 0.48) | ~2.5 KB (bank $86) | ~60 B | M |
| 2 | Pekorata idle poses + "sleeping boss does not guard" + petting | 22.3 KB measured (+0.1 KB descriptor) | ~1.2 KB | ~260 B | M |
| 3 | Boss special-move effects, 24 kinds | ~5.0 KB (art 3.3 KB measured + recipes ~1.7 KB) | ~6 KB (bank $86) + 0.4 KB | ~770 B | L |
| 4 | Dark aura on 龍騎士マーゴス (MON_114) in 魔王の城 | ~0.6 KB | ~1.2 KB | ~60 B | M |
| 5 | Hero spin poses for ランページ (SP_RAMPAGE) | 2.5 KB measured | ~0.3 KB | ~110 B (incl. the 90-B jump table in RODATA) | S |
| | total | **~31.5 KB in GFX/asset banks** | **~11.6 KB code** (mostly the empty bank $86) | **~1.26 KB** of the 7.2 KB free | |

No new text and no new glyphs: every string these items show is already in the port
(`NAME_KING` 王さま, `TALK_START`, `T_BOSS_WARN` WARNING, the `MONSP_*` special names). The "Z", "!" and hearts
are sprite tiles.

---------------------------------------------------------------------------------------------------

## 1. Shared foundations (build these first; items 1, 2, 3, 4 use them)

### 1.1 New module `src/c/battle_fx.c` (bank 6 = $86)

`build.py` already reserves `'battle_fx.c': 6` in `C_BANKS`, and bank $86 is completely empty (32 KB).
Put the effect engine, the entrance, the aura and the Pekorata rest/pet logic there; `battle_view.c` (bank $85, 27.2 KB
of 32 KB used, other groups also add to it) only gets hooks. Cross-bank calls go through the existing trampolines
(about 30 cycles each, 3-5 calls per frame).

Header `src/c/battle_fx.h` (all functions non-static so build.py makes trampolines):

```c
void fx_battle_init(void);              /* battle_scene() setup, screen off: strip-4 common tiles, HDMA off */
void fx_stage(u8 boss_actor, u8 mon);   /* EV_STAGE_START (after load_stage_gfx) and EV_FORM_SWAP */
void fx_bang(u8 enemy, u8 special);     /* EV_ENEMY_BANG */
void fx_windup(u8 enemy, u8 special);   /* EV_ENEMY_WINDUP */
void fx_strike(u8 enemy, u8 special);   /* EV_ENEMY_STRIKE */
void fx_frame(void);                    /* animate(): particles, colour math, aura, intro, rest */
void fx_sprites(void);                  /* draw_sprites(): after the numbers, before the hero (in front) */
void fx_defeat(u8 enemy);               /* EV_DEFEAT: aura off, rest off */
u8   fx_rest_pose(u8 enemy);            /* item 2: pose code wanted by the rest/pet rules, 255 = none */
void fx_number(u8 enemy, u8 block);     /* EV_NUMBER on an enemy: wake rule, item 2 */
u8   fx_intro_on(void);                 /* item 1 running (HUD, input and boss drawing check it) */
```

The geometry it needs from `battle_view.c` (boss pose rect, foe x) is passed through a small exported struct
`FxGeo { s16 bx0, by0; u8 bw, bh; s16 ax[3]; }` that `battle_view.c` refreshes in `boss_place()` and `animate()`
(no static symbols cross the bank).

### 1.2 OBJ VRAM / palette allocation in battle (measured from `battle_view.c`)

The battle uses all 8 OBJ strips (64 tiles each, OBSEL $22 = 8x8 / 32x32, name base $4000, 2nd table $5000):
0-1 hero, 2-4 zako slots 0-2, 5 misc, 6 FX digits, 7 FX cells; and all 8 OBJ palettes: 0 hero, 1 shield,
2-4 zako slots, 5 FX_PAL, 6-7 skill icons.

**Rule found in the code:** whenever a boss is on the field, at most 2 other actors exist (`ENEMY_MAX` 3), so zako
slot 2 is never used: **OBJ strip 4 (tiles 256-319, VRAM word $5000-$53FF, 2 KB) and OBJ palette 4 (CGRAM
192-207) are always free in a boss stage** (secret island: escort max 0; castle/area bosses: escort max 1 + lucky).
Strip 4 layout for effects:

| tiles (strip 4) | content | loaded |
|---|---|---|
| cols 0-7 (2 large 32x32 cells, 32 tiles) | the boss's "pair" of large effect cells (or 32 small tiles for MULTI) | `fx_stage()` via `queue_vram` (1 KB) |
| cols 8-15 (32 small 8x8 tiles) | common small tiles (dots, arcs, streaks, bubbles, flames, Z, heart, "!") | `fx_battle_init()` and again in `fx_stage()` if a zako used strip 4 in the stage before (1 KB) |

Palette 4 = one 15-colour palette **per boss** (`fx_stage` loads `ASSET_BFX_PAL_<n>` to CGRAM 192). Roles
(index): 1 ink, 2-6 the effect colour ramp (darkened 0.6, darkened 0.3, base, lightened 0.3, lightened 0.6), 7 white,
8 cream, 9-11 "accent" ramp (flames: light/base/dark), 12 Z colour #dfe8ff, 13 heart #ff7aa8, 14 heart dark #b0285a,
15 yellow #ffe060 (the wake "!").

Non-boss stages (a human mid-boss's ギガスラスト) may have zako in slot 2; then the GIGA effect uses only strip 7
(`FX_CELLS`) and palette 5 (`FX_PAL`), see 3.4.

WRAM $7F buffers free during a battle (measured from the unpack calls): $7F:6C00-$9FFF and $7F:C000-$CFFF.
Use: $7F:6C00-$73FF effect tiles staging (2 KB), $7F:7400-$8BFF king tiles (6 KB, item 1), $7F:8C00-$8EFF
entrance sprite tiles. (Hero $7F:2000, zako $7F:4800-$5FFF, boss $7F:A000/$D000, text canvas $7F:0000-$3FFF,
PROFILE logs $7F:DA00-$DBFF/$E000/$F000 stay untouched.)

### 1.3 Colour-math manager and two new HDMA channels

The effects are mostly colour math (fixed-colour add/subtract) limited to lines or a window, which costs **0 bytes of
the 4864-byte NMI budget** (registers only). Today `animate()` writes `cgwsel/cgadsub/coldata` directly for the
special's white flash (`flash_t`) and WARNING (`warn_t`). Replace those writes with requests to one arbiter in
`battle_fx.c`:

```c
/* each frame, every effect that wants colour math calls this; the highest prio wins */
void cm_req(u8 prio, u8 cgwsel, u8 cgadsub, u8 r, u8 g, u8 b, u8 line0, u8 line1);
```

Priorities: 4 special cut-in flash (`flash_t`), 3 WARNING red, 2 boss-effect flash/band/darken, 1 boss tint
(bang red, BG1 only), 0 dark-aura floor ring. `fx_frame()` applies the winner at the end of `animate()`; nothing
requested -> `cgadsub = 0`, HDMA ch 4/5 off.

* **HDMA ch 4 -> CGADSUB ($2131), mode 0**, table in WRAM (8 B): `[line0, 0x00] [line1-line0+1, value] [1, 0x00] [0]`
  (counts <= 127; split if needed). This limits any colour math to the lines line0..line1 (the field = 16..127; the
  panel never flashes unless the web flashes the whole screen).
* **HDMA ch 5 -> WH0..WH3 ($2126-$2129), mode 4**, table in WRAM (256 B), used for slanted beams and the aura ring.
  Rectangles need no table: one entry for all lines.
* New PPU shadows in `src/asm/crt0.s` (written in the NMI register block next to CGWSEL): `_wobjsel` ($2125),
  `_wobjlog` ($212B), `_wh[4]` ($2126-$2129); `hdma_setup` enables ch 4/5 from bits 4/5 of `_hdma_on`
  (ch 6/7 keep bits 6/7). DMA channels in use today: 0, 1 (GP), 2 (OAM), 6, 7 (HDMA) -> 4 and 5 are free.
* Window colour region: `CGWSEL = $10` (prevent colour math outside the colour window), `WOBJSEL` bits 5/7 enable
  W1/W2 for the colour window, `WOBJLOG` bits 2-3 = %10 (XOR) for a ring.
* Tearing: the tables are rewritten during the frame (like the existing scroll tables). Double-buffer ch 5 only if
  a test shot shows a torn beam.
* Important hardware limit used below: OBJ colour math only affects sprites on palettes 4-7.

### 1.4 Particle engine

Up to `FXP_MAX` = 24 particles, 14 bytes each (336 B BSS):
`s16 x, y` (1/16 px), `s16 vx, vy` (1/256 px per frame), `s8 ax, ay` (1/256 px per frame^2), `u8 age, life, style, fl`.
Modes in `fl`: LINEAR (pos += vel, vel += acc, optional drag vel -= vel>>4), RADIAL (centre + direction table x
radius(t), radius eased 1-(1-k)^3: rings), ORBIT (angle += speed, radius r0->r1 by k^2: leaves, orbs), CELL (a large
32x32 sprite, optional k^2 ease-in path from A to B).
`style` indexes a small const table (<= 16 styles x 8 B): up to 4 tiles over the life, palette (4 or 5), large/small,
flip rules, gravity. Sprites are emitted with the existing `spr_list` (SprEnt list, asm) from `fx_sprites()`,
OBJ priority 2 (in front of BG1/BG2, under BG3 text), before the hero so effects are in front.
Culling: drop a small sprite with top y > 120 and a large one with top y > 104 (keeps effects off the panel).

Budget per effect (all recipes in section 3 respect it): <= 16 small + <= 4 large alive (bolt: 6 large, stacked
vertically), and **<= 10 sprite slivers (8 px columns) on any scanline**: the hero (8-12), shield (2), damage
numbers (<= 8) and an escort (8) already sit on the hero's lines and the PPU drops slivers beyond 34 per line.
CPU in C about 150 cycles per particle per frame (24 -> 3600 cycles, 6 % of a frame); write the update+emit loop in
asm (`src/asm/fx.s`, ~50 cycles per particle) if the lag test (3.7) shows > 1 % extra lag.

Measured headroom (`prof.py`, PROFILE build, Pekorata's normal attack, 120 frames, script `t/p2.txt`): the wait
loop and the BGM streaming while waiting take 38 % (`stream_idle` 9.8, `refresh` 8.6, `vline` 7.6, `quiet` 6.7,
`wait_frame_far` 5.4) plus `send_block` 16.2 %; the boss pose unpack (`lz` 5.8 %, `boss_place` 3.9 %) is the
biggest game cost on those frames. So ~6 % for effects fits; avoid starting a heavy effect step on the frame of a
boss pose change if a lag spike shows.

### 1.5 Asset plumbing

C `const` data lands in RODATA, which build.py **copies to WRAM** ($7E:2000.., `CRAM`); WRAM C data is
46.8 KB / 54 KB. So big tables (recipes, palettes, ring tables) are assets in the asset banks read with
`copy_asset()` into a small WRAM buffer when needed (the pattern used for `bdesc`). Only tables < 256 B total stay
`const` in C (direction table 16 x 2 B, 64-entry sine, rampage offsets).

### 1.6 Harness-only test hooks (PROFILE builds only, `#ifdef PROFILE` in `battle_scene` input)

The harness cannot poke memory, so add, for test ROMs only: **SELECT = the targeted enemy does its special now**
(`attacks = sp_every`, `battle_force_attack(i)`), **SELECT+R = every enemy's HP to 0** (clears the wave), and
`prof_marks[8..11]` = fx state (current kind, particles alive, colour-math winner, rest state). Release builds are
unchanged.

---------------------------------------------------------------------------------------------------

## 2. Item 1: Pekorata's entrance

### 2.1 Web rules (battle.gd `_secret_intro`, `_orbs_to_king`, `_warning`; battle_core.gd)

* `core.boss_intro = true` in the battle scene. In `start_stage`: a boss stage normally goes to `Phase.WARN`
  (BOSS_WARN_FRAMES 90), but `if boss_intro and has_secret_boss(): phase = Phase.INTRO`
  (`has_secret_boss` = a boss with `awaken_boss(mon)`; on the surface that is `AWAKEN_MON` = MON_117
  兎騎士ペコラータ, so this also happens at 蜃気楼の斜塔 wave 18). `post("stage_start", {"intro": true})`.
  During INTRO the core does nothing (`_inputs.clear()`). `intro_done()` -> `Phase.FIGHT`, `post("fight")`.
  **The core does not request the "start" talk** in this path (that request is only in the SLIDE -> FIGHT branch);
  the intro shows it itself. No pause and no taps during INTRO (touch_down returns early).
* Sequence (times in s, frames at 60 fps):
  1. Boss hidden (`f.visible = false`), boss HP box hidden (`boss_box.visible = not phase in [WARN, INTRO]`),
     counters hidden; the hero slides in (`intro_slide` 1 -> 0 in 0.5 s).
  2. The king chibi (`king_chibi`, same dot size as the hero) walks in from x = W + w to `stand = (boss.home.x,
     hero.home.y)` in 0.9 s (54 f): x = lerp(x0, stand.x, 1-(1-k)^2), y = stand.y - |sin(3 pi k)| x kh x 0.16 x
     (1 - k/2) (three hops). Wait 0.25 s (15 f).
  3. Talk window, name **「王さま」** (`SECRET_INTRO_NAME`), page `TALK.start`:
     **「地上が平和になって暇してたウサ～。」「宝玉を取り戻した余の力を見せてやるウサ！」**; after
     `TALK_MIN_FRAMES` 20 a tap closes it.
  4. `_warning(true)` (strong): banner **「WARNING」** red #ff4040 (32 px, 1.5 s), red field flash 3 x (alpha 0 ->
     0.45 in 0.25 s -> 0 in 0.25 s), SE `warning`, two scrolling red stripe bands (top and bottom of the field,
     1.25 s). The king flashes red 3 x (modulate (1, 0.4, 0.4) 0.25 s / white 0.25 s) and jitters x +-1.5 px for 1.6 s.
  5. `_orbs_to_king`: the 4 orbs of `ORB_TABLE` (炎・水・風・光) appear at the hero's centre one every
     `ORB_APPEAR` 0.16 s with a small ring and SE `just1..4`; each flies `ORB_FLY` 0.45 s (ease-out cubic, arc
     height sin(k pi) x kh x 0.45) to its place on an ellipse around `mid = stand - (0, kh/2)`: rx = kh x 0.9,
     ry = rx x `ORB_TILT` 0.3; the angle starts at -pi/2 and turns lerp(2.4, 16.0, smoothstep(`ORB_SPIN_UP` 0.9 s,
     `ORB_IN` 2.3 s, t)) rad/s; after `ORB_SHRINK` 1.5 s the radius shrinks by rk = 1 - ((t-1.5)/0.8)^2; orbs with
     sin(a) < 0 are behind the king; orb size kh x 0.3 x (0.55 + 0.45 min(1, 2k)) x (1 + 0.15 depth) x lerp(0.75, 1, rk);
     10-point trails. SE `charge` at t = ORB_IN - 1.8 = 0.5 s. At 2.3 s: 4 rings (orb colours, r 6 -> 70 + 22 i), a
     burst of 30 #fff0a0, the king flashes (modulate 2.4 -> 1 in 0.3 s), wait 0.35 s.
  6. 16 sparkles fly from a circle r 170 to `mid` in 0.55 s (#fff0a0); SE `transform`; the king glows to
     modulate (3, 3, 2.6) over 0.6 s while shaking (+-2, +-1) x (1 + 4t).
  7. Spin: scale.x = ks x cos(v), v 0 -> 6 pi in 0.45 s, rising kh x 0.12.
  8. Full-screen white flash (alpha 1, 0.7 s), shake 10, the king disappears; Pekorata appears: white flash on her
     0.9 s, scale 0.45 -> 1.06 (0.24 s, back-ease) -> 1.0 (0.12 s), rings #fff0a0 r 12 -> 220 (0.6 s) and white
     r 8 -> 150 (0.45 s), burst 40 #ffe070. Wait 0.55 s, then `core.intro_done()`.
  Total about 7.0 s plus the talk.

### 2.2 SFC design

**Core (`battle_core.c`, bank $83)**
* `start_stage()`: `if (has_boss()) { if (b_mode == BM_NORMAL && secret_boss_here()) b_phase = PH_INTRO; else
  { b_phase = PH_WARN; b_phase_timer = D_BOSS_WARN_FRAMES; } }`; post `EV_STAGE_START` with `b = 2` for the intro
  (`b` is `has_boss()` today). `PH_INTRO` already exists in the enum; `battle_step()`'s default case already clears
  the inputs. `secret_boss_here()` = a boss with `awaken_boss(mon)`.
* New `void battle_intro_done(void) { if (b_phase == PH_INTRO) { b_phase = PH_FIGHT; ev_post(EV_FIGHT, ...); } }`
  plus the `opening_lock / opening_beg` calls that the SLIDE end does (Pekorata has neither; call them anyway for
  symmetry). Do **not** request `TALK_START` there.

**View (`battle_fx.c` state machine, driven from `fx_frame()` while `b_phase == PH_INTRO`)**
* EV_STAGE_START with b == 2 (in `battle_view.c`): `load_stage_gfx()` as today (the boss pose is uploaded but
  `boss_place()` is suppressed while `fx_intro_on()`: keep the BG1 map clear), `bgm_play(battle_bgm())`
  (16_boss_secret starts at once, as in the web), then `fx_intro_start()`. On the secret island this event comes
  before `screen_on()`: only arm the intro there; the first state runs in the main loop.
* Resources during the intro (no zako exist): king tiles `ASSET_KING_OBJ` (192 tiles, LZSS, already in ROM, used by
  story.c) unpacked to $7F:7400 and `queue_vram_big` into OBJ strips 2-4 (cells at cols 0-7; the asset's right
  halves are empty); `ASSET_KING_PAL` (3 palettes, one per cell row) -> OBJ palettes 2-4 (CGRAM 160-207).
  Entrance tiles (new asset `INTRO_FX`, 22 tiles in FX_PAL colours: 4 orbs 16x16, 2 small orbs 8x8 for the far side,
  cream dot/star/arc tiles) -> strip 2 cols 8-15 (palette 5 FX_PAL, which already holds red/blue/green/yellow/cream).
  King tint palettes (new asset `KING_PAL_FX`, 5 x 96 B: red = G,B x 0.4; glow 1-4 = 25/50/75/100 % toward white).
* States (frames):

| state | frames | what the SFC does |
|---|---|---|
| WALK | 54 | king (6 large sprites 2x3, palettes 2-4) feet from x 288 to 172 (`x = 288 - 116 (1-(1-k)^2)`), y = 123 - abs(sin(3 pi k)) x 15 x (1 - k/2) (sine from the 64-entry table); hero walks in with the existing `hero_slide = -80` |
| PAUSE | 15 | |
| TALK | until A/B after >= 20 f | `talk_show(TALK_START, 255)` -> speaker `NAME_KING` 王さま (existing text); `talk_close()` |
| WARN | 96 | `banner(T_BOSS_WARN, PAL_T_RED, 90)`, `se_play(SE_WARNING)`, `warn_t = 90` (the existing 3 red field flashes through `cm_req` prio 3); king palette red on frames 0-14, 30-44, 60-74; king x jitter +-1 px (rnd) each frame. (The stripe bands are not reproduced.) |
| ORBS | 138 + 21 | 4 orbs: orb i appears at H at f = 10 i (+ `SE_JUST`, a 4-arc ring of FX cream arcs r 3 -> 26 px over 18 f), flies 27 f (ease-out cubic, arc up 43 px) to its orbit slot; orbit centre `mid` = (172, 75), rx 70 px (`kh` 96 x 0.9 x 0.73... use 70 so it stays on screen), ry 21; angle (u16 8.8, 256 = 2 pi) starts at 192 and adds 1.63 -> 10.9 units per frame (lerp by smoothstep over f 54..138); after f 90 multiply the radius by rk = 1 - ((f-90)/48)^2 (hardware multiplier, `math.s`). sin(a) < 0: small 8x8 orb, emitted after the king's sprites (behind); else the 16x16 orb, emitted before (in front). Trails: 2 cream dots at the positions of 3 and 6 frames ago. `SE_CHARGE` at f 30. End: one 8-arc ring of cream at `mid` r 4 -> 51 px (27 f), 10 cream dots burst, king palette glow 4 -> 0 over 18 f, wait 21 f |
| CHARGE | 36 | 16 cream dots fly from a circle r 124 px to `mid` in 33 f (RADIAL particles, radius 124 -> 0); `SE_TRANSFORM`; king palettes glow 1 -> 4 (every 9 f); jitter x +-(2 + 8 k), y +-(1 + 4 k) |
| SPIN | 27 | toggle the king's H-flip at f 2, 7, 11, 16, 20, 25 (the zero crossings of cos 6 pi k); rise 11 px linearly |
| BURST | 33 | full-screen white add (COLDATA 31 -> 0 over 42 f, `cm_req` prio 2, all lines); shake 7 px 18 f; hide the king; `boss_place()` now; MOSAIC on BG1 size 15 -> 0 over 15 f (`mosaic = (s << 4) | 1`: the SFC stand-in for the scale-up); ring of 8 cream arcs at BC r 9 -> 120 px (36 f) and 8 white dots r 6 -> 80 (27 f); 12 #ffe070 dots burst (speed 3.4 px/f, gravity 0.05, life 54) |
| DONE | 1 | `battle_intro_done()`; upload the boss-effect strip 4 (SHINE pair + common, 2 KB) and palette 4 (MON_117's); `top_dirty = 1` |

* While the intro runs: `draw_top()` hides the boss name/HP bar (extend the `!warn_t` test with `!fx_intro_on()`),
  `overlay_sprites()` skips the boss (extend `if (a->boss && warn_t)`), the battle loop ignores START, Y/X/A/B/R
  (the core is in PH_INTRO anyway) and only forwards A/B to the talk.
* OAM: king 6 large + orbs <= 4 x 4 small + trails 8 + rings 8 = <= 38 entries; the hero and the panel sprites are
  the only other sprites at that time.
* NMI: the king's 6 KB goes in 2-KB entries during WALK (the king is still off-screen at x > 256 for the first
  ~15 frames); palette swaps are full CGRAM uploads (512 B) on at most one frame in 4.

### 2.3 Data conversion
* `make_gfx.py menu_obj()`: nothing to change for `KING_OBJ/KING_PAL`. Add `intro_fx()` (orbs drawn in FX_PAL
  colours; the prototype in `scripts/proto_fx.py` draws them: 4 x 16x16 = 512 B raw, 333 B LZSS; with the 2 small
  orbs and 4 sparkle tiles 22 tiles ~ 450 B) and `KING_PAL_FX` (5 x 96 B raw = 480 B).
* No text changes.

### 2.4 Risks
* The king is the story chibi at its own pixel size (64x96, 79 px tall) while the hero is scaled 0.73 (47 px):
  bigger than in the web (79/64 = 1.23 x the hero there, 1.68 x here). Pekorata herself is 96 px tall, so it reads
  as "the king grows into Pekorata". If the test shot looks wrong, add a 0.73-scaled king (2x2 cells, ~2 KB) later.
* The talk window (rows 2-12) covers most of the king during the talk (same in the web).
* Mirage wave 18 also plays the intro (web behaviour). The mirage autosave/suspend (another group) must treat
  PH_INTRO like PH_WARN (not suspendable).

### 2.5 Test plan
1. `mksave.py t/secret.srm --open 15 --clear-all --ending` (cursor starts on 月見の島), script: `wait 200, press A`
   (title), `wait 120, press A` (area), `wait 40, press A` (stage), `wait 40, press A` (出撃), then shots every
   10 frames for 300 frames: the king walks in, no boss on BG1, no boss bar.
2. After the talk (press A at frame +260, then again after 20 frames): shots every 10 frames for 420 frames:
   WARNING banner, red king, 4 orbs orbiting (front/back), spin, white flash, Pekorata appears with mosaic.
3. WRAM: `_b_phase` (address from `build/kishidra.map`) == PH_INTRO until DONE, then PH_FIGHT; `_b_talk_text`
   was `TALK_START` once and the core never requested a second talk.
4. `textcheck.py` on the script (the talk box and the WARNING banner); `prof.py` over the orbit state: no lag frame.
5. Mirage: `--clear-all` save, debug-open, run the tower with the PROFILE hook SELECT+R to wave 18: the intro plays.

---------------------------------------------------------------------------------------------------

## 3. Item 2: Pekorata idle poses (bored, sit, sleep, pet, stun) and the sleep rule

### 3.1 Web rules (battle.gd `_rest_update`, `rest_time`, `_pet_tap`, `_pet_update`, `_wake`, `_sync_all`, `_set_idle`;
battle_core.gd `boss_guard`; tuning.gd)

* `bored_t` grows while `core.can_command()` (phase FIGHT, player's turn, hero NORMAL) and no talk is shown and no
  hold; otherwise 0. **Any key or touch press sets it to 0** (`_input`).
* Times: `ENEMY_BORED_TIME` 10.0 s; for MON_117 `rest_time()` returns `ENEMY_SIT_TIME` **60.0 s** and
  `ENEMY_SLEEP_TIME` **120.0 s** (`PEKO_SIT_TIME` 30 / `PEKO_SLEEP_TIME` 60 are only for the underground/sea/sky
  rabbits `pekorate/pecorana/pecorano`; the secret-boss auto test checks 60/120 s for 兎騎士ペコラータ).
* `_rest_update`: no `sit` pose, dying, gone, stunned or phase != FIGHT -> rest = "". rest == "sleep" is sticky: it
  ends only when the enemy turn starts (`_wake`) or she is hit (`_on_number` -> `_wake`). Otherwise
  bored_t >= 120 s -> "sleep", >= 60 s -> "sit", else "". `e.asleep = rest == "sleep"`.
* **`boss_guard`: `if e.asleep or e.no_guard: e.miracle_next = false; return ""`** -> while asleep no hit of the
  skill is guarded (all hits of one skill are resolved at once) and her first-hit MIRACLE is lost.
* Zzz: while asleep and showing sleep/sleep2, a "Z" (#dfe8ff, 15 px) every `ENEMY_ZZZ_EVERY` 1.3 s (first after
  0.4 s) from `top + (0.12 w, 0.18 h)`: fades in 0.3 s, grows 0.55 -> 1.25, moves +(24, -38) in 1.8 s, fades out
  from 1.1 s.
* `_wake`: rest = "", `wake` pose for `ENEMY_WAKE_TIME` 0.6 s, asleep = false, popup **「!」** #ffe060 size 26 at
  `top + (0.2 w, 0.08 h)` (pops from scale 1.7, rises 18 px in 0.5 s, fades).
* Pet (`_pet_tap`, a tap on her body): only `AWAKEN_MON` (MON_117), not dying/gone, rest != "sleep", not already
  petted. Taps within `ENEMY_PET_GAP` 0.8 s count up; at `ENEMY_PET_TAPS` **10** -> pet for `ENEMY_PET_TIME`
  3.0 s (frames pet/pet2 0.5 s each, entry tween 0.2 s), a heart (#ff7aa8, outline #b0285a, 18 px) every
  `ENEMY_PET_HEART_EVERY` 0.3 s from `top + (+-0.25 w, 0.1 h)` rising 40 px in 1.1 s; then `pet_end` 0.6 s.
  Petting stops if she attacks, holds, is hurt/guards, wakes, dies. The tap also targets her and resets bored_t.
* Pose priority (`_sync_all`): defeat > windup > repel > attack/special (while attacking) > **stun** (stun_turns >
  0: frames stun/stun2 0.5 s each) > **wake** > guard (0.3 s after a blocked hit) > hurt (0.3 s) > **pet** >
  **pet_end** > **sleep** (sleep/sleep2 1.1 s each, entry tween 1.2 s) > **sit** (tween 0.3 s) > **bored**
  (bored_t >= 10 s and not stunned; bored/bored2 0.45 s each, tween 0.16 s) > normal (normal/idle2 breathing).

### 3.2 Measured art (port's own converter, `scripts/measure_peko.py`)

`assets/mobs_hd/extra.json` mon_117 has 11 extra poses. Converted with `Gfx.boss_set` (BG1 tiles + map, LZSS,
2 shared BG palettes 6-7, SCALE 0.73):

| pose | tiles W x H | tiles | anchor | stored T + M bytes |
|---|---|---|---|---|
| bored / bored2 | 7 x 12 | 68 / 68 | (29, 89) | 2046+168 / 2049+168 |
| sit | 9 x 9 | 62 | (42, 67) | 1730+153 |
| sleep / sleep2 | 9 x 9 | 62 / 61 | (41, 65) | 1670+156 / 1652+156 |
| pet / pet2 | 7 x 11 | 63 / 68 | (28, 84) / (29, 86) | 1947+154 / 2062+154 |
| pet_end | 7 x 13 | 72 | (30, 96) | 2081+179 |
| stun / stun2 | 14 x 9 | 60 / 61 | (50, 61) | 1645+161 / 1679+161 |
| wake | 8 x 13 | 83 | (33, 97) | 2317+202 |
| **11 poses** | | max 83 (limit 224 per buffer) | | **22,690 B (22.2 KB)** + descriptor +121 B |

All fit the BG1 buffers (BOSS_TILE_A 64-287 / B 288-511 = 224 tiles each), the `boss_map` buffer (20 x 16) and the
field rows (stun: x 122..234). **Palette:** fit the two palettes on the 7 existing poses only and map the new poses
to them: measured colour error is lower for every pose (e.g. normal 1206 vs 1416, sit 941 vs 1055) than refitting on
all 18, and the existing poses stay bit-identical. Tween poses (`tween_*`) are not converted.

### 3.3 SFC design

**Data (`make_gfx.py`)**
* `POSE_CODE` += `bored 7, bored2 8, sit 9, sleep 10, sleep2 11, pet 12, pet2 13, pet_end 14, stun 15, stun2 16,
  wake 17`.
* `monsters()`: `EXTRA = {'mon_117': ['bored','bored2','sit','sleep','sleep2','pet','pet2','pet_end','stun','stun2','wake']}`;
  `want = base poses + EXTRA.get(key, [])`.
* `boss_set(name, poses, fit_n=None)`: build `blocks` only from `poses[:fit_n]` (fit_n = number of base poses).

**View (`battle_view.c`)**
* `bdesc[96]` -> `bdesc[200]` (1 + 11 x 18 = 199 B). `Actor.off[8], code[8]` -> `[18]`, `pcode[7]` -> `[18]`;
  `parse_desc` loop limit 18 for bosses; `pose_fill` loops codes 0..17; `pose_find` falls back to 0 for codes >= 7.
  Add `has_code(a, c)` = `a->code[a->pcode[c]] == c`. (+123 B WRAM; struct size changes, still indexed by pointer
  walks in hot loops.)
* New per-actor `guard_t` (u8): on `EV_NUMBER` with `block = (e->b >> 2) & 3` != 0 and the boss `has_code(6)`:
  `guard_t = 18` instead of `hurt_t` (web: guard pose for a blocked hit; remove the dead `e->awake && 0` line).
* Boss pose choice in `animate()` (replace the boss branch):
  `dying/gone -> 4; atk_t||dx -> 2 or 5; else { r = fx_rest_pose(i); if (r != 255) want = r; else if hurt_t -> 3;
  else idle (normal/idle2) }` where `fx_rest_pose()` implements the web priority from "stun" down (stun, wake,
  guard, hurt are passed in) and returns 255 for "normal".

**Rules (`battle_fx.c`, `fx_rest_pose` / `fx_frame` / `fx_number`)** - one boss at a time, scalars:
`u16 bored_t; u8 rest, wake_t, zzz_t, pet_t, pet_end_t, pet_taps, pet_gap, heart_t, frame_k;`
* bored_t: `++` (saturate) when `battle_can_command() && !talk_open && !modal_on && !pad_new`; `= 0` when
  `pad_new` or not commandable or a talk is open.
* Thresholds: bored 600 f, sit 3600 f, sleep 7200 f (only for a boss that `has_code(9)`, i.e. MON_117).
* rest == SLEEP and `b_turn == TURN_ENEMY` -> wake. `fx_number(enemy, block)` (from EV_NUMBER, kind PK_HIT): if rest
  == SLEEP -> wake.
* `b_e[i].asleep = rest == SLEEP` (new `u8 asleep` in `Foe`, battle.h; `make_enemy` clears it via memset).
* **`battle_hit.c boss_guard()`: first line `if (e->asleep) { e->miracle_next = 0; return BLK_NONE; }`**.
* wake: `wake_t = 36`, "!" sprite (tiles 24 over 31 = 8x16, palette 4 index 15) at `top + (0.2 w, 0.08 h)`, rising
  13 px over 30 f, hidden after 45 f.
* Zzz: while asleep and the shown pose is code 10/11: first Z after 24 f, then every 78 f; Z (tile 23) moves
  +(18, -28) px over 108 f (sine ease-out from the 64-entry table), blinks out over its last 24 f.
* Pet: **L button** (unused in battle; README controls: Y/X/A skills, B guard, R meat, LR-arrows target, START):
  `if (pad_new & BTN_L)` and `b_phase == PH_FIGHT` and the target is this boss and `mon == MON_117` and not
  dying/gone and rest != SLEEP and `pet_t == 0`: `pet_taps = pet_gap ? pet_taps + 1 : 1; pet_gap = 48;` at 10 ->
  `pet_t = 180, pet_end_t = 0, heart_t = 0`. Each frame `pet_gap--`, `pet_end_t--`; while `pet_t`: cancel if
  attacking/hurt/guard/wake/dying, else `--pet_t`, a heart (tile 22) every 18 f from `top + (rnd(-w/4..w/4), h/10)`,
  rising 29 px, drifting +-7 px over 66 f, blinking out from 39 f; when `pet_t` reaches 0 -> `pet_end_t = 36`.
  Pekorata is the only enemy on 月見の島 (escort max 0) and at mirage wave 18, so she is always the target.
* Frame alternation: stun/stun2 30 f, pet/pet2 30 f, sleep/sleep2 66 f, bored/bored2 27 f (each change is one boss
  pose upload: LZSS ~1.7-2.3 KB unpack + 1-2 NMI frames, already the path for normal/idle2).
* Hero hits while sitting: pressing any key resets bored_t, so she stands up at once (web).

**OAM/VRAM:** Z, "!" and hearts are small sprites from the strip-4 common tiles, palette 4 (MON_117's palette
carries the Z/heart/yellow roles): at most 2 Z + 3 hearts + "!" (2) alive = 7 sprites.

### 3.4 Save fields: none.

### 3.5 Risks
* Pose uploads: each alternation unpacks LZSS (the profile shows `lz` at 5.8 % of the attack frames). At most one
  change per 27 frames; fine.
* GFX bank space: 22.3 KB of the ~56 KB free inside `GFX_BANKS`, which other groups also need (shared need below).
* `Actor` grows from 56 to 97 bytes (`act[]` indexing is a multiply in cc65; hot loops already walk pointers).

### 3.6 Test plan
1. Secret-boss script (2.5) to PH_FIGHT, close the talk, then no input: shots at +590 / +620 (normal -> bored),
   +3610 (sit), +7210 and +7300 (sleep, sleep2, a Z on screen). WRAM: `_b_e` + offset of `asleep` == 1 after 7200 f.
2. Then `press Y` (attack): her HP (`_b_e` hp, u32) drops on the first hit (normally the first hit on MON_117 is a
   MIRACLE with 0 damage), `miracle_next` == 0, the next shots show `wake` and the "!", then `normal`.
3. Fresh battle: `press L` x 9 every 6 frames -> no pet; the 10th -> shots +10, +40 show pet/pet2 and hearts; +200
   shows pet_end, +240 normal. `press L` x 10 with 60-frame gaps -> no pet (gap 48).
4. Sleep, then wait for her turn without attacking (press Y with a buff skill or let the count run): wake pose
   at the enemy turn.
5. Stun her (a stun-heavy loadout via `mksave --items`): stun/stun2 alternate; the HUD star follows `boss_top`.
6. `prof.py` over 300 frames of sleep with Z: no lag added.

---------------------------------------------------------------------------------------------------

## 4. Item 3: Boss special-move effects (24 kinds)

### 4.1 Web rules (battle.gd `_boss_special_fx` and helpers, tuning.gd `BOSS_FX`)

* At `enemy_bang` with a special coming: the special name banner (existing in the port) and **the attacker flashes
  #ff4020 for 0.5 s, strength 0.6**. At `enemy_windup` with `sp_name` in `STREAK_SPECIALS`
  (**「シャイニングレイ」**): `_streak_flash()` (a horizontal glowing streak across the field middle, 0.7 s). At
  `enemy_strike` with a special: `_boss_special_fx(f, e.sp_fx)`, and for the streak special `_shine_cover()` (the
  streak spreads to cover the field: spread 0.24 s, hold 0.1 s, fade 0.6 s, peak 0.9, then shake 7).
* `sp_fx` = `BOSS_SP_TABLE` fx without `FX_BOSS_`, else by race RC_DRAGON BREATH, RC_HUMAN GIGA, RC_MAOU DARKRAY,
  default BREATH; MON_115 ミミッくん in normal play (not classic) uses `MEAT_ASK_FX` **CHOMP**
  (`make_data.py` writes SCYTHE for it today; override in the view). The port already stores the index as
  `Foe.sp_fx` (BFX_* in gdata.h) but nothing reads it; today a boss special plays SE_BREATH at windup.
* `BOSS_FX` (23 keys) -> 23 visuals, plus the default `breath` for an fx key that is not in the table = **24 kinds**.
* SEs: the effect plays its own SE at the strike (port: `SE_BREATH`, `SE_HAMMER` for hammer1-4, `SE_SPEAR` for
  spear1-4, `SE_SLASH` for slash2/3). Move the port's windup `SE_BREATH` to the strike.

### 4.2 The 24 kinds: web look -> SFC recipe

Times are frames after the strike; "dots" = small dot tiles 0-3 fading by tile; "ring n" = n arc sprites on a
RADIAL circle (8 arcs unless said), radius eased out-cubic; FLASH = `cm_req` fixed colour (5-bit R,G,B) fading
linearly to 0, on the field lines unless "all"; SUB = subtract mode. Large cells come from the boss's "pair".

| # | key (colour) / used by | web look | SFC recipe | peak sprites |
|---|---|---|---|---|
| 1 | BREATH -> `fire` (#ff9020) / MON_090 炎龍ゴーゴモス イグニスブレス | 150 fire particles mouth -> hero in 0.55 s (speed 400-560, spread 20); at 0.24 s 70 rising flames at H+(0,30), burst 28, field flash (1,0.45,0.1,0.3) 0.25 s, shake 9; SE breath | f0-11 stream 12 dots M->H (1/frame, 5.5 px/f, +-0.4 px/f jitter, life 18, tiles dot4->dot3->puffS->dotDark); f14 rise 6 flames (accent ramp) HF+-22 px, vy -2 px/f, acc -0.06, life 30; burst 6 dots at H (3.4 px/f, gravity 0.05, life 27); FLASH (9,4,1) 15 f; shake 6 px 18 f; SE_BREATH | 16 |
| 2 | AQUA -> `bubbles` (#40a8ff) / MON_091 水龍ピチャルス アクアブレス | 18 wobbling bubbles mouth -> hero 0.55 s; at 0.4 s splash 46 drops at H+(0,24), ring 10->90, shake 7 | f0 cell BUBBLES M->H 33 f; f0-21 8 bubbles (bubbleM/S) every 3 f M->H, 4 px/f, sideways sine wobble (+-6 px) ; f24 burst 10 drops at H+(0,18) up (3-6 px/f, +-40 deg, gravity 0.18, life 45); ring at H r 7->66 (24 f); shake 5; SE_BREATH | 14+1L |
| 3 | GLOOM -> `fog` (#9a50e0) / MON_094 闇龍ヤミジャス グルームブレス | 44 dark puffs mouth -> hero, slowing; at 0.36 s 30 puffs swirling around H, field tint (0.1,0.02,0.18,0.5) 0.7 s, ring 100->14 (closing), shake 7; at 0.7 s burst 26 | f0-15 8 puffs (puffM/puffS dark) every 2 f M->H, 4 px/f with drag; f22 cell PUFF at H (26 f); orbit 8 puffS around H r 29->20, 0.6 turn, 48 f; FLASH SUB (9,11,7) 42 f; closing ring r 73->10 (27 f); shake 5; f42 burst 8 light dots; SE_BREATH | 16+1L |
| 4 | GUST -> `gust` (#e0f4ff) / MON_089 翼龍ドラゴス ウイングガスト | 4 rows x 4 long horizontal slashes sweeping from the boss to the hero, 40 wind streaks; at 0.24 s orbit of 10 leaves r 80->24, burst 20, shake 6 | f0, 2, 4, 6: cell SLASH_H per row (y = H.y -37/-12/+12/+37) moving left from BC.x-22 to H.x in 12 f; f0-9 8 streakH particles leftward 8 px/f, y random +-50, life 20; f14 orbit 8 leaves at H r 58->18, 1.8 turns, 27 f; burst 8 dots; shake 4; SE_BREATH | 16+4L |
| 5 | STORM -> `storm` (#60d070) / MON_092 樹龍ワシャラス グリーンストーム | orbit 16 leaves r 130->18 (1.5 turns, 0.5 s) + 10 darker r 95->12 (-1.2 turns); at 0.46 s burst 34, ring 10->120, shake 7 | f0 orbit 10 leaves r 95->13, 1.5 turns, 30 f; orbit 6 leaves r 69->9, -1.2 turns, 27 f; f28 burst 10 dots (3.6 px/f); ring 7->88 (27 f); shake 5; SE_BREATH | 16 |
| 6 | BOLT -> `bolt` (#fff070) / MON_093 光龍キラノス サンダーボルト | 3 lightning bolts from the field top to the hero at x -26/+22/0, 0.12 s apart, each 0.22 s with a field flash (1,1,0.8,0.35) 0.1 s, burst 14, shake 5 | for i = 0..2 at f 0, 7, 14: 3 BOLT cells stacked from y 16 to H.y+4 at x = H.x + (-19, 16, 0) (top cell +16 px, middle +8: leaning), H-flip toggles every 3 f, life 13; FLASH (11,11,9) 6 f; burst 6 dots at the foot (life 21); shake 4; SE_BREATH | 12+6L (2 per line) |
| 7 | QUAKE -> `quake` (#c89860) / MON_095 鈍龍タトルノドン グランドスラム | 7 rock spikes at the feet (0.55 s), 3 flat rings (y x 0.3) r 10->170, 26 dust puffs rising, burst 26, shake **16**; SE hammer | f0 cells SPIKE x 3 at HF + (-36,-16), (0,-20), (36,-16): rise 16 px in 6 f, hold, sink 10 f, life 33; flat rings (y x 0.3) r 7->124 at f 0, 6, 12 (27 f each); rise 6 dust puffM over +-51 px (vy -0.5, life 51); burst 8 shards (4 px/f, gravity 0.05, life 42); shake **12 px**, 18 f; SE_HAMMER | 16+3L |
| 8 | MARINE -> `marine` (#40a0ff) / MON_096 海龍オシャルシアス マリンブラスト | a water beam mouth -> hero (width 26, grows in 0.08 s, 0.5 s), burst, ring, splash 60, two flat ripples | f0 BEAM: slanted band M->H, half-thickness 9 px, ch-5 window per line, add (4,12,30), grows from M to H in 5 f, holds to f18, thins to 0 by f30; cell GLOW at H f0-30; f7 burst 8 dots, ring 7->66, shake 6; f8 burst 10 drops at H+(0,15) up; flat ring at HF r 9->102 (30 f); f10 flat ring r 9->139 (37 f); SE_BREATH | 16+1L |
| 9 | GLORY -> `pillar` (#ffd860) / MON_098 光龍ゴルデピカライア グロリアスレイ | a pillar of light from the top onto the hero (width 56, 0.6 s), 34 falling sparks, full-screen flash (1,0.95,0.7,0.5) 0.3 s, flat ring, burst 26, shake 8 | f0 BAND rect x H.x+-20, lines 16..HF.y, add (22,19,8), grows down 6 f, fades f21-36; 8 sparkle dots falling from y 18 (x +-26, 2.5-4.3 px/f, life 54); f6 FLASH all (15,14,10) 18 f (overrides the band, which resumes); flat ring at HF r 7->117 (33 f); burst 8 dots; shake 6; SE_BREATH | 16 |
| 10 | MAGMA -> `meteor` (#ff6a20) / MON_097 熱龍マグードワイバル マグマレイン | 4 meteors falling (0.26 s each, 0.12 s apart) at x -30/+26/0/+40, each: burst 18, ring 6->50, 18 flames, shake 5 | for i 0..3 at f 7i: cell FIREBALL from (end.x+80, end.y-175) to end = (H.x + (-22,19,0,29)[i], H.y+7) in 16 f (ease-in k^2); on arrival: ring **4** arcs r 4->37 (18 f), burst 6 dots, rise 3 flames at end+(0,10), shake 4; SE_BREATH | 16+2L |
| 11 | ARMA -> `arma` (#e02040) / MON_119 魔王最終形態 アルマゲドン | darken 0.5 for 1.1 s, 6 big meteors 0.09 s apart, then full-screen flash (1,0.3,0.2,0.55) 0.35 s, ring 10->260, shake 14 | f0 DARKEN SUB (15,15,15) in 9 f, hold to f75, out by f96; meteors as #10 x 6 at f 5i, x (-22,19,0,29,-32,9); f52 FLASH all (17,5,3) 21 f (wins over the darken while it lasts); ring at H r 7->190 (36 f); shake 10 px; SE_BREATH | 16+2L |
| 12 | MULTI -> `multi` (#b070ff) / MON_099 奇龍ナーゾキーモス マルチブレス | 5 coloured streams (ff7030, 60d0ff, ffe040, 70e060, b070ff) from 5 points of the body, 0.06 s apart; at 0.5 s 5 bursts of 8 + white ring + shake 10 | MULTI's "pair" area holds 10 small dot tiles drawn in FX_PAL indices (FX_PAL has all five colours); for i 0..4 at f 4i: 3 dots every 2 f from (x0 + w(0.1+0.07i), y0 + h(0.12+0.12i)) to H, 5.5 px/f, life 26, palette 5; f30 burst 10 dots (2 per colour) at H; ring 7->80 (27 f, kind palette); shake 7; SE_BREATH | 15+0L |
| 13 | ROAR -> `roar` (#e0e8f0) / MON_100 魔王のペット ギガロア | 3 big shock rings from the boss's head r 20->300, 0.12 s apart, each shake 10; field flash white 0.3 for 0.15 s; SE hammer | f0, 7, 14: ring **12** arcs at BC+(-29,-15) r 15->219 (33 f) (arcs leave the screen); shake 7 each; f0 FLASH (9,9,9) 9 f; SE_HAMMER. (Optional extra: a BG2 per-line horizontal wobble through the ch-7 table for 30 f.) | 12-16 |
| 14 | DARKARC -> `wave` (#8a50e0) / MON_102 魔龍ドットアーク ダークアーク | a dark crescent flies from the boss to the hero in 0.3 s (size 80); then burst 28, ring, field tint (darkened colour, 0.35) 0.2 s, shake 9; SE slash2 | f0 cells CRESCENT (upper) + CRESCENT V-flipped (lower) = 32x64 crescent from BC+(-44,0) to H in 18 f; f18 burst 8 dots, ring 7->80 (27 f), FLASH SUB (6,8,3) 12 f, shake 7; SE_SLASH | 8+2L |
| 15 | SOUL -> `soul` (#a0d8ff) / MON_113 闇のダルクネス ソウルサイス | scythe: 12 slashes on an arc (r 96 around H+(52,0), -100 deg -> -260 deg, 0.02 s apart), at 0.14 s field tint, ring 10->120, burst 32, shake 8; wisps: 3 x 6 particles rising 170 px from H | SCYTHE: 5 SLASH_D/SLASH_H cells (flip per point) at f 0, 2, 4, 6, 8 on the points below, each shown 6 f (<= 3 at once); f8 FLASH SUB (4,3,2) 12 f, ring 7->88 (30 f), burst 8 dots, shake 6; wisps: f13, 18, 23 two wisp tiles each from H+(-19+19i, 10) rising 3.8 px/f, life 45; SE_SLASH | 14+3L |
| 16 | HELL -> `hell` (#9030d0) / MON_101 魔王 ヘルサイス | scythe + 3 particle pillars at the feet (x -34/0/+34, 0.07 s apart, 44 fast upward sparks each), each shake 5 | SCYTHE as #15; f12, 16, 20: rise 4 dots from HF+(-25+25i, 0), vy -4.5 px/f with drag, life 33; shake 4 each; SE_SLASH | 12+3L |
| 17 | CROSS -> `cross` (#e03050) / MON_118 魔王第2形態 ダブルサイス | scythe, then a mirrored scythe 0.16 s later | SCYTHE as #15 (with its flash/ring/burst at f8), mirrored SCYTHE at f10 (its flash/ring at f18); SE_SLASH twice | 14+3L |
| 18 | SCYTHE -> `scythe` (#b050ff) / MON_115 in classic mode only | the scythe alone | SCYTHE as #15 without wisps | 10+3L |
| 19 | CHOMP -> `chomp` (#ffb07a) / MON_115 ミミッくん お肉ちょーだい！ (normal play) | big jaws (5 teeth, 116 wide) close on the hero (gap 84 -> 10 in 0.13 s), ring, burst 20, shake 7, SE hammer; hold 0.12 s; fade 0.2 s | JAW cells: upper 2 cells side by side, lower 2 V-flipped (+8 px so the teeth interleave), gap 61 -> 7 px in 8 f (k^2); f8 ring 7->66 (24 f), burst 7 dots, shake 5, SE_HAMMER; hold 7 f; blink out over 12 f | 7+4L |
| 20 | DIVE -> `dive` (#6080e0) / MON_114 龍騎士マーゴス ドラゴンダイブ | 3 long slashes along the dive line from above the boss to the hero (0.05 s apart); at 0.18 s full-screen flash white 0.45 0.2 s, burst 30, ring, shake 12; SE spear | f0, 3, 6: 2 SLASH_D cells end to end at lerp(BC+(22,-124), H, 0.3/0.6/0.9), life 8 f each; f11 FLASH all (14,14,14) 12 f; burst 8 dots, ring 7->88 (27 f); shake 9 px; SE_SPEAR | 8+4L |
| 21 | SHINE -> `shine` (#ffe070) / MON_117 兎騎士ペコラータ シャイニングレイ | windup streak; strike: 6 directions x 4 converging ray slashes, closing ring 96->8, burst 30, white ring, full-screen flash (1,1,0.85,0.6) 0.3 s, boss flash, shake 8; plus the shine cover | windup (`fx_windup`, 12 f + into the strike): STREAK band, lines 72+-8, add up to (31,28,14), the window x range grows from the centre to full width in 7 f (ease-out), thickness +-1 px wobble every 4 f, fades f17-42. Strike: SHINE COVER: band lines from 72+-8 to 16..127 in 14 f (ease-out cubic), hold 6 f, fade 36 f, add (27,26,25) x 0.9; shake 5 at f14. Rays: 4 SLASH_D (diagonals, flips) + 2 SLASH_H converge from r 110 to 20 in 12 f (ease-in); f12 closing ring 70->6 (15 f), burst 8 dots, white ring 6->80 (24 f); the cover's colour math also stands for the web's full flash; SE_BREATH | 12+6L (3 per line) |
| 22 | DARKRAY -> `rays` (#b060ff) / RC_MAOU default, unused on the surface | as shine without the light: final field tint (0.3,0.05,0.45,0.4) 0.25 s, the boss flashes the colour | rays as #21 (no streak/cover); f12 FLASH SUB (6,9,3) 15 f; boss tint (BG1 add (11,6,16), 18 f); SE_BREATH | 12+6L |
| 23 | GIGA -> `giga` (#e0ecff) / human mid-bosses MON_107/109/111/112/116 ギガスラスト | 3 horizontal slashes from the attacker toward the hero (0.35/0.6/0.85 of the way, 0.05 s apart); at 0.16 s ring, burst 18, shake 8; SE spear | strip 4 may hold a zako: use strip 7 + FX_PAL only. f0, 3, 6: FX cell 2 (thrust) at lerp(A.x-29, H.x, 0.35/0.6/0.85), y = H.y, moving left 4 px/f, life 8; f10 FX cell 0 (star burst) and cell 3 (impact) at H for 12 f; 6 misc star tiles (MISC_BAR+10) radial r 4->40; shake 6; SE_SPEAR | 6+3L |
| 24 | (not a key) -> `breath` (#ff9020) / default, unused on the surface | 90 particles mouth -> hero 0.55 s, shake 8 at once; SE breath | f0-11 stream 12 dots M->H, 5.5 px/f, life 24; shake 6 at f0; SE_BREATH | 12 |

Plus for every special (not just bosses with these kinds):
* **Bang red flash** (`fx_bang`, EV_ENEMY_BANG with `a = foe_special_next(e)` added to the post): for a boss,
  BG1-only add (19,5,0) fading over 30 f (`cm_req` prio 1, CGADSUB $01). Not for zako (palettes 2-4 cannot take OBJ
  colour math).
* The boss's lunge (dx -16) and the hit result banners stay as today.

Scythe points (computed for this spec from the web formula, centre c = H + (38, 0) = (93, 98), r 70 x (1, 0.8),
angles -100, -125, -150, -175, -200 deg, kept inside the field): (81, 29), (53, 41), (32, 70), (23, 93), (27, 117);
cell per point: SLASH_H, SLASH_D (H-flip), SLASH_D (H-flip), SLASH_H, SLASH_D. Mirrored (CROSS): c = (17, 98),
x' = 2 c.x - x: (29, 29), (57, 41), (78, 70), (87, 93), (83, 117), flips mirrored.

Ring arc tiles: 8 directions use 3 tiles with flips (E/W = side arc, N/S = top arc, diagonals = diagonal arc).

### 4.3 Data conversion

`make_gfx.py` new `boss_fx()` (prototype: `scripts/proto_fx.py`, previews `out/bfx_all.png`), measured with
`snesgfx.tile4` + `lzss`:

| asset | content | raw | stored |
|---|---|---|---|
| `BFX_SMALL` | 32 common 8x8 tiles (dot1-4, dot dark, spark, star, ember, 3 ring arcs, 2 streaks, drop, 2 bubbles, 2 puffs, 3 flames, leaf, heart, Z, "!" top, glow core, spike, wisp, ray, shard, haze, "!" bottom) | 1024 | 475 (LZSS) |
| `BFX_CELL_*` | 10 large cells: SLASH_D, SLASH_H, CRESCENT, BOLT, FIREBALL, SPIKE, JAW, PUFF, BUBBLES, GLOW | 10 x 512 | 1846 (LZSS) |
| `BFX_MULTI` | 10 dot tiles in FX_PAL indices (MULTI's pair area) | 320 | ~200 |
| `BFX_PAL_n` | one 15-colour palette per boss (+ GIGA + default), built from the `BOSS_FX` colour: ramp, white, cream, accent ramp (fire: fff0b0/ff9020/8c1408; magma/arma: ffe0a0/ff6a20/8c1408; MON_114: the dark-aura reds ff1a3a/c8102a/3a0626), Z/heart/yellow | 24 x 32 = 768 | 768 (raw) |
| `BFX_REC_n` | the recipe of each kind (steps of 10 B: t, op, anchor, dx, dy, n, speed, life, style, arg), read with `copy_asset` into a 128-B buffer at the strike | 24 x ~70 | ~1.7 KB |

`make_data.py`: write the recipes from a Python table `BFX_RECIPES` (the table in 4.2) and the boss -> palette /
pair / kind mapping (`bfx_of_mon[N_MON]`, 93 B, or fold it into the existing `enemy_rows`). MON_115 -> CHOMP.

### 4.4 C/asm changes

* `battle_fx.c`: the engine (1.3, 1.4) and the recipe interpreter. Ops: SE, SHAKE(amp, frames), FLASH(area,
  colour, frames, sub), BAND(rect/beam/pillar/streak/cover, colour, frames, thickness), DARKEN, TINT_BOSS, STREAM,
  BURST, RISE, RING(r0, r1, frames, arcs, flat), ORBIT, CELL(from, to, frames, ease), BOLT, SCYTHE(mirror), JAWS,
  END. Anchors: HERO, FEET, MOUTH, BOSS, TOP, ATTACKER.
* `battle_view.c`: call `fx_battle_init()` in `battle_scene()`, `fx_stage()` after `load_stage_gfx()` and in
  EV_FORM_SWAP, `fx_bang/fx_windup/fx_strike` in the events (and drop the windup `SE_BREATH`), `fx_frame()` at the
  end of `animate()` (replacing the direct colour-math writes), `fx_sprites()` in `draw_sprites()` after the
  numbers. Shake: add `shake_amp` (today fixed +-2/+-1): `shake_x = (shake_t & 2 ? 1 : -1) * amp * shake_t / 18`.
* `battle_core.c`: EV_ENEMY_BANG gets `a = foe_special_next(e)`.
* `crt0.s`: the window shadows and HDMA ch 4/5 (1.3).

### 4.5 Budget during a boss attack

| resource | use |
|---|---|
| VRAM | OBJ strip 4 only (2 KB, $5000-$53FF word), loaded once per boss stage / form; 0 BG VRAM |
| CGRAM | OBJ palette 4 (32 B, once per stage) |
| OAM | <= 16 small + <= 4 large per effect (bolt, shine 6 large), from the dynamic pool 0-95 (a boss attack uses about 45 of them today: hero 8, shield 4, escort 4, overlays ~10, numbers <= 16) |
| scanline | <= 10 effect slivers per line |
| NMI bytes | 0 extra per frame (colour math and HDMA tables are not in the 4864-byte budget) |
| CPU | particles ~150 cycles each in C (16 -> 2400 cycles, ~4 % of a frame), a beam table build once (~2500 cycles); 38 % of the frame is idle at that time (measured) |
| WRAM | particles 336, HDMA tables 264, recipe buffer 128, state ~40 |

### 4.6 Risks
* Colour math is one state per scanline: overlapping flashes are serialized by priority (documented per recipe);
  an effect that wants add and subtract on the same lines takes the higher one.
* OBJ colour math only works on palettes 4-7: effect sprites are palette 4/5, fine; zako cannot be tinted.
* The shake moves BG1/BG2 only (as today); effect sprites stay put, like the web's fx layer.
* Strip-4 assumption: if another group puts a 4th actor or other sprites into strip 4 / palette 4 in boss stages,
  this breaks; keep a written OBJ allocation table (shared need).
* Sprite slivers: test every recipe with an escort present (castle) for missing slivers on the hero line.

### 4.7 Test plan
1. PROFILE build with the harness hooks (1.6). Mirage tower save (`mksave --open 15 --clear-all --ending`, then
   select the tower): for each wave 1-18 (the 18 surface bosses in the tower order MON_114, 089, 095, 096, 097, 092,
   093, 091, 115, 099, 098, 094, 113, 090, 101, 118, 119, 117): wait for the fight, `press SELECT`, shots at strike
   +2, +8, +16, +28, +44, +70; read `prof_marks[8..11]` (kind, particles, cm winner) and `$7F:DBFA` (NMI end line max)
   from `wram` dumps; then `press SELECT+R`, pick a mirage reward with A.
2. MON_100 ギガロア and MON_102 ダークアーク: castle save (`--open 14`), stages 2-9 with SELECT; GIGA: an area with a
   human mid-boss at stage 4 (王宮 areas), with 2-3 zako on screen (strip-7 fallback).
3. Escort case: a castle boss with an escort and a lucky monster: shots for missing slivers on the hero line.
4. `prof.py t/<boss>.txt 120` on each heavy recipe (fire, bolt, arma, shine): lag frames over the battle loop
   <= today's + 1 %; `nmi_vmax` not raised.
5. `textcheck.py` on the battle scripts (no text changes, but the banners must stay clean).
6. Compare each shot with the web's auto-test shots (`_auto_bossfx` takes one per boss) by eye.

---------------------------------------------------------------------------------------------------

## 5. Item 4: Dark aura on 龍騎士マーゴス (MON_114) in 魔王の城

### 5.1 Web rules (battle.gd `_spawn_stage`, ui/fighter.gd `set_dark_aura/aura_pulse/aura_level`, ui/dark_aura.gd)
* Only `core.area == "AR_CASTLE" and e.mon == DARK_AURA_BOSS` (params.json: **MON_114**, the boss of 魔王の城
  STAGE 1; not in the mirage tower, area AR_MIRAGE). The auto test checks "outside the castle: no dark aura".
* Body: shader tint toward `DARK_AURA_TINT` #140010 by `aura_level` = lerp(`DARK_AURA_TINT_MIN` 0.12,
  `DARK_AURA_TINT_MAX` 0.42, pulse); a red rim #ff1a3a (2 px) at `DARK_AURA_RIM_MAX` 0.75 x (0.45 + 0.55 pulse);
  pulse = triangle over `DARK_AURA_PERIOD` **64 frames** (params.json; gdata.h `D_DARK_AURA_PERIOD`).
* Particles from up to 40 spots on the outline of the normal pose (`_find_spots`): flames 120 (life 0.85 s, colour
  #c8102a -> #3a0626 -> transparent, rising), haze 40 (dark #0a000a, 1.4 s), smoke 14 in front (2.2 s), embers 16 in
  front (1.1 s, bright -> #c8102a).
* Floor circle under the boss: 3 dark ellipses (rx = max(0.62 w, 0.4 h), flat 0.26), two red rings (0.92 rx,
  0.74 rx) pulsing (`DARK_AURA_CIRCLE_PULSE` 1.6 s, alpha 0.45 + 0.4 pulse), two turning triangles and 12 turning
  dots (`DARK_AURA_CIRCLE_TURN` 9 s).
* On defeat: no new particles, the circle fades in 0.67 s, tint and rim off.
* params.json also has the original SFC-style numbers `DARK_AURA_LEVEL_MIN` 6, `_MAX` 20, `_R` 10, `_G` 2, `_B` 18
  and `DARK_ORB_EVERY` 5, `DARK_ORB_MAX` 8, `DARK_ORB_LIFE` 40 (unused by the web now; used below for the flames).

### 5.2 Measured geometry (`scripts/aura_spots.py`, the converted normal pose)
Normal pose 137 x 100 px, anchor (51, 99), body_w 137, body_h 100. 15 outline spots (dx, dy from the feet):
(-49,-68) (-38,-83) (-19,-96) (16,-97) (23,-97) (46,-89) (63,-78) (77,-60) (83,-37) (73,-16) (53,-23) (23,-17)
(-14,-17) (-27,-17) (-37,-58). Floor circle rx 85, ry 22 (centre at the feet x 172, y 127: only the upper half is
above the panel).

### 5.3 SFC design (`battle_fx.c`, `aura_*`)
* On: `fx_stage()` when `b_area == AR_CASTLE` and the boss is MON_114. Off: `fx_defeat()` / stage end.
* **Body tint pulse (palette)**: new asset `BOSS_086_AURA` = 8 versions of the boss's 2 BG palettes mixed toward
  #140010 by a_k = 0.12 + 0.30 k/7 (k = 0..7), made by `make_gfx.py` from the same palettes (8 x 64 B = 512 B ROM).
  Every frame: k = triangle(frame mod 64) x 7 / 32; when k changes (14 times per 64 frames) `load_pal(ASSET_
  BOSS_086_AURA + k, 96)`. Cost: one CGRAM upload (512 B of the NMI) every ~4.6 frames; `pose_tiles()` already
  reserves 512 B when `cgram_dirty`. (The red rim is not reproduced: the SFC has no per-pixel outline; the flames
  hug the outline instead.)
* **Flames (OBJ, palette 4 accent ramp ff1a3a/c8102a/3a0626, strip-4 tiles flame1/2/3)**, the params numbers: one
  flame every 5 frames at a random spot (+ boss x incl. lunge `dx`), at most 8 alive, life 40: vy -0.4 px/f, ay
  -0.02 (rises ~32 px), x drift +-0.2 px/f, tile flame1 (0-12), flame2 (13-26), flame3 (27-39).
* **Embers**: 4 alive, one every 16 f at a spot, vy -1.0 -> -1.6, tile ember, life 66.
* **Smoke** (front): 3 dark puffs (puffM/haze) in the rect (+-0.32 w, -0.3 h +- 0.22 h) around the body, vy -0.3,
  life 132 (2.2 s).
* **Floor ring (colour math on BG2 only, behind the boss for free)**: HDMA ch 5 per-line WH0..WH3 = outer ellipse
  (rx 78, ry 20) and inner ellipse (rx 63, ry 16) centred (172, 125), lines 105..127 (table made at stage start from a
  precomputed half-width list, asset `AURA_RING` ~100 B); WOBJSEL W1+W2 on, WOBJLOG XOR (ring), CGWSEL $10, CGADSUB
  $02 (BG2 only, add); COLDATA = #c8102a (25,2,5) x (0.45 + 0.4 pulse96): (11..21, 1..2, 2..4); `cm_req` prio 0 (any
  flash/WARNING/effect takes over for its frames). The boss (BG1) and the hero (OBJ) are not tinted, so the ring
  shows on the floor behind them. The dots, triangles and dark ellipses are left out.
* Defeat (`fx_defeat`): stop spawning, ring intensity -> 0 over 40 f, `load_pal` the normal boss palette.
* Totals: <= 15 small sprites, CPU ~2.2 k cycles/frame in C for the whole fight (asm loop -> ~0.8 k).

### 5.4 Risks
* The whole MON_114 fight carries the particle cost (not only one attack): measure with `prof.py`; move the loop to
  asm if lag rises above +1 %.
* Palette pulsing uses full CGRAM uploads; an optional crt0 change (upload only a dirty CGRAM range) would cut it to
  64 B per step and help every palette effect.
* The DIVE special's colour math overrides the ring for its frames (intended).

### 5.5 Test plan
1. `mksave --open 14 --clear-all` -> 魔王の城 STAGE 1 (MON_114): shots at 4 points of one 64-frame cycle (tint
   min/max differ), flames around the outline, the red ring on the floor; `prof_marks` particles <= 15.
2. Same boss in the mirage tower wave 1 (AR_MIRAGE): no aura (palette static, no flames, no ring).
3. Defeat MON_114 (SELECT+R): flames stop, the ring fades, normal palette.
4. `prof.py` 300 frames in the castle fight vs the same fight with the aura disabled: lag difference <= 1 %.
5. The DIVE special (SELECT) during the aura: the effect's flash wins, the ring returns afterwards.

---------------------------------------------------------------------------------------------------

## 6. Item 5: The hero's spin poses for ランページ (SP_RAMPAGE)

### 6.1 Web rules (battle.gd `ATTACK_POSES`, `_hero_pose`, `_on_attack`, `_on_number`; tuning.gd)
* `SP_RAMPAGE` (ランページ, the special of three キック in a row: `specials[12] = {39, 39, 39}`) has no
  `SPECIAL_MOTIONS` entry, so `_sp_start` fails and `_on_attack` sets `atk_wtype = "RAMPAGE"`:
  `hero.move_offset(Vector2(LUNGE 18, -RAMPAGE_JUMP 22), 0.1 s, quad ease-out)`, and after `RAMPAGE_AIR` **0.55 s**
  `move_offset(0, 0.18 s)`.
* Poses `ATTACK_POSES.RAMPAGE = [["spin_front", "spin_side"], ["spin_back", "spin_side"]]`: before the first hit
  `spin_front`; at each hit (`strike_t = 0, strike_k = hit index`): for `STRIKE_HOLD` 0.07 s, or when no hits are
  left, `pairs[k % 2][1]` = `spin_side`; then `pairs[(k+1) % 2][0]` = `spin_back` after hit 0, `spin_front` after
  hit 1, ... The weapon is not drawn in these poses (no `hand`), the shield is behind (hero.json shield z < 0).

### 6.2 Measured art (`scripts/measure_hero.py`, the port's `sprite_set` with the existing hero palette)
| pose | cells (32x32) | strips | tiles stored | extent rel. anchor |
|---|---|---|---|---|
| spin_front | 2 | 1 | 749 B | x -10..22, y -58..-7 |
| spin_side | 4 | 1 | 879 B | x -24..24, y -52..-10 |
| spin_back | 2 | 1 | 845 B | x -15..15, y -58..-7 |
| **total** | | | **2,473 B + 39 B descriptor = 2.5 KB** | |

The hero set today: 129 poses, 127,463 B, descriptor 1,990 B (buffer `hdesc[2600]` -> 2,029 B fits).

### 6.3 SFC design
* `make_gfx.py hero()`: append `('spin_front', 'spin_side', 'spin_back')` to `plain` (no weapon), and give
  `sprite_set` a `fit_n` (fit the hero palette on the existing poses only, so no other pose changes). Generates
  `HP_SPIN_FRONT/SIDE/BACK`, `N_HERO_POSE` 132, `hero_shield_pos` +3 rows (shield behind).
* `battle_view.c`:
  * state `rp_k` (hits seen), `rp_t` (frames since the last hit, 255 = none yet), `rp_f` (frames since the attack).
  * EV_ATTACK with `c == SP_RAMPAGE_IDX` (12; emit `#define SP_RAMPAGE 12` from make_data): `rp_k = 0, rp_t = 255,
    rp_f = 0`.
  * EV_NUMBER of the hero's hits (`(a >> 4) == PK_HIT`) while `last_attack_sp == 12`: `rp_t = 0; ++rp_k`.
  * `hp_attack()`: `if (sk == 12) { if (rp_t == 255) return HP_SPIN_FRONT; if (rp_t < 4 || (!b_npending &&
    !b_npopups)) return HP_SPIN_SIDE; return (rp_k & 1) ? HP_SPIN_BACK : HP_SPIN_FRONT; }` (rp_k counts from 1:
    after hit 1 -> back, after hit 2 -> front, as the web's k = 0, 1). `animate()`: `if (rp_t < 255) ++rp_t;`.
  * Jump/lunge: const table `rp_off[45][2]` (s8 dx, dy; 90 B RODATA): frames 0-5 dx 0 -> 13 px, dy 0 -> -16 px
    (quad ease-out, LUNGE 18 x 0.73, RAMPAGE_JUMP 22 x 0.73); 6-32 hold; 33-43 back to 0 (0.18 s, ease-out); apply
    while `rp_f < 44` to the hero's x and y in `draw_sprites()` (and the shield, which follows `hx`); `++rp_f`.
* Hits: 3 popups at `MULTI_HIT_INTERVAL` 6 f within the 24-frame attack motion, so the three spins show.

### 6.4 Risks
* The hero's GFX share of the GFX banks (2.5 KB). The +-16 px jump keeps the hero inside the field (top ~49 px).
* If the hero's attack motion ends before the last hit (b_hero_timer 24 f) the pose returns to idle a little early:
  same as the other specials in the port.

### 6.5 Test plan
1. `mksave.py t/kick.srm --open 1 --items 58:39 --main 2` (a MAIN weapon whose only skill is キック), any area:
   `press Y` three times with turns between (wait for the enemy turn each time); on the third (ランページ cut-in)
   shots at +1, +6, +10, +16, +22, +40: spin_front up in the air, side at each hit, back/front between, landing.
2. WRAM: OAM shadow `_oam` hero entries' y = FOOT_Y-based value - 16 during the hold.
3. Other specials and plain kicks unchanged (shots of a キック and a ジャンプギリ).

---------------------------------------------------------------------------------------------------

## 7. Measurements and scripts

* `scripts/measure_peko.py <work>`: the 11 Pekorata poses with `Gfx.boss_set` (two palette strategies + error).
* `scripts/measure_hero.py <work>`: the 3 spin poses with `Gfx.sprite_set` and the built hero palette.
* `scripts/proto_fx.py <work>`: the effect sheet art, LZSS sizes, palettes, orbs; previews in `out/`.
* `scripts/aura_spots.py <work>`: MON_114 outline spots and the floor-circle size.
* `t/p2.txt` + `prof.py`: CPU profile of a Pekorata attack (38 % idle measured).
* Work copy: `scratchpad/plan/boss_fx/work` (unchanged sources; built with `python build.py rom`, 4 MB, ROM used
  3550.7 KB, WRAM C data 46.8 / 54 KB, code bank $85 27.2 / 32 KB, $86 empty).

## 8. Shared needs (other groups / the integrator)
1. **GFX bank space**: these items add ~32 KB to the asset banks; only ~56 KB are free inside `GFX_BANKS` and the
   mirage cards (77.6 KB) and wing flaps also need room -> the shared bank-map change (assets in code-bank tails or
   in the empty $8F/$9D-$9F) must land first.
2. **Battle OBJ / palette allocation table**: this spec takes OBJ strip 4 + palette 4 in boss stages, strips 2-4 +
   palettes 2-4 during the entrance, HDMA ch 4/5, the window registers and the colour-math arbiter; the lucky
   presentation, the battle chest row and info popups must not reuse them (or must go through `cm_req`).
3. **WRAM budget**: ~1.26 KB of the 7.2 KB headroom; C `const` tables cost WRAM (RODATA is copied to WRAM).
4. **battle_view.c bank $85** has 5.5 KB left; keep new battle presentation code in `battle_fx.c` (bank $86).
5. The harness-only PROFILE hooks (SELECT = special now, SELECT+R = clear the wave) are useful to every battle group.
6. Mirage suspend/resume (another group) must not suspend during PH_INTRO.
