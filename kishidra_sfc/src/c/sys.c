#include "sys.h"
#include "sound.h"

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

void queue_vram(u16 vaddr, u16 src, u8 bank, u16 size) {
  ea[0] = vaddr; ea[1] = src; ea[2] = bank; ea[3] = size; ea[4] = 0;
  while (!vram_queue()) wait_frame();
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
  u16 i;
  for (i = 0; i < 1024; ++i) { bg1map[i] = 0; bg2map[i] = 0; bg3map[i] = 0; }
  bgmap_dirty = 7;
}

/* BG3 text palettes: 0-3 transparent text (white, yellow, red, gray), 4-7 opaque windows */
static const u16 bg3pal[32] = {
  0x0000, 0x7fff, 0x0842, 0x7fff,     /* 0 white */
  0x0000, 0x23ff, 0x0842, 0x23ff,     /* 1 yellow */
  0x0000, 0x295f, 0x0842, 0x295f,     /* 2 red */
  0x0000, 0x0421, 0x0866, 0x5294,     /* 3 window gray (shadow, fill, gray) */
  0x0000, 0x0421, 0x0866, 0x7fff,     /* 4 window: shadow, fill, white */
  0x0000, 0x0421, 0x0866, 0x23ff,     /* 5 window yellow */
  0x0000, 0x0421, 0x0866, 0x229b,     /* 6 window frame gold */
  0x0000, 0x0421, 0x0866, 0x295f,     /* 7 window red */
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
  clear_maps();
  set_bg3_palettes();
  oam_clear();
  oam_hide = 0;
}

void screen_off(void) {
  force_blank(1);
}

void screen_on(void) {
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

void oam_clear(void) {
  u16 i;
  for (i = 0; i < 512; i += 4) { oam[i] = 0; oam[i + 1] = 240; oam[i + 2] = 0; oam[i + 3] = 0; }
  for (i = 512; i < 544; ++i) oam[i] = 0x55;   /* x9 = 1 (off screen), small */
  oam_next = 0;
}

/* sprite i at (x,y); tile_attr = tile (9 bits) | attr<<8 style: bits 9-11 pal, 12-13 prio, 14 hflip, 15 vflip */
void spr(u8 i, s16 x, s16 y, u16 tile_attr, u8 large) {
  u16 o = (u16)i << 2;
  u8 hi = i >> 2, sh = (i & 3) << 1, bits;
  if (y < -64 || y >= 224 || x <= -64 || x >= 256) { oam[o + 1] = 240; return; }
  oam[o] = (u8)x;
  oam[o + 1] = (u8)y;
  oam[o + 2] = (u8)tile_attr;
  oam[o + 3] = (u8)(tile_attr >> 8);
  bits = ((x < 0) ? 1 : 0) | (large ? 2 : 0);
  oam[512 + hi] = (oam[512 + hi] & ~(3 << sh)) | (bits << sh);
}

void spr_add(s16 x, s16 y, u16 tile_attr, u8 large) {
  if (oam_next < 128) spr(oam_next++, x, y, tile_attr, large);
}

void oam_finish(void) {
  while (oam_next < 128) { oam[(u16)oam_next << 2 | 1] = 240; ++oam_next; }
  oam_next = 0;
}

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

void frame_wait(void) {
  sound_service();
  wait_frame();
  pad = joy;
  pad_new = pad_lat; pad_lat = 0;
  pad_rep = rep_lat; rep_lat = 0;
  rnd();
}
