#include "game.h"

void main(void) {
  u8 sc = SC_TITLE;
  for (;;) {
    switch (sc) {
    case SC_TITLE: sc = title_scene(); break;
    default: sc = SC_TITLE; break;
    }
  }
}
