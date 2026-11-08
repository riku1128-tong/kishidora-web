; arithmetic for C with the hardware multiplier and divider (main code only: no interrupt
; handler uses them).  cc65's 32-bit multiply and divide take a thousand cycles and more each,
; and a hit computes its damage with ten of them.
.include "snes.inc"

.export _pa
.importzp sp

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

; void mul_now(void): pa = pa[0] * pp (16 x 16 -> 32 bits)
CFUNC mul_now
  php
  phb
  A8
  lda #$80
  pha
  plb
  AXY16
  lda _pa
  sta m_a
  lda _pp
  sta m_b
  jsr mul16
  lda m_lo
  sta _pa
  lda m_hi
  sta _pa+2
  plb
  plp
  rtl

; void div_now(void): pa /= pp (32 bits by the divisor 2..255; the low byte of pp is used)
CFUNC div_now
  php
  phb
  A8
  lda #$80
  pha
  plb
  AXY16
  lda _pp
  and #$00ff
  jsr div8
  plb
  plp
  rtl

; ---------------------------------------------------------------- random numbers
; The port's xorshift (s ^= s << 7; s ^= s >> 9; s ^= s << 8), moved here from sys.c: the same
; sequence from the same boot value, in 16-bit registers.  _rnd_state is the state (C: rnd_state; the
; tower snapshot saves it).  rnd_range() (sys.c) and rnd_below() take (r * n) >> 16 with the hardware
; multiplier for n < 256: r * n = rh * n * 256 + rl * n, so (r * n) >> 16 = (rh * n + (rl * n >> 8)) >> 8
; exactly (the C value).
.export _rnd_state
.segment "DATA"
_rnd_state: .word $2b7e       ; (crt0 copies DATA to WRAM at power-on: the C version's boot value)
.segment "ENGINE"

; u16 rnd(void): the next state (A = low, X = high byte).  DB = $7E (the C data bank, as every CFUNC is
; entered): _rnd_state is in DATA ($7E:2000+, a 16-bit address)
CFUNC rnd
  php
  A16
  lda _rnd_state
  sta m_lo
  asl
  asl
  asl
  asl
  asl
  asl
  asl
  eor m_lo
  sta m_lo             ; s ^= s << 7
  xba
  and #$00ff
  lsr
  eor m_lo
  sta m_lo             ; s ^= s >> 9
  xba
  and #$ff00
  eor m_lo             ; s ^= s << 8
  sta _rnd_state
  plp
  .a8
  xba
  tax                  ; X = the high byte
  xba                  ; A = the low byte
  rtl

; (r * n) >> 16 of m_a = r, m_b = n (0..255) in A (A8 on entry, A16 on exit; DB $80)
mul_rn:
  .a8
  lda m_a
  sta WRMPYA
  lda m_b
  sta WRMPYB           ; rl * n
  nop
  nop
  nop
  nop
  A16
  lda RDMPYL
  xba
  and #$00ff
  sta m_c              ; (rl * n) >> 8
  A8
  lda m_a+1
  sta WRMPYA
  lda m_b
  sta WRMPYB           ; rh * n
  nop
  nop
  nop
  nop
  A16
  lda RDMPYL
  clc
  adc m_c              ; <= 255 * 255 + 254: no carry
  xba
  and #$00ff           ; >> 8
  rts

; u8 __fastcall__ rnd_mul8(u16 r, u8 n): (r * n) >> 16.  C stack: r; A = n.
CFUNC rnd_mul8
  php
  phb
  AXY16
  and #$00ff
  sta m_b
  lda (sp)
  sta m_a
  lda sp
  clc
  adc #2
  sta sp
  A8
  lda #$80
  pha
  plb
  jsr mul_rn
  plb
  plp
  .a8
  .i8
  ldx #0
  rtl

; u8 __fastcall__ rnd_below(u8 n): the next rnd() times n, >> 16 (= rnd_range(0, n - 1) for n 1..255,
; the same value and the same state after it)
CFUNC rnd_below
  php
  phb
  AXY16
  and #$00ff
  sta m_b
  AXY8
  jsl rnd_far          ; A = low, X = high (it keeps the register sizes it was called with; DB still $7E)
  sta m_a
  stx m_a+1
  lda #$80
  pha
  plb
  jsr mul_rn
  plb
  plp
  .a8
  .i8
  ldx #0
  rtl
