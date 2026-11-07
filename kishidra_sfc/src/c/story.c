/* story scenes: king talks, difficulty choice, name entry, orb / opened notices, ending */
#include "menukit.h"
#include "save.h"
#include "story.h"

#define BX_TALK 10
#define T_TALK 625
#define KING_X 96
#define KING_Y 38           /* feet just above the message window (row 17) */

static u8 king_on;

void name_sync(void) {
  u8 i;
  const u16 *src = sv.name[0] ? (const u16 *)sv.name : default_name;
  for (i = 0; i < 7; ++i) hero_name[i] = src[i];
  hero_name[7] = 0;
}

static void king_sprites(void) {
  u8 c, r;
  if (!king_on) return;
  for (r = 0; r < 3; ++r)
    for (c = 0; c < 2; ++c)
      spr_add(KING_X + c * 32, KING_Y + r * 32, MK_ATTR(MK_T_KING + r * 64 + c * 4, MK_PAL_KING + r, 3), 1);   /* a palette per row */
}

void king_load(void) {
  unpack(ASSET_KING_OBJ, 0x8000);
  queue_vram_big(MK_OBJ_VRAM + MK_T_KING * 16, 0x8000, 0x7f, 192 * 32);
  load_pal(ASSET_KING_PAL, 128 + MK_PAL_KING * 16);   /* OBJ palettes 1-3 */
}

/* pages first..first+n-1; speaker = text id or 0; king = show the king chibi */
void talk(u16 first, u8 n, u16 speaker, u8 king) {
  u8 i, skip = 0;
  void (*old)(void) = mk_sprites;
  name_sync();
  king_on = king;
  if (king) king_load();
  mk_sprites = king_sprites;
  for (i = 0; i < n && !skip; ++i) {
    /* rows 17-25 (the map's key help, 26-27, stays whole): speaker, then up to 3 lines */
    win_open(BX_TALK, 1, 17, 30, 9, T_TALK, PAL_W);
    if (speaker) says(BX_TALK, 10, TOP_Y, TXT_SPEAKER, speaker);
    say(BX_TALK, 14, speaker ? TOP_Y + LINE_C : TOP_Y + 6, first + i);
    tb_flush(BX_TALK);
    for (;;) {
      oam_next = 0;
      king_sprites();
      if ((frame >> 4) & 1) spr_add(232, 196, MK_ATTR(MK_T_DOWN, MK_PAL_MENU, 3), 0);
      oam_finish();
      frame_wait();
      if (pad_new & BTN_A) { se_play(SE_OK); break; }
      if (pad_new & BTN_START) { skip = 1; break; }
    }
  }
  win_close(BX_TALK);
  king_on = 0;
  mk_sprites = old;
  oam_next = 0;
  oam_finish();
}

/* ---------------------------------------------------------------- difficulty */
/* Safari order on screen: lunatic .. easy.  The five names, and the rules of the one under the
 * cursor in a box below them (all five with their rules do not fit in the frame). */
#define BX_DDESC 1
#define DIFF_X 48
#define DIFF_Y 48

static void diff_desc(u8 d) {
  box(BX_DDESC, 1, 18, 30, 5, 833, PAL_W, BOX_OPAQUE);   /* tiles after the window's 832 */
  say(BX_DDESC, 4, 2, DIFFD1_0 + d);
  say(BX_DDESC, 4, 18, DIFFD2_0 + d);
  tb_flush(BX_DDESC);
}

u8 difficulty_select(u8 cur, u8 cancel) {
  u8 sel = 4 - cur, i, shown = 255;
  mk_screen();
  load_vram(ASSET_TITLE_TILES, VRAM_BG1_TILES);
  load_pal(ASSET_TITLE_PAL, 32);
  load_map(ASSET_TITLE_MAP, 0, 0, 0);
  win_base();
  win_open(0, 0, 1, 32, 26, 1, PAL_W);
  say_center(0, TOP_Y, T_DIFF_TITLE);
  say_center(0, TOP_Y + LINE_C, T_DIFF_NOTE);
  for (i = 0; i < 5; ++i) say(0, DIFF_X, DIFF_Y + i * LINE_C, T_DIFF_0 + 4 - i);
  tb_flush(0);
  screen_on();
  for (;;) {
    if (sel != shown) { diff_desc(4 - sel); shown = sel; }
    oam_next = 0;
    mk_cursor(DIFF_X - 11, 8 + DIFF_Y + sel * LINE_C + 2);
    oam_finish();
    frame_wait();
    if (pad_rep & BTN_UP) { sel = sel ? sel - 1 : 4; se_play(SE_CURSOR); }
    if (pad_rep & BTN_DOWN) { sel = sel < 4 ? sel + 1 : 0; se_play(SE_CURSOR); }
    if (pad_new & BTN_A) { se_play(SE_OK); break; }
    if (cancel && (pad_new & BTN_B)) { se_play(SE_CANCEL); fade_out(); return 255; }
  }
  fade_out();
  return 4 - sel;
}

/* ---------------------------------------------------------------- name entry */
#define BX_NTITLE 0
#define BX_NAME 1
#define BX_GRID 2
#define BX_NKEYS 3
#define CELL_W 16
#define CELL_H 13        /* one colour: lines may be this close */
#define GRID_X 16        /* screen pixels of the grid box */
#define GRID_Y 48
#define NAME_X 96        /* screen pixels of the name */
#define NAME_Y 26

static u16 nbuf[NAME_MAX + 1];
static u8 nlen, npage;

static void name_draw(void) {
  u8 i;
  tb_clear(BX_NAME);
  for (i = 0; i < 7; ++i) hero_name[i] = nbuf[i];
  hero_name[7] = 0;
  say(BX_NAME, 0, 2, TXT_NAME);
  tb_flush(BX_NAME);
}

static u16 one[2];

static void grid_draw(void) {
  u8 r, c;
  tb_clear(BX_GRID);
  for (r = 0; r < NAME_GRID_H; ++r)
    for (c = 0; c < NAME_GRID_W; ++c) {
      one[0] = name_grid[npage][r * NAME_GRID_W + c];
      one[1] = 0;
      if (!one[0]) continue;
      ea[0] = 8 + c * CELL_W + (c >= 5 ? 6 : 0); ea[1] = TOP_Y + r * CELL_H; ea[2] = (u16)one;
      tb_codes(BX_GRID);
    }
  for (c = 0; c < 3; ++c) {
    targ_str = T_NAME_KANA + c;
    say(BX_GRID, 8 + c * 64, 128, c == npage ? TXT_YELLOW_S : TXT_GRAY_S);   /* own tile rows, own tiles */
  }
  tb_flush(BX_GRID);
}

/* rows of the current page that have characters (the cursor skips empty ones at the end) */
static u8 name_rows(void) {
  u8 r = NAME_GRID_H, x;
  while (r > 1) {
    for (x = 0; x < NAME_GRID_W; ++x) if (name_grid[npage][(r - 1) * NAME_GRID_W + x]) return r;
    --r;
  }
  return 1;
}

void name_entry(void) {
  u8 cx = 0, cy = 0, i;
  u16 g;
  mk_screen();
  memset(nbuf, 0, sizeof(nbuf));
  for (nlen = 0; nlen < NAME_MAX && sv.name[nlen]; ++nlen) nbuf[nlen] = sv.name[nlen];
  npage = 0;
  /* title window (rows 0-5) with the name in a box of its own inside, the grid (rows 6-24),
   * the key help on a bar at the bottom */
  win_open(BX_NTITLE, 2, 0, 28, 6, 1, PAL_W);
  say_center(BX_NTITLE, TOP_Y, T_NAME_TITLE);
  tb_flush(BX_NTITLE);
  box(BX_NAME, NAME_X / 8, NAME_Y / 8, 12, 2, 170, PAL_W, BOX_OPAQUE);
  win_open(BX_GRID, 2, 6, 28, 19, 200, PAL_W);
  box(BX_NKEYS, 0, 26, 32, 2, 740, PAL_W, BOX_OPAQUE);
  say_center(BX_NKEYS, 2, T_NAME_KEYS);
  tb_flush(BX_NKEYS);
  name_draw();
  grid_draw();
  screen_on();
  for (;;) {
    oam_next = 0;
    mk_cursor(GRID_X + 8 + cx * CELL_W + (cx >= 5 ? 6 : 0) - 9, GRID_Y + TOP_Y + cy * CELL_H + 2);
    if (nlen < NAME_MAX && ((frame >> 4) & 1)) spr_add(NAME_X + msg_width(TXT_NAME), NAME_Y + 2 + 10, MK_ATTR(MK_T_DOT, MK_PAL_MENU, 3), 0);
    oam_finish();
    frame_wait();
    if (pad_rep & BTN_LEFT) { cx = cx ? cx - 1 : NAME_GRID_W - 1; se_play(SE_CURSOR); }
    if (pad_rep & BTN_RIGHT) { cx = cx + 1 < NAME_GRID_W ? cx + 1 : 0; se_play(SE_CURSOR); }
    if (pad_rep & BTN_UP) { cy = cy ? cy - 1 : name_rows() - 1; se_play(SE_CURSOR); }
    if (pad_rep & BTN_DOWN) { cy = cy + 1 < name_rows() ? cy + 1 : 0; se_play(SE_CURSOR); }
    if (pad_new & (BTN_L | BTN_R)) {
      npage = (pad_new & BTN_R) ? (npage + 1) % 3 : (npage + 2) % 3;
      if (cy >= name_rows()) cy = name_rows() - 1;
      se_play(SE_CURSOR);
      grid_draw();
    }
    if (pad_new & BTN_A) {
      g = name_grid[npage][cy * NAME_GRID_W + cx];
      if (g && nlen < NAME_MAX) { nbuf[nlen++] = g; se_play(SE_OK); name_draw(); }
    }
    if ((pad_new & BTN_B) && nlen) { nbuf[--nlen] = 0; se_play(SE_CANCEL); name_draw(); }
    if (pad_new & BTN_START) {
      se_play(SE_OK);
      break;
    }
  }
  for (i = 0; i <= NAME_MAX; ++i) sv.name[i] = i < nlen ? nbuf[i] : 0;
  name_sync();
  fade_out();
}

/* ---------------------------------------------------------------- notices */
void orb_scene(u8 orb) {
  void (*old)(void) = mk_sprites;
  mk_sprites = 0;
  win_open(MK_BOX_DLG, 1, 8, 30, 7, MK_DLG_TILE(30, 7), PAL_W_FRAME);
  targ_str = ORBN_0 + orb;
  say_center(MK_BOX_DLG, TOP_Y, T_ORB_GET);
  say_center(MK_BOX_DLG, TOP_Y + 24, ORBD_0 + orb);           /* ends by 45: inside the bottom frame (48) */
  tb_flush(MK_BOX_DLG);
  se_play(SE_MIRACLE);
  do { oam_next = 0; oam_finish(); frame_wait(); } while (!(pad_new & (BTN_A | BTN_B)));
  se_play(SE_OK);
  win_close(MK_BOX_DLG);
  mk_sprites = old;
}

void opened_notice(u8 area) {
  targ_str = AREAN_0 + area;
  mk_notice(T_OPENED, TXT_YELLOW_S);
}

void show_area_story(u8 area) {
  if (area_story_n(area)) talk(ASTORY_0 + area_story[area][0], area_story[area][1], NAME_KING, 1);
}

u8 area_story_n(u8 area) { return area < N_AREA ? area_story[area][1] : 0; }

/* ---------------------------------------------------------------- opening / ending */
void king_opening(void) {
  talk(KING_ASK_0, N_KING_ASK, NAME_KING, 1);
}

void king_opening2(void) {
  talk(KING_OPEN_0, N_KING_OPEN, NAME_KING, 1);
}

void ending(void) {
  u8 i;
  talk(KING_RET_0, N_KING_RET, NAME_KING, 1);
  mk_screen();
  screen_on();
  talk(EPI_0, N_EPI, 0, 0);
  /* the credits are wider than a framed window: a plain screen */
  box(0, 0, 0, 32, 28, 1, PAL_W, BOX_OPAQUE);
  for (i = 0; i < N_CREDIT; ++i) say(0, 6, 40 + i * 56, CREDIT_0 + i);
  say_center(0, 168, T_THE_END);
  tb_flush(0);
  do { frame_wait(); } while (!(pad_new & (BTN_A | BTN_START)));
  sv.ending_seen = 1;
  save_write();
  fade_out();
}
