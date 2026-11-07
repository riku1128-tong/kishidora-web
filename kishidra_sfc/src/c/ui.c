#include "ui.h"

/* upload rows r0..r0+nr-1 of a box canvas ($7F:tile*16) in chunks the NMI can take */
void tb_flush_rows(u8 id, u8 r0, u8 nr) {
  u8 *b = &tb[id * 8];
  u16 w = b[2], t0 = (b[4] | ((u16)b[5] << 8)) + r0 * w;
  u16 bytes = nr * w * 16, src = t0 * 16, vaddr = bg3_vram + t0 * 8, c;
  while (bytes) {
    c = bytes > 2048 ? 2048 : bytes;
    queue_vram(vaddr, src, 0x7f, c);
    vaddr += c >> 1; src += c; bytes -= c;
  }
}

void tb_flush(u8 id) { tb_flush_rows(id, 0, tb[id * 8 + 3]); }

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

u8 canvas_busy(u16 t0, u16 n) { return vq_busy(t0 * 16, (t0 + n) * 16); }

void canvas_wait(u16 t0, u16 n) {
  while (canvas_busy(t0, n)) wait_frame();
}

void tb_rect(u8 id, u16 x, u8 y, u16 w, u8 h, u8 colour) {
  ea[0] = x; ea[1] = y; ea[2] = w; ea[3] = h | ((u16)colour << 8);
  tb_rect_now(id);
}

void tb_clear(u8 id) {
  u8 *b = &tb[id * 8];
  canvas_wait(b[4] | ((u16)b[5] << 8), (u16)b[2] * b[3]);
  tb_clear_now(id);
}

void box(u8 id, u8 x, u8 y, u8 w, u8 h, u16 tile, u8 pal, u8 flags) {
  canvas_wait(tile, (u16)w * h);
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
  u16 i;
  for (y = 0; y < th; ++y) {
    i = ((u16)(b[1] + ty + y) << 5) + b[0] + tx;
    for (x = 0; x < tw; ++x, ++i) bg3map[i] = (bg3map[i] & 0xe3ff) | ((u16)pal << 10);
  }
  bgmap_dirty |= 4;
}

void box_hide(u8 id) {
  u8 *b = &tb[id * 8];
  u8 x, y;
  u16 i;
  for (y = 0; y < b[3]; ++y) {
    i = ((u16)(b[1] + y) << 5) + b[0];
    for (x = 0; x < b[2]; ++x) bg3map[i++] = 0;
  }
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

static void bg3_rect(u8 x, u8 y, u8 w, u8 h, u16 at, u8 restore) {
  u8 j;
  u16 k;
  for (j = 0; j < h; ++j) {
    if (at + w > WIN_SAVE) break;
    k = ((u16)(y + j) << 5) + x;
    if (restore) { ea[0] = (u16)&bg3_save[at]; ea[1] = (u16)&bg3map[k]; }
    else { ea[0] = (u16)&bg3map[k]; ea[1] = (u16)&bg3_save[at]; }
    ea[2] = (u16)w * 2;
    mem_copy();
    at += w;
  }
  bgmap_dirty |= 4;
}

void win_open(u8 id, u8 x, u8 y, u8 w, u8 h, u16 tile, u8 pal) {
  u8 i;
  for (i = 0; i < nwins && wins[i][0] != id; ++i) {}
  if (i == nwins) {
    if (nwins == 12) return;
    ++nwins;
    wins_at[i] = save_top;
    bg3_rect(x, y, w, h, save_top, 0);
    save_top += (u16)w * h;
  }
  wins[i][0] = id; wins[i][1] = x; wins[i][2] = y; wins[i][3] = w; wins[i][4] = h;
  bg1_rect(x, y, w, h, 1);
  box(id, x, y, w, h, tile, pal, BOX_WINDOW | BOX_LOW);
}

void win_close(u8 id) {
  u8 i, j;
  for (i = 0; i < nwins && wins[i][0] != id; ++i) {}
  if (i == nwins) { box_hide(id); return; }
  bg3_rect(wins[i][1], wins[i][2], wins[i][3], wins[i][4], wins_at[i], 1);
  bg1_rect(wins[i][1], wins[i][2], wins[i][3], wins[i][4], 0);
  if (i == nwins - 1) save_top = wins_at[i];
  for (j = i; j + 1 < nwins; ++j) { memcpy(wins[j], wins[j + 1], 5); wins_at[j] = wins_at[j + 1]; }
  --nwins;
  for (i = 0; i < nwins; ++i) bg1_rect(wins[i][1], wins[i][2], wins[i][3], wins[i][4], 1);
}
