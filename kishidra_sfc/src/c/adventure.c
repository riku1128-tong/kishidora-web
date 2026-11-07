/* adventure rules: loadout, battle results, gacha, shop and forge (adventure.gd) */
#include "adventure.h"

AdvResult res;
GachaPull gacha_out[GACHA_MAX];
Need need;
u8 enh_err;

/* ---------------------------------------------------------------- loadout */
void adv_loadout(Loadout *lo) {
  u8 s, k;
  Inv *it;
  memset(lo, 0, sizeof(Loadout));
  for (s = 0; s < 3; ++s) {
    lo->w[s].item = ITEM_NONE;
    lo->w[s].pos = s;
    if (sv.equip[s] == NO_ITEM) continue;
    it = &sv.inv[sv.equip[s]];
    lo->w[s].item = it->id;
    for (k = 0; k < 3; ++k) {
      lo->w[s].sk[k] = it->sk[k];
      if (it->sk[k] != 255) lo->w[s].nsk = k + 1;
    }
    lo->w[s].plus = enh_sum(it);
  }
  lo->shield = ITEM_EQUIP_SHIELD;
  if (sv.equip[EQ_SHIELD] != NO_ITEM) {
    it = &sv.inv[sv.equip[EQ_SHIELD]];
    lo->shield = it->id;
    lo->shield_plus = (u16)enh_sum(it) * T_ENH_SHIELD_HP;
  }
}

void adv_prepare(u8 area, u8 men) {
  b_difficulty = area == AR_MIRAGE ? DIFF_LUNATIC : sv.difficulty;
  b_guard_late = sv.guard_late;
  b_men_cleared = is_cleared(area, men);
}

/* ---------------------------------------------------------------- results */
static void commit_kills(void) {
  u8 m;
  for (m = 0; m < N_MON; ++m) {
    if (!b_kills[m]) continue;
    sv.kills[m] = ((u32)sv.kills[m] + b_kills[m] > 0xffff) ? 0xffff : sv.kills[m] + b_kills[m];
    bit_set(sv.found_mons, m);
    b_kills[m] = 0;
  }
  for (m = 0; m < b_ne; ++m) bit_set(sv.found_mons, b_e[m].mon);
}

static void commit_rewards(void) {
  u8 i;
  RunItem *r;
  coins_add(b_coins);
  res.coins = b_coins;
  res.overflow = 0;
  res.nitems = b_nitems;
  for (i = 0; i < b_nitems; ++i) {
    r = &b_items[i];
    res.fresh[i] = !bit_get(sv.found_items, r->item);
    res.idx[i] = inv_add(r->item, r->sk, r->nsk, -1, 1);
    if (res.idx[i] == NO_ITEM) {
      res.fresh[i] = 0;
      res.overflow += sell_price(r->item);
      coins_add(sell_price(r->item));
    }
  }
  for (i = 0; i < N_MAT; ++i) if (b_mats[i]) mat_add(i, b_mats[i]);
}

static u8 orb_at(u8 area, u8 men) {
  u8 i;
  for (i = 0; i < N_ORB; ++i) if (orbs[i].area == area && orbs[i].men == men) return i;
  return 255;
}

u8 king_return_due(void) { return !sv.ending_seen && area_done(AR_CASTLE); }

void commit_win(void) {
  memset(&res, 0, sizeof(res));
  res.win = 1;
  res.orb = 255;
  res.opened = 255;
  commit_rewards();
  commit_kills();
  if (is_treasure()) {
    if (sv.tickets) --sv.tickets;
    men_clear(b_area, b_men);
  } else if (is_money()) {
    men_clear(b_area, b_men);
  } else {
    if (!is_cleared(b_area, b_men)) {
      res.first = 1;
      res.orb = orb_at(b_area, b_men);
    }
    if (men_clear(b_area, b_men)) res.opened = sv.area_open;
  }
  res.king = king_return_due();
  save_write();
}

void commit_lose(void) {
  memset(&res, 0, sizeof(res));
  res.orb = res.opened = 255;
  res.coins = b_coins;
  res.nitems = b_nitems;
  commit_kills();
  save_write();
}

void commit_leave(void) { commit_lose(); }

/* mirage: score = wave * 2 + clear; ties go to fewer turns */
void commit_mirage(u8 clear) {
  u8 wave = clear ? b_stage_count : (b_stage ? b_stage : 1);
  u16 turns = b_turns_total;
  u8 a = wave * 2 + clear, b = sv.mirage_best * 2 + sv.mirage_clear;
  memset(&res, 0, sizeof(res));
  res.orb = res.opened = 255;
  res.mirage = 1;
  res.win = clear;
  res.mirage_wave = wave;
  res.mirage_clear = clear;
  if (a > b || (a == b && (sv.mirage_turns == 0 || turns < sv.mirage_turns))) {
    res.mirage_up = 1;
    sv.mirage_best = wave;
    sv.mirage_turns = turns;
  }
  if (clear) {
    if (!sv.mirage_clear) {
      mat_add(MIRAGE_FIRST_MAT, MIRAGE_FIRST_MAT_N);
      res.first = 1;
    }
    sv.mirage_clear = 1;
    coins_add(MIRAGE_COINS);
    res.coins = MIRAGE_COINS;
  }
  commit_kills();
  save_write();
}

/* ---------------------------------------------------------------- shops */
u8 area_has_shop(u8 area) {
  return area == AR_PLAIN || area == AR_POTOS || area == AR_OASIS || area == AR_FISHING;
}

u8 last_shop_area(void) {
  u8 a, best = 255;
  for (a = 0; a <= sv.area_open && a < AR_MIRAGE; ++a) if (area_has_shop(a)) best = a;
  return best;
}

static u8 gacha_type(void) {
  u8 x = (u8)rnd_range(0, gacha_wt[0] + gacha_wt[1] + gacha_wt[2] + gacha_wt[3] - 1), t;
  for (t = 0; t < 4; ++t) {
    if (x < gacha_wt[t]) return t;
    x -= gacha_wt[t];
  }
  return 0;
}

static u8 gacha_pick(u8 type) {
  u8 roll = (u8)rnd_range(0, 99), rank = 0, acc = 0, i, id;
  for (i = 0; i < N_R; ++i) {
    acc += gacha_rate[i];
    if (roll < acc) { rank = i; break; }
  }
  for (;;) {
    id = gacha_candidate(type, rank);
    if (id != ITEM_NONE || rank == 0) return id;
    --rank;
  }
}

u8 gacha_pull(u8 n) {
  u8 i, type, id, sk[3], nsk;
  GachaPull *g;
  if ((u32)GACHA_PRICE * n > sv.coins) return ERR_COINS;
  if (sv.ninv + n > D_INV_MAX) return ERR_FULL;
  for (i = 0; i < n; ++i) {
    g = &gacha_out[i];
    type = gacha_type();
    id = gacha_pick(type);
    nsk = 0;
    if (type != WT_SHIELD) roll_skills(type, sk, &nsk);
    g->id = id;
    g->fresh = !bit_get(sv.found_items, id);
    g->idx = inv_add(id, sk, nsk, -1, 0);
    sv.coins -= GACHA_PRICE;
    g->ticket = 0;
    if (sv.tickets < D_TICKET_MAX && rnd_pct(D_GACHA_TICKET_PCT)) { ++sv.tickets; g->ticket = 1; }
  }
  save_write();
  return ERR_OK;
}

/* ---------------------------------------------------------------- items */
u8 sell_item(u16 i) {
  Inv *it = &sv.inv[i];
  if (i >= sv.ninv || equipped_slot(i) != 255 || inv_fav(it)) return ERR_CANT;
  coins_add(sell_price(it->id));
  inv_remove(i);
  save_write();
  return ERR_OK;
}

void fav_toggle(u16 i) {
  sv.inv[i].slots ^= 0x80;
  save_write();
}

u8 goods_sold(u8 area, u8 mat) {
  return mat == MAT_IT_RESET && area_has_shop(area) && area >= AR_POTOS;
}

u8 goods_buy(u8 mat) {
  u16 price = mat == MAT_IT_FUERU ? PRICE_IT_FUERU : PRICE_IT_RESET;
  if (sv.coins < price) return ERR_COINS;
  if (sv.mats[mat] >= T_GOODS_MAX) return ERR_MAX;
  sv.coins -= price;
  ++sv.mats[mat];
  save_write();
  return ERR_OK;
}

u32 item_value(u8 id) {
  return items[id].atk;
}

/* ---------------------------------------------------------------- forge */
u8 evolve_target(u8 id) { return items[id].evolve; }

static void need_add(u8 m, u16 n) {
  u8 i;
  if (!n || m == 255) return;
  for (i = 0; i < need.n; ++i) if (need.mat[i] == m) { need.cnt[i] += n; return; }
  if (need.n < 7) { need.mat[need.n] = m; need.cnt[need.n++] = n; }
}

void evolve_need(u8 id) {
  u8 to = evolve_target(id), el, i;
  const EvolveCost *c;
  need.n = 0;
  need.coins = 0;
  if (to == 255) return;
  if (id == ITEM_FUUIN) {
    need_add(MAT_BELL, D_FUUIN_BELLS);
    need.coins = evolve_cost[R_SRP].coins;
    return;
  }
  for (i = 0; i < EVOLVE_SPECIAL_N; ++i) {
    if (evolve_special[i].item != id) continue;
    for (el = 0; el < 7; ++el) need_add(evolve_special[i].mats[el][0], evolve_special[i].mats[el][1]);
    need.coins = evolve_special[i].coins;
    return;
  }
  c = &evolve_cost[items[id].rarity];
  el = item_is_shield(id) ? items[id].element : items[to].element;
  need_add(MAT_KYOUKA, c->kyouka);
  need_add(MAT_SKYOUKA, c->skyouka);
  if (el == EL_NONE || el == 255) {
    need_add(MAT_SKYOUKA, c->tsume);
    need_add(MAT_HKYOUKA, c->claw);
  } else if (el == 254) {          /* every element */
    for (i = EL_FIRE; i <= EL_DARK; ++i) need_add(MAT_TSUME_FIRE + i - EL_FIRE, MIN(1, c->tsume));
    for (i = EL_FIRE; i <= EL_DARK; ++i) need_add(MAT_CLAW_FIRE + i - EL_FIRE, MIN(1, c->claw));
  } else {
    need_add(MAT_TSUME_FIRE + el - EL_FIRE, c->tsume);
    need_add(MAT_CLAW_FIRE + el - EL_FIRE, c->claw);
  }
  need.coins = c->coins;
}

u8 evolve_short(void) {
  u8 i;
  for (i = 0; i < need.n; ++i) if (sv.mats[need.mat[i]] < need.cnt[i]) return ERR_MATS;
  return sv.coins < need.coins ? ERR_COINS : ERR_OK;
}

u8 evolve_item(u16 i) {
  Inv *it = &sv.inv[i];
  u8 to = evolve_target(it->id), k, e;
  if (to == 255) return ERR_CANT;
  evolve_need(it->id);
  e = evolve_short();
  if (e) return e;
  for (k = 0; k < need.n; ++k) sv.mats[need.mat[k]] -= need.cnt[k];
  sv.coins -= need.coins;
  it->id = to;
  add_got(to);
  save_write();
  return ERR_OK;
}

u8 enh_free_slot(const Inv *it) {
  u8 i;
  for (i = 0; i < inv_slots_of(it); ++i) if (!enh_get(it, i)) return i;
  return 255;
}

/* src_mat != 255: use a material, else the item src_item. odds rows: kyouka, skyouka, hkyouka, bell, same effect, other */
u8 enhance(u16 target, u8 src_mat, u16 src_item, u8 *slot) {
  Inv *it = &sv.inv[target];
  const u8 *odds;
  u8 s = enh_free_slot(it), x, r, acc;
  enh_err = ERR_OK;
  if (s == 255) { enh_err = ERR_NO_SLOT; return 255; }
  if (src_mat != 255) {
    if (!sv.mats[src_mat]) { enh_err = ERR_MATS; return 255; }
    odds = enh_odds[src_mat <= MAT_BELL ? src_mat : 5];
    --sv.mats[src_mat];
  } else {
    Inv *src = &sv.inv[src_item];
    if (src_item == target || src_item >= sv.ninv || inv_fav(src) || equipped_slot(src_item) != 255) { enh_err = ERR_CANT; return 255; }
    odds = (items[it->id].effect != FX_NONE && items[it->id].effect == items[src->id].effect) ? enh_odds[4] : enh_odds[5];
    inv_remove(src_item);
    if (src_item < target) { --target; it = &sv.inv[target]; }
  }
  x = (u8)rnd_range(0, 99);
  r = 0; acc = 0;
  for (r = 0; r < 4; ++r) {
    acc += odds[r];
    if (x < acc) break;
  }
  if (r > 3) r = 0;
  if (r) enh_set(it, s, r);
  *slot = r ? s : 255;
  save_write();
  return r;
}

u8 slot_add(u16 i) {
  Inv *it = &sv.inv[i];
  if (!sv.mats[MAT_IT_FUERU]) return ERR_NO_GOODS;
  if (inv_slots_of(it) >= T_ENH_SLOT_MAX) return ERR_MAX;
  --sv.mats[MAT_IT_FUERU];
  ++it->slots;
  save_write();
  return ERR_OK;
}

u8 slot_reset(u16 i, u8 slot) {
  Inv *it = &sv.inv[i];
  if (slot >= inv_slots_of(it) || !enh_get(it, slot)) return ERR_CANT;
  if (!sv.mats[MAT_IT_RESET]) return ERR_NO_GOODS;
  --sv.mats[MAT_IT_RESET];
  enh_set(it, slot, 0);
  save_write();
  return ERR_OK;
}

u16 mat_up_max(u8 m) {
  u8 up = mat_up[m];
  u16 a, b, c;
  if (up == 255) return 0;
  a = sv.mats[m] / D_MAT_UP_COUNT;
  b = (u16)MIN(sv.coins / D_MAT_UP_COINS, 999);
  c = D_MAT_MAX - sv.mats[up];
  return MIN(MIN(a, b), c);
}

u8 mat_up_do(u8 m, u16 n) {
  u8 up = mat_up[m];
  if (up == 255) return ERR_CANT;
  if (n < 1 || sv.mats[m] < D_MAT_UP_COUNT * n) return ERR_MATS;
  if (sv.coins < (u32)D_MAT_UP_COINS * n) return ERR_COINS;
  if (sv.mats[up] + n > D_MAT_MAX) return ERR_MAX;
  sv.mats[m] -= D_MAT_UP_COUNT * n;
  sv.coins -= (u32)D_MAT_UP_COINS * n;
  mat_add(up, n);
  save_write();
  return ERR_OK;
}

/* ---------------------------------------------------------------- tutorial */
void adv_tutorial_loadout(Loadout *lo) {
  memset(lo, 0, sizeof(Loadout));
  lo->w[0].item = ITEM_START_WEAPON;
  lo->w[0].sk[0] = START_SKILL_0; lo->w[0].sk[1] = START_SKILL_1; lo->w[0].sk[2] = 255;
  lo->w[0].nsk = START_SKILL_1 != 255 ? 2 : 1;
  lo->w[1].item = ITEM_TUTORIAL_SUB_WEAPON;
  lo->w[1].sk[0] = TUTORIAL_SUB_SKILL_0; lo->w[1].sk[1] = TUTORIAL_SUB_SKILL_1; lo->w[1].sk[2] = 255;
  lo->w[1].nsk = TUTORIAL_SUB_SKILL_1 != 255 ? 2 : 1;
  lo->w[2].item = ITEM_NONE;
  lo->shield = ITEM_EQUIP_SHIELD;
}

static void give(u8 id, const u8 *sk, u8 nsk) {
  u8 i = res.nitems;
  if (i >= ITEMS_RUN_MAX) return;
  res.fresh[i] = !bit_get(sv.found_items, id);
  res.idx[i] = inv_add(id, sk, nsk, -1, 1);
  if (res.idx[i] == NO_ITEM) { res.fresh[i] = 0; res.overflow += sell_price(id); coins_add(sell_price(id)); }
  b_items[i].item = id;
  res.nitems = ++i;
}

/* commit_tutorial(): the reward shield the first time, a relief weapon on the 5th clear */
void commit_tutorial(void) {
  static const u8 relief[3] = { TUTORIAL_RELIEF_SKILL_0, TUTORIAL_RELIEF_SKILL_1, TUTORIAL_RELIEF_SKILL_2 };
  u8 n = 0, k;
  memset(&res, 0, sizeof(res));
  res.win = 1;
  res.orb = res.opened = 255;
  commit_kills();
  if (sv.tut_clears < 7) ++sv.tut_clears;
  if (!sv.tut_cleared) {
    give(ITEM_TUTORIAL_REWARD_SHIELD, 0, 0);
    sv.tut_cleared = 1;
  }
  if (sv.tut_clears == D_TUTORIAL_RELIEF_CLEARS) {
    for (k = 0; k < 3; ++k) if (relief[k] != 255) ++n;
    give(ITEM_TUTORIAL_RELIEF_WEAPON, relief, n);
  }
  b_nitems = res.nitems;
  save_write();
}
