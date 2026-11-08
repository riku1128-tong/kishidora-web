/* system helpers: screen, sprites, random numbers, assets */
#ifndef SYS_H
#define SYS_H
#include "sfc.h"
#include "gdata.h"
#include "gfx.h"

/* asm (asset.s / text.s) */
u16  asset_unpack(void);    /* ea0 id, ea1 dst offset in $7F -> size */
void asset_vram(void);      /* ea0 id, ea1 vram (display blanked) */
void asset_pal(void);       /* ea0 id, ea1 first colour */
void asset_map(void);       /* ea0 id, ea1 bg 0-2, ea2 add, ea3 row */
void far_copy(void);        /* ea0 src, ea1 bank, ea2 dst, ea3 len */
void asset_copy(void);      /* ea0 id, ea1 dst, ea2 max -> ea2 len */
void asset_where(void);     /* ea0 id -> ea1 address, ea2 bank (raw assets: DMA from ROM) */
u16  copy_asset(u16 id, void *dst, u16 max);
void estat_block(void);     /* ea0 area, ea1 men, ea3 dst -> ea2 count */

void load_vram(u16 id, u16 vaddr);
void load_pal(u16 id, u8 first);
void load_map(u16 id, u8 bg, u16 add, u8 row);
u16  unpack(u16 id, u16 dst7f);
void queue_vram(u16 vaddr, u16 src, u8 bank, u16 size);
void queue_vram_big(u16 vaddr, u16 src, u8 bank, u16 size);
void queue_vram_front(u16 vaddr, u16 src, u8 bank, u16 size);   /* first in the queue, one entry */

void sys_init(void);
void screen_off(void);
void screen_on(void);
void fade_out(void);
void fade_in(void);
void clear_maps(void);
void set_bg3_palettes(void);

/* sprites: OAM shadow */
void oam_clear(void);
void wram_zero(void);        /* ea0 address, ea1 bank, ea2 length */
void mem_copy(void);         /* ea0 source, ea1 destination, ea2 length (bank $7E) */
extern u8 oam_next;
extern u16 oam_used;          /* sprite.s: slots oam_finish still has to hide */
extern u16 oam_top;           /* sprite.s: slots from here on are kept (fixed sprites); 128 none */
void spr_add(s16 x, s16 y, u16 tile_attr, u8 large);
void oam_finish(void);
void spr_list(void);         /* ea0 entries (s16 x, s16 y, u16 tile_attr, u16 large), ea1 count */
void meta_add(void);         /* ea0 pose (n at +4, then dx dy cell), ea1 strip tile, ea2 x, ea3 y, ea4 attr */

/* profiling build (PROFILE=1): frame numbers of a few moments, read by the test scripts */
#ifdef PROFILE
extern u16 prof_marks[16];
#define PROF_MARK(i) (prof_marks[i] = frame)
u16 prof_line(void);
#define PROF_LINE(i) (prof_marks[i] = prof_line())
#else
#define PROF_MARK(i)
#define PROF_LINE(i)
#endif

/* test builds (XDEFS=TEST_HOOKS): the harness's command mailbox, run by frame_wait (sys.c) */
#ifdef TEST_HOOKS
#define DC_PING 1                 /* dbg_res = ~dbg_arg[0], frame, dbg_scr */
#define DC_SE 2                   /* play sound effect dbg_arg[0] (SE_*); dbg_arg[1] != 0: stop the BGM first */
/* text / UI kit (S6; tools/harness/t/s6_*.txt, kitcheck.py).  dbg_buf: codes / a name / OAM bytes */
#define DC_TEXT 3                 /* window (box 9, 1,9 30x8, tiles 784-1023, canvas $7F:3100-3FFF) with the
                                   * codes in dbg_buf at (8, dbg_buf[31] or 8); arg0 bit0 8-dot font, bit1
                                   * text_lh 16, bit2 only close the window, bit3 draw into the open one,
                                   * bits 8-15 {k} high byte; arg1 {n}, arg2 {m}, arg3 {k} low word */
#define DC_WINS 4                 /* arg0 rounds of an out-of-order window close (ui.c dbg_wins); arg1 0 side by
                                   * side, 1 overlapping, 2 under a full-screen window (the fallback) -> res0
                                   * how much the save stack grew (0 = no leak) */
#define DC_NAME 5                 /* name_input(dbg_buf, arg0, T_NAME_TITLE, arg1 = NI_*) -> res0 its result,
                                   * res1 hero_name[0] after (the screen is left faded out) */
#define DC_CONFIRM3 6             /* mk_confirm3 on the screen with arg0 lines (1-3: T_NEW_ASK, T_NEW_ASK2,
                                   * T_DBG_NOUNDO), yes = T_NEWGAME -> res0 */
#define DC_NUMSPR 7               /* mk_num_spr(arg0, arg1, arg2, arg3) from OAM 0 -> res0 sprites, OAM in dbg_buf */
/* loadout rules and the RNG (S7; tools/harness/loadtest.py).  Records in the arena: ovl[0..511] is the
 * sp_reach scratch, ovl[DBG_IO..1535] the records (in and out, n = arg0) */
#define DBG_IO 512
#define DC_SOLO 8                 /* n lists of 3 skills (255 none) -> [solo_ok_sk | kick << 1 | sp_reach(that
                                   * one weapon) count << 2 | (arg1: inv_add(arg1 - 1, the list) made it INV_SOLO)
                                   * << 7 (bit 6: the bag was full), out0, out1] */
#define DC_REACH 9                /* n loadouts of 9 B (3 weapons x 3 skills; all 255 = no weapon) ->
                                   * [count, out0, out1, frames lo, hi]; res0 the most frames */
#define DC_REACH_EQ 10            /* sp_reach_equipped -> res0 count, res1 out bits, res2 frames */
#define DC_FX 11                  /* n records of 18 B [k, fx, 4 x (item id or 255, sk0, sk1, sk2)] ->
                                   * [fx_state_of, fx_note lo, hi, fx_slots]; fx 255 = fx_state (the item's) */
#define DC_SOLOALL 12             /* inv_solo_all -> res0 frames, res1 items with INV_SOLO */
#define DC_RND 13                 /* arg0 n (<= 512) values as u16: arg1 mode 0 rnd(), 1 rnd_range(arg2 & 255,
                                   * arg3), 2 rnd_below(arg2), 3 rnd_pct(arg2), 4 rnd_range(0, arg3); res0 / res1
                                   * rnd_state before / after */
#define DC_MATH 14                /* n records of 8 B [u32 x, u16 p, u16 0] -> u32 over x: arg1 mode 0 mul_now
                                   * (x & 0xffff, p), 1 div_now, 2 pct_now, 3 rnd_mul8(x & 0xffff, p) */
#define DC_POOL 15                /* the 必殺10連 pool: n = arg0 bytes solo_pool_id(0 ..) -> the records */
/* battle (A1; tools/harness/battletest.py).  On the battle screen only (dbg_scr DS_BATTLE, battle_core.c
 * dbg_battle; elsewhere the ack is DC_UNKNOWN).  DC_SPECIAL, DC_CLEAR, DC_CALM and a kill by DC_SETHP wait for a
 * calm moment of the fight (the player's turn, no blow, death or flight under way, no talk or form
 * change due; so after a DC_CLEAR the next one waits for the next WAVE): until
 * then the command stays in dbg_cmd and runs again in the next frame (waitfor dbg_seq) */
#define DC_SPECIAL 16             /* enemy arg0 (255 = the target) attacks now with its special (attacks =
                                   * sp_every) -> res0 1 (0: not targetable), res1 foe_special_next */
#define DC_CLEAR 17               /* the enemies of mask arg0 (0 = all) are killed as by a blow, the lucky ones
                                   * first (the web autotest _auto_adv_money) -> res0 how many */
#define DC_SETHP 18               /* HP of arg0 (255 = the hero, else an enemy) = arg1 | arg2 << 16 (capped at
                                   * its max; an enemy at 0 is killed as by DC_CLEAR) -> res0 / res1 the HP */
#define DC_FOE 19                 /* enemy arg0 -> dbg_buf [mon, kind, hp lo, hi, hp_max lo, hi, atk, sp_atk,
                                   * sp_every, attacks, count, dying | gone << 8, hits_guarded | hits_just
                                   * << 8 (of its last attack)], res0 b_stage, res1 b_ne,
                                   * res2 b_turns_stage, res3 b_turns_total */
#define DC_MONEYMAT 20            /* (any screen) arg0 rolls of the 魔王城の地下 lucky material (battle_hit.c)
                                   * at drop pct arg1 (0 = 100) -> res0 none, res1 ベル, res2 スーパー強化鉄,
                                   * res3 強化鉄; dbg_buf[0] / [1] rnd_state before / after */
#define DC_CALM 21                /* waits for the calm moment (as DC_CLEAR) -> res0 b_turns_total, res1 b_stage,
                                   * res2 b_turns_stage, res3 b_phase */
#define DC_TALK 22                /* waits for a talk window that A would close, then closes it (as A) -> res0
                                   * the talk's text id */
#define DC_UNKNOWN 0xff          /* dbg_ack of a command no one knows */
#define DS_NONE 0                 /* dbg_scr: screens that take commands set their DS_ id */
#define DS_BATTLE 1               /* battle_scene (DC_SPECIAL .. DC_FOE, DC_CALM, DC_TALK) */
extern u8 dbg_cmd, dbg_ack, dbg_seq, dbg_scr;
extern u16 dbg_arg[4], dbg_res[4];
extern u16 dbg_buf[32];
#define DBG_SCREEN(s) (dbg_scr = (s))
#else
#define DBG_SCREEN(s)
#endif

/* random (xorshift; rnd / rnd_mul8 / rnd_below and the state in src/asm/math.s) */
extern u16 rnd_state;            /* the generator's state (never 0) */
void rnd_seed(u16 s);
u16  rnd(void);
u16  rnd_range(u16 lo, u16 hi);   /* inclusive */
u8   rnd_pct(u16 p);              /* true with p% */
u8 __fastcall__ rnd_mul8(u16 r, u8 n);   /* (r * n) >> 16 */
u8 __fastcall__ rnd_below(u8 n);         /* = rnd_range(0, n - 1) for n 1..255 (the same value) */

/* misc */
u16  mul_pct16(u16 v, u16 p);
u32  mul_pct(u32 v, u16 p);
extern u16 pa[2], pp;           /* math.s: 32-bit argument / result, percentage */
void pct_now(void);             /* pa = (pa * pp) / 100 */
u8   div10_now(void);           /* pa /= 10, returns the remainder */
void mul_now(void);             /* pa = pa[0] * pp (16 x 16 -> 32 bits) */
void div_now(void);             /* pa /= pp (the divisor 2..255) */
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

extern u16 pad, pad_new, pad_rep;
extern u16 dbg[8];   /* input for this frame */
void frame_wait(void);              /* wait_frame + input + sound service */
#endif
