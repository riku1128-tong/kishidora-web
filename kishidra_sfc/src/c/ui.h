/* text boxes (BG3) and small UI helpers */
#ifndef UI_H
#define UI_H
#include "sys.h"
#include "text.h"

#define BOX_OPAQUE 1
#define BOX_FRAME  2
#define BOX_WINDOW 3
#define BOX_LOW 4
#define BOX_SMALL 8            /* text in the 8-dot font (7 rows + shadow, lines 9 px apart) */
/* BG3 text palettes (bg3pal in sys.c): the same for transparent boxes and windows */
#define PAL_WHITE 0
#define PAL_YELLOW 1
#define PAL_RED 2
#define PAL_GRAY 3
#define PAL_GREEN 4
#define PAL_BLUE 5
#define PAL_GOLD 6             /* window frames */
#define PAL_T_WHITE PAL_WHITE
#define PAL_T_YELLOW PAL_YELLOW
#define PAL_T_RED PAL_RED
#define PAL_T_GRAY PAL_GRAY
#define PAL_W PAL_WHITE
#define PAL_W_YELLOW PAL_YELLOW
#define PAL_W_FRAME PAL_WHITE  /* framed windows: gold frame (ring tiles), white text */
#define PAL_W_RED PAL_RED

extern u8 tb[12 * 8];
extern u32 targ_num, targ_num2;
extern u16 targ_str;
extern u16 text_wrap;          /* > 0: lines break before this x (say_wrap) */
extern u16 text_small;         /* msg_width measures in the 8-dot font */
extern u16 hero_name[8];

void tb_open(u8 box);
void tb_clear(u8 box);         /* waits until no queued upload reads the box's canvas */
void canvas_wait(u16 t0, u16 n);   /* until no queued upload reads BG3 tiles t0..t0+n-1 (their canvas) */
u8 canvas_busy(u16 t0, u16 n);
u8 vq_busy(u16 lo, u16 hi);       /* a queued upload reads $7F:lo..hi-1 */
void tb_clear_now(u8 box);
void tb_rect_now(u8 box);
void tb_rect(u8 id, u16 x, u8 y, u16 w, u8 h, u8 colour);   /* filled pixels in the canvas */
void tb_flush(u8 box);
void tb_flush_rows(u8 box, u8 r0, u8 nr);
void tb_text(u8 box);
void tb_codes(u8 box);
u16  text_width(void);

void box(u8 id, u8 x, u8 y, u8 w, u8 h, u16 tile, u8 pal, u8 flags);
void say(u8 id, u16 px, u16 py, u16 msg);
void sayn(u8 id, u16 px, u16 py, u16 msg, u32 n);
void says(u8 id, u16 px, u16 py, u16 msg, u16 s);
void say_center(u8 id, u16 py, u16 msg);
void say_wrap(u8 id, u16 px, u16 py, u16 msg, u16 maxx);
u16  msg_width(u16 msg);
void box_pal(u8 id, u8 tx, u8 ty, u8 tw, u8 th, u8 pal);
void box_hide(u8 id);
void win_base(void);          /* snapshot BG1 (call after loading the screen picture) */
void win_open(u8 id, u8 x, u8 y, u8 w, u8 h, u16 tile, u8 pal);
void win_close(u8 id);

/* menu cursor (sprite) */
void cursor_at(s16 x, s16 y);
#endif
