/* brr: encode mono s16le PCM to SNES BRR (9-byte blocks).
 * usage: brr in.raw out.brr [-end] [-f0 block ...]
 *   -end        set the END flag on the last block (one-shot sound effect)
 *   -f0 N ...   force filter 0 at these block indices (stream entry points: start, loop)
 * Every block is chosen by trying all 4 filters x 13 shifts against the exact DSP decoder
 * (bsnes / fullsnes behaviour, 15-bit wrap included). Build: gcc -O2 -o brr brr.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clamp16(int v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }

/* decode one nibble into the buffer (p1 = last output, p2 = the one before) */
static int dec(int nib, int shift, int filter, int p1, int p2) {
  int s = nib;                       /* -8..7 */
  if (shift <= 12) { s = (s << shift) >> 1; } else { s = s < 0 ? -2048 : 0; }
  int q2 = p2 >> 1;
  if (filter == 1) { s += p1 >> 1; s += (-p1) >> 5; }
  else if (filter == 2) { s += p1; s -= q2; s += q2 >> 4; s += (p1 * -3) >> 6; }
  else if (filter == 3) { s += p1; s -= q2; s += (p1 * -13) >> 7; s += (q2 * 3) >> 4; }
  s = clamp16(s);
  s = (short)(s << 1);
  return s;
}

/* try one filter/shift; returns squared error, fills nibbles and the final history */
static double try_block(const short *x, int shift, int filter, int p1, int p2, int *nib, int *o1, int *o2) {
  double err = 0;
  for (int i = 0; i < 16; ++i) {
    int best = 0, bout = 0;
    double be = 1e300;
    /* the ideal nibble, then test its neighbours (rounding and clamping) */
    int base = dec(0, shift, filter, p1, p2);
    double ideal = shift <= 12 ? (double)(x[i] - base) / (double)(1 << shift) : 0;
    int c = (int)(ideal >= 0 ? ideal + 0.5 : ideal - 0.5);
    for (int n = c - 1; n <= c + 1; ++n) {
      int nn = n < -8 ? -8 : (n > 7 ? 7 : n);
      int out = dec(nn, shift, filter, p1, p2);
      double e = (double)(out - x[i]) * (double)(out - x[i]);
      if (e < be) { be = e; best = nn; bout = out; }
    }
    nib[i] = best;
    err += be;
    p2 = p1; p1 = bout;
  }
  *o1 = p1; *o2 = p2;
  return err;
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: brr in.raw out.brr [-end] [-f0 n...]\n"); return 1; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f) / 2;
  fseek(f, 0, SEEK_SET);
  long nb = (n + 15) / 16;
  short *x = calloc(nb * 16, sizeof(short));
  if (fread(x, 2, n, f) != (size_t)n) { fprintf(stderr, "short read\n"); return 1; }
  fclose(f);
  int end = 0;
  char *f0 = calloc(nb + 1, 1);
  for (int i = 3; i < argc; ++i) {
    if (!strcmp(argv[i], "-end")) end = 1;
    else if (!strcmp(argv[i], "-f0")) { while (i + 1 < argc && argv[i + 1][0] != '-') { long b = atol(argv[++i]); if (b >= 0 && b < nb) f0[b] = 1; } }
  }
  FILE *o = fopen(argv[2], "wb");
  int p1 = 0, p2 = 0;
  double total = 0;
  for (long b = 0; b < nb; ++b) {
    const short *blk = x + b * 16;
    double be = 1e300;
    int bn[16], bs = 0, bf = 0, b1 = 0, b2 = 0;
    for (int filter = 0; filter < (f0[b] ? 1 : 4); ++filter)
      for (int shift = 0; shift <= 12; ++shift) {
        int nib[16], o1, o2;
        double e = try_block(blk, shift, filter, p1, p2, nib, &o1, &o2);
        if (e < be) { be = e; memcpy(bn, nib, sizeof(bn)); bs = shift; bf = filter; b1 = o1; b2 = o2; }
      }
    total += be;
    unsigned char out[9];
    out[0] = (unsigned char)((bs << 4) | (bf << 2) | (end && b == nb - 1 ? 1 : 0));
    for (int i = 0; i < 8; ++i) out[1 + i] = (unsigned char)(((bn[2 * i] & 15) << 4) | (bn[2 * i + 1] & 15));
    fwrite(out, 1, 9, o);
    p1 = b1; p2 = b2;
  }
  fclose(o);
  fprintf(stderr, "%ld blocks, rms err %.1f\n", nb, nb ? __builtin_sqrt(total / (nb * 16)) : 0);
  return 0;
}
