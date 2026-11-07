/* sound: C side of the SPC700 driver (src/asm/sound.s, tools/audio/driver.py) */
#include "sound.h"
#include "audio.h"

void bgm_start(void);    /* asm: ea0 track, ea1 pitch */
void bgm_halt(void);
void sound_tick(void);
void se_start(void);     /* asm: ea0 SE id */
void bgm_volume(void);

static u8 cur;

void sound_service(void) { sound_tick(); }

void bgm_play(u8 song) {
  if (song == cur) return;
  cur = song;
  if (song == BGM_NONE) { bgm_halt(); return; }
  ea[0] = song - 1;
  ea[1] = BGM_PITCH;
  bgm_start();
}

void bgm_stop(void) {
  cur = BGM_NONE;
  bgm_halt();
}

void se_play(u8 se) {
  if (se == SE_NONE) return;
  ea[0] = se;
  se_start();
}
