/* forge: enhance / evolve / slots / material synthesis (forge_screens.gd) */
#include "menukit.h"
#include "adventure.h"

#define BX_HEAD 0
#define BX_CARD 1
#define BX_MENU 2
#define BX_SUB 3
#define T_HEAD 1
#define T_CARD 65
#define T_MENU 417         /* 65 + 32 x 11 (card) */
#define T_SUB 417          /* menu and sub windows are never open together; up to 22 x 27 */

static u16 head_item = NO_ITEM;

static void head(void) {
  box(BX_HEAD, 0, 0, 32, 2, T_HEAD, PAL_W, BOX_OPAQUE);
  say(BX_HEAD, 8, 2, T_FORGE_TITLE);
  if (head_item != NO_ITEM) mk_item_name(BX_HEAD, 56, 2, &sv.inv[head_item]);
  sayn(BX_HEAD, 200, 2, TXT_COINS, sv.coins);
  tb_flush(BX_HEAD);
}

static void card(u16 i) {
  win_open(BX_CARD, 0, 2, 32, MK_CARD_H, T_CARD, PAL_W);
  mk_item_detail(BX_CARD, &sv.inv[i]);
  tb_flush(BX_CARD);
}

/* small vertical menu; returns the row or 255 */
static u8 menu_rows(u8 bx, u8 n, u8 sx, u8 sy) {
  u8 sel = 0;
  for (;;) {
    oam_next = 0;
    mk_cursor(sx + 3, sy + TOP_Y + sel * LINE_H + 2);
    oam_finish();
    frame_wait();
    if (pad_rep & BTN_UP) { sel = sel ? sel - 1 : n - 1; se_play(SE_CURSOR); }
    if (pad_rep & BTN_DOWN) { sel = sel + 1 < n ? sel + 1 : 0; se_play(SE_CURSOR); }
    if (pad_new & BTN_B) { se_play(SE_CANCEL); return 255; }
    if (pad_new & BTN_A) { se_play(SE_OK); return sel; }
  }
  (void)bx;
}

static void result_banner(u8 r) {
  se_play(r == 3 ? SE_MIRACLE : (r ? SE_JUST : SE_CANCEL));
  mk_notice(T_FG_RESULT0 + r, 0);
}

/* ---------------------------------------------------------------- enhance */
static const u8 enh_mats[4] = { MAT_KYOUKA, MAT_SKYOUKA, MAT_HKYOUKA, MAT_BELL };

static u16 do_enhance(u16 i) {
  u8 k, sel, r, slot;
  u16 src;
  if (enh_free_slot(&sv.inv[i]) == 255) { mk_notice(inv_slots_of(&sv.inv[i]) ? T_FORGE_NO_SLOT : T_FG_NO_SLOT0, 0); return i; }
  win_open(BX_SUB, 6, 12, 20, 13, T_SUB, PAL_W_FRAME);
  say(BX_SUB, 10, TOP_Y, T_FG_SRC_HEAD);
  for (k = 0; k < 4; ++k) {
    targ_str = MATN_0 + enh_mats[k];
    sayn(BX_SUB, 14, TOP_Y + LINE_C + k * LINE_H, T_FG_SRC_LINE, sv.mats[enh_mats[k]]);
  }
  say(BX_SUB, 14, TOP_Y + LINE_C + 4 * LINE_H, T_FG_SRC_ITEM);
  tb_flush(BX_SUB);
  sel = menu_rows(BX_SUB, 5, 48, 96 + LINE_C);
  win_close(BX_SUB);
  if (sel == 255) return i;
  if (sel < 4) {
    if (!sv.mats[enh_mats[sel]]) { mk_notice(T_FORGE_SHORT, 0); return i; }
    r = enhance(i, enh_mats[sel], 0, &slot);
  } else {
    pick_exclude = i;
    src = pick_item(3, T_FG_PICK_SRC, NO_ITEM);
    pick_exclude = NO_ITEM;
    head();
    if (src == NO_ITEM) return i;
    if (!mk_confirm(T_CONFIRM, 0)) return i;
    r = enhance(i, 255, src, &slot);
    if (src < i) --i;
  }
  if (r == 255) { mk_notice(T_FORGE_SHORT, 0); return i; }
  result_banner(r);
  return i;
}

/* ---------------------------------------------------------------- evolve */
/* the needed materials (red when short, so 16 px apart) and the yes / no question in one
 * window over the card */
static u8 evolve_ask(u8 y0, u8 yq) {
  u8 sel = 0;
  say_center(BX_SUB, yq, T_FG_EVO);
  say(BX_SUB, 40, yq + LINE_C, T_YESNO_Y);
  say(BX_SUB, 104, yq + LINE_C, T_YESNO_N);
  tb_flush(BX_SUB);
  for (;;) {
    oam_next = 0;
    mk_cursor(40 + 40 - 12 + sel * 64, y0 * 8 + yq + LINE_C + 2);
    oam_finish();
    frame_wait();
    if (pad_new & (BTN_LEFT | BTN_RIGHT)) { sel ^= 1; se_play(SE_CURSOR); }
    if (pad_new & BTN_A) { se_play(SE_OK); break; }
    if (pad_new & BTN_B) { se_play(SE_CANCEL); sel = 1; break; }
  }
  return sel == 0;
}

static void do_evolve(u16 i) {
  u8 id = sv.inv[i].id, to = evolve_target(id), k, e, lh, yq, h, y0;
  if (to == 255) { mk_notice(T_FG_NO_EVO, 0); return; }
  evolve_need(id);
  lh = LINE_C;                       /* need.n <= 7: at most 24 rows (22 x 24 tiles from 417) */
  yq = (u8)((TOP_Y + LINE_C + (need.n + 1) * lh + 7) & ~7);
  h = (u8)((yq + LINE_C + 12 + 8 + 7) >> 3);
  y0 = h > 26 ? 28 - h : 2;
  win_open(BX_SUB, 5, y0, 22, h, T_SUB, PAL_W_FRAME);
  targ_str = ITEMN_0 + to;
  say(BX_SUB, 10, TOP_Y, T_FG_TO);
  for (k = 0; k < need.n; ++k) {
    targ_str = MATN_0 + need.mat[k];
    targ_num2 = need.cnt[k];
    sayn(BX_SUB, 14, TOP_Y + LINE_C + k * lh, sv.mats[need.mat[k]] < need.cnt[k] ? T_FG_NEED_RED : T_FG_NEED, sv.mats[need.mat[k]]);
  }
  sayn(BX_SUB, 14, TOP_Y + LINE_C + need.n * lh, sv.coins < need.coins ? T_FG_COINS_RED : T_FG_COINS, need.coins);
  e = evolve_ask(y0, yq);
  win_close(BX_SUB);
  if (!e) return;
  e = evolve_item(i);
  if (e == ERR_MATS) mk_notice(T_FORGE_SHORT, 0);
  else if (e == ERR_COINS) mk_notice(T_FORGE_NO_COIN, 0);
  else if (e == ERR_OK) {
    se_play(SE_MIRACLE);
    targ_str = ITEMN_0 + to;
    mk_notice(T_FG_EVOLVED, 0);
  }
}

/* ---------------------------------------------------------------- slots */
static void do_reset(u16 i) {
  Inv *it = &sv.inv[i];
  u8 n = inv_slots_of(it), sel = 0, k;
  if (!enh_count(it)) { mk_notice(T_FG_NOTHING, 0); return; }
  if (!sv.mats[MAT_IT_RESET]) { mk_notice(T_FG_NO_RESET, 0); return; }
  win_open(BX_SUB, 4, 12, 24, 6, T_SUB, PAL_W_FRAME);
  say(BX_SUB, 10, TOP_Y, T_FG_WHICH);
  for (k = 0; k < n; ++k) say(BX_SUB, 24 + k * 16, TOP_Y + LINE_C, T_STAR_0 + enh_get(it, k));
  tb_flush(BX_SUB);
  for (;;) {
    oam_next = 0;
    spr_add(32 + 24 + sel * 16 + 2, 96 + TOP_Y + LINE_C - 8, MK_ATTR(MK_T_DOWN, MK_PAL_MENU, 3), 0);
    oam_finish();
    frame_wait();
    if (pad_rep & BTN_LEFT) { sel = sel ? sel - 1 : n - 1; se_play(SE_CURSOR); }
    if (pad_rep & BTN_RIGHT) { sel = sel + 1 < n ? sel + 1 : 0; se_play(SE_CURSOR); }
    if (pad_new & BTN_B) { se_play(SE_CANCEL); win_close(BX_SUB); return; }
    if (pad_new & BTN_A) break;
  }
  win_close(BX_SUB);
  if (slot_reset(i, sel) == ERR_OK) se_play(SE_OK);
  else mk_notice(T_FG_NOTHING, 0);
}

static void item_forge(u16 i) {
  u8 sel;
  for (;;) {
    head_item = i;
    head();
    card(i);
    win_open(BX_MENU, 4, 2 + MK_CARD_H, 24, 11, T_MENU, PAL_W);
    say(BX_MENU, 14, TOP_Y, T_FG_ENH);
    say(BX_MENU, 14, TOP_Y + LINE_H, T_FG_EVO);
    sayn(BX_MENU, 14, TOP_Y + 2 * LINE_H, T_FG_ADD, sv.mats[MAT_IT_FUERU]);
    sayn(BX_MENU, 14, TOP_Y + 3 * LINE_H, T_FG_RESET, sv.mats[MAT_IT_RESET]);
    say(BX_MENU, 14, TOP_Y + 4 * LINE_H, T_ACT_BACK);
    tb_flush(BX_MENU);
    sel = menu_rows(BX_MENU, 5, 32, (2 + MK_CARD_H) * 8);
    win_close(BX_MENU);
    if (sel == 255 || sel == 4) { head_item = NO_ITEM; return; }
    switch (sel) {
    case 0: i = do_enhance(i); break;
    case 1: do_evolve(i); break;
    case 2:
      sel = slot_add(i);
      if (sel == ERR_NO_GOODS) mk_notice(T_FG_NO_FUERU, 0);
      else if (sel == ERR_MAX) mk_notice(T_FG_SLOT_MAX, 0);
      else se_play(SE_OK);
      break;
    case 3: do_reset(i); break;
    }
  }
}

/* ---------------------------------------------------------------- material synthesis */
static u8 up_list[N_MAT], nup;

/* two lines per material: what it is and what 3 of them become */
static void up_row(u16 n, u8 y) {
  u8 m = up_list[n];
  targ_str = MATN_0 + m;
  sayn(BX_CARD, 14, y, T_FG_UP_LINE, sv.mats[m]);
  targ_str = MATN_0 + mat_up[m];
  say(BX_CARD, 14, y + LINE_C, T_FG_UP_TO);
}

static void up_header(void) { say(BX_CARD, 14, TOP_Y + 5 * 2 * LINE_C, T_FG_KEYS); }

static void matup_scene(void) {
  List l;
  u16 k, n;
  u8 m, e;
  nup = 0;
  for (m = 0; m < N_MAT; ++m) if (mat_up[m] != 255) up_list[nup++] = m;
  list_init(&l, BX_CARD, 5, nup);
  l.lh = 2 * LINE_C;
  for (;;) {
    head();
    mk_draw = up_row; mk_moved = 0; mk_header = up_header; mk_sprites = 0;
    win_open(BX_CARD, 0, 2, 32, 26, T_CARD, PAL_W);
    k = list_run(&l, BTN_X);
    mk_header = 0;
    if (k & BTN_B) return;
    m = up_list[l.cur];
    n = (k & BTN_X) ? mat_up_max(m) : 1;
    if (!n) n = 1;
    e = mat_up_do(m, n);
    if (e == ERR_MATS) mk_notice(T_FORGE_SHORT, 0);
    else if (e == ERR_COINS) mk_notice(T_FORGE_NO_COIN, 0);
    else if (e == ERR_MAX) mk_notice(T_GOODS_LIMIT, 0);
    else { se_play(SE_OK); mk_notice(T_FG_UP_DONE, 0); }
  }
}

/* ---------------------------------------------------------------- root */
void forge_scene(u16 start) {
  u8 sel;
  u16 i;
  mk_screen();
  head();
  screen_on();
  if (start != NO_ITEM) {
    item_forge(start);
    fade_out();
    return;
  }
  for (;;) {
    head();
    win_open(BX_MENU, 6, 8, 20, 7, T_MENU, PAL_W);
    say(BX_MENU, 14, TOP_Y, T_FG_PICK);
    say(BX_MENU, 14, TOP_Y + LINE_H, T_FG_MATUP);
    say(BX_MENU, 14, TOP_Y + 2 * LINE_H, T_ACT_BACK);
    tb_flush(BX_MENU);
    sel = menu_rows(BX_MENU, 3, 48, 64);
    win_close(BX_MENU);
    if (sel == 255 || sel == 2) break;
    if (sel == 0) {
      i = pick_item(0, T_FG_PICK, NO_ITEM);
      if (i != NO_ITEM) {
        mk_screen(); screen_on();
        item_forge(i);
      }
      mk_screen(); screen_on();
    } else {
      matup_scene();
      mk_screen(); screen_on();
    }
  }
  save_write();
  fade_out();
}
