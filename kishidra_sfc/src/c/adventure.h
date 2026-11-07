/* adventure rules (port of adventure.gd, surface world): loadout, results, shop, forge */
#ifndef ADVENTURE_H
#define ADVENTURE_H
#include "save.h"
#include "battle.h"

#define ERR_OK 0
#define ERR_COINS 1
#define ERR_FULL 2
#define ERR_MATS 3
#define ERR_NO_SLOT 4
#define ERR_CANT 5
#define ERR_MAX 6
#define ERR_NO_GOODS 7

/* what the result screen and the map show after a battle */
typedef struct {
  u8 win, first, orb, opened, king, mirage, mirage_up, mirage_wave, mirage_clear;
  u16 overflow;
  u32 coins, bonus;
  u8 nitems;
  u16 idx[ITEMS_RUN_MAX];     /* inventory index of each item won (NO_ITEM = sold) */
  u8 fresh[ITEMS_RUN_MAX];
} AdvResult;
extern AdvResult res;

void adv_loadout(Loadout *lo);
void adv_prepare(u8 area, u8 men);
void commit_win(void);
void commit_lose(void);
void commit_leave(void);
void commit_mirage(u8 clear);
u8   king_return_due(void);
u8   area_has_shop(u8 area);
u8   last_shop_area(void);   /* newest open town with a shop, 255 = none */

/* gacha (mixed sword / spear / hammer / shield) */
#define GACHA_MAX 10
typedef struct { u16 idx; u8 id, fresh, ticket; } GachaPull;
extern GachaPull gacha_out[GACHA_MAX];
u8   gacha_pull(u8 n);       /* ERR_*; results in gacha_out[0..n-1] */

/* items */
u8   sell_item(u16 i);       /* ERR_* */
void fav_toggle(u16 i);
u8   goods_buy(u8 mat);      /* MAT_IT_RESET / MAT_IT_FUERU */
u8   goods_sold(u8 area, u8 mat);

/* forge */
typedef struct { u8 n; u8 mat[7]; u16 cnt[7]; u32 coins; } Need;
extern Need need;
u8   evolve_target(u8 id);   /* 255 = cannot evolve */
void evolve_need(u8 id);
u8   evolve_short(void);     /* 0 ok, ERR_MATS, ERR_COINS */
u8   evolve_item(u16 i);     /* ERR_* */
u8   enh_free_slot(const Inv *it);   /* 255 = none */
u8   enhance(u16 target, u8 src_mat, u16 src_item, u8 *slot);   /* returns result 0..3 or 255 + err in enh_err */
extern u8 enh_err;
u8   slot_add(u16 i);
u8   slot_reset(u16 i, u8 slot);
u8   mat_up_do(u8 m, u16 n);
u16  mat_up_max(u8 m);
u32  item_value(u8 id);
void adv_tutorial_loadout(Loadout *lo);
void commit_tutorial(void);
/* debug (debug_screens.gd) */
void debug_open_all(void);
void debug_goods(void);
void debug_dex_all(void);
u8   debug_give(u8 id, const u8 *sk, u8 nsk);   /* 1 = given */
u16  debug_give_all(void);                      /* one of every weapon and shield */
u8   debug_dex_count(u8 what);                  /* 0 monsters, 1 weapons + shields, 2 specials */
#define MEN_TUTORIAL 250
#define MEN_TRAINING 251
#endif
