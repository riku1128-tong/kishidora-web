/* result screen after a battle (battle.gd _show_result / _show_rewards) */
#include "menukit.h"
#include "adventure.h"

#define BX_RES 0
#define T_RES 1

void result_scene(u8 outcome) {
  u8 i, y, shown, bags = 0;
  if (b_mirage) commit_mirage(outcome == 1);
  else if (outcome == 1) commit_win();
  else commit_lose();
  for (i = 0; i < N_MAT; ++i) bags += b_mats[i];
  mk_screen();
  win_open(BX_RES, 2, 2, 28, 24, T_RES, PAL_W_FRAME);
  say_center(BX_RES, 6, outcome == 1 ? T_RES_WIN : (outcome == 2 ? T_RES_LEFT : T_RES_LOSE));
  y = 32;
  if (res.mirage) {
    targ_num2 = MIRAGE_WAVES;
    sayn(BX_RES, 24, y, T_RES_WAVE, res.mirage_wave); y += 18;
    if (res.mirage_clear) { say(BX_RES, 24, y, T_RES_CLEARED); y += 16; }
    say(BX_RES, 24, y, res.mirage_up ? T_RES_BEST : T_RES_NOREC); y += 16;
    if (res.coins) { sayn(BX_RES, 24, y, T_RES_COINS, res.coins); y += 16; }
    if (res.first) { targ_str = MATN_0 + MIRAGE_FIRST_MAT; sayn(BX_RES, 24, y, T_RES_FIRSTMAT, MIRAGE_FIRST_MAT_N); y += 16; }
  } else {
    say(BX_RES, 24, y, outcome == 1 ? T_RES_GOT : T_RES_LOST); y += 16;
    sayn(BX_RES, 24, y, outcome == 1 ? T_RES_COINS : T_RES_COINS_GRAY, b_coins);
    if (bags) sayn(BX_RES, 120, y, T_RES_BAGS, bags);
    y += 16;
    shown = 0;
    for (i = 0; i < b_nitems && shown < 8; ++i, ++shown) {
      u8 id = b_items[i].item;
      targ_str = RARN_0 + items[id].rarity;
      say(BX_RES, 24, y, outcome == 1 && items[id].rarity >= R_SR ? TXT_YELLOW_S : TXT_GRAY_S);
      say(BX_RES, 60, y, ITEMN_0 + id);
      if (outcome == 1 && res.fresh[i]) say(BX_RES, 180, y, T_GACHA_NEWTAG);
      y += 13;
    }
    if (b_nitems > shown) { sayn(BX_RES, 24, y, T_RES_MORE, b_nitems - shown); y += 13; }
    if (!b_nitems) { say(BX_RES, 24, y, T_RES_NONE); y += 13; }
    if (outcome == 1 && res.overflow) sayn(BX_RES, 24, y + 4, T_RES_OVER, res.overflow);
  }
  say(BX_RES, 24, 176, T_RES_KEYS);
  tb_flush(BX_RES);
  screen_on();
  do { frame_wait(); } while (!(pad_new & (BTN_A | BTN_START)));
  se_play(SE_OK);
  fade_out();
}
