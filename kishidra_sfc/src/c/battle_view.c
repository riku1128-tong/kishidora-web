/* Battle screen: draws the battle from the rules' events (src/c/battle_*.c). */
#include "game.h"
#include "battle.h"
#include "gfxtab.h"
#include "sound.h"
#include <string.h>

/* ---------------------------------------------------------------- layout */
#define FIELD_Y   16           /* rows 2-17 */
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

/* BG3 boxes */
#define BX_TOP 0
#define BX_HPST 1
#define BX_PANEL 2
#define BX_BANNER 3
#define BX_HERO 4
#define T_TOP 1
#define T_HPST (T_TOP + 64)
#define T_PANEL (T_HPST + 64)
#define T_BANNER (T_PANEL + 256)
#define T_HERO (T_BANNER + 64)

/* ---------------------------------------------------------------- actors */
typedef struct {
  u8 mon, boss, npose, slot;
  u8 cur, want;            /* pose index uploaded / wanted */
  u16 off[8];              /* descriptor offset of each pose */
  u8 code[8];
  u8 anim, hurt_t, atk_t, flash, idle_t;
  s8 dx;                   /* lunge / slide offset */
  s16 slide;
  u8 boss_buf;             /* which BG1 tile buffer holds the boss pose */
  u8 boss_drawn;
} Actor;

static u8 zdesc[3][96];
static u8 bdesc[96];
static u8 hdesc[2600];
static Actor act[ENEMY_MAX];
static u16 hero_off[N_HERO_POSE];
static u8 hero_pose_now = 255, hero_want;
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
static u8 panel_dirty, top_dirty, hp_dirty;
static u8 banner_t, hero_msg_t;
static u8 bang_t[ENEMY_MAX];
static u8 talk_open;
static u16 frame_ct;
static u8 lose_t, win_t;
static u8 sp_frame_t;
static u8 last_attack_sp;

typedef struct { s16 x, y; u32 v; u8 t, col; } Num;
#define NUM_MAX 8
static Num nums[NUM_MAX];
typedef struct { s16 x, y; u8 t, cell; } Fx;
#define FX_MAX 6
static Fx fxs[FX_MAX];

static void num_add(s16 x, s16 y, u32 v, u8 col) {
  u8 i;
  for (i = 0; i < NUM_MAX; ++i) if (!nums[i].t) break;
  if (i == NUM_MAX) i = 0;
  nums[i].x = x; nums[i].y = y; nums[i].v = v; nums[i].t = 45; nums[i].col = col;
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

static u8 pose_by_code(Actor *a, u8 c) {
  u8 i;
  for (i = 0; i < a->npose; ++i) if (a->code[i] == c) return i;
  if (c == 1 || c == 6) return pose_by_code(a, 0);
  if (c == 5) return pose_by_code(a, 2);
  return 0;
}

static u16 rd16(const u8 *p) { return p[0] | ((u16)p[1] << 8); }

/* queue the tiles of zako pose p of slot k */
static void zako_upload(u8 k, u8 p) {
  Actor *a = &act[k];
  const u8 *d = zdesc[a->slot] + a->off[p];
  u16 aid = rd16(d + 1), size;
  u16 buf = 0x3000 + a->slot * 0x0800;
  size = unpack(aid, buf);
  if (size > 2048) size = 2048;
  queue_vram(OBJ_VRAM(STRIP_ZAKO + a->slot), buf, 0x7f, size);
  a->cur = p;
}

static void hero_upload(u8 p) {
  const u8 *d = hdesc + hero_off[p];
  u16 aid = rd16(d + 1), size = unpack(aid, 0x2000);
  queue_vram(OBJ_VRAM(STRIP_HERO), 0x2000, 0x7f, size);
  hero_pose_now = p;
}

/* boss: tiles to the free buffer now, map written when the tiles have arrived */
static void boss_upload(u8 k, u8 p) {
  Actor *a = &act[k];
  const u8 *d = bdesc + a->off[p];
  u16 ta = rd16(d + 1), ma = rd16(d + 3), size;
  u8 buf = a->boss_buf ^ 1;
  size = unpack(ta, 0xa000);
  queue_vram((buf ? BOSS_TILE_B : BOSS_TILE_A) * 16, 0xa000, 0x7f, size);
  size = unpack(ma, 0xd000);
  ea[0] = 0xd000; ea[1] = 0x7f; ea[2] = (u16)boss_map; ea[3] = MIN(size, sizeof(boss_map));
  far_copy();
  boss_w = d[5]; boss_h = d[6];
  boss_ax = (s8)d[7]; boss_ay = (s8)d[8];
  a->boss_buf = buf;
  a->cur = p;
  boss_pending = 2;
}

static void boss_place(u8 k) {
  Actor *a = &act[k];
  u8 x, y, tx0, ty0;
  u16 add = a->boss_buf ? BOSS_TILE_B : BOSS_TILE_A, w, i;
  s16 px = a->slide + b_e[k].sx + boss_ax, py = FOOT_Y + 4 + boss_ay;
  /* clear rows 2-17 */
  for (i = 2 * 32; i < 18 * 32; ++i) bg1map[i] = 0;
  if (px < 0) px = 0;
  tx0 = (u8)(px >> 3);
  ty0 = (u8)(py >> 3);
  boss_x0 = (s16)tx0 * 8 - px;
  boss_y0 = (s16)ty0 * 8 - py;
  for (y = 0; y < boss_h; ++y) {
    if (ty0 + y < 2 || ty0 + y > 17) continue;
    for (x = 0; x < boss_w; ++x) {
      if (tx0 + x > 31) continue;
      w = rd16(boss_map + ((u16)y * boss_w + x) * 2);
      bg1map[((u16)(ty0 + y) << 5) + tx0 + x] = w ? w + add : 0;
    }
  }
  bgmap_dirty |= 1;
  a->boss_drawn = 1;
}

/* ---------------------------------------------------------------- set up */
static void load_stage_gfx(void) {
  u8 i, z = 0;
  u16 i16;
  Actor *a;
  for (i16 = 2 * 32; i16 < 18 * 32; ++i16) bg1map[i16] = 0;
  bgmap_dirty |= 1;
  for (i = 0; i < b_ne; ++i) {
    a = &act[i];
    memset(a, 0, sizeof(Actor));
    a->mon = b_e[i].mon;
    a->boss = (u8)mon_gfx[a->mon][2];
    a->cur = 255;
    if (a->boss) {
      copy_asset(mon_gfx[a->mon][0], bdesc, sizeof(bdesc));
      a->npose = bdesc[0];
      parse_desc(bdesc, a->off, a->code, 1);
      load_pal(mon_gfx[a->mon][1], 96);
      a->boss_buf = 1;
      boss_upload(i, pose_by_code(a, 0));
    } else {
      a->slot = z++;
      copy_asset(mon_gfx[a->mon][0], zdesc[a->slot], sizeof(zdesc[0]));
      a->npose = zdesc[a->slot][0];
      parse_desc(zdesc[a->slot], a->off, a->code, 0);
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
}

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
  load_vram(ASSET_HUD_TILES, VRAM_BG1_TILES);
  load_pal(ASSET_HUD_PAL, 80);
  for (i = 18 * 32; i < 20 * 32; ++i) bg1map[i] = 0;
}

/* skill icon of slot s (current) into misc strip tiles 2+s*2 */
static u8 skill_icon_shown[3];
static u8 skill_sel[N_SKILL];
static void skill_icons_update(void) {
  u8 s, sk;
  u16 src;
  for (s = 0; s < 3; ++s) {
    sk = skill_of(s);
    if (sk == skill_icon_shown[s]) continue;
    skill_icon_shown[s] = sk;
    if (sk == SK_NONE) continue;
    unpack(ASSET_SKILL_ICONS, 0x6800);
    src = 0x6800 + (u16)sk * 128;
    queue_vram(OBJ_VRAM(STRIP_MISC) + (2 + s * 2) * 16, src, 0x7f, 64);
    queue_vram(OBJ_VRAM(STRIP_MISC) + (18 + s * 2) * 16, src + 64, 0x7f, 64);
  }
}

static void bg_setup(void) {
  u16 id = area_bg[b_area];
  if (b_area == AR_CASTLE && b_men == D_CASTLE_MEN) id = BG_CASTLE_FINAL;
  load_vram(id, VRAM_BG2_TILES);
  load_map(id + 1, 1, 0, 2);
  load_pal(id + 2, 32);
}

/* ---------------------------------------------------------------- HUD text */
static void hud_boxes(void) {
  box(BX_TOP, 0, 0, 32, 2, T_TOP, PAL_W, BOX_OPAQUE);
  box(BX_PANEL, 0, 20, 32, 8, T_PANEL, PAL_W, BOX_WINDOW | BOX_LOW);
  box(BX_BANNER, 0, 8, 32, 2, T_BANNER, PAL_T_YELLOW, 0);
  box(BX_HERO, 1, 9, 11, 2, T_HERO, PAL_T_WHITE, 0);
  copy_asset(ASSET_SKILL_ICONS_SEL, skill_sel, N_SKILL);
}

static void draw_top(void) {
  Foe *t;
  tb_clear(BX_TOP);
  targ_num = b_stage; targ_num2 = b_stage_count;
  say(BX_TOP, 4, 2, TXT_TOP_WAVE);
  if (b_target < b_ne) {
    t = &b_e[b_target];
    if (foe_targetable(t)) say(BX_TOP, 74, 2, t->awake ? NAME_SECRET_AWAKE : MONN_0 + t->mon);
  }
  sayn(BX_TOP, 204, 2, TXT_COINS, b_coins);
  tb_flush(BX_TOP);
  top_dirty = 0;
}

/* HUD glyph tiles on BG1 (palette 5): digits, '/', H P S T */
#define HUD_T(c) (HUD_GLYPH0 + (c))
#define HUD_SLASH HUD_T(10)
#define HUD_H HUD_T(11)
#define HUD_P HUD_T(12)
#define HUD_S HUD_T(13)
#define HUD_TT HUD_T(14)
#define HUD_ATTR ((5 << 10) | 0x2000)
static void hud_put(u8 row, u8 col, u16 t) { bg1map[row * 32 + col] = t | HUD_ATTR; }

/* "a/b" right-aligned so that the last digit is at col 30; clears cols 22-30 */
static void hud_frac(u8 row, u16 a, u16 b) {
  u8 col = 30, i;
  for (i = 22; i <= 30; ++i) bg1map[row * 32 + i] = 0;
  do { hud_put(row, col--, HUD_T(b % 10)); b /= 10; } while (b);
  hud_put(row, col--, HUD_SLASH);
  do { hud_put(row, col--, HUD_T(a % 10)); a /= 10; } while (a && col >= 22);
}

static void draw_hpst(void) {
  u16 i, fill, t;
  u8 st;
  hud_put(18, 0, HUD_H); hud_put(18, 1, HUD_P);
  hud_put(19, 0, HUD_S); hud_put(19, 1, HUD_TT);
  hud_frac(18, b_hp, b_hp_max);
  hud_frac(19, b_st, b_st_max);
  /* HP bar: 18 tiles at cols 3-20 (144 px) */
  fill = b_hp_max ? (u16)((u32)b_hp * 144 / b_hp_max) : 0;
  if (b_hp && !fill) fill = 1;
  t = (b_hp * 4 > b_hp_max) ? (b_hp * 2 > b_hp_max ? 1 : 10) : 19;
  for (i = 0; i < 18; ++i) {
    u16 f = fill > i * 8 ? MIN(fill - i * 8, 8) : 0;
    bg1map[18 * 32 + 3 + i] = (t + f) | HUD_ATTR;
  }
  st = b_st;
  for (i = 0; i < 10 && i < b_st_max; ++i) bg1map[19 * 32 + 3 + i] = (28 + (i < st ? 8 : 0)) | HUD_ATTR;
  bgmap_dirty |= 1;
  hp_dirty = 0;
}

static void draw_panel(void) {
  u8 s, sk, nx, cost, sp;
  u16 y;
  tb_clear(BX_PANEL);
  for (s = 0; s < 3; ++s) {
    y = 4 + s * 14;
    sk = skill_of(s);
    say(BX_PANEL, 6, y, TXT_BTN_Y + s);
    if (sk == SK_NONE) { say(BX_PANEL, 34, y, T_SLOT_EMPTY); continue; }
    if (is_locked(s)) { say(BX_PANEL, 34, y, T_LOCKED); continue; }
    say(BX_PANEL, 34, y, SKILLN_0 + sk);
    cost = skill_cost(sk);
    sayn(BX_PANEL, 112, y, cost > b_st ? TXT_COST_RED : TXT_COST, cost);
    sp = special_if(s);
    if (sp != 255) say(BX_PANEL, 148, y, TXT_SP_ARROW), say(BX_PANEL, 160, y, SPN_0 + sp);
    else {
      nx = next_skill_of(s);
      say(BX_PANEL, 148, y, TXT_NEXT_ARROW);
      if (nx != SK_NONE) say(BX_PANEL, 160, y, SKILLN_0 + nx);
    }
  }
  say(BX_PANEL, 6, 46, meat_asker() != 255 ? TXT_HELP_MEAT : T_BATTLE_HELP);
  tb_flush(BX_PANEL);
  panel_dirty = 0;
}

static void banner(u16 msg, u8 pal, u8 t) {
  tb[BX_BANNER * 8 + 6] = pal;
  box(BX_BANNER, 0, 8, 32, 2, T_BANNER, pal, 0);
  say_center(BX_BANNER, 2, msg);
  tb_flush(BX_BANNER);
  banner_t = t;
}

static void hero_msg(u16 msg, u8 pal) {
  box(BX_HERO, 1, 9, 11, 2, T_HERO, pal, 0);
  say_center(BX_HERO, 2, msg);
  tb_flush(BX_HERO);
  hero_msg_t = 40;
}

/* ---------------------------------------------------------------- talk window */
static void talk_show(u16 text, u8 who) {
  box(BX_PANEL, 0, 20, 32, 8, T_PANEL, PAL_W, BOX_WINDOW);
  say(BX_PANEL, 8, 3, who == MON_117 ? MONN_0 + MON_117 : (who == 255 ? NAME_KING : MONN_0 + maou_forms[0]));
  say(BX_PANEL, 8, 18, text);
  tb_flush(BX_PANEL);
  talk_open = 1;
}

static void talk_close(void) {
  box(BX_PANEL, 0, 20, 32, 8, T_PANEL, PAL_W, BOX_WINDOW | BOX_LOW);
  talk_open = 0;
  panel_dirty = 1;
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
  box(BX_PANEL, 0, 20, 32, 8, T_PANEL, PAL_W, BOX_WINDOW);
  targ_str = title;
  say(BX_PANEL, 8, 3, TXT_YELLOW_S);
  say(BX_PANEL, 12, 17, body);
  say(BX_PANEL, 196, 3, T_TALK_NEXT);
  tb_flush(BX_PANEL);
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
  box(BX_PANEL, 0, 20, 32, 8, T_PANEL, PAL_W, BOX_WINDOW | BOX_LOW);
  panel_dirty = 1;
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
    if (e->b) { banner(T_BOSS_WARN, PAL_T_RED, 90); se_play(SE_WARNING); }
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
    if (e->a == GD_MIRACLE) { hero_msg(T_RESULT_MIRACLE, PAL_T_YELLOW); se_play(SE_MIRACLE); hero_parry = 16; }
    else if (e->a == GD_JUST) { hero_msg(T_RESULT_JUST, PAL_T_YELLOW); se_play(SE_JUST); hero_parry = 14; }
    else if (e->a == GD_GUARD) { hero_msg(T_RESULT_GUARD, PAL_T_WHITE); se_play(SE_GUARD); hero_parry = 10; }
    else { se_play(SE_DAMAGE); hero_hurt = D_HERO_HURT_FRAMES; shake_t = 8; }
    if (e->v) num_add(HERO_X, FOOT_Y - 50, e->v, 2);
    if (e->c) num_add(HERO_X + 8, FOOT_Y - 40, e->c, 3);
    if (e->b & 4) banner(T_LOCK, PAL_T_RED, 60);
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_MISS:
    act[e->enemy].dx = 0;
    hero_msg(T_MISS, PAL_T_WHITE);
    break;
  case EV_GUARD:
    break;
  case EV_ATTACK:               /* a = slot, b = skill, c = special (255 = none) */
    last_attack_sp = (u8)e->c;
    sp_frame_t = 0;
    if (e->c != 255) { banner(SPN_0 + e->c, PAL_T_YELLOW, 60); se_play(SE_SPECIAL); }
    else if (e->a < 3) {
      u8 wt = items[b_w[e->a].item].type;
      se_play(e->b == SK_KICK ? SE_KICK : (e->b == SK_SAKEBU ? SE_SHOUT : (wt == WT_SPEAR ? SE_SPEAR : (wt == WT_HAMMER ? SE_HAMMER : SE_SLASH))));
    }
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_CHARGE:
    se_play(SE_CHARGE);
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_MEAT:
    se_play(SE_EAT);
    hp_dirty = 1; panel_dirty = 1;
    break;
  case EV_ST_SHORT:
    hero_msg(T_ST_SHORT, PAL_T_RED);
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
    if (e->b & 1) banner(T_WEAK, PAL_T_YELLOW, 30);
    if (e->a & 1) shake_t = 6;
    top_dirty = 1; hp_dirty = 1;
    break;
  case EV_DEFEAT:
    act[e->enemy].hurt_t = 0;
    se_play(SE_DEFEAT);
    break;
  case EV_FLEE:
    hero_msg(T_FLEE, PAL_T_WHITE);
    break;
  case EV_REWARD:
    if (e->v) { num_add(foe_x(e->enemy), FOOT_Y - 60, e->v, 4); se_play(SE_COIN); }
    if (e->a != ITEM_NONE) se_play(SE_DROP);
    top_dirty = 1;
    break;
  case EV_STUN:
    banner(T_STUN, PAL_T_YELLOW, 30);
    se_play(SE_STUN);
    break;
  case EV_DRAIN:
    hero_msg(T_DRAIN, PAL_T_RED);
    break;
  case EV_STDRAIN:
    hero_msg(T_STDRAIN, PAL_T_RED);
    hp_dirty = 1;
    break;
  case EV_LOCK_GUARDED:
    hero_msg(T_LOCK_GUARDED, PAL_T_YELLOW);
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
    banner(T_VICTORY, PAL_T_YELLOW, 255);
    bgm_play(BGM_VICTORY);
    win_t = 1;
    break;
  case EV_GAMEOVER:
    banner(T_GAMEOVER, PAL_T_RED, 255);
    bgm_play(BGM_GAMEOVER);
    lose_t = 1;
    break;
  case EV_ST_GAIN:
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
  k = skill_of(b_focus);
  (void)k;
  if (wt == WT_SPEAR) return t < 6 ? HP_SPEAR_READY_P : HP_SPEAR_THRUST_P;
  if (wt == WT_HAMMER) return t < 8 ? HP_HAMMER_UP_H : HP_HAMMER_DOWN_H;
  return t < 6 ? HP_SWORD_WINDUP_S : HP_SWORD_SWING_S;
}

static u8 hero_pose(void) {
  u8 wt = hero_wt();
  if (lose_t) return HP_DOWN;
  if (win_t) return wt == WT_SPEAR ? HP_VICTORY_P : (wt == WT_HAMMER ? HP_VICTORY_H : HP_VICTORY_S);
  if (hero_slide < 0) return HP_WALK1 + (u8)((frame_ct >> 3) & 3);
  if (hero_hurt) return hero_hurt > 10 ? HP_RECOIL_BIG : HP_RECOIL;
  if (hero_parry) return HP_PARRY;
  switch (b_hero_state) {
  case HS_ATTACK: return hp_attack();
  case HS_CHARGE: return wt == WT_SPEAR ? HP_CHARGE_P : (wt == WT_HAMMER ? HP_CHARGE_H : HP_CHARGE_S);
  case HS_EAT: return b_hero_timer > 30 ? HP_EAT1 : (b_hero_timer > 15 ? HP_EAT2 : HP_EAT3);
  case HS_GUARD: return HP_GUARD;
  }
  if ((frame_ct & 127) < 4) return HP_BLINK;
  return (frame_ct & 64) ? HP_IDLE2 : HP_IDLE1;
}

/* ---------------------------------------------------------------- sprites */
static void draw_meta(const u8 *d, u8 strip, s16 x, s16 y, u8 pal, u8 prio) {
  u8 n = d[4], k, cell;
  const u8 *p = d + 5;
  for (k = 0; k < n; ++k, p += 3) {
    cell = p[2];
    spr_add(x + (s8)p[0], y + (s8)p[1], ATTR(STRIP_TILE(strip) + (cell >> 2) * 64 + (cell & 3) * 4, pal, prio), 1);
  }
}

static void draw_number(s16 x, s16 y, u32 v, u8 col) {
  u8 dig[8], n = 0, i;
  u16 base = STRIP_TILE(STRIP_FX) + (col == 1 || col == 4 ? 32 : 0);
  do { dig[n++] = (u8)(v % 10); v /= 10; } while (v && n < 8);
  x -= n * 4;
  for (i = 0; i < n; ++i) {
    u8 d = dig[n - 1 - i];
    spr_add(x + i * 8, y, ATTR(base + d, OBJ_FX, 3), 0);
    spr_add(x + i * 8, y + 8, ATTR(base + 16 + d, OBJ_FX, 3), 0);
  }
}

static void draw_sprites(void) {
  u8 i, p, hp;
  s16 x, y, hx = HERO_X + hero_slide;
  Actor *a;
  Foe *e;
  const s8 *sp;
  /* fx and numbers first (front) */
  for (i = 0; i < NUM_MAX; ++i) if (nums[i].t) {
    draw_number(nums[i].x, nums[i].y - (45 - nums[i].t) / 3, nums[i].v, nums[i].col);
    --nums[i].t;
  }
  for (i = 0; i < FX_MAX; ++i) if (fxs[i].t) {
    spr_add(fxs[i].x, fxs[i].y, ATTR(STRIP_TILE(STRIP_CELLS) + fxs[i].cell * 4, OBJ_FX, 3), 1);
    --fxs[i].t;
  }
  /* skill icons on the panel */
  for (i = 0; i < 3; ++i) {
    u8 sk = skill_of(i);
    if (sk == SK_NONE) continue;
    p = skill_sel[sk];
    x = 16; y = 164 + i * 14;
    spr_add(x, y, ATTR(STRIP_TILE(STRIP_MISC) + 2 + i * 2, OBJ_SKILL + p, 3), 0);
    spr_add(x + 8, y, ATTR(STRIP_TILE(STRIP_MISC) + 3 + i * 2, OBJ_SKILL + p, 3), 0);
    spr_add(x, y + 8, ATTR(STRIP_TILE(STRIP_MISC) + 18 + i * 2, OBJ_SKILL + p, 3), 0);
    spr_add(x + 8, y + 8, ATTR(STRIP_TILE(STRIP_MISC) + 19 + i * 2, OBJ_SKILL + p, 3), 0);
  }
  /* enemy overlays: target arrow, count bubble, HP bar, ailments */
  for (i = 0; i < b_ne; ++i) {
    e = &b_e[i];
    if (e->gone) continue;
    x = foe_x(i);
    y = act[i].boss ? FIELD_Y + 4 : FOOT_Y - 56;
    if (foe_targetable(e)) {
      if (i == b_target) spr_add(x - 4, y - 10 + ((frame_ct >> 3) & 1), ATTR(STRIP_TILE(STRIP_FX) + 48 + 9, OBJ_FX, 3), 0);
      if (!e->stun_turns) {
        u8 c = MIN(e->count, 9);
        spr_add(x + 6, y - 10, ATTR(STRIP_TILE(STRIP_FX) + (c < 6 ? 10 + c : 16 + 10 + (c - 6)), OBJ_FX, 3), 0);
      } else spr_add(x + 6, y - 10, ATTR(STRIP_TILE(STRIP_FX) + 48 + 10, OBJ_FX, 3), 0);
      hp = e->hp_max ? (u8)((u32)e->hp * 24 / e->hp_max) : 0;
      if (e->hp && !hp) hp = 1;
      spr_add(x - 12, y - 2, ATTR(STRIP_TILE(STRIP_FX) + 48 + MIN(hp, 8), OBJ_FX, 3), 0);
      spr_add(x - 4, y - 2, ATTR(STRIP_TILE(STRIP_FX) + 48 + (hp > 8 ? MIN(hp - 8, 8) : 0), OBJ_FX, 3), 0);
      spr_add(x + 4, y - 2, ATTR(STRIP_TILE(STRIP_FX) + 48 + (hp > 16 ? MIN(hp - 16, 8) : 0), OBJ_FX, 3), 0);
      if (e->ail[AIL_POISON]) spr_add(x - 12, y - 10, ATTR(STRIP_TILE(STRIP_FX) + 48 + 11, OBJ_FX, 3), 0);
      if (e->ail[AIL_PARALYZE]) spr_add(x - 20, y - 10, ATTR(STRIP_TILE(STRIP_FX) + 48 + 12, OBJ_FX, 3), 0);
      if (e->ail[AIL_BLIND]) spr_add(x - 28, y - 10, ATTR(STRIP_TILE(STRIP_FX) + 48 + 13, OBJ_FX, 3), 0);
    }
  }
  /* shield (front of hero when the pose says so) */
  p = hero_pose_now == 255 ? 0 : hero_pose_now;
  sp = hero_shield_pos[p];
  if (sp[2] && !lose_t) {
    x = hx + sp[0] - 8; y = FOOT_Y + sp[1] - 8;
    spr_add(x, y, ATTR(STRIP_TILE(STRIP_MISC) + 0, OBJ_SHIELD, 2), 0);
    spr_add(x + 8, y, ATTR(STRIP_TILE(STRIP_MISC) + 1, OBJ_SHIELD, 2), 0);
    spr_add(x, y + 8, ATTR(STRIP_TILE(STRIP_MISC) + 16, OBJ_SHIELD, 2), 0);
    spr_add(x + 8, y + 8, ATTR(STRIP_TILE(STRIP_MISC) + 17, OBJ_SHIELD, 2), 0);
  }
  /* hero */
  if (hero_pose_now != 255 && !((lose_t && (frame_ct & 2)) && 0))
    draw_meta(hdesc + hero_off[hero_pose_now], STRIP_HERO, hx, FOOT_Y, OBJ_HERO, 2);
  if (!sp[2] && !lose_t) {
    x = hx + sp[0] - 8; y = FOOT_Y + sp[1] - 8;
    spr_add(x, y, ATTR(STRIP_TILE(STRIP_MISC) + 0, OBJ_SHIELD, 2), 0);
    spr_add(x + 8, y, ATTR(STRIP_TILE(STRIP_MISC) + 1, OBJ_SHIELD, 2), 0);
    spr_add(x, y + 8, ATTR(STRIP_TILE(STRIP_MISC) + 16, OBJ_SHIELD, 2), 0);
    spr_add(x + 8, y + 8, ATTR(STRIP_TILE(STRIP_MISC) + 17, OBJ_SHIELD, 2), 0);
  }
  /* zako */
  for (i = 0; i < b_ne; ++i) {
    a = &act[i];
    e = &b_e[i];
    if (a->boss || e->gone || a->cur == 255) continue;
    if ((e->dying || e->fleeing || a->flash) && (frame_ct & 4)) continue;
    draw_meta(zdesc[a->slot] + a->off[a->cur], STRIP_ZAKO + a->slot, foe_x(i), FOOT_Y + 4, OBJ_ZAKO + a->slot, 2);
  }
  oam_finish();
}

/* ---------------------------------------------------------------- per-frame animation */
static void animate(void) {
  u8 i, want;
  Actor *a;
  Foe *e;
  ++frame_ct;
  if (hero_slide < 0) hero_slide += 2;
  if (hero_hurt) --hero_hurt;
  if (hero_parry) --hero_parry;
  want = hero_pose();
  if (want != hero_pose_now) hero_upload(want);
  for (i = 0; i < b_ne; ++i) {
    a = &act[i];
    e = &b_e[i];
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
    else want = pose_by_code(a, ((frame_ct + i * 23) & 64) ? 1 : 0);
    if (a->boss) {
      if (want != a->cur && !boss_pending) boss_upload(i, want);
    } else if (want != a->cur) zako_upload(i, want);
  }
  /* boss map once its tiles are in VRAM; position via BG1 scroll */
  for (i = 0; i < b_ne; ++i) {
    a = &act[i];
    if (!a->boss) continue;
    if (boss_pending) {
      if (--boss_pending == 0 || !a->boss_drawn) boss_place(i);
    }
    if (b_e[i].gone && a->boss_drawn) {
      u16 k;
      for (k = 2 * 32; k < 18 * 32; ++k) bg1map[k] = 0;
      bgmap_dirty |= 1;
      a->boss_drawn = 0;
    }
  }
  if (shake_t) { --shake_t; shake_x = (shake_t & 2) ? 2 : -2; shake_y = (shake_t & 1) ? 1 : 0; }
  else shake_x = shake_y = 0;
  /* HDMA: BG1 (boss) and BG2 (scenery) scroll for lines 0-143, 0 below */
  {
    s16 bx = 0, by = 0;
    for (i = 0; i < b_ne; ++i) if (act[i].boss) {
      bx = -(act[i].dx + boss_x0 * 0) - shake_x;
      by = -shake_y;
      if ((b_e[i].dying || act[i].flash) && (frame_ct & 4)) by = 256 - 0;   /* blink: scroll the boss away */
      if (b_e[i].dying && (frame_ct & 4)) bx = 256;
    }
    hdma_tab1[0] = 127; hdma_tab1[1] = (u8)bx; hdma_tab1[2] = (u8)(bx >> 8); hdma_tab1[3] = (u8)by; hdma_tab1[4] = (u8)(by >> 8);
    hdma_tab1[5] = 17; hdma_tab1[6] = (u8)bx; hdma_tab1[7] = (u8)(bx >> 8); hdma_tab1[8] = (u8)by; hdma_tab1[9] = (u8)(by >> 8);
    hdma_tab1[10] = 1; hdma_tab1[11] = 0; hdma_tab1[12] = 0; hdma_tab1[13] = 0; hdma_tab1[14] = 0; hdma_tab1[15] = 0;
    bx = -shake_x; by = -shake_y;
    hdma_tab2[0] = 127; hdma_tab2[1] = (u8)bx; hdma_tab2[2] = (u8)(bx >> 8); hdma_tab2[3] = (u8)by; hdma_tab2[4] = (u8)(by >> 8);
    hdma_tab2[5] = 17; hdma_tab2[6] = (u8)bx; hdma_tab2[7] = (u8)(bx >> 8); hdma_tab2[8] = (u8)by; hdma_tab2[9] = (u8)(by >> 8);
    hdma_tab2[10] = 1; hdma_tab2[11] = 0; hdma_tab2[12] = 0; hdma_tab2[13] = 0; hdma_tab2[14] = 0; hdma_tab2[15] = 0;
    hdma_on = 0xc0;
  }
  if (banner_t && banner_t != 255) { if (--banner_t == 0) { tb_clear(BX_BANNER); tb_flush(BX_BANNER); } }
  if (hero_msg_t) { if (--hero_msg_t == 0) { tb_clear(BX_HERO); tb_flush(BX_HERO); } }
}

/* ---------------------------------------------------------------- mirage chest choice */
static u8 mirage_cursor;
static void mirage_draw(void) {
  u8 i;
  box(BX_PANEL, 0, 20, 32, 8, T_PANEL, PAL_W, BOX_WINDOW);
  say(BX_PANEL, 8, 3, T_MIRAGE_PICK);
  for (i = 0; i < b_mirage_offer_n; ++i) {
    u8 k = b_mirage_offer[i][0], t = b_mirage_offer[i][1];
    targ_num = mirage_rewards[k].v[t];
    say(BX_PANEL, 16, 17 + i * 14, i == mirage_cursor ? TXT_CURSOR : TXT_SPACE);
    says(BX_PANEL, 30, 17 + i * 14, TXT_MIRAGE_ITEM, MIRN_0 + k);
  }
  tb_flush(BX_PANEL);
}

/* ---------------------------------------------------------------- main loop */
static u8 pause_menu(void) {
  u8 sel = 0;
  box(BX_PANEL, 0, 20, 32, 8, T_PANEL, PAL_W, BOX_WINDOW);
  for (;;) {
    tb_clear(BX_PANEL);
    say(BX_PANEL, 8, 3, T_PAUSE_TITLE);
    say(BX_PANEL, 24, 18, sel == 0 ? TXT_CURSOR : TXT_SPACE);
    say(BX_PANEL, 38, 18, T_PAUSE_RESUME);
    say(BX_PANEL, 24, 32, sel == 1 ? TXT_CURSOR : TXT_SPACE);
    say(BX_PANEL, 38, 32, T_PAUSE_LEAVE);
    tb_flush(BX_PANEL);
    do { frame_wait(); } while (!(pad_new & (BTN_UP | BTN_DOWN | BTN_A | BTN_B | BTN_START)));
    if (pad_new & (BTN_UP | BTN_DOWN)) { sel ^= 1; se_play(SE_CURSOR); }
    if (pad_new & (BTN_B | BTN_START)) { sel = 0; break; }
    if (pad_new & BTN_A) break;
  }
  box(BX_PANEL, 0, 20, 32, 8, T_PANEL, PAL_W, BOX_WINDOW | BOX_LOW);
  panel_dirty = 1;
  return sel;
}

/* returns 1 = won, 0 = lost, 2 = left */
u8 battle_scene(u8 area, u8 men, const Loadout *lo) {
  Ev e;
  u8 i, result = 0;
  screen_off();
  sys_init();
  memset(nums, 0, sizeof(nums));
  memset(fxs, 0, sizeof(fxs));
  memset(act, 0, sizeof(act));
  memset(skill_icon_shown, 0xfe, 3);
  lose_t = win_t = 0;
  banner_t = hero_msg_t = 0;
  talk_open = 0;
  boss_pending = 0;
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
  screen_on();
  tut_open = tut_wait = tut_due = 0;
  for (;;) {
    frame_wait();
    if (tut_due && battle_can_command() && !b_npending && !b_npopups) { tut_show(tut_due - 1); tut_due = 0; }
    if (tut_open) {                  /* tutorial window: the battle waits */
      if (pad_new & BTN_A) { se_play(SE_OK); tut_ok(); }
      animate();
      draw_sprites();
      continue;
    }
    /* input */
    if (b_phase == PH_TALK) { if (pad_new & (BTN_A | BTN_B)) battle_press_next(); }
    else if (b_phase == PH_REWARD) {
      if (pad_new & (BTN_UP | BTN_DOWN)) {
        mirage_cursor = (u8)((mirage_cursor + ((pad_new & BTN_UP) ? b_mirage_offer_n - 1 : 1)) % b_mirage_offer_n);
        se_play(SE_CURSOR);
        mirage_draw();
      }
      if (pad_new & BTN_A) {
        se_play(SE_OK);
        battle_mirage_pick(mirage_cursor);
        box(BX_PANEL, 0, 20, 32, 8, T_PANEL, PAL_W, BOX_WINDOW | BOX_LOW);
        panel_dirty = 1; hp_dirty = 1;
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
      if ((pad_new & BTN_START) && battle_can_command()) {
        if (pause_menu()) { result = 2; break; }
      }
    }
    battle_step();
    while (ev_pop(&e)) on_event(&e);
    if (b_phase == PH_REWARD && e.type != 0) { }
    if (b_phase == PH_REWARD && !b_npending && mirage_cursor < 255) {
      static u8 shown;
      if (!shown) { mirage_cursor = 0; mirage_draw(); shown = 1; }
      if (b_phase != PH_REWARD) shown = 0;
    }
    animate();
    skill_icons_update();
    if (top_dirty) draw_top();
    if (hp_dirty) draw_hpst();
    if (panel_dirty && !talk_open && !tut_open && b_phase != PH_REWARD) draw_panel();
   draw_sprites();
    if (win_t) { if (++win_t > 150) { result = 1; break; } }
    if (lose_t) { if (++lose_t > 180) { result = 0; break; } }
  }
  fade_out();
  hdma_on = 0;
  return result;
}

/* monster picture assets for other screens (dex): 0 pose descriptor, 1 palette, 2 boss (BG) */
u16 mon_gfx_get(u8 m, u8 k) { return mon_gfx[m][k]; }
