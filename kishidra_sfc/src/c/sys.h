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
u16  copy_asset(u16 id, void *dst, u16 max);
void estat_block(void);     /* ea0 area, ea1 men, ea3 dst -> ea2 count */

void load_vram(u16 id, u16 vaddr);
void load_pal(u16 id, u8 first);
void load_map(u16 id, u8 bg, u16 add, u8 row);
u16  unpack(u16 id, u16 dst7f);
void queue_vram(u16 vaddr, u16 src, u8 bank, u16 size);
void queue_vram_big(u16 vaddr, u16 src, u8 bank, u16 size);

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

/* random */
void rnd_seed(u16 s);
u16  rnd(void);
u16  rnd_range(u16 lo, u16 hi);   /* inclusive */
u8   rnd_pct(u16 p);              /* true with p% */

/* misc */
u16  mul_pct16(u16 v, u16 p);
u32  mul_pct(u32 v, u16 p);
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

extern u16 pad, pad_new, pad_rep;
extern u16 dbg[8];   /* input for this frame */
void frame_wait(void);              /* wait_frame + input + sound service */
#endif
