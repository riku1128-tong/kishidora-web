/* shared loadout rules: sp_reach, solo_ok_sk + the INV_SOLO cache, fx_state (loadout.h) */
#include "loadout.h"
#include "text.h"
#include <string.h>

static const u8 rs_bit[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

/* ---------------------------------------------------------------- reachable specials
 * (adventure.gd reachable_specials) Each weapon cycles its skills in order; an attacking skill goes into
 * the last-3 history (buff skills do not); the longest special that ends the history fires (ties: the
 * first in the table) and clears it, else the history keeps its last 2.  Only specials whose skills are
 * all on the weapons can ever fire (the candidates), so the history is kept as its longest suffix that
 * is a proper prefix of a candidate (a "prefix", 0-2 skills, at most 28 of them).  Exact search over the
 * states prefix * 32 + p0 + 3 p1 + 9 p2: bit sets of seen / done states, swept byte by byte until no new
 * state appears (or every candidate fired).  tools/data/loadout_model.py snes_reach is this algorithm in
 * Python; it equals the web BFS (tools/harness/loadtest.py). */
#define RS_PRE 28                                  /* prefixes of the candidates + the empty one */
#define RS_BITS (RS_PRE * 4)                       /* 32 states per prefix */
#if 2 * RS_BITS + RS_PRE * 9 > SP_SCRATCH
#error SP_SCRATCH
#endif
static u8 rs_w[9], rs_n[3], rs_nw;                 /* skill of weapon i position q at [i * 3 + q] */
static u8 rs_c1[N_SPECIAL], rs_c2[N_SPECIAL], rs_c3[N_SPECIAL], rs_cid[N_SPECIAL], rs_nc;
static u8 rs_pa[RS_PRE], rs_pb[RS_PRE], rs_pl[RS_PRE], rs_np;
static u8 rs_have[(N_SKILL + 7) / 8], rs_atk[9];
/* pos (p0 + 3 p1 + 9 p2) -> p0 | p1 << 2 | p2 << 4 */
static const u8 rs_pq[27] = { 0, 1, 2, 4, 5, 6, 8, 9, 10, 16, 17, 18, 20, 21, 22, 24, 25, 26, 32, 33, 34, 36, 37, 38, 40, 41, 42 };

/* (u16 parameters: cc65 used a stale X as the high byte of an index made from a u8 parameter) */
static u8 rs_find(u16 a, u16 b, u16 len) {
  u8 k;
  for (k = 1; k < rs_np; ++k) if (rs_pb[k] == b && rs_pl[k] == len && (len == 1 || rs_pa[k] == a)) return k;
  return 0;
}

static void rs_add(u16 a, u16 b, u16 len) {
  if (rs_find(a, b, len) || rs_np == RS_PRE) return;
  rs_pa[rs_np] = a; rs_pb[rs_np] = b; rs_pl[rs_np] = len; ++rs_np;
}

/* attacking skill x after prefix p: 0x80 | special when one fires (the history clears), else the next prefix */
static u8 rs_step(u16 p, u16 x) {
  u8 c, best = 255, l = rs_pl[p], a = rs_pa[p], b = rs_pb[p];
  for (c = 0; c < rs_nc; ++c) {
    if (rs_c3[c] == SK_NONE) { if (best == 255 && l && rs_c2[c] == x && rs_c1[c] == b) best = c; }
    else if (l == 2 && rs_c3[c] == x && rs_c2[c] == b && rs_c1[c] == a) { best = c; break; }   /* 3 beats 2 */
  }
  if (best != 255) return 0x80 | rs_cid[best];
  if (l && (c = rs_find(b, x, 2)) != 0) return c;
  return rs_find(0, x, 1);
}

u8 sp_reach(const Inv *const *w, u8 nw, u8 *out, u8 *scratch) {
  static u8 *vis, *done, *tr, *t, *wp;
  static const Special *sp;
  static const Inv *it;
  u8 s, k, n, i, x, e, bits, b, pid, pos, pq, found = 0, more;
  u16 j;
  vis = scratch; done = scratch + RS_BITS; tr = scratch + 2 * RS_BITS;
  out[0] = out[1] = 0;
  memset(rs_have, 0, sizeof(rs_have));
  rs_nw = 0;
  if (nw > 3) nw = 3;
  for (s = 0; s < nw; ++s) {
    it = w[s];
    if (!it) continue;
    wp = rs_w + rs_nw * 3;
    for (k = 0, n = 0; k < 3; ++k) {
      x = it->sk[k];
      if (x == 255) continue;
      wp[n++] = x;
      e = x >> 3; b = x & 7;               /* (u8 temporaries for the index, see rs_find) */
      rs_have[e] |= rs_bit[b];
    }
    if (n) rs_n[rs_nw++] = n;
  }
  rs_nc = 0; rs_np = 1; rs_pl[0] = 0;
  for (i = 0, sp = specials; i < N_SPECIAL; ++i, ++sp) {
    for (k = 0; k < 3; ++k) {
      x = k == 0 ? sp->sk1 : (k == 1 ? sp->sk2 : sp->sk3);
      if (x == SK_NONE) continue;
      e = x >> 3; n = x & 7;
      if (!(rs_have[e] & rs_bit[n])) break;
    }
    if (k < 3) continue;
    rs_c1[rs_nc] = sp->sk1; rs_c2[rs_nc] = sp->sk2; rs_c3[rs_nc] = sp->sk3; rs_cid[rs_nc++] = i;
    rs_add(0, sp->sk1, 1);
    if (sp->sk3 != SK_NONE) rs_add(sp->sk1, sp->sk2, 2);
  }
  if (!rs_nc) return 0;
  /* transitions of every (prefix, weapon position) */
  for (i = 0; i < 9; ++i) rs_atk[i] = 0;           /* attacking skill at each weapon position */
  for (i = 0, wp = rs_w; i < rs_nw; ++i, wp += 3)
    for (k = 0; k < rs_n[i]; ++k) rs_atk[i * 3 + k] = skills[wp[k]].hits != 0;
  for (pid = 0, t = tr; pid < rs_np; ++pid, t += 9)
    for (i = 0, wp = rs_w; i < 9; ++i, ++wp) t[i] = rs_atk[i] ? rs_step(pid, *wp) : pid;
  memset(vis, 0, 2 * RS_BITS);
  vis[0] = 1;                                      /* prefix 0, positions 0 */
  do {
    more = 0;
    for (b = 0; b < rs_np * 4; ++b) {
      bits = vis[b] & ~done[b];
      if (!bits) continue;
      done[b] |= bits;
      more = 1;
      pid = b >> 2;
      t = tr + pid * 9;
      for (k = 0; k < 8; ++k) {
        if (!(bits & rs_bit[k])) continue;
        pos = ((b & 3) << 3) | k;
        pq = rs_pq[pos];
        for (i = 0; i < rs_nw; ++i, pq >>= 2) {
          n = pq & 3;
          e = t[i * 3 + n];
          if (e & 0x80) {
            e &= 0x7f;
            x = e >> 3; s = e & 7;
            if (!(out[x] & rs_bit[s])) { out[x] |= rs_bit[s]; ++found; }
            e = 0;
          }
          x = i == 0 ? 1 : (i == 1 ? 3 : 9);
          j = (n + 1 < rs_n[i] ? pos + x : pos - n * x) + ((u16)e << 5);
          x = (u8)j & 7;
          vis[j >> 3] |= rs_bit[x];
        }
      }
    }
  } while (more && found < rs_nc);
  return found;
}

u8 sp_reach_equipped(u8 *out, u8 *scratch) {
  static const Inv *w[4];
  lo_equipped(w);
  return sp_reach(w, 3, out, scratch);
}

/* ---------------------------------------------------------------- one weapon (loadout.s solo_eval) */
u8 solo_ok_sk(const u8 *sk, u8 *kick) {
  u8 f = solo_eval(sk);
  *kick = (f & SOLO_KICK) != 0;
  return f & SOLO_FIRES;
}

void inv_solo_update(u16 i) { solo_bag(&sv.inv[i], 1); }

/* the whole bag (a save without ST_SOLO: save_load) */
void inv_solo_all(void) { solo_bag(sv.inv, sv.ninv); }

/* ---------------------------------------------------------------- effect states (desc.gd fx_state) */
u16 fx_note;
u8 fx_slots;

void lo_equipped(const Inv **lo) {
  u8 s;
  u16 i;
  for (s = 0; s < 4; ++s) {
    i = sv.equip[s];
    lo[s] = i == NO_ITEM ? 0 : &sv.inv[i];
  }
}

u8 fx_state(const Inv *const *lo, u8 k) {
  const Inv *it = lo[k];
  if (!it) { fx_note = 0; fx_slots = 0; return FS_NONE; }
  return fx_state_of(lo, k, items[it->id].effect);
}

/* the generated rule fx_rule[fx] (make_data build_fx_state): class << 4 | parameter */
u8 fx_state_of(const Inv *const *lo, u8 k, u8 fx) {
  static const Inv *it;
  static const Item *ri;
  u8 r, par, s, ok, j, x;
  fx_note = 0; fx_slots = 0;
  if (fx == FX_NONE) return FS_NONE;
  if (k == EQ_SUB || k == EQ_THIRD) { fx_note = T_ST_FX_MAINONLY; return FS_OFF; }
  r = fx_rule[fx];
  par = r & 15;
  r >>= 4;
  switch (r) {
  case FR_RESO: case FR_TYPE: case FR_BOOST:      /* the equipped weapons of element / type par */
    for (s = 0; s < 3; ++s) {
      it = lo[s];
      if (!it) continue;
      ri = &items[it->id];
      ok = r == FR_TYPE ? ri->type == par : ri->element == par;
      if (ok) fx_slots |= rs_bit[s];
    }
    if (!fx_slots) { fx_note = FXO_0 + fx; return FS_OFF; }
    if (r == FR_BOOST) { fx_note = FXS_0 + fx; return FS_COND; }
    fx_note = T_ST_FX_TARGET;
    return FS_ON;
  case FR_ALLSR:                                   /* the shield and all three weapons SR or SR+ */
    ok = lo[EQ_SHIELD] && items[lo[EQ_SHIELD]->id].rarity >= R_SR;
    for (s = 0; s < 3; ++s) {
      it = lo[s];
      if (!it || items[it->id].rarity < R_SR) ok = 0;
    }
    if (!ok) { fx_note = FXO_0 + fx; return FS_OFF; }
    return FS_ON;
  case FR_BERSERK:                                 /* an equipped weapon has a skill that charges ベルセルク */
    for (s = 0; s < 3; ++s) {
      it = lo[s];
      if (!it) continue;
      for (j = 0; j < 3; ++j) {
        x = it->sk[j];
        if (x != 255 && skills[x].buff == BF_CRIT) { fx_note = FXS_0 + fx; return FS_COND; }
      }
    }
    fx_note = FXO_0 + fx;
    return FS_OFF;
  case FR_COND:
    fx_note = FXS_0 + fx;
    return FS_COND;
  case FR_NONE:
    return FS_NONE;
  }
  return FS_ON;
}

#ifdef TEST_HOOKS
/* ---------------------------------------------------------------- test commands (sys.h DC_SOLO ..) */
#include "ovl.h"
static Inv dl_inv[4];

static void dl_set(u8 s, const u8 *r) {         /* r = [id, sk0, sk1, sk2] */
  Inv *it = &dl_inv[s];
  it->id = r[0]; it->sk[0] = r[1]; it->sk[1] = r[2]; it->sk[2] = r[3];
  it->slots = 0; it->enh[0] = it->enh[1] = it->enh[2] = 0;
}

void dbg_loadout(u8 c, u16 n, u16 a1) {
  static u8 *p;
  static const Inv *w[4];
  u8 s, f, k, out[2];
  u16 t, worst = 0;
  p = ovl + DBG_IO;
  switch (c) {
  case DC_SOLO:
    for (; n; --n, p += 3) {
      f = solo_ok_sk(p, &k) | (k << 1);
      if (a1) {                                    /* inv_add of item a1 - 1 with these skills */
        t = inv_add((u8)(a1 - 1), p, 3, 0, 1);
        if (t == NO_ITEM) f |= 0x40;
        else {
          if (inv_solo(&sv.inv[t])) f |= 0x80;
          inv_remove(t);
        }
      }
      dl_inv[0].sk[0] = p[0]; dl_inv[0].sk[1] = p[1]; dl_inv[0].sk[2] = p[2];
      w[0] = &dl_inv[0];
      s = sp_reach(w, 1, out, ovl);
      p[0] = f | (s << 2); p[1] = out[0]; p[2] = out[1];
    }
    break;
  case DC_REACH:
    for (; n; --n, p += 9) {
      for (s = 0; s < 3; ++s) {
        dl_inv[s].sk[0] = p[s * 3]; dl_inv[s].sk[1] = p[s * 3 + 1]; dl_inv[s].sk[2] = p[s * 3 + 2];
        w[s] = (p[s * 3] & p[s * 3 + 1] & p[s * 3 + 2]) == 255 ? 0 : &dl_inv[s];
      }
      t = frame;
      s = sp_reach(w, 3, out, ovl);
      t = frame - t;
      if (t > worst) worst = t;
      p[0] = s; p[1] = out[0]; p[2] = out[1]; p[3] = (u8)t; p[4] = (u8)(t >> 8);
    }
    dbg_res[0] = worst;
    break;
  case DC_REACH_EQ:
    t = frame;
    dbg_res[0] = sp_reach_equipped(out, ovl);
    dbg_res[2] = frame - t;
    dbg_res[1] = out[0] | ((u16)out[1] << 8);
    break;
  case DC_FX:
    for (; n; --n, p += 18) {
      for (s = 0; s < 4; ++s) {
        dl_set(s, p + 2 + s * 4);
        w[s] = p[2 + s * 4] == 255 ? 0 : &dl_inv[s];
      }
      f = p[1] == 255 ? fx_state(w, p[0]) : fx_state_of(w, p[0], p[1]);
      p[0] = f; p[1] = (u8)fx_note; p[2] = (u8)(fx_note >> 8); p[3] = fx_slots;
    }
    break;
  case DC_SOLOALL:
    t = frame;
    inv_solo_all();
    dbg_res[0] = frame - t;
    for (t = 0, n = 0; t < sv.ninv; ++t) if (inv_solo(&sv.inv[t])) ++n;
    dbg_res[1] = n;
    break;
  case DC_POOL:
    for (t = 0; t < n; ++t) p[t] = solo_pool_id(t);
    break;
  }
}
#endif
