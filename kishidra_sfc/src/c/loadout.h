/* Shared loadout rules (docs/plan/PLAN.md step S7; loadout.c, bank 8 with save.c):
 *  - sp_reach: which specials a set of weapons can fire (web adventure.gd reachable_specials: the exact
 *    search over the weapons' skill cycles and the combo history).  Lists of specials: ステータス, the
 *    dex mark, そうび詳細 (one weapon), the gacha card.
 *  - solo_ok_sk: can one weapon's skill list alone fire a special (web solo_ok), by a table: the yes / no
 *    of the INV_SOLO bits, the 必殺10連 roll, the list filters.  Equal to sp_reach(w, 1) != 0.
 *  - fx_state: the state of the effect of an equipped item (web desc.gd fx_state) outside battle.
 * The Python models of all three are in tools/data/loadout_model.py (tools/harness/loadtest.py tests the
 * ROM against them). */
#ifndef LOADOUT_H
#define LOADOUT_H
#include "save.h"

/* out[0..1]: bit n = special n can fire; returns how many.  w[0..nw-1] (nw <= 3) = the weapons, 0 = an
 * empty slot.  scratch = SP_SCRATCH bytes of $7E WRAM the caller lends for the call (battle_view's
 * held[] outside battle, PLAN conflict 7).  2-9 frames for three weapons; never in a battle frame. */
#define SP_SCRATCH 476
u8 sp_reach(const Inv *const *w, u8 nw, u8 *out, u8 *scratch);
u8 sp_reach_equipped(u8 *out, u8 *scratch);   /* MAIN, SUB, THIRD of sv.equip */

/* one weapon: loadout.s solo_eval reads the table blob (bit 0 = fires a special, bit 1 = kick-only) */
#define SOLO_FIRES 1
#define SOLO_KICK 2
u8 __fastcall__ solo_eval(const u8 *sk);
u8 solo_ok_sk(const u8 *sk, u8 *kick);        /* 1 = sk[0..2] alone fires a special; *kick = kick-only */
void __fastcall__ solo_bag(Inv *it, u16 n);   /* loadout.s: the INV_SOLO bit of n items from it on */

/* the INV_SOLO bit of a bag item (Inv.slots bit 6): valid while sv.story & ST_SOLO.  inv_add sets it,
 * save_load runs inv_solo_all once for a save without ST_SOLO; code that changes the skills of an item
 * calls inv_solo_update */
#define inv_solo(it) ((it)->slots & INV_SOLO)
void inv_solo_update(u16 i);
void inv_solo_all(void);

/* effect states: desc.gd STATE_WORD index (outside battle only NONE, ON, COND, OFF; NOW / WAIT are the
 * battle's live states) */
#define FS_NONE 0
#define FS_ON 1
#define FS_COND 2
#define FS_NOW 3
#define FS_WAIT 4
#define FS_OFF 5
extern u16 fx_note;      /* the note of the last fx_state: 0 none, FXS_0 + fx (条件つき), FXO_0 + fx (why
                            not), T_ST_FX_MAINONLY (SUB / THIRD), T_ST_FX_TARGET (+ the slot names of fx_slots) */
extern u8 fx_slots;      /* RESO / TYPE / BOOST: bit s = the weapon in slot s (EQ_MAIN..EQ_THIRD) matches */
/* lo[EQ_MAIN..EQ_SHIELD] = the items of a loadout, 0 = empty.  fx_state: the effect of lo[k];
 * fx_state_of: effect fx as if lo[k] carried it */
void lo_equipped(const Inv **lo);              /* lo[0..3] from sv.equip */
u8 fx_state(const Inv *const *lo, u8 k);
u8 fx_state_of(const Inv *const *lo, u8 k, u8 fx);

#ifdef TEST_HOOKS
void dbg_loadout(u8 c, u16 n, u16 a1);         /* sys.c DC_SOLO .. DC_SOLOALL (records in ovl + DBG_IO) */
#endif

#endif
