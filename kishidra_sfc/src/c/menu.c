/* menu: root, equipment (MAIN / SUB / THIRD / shield), inventory, specials, options (menu_screens.gd) */
#include "menukit.h"
#include "adventure.h"
#include "story.h"

#define BX_HEAD 0
#define BX_LIST 1
#define BX_INFO 2
#define BX_ACT 3
#define T_HEAD 1
#define T_LIST 65
#define T_INFO 545          /* 65 + 32 x 15 (list); the card is 32 x 11 (to 896) */
#define T_ACT MK_DLG_TILE(18, 7)   /* shares the dialog range (never open together) */

static u16 order[INV_SLOTS];
static u16 norder;

/* ---------------------------------------------------------------- sorting (sorted_items) */
static const u8 el_order[6] = { 5, 0, 1, 2, 3, 4 };   /* EL_NONE last */

static u32 sort_key(u16 i, u8 mode) {
  const Inv *it = &sv.inv[i];
  const Item *r = &items[it->id];
  u32 t = r->type, rr = 15 - r->rarity, v = 4095 - MIN(r->type == WT_SHIELD ? r->def : r->atk, 4095);
  u32 e = r->type == WT_SHIELD ? 5 : el_order[r->element < 6 ? r->element : 0];
  u32 k;
  switch (mode) {
  case 1: k = (t * 16 + rr) * 4096 + v; break;
  case 2: k = (rr * 16 + t) * 4096 + v; break;
  case 4: k = ((e * 16 + t) * 16 + rr) * 4096 + v; break;
  default: k = ((r->type == WT_SHIELD ? 1UL : 0UL) * 4096 + v) * 16 + rr; break;
  }
  return (k << 8) | it->id;
}

u16 pick_exclude = NO_ITEM;

/* filter: 0 all, 1 weapons, 2 shields, 3 enhancement sources (not equipped / favourite / pick_exclude) */
static void make_order(u8 filter) {
  u16 i, j, gap, t;
  u32 kt;
  u8 mode = sv.sort_mode;
  norder = 0;
  for (i = 0; i < sv.ninv; ++i) {
    u8 sh = item_is_shield(sv.inv[i].id);
    if ((filter == 1 && sh) || (filter == 2 && !sh)) continue;
    if (filter == 3 && (i == pick_exclude || equipped_slot(i) != 255 || inv_fav(&sv.inv[i]))) continue;
    order[norder++] = i;
  }
  if (mode == 0 || norder < 2) return;
  /* shell sort, ties by inventory order */
  for (gap = norder / 2; gap > 0; gap /= 2) {
    for (i = gap; i < norder; ++i) {
      t = order[i];
      kt = sort_key(t, mode);
      for (j = i; j >= gap; j -= gap) {
        u32 kj = sort_key(order[j - gap], mode);
        if (kj < kt || (kj == kt && order[j - gap] < t)) break;
        order[j] = order[j - gap];
      }
      order[j] = t;
    }
  }
}

/* ---------------------------------------------------------------- shared list screen */
/* list window above the item card: 6 rows and the key help on its own tile rows */
#define LIST_H (26 - MK_CARD_H)
#define LIST_ROWS 6
#define LIST_KEYS_Y 96
static u8 list_slot;        /* equip pick: slot being changed, 255 = inventory */

static void head(u16 title) {
  box(BX_HEAD, 0, 0, 32, 2, T_HEAD, PAL_W, BOX_OPAQUE);
  say(BX_HEAD, 8, 2, title);
  sayn(BX_HEAD, 176, 2, TXT_COINS, sv.coins);
  tb_flush(BX_HEAD);
}

static void info(u16 i) {
  win_open(BX_INFO, 0, 28 - MK_CARD_H, 32, MK_CARD_H, T_INFO, PAL_W);
  mk_item_detail(BX_INFO, i == NO_ITEM ? 0 : &sv.inv[i]);
  tb_flush(BX_INFO);
}

static void item_row(u16 n, u8 y) {
  u16 i = order[n];
  Inv *it = &sv.inv[i];
  u8 s = equipped_slot(i);
  mk_item_name(BX_LIST, 14, y, it, 1);
  if (s != 255) say(BX_LIST, 200, y, T_TAG_MAIN + s);
  if (inv_fav(it)) say(BX_LIST, 216, y, T_TAG_FAV);      /* ends by 227: clear of the scroll marks at 236 */
  targ_str = RARN_0 + items[it->id].rarity;
  say(BX_LIST, 176, y, TXT_GRAY_S);
}

static void item_header(void) {
  targ_str = T_SORT_0 + sv.sort_mode;
  say(BX_LIST, 14, LIST_KEYS_Y, T_LIST_KEYS);
}

static void item_moved(u16 n) { info(norder ? order[n] : NO_ITEM); }

/* list of items (filter); returns the inventory index picked or NO_ITEM */
u16 pick_item(u8 filter, u16 title, u16 start) {
  List l;
  u16 k, i;
  head(title);
  make_order(filter);
  list_init(&l, BX_LIST, LIST_ROWS, norder);
  for (i = 0; i < norder; ++i) if (order[i] == start) l.cur = i;
  for (;;) {
    mk_draw = item_row; mk_moved = item_moved; mk_header = item_header; mk_sprites = 0;
    win_open(BX_LIST, 0, 2, 32, LIST_H, T_LIST, PAL_W);
    l.n = norder;
    k = list_run(&l, BTN_SELECT);
    if (k & BTN_SELECT) {
      i = norder ? order[l.cur] : NO_ITEM;
      sv.sort_mode = (sv.sort_mode + 1) % 5;
      make_order(filter);
      for (k = 0; k < norder; ++k) if (order[k] == i) l.cur = k;
      continue;
    }
    mk_header = 0;
    if (k & BTN_B) return NO_ITEM;
    if (norder) return order[l.cur];
  }
}

/* ---------------------------------------------------------------- equipment */
static void equip_set(u8 slot, u16 i) {
  u8 other = equipped_slot(i);
  if (other != 255 && other != slot) sv.equip[other] = sv.equip[slot];
  sv.equip[slot] = i;
}

static void slot_row(u16 s, u8 y) {
  say(BX_LIST, 14, y, T_SLOT_MAIN + s);
  if (sv.equip[s] == NO_ITEM) say(BX_LIST, 70, y, T_SLOT_EMPTY);
  else mk_item_name(BX_LIST, 70, y, &sv.inv[sv.equip[s]], 0);
}

static void slot_header(void) { say(BX_LIST, 14, LIST_KEYS_Y, T_EQ_KEYS); }

static void slot_moved(u16 s) { info(sv.equip[s]); }

void equip_scene(void) {
  List l;
  u16 k, i;
  mk_screen();
  screen_on();
  list_init(&l, BX_LIST, 4, 4);
  l.lh = LINE_C;
  for (;;) {
    head(T_EQUIP_TITLE);
    mk_draw = slot_row; mk_moved = slot_moved; mk_header = slot_header; mk_sprites = 0;
    win_open(BX_LIST, 0, 2, 32, LIST_H, T_LIST, PAL_W);
    k = list_run(&l, BTN_X);
    mk_header = 0;
    if (k & BTN_B) break;
    if (k & BTN_X) {
      if (l.cur == EQ_SHIELD) mk_notice(T_SHIELD_KEEP, 0);
      else if (l.cur == EQ_MAIN) mk_notice(T_MAIN_KEEP, 0);
      else sv.equip[l.cur] = NO_ITEM;
      continue;
    }
    list_slot = (u8)l.cur;
    i = pick_item(l.cur == EQ_SHIELD ? 2 : 1, T_EQUIP_PICK, sv.equip[l.cur]);
    if (i != NO_ITEM) equip_set(list_slot, i);
  }
  save_write();
  fade_out();
}

/* ---------------------------------------------------------------- inventory */
static u8 item_actions(u16 i) {
  List l;
  u16 k;
  Inv *it = &sv.inv[i];
  u8 sel;
  win_open(BX_ACT, 7, 7, 18, 7, T_ACT, PAL_W_FRAME);
  say(BX_ACT, 14, TOP_Y, inv_fav(it) ? T_ACT_UNFAV : T_ACT_FAV);
  sayn(BX_ACT, 14, TOP_Y + LINE_H, T_ACT_SELL, sell_price(it->id));
  say(BX_ACT, 14, TOP_Y + 2 * LINE_H, T_ACT_FORGE);
  tb_flush(BX_ACT);
  list_init(&l, BX_ACT, 3, 3);
  sel = 0;
  for (;;) {
    oam_next = 0;
    mk_cursor(56 + 3, 56 + TOP_Y + sel * LINE_H + 2);
    oam_finish();
    frame_wait();
    if (pad_rep & BTN_UP) { sel = sel ? sel - 1 : 2; se_play(SE_CURSOR); }
    if (pad_rep & BTN_DOWN) { sel = sel < 2 ? sel + 1 : 0; se_play(SE_CURSOR); }
    if (pad_new & BTN_B) { se_play(SE_CANCEL); win_close(BX_ACT); return 0; }
    if (pad_new & BTN_A) break;
  }
  se_play(SE_OK);
  win_close(BX_ACT);
  (void)k;
  if (sel == 0) { fav_toggle(i); return 0; }
  if (sel == 1) {
    k = sell_price(it->id);
    if (equipped_slot(i) != 255 || inv_fav(it)) { mk_notice(T_CANT_SELL, 0); return 0; }
    if (!mk_confirm(T_CONFIRM, 0)) return 0;
    if (sell_item(i) == ERR_OK) { se_play(SE_COIN); targ_num = k; mk_notice(T_SOLD, 0); }
    return 0;
  }
  return 1;   /* forge */
}

static void items_scene(void) {
  u16 i, last = NO_ITEM;
  mk_screen();
  screen_on();
  for (;;) {
    targ_num2 = D_INV_MAX;
    targ_num = sv.ninv;
    i = pick_item(0, T_ITEMS_HEAD, last);
    if (i == NO_ITEM) break;
    last = i;
    if (item_actions(i)) {
      fade_out();
      forge_scene(i);
      mk_screen();
      screen_on();
    }
  }
  save_write();
  fade_out();
}

/* ---------------------------------------------------------------- specials (status) */
static u8 sp_have[N_SKILL];

/* two lines per entry: the name, then the skill chain, both yellow when it can be used */
static void sp_row(u16 n, u8 y) {
  const Special *p = &specials[n];
  u8 ok = sp_have[p->sk1] && sp_have[p->sk2] && (p->sk3 == 255 || sp_have[p->sk3]);
  u16 c = ok ? TXT_YELLOW_S : TXT_GRAY_S, x = 26;
  u8 k, sk;
  targ_str = SPN_0 + n;
  say(BX_LIST, 14, y, c);
  y += LINE_C;                 /* the gray arrows must not share tile rows with the name */
  for (k = 0; k < 3; ++k) {
    sk = k == 0 ? p->sk1 : (k == 1 ? p->sk2 : p->sk3);
    if (sk == 255) break;
    if (k) { x = (x + 7) & ~7; say(BX_LIST, x, y, TXT_NEXT_ARROW); x += 12; }   /* gray arrow on a tile of its own */
    targ_str = SKILLN_0 + sk; say(BX_LIST, x, y, c); x += msg_width(SKILLN_0 + sk) + 4;
  }
}

static void specials_scene(void) {
  List l;
  u8 s, k;
  memset(sp_have, 0, sizeof(sp_have));
  for (s = 0; s < 3; ++s) {
    if (sv.equip[s] == NO_ITEM) continue;
    for (k = 0; k < 3; ++k) if (sv.inv[sv.equip[s]].sk[k] != 255) sp_have[sv.inv[sv.equip[s]].sk[k]] = 1;
  }
  mk_screen();
  head(T_SP_HEAD);
  screen_on();
  list_init(&l, BX_LIST, 6, N_SPECIAL);
  l.lh = 2 * LINE_C;
  mk_draw = sp_row; mk_moved = 0; mk_header = 0; mk_sprites = 0;
  win_open(BX_LIST, 0, 2, 32, 26, T_LIST, PAL_W);
  do { s = (u8)(list_run(&l, 0) & BTN_B); } while (!s);
  fade_out();
}

/* ---------------------------------------------------------------- options */
static void options_scene(void) {
  u8 sel = 0;
  mk_screen();
  head(T_MENU_OPTION);
  screen_on();
  for (;;) {
    win_open(BX_LIST, 4, 6, 24, 6, T_LIST, PAL_W);
    sayn(BX_LIST, 20, TOP_Y, T_OPT_GUARD, sv.guard_late);
    say(BX_LIST, 20, TOP_Y + LINE_C, T_OPT_BACK);
    tb_flush(BX_LIST);
    for (;;) {
      oam_next = 0;
      mk_cursor(32 + 8, 48 + TOP_Y + sel * LINE_C + 2);
      oam_finish();
      frame_wait();
      if (pad_rep & (BTN_UP | BTN_DOWN)) { sel ^= 1; se_play(SE_CURSOR); }
      if (sel == 0 && (pad_rep & (BTN_LEFT | BTN_RIGHT))) {
        if ((pad_rep & BTN_LEFT) && sv.guard_late) --sv.guard_late;
        if ((pad_rep & BTN_RIGHT) && sv.guard_late < 10) ++sv.guard_late;
        se_play(SE_CURSOR);
        break;
      }
      if ((pad_new & BTN_B) || (sel == 1 && (pad_new & BTN_A))) { se_play(SE_CANCEL); save_write(); fade_out(); return; }
    }
  }
}

/* ---------------------------------------------------------------- root */
enum { M_EQUIP, M_ITEMS, M_FORGE, M_SHOP, M_DEX, M_STATUS, M_DIFF, M_OPTION, M_TITLE, M_N };
static const u16 menu_text[M_N] = { T_MENU_EQUIP, T_MENU_ITEMS, T_MENU_FORGE, T_MENU_SHOP, T_MENU_DEX, T_MENU_STATUS, T_MENU_DIFF, T_MENU_OPTION, T_MENU_TITLE };

static void root_row(u16 i, u8 y) {
  say(BX_LIST, 14, y, menu_text[i]);
  if (i == M_DIFF) { targ_str = T_DIFF_0 + sv.difficulty; say(BX_LIST, 64, y, TXT_GRAY_S); }
}

u8 menu_scene(void) {
  List l;
  u16 k;
  u8 d, a;
  fade_out();
  list_init(&l, BX_LIST, M_N, M_N);
  l.lh = 16;
  for (;;) {
    mk_screen();
    head(T_MENU_HEAD);
    mk_draw = root_row; mk_moved = 0; mk_header = 0; mk_sprites = 0;
    win_open(BX_LIST, 8, 3, 16, 20, T_LIST, PAL_W);
    screen_on();
    k = list_run(&l, BTN_Y | BTN_X | BTN_START);
    if (k & (BTN_B | BTN_Y | BTN_X | BTN_START)) break;
    fade_out();
    switch (l.cur) {
    case M_EQUIP: equip_scene(); break;
    case M_ITEMS: items_scene(); break;
    case M_FORGE: forge_scene(NO_ITEM); break;
    case M_SHOP:
      a = last_shop_area();
      if (a == 255) { mk_screen(); screen_on(); mk_notice(T_NO_SHOP, 0); fade_out(); }
      else shop_scene(a, 0);
      break;
    case M_DEX: dex_scene(); break;
    case M_STATUS: specials_scene(); break;
    case M_DIFF:
      d = difficulty_select(sv.difficulty, 1);
      if (d != 255) { sv.difficulty = d; save_write(); }
      break;
    case M_OPTION: options_scene(); break;
    case M_TITLE:
      mk_screen(); screen_on();
      if (mk_confirm(T_TO_TITLE, 0)) { save_write(); fade_out(); return MA_TITLE; }
      fade_out();
      break;
    }
  }
  fade_out();
  return MA_NONE;
}
