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

void box(u8 id, u8 x, u8 y, u8 w, u8 h, u16 tile, u8 pal, u8 flags) {
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

u16 msg_width(u16 msg) {
  ea[2] = msg;
  return text_width();
}

void say_center(u8 id, u16 py, u16 msg) {
  u16 w = (u16)tb[id * 8 + 2] * 8;
  u16 tw = msg_width(msg);
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
