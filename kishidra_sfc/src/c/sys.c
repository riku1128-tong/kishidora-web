#include "sys.h"
#include "sound.h"
#include "ovl.h"
#include <string.h>

u16 pad, pad_new, pad_rep;
u16 dbg[8];
u8 oam_next;
u8 ovl[OVL_SIZE];          /* the overlay arena (ovl.h) */

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
  u8 k, *e = spr_q;
  u16 n = 0, z;
  if (!spr_on) return 0;      /* the NMI took them all */
  for (k = SPR_SLOTS; k; --k, e += 8) if ((z = e[5] | ((u16)e[6] << 8)) != 0) n += z + 80;
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
  set_vram_layout();     /* (also the windows and the colour window off: crt0.s) */
  bgmode = 0x09;         /* mode 1, BG3 priority */
  tm_main = 0x17;
  tm_sub = 0;
  /* every screen starts without colour math, mosaic or the effect HDMA channels 4 / 5 (a battle
   * flash or a menu dim never carries over; mk_screen comes here too) */
  cgwsel = 0; cgadsub = 0;
  coldata[0] = coldata[1] = coldata[2] = 0;
  mosaic = 0;
  hdma_on &= ~0x30;
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

/* rnd(), rnd_mul8(), rnd_below() and the state rnd_state: src/asm/math.s */
void rnd_seed(u16 s) { rnd_state = s ? s : 0x2b7e; }

u16 rnd_range(u16 lo, u16 hi) {
  u16 n = hi - lo + 1;
  if (hi <= lo) return lo;
  if (n < 256) return lo + rnd_mul8(rnd(), (u8)n);   /* the same value, by the multiplier */
  return lo + (u16)(((u32)rnd() * n) >> 16);
}

u8 rnd_pct(u16 p) {
  return p > 0 && rnd_below(100) < p;                /* (= rnd_range(0, 99) < p) */
}

/* (v * p) / 100 as before, by the hardware multiplier and divider (math.s) */
u16 mul_pct16(u16 v, u16 p) {
  pa[0] = v; pa[1] = 0; pp = p;
  pct_now();
  return pa[0];
}

u32 mul_pct(u32 v, u16 p) {
  if (p == 100) return v;
  pa[0] = (u16)v; pa[1] = (u16)(v >> 16); pp = p;
  pct_now();
  return pa[0] | ((u32)pa[1] << 16);
}

#ifdef PROFILE
u16 prof_marks[16];
#endif

#ifdef TEST_HOOKS
#include "ui.h"
#include "story.h"
#include "loadout.h"
#include "battle.h"
/* test command mailbox (XDEFS=TEST_HOOKS builds only; tools/harness/runrom poke / pokew / waitfor).
 * The harness writes the arguments to dbg_arg[], then the command to dbg_cmd; the next frame_wait
 * clears dbg_cmd, runs the command (one that runs frames of its own, DC_NAME / DC_CONFIRM3, returns
 * frames later), writes dbg_res[], sets dbg_ack = the command (DC_UNKNOWN if no one knows it) and
 * ++dbg_seq.  New commands: a case below, numbered in sys.h; a command of one
 * screen checks dbg_scr (the screen sets it with DBG_SCREEN) and calls that screen's handler
 * (a non-static function: a direct call works across banks, a function pointer does not) */
u8 dbg_cmd, dbg_ack, dbg_seq, dbg_scr;
u16 dbg_arg[4], dbg_res[4];
u16 dbg_buf[32];

static void dbg_text(void) {
  u8 f = (u8)dbg_arg[0];
  if (f & 4) { win_close(9); return; }
  targ_num = dbg_arg[1]; targ_num2 = dbg_arg[2];
  targ_num3 = dbg_arg[3] | ((u32)(dbg_arg[0] >> 8) << 16);
  if (!(f & 8)) win_open(9, 1, 9, 30, 8, 784, PAL_W);
  if (f & 1) tb[9 * 8 + 7] |= BOX_SMALL;
  else tb[9 * 8 + 7] &= ~BOX_SMALL;
  if (f & 2) text_lh = 16;
  ea[0] = 8; ea[1] = dbg_buf[31] ? dbg_buf[31] : 8; ea[2] = (u16)dbg_buf;
  tb_codes(9);
  text_lh = 0;
  tb_flush(9);
}

/* S7: the RNG and the multiplier helpers against their models (tools/harness/loadtest.py) */
static void dbg_rnd(u16 n, u8 mode, u16 a, u16 b) {
  static u16 *o;
  u16 v;
  o = (u16 *)(ovl + DBG_IO);
  dbg_res[0] = rnd_state;
  for (; n; --n, ++o) {
    switch (mode) {
    case 0: v = rnd(); break;
    case 1: v = rnd_range(a & 255, b); break;
    case 2: v = rnd_below((u8)a); break;
    case 3: v = rnd_pct(a); break;
    default: v = rnd_range(0, b); break;
    }
    *o = v;
  }
  dbg_res[1] = rnd_state;
}

static void dbg_math(u16 n, u8 mode) {
  static u16 *o;
  o = (u16 *)(ovl + DBG_IO);
  for (; n; --n, o += 4) {
    pa[0] = o[0]; pa[1] = mode == 0 ? 0 : o[1]; pp = o[2];
    if (mode == 0) mul_now();
    else if (mode == 1) div_now();
    else if (mode == 2) pct_now();
    else { pa[0] = rnd_mul8(o[0], (u8)o[2]); pa[1] = 0; }
    o[0] = pa[0]; o[1] = pa[1];
  }
}

static void dbg_poll(void) {
  u8 c = dbg_cmd;
  u16 a0 = dbg_arg[0], a1 = dbg_arg[1], a2 = dbg_arg[2], a3 = dbg_arg[3];
  dbg_res[0] = dbg_res[1] = dbg_res[2] = dbg_res[3] = 0;
  dbg_cmd = 0;                  /* (the commands below that wait for frames see no command again) */
  switch (c) {
  case DC_PING:                 /* is the mailbox there?  ~arg0, the frame, the screen */
    dbg_res[0] = ~dbg_arg[0];
    dbg_res[1] = frame;
    dbg_res[2] = dbg_scr;
    break;
  case DC_SE:                   /* a sound effect alone (tools/harness/t/se_all.txt) */
    if (dbg_arg[1]) bgm_stop();
    se_play((u8)dbg_arg[0]);
    dbg_res[0] = dbg_arg[0];
    break;
  case DC_TEXT:
    dbg_text();
    break;
  case DC_WINS:
    dbg_res[0] = dbg_wins((u8)a0, (u8)a1);
    break;
  case DC_NAME:                 /* (runs its own frames; the result is written when it returns, */
    a0 = name_input(dbg_buf, (u8)a0, T_NAME_TITLE, (u8)a1);   /* and the screen below does not */
    dbg_res[0] = a0;                                          /* see the keys it took) */
    dbg_res[1] = hero_name[0];
    pad_new = pad_rep = 0;
    break;
  case DC_CONFIRM3:
    a0 = dbg_confirm3(T_NEW_ASK, a0 > 1 ? T_NEW_ASK2 : 0, a0 > 2 ? T_DBG_NOUNDO : 0, T_NEWGAME);
    dbg_res[0] = a0;
    pad_new = pad_rep = 0;
    break;
  case DC_NUMSPR:
    dbg_res[0] = dbg_numspr((s16)a0, (s16)a1, (s16)a2, (u8)a3, (u8 *)dbg_buf);
    break;
  case DC_SOLO: case DC_REACH: case DC_REACH_EQ: case DC_FX: case DC_SOLOALL: case DC_POOL:
    dbg_loadout(c, a0, a1);
    break;
  case DC_RND:
    dbg_rnd(a0, (u8)a1, a2, a3);
    break;
  case DC_MATH:
    dbg_math(a0, (u8)a1);
    break;
  case DC_SPECIAL: case DC_CLEAR: case DC_SETHP: case DC_FOE: case DC_CALM: case DC_TALK:
    if (dbg_scr != DS_BATTLE) { c = DC_UNKNOWN; break; }
    if (!dbg_battle(c, a0, a1, a2)) { dbg_cmd = c; return; }   /* not now: again in the next frame */
    break;
  case DC_MONEYMAT:
    dbg_money_mat(a0, a1);
    break;
  default:
    c = DC_UNKNOWN;
  }
  dbg_ack = c;
  ++dbg_seq;
}
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
#ifdef TEST_HOOKS
  if (dbg_cmd) dbg_poll();
#endif
}
