/* debug menu: R x3 on the map (debug_screens.gd, surface cards)
 *   武器・盾をもらう         any weapon or shield; a weapon's skills can be chosen
 *   道具を全部もらう         goods 99, materials 999, tickets 9
 *   武器・盾を全種類もらう   one of each (the SFC addition for "every item")
 *   図鑑を全部うめる         every monster, weapon, shield and special seen
 *   全部のエリアと STAGE を開く  (also L x3 on the map) */
#include "menukit.h"
#include "adventure.h"

#define BX_HEAD 0
#define BX_LIST 1
#define BX_INFO 2
#define BX_SUB 3
#define T_HEAD 1
#define T_LIST 65
#define T_INFO 449
#define T_ED 65              /* the editor replaces the list (65 + 28 * 16 = 513) */
#define T_SUB 513            /* skill list over the editor: 22 x 16 = 352 tiles (below the dialog's 865) */

static void head(u16 title) {
  box(BX_HEAD, 0, 0, 32, 2, T_HEAD, PAL_W, BOX_OPAQUE);
  say(BX_HEAD, 8, 2, title);
  tb_flush(BX_HEAD);
}

/* ---------------------------------------------------------------- give one item */
static u8 cat[N_DEX_ITEM];
static u8 ncat, cat_f;            /* filter 0 all, 1 sword, 2 spear, 3 hammer, 4 shield */
static u16 owned[N_ITEM];

static void make_cat(void) {
  u8 i, it;
  ncat = 0;
  for (i = 0; i < N_DEX_ITEM; ++i) {
    it = dex_item[i];
    if (cat_f && items[it].type != cat_f - 1) continue;
    cat[ncat++] = it;
  }
}

static void count_owned(void) {
  u16 i;
  memset(owned, 0, sizeof(owned));
  for (i = 0; i < sv.ninv; ++i) ++owned[sv.inv[i].id];
}

static u16 cat_filter_name(void) { return cat_f == 0 ? T_DEX_ALL : WTN_0 + cat_f - 1; }

static void cat_row(u16 i, u8 y) {
  u8 it = cat[i];
  const Item *r = &items[it];
  targ_str = RARN_0 + r->rarity;
  say(BX_LIST, 14, y, r->rarity >= R_SR ? TXT_YELLOW_S : TXT_GRAY_S);
  say(BX_LIST, 40, y, ITEMN_0 + it);
  if (r->type == WT_SHIELD) sayn(BX_LIST, 150, y, T_ITEM_DEF, r->def);
  else sayn(BX_LIST, 150, y, T_ITEM_ATK, r->atk);
  if (owned[it]) sayn(BX_LIST, 214, y, T_DBG_HAVE, owned[it]);
}

static void cat_header(void) {
  targ_num2 = D_INV_MAX;
  sayn(BX_LIST, 10, 4, sv.ninv >= D_INV_MAX ? T_DBG_PICK_FULL : T_DBG_PICK_HEAD, sv.ninv);
  targ_str = cat_filter_name();
  say(BX_LIST, 168, 4, T_DEX_FILTER);
  say(BX_LIST, 10, 174, T_DBG_PICK_KEYS);
}

/* skill editor: three slots, おまかせ, もらう, やめる */
static u8 ed_sk[3], ed_item;
static u8 sk_list[N_SKILL + 1], nsk_list;

static void sk_row(u16 i, u8 y) {
  u8 s = sk_list[i];
  if (s == 255) { say(BX_SUB, 14, y, T_DBG_SK_NONE); return; }
  say(BX_SUB, 14, y, SKILLN_0 + s);
  sayn(BX_SUB, 104, y, T_DEX_ST, skills[s].cost);
  targ_str = skills[s].wtype > 2 ? T_DEX_SPECIAL : WTN_0 + skills[s].wtype;
  say(BX_SUB, 144, y, TXT_GRAY_S);
}

/* pick a skill for the slot; 254 = cancelled */
static u8 pick_skill(void) {
  List l;
  u16 k;
  u8 s, t = items[ed_item].type;
  nsk_list = 0;
  sk_list[nsk_list++] = 255;
  for (s = 0; s < N_SKILL; ++s) if (skills[s].wtype == t || skills[s].wtype > 2) sk_list[nsk_list++] = s;
  list_init(&l, BX_SUB, 7, nsk_list);
  mk_draw = sk_row; mk_moved = 0; mk_header = 0; mk_sprites = 0;
  win_open(BX_SUB, 5, 6, 22, 16, T_SUB, PAL_W_FRAME);
  k = list_run(&l, 0);
  win_close(BX_SUB);
  if (k & BTN_B) return 254;
  return sk_list[l.cur];
}

static void ed_roll(void) {
  u8 n, k, sk[3];
  roll_skills(items[ed_item].type, sk, &n);
  for (k = 0; k < 3; ++k) ed_sk[k] = k < n ? sk[k] : 255;
}

static void ed_draw(void) {
  u8 k;
  const Item *r = &items[ed_item];
  win_open(BX_INFO, 2, 4, 28, 16, T_ED, PAL_W);
  tb_clear(BX_INFO);
  targ_str = RARN_0 + r->rarity;
  say(BX_INFO, 10, 4, TXT_YELLOW_S);
  say(BX_INFO, 36, 4, ITEMN_0 + ed_item);
  sayn(BX_INFO, 150, 4, T_ITEM_ATK, r->atk);
  for (k = 0; k < 3; ++k) {
    sayn(BX_INFO, 24, 24 + k * 16, T_DBG_SLOT, k + 1);
    say(BX_INFO, 80, 24 + k * 16, ed_sk[k] == 255 ? T_DBG_NONE : SKILLN_0 + ed_sk[k]);
  }
  say(BX_INFO, 24, 80, T_DBG_AUTO);
  say(BX_INFO, 24, 96, T_DBG_TAKE);
  say(BX_INFO, 24, 112, T_DBG_CANCEL);
  tb_flush(BX_INFO);
}

static void editor(u8 it) {
  static const u8 row_y[6] = { 24, 40, 56, 80, 96, 112 };
  u8 sel = 0, k, s, n, sk[3];
  ed_item = it;
  ed_roll();
  for (;;) {
    ed_draw();
    for (;;) {
      oam_next = 0;
      mk_cursor(16 + 24 - 11, 32 + row_y[sel] + 2);
      oam_finish();
      frame_wait();
      if (pad_rep & BTN_UP) { sel = sel ? sel - 1 : 5; se_play(SE_CURSOR); }
      if (pad_rep & BTN_DOWN) { sel = sel < 5 ? sel + 1 : 0; se_play(SE_CURSOR); }
      if (pad_new & BTN_B) { se_play(SE_CANCEL); win_close(BX_INFO); return; }
      if (pad_new & BTN_A) { se_play(SE_OK); break; }
    }
    oam_next = 0; oam_finish();
    if (sel < 3) {
      s = pick_skill();
      if (s != 254) { ed_sk[sel] = s; if (sel < 2) ++sel; }
    } else if (sel == 3) ed_roll();
    else if (sel == 4) {
      for (k = 0, n = 0; k < 3; ++k) if (ed_sk[k] != 255) sk[n++] = ed_sk[k];
      if (!n) { mk_notice(T_DBG_NEED_SKILL, 0); continue; }
      win_close(BX_INFO);
      mk_notice(debug_give(it, sk, n) ? T_DBG_GIVEN : T_DBG_BAG_FULL, 0);
      return;
    } else { win_close(BX_INFO); return; }
  }
}

static void give_scene(void) {
  List l;
  u16 k;
  u8 it;
  cat_f = 0;
  make_cat();
  count_owned();
  mk_screen();
  head(T_DBG_GIVE);
  screen_on();
  list_init(&l, BX_LIST, 10, ncat);
  l.y = 20;
  for (;;) {
    mk_draw = cat_row; mk_moved = 0; mk_header = cat_header; mk_sprites = 0;
    win_open(BX_LIST, 0, 2, 32, 24, T_LIST, PAL_W);
    k = list_run(&l, BTN_SELECT);
    mk_header = 0;
    if (k & BTN_B) return;
    if (k & BTN_SELECT) {
      cat_f = (u8)((cat_f + 1) % 5);
      make_cat();
      l.n = ncat; l.cur = l.top = 0;
      continue;
    }
    it = cat[l.cur];
    if (sv.ninv >= D_INV_MAX) { mk_notice(T_DBG_BAG_FULL, 0); continue; }
    if (items[it].type == WT_SHIELD || it == ITEM_FUUIN) {
      targ_str = ITEMN_0 + it;
      if (!mk_confirm(T_DBG_GIVE_ASK, 0)) continue;
      mk_notice(debug_give(it, 0, 0) ? T_DBG_GIVEN : T_DBG_BAG_FULL, 0);
    } else {
      win_close(BX_LIST);
      editor(it);
    }
    count_owned();
  }
}

/* ---------------------------------------------------------------- root */
enum { DB_GIVE, DB_GOODS, DB_ALL, DB_DEX, DB_OPEN, DB_BACK, DB_N };
static const u16 db_text[DB_N] = { T_DBG_GIVE, T_DBG_GOODS, T_DBG_ALL, T_DBG_DEX, T_DBG_OPEN, T_ACT_BACK };
static const u16 db_desc[DB_N] = { T_DBG_GIVE_D, T_DBG_GOODS_D, T_DBG_ALL_D, T_DBG_DEX_D, T_DBG_OPEN_D, 0 };

static void root_row(u16 i, u8 y) { say(BX_LIST, 14, y, db_text[i]); }

static void root_info(u16 i) {
  win_open(BX_INFO, 0, 17, 32, 11, T_INFO, PAL_W);
  tb_clear(BX_INFO);
  if (db_desc[i]) say(BX_INFO, 10, 6, db_desc[i]);
  switch (i) {
  case DB_GOODS:
    targ_num2 = sv.mats[MAT_IT_RESET];
    sayn(BX_INFO, 10, 40, T_DBG_GOODS_NOW, sv.mats[MAT_IT_FUERU]);
    targ_num2 = sv.mats[MAT_KYOUKA];
    targ_str = MATN_0 + MAT_KYOUKA;
    sayn(BX_INFO, 10, 54, T_DBG_GOODS_NOW2, sv.tickets);
    break;
  case DB_ALL:
    targ_num2 = D_INV_MAX;
    sayn(BX_INFO, 10, 40, T_DBG_BAG_NOW, sv.ninv);
    break;
  case DB_DEX:
    targ_num2 = debug_dex_count(2);
    sayn(BX_INFO, 10, 40, T_DBG_DEX_NOW, debug_dex_count(0));
    sayn(BX_INFO, 10, 54, T_DBG_DEX_NOW2, debug_dex_count(1));
    break;
  case DB_OPEN:
    if ((sv.story & ST_DEBUG_OPEN) && sv.area_open == AR_SECRET) say(BX_INFO, 10, 40, T_DBG_OPEN_ALL);
    else { targ_num2 = AR_SECRET + 1; sayn(BX_INFO, 10, 40, T_DBG_OPEN_NOW, sv.area_open + 1); }
    break;
  }
  tb_flush(BX_INFO);
}

static void root_screen(void) {
  mk_screen();
  head(T_DBG_HEAD);
  screen_on();
}

void debug_scene(void) {
  List l;
  u16 k, n;
  fade_out();
  list_init(&l, BX_LIST, DB_N, DB_N);
  l.lh = 16;
  root_screen();
  for (;;) {
    mk_draw = root_row; mk_moved = root_info; mk_header = 0; mk_sprites = 0;
    win_open(BX_LIST, 4, 3, 24, 13, T_LIST, PAL_W);
    k = list_run(&l, 0);
    if ((k & BTN_B) || l.cur == DB_BACK) break;
    switch (l.cur) {
    case DB_GIVE:
      fade_out();
      give_scene();
      fade_out();
      root_screen();
      break;
    case DB_GOODS:
      if (!mk_confirm(T_DBG_GOODS_ASK, 0)) break;
      debug_goods();
      mk_notice(T_DBG_GOODS_DONE, 0);
      break;
    case DB_ALL:
      if (sv.ninv + 1 >= D_INV_MAX) { mk_notice(T_DBG_BAG_FULL, 0); break; }
      if (!mk_confirm(T_DBG_ALL_ASK, 0)) break;
      n = debug_give_all();
      targ_num = n;
      mk_notice(T_DBG_ALL_DONE, n < N_DEX_ITEM ? T_DBG_ALL_PART : 0);
      break;
    case DB_DEX:
      if (!mk_confirm(T_DBG_DEX_ASK, T_DBG_NOUNDO)) break;
      debug_dex_all();
      mk_notice(T_DBG_DEX_DONE, 0);
      break;
    case DB_OPEN:
      if ((sv.story & ST_DEBUG_OPEN) && sv.area_open == AR_SECRET) { mk_notice(T_DBG_OPEN_ALREADY, 0); break; }
      if (!mk_confirm(T_DBG_OPEN_ASK, T_DBG_NOUNDO)) break;
      debug_open_all();
      mk_notice(T_DBG_OPEN_DONE, 0);
      break;
    }
  }
  fade_out();
}
