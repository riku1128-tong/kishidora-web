/* menukit.h: small UI kit for the menu screens.  Everything is static so that each
 * screen file (= ROM bank) gets its own copy and callbacks stay inside the bank. */
#ifndef MENUKIT_H
#define MENUKIT_H
#include "game.h"
#include "sound.h"
#include "save.h"

#define MK_OBJ_VRAM 0x6000
#define MK_PAL_MENU 0          /* OBJ palette numbers (colours 128 + 16 * n) */
#define MK_PAL_KING 1
#define MK_ATTR(t, pal, prio) ((t) | ((u16)(pal) << 9) | ((u16)(prio) << 12))
#define MK_T_CURSOR 0
#define MK_T_DOWN 1
#define MK_T_DOT 2
#define MK_T_STAR 3
#define MK_T_MARK 4
#define MK_T_RING 8
#define MK_T_COIN 11
#define MK_T_KING 64

#define LINE_H 14
/* Text in a framed window stays 8 px inside it (the frame's tiles use the gold palette) and an
 * 8x8 tile has one palette: lines of different colours in the same columns are 16 px apart
 * (LINE_C, starting on a tile row), lines of one colour may be LINE_H apart. */
#define LINE_C 16
#define TOP_Y 8

/* screen in the menu VRAM layout with the menu sprites loaded */
static void mk_screen(void) {
  screen_off();
  sys_init();
  menu_layout();
  load_vram(ASSET_MENU_OBJ, MK_OBJ_VRAM);
  load_pal(ASSET_MENU_OBJ_PAL, 128 + MK_PAL_MENU * 16);
  cgram[0] = 0x1084;
  cgram_dirty = 3;
  bgmode = 0x09;
  tm_main = 0x15;           /* BG1 + BG3 + OBJ */
  oam_clear();
  win_base();
}

static void mk_cursor(s16 x, s16 y) {
  u8 bob = (frame >> 3) & 1;
  spr_add(x + bob, y, MK_ATTR(MK_T_CURSOR, MK_PAL_MENU, 3), 0);
}

/* ---------------------------------------------------------------- lists */
typedef struct {
  u8 box, rows, lh, x, y;      /* x, y: pixel position of row 0 inside the box */
  u16 n, cur, top;
  u8 sx, sy;                    /* screen position of the box (pixels) for the cursor */
} List;

static void (*mk_draw)(u16 i, u8 y);    /* draw row i at y (box pixels) */
static void (*mk_moved)(u16 i);         /* cursor moved (may be 0) */
static void (*mk_sprites)(void);        /* extra sprites every frame (may be 0) */
static void (*mk_header)(void);         /* fixed text of the list box (may be 0) */

static void list_draw(List *l) {
  u8 r;
  tb_clear(l->box);
  if (mk_header) mk_header();
  for (r = 0; r < l->rows && l->top + r < l->n; ++r) mk_draw(l->top + r, l->y + r * l->lh);
  r = tb[l->box * 8 + 2];          /* scroll hints at the right edge */
  if (l->top > 0) say(l->box, r * 8 - 20, l->y, TXT_MORE_UP);
  if (l->top + l->rows < l->n) say(l->box, r * 8 - 20, l->y + (l->rows - 1) * l->lh, TXT_MORE_DOWN);
  tb_flush(l->box);
}

static void list_init(List *l, u8 box, u8 rows, u16 n) {
  u8 *b = &tb[box * 8];
  l->box = box; l->rows = rows; l->lh = LINE_H; l->x = 14; l->y = TOP_Y;
  l->n = n; l->cur = 0; l->top = 0;
  l->sx = b[0] * 8; l->sy = b[1] * 8;
}

static void list_fix(List *l) {
  if (l->n == 0) { l->cur = l->top = 0; return; }
  if (l->cur >= l->n) l->cur = l->n - 1;
  if (l->cur < l->top) l->top = l->cur;
  if (l->cur >= l->top + l->rows) l->top = l->cur - l->rows + 1;
  if (l->top + l->rows > l->n) l->top = l->n > l->rows ? l->n - l->rows : 0;
}

/* run until one of keys (plus A / B) is pressed; returns the button */
static u16 list_run(List *l, u16 keys) {
  u16 top0 = 0xffff, k, cur0 = 0xffff;
#ifdef PROFILE
  u8 pm = 0;
#endif
  keys |= BTN_A | BTN_B;
  PROF_MARK(2);
  l->sx = tb[l->box * 8] * 8;
  l->sy = tb[l->box * 8 + 1] * 8;
  for (;;) {
    list_fix(l);
    if (l->top != top0) { list_draw(l); top0 = l->top; }
    if (l->cur != cur0) { cur0 = l->cur; if (mk_moved) mk_moved(l->cur); }
    oam_next = 0;
    if (l->n) mk_cursor(l->sx + l->x - 11, l->sy + l->y + (l->cur - l->top) * l->lh + 2);
    if (mk_sprites) mk_sprites();
    oam_finish();
    frame_wait();
#ifdef PROFILE
    if (!pm) { pm = 1; PROF_MARK(3); }
#endif
    k = pad_new & keys;
    if (k) {
      se_play((k & BTN_B) ? SE_CANCEL : SE_OK);
      return k;
    }
    if (!l->n) continue;
    if (pad_rep & BTN_UP) { l->cur = l->cur ? l->cur - 1 : l->n - 1; se_play(SE_CURSOR); }
    else if (pad_rep & BTN_DOWN) { l->cur = (l->cur + 1 < l->n) ? l->cur + 1 : 0; se_play(SE_CURSOR); }
    else if ((pad_rep & BTN_LEFT) && !(keys & BTN_LEFT) && l->n > l->rows) {
      l->cur = l->cur > l->rows ? l->cur - l->rows : 0; l->top = l->top > l->rows ? l->top - l->rows : 0; se_play(SE_CURSOR);
    } else if ((pad_rep & BTN_RIGHT) && !(keys & BTN_RIGHT) && l->n > l->rows) {
      l->cur = MIN(l->cur + l->rows, l->n - 1); l->top += l->rows; se_play(SE_CURSOR);
    }
  }
}

/* ---------------------------------------------------------------- dialogs (box 11) */
#define MK_BOX_DLG 11
/* a dialog's tiles are the last w * h of the 1024 BG3 tiles: a screen keeps its boxes below
 * 1024 - (its largest dialog), 904 for one-line dialogs (26 x 4, 20 x 6) */
#define MK_DLG_TILE(w, h) (1024 - (u16)(w) * (h))

/* message window with one or two lines; waits for A / B */
static void mk_notice(u16 l1, u16 l2) {
  u8 h = l2 ? 6 : 4;
  win_open(MK_BOX_DLG, 3, 9, 26, h, MK_DLG_TILE(26, h), PAL_W);
  say_center(MK_BOX_DLG, 10, l1);
  if (l2) say_center(MK_BOX_DLG, 26, l2);
  tb_flush(MK_BOX_DLG);
  do { oam_next = 0; if (mk_sprites) mk_sprites(); oam_finish(); frame_wait(); } while (!(pad_new & (BTN_A | BTN_B)));
  se_play(SE_OK);
  win_close(MK_BOX_DLG);
}

/* yes / no on a question line; returns 1 = yes */
static u8 mk_confirm(u16 q, u16 q2) {
  u8 sel = 0;
  u8 h = q2 ? 8 : 6, y0 = q2 ? 40 : 24;
  win_open(MK_BOX_DLG, 6, 8, 20, h, MK_DLG_TILE(20, h), PAL_W);
  say_center(MK_BOX_DLG, 8, q);
  if (q2) say_center(MK_BOX_DLG, 24, q2);
  say(MK_BOX_DLG, 40, y0, T_YESNO_Y);
  say(MK_BOX_DLG, 104, y0, T_YESNO_N);
  tb_flush(MK_BOX_DLG);
  for (;;) {
    oam_next = 0;
    mk_cursor(48 + 28 + sel * 64, 64 + y0 + 2);
    if (mk_sprites) mk_sprites();
    oam_finish();
    frame_wait();
    if (pad_new & (BTN_LEFT | BTN_RIGHT)) { sel ^= 1; se_play(SE_CURSOR); }
    if (pad_new & BTN_A) { se_play(SE_OK); break; }
    if (pad_new & BTN_B) { se_play(SE_CANCEL); sel = 1; break; }
  }
  win_close(MK_BOX_DLG);
  return sel == 0;
}

/* item name with "+N" (enhanced slots).  The yellow "+N" starts on its own tile; in lists with
 * rows 14 px apart (two rows share a tile row) it goes to a column of its own (fixed = 1),
 * past the longest name (108 px), so it never shares a tile with a name. */
static void mk_item_name(u8 bx, u16 x, u8 y, const Inv *it, u8 fixed) {
  u8 n = enh_count(it);
  u16 px;
  say(bx, x, y, ITEMN_0 + it->id);
  if (!n) return;
  px = (x + msg_width(ITEMN_0 + it->id) + 7) & ~7;
  if (fixed) px = MAX(px, (x + 112 + 7) & ~7);
  sayn(bx, px, y, TXT_PLUS, n);
}

/* item card: 4 lines (5 with a two-line effect text) in a window of MK_CARD_H tiles:
 *   rarity type element stars    ATK +n   |  会心 n%  効果 name  |  skills  |  effect text */
#define MK_CARD_H 11
static void mk_item_detail(u8 bx, const Inv *it) {
  const Item *r;
  u16 x;
  u8 k, n, y;
  if (!it) { say(bx, 10, TOP_Y, T_SLOT_EMPTY); return; }
  r = &items[it->id];
  targ_str = RARN_0 + r->rarity;
  say(bx, 10, TOP_Y, TXT_YELLOW_S);
  x = 10 + msg_width(RARN_0 + r->rarity) + 8;
  say(bx, x, TOP_Y, WTN_0 + r->type);
  x += msg_width(WTN_0 + r->type) + 6;
  if (r->type != WT_SHIELD && r->element != EL_NONE) { say(bx, x, TOP_Y, ELN_0 + r->element); x += msg_width(ELN_0 + r->element); }
  x = (x + 15) & ~7;                       /* stars (8-dot font) one tile each: up to 9 end by 152 */
  n = inv_slots_of(it);
  text_small = 8;
  for (k = 0; k < n; ++k) say(bx, x + k * 8, TOP_Y + 2, T_STAR_0 + enh_get(it, k));
  text_small = 0;
  if (r->type == WT_SHIELD) { sayn(bx, 150, TOP_Y, T_ITEM_HP, r->atk); sayn(bx, 200, TOP_Y, T_ITEM_DEF, r->def); }
  else { sayn(bx, 160, TOP_Y, T_ITEM_ATK, r->atk); if (enh_sum(it)) sayn(bx, 208, TOP_Y, TXT_PLUS, enh_sum(it)); }
  y = TOP_Y + LINE_C;
  x = 10;
  if (r->type != WT_SHIELD) { sayn(bx, 10, y, T_ITEM_CRIT, r->crit); x = 80; }
  if (r->effect != FX_NONE) { targ_str = FXN_0 + r->effect; say(bx, x, y, T_ITEM_FX); }
  if (x == 80 || r->effect != FX_NONE) y += LINE_H;
  if (r->type != WT_SHIELD) {
    x = 10;
    for (k = 0; k < 3; ++k) {
      if (it->sk[k] == 255) continue;
      say(bx, x, y, SKILLN_0 + it->sk[k]);
      x += msg_width(SKILLN_0 + it->sk[k]) + 8;
    }
    y += LINE_H;
  }
  if (r->effect != FX_NONE) say(bx, 10, y, FXD_0 + r->effect);
}

static void mk_wait_release(void) {
  do { frame_wait(); } while (pad & (BTN_A | BTN_B | BTN_X | BTN_Y | BTN_START));
}
#endif
