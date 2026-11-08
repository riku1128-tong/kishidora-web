#ifndef SOUND_H
#define SOUND_H
#include "sfc.h"
#include "audio.h"     /* the song ids BGM_NONE, BGM_OPENING .. N_BGM (tools/make_audio.py BGM order) */
enum { SE_NONE, SE_CURSOR, SE_OK, SE_CANCEL, SE_SLASH, SE_SPEAR, SE_HAMMER, SE_KICK, SE_SHOUT, SE_DAMAGE, SE_GUARD, SE_JUST,
       SE_MIRACLE, SE_EAT, SE_CHARGE, SE_STUN, SE_LUCKY, SE_WARNING, SE_BREATH, SE_TRANSFORM, SE_COIN, SE_DROP, SE_DEFEAT, SE_SPECIAL, N_SE };
void sound_service(void);
void bgm_play(u8 song);
void bgm_stop(void);
void se_play(u8 se);
#endif
