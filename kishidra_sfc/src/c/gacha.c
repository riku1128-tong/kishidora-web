/* town shop: mixed gacha (sword / spear / hammer / shield) and the item shop */
#include "menukit.h"
#include "adventure.h"

#define BX_HEAD 0
#define BX_BODY 1
#define BX_RES 2
#define T_HEAD 1
#define T_BODY 65
#define T_RES 385

static u8 shop_area;

static void head(void) {
  box(BX_HEAD, 0, 0, 32, 2, T_HEAD, PAL_W, BOX_OPAQUE);
  targ_str = AREAN_0 + shop_area;
  say(BX_HEAD, 8, 2, T_SHOP_HEAD);
  sayn(BX_HEAD, 176, 2, TXT_COINS, sv.coins);
  tb_flush(BX_HEAD);
}

/* white flash: colour math adds the fixed colour */
static void flash(u8 frames) {
  u8 i, v;
  cgwsel = 0; cgadsub = 0x3f;
  for (i = 0; i < frames; ++i) {
    v = (u8)(31 - i * 31 / frames);
    coldata[0] = coldata[1] = coldata[2] = v;
    frame_wait();
  }
  cgadsub = 0;
  coldata[0] = coldata[1] = coldata[2] = 0;
}

static void show_results(u8 n) {
  u8 i, best = 0, y;
  GachaPull *g;
  for (i = 0; i < n; ++i) if (items[gacha_out[i].id].rarity > best) best = items[gacha_out[i].id].rarity;
  /* charge: a short shake of the window, then the flash (brighter for SR) */
  se_play(SE_CHARGE);
  for (i = 0; i < 40; ++i) { scroll[0] = (i & 2) ? 2 : 0; frame_wait(); }
  scroll[0] = 0;
  se_play(best >= R_SR ? SE_LUCKY : SE_DROP);
  flash(best >= R_SR ? 40 : 20);
  win_open(BX_RES, 1, 3, 30, 21, T_RES, PAL_W_FRAME);
  say_center(BX_RES, 4, T_GACHA_GOT);
  for (i = 0; i < n; ++i) {
    g = &gacha_out[i];
    y = 20 + i * 13;
    targ_str = RARN_0 + items[g->id].rarity;
    say(BX_RES, 10, y, items[g->id].rarity >= R_SR ? TXT_YELLOW_S : TXT_S);
    targ_str = WTN_0 + items[g->id].type;
    say(BX_RES, 44, y, TXT_GRAY_S);
    say(BX_RES, 66, y, ITEMN_0 + g->id);
    if (g->fresh) say(BX_RES, 196, y, T_GACHA_NEWTAG);
    if (g->ticket) say(BX_RES, 224, y, T_GACHA_TKT);
  }
  say(BX_RES, 10, 152, T_GACHA_KEYS);
  tb_flush(BX_RES);
  do { frame_wait(); } while (!(pad_new & (BTN_A | BTN_B)));
  se_play(SE_OK);
  win_close(BX_RES);
}

static void gacha_body(void) {
  win_open(BX_BODY, 2, 4, 28, 12, T_BODY, PAL_W);
  say_center(BX_BODY, 4, T_GACHA_DESC);
  say(BX_BODY, 20, 20, T_GACHA_RATES);
  sayn(BX_BODY, 30, 48, T_GACHA_PULL1, GACHA_PRICE);
  sayn(BX_BODY, 30, 62, T_GACHA_PULL10, (u32)GACHA_PRICE * T_GACHA_MULTI);
  say(BX_BODY, 30, 76, T_GACHA_BACK);
  tb_flush(BX_BODY);
}

static void gacha_run(void) {
  u8 sel = 0, n, e;
  gacha_body();
  for (;;) {
    oam_next = 0;
    mk_cursor(16 + 30 - 11, 32 + 48 + sel * 14 + 2);
    oam_finish();
    frame_wait();
    if (pad_rep & BTN_UP) { sel = sel ? sel - 1 : 2; se_play(SE_CURSOR); }
    if (pad_rep & BTN_DOWN) { sel = sel < 2 ? sel + 1 : 0; se_play(SE_CURSOR); }
    if (pad_new & BTN_B) { se_play(SE_CANCEL); return; }
    if (!(pad_new & BTN_A)) continue;
    if (sel == 2) { se_play(SE_CANCEL); return; }
    se_play(SE_OK);
    n = sel ? T_GACHA_MULTI : 1;
    e = gacha_pull(n);
    if (e == ERR_COINS) { mk_notice(T_GACHA_NO_COIN, 0); gacha_body(); continue; }
    if (e == ERR_FULL) { mk_notice(T_GACHA_FULL, 0); gacha_body(); continue; }
    oam_next = 0; oam_finish();
    show_results(n);
    head();
    gacha_body();
  }
}

static void goods_run(void) {
  u8 e;
  for (;;) {
    win_open(BX_BODY, 2, 4, 28, 9, T_BODY, PAL_W);
    targ_str = MATN_0 + MAT_IT_RESET;
    sayn(BX_BODY, 30, 6, T_GOODS_LINE, PRICE_IT_RESET);
    sayn(BX_BODY, 150, 20, T_GOODS_HAVE, sv.mats[MAT_IT_RESET]);
    say(BX_BODY, 30, 34, GOODSD_IT_RESET);
    say(BX_BODY, 30, 54, T_GACHA_BACK);
    tb_flush(BX_BODY);
    for (;;) {
      oam_next = 0;
      mk_cursor(16 + 30 - 11, 32 + 6 + 2);
      oam_finish();
      frame_wait();
      if (pad_new & (BTN_B | BTN_A)) break;
    }
    if (pad_new & BTN_B) { se_play(SE_CANCEL); return; }
    se_play(SE_OK);
    if (!mk_confirm(T_BUY_ASK, 0)) continue;
    e = goods_buy(MAT_IT_RESET);
    if (e == ERR_COINS) mk_notice(T_GACHA_NO_COIN, 0);
    else if (e == ERR_MAX) mk_notice(T_GOODS_LIMIT, 0);
    else { se_play(SE_COIN); mk_notice(T_GOODS_BOUGHT, 0); }
    head();
  }
}

void shop_scene(u8 area, u8 goods) {
  shop_area = area;
  fade_out();
  mk_screen();
  head();
  screen_on();
  if (goods) goods_run(); else gacha_run();
  fade_out();
}
