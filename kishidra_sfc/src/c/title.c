#include "menukit.h"

/* has_save: "つづきから" is offered; returns SC_NEWGAME or SC_MAP */
u8 title_scene(u8 has_save) {
  u8 sel = has_save ? 0 : 1, i;
  screen_off();
  sys_init();
  load_vram(ASSET_TITLE_TILES, VRAM_BG1_TILES);
  load_pal(ASSET_TITLE_PAL, 32);
  load_map(ASSET_TITLE_MAP, 0, 0, 0);
  win_base();
  box(0, 9, 17, 14, 6, 1, PAL_W, BOX_WINDOW);
  say_center(0, 6, has_save ? T_CONTINUE : T_CONTINUE_GRAY);
  say_center(0, 26, T_NEWGAME);
  tb_flush(0);
  box(1, 6, 25, 20, 2, 1 + 14 * 6, PAL_T_WHITE, 0);
  say_center(1, 2, T_COPYRIGHT);
  tb_flush(1);
  tm_main = 0x15;
  screen_on();
  bgm_play(BGM_OPENING);
  for (;;) {
    for (i = 0; i < 2; ++i) box_pal(0, 1, (u8)(i * 2 + 1 + (i ? 1 : 0)), 12, 2, i == sel ? PAL_W_YELLOW : PAL_W);
    frame_wait();
    if ((pad_new & (BTN_UP | BTN_DOWN)) && has_save) { sel ^= 1; se_play(SE_CURSOR); }
    if (pad_new & (BTN_A | BTN_START)) {
      se_play(SE_OK);
      if (sel == 1 && has_save) {
        if (!mk_confirm(T_NEW_ASK, T_NEW_ASK2)) continue;
      }
      break;
    }
  }
  fade_out();
  return sel ? SC_NEWGAME : SC_MAP;
}
