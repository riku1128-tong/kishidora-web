/* world map: area markers, stage list per area, sortie, after-battle events (map.gd) */
#include "menukit.h"
#include "adventure.h"
#include "story.h"

#define BX_TOP 0
#define BX_LABEL 1
#define BX_HELP 2
#define BX_WIN 3
#define BX_SORTIE 4
#define T_TOP 1
#define T_LABEL 97
#define T_HELP 121
#define T_WIN 185
#define T_SORTIE 625
#define VIEW_TOP 24
#define VIEW_H 184

u8 map_area, map_men;       /* chosen stage (MA_BATTLE) */

static u8 sel;              /* selected node: area index, AR_MIRAGE = tower */
static u8 nodes[N_AREA], nnodes;
static s16 scroll_y;
static u8 label_tx, label_ty, label_w;

static u8 mirage_open(void) { return area_done(AR_CASTLE) || (sv.story & ST_DEBUG_OPEN); }

static void make_nodes(void) {
  u8 a;
  nnodes = 0;
  for (a = 0; a <= sv.area_open && a < AR_MIRAGE; ++a) nodes[nnodes++] = a;
  if (mirage_open()) nodes[nnodes++] = AR_MIRAGE;
}

static u8 node_index(u8 a) {
  u8 i;
  for (i = 0; i < nnodes; ++i) if (nodes[i] == a) return i;
  return 0;
}

static void map_sprites(void) {
  u8 i, a, t, k, n;
  s16 x0, y0, x1, y1, dx, dy, x, y;
  /* selection ring (16x16 = 4 tiles), blinking */
  x = areas[sel].x - 8; y = areas[sel].y - 8 - scroll_y;
  if ((frame >> 4) & 1) {
    spr_add(x, y, MK_ATTR(MK_T_RING, MK_PAL_MENU, 2), 0);
    spr_add(x + 8, y, MK_ATTR(MK_T_RING + 1, MK_PAL_MENU, 2), 0);
    spr_add(x, y + 8, MK_ATTR(MK_T_RING + 16, MK_PAL_MENU, 2), 0);
    spr_add(x + 8, y + 8, MK_ATTR(MK_T_RING + 17, MK_PAL_MENU, 2), 0);
  }
  for (i = 0; i < nnodes; ++i) {
    a = nodes[i];
    if (a == AR_MIRAGE) t = MK_T_MARK + 3;
    else if (area_done(a)) t = MK_T_MARK + 2;
    else if (a == sv.area_open) t = MK_T_MARK + 0;
    else t = MK_T_MARK + 1;
    y = areas[a].y - 4 - scroll_y;
    if (y < VIEW_TOP - 4 || y > VIEW_TOP + VIEW_H) continue;
    spr_add(areas[a].x - 4, y, MK_ATTR(t, MK_PAL_MENU, 2), 0);
  }
  /* dotted path along the open areas */
  for (i = 0; i + 1 <= sv.area_open && i + 1 < AR_MIRAGE && oam_next < 120; ++i) {
    x0 = areas[i].x; y0 = areas[i].y; x1 = areas[i + 1].x; y1 = areas[i + 1].y;
    dx = x1 - x0; dy = y1 - y0;
    n = (u8)((MAX(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy)) / 10);
    for (k = 1; k < n && oam_next < 120; ++k) {
      x = x0 + dx * k / n - 4;
      y = y0 + dy * k / n - 4 - scroll_y;
      if (y < VIEW_TOP - 4 || y > VIEW_TOP + VIEW_H) continue;
      spr_add(x, y, MK_ATTR(MK_T_DOT, MK_PAL_MENU, 2), 0);
    }
  }
}

static void draw_top(void) {
  tb_clear(BX_TOP);
  say(BX_TOP, 6, 0, TXT_NAME);
  sayn(BX_TOP, 168, 0, T_MAP_TICKET2, sv.tickets);
  if (sel != AR_MIRAGE) {
    targ_num2 = men_count(sel);
    sayn(BX_TOP, 6, 12, T_MAP_CLEAR2, cleared_count(sel));
  }
  sayn(BX_TOP, 168, 12, TXT_COINS, sv.coins);
  tb_flush(BX_TOP);
}

static void set_scroll(void) {
  s16 y = (s16)areas[sel].y - (VIEW_TOP + VIEW_H / 2);
  if (y < -VIEW_TOP) y = -VIEW_TOP;
  if (y > 256 - VIEW_TOP - VIEW_H) y = 256 - VIEW_TOP - VIEW_H;
  scroll_y = y;
  scroll[1] = (u16)y;          /* BG1 vertical */
}

static void draw_label(void) {
  u16 w = msg_width(AREAN_0 + sel) + 12;
  s16 tx, ty;
  if (label_w) win_close(BX_LABEL);
  label_w = (u8)((w + 7) >> 3);
  tx = ((s16)areas[sel].x - (s16)w / 2) >> 3;
  ty = ((s16)areas[sel].y - scroll_y + 10) >> 3;
  if (tx < 0) tx = 0;
  if (tx + label_w > 32) tx = 32 - label_w;
  if (ty > 23) ty = ((s16)areas[sel].y - scroll_y - 26) >> 3;
  label_tx = (u8)tx; label_ty = (u8)ty;
  box(BX_LABEL, label_tx, label_ty, label_w, 2, T_LABEL, PAL_W_FRAME, BOX_OPAQUE);
  say_center(BX_LABEL, 2, AREAN_0 + sel);
  tb_flush(BX_LABEL);
}

static void map_setup(void) {
  mk_screen();
  load_vram(ASSET_WORLDMAP_TILES, VRAM_BG1_TILES);
  load_pal(ASSET_WORLDMAP_PAL, 32);
  load_map(ASSET_WORLDMAP_MAP, 0, 0, 0);
  win_base();
  box(BX_TOP, 0, 0, 32, 3, T_TOP, PAL_W, BOX_OPAQUE);
  box(BX_HELP, 0, 26, 32, 2, T_HELP, PAL_W, BOX_OPAQUE);
  say(BX_HELP, 8, 2, T_MAP_KEYS);
  tb_flush(BX_HELP);
  label_w = 0;
  name_sync();
  make_nodes();
  set_scroll();
  draw_top();
  draw_label();
  mk_sprites = map_sprites;
  oam_next = 0; map_sprites(); oam_finish();
  screen_on();
  bgm_play(BGM_OPENING);
}

/* ---------------------------------------------------------------- stage list */
enum { RK_MEN, RK_TREASURE, RK_GACHA, RK_GOODS, RK_TUT, RK_TRAIN, RK_MONEY };
typedef struct { u8 kind, men, open; } Row;
static Row rows[16];
static u8 nrows;

static void make_rows(u8 a) {
  u8 m, n = men_count(a);
  nrows = 0;
  if (a == AR_ARCADIA) {
    rows[nrows].kind = RK_TUT; rows[nrows].men = MEN_TUTORIAL; rows[nrows].open = 1; ++nrows;
    rows[nrows].kind = RK_TRAIN; rows[nrows].men = MEN_TRAINING; rows[nrows].open = 1; ++nrows;
  }
  for (m = 1; m <= n; ++m) {
    rows[nrows].kind = RK_MEN; rows[nrows].men = m; rows[nrows].open = men_open(a, m);
    ++nrows;
  }
  if (areas[a].treasure) {
    rows[nrows].kind = RK_TREASURE; rows[nrows].men = D_TREASURE_MEN; rows[nrows].open = sv.tickets > 0;
    ++nrows;
  }
  if (a == AR_CASTLE && (area_done(AR_CASTLE) || (sv.story & ST_DEBUG_OPEN))) {    /* 金策: after the demon king */
    rows[nrows].kind = RK_MONEY; rows[nrows].men = T_MONEY_MEN; rows[nrows].open = 1; ++nrows;
  }
  if (area_has_shop(a)) {
    rows[nrows].kind = RK_GACHA; rows[nrows].men = 0; rows[nrows].open = 1; ++nrows;
    if (goods_sold(a, MAT_IT_RESET)) { rows[nrows].kind = RK_GOODS; rows[nrows].men = 0; rows[nrows].open = 1; ++nrows; }
  }
}

static u16 waves_of(u8 a) {
  if (a == AR_CASTLE) return D_CASTLE_STAGES;
  if (a == AR_SECRET) return D_SECRET_STAGES;
  return D_MEN_STAGES;
}

static void row_draw(u16 i, u8 y) {
  Row *r = &rows[i];
  u8 n = men_count(map_area);
  switch (r->kind) {
  case RK_MEN:
    targ_str = AREAN_0 + map_area;
    sayn(BX_WIN, 14, y, r->open ? T_MEN : T_MEN_GRAY, r->men);
    if (is_cleared(map_area, r->men)) say(BX_WIN, 124, y, T_LBL_CLEAR);
    else if (map_area == AR_CASTLE) say(BX_WIN, 124, y, r->men == n ? T_LBL_MAOU : T_LBL_BOSS);
    else if (map_area == AR_SECRET) say(BX_WIN, 124, y, T_LBL_SECRET);
    else if (r->men == D_AREA_MEN) say(BX_WIN, 124, y, T_LBL_BOSS);
    else if (r->men == D_AREA_MEN - 1) say(BX_WIN, 124, y, T_LBL_MID);
    sayn(BX_WIN, 166, y, T_LBL_WAVE, waves_of(map_area));
    break;
  case RK_TREASURE:
    say(BX_WIN, 14, y, r->open ? T_ROW_TREASURE : T_ROW_TREASURE_GRAY);
    sayn(BX_WIN, 124, y, sv.tickets ? T_LBL_TICKET : T_LBL_NOTICKET, sv.tickets);
    break;
  case RK_GACHA:
    say(BX_WIN, 14, y, T_ROW_GACHA);
    break;
  case RK_MONEY:
    say(BX_WIN, 14, y, T_ROW_MONEY);
    say(BX_WIN, 124, y, is_cleared(AR_CASTLE, T_MONEY_MEN) ? T_LBL_CLEAR : T_LBL_MONEY);
    sayn(BX_WIN, 166, y, T_LBL_WAVE, T_MONEY_STAGES);
    break;
  case RK_TUT:
    say(BX_WIN, 14, y, T_ROW_TUTORIAL);
    if (sv.tut_cleared) say(BX_WIN, 124, y, T_LBL_CLEAR);
    break;
  case RK_TRAIN:
    say(BX_WIN, 14, y, T_ROW_TRAINING);
    break;
  case RK_GOODS:
    say(BX_WIN, 14, y, T_ROW_GOODS);
    break;
  }
}

static void sortie_draw(u8 cur) {
  u8 s;
  tb_clear(BX_SORTIE);
  for (s = 0; s < 4; ++s) {
    say(BX_SORTIE, 8, 4 + s * 13, T_SLOT_MAIN + s);
    if (sv.equip[s] == NO_ITEM) say(BX_SORTIE, 52, 4 + s * 13, T_SLOT_EMPTY);
    else mk_item_name(BX_SORTIE, 52, 4 + s * 13, &sv.inv[sv.equip[s]]);
  }
  say(BX_SORTIE, 24, 58, T_SORTIE_GO);
  say(BX_SORTIE, 96, 58, T_SORTIE_EQUIP);
  tb_flush(BX_SORTIE);
  (void)cur;
}

/* returns 1 = go, 0 = back */
static u8 sortie(void) {
  u8 cur = 0;
  for (;;) {
    win_open(BX_SORTIE, 4, 8, 24, 9, T_SORTIE, PAL_W_FRAME);
    sortie_draw(cur);
    for (;;) {
      oam_next = 0;
      mk_cursor(32 + 14 + cur * 72, 64 + 58 + 2);
      oam_finish();
      frame_wait();
      if (pad_new & (BTN_LEFT | BTN_RIGHT)) { cur ^= 1; se_play(SE_CURSOR); }
      if (pad_new & BTN_B) { se_play(SE_CANCEL); win_close(BX_SORTIE); return 0; }
      if (pad_new & BTN_A) { se_play(SE_OK); break; }
    }
    win_close(BX_SORTIE);
    if (cur == 0) {
      if (sv.equip[EQ_MAIN] == NO_ITEM) { mk_notice(T_NEED_MAIN, 0); continue; }
      return 1;
    }
    equip_scene();
    map_setup();
  }
}

static void win_header(void) {
  targ_str = AREAN_0 + map_area;
  say(BX_WIN, 10, 4, TXT_YELLOW_S);
  targ_num2 = men_count(map_area);
  sayn(BX_WIN, 140, 4, T_MAP_CLEAR2, cleared_count(map_area));
  say(BX_WIN, 10, 145, T_WIN_KEYS);
}

/* stage list of the selected area; returns MA_* */
static u8 area_window(void) {
  List l;
  u16 k;
  u8 r;
  map_area = sel;
  make_rows(sel);
  list_init(&l, BX_WIN, 8, nrows);
  l.y = 22;
  for (r = 0; r < nrows; ++r) if (rows[r].open && rows[r].kind == RK_MEN) l.cur = r;   /* newest open stage */
  mk_sprites = 0;          /* map sprites would show through the low-priority window */
  for (;;) {
    mk_draw = row_draw;
    mk_moved = 0;
    mk_header = win_header;
    win_open(BX_WIN, 3, 3, 26, 20, T_WIN, PAL_W);
    k = list_run(&l, 0);
    mk_header = 0;
    if (k & BTN_B) { win_close(BX_WIN); return MA_NONE; }
    r = (u8)l.cur;
    if (!rows[r].open) { mk_notice(T_LOCKED_ROW, 0); continue; }
    if (rows[r].kind == RK_GACHA) { win_close(BX_WIN); shop_scene(sel, 0); return MA_REDRAW; }
    if (rows[r].kind == RK_GOODS) { win_close(BX_WIN); shop_scene(sel, 1); return MA_REDRAW; }
    if (sv.ninv >= D_INV_MAX) { mk_notice(T_INV_FULL, T_INV_FULL2); continue; }
    win_close(BX_WIN);
    if (rows[r].kind == RK_TUT) { map_men = MEN_TUTORIAL; return MA_BATTLE; }   /* fixed tutorial gear */
    k = sortie();
    if (k == 1) { map_men = rows[r].men; return MA_BATTLE; }
  }
}

/* ---------------------------------------------------------------- mirage tower */
static u8 mirage_window(void) {
  u8 sel = 0;
  mk_sprites = 0;
  win_open(BX_WIN, 2, 3, 28, 22, T_WIN, PAL_W_FRAME);
  say_center(BX_WIN, 4, T_MIRAGE_TITLE);
  if (sv.mirage_best) sayn(BX_WIN, 14, 22, sv.mirage_clear ? T_MIRAGE_BEST_CLEAR : T_MIRAGE_BEST, sv.mirage_best);
  else say(BX_WIN, 14, 22, T_MIRAGE_NONE);
  say(BX_WIN, 14, 42, T_MIRAGE_RULES);
  say(BX_WIN, 40, 150, T_MIRAGE_GO);
  say(BX_WIN, 136, 150, T_SORTIE_BACK);
  tb_flush(BX_WIN);
  for (;;) {
    oam_next = 0;
    mk_cursor(16 + 40 - 11 + sel * 96, 24 + 150 + 2);
    oam_finish();
    frame_wait();
    if (pad_new & (BTN_LEFT | BTN_RIGHT)) { sel ^= 1; se_play(SE_CURSOR); }
    if (pad_new & BTN_B) { sel = 1; break; }
    if (pad_new & BTN_A) break;
  }
  se_play(sel ? SE_CANCEL : SE_OK);
  win_close(BX_WIN);
  if (sel) return MA_NONE;
  if (sv.ninv >= D_INV_MAX) { mk_notice(T_INV_FULL, T_INV_FULL2); return MA_NONE; }
  map_area = AR_MIRAGE; map_men = 1;
  return MA_BATTLE;
}

/* ---------------------------------------------------------------- after battle */
static u8 after_battle(void) {
  u8 cleared = map_area;
  if (!res.win || res.mirage) return MA_NONE;
  if (res.orb != 255) orb_scene(res.orb);
  if (res.king) { ending(); return MA_TITLE; }
  if (res.opened != 255) {
    show_area_story(cleared);
    sel = res.opened;
    sv.cursor = sel;
    make_nodes();
    set_scroll();
    draw_top();
    draw_label();
    opened_notice(res.opened);
  }
  return MA_NONE;
}

/* ---------------------------------------------------------------- debug: L x3 / R x3 */
static u8 lr_key, lr_n;
static u16 lr_t;

static void debug_open_map(void) {
  if ((sv.story & ST_DEBUG_OPEN) && sv.area_open == AR_SECRET) { mk_notice(T_DBG_OPEN_ALREADY, 0); return; }
  if (!mk_confirm(T_DBG_OPEN_ASK, T_DBG_NOUNDO)) return;
  debug_open_all();
  map_setup();
  mk_notice(T_DBG_OPEN_DONE, 0);
}

/* three presses of the same shoulder button, each within 45 frames of the last */
static void debug_keys(void) {
  u8 k;
  if (!(pad_new & (BTN_L | BTN_R))) return;
  k = (pad_new & BTN_L) ? 1 : 2;
  if (k != lr_key || (u16)(frame - lr_t) > 45) lr_n = 0;
  lr_key = k; lr_t = frame;
  if (++lr_n < 3) return;
  lr_n = 0;
  se_play(SE_OK);
  if (k == 1) debug_open_map();
  else { debug_scene(); map_setup(); }
}

u8 map_scene(u8 mode) {
  u8 a, i;
  sel = sv.cursor;
  if (sel > sv.area_open && sel != AR_MIRAGE) sel = sv.area_open;
  if (sel == AR_MIRAGE && !mirage_open()) sel = sv.area_open;
  map_setup();
  if (mode == MS_NEWGAME) {
    king_opening();
    name_entry();
    map_setup();
  }
  if (!(sv.story & ST_OPENING)) {
    sv.story |= ST_OPENING;
    king_opening2();
    save_write();
  }
  if (mode == MS_AFTER) {
    a = after_battle();
    if (a == MA_TITLE) return MA_TITLE;
  }
  for (;;) {
    oam_next = 0;
    map_sprites();
    oam_finish();
    frame_wait();
    if (pad_rep & (BTN_LEFT | BTN_UP | BTN_RIGHT | BTN_DOWN)) {
      i = node_index(sel);
      if (pad_rep & (BTN_LEFT | BTN_UP)) i = i ? i - 1 : nnodes - 1;
      else i = i + 1 < nnodes ? i + 1 : 0;
      sel = nodes[i];
      sv.cursor = sel;
      se_play(SE_CURSOR);
      set_scroll();
      draw_top();
      draw_label();
    }
    if (pad_new & BTN_A) {
      se_play(SE_OK);
      a = sel == AR_MIRAGE ? mirage_window() : area_window();
      mk_sprites = map_sprites;
      if (a == MA_BATTLE) { save_write(); fade_out(); return MA_BATTLE; }
      if (a == MA_REDRAW) map_setup();
      draw_top();
    }
    if (pad_new & (BTN_Y | BTN_X | BTN_START)) {
      se_play(SE_OK);
      if (menu_scene() == MA_TITLE) return MA_TITLE;
      map_setup();
    }
    debug_keys();
  }
}
