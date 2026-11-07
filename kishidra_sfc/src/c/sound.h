#ifndef SOUND_H
#define SOUND_H
#include "sfc.h"
void sound_service(void);
void bgm_play(u8 song);
void bgm_stop(void);
void se_play(u8 se);
#endif
