; arithmetic for C with the hardware multiplier and divider (main code only: no interrupt
; handler uses them).  cc65's 32-bit multiply and divide take a thousand cycles and more each,
; and a hit computes its damage with ten of them.
.include "snes.inc"

.export _pa

.segment "LOWBSS"
_pa:   .res 4          ; the 32-bit argument and result (C: pa[0] low, pa[1] high)
_pp:   .res 2          ; ... and the percentage
m_a:   .res 2
m_b:   .res 2
m_00:  .res 2
m_01:  .res 2
m_10:  .res 2
m_11:  .res 2
m_mid: .res 2
m_c:   .res 2
m_lo:  .res 2
m_hi:  .res 2
.export _pp

.segment "ENGINE"

; m_hi:m_lo = m_a * m_b (16 x 16 -> 32 bits; A16 I16, DB $80)
mul16:
  .a16
  .i16
  A8
  lda m_a
  sta WRMPYA
  lda m_b
  sta WRMPYB           ; a0 * b0
  nop
  nop
  nop
  nop
  A16
  lda RDMPYL
  sta m_00
  A8
  lda m_b+1
  sta WRMPYB           ; a0 * b1
  nop
  nop
  nop
  nop
  A16
  lda RDMPYL
  sta m_01
  A8
  lda m_a+1
  sta WRMPYA
  lda m_b
  sta WRMPYB           ; a1 * b0
  nop
  nop
  nop
  nop
  A16
  lda RDMPYL
  sta m_10
  A8
  lda m_b+1
  sta WRMPYB           ; a1 * b1
  nop
  nop
  nop
  nop
  A16
  lda RDMPYL
  sta m_11
  ; a*b = m_00 + (m_01 + m_10) << 8 + m_11 << 16
  lda m_01
  clc
  adc m_10
  sta m_mid
  lda #0
  rol
  sta m_c              ; bit 16 of the middle sum
  lda m_mid
  xba
  and #$ff00
  clc
  adc m_00
  sta m_lo
  lda m_mid
  xba
  and #$00ff
  adc m_11             ; (+ the carry out of the low word)
  sta m_hi
  lda m_c
  beq :+
  lda m_hi
  clc
  adc #$0100
  sta m_hi
:
  rts

; pa = pa / A (A16, the divisor 2..255; A16 I16, DB $80); A = the remainder
div8:
  .a16
  .i16
  sta m_b
  ldx #3               ; bytes from the top: x = remainder * 256 + byte < 65536
  lda #0
  sta m_c              ; remainder
@l:
  lda m_c
  xba
  and #$ff00
  A8
  ora _pa,x
  A16
  sta WRDIVL
  A8
  lda m_b
  sta WRDIVB           ; 16 cycles
  nop
  nop
  nop
  nop
  nop
  nop
  nop
  nop
  lda RDDIVL           ; quotient < 256
  sta _pa,x
  A16
  lda RDMPYL           ; remainder
  sta m_c
  dex
  bpl @l
  lda m_c
  rts

; void pct_now(void): pa = (pa * pp, modulo 2^32) / 100 -- mul_pct exactly as (v * p) / 100
CFUNC pct_now
  php
  phb
  A8
  lda #$80
  pha
  plb
  AXY16
  lda _pa+2            ; high word * p: only its low 16 bits are kept
  sta m_a
  lda _pp
  sta m_b
  jsr mul16
  lda m_lo
  pha
  lda _pa
  sta m_a
  jsr mul16            ; (m_b = p still)
  pla
  clc
  adc m_hi
  sta _pa+2
  lda m_lo
  sta _pa
  lda #100
  jsr div8
  plb
  plp
  rtl

; u8 div10_now(void): pa /= 10, returns the remainder (the last digit)
CFUNC div10_now
  php
  phb
  A8
  lda #$80
  pha
  plb
  AXY16
  lda #10
  jsr div8
  plb
  plp
  .a8
  .i8
  ldx #0               ; (cc65 return: A, X = high byte)
  rtl
