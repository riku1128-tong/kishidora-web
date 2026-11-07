/* table row types for data/gen/tables.c (field order = initializer order) */
#ifndef GTYPES_H
#define GTYPES_H

typedef struct { u8 type, rarity, element, crit, stun, effect; u16 atk; u8 def, evolve; } Item;   /* shield: atk = HP */
typedef struct { u8 wtype, cost, target, hits; u16 power, stun_pct; u8 buff, star, crit_pct; u16 crit_mult; u8 race; u16 race_pct; u8 reset_pct, ail; } Skill;
typedef struct { u8 sk1, sk2, sk3, target, hits; u16 mult; } Special;
typedef struct { u8 kind; s16 v1, v2, v3; } Effect;
typedef struct { u8 pct, turns; } Ail;
typedef struct { u8 kind, race, element, weak, size, sp_fx, ability; } Enemy;
typedef struct { u8 x, y, scenery, mid, boss, n_pct, r_pct, sr_pct; u16 coin_pct, coin_drop; u8 men, treasure, pool_n, pool[8], tr_r, tr_sr, reward; } Area;
typedef struct { u8 atk, heal, guard, just, miracle; } DiffRule;
typedef struct { u8 kyouka, skyouka, tsume, claw; u16 coins; } EvolveCost;
typedef struct { u8 item; u32 coins; u8 mats[7][2]; } EvolveSpecial;
typedef struct { u8 v[MIRAGE_TIERS]; u8 stock[MIRAGE_TIERS]; } MirageReward;
typedef struct { u8 area, men; } OrbRow;

/* precomputed enemy stats (data/gen/estat.s), ESTAT_SIZE bytes */
typedef struct {
  u8 mon, stage, role, base;
  u32 hp;
  u16 atk, sp_atk;
  s16 lock_atk;
  u16 stun_need, stun_step, stun_cap;
  u8 sp_every, flags, ability;
  u8 pad[1];
} EStat;
#define ROLE_ZAKO  0
#define ROLE_MID   1
#define ROLE_BOSS  2
#define ROLE_LUCKY 3

extern const Item items[N_ITEM];
extern const Skill skills[N_SKILL];
extern const Special specials[N_SPECIAL];
extern const Effect effects[N_EFFECT];
extern const Ail ails[N_AIL];
extern const Enemy enemy_rows[N_MON];
extern const Area areas[N_AREA];
extern const DiffRule diff_rules[5];
extern const EvolveCost evolve_cost[N_R];
extern const EvolveSpecial evolve_special[];
extern const MirageReward mirage_rewards[N_MIRAGE_KIND];
extern const OrbRow orbs[N_ORB];
extern const u8 el_strong[N_EL];
extern const u16 buff_value[N_BF];
extern const u8 castle_bosses[9], castle_final[5], maou_forms[3];
extern const u8 no_escort[N_MON];
extern const u8 lucky_mons[N_LUCKY];
extern const u8 claw_pct[N_MON];
extern const u8 cand_list[];
extern const u8 cand_off[4][6][2];
extern const u8 skill_lot0[], skill_lot1[], skill_lot2[];
extern const u8 mat_up[N_MAT];
extern const u8 enh_slots[N_R][2];
extern const u8 enh_odds[6][4];
extern const u16 price_table[N_R];
extern const u8 gacha_rate[N_R];
extern const u8 gacha_wt[4];
extern const u8 area_story[N_AREA][2];
extern const u16 name_grid[3][90];
extern const u16 default_name[7];
extern const u8 tut_goal[N_TUT_STEP];
extern const u8 tut_stage_first[4];
extern const u8 dex_mon[N_DEX_MON], dex_item[N_DEX_ITEM], mon_bgm[N_MON];
extern const u16 mon_areas[N_MON];
extern const u8 item_evo_from[N_ITEM], item_evo_lost[N_ITEM];

#endif
