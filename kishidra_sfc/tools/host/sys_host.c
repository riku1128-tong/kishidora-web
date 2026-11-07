/* the parts of src/c/sys.c the rules use (no hardware) */
#include "sys.h"
static u16 seed = 0x2b7e;
void rnd_seed(u16 s) { seed = s ? s : 0x2b7e; }
u16 rnd(void) { seed ^= seed << 7; seed ^= seed >> 9; seed ^= seed << 8; return seed; }
u16 rnd_range(u16 lo, u16 hi) { u16 n = hi - lo + 1; if (hi <= lo) return lo; return lo + (u16)(((u32)rnd() * n) >> 16); }
u8 rnd_pct(u16 p) { return p > 0 && rnd_range(0, 99) < p; }
u16 mul_pct16(u16 v, u16 p) { return (u16)(((u32)v * p) / 100); }
u32 mul_pct(u32 v, u16 p) { if (p == 100) return v; return (v * p) / 100; }
