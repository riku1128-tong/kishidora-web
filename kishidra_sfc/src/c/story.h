/* story scenes (story.c) */
#ifndef STORY_H
#define STORY_H
#include "sys.h"
void name_sync(void);
void king_load(void);
void talk(u16 first, u8 n, u16 speaker, u8 king);
u8   difficulty_select(u8 cur, u8 cancel);   /* 255 = cancelled */
void name_entry(void);                       /* new game: name_input(sv.name, NAME_MAX, T_NAME_TITLE, 0) */
#define NI_CANCEL 1                          /* name_input: SELECT leaves without a change (returns 0) */
#define NI_NONEMPTY 2                        /* name_input: START does nothing on an empty name */
u8   name_input(u16 *buf, u8 max, u16 title, u8 flags);   /* 1 = decided (buf changed); max <= 8 */
#ifdef TEST_HOOKS
u8   dbg_confirm3(u16 l1, u16 l2, u16 l3, u16 yes);         /* sys.c DC_CONFIRM3 */
u8   dbg_numspr(s16 x, s16 y, s16 v, u8 pal, u8 *out);      /* sys.c DC_NUMSPR: -> sprites, OAM bytes in out */
#endif
void orb_scene(u8 orb);
void opened_notice(u8 area);
void show_area_story(u8 area);
u8   area_story_n(u8 area);
void king_opening(void);
void king_opening2(void);
void ending(void);
#endif
