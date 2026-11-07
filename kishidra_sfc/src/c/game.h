/* scenes and shared game state */
#ifndef GAME_H
#define GAME_H
#include "ui.h"

enum { SC_TITLE, SC_NEWGAME, SC_MAP, SC_BATTLE, SC_RESULT, SC_ENDING };
/* map actions */
enum { MA_NONE, MA_BATTLE, MA_TITLE, MA_REDRAW };
enum { MS_NORMAL, MS_AFTER, MS_NEWGAME };
extern u8 map_area, map_men;
u8 map_scene(u8 mode);
u8 menu_scene(void);
void equip_scene(void);
void shop_scene(u8 area, u8 goods);
void result_scene(u8 outcome);
void forge_scene(u16 start);
void dex_scene(void);
void debug_scene(void);
u16 mon_gfx_get(u8 m, u8 k);   /* battle_view.c */
u16  pick_item(u8 filter, u16 title, u16 start);   /* menu.c; NO_ITEM = cancelled */
extern u16 pick_exclude;
u8 title_scene(u8 has_save);
#include "battle.h"
u8 battle_scene(u8 area, u8 men, const Loadout *lo);
#endif
