/* result screen after a battle (battle.gd _show_result / _show_rewards) */
#include "menukit.h"
#include "adventure.h"

#define BX_RES 0
#define T_RES 1

void result_scene(u8 outcome) {
  u8 i, y, shown, bags = 0;
  if (b_mode == BM_TUTORIAL) { if (outcome == 1) commit_tutorial(); else commit_lose(); }
  else if (b_mode == BM_TRAINING) { memset(&res, 0, sizeof(res)); res.orb = res.opened = 255; }
  else if (b_mirage) commit_mirage(outcome == 1);
  else if (outcome == 1) commit_win();
  else if (outcome == 2) commit_leave();
  else commit_lose();
  for (i = 0; i < N_MAT; ++i) bags += b_mats[i];
  mk_screen();
  /* full height; lines 16 px apart (their colours differ) */
  win_open(BX_RES, 1, 0, 30, 28, T_RES, PAL_W_FRAME);
  if (b_mode == BM_TUTORIAL && outcome == 1) say_center(BX_RES, TOP_Y + 4, TUTX_CLEAR);
  else say_center(BX_RES, TOP_Y + 4, outcome == 1 ? T_RES_WIN : (outcome == 2 ? T_RES_LEFT : T_RES_LOSE));
  y = 32;
  if (b_mode == BM_TRAINING) {
    say(BX_RES, 24, y, T_MEN_TRAINING_D);
  } else if (b_mode == BM_TUTORIAL) {
    say(BX_RES, 24, y, outcome == 1 ? T_RES_TUT_GIFT : T_RES_NONE); y += LINE_C;
    for (i = 0; outcome == 1 && i < res.nitems; ++i) {
      say(BX_RES, 40, y, ITEMN_0 + b_items[i].item);
      if (res.fresh[i]) say(BX_RES, 184, y, T_GACHA_NEWTAG);
      y += LINE_C;
    }
  } else if (res.mirage) {
    targ_num2 = MIRAGE_WAVES;
    sayn(BX_RES, 24, y, T_RES_WAVE, res.mirage_wave); y += LINE_C;
    if (res.mirage_clear) { say(BX_RES, 24, y, T_RES_CLEARED); y += LINE_C; }
    say(BX_RES, 24, y, res.mirage_up ? T_RES_BEST : T_RES_NOREC); y += LINE_C;
    if (res.coins) { sayn(BX_RES, 24, y, T_RES_COINS, res.coins); y += LINE_C; }
    if (res.first) { targ_str = MATN_0 + MIRAGE_FIRST_MAT; targ_num = MIRAGE_FIRST_MAT_N; say_wrap(BX_RES, 24, y, T_RES_FIRSTMAT, 232); y += LINE_C; }
  } else {
    say(BX_RES, 24, y, outcome == 1 ? T_RES_GOT : T_RES_LOST); y += LINE_C;
    sayn(BX_RES, 24, y, outcome == 1 ? T_RES_COINS : T_RES_COINS_GRAY, b_coins);
    if (bags) sayn(BX_RES, 128, y, T_RES_BAGS, bags);
    y += LINE_C;
    shown = 0;
    for (i = 0; i < b_nitems && shown < 6; ++i, ++shown) {
      u8 id = b_items[i].item;
      targ_str = RARN_0 + items[id].rarity;
      say(BX_RES, 24, y, outcome == 1 && items[id].rarity >= R_SR ? TXT_YELLOW_S : TXT_GRAY_S);
      say(BX_RES, 56, y, ITEMN_0 + id);
      if (outcome == 1 && res.fresh[i]) say(BX_RES, 184, y, T_GACHA_NEWTAG);
      y += LINE_C;
    }
    if (b_nitems > shown) { sayn(BX_RES, 24, y, T_RES_MORE, b_nitems - shown); y += LINE_C; }
    if (!b_nitems) { say(BX_RES, 24, y, T_RES_NONE); y += LINE_C; }
    if (outcome == 1 && res.overflow) sayn(BX_RES, 24, y, T_RES_OVER, res.overflow);
  }
  say(BX_RES, 24, 192, T_RES_KEYS);
  tb_flush(BX_RES);
  screen_on();
  do { frame_wait(); } while (!(pad_new & (BTN_A | BTN_START)));
  se_play(SE_OK);
  fade_out();
}
