/* forge: enhance / evolve / slots / material synthesis (forge_screens.gd) */
#include "menukit.h"
#include "adventure.h"

#define BX_HEAD 0
#define BX_CARD 1
#define BX_MENU 2
#define BX_SUB 3
#define T_HEAD 1
#define T_CARD 65
#define T_MENU 353
#define T_SUB 353          /* menu and sub windows are never open together */

static u16 head_item = NO_ITEM;

static void head(void) {
  box(BX_HEAD, 0, 0, 32, 2, T_HEAD, PAL_W, BOX_OPAQUE);
  say(BX_HEAD, 8, 2, T_FORGE_TITLE);
  if (head_item != NO_ITEM) mk_item_name(BX_HEAD, 64, 2, &sv.inv[head_item]);
  sayn(BX_HEAD, 184, 2, TXT_COINS, sv.coins);
  tb_flush(BX_HEAD);
}

static void card(u16 i) {
  win_open(BX_CARD, 0, 2, 32, 9, T_CARD, PAL_W);
  mk_item_detail(BX_CARD, &sv.inv[i]);
  tb_flush(BX_CARD);
}

/* small vertical menu; returns the row or 255 */
static u8 menu_rows(u8 bx, u8 n, u8 sx, u8 sy) {
  u8 sel = 0;
  for (;;) {
    oam_next = 0;
    mk_cursor(sx + 3, sy + 4 + sel * 14 + 2);
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
  win_open(BX_SUB, 6, 12, 20, 12, T_SUB, PAL_W_FRAME);
  say(BX_SUB, 10, 4, T_FG_SRC_HEAD);
  for (k = 0; k < 4; ++k) {
    targ_str = MATN_0 + enh_mats[k];
    sayn(BX_SUB, 14, 18 + k * 14, T_FG_SRC_LINE, sv.mats[enh_mats[k]]);
  }
  say(BX_SUB, 14, 18 + 4 * 14, T_FG_SRC_ITEM);
  tb_flush(BX_SUB);
  sel = menu_rows(BX_SUB, 5, 48, 96 + 14);
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
static void do_evolve(u16 i) {
  u8 id = sv.inv[i].id, to = evolve_target(id), k, e;
  if (to == 255) { mk_notice(T_FG_NO_EVO, 0); return; }
  evolve_need(id);
  k = (u8)((18 + (need.n + 1) * 14 + 13) >> 3);     /* below the confirm dialog (rows 8-14) */
  win_open(BX_SUB, 4, k > 13 ? 28 - k : 15, 24, k, T_SUB, PAL_W_FRAME);
  targ_str = ITEMN_0 + to;
  say(BX_SUB, 10, 4, T_FG_TO);
  for (k = 0; k < need.n; ++k) {
    targ_str = MATN_0 + need.mat[k];
    targ_num2 = need.cnt[k];
    sayn(BX_SUB, 14, 18 + k * 14, sv.mats[need.mat[k]] < need.cnt[k] ? T_FG_NEED_RED : T_FG_NEED, sv.mats[need.mat[k]]);
  }
  sayn(BX_SUB, 14, 18 + need.n * 14, sv.coins < need.coins ? T_FG_COINS_RED : T_FG_COINS, need.coins);
  tb_flush(BX_SUB);
  e = mk_confirm(T_FG_EVO, 0);
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
  win_open(BX_SUB, 4, 12, 24, 4, T_SUB, PAL_W_FRAME);
  say(BX_SUB, 10, 4, T_FG_WHICH);
  for (k = 0; k < n; ++k) say(BX_SUB, 24 + k * 16, 18, T_STAR_0 + enh_get(it, k));
  tb_flush(BX_SUB);
  for (;;) {
    oam_next = 0;
    spr_add(32 + 24 + sel * 16 + 2, 96 + 18 - 8, MK_ATTR(MK_T_DOWN, MK_PAL_MENU, 3), 0);
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
    win_open(BX_MENU, 4, 12, 24, 9, T_MENU, PAL_W);
    say(BX_MENU, 14, 4, T_FG_ENH);
    say(BX_MENU, 14, 18, T_FG_EVO);
    sayn(BX_MENU, 14, 32, T_FG_ADD, sv.mats[MAT_IT_FUERU]);
    sayn(BX_MENU, 14, 46, T_FG_RESET, sv.mats[MAT_IT_RESET]);
    say(BX_MENU, 14, 60, T_ACT_BACK);
    tb_flush(BX_MENU);
    sel = menu_rows(BX_MENU, 5, 32, 96);
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

static void up_row(u16 n, u8 y) {
  u8 m = up_list[n];
  targ_str = MATN_0 + m;
  sayn(BX_CARD, 14, y, T_FG_UP_LINE, sv.mats[m]);
  targ_str = MATN_0 + mat_up[m];
  say(BX_CARD, 120, y, T_FG_UP_TO);
}

static void up_header(void) { say(BX_CARD, 14, 186, T_FG_KEYS); }

static void matup_scene(void) {
  List l;
  u16 k, n;
  u8 m, e;
  nup = 0;
  for (m = 0; m < N_MAT; ++m) if (mat_up[m] != 255) up_list[nup++] = m;
  list_init(&l, BX_CARD, 12, nup);
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
    win_open(BX_MENU, 6, 8, 20, 6, T_MENU, PAL_W);
    say(BX_MENU, 14, 4, T_FG_PICK);
    say(BX_MENU, 14, 18, T_FG_MATUP);
    say(BX_MENU, 14, 32, T_ACT_BACK);
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
