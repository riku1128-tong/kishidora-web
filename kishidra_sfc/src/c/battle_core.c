/* Battle rules: flow, enemy turns, guard (port of scripts/battle_core.gd, surface world). */
#include "battle.h"
#include "text.h"
#include <string.h>

u8 b_phase, b_turn, b_hero_state, b_step_kind;
u16 b_phase_timer, b_hero_timer, b_step_timer, b_time;
u8 b_area, b_men, b_stage, b_stage_count, b_target, b_focus;
u32 b_hp, b_hp_max;
u8 b_st, b_st_max;
u16 b_combo;
Slot b_w[SLOTS];
u8 b_shield;
u16 b_shield_plus;
Foe b_e[ENEMY_MAX];
u8 b_ne;
u8 b_buff_free, b_buff_power, b_buff_crit;
u8 b_lock_slot, b_lock_slot2, b_lock_turns;
u8 b_attacker;
u8 b_special_now;
u16 b_guard_cool;
u8 b_guard_meat;
u32 b_coins;
RunItem b_items[ITEMS_RUN_MAX];
u8 b_nitems;
u8 b_mats[N_MAT];
u8 b_talk;
u16 b_talk_text, b_talk_req;
u8 b_talk_who;
u8 b_form_who;
u8 b_difficulty = 1;
u8 b_mode, b_freeze, b_tut_protect;   /* tutorial / training */
u8 b_guard_late = T_GUARD_LATE_FRAMES;
u8 b_mirage, b_mirage_offer_n;
u8 b_mirage_offer[3][2];
u16 b_mirage_buff[N_MIRAGE_KIND];
u8 b_men_cleared;
u8 b_reward_got;
u16 b_turns_total;
u16 b_turns_stage;
u8 b_meat_gifts;
u8 b_history[3], b_nhist;
u8 b_last_special;
u16 b_kills[N_MON];
u8 b_seen[(N_MON + 7) / 8];
u8 b_sp_done[(N_SPECIAL + 7) / 8];

static u16 guard_press, hit_time;
static u8 attack_queue[ENEMY_MAX], aq_n;
static u8 attack_over;
static u8 opening_lock, opening_beg, lock_open_now, beg_said;
static u8 blind_mask;
static u8 mid_def, mid_placed;
static u16 talk_wait, talk_timer;
static s8 after_talk;
static u16 form_time, form_gap, form_beat;
static u8 attack_sab;
static u8 in_skill, in_guard, in_meat, in_next;
static u8 mirage_stock[N_MIRAGE_KIND][MIRAGE_TIERS];

static EStat est[24];
static u8 nest;

static Ev evq[EVENT_MAX];
static u8 ev_head, ev_tail;

#ifdef FORCE_HOOKS
/* test builds (battle.h): XDEFS FORCE_WAVE=0,0,6 (monster ids) makes every wave of a normal battle these
 * monsters, FORCE_MIRAGE_WAVE=n starts the tower run at WAVE n; in a TEST_HOOKS ROM both can also be
 * poked before the battle starts (force_wave 0 0 6 255) */
#ifndef FORCE_WAVE
#define FORCE_WAVE MON_NONE
#endif
#ifndef FORCE_MIRAGE_WAVE
#define FORCE_MIRAGE_WAVE 0
#endif
u8 force_wave[ENEMY_MAX + 1] = { FORCE_WAVE, MON_NONE };
u8 force_mwave = FORCE_MIRAGE_WAVE;
#endif

#ifdef TEST_HOOKS
u8 dbg_evlog[DBG_EVLOG * 4], dbg_evn;   /* the last DBG_EVLOG events posted: type, enemy, a, b (battle.h) */
#endif
void ev_post(u8 type, u8 enemy, u8 a, u8 b, u16 c, u32 v) {
  u8 n = (ev_tail + 1) % EVENT_MAX;
  Ev *e;
#ifdef TEST_HOOKS
  { u8 *l = dbg_evlog + (dbg_evn & (DBG_EVLOG - 1)) * 4; l[0] = type; l[1] = enemy; l[2] = a; l[3] = b; ++dbg_evn; }
#endif
  if (n == ev_head) return;        /* full: drop */
  e = &evq[ev_tail];
  e->type = type; e->enemy = enemy; e->a = a; e->b = b; e->c = c; e->v = v;
  ev_tail = n;
}

u8 ev_pop(Ev *out) {
  if (ev_head == ev_tail) return 0;
  *out = evq[ev_head];
  ev_head = (ev_head + 1) % EVENT_MAX;
  return 1;
}

/* ------------------------------------------------------------------ helpers */
const DiffRule *rules(void) { return &diff_rules[b_difficulty]; }

u8 is_castle(void) { return b_area == AR_CASTLE && b_men != T_MONEY_MEN; }
u8 is_money(void) { return b_area == AR_CASTLE && b_men == T_MONEY_MEN; }   /* 金策: 魔王城の地下 */
u8 is_secret(void) { return b_area == AR_SECRET; }

/* the dex marks every foe on the field (not the training dummy) */
static void mark_seen(void) {
  u8 i;
  if (b_mode == BM_TRAINING) return;
  for (i = 0; i < b_ne; ++i) b_seen[b_e[i].mon >> 3] |= (u8)(1 << (b_e[i].mon & 7));
}
u8 is_treasure(void) { return b_men == D_TREASURE_MEN && areas[b_area].treasure; }

u8 foe_targetable(const Foe *e) { return !e->gone && e->dying == 0 && e->fleeing == 0; }
u8 foe_special_next(const Foe *e) { return e->sp_atk > 0 && e->sp_every > 0 && e->attacks >= e->sp_every; }
static u8 foe_boss(const Foe *e) { return e->kind == EK_BOSS; }
static u8 foe_lucky(const Foe *e) { return e->kind == EK_LUCKY; }

static u8 forms_index(u8 mon) {
  u8 i;
  for (i = 0; i < 3; ++i) if (maou_forms[i] == mon) return i;
  return 255;
}
static u8 awaken_boss(u8 mon) { return mon == MON_117; }

u8 fx_has(u8 kind) {
  return effects[b_fx[0]].kind == kind || effects[b_fx[1]].kind == kind;
}

s16 fx_sum(u8 kind, u8 which) {
  s16 n = 0;
  u8 i;
  const Effect *f;
  for (i = 0; i < 2; ++i) {
    f = &effects[b_fx[i]];
    if (f->kind == kind) n += which == 1 ? f->v1 : (which == 2 ? f->v2 : f->v3);
  }
  return n;
}

u8 item_rarity_of(u8 item) { return items[item].rarity; }

u8 skill_of(u8 slot) {
  Slot *w = &b_w[slot];
  if (!w->nsk) return SK_NONE;
  return w->sk[w->pos];
}

u8 next_skill_of(u8 slot) {
  Slot *w = &b_w[slot];
  if (!w->nsk) return SK_NONE;
  return w->sk[(w->pos + 1) % w->nsk];
}

static s16 cost_cut(void) { return 0; }   /* FK_ALL_COST / FK_FIRST_COST are not in the surface world */

u8 skill_cost(u8 sk) {
  s16 c;
  if (sk == SK_NONE) return 0;
  if (b_buff_free) return buff_value[BF_FREE];
  c = skills[sk].cost + (skills[sk].hits > 0 ? fx_sum(FK_COST_POWER, 1) : 0);
  c = MAX(MIN(c, 1), c - cost_cut());
  return (u8)c;
}

u8 is_locked(u8 slot) { return slot != 255 && (slot == b_lock_slot || slot == b_lock_slot2); }

static u8 slot_empty(u8 s) { return b_w[s].nsk == 0; }

static u8 next_slot(u8 from) {
  u8 k, s = from;
  for (k = 1; k < SLOTS; ++k) {
    s = (s + 1) % SLOTS;
    if (!slot_empty(s)) return s;
  }
  return from;
}

u32 calc_hp_max(void) {
  s16 heart = fx_sum(FK_HEART, 1);
  u32 sh = items[b_shield].atk + b_shield_plus;
  return mul_pct(D_HERO_HP_BASE + sh, 100 + heart + b_mirage_buff[MK_MAXHP]);
}

static u16 shield_guard_pct(void) {
  u16 p = (items[b_shield].def + b_mirage_buff[MK_DEF]) * D_GUARD_PCT_PER_DEF;
  return MIN(p, D_GUARD_PCT_MAX);
}

static u8 base_count(Foe *e) {
  s16 n;
  if (e->awake) return 1;
  n = e->base;
  if (!foe_lucky(e)) n += fx_sum(FK_TURNS, 1) - fx_sum(FK_TURNS, 2);
  return (u8)MAX(1, n);
}

u16 combo_pct(u16 c) {
  static const u8 counts[4] = { D_COMBO_TIER_COUNT_0, D_COMBO_TIER_COUNT_1, D_COMBO_TIER_COUNT_2, D_COMBO_TIER_COUNT_3 };
  static const u16 pcts[4] = { T_COMBO_TIER_PCT_0, T_COMBO_TIER_PCT_1, T_COMBO_TIER_PCT_2, T_COMBO_TIER_PCT_3 };
  u16 p = 100;
  u8 i;
  for (i = 0; i < 4; ++i) if (c >= counts[i]) p = pcts[i];
  return p;
}

/* ------------------------------------------------------------------ set up */
static void greed(void) {
  const Effect *f;
  u8 i;
  b_coin_pct = 100; b_drop_pct = 100;
  for (i = 0; i < 2; ++i) {
    f = &effects[b_fx[i]];
    if (f->kind == FK_GREED) { b_coin_pct = mul_pct16(b_coin_pct, f->v1); b_drop_pct = mul_pct16(b_drop_pct, f->v2); }
  }
}

static void equip(const Loadout *lo) {
  u8 i;
  b_st_max = D_HERO_ST_MAX;
  b_shield = lo->shield;
  b_shield_plus = lo->shield_plus;
  for (i = 0; i < SLOTS; ++i) { b_w[i] = lo->w[i]; b_w[i].pos = 0; }
  b_fx[0] = b_w[0].item != ITEM_NONE ? items[b_w[0].item].effect : FX_NONE;
  b_fx[1] = items[b_shield].effect;
  greed();
}

static EStat *estat_find(u8 mon, u8 role, u8 stage) {
  u8 i;
  for (i = 0; i < nest; ++i)
    if (est[i].mon == mon && est[i].role == role && (stage == 0 || est[i].stage == stage)) return &est[i];
  for (i = 0; i < nest; ++i)
    if (est[i].mon == mon) return &est[i];
  return &est[0];
}

#ifdef HOST
void host_estat(u8 area, u8 men, EStat *dst, u8 *n);
#endif
static void load_estats(void) {
#ifdef HOST
  host_estat(b_area, b_mirage ? 1 : b_men, est, &nest);
#else
  ea[0] = b_area; ea[1] = b_mirage ? 1 : b_men; ea[3] = (u16)est;
  estat_block();
  nest = (u8)ea[2];
#endif
  if (nest > 24) nest = 24;
}

static void make_enemy(Foe *e, u8 mon) {
  const Enemy *r = &enemy_rows[mon];
  EStat *s;
  u8 role = r->kind == EK_LUCKY ? ROLE_LUCKY : (r->kind == EK_BOSS ? ROLE_BOSS : ROLE_ZAKO);
  u16 dp;
  if (b_mirage) s = &est[b_stage - 1 < nest ? b_stage - 1 : 0];
  else if (role == ROLE_BOSS) s = estat_find(mon, ROLE_BOSS, b_stage);
  else if (mon == mid_def && !mid_placed) { s = estat_find(mon, ROLE_MID, b_stage); mid_placed = 1; }
  else s = estat_find(mon, role, 0);
  memset(e, 0, sizeof(Foe));
  e->mon = mon;
  e->kind = r->kind; e->size = r->size; e->race = r->race; e->element = r->element;
  e->weak = r->weak; e->sp_fx = r->sp_fx;
  e->hp_max = s->hp; e->atk = s->atk; e->sp_atk = s->sp_atk; e->lock_atk = s->lock_atk;
  e->sp_every = s->sp_every; e->base = s->base;
  e->stun_need = s->stun_need; e->stun_step = s->stun_step; e->stun_cap = s->stun_cap;
  e->mid = s->flags & 1;
  e->miracle_next = (s->flags & 2) ? 1 : 0;
  e->ability = (e->kind == EK_BOSS || e->mid) ? s->ability : BA_NONE;
  dp = rules()->atk;
  if (dp != 100 && !foe_lucky(e)) {
    e->atk = (u16)((u32)e->atk * dp / 100);
    e->sp_atk = (u16)((u32)e->sp_atk * dp / 100);
    if (e->lock_atk > 0) e->lock_atk = (s16)((u32)e->lock_atk * dp / 100);
  }
  e->hp = e->hp_max;
  if (mon == MON_115) e->attacks = e->sp_every;   /* MEAT_ASK: asks at once */
  e->count = base_count(e);
  if (!foe_lucky(e) && !foe_boss(e)) e->count += (u8)rnd_range(0, D_SPAWN_TURN_JITTER);
}

u8 stage_boss_kind(void) {
  if (b_mirage) return 2;
  if (b_stage != b_stage_count && !(is_castle() && b_men == D_CASTLE_MEN)) return 0;
  if (is_castle() || is_secret() || b_men == D_AREA_MEN) return 2;
  if (is_treasure() || is_money()) return 2;
  return b_men == D_AREA_MEN - 1 ? 1 : 0;
}

static u8 pick(u8 n) { return (u8)rnd_range(0, n - 1); }

static u8 enemy_cap(void) { return b_area == AR_PLAIN ? 2 : ENEMY_MAX; }

static u8 pick_formation(u8 *ids) {
  u8 n = 0, kind, s = b_stage - 1, boss = MON_NONE, k, tries = 0, m;
  const Area *a = &areas[b_area];
  mid_def = MON_NONE;
  if (b_mirage) { ids[0] = est[b_stage - 1 < nest ? b_stage - 1 : 0].mon; return 1; }
  if (is_treasure()) {
    static const u8 tl[3] = { D_TREASURE_LUCKY_0, D_TREASURE_LUCKY_1, D_TREASURE_LUCKY_2 };
    static const u8 tz[3] = { D_TREASURE_ZAKO_0, D_TREASURE_ZAKO_1, D_TREASURE_ZAKO_2 };
    if (b_stage == b_stage_count) ids[n++] = MON_115;
    for (k = 0; k < tl[s] && n < ENEMY_MAX; ++k) ids[n++] = lucky_mons[pick(N_LUCKY)];
    for (k = 0; k < tz[s] && n < ENEMY_MAX; ++k) ids[n++] = a->pool[pick(a->pool_n)];
    return n;
  }
  if (is_money()) {
    static const u8 ml[5] = { T_MONEY_LUCKY_0, T_MONEY_LUCKY_1, T_MONEY_LUCKY_2, T_MONEY_LUCKY_3, T_MONEY_LUCKY_4 };
    static const u8 mz[5] = { T_MONEY_ZAKO_0, T_MONEY_ZAKO_1, T_MONEY_ZAKO_2, T_MONEY_ZAKO_3, T_MONEY_ZAKO_4 };
    if (b_stage == b_stage_count) ids[n++] = MON_115;          /* the guard: ミミッくん */
    for (k = 0; k < ml[s] && n < ENEMY_MAX; ++k) ids[n++] = lucky_mons[pick(N_LUCKY)];
    for (k = 0; k < mz[s] && n < ENEMY_MAX; ++k) ids[n++] = a->pool[pick(a->pool_n)];
    return n;
  }
  kind = stage_boss_kind();
  if (kind > 0) {
    if (is_castle()) boss = b_men == D_CASTLE_MEN ? castle_final[s] : castle_bosses[b_men - 1];
    else boss = kind == 2 ? a->boss : a->mid;
    ids[n++] = boss;
    if (is_secret()) m = (u8)rnd_range(0, D_SECRET_ESCORT_MAX);
    else if (forms_index(boss) != 255) m = (u8)rnd_range(0, D_MAOU_ESCORT_MAX);
    else m = (u8)rnd_range(0, D_BOSS_ESCORT_MAX);
    if (no_escort[boss]) m = 0;
  } else if (is_castle()) {
    static const u8 cmin[5] = { D_CASTLE_ZAKO_MIN_0, D_CASTLE_ZAKO_MIN_1, D_CASTLE_ZAKO_MIN_2, D_CASTLE_ZAKO_MIN_3, D_CASTLE_ZAKO_MIN_4 };
    static const u8 cmax[5] = { D_CASTLE_ZAKO_MAX_0, D_CASTLE_ZAKO_MAX_1, D_CASTLE_ZAKO_MAX_2, D_CASTLE_ZAKO_MAX_3, D_CASTLE_ZAKO_MAX_4 };
    m = (u8)rnd_range(cmin[s], cmax[s]);
  } else {
    static const u8 zmin[5] = { D_MEN_ZAKO_MIN_0, D_MEN_ZAKO_MIN_1, D_MEN_ZAKO_MIN_2, D_MEN_ZAKO_MIN_3, D_MEN_ZAKO_MIN_4 };
    static const u8 zmax[5] = { D_MEN_ZAKO_MAX_0, D_MEN_ZAKO_MAX_1, D_MEN_ZAKO_MAX_2, D_MEN_ZAKO_MAX_3, D_MEN_ZAKO_MAX_4 };
    m = (u8)rnd_range(zmin[s], zmax[s]);
  }
  if (b_area == AR_ARCADIA) m = kind > 0 ? 0 : 1;   /* ONE_ENEMY_AREA */
  while (m > 0 && n < enemy_cap() && tries < 100 && a->pool_n) {
    ++tries;
    k = a->pool[pick(a->pool_n)];
    if (k == boss) continue;
    ids[n++] = k;
    --m;
  }
  if (kind == 1) mid_def = boss;
  return n;
}

static void add_lucky(u8 *ids, u8 *n) {
  u8 i, boss = 0;
  if (is_secret() || b_mirage || b_area == AR_ARCADIA) return;
  for (i = 0; i < *n; ++i) {
    if (no_escort[ids[i]] || forms_index(ids[i]) != 255) return;
    if (enemy_rows[ids[i]].kind == EK_BOSS) boss = 1;
  }
  if (*n >= enemy_cap() || !rnd_pct(D_LUCKY_APPEAR_PCT)) return;
  if (boss && *n >= 2) return;
  ids[(*n)++] = lucky_mons[pick(N_LUCKY)];
}

static void place_enemies(const u8 *ids, u8 n) {
  static const u8 x1[1] = { 176 }, x2[2] = { 144, 208 }, x3[3] = { 128, 176, 224 }, xb[2] = { 108, 160 };
  u8 i, j, k = 0, boss_n = 0, others;
  const u8 *xs;
  u8 nx;
  Foe t;
  for (i = 0; i < n; ++i) if (enemy_rows[ids[i]].kind == EK_BOSS) ++boss_n;
  others = n - boss_n;
  if (boss_n) { xs = xb; nx = 2; }
  else if (others <= 1) { xs = x1; nx = 1; }
  else if (others == 2) { xs = x2; nx = 2; }
  else { xs = x3; nx = 3; }
  for (i = 0; i < n; ++i) {
    make_enemy(&b_e[i], ids[i]);
    if (foe_boss(&b_e[i])) b_e[i].sx = D_BOSS_POS_X + 84;
    else { b_e[i].sx = xs[MIN(k, nx - 1)]; ++k; }
  }
  b_ne = n;
  for (i = 0; i + 1 < n; ++i)
    for (j = 0; j + 1 < n - i; ++j)
      if (b_e[j].sx > b_e[j + 1].sx) { t = b_e[j]; b_e[j] = b_e[j + 1]; b_e[j + 1] = t; }
  for (i = 0; i < n; ++i) b_e[i].idx = i;
}

static u8 first_targetable(u8 from) {
  u8 k, i;
  for (k = 0; k < b_ne; ++k) {
    i = (from + k) % b_ne;
    if (foe_targetable(&b_e[i])) return i;
  }
  return 255;
}

u8 battle_set_target(u8 i) {
  if (i < b_ne && foe_targetable(&b_e[i])) {
    b_target = i;
    ev_post(EV_TARGET, i, 0, 0, 0, 0);
    return 1;
  }
  return 0;
}

static void retarget(void) {
  u8 t;
  if (!b_ne) return;
  if (b_target >= b_ne || !foe_targetable(&b_e[b_target])) {
    t = first_targetable(b_target);
    if (t != 255 && t != b_target) { b_target = t; ev_post(EV_TARGET, t, 0, 0, 0, 0); }
  }
}

static void request_talk(u16 id, u8 who, u16 wait) {
  b_talk_req = id;
  b_talk_who = who;
  talk_wait = wait == 0xffff ? D_SPECIAL_NAME_FRAMES : wait;
}

static u8 has_boss(void) {
  u8 i;
  for (i = 0; i < b_ne; ++i) if (foe_boss(&b_e[i])) return 1;
  return 0;
}

static void start_stage(void) {
  u8 ids[ENEMY_MAX + 1], n, i;
  if (b_mode == BM_TUTORIAL) {
    ids[0] = ids[1] = MON_TUTORIAL;
    n = b_stage >= T_TUTORIAL_MULTI_STAGE ? 2 : 1;
    place_enemies(ids, n);
    for (i = 0; i < n; ++i) b_e[i].hp = b_e[i].hp_max = D_TUT_ENEMY_HP;
    b_tut_protect = 1;
    for (i = 0; i < SLOTS; ++i) b_w[i].pos = 0;
  } else if (b_mode == BM_TRAINING) {
    ids[0] = MON_KAKASHI;
    n = 1;
    place_enemies(ids, n);
    b_e[0].hp = b_e[0].hp_max = TRAINING_HP;
    b_e[0].base = b_e[0].count = TRAINING_COUNT;
  } else {
#ifdef FORCE_HOOKS
    if (force_wave[0] != MON_NONE && !b_mirage) {
      for (n = 0; n < ENEMY_MAX && force_wave[n] != MON_NONE; ++n) ids[n] = force_wave[n];
      mid_def = MON_NONE;
    } else
#endif
    {
      n = pick_formation(ids);
      add_lucky(ids, &n);
    }
    place_enemies(ids, n);
  }
  b_hero_state = HS_NORMAL;
  b_hero_timer = 0;
  b_guard_cool = 0;
  guard_press = 0xc000;
  b_turn = TURN_PLAYER;
  b_step_kind = ST_NONE;
  b_attacker = 255;
  aq_n = 0;
  b_npending = 0;
  b_npopups = 0;
  in_skill = 255; in_guard = 0; in_meat = 0; in_next = 0;
  blind_mask = 0;
  b_lock_slot = b_lock_slot2 = 255;
  b_lock_turns = 0;
  lock_open_now = 0;
  opening_lock = 255;
  for (i = 0; i < b_ne; ++i)
    if (b_e[i].ability == BA_LOCK && b_e[i].sp_atk > 0 && b_e[i].sp_every > 0) { opening_lock = i; break; }
  opening_beg = 255;
  beg_said = 0;
  for (i = 0; i < b_ne; ++i)
    if (b_e[i].mon == MON_115 && b_e[i].sp_every > 0) { opening_beg = i; break; }
  b_turns_stage = 0;
  b_target = first_targetable(0);
  if (b_target == 255) b_target = 0;
  if (has_boss()) { b_phase = PH_WARN; b_phase_timer = D_BOSS_WARN_FRAMES; }
  else { b_phase = PH_SLIDE; b_phase_timer = D_SLIDE_IN_FRAMES; }
  mark_seen();
  ev_post(EV_STAGE_START, 0, b_stage, has_boss(), 0, 0);
}

static void start_run(void) {
  u8 i;
  b_hp_max = calc_hp_max();
  b_hp = b_hp_max;
  b_st = b_st_max;
  b_focus = 0;
  if (slot_empty(b_focus)) b_focus = next_slot(b_focus);
  b_buff_free = b_buff_power = b_buff_crit = 0;
  b_combo = 0;
  b_nhist = 0;
  b_coins = 0;
  b_nitems = 0;
  b_reward_got = 0;
  for (i = 0; i < N_MAT; ++i) b_mats[i] = 0;
  for (i = 0; i < N_MON; ++i) b_kills[i] = 0;
  memset(b_seen, 0, sizeof(b_seen));
  memset(b_sp_done, 0, sizeof(b_sp_done));
  b_men_kills = 0;
  b_boss_specials = 0;
  b_turns_total = 0;
  b_meat_gifts = 0;
  b_talk = 0; b_talk_req = 0; b_form_who = 255; after_talk = -1;
  b_stage = 1;
#ifdef FORCE_HOOKS
  if (b_mirage && force_mwave > 1 && force_mwave <= b_stage_count) b_stage = force_mwave;
#endif
  b_last_special = 255;
  start_stage();
}

void battle_start(u8 area, u8 men, const Loadout *lo) {
  u8 i, t;
  ev_head = ev_tail = 0;
  b_area = area;
  b_men = men;
  b_mirage = area == AR_MIRAGE;
  if (b_mode == BM_TUTORIAL) b_stage_count = D_TUTORIAL_STAGES;
  else if (b_mode == BM_TRAINING) b_stage_count = 1;
  else if (b_mirage) b_stage_count = MIRAGE_WAVES;
  else if (is_money()) b_stage_count = T_MONEY_STAGES;
  else if (is_castle()) b_stage_count = D_CASTLE_STAGES;
  else if (is_secret()) b_stage_count = D_SECRET_STAGES;
  else if (is_treasure()) b_stage_count = D_TREASURE_STAGES;
  else b_stage_count = D_MEN_STAGES;
  for (i = 0; i < N_MIRAGE_KIND; ++i) {
    b_mirage_buff[i] = 0;
    for (t = 0; t < MIRAGE_TIERS; ++t) mirage_stock[i][t] = mirage_rewards[i].stock[t];
  }
  load_estats();
  equip(lo);
  start_run();
}

/* ------------------------------------------------------------------ input */
void battle_press_skill(u8 slot) { in_skill = slot; }
void battle_press_guard(void) { in_guard = 1; }
void battle_press_meat(void) { in_meat = 1; }
void battle_press_next(void) { in_next = 1; }

u8 battle_can_command(void) {
  return b_phase == PH_FIGHT && b_turn == TURN_PLAYER && b_hero_state == HS_NORMAL;
}

static void force_attack(u8 i) {
  Foe *e = &b_e[i];
  e->count = 0;
  attack_queue[0] = i; aq_n = 1;
  b_attacker = 255;
  b_turn = TURN_ENEMY;
  b_step_kind = ST_ZERO;
  b_step_timer = D_ENEMY_ZERO_FRAMES;
  ev_post(EV_ENEMY_PHASE, i, 1, 0, 0, 0);
}

void battle_force_attack(u8 i) { if (i < b_ne && foe_targetable(&b_e[i])) force_attack(i); }

static void open_lock(void) {
  Foe *e;
  if (opening_lock >= b_ne) return;
  e = &b_e[opening_lock];
  opening_lock = 255;
  if (!foe_targetable(e)) return;
  e->attacks = MAX(e->attacks, e->sp_every);
  lock_open_now = 1;
  force_attack(e->idx);
  b_step_timer = D_ENEMY_ZERO_FRAMES + T_OPEN_LOCK_WAIT;
}

static void open_beg(void) {
  Foe *e;
  if (opening_beg >= b_ne) return;
  e = &b_e[opening_beg];
  opening_beg = 255;
  if (!foe_targetable(e)) return;
  e->attacks = MAX(e->attacks, e->sp_every);
  force_attack(e->idx);
  b_step_timer = D_ENEMY_ZERO_FRAMES + T_OPEN_LOCK_WAIT;
}

static u8 stance_frames(void) { return rules()->guard; }

static void guard(u8 meat) {
  guard_press = b_time;
  b_guard_meat = meat;
  b_hero_state = HS_GUARD;
  b_hero_timer = stance_frames();
  ev_post(EV_GUARD, 0, meat, 0, 0, 0);
}

u8 meat_asker(void) {
  Foe *e;
  if (b_turn != TURN_ENEMY || b_attacker == 255 || attack_over) return 255;
  e = &b_e[b_attacker];
  if (!foe_targetable(e)) return 255;
  return (e->mon == MON_115 && foe_special_next(e)) ? b_attacker : 255;
}

static void eat_meat(void) {
  u16 n = D_MEAT_ST_RECOVER;
  u8 i;
  for (i = 0; i < 2; ++i) if (effects[b_fx[i]].kind == FK_POWER) n = mul_pct16(n, effects[b_fx[i]].v2);
  b_st = (u8)MIN(b_st + MAX(n, 1) + fx_sum(FK_ST_HEAL_UP, 1), b_st_max);
  b_hero_state = HS_EAT;
  b_hero_timer = D_MEAT_EAT_FRAMES;
  b_turn = TURN_HERO;
  ev_post(EV_MEAT, 0, b_st, 0, 0, 0);
}

static Hit cur_hit;

static void hero_update(void) {
  u8 s, g, m;
  if (b_npending && b_time >= b_pending_next) {
    u8 i;
    cur_hit = b_pending[0];
    for (i = 1; i < b_npending; ++i) b_pending[i - 1] = b_pending[i];
    --b_npending;
    hit_enemy(&cur_hit);
    b_pending_next = b_time + 2;
  }
  if (b_guard_cool) --b_guard_cool;
  if (b_hero_timer) {
    --b_hero_timer;
    if (!b_hero_timer) {
      if (b_hero_state == HS_GUARD) b_guard_cool = D_GUARD_COOLDOWN_FRAMES;
      b_hero_state = HS_NORMAL;
      ev_post(EV_HERO_NORMAL, 0, 0, 0, 0, 0);
    }
  }
  s = in_skill; g = in_guard; m = in_meat;
  in_skill = 255; in_guard = 0; in_meat = 0;
  if (b_hero_state != HS_NORMAL) return;
  if (s != 255 && b_turn == TURN_PLAYER) { use_skill(s); return; }
  if (g) {
    if (b_guard_cool == 0) guard(0);
    return;
  }
  if (m) {
    if (meat_asker() != 255) { if (b_guard_cool == 0) guard(1); }
    else if (b_turn == TURN_PLAYER) eat_meat();
  }
}

/* ------------------------------------------------------------------ enemy turn */
static void enemies_turn(void) {
  u8 i, para, d = 2;
  Foe *e;
  ++b_turns_total;
  ++b_turns_stage;
  aq_n = 0;
  if (b_lock_turns) {
    if (--b_lock_turns == 0) {
      ev_post(EV_UNLOCK, 0, b_lock_slot, b_lock_slot2, 0, 0);
      b_lock_slot = b_lock_slot2 = 255;
    }
  }
  blind_mask = 0;
  for (i = 0; i < b_ne; ++i) {
    e = &b_e[i];
    if (!foe_targetable(e)) continue;
    if (e->ail[AIL_POISON]) {
      add_popup(i, MAX(1, mul_pct(e->hp_max, D_POISON_PCT)), d, PK_POISON);
      --e->ail[AIL_POISON];
      d += D_DAMAGE_POPUP_FRAMES / 2;
    }
    if (e->ail[AIL_BLIND]) { --e->ail[AIL_BLIND]; blind_mask |= 1 << i; }
    para = e->ail[AIL_PARALYZE] > 0;
    if (para) --e->ail[AIL_PARALYZE];
    if (e->stun_turns) {
      if (--e->stun_turns == 0) stun_end(e);
      continue;
    }
    if (para) continue;
    if (e->count && !b_freeze) --e->count;
    if (e->count == 0) {
      if (foe_lucky(e)) {
        e->fleeing = D_DEFEAT_BLINK_FRAMES;
        ev_post(EV_FLEE, i, 0, 0, 0, 0);
      } else attack_queue[aq_n++] = i;
    }
  }
  ev_post(EV_TURN_END, 0, 0, 0, 0, 0);
  if (!aq_n) {
    b_turn = TURN_PLAYER;
    ev_post(EV_TURN_PLAYER, 0, 0, 0, 0, 0);
  } else {
    b_turn = TURN_ENEMY;
    b_step_kind = ST_ZERO;
    b_step_timer = D_ENEMY_ZERO_FRAMES;
    ev_post(EV_ENEMY_PHASE, attack_queue[0], aq_n, 0, 0, 0);
  }
}

static void start_hit(Foe *e) {
  (void)e;
  b_step_kind = ST_BANG;
  b_step_timer = D_ENEMY_BANG_FRAMES;
}

static void start_next_attacker(void) {
  Foe *e;
  u8 i;
  while (aq_n) {
    i = attack_queue[0];
    e = &b_e[i];
    if (foe_targetable(e)) {
      if (e->mon == MON_115 && foe_special_next(e) && !beg_said) {
        beg_said = 1;
        b_step_kind = ST_ZERO;
        b_step_timer = T_MEAT_BEG_FRAMES;
        ev_post(EV_MEAT_BEG, i, 0, 0, 0, 0);
        return;
      }
      for (i = 1; i < aq_n; ++i) attack_queue[i - 1] = attack_queue[i];
      --aq_n;
      beg_said = 0;
      b_attacker = e->idx;
      e->bang = 1;
      attack_over = 0;
      e->hits_guarded = 0;
      e->hits_just = 0;
      start_hit(e);
      ev_post(EV_ENEMY_BANG, e->idx, foe_special_next(e), lock_open_now, 0, 0);
      return;
    }
    for (i = 1; i < aq_n; ++i) attack_queue[i - 1] = attack_queue[i];
    --aq_n;
  }
  b_attacker = 255;
  b_step_kind = ST_NONE;
  b_turn = TURN_PLAYER;
  ev_post(EV_TURN_PLAYER, 0, 0, 0, 0, 0);
}

static u8 guard_up(void) {
  return b_hero_state == HS_GUARD || b_guard_cool == D_GUARD_COOLDOWN_FRAMES;
}

static u8 judge_guard(u8 just_window, u8 no_guard) {
  s16 since;
  const DiffRule *dr = rules();
  if (!guard_up() || no_guard) return GD_NONE;
  since = (s16)(hit_time - guard_press);
  if (since < 0) return GD_NONE;
  if (since < dr->miracle) return GD_MIRACLE;
  return since < just_window ? GD_JUST : GD_GUARD;
}

static u8 just_window_of(void) {
  const DiffRule *dr = rules();
  return dr->miracle + dr->just;
}

static u32 miracle_heal(void) {
  u32 h = mul_pct(D_MIRACLE_HEAL, rules()->heal);
  u8 i;
  if (b_mirage) h = MAX(1, mul_pct(b_hp_max, T_MIRAGE_MIRACLE_HEAL_PCT));
  for (i = 0; i < 2; ++i) if (effects[b_fx[i]].kind == FK_MIRACLE_X) h = mul_pct(h, effects[b_fx[i]].v1);
  return h;
}

static void heal(u32 n) {
  b_hp = MIN(b_hp + n, b_hp_max);
}

static u16 stun_chance(Foe *e, u16 pct) {
  u16 r = 100;
  if (e->mon == MON_117 || forms_index(e->mon) != 255) r = T_SECRET_STUN_PCT;
  return r != 100 ? (u16)((u32)pct * 100 / r) : pct;
}

static u32 guard_cut(Foe *e, u32 atk) {
  u8 i;
  const Effect *f;
  for (i = 0; i < 2; ++i) {
    f = &effects[b_fx[i]];
    if (f->kind != FK_GUARD_CUT) continue;
    if (f->v2 == GC_ELEMENT && e->element != f->v3) continue;
    if (f->v2 == GC_RACE && e->race != f->v3) continue;
    if (f->v2 == GC_SCENE && areas[b_area].scenery != f->v3) continue;
    atk = mul_pct(atk, 100 - f->v1);
  }
  return atk;
}

static void guard_effects(u8 result, Foe *e, u32 atk) {
  u8 miracle = result == GD_MIRACLE, i;
  const Effect *f;
  for (i = 0; i < 2; ++i) {
    f = &effects[b_fx[i]];
    if (f->kind == FK_REFLECT) add_popup(e->idx, mul_pct(atk, miracle ? f->v2 : f->v1), D_REFLECT_DELAY_FRAMES, PK_REFLECT);
    else if (f->kind == FK_GUARD_STUN) { if (rnd_pct(stun_chance(e, miracle ? f->v2 : f->v1))) enemy_stun_now(e); }
  }
}

/* returns damage; *res = guard result */
static u32 take_hit(Foe *e, u32 atk, u8 just_window, u8 no_guard, u8 *res) {
  u8 i, result;
  u32 dmg;
  const Effect *f;
  for (i = 0; i < 2; ++i) {
    f = &effects[b_fx[i]];
    if (f->kind == FK_JUST_WIDE) just_window = (u8)(((u16)just_window * f->v1 + 50) / 100);
  }
  result = judge_guard(just_window, no_guard);
  if (result == GD_GUARD || result == GD_JUST) {
    for (i = 0; i < 2; ++i) {
      f = &effects[b_fx[i]];
      if (f->kind == FK_AUTO_MG && rnd_pct(f->v1)) { result = GD_MIRACLE; break; }
    }
  }
  dmg = atk;
  switch (result) {
  case GD_MIRACLE:
    dmg = 0;
    heal(miracle_heal());
    b_st = b_st_max;
    break;
  case GD_JUST:
    dmg = mul_pct(guard_cut(e, atk), 100 - D_JUST_GUARD_CUT_PCT);
    b_st = MIN(b_st + D_JUST_GUARD_ST, b_st_max);
    break;
  case GD_GUARD:
    dmg = mul_pct(guard_cut(e, atk), 100 - shield_guard_pct());
    break;
  }
  if (result != GD_NONE) {
    if (result != GD_GUARD) guard_effects(result, e, atk);
    for (i = 0; i < 2; ++i) {
      f = &effects[b_fx[i]];
      if (f->kind == FK_REVENGE && rnd_pct(f->v1)) add_popup(e->idx, mul_pct(atk, f->v2), D_REFLECT_DELAY_FRAMES, PK_REFLECT);
    }
  }
  for (i = 0; i < 2; ++i) {
    f = &effects[b_fx[i]];
    if (f->kind == FK_HAISUI && b_hp <= mul_pct(b_hp_max, f->v1)) dmg = mul_pct(dmg, 100 - f->v2);
  }
  if (dmg < D_DAMAGE_MIN && result != GD_MIRACLE) dmg = D_DAMAGE_MIN;
  if (b_mirage) { u32 cap = MAX(1, mul_pct(b_hp_max, T_MIRAGE_HIT_CAP_PCT)); if (dmg > cap) dmg = cap; }
  if (dmg > 0 && result == GD_NONE && b_combo > 0) b_combo = 0;
  if (dmg > 0 && dmg >= b_hp && b_hp > 1) {
    for (i = 0; i < 2; ++i) {
      f = &effects[b_fx[i]];
      if (f->kind == FK_FUNBARI && rnd_pct(f->v1)) { dmg = b_hp - 1; ev_post(EV_FUNBARI, 0, 0, 0, 0, 0); break; }
    }
  }
  *res = result;
  return dmg;
}

static u8 other_lock_slot(void) {
  u8 rest[3], n = 0, s;
  for (s = 0; s < SLOTS; ++s) if (s != b_focus && !slot_empty(s)) rest[n++] = s;
  return n ? rest[rnd_range(0, n - 1)] : 255;
}

static u8 attack_ability(Foe *e, u8 special) {
  static u8 ab_cycle;
  if (e->ability != BA_ALL) return e->ability;
  ++ab_cycle;
  if (special) return (b_boss_specials & 1) ? BA_NONE : BA_LOCK;
  return (ab_cycle & 1) ? BA_DRAIN : BA_STDRAIN;
}

static void barrier_change(Foe *e) {
  static const u8 cyc[3] = { WT_SPEAR, WT_SWORD, WT_HAMMER };
  u8 i;
  if (e->mon != MON_117 || !foe_targetable(e)) return;
  for (i = 0; i < 3 && cyc[i] != e->weak; ++i) ;
  e->weak = cyc[(i + 1) % 3];
  ev_post(EV_BARRIER, e->idx, e->weak, 0, 0, 0);
}

/* the web: no gift once the run holds ITEM_MAX + MEAT_GIFT_EXTRA_MAX items (drops stop at ITEM_MAX) */
#if D_ITEM_MAX + T_MEAT_GIFT_EXTRA_MAX > ITEMS_RUN_MAX
#error ITEMS_RUN_MAX is below the meat gift cap
#endif
static u8 meat_gift(Foe *e) {
  u8 type, id;
  RunItem *it;
  (void)e;
  if (b_mode || b_nitems >= D_ITEM_MAX + T_MEAT_GIFT_EXTRA_MAX) return ITEM_NONE;
  id = surface_pick(1, 0, 0, &type);
  it = &b_items[b_nitems++];
  it->item = id;
  if (type == WT_SHIELD) it->nsk = 0;
  else roll_skills(type, it->sk, &it->nsk);
  ++b_meat_gifts;
  return id;
}

static void attack_hero(void) {
  Foe *e = &b_e[b_attacker];
  u8 special = b_special_now, ab, lock, lock_soft, opening, result, locked = 0;
  u32 atk, dmg, gain;
  u32 hp0 = b_hp;
  u8 st0 = b_st;
  e->bang = 0;
  if ((blind_mask & (1 << e->idx)) && rnd_pct(D_BLIND_MISS_PCT)) {
    if (special) e->attacks = 0; else ++e->attacks;
    e->count = base_count(e);
    ev_post(EV_MISS, e->idx, special, 0, 0, 0);
    barrier_change(e);
    attack_over = 1;
    return;
  }
  attack_sab = attack_ability(e, special);
  ab = attack_sab;
  lock = special && ab == BA_LOCK;
  lock_soft = lock && (e->ability == BA_LOCK || e->ability == BA_ALL) && !lock_open_now;
  if (special && !lock) atk = e->sp_atk;
  else if (lock && e->lock_atk >= 0) atk = (u32)e->lock_atk;
  else atk = e->atk;
  opening = lock && lock_open_now;
  if (opening) atk = 0;
  dmg = take_hit(e, atk, just_window_of(), lock && !lock_soft, &result);
  if (opening) dmg = 0;
  b_hp = dmg >= b_hp ? 0 : b_hp - dmg;
  if (b_hp > b_hp_max) b_hp = b_hp_max;
  if (b_mode && b_hp == 0) b_hp = 1;       /* tutorial / training: the hero cannot fall */
  if (ab == BA_DRAIN && dmg > 0) {
    gain = mul_pct(dmg, D_DRAIN_PCT);
    if (b_mirage) gain = MAX(1, mul_pct(gain, T_MIRAGE_DRAIN_PCT));
    gain = MIN(gain, e->hp_max - e->hp);
    e->hp += gain;
    ev_post(EV_DRAIN, e->idx, special, 0, 0, gain);
  }
  if (ab == BA_STDRAIN && b_st > 0 && (result == GD_NONE || result == GD_GUARD)) {
    b_st = b_st > D_ST_DRAIN ? b_st - D_ST_DRAIN : 0;
    ev_post(EV_STDRAIN, e->idx, special, b_st, 0, 0);
  }
  if (result != GD_NONE) ++e->hits_guarded;
  if (result >= GD_JUST) ++e->hits_just;
  e->count = base_count(e);
  if (special) {
    e->attacks = 0;
    ++b_boss_specials;
    if (lock && (!lock_soft || result != GD_MIRACLE)) {
      b_lock_slot = b_focus;
      b_lock_slot2 = (e->awake && e->mon == MON_117) ? other_lock_slot() : 255;
      b_lock_turns = D_LOCK_TURNS;
      locked = 1;
      ev_post(EV_LOCK, e->idx, b_lock_slot, b_lock_slot2, 0, 0);
    } else if (lock) ev_post(EV_LOCK_GUARDED, e->idx, 0, 0, 0, 0);
    lock_open_now = 0;
  } else ++e->attacks;
  ev_post(EV_ENEMY_HIT, e->idx, result, (u8)((special && !lock ? 1 : 0) | (lock ? 2 : 0) | (locked ? 4 : 0) | (opening ? 8 : 0)
          | (b_guard_meat && result != GD_NONE ? 16 : 0)), (u16)(b_hp > hp0 ? b_hp - hp0 : 0), dmg);
  if (b_st > st0 && result >= GD_JUST) ev_post(EV_ST_GAIN, 0, b_st, b_st - st0, 0, 0);
  if (special && b_guard_meat && result != GD_NONE && e->mon == MON_115) {
    u8 g = meat_gift(e);
    ev_post(EV_MEAT_GIFT, e->idx, g, result, 0, 0);
  }
  barrier_change(e);
  attack_over = 1;
}

static void enemies_update(void) {
  u8 i, normal = 0;
  Foe *e;
  for (i = 0, e = b_e; i < b_ne; ++i, ++e) {
    if (e->dying) {
      if (i == b_form_who || b_talk_req == TALK_DEFEAT || b_talk_req == TALK_MAOU_DEFEAT) continue;
      if (--e->dying == 0) { e->gone = 1; ev_post(EV_GONE, i, 0, 0, 0, 0); }
    } else if (e->fleeing) {
      if (--e->fleeing == 0) { e->gone = 1; ev_post(EV_GONE, i, 0, 0, 0, 0); }
    }
  }
  for (i = 0, e = b_e; i < b_ne && !normal; ++i, ++e)
    if ((foe_targetable(e) && !foe_lucky(e)) || i == b_form_who) normal = 1;
  if (!normal) {
    for (i = 0, e = b_e; i < b_ne; ++i, ++e)
      if (foe_lucky(e) && foe_targetable(e) && e->count > 1) e->count = 1;
  }
  if (b_turn != TURN_ENEMY) return;
  if (b_step_timer) --b_step_timer;
  if (b_step_timer) return;
  switch (b_step_kind) {
  case ST_ZERO:
    start_next_attacker();
    break;
  case ST_BANG:
    b_step_kind = ST_BLINK;
    b_step_timer = D_ENEMY_BLINK_FRAMES * 2 * D_ENEMY_BLINK_TIMES;
    ev_post(EV_ENEMY_BLINK, b_attacker, 0, 0, 0, 0);
    break;
  case ST_BLINK:
    b_step_kind = ST_WINDUP;
    b_step_timer = D_ENEMY_WINDUP_FRAMES;
    b_special_now = foe_special_next(&b_e[b_attacker]);
    ev_post(EV_ENEMY_WINDUP, b_attacker, b_special_now, 0, 0, 0);
    break;
  case ST_WINDUP:
    ev_post(EV_ENEMY_STRIKE, b_attacker, b_special_now, 0, 0, 0);
    if (b_guard_late) { b_step_kind = ST_HOLD; b_step_timer = b_guard_late; }
    else { hit_time = b_time; b_step_kind = ST_RESOLVE; b_step_timer = 1; }
    break;
  case ST_HOLD:
    hit_time = b_time; b_step_kind = ST_RESOLVE; b_step_timer = 1;
    break;
  case ST_RESOLVE:
    attack_hero();
    b_step_kind = ST_GAP;
    b_step_timer = D_ENEMY_CHAIN_GAP_FRAMES;
    break;
  case ST_GAP:
    start_next_attacker();
    break;
  }
}

/* ------------------------------------------------------------------ talks and forms */
static u8 talk_ready(void) {
  return b_turn != TURN_ENEMY && b_hero_state == HS_NORMAL && !b_npending && !b_npopups;
}

static void form_swap(void) {
  Foe *e = &b_e[b_form_who];
  u8 f = forms_index(e->mon), next = maou_forms[f + 1];
  s16 sx = e->sx;
  u8 idx = e->idx;
  if (b_mirage && b_stage < b_stage_count) { ++b_stage; b_turns_stage = 0; }   /* the tower: each form is a WAVE */
  make_enemy(e, next);
  e->sx = sx;
  e->idx = idx;
}

static void form_talk_done(void) {
  s8 i = after_talk;
  after_talk = -1;
  if (i < 0 || i >= b_ne) return;
  ev_post(EV_BARRIER, i, b_e[i].weak, 1, 0, 0);
  if (forms_index(b_e[i].mon) == 2 && foe_targetable(&b_e[i])) {
    force_attack(i);
    b_step_timer += T_MAOU_FINAL_ATTACK_WAIT;
  }
}

static void form_frame(void) {
  u16 t = form_time, t1, t2, t3, t4, t5;
  u8 fm;
  in_skill = 255; in_guard = in_meat = 0;
  ++form_time;
  t1 = D_FORM_THROES_FRAMES;
  t2 = t1 + D_FORM_PULSE_FRAMES;
  t3 = t2 + D_FORM_DARK_FRAMES;
  t4 = t3 + D_FORM_BLACK_FRAMES;
  t5 = t4 + D_FORM_RISE_FRAMES;
  if (t >= t1 && t < t2) {
    if (--form_beat == 0) {
      form_gap = form_gap > D_FORM_BEAT_MIN + D_FORM_BEAT_STEP ? form_gap - D_FORM_BEAT_STEP : D_FORM_BEAT_MIN;
      form_beat = form_gap;
      ev_post(EV_FORM_BEAT, b_form_who, 0, 0, 0, 0);
    }
  }
  if (t == t2) ev_post(EV_FORM_DARK, b_form_who, 0, 0, 0, 0);
  else if (t == t3) { form_swap(); mark_seen(); ev_post(EV_FORM_SWAP, b_form_who, b_e[b_form_who].mon, 0, 0, 0); }
  else if (t == t4) ev_post(EV_FORM_RISE, b_form_who, forms_index(b_e[b_form_who].mon), 0, 0, 0);
  else if (t >= t5) {
    ev_post(EV_FORM_END, b_form_who, 0, 0, 0, 0);
    after_talk = b_form_who;
    fm = forms_index(b_e[b_form_who].mon);
    request_talk(fm == 1 ? TALK_MAOU2 : TALK_MAOU3, maou_forms[0], 0);
    b_form_who = 255;
    b_phase = PH_FIGHT;
  }
}

static void start_form(void) {
  b_phase = PH_FORM;
  form_time = 0;
  form_gap = D_FORM_BEAT_FIRST + D_FORM_BEAT_STEP;
  form_beat = 1;
  ev_post(EV_FORM_START, b_form_who, 0, 0, 0, 0);
}

static void show_talk(void) {
  b_talk_text = b_talk_req;
  b_talk = 1;
  b_talk_req = 0;
  talk_timer = 0;
  b_phase = PH_TALK;
  ev_post(EV_TALK, 0, b_talk_who, 0, b_talk_text, 0);
}

static void talk_frame(void) {
  u8 next = in_next;
  in_next = 0; in_skill = 255; in_guard = in_meat = 0;
  ++talk_timer;
  if (next && talk_timer >= D_TALK_MIN_FRAMES) {
    ev_post(EV_TALK_END, 0, 0, 0, b_talk_text, 0);
    b_talk = 0;
    b_phase = PH_FIGHT;
    if (after_talk >= 0) form_talk_done();
  }
}

/* called by battle_hit.c when an enemy reaches 0 HP: talks / form change */
void foe_killed(Foe *e) {
  u8 f = forms_index(e->mon);
  if (awaken_boss(e->mon)) request_talk(TALK_DEFEAT, MON_117, 0xffff);
  if (f == 2) request_talk(TALK_MAOU_DEFEAT, maou_forms[0], 0xffff);
  if (f != 255 && f < 2) {
    e->dying = D_DEFEAT_BLINK_FRAMES;
    e->bang = 0;
    b_form_who = e->idx;
    ++b_kills[e->mon];
    ev_post(EV_DEFEAT, e->idx, 1, 0, 0, 0);
    retarget();
  } else {
    ++b_men_kills;
    start_dying(e);
  }
}

void awaken_talk(Foe *e) {
  (void)e;
  request_talk(TALK_AWAKEN, MON_117, 0xffff);
}

/* ------------------------------------------------------------------ mirage chests */
static void mirage_roll_offer(void) {
  u8 n = 3, i, t, k, kinds[N_MIRAGE_KIND], nk, used[N_MIRAGE_KIND], tier;
  u16 roll = rnd_range(1, 100), w[MIRAGE_TIERS], total, hit, at, d;
  static const u8 rolls[3][2] = { { 3, 60 }, { 4, 30 }, { 5, 10 } };
  for (i = 0; i < 3; ++i) { n = rolls[i][0]; if (roll <= rolls[i][1]) break; roll -= rolls[i][1]; }
  if (n > 3) n = 3;   /* the SFC screen shows up to 3 chests */
  for (k = 0; k < N_MIRAGE_KIND; ++k) used[k] = 0;
  /* at = (stage-1)/(stage_count-2) * (tiers-1), in 1/100 */
  at = (u16)((u32)(b_stage - 1) * 100 * (MIRAGE_TIERS - 1) / MAX(b_stage_count - 2, 1));
  if (at > 100 * (MIRAGE_TIERS - 1)) at = 100 * (MIRAGE_TIERS - 1);
  b_mirage_offer_n = 0;
  for (i = 0; i < n; ++i) {
    total = 0;
    for (t = 0; t < MIRAGE_TIERS; ++t) {
      u8 free = 0;
      for (k = 0; k < N_MIRAGE_KIND; ++k) if (!used[k] && mirage_stock[k][t]) free = 1;
      d = at > t * 100 ? at - t * 100 : t * 100 - at;
      w[t] = free ? MAX(1, 100 - (u16)((55UL * d + 50) / 100)) : 0;
      total += w[t];
    }
    if (!total) break;
    hit = rnd_range(0, total - 1);
    tier = 0;
    for (t = 0; t < MIRAGE_TIERS; ++t) { if (hit < w[t]) { tier = t; break; } hit -= w[t]; }
    nk = 0;
    for (k = 0; k < N_MIRAGE_KIND; ++k) if (!used[k] && mirage_stock[k][tier]) kinds[nk++] = k;
    k = kinds[rnd_range(0, nk - 1)];
    used[k] = 1;
    b_mirage_offer[b_mirage_offer_n][0] = k;
    b_mirage_offer[b_mirage_offer_n][1] = tier;
    ++b_mirage_offer_n;
  }
  /* higher tiers first */
  for (i = 0; i + 1 < b_mirage_offer_n; ++i)
    for (k = 0; k + 1 < b_mirage_offer_n - i; ++k)
      if (b_mirage_offer[k][1] < b_mirage_offer[k + 1][1]) {
        u8 a = b_mirage_offer[k][0], b = b_mirage_offer[k][1];
        b_mirage_offer[k][0] = b_mirage_offer[k + 1][0]; b_mirage_offer[k][1] = b_mirage_offer[k + 1][1];
        b_mirage_offer[k + 1][0] = a; b_mirage_offer[k + 1][1] = b;
      }
}

static void next_wave(void) {
  u8 st0 = b_st;
  ++b_stage;
  b_st = (u8)MIN(b_st + fx_sum(FK_ROUND_ST, 1), b_st_max);
  start_stage();
  if (b_st > st0) ev_post(EV_ST_GAIN, 0, b_st, b_st - st0, 0, 0);
}

void battle_mirage_pick(u8 i) {
  u8 k, t;
  u16 v;
  u32 old;
  if (b_phase != PH_REWARD || i >= b_mirage_offer_n) return;
  k = b_mirage_offer[i][0];
  t = b_mirage_offer[i][1];
  --mirage_stock[k][t];
  v = mirage_rewards[k].v[t];
  if (k == MK_ST) b_st = (u8)MIN(b_st + v, b_st_max);
  else if (k == MK_HEAL) b_hp = MIN(b_hp + MAX(1, mul_pct(b_hp_max, v)), b_hp_max);
  else {
    b_mirage_buff[k] += v;
    if (k == MK_MAXHP) {
      old = b_hp_max;
      b_hp_max = calc_hp_max();
      b_hp = MIN(b_hp + b_hp_max - old, b_hp_max);
    }
  }
  b_mirage_offer_n = 0;
  next_wave();
}

/* ------------------------------------------------------------------ frame */
static void fight_frame(void) {
  u8 i, left = 0;
  if (talk_wait) --talk_wait;
  if (b_form_who != 255 && talk_ready()) { start_form(); return; }
  if (b_talk_req && !talk_wait && talk_ready()) { show_talk(); return; }
  if (b_talk_req == TALK_START || b_talk_req == TALK_MAOU1) { in_skill = 255; in_guard = in_meat = 0; return; }
  ++b_time;
  hero_update();
  if (b_turn == TURN_HERO && b_hero_state == HS_NORMAL && !b_npending && !b_npopups && !b_talk_req && b_form_who == 255)
    enemies_turn();
  enemies_update();
  retarget();
  if (b_hp == 0) {
    b_phase = PH_GAMEOVER;
    ev_post(EV_GAMEOVER, 0, 0, 0, 0, 0);
    return;
  }
  { Foe *e = b_e; for (i = 0; i < b_ne; ++i, ++e) if (!e->gone) left = 1; }   /* (pointers: indexing Foe multiplies) */
  if (!left) {
    b_phase = PH_CLEAR;
    b_phase_timer = D_STAGE_CLEAR_FRAMES;
    ev_post(EV_STAGE_CLEAR, 0, b_stage, 0, 0, 0);
    return;
  }
  popup_update();
}

void battle_step(void) {
  u8 i;
  switch (b_phase) {
  case PH_WARN:
    in_skill = 255; in_guard = in_meat = 0;
    if (b_phase_timer) --b_phase_timer;
    if (!b_phase_timer) { b_phase = PH_SLIDE; b_phase_timer = D_SLIDE_IN_FRAMES; ev_post(EV_SLIDE, 0, 0, 0, 0, 0); }
    break;
  case PH_SLIDE:
    in_skill = 255; in_guard = in_meat = 0;
    if (b_phase_timer) --b_phase_timer;
    if (!b_phase_timer) {
      b_phase = PH_FIGHT;
      ev_post(EV_FIGHT, 0, 0, 0, 0, 0);
      for (i = 0; i < b_ne; ++i) {
        if (foe_boss(&b_e[i]) && awaken_boss(b_e[i].mon)) request_talk(TALK_START, MON_117, 0xffff);
        else if (foe_boss(&b_e[i]) && forms_index(b_e[i].mon) == 0) request_talk(TALK_MAOU1, maou_forms[0], 0xffff);
      }
      if (opening_lock != 255) open_lock();
      else if (opening_beg != 255) open_beg();
    }
    break;
  case PH_FIGHT:
    fight_frame();
    break;
  case PH_FORM:
    form_frame();
    break;
  case PH_TALK:
    talk_frame();
    break;
  case PH_CLEAR:
    in_skill = 255; in_guard = in_meat = 0;
    if (b_phase_timer) --b_phase_timer;
    if (!b_phase_timer) {
      if (b_stage < b_stage_count) {
        if (b_mirage) {
          mirage_roll_offer();
          if (b_mirage_offer_n) { b_phase = PH_REWARD; ev_post(EV_MIRAGE_REWARD, 0, b_mirage_offer_n, 0, 0, 0); break; }
        }
        next_wave();
      } else {
        b_phase = PH_VICTORY;
        ev_post(EV_VICTORY, 0, 0, 0, 0, 0);
      }
    }
    break;
  default:
    in_skill = 255; in_guard = in_meat = 0;
    break;
  }
}

/* ------------------------------------------------------------------ BGM choice */
#include "sound.h"
u8 battle_bgm(void) {
  u8 kind = stage_boss_kind(), i;
  if (b_mirage) {
    u8 m = b_ne ? b_e[0].mon : MON_NONE;
    if (m == MON_117) return BGM_PEKO;
    if (forms_index(m) == 2) return BGM_MAOU_FINAL;
    if (forms_index(m) != 255) return BGM_MAOU;
    return BGM_BOSS;
  }
  if (!kind) return BGM_ZAKO;
  for (i = 0; i < b_ne; ++i) {
    if (b_e[i].mon == MON_117) return BGM_PEKO;
    if (forms_index(b_e[i].mon) == 2) return BGM_MAOU_FINAL;
    if (forms_index(b_e[i].mon) != 255) return BGM_MAOU;
  }
  if (is_castle() && b_men == D_CASTLE_MEN && b_stage < b_stage_count) return BGM_BOSS;
  return BGM_BOSS;
}

#ifdef TEST_HOOKS
/* ------------------------------------------------------------------ test commands (sys.h DC_SPECIAL ..) */
static u8 dbg_calm(void) {   /* the player's turn with nothing in motion (no blow, death or flight under way) */
  u8 i;
  Foe *e;
  if (!battle_can_command() || b_npending || b_npopups || b_form_who != 255 || b_talk_req) return 0;
  for (i = 0, e = b_e; i < b_ne; ++i, ++e) if (!e->gone && (e->dying || e->fleeing)) return 0;
  return 1;
}

static void dbg_kill(Foe *e) {   /* as a blow that takes the last HP (popup_update) */
  e->hp = 0;
  foe_killed(e);
}

u8 dbg_battle(u8 c, u16 a0, u16 a1, u16 a2) {
  u8 i, pass, n = 0;
  u32 v = a1 | ((u32)a2 << 16);
  Foe *e;
  u16 *o = dbg_buf;
  switch (c) {
  case DC_SPECIAL:
    if (!dbg_calm()) return 0;
    i = a0 == 255 ? b_target : (u8)a0;
    if (i >= b_ne || !foe_targetable(&b_e[i])) break;
    e = &b_e[i];
    if (e->sp_every) e->attacks = e->sp_every;
    force_attack(i);
    dbg_res[0] = 1;
    dbg_res[1] = foe_special_next(e);
    break;
  case DC_CLEAR:
    if (!dbg_calm()) return 0;
    for (pass = 0; pass < 2; ++pass)
      for (i = 0, e = b_e; i < b_ne; ++i, ++e)
        if ((!a0 || (a0 & (1 << i))) && foe_targetable(e) && foe_lucky(e) == !pass) { dbg_kill(e); ++n; }
    dbg_res[0] = n;
    break;
  case DC_SETHP:
    if (a0 == 255) {
      b_hp = MIN(v, b_hp_max);
      v = b_hp;
    } else {
      if (a0 >= b_ne || !foe_targetable(&b_e[a0])) break;
      e = &b_e[a0];
      if (!v) { if (!dbg_calm()) return 0; dbg_kill(e); }
      else e->hp = MIN(v, e->hp_max);
      v = e->hp;
    }
    dbg_res[0] = (u16)v; dbg_res[1] = (u16)(v >> 16);
    ev_post(EV_DBG_HP, (u8)a0, 0, 0, 0, 0);
    break;
  case DC_FOE:
    if (a0 >= b_ne) break;
    e = &b_e[a0];
    o[0] = e->mon; o[1] = e->kind;
    o[2] = (u16)e->hp; o[3] = (u16)(e->hp >> 16);
    o[4] = (u16)e->hp_max; o[5] = (u16)(e->hp_max >> 16);
    o[6] = e->atk; o[7] = e->sp_atk; o[8] = e->sp_every; o[9] = e->attacks; o[10] = e->count;
    o[11] = e->dying | (e->gone << 8);
    o[12] = e->hits_guarded | (e->hits_just << 8);
    dbg_res[0] = b_stage; dbg_res[1] = b_ne; dbg_res[2] = b_turns_stage; dbg_res[3] = b_turns_total;
    break;
  case DC_CALM:
    if (!dbg_calm()) return 0;
    dbg_res[0] = b_turns_total; dbg_res[1] = b_stage; dbg_res[2] = b_turns_stage; dbg_res[3] = b_phase;
    break;
  case DC_TALK:                  /* the next battle_step closes it (talk_frame), as the A of battle_scene */
    if (b_phase != PH_TALK || talk_timer < D_TALK_MIN_FRAMES) return 0;
    dbg_res[0] = b_talk_text;
    in_next = 1;
    break;
  }
  return 1;
}
#endif
