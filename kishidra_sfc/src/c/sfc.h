/* キシドラ！ SFC - engine interface (asm side in src/asm) */
#ifndef SFC_H
#define SFC_H
#include <stdint.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef int8_t   s8;
typedef uint32_t u32;
typedef int32_t  s32;

/* VRAM layout (word addresses) */
#define VRAM_BG1_TILES 0x0000
#define VRAM_BG2_TILES 0x2000
#define VRAM_OBJ_TILES 0x4000
#define VRAM_BG3_TILES 0x6000
#define VRAM_BG1_MAP   0x7000
#define VRAM_BG2_MAP   0x7400
#define VRAM_BG3_MAP   0x7800

/* buttons (JOY1 16-bit) */
#define BTN_B      0x8000
#define BTN_Y      0x4000
#define BTN_SELECT 0x2000
#define BTN_START  0x1000
#define BTN_UP     0x0800
#define BTN_DOWN   0x0400
#define BTN_LEFT   0x0200
#define BTN_RIGHT  0x0100
#define BTN_A      0x0080
#define BTN_X      0x0040
#define BTN_L      0x0020
#define BTN_R      0x0010

/* shadows read by the NMI (low WRAM) */
extern u16 ea[16];
extern u8  oam[544];
extern u16 cgram[256];
extern u8  cgram_dirty;
extern u16 scroll[6];
extern u8  tm_main, tm_sub, bright, bgmode, mosaic, cgadsub, cgwsel;
extern u8  coldata[3];
extern u8  hdma_on;
extern u8  hdma_tab1[32], hdma_tab2[32];
extern u16 joy, joy_new, joy_rep;
extern u16 pad_lat, rep_lat;
extern u16 bg3_vram;          /* BG3 tile base used by text boxes */
void menu_layout(void);       /* BG3 tiles $4000 (1024), OBJ $6000 */   /* edges since the last frame_wait */
extern u16 frame, lag;
extern u8  bgmap_dirty;
extern u8  oam_hide;
extern u16 bg1map[1024], bg2map[1024], bg3map[1024];

void wait_frame(void);
void vram_now(void);          /* ea0=vaddr ea1=src ea2=bank ea3=size ; display blanked */
u8   vram_queue(void);        /* ea0..ea4 ; returns 1 if queued */
void force_blank(u8 on);
void set_vram_layout(void);
void vram_clear(void);       /* display blanked */

#endif
