#include "ui.h"

/* upload rows r0..r0+nr-1 of a box canvas ($7F:tile*16) in chunks the NMI can take */
u8 tb_front;                    /* tb_flush: ahead of what is queued */
void tb_flush_rows(u8 id, u8 r0, u8 nr) {
  u8 *b = &tb[id * 8];
  u16 w = b[2], t0 = (b[4] | ((u16)b[5] << 8)) + r0 * w;
  u16 bytes = nr * w * 16, src = t0 * 16, vaddr = bg3_vram + t0 * 8, c;
  while (bytes) {
    c = bytes > 2048 ? 2048 : bytes;
    if (tb_front) queue_vram_front(vaddr, src, 0x7f, c);
    else queue_vram(vaddr, src, 0x7f, c);
    vaddr += c >> 1; src += c; bytes -= c;
  }
}

/* a window just opened (win_open) on a screen that is shown appears once its text is in VRAM,
 * not in pieces: its cells stay blank while the canvas is uploaded */
static u16 tb_fresh;
static u16 hold_cells[32 * 28];
void tb_flush(u8 id) {
  u8 *b = &tb[id * 8], y;
  u16 *h = hold_cells, i;
  if (!(tb_fresh & (1 << id)) || (bright & 0x80) || !bright) {
    tb_fresh &= ~(1 << id);
    tb_flush_rows(id, 0, b[3]);
    return;
  }
  tb_fresh &= ~(1 << id);
  for (y = 0; y < b[3]; ++y, h += b[2]) {
    i = ((u16)(b[1] + y) << 5) + b[0];
    memcpy(h, &bg3map[i], (u16)b[2] * 2);
    memset(&bg3map[i], 0, (u16)b[2] * 2);
  }
  bgmap_dirty |= 4;
  tb_flush_rows(id, 0, b[3]);
  canvas_wait(b[4] | ((u16)b[5] << 8), (u16)b[2] * b[3]);
  for (y = 0, h = hold_cells; y < b[3]; ++y, h += b[2]) memcpy(&bg3map[((u16)(b[1] + y) << 5) + b[0]], h, (u16)b[2] * 2);
  bgmap_dirty |= 4;
}

/* canvases that change in the same NMI as the BG3 map (crt0.s _mq): a box redrawn in another
 * colour (or shown again) never shows the old text in the new colours, or the new in the old.
 * At most MQ_CAP bytes wait at a time (the map's 2KB and these fit in one NMI). */
#define MQ_CAP 2272
#define Q_SIZE(e) ((e)[5] | ((u16)(e)[6] << 8))   /* bytes of a queue entry (0 = free) */

#define MQ_ENTRY 80             /* the NMI's time to set up an entry, in bytes (crt0.s VQ_ENTRY) */
u16 mq_bytes(void) {
  u8 k, *e = mq;
  u16 n = 0, z;
  if (!mq_on) return 0;
  for (k = MQ_SLOTS; k; --k, e += 8) if ((z = Q_SIZE(e)) != 0) n += z + MQ_ENTRY;
  return n;
}

u8 mq_fits(u16 bytes) {           /* (sprite tiles waiting for the same NMI count too) */
  u8 k, *e = mq;
  for (k = MQ_SLOTS; k; --k, e += 8) if (!(e[5] | e[6])) return mq_bytes() + spr_bytes() + bytes + MQ_ENTRY <= MQ_CAP;
  return 0;
}

/* before changing a box whose text goes with the map: room for it (else the map now) */
void mq_room(u16 bytes) {
  while (!mq_fits(bytes)) wait_frame();
}

static u8 mq_add(u16 vaddr, u16 src, u16 size) {
  u8 k, *e = mq, f = 255;
  u16 z;
  for (k = 0; k < MQ_SLOTS; ++k, e += 8) {
    if (!(z = Q_SIZE(e))) { if (f == 255) f = k; continue; }
    if ((e[0] | ((u16)e[1] << 8)) == vaddr && z == size) return 1;   /* sent from WRAM then */
  }
  if (f == 255 || mq_bytes() + size + MQ_ENTRY > MQ_CAP) return 0;
  e = &mq[f * 8];
  e[0] = (u8)vaddr; e[1] = (u8)(vaddr >> 8); e[2] = (u8)src; e[3] = (u8)(src >> 8); e[4] = 0x7f;
  e[5] = (u8)size; e[6] = (u8)(size >> 8);
  mq_on = 1;
  return 1;
}

void tb_flush_rows_sync(u8 id, u8 r0, u8 nr) {
  u8 *b = &tb[id * 8];
  u16 w = b[2], t0 = (b[4] | ((u16)b[5] << 8)) + r0 * w;
  if (!mq_add(bg3_vram + t0 * 8, t0 * 16, nr * w * 16)) tb_flush_rows(id, r0, nr);   /* too big: the queue */
}

void tb_flush_sync(u8 id) { tb_flush_rows_sync(id, 0, tb[id * 8 + 3]); }

/* A box canvas must not change while the NMI still has to upload it (the screen would show a
 * half-drawn canvas for a frame): wait until no queued upload reads tiles t0..t0+n-1. */
/* does a queued upload read $7F:lo..hi-1? */
u8 vq_busy(u16 lo, u16 hi) {
  u16 s;
  u8 i, k, *e;
  for (i = 0, k = vq_head; i < vq_count; ++i) {
    e = &vq[k * 8];
    if (++k == 24) k = 0;
    if (e[4] != 0x7f) continue;
    s = e[2] | ((u16)e[3] << 8);
    if (s < hi && s + (e[5] | ((u16)e[6] << 8)) > lo) return 1;
  }
  return 0;
}

/* the canvas of tiles t0..t0+n-1 is not in VRAM yet (queued, or waiting with the map) */
u8 canvas_busy(u16 t0, u16 n) {
  u8 k, *e = mq;
  u16 lo = t0 * 16, hi = (t0 + n) * 16, s, z;
  if (mq_on) for (k = MQ_SLOTS; k; --k, e += 8) {
    if (!(z = Q_SIZE(e))) continue;
    s = e[2] | ((u16)e[3] << 8);
    if (s < hi && s + z > lo) return 1;
  }
  return vq_busy(lo, hi);
}

void canvas_wait(u16 t0, u16 n) {
  while (canvas_busy(t0, n)) wait_frame();
}

/* before writing a canvas: no queued upload is part way through it (an upload waiting with the
 * map goes in one NMI and reads the canvas then, so it may change meanwhile) */
static void canvas_free(u16 t0, u16 n) {
  while (vq_busy(t0 * 16, (t0 + n) * 16)) wait_frame();
}

void tb_rect(u8 id, u16 x, u8 y, u16 w, u8 h, u8 colour) {
  ea[0] = x; ea[1] = y; ea[2] = w; ea[3] = h | ((u16)colour << 8);
  tb_rect_now(id);
}

void tb_free(u8 id) {               /* before drawing into a box without clearing it */
  u8 *b = &tb[id * 8];
  canvas_free(b[4] | ((u16)b[5] << 8), (u16)b[2] * b[3]);
}

void tb_clear(u8 id) {
  u8 *b = &tb[id * 8];
  canvas_free(b[4] | ((u16)b[5] << 8), (u16)b[2] * b[3]);
  tb_clear_now(id);
}

void box(u8 id, u8 x, u8 y, u8 w, u8 h, u16 tile, u8 pal, u8 flags) {
  canvas_free(tile, (u16)w * h);
  ea[0] = x | ((u16)y << 8);
  ea[1] = w | ((u16)h << 8);
  ea[2] = tile;
  ea[3] = pal | ((u16)flags << 8);
  tb_open(id);
}

void say(u8 id, u16 px, u16 py, u16 msg) {
  ea[0] = px; ea[1] = py; ea[2] = msg;
  tb_text(id);
}

void sayn(u8 id, u16 px, u16 py, u16 msg, u32 n) {
  targ_num = n;
  say(id, px, py, msg);
}

void says(u8 id, u16 px, u16 py, u16 msg, u16 s) {
  targ_str = s;
  say(id, px, py, msg);
}

/* text that breaks into lines (14 px apart) before it would pass x = maxx; a glyph that must
 * not start a line (、。！？）… small kana) takes the glyph before it to the next line */
void say_wrap(u8 id, u16 px, u16 py, u16 msg, u16 maxx) {
  text_wrap = maxx;
  say(id, px, py, msg);
  text_wrap = 0;
}

u16 msg_width(u16 msg) {
  ea[2] = msg;
  return text_width();
}

void say_center(u8 id, u16 py, u16 msg) {
  u16 w = (u16)tb[id * 8 + 2] * 8;
  u16 tw;
  text_small = tb[id * 8 + 7] & BOX_SMALL;
  tw = msg_width(msg);
  text_small = 0;
  say(id, tw < w ? (w - tw) / 2 : 0, py, msg);
}

/* recolour a rectangle of a box's tiles */
void box_pal(u8 id, u8 tx, u8 ty, u8 tw, u8 th, u8 pal) {
  u8 *b = &tb[id * 8];
  u8 x, y;
  u16 pb = (u16)pal << 10, *p, *row = &bg3map[((u16)(b[1] + ty) << 5) + b[0] + tx];
  for (y = th; y; --y, row += 32)
    for (x = tw, p = row; x; --x, ++p) *p = (*p & 0xe3ff) | pb;
  bgmap_dirty |= 4;
}

void box_hide(u8 id) {
  u8 *b = &tb[id * 8];
  u8 y;
  u16 n = (u16)b[2] * 2, *row = &bg3map[((u16)b[1] << 5) + b[0]];
  tb_fresh &= ~(1 << id);
  for (y = b[3]; y; --y, row += 32) memset(row, 0, n);
  bgmap_dirty |= 4;
}

/* ---------------------------------------------------------------- windows
 * Windows use low-priority BG3 tiles so that sprites (cursor) show above them.  Low BG3
 * is below BG1, so the BG1 picture is cleared under every open window and restored from a
 * snapshot when it closes.  The BG3 cells a window covers are saved and put back on close,
 * so a dialog over another window does not erase it. */
#define WIN_SAVE 2048
static u16 bg1_base[1024];
static u16 bg3_save[WIN_SAVE];
static u16 save_top;
static u8 wins[12][5];        /* id, x, y, w, h of the open windows (in opening order) */
static u16 wins_at[12];
static u8 nwins;

void win_base(void) {
  ea[0] = (u16)bg1map; ea[1] = (u16)bg1_base; ea[2] = sizeof(bg1_base);
  mem_copy();
  nwins = 0;
  save_top = 0;
}

/* screen tile rect -> BG1 map rows under it (BG1 may be scrolled vertically).
 * Rows are cleared / copied with DMA and MVN (src/asm/sprite.s). */
static void bg1_rect(u8 x, u8 y, u8 w, u8 h, u8 clear) {
  u8 r, r0, r1, n;
  u16 k, vs = scroll[1];
  if (x >= 32) return;
  n = x + w > 32 ? 32 - x : w;
  r0 = (u8)((((u16)y << 3) + vs) >> 3);
  r1 = (u8)((((u16)(y + h) << 3) - 1 + vs) >> 3);
  for (r = r0; ; ++r) {
    k = ((u16)(r & 31) << 5) + x;
    if (clear) { ea[0] = (u16)&bg1map[k]; ea[1] = 0x7e; ea[2] = (u16)n * 2; wram_zero(); }
    else { ea[0] = (u16)&bg1_base[k]; ea[1] = (u16)&bg1map[k]; ea[2] = (u16)n * 2; mem_copy(); }
    if (r == r1) break;
  }
  bgmap_dirty |= 1;
}

/* rows of window rect win <-> buf (room cells there; rows that do not fit are left out) */
static void win_rows(const u8 *win, u16 *buf, u16 room, u8 to_screen) {
  u8 r, w = win[3];
  u16 k = ((u16)win[2] << 5) + win[1], n = (u16)w * 2;
  for (r = win[4]; r && room >= w; --r, k += 32, buf += w, room -= w) {
    if (to_screen) { ea[0] = (u16)buf; ea[1] = (u16)&bg3map[k]; }
    else { ea[0] = (u16)&bg3map[k]; ea[1] = (u16)buf; }
    ea[2] = n;
    mem_copy();
  }
  bgmap_dirty |= 4;
}

/* the BG3 cells under window j <-> its place in the save stack */
static void win_save(u8 j, u8 to_screen) {
  u16 at = wins_at[j];
  win_rows(wins[j], &bg3_save[at], at < WIN_SAVE ? WIN_SAVE - at : 0, to_screen);
}

void win_open(u8 id, u8 x, u8 y, u8 w, u8 h, u16 tile, u8 pal) {
  u8 i, *p;
  for (i = 0; i < nwins && wins[i][0] != id; ++i) {}
  p = wins[i];
  if (i == nwins) {
    if (nwins == 12) return;
    ++nwins;
    p[1] = x; p[2] = y; p[3] = w; p[4] = h;
    wins_at[i] = save_top;
    win_save(i, 0);
    save_top += (u16)w * h;
  }
  p[0] = id; p[1] = x; p[2] = y; p[3] = w; p[4] = h;
  bg1_rect(x, y, w, h, 1);
  box(id, x, y, w, h, tile, pal, BOX_WINDOW | BOX_LOW);
  tb_fresh |= 1 << id;
}

/* Window i closes under later windows: they come off, top first (their cells kept in hold_cells,
 * their saves back on the screen), window i's save goes back, and they go on again, bottom first,
 * with new saves from wins_at[i] on.  The same as closing them all in order and opening them again,
 * so the stack does not leak (forge spec §0.4: otherwise the saves pass WIN_SAVE after a few rounds
 * and later windows leave text behind) and window i's cells do not come back when a window that
 * covered them closes.  0: the later windows hold more cells than hold_cells (a screen). */
static u16 un_off;
static void win_unone(u8 j, u8 on) {
  u8 *w = wins[j];
  u16 n = (u16)w[3] * w[4];
  if (on) { win_save(j, 0); win_rows(w, &hold_cells[un_off], n, 1); un_off += n; }
  else { un_off -= n; win_rows(w, &hold_cells[un_off], n, 0); win_save(j, 1); }
}

static u8 win_unstack(u8 i) {
  u8 j;
  u16 at;
  for (un_off = 0, j = i + 1; j < nwins; ++j) un_off += (u16)wins[j][3] * wins[j][4];
  if (un_off > 32 * 28) return 0;
  for (j = nwins - 1; j > i; --j) win_unone(j, 0);
  win_save(i, 1);
  for (at = wins_at[i], j = i + 1; j < nwins; ++j) {
    wins_at[j] = at;
    at += (u16)wins[j][3] * wins[j][4];
    win_unone(j, 1);
  }
  save_top = at;
  return 1;
}

/* windows may close in any order; the saves are contiguous in opening order (window i's is
 * wins_at[i] .. wins_at[i + 1]) */
void win_close(u8 id) {
  u8 i, j, *w;
  u16 at, n, end;
  for (i = 0; i < nwins && wins[i][0] != id; ++i) {}
  if (i == nwins) { box_hide(id); return; }
  w = wins[i];
  at = wins_at[i];
  if (i == nwins - 1) {
    win_save(i, 1);
    save_top = at;
  } else if (!win_unstack(i)) {
    /* (more than a screen of windows over it: its cells come back where they covered it, but its
     * save leaves the stack: the saves above it move down) */
    win_save(i, 1);
    n = wins_at[i + 1] - at;
    end = save_top < WIN_SAVE ? save_top : WIN_SAVE;
    if (at + n < end) {
      ea[0] = (u16)&bg3_save[at + n]; ea[1] = (u16)&bg3_save[at]; ea[2] = (end - at - n) * 2;
      mem_copy();               /* MVN upwards: safe for an overlapping move down */
    }
    for (j = i + 1; j < nwins; ++j) wins_at[j] -= n;
    save_top -= n;
  }
  bg1_rect(w[1], w[2], w[3], w[4], 0);
  for (j = i; j + 1 < nwins; ++j) { memcpy(wins[j], wins[j + 1], 5); wins_at[j] = wins_at[j + 1]; }
  --nwins;
  for (i = 0; i < nwins; ++i) bg1_rect(wins[i][1], wins[i][2], wins[i][3], wins[i][4], 1);
}

#ifdef TEST_HOOKS
/* sys.c DC_WINS: k rounds of the forge guide's window order (forge spec §0.4) on the current screen:
 * a card (box 9), a menu (box 10), the card closed first (out of order), a page (box 8) opened and
 * closed, the menu closed.  over 0: the card (0,2 32x6) and the menu (4,8 24x6) do not overlap, as in
 * the guide; over 1: the card at (0,4) overlaps the menu's top 2 rows.  BG3 tiles 576-1023 (free on the
 * forge root).  over 2: a full-screen window (box 7) between the card and the menu: more than a screen
 * of windows over the card, the fallback (its canvas takes tiles 128-1023: only the save stack counts
 * then).  Returns how much the save stack grew: 0 when nothing leaked (without win_unstack it grows
 * by the card's 192 cells a round; from round 11 on the saves pass WIN_SAVE and the menu's cells stay
 * on the screen after it closes; with over 1 the menu's save gave the card's cells back). */
u16 dbg_wins(u8 k, u8 over) {
  const u16 msg = T_NAME_TITLE;          /* (84 px: inside every window here) */
  u16 t0 = save_top;
  for (; k; --k) {
    win_open(9, 0, over == 1 ? 4 : 2, 32, 6, 832, PAL_W);
    say(9, 8, 8, msg);
    tb_flush(9);
    if (over == 2) win_open(7, 0, 0, 32, 28, 128, PAL_W);
    win_open(10, 4, 8, 24, 6, 688, PAL_W);
    say(10, 8, 8, msg);
    tb_flush(10);
    win_close(9);
    win_open(8, 2, 2, 28, 4, 576, PAL_W);
    say(8, 8, 8, msg);
    tb_flush(8);
    win_close(8);
    win_close(10);
    if (over == 2) win_close(7);
  }
  return save_top - t0;
}
#endif
