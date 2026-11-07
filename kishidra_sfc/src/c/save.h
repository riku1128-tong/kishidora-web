/* save data (port of save_data.gd, surface world) kept in WRAM and mirrored to battery SRAM */
#ifndef SAVE_H
#define SAVE_H
#include "sys.h"

#define INV_SLOTS (D_INV_MAX + D_ITEM_MAX)   /* the extra slots only take battle rewards */
#define NO_ITEM 0xffff
#define EQ_MAIN 0
#define EQ_SUB 1
#define EQ_THIRD 2                           /* replaces the Safari rental slot: any owned weapon */
#define EQ_SHIELD 3
#define MEN_BITS_MONEY 19                    /* bit of the castle money stage (MONEY_MEN 20) */
#define NAME_MAX 6

/* one owned weapon / shield (8 bytes) */
typedef struct {
  u8 id;          /* item index */
  u8 sk[3];       /* skills (255 = none) */
  u8 slots;       /* bits 0-3 enhancement slots, bit 7 favourite */
  u8 enh[3];      /* 9 slots x 2 bits (0 empty, 1 success, 2 great, 3 miracle) */
} Inv;

/* story flags */
#define ST_OPENING 1
#define ST_DEBUG_OPEN 4      /* debug: every area and STAGE open (L x3 on the map) */
#define ST_MIRAGE_RULES 2

typedef struct {
  u8 magic[4];
  u16 seq, sum, size, ver;
  u8 area_open, difficulty, cursor, tickets;
  u32 coins;
  u32 cleared[N_AREA];
  u16 name[NAME_MAX + 1];
  u8 ending_seen, tut_cleared, tut_clears, sort_mode, guard_late, story, bgm_vol, se_vol;
  u16 equip[4];
  u16 mats[N_MAT];
  u8 found_items[32], found_sp[(N_SPECIAL + 7) / 8], found_mons[(N_MON + 7) / 8];
  u16 kills[N_MON];
  u8 got[N_ITEM];
  u8 mirage_best, mirage_clear;
  u16 mirage_turns;
  u16 ninv;
  Inv inv[INV_SLOTS];
} Save;

extern Save sv;

void save_new(void);
u8   save_load(void);          /* 1 = a valid save was read */
u8   save_exists(void);
void save_write(void);

/* inventory */
u8   inv_slots_of(const Inv *it);
u8   inv_fav(const Inv *it);
u8   enh_get(const Inv *it, u8 i);
void enh_set(Inv *it, u8 i, u8 v);
u8   enh_count(const Inv *it);
u8   enh_sum(const Inv *it);
u16  inv_add(u8 id, const u8 *sk, u8 nsk, s8 slots, u8 over);   /* index or NO_ITEM */
void inv_remove(u16 i);
u8   equipped_slot(u16 i);     /* 255 = not equipped */
u8   item_is_shield(u8 id);
u16  sell_price(u8 id);
void coins_add(u32 n);
void mat_add(u8 m, u16 n);
void add_got(u8 id);
void bit_set(u8 *bits, u16 i);
u8   bit_get(const u8 *bits, u16 i);
u8   roll_slots(u8 id);

/* progress */
u8   men_count(u8 area);
u8   is_cleared(u8 area, u8 men);
u8   men_open(u8 area, u8 men);
u8   area_done(u8 area);
u8   cleared_count(u8 area);
u8   men_clear(u8 area, u8 men);   /* 1 = a new area opened */
#endif
