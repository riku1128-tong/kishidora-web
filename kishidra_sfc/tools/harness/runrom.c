/* runrom: headless libretro frontend for automated SFC ROM tests.
 * usage: runrom <core.so> <rom.sfc> <script.txt>
 * script commands (one per line):
 *   wait N            run N frames with no input
 *   press BTN [N]     hold BTN for N frames (default 2) then release 1 frame
 *   hold BTN N        hold BTN for N frames (no release frame)
 *   shot FILE.ppm     save current frame
 *   wav FILE.wav      start recording audio to FILE (stop with wavend)
 *   wavend
 *   wram FILE         dump 128KB WRAM
 *   sram FILE         dump SRAM
 *   loadsram FILE     load SRAM content (before first frame)
 * BTN: A B X Y L R START SELECT UP DOWN LEFT RIGHT (combine with +)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <dlfcn.h>
#include "libretro.h"

static void (*p_init)(void); static void (*p_deinit)(void);
static void (*p_set_env)(retro_environment_t); static void (*p_set_video)(retro_video_refresh_t);
static void (*p_set_audio)(retro_audio_sample_t); static void (*p_set_audio_batch)(retro_audio_sample_batch_t);
static void (*p_set_poll)(retro_input_poll_t); static void (*p_set_state)(retro_input_state_t);
static bool (*p_load)(const struct retro_game_info*); static void (*p_run)(void);
static void* (*p_mem)(unsigned); static size_t (*p_memsize)(unsigned);
static void (*p_get_av)(struct retro_system_av_info*);

static unsigned fmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint8_t frame[1024*1024*4]; static unsigned fw, fh; static size_t fpitch;
static uint16_t joy;
static FILE *wavf; static uint32_t wavbytes;

static void lg(enum retro_log_level l, const char *f, ...) { va_list a; va_start(a,f); if(l>=RETRO_LOG_WARN) vfprintf(stderr,f,a); va_end(a); }
static bool env(unsigned cmd, void *data) {
  switch (cmd) {
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: fmt = *(enum retro_pixel_format*)data; return true;
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log = lg; return true;
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = "."; return true;
  case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data = true; return true;
  default: return false;
  }
}
static void video(const void *d, unsigned w, unsigned h, size_t pitch) {
  if (!d) return; fw = w; fh = h; fpitch = pitch;
  for (unsigned y = 0; y < h; y++) memcpy(frame + y*pitch, (const uint8_t*)d + y*pitch, pitch);
}
static void audio1(int16_t l, int16_t r) { if (wavf) { int16_t s[2]={l,r}; fwrite(s,4,1,wavf); wavbytes+=4; } }
static size_t audiob(const int16_t *d, size_t n) { if (wavf) { fwrite(d,4,n,wavf); wavbytes+=4*n; } return n; }
static void poll(void) {}
static int16_t state(unsigned port, unsigned dev, unsigned idx, unsigned id) {
  if (port || dev != RETRO_DEVICE_JOYPAD) return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return joy;
  return (joy >> id) & 1;
}
static void shot(const char *fn) {
  FILE *f = fopen(fn, "wb"); if (!f) return;
  fprintf(f, "P6\n%u %u\n255\n", fw, fh);
  for (unsigned y = 0; y < fh; y++) for (unsigned x = 0; x < fw; x++) {
    uint8_t rgb[3];
    if (fmt == RETRO_PIXEL_FORMAT_XRGB8888) { uint32_t p = *(uint32_t*)(frame + y*fpitch + x*4); rgb[0]=p>>16; rgb[1]=p>>8; rgb[2]=p; }
    else if (fmt == RETRO_PIXEL_FORMAT_RGB565) { uint16_t p = *(uint16_t*)(frame + y*fpitch + x*2); rgb[0]=((p>>11)&31)<<3; rgb[1]=((p>>5)&63)<<2; rgb[2]=(p&31)<<3; }
    else { uint16_t p = *(uint16_t*)(frame + y*fpitch + x*2); rgb[0]=((p>>10)&31)<<3; rgb[1]=((p>>5)&31)<<3; rgb[2]=(p&31)<<3; }
    fwrite(rgb, 3, 1, f);
  }
  fclose(f);
}
static void wavhdr(FILE *f, uint32_t n) {
  uint32_t sr = 32040; uint8_t h[44]; memcpy(h,"RIFF",4); uint32_t v=36+n; memcpy(h+4,&v,4); memcpy(h+8,"WAVEfmt ",8);
  v=16; memcpy(h+16,&v,4); uint16_t s=1; memcpy(h+20,&s,2); s=2; memcpy(h+22,&s,2); memcpy(h+24,&sr,4); v=sr*4; memcpy(h+28,&v,4);
  s=4; memcpy(h+32,&s,2); s=16; memcpy(h+34,&s,2); memcpy(h+36,"data",4); memcpy(h+40,&n,4); fseek(f,0,SEEK_SET); fwrite(h,44,1,f);
}
static uint16_t btn(const char *s) {
  uint16_t m = 0; char buf[128]; strncpy(buf, s, 127); buf[127]=0;
  for (char *t = strtok(buf, "+"); t; t = strtok(NULL, "+")) {
    if (!strcmp(t,"A")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_A; else if (!strcmp(t,"B")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_B;
    else if (!strcmp(t,"X")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_X; else if (!strcmp(t,"Y")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_Y;
    else if (!strcmp(t,"L")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_L; else if (!strcmp(t,"R")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_R;
    else if (!strcmp(t,"START")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_START; else if (!strcmp(t,"SELECT")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_SELECT;
    else if (!strcmp(t,"UP")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_UP; else if (!strcmp(t,"DOWN")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_DOWN;
    else if (!strcmp(t,"LEFT")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_LEFT; else if (!strcmp(t,"RIGHT")) m |= 1<<RETRO_DEVICE_ID_JOYPAD_RIGHT;
    else fprintf(stderr, "bad button %s\n", t);
  }
  return m;
}
static void dump(unsigned id, const char *fn) {
  FILE *f = fopen(fn, "wb"); if (!f) return; void *p = p_mem(id); size_t n = p_memsize(id);
  if (p && n) fwrite(p, 1, n, f); fclose(f);
}
static unsigned long frames;
static void run(int n) { for (int i = 0; i < n; i++) { p_run(); frames++; } }
#define SYM(v,n) do { *(void**)&v = dlsym(h, n); if (!v) { fprintf(stderr,"no %s\n", n); return 1; } } while(0)
int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: runrom core.so rom.sfc script\n"); return 1; }
  void *h = dlopen(argv[1], RTLD_NOW); if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
  SYM(p_init,"retro_init"); SYM(p_deinit,"retro_deinit"); SYM(p_set_env,"retro_set_environment"); SYM(p_set_video,"retro_set_video_refresh");
  SYM(p_set_audio,"retro_set_audio_sample"); SYM(p_set_audio_batch,"retro_set_audio_sample_batch"); SYM(p_set_poll,"retro_set_input_poll");
  SYM(p_set_state,"retro_set_input_state"); SYM(p_load,"retro_load_game"); SYM(p_run,"retro_run"); SYM(p_mem,"retro_get_memory_data");
  SYM(p_memsize,"retro_get_memory_size"); SYM(p_get_av,"retro_get_system_av_info");
  p_set_env(env); p_init(); p_set_video(video); p_set_audio(audio1); p_set_audio_batch(audiob); p_set_poll(poll); p_set_state(state);
  FILE *rf = fopen(argv[2], "rb"); if (!rf) { perror(argv[2]); return 1; }
  fseek(rf, 0, SEEK_END); long rs = ftell(rf); fseek(rf, 0, SEEK_SET); void *rom = malloc(rs); fread(rom, 1, rs, rf); fclose(rf);
  struct retro_game_info gi = { argv[2], rom, (size_t)rs, NULL };
  if (!p_load(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
  FILE *sf = fopen(argv[3], "r"); if (!sf) { perror(argv[3]); return 1; }
  char line[512];
  while (fgets(line, sizeof line, sf)) {
    char cmd[64] = {0}, a1[256] = {0}; int n = -1;
    if (sscanf(line, "%63s %255s %d", cmd, a1, &n) < 1 || cmd[0] == '#') continue;
    if (!strcmp(cmd, "wait")) { run(atoi(a1)); }
    else if (!strcmp(cmd, "press")) { joy = btn(a1); run(n > 0 ? n : 2); joy = 0; run(1); }
    else if (!strcmp(cmd, "hold")) { joy = btn(a1); run(n > 0 ? n : 1); joy = 0; }
    else if (!strcmp(cmd, "shot")) { shot(a1); }
    else if (!strcmp(cmd, "wav")) { wavf = fopen(a1, "wb+"); wavbytes = 0; uint8_t z[44] = {0}; fwrite(z, 44, 1, wavf); }
    else if (!strcmp(cmd, "wavend")) { if (wavf) { wavhdr(wavf, wavbytes); fclose(wavf); wavf = NULL; } }
    else if (!strcmp(cmd, "wram")) dump(RETRO_MEMORY_SYSTEM_RAM, a1);
    else if (!strcmp(cmd, "sram")) dump(RETRO_MEMORY_SAVE_RAM, a1);
    else if (!strcmp(cmd, "loadsram")) { FILE *f = fopen(a1, "rb"); if (f) { fread(p_mem(RETRO_MEMORY_SAVE_RAM), 1, p_memsize(RETRO_MEMORY_SAVE_RAM), f); fclose(f); } }
    else fprintf(stderr, "unknown cmd %s\n", cmd);
  }
  if (wavf) { wavhdr(wavf, wavbytes); fclose(wavf); }
  printf("frames %lu\n", frames);
  return 0;
}
