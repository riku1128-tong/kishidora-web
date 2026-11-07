; battery SRAM ($70:0000-$7FFF, 32KB) <-> WRAM copies with a 16-bit byte sum
.include "snes.inc"

.import _ea

.segment "ENGZP": zeropage
sr_w:   .res 3
sr_s:   .res 3
sr_sum: .res 2

.segment "ENGINE"

; common setup: ea0 = WRAM address (bank $7E), ea1 = SRAM offset, ea2 = length
sr_setup:
  .a16
  .i16
  lda _ea+0
  sta sr_w
  lda _ea+2
  sta sr_s
  A8
  lda #$7e
  sta sr_w+2
  lda #$70
  sta sr_s+2
  A16
  stz sr_sum
  rts

; void sram_write(void): WRAM -> SRAM; ea3 = sum of the bytes
CFUNC sram_write
  php
  AXY16
  jsr sr_setup
  ldy #0
@l:
  cpy _ea+4
  beq @d
  A8
  lda [sr_w],y
  sta [sr_s],y
  A16
  and #$00ff
  clc
  adc sr_sum
  sta sr_sum
  iny
  bra @l
@d:
  lda sr_sum
  sta _ea+6
  plp
  rtl

; void sram_read(void): SRAM -> WRAM; ea3 = sum of the bytes
CFUNC sram_read
  php
  AXY16
  jsr sr_setup
  ldy #0
@l:
  cpy _ea+4
  beq @d
  A8
  lda [sr_s],y
  sta [sr_w],y
  A16
  and #$00ff
  clc
  adc sr_sum
  sta sr_sum
  iny
  bra @l
@d:
  lda sr_sum
  sta _ea+6
  plp
  rtl

; void wram_sum(void): ea0 = WRAM address, ea2 = length -> ea3 = sum
CFUNC wram_sum
  php
  AXY16
  jsr sr_setup
  ldy #0
@l:
  cpy _ea+4
  beq @d
  lda [sr_w],y
  and #$00ff
  clc
  adc sr_sum
  sta sr_sum
  iny
  bra @l
@d:
  lda sr_sum
  sta _ea+6
  plp
  rtl
