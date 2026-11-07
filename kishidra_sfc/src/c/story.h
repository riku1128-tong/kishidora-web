/* story scenes (story.c) */
#ifndef STORY_H
#define STORY_H
#include "sys.h"
void name_sync(void);
void king_load(void);
void talk(u16 first, u8 n, u16 speaker, u8 king);
u8   difficulty_select(u8 cur, u8 cancel);   /* 255 = cancelled */
void name_entry(void);
void orb_scene(u8 orb);
void opened_notice(u8 area);
void show_area_story(u8 area);
u8   area_story_n(u8 area);
void king_opening(void);
void king_opening2(void);
void ending(void);
#endif
