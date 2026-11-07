#include "game.h"

u8 title_scene(void) {
  u8 sel = 0, n = 2, i;
  screen_off();
  sys_init();
  load_vram(ASSET_TITLE_TILES, VRAM_BG1_TILES);
  load_pal(ASSET_TITLE_PAL, 32);
  load_map(ASSET_TITLE_MAP, 0, 0, 0);
  box(0, 9, 17, 14, 6, 1, PAL_W, BOX_WINDOW);
  say_center(0, 6, T_CONTINUE);
  say_center(0, 26, T_NEWGAME);
  tb_flush(0);
  box(1, 6, 25, 20, 2, 1 + 14 * 6, PAL_T_WHITE, 0);
  say_center(1, 2, T_COPYRIGHT);
  tb_flush(1);
  tm_main = 0x15;
  screen_on();
  for (;;) {
    frame_wait();
    if (pad_new & (BTN_UP | BTN_DOWN)) {
      sel ^= 1;
    }
    for (i = 0; i < n; ++i) box_pal(0, 1, (u8)(i * 2 + 1 + (i ? 1 : 0)), 12, 2, i == sel ? PAL_W_YELLOW : PAL_W);
    if (pad_new & (BTN_A | BTN_START)) break;
  }
  return sel ? SC_NEWGAME : SC_MAP;
}
