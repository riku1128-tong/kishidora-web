/* Battle screen: draws the battle from the rules' events (src/c/battle_*.c). */
#include "game.h"
#include "battle.h"
#include "gfxtab.h"
#include "sound.h"
#include <string.h>

/* ---------------------------------------------------------------- layout */
#define FIELD_Y   16           /* rows 2-15; the panel is rows 16-27 */
#define FIELD_ROWS 14
#define FOOT_Y    123          /* D_HERO_Y: feet line */
#define HERO_X    D_HERO_X
#define STRIP_TILE(n) ((n) * 64)
#define OBJ_VRAM(n)   (VRAM_OBJ_TILES + (n) * 0x400)
#define PAL_OBJ(k)    (128 + (k) * 16)
#define OBJ_HERO 0
#define OBJ_SHIELD 1
#define OBJ_ZAKO 2             /* 2..4 */
#define OBJ_FX 5
#define OBJ_SKILL 6            /* 6..7 */
#define STRIP_HERO 0
#define STRIP_ZAKO 2           /* 2..4 */
#define STRIP_MISC 5
#define STRIP_FX 6
#define STRIP_CELLS 7
#define BOSS_TILE_A 64
#define BOSS_TILE_B 288
#define ATTR(tile, pal, prio) ((u16)(tile) | ((u16)(pal) << 9) | ((u16)(prio) << 12))

/* BG3 boxes: 2bpp tiles $6000-$6FFF (0-511) and $7C00-$7FFF (896-1023, after the maps; their
 * canvas is $7F:3800-$3FFF).  The panel's own art is BG2 (tiles 448-511, palette 5) and BG1
 * (tiles 0-60: bars and icons); its text is in these transparent boxes.  A window over the field
 * takes the tiles of the panel text (TABS / CARDS / NEXT, hidden meanwhile); the top bar, the HP
 * numbers and the banners keep their own tiles. */
#define BX_TOP 0
#define BX_HPNUM 1             /* "200/200" in the tab, rows 16-17 */
#define BX_PANEL 2             /* windows over the field: talk, tutorial, pause, mirage chests */
#define BX_BANNER 3
#define BX_HERO 4
#define BX_LBL 5               /* "HP" / "ST" */
/* the panel text: one box per column (8 x 10 tiles, rows 18-27), so a column changes in one
 * upload: rows 0-1 the tab (button, element orb or 必殺), 2-6 the card (cost, name), 7-9 NEXT */
#define BX_COL0 6              /* columns 0-2: boxes 6, 7, 8 */
#define BX_COL3 10             /* the guard / meat column */
#define BX_COMBO 9             /* rows 2-3: "n HIT!" */
#define TL_TOP 1
#define TL_HPNUM (TL_TOP + 64)
#define TL_LBL (TL_HPNUM + 16)
#define TL_TABS (TL_LBL + 6)
#define TL_COL(c) (TL_TABS + (c) * 80)
#define TL_WIN TL_TABS           /* windows: up to 32x11 = 352 tiles (87-438) */
#define TL_COMBO 439             /* 12x2 (439-462) */
#define TL_BANNER 896            /* 32x3, rows 4-6 (896-991) */
#define TL_HERO (TL_BANNER + 96) /* 15x2, rows 7-8 over the hero's head (992-1021) */
static void hero_forget(void);
#define TOP_Y_B 8              /* text 8 px inside a framed window (its frame tiles are gold) */

/* ---------------------------------------------------------------- actors */
typedef struct {
  u8 mon, boss, npose, slot;
  u8 cur, want;            /* pose index uploaded (queued) / wanted */
  u8 shown;                /* zako: the pose whose tiles are in VRAM (drawn) */
  u16 off[8];              /* descriptor offset of each pose */
  u8 code[8];
  u8 anim, hurt_t, atk_t, flash, idle_t;
  s8 dx;                   /* lunge / slide offset */
  s16 slide;
  u8 boss_buf;             /* which BG1 tile buffer holds the boss pose */
  u8 boss_drawn;
  u8 pcode[7];             /* pose index for each pose code (pose_fill) */
  u32 bar_hp;              /* HP the bar was computed for */
  u8 bar;                  /* HP bar 0..24 */
  u8 bar_ok;               /* bar is worked out for bar_hp */
  const u8 *zd;            /* zako: its descriptor (zdesc[slot]) */
} Actor;

static u8 zdesc[3][96];
static u8 bdesc[96];
static u8 hdesc[2600];
static Actor act[ENEMY_MAX];
static u16 hero_off[N_HERO_POSE];
static u8 hero_pose_now = 255, hero_want;
static u8 hero_shown = 255;    /* the pose whose tiles are in VRAM (drawn; its pieces match them) */
static u8 hero_hurt, hero_parry, hero_anim;
static s16 hero_slide;
static u8 shield_i;
static u8 boss_map[20 * 16 * 2];
static u8 boss_w, boss_h;
static s8 boss_ax, boss_ay;
static s16 boss_x0, boss_y0;
static u8 boss_pending;
static s8 shake_x, shake_y;
static u8 shake_t;
static u8 boss_i = 255;        /* the actor that is a boss (255 none) */
static s16 hd_bx, hd_by;       /* the HDMA scroll tables as written */
static s8 hd_sx, hd_sy;
static u8 panel_dirty, top_dirty, hp_dirty;
static u8 banner_t, hero_msg_t, banner_sp;
static u8 warn_t;              /* WARNING: the boss's HUD waits, the screen flashes red */
static u8 flash_t;             /* white flash (special move) */
static u16 combo_shown;        /* the hit count drawn in BX_COMBO */
static u8 combo_w;             /* its width in tiles */
static s16 boss_top;           /* screen y of the boss picture's top */
static u8 bang_t[ENEMY_MAX];
static u8 talk_open;
static u16 frame_ct;
static u8 lose_t, win_t;
static u8 sp_frame_t;
static u8 last_attack_sp;
static u8 pause_req;           /* START during a cut-in: the menu opens after it */
static u8 prof_ready;

typedef struct { s16 x, y; u8 t, col, n, dig[8]; } Num;   /* digits worked out once (32-bit division is slow) */
#define NUM_MAX 8
static Num nums[NUM_MAX];
typedef struct { s16 x, y; u8 t, cell; } Fx;
#define FX_MAX 6
static Fx fxs[FX_MAX];

static void num_add(s16 x, s16 y, u32 v, u8 col) {
  u8 i, n = 0, d[8];
  Num *p;
  for (i = 0; i < NUM_MAX; ++i) if (!nums[i].t) break;
  if (i == NUM_MAX) i = 0;
  p = &nums[i];
  pa[0] = (u16)v; pa[1] = (u16)(v >> 16);
  do d[n++] = div10_now(); while ((pa[0] | pa[1]) && n < 8);
  for (i = 0; i < n; ++i) p->dig[i] = d[n - 1 - i];   /* most significant first */
  p->n = n;
  p->x = x; p->y = y; p->t = 45; p->col = col;
}

static void fx_add(s16 x, s16 y, u8 cell) {
  u8 i;
  for (i = 0; i < FX_MAX; ++i) if (!fxs[i].t) break;
  if (i == FX_MAX) i = 0;
  fxs[i].x = x; fxs[i].y = y; fxs[i].t = 10; fxs[i].cell = cell;
}

/* ---------------------------------------------------------------- descriptors */
static void parse_desc(const u8 *d, u16 *off, u8 *code, u8 boss) {
  u8 n = d[0], i;
  u16 o = 1;
  for (i = 0; i < n && i < 8; ++i) {
    off[i] = o;
    code[i] = d[o];
    if (boss) o += 11;
    else o += 5 + 3 * d[o + 4];
  }
}

static u8 pose_find(Actor *a, u8 c) {
  u8 i;
  for (i = 0; i < a->npose; ++i) if (a->code[i] == c) return i;
  if (c == 1 || c == 6) return pose_find(a, 0);
  if (c == 5) return pose_find(a, 2);
  return 0;
}

/* after parse_desc: the pose of every code, so animate() needs no search */
static void pose_fill(Actor *a) {
  u8 c;
  for (c = 0; c < 7; ++c) a->pcode[c] = pose_find(a, c);
}

#define pose_by_code(a, c) ((a)->pcode[c])

static u16 rd16(const u8 *p) { return p[0] | ((u16)p[1] << 8); }

/* a new pose: its tiles go with the OAM that draws it (pose_tiles) */
static void zako_upload(u8 k, u8 p) { act[k].cur = p; }
static void hero_upload(u8 p) { hero_pose_now = p; }

/* the tiles of the poses to draw, unpacked and sent in the NMI that also sends this OAM
 * (crt0.s _spr_q: slot 0 hero, 1-3 zako), so a pose never shows with another pose's tiles.  The
 * NMI has 4320 bytes after the OAM; the BG3 map waits for this unless it already waited (then
 * the pose waits a frame), the palette too. */
#define SPR_ROOM (4864 - 544)
static u16 slot_cost(u8 k) {     /* a pending slot's bytes and its set-up time (crt0.s VQ_ENTRY) */
  u16 z = spr_q[k * 8 + 5] | ((u16)spr_q[k * 8 + 6] << 8);
  return z ? z + 80 : 0;
}

static void pose_tiles(void) {
  s16 room = SPR_ROOM - (s16)spr_bytes();
  u16 size, buf;
  u8 i, k;
  const u8 *d;
  Actor *a;
  if (cgram_dirty) room -= 512;
  if (nmi_late || mq_on) room -= ((bgmap_dirty & 4) ? 2048 : 0) + (s16)mq_bytes();   /* text with its map first */
  if (hero_pose_now != 255 && hero_pose_now != hero_shown) {
    d = hdesc + hero_off[hero_pose_now];
    size = (u16)d[3] * 2048;
    if (size > 4096) size = 4096;
    room += (s16)slot_cost(0);       /* replaced */
    if ((s16)(size + 80) <= room) {
      unpack(rd16(d + 1), 0x2000);
      spr_put(0, OBJ_VRAM(STRIP_HERO), 0x2000, 0x7f, size);
      hero_shown = hero_pose_now;
      room -= (s16)(size + 80);
    } else room -= (s16)slot_cost(0);
  }
  for (i = 0, a = act; i < b_ne; ++i, ++a) {
    if (a->boss || a->cur == 255 || a->cur == a->shown) continue;
    k = 1 + a->slot;
    if ((s16)(2048 + 80) > room + (s16)slot_cost(k)) continue;   /* next frame */
    room += (s16)slot_cost(k);
    d = zdesc[a->slot] + a->off[a->cur];
    buf = 0x4800 + a->slot * 0x0800;   /* $7F:3800-$3FFF is the canvas of BG3 tiles 896-1023 */
    size = unpack(rd16(d + 1), buf);
    if (size > 2048) size = 2048;
    spr_put(k, OBJ_VRAM(STRIP_ZAKO + a->slot), buf, 0x7f, size);
    a->shown = a->cur;
    room -= (s16)(size + 80);
  }
}

/* every pose's tiles in VRAM (battle start, screen off) */
static u8 poses_waiting(void) {
  u8 i;
  if (hero_pose_now != hero_shown) return 1;
  for (i = 0; i < b_ne; ++i) if (!act[i].boss && act[i].cur != act[i].shown) return 1;
  return spr_on;
}

/* boss: tiles to the free buffer now, map written when the tiles have arrived */
static void boss_upload(u8 k, u8 p) {
  Actor *a = &act[k];
  const u8 *d = bdesc + a->off[p];
  u16 ta = rd16(d + 1), ma = rd16(d + 3), size;
  u8 buf = a->boss_buf ^ 1;
  size = unpack(ta, 0xa000);
  queue_vram_big((buf ? BOSS_TILE_B : BOSS_TILE_A) * 16, 0xa000, 0x7f, size);   /* 2KB entries: one NMI cannot take 6KB */
  size = unpack(ma, 0xd000);
  ea[0] = 0xd000; ea[1] = 0x7f; ea[2] = (u16)boss_map; ea[3] = MIN(size, sizeof(boss_map));
  far_copy();
  boss_w = d[5]; boss_h = d[6];
  boss_ax = (s8)d[7]; boss_ay = (s8)d[8];
  a->boss_buf = buf;
  a->cur = p;
  boss_pending = 1;              /* the map follows once the tiles are in VRAM */
}

static void boss_place(u8 k) {
  Actor *a = &act[k];
  u8 x, y, tx0, ty0;
  u16 add = a->boss_buf ? BOSS_TILE_B : BOSS_TILE_A, w, i;
  s16 px = a->slide + b_e[k].sx + boss_ax, py = FOOT_Y + 4 + boss_ay;
  /* clear rows 2-15 (the panel's BG1 rows stay) */
  for (i = 2 * 32; i < 16 * 32; ++i) bg1map[i] = 0;
  if (px < 0) px = 0;
  tx0 = (u8)(px >> 3);
  ty0 = (u8)(py >> 3);
  boss_x0 = (s16)tx0 * 8 - px;
  boss_y0 = (s16)ty0 * 8 - py;
  boss_top = MAX((s16)ty0 * 8, FIELD_Y);
  for (y = 0; y < boss_h; ++y) {
    if (ty0 + y < 2 || ty0 + y > 15) continue;
    for (x = 0; x < boss_w; ++x) {
      if (tx0 + x > 31) continue;
      w = rd16(boss_map + ((u16)y * boss_w + x) * 2);
      bg1map[((u16)(ty0 + y) << 5) + tx0 + x] = w ? w + add : 0;
    }
  }
  bgmap_dirty |= 1;
  a->boss_drawn = 1;
}

static void panel_icons(void);

/* ---------------------------------------------------------------- set up */
static void load_stage_gfx(void) {
  u8 i, z = 0;
  u16 i16;
  Actor *a;
  for (i16 = 2 * 32; i16 < 16 * 32; ++i16) bg1map[i16] = 0;
  bgmap_dirty |= 1;
  boss_i = 255;
  for (i = 0; i < b_ne; ++i) {
    a = &act[i];
    memset(a, 0, sizeof(Actor));
    a->mon = b_e[i].mon;
    a->boss = (u8)mon_gfx[a->mon][2];
    a->cur = 255;
    a->shown = 255;
    if (a->boss) {
      boss_i = i;
      copy_asset(mon_gfx[a->mon][0], bdesc, sizeof(bdesc));
      a->npose = bdesc[0];
      parse_desc(bdesc, a->off, a->code, 1);
      pose_fill(a);
      load_pal(mon_gfx[a->mon][1], 96);
      a->boss_buf = 1;
      boss_upload(i, pose_by_code(a, 0));
    } else {
      a->slot = z++;
      a->zd = zdesc[a->slot];
      copy_asset(mon_gfx[a->mon][0], zdesc[a->slot], sizeof(zdesc[0]));
      a->npose = zdesc[a->slot][0];
      parse_desc(zdesc[a->slot], a->off, a->code, 0);
      pose_fill(a);
      load_pal(mon_gfx[a->mon][1], PAL_OBJ(OBJ_ZAKO + a->slot));
      zako_upload(i, pose_by_code(a, 0));
    }
    a->slide = a->boss ? 0 : 160;   /* BG1 is 256 px wide: a sliding boss would wrap around */
    a->want = a->cur;
  }
}

static void hero_setup(void) {
  u16 i, o = 1;
  copy_asset(ASSET_HERO_DESC, hdesc, sizeof(hdesc));
  for (i = 0; i < N_HERO_POSE; ++i) {
    hero_off[i] = o;
    o += 5 + 3 * hdesc[o + 4];
  }
  load_pal(ASSET_HERO_PAL, PAL_OBJ(OBJ_HERO));
  hero_pose_now = 255;
  hero_shown = 255;
}

static u16 bars_fill;           /* draw_bars: what the HP / ST cells show */
static u8 bars_red, bars_st, bars_sh, bars_max, bars_ok;
static void misc_setup(void) {
  u16 i;
  /* shield (tiles 0,1,16,17 of the misc strip) */
  shield_i = b_shield - ITEM_SHIELD0;
  if (shield_i >= N_SHIELD_GFX) shield_i = 0;
  unpack(ASSET_SHIELD_00 + shield_i * 2, 0x6000);
  ea[0] = OBJ_VRAM(STRIP_MISC); ea[1] = 0x6000; ea[2] = 0x7f; ea[3] = 64; vram_now();
  ea[0] = OBJ_VRAM(STRIP_MISC) + 16 * 16; ea[1] = 0x6040; ea[2] = 0x7f; ea[3] = 64; vram_now();
  load_pal(ASSET_SHIELD_00_PAL + shield_i * 2, PAL_OBJ(OBJ_SHIELD));
  load_vram(ASSET_FX_STRIP, OBJ_VRAM(STRIP_FX));
  load_vram(ASSET_FX_CELLS, OBJ_VRAM(STRIP_CELLS));
  load_pal(ASSET_FX_PAL, PAL_OBJ(OBJ_FX));
  load_pal(ASSET_SKILL_ICONS_PAL, PAL_OBJ(OBJ_SKILL));
  /* misc strip: cost badges (row 0-1 cols 8-15), mini HP bars and marks (row 3) */
  unpack(ASSET_FX_MISC, 0x6800);
  ea[0] = OBJ_VRAM(STRIP_MISC) + 8 * 16; ea[1] = 0x6800; ea[2] = 0x7f; ea[3] = 256; vram_now();
  ea[0] = OBJ_VRAM(STRIP_MISC) + 24 * 16; ea[1] = 0x6900; ea[2] = 0x7f; ea[3] = 256; vram_now();
  ea[0] = OBJ_VRAM(STRIP_MISC) + 32 * 16; ea[1] = 0x6a00; ea[2] = 0x7f; ea[3] = 1024; vram_now();
  /* panel overlay on BG1 (bars, icons) and the panel art on BG2 tiles 448-511, palette 5 */
  load_vram(ASSET_HUD_TILES, VRAM_BG1_TILES);
  load_pal(ASSET_PANEL_PAL, 80);
  for (i = 16 * 32; i < 28 * 32; ++i) bg1map[i] = 0;
  bars_ok = 0;
  panel_icons();
}

/* skill icon of column s into misc strip tiles 2+s*2, with the OAM that shows it (_spr_q slots
 * 4-9), in the frame its column's text goes */
static u8 skill_icon_shown[3];
static u8 skill_sel[N_SKILL];
static void skill_icon(u8 s, u8 sk) {
  u16 src;
  if (sk == skill_icon_shown[s]) return;
  skill_icon_shown[s] = sk;
  if (sk == SK_NONE) return;
  ea[0] = ASSET_SKILL_ICONS; asset_where();     /* stored raw: DMA'd from the ROM */
  src = ea[1] + (u16)sk * 128;
  spr_put(4 + s * 2, OBJ_VRAM(STRIP_MISC) + (2 + s * 2) * 16, src, (u8)ea[2], 64);
  spr_put(5 + s * 2, OBJ_VRAM(STRIP_MISC) + (18 + s * 2) * 16, src + 64, (u8)ea[2], 64);
}

static void bg_setup(void) {
  u16 id = area_bg[b_area];
  if (b_area == AR_CASTLE && b_men == D_CASTLE_MEN) id = BG_CASTLE_FINAL;
  load_vram(id, VRAM_BG2_TILES);
  load_map(id + 1, 1, 0, 2);                       /* rows 2-15 */
  load_pal(id + 2, 32);
  load_vram(ASSET_PANEL_TILES, VRAM_BG2_TILES + 448 * 16);
  load_map(ASSET_PANEL_MAP, 1, 0, 16);             /* rows 16-27 */
}

/* ---------------------------------------------------------------- HUD: the phone's battle panel
 * Rows 16-27 (battle_frame.gd / skill_card.gd): row 16 the HP bar and the HP tab, row 17 the
 * 10 ST cells, then four 64 px columns: the weapon tab (icon, button, element orb or 必殺),
 * the skill card (icon, cost badge, name) and the NEXT card; column 3 has the shield, the
 * guard button (B) and the meat button (R).  Art: BG2 (frame, state variants), BG1 (bars,
 * icons), OBJ (skill icons, cost badges); text: the BG3 boxes above. */
#define HUD_ATTR (5 << 10)
#define BG1_ICON(c, k) (HUD_ICON_T + (c) * 4 + (k))
#define HT_HP 1                /* BG1 HUD tiles (make_gfx.py hud) */
#define HT_HP_RED 10
#define HT_CAP_L 19
#define HT_CAP_R 20
#define HT_ST 21               /* + 2 empty, + 4 short, + 6 short empty */
#define HT_GUARD 29            /* + 4 dimmed */
#define HT_MEAT 37
#define MISC_BADGE 8           /* misc strip: badges 16x16 (red, short, gray) at 8 / 10 / 12, small 14 */
#define MISC_BAR 48            /* mini HP bars 0-8, then arrow, star, poison, paralyze, blind */
static u16 pbase[32 * 12];     /* the panel's BG2 map as loaded (rows 16-27) */
static u8 col_var[4][2];       /* shown variant: [card, next] 0 normal, 1 empty / off */
static u8 st_blink;            /* frames of the ST-short blink */
static u8 modal_on;            /* a window over the field: the HUD text boxes are hidden */
static u8 panel_can;           /* the cards were drawn for "can command" */
static u8 panel_st;            /* ... and for this ST (a cost above it is red) */
static u8 sk_recalc;           /* a column was sent: the panel sprite list is built again */
static u8 sk_col[3][3];        /* the panel sprites of each column as it was sent: icon palette (255 none), badge, NEXT badge */
static u8 col_sig[4][8];       /* what each column's text shows, as sent */
static u8 col_drawn;           /* bit c: column c's box holds its text */
static u8 panel_hold;          /* the panel rows are held blank: columns go through the queue */
static u8 col_box(u8 c) { return c < 3 ? BX_COL0 + c : BX_COL3; }

static void bg1_put(u8 row, u8 col, u16 t) { bg1map[(u16)row * 32 + col] = t | HUD_ATTR; }

/* item icons (weapons, shield) into BG1 tiles BG1_ICON from the ROM; the guard / meat icons */
static void panel_icons(void) {
  u8 c, k;
  u16 at, item;
  ea[0] = ASSET_PANEL_ICONS; asset_where();
  at = ea[1]; k = (u8)ea[2];
  for (c = 0; c < 4; ++c) {
    item = c < 3 ? (b_w[c].nsk ? b_w[c].item : 255) : b_shield;
    if (item == 255 || item >= N_ITEM) continue;
    ea[0] = VRAM_BG1_TILES + BG1_ICON(c, 0) * 16; ea[1] = at + item * 128; ea[2] = k; ea[3] = 128; vram_now();
    at = ea[1] - item * 128;
    bg1_put(18, c * 8 + 3, BG1_ICON(c, 0)); bg1_put(18, c * 8 + 4, BG1_ICON(c, 1));
    bg1_put(19, c * 8 + 3, BG1_ICON(c, 2)); bg1_put(19, c * 8 + 4, BG1_ICON(c, 3));
  }
  bgmap_dirty |= 1;
}

/* the guard / meat icons, dimmed when the button is off */
static void button_icons(void) {
  u8 g = col_var[3][0] ? 4 : 0, m = col_var[3][1] ? 4 : 0;
  bg1_put(21, 27, HT_GUARD + g); bg1_put(21, 28, HT_GUARD + g + 1);
  bg1_put(22, 27, HT_GUARD + g + 2); bg1_put(22, 28, HT_GUARD + g + 3);
  bg1_put(25, 27, HT_MEAT + m); bg1_put(25, 28, HT_MEAT + m + 1);
  bg1_put(26, 27, HT_MEAT + m + 2); bg1_put(26, 28, HT_MEAT + m + 3);
  bgmap_dirty |= 1;
}

/* BG2 map of column c, part k (0 card rows 20-24, 1 NEXT rows 25-27): normal or a variant */
static void panel_part(u8 c, u8 k, u8 v) {
  static u16 buf[8 * 5];
  u8 r0 = k ? 9 : 4, h = k ? 3 : 5, x, y;
  u16 id;
  if (col_var[c][k] == v) return;
  col_var[c][k] = v;
  if (v) {
    id = c == 3 ? (k ? ASSET_PANEL_MEAT_OFF : ASSET_PANEL_GUARD_OFF) : (k ? ASSET_PANEL_NEXT_NONE : ASSET_PANEL_CARD_EMPTY);
    ea[0] = id; ea[1] = (u16)buf; ea[2] = sizeof(buf); asset_copy();
  }
  for (y = 0; y < h; ++y)
    for (x = 0; x < 8; ++x)
      bg2map[(u16)(16 + r0 + y) * 32 + c * 8 + x] = v ? buf[y * 8 + x] : pbase[(r0 + y) * 32 + c * 8 + x];
  bgmap_dirty |= 2;
  if (c == 3) button_icons();
}

/* the top bar, the HP numbers and the HP / ST labels (also shown under a window) */
static u8 hpst_ok;
static void hud_fixed_boxes(void) {
  hpst_ok = 0;                   /* the HP numbers' box is new */
  box(BX_TOP, 0, 0, 32, 2, TL_TOP, PAL_W, BOX_OPAQUE);
  box(BX_HPNUM, 24, 16, 8, 2, TL_HPNUM, PAL_W, 0);
  box(BX_LBL, 0, 16, 3, 2, TL_LBL, PAL_W, BOX_SMALL);
  say(BX_LBL, 3, 0, TXT_HP_LABEL);
  say(BX_LBL, 3, 8, TXT_ST_LABEL);
  tb_flush(BX_LBL);
}

/* the panel text: tabs, cards, NEXT (their tiles are a window's while one is open) */
static void hud_panel_boxes(void) {
  u8 c;
  for (c = 0; c < 4; ++c) box(col_box(c), c * 8, 18, 8, 10, TL_COL(c), PAL_W, 0);
  col_drawn = 0;                 /* new boxes: the next draw_panel draws every column */
}

static void hud_boxes(void) {
  u8 c;
  hud_fixed_boxes();
  hud_panel_boxes();
  /* empty for now (box ids are shared with other screens: hiding one must not hit their cells) */
  box(BX_BANNER, 0, 4, 32, 3, TL_BANNER, PAL_W, 0);
  box(BX_HERO, 0, 7, 15, 2, TL_HERO, PAL_W, 0);
  hero_forget();
  box(BX_COMBO, 0, 2, 12, 2, TL_COMBO, PAL_W, 0);
  /* the panel map as loaded by bg_setup, for restoring columns */
  for (c = 0; c < 12; ++c) memcpy(&pbase[c * 32], &bg2map[(16 + c) * 32], 64);
  memset(col_var, 0, sizeof(col_var));
  button_icons();
  copy_asset(ASSET_SKILL_ICONS_SEL, skill_sel, N_SKILL);
}

/* BG3 map rows held back (blank) while the tiles they show are uploaded, so that no frame
 * shows a half-drawn canvas (or the old text of tiles that are being reused) */
static u16 held[14 * 32];
static void rows_hold(u8 y, u8 h) {
  memcpy(held, &bg3map[(u16)y * 32], (u16)h * 64);
  memset(&bg3map[(u16)y * 32], 0, (u16)h * 64);
  bgmap_dirty |= 4;
}

static void rows_restore(u8 y, u8 h) {
  memcpy(&bg3map[(u16)y * 32], held, (u16)h * 64);
  bgmap_dirty |= 4;
}

/* the banner, hero and combo boxes: their text goes in the NMI that takes their map cells
 * (tb_flush_sync), so a message never shows the old text in its colours, the new text in the old
 * colours, or the old text for a frame when the box is shown again */
/* windows over the field (talk, tutorial, pause, mirage chests): modal_open, the text, then
 * modal_show.  The banners stop; the panel text and its sprites go while the window has its
 * tiles; the top bar and the HP numbers stay. */
static void draw_sprites(void);
static u8 panel_shown;         /* the panel text still shows (its tiles become the window's) */
static void modal_open(u8 x, u8 y, u8 w, u8 h) {
  if (!modal_on) {
    banner_t = hero_msg_t = 0;
    box_hide(BX_BANNER); box_hide(BX_HERO); box_hide(BX_COMBO);
    hero_forget();
    modal_on = 1;
    panel_shown = 1;
    flash_t = 0; cgadsub = 0; coldata[0] = coldata[1] = coldata[2] = 0;   /* colour math stops too */
    canvas_wait(TL_WIN, 352);  /* no upload of the panel text may read these tiles once the window is in them */
  }
  box(BX_PANEL, x, y, w, h, TL_WIN, PAL_W, BOX_WINDOW);
}

/* the window's text is in its canvas: the panel text and its sprites go, the window comes
 * (the panel shows until now: its tiles in VRAM are still its own) */
static void modal_show(void) {
  u8 *b, c;
  if (panel_shown) {
    for (c = 0; c < 4; ++c) box_hide(col_box(c));
    draw_sprites();
    panel_shown = 0;
  }
  b = &tb[BX_PANEL * 8];
  rows_hold(b[1], b[3]);
  tb_flush(BX_PANEL);
  canvas_wait(TL_WIN, (u16)b[2] * b[3]);
  rows_restore(b[1], b[3]);
}

static void draw_panel(void);
static void modal_close(void) {
  if (!modal_on) return;
  box_hide(BX_PANEL);
  modal_on = 0;
  hud_panel_boxes();
  tb_front = 1;                /* ahead of what is queued (a wave's boss tiles) */
  panel_hold = 1;              /* the rows are held: the columns go through the queue */
  draw_panel();
  panel_hold = 0;
  tb_front = 0;
  rows_hold(18, 10);
  canvas_wait(TL_TABS, 320);
  rows_restore(18, 10);
  draw_sprites();              /* the panel sprites with their text, in this NMI */
  wait_frame();
  top_dirty = hp_dirty = 1;
  combo_shown = 0xffff;
}

/* top bar: WAVE n/m, the boss's name and HP bar, the coins */
static void draw_top(void) {
  u8 i, b = 255;
  u16 w;
  Foe *f;
  mq_room(32 * 2 * 16);          /* its text with its colours (the coin mark moves with the digits) */
  tb_clear(BX_TOP);
  targ_num = b_stage; targ_num2 = b_stage_count;
  say(BX_TOP, 6, 2, TXT_WAVE_Y);
  for (i = 0; i < b_ne; ++i) if (act[i].boss && foe_targetable(&b_e[i])) b = i;
  if (b != 255 && !warn_t) {
    f = &b_e[b];
    text_small = 8;
    say(BX_TOP, 88, 1, f->awake ? NAME_SECRET_AWAKE : MONN_0 + f->mon);   /* BG line 0 is never shown */
    text_small = 0;
    box_pal(BX_TOP, 11, 1, 15, 1, PAL_RED);
    w = f->hp_max ? (u16)((u32)f->hp * 116 / f->hp_max) : 0;
    if (f->hp && !w) w = 1;
    tb_rect(BX_TOP, 88, 10, 116, 4, 1);          /* back: shadow colour */
    if (w) tb_rect(BX_TOP, 88, 10, w, 3, 3);       /* fill: the tiles' red */
  }
  /* coins right-aligned at 252, the yellow mark on its own tile before them */
  targ_num = b_coins;
  w = msg_width(TXT_N_WHITE);
  say(BX_TOP, 252 - w, 2, TXT_N_WHITE);
  say(BX_TOP, ((252 - w) & ~7) - 16, 2, TXT_ORB_4);   /* its 12 px end before the digits' first tile */
  if (bright & 0x80) tb_flush(BX_TOP);
  else tb_flush_sync(BX_TOP);
  top_dirty = 0;
}

/* HP bar (20 tiles, green; red at 25 % or less) and the 10 ST cells on BG1; only what changed
 * since the last call is written (bars_ok = 0 draws everything) */
static void draw_bars(void) {
  u16 fill, t, f, *m;
  u8 i, sh = st_blink && (st_blink & 4), cell, red;
  fill = b_hp_max ? (u16)((u32)b_hp * 160 / b_hp_max) : 0;
  if (b_hp && !fill) fill = 1;
  red = b_hp * 4 <= b_hp_max;
  if (!bars_ok || fill != bars_fill || red != bars_red) {
    bars_fill = fill; bars_red = red;
    m = &bg1map[16 * 32 + 2];
    *m++ = HT_CAP_L | HUD_ATTR;
    t = red ? HT_HP_RED : HT_HP;
    for (i = 0, f = fill; i < 20; ++i, ++m, f = f > 8 ? f - 8 : 0) *m = (t + (f > 8 ? 8 : f)) | HUD_ATTR;
    *m = HT_CAP_R | HUD_ATTR;
    bgmap_dirty |= 1;
  }
  if (!bars_ok || b_st != bars_st || b_st_max != bars_max || sh != bars_sh) {
    bars_st = b_st; bars_max = b_st_max; bars_sh = sh;
    m = &bg1map[17 * 32 + 3];
    for (i = 0; i < 10; ++i, m += 2) {
      cell = (HT_ST + (sh ? 4 : 0) + (i < b_st && i < b_st_max ? 0 : 2));
      m[0] = cell | HUD_ATTR;
      m[1] = (cell + 1) | HUD_ATTR;
    }
    bgmap_dirty |= 1;
  }
  bars_ok = 1;
}

/* the HP numbers as drawn: the text is drawn again only when they change (most hits change
 * only the ST cells) */
static u32 hpst_hp, hpst_max;
static void draw_hpst(void) {
  u16 w;
  draw_bars();
  hp_dirty = 0;
  if (hpst_ok && hpst_hp == b_hp && hpst_max == b_hp_max) return;
  hpst_ok = 1; hpst_hp = b_hp; hpst_max = b_hp_max;
  mq_room(8 * 2 * 16);
  tb_clear(BX_HPNUM);
  targ_num = b_hp; targ_num2 = b_hp_max;
  w = msg_width(TXT_FRAC);
  say(BX_HPNUM, w < 60 ? 60 - w : 0, 2, TXT_FRAC);
  if (bright & 0x80) tb_flush(BX_HPNUM);
  else tb_flush_sync(BX_HPNUM);
}

/* centred in a column: x of a text of width w in the 56 px card at column c */
static u16 col_mid(u8 c, u16 w) { return (u16)c * 64 + 4 + (w < 56 ? (56 - w) / 2 : 0); }

static u16 small_width(u16 msg) {
  u16 w;
  text_small = 8;
  w = msg_width(msg);
  text_small = 0;
  return w;
}

/* in the 8-dot font, centred in the card of column c */
static void say_small_mid(u8 bx, u8 c, u8 y, u16 msg, u16 w) {
  u16 x = col_mid(c, w);
  text_small = 8;
  say(bx, x, y, msg);
  text_small = 0;
}

static u8 guard_off(void) { return b_guard_cool || b_phase != PH_FIGHT; }

/* what column c shows: skill, cost, lock turns (255 none), 必殺 tag, can command, cost above ST,
 * next skill (255 none) and its cost.  A column is drawn and sent only when this changes */
static void col_signature(u8 c, u8 can, u8 *g) {
  u8 sk, nx;
  memset(g, 0, 8);
  if (c == 3) return;
  sk = skill_of(c);
  g[0] = sk;
  if (sk == SK_NONE) return;
  g[1] = skill_cost(sk);
  g[2] = is_locked(c) ? b_lock_turns : 255;
  g[3] = special_if(c) != 255 && g[1] <= b_st && g[2] == 255;
  g[4] = can;
  g[5] = g[1] > b_st;
  g[6] = 255;
  if (b_w[c].nsk > 1) { nx = next_skill_of(c); g[6] = nx; g[7] = skill_cost(nx); }
}

/* the panel sprites of column c (skill icon, cost badge, NEXT badge) as its text is sent */
static void col_sprites(u8 c, const u8 *g) {
  u8 sk = g[0], *k = sk_col[c];
  k[0] = sk == SK_NONE ? 255 : skill_sel[sk];
  k[1] = sk == SK_NONE || g[2] != 255 ? 0 : (g[5] ? 2 : 1);
  k[2] = sk != SK_NONE && g[6] != 255;
  skill_icon(c, sk);
  sk_recalc = 1;
}

/* column c's text in its box (8 x 10 tiles): rows 0-1 tab, 2-3 cost (or B), 5-6 name, 7-9 NEXT
 * (row 4 stays empty).  The parts in send (CP_*) go with the map, so text, colours and sprites
 * change in one frame (all of it goes through the queue while the rows are held).  The other
 * parts keep the same glyphs in VRAM: only their colours change, and the map carries those */
#define CP_TAB 1
#define CP_COST 2
#define CP_NAME 4
#define CP_NEXT 8
static const u8 cp_row[4] = { 0, 2, 5, 7 }, cp_rows[4] = { 2, 2, 2, 3 };
static void draw_column(u8 c, const u8 *g, const u8 *o, u8 send, u8 all) {
  u8 bx = col_box(c), sk = g[0], el;
  u16 w;
  if (all) tb_clear(bx);
  else {                         /* only the parts with new glyphs (the rest stays in the canvas) */
    tb_free(bx);
    for (el = 0; el < 4; ++el) if (send & (1 << el)) {
      /* a part's tile rows are one run of the canvas ($7F:tile*16): zeroed by DMA (<= 384 bytes) */
      ea[0] = (TL_COL(c) + cp_row[el] * 8) * 16; ea[1] = 0x7f; ea[2] = cp_rows[el] * 8 * 16;
      wram_zero();
#ifdef PROFILE
      ea[0] = bx; ea[1] = cp_row[el]; ea[2] = cp_rows[el]; tc_rows();
#endif
    }
  }
  if (c == 3) {                  /* guard (B), meat (R) */
    say(bx, 8, 17, TXT_BTN_B);
    say_small_mid(bx, 0, 44, TXT_GUARD_LABEL, small_width(TXT_GUARD_LABEL));
    text_small = 8;
    say(bx, 8, 59, TXT_BTN_R);
    text_small = 0;
  } else {
    text_small = 8;
    if (send & CP_TAB) {
      say(bx, 8, 4, TXT_BTN_Y + c);
      if (sk != SK_NONE) {
        el = items[b_w[c].item].element;
        if (g[3]) say(bx, 42, 4, TXT_SP_TAG);
        else say(bx, 48, 4, TXT_ORB_0 + (el < 6 ? el : 0));
      }
    }
    if ((send & CP_COST) && sk != SK_NONE && g[2] == 255) {
      targ_num = g[1];           /* in the badge's disc (screen rows 164-171): the 8-dot digits */
      w = msg_width(TXT_N_WHITE);
      say(bx, 48 - w / 2, 21, g[5] ? TXT_N_RED : TXT_N_WHITE);
    }
    text_small = 0;
    if (send & CP_NAME) {
      if (sk == SK_NONE) say_small_mid(bx, 0, 44, TXT_NONE_GRAY, small_width(TXT_NONE_GRAY));   /* the name's place */
      else if (g[2] != 255) {
        targ_num = g[2];
        say_small_mid(bx, 0, 44, TXT_LOCK_TURNS, small_width(TXT_LOCK_TURNS));
      } else {
        targ_str = SKILLN_0 + sk;
        say_small_mid(bx, 0, 44, g[4] ? TXT_S : TXT_S_GRAY, small_width(SKILLN_0 + sk));
      }
    }
    if ((send & CP_NEXT) && sk != SK_NONE) {
      text_small = 8;            /* NEXT: the label, the next skill's cost (gray badge) and name */
      say(bx, 8, 59, TXT_NEXT_LABEL);
      if (g[6] != 255) {
        targ_num = g[7];
        say(bx, 47, 59, TXT_N_WHITE);
        targ_str = SKILLN_0 + g[6];
        say_small_mid(bx, 0, 68, TXT_S, small_width(SKILLN_0 + g[6]));   /* white like the label (a tile row is shared) */
      }
      text_small = 0;
    }
    /* colours only: the cells' palettes, the same glyphs (the gray name while the hero acts,
     * the red cost above ST) */
    if (!all && sk != SK_NONE && g[2] == 255) {
      if (!(send & CP_NAME) && g[4] != o[4]) box_pal(bx, 0, 5, 8, 2, g[4] ? PAL_W : PAL_GRAY);
      if (!(send & CP_COST) && g[5] != o[5]) box_pal(bx, 0, 2, 8, 2, g[5] ? PAL_RED : PAL_W);
    }
  }
  if (panel_hold || (bright & 0x80)) tb_flush(bx);
  else for (el = 0; el < 4; ++el) if (send & (1 << el)) tb_flush_rows_sync(bx, cp_row[el], cp_rows[el]);
  if (c < 3) col_sprites(c, g);
}

static void draw_panel(void) {
  u8 c, can, g[4][8], sk, parts[4], all[4];
  u16 need[4], total = 0;
  if (modal_on) return;
  can = battle_can_command();
  for (c = 0; c < 4; ++c) {
    u8 *o = col_sig[c], *h = g[c], p = 0;
    u16 n = 0;
    col_signature(c, can, h);
    parts[c] = 0; need[c] = 0; all[c] = 0;
    if ((col_drawn & (1 << c)) && !memcmp(h, o, 8)) { parts[c] = 0xff; continue; }   /* unchanged */
    /* the parts with new glyphs (h[4], h[5] are colours only: gray name, red cost) */
    all[c] = !(col_drawn & (1 << c)) || panel_hold || (bright & 0x80);
    if (all[c]) p = CP_TAB | CP_COST | CP_NAME | CP_NEXT;
    else {
      if (h[3] != o[3] || (h[0] == SK_NONE) != (o[0] == SK_NONE)) p |= CP_TAB;
      if ((h[0] == SK_NONE) != (o[0] == SK_NONE) || h[1] != o[1] || (h[2] == 255) != (o[2] == 255)) p |= CP_COST;
      if (h[0] != o[0] || h[2] != o[2]) p |= CP_NAME;
      if (h[0] != o[0] || h[6] != o[6] || h[7] != o[7]) p |= CP_NEXT;
    }
    /* each part and the icon's two DMAs with their set-up time (80, crt0.s VQ_ENTRY) */
    if (p & CP_TAB) n += 2 * 8 * 16 + 80;
    if (p & CP_COST) n += 2 * 8 * 16 + 80;
    if (p & CP_NAME) n += 2 * 8 * 16 + 80;
    if (p & CP_NEXT) n += 3 * 8 * 16 + 80;
    if (h[0] != o[0]) n += 128 + 2 * 80;     /* its icon */
    parts[c] = p; need[c] = n; total += n;
  }
  /* the whole panel changes in one frame: when the map's group cannot take all of it this frame,
   * it waits (a column alone could show its colours before the others) */
  if (total && !panel_hold && !(bright & 0x80) && !mq_fits(total - 80) && total <= 2272 - 80) {
    panel_dirty = 1;
    return;
  }
  panel_can = can;
  panel_st = b_st;
  panel_dirty = 0;
  for (c = 0; c < 3; ++c) { sk = skill_of(c) == SK_NONE; panel_part(c, 0, sk); panel_part(c, 1, sk); }
  panel_part(3, 0, guard_off());
  panel_part(3, 1, (can || meat_asker() != 255) ? 0 : 1);
  for (c = 0; c < 4; ++c) {
    u8 old[8];
    if (parts[c] == 0xff) continue;
    /* (too much for one frame: column by column) */
    if (parts[c] && !panel_hold && !(bright & 0x80) && !mq_fits(need[c] - 80)) { panel_dirty = 1; continue; }
    memcpy(old, col_sig[c], 8);
    memcpy(col_sig[c], g[c], 8);
    col_drawn |= 1 << c;
    draw_column(c, g[c], old, parts[c], all[c]);
  }
}

/* the banner (rows 4-6, above the enemies' bubbles and numbers) */
static void banner(u16 msg, u8 pal, u8 t) {
  if (modal_on) return;
  mq_room(32 * 3 * 16);
  box(BX_BANNER, 0, 4, 32, 3, TL_BANNER, pal, 0);
  say_center(BX_BANNER, 6, msg);
  tb_flush_sync(BX_BANNER);
  banner_t = t;
  banner_sp = 0;
}

/* a message over the hero's head (rows 7-8, x 0-119); ribbon: on a dark band from the left edge
 * (the phone's skill ribbon) */
/* the same message again (text, colour, number): its tiles are still in VRAM, only its cells
 * come back (drawing it took a frame and a half, pressing on without ST showed it each time) */
static u16 hero_last = 0xffff;
static u8 hero_lpal, hero_lrib, hero_ltw;
static u32 hero_lnum;
static u16 hero_cells[2][15];

static void hero_forget(void) { hero_last = 0xffff; }

static void hero_box(u16 msg, u8 pal, u8 ribbon) {
  u16 w;
  u8 tw = 15;
  if (modal_on) return;
  if (msg == hero_last && pal == hero_lpal && ribbon == hero_lrib && targ_num == hero_lnum) {
    memcpy(&bg3map[7 * 32], hero_cells[0], hero_ltw * 2);
    memcpy(&bg3map[8 * 32], hero_cells[1], hero_ltw * 2);
    bgmap_dirty |= 4;
    hero_msg_t = 40;
    return;
  }
  mq_room(15 * 2 * 16);
  w = msg_width(msg);
  if (ribbon) { tw = (u8)((w + 20 + 7) >> 3); if (tw > 15) tw = 15; }
  box_hide(BX_HERO);           /* the width changes: no cells of the last one stay */
  box(BX_HERO, 0, 7, tw, 2, TL_HERO, pal, ribbon ? BOX_OPAQUE : 0);
  say(BX_HERO, ribbon ? 8 : (w < 120 ? (120 - w) / 2 : 0), 2, msg);
  tb_flush_sync(BX_HERO);
  hero_msg_t = 40;
  hero_last = msg; hero_lpal = pal; hero_lrib = ribbon; hero_lnum = targ_num; hero_ltw = tw;
  memcpy(hero_cells[0], &bg3map[7 * 32], tw * 2);
  memcpy(hero_cells[1], &bg3map[8 * 32], tw * 2);
}

static void hero_msg(u16 msg, u8 pal) { hero_box(msg, pal, 0); }

/* special move: an opaque band with 必殺技 and the name, and a white flash (the phone's cut-in) */
static void banner_cutin(u8 sp) {
  u16 w;
  if (modal_on) return;
  mq_room(32 * 3 * 16);
  box(BX_BANNER, 0, 4, 32, 3, TL_BANNER, PAL_T_YELLOW, BOX_OPAQUE);
  text_small = 8;
  w = msg_width(T_SP_CUTIN);
  say(BX_BANNER, (256 - w) / 2, 2, T_SP_CUTIN);
  text_small = 0;
  say_center(BX_BANNER, 10, SPN_0 + sp);
  tb_flush_sync(BX_BANNER);
  banner_t = 50;
  banner_sp = 1;
  flash_t = 6;
}

/* the combo ribbon: "n HIT!" and, from 10 hits, the special's bonus (rows 2-3, left) */
static void draw_combo(void) {
  u16 pct, w, x2 = 0, rate = 0, frac = 0;
  u8 cw;
  combo_shown = b_combo;
  if (!b_combo) { box_hide(BX_COMBO); combo_w = 0; return; }
  targ_num = b_combo;
  w = msg_width(TXT_COMBO);
  pct = combo_pct(b_combo);
  if (pct > 100) {
    frac = pct % 100;
    if (frac % 10 == 0) frac /= 10;
    rate = frac ? TXT_COMBO_RATE_D : TXT_COMBO_RATE;
    x2 = (6 + w + 13) & ~7;
  }
  /* a dark ribbon: 8 tiles, 14 with the bonus */
  mq_room(14 * 2 * 16);
  cw = rate ? 14 : 8;
  if (cw != combo_w) { combo_w = cw; box_hide(BX_COMBO); }
  box(BX_COMBO, 0, 2, combo_w, 2, TL_COMBO, PAL_W, BOX_OPAQUE);
  if (rate) {
    targ_num = pct / 100;      /* 1.1 / 1.25 / 1.5 / 2 */
    targ_num2 = frac;
    text_small = 8;
    say(BX_COMBO, x2, 5, rate);
    text_small = 0;
  }
  targ_num = b_combo;
  say(BX_COMBO, 6, 2, TXT_COMBO);
  tb_flush_sync(BX_COMBO);
}

/* ---------------------------------------------------------------- talk window (over the field) */
static void talk_show(u16 text, u8 who) {
  modal_open(0, 2, 32, 11);
  say(BX_PANEL, 10, TOP_Y_B, who == MON_117 ? MONN_0 + MON_117 : (who == 255 ? NAME_KING : MONN_0 + maou_forms[0]));
  say(BX_PANEL, 200, TOP_Y_B, T_TALK_NEXT);
  say_wrap(BX_PANEL, 12, TOP_Y_B + 16, text, 248);
  modal_show();
  talk_open = 1;
}

static void talk_close(void) {
  modal_close();
  talk_open = 0;
}

/* ---------------------------------------------------------------- tutorial guide (TUTORIAL_STEPS) */
static u8 tut_step, tut_open, tut_wait, tut_fails, tut_first_slot, tut_target_moved, tut_msg, tut_due;
#define TUT_MSG_STEP 0
#define TUT_MSG_RETRY 1
#define TUT_MSG_GIVEUP 2
#define TUT_MSG_HINT 3

static u8 tut_stage_end(void) {
  return b_stage < 4 ? tut_stage_first[b_stage] : N_TUT_STEP;
}

static void tut_show(u8 msg) {
  u16 title = TUTT_0 + tut_step, body = TUTL_0 + tut_step;
  if (msg == TUT_MSG_RETRY) { title = TUTX_RETRY; body = tut_goal[tut_step] == TG_GUARD ? TUTX_GUARD_NG : TUTH_0 + tut_step; }
  else if (msg == TUT_MSG_GIVEUP) { title = TUTX_GIVEUP; body = TUTX_GIVEUP_LINES; }
  else if (msg == TUT_MSG_HINT) { title = TUTX_HINT_TITLE; body = tut_goal[tut_step] == TG_TARGET ? TUTX_TARGET_HINT : TUTX_HINT_LINES; }
  tut_msg = msg;
  modal_open(0, 2, 32, 11);
  targ_str = title;
  say(BX_PANEL, 10, TOP_Y_B, TXT_YELLOW_S);
  say(BX_PANEL, 200, TOP_Y_B, T_TALK_NEXT);
  say_wrap(BX_PANEL, 12, TOP_Y_B + 16, body, 248);
  modal_show();
  tut_open = 1;
}

/* the step's action starts: freeze the enemy count unless this is a guard lesson */
static void tut_arm(void) {
  u8 g = tut_goal[tut_step], i;
  tut_wait = 1;
  tut_fails = 0;
  tut_target_moved = 0;
  b_freeze = !(g == TG_GUARD || g == TG_JUST || g == TG_MIRACLE);
  b_tut_protect = g != TG_DEFEAT;
  switch (g) {                     /* _start_goal() */
  case TG_MEAT: b_st = 0; break;
  case TG_GUARD: case TG_JUST: case TG_MIRACLE: battle_force_attack(0); break;
  case TG_SKILL2: b_nhist = 0; break;
  case TG_SPECIAL: b_st = b_st_max; b_w[0].pos = b_w[1].pos = 0; b_nhist = 0; tut_fails = 0; break;
  case TG_TARGET: b_st = b_st_max; break;
  case TG_DEFEAT:
    for (i = 0; i < b_ne; ++i) if (b_e[i].hp > D_TUT_LAST_HP) b_e[i].hp = D_TUT_LAST_HP;
    b_st = b_st_max;
    break;
  }
  hp_dirty = panel_dirty = top_dirty = 1;
  banner(TUTH_0 + tut_step, PAL_T_YELLOW, 150);
}

/* windows open when the player has control again (the turn must not freeze mid-attack) */
static void tut_later(u8 msg) { tut_due = msg + 1; tut_wait = 0; }

static void tut_next(void) {
  ++tut_step;
  tut_wait = 0;
  if (tut_step < tut_stage_end()) tut_later(TUT_MSG_STEP);
}

/* A pressed on the tutorial window */
static void tut_ok(void) {
  tut_open = 0;
  modal_close();
  if (tut_msg == TUT_MSG_GIVEUP) { tut_next(); return; }
  if (tut_msg != TUT_MSG_STEP) {
    u8 g = tut_goal[tut_step];
    tut_wait = 1;
    if (g == TG_GUARD || g == TG_JUST || g == TG_MIRACLE) battle_force_attack(0);
    return;
  }
  if (tut_goal[tut_step] == TG_INFO) tut_next();
  else tut_arm();
}

static void tut_done(void) {
  se_play(SE_OK);
  tut_next();
}

static void tut_event(Ev *e) {
  u8 g;
  if (b_mode != BM_TUTORIAL) return;
  if (e->type == EV_FIGHT) {
    tut_step = tut_stage_first[b_stage - 1];
    tut_wait = 0;
    b_freeze = 1;
    tut_later(TUT_MSG_STEP);
    return;
  }
  if (!tut_wait || tut_step >= N_TUT_STEP) return;
  g = tut_goal[tut_step];
  switch (e->type) {
  case EV_ATTACK:
  case EV_CHARGE:
    if (g == TG_SKILL) { tut_first_slot = e->a; tut_done(); }
    else if (g == TG_SKILL2 && e->a != tut_first_slot) tut_done();
    else if (g == TG_SPECIAL && e->type == EV_ATTACK) {
      if (e->c != 255) tut_done();
      else if (++tut_fails == 3) tut_later(TUT_MSG_HINT);
    } else if (g == TG_TARGET && e->type == EV_ATTACK) {
      if (tut_target_moved) tut_done();
      else if (++tut_fails == 2) { tut_fails = 0; tut_later(TUT_MSG_HINT); }
    }
    break;
  case EV_MEAT:
    if (g == TG_MEAT) tut_done();
    break;
  case EV_TARGET:
    if (g == TG_TARGET) tut_target_moved = 1;
    break;
  case EV_STAGE_CLEAR:
    if (g == TG_DEFEAT) { tut_wait = 0; ++tut_step; }
    break;
  case EV_ENEMY_HIT:
    if (g == TG_GUARD || g == TG_JUST || g == TG_MIRACLE) {
      u8 need = g == TG_GUARD ? GD_GUARD : (g == TG_JUST ? GD_JUST : GD_MIRACLE);
      if (e->a >= need) tut_done();
      else if (++tut_fails >= (g == TG_GUARD ? 255 : 4)) tut_later(TUT_MSG_GIVEUP);
      else tut_later(TUT_MSG_RETRY);
    }
    break;
  }
}

/* ---------------------------------------------------------------- events */
static s16 foe_x(u8 i) { return b_e[i].sx + act[i].slide + act[i].dx; }

static u8 hero_wt(void) {
  u8 it = b_w[b_focus].item;
  return it == ITEM_NONE ? WT_SWORD : items[it].type;
}

static void on_event(Ev *e) {
  u8 i;
  tut_event(e);
  switch (e->type) {
  case EV_STAGE_START:
    load_stage_gfx();
    top_dirty = 1;
    if (e->b) { banner(T_BOSS_WARN, PAL_T_RED, 90); se_play(SE_WARNING); warn_t = 90; }
    else if (b_mode == BM_TRAINING) banner(T_TRAINING, PAL_T_WHITE, 45);
    else if (b_mode == BM_NORMAL) { targ_num = b_stage; banner(TXT_WAVE_N, PAL_T_WHITE, 30); }
    bgm_play(battle_bgm());
    hero_slide = b_stage == 1 ? -80 : 0;
    break;
  case EV_FIGHT:
    panel_dirty = 1;
    break;
  case EV_TURN_PLAYER:
  case EV_TURN_END:
    panel_dirty = 1;
    break;
  case EV_ENEMY_BANG:
    bang_t[e->enemy] = 30;
    break;
  case EV_ENEMY_BLINK:
    act[e->enemy].flash = 16;
    break;
  case EV_ENEMY_WINDUP:
    act[e->enemy].atk_t = 30;
    if (e->a) {
      banner(MONSP_0 + b_e[e->enemy].mon, PAL_T_RED, 60);
      if (act[e->enemy].boss) se_play(SE_BREATH);
    }
    panel_dirty = 1;
    break;
  case EV_ENEMY_STRIKE:
    act[e->enemy].dx = act[e->enemy].boss ? -16 : -12;
    break;
  case EV_ENEMY_HIT:
    act[e->enemy].dx = 0;
    if (e->a == GD_MIRACLE) { banner(T_RESULT_MIRACLE, PAL_T_YELLOW, 45); se_play(SE_MIRACLE); hero_parry = 16; }
    else if (e->a == GD_JUST) { banner(T_RESULT_JUST, PAL_T_YELLOW, 45); se_play(SE_JUST); hero_parry = 14; }
    else if (e->a == GD_GUARD) { hero_msg(T_RESULT_GUARD, PAL_BLUE); se_play(SE_GUARD); hero_parry = 10; }
    else { se_play(SE_DAMAGE); hero_hurt = D_HERO_HURT_FRAMES; shake_t = 8; }
    /* the numbers start under the hero box (rows 7-8) and float up to it */
    if (e->v) num_add(HERO_X, FOOT_Y - 34, e->v, 2);
    if (e->c) num_add(HERO_X + 8, FOOT_Y - 26, e->c, 3);
    if (e->b & 4) {
      targ_str = T_SLOT_MAIN + b_lock_slot;
      banner(b_lock_slot2 == 255 ? T_LOCK : T_LOCK_2_0 + b_lock_slot + b_lock_slot2 - 1, PAL_T_RED, 60);
    }
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_MISS:
    act[e->enemy].dx = 0;
    banner(T_MISS, PAL_T_YELLOW, 42);
    break;
  case EV_GUARD:
    break;
  case EV_ATTACK:               /* a = slot, b = skill, c = special (255 = none) */
    last_attack_sp = (u8)e->c;
    sp_frame_t = 0;
    if (e->c != 255) { banner_cutin((u8)e->c); se_play(SE_SPECIAL); }
    else if (e->a < 3) {
      u8 wt = items[b_w[e->a].item].type;
      hero_box(SKILLN_0 + e->b, PAL_T_WHITE, 1);     /* the skill's name on the phone's ribbon */
      se_play(e->b == SK_KICK ? SE_KICK : (e->b == SK_SAKEBU ? SE_SHOUT : (wt == WT_SPEAR ? SE_SPEAR : (wt == WT_HAMMER ? SE_HAMMER : SE_SLASH))));
    }
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_CHARGE:                 /* c = the buff of the next skill */
    if (e->c >= BF_FREE && e->c <= BF_CRIT) hero_msg(T_BUFF_1 + e->c - BF_FREE, PAL_T_YELLOW);
    se_play(SE_CHARGE);
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_MEAT:
    se_play(SE_EAT);
    targ_num = D_MEAT_ST_RECOVER;  /* the phone shows the configured amount */
    hero_msg(TXT_ST_PLUS, PAL_BLUE);
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_ST_SHORT:
    hero_msg(T_ST_SHORT, PAL_T_RED);
    st_blink = D_ST_SHORT_BLINK_FRAMES;
    se_play(SE_CANCEL);
    break;
  case EV_LOCKED:
    hero_msg(T_LOCKED, PAL_T_RED);
    se_play(SE_CANCEL);
    break;
  case EV_NUMBER:
    i = e->enemy;
    if (!act[i].boss || (e->a & 0xf0) != (PK_POISON << 4)) act[i].hurt_t = 18;
    num_add(foe_x(i), FOOT_Y - 40 - (e->a & 2 ? 8 : 0), e->v, (e->a & 1) ? 1 : ((e->a >> 4) == PK_POISON ? 6 : 0));
    if ((e->a >> 4) != PK_POISON) fx_add(foe_x(i) - 8, FOOT_Y - 44, (e->c & 0xff) == WT_SPEAR ? 2 : ((e->c & 0xff) == WT_HAMMER ? 3 : 1));
    if ((e->b & 1) && !(banner_sp && banner_t)) banner(T_WEAK, PAL_T_YELLOW, 30);   /* the special's name stays */
    if (e->a & 1) shake_t = 6;
    if (act[i].boss) top_dirty = 1;   /* its HP bar is in the top bar */
    hp_dirty = 1;
    break;
  case EV_DEFEAT:
    act[e->enemy].hurt_t = 0;
    se_play(SE_DEFEAT);
    break;
  case EV_FLEE:
    banner(T_FLEE, PAL_T_WHITE, 40);
    break;
  case EV_REWARD:
    if (e->v) { num_add(foe_x(e->enemy), FOOT_Y - 12, e->v, 4); se_play(SE_COIN); }   /* below the damage numbers */
    if (e->a != ITEM_NONE) se_play(SE_DROP);
    top_dirty = 1;
    break;
  case EV_STUN:
    banner(T_STUN, PAL_T_YELLOW, 30);
    se_play(SE_STUN);
    break;
  case EV_DRAIN:
    banner(T_DRAIN, PAL_T_RED, 48);
    break;
  case EV_STDRAIN:
    banner(T_STDRAIN, PAL_T_RED, 48);
    targ_num = D_ST_DRAIN;
    hero_msg(TXT_ST_MINUS, PAL_T_RED);
    hp_dirty = 1;
    break;
  case EV_LOCK_GUARDED:
    banner(T_LOCK_GUARDED, PAL_T_YELLOW, 48);
    break;
  case EV_UNLOCK:
    panel_dirty = 1;
    break;
  case EV_TALK:
    talk_show(e->c, e->a);
    break;
  case EV_TALK_END:
    talk_close();
    break;
  case EV_FORM_START:
    se_play(SE_TRANSFORM);
    break;
  case EV_FORM_BEAT:
    shake_t = 4;
    break;
  case EV_FORM_DARK:
    bright = 6;
    break;
  case EV_FORM_SWAP:
    act[e->enemy].mon = b_e[e->enemy].mon;
    copy_asset(mon_gfx[b_e[e->enemy].mon][0], bdesc, sizeof(bdesc));
    act[e->enemy].npose = bdesc[0];
    parse_desc(bdesc, act[e->enemy].off, act[e->enemy].code, 1);
    pose_fill(&act[e->enemy]);
    load_pal(mon_gfx[b_e[e->enemy].mon][1], 96);
    boss_upload(e->enemy, pose_by_code(&act[e->enemy], 0));
    bgm_play(battle_bgm());
    break;
  case EV_FORM_RISE:
    bright = 15;
    banner(FORM_TEXT_1 + (e->a > 1 ? 1 : 0), PAL_T_RED, 60);
    break;
  case EV_FORM_END:
    top_dirty = 1;
    break;
  case EV_AWAKEN:
    banner(AWAKEN_TEXT, PAL_T_YELLOW, 60);
    act[e->enemy].flash = 20;
    top_dirty = 1;
    break;
  case EV_STAGE_CLEAR:
    banner(T_STAGE_CLEAR, PAL_T_YELLOW, 60);
    se_play(SE_MIRACLE);
    break;
  case EV_VICTORY:
    if (b_mode == BM_NORMAL) banner(T_CLEAR, PAL_GOLD, 255);   /* the phone: CLEAR in adventures */
    else banner(T_VICTORY, PAL_T_YELLOW, 255);
    bgm_play(BGM_VICTORY);
    win_t = 1;
    break;
  case EV_GAMEOVER:
    banner(T_GAMEOVER, PAL_T_RED, 255);
    bgm_play(BGM_GAMEOVER);
    lose_t = 1;
    break;
  case EV_ST_GAIN:                /* a = ST now, b = the gain */
    targ_num = e->b;
    hero_msg(TXT_ST_PLUS, PAL_BLUE);
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_HP_COST:
  case EV_FUNBARI:
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_MEAT_BEG:
    banner(T_MEAT_ASK, PAL_T_YELLOW, 50);
    panel_dirty = 1;
    break;
  case EV_MEAT_GIFT:
    if (e->a != ITEM_NONE) { banner(T_MEAT_GIFT, PAL_T_YELLOW, 70); se_play(SE_DROP); }
    hero_msg(T_MEAT_THANKS, PAL_T_YELLOW);
    break;
  case EV_TARGET:
    top_dirty = 1;
    break;
  case EV_MIRAGE_REWARD:
    break;
  }
}

/* ---------------------------------------------------------------- hero pose */
static u8 hp_attack(void) {
  u8 wt = hero_wt(), k, sk = b_last_special;
  u16 t = D_ATTACK_MOTION_FRAMES - b_hero_timer;
  static const u8 sp_base[12] = { HP_SP_JUMPGIRI_1, HP_SP_MOACLIMB_1, HP_SP_CYCLONE_1, HP_SP_SWORDDANCE_1, HP_SP_KABUTOWARI_1,
                                  HP_SP_SHOURYUU_1, HP_SP_MEGAROLL_1, HP_SP_HEAVYBOMB_1, HP_SP_KANTSUU_1, HP_SP_SKYDIVE_1,
                                  HP_SP_SENPUU_1, HP_SP_MULTILANCE_1 };
  if (sk != 255 && sk < 12) {
    k = (u8)(t * 8 / D_ATTACK_MOTION_FRAMES);
    return sp_base[sk] + MIN(k, 7);
  }
  if (wt == WT_SPEAR) return t < 6 ? HP_SPEAR_READY_P : HP_SPEAR_THRUST_P;
  if (wt == WT_HAMMER) return t < 8 ? HP_HAMMER_UP_H : HP_HAMMER_DOWN_H;
  return t < 6 ? HP_SWORD_WINDUP_S : HP_SWORD_SWING_S;
}

static u8 hero_pose(void) {
  u8 wt;
  if (lose_t) return HP_DOWN;
  if (win_t) { wt = hero_wt(); return wt == WT_SPEAR ? HP_VICTORY_P : (wt == WT_HAMMER ? HP_VICTORY_H : HP_VICTORY_S); }
  if (hero_slide < 0) return HP_WALK1 + (u8)((frame_ct >> 3) & 3);
  if (hero_hurt) return hero_hurt > 10 ? HP_RECOIL_BIG : HP_RECOIL;
  if (hero_parry) return HP_PARRY;
  switch (b_hero_state) {
  case HS_ATTACK: return hp_attack();
  case HS_CHARGE: wt = hero_wt(); return wt == WT_SPEAR ? HP_CHARGE_P : (wt == WT_HAMMER ? HP_CHARGE_H : HP_CHARGE_S);
  case HS_EAT: return b_hero_timer > 30 ? HP_EAT1 : (b_hero_timer > 15 ? HP_EAT2 : HP_EAT3);
  case HS_GUARD: return HP_GUARD;
  }
  if ((frame_ct & 127) < 4) return HP_BLINK;
  return (frame_ct & 64) ? HP_IDLE2 : HP_IDLE1;
}

/* ---------------------------------------------------------------- sprites */
/* pose pieces (32x32 cells of the strip) at (x, y): src/asm/sprite.s meta_add */
static void draw_meta(const u8 *d, u8 strip, s16 x, s16 y, u8 pal, u8 prio) {
  ea[0] = (u16)d; ea[1] = STRIP_TILE(strip); ea[2] = x; ea[3] = y;
  ea[4] = ((u16)pal << 9) | ((u16)prio << 12);
  meta_add();
}

static void draw_number(const Num *p, s16 y) {
  u8 i, n = p->n;
  u16 base = STRIP_TILE(STRIP_FX) + (p->col == 1 || p->col == 4 ? 32 : 0);
  s16 x = p->x - n * 4;
  for (i = 0; i < n; ++i, x += 8) {
    spr_add(x, y, ATTR(base + p->dig[i], OBJ_FX, 3), 0);
    spr_add(x, y + 8, ATTR(base + 16 + p->dig[i], OBJ_FX, 3), 0);
  }
}

/* sprite lists rebuilt only when what they show changes, put out by src/asm/sprite.s spr_list */
typedef struct { s16 x, y; u16 ta, big; } SprEnt;
#define OV_MAX 9
static SprEnt ov_spr[ENEMY_MAX][OV_MAX], sk_spr[30];
static u8 ov_n[ENEMY_MAX], ov_sig[ENEMY_MAX][7], sk_n;

static void spr_emit(const SprEnt *p, u8 n) {
  if (!n) return;
  ea[0] = (u16)p; ea[1] = n;
  spr_list();
}

static SprEnt *ent;
static void ent_add(s16 x, s16 y, u16 ta) { ent->x = x; ent->y = y; ent->ta = ta; ent->big = 0; ++ent; }

static void lists_reset(void) {
  memset(ov_sig, 0xff, sizeof(ov_sig));
  memset(sk_col, 0, sizeof(sk_col));
  sk_col[0][0] = sk_col[1][0] = sk_col[2][0] = 255;
  sk_recalc = 1;
  memset(ov_n, 0, sizeof(ov_n));
  sk_n = 0;
}

/* panel sprites: the skill icon (16x16, palette = selected / not) and the cost badge of each
 * card, the small gray badge of each NEXT card */
/* the panel sprites keep OAM slots 96-127 (sprite.s oam_top): written only when they change,
 * not every frame */
#define PANEL_OAM 96
static u8 panel_oam_on;
static void panel_oam(u8 show) {   /* call before this frame's other sprites (no group is part-filled) */
  u16 used = oam_used;
  oam_top = 128;
  oam_next = PANEL_OAM; oam_used = 128;
  if (show) spr_emit(sk_spr, sk_n);
  oam_finish();                    /* hides the rest of 96-127 */
  oam_used = used;
  oam_top = PANEL_OAM;
  panel_oam_on = show;
}

static void skill_sprites(void) {
  u8 i, x, *k;
  if (sk_recalc) {               /* a column was sent this frame: its sprites go in the same NMI */
    sk_recalc = 0;
    ent = sk_spr;
    for (i = 0, k = sk_col[0]; i < 3; ++i, k += 3) {
      x = i * 64;
      if (k[0] != 255) {
        ent_add(x + 10, 164, ATTR(STRIP_TILE(STRIP_MISC) + 2 + i * 2, OBJ_SKILL + k[0], 3));
        ent_add(x + 18, 164, ATTR(STRIP_TILE(STRIP_MISC) + 3 + i * 2, OBJ_SKILL + k[0], 3));
        ent_add(x + 10, 172, ATTR(STRIP_TILE(STRIP_MISC) + 18 + i * 2, OBJ_SKILL + k[0], 3));
        ent_add(x + 18, 172, ATTR(STRIP_TILE(STRIP_MISC) + 19 + i * 2, OBJ_SKILL + k[0], 3));
      }
      if (k[1]) {
        u16 t = STRIP_TILE(STRIP_MISC) + MISC_BADGE + (k[1] == 2 ? 2 : 0);
        ent_add(x + 40, 160, ATTR(t, OBJ_FX, 3));
        ent_add(x + 48, 160, ATTR(t + 1, OBJ_FX, 3));
        ent_add(x + 40, 168, ATTR(t + 16, OBJ_FX, 3));
        ent_add(x + 48, 168, ATTR(t + 17, OBJ_FX, 3));
      }
      if (k[2]) {
        ent_add(x + 42, 202, ATTR(STRIP_TILE(STRIP_MISC) + MISC_BADGE + 6, OBJ_FX, 3));
        ent_add(x + 50, 202, ATTR(STRIP_TILE(STRIP_MISC) + MISC_BADGE + 7, OBJ_FX, 3));
      }
    }
    sk_n = (u8)(ent - sk_spr);
    panel_oam(1);
  } else if (!panel_oam_on) panel_oam(1);
}

/* target arrow, count bubble, HP bar, ailments of enemy i */
static void overlay_sprites(u8 i, Foe *e, Actor *a) {
  u8 sig[7], c, hp, *os;
  s16 x, y;
  SprEnt *ol;
  u16 *hq, *bq;
  if (e->gone || !foe_targetable(e)) return;
  if (a->boss && warn_t) return;
  x = e->sx + a->slide + a->dx;
  /* (two 16-bit compares: a 32-bit one is a slow runtime call; the bar's division only when
   * the HP changed) */
  hq = (u16 *)&e->hp; bq = (u16 *)&a->bar_hp;
  if (!a->bar_ok || hq[0] != bq[0] || hq[1] != bq[1]) {
    a->bar_ok = 1;
    hp = e->hp_max ? (u8)((u32)e->hp * 24 / e->hp_max) : 0;
    if (e->hp && !hp) hp = 1;
    a->bar = hp; a->bar_hp = e->hp;
  }
  hp = a->bar;
  c = e->stun_turns ? 255 : MIN(e->count, 9);
  sig[0] = (u8)x; sig[1] = (u8)((u16)x >> 8);
  sig[2] = i == b_target ? 1 + ((frame_ct >> 3) & 1) : 0;
  sig[3] = c; sig[4] = hp;
  sig[5] = (e->ail[AIL_POISON] ? 1 : 0) | (e->ail[AIL_PARALYZE] ? 2 : 0) | (e->ail[AIL_BLIND] ? 4 : 0);
  sig[6] = a->boss ? (u8)boss_top : 0;
  os = ov_sig[i];
  ol = ov_spr[i];
  if (memcmp(sig, os, 7)) {
    memcpy(os, sig, 7);
    /* zako: above the head; boss: at the picture's top, below the top bar (rows 0-1) */
    y = a->boss ? MAX(boss_top + 4, FIELD_Y + 14) : FOOT_Y - 56;
    ent = ol;
    if (sig[2]) ent_add(x - 4, y - 10 + (sig[2] - 1), ATTR(STRIP_TILE(STRIP_MISC) + MISC_BAR + 9, OBJ_FX, 3));
    if (c != 255) ent_add(x + 6, y - 10, ATTR(STRIP_TILE(STRIP_FX) + (c < 6 ? 10 + c : 16 + 10 + (c - 6)), OBJ_FX, 3));
    else ent_add(x + 6, y - 10, ATTR(STRIP_TILE(STRIP_MISC) + MISC_BAR + 10, OBJ_FX, 3));
    if (!a->boss) {            /* the boss's HP bar is in the top bar */
      ent_add(x - 12, y - 2, ATTR(STRIP_TILE(STRIP_MISC) + MISC_BAR + MIN(hp, 8), OBJ_FX, 3));
      ent_add(x - 4, y - 2, ATTR(STRIP_TILE(STRIP_MISC) + MISC_BAR + (hp > 8 ? MIN(hp - 8, 8) : 0), OBJ_FX, 3));
      ent_add(x + 4, y - 2, ATTR(STRIP_TILE(STRIP_MISC) + MISC_BAR + (hp > 16 ? MIN(hp - 16, 8) : 0), OBJ_FX, 3));
    }
    if (sig[5] & 1) ent_add(x - 12, y - 10, ATTR(STRIP_TILE(STRIP_MISC) + MISC_BAR + 11, OBJ_FX, 3));
    if (sig[5] & 2) ent_add(x - 20, y - 10, ATTR(STRIP_TILE(STRIP_MISC) + MISC_BAR + 12, OBJ_FX, 3));
    if (sig[5] & 4) ent_add(x - 28, y - 10, ATTR(STRIP_TILE(STRIP_MISC) + MISC_BAR + 13, OBJ_FX, 3));
    ov_n[i] = (u8)(ent - ol);
  }
  spr_emit(ol, ov_n[i]);
}

static void shield_sprites(s16 x, s16 y) {
  spr_add(x, y, ATTR(STRIP_TILE(STRIP_MISC) + 0, OBJ_SHIELD, 2), 0);
  spr_add(x + 8, y, ATTR(STRIP_TILE(STRIP_MISC) + 1, OBJ_SHIELD, 2), 0);
  spr_add(x, y + 8, ATTR(STRIP_TILE(STRIP_MISC) + 16, OBJ_SHIELD, 2), 0);
  spr_add(x + 8, y + 8, ATTR(STRIP_TILE(STRIP_MISC) + 17, OBJ_SHIELD, 2), 0);
}

static void draw_sprites(void) {
  u8 i, p;
  s16 hx = HERO_X + hero_slide;
  Actor *a;
  Foe *e;
  Num *n;
  Fx *f;
  const s8 *sp;
  /* the panel's (fixed slots, first: no group of 4 slots is part-filled yet) */
  if (!modal_on) skill_sprites();
  else if (panel_oam_on) panel_oam(0);
  /* fx and numbers first (front) */
  for (i = 0, n = nums; i < NUM_MAX; ++i, ++n) if (n->t)
    draw_number(n, n->y - (u8)(45 - n->t) / 3);
  for (i = 0, f = fxs; i < FX_MAX; ++i, ++f) if (f->t)
    spr_add(f->x, f->y, ATTR(STRIP_TILE(STRIP_CELLS) + f->cell * 4, OBJ_FX, 3), 1);
  for (i = 0, e = b_e, a = act; i < b_ne; ++i, ++e, ++a) overlay_sprites(i, e, a);
  pose_tiles();
  /* shield (in front of the hero or behind, as the pose says) and the hero */
  p = hero_shown == 255 ? 0 : hero_shown;
  sp = hero_shield_pos[p];
  if (sp[2] && !lose_t) shield_sprites(hx + sp[0] - 8, FOOT_Y + sp[1] - 8);
  if (hero_shown != 255)
    draw_meta(hdesc + hero_off[hero_shown], STRIP_HERO, hx, FOOT_Y, OBJ_HERO, 2);
  if (!sp[2] && !lose_t) shield_sprites(hx + sp[0] - 8, FOOT_Y + sp[1] - 8);
  /* zako */
  for (i = 0, e = b_e, a = act; i < b_ne; ++i, ++e, ++a) {
    if (a->boss || e->gone || a->shown == 255) continue;
    if ((e->dying || e->fleeing || a->flash) && (frame_ct & 4)) continue;
    draw_meta(a->zd + a->off[a->shown], STRIP_ZAKO + a->slot, e->sx + a->slide + a->dx, FOOT_Y + 4, OBJ_ZAKO + a->slot, 2);
  }
  oam_finish();
}

/* ---------------------------------------------------------------- per-frame animation */
/* (loops walk pointers: indexing an array of structs costs a multiplication in cc65) */
static void animate(void) {
  u8 i, want, ph;
  Actor *a;
  Foe *e;
  ++frame_ct;
  { Num *n = nums; for (i = 0; i < NUM_MAX; ++i, ++n) if (n->t) --n->t; }
  { Fx *f = fxs; for (i = 0; i < FX_MAX; ++i, ++f) if (f->t) --f->t; }
  if (hero_slide < 0) hero_slide += 2;
  if (hero_hurt) --hero_hurt;
  if (hero_parry) --hero_parry;
  want = hero_pose();
  if (want != hero_pose_now) hero_upload(want);
  for (i = 0, a = act, e = b_e, ph = (u8)frame_ct; i < b_ne; ++i, ++a, ++e, ph += 23) {
    if (a->slide > 0) a->slide = a->slide > 6 ? a->slide - 6 : 0;
    if (e->fleeing) a->slide += 3;
    if (a->hurt_t) --a->hurt_t;
    if (a->atk_t) --a->atk_t;
    if (a->flash) --a->flash;
    if (bang_t[i]) --bang_t[i];
    if (e->dying || (e->gone && !a->boss)) want = pose_by_code(a, 4);
    else if (a->hurt_t) want = pose_by_code(a, 3);
    else if (a->atk_t || a->dx) want = pose_by_code(a, b_special_now && b_attacker == i ? 5 : 2);
    else if (e->mon == MON_117 && e->awake && 0) want = pose_by_code(a, 6);
    else want = pose_by_code(a, (ph & 64) ? 1 : 0);
    if (a->boss) {
      if (want != a->cur && !boss_pending) boss_upload(i, want);
    } else a->cur = want;     /* its tiles go with the OAM that draws it (pose_tiles) */
  }
  /* boss map once its tiles are in VRAM; position via BG1 scroll */
  for (i = 0, a = act, e = b_e; i < b_ne; ++i, ++a, ++e) {
    if (!a->boss) continue;
    if (boss_pending && !vq_busy(0xa000, 0xc000)) {
      boss_pending = 0;
      boss_place(i);
    }
    if (e->gone && a->boss_drawn) {
      u16 k;
      for (k = 2 * 32; k < 16 * 32; ++k) bg1map[k] = 0;
      bgmap_dirty |= 1;
      a->boss_drawn = 0;
    }
  }
  if (shake_t) { --shake_t; shake_x = (shake_t & 2) ? 2 : -2; shake_y = (shake_t & 1) ? 1 : 0; }
  else shake_x = shake_y = 0;
  /* HDMA: BG1 (boss) and BG2 (scenery) scroll for lines 0-127, 0 below (the panel) */
  {
    s16 bx = 0, by = 0;
    if (boss_i != 255) {
      a = &act[boss_i]; e = &b_e[boss_i];
      bx = -a->dx - shake_x;
      by = -shake_y;
      if ((e->dying || a->flash) && (frame_ct & 4)) by = 256;   /* blink: scroll the boss away */
      if (e->dying && (frame_ct & 4)) bx = 256;
    }
    if (bx != hd_bx || by != hd_by || shake_x != hd_sx || shake_y != hd_sy || !hdma_on) {   /* the tables only when the scroll changes */
    hd_bx = bx; hd_by = by; hd_sx = shake_x; hd_sy = shake_y;
    hdma_tab1[0] = 127; hdma_tab1[1] = (u8)bx; hdma_tab1[2] = (u8)(bx >> 8); hdma_tab1[3] = (u8)by; hdma_tab1[4] = (u8)(by >> 8);
    hdma_tab1[5] = 1; hdma_tab1[6] = (u8)bx; hdma_tab1[7] = (u8)(bx >> 8); hdma_tab1[8] = (u8)by; hdma_tab1[9] = (u8)(by >> 8);
    hdma_tab1[10] = 1; hdma_tab1[11] = 0; hdma_tab1[12] = 0; hdma_tab1[13] = 0; hdma_tab1[14] = 0; hdma_tab1[15] = 0;
    bx = -shake_x; by = -shake_y;
    hdma_tab2[0] = 127; hdma_tab2[1] = (u8)bx; hdma_tab2[2] = (u8)(bx >> 8); hdma_tab2[3] = (u8)by; hdma_tab2[4] = (u8)(by >> 8);
    hdma_tab2[5] = 1; hdma_tab2[6] = (u8)bx; hdma_tab2[7] = (u8)(bx >> 8); hdma_tab2[8] = (u8)by; hdma_tab2[9] = (u8)(by >> 8);
    hdma_tab2[10] = 1; hdma_tab2[11] = 0; hdma_tab2[12] = 0; hdma_tab2[13] = 0; hdma_tab2[14] = 0; hdma_tab2[15] = 0;
    hdma_on = 0xc0;
    }
  }
  if (!modal_on) {
    if (banner_t && banner_t != 255) { if (--banner_t == 0) box_hide(BX_BANNER); }
    if (hero_msg_t) { if (--hero_msg_t == 0) box_hide(BX_HERO); }
  }
  if (warn_t && --warn_t == 0) top_dirty = 1;
  /* colour math: the special's white flash, WARNING's three red flashes */
  if (flash_t) {
    --flash_t;
    cgwsel = 0; cgadsub = flash_t ? 0x3f : 0;
    coldata[0] = coldata[1] = coldata[2] = flash_t * 4;
  } else if (warn_t > 66) {
    cgwsel = 0; cgadsub = 0x3f;
    coldata[0] = ((90 - warn_t) & 4) ? 0 : 10; coldata[1] = coldata[2] = 0;
  } else if (warn_t == 66) {
    cgadsub = 0; coldata[0] = coldata[1] = coldata[2] = 0;
  }
  if (st_blink) { --st_blink; if ((st_blink & 3) == 0) draw_bars(); }
}

/* ---------------------------------------------------------------- mirage chest choice */
static u8 mirage_cursor, mirage_shown, mirage_prev;
static void mirage_draw(void) {
  u8 i;
  if (mirage_shown) {            /* cursor moved: the two lines it left and entered, in one NMI */
    mq_room(2 * 2 * 28 * 16);
    tb_clear(BX_PANEL);
  } else modal_open(2, 4, 28, 10);
  say(BX_PANEL, 10, TOP_Y_B, T_MIRAGE_PICK);
  for (i = 0; i < b_mirage_offer_n; ++i) {
    u8 k = b_mirage_offer[i][0], t = b_mirage_offer[i][1];
    targ_num = mirage_rewards[k].v[t];
    if (i == mirage_cursor) say(BX_PANEL, 16, TOP_Y_B + 16 + i * 16, TXT_CURSOR);
    says(BX_PANEL, 30, TOP_Y_B + 16 + i * 16, TXT_MIRAGE_ITEM, MIRN_0 + k);
  }
  if (mirage_shown) {            /* line i: tile rows 3+2i, 4+2i */
    tb_flush_rows_sync(BX_PANEL, 3 + 2 * mirage_prev, 2);
    tb_flush_rows_sync(BX_PANEL, 3 + 2 * mirage_cursor, 2);
  } else { modal_show(); mirage_shown = 1; }
  mirage_prev = mirage_cursor;
}

/* ---------------------------------------------------------------- main loop */
static void draw_sprites(void);
/* the phone's menu: WAVE n/m, 続ける / 離脱する (asked again before leaving) */
static u8 pause_menu(void) {
  u8 sel = 0, first = 1, ask = 0;
  for (;;) {
    if (first) modal_open(7, 5, 18, 10);
    else tb_clear(BX_PANEL);
    say_center(BX_PANEL, TOP_Y_B, T_PAUSE_TITLE);
    targ_num = b_stage; targ_num2 = b_stage_count;
    say_center(BX_PANEL, TOP_Y_B + 16, T_PAUSE_WAVE);
    say(BX_PANEL, 24, TOP_Y_B + 32 + sel * 16, TXT_CURSOR);
    say(BX_PANEL, 38, TOP_Y_B + 32, T_PAUSE_RESUME);
    say(BX_PANEL, 38, TOP_Y_B + 48, ask ? T_PAUSE_LEAVE2 : T_PAUSE_LEAVE);
    if (first) { modal_show(); draw_sprites(); first = 0; }   /* the panel sprites go with their text */
    else tb_flush(BX_PANEL);
    do { frame_wait(); } while (!(pad_new & (BTN_UP | BTN_DOWN | BTN_A | BTN_B | BTN_START)));
    if (pad_new & (BTN_UP | BTN_DOWN)) { sel ^= 1; ask = 0; se_play(SE_CURSOR); continue; }
    if (pad_new & (BTN_B | BTN_START)) { sel = 0; se_play(SE_CANCEL); break; }
    if (pad_new & BTN_A) {
      se_play(SE_OK);
      if (sel == 1 && !ask) { ask = 1; continue; }
      break;
    }
  }
  modal_close();
  return sel;
}

/* returns 1 = won, 0 = lost, 2 = left */
u8 battle_scene(u8 area, u8 men, const Loadout *lo) {
  Ev e;
  u8 i, result = 0;
  prof_ready = 0;
  screen_off();
  sys_init();
  memset(nums, 0, sizeof(nums));
  memset(fxs, 0, sizeof(fxs));
  memset(act, 0, sizeof(act));
  lists_reset();
  memset(skill_icon_shown, 0xfe, 3);
  lose_t = win_t = 0;
  banner_t = hero_msg_t = banner_sp = warn_t = flash_t = 0;
  combo_shown = 0;
  combo_w = 0;
  talk_open = 0;
  boss_pending = 0;
  mirage_shown = 0;
  modal_on = 0;
  pause_req = 0;
  panel_oam_on = 0;
  oam_top = PANEL_OAM;           /* slots 96-127: the panel sprites (sys_init hid them) */
  battle_start(area, men, lo);
  hero_setup();
  misc_setup();
  bg_setup();
  hud_boxes();
  bgmode = 0x09;
  tm_main = 0x17;
  set_bg3_palettes();
  cgram[0] = 0x0c63;   /* backdrop behind the HUD rows */
  while (ev_pop(&e)) on_event(&e);
  animate();
  draw_top(); draw_hpst(); draw_panel();
  while (vq_count || mq_on) wait_frame();   /* the panel sprites go with their text */
  do { draw_sprites(); wait_frame(); } while (poses_waiting());   /* the poses' tiles (screen off) */
  draw_sprites();
  screen_on();
  tut_open = tut_wait = tut_due = 0;
  for (;;) {
    frame_wait();
#ifdef PROFILE
    if (!prof_ready && battle_can_command()) { prof_ready = 1; PROF_MARK(1); }
    ++prof_marks[6];
#endif
    if (tut_due && battle_can_command() && !b_npending && !b_npopups) { tut_show(tut_due - 1); tut_due = 0; }
    if (tut_open) {                  /* tutorial window: the battle waits */
      if (pad_new & BTN_A) { se_play(SE_OK); tut_ok(); }
      animate();
      draw_sprites();
      continue;
    }
    /* input */
    if (b_phase == PH_TALK) { if (pad_new & (BTN_A | BTN_B)) battle_press_next(); }
    else if (b_phase == PH_REWARD && mirage_shown) {
      if (pad_new & (BTN_UP | BTN_DOWN)) {
        mirage_cursor = (u8)((mirage_cursor + ((pad_new & BTN_UP) ? b_mirage_offer_n - 1 : 1)) % b_mirage_offer_n);
        se_play(SE_CURSOR);
        mirage_draw();
      }
      if (pad_new & BTN_A) {
        se_play(SE_OK);
        battle_mirage_pick(mirage_cursor);
        modal_close();
        mirage_shown = 0;
      }
    } else if (b_phase == PH_FIGHT) {
      if (pad_new & BTN_Y) battle_press_skill(0);
      if (pad_new & BTN_X) battle_press_skill(1);
      if (pad_new & BTN_A) battle_press_skill(2);
      if (pad_new & BTN_B) battle_press_guard();
      if (pad_new & BTN_R) battle_press_meat();
      if (pad_new & (BTN_LEFT | BTN_RIGHT)) {
        for (i = 1; i < b_ne; ++i) {
          u8 t = (u8)((b_target + ((pad_new & BTN_LEFT) ? b_ne - i : i)) % b_ne);
          if (battle_set_target(t)) { se_play(SE_CURSOR); break; }
        }
      }
      if (pad_new & BTN_START) pause_req = 1;
      /* the battle waits while the menu is open; it opens after a special's cut-in or WARNING */
      if (pause_req && !flash_t && !(banner_sp && banner_t) && warn_t <= 66) {
        pause_req = 0;
        if (pause_menu()) { result = 2; break; }
      }
    }
    if (b_phase != PH_FIGHT) pause_req = 0;
    battle_step();
    while (ev_pop(&e)) on_event(&e);
    if (b_phase == PH_REWARD && !b_npending && mirage_cursor < 255 && !mirage_shown) { mirage_cursor = 0; mirage_draw(); }
    animate();
    if (top_dirty) draw_top();
    if (hp_dirty) draw_hpst();
    if (!modal_on) panel_part(3, 0, guard_off());   /* the cooldown ends between redraws */
    if (!modal_on && combo_shown != b_combo) draw_combo();
    /* last before the sprites: a column's text and its sprites go in the next NMI */
    if (panel_dirty || panel_can != battle_can_command() || panel_st != b_st) draw_panel();
    draw_sprites();
    if (win_t) { if (++win_t > 150) { result = 1; break; } }
    if (lose_t) { if (++lose_t > 180) { result = 0; break; } }
  }
  fade_out();
  hdma_on = 0;
  oam_top = 128;
  oam_clear();
  return result;
}

/* monster picture assets for other screens (dex): 0 pose descriptor, 1 palette, 2 boss (BG) */
u16 mon_gfx_get(u8 m, u8 k) { return mon_gfx[m][k]; }
