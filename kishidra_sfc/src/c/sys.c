#include "sys.h"
#include "sound.h"
#include <string.h>

u16 pad, pad_new, pad_rep;
u16 dbg[8];
u8 oam_next;
static u16 seed = 0x2b7e;

void load_vram(u16 id, u16 vaddr) {
  ea[0] = id; ea[1] = vaddr;
  asset_vram();
}

void load_pal(u16 id, u8 first) {
  ea[0] = id; ea[1] = first;
  asset_pal();
}

void load_map(u16 id, u8 bg, u16 add, u8 row) {
  ea[0] = id; ea[1] = bg; ea[2] = add; ea[3] = row;
  asset_map();
}

u16 unpack(u16 id, u16 dst7f) {
  ea[0] = id; ea[1] = dst7f;
  return asset_unpack();
}

u16 copy_asset(u16 id, void *dst, u16 max) {
  ea[0] = id; ea[1] = (u16)dst; ea[2] = max;
  asset_copy();
  return ea[2];
}

/* tiles that change with the OAM of this frame (crt0.s _spr_q): call right before the frame's
 * wait, after the OAM that shows them is built; the caller keeps them within the NMI's budget */
void spr_put(u8 k, u16 vaddr, u16 src, u8 bank, u16 size) {
  u8 *e = &spr_q[k * 8];
  e[0] = (u8)vaddr; e[1] = (u8)(vaddr >> 8); e[2] = (u8)src; e[3] = (u8)(src >> 8); e[4] = bank;
  e[5] = (u8)size; e[6] = (u8)(size >> 8);
  spr_on = 1;
}

u16 spr_bytes(void) {         /* (with the NMI's set-up time of each, crt0.s VQ_ENTRY) */
  u8 k;
  u16 n = 0, z;
  if (!spr_on) return 0;      /* the NMI took them all */
  for (k = 0; k < SPR_SLOTS; ++k) if ((z = spr_q[k * 8 + 5] | ((u16)spr_q[k * 8 + 6] << 8)) != 0) n += z + 80;
  return n;
}

void queue_vram(u16 vaddr, u16 src, u8 bank, u16 size) {
  ea[0] = vaddr; ea[1] = src; ea[2] = bank; ea[3] = size; ea[4] = 0;
  while (!vram_queue()) wait_frame();
}

/* one entry ahead of everything queued (it must arrive in one NMI: at most 4KB) */
void queue_vram_front(u16 vaddr, u16 src, u8 bank, u16 size) {
  ea[0] = vaddr; ea[1] = src; ea[2] = bank; ea[3] = size; ea[4] = 0;
  while (!vram_queue_front()) wait_frame();
}

/* large uploads while the display is on: 2KB per queue entry */
void queue_vram_big(u16 vaddr, u16 src, u8 bank, u16 size) {
  u16 c;
  while (size) {
    c = size > 2048 ? 2048 : size;
    queue_vram(vaddr, src, bank, c);
    vaddr += c >> 1; src += c; size -= c;
  }
}

void clear_maps(void) {
  ea[0] = (u16)bg1map; ea[1] = 0x7e; ea[2] = sizeof(bg1map) * 3;   /* bg1map, bg2map, bg3map in a row */
  wram_zero();
  bgmap_dirty = 7;
}

/* BG3 text palettes, the same in every box: colour 1 = text shadow, 2 = window fill, 3 = text.
 * (text.s draws text as colour 3 and its shadow as 1, so one palette serves transparent boxes
 * and windows; an 8x8 tile has one palette, so colours change on tile boundaries) */
static const u16 bg3pal[32] = {
  0x0000, 0x0421, 0x0866, 0x7fff,     /* 0 white */
  0x0000, 0x0421, 0x0866, 0x23ff,     /* 1 yellow */
  0x0000, 0x0421, 0x0866, 0x295f,     /* 2 red */
  0x0000, 0x0421, 0x0866, 0x5294,     /* 3 gray */
  0x0000, 0x0421, 0x0866, 0x2f4a,     /* 4 green */
  0x0000, 0x0421, 0x0866, 0x7eed,     /* 5 blue */
  0x0000, 0x0421, 0x0866, 0x229b,     /* 6 gold (window frames) */
  0x0000, 0x0421, 0x0866, 0x6d36,     /* 7 purple (dark element) */
};

void set_bg3_palettes(void) {
  u8 i;
  for (i = 1; i < 32; ++i) cgram[i] = bg3pal[i];
  cgram_dirty = 3;
}

void sys_init(void) {
  vram_clear();
  set_vram_layout();
  bgmode = 0x09;         /* mode 1, BG3 priority */
  tm_main = 0x17;
  tm_sub = 0;
  cgwsel = 0; cgadsub = 0;
  memset(scroll, 0, sizeof(scroll));
  clear_maps();
  set_bg3_palettes();
  oam_clear();
  oam_hide = 0;
  memset(spr_q, 0, sizeof(spr_q)); spr_on = 0;
  memset(mq, 0, sizeof(mq)); mq_on = 0;
}

void screen_off(void) {
  force_blank(1);
}

/* the first frame shows a finished screen: everything queued is in VRAM first */
void screen_on(void) {
  while (vq_count || spr_on || mq_on) wait_frame();
  bright = 15;
  wait_frame();
  force_blank(0);
}

void fade_out(void) {
  s8 b;
  for (b = 14; b >= 0; b -= 2) { bright = b; frame_wait(); }
  bright = 0x80;
  frame_wait();
}

void fade_in(void) {
  u8 b;
  for (b = 1; b <= 15; b += 2) { bright = b; frame_wait(); }
  bright = 15;
  frame_wait();
}

/* oam_clear: src/asm/sprite.s */

/* spr_add and oam_finish: src/asm/sprite.s */

void rnd_seed(u16 s) { seed = s ? s : 0x2b7e; }

u16 rnd(void) {
  seed ^= seed << 7;
  seed ^= seed >> 9;
  seed ^= seed << 8;
  return seed;
}

u16 rnd_range(u16 lo, u16 hi) {
  u16 n = hi - lo + 1;
  if (hi <= lo) return lo;
  return lo + (u16)(((u32)rnd() * n) >> 16);
}

u8 rnd_pct(u16 p) {
  return p > 0 && rnd_range(0, 99) < p;
}

u16 mul_pct16(u16 v, u16 p) {
  return (u16)(((u32)v * p) / 100);
}

u32 mul_pct(u32 v, u16 p) {
  if (p == 100) return v;
  return (v * p) / 100;
}

#ifdef PROFILE
u16 prof_marks[16];
#endif

void frame_wait(void) {
  PROF_LINE(4);
  sound_service();
  PROF_LINE(5);
  wait_frame();
  PROF_LINE(0);
  pad = joy;
  pad_new = pad_lat; pad_lat = 0;
#ifdef PROFILE
  if (pad_new & (BTN_A | BTN_B | BTN_Y | BTN_X | BTN_START | BTN_L | BTN_R | BTN_SELECT)) PROF_MARK(7);
#endif
  pad_rep = rep_lat; rep_lat = 0;
  rnd();
}
