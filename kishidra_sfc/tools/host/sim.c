/* battle simulation with a simple bot: python build.py host  (see tools/host) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "battle.h"
#include "text.h"

static const char *evname(u8 t) {
  static char b[8];
  sprintf(b, "%d", t);
  return b;
}

/* skill-only bot that guards at a set accuracy */
static int run(u8 area, u8 men, int guard_skill, int verbose, u32 seed) {
  Loadout lo;
  Ev e;
  int frames = 0, guard_at = -1, i;
  memset(&lo, 0, sizeof lo);
  rnd_seed((u16)seed);
  /* a mid-game loadout: three weapons with two skills each */
  lo.w[0].item = 0; lo.w[0].nsk = 2; lo.w[0].sk[0] = SK_KIRIAGE; lo.w[0].sk[1] = SK_SLASH;
  lo.w[1].item = 10; lo.w[1].nsk = 2; lo.w[1].sk[0] = SK_TSUKI; lo.w[1].sk[1] = SK_JAVELIN;
  lo.w[2].item = 30; lo.w[2].nsk = 2; lo.w[2].sk[0] = SK_TATAKI; lo.w[2].sk[1] = SK_UPPER;
  for (i = 0; i < 3; ++i) lo.w[i].pos = 0;
  lo.shield = ITEM_SHIELD0 + 5;
  lo.shield_plus = 0;
  b_men_cleared = 0;
  battle_start(area, men, &lo);
  while (frames < 60 * 60 * 20) {
    if (b_phase == PH_TALK) battle_press_next();
    if (battle_can_command()) {
      u8 s = (u8)(frames / 7 % 3);
      if (b_st < 3) battle_press_meat(); else battle_press_skill(s);
    }
    battle_step();
    while (ev_pop(&e)) {
      if (verbose) printf("%6d ev %s e%d a%d b%d c%d v%lu  hp %lu st %d\n", frames, evname(e.type), e.enemy, e.a, e.b, e.c, (unsigned long)e.v,
                          (unsigned long)b_hp, b_st);
      if (e.type == EV_ENEMY_WINDUP) {
        int r = rand() % 100;
        guard_at = r < guard_skill ? frames + 6 + (rand() % 9) : -1;   /* 12 windup + 4 hold frames */
      }
    }
    if (guard_at >= 0 && frames >= guard_at) { battle_press_guard(); guard_at = -1; }
    if (b_phase == PH_VICTORY) return 1;
    if (b_phase == PH_GAMEOVER) return 0;
    ++frames;
  }
  return -1;
}

int main(int argc, char **argv) {
  int area = argc > 1 ? atoi(argv[1]) : 0, men = argc > 2 ? atoi(argv[2]) : 1, n = argc > 3 ? atoi(argv[3]) : 1, g = argc > 4 ? atoi(argv[4]) : 80;
  int win = 0, k;
  for (k = 0; k < n; ++k) win += run((u8)area, (u8)men, g, n == 1, 1234 + k * 77) == 1;
  printf("area %d men %d: %d/%d wins (guard %d%%)\n", area, men, win, n, g);
  return 0;
}
