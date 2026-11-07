#include "game.h"
#include "adventure.h"
#include "story.h"

static Loadout lo;

void main(void) {
  u8 sc = SC_TITLE, mode = MS_NORMAL, r, d;
  if (!save_load()) save_new();
  for (;;) {
    switch (sc) {
    case SC_TITLE:
      r = title_scene(save_exists());
      if (r == SC_NEWGAME) {
        save_new();
        d = difficulty_select(DIFF_NORMAL, 0);
        sv.difficulty = d;
        save_write();
        mode = MS_NEWGAME;
      } else {
        if (!save_load()) { save_new(); mode = MS_NEWGAME; }
        else mode = MS_NORMAL;
      }
      sc = SC_MAP;
      break;
    case SC_MAP:
      r = map_scene(mode);
      mode = MS_NORMAL;
      sc = r == MA_BATTLE ? SC_BATTLE : SC_TITLE;
      break;
    case SC_BATTLE:
      adv_loadout(&lo);
      adv_prepare(map_area, map_men);
      r = battle_scene(map_area, map_men, &lo);
      result_scene(r);
      mode = MS_AFTER;
      sc = SC_MAP;
      break;
    }
  }
}
