/* dex (図鑑): monsters, weapons, shields, specials, skills (dex_screens.gd / dex_data.gd)
 * Lists in Safari's dex order (generated in make_data.py build_dex).  L / R change the tab,
 * SELECT the weapon type filter, A opens the card of an entry.  Monster cards show the
 * battle picture (a silhouette while the monster is unknown) and play its battle music. */
#include "menukit.h"
#include "adventure.h"

#define BX_HEAD 0
#define BX_LIST 1
#define BX_INFO 2
#define T_HEAD 1
#define T_LIST 65            /* list window: up to 32 x 24 tiles (768) */
#define T_INFO 449           /* info under a 14-row list (65 + 32 * 12) */

enum { TB_MON, TB_WP, TB_SH, TB_SP, TB_SK, N_TAB };
#define N_FILT_WP 4          /* 全部 / 剣 / 槍 / 槌 */
#define N_FILT_SK 5          /* + 特殊 */

static u8 tab, filt[N_TAB];
static u8 ids[256];          /* entries of the list: dex position (monster, item) or id (special, skill) */
static u16 nids, nknown;
static u16 cur_of[N_TAB], top_of[N_TAB];
static u8 have[N_SKILL];     /* 0 none, 1 in the bag, 2 on the equipped weapons */

/* ---------------------------------------------------------------- data */
static u8 mon_known(u8 m) { return bit_get(sv.found_mons, m); }
static u8 item_known(u8 i) { return bit_get(sv.found_items, i); }
static u8 sp_known(u8 n) { return bit_get(sv.found_sp, n); }

static void skill_have(void) {
  u16 i;
  u8 k, s;
  memset(have, 0, sizeof(have));
  for (i = 0; i < sv.ninv; ++i)
    for (k = 0; k < 3; ++k) if (sv.inv[i].sk[k] != 255) have[sv.inv[i].sk[k]] = 1;
  for (s = 0; s < 3; ++s) {
    if (sv.equip[s] == NO_ITEM) continue;
    for (k = 0; k < 3; ++k) if (sv.inv[sv.equip[s]].sk[k] != 255) have[sv.inv[sv.equip[s]].sk[k]] = 2;
  }
}

static u8 sp_skill(u8 n, u8 k) {
  const Special *p = &specials[n];
  return k == 0 ? p->sk1 : (k == 1 ? p->sk2 : p->sk3);
}

/* 0 a skill is missing, 1 change the gear, 2 the current gear has every skill */
static u8 sp_ready(u8 n) {
  u8 k, s, r = 2;
  for (k = 0; k < 3; ++k) {
    s = sp_skill(n, k);
    if (s != 255 && have[s] < r) r = have[s];
  }
  return r;
}

/* a special passes the type filter if one of its skills has that type */
static u8 sp_pass(u8 n, u8 f) {
  u8 k, s;
  if (!f) return 1;
  for (k = 0; k < 3; ++k) {
    s = sp_skill(n, k);
    if (s != 255 && skills[s].wtype == f - 1) return 1;
  }
  return 0;
}

static u8 sk_group(u8 s) { return skills[s].wtype > 2 ? 3 : skills[s].wtype; }   /* 剣 槍 槌 特殊 */

static void make_list(void) {
  u16 i;
  u8 f = filt[tab], it;
  nids = nknown = 0;
  switch (tab) {
  case TB_MON:
    for (i = 0; i < N_DEX_MON; ++i) { ids[nids++] = (u8)i; nknown += mon_known(dex_mon[i]); }
    break;
  case TB_WP:
    for (i = 0; i < N_DEX_WP; ++i) {
      it = dex_item[i];
      if (f && items[it].type != f - 1) continue;
      ids[nids++] = (u8)i; nknown += item_known(it);
    }
    break;
  case TB_SH:
    for (i = 0; i < N_DEX_SH; ++i) { ids[nids++] = (u8)i; nknown += item_known(dex_item[N_DEX_WP + i]); }
    break;
  case TB_SP:
    for (i = 0; i < N_SPECIAL; ++i) if (sp_pass((u8)i, f)) { ids[nids++] = (u8)i; nknown += sp_known((u8)i); }
    break;
  case TB_SK:
    for (i = 0; i < N_SKILL; ++i) {
      if (f && sk_group((u8)i) != f - 1) continue;
      ids[nids++] = (u8)i; nknown += have[i] > 0;
    }
    break;
  }
}

/* ---------------------------------------------------------------- text helpers */
static u16 px;               /* running x of put() */

static void put(u8 bx, u8 y, u16 msg) {
  say(bx, px, y, msg);
  px += msg_width(msg);
}

static void put_s(u8 bx, u8 y, u16 fmt, u16 s) {
  targ_str = s;
  say(bx, px, y, fmt);
  px += msg_width(s);
}

static void head(void) {
  u8 t;
  box(BX_HEAD, 0, 0, 32, 2, T_HEAD, PAL_W, BOX_OPAQUE);
  say(BX_HEAD, 4, 2, T_DEX_HEAD);
  px = 34;
  for (t = 0; t < N_TAB; ++t) {
    put_s(BX_HEAD, 2, t == tab ? TXT_YELLOW_S : TXT_GRAY_S, T_DEX_TAB_0 + t);
    px += 9;
  }
  tb_flush(BX_HEAD);
}

static u16 filt_name(u8 f) { return f == 0 ? T_DEX_ALL : (f == 4 ? T_DEX_SPECIAL : WTN_0 + f - 1); }

/* ---------------------------------------------------------------- list rows */
static void row_mon(u16 i, u8 y) {
  u8 m = dex_mon[ids[i]];
  sayn(BX_LIST, 14, y, T_DEX_NO, ids[i] + 1);
  if (!mon_known(m)) { say(BX_LIST, 62, y, T_DEX_UNKNOWN_GRAY); return; }
  say(BX_LIST, 62, y, MONN_0 + m);
  if (sv.kills[m]) sayn(BX_LIST, 196, y, T_DEX_COUNT, sv.kills[m]);
}

static void row_item(u16 i, u8 y) {
  u8 pos = ids[i], it = dex_item[tab == TB_SH ? N_DEX_WP + pos : pos];
  sayn(BX_LIST, 14, y, T_DEX_NO, pos + 1);
  if (!item_known(it)) { say(BX_LIST, 62, y, T_DEX_UNKNOWN_GRAY); return; }
  targ_str = RARN_0 + items[it].rarity;
  say(BX_LIST, 62, y, items[it].rarity >= R_SR ? TXT_YELLOW_S : TXT_GRAY_S);
  say(BX_LIST, 88, y, ITEMN_0 + it);
  if (sv.got[it]) sayn(BX_LIST, 208, y, T_DEX_COUNT, sv.got[it]);
}

static void row_sp(u16 i, u8 y) {
  u8 n = ids[i];
  const Special *p = &specials[n];
  if (!sp_known(n)) { say(BX_LIST, 22, y, T_DEX_UNKNOWN_GRAY); return; }
  if (sp_ready(n) == 2) say(BX_LIST, 10, y, T_DEX_STAR);
  say(BX_LIST, 22, y, SPN_0 + n);
  px = 140;
  put(BX_LIST, y, TGTN_0 + p->target);
  targ_num2 = p->mult;
  sayn(BX_LIST, px + 6, y, T_DEX_HITS, p->hits);
}

static void row_sk(u16 i, u8 y) {
  u8 s = ids[i];
  static const u16 col[3] = { TXT_GRAY_S, TXT_S, TXT_YELLOW_S };
  targ_str = SKILLN_0 + s;
  say(BX_LIST, 14, y, col[have[s]]);
  sayn(BX_LIST, 140, y, T_DEX_ST, skills[s].cost);
  targ_str = filt_name(sk_group(s) + 1);
  say(BX_LIST, 190, y, TXT_GRAY_S);
}

static void list_header(void) {
  targ_num2 = nids;
  sayn(BX_LIST, 10, 4, tab == TB_SK ? T_DEX_HAVE : T_DEX_FOUND, nknown);
  if (tab == TB_WP || tab == TB_SP || tab == TB_SK) {
    targ_str = filt_name(filt[tab]);
    say(BX_LIST, 150, 4, T_DEX_FILTER);
  }
  if (tab <= TB_SH) say(BX_LIST, 10, 174, tab == TB_WP ? T_DEX_KEYS_F : T_DEX_KEYS);
}

/* ---------------------------------------------------------------- info box (specials, skills) */
static void seq_line(u8 bx, u8 y, u8 n) {
  u8 k, s;
  static const u16 col[3] = { TXT_GRAY_S, TXT_S, TXT_YELLOW_S };
  px = 14;
  for (k = 0; k < 3; ++k) {
    s = sp_skill(n, k);
    if (s == 255) break;
    if (k) { put(bx, y, TXT_NEXT_ARROW); px += 4; }
    put_s(bx, y, col[have[s]], SKILLN_0 + s);
    px += 4;
  }
}

static void info_sp(u16 i) {
  u8 n = ids[i], r;
  const Special *p = &specials[n];
  win_open(BX_INFO, 0, 16, 32, 12, T_INFO, PAL_W);
  tb_clear(BX_INFO);
  if (!sp_known(n)) {
    say(BX_INFO, 10, 4, T_DEX_UNKNOWN_GRAY);
    say(BX_INFO, 10, 20, T_DEX_SP_UNK1);
    say(BX_INFO, 10, 34, T_DEX_SP_UNK2);
  } else {
    targ_str = SPN_0 + n;
    say(BX_INFO, 10, 4, TXT_YELLOW_S);
    seq_line(BX_INFO, 20, n);
    targ_num2 = p->hits;
    targ_str = TGTN_0 + p->target;
    sayn(BX_INFO, 14, 36, T_DEX_SP_POWER, p->mult);
    r = sp_ready(n);
    say(BX_INFO, 14, 52, T_DEX_SP_READY_0 + r);
    say(BX_INFO, 10, 72, T_DEX_SP_LEGEND);
  }
  tb_flush(BX_INFO);
}

static void info_sk(u16 i) {
  u8 s = ids[i], n, c = 0;
  static const u16 col[3] = { TXT_GRAY_S, TXT_S, TXT_YELLOW_S };
  u8 y = 4;
  u16 w;
  win_open(BX_INFO, 0, 16, 32, 12, T_INFO, PAL_W);
  tb_clear(BX_INFO);
  targ_str = SKILLN_0 + s;
  say(BX_INFO, 10, y, col[have[s]]);
  px = 10 + msg_width(SKILLN_0 + s) + 10;
  sayn(BX_INFO, px, y, T_DEX_ST, skills[s].cost);
  targ_str = filt_name(sk_group(s) + 1);
  say(BX_INFO, px + 40, y, TXT_GRAY_S);
  say(BX_INFO, 14, 18, SKILLD_0 + s);
  if (skills[s].hits > 0) {
    say(BX_INFO, 10, 48, T_DEX_SK_IN);
    y = 62; px = 14;
    for (n = 0; n < N_SPECIAL; ++n) {
      if (sp_skill(n, 0) != s && sp_skill(n, 1) != s && sp_skill(n, 2) != s) continue;
      if (c++) put(BX_INFO, y, T_DEX_DOT);
      w = sp_known(n) ? SPN_0 + n : T_DEX_UNKNOWN_GRAY;
      if (px + msg_width(w) > 240) { y += 14; px = 14; }
      put(BX_INFO, y, w);
    }
    if (!c) say(BX_INFO, 14, y, T_DEX_NONE);
  }
  tb_flush(BX_INFO);
}

/* ---------------------------------------------------------------- monster picture */
#define PIC_OBJ_TILE 64      /* menu OBJ tiles 64-127 (the king's tiles; no king here) */
#define PIC_OBJ_PAL 2
#define PIC_BG_TILE 64       /* BG1 tiles of a boss picture */
#define PIC_FOOT 136         /* screen y of the feet */
#define SIL 0x0c63           /* silhouette colour */

static u8 pdesc[96];
static u8 pmap[20 * 16 * 2];
static const u8 *ppose;
static u8 pic_zako;

static u16 rd16(const u8 *p) { return p[0] | ((u16)p[1] << 8); }

static const u8 *pose0(u8 boss) {
  u8 n = pdesc[0], i;
  u16 o = 1;
  for (i = 0; i < n && i < 8; ++i) {
    if (pdesc[o] == 0) return pdesc + o;
    o += boss ? 11 : 5 + 3 * pdesc[o + 4];
  }
  return pdesc + 1;
}

static void sil_pal(u8 first) {
  u8 k;
  for (k = 1; k < 16; ++k) cgram[first + k] = SIL;
  cgram_dirty = 3;
}

/* display blanked: tiles, map and palette of the monster's normal pose */
static void pic_load(u8 m, u8 known) {
  const u8 *d;
  u16 size, i, w;
  u8 x, y, bw, bh, tx0, ty0;
  s16 bx, by;
  pic_zako = !mon_gfx_get(m, 2);
  copy_asset(mon_gfx_get(m, 0), pdesc, sizeof(pdesc));
  if (pic_zako) {
    ppose = pose0(0);
    size = unpack(rd16(ppose + 1), 0x3000);
    if (size > 2048) size = 2048;
    ea[0] = MK_OBJ_VRAM + PIC_OBJ_TILE * 16; ea[1] = 0x3000; ea[2] = 0x7f; ea[3] = size; vram_now();
    if (known) load_pal(mon_gfx_get(m, 1), 128 + PIC_OBJ_PAL * 16); else sil_pal(128 + PIC_OBJ_PAL * 16);
    return;
  }
  d = pose0(1);
  size = unpack(rd16(d + 1), 0xa000);
  ea[0] = VRAM_BG1_TILES + PIC_BG_TILE * 16; ea[1] = 0xa000; ea[2] = 0x7f; ea[3] = size; vram_now();
  size = unpack(rd16(d + 3), 0xd000);
  ea[0] = 0xd000; ea[1] = 0x7f; ea[2] = (u16)pmap; ea[3] = MIN(size, sizeof(pmap));
  far_copy();
  bw = d[5]; bh = d[6];
  if (known) load_pal(mon_gfx_get(m, 1), 96); else sil_pal(96);
  /* centred, feet on PIC_FOOT */
  bx = 128 - (s16)bw * 4;
  by = PIC_FOOT + 8 - (s16)bh * 8;
  if (by < 16) by = 16;
  tx0 = (u8)(bx >> 3); ty0 = (u8)(by >> 3);
  for (y = 0; y < bh; ++y) {
    if (ty0 + y > 17) break;
    for (x = 0; x < bw; ++x) {
      if (tx0 + x > 31) continue;
      w = rd16(pmap + ((u16)y * bw + x) * 2);
      i = ((u16)(ty0 + y) << 5) + tx0 + x;
      bg1map[i] = w ? w + PIC_BG_TILE : 0;
    }
  }
  bgmap_dirty |= 1;
}

static void pic_sprites(void) {
  u8 n, k, cell;
  const u8 *p;
  if (!pic_zako) return;
  n = ppose[4];
  p = ppose + 5;
  for (k = 0; k < n; ++k, p += 3) {
    cell = p[2];
    spr_add(128 + (s8)p[0], PIC_FOOT + (s8)p[1],
            MK_ATTR(PIC_OBJ_TILE + (cell >> 2) * 64 + (cell & 3) * 4, PIC_OBJ_PAL, 2), 1);
  }
}

static u8 mon_track(u8 m) {
  static const u8 t[5] = { BGM_ZAKO, BGM_BOSS, BGM_MAOU, BGM_MAOU_FINAL, BGM_PEKO };
  return t[mon_bgm[m]];
}

/* ---------------------------------------------------------------- cards */
static void card_head(u16 no, u16 name) {
  box(BX_HEAD, 0, 0, 32, 2, T_HEAD, PAL_W, BOX_OPAQUE);
  sayn(BX_HEAD, 6, 2, T_DEX_NO, no);
  say(BX_HEAD, 54, 2, name);
  say(BX_HEAD, 186, 2, T_DEX_CARD_KEYS);
  tb_flush(BX_HEAD);
}

static void mon_lines(u8 m) {
  const Enemy *r = &enemy_rows[m];
  u8 a, el, y = 4, k;
  u16 mask = mon_areas[m];
  px = 10;
  put(BX_INFO, y, m == MON_117 ? T_RACE_SECRET : RACEN_0 + r->race);
  put(BX_INFO, y, T_DEX_DOT);
  put(BX_INFO, y, ELN_0 + r->element);
  put(BX_INFO, y, T_DEX_ZOKUSEI);
  put(BX_INFO, y, T_DEX_DOT);
  put(BX_INFO, y, SZN_0 + r->size);
  put(BX_INFO, y, T_DEX_SIZE);
  y = 18; px = 10;
  put(BX_INFO, y, T_DEX_WEAK_W);
  put(BX_INFO, y, r->weak < WT_SHIELD ? WTN_0 + r->weak : T_DEX_NONE);
  px += 12;
  put(BX_INFO, y, T_DEX_WEAK_E);
  el = 255;
  for (k = 0; k < N_EL; ++k) if (r->element != EL_NONE && el_strong[k] == r->element) el = k;
  if (el != 255) { put(BX_INFO, y, ELN_0 + el); put(BX_INFO, y, T_DEX_ZOKUSEI); }
  else put(BX_INFO, y, T_DEX_NONE);
  y = 32; px = 10;
  put(BX_INFO, y, T_DEX_PLACE);
  if (!mask) put(BX_INFO, y, m == MON_116 ? T_DEX_PLACE_TUT : T_DEX_Q);
  for (a = 0, k = 0; a < 16; ++a) {
    if (!(mask & (1 << a))) continue;
    if (k++) put(BX_INFO, y, T_DEX_DOT);
    if (px + msg_width(AREAN_0 + a) > 236) { y += 14; px = 26; }
    put(BX_INFO, y, AREAN_0 + a);
  }
  sayn(BX_INFO, 10, y + 16, T_DEX_KILLS, sv.kills[m]);
}

/* monster card; returns the button that closed it (B, LEFT, RIGHT) */
static u16 mon_card(u8 pos) {
  u8 m = dex_mon[pos], known = mon_known(m);
  u16 k;
  mk_screen();
  pic_load(m, known);
  win_base();
  card_head(pos + 1, known ? MONN_0 + m : T_DEX_UNKNOWN);
  win_open(BX_INFO, 0, 18, 32, 10, T_INFO, PAL_W);
  tb_clear(BX_INFO);
  if (known) mon_lines(m); else say(BX_INFO, 10, 4, T_DEX_NOT_YET);
  tb_flush(BX_INFO);
  oam_next = 0; pic_sprites(); oam_finish();
  screen_on();
  bgm_play(known ? mon_track(m) : BGM_OPENING);
  for (;;) {
    oam_next = 0; pic_sprites(); oam_finish();
    frame_wait();
    k = pad_rep & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN);
    if (pad_new & BTN_B) { se_play(SE_CANCEL); k = BTN_B; }
    if (k) break;
  }
  fade_out();
  return k;
}

static u8 item_count(u8 it) {
  u16 i, n = 0;
  for (i = 0; i < sv.ninv; ++i) if (sv.inv[i].id == it) ++n;
  return n > 255 ? 255 : (u8)n;
}

static void evo_name(u8 y, u16 label, u8 it, u8 lost) {
  px = 10;
  put(BX_INFO, y, label);
  if (it != 255) put(BX_INFO, y, item_known(it) ? ITEMN_0 + it : T_DEX_UNKNOWN);
  else put(BX_INFO, y, lost ? T_DEX_UNKNOWN : T_DEX_NONE);
}

static void item_lines(u8 it) {
  const Item *r = &items[it];
  u8 y;
  targ_str = RARN_0 + r->rarity;
  say(BX_INFO, 10, 4, TXT_YELLOW_S);
  px = 10 + msg_width(RARN_0 + r->rarity) + 8;
  put(BX_INFO, 4, WTN_0 + r->type);
  if (r->type == WT_SHIELD) {
    sayn(BX_INFO, 10, 20, T_ITEM_DEF, r->def);
    sayn(BX_INFO, 70, 20, T_DEX_HP, r->atk);
    sayn(BX_INFO, 10, 34, T_DEX_GUARD, MIN((u16)r->def * D_GUARD_PCT_PER_DEF, D_GUARD_PCT_MAX));
  } else {
    sayn(BX_INFO, 10, 20, T_ITEM_ATK, r->atk);
    sayn(BX_INFO, 70, 20, T_ITEM_CRIT, r->crit);
    sayn(BX_INFO, 130, 20, T_DEX_STUN, r->stun);
    if (r->element == EL_NONE) say(BX_INFO, 10, 34, T_DEX_EL_NONE);
    else {
      px = 10;
      put_s(BX_INFO, 34, T_DEX_EL, ELN_0 + r->element);
      px += 30;
      if (el_strong[r->element] != 255) put_s(BX_INFO, 34, T_DEX_EL_X2, ELN_0 + el_strong[r->element]);
    }
  }
  targ_str = FXN_0 + r->effect;
  say(BX_INFO, 10, 50, T_ITEM_FX);
  y = 64;
  if (r->effect != FX_NONE) { say(BX_INFO, 18, y, FXD_0 + r->effect); y += 42; }
  evo_name(y, T_DEX_EVO_FROM, item_evo_from[it], 0); y += 14;
  evo_name(y, T_DEX_EVO_TO, r->evolve, item_evo_lost[it]); y += 18;
  targ_num2 = item_count(it);
  sayn(BX_INFO, 10, y, T_DEX_GOT, sv.got[it]);
}

static u16 item_card(u8 pos) {
  u8 it = dex_item[tab == TB_SH ? N_DEX_WP + pos : pos], known = item_known(it);
  u16 k;
  mk_screen();
  card_head(pos + 1, known ? ITEMN_0 + it : T_DEX_UNKNOWN);
  win_open(BX_INFO, 0, 2, 32, 26, T_LIST, PAL_W);
  tb_clear(BX_INFO);
  if (known) item_lines(it); else say(BX_INFO, 10, 4, T_DEX_NOT_YET);
  tb_flush(BX_INFO);
  screen_on();
  for (;;) {
    frame_wait();
    k = pad_rep & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN);
    if (pad_new & BTN_B) { se_play(SE_CANCEL); k = BTN_B; }
    if (k) break;
  }
  fade_out();
  return k;
}

/* ---------------------------------------------------------------- list screen */
static void moved(u16 i) {
  if (tab == TB_SP) info_sp(i);
  else if (tab == TB_SK) info_sk(i);
}

/* returns 0 = leave the dex */
static u8 list_screen(void) {
  List l;
  u16 k;
  u8 tall = tab == TB_SP || tab == TB_SK ? 0 : 1, pos;
  make_list();
  mk_screen();
  head();
  list_init(&l, BX_LIST, tall ? 10 : 5, nids);
  l.y = 20;
  l.cur = cur_of[tab]; l.top = top_of[tab];
  mk_draw = tab == TB_MON ? row_mon : (tab == TB_SP ? row_sp : (tab == TB_SK ? row_sk : row_item));
  mk_moved = tall ? 0 : moved;
  mk_header = list_header;
  mk_sprites = 0;
  win_open(BX_LIST, 0, 2, 32, tall ? 24 : 14, T_LIST, PAL_W);
  screen_on();
  for (;;) {
    k = list_run(&l, BTN_L | BTN_R | BTN_SELECT);
    cur_of[tab] = l.cur; top_of[tab] = l.top;
    if (k & BTN_B) return 0;
    if (k & (BTN_L | BTN_R)) {
      tab = (k & BTN_L) ? (tab ? tab - 1 : N_TAB - 1) : (tab + 1 < N_TAB ? tab + 1 : 0);
      fade_out();
      return 1;
    }
    if (k & BTN_SELECT) {
      if (tab != TB_WP && tab != TB_SP && tab != TB_SK) continue;
      filt[tab] = (u8)((filt[tab] + 1) % (tab == TB_SK ? N_FILT_SK : N_FILT_WP));
      cur_of[tab] = top_of[tab] = 0;
      fade_out();
      return 1;
    }
    if (!(k & BTN_A) || !nids || tab == TB_SP || tab == TB_SK) continue;
    /* card; ←→ / ↑↓ step through the list */
    for (;;) {
      pos = ids[l.cur];
      k = tab == TB_MON ? mon_card(pos) : item_card(pos);
      if (k & BTN_B) break;
      if (k & (BTN_LEFT | BTN_UP)) l.cur = l.cur ? l.cur - 1 : nids - 1;
      else l.cur = l.cur + 1 < nids ? l.cur + 1 : 0;
      se_play(SE_CURSOR);
    }
    list_fix(&l);
    cur_of[tab] = l.cur; top_of[tab] = l.top;
    if (tab == TB_MON) bgm_play(BGM_OPENING);
    return 1;
  }
}

void dex_scene(void) {
  u8 t;
  tab = 0;
  for (t = 0; t < N_TAB; ++t) { filt[t] = 0; cur_of[t] = top_of[t] = 0; }
  skill_have();
  while (list_screen()) {}
  bgm_play(BGM_OPENING);
  fade_out();
}
