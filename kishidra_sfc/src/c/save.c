/* save data: WRAM copy + two alternating SRAM slots with sequence number and byte sum */
#include "save.h"
#include "text.h"

Save sv;

#define SAVE_VER 1
#define SLOT_A 0x0000
#define SLOT_B 0x4000
#define HDR 12              /* magic..ver are not summed */

void sram_write(void);      /* asm: ea0 wram, ea1 sram offset, ea2 len -> ea3 sum */
void sram_read(void);
void wram_sum(void);

static const u8 magic[4] = { 'K', 'S', 'F', 'C' };
static u8 slot_now;         /* slot the last read / write used */

void bit_set(u8 *bits, u16 i) { bits[i >> 3] |= (u8)(1 << (i & 7)); }
u8 bit_get(const u8 *bits, u16 i) { return (bits[i >> 3] >> (i & 7)) & 1; }

u8 item_is_shield(u8 id) { return items[id].type == WT_SHIELD; }

u8 inv_slots_of(const Inv *it) { return it->slots & 15; }
u8 inv_fav(const Inv *it) { return it->slots >> 7; }

u8 enh_get(const Inv *it, u8 i) { return (it->enh[i >> 2] >> ((i & 3) << 1)) & 3; }

void enh_set(Inv *it, u8 i, u8 v) {
  u8 sh = (i & 3) << 1;
  it->enh[i >> 2] = (u8)((it->enh[i >> 2] & ~(3 << sh)) | (v << sh));
}

u8 enh_count(const Inv *it) {
  u8 i, n = 0;
  for (i = 0; i < inv_slots_of(it); ++i) if (enh_get(it, i)) ++n;
  return n;
}

u8 enh_sum(const Inv *it) {
  u8 i, n = 0;
  for (i = 0; i < inv_slots_of(it); ++i) n += enh_get(it, i);
  return n;
}

u8 roll_slots(u8 id) {
  const u8 *r = enh_slots[items[id].rarity];
  return (u8)rnd_range(r[0], r[1]);
}

void add_got(u8 id) {
  if (sv.got[id] < 255) ++sv.got[id];
  bit_set(sv.found_items, id);
}

/* returns the new index, NO_ITEM when full; slots < 0 = roll */
u16 inv_add(u8 id, const u8 *sk, u8 nsk, s8 slots, u8 over) {
  Inv *it;
  u8 i;
  if (sv.ninv >= (over ? INV_SLOTS : D_INV_MAX)) return NO_ITEM;
  it = &sv.inv[sv.ninv];
  it->id = id;
  for (i = 0; i < 3; ++i) it->sk[i] = (!item_is_shield(id) && i < nsk) ? sk[i] : 255;
  it->slots = (u8)(slots >= 0 ? slots : roll_slots(id));
  it->enh[0] = it->enh[1] = it->enh[2] = 0;
  add_got(id);
  return sv.ninv++;
}

void inv_remove(u16 i) {
  u8 s;
  if (i >= sv.ninv) return;
  --sv.ninv;
  if (i < sv.ninv) memmove(&sv.inv[i], &sv.inv[i + 1], (sv.ninv - i) * sizeof(Inv));
  for (s = 0; s < 4; ++s) {
    if (sv.equip[s] == i) sv.equip[s] = NO_ITEM;
    else if (sv.equip[s] != NO_ITEM && sv.equip[s] > i) --sv.equip[s];
  }
}

u8 equipped_slot(u16 i) {
  u8 s;
  for (s = 0; s < 4; ++s) if (sv.equip[s] == i) return s;
  return 255;
}

u16 sell_price(u8 id) {
  return (u16)((u32)price_table[items[id].rarity] * D_SELL_PCT / 100);
}

void coins_add(u32 n) {
  sv.coins += n;
  if (sv.coins > COIN_CAP) sv.coins = COIN_CAP;
}

void mat_add(u8 m, u16 n) {
  u16 cap = (m == MAT_IT_FUERU || m == MAT_IT_RESET) ? T_GOODS_MAX : D_MAT_MAX;
  sv.mats[m] = (sv.mats[m] + n > cap) ? cap : sv.mats[m] + n;
}

/* ---------------------------------------------------------------- progress */
u8 men_count(u8 area) {
  if (area == AR_CASTLE) return D_CASTLE_MEN;
  if (area == AR_SECRET) return D_SECRET_MEN;
  if (area == AR_MIRAGE) return 1;
  return D_AREA_MEN;
}

u8 is_cleared(u8 area, u8 men) {
  return men >= 1 && ((sv.cleared[area] >> (men - 1)) & 1);
}

u8 men_open(u8 area, u8 men) {
  if (area > sv.area_open || men < 1 || men > men_count(area)) return 0;
  return men == 1 || (sv.story & ST_DEBUG_OPEN) || is_cleared(area, men - 1);
}

u8 area_done(u8 area) { return is_cleared(area, men_count(area)); }

u8 cleared_count(u8 area) {
  u8 m, n = 0;
  for (m = 1; m <= men_count(area); ++m) if (is_cleared(area, m)) ++n;
  return n;
}

u8 men_clear(u8 area, u8 men) {
  sv.cleared[area] |= (u32)1 << (men - 1);
  if (area < AR_MIRAGE && men == men_count(area) && area == sv.area_open && area + 1 < AR_MIRAGE) {
    ++sv.area_open;
    return 1;
  }
  return 0;
}

/* ---------------------------------------------------------------- new game */
static const u8 start_skills[3] = { START_SKILL_0, START_SKILL_1, START_SKILL_2 };

void save_new(void) {
  u8 i, n = 0;
  memset(&sv, 0, sizeof(sv));
  memcpy(sv.magic, magic, 4);
  sv.ver = SAVE_VER;
  sv.size = sizeof(Save);
  sv.difficulty = DIFF_NORMAL;
  sv.bgm_vol = 8; sv.se_vol = 8;
  for (i = 0; i < 4; ++i) sv.equip[i] = NO_ITEM;
  for (i = 0; i < 3; ++i) if (start_skills[i] != 255) ++n;
  sv.equip[EQ_MAIN] = inv_add(ITEM_START_WEAPON, start_skills, n, T_ENH_START_SLOTS, 0);
  sv.equip[EQ_SHIELD] = inv_add(ITEM_EQUIP_SHIELD, 0, 0, -1, 0);
  for (i = 0; i < NAME_MAX; ++i) sv.name[i] = 0;
}

/* ---------------------------------------------------------------- SRAM */
static u16 body_len(void) { return sizeof(Save) - HDR - (INV_SLOTS - sv.ninv) * sizeof(Inv); }

static u8 read_slot(u16 off, Save *hdr_only) {
  /* header first */
  ea[0] = (u16)hdr_only; ea[1] = off; ea[2] = HDR;
  sram_read();
  return memcmp(hdr_only->magic, magic, 4) == 0 && hdr_only->ver == SAVE_VER && hdr_only->size == sizeof(Save);
}

u8 save_exists(void) {
  static Save *h;
  static u8 hb[HDR];
  h = (Save *)hb;
  return read_slot(SLOT_A, h) || read_slot(SLOT_B, h);
}

static u8 load_slot(u16 off) {
  u16 len;
  if (!read_slot(off, &sv)) return 0;
  ea[0] = (u16)&sv + HDR; ea[1] = off + HDR; ea[2] = sizeof(Save) - HDR;
  sram_read();
  if (sv.ninv > INV_SLOTS) return 0;
  len = body_len();
  ea[0] = (u16)&sv + HDR; ea[2] = len;
  wram_sum();
  return ea[3] == sv.sum;
}

u8 save_load(void) {
  static u8 hb[HDR];
  Save *h = (Save *)hb;
  u16 sa = 0, sb = 0;
  u8 a = read_slot(SLOT_A, h), b;
  if (a) sa = h->seq;
  b = read_slot(SLOT_B, h);
  if (b) sb = h->seq;
  /* newest first, the other one as a fallback */
  if (a && (!b || (s16)(sa - sb) > 0)) {
    if (load_slot(SLOT_A)) { slot_now = 0; return 1; }
    if (b && load_slot(SLOT_B)) { slot_now = 1; return 1; }
  } else if (b) {
    if (load_slot(SLOT_B)) { slot_now = 1; return 1; }
    if (a && load_slot(SLOT_A)) { slot_now = 0; return 1; }
  }
  save_new();
  return 0;
}

void save_write(void) {
  u16 off;
  slot_now ^= 1;
  off = slot_now ? SLOT_B : SLOT_A;
  ++sv.seq;
  ea[0] = (u16)&sv + HDR; ea[2] = body_len();
  wram_sum();
  sv.sum = ea[3];
  /* body first, header last: a reset in between leaves the old slot as the valid one */
  ea[0] = (u16)&sv + HDR; ea[1] = off + HDR; ea[2] = body_len();
  sram_write();
  ea[0] = (u16)&sv; ea[1] = off; ea[2] = HDR;
  sram_write();
}
