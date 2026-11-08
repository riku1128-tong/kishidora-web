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
  u8 slots;       /* bits 0-3 enhancement slots, bit 6 INV_SOLO, bit 7 favourite */
  u8 enh[3];      /* 9 slots x 2 bits (0 empty, 1 success, 2 great, 3 miracle) */
} Inv;
#define INV_SOLO 0x40   /* Inv.slots: this weapon alone can make a special (valid while sv.story & ST_SOLO) */

/* story flags */
#define ST_OPENING 1
#define ST_DEBUG_OPEN 4      /* debug: every area and STAGE open (L x3 on the map) */
#define ST_MIRAGE_RULES 2
#define ST_SOLO 8            /* inventory: the INV_SOLO bits of the bag are up to date */

/* one-time tips and "seen" marks (web save.guides), sv.guides */
#define GDB_LUCKY 1          /* lucky_tip: the first lucky-character tip was shown */
#define GDB_ENH 2            /* enhance: the forge enhance guide was shown */
#define GDB_EVO 4            /* evolve: the forge evolve guide was shown */
#define GDB_GIFT 8           /* enhance_gift: the free material of the guide was given */
#define GDB_NAV_FORGE 16     /* nav_enhance: the forge NEW mark was seen */

/* 討伐記録 (web boss_records): the loadout as it was at the kill */
typedef struct {
  u8 valid, difficulty;
  u16 turns, boss_turns, nth;
  Inv w[3];            /* MAIN, SUB, THIRD; id 255 = empty slot */
  Inv shield;
} BossRec;             /* 40 B */

/* セット装備 (web equip_sets) */
#define EQUIP_SETS 15
#define SET_NAME_MAX 8
#define SET_GONE 0xfffe      /* the item of this slot was sold or used up */
typedef struct {
  u16 slot[4];               /* inventory index per EQ_* slot, NO_ITEM = empty, SET_GONE = gone */
  u16 name[SET_NAME_MAX];    /* glyph codes, 0-terminated when shorter; name[0] == 0: unregistered */
} EquipSet;                  /* 24 B */

/* SAVE_VER 1 (8702 B: ninv at 628, inv at 630) is read and migrated by save.c */
#define SAVE_V1_SIZE 8702
#define SAVE_V1_NINV 628
#ifndef SAVE_SPARE
#define SAVE_SPARE 64        /* zero bytes for later fields: a field there needs no new SAVE_VER when 0
                                is its right value for an older save (XDEFS=SAVE_SPARE=n only to test
                                the slot-size guard in save.c) */
#endif

/* SAVE_VER 2 (9300 B).  The v1 fields keep their offsets 0..627; docs/plan/PLAN.md §2 */
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
  /* ---- v2 block (628..1225): zero in a migrated v1 save except where save.c v1_init sets it ---- */
  u8 guides;                   /*  628 GDB_* */
  u8 solo_tickets;             /*  629 必殺10連武器チケット 0..99 */
  u8 mirage_run;               /*  630 1 = a tower run is going / suspended (record at slot + $3E00) */
  u8 mirage_cards[3];          /*  631 bit w = the card of WAVE w+1 (18 bits) */
  u16 mirage_run_id;           /*  634 ++ per run; the snapshot record must match */
  u16 shops_seen;              /*  636 bit = area index of a shop town seen (NEW marks) */
  u16 boss_wins[2];            /*  638 0 魔王, 1 裏ボス: wins counted since v2 */
  BossRec rec[4];              /*  642 魔王 first / latest, 裏ボス first / latest */
  EquipSet sets[EQUIP_SETS];   /*  802 */
  u8 spare[SAVE_SPARE];        /* 1162 */
  /* ---- the bag last: save.c writes only ninv items ---- */
  u16 ninv;                    /* 1226 */
  Inv inv[INV_SLOTS];          /* 1228 */
} Save;                        /* 9300 */

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
