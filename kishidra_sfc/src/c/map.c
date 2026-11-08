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
static u8 label_off;            /* a window covers the map: the label stays hidden */

static u8 mirage_open(void) { return area_done(AR_CASTLE) || (sv.story & ST_DEBUG_OPEN); }

/* the markers and the dotted path change only with the save: made here, drawn every frame
   (computing the dots took several frames of multiplies and divides per frame) */
#define MAX_DOTS 120
static u8 node_t[N_AREA];
static s16 node_x[N_AREA], node_y[N_AREA];
static s16 dot_x[MAX_DOTS], dot_y[MAX_DOTS];
static u8 ndots;

static void make_dots(void) {
  u8 i, k, n;
  s16 x0, y0, dx, dy;
  ndots = 0;
  for (i = 0; i + 1 <= sv.area_open && i + 1 < AR_MIRAGE; ++i) {
    x0 = areas[i].x; y0 = areas[i].y;
    dx = (s16)areas[i + 1].x - x0; dy = (s16)areas[i + 1].y - y0;
    n = (u8)((MAX(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy)) / 10);
    for (k = 1; k < n && ndots < MAX_DOTS; ++k) {
      dot_x[ndots] = x0 + dx * k / n - 4;
      dot_y[ndots] = y0 + dy * k / n - 4;
      ++ndots;
    }
  }
}

static void make_nodes(void) {
  u8 a, i;
  nnodes = 0;
  for (a = 0; a <= sv.area_open && a < AR_MIRAGE; ++a) nodes[nnodes++] = a;
  if (mirage_open()) nodes[nnodes++] = AR_MIRAGE;
  for (i = 0; i < nnodes; ++i) {
    a = nodes[i];
    if (a == AR_MIRAGE) node_t[i] = MK_T_MARK + 3;
    else if (area_done(a)) node_t[i] = MK_T_MARK + 2;
    else if (a == sv.area_open) node_t[i] = MK_T_MARK + 0;
    else node_t[i] = MK_T_MARK + 1;
    node_x[i] = (s16)areas[a].x - 4;
    node_y[i] = (s16)areas[a].y - 4;
  }
  make_dots();
}

static u8 node_index(u8 a) {
  u8 i;
  for (i = 0; i < nnodes; ++i) if (nodes[i] == a) return i;
  return 0;
}

static void map_sprites(void) {
  u8 i;
  s16 x, y, *px, *py;
  /* selection ring (16x16 = 4 tiles), blinking */
  x = areas[sel].x - 8; y = areas[sel].y - 8 - scroll_y;
  if ((frame >> 4) & 1) {
    spr_add(x, y, MK_ATTR(MK_T_RING, MK_PAL_MENU, 2), 0);
    spr_add(x + 8, y, MK_ATTR(MK_T_RING + 1, MK_PAL_MENU, 2), 0);
    spr_add(x, y + 8, MK_ATTR(MK_T_RING + 16, MK_PAL_MENU, 2), 0);
    spr_add(x + 8, y + 8, MK_ATTR(MK_T_RING + 17, MK_PAL_MENU, 2), 0);
  }
  for (i = 0; i < nnodes; ++i) {
    y = node_y[i] - scroll_y;
    if (y < VIEW_TOP - 4 || y > VIEW_TOP + VIEW_H) continue;
    spr_add(node_x[i], y, MK_ATTR(node_t[i], MK_PAL_MENU, 2), 0);
  }
  /* dotted path along the open areas */
  px = dot_x; py = dot_y;
  for (i = 0; i < ndots && oam_next < 120; ++i, ++px, ++py) {
    y = *py - scroll_y;
    if (y < VIEW_TOP - 4 || y > VIEW_TOP + VIEW_H) continue;
    spr_add(*px, y, MK_ATTR(MK_T_DOT, MK_PAL_MENU, 2), 0);
  }
}

static void draw_top(void) {
  tb_clear(BX_TOP);
  /* two lines 12 px apart share a tile row: the coin mark (yellow) has no text above it */
  say(BX_TOP, 6, 0, TXT_NAME);
  sayn(BX_TOP, 184, 0, T_MAP_TICKET2, sv.tickets);
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

/* the area name is a high-priority BG3 box: above sprites, so it would cover the king */
static void label_hide(void) {
  if (label_w) { box_hide(BX_LABEL); label_w = 0; }
}

static void draw_label(void) {
  u16 w = msg_width(AREAN_0 + sel) + 12;
  s16 tx, ty;
  if (label_off) return;
  if (label_w) win_close(BX_LABEL);
  label_w = (u8)((w + 7) >> 3);
  tx = ((s16)areas[sel].x - (s16)w / 2) >> 3;
  ty = ((s16)areas[sel].y - scroll_y + 10) >> 3;
  if (tx < 0) tx = 0;
  if (tx + label_w > 32) tx = 32 - label_w;
  if (ty > 23) ty = ((s16)areas[sel].y - scroll_y - 26) >> 3;
  label_tx = (u8)tx; label_ty = (u8)ty;
  box(BX_LABEL, label_tx, label_ty, label_w, 2, T_LABEL, PAL_GOLD, BOX_OPAQUE);
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
enum { RK_MEN, RK_TREASURE, RK_MONEY, RK_GACHA, RK_GOODS, RK_TUT, RK_TRAIN };    /* the STAGE rows first */
typedef struct { u8 kind, men, open; } Row;
static Row rows[16];
static u8 nrows;
/* area_window's arguments: the STAGE it was opened on (0 = none) and its AW_* flags */
#define AW_GO 1               /* straight to the sortie of the focus STAGE when its row is open (再挑戦) */
#define AW_FARM 2             /* the forge sent the player to the focus STAGE (its row tag) */
static u8 aw_focus, aw_flags;

/* a row of one STAGE (its own men number: a jump's focus, X's details): STAGE n, 宝の部屋, 金策 */
static u8 row_stage(const Row *r) { return r->kind <= RK_MONEY; }

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

/* stage list window: 30 tiles wide; rows 16 px apart (open / locked rows and the labels have
 * different colours); the scroll marks at the right edge are clear of the WAVE column */
#define ROW_LBL 128
#define ROW_WAVE 168

/* the row-tag callback: a mark (text id) that takes the place of row r's label (RT_LBL, x ROW_LBL:
 * クリア / ボス / チケット n ...) or of its WAVE column (RT_WAVE, x ROW_WAVE); 0 = the usual text.
 * The marks of later steps go here: NEW!! on the ガチャ row of a shop not seen yet, おすすめ on the
 * STAGE the forge sends the player to (aw_flags & AW_FARM, row_stage(r) && r->men == aw_focus) */
#define RT_LBL 0
#define RT_WAVE 1
static u16 row_tag(const Row *r, u8 col) {
  (void)r; (void)col;
  return 0;
}

static void row_draw(u16 i, u8 y) {
  Row *r = &rows[i];
  u16 lbl = 0, t;               /* the label's text and number, the WAVE count (0 = none) */
  u16 nlbl = 0, waves = 0;
  switch (r->kind) {
  case RK_MEN:
    targ_str = AREAN_0 + map_area;
    sayn(BX_WIN, 14, y, r->open ? T_MEN : T_MEN_GRAY, r->men);
    if (is_cleared(map_area, r->men)) lbl = T_LBL_CLEAR;
    else if (map_area == AR_CASTLE) lbl = r->men == men_count(map_area) ? T_LBL_MAOU : T_LBL_BOSS;
    else if (map_area == AR_SECRET) lbl = T_LBL_SECRET;
    else if (r->men == D_AREA_MEN) lbl = T_LBL_BOSS;
    else if (r->men == D_AREA_MEN - 1) lbl = T_LBL_MID;
    waves = waves_of(map_area);
    break;
  case RK_TREASURE:
    say(BX_WIN, 14, y, r->open ? T_ROW_TREASURE : T_ROW_TREASURE_GRAY);
    lbl = sv.tickets ? T_LBL_TICKET : T_LBL_NOTICKET;
    nlbl = sv.tickets;
    break;
  case RK_GACHA:
    say(BX_WIN, 14, y, T_ROW_GACHA);
    break;
  case RK_MONEY:
    say(BX_WIN, 14, y, T_ROW_MONEY);
    lbl = is_cleared(AR_CASTLE, T_MONEY_MEN) ? T_LBL_CLEAR : T_LBL_MONEY;
    waves = T_MONEY_STAGES;
    break;
  case RK_TUT:
    say(BX_WIN, 14, y, T_ROW_TUTORIAL);
    if (sv.tut_cleared) lbl = T_LBL_CLEAR;
    break;
  case RK_TRAIN:
    say(BX_WIN, 14, y, T_ROW_TRAINING);
    break;
  case RK_GOODS:
    say(BX_WIN, 14, y, T_ROW_GOODS);
    break;
  }
  t = row_tag(r, RT_LBL);
  if (t) say(BX_WIN, ROW_LBL, y, t);
  else if (lbl) sayn(BX_WIN, ROW_LBL, y, lbl, nlbl);
  t = row_tag(r, RT_WAVE);
  if (t) say(BX_WIN, ROW_WAVE, y, t);
  else if (waves) sayn(BX_WIN, ROW_WAVE, y, T_LBL_WAVE, waves);
}

/* ---------------------------------------------------------------- sortie window */
#define SORTIE_X 3                  /* the window (tiles) */
#define SORTIE_Y 7
#define SORTIE_W 26
#define SORTIE_H 12
#define SORTIE_BTN_Y (TOP_Y + 4 * LINE_C)
/* its buttons, left to right: what it does, the x of its text in the box (the cursor sits 11 px left
 * of it), the text.  A new button is a row here and a case in sortie() */
enum { SB_GO, SB_EQUIP };
typedef struct { u8 id, x; u16 text; } SortieBtn;
static const SortieBtn sortie_btn[] = {
  { SB_GO, 32, T_SORTIE_GO },
  { SB_EQUIP, 112, T_SORTIE_EQUIP },
};
#define N_SORTIE_BTN ((u8)(sizeof(sortie_btn) / sizeof(sortie_btn[0])))

static void sortie_draw(void) {
  u8 s;
  tb_clear(BX_SORTIE);
  for (s = 0; s < 4; ++s) {
    say(BX_SORTIE, 12, TOP_Y + s * LINE_C, T_SLOT_MAIN + s);
    if (sv.equip[s] == NO_ITEM) say(BX_SORTIE, 56, TOP_Y + s * LINE_C, T_SLOT_EMPTY);
    else mk_item_name(BX_SORTIE, 56, TOP_Y + s * LINE_C, &sv.inv[sv.equip[s]], 0);
  }
  for (s = 0; s < N_SORTIE_BTN; ++s) say(BX_SORTIE, sortie_btn[s].x, SORTIE_BTN_Y, sortie_btn[s].text);
  tb_flush(BX_SORTIE);
}

/* returns 1 = go, 0 = back */
static u8 sortie(void) {
  u8 cur = 0;
  for (;;) {
    win_open(BX_SORTIE, SORTIE_X, SORTIE_Y, SORTIE_W, SORTIE_H, T_SORTIE, PAL_W_FRAME);
    sortie_draw();
    for (;;) {
      oam_next = 0;
      mk_cursor(SORTIE_X * 8 - 11 + sortie_btn[cur].x, SORTIE_Y * 8 + SORTIE_BTN_Y + 2);
      oam_finish();
      frame_wait();
      if (pad_new & BTN_LEFT) { cur = cur ? cur - 1 : N_SORTIE_BTN - 1; se_play(SE_CURSOR); }
      else if (pad_new & BTN_RIGHT) { cur = cur + 1 < N_SORTIE_BTN ? cur + 1 : 0; se_play(SE_CURSOR); }
      if (pad_new & BTN_B) { se_play(SE_CANCEL); win_close(BX_SORTIE); return 0; }
      if (pad_new & BTN_A) { se_play(SE_OK); break; }
    }
    win_close(BX_SORTIE);
    switch (sortie_btn[cur].id) {
    case SB_GO:
      if (sv.equip[EQ_MAIN] != NO_ITEM) return 1;
      mk_notice(T_NEED_MAIN, 0);
      break;
    case SB_EQUIP:
      equip_scene();
      map_setup();
      break;
    }
  }
}

/* no battle while the bag is full: 1 (said so) */
static u8 bag_full(void) {
  if (sv.ninv < D_INV_MAX) return 0;
  mk_notice(T_INV_FULL, T_INV_FULL2);
  return 1;
}

static void win_header(void) {
  targ_str = AREAN_0 + map_area;
  say(BX_WIN, 10, TOP_Y, TXT_YELLOW_S);
  targ_num2 = men_count(map_area);
  sayn(BX_WIN, 150, TOP_Y, T_MAP_CLEAR2, cleared_count(map_area));
  say(BX_WIN, 10, 24 + 7 * LINE_C, T_WIN_KEYS);
}

/* keys of the stage list besides A / B (list_run), and what key k does on row r while the list is
 * open: MA_NONE = back to the list (it opens again, so the key may draw the map anew with map_setup),
 * else area_window's result.  None yet */
static u16 aw_keys(void) { return 0; }
static u8 aw_key(u16 k, u8 r) {
  (void)k; (void)r;
  return MA_NONE;
}

/* stage list of the selected area; returns MA_*.  focus: the cursor starts on that STAGE's row
 * (row_stage) instead of the newest open STAGE; flags: AW_GO (if that row is open: its sortie at once,
 * the list after it; with a full bag the list and the notice over it, as A on the row), AW_FARM */
static u8 area_window(u8 focus, u8 flags) {
  List l;
  Row *w;
  u16 k;
  u8 r, go = 0, full = 0;
  map_area = sel;
  aw_focus = focus;
  aw_flags = flags;
  make_rows(sel);
  list_init(&l, BX_WIN, 7, nrows);
  l.y = 24;
  l.lh = LINE_C;
  for (r = 0; r < nrows; ++r) if (rows[r].open && rows[r].kind == RK_MEN) l.cur = r;   /* newest open stage */
  if (focus) for (r = 0, w = rows; r < nrows; ++r, ++w) if (row_stage(w) && w->men == focus) {
    l.cur = r;
    go = (flags & AW_GO) && w->open;
  }
  if (go && sv.ninv >= D_INV_MAX) { go = 0; full = 1; }      /* (map.gd _retry_after) */
  for (;;) {
    mk_sprites = 0;            /* map sprites would show through the low-priority windows (map_setup sets them) */
    r = (u8)l.cur;
    if (go) go = 0;            /* AW_GO: the sortie first */
    else {
      mk_draw = row_draw;
      mk_moved = 0;
      mk_header = win_header;
      win_open(BX_WIN, 1, 3, 30, 20, T_WIN, PAL_W);
      if (full) {              /* the list, then as A on the row (so a bag_full that redraws the map works here too) */
        full = 0;
        list_fix(&l);
        list_draw(&l);
        if (bag_full()) continue;
      }
      k = list_run(&l, aw_keys());
      mk_header = 0;
      if (k & BTN_B) { win_close(BX_WIN); return MA_NONE; }
      r = (u8)l.cur;
      if (k & ~(BTN_A | BTN_B)) {
        k = aw_key(k, r);
        if (k != MA_NONE) return (u8)k;
        continue;
      }
      w = &rows[r];
      if (!w->open) { mk_notice(T_LOCKED_ROW, 0); continue; }
      if (w->kind == RK_GACHA) { win_close(BX_WIN); shop_scene(sel, 0); return MA_REDRAW; }
      if (w->kind == RK_GOODS) { win_close(BX_WIN); shop_scene(sel, 1); return MA_REDRAW; }
      if (bag_full()) continue;
      win_close(BX_WIN);
    }
    w = &rows[r];
    if (w->kind == RK_TUT) { map_men = MEN_TUTORIAL; return MA_BATTLE; }   /* fixed tutorial gear */
    if (sortie()) { map_men = w->men; return MA_BATTLE; }
  }
}

/* ---------------------------------------------------------------- mirage tower */
static u8 mirage_window(void) {
  u8 sel = 0;
  mk_sprites = 0;
  win_open(BX_WIN, 1, 3, 30, 22, T_WIN, PAL_W_FRAME);
  say_center(BX_WIN, TOP_Y, T_MIRAGE_TITLE);
  if (sv.mirage_best) sayn(BX_WIN, 12, TOP_Y + LINE_C, sv.mirage_clear ? T_MIRAGE_BEST_CLEAR : T_MIRAGE_BEST, sv.mirage_best);
  else say(BX_WIN, 12, TOP_Y + LINE_C, T_MIRAGE_NONE);
  say(BX_WIN, 12, 42, T_MIRAGE_RULES);
  say(BX_WIN, 48, 150, T_MIRAGE_GO);
  say(BX_WIN, 144, 150, T_SORTIE_BACK);
  tb_flush(BX_WIN);
  for (;;) {
    oam_next = 0;
    mk_cursor(8 + 48 - 11 + sel * 96, 24 + 150 + 2);
    oam_finish();
    frame_wait();
    if (pad_new & (BTN_LEFT | BTN_RIGHT)) { sel ^= 1; se_play(SE_CURSOR); }
    if (pad_new & BTN_B) { sel = 1; break; }
    if (pad_new & BTN_A) break;
  }
  se_play(sel ? SE_CANCEL : SE_OK);
  win_close(BX_WIN);
  if (sel || bag_full()) return MA_NONE;
  map_area = AR_MIRAGE; map_men = 1;
  return MA_BATTLE;
}

/* the stage list (or the tower window) of the selected area, from the map; MA_BATTLE when a battle
 * starts, else MA_NONE with the map drawn again.  focus, flags: area_window's */
static u8 open_area(u8 focus, u8 flags) {
  u8 a;
  label_hide();                /* under the windows a piece of the label would stick out */
  label_off = 1;
  a = sel == AR_MIRAGE ? mirage_window() : area_window(focus, flags);
  label_off = 0;
  mk_sprites = map_sprites;
  if (a == MA_BATTLE) return MA_BATTLE;
  if (a == MA_REDRAW) map_setup();
  else draw_label();
  draw_top();
  return MA_NONE;
}

/* a jump to area a (the map scrolls to it) and its stage list opened on STAGE men: the forge's farm
 * jump (MA_FARM, AW_FARM), 再挑戦 (AW_GO), the test command DC_AREA; an area not open: MA_NONE */
static u8 goto_area(u8 a, u8 men, u8 flags) {
  if (a == AR_MIRAGE ? !mirage_open() : a > sv.area_open) return MA_NONE;
  sel = a;
  sv.cursor = a;
  set_scroll();
  draw_top();
  return open_area(men, flags);
}

/* ---------------------------------------------------------------- after battle */
static u8 after_battle(void) {
  u8 cleared = map_area;
  if (!res.win || res.mirage) return MA_NONE;
  if (res.orb != 255) { label_hide(); orb_scene(res.orb); draw_label(); }   /* the label under the orb window */
  if (res.king) { label_hide(); ending(); return MA_TITLE; }
  if (res.opened != 255) {
    label_hide();
    show_area_story(cleared);
    sel = res.opened;
    sv.cursor = sel;
    make_nodes();
    set_scroll();
    draw_top();
    opened_notice(res.opened);     /* before the label: the notice covers its row */
    draw_label();
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
    label_hide();
    king_opening();
    name_entry();
    map_setup();
  }
  if (!(sv.story & ST_OPENING)) {
    sv.story |= ST_OPENING;
    label_hide();
    king_opening2();
    draw_label();
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
    DBG_SCREEN(DS_MAP);
    frame_wait();
    DBG_SCREEN(DS_NONE);
#ifdef TEST_HOOKS
    if (dbg_map_go) {            /* DC_AREA */
      dbg_map_go = 0;
      if (goto_area((u8)dbg_arg[0], (u8)dbg_arg[1], (u8)dbg_arg[2]) == MA_BATTLE) break;
      continue;                  /* the keys of the window's last frame are not the map's (A on もどる) */
    }
#endif
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
      if (open_area(0, 0) == MA_BATTLE) break;
    }
    if (pad_new & (BTN_Y | BTN_X | BTN_START)) {
      se_play(SE_OK);
      a = menu_scene();
      if (a == MA_TITLE) return MA_TITLE;
      map_setup();
      if (a == MA_FARM && goto_area(map_area, map_men, AW_FARM) == MA_BATTLE) break;
    }
    debug_keys();
  }
  save_write();
  fade_out();
  return MA_BATTLE;
}
