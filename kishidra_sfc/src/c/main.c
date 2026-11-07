#include "game.h"

static Loadout test_lo;

void main(void) {
  u8 sc = SC_TITLE;
  for (;;) {
    switch (sc) {
    case SC_TITLE: sc = title_scene(); break;
    default:
      test_lo.w[0].item = ITEM_START_WEAPON; test_lo.w[0].nsk = 2; test_lo.w[0].sk[0] = SK_KIRIAGE; test_lo.w[0].sk[1] = SK_SLASH;
      test_lo.w[1].item = 10; test_lo.w[1].nsk = 2; test_lo.w[1].sk[0] = SK_TSUKI; test_lo.w[1].sk[1] = SK_JAVELIN;
      test_lo.w[2].item = 30; test_lo.w[2].nsk = 1; test_lo.w[2].sk[0] = SK_TATAKI;
      test_lo.shield = ITEM_EQUIP_SHIELD;
      battle_scene(sc == SC_MAP ? AR_ARCADIA : AR_PLAIN, sc == SC_MAP ? 1 : 5, &test_lo);
      sc = SC_TITLE;
      break;
    }
  }
}
