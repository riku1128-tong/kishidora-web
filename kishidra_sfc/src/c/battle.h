/* battle rules (port of scripts/battle_core.gd, surface world) */
#ifndef BATTLE_H
#define BATTLE_H
#include "sys.h"

#define ENEMY_MAX 3
#define SLOTS 3
#define PENDING_MAX 24
#define POPUP_MAX 24
#define EVENT_MAX 48
#define ITEMS_RUN_MAX 20

enum { PH_WARN, PH_SLIDE, PH_FIGHT, PH_CLEAR, PH_VICTORY, PH_GAMEOVER, PH_FORM, PH_TALK, PH_INTRO, PH_REWARD };
enum { TURN_PLAYER, TURN_HERO, TURN_ENEMY };
enum { HS_NORMAL, HS_ATTACK, HS_CHARGE, HS_EAT, HS_GUARD };
enum { ST_NONE, ST_ZERO, ST_BANG, ST_BLINK, ST_WINDUP, ST_HOLD, ST_RESOLVE, ST_GAP, ST_CGAP };
enum { GD_NONE, GD_GUARD, GD_JUST, GD_MIRACLE };
enum { BLK_NONE, BLK_GUARD, BLK_JUST, BLK_MIRACLE };

/* events for the presentation (battle_view.c) */
enum {
  EV_STAGE_START = 1, EV_SLIDE, EV_FIGHT, EV_TURN_PLAYER, EV_TURN_END, EV_ENEMY_PHASE, EV_ENEMY_BANG, EV_ENEMY_BLINK,
  EV_ENEMY_WINDUP, EV_ENEMY_STRIKE, EV_ENEMY_HIT, EV_MISS, EV_GUARD, EV_HERO_NORMAL, EV_ATTACK, EV_CHARGE, EV_MEAT,
  EV_ST_SHORT, EV_LOCKED, EV_NUMBER, EV_DEFEAT, EV_GONE, EV_FLEE, EV_REWARD, EV_STUN, EV_STUN_RECOVER, EV_AILMENT,
  EV_COUNT_RESET, EV_SLOW, EV_SPECIAL_CANCEL, EV_GUARD_COUNTER, EV_DRAIN, EV_STDRAIN, EV_LOCK, EV_LOCK_GUARDED, EV_UNLOCK,
  EV_TALK, EV_TALK_END, EV_FORM_START, EV_FORM_BEAT, EV_FORM_DARK, EV_FORM_SWAP, EV_FORM_RISE, EV_FORM_END, EV_AWAKEN,
  EV_BARRIER, EV_STAGE_CLEAR, EV_VICTORY, EV_GAMEOVER, EV_ST_GAIN, EV_HP_COST, EV_MEAT_BEG, EV_MEAT_GIFT, EV_FUNBARI,
  EV_TARGET, EV_MIRAGE_REWARD, EV_HIT_ROLL, EV_GUARD_BLOCKED
};

typedef struct {
  u8 type, enemy, a, b;
  u16 c;
  u32 v;
} Ev;

typedef struct {
  u8 mon, kind, size, race, element, weak, ability, sp_fx;
  u32 hp, hp_max;
  u16 atk, sp_atk;
  s16 lock_atk;
  u8 sp_every, base, count, attacks;
  u16 stun, stun_need, stun_step, stun_cap;
  u8 stun_turns, stun_times;
  u8 dying, fleeing, gone, bang, mid, awake, miracle_next;
  u8 ail[N_AIL];
  u8 hits_guarded, hits_just, idx;
  s16 sx;
} Foe;

typedef struct { u8 item, nsk, pos; u8 sk[3]; u16 plus; } Slot;

typedef struct {
  u8 enemy, slot, skill, special, crit, hits, crit_st, hit_stun, hit_slow, ail, power, tset;
  u32 base;
  u16 crit_mult, atk, boost;
} Hit;

typedef struct {
  u8 enemy, crit, stun, reset, special, wtype, hit, st_gain, block, slow, ail, weak_wt, weak_el, kind;
  u16 at;
  u32 dmg;
} Popup;
#define PK_HIT 0
#define PK_POISON 1
#define PK_REFLECT 2
#define PK_KICK 3
#define PK_SHOUT 4

typedef struct { u8 item; u8 sk[3]; u8 nsk; } RunItem;   /* items picked up in this run */

/* loadout passed to battle_start */
typedef struct { Slot w[SLOTS]; u8 shield; u16 shield_plus; } Loadout;

extern u8 b_phase, b_turn, b_hero_state, b_step_kind;
extern u16 b_phase_timer, b_hero_timer, b_step_timer, b_time;
extern u8 b_area, b_men, b_stage, b_stage_count, b_target, b_focus;
extern u32 b_hp, b_hp_max;
extern u8 b_st, b_st_max;
extern u16 b_combo;
extern Slot b_w[SLOTS];
extern u8 b_shield;
extern u16 b_shield_plus;
extern Foe b_e[ENEMY_MAX];
extern u8 b_ne;
extern u8 b_buff_free, b_buff_power, b_buff_crit;
extern u8 b_lock_slot, b_lock_slot2, b_lock_turns;
extern u8 b_attacker;          /* 255 = none */
extern u8 b_special_now;
extern u16 b_guard_cool;
extern u8 b_guard_meat;
extern u32 b_coins;
extern RunItem b_items[ITEMS_RUN_MAX];
extern u8 b_nitems;
extern u8 b_mats[N_MAT];
extern u8 b_talk;              /* text id of the running talk (0 = none) */
extern u16 b_talk_text, b_talk_req;
extern u8 b_talk_who;          /* mon of the speaker or 255 = king */
extern u8 b_form_who;
extern u8 b_difficulty;
extern u8 b_guard_late;
extern u8 b_mirage, b_mirage_offer_n;
extern u8 b_mirage_offer[3][2];
extern u16 b_mirage_buff[N_MIRAGE_KIND];
extern u8 b_men_cleared;
extern u8 b_reward_got;
extern u16 b_turns_total;
extern u8 b_meat_gifts;
extern u8 b_history[3], b_nhist;
extern u8 b_last_special;      /* special id of the last attack (255 none) */
extern u16 b_kills[N_MON];

/* core */
void battle_start(u8 area, u8 men, const Loadout *lo);
void battle_step(void);
void battle_press_skill(u8 slot);
void battle_press_guard(void);
void battle_press_meat(void);
void battle_press_next(void);
void battle_mirage_pick(u8 i);
u8   battle_can_command(void);
u8   battle_set_target(u8 i);
u8   ev_pop(Ev *out);
void ev_post(u8 type, u8 enemy, u8 a, u8 b, u16 c, u32 v);
u8   foe_targetable(const Foe *e);
u8   foe_special_next(const Foe *e);
u8   skill_of(u8 slot);
u8   next_skill_of(u8 slot);
u8   skill_cost(u8 sk);
u8   is_locked(u8 slot);
u8   meat_asker(void);
u8   special_if(u8 slot);       /* special id that this slot's skill would finish, or 255 */
u16  combo_pct(u16 c);
u8   is_castle(void);
u8   is_secret(void);
u8   is_treasure(void);
const DiffRule *rules(void);
u8   fx_has(u8 kind);
s16  fx_sum(u8 kind, u8 which);
u8   item_rarity_of(u8 item);
void roll_skills(u8 wtype, u8 *out, u8 *n);
u8   gacha_candidate(u8 type, u8 rarity);
u8   surface_pick(u8 boss_like, u8 lucky, u8 sure, u8 *type);
u32  calc_hp_max(void);
u8   stage_boss_kind(void);
u8   battle_bgm(void);

/* battle_hit.c */
void use_skill(u8 slot);
void hit_enemy(Hit *h);
void popup_update(void);
void add_popup(u8 e, u32 dmg, u8 delay, u8 kind);
void give_reward(Foe *e);
void start_dying(Foe *e);
void enemy_stun_now(Foe *e);
void stun_end(Foe *e);
void awaken(Foe *e);
u8   find_special(void);
extern Hit b_pending[PENDING_MAX];
extern u8 b_npending;
extern Popup b_popups[POPUP_MAX];
extern u8 b_npopups;
extern u16 b_pending_next;
extern u8 b_fx[2];
extern u16 b_coin_pct, b_drop_pct;
extern u8 b_men_kills;
extern u8 b_stair;
extern u16 b_boss_specials;
#endif
