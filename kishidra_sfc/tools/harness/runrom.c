/* runrom: headless libretro frontend for automated SFC ROM tests.
 * usage: runrom <core.so> <rom.sfc> <script.txt>
 * build: gcc -O2 -I/usr/include/libretro-common -o runrom runrom.c -ldl
 * script commands (one per line, '#' starts a comment line):
 *   wait N            run N frames with no input
 *   press BTN [N]     hold BTN for N frames (default 2) then release 1 frame
 *   hold BTN N        hold BTN for N frames (no release frame)
 *   shot FILE.ppm     save current frame
 *   wav FILE.wav      start recording audio to FILE (stop with wavend)
 *   wavend
 *   wram FILE [ADDR LEN]  dump 128KB WRAM (or LEN bytes from ADDR)
 *   sram FILE         dump SRAM
 *   vram FILE         dump 64KB VRAM
 *   loadsram FILE     load SRAM content (before first frame)
 *   poke ADDR V ...   write bytes to WRAM (between two frames: the game sees them in the next one)
 *   pokew ADDR V ...  write 16-bit words (little endian)
 *   pokel ADDR V ...  write 32-bit values (little endian)
 *   peek ADDR [N]     print N bytes (default 1) to stdout
 *   waitfor ADDR V [MAX]  run frames until the byte at ADDR is V (at most MAX frames, default 600;
 *                     a timeout is reported and makes the exit status 3)
 *   map FILE          symbols for ADDR (default: the ROM's .lbl, else its .map, next to the ROM;
 *                     build.py writes both)
 * BTN: A B X Y L R START SELECT UP DOWN LEFT RIGHT (combine with +)
 * ADDR: a symbol of the link (C names with or without the leading _: sv, _sv, dbg_cmd), optionally
 *   +N / -N (decimal or $hex / 0xhex), or a hex address ($7e2000, 0x7f0000, 7e2000; $0000-$1fff and
 *   its mirrors are low WRAM).  A symbol below $10000 at $2000 or above is in bank $7E (the C data).
 *   Use data symbols: the bank-switched C code also links at $8000-$FFFF (16-bit values), so a
 *   function name is not caught and reads / writes the C data at that offset.
 * V: decimal, or hex with $ / 0x; negative values are stored as two's complement.
 * Exit status: 0 ok, 1 cannot start, 2 bad command / address (the script stops: an unknown command,
 *   button or frame count, a missing argument, a loadsram file that cannot be read), 3 waitfor timeout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
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
static void die(const char *f, ...);   /* a bad script line: message, exit status 2 */

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
  FILE *f = fopen(fn, "wb"); if (!f) die("cannot write %s", fn);
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
    else die("bad button %s", t);
  }
  if (!m) die("no button");
  return m;
}
static void dump(unsigned id, const char *fn) {
  FILE *f = fopen(fn, "wb"); if (!f) die("cannot write %s", fn); void *p = p_mem(id); size_t n = p_memsize(id);
  if (p && n) fwrite(p, 1, n, f); fclose(f);
}
static unsigned long frames;
static void run(int n) { for (int i = 0; i < n; i++) { p_run(); frames++; } }

/* ---- symbols and WRAM addresses (poke / peek / waitfor) */
static const char *rom_path, *map_path;
static int lineno;
static char **sym_name; static long *sym_val; static int nsym, syms_loaded;
static void die(const char *f, ...) {
  va_list a; va_start(a, f); fprintf(stderr, "runrom: line %d: ", lineno); vfprintf(stderr, f, a); fputc('\n', stderr); va_end(a);
  exit(2);
}
static void sym_add(const char *n, long v) {
  if (nsym % 1024 == 0) { sym_name = realloc(sym_name, (nsym + 1024) * sizeof *sym_name); sym_val = realloc(sym_val, (nsym + 1024) * sizeof *sym_val); }
  sym_name[nsym] = strdup(n); sym_val[nsym++] = v;
}
static int is_hex6(const char *t) { int i; for (i = 0; i < 6; i++) if (!isxdigit((unsigned char)t[i]) || islower((unsigned char)t[i])) return 0; return t[6] == 0; }
static int is_flags(const char *t) { int n = (int)strlen(t), i; if (n < 2 || n > 4) return 0; for (i = 0; i < n; i++) if (!isupper((unsigned char)t[i])) return 0; return 1; }
/* ld65 VICE label file ("al 0080B6 ._sv") or map file (the exports lists: "_sv  0080B6 RLA") */
static void load_syms(void) {
  char path[1024], line[1024];
  FILE *f = NULL;
  syms_loaded = 1;
  if (map_path) { snprintf(path, sizeof path, "%s", map_path); f = fopen(path, "r"); }
  else {
    const char *ext[2] = { ".lbl", ".map" }; int k;
    for (k = 0; k < 2 && !f; k++) {
      const char *dot = strrchr(rom_path, '.'), *sl = strrchr(rom_path, '/');
      size_t n = (dot && (!sl || dot > sl)) ? (size_t)(dot - rom_path) : strlen(rom_path);
      snprintf(path, sizeof path, "%.*s%s", (int)n, rom_path, ext[k]);
      f = fopen(path, "r");
    }
  }
  if (!f) die("no symbol file (%s; give one with: map FILE)", map_path ? map_path : "<rom>.lbl / <rom>.map");
  while (fgets(line, sizeof line, f)) {
    char *tok[16]; int nt = 0, i;
    for (char *t = strtok(line, " \t\r\n"); t && nt < 16; t = strtok(NULL, " \t\r\n")) tok[nt++] = t;
    if (nt == 3 && !strcmp(tok[0], "al") && is_hex6(tok[1])) { sym_add(tok[2][0] == '.' ? tok[2] + 1 : tok[2], strtol(tok[1], NULL, 16)); continue; }
    for (i = 0; i + 2 < nt; i++) if (is_hex6(tok[i + 1]) && is_flags(tok[i + 2])) { sym_add(tok[i], strtol(tok[i + 1], NULL, 16)); i += 2; }
  }
  fclose(f);
  if (!nsym) die("no symbols in %s", path);
}
static int find_sym(const char *n, long *v) {
  int i, pass;
  if (!syms_loaded) load_syms();
  for (pass = 0; pass < 2; pass++)
    for (i = 0; i < nsym; i++) {
      const char *s = sym_name[i];
      if (pass == 1) { if (s[0] != '_') continue; s++; }
      if (!strcmp(s, n)) { *v = sym_val[i]; return 1; }
    }
  return 0;
}
/* a number: decimal, or hex with $ / 0x (hex_default: plain digits are hex too) */
static int number(const char *t, int hex_default, long *v) {
  char *e; int neg = 0;
  if (*t == '-') { neg = 1; t++; } else if (*t == '+') t++;
  if (*t == '$') *v = strtol(t + 1, &e, 16), t++;
  else if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) *v = strtol(t + 2, &e, 16), t += 2;
  else *v = strtol(t, &e, hex_default ? 16 : 10);
  if (e == t || *e) return 0;
  if (neg) *v = -*v;
  return 1;
}
/* WRAM offset (0..$1FFFF) of an ADDR token */
static long wram_addr(const char *t) {
  long a, off = 0; char name[256]; const char *p;
  if (t[0] == '$' || isdigit((unsigned char)t[0])) {
    if (!number(t, 1, &a)) die("bad address %s", t);
    if (a >= 0x7e0000 && a <= 0x7fffff) return a - 0x7e0000;
    if (((a >> 16) < 0x40 || ((a >> 16) >= 0x80 && (a >> 16) < 0xc0)) && (a & 0xffff) < 0x2000) return a & 0x1fff;
    die("%s is not a WRAM address", t);
  }
  for (p = t; *p && *p != '+' && *p != '-'; p++) ;
  if ((size_t)(p - t) >= sizeof name) die("symbol too long: %s", t);
  memcpy(name, t, p - t); name[p - t] = 0;
  if (*p && !number(p, 0, &off)) die("bad offset in %s", t);
  if (!find_sym(name, &a)) die("unknown symbol %s", name);
  if (a >= 0x7e0000 && a <= 0x7fffff) a -= 0x7e0000;
  else if (a >= 0x10000) die("%s (%06lx) is not in WRAM", name, a);
  /* else: low WRAM ($0000-$1FFF) or the C data area (CRAM, bank $7E): offset = address */
  a += off;
  if (a < 0 || a >= 0x20000) die("%s is outside WRAM", t);
  return a;
}
static uint8_t *wram(long a, long n) {
  uint8_t *m = p_mem(RETRO_MEMORY_SYSTEM_RAM); size_t sz = p_memsize(RETRO_MEMORY_SYSTEM_RAM);
  if (!m || (size_t)(a + n) > sz) die("WRAM %05lx+%ld not available", a, n);
  return m + a;
}
static int known(const char *c) {
  static const char *const k[] = { "wait", "press", "hold", "shot", "wav", "wavend", "wram", "sram", "vram",
                                   "loadsram", "map", "poke", "pokew", "pokel", "peek", "waitfor" };
  size_t i;
  for (i = 0; i < sizeof k / sizeof *k; i++) if (!strcmp(c, k[i])) return 1;
  return 0;
}
/* a frame count (decimal) */
static int count(const char *t) {
  long v;
  if (!number(t, 0, &v) || v < 0 || v > 1000000) die("bad frame count %s", t);
  return (int)v;
}
static long value(const char *t, int bytes) {
  long v;
  if (!number(t, 0, &v)) die("bad value %s", t);
  if (bytes < 4 && (v >= (1L << (8 * bytes)) || v < -(1L << (8 * bytes - 1)))) die("value %s does not fit %d byte(s)", t, bytes);
  if (bytes == 4 && (v > 0xffffffffL || v < -0x80000000L)) die("value %s does not fit 4 bytes", t);
  return v;
}

#define SYM(v,n) do { *(void**)&v = dlsym(h, n); if (!v) { fprintf(stderr,"no %s\n", n); return 1; } } while(0)
int main(int argc, char **argv) {
  int status = 0;
  if (argc < 4) { fprintf(stderr, "usage: runrom core.so rom.sfc script\n"); return 1; }
  void *h = dlopen(argv[1], RTLD_NOW); if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
  SYM(p_init,"retro_init"); SYM(p_deinit,"retro_deinit"); SYM(p_set_env,"retro_set_environment"); SYM(p_set_video,"retro_set_video_refresh");
  SYM(p_set_audio,"retro_set_audio_sample"); SYM(p_set_audio_batch,"retro_set_audio_sample_batch"); SYM(p_set_poll,"retro_set_input_poll");
  SYM(p_set_state,"retro_set_input_state"); SYM(p_load,"retro_load_game"); SYM(p_run,"retro_run"); SYM(p_mem,"retro_get_memory_data");
  SYM(p_memsize,"retro_get_memory_size"); SYM(p_get_av,"retro_get_system_av_info");
  p_set_env(env); p_init(); p_set_video(video); p_set_audio(audio1); p_set_audio_batch(audiob); p_set_poll(poll); p_set_state(state);
  rom_path = argv[2];
  FILE *rf = fopen(argv[2], "rb"); if (!rf) { perror(argv[2]); return 1; }
  fseek(rf, 0, SEEK_END); long rs = ftell(rf); fseek(rf, 0, SEEK_SET); void *rom = malloc(rs);
  if (fread(rom, 1, rs, rf) != (size_t)rs) { perror(argv[2]); return 1; }
  fclose(rf);
  struct retro_game_info gi = { argv[2], rom, (size_t)rs, NULL };
  if (!p_load(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
  FILE *sf = fopen(argv[3], "r"); if (!sf) { perror(argv[3]); return 1; }
  char line[512];
  while (fgets(line, sizeof line, sf)) {
    char *tok[64]; int nt = 0, i;
    lineno++;
    for (char *t = strtok(line, " \t\r\n"); t && nt < 64; t = strtok(NULL, " \t\r\n")) tok[nt++] = t;
    if (!nt || tok[0][0] == '#') continue;
    const char *cmd = tok[0], *a1 = nt > 1 ? tok[1] : "";
    int n = -1;
    if (!known(cmd)) die("unknown command %s", cmd);
    if (nt < 2 && strcmp(cmd, "wavend")) die("%s: argument missing", cmd);
    if (nt > 2 && (!strcmp(cmd, "press") || !strcmp(cmd, "hold"))) n = count(tok[2]);
    if (!strcmp(cmd, "wait")) { run(count(a1)); }
    else if (!strcmp(cmd, "press")) { joy = btn(a1); run(n > 0 ? n : 2); joy = 0; run(1); }
    else if (!strcmp(cmd, "hold")) { joy = btn(a1); run(n > 0 ? n : 1); joy = 0; }
    else if (!strcmp(cmd, "shot")) { shot(a1); }
    else if (!strcmp(cmd, "wav")) { if (wavf) { wavhdr(wavf, wavbytes); fclose(wavf); } wavf = fopen(a1, "wb+"); if (!wavf) die("cannot write %s", a1); wavbytes = 0; uint8_t z[44] = {0}; fwrite(z, 44, 1, wavf); }
    else if (!strcmp(cmd, "wavend")) { if (wavf) { wavhdr(wavf, wavbytes); fclose(wavf); wavf = NULL; } }
    else if (!strcmp(cmd, "wram") && nt >= 4) {
      long a = wram_addr(tok[2]), len; FILE *f;
      if (!number(tok[3], 0, &len) || len <= 0) die("bad length %s", tok[3]);
      uint8_t *m = wram(a, len);
      f = fopen(a1, "wb"); if (!f) die("cannot write %s", a1);
      fwrite(m, 1, len, f); fclose(f);
    }
    else if (!strcmp(cmd, "wram") && nt == 3) die("wram FILE [ADDR LEN]");
    else if (!strcmp(cmd, "wram")) dump(RETRO_MEMORY_SYSTEM_RAM, a1);
    else if (!strcmp(cmd, "sram")) dump(RETRO_MEMORY_SAVE_RAM, a1);
    else if (!strcmp(cmd, "vram")) dump(RETRO_MEMORY_VIDEO_RAM, a1);
    else if (!strcmp(cmd, "loadsram")) {
      FILE *f = fopen(a1, "rb"); if (!f) die("cannot read %s", a1);
      if (fread(p_mem(RETRO_MEMORY_SAVE_RAM), 1, p_memsize(RETRO_MEMORY_SAVE_RAM), f)) {}
      fclose(f);
    }
    else if (!strcmp(cmd, "map")) { if (nt < 2) die("map FILE"); map_path = strdup(a1); syms_loaded = 0; nsym = 0; }
    else if (!strcmp(cmd, "poke") || !strcmp(cmd, "pokew") || !strcmp(cmd, "pokel")) {
      int w = cmd[4] == 'w' ? 2 : cmd[4] == 'l' ? 4 : 1, k;
      if (nt < 3) die("%s ADDR VALUE ...", cmd);
      long a = wram_addr(a1);
      uint8_t *m = wram(a, (long)w * (nt - 2));
      for (i = 2; i < nt; i++) { long v = value(tok[i], w); for (k = 0; k < w; k++) *m++ = (uint8_t)(v >> (8 * k)); }
    }
    else if (!strcmp(cmd, "peek")) {
      if (nt < 2) die("peek ADDR [N]");
      long a = wram_addr(a1), len = 1;
      if (nt > 2 && (!number(tok[2], 0, &len) || len <= 0)) die("bad length %s", tok[2]);
      uint8_t *m = wram(a, len);
      printf("peek %s %05lx:", a1, a);
      for (i = 0; i < len; i++) printf(" %02x", m[i]);
      printf("  (frame %lu)\n", frames);
    }
    else if (!strcmp(cmd, "waitfor")) {
      if (nt < 3) die("waitfor ADDR VALUE [MAX]");
      long a = wram_addr(a1), v = value(tok[2], 1), max = 600, k;
      if (nt > 3 && (!number(tok[3], 0, &max) || max < 0)) die("bad frame count %s", tok[3]);
      uint8_t *m = wram(a, 1);
      for (k = 0; k < max && *m != (uint8_t)v; k++) run(1);
      if (*m != (uint8_t)v) { fprintf(stderr, "runrom: line %d: waitfor %s %s: timeout after %ld frames (byte is %02x)\n", lineno, a1, tok[2], max, *m); status = 3; }
    }
    else die("unknown command %s", cmd);
  }
  if (wavf) { wavhdr(wavf, wavbytes); fclose(wavf); }
  printf("frames %lu\n", frames);
  return status;
}
