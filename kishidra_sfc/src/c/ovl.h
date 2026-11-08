/* The overlay arena (docs/plan/PLAN.md §5.4): 1,536 bytes of WRAM $7E shared by EITHER the battle
 * (particles, effect recipes, intro / rest / aura state) OR the menu screen that is open (treasure /
 * gacha views, forge rows and guides, STAGE detail lines, ...).  A user lays its state out as one
 * struct and reaches it as ((T *)ovl)->field, which cc65 compiles to absolute addresses (no pointer, no
 * runtime cost).  Rule: nothing in the arena survives a screen change (a screen sets up what it needs
 * when it opens; the treasure view starts after battle_scene returned).  OVL_FITS(T) fails the compile
 * when T is larger than the arena. */
#ifndef OVL_H
#define OVL_H
#include "sfc.h"

#define OVL_SIZE 1536
extern u8 ovl[OVL_SIZE];          /* sys.c */
#define OVL(T) ((T *)ovl)
#define OVL_FITS(T) extern char ovl_fits_##T[OVL_SIZE / sizeof(T)]   /* (size 0 = an error) */

#endif
