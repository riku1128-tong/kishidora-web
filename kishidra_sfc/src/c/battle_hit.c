/* Battle rules: hero attacks, hits, damage numbers, rewards (port of scripts/battle_core.gd). */
#include "battle.h"
#include "text.h"
#include <string.h>

Hit b_pending[PENDING_MAX];
u8 b_npending;
Popup b_popups[POPUP_MAX];
u8 b_npopups;
u16 b_pending_next;
u8 b_fx[2];
u16 b_coin_pct, b_drop_pct;
u8 b_men_kills;
u8 b_stair;
u16 b_boss_specials;

void foe_killed(Foe *e);
void awaken_talk(Foe *e);

static u8 slot_empty(u8 s) { return b_w[s].nsk == 0; }
static u8 foe_boss(const Foe *e) { return e->kind == EK_BOSS; }
static u8 foe_lucky(const Foe *e) { return e->kind == EK_LUCKY; }
static u8 forms_index(u8 mon) {
  u8 i;
  for (i = 0; i < 3; ++i) if (maou_forms[i] == mon) return i;
  return 255;
}
static u8 barrier_foe(const Foe *e) { return e->mon == MON_117 || forms_index(e->mon) != 255; }

/* ------------------------------------------------------------------ specials */
u8 find_special(void) {
  u8 best = 255, best_len = 0, i, n, k;
  const Special *sp;
  u8 seq[3];
  for (i = 0; i < N_SPECIAL; ++i) {
    sp = &specials[i];
    seq[0] = sp->sk1; seq[1] = sp->sk2; n = 2;
    if (sp->sk3 != SK_NONE) { seq[2] = sp->sk3; n = 3; }
    if (b_nhist < n || n <= best_len) continue;
    for (k = 0; k < n; ++k) if (b_history[b_nhist - n + k] != seq[k]) break;
    if (k == n) { best = i; best_len = n; }
  }
  return best;
}

static void hist_push(u8 sk) {
  if (b_nhist == 3) { b_history[0] = b_history[1]; b_history[1] = b_history[2]; b_nhist = 2; }
  b_history[b_nhist++] = sk;
}

u8 special_if(u8 slot) {
  u8 sk = skill_of(slot), sp;
  u8 save[3], n = b_nhist;
  if (sk == SK_NONE || skills[sk].hits == 0) return 255;
  memcpy(save, b_history, 3);
  hist_push(sk);
  sp = find_special();
  memcpy(b_history, save, 3);
  b_nhist = n;
  return sp;
}

static u8 first_targetable(u8 from) {
  u8 k, i;
  for (k = 0; k < b_ne; ++k) {
    i = (from + k) % b_ne;
    if (foe_targetable(&b_e[i])) return i;
  }
  return 255;
}

static u8 next_targetable(u8 from) {
  u8 k, i;
  for (k = 1; k <= b_ne; ++k) {
    i = (from + k) % b_ne;
    if (foe_targetable(&b_e[i])) return i;
  }
  return 255;
}

/* bit mask of targets */
static u8 target_set(u8 tgt) {
  u8 t = first_targetable(b_target), m = 0, i, n;
  if (t == 255) return 0;
  if (tgt == TGT_ALL) {
    for (i = 0; i < b_ne; ++i) if (foe_targetable(&b_e[i])) m |= 1 << i;
    return m;
  }
  if (tgt == TGT_TWO) {
    n = next_targetable(t);
    if (n != 255 && n != t) return (1 << t) | (1 << n);
  }
  return 1 << t;
}

static u8 all_sr(void) {
  u8 i;
  if (items[b_shield].rarity < R_SR) return 0;
  for (i = 0; i < SLOTS; ++i) if (b_w[i].item == ITEM_NONE || items[b_w[i].item].rarity < R_SR) return 0;
  return 1;
}

void use_skill(u8 slot) {
  Slot *w;
  u8 sk, cost, sp, special, i, tset, hits_n;
  const Skill *s;
  const Item *wr;
  const Effect *f;
  u32 base;
  u16 crit, crit_mul = 100, crit_up = 100, boost = 100, st_pct = 0;
  u8 crit_st = 0, hit_stun = 0, hit_slow = 0, st_gain = 0, powered;
  u16 hp_cost = 0;
  Hit h;
  if (slot >= SLOTS || slot_empty(slot) || first_targetable(b_target) == 255) return;
  if (is_locked(slot)) { ev_post(EV_LOCKED, 0, slot, 0, 0, 0); return; }
  sk = skill_of(slot);
  s = &skills[sk];
  cost = skill_cost(sk);
  if (b_st < cost) { ev_post(EV_ST_SHORT, 0, slot, cost, 0, 0); return; }
  b_st -= cost;
  w = &b_w[slot];
  w->pos = (w->pos + 1) % w->nsk;
  b_focus = slot;
  b_turn = TURN_HERO;
  b_buff_free = 0;
  if (s->hits == 0) {
    if (s->buff == BF_FREE) b_buff_free = 1;
    else if (s->buff == BF_POWER) b_buff_power = 1;
    else if (s->buff == BF_CRIT) b_buff_crit = 1;
    b_hero_state = HS_CHARGE;
    b_hero_timer = D_CHARGE_MOTION_FRAMES;
    ev_post(EV_CHARGE, 0, slot, sk, s->buff, cost);
    return;
  }
  b_hero_state = HS_ATTACK;
  b_hero_timer = D_ATTACK_MOTION_FRAMES;
  wr = &items[w->item];
  hist_push(sk);
  sp = find_special();
  special = sp != 255;
  base = mul_pct(mul_pct(wr->atk + w->plus, s->power), 100 + b_mirage_buff[MK_ATK]);
  if (special) {
    b_nhist = 0;
    base = mul_pct(base, specials[sp].mult);
    base = mul_pct(base, combo_pct(b_combo));
    base = mul_pct(base, 100 + b_mirage_buff[MK_SP]);
  }
  crit = (s->crit_pct > 0 ? s->crit_pct : wr->crit) + b_mirage_buff[MK_CRIT];
  if (b_buff_power) base = mul_pct(base, buff_value[BF_POWER]);
  if (b_buff_crit) crit_mul = buff_value[BF_CRIT];
  for (i = 0; i < 2; ++i) {
    f = &effects[b_fx[i]];
    switch (f->kind) {
    case FK_RESONANCE: if (f->v1 == wr->element) base = mul_pct(base, f->v2); break;
    case FK_TYPE_UP: if (f->v1 == wr->type) base = mul_pct(base, f->v2); break;
    case FK_BOOST: if (f->v1 == wr->element) boost = mul_pct16(boost, f->v2); break;
    case FK_SPECIAL_UP: if (special) base = mul_pct(base, f->v1); break;
    case FK_FULLHP_UP: if (b_hp >= b_hp_max) base = mul_pct(base, f->v1); break;
    case FK_PINCH_UP: if (b_hp <= mul_pct(b_hp_max, f->v1)) base = mul_pct(base, f->v2); break;
    case FK_COMBO_UP: if (b_combo >= (u16)f->v1) base = mul_pct(base, f->v2); break;
    case FK_POWER: base = mul_pct(base, f->v1); break;
    case FK_ROUND_POWER: base = mul_pct(base, 100 + f->v1 * (b_stage - 1)); break;
    case FK_CRIT_UP: crit_up = mul_pct16(crit_up, f->v1); break;
    case FK_BERSERK_UP: if (b_buff_crit) crit_mul = mul_pct16(crit_mul, f->v1); break;
    case FK_CRIT_ST: crit_st += f->v1; break;
    case FK_COMBO_ALL: if (!special) base = mul_pct(base, 100 + mul_pct16(combo_pct(b_combo) - 100, f->v1)); break;
    case FK_ALL_SR: if (all_sr()) base = mul_pct(base, f->v1); break;
    case FK_ST_POWER: base = mul_pct(base, 100 + f->v1 * cost); break;
    case FK_HP_POWER: base = mul_pct(base, f->v1); hp_cost += f->v2; break;
    case FK_COST_POWER: base = mul_pct(base, f->v2); break;
    case FK_HIT_ST: st_pct += f->v1; st_gain = MAX(st_gain, f->v2); break;
    case FK_HIT_STUN: hit_stun += f->v1; break;
    case FK_HIT_SLOW: hit_slow += f->v1; break;
    case FK_KILL_POWER: base = mul_pct(base, 100 + MIN(f->v1 * b_men_kills, f->v2)); break;
    case FK_HAISUI: if (b_hp <= mul_pct(b_hp_max, f->v1)) base = mul_pct(base, f->v3); break;
    }
  }
  if (hp_cost) {
    u32 up = MAX(1, mul_pct(b_hp_max, hp_cost));
    b_hp = b_hp > up ? b_hp - up : 1;
    ev_post(EV_HP_COST, 0, 0, 0, 0, b_hp);
  }
  crit = (u16)MIN(mul_pct16(mul_pct16(crit, crit_mul), crit_up), 100);
  powered = b_buff_power;
  b_buff_power = 0;
  b_buff_crit = 0;
  hits_n = special ? specials[sp].hits : s->hits;
  memset(&h, 0, sizeof(h));
  h.slot = slot;
  h.skill = sk;
  h.special = sp;
  h.base = base;
  h.crit = (u8)crit;
  h.crit_mult = s->crit_mult > 0 ? s->crit_mult : D_CRIT_PCT;
  h.atk = wr->atk + w->plus;
  h.hits = hits_n;
  h.boost = boost;
  h.crit_st = crit_st;
  h.hit_stun = hit_stun;
  h.hit_slow = hit_slow;
  h.ail = special ? AIL_NONE : s->ail;
  h.power = powered;
  tset = target_set(special ? specials[sp].target : s->target);
  h.tset = tset;
  for (i = 0; i < b_ne; ++i) {
    if (!(tset & (1 << i)) || b_npending >= PENDING_MAX) continue;
    h.enemy = i;
    b_pending[b_npending++] = h;
  }
  b_pending_next = b_time + 1;
  if (st_pct > 0 && tset && rnd_pct(st_pct)) {
    u8 st0 = b_st;
    b_st = (u8)MIN(b_st + st_gain + fx_sum(FK_ST_HEAL_UP, 1), b_st_max);
    ev_post(EV_ST_GAIN, 0, b_st, b_st - st0, 0, 0);
  }
  b_last_special = sp;
  ev_post(EV_ATTACK, 0, slot, sk, sp, cost);
}

/* ------------------------------------------------------------------ hits */
static u8 boss_guard(Foe *e, u8 power) {
  u8 secret = e->mon == MON_117;
  u16 pct, r;
  if (secret && e->miracle_next) { e->miracle_next = 0; return BLK_MIRACLE; }
  pct = D_BOSS_GUARD_PCT;
  if (power) pct = mul_pct16(pct, D_POWER_GUARD_PCT);
  if (!rnd_pct(pct)) return BLK_NONE;
  if (!secret) return BLK_GUARD;
  r = rnd_range(0, 99);
  if (r < D_SECRET_MIRACLE_PCT) return BLK_MIRACLE;
  if (r < D_SECRET_MIRACLE_PCT + D_SECRET_JUST_PCT) return BLK_JUST;
  return BLK_GUARD;
}

static u16 stun_chance(Foe *e, u16 pct) {
  u16 r = (e->mon == MON_117 || forms_index(e->mon) != 255) ? T_SECRET_STUN_PCT : 100;
  return r != 100 ? (u16)((u32)pct * 100 / r) : pct;
}

static u8 stun_turns_new(void) { return D_STUN_TURNS; }

void hit_enemy(Hit *h) {
  Foe *e = &b_e[h->enemy];
  const Item *wr = &items[b_w[h->slot].item];
  const Skill *sk = &skills[h->skill];
  const Effect *f;
  u32 base = h->base, stun, crit_dmg, dmg, add;
  u8 strong = el_strong[wr->element], weak_wt, weak_el = 255, i, k, special = h->special != 255;
  u8 block, crit, stunned, reset, slow, hail, miracle;
  u16 ail_pct = 0;
  s32 stun_room = -1;
  Popup *p;
  stun = mul_pct(mul_pct(wr->stun, sk->stun_pct), 100 + b_mirage_buff[MK_STUN]);
  weak_wt = e->weak != 255 && e->weak == wr->type;
  if (strong == e->element && strong != EL_NONE && strong != 255) {
    weak_el = wr->element;
    base = mul_pct(mul_pct(base, D_ELEMENT_ADV_PCT), h->boost);
  }
  if (weak_wt) {
    base = mul_pct(base, D_WEAPON_WEAK_PCT);
    stun = mul_pct(stun, D_WEAPON_WEAK_STUN_PCT);
  } else {
    stun = mul_pct(stun, T_STUN_NOT_WEAK_PCT);
    if (e->weak != 255 && barrier_foe(e)) base = MAX(1, base / T_BARRIER_NOT_WEAK_DIV);
  }
  if (sk->race != 255 && sk->race == e->race) {
    base = mul_pct(base, sk->race_pct);
    stun = mul_pct(stun, sk->race_pct);
  }
  for (i = 0; i < 2; ++i) {
    f = &effects[b_fx[i]];
    if (f->kind == FK_KILLER && (f->v1 == e->race || f->v3 == e->race)) base = mul_pct(base, f->v2);
    else if (f->kind == FK_PANIC && f->v1 == e->size) stun = mul_pct(stun, f->v2);
  }
  if (h->ail != AIL_NONE) {
    ail_pct = ails[h->ail].pct;
    if (h->ail == AIL_PARALYZE && (foe_boss(e) || e->mid)) ail_pct = mul_pct16(ail_pct, D_PARALYZE_BOSS_PCT);
  }
  crit_dmg = mul_pct(base, h->crit_mult);
  if (e->stun_times > 0) stun_room = MAX(1, e->stun_need / (1 + T_STUN_AGAIN_STEP * e->stun_times));
  for (k = 0; k < h->hits; ++k) {
    dmg = base;
    crit = e->stun_turns > 0 || (h->crit > 0 && rnd_pct(h->crit));
    if (crit) {
      dmg = crit_dmg;
      if (h->crit_st) {
        u8 st0 = b_st;
        b_st = (u8)MIN(b_st + h->crit_st, b_st_max);
        if (b_st > st0) ev_post(EV_ST_GAIN, 0, b_st, b_st - st0, 0, 0);
      }
    }
    block = BLK_NONE;
    if ((e->ability == BA_GUARD || e->ability == BA_ALL) && e->stun_turns == 0 && e->ail[AIL_PARALYZE] == 0 && !special)
      block = boss_guard(e, h->power);
    if (block == BLK_MIRACLE) dmg = 0;
    else if (block == BLK_JUST) dmg = MAX(1, mul_pct(dmg, 100 - D_SECRET_JUST_CUT_PCT));
    else if (block == BLK_GUARD) dmg = MAX(1, mul_pct(dmg, 100 - D_BOSS_GUARD_CUT_PCT));
    miracle = block == BLK_MIRACLE;
    stunned = 0;
    if (e->stun_turns == 0 && e->stun_need > 0 && !miracle) {
      add = stun;
      if (stun_room >= 0) { if (add > (u32)stun_room) add = stun_room; stun_room -= add; }
      e->stun += (u16)MIN(add, 60000);
      if (e->stun >= e->stun_need) { e->stun = e->stun_need; e->stun_turns = stun_turns_new(); stunned = 1; }
    }
    reset = !miracle && e->stun_turns == 0 && sk->reset_pct > 0 && rnd_pct(sk->reset_pct);
    if (!miracle && h->hit_stun > 0 && e->stun_turns == 0 && e->stun_need > 0 && rnd_pct(stun_chance(e, h->hit_stun))) {
      e->stun = e->stun_need; e->stun_turns = stun_turns_new(); stunned = 1;
    }
    slow = !miracle && h->hit_slow > 0 && e->stun_turns == 0 && rnd_pct(h->hit_slow);
    if (b_combo < 9999) ++b_combo;
    hail = (!miracle && ail_pct > 0 && rnd_pct(ail_pct)) ? h->ail : AIL_NONE;
    if (b_npopups < POPUP_MAX) {
      p = &b_popups[b_npopups++];
      memset(p, 0, sizeof(Popup));
      p->enemy = h->enemy; p->dmg = dmg; p->crit = crit; p->stun = stunned; p->reset = reset; p->special = special;
      p->at = b_time + 1 + D_MULTI_HIT_INTERVAL * k;
      p->wtype = wr->type; p->hit = k; p->st_gain = e->stun_turns > 0 && !stunned; p->block = block; p->slow = slow; p->ail = hail;
      p->weak_wt = weak_wt; p->weak_el = weak_el;
      p->kind = h->skill == SK_KICK ? PK_KICK : (h->skill == SK_SAKEBU ? PK_SHOUT : PK_HIT);
    }
  }
  ev_post(EV_HIT_ROLL, h->enemy, 0, 0, b_combo, 0);
}

void add_popup(u8 e, u32 dmg, u8 delay, u8 kind) {
  Popup *p;
  if (b_npopups >= POPUP_MAX) return;
  p = &b_popups[b_npopups++];
  memset(p, 0, sizeof(Popup));
  p->enemy = e; p->dmg = dmg; p->at = b_time + delay; p->kind = kind;
  p->wtype = kind == PK_REFLECT ? WT_SHIELD : 255;
  p->weak_el = 255;
}

void enemy_stun_now(Foe *e) {
  if (e->stun_turns || !foe_targetable(e) || foe_lucky(e)) return;
  e->stun = e->stun_need;
  e->stun_turns = stun_turns_new();
  ev_post(EV_STUN, e->idx, 0, 0, 0, 0);
  if (D_STUN_CANCEL_SPECIAL && foe_special_next(e)) { e->attacks = 0; ev_post(EV_SPECIAL_CANCEL, e->idx, 0, 0, 0, 0); }
}

void stun_end(Foe *e) {
  e->stun_turns = 0;
  e->count = e->awake ? 1 : MAX(1, (s16)e->base + fx_sum(FK_TURNS, 1) - fx_sum(FK_TURNS, 2));
  e->stun = 0;
  e->stun_need = MIN(e->stun_need + e->stun_step, e->stun_cap);
  ++e->stun_times;
  ev_post(EV_STUN_RECOVER, e->idx, 0, 0, 0, 0);
}

void awaken(Foe *e) {
  e->awake = 1;
  e->miracle_next = 1;
  e->hp_max = MAX(1, e->hp_max * T_SECRET_AWAKE_HP_PCT / 100);
  e->hp = e->hp_max;
  if (e->stun_turns) stun_end(e);
  memset(e->ail, 0, sizeof(e->ail));
  if (e->count > 1) e->count = 1;
  ev_post(EV_AWAKEN, e->idx, 0, 0, 0, 0);
  awaken_talk(e);
}

static void enemy_ailment(Foe *e, u8 kind) {
  if (!foe_targetable(e) || foe_lucky(e)) return;
  e->ail[kind] = ails[kind].turns;
  ev_post(EV_AILMENT, e->idx, kind, 0, 0, 0);
}

static void reset_count(Foe *e) {
  if (e->stun_turns || !foe_targetable(e) || foe_lucky(e)) return;
  e->count = e->awake ? 1 : MAX(1, (s16)e->base + fx_sum(FK_TURNS, 1) - fx_sum(FK_TURNS, 2));
  ev_post(EV_COUNT_RESET, e->idx, 0, 0, 0, 0);
}

static void enemy_slow(Foe *e) {
  if (e->stun_turns || !foe_targetable(e) || foe_lucky(e)) return;
  e->count = MIN(e->count + 1, 99);
  ev_post(EV_SLOW, e->idx, 0, 0, 0, 0);
}

static void guard_counter(Foe *e) {
  if (e->stun_turns || !foe_targetable(e)) return;
  e->count = 0;
  ev_post(EV_GUARD_COUNTER, e->idx, 0, 0, 0, 0);
}

void start_dying(Foe *e) {
  e->dying = D_DEFEAT_BLINK_FRAMES;
  e->bang = 0;
  ++b_kills[e->mon];
  ev_post(EV_DEFEAT, e->idx, 0, 0, 0, 0);
  give_reward(e);
}

void popup_update(void) {
  u8 i, killed, st0, j, hstop;
  Popup p;
  Foe *e;
  for (i = 0; i < b_npopups; ++i) {
    if (b_popups[i].at > b_time) continue;
    p = b_popups[i];
    for (j = i + 1; j < b_npopups; ++j) b_popups[j - 1] = b_popups[j];
    --b_npopups;
    e = &b_e[p.enemy];
    killed = 0;
    if (foe_targetable(e) && e->hp > 0) {
      e->hp = p.dmg >= e->hp ? 0 : e->hp - p.dmg;
      if (e->hp > 0 && e->mon == MON_117 && !e->awake && e->hp * 100 < e->hp_max * D_AWAKEN_HP_PCT) awaken(e);
      if (e->hp == 0) { killed = 1; foe_killed(e); }
    }
    st0 = b_st;
    if (p.st_gain) b_st = (u8)MIN(b_st + D_STUN_HIT_ST, b_st_max);
    hstop = p.kind == PK_POISON ? 0 : (p.special ? D_HITSTOP_SPECIAL : (p.crit ? D_HITSTOP_CRIT : D_HITSTOP_SKILL));
    ev_post(EV_NUMBER, p.enemy, (u8)((p.crit ? 1 : 0) | (p.special ? 2 : 0) | (killed ? 4 : 0) | (p.kind << 4)),
            (u8)((p.weak_wt ? 1 : 0) | (p.weak_el != 255 ? 2 : 0) | (p.block << 2) | (hstop << 4)),
            (u16)(p.wtype | ((u16)(b_st - st0) << 8)), p.dmg);
    if (p.stun && e->stun_turns && foe_targetable(e)) {
      ev_post(EV_STUN, e->idx, 0, 0, 0, 0);
      if (D_STUN_CANCEL_SPECIAL && foe_special_next(e)) { e->attacks = 0; ev_post(EV_SPECIAL_CANCEL, e->idx, 0, 0, 0, 0); }
    }
    if (p.ail != AIL_NONE && p.kind != PK_POISON) enemy_ailment(e, p.ail);
    if (p.reset) reset_count(e);
    if (p.slow) enemy_slow(e);
    if (p.block == BLK_MIRACLE) guard_counter(e);
    return;
  }
}

/* ------------------------------------------------------------------ drops */
static u8 roll_rarity(u8 n, u8 r) {
  u16 x = rnd_range(0, 99);
  if (x < n) return 0;
  if (x < (u16)n + r) return 1;
  return 2;
}

static const u8 drop_rarity[4] = { R_N, R_R, R_SR, R_SRP };

static u8 cand_count(u8 type, u8 rarity) { return cand_off[type][rarity][1]; }

u8 gacha_candidate(u8 type, u8 rarity) {
  u8 n = cand_off[type][rarity][1];
  if (!n) return ITEM_NONE;
  return cand_list[cand_off[type][rarity][0] + rnd_range(0, n - 1)];
}

static u8 game_phase(void) {
  if (b_area <= AR_POTOS) return 0;
  if (b_area <= AR_RUINS) return 1;
  if (b_area <= AR_ISLAND) return 2;
  return 3;
}

/* surface drop: returns item, *type = weapon type */
u8 surface_pick(u8 boss_like, u8 lucky, u8 sure, u8 *type) {
  static const u8 lucky_sr[4] = { D_LUCKY_SR_PCT_0, D_LUCKY_SR_PCT_1, D_LUCKY_SR_PCT_2, D_LUCKY_SR_PCT_3 };
  const Area *a = &areas[b_area];
  u8 pn, pr, r, t, sh, ls = 255;
  static const u8 wtypes[4] = { WT_SPEAR, WT_SWORD, WT_HAMMER, WT_SHIELD };   /* WTYPE_TABLE order */
  if (!lucky) {
    pn = a->n_pct; pr = a->r_pct;
    if (boss_like) { sh = MIN(pn, D_BOSS_DROP_SHIFT_PCT); pn -= sh; pr += sh / 2; }
  } else {
    ls = lucky_sr[game_phase()];
    pn = 0; pr = 100 - ls;
  }
  r = roll_rarity(pn, pr);
  if (boss_like && is_treasure()) r = 1 + roll_rarity(a->tr_r, a->tr_sr);
  if (b_area <= AR_RUINS && r > 1) r = 1;
  if (ls == 0 && r > 1) r = 1;
  if (is_treasure()) t = rnd_pct(T_TREASURE_SHIELD_PCT) ? WT_SHIELD : wtypes[rnd_range(0, 2)];
  else t = wtypes[rnd_range(0, 3)];
  if (sure) {
    r = 2;   /* SECRET_REPEAT_RARITY = SR */
    if (!cand_count(t, drop_rarity[r])) t = WT_SWORD;
  }
  while (r > 0 && !cand_count(t, drop_rarity[r])) --r;
  *type = t;
  return gacha_candidate(t, drop_rarity[r]);
}

static u8 kick_only(const u8 *sk, u8 n) {
  u8 i, kick = 0;
  for (i = 0; i < n; ++i) {
    if (sk[i] == SK_KICK) kick = 1;
    else if (skills[sk[i]].hits > 0) return 0;
  }
  return kick;
}

void roll_skills(u8 wtype, u8 *out, u8 *nout) {
  const u8 *lot = wtype == WT_SWORD ? skill_lot0 : (wtype == WT_SPEAR ? skill_lot1 : skill_lot2);
  u8 nl = wtype == WT_SWORD ? N_SKILL_LOT0 : (wtype == WT_SPEAR ? N_SKILL_LOT1 : N_SKILL_LOT2);
  u8 n = (u8)rnd_range(D_WEAPON_SLOTS_MIN, D_WEAPON_SLOTS_MAX), c = 0, sk, i, add, k;
  while (c < n) {
    sk = lot[rnd_range(0, nl - 1)];
    for (i = 0; i < c && out[i] != sk; ++i) ;
    if (i == c) out[c++] = sk;
  }
  if (kick_only(out, c) && rnd_pct(T_KICK_ONLY_FIX_PCT)) {
    do { add = lot[rnd_range(0, nl - 1)]; } while (add == SK_KICK || skills[add].hits == 0);
    if (c < D_WEAPON_SLOTS_MAX) out[c++] = add;
    else {
      k = c - 1;
      while (out[k] == SK_KICK) --k;
      out[k] = add;
    }
  }
  *nout = c;
}

static u8 reward_men(u8 *item) {
  if (is_secret()) {
    if (b_men != D_SECRET_MEN || b_men_cleared) return 0;
    *item = ITEM_SECRET_REWARD_SHIELD;
    return 1;
  }
  if (b_area == AR_PLAIN) {
    if (b_men != D_REWARD_SHIELD_MEN) return 0;
    *item = ITEM_REWARD_SHIELD_ID;
  } else {
    if (b_men != D_AREA_MEN || areas[b_area].reward == ITEM_NONE) return 0;
    *item = areas[b_area].reward;
  }
  return b_men_cleared ? 2 : 1;
}

static u8 reward_first(Foe *e, u8 *item) {
  if (!(foe_boss(e) || e->mid) || b_mirage) return 0;
  return reward_men(item);
}

static void run_item_add(u8 id) {
  RunItem *it;
  if (b_nitems >= ITEMS_RUN_MAX) return;
  it = &b_items[b_nitems++];
  it->item = id;
  if (items[id].type == WT_SHIELD || id == ITEM_FUUIN) it->nsk = 0;
  else roll_skills(items[id].type, it->sk, &it->nsk);
}

static u8 roll_item(Foe *e, u8 boss_like, u8 sure) {
  u8 type, id, first, rid = ITEM_NONE, lim;
  id = surface_pick(boss_like, foe_lucky(e), sure, &type);
  first = reward_first(e, &rid);
  if (first == 2 && !rnd_pct(D_REWARD_SHIELD_PCT)) first = 0;
  if (first) id = rid;
  if (!first && boss_like && !sure && b_area >= AR_CASTLE && rnd_pct(D_FUUIN_BOSS_PCT)) id = ITEM_FUUIN;
  lim = D_ITEM_MAX;
  if (!first && !b_reward_got && reward_men(&rid) == 1) --lim;
  if (b_nitems >= lim) return ITEM_NONE;
  if (first) b_reward_got = 1;
  run_item_add(id);
  return id;
}

static u8 mat_of_el(u8 claw, u8 el) {
  /* MAT_TSUME_FIRE.. / MAT_CLAW_FIRE.. in element order fire, water, wood, light, dark */
  return (claw ? MAT_CLAW_FIRE : MAT_TSUME_FIRE) + (el - EL_FIRE);
}

static u8 mat_roll(u16 pct) { return rnd_pct(MIN(mul_pct16(pct, b_drop_pct), 100)); }

static u8 claw_boss_pct(u8 mon) { return claw_pct[mon]; }

static u8 roll_mat(Foe *e, u8 boss_like) {
  u8 m = 255;
  u16 pct;
  if (e->race == RC_DRAGON && e->element != EL_NONE) {
    if (boss_like) {
      m = mat_of_el(0, e->element);
      if (rnd_pct(claw_boss_pct(e->mon))) m = mat_of_el(1, e->element);
    } else if (mat_roll(D_MAT_TSUME_ZAKO_PCT)) m = mat_of_el(0, e->element);
  } else if (e->element != EL_NONE && !boss_like && !foe_lucky(e) && mat_roll(T_TSUME_ELEM_ZAKO_PCT)) m = mat_of_el(0, e->element);
  if (m == 255) {
    pct = boss_like ? D_MAT_BELL_BOSS_PCT : (foe_lucky(e) ? D_MAT_BELL_LUCKY_PCT : 0);
    if (pct > 0 && mat_roll(pct)) m = MAT_BELL;
  }
  if (m == 255 && b_area >= AR_ISLET && mat_roll(boss_like ? D_MAT_SKYOUKA_BOSS_PCT : D_MAT_SKYOUKA_ZAKO_PCT)) m = MAT_SKYOUKA;
  if (m == 255) {
    pct = boss_like ? D_MAT_KYOUKA_BOSS_PCT : (foe_lucky(e) ? D_MAT_KYOUKA_LUCKY_PCT : D_MAT_KYOUKA_ZAKO_PCT);
    if (mat_roll(pct)) m = MAT_KYOUKA;
  }
  if (m != 255 && b_mats[m] < 250) ++b_mats[m];
  return m;
}

static u8 roll_goods(Foe *e, u8 boss_like) {
  u16 pct = boss_like ? T_FUERU_BOSS_PCT : (foe_lucky(e) ? T_FUERU_LUCKY_PCT : 0);
  if (!pct || b_area < AR_OASIS || !mat_roll(pct)) return 255;
  if (b_mats[MAT_IT_FUERU] < 250) ++b_mats[MAT_IT_FUERU];
  return MAT_IT_FUERU;
}

void give_reward(Foe *e) {
  u8 boss_like = foe_boss(e) || e->mid, kind = boss_like ? EK_BOSS : e->kind, item = ITEM_NONE, mat, goods, rid, sure, peko;
  static const u16 cmin[3] = { D_EK_ZAKO_COIN_MIN, D_EK_BOSS_COIN_MIN, D_EK_LUCKY_COIN_MIN };
  static const u16 cmax[3] = { D_EK_ZAKO_COIN_MAX, D_EK_BOSS_COIN_MAX, D_EK_LUCKY_COIN_MAX };
  static const u8 ipct[3] = { D_EK_ZAKO_ITEM_PCT, D_EK_BOSS_ITEM_PCT, D_EK_LUCKY_ITEM_PCT };
  u32 c;
  u16 pct;
  if (b_mirage) return;
  c = rnd_range(cmin[kind], cmax[kind]);
  pct = ipct[kind];
  c = mul_pct(c, areas[b_area].coin_pct);
  c = mul_pct(c, areas[b_area].coin_drop);
  if (!foe_lucky(e)) pct = boss_like ? D_MAP_BOSS_ITEM_PCT : D_MAP_ZAKO_ITEM_PCT;
  if (boss_like && is_treasure()) pct = 100;
  peko = is_secret() && e->mon == MON_117;
  if (peko) c = T_SECRET_COINS * (u32)b_coin_pct / 100;
  else c = mul_pct(c, b_coin_pct);
  b_coins += c;
  sure = peko && b_men_cleared;
  if (reward_first(e, &rid) == 1 || sure || rnd_pct(MIN(mul_pct16(pct, b_drop_pct), 100)))
    item = roll_item(e, boss_like, sure);
  mat = roll_mat(e, boss_like);
  goods = roll_goods(e, boss_like);
  ev_post(EV_REWARD, e->idx, item, mat, goods, c);
}
