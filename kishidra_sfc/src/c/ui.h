/* text boxes (BG3) and small UI helpers */
#ifndef UI_H
#define UI_H
#include "sys.h"
#include "text.h"

#define BOX_OPAQUE 1
#define BOX_FRAME  2
#define BOX_WINDOW 3
#define PAL_T_WHITE 0
#define PAL_T_YELLOW 1
#define PAL_T_RED 2
#define PAL_T_GRAY 3
#define PAL_W 4
#define PAL_W_YELLOW 5
#define PAL_W_FRAME 6
#define PAL_W_RED 7

extern u8 tb[12 * 8];
extern u32 targ_num;
extern u16 targ_str;
extern u16 hero_name[8];

void tb_open(u8 box);
void tb_clear(u8 box);
void tb_flush(u8 box);
void tb_text(u8 box);
void tb_codes(u8 box);
u16  text_width(void);

void box(u8 id, u8 x, u8 y, u8 w, u8 h, u16 tile, u8 pal, u8 flags);
void say(u8 id, u16 px, u16 py, u16 msg);
void sayn(u8 id, u16 px, u16 py, u16 msg, u32 n);
void says(u8 id, u16 px, u16 py, u16 msg, u16 s);
void say_center(u8 id, u16 py, u16 msg);
u16  msg_width(u16 msg);
void box_pal(u8 id, u8 tx, u8 ty, u8 tw, u8 th, u8 pal);
void box_hide(u8 id);

/* menu cursor (sprite) */
void cursor_at(s16 x, s16 y);
#endif
