/* scenes and shared game state */
#ifndef GAME_H
#define GAME_H
#include "ui.h"

enum { SC_TITLE, SC_NEWGAME, SC_MAP, SC_BATTLE, SC_RESULT, SC_ENDING };
u8 title_scene(void);
#endif
