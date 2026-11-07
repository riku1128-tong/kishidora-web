/* native test shim: stands in for the asm engine so the game rules run under gcc */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sfc.h"
#include "gdata.h"
u16 ea[16];
extern const unsigned short estat_index_host[];
extern const unsigned char estat_data_host[];
void host_estat(u8 area, u8 men, EStat *dst, u8 *n) {
  unsigned i = (area * 21 + men) * 2;
  unsigned off = estat_index_host[i], c = estat_index_host[i + 1];
  if (c > 24) c = 24;
  memcpy(dst, estat_data_host + off, c * 24);
  *n = c;
}
void estat_block(void) {}
