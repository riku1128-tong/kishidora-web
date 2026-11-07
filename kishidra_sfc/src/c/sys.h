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
void estat_block(void);     /* ea0 area, ea1 men, ea3 dst -> ea2 count */

void load_vram(u16 id, u16 vaddr);
void load_pal(u16 id, u8 first);
void load_map(u16 id, u8 bg, u16 add, u8 row);
u16  unpack(u16 id, u16 dst7f);
void queue_vram(u16 vaddr, u16 src, u8 bank, u16 size);

void sys_init(void);
void screen_off(void);
void screen_on(void);
void fade_out(void);
void fade_in(void);
void clear_maps(void);
void set_bg3_palettes(void);

/* sprites: OAM shadow */
void oam_clear(void);
void spr(u8 i, s16 x, s16 y, u16 tile_attr, u8 large);
extern u8 oam_next;
void spr_add(s16 x, s16 y, u16 tile_attr, u8 large);
void oam_finish(void);

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

extern u16 pad, pad_new, pad_rep;   /* input for this frame */
void frame_wait(void);              /* wait_frame + input + sound service */
#endif
