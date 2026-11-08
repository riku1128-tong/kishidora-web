; sprites: the OAM shadow (_oam, 512 + 32 bytes in low WRAM) written by the C screens
; (formerly C in sys.c; these run every frame for every sprite, so they are asm)
; Called from C with DBR = $7E: absolute addresses below $2000 are low WRAM.
.include "snes.inc"

.importzp sp
.import _oam, _oam_next, _ea
.export _oam_used, _oam_top

.segment "LOWBSS"
o_x:    .res 2
o_y:    .res 2
o_ta:   .res 2
o_bits: .res 2
o_keep: .res 2
m_n:    .res 2
o_acc:  .res 2          ; high-table bits of the current group of 4 slots (filled from bit 7 down)
o_tmp:  .res 2
_oam_used: .res 2       ; slots that may show something (multiple of 4); oam_finish hides up to here
_oam_top:  .res 2       ; slots from here on are kept (a screen's fixed sprites), 128 = none (oam_clear)

.segment "ENGINE"

; high table: per sprite 2 bits (x bit 8, large) at bit (i & 3) * 2 of byte 512 + i / 4
hi_keep: .word $fffc, $fff3, $ffcf, $ff3f
hi_bits: .word $00, $01, $02, $03,  $00, $04, $08, $0c,  $00, $10, $20, $30,  $00, $40, $80, $c0

; A16 I16: one sprite from o_x, o_y, o_ta, o_bits (0 small, 2 large); X is kept.
; Sprites off the screen take no slot.  The 2 high-table bits (x bit 8, large) of a group
; of 4 slots are collected in o_acc and stored when the group is full (oam_finish stores
; a group left part-filled).
put_spr:
  .a16
  .i16
  lda o_y              ; visible: -64 <= y < 224 and -64 < x < 256
  clc
  adc #64
  cmp #224 + 64
  bcs @out
  lda o_x
  clc
  adc #63
  cmp #256 + 63
  bcs @out
  lda _oam_next
  and #$00ff
  cmp _oam_top
  bcs @out
  phx
  tay                  ; slot
  asl
  asl
  tax
  lda o_ta
  sta _oam+2,x
  sep #$20
  .a8
  lda o_x
  sta _oam,x
  lda o_y
  sta _oam+1,x
  lda o_x+1
  asl                  ; carry = x < 0
  lda o_bits
  adc #0               ; 0..3
  lsr
  ror
  ror                  ; bits 0-1 -> 7-6
  sta o_tmp
  tya
  and #3
  beq @first
  lda o_acc
  lsr
  lsr
  ora o_tmp
  sta o_acc
  tya
  and #3
  cmp #3
  bne @next
  rep #$20
  .a16
  tya
  lsr
  lsr
  tax
  sep #$20
  .a8
  lda o_acc
  sta _oam+512,x
  bra @next
@first:
  lda o_tmp
  sta o_acc
@next:
  tya
  inc a
  sta _oam_next
  rep #$20
  .a16
  plx
@out:
  rts

; void __fastcall__ spr_add(s16 x, s16 y, u16 tile_attr, u8 large)
;   tile_attr = tile (9 bits) | pal << 9 | prio << 12 | hflip << 14 | vflip << 15
;   C stack: tile_attr, y, x (popped here); A = large
CFUNC spr_add
  php
  rep #$30
  .a16
  .i16
  and #$00ff
  beq :+
  lda #2
:
  sta o_bits
  lda (sp)
  sta o_ta
  ldy #2
  lda (sp),y
  sta o_y
  ldy #4
  lda (sp),y
  sta o_x
  lda sp
  clc
  adc #6
  sta sp
  jsr put_spr
  plp
  rtl

; void spr_list(void): ea0 = entries in $7E (s16 x, s16 y, u16 tile_attr, u16 large), ea1 = count
CFUNC spr_list
  php
  rep #$30
  .a16
  .i16
  ldx _ea+0
  lda _ea+2
  beq @done
  sta m_n
@one:
  lda a:0,x
  sta o_x
  lda a:2,x
  sta o_y
  lda a:4,x
  sta o_ta
  lda a:6,x
  beq :+
  lda #2
:
  sta o_bits
  jsr put_spr
  txa
  clc
  adc #8
  tax
  dec m_n
  bne @one
@done:
  plp
  rtl

; void meta_add(void): ea0 = pose descriptor in $7E (count at +4, then dx, dy, cell per
; piece), ea1 = first tile of the strip, ea2 = x, ea3 = y, ea4 = attribute bits (pal, prio).
; Each piece is a 32x32 cell: tile = strip + (cell / 4) * 64 + (cell % 4) * 4.
CFUNC meta_add
  php
  rep #$30
  .a16
  .i16
  ldx _ea+0
  lda a:4,x
  and #$00ff
  beq @done
  sta m_n
  txa
  clc
  adc #5
  tax
@piece:
  lda a:0,x            ; dx (s8)
  and #$00ff
  cmp #$0080
  bcc :+
  ora #$ff00
:
  clc
  adc _ea+4
  sta o_x
  lda a:1,x            ; dy (s8)
  and #$00ff
  cmp #$0080
  bcc :+
  ora #$ff00
:
  clc
  adc _ea+6
  sta o_y
  lda a:2,x            ; cell
  and #$00fc
  asl
  asl
  asl
  asl
  sta o_ta             ; (cell / 4) * 64
  lda a:2,x
  and #$0003
  asl
  asl
  clc
  adc o_ta
  adc _ea+2
  ora _ea+8
  sta o_ta
  lda #2
  sta o_bits
  jsr put_spr
  inx
  inx
  inx
  dec m_n
  bne @piece
@done:
  plp
  rtl

; void oam_finish(void): hide the slots after the last sprite of this frame (y 240, small,
; x bit 8 set) up to the ones the last frame used, and start the next frame at slot 0
CFUNC oam_finish
  php
  rep #$30
  .a16
  .i16
  lda _oam_next
  and #$00ff
  tay
  and #3
  beq @groups
  sep #$20
  .a8
@pad:                  ; the rest of a part-filled group
  rep #$20
  .a16
  tya
  asl
  asl
  tax
  sep #$20
  .a8
  lda #240
  sta _oam+1,x
  lda o_acc
  lsr
  lsr
  ora #$40             ; %01 << 6
  sta o_acc
  iny
  tya
  and #3
  bne @pad
  rep #$20
  .a16
  tya
  dec a
  lsr
  lsr
  tax
  sep #$20
  .a8
  lda o_acc
  sta _oam+512,x
  rep #$20
  .a16
@groups:               ; whole groups up to what the last frame used
  cpy _oam_used
  bcs @done
  tya
  asl
  asl
  tax
  sep #$20
  .a8
  lda #240
  sta _oam+1,x
  sta _oam+5,x
  sta _oam+9,x
  sta _oam+13,x
  rep #$20
  .a16
  tya
  lsr
  lsr
  tax
  sep #$20
  .a8
  lda #$55
  sta _oam+512,x
  rep #$20
  .a16
  tya
  clc
  adc #4
  tay
  bra @groups
@done:
  lda _oam_next
  and #$00ff
  clc
  adc #3
  and #$00fc
  sta _oam_used
  sep #$20
  .a8
  stz _oam_next
  plp
  rtl

.ifdef PROFILE
; u16 prof_line(void): the scanline now (0..261), for timing in profiling builds
CFUNC prof_line
  php
  rep #$30
  .a16
  .i16
  sep #$20
  .a8
  lda f:$2137
  lda f:$213F
  lda f:$213D
  xba
  lda f:$213D
  and #1
  xba
  rep #$20
  .a16
  and #$01ff
  sta f:_ea+30
  plp
  lda f:_ea+31
  tax
  lda f:_ea+30
  rtl
.endif

; ---------------------------------------------------------------- fast memory helpers (C)
.segment "ENGINE"
zero_byte: .byte 0

; void wram_zero(void): ea0 = address, ea1 = bank ($7E/$7F), ea2 = length (> 0).
; DMA channel 2 through the WRAM port (the NMI uses channel 0).
CFUNC wram_zero
  php
  rep #$30
  .a16
  .i16
  lda f:_ea+0
  sta f:WMADDL
  sep #$20
  .a8
  lda f:_ea+2
  sta f:WMADDH
  lda #^zero_byte
  sta f:$4324
  rep #$20
  .a16
  lda #$8008           ; fixed source -> WMDATA
  sta f:$4320
  lda #.loword(zero_byte)
  sta f:$4322
  lda f:_ea+4
  sta f:$4325
  sep #$20
  .a8
  lda #$04
  sta f:MDMAEN
  plp
  rtl

; void mem_copy(void): ea0 = source, ea1 = destination, ea2 = length (> 0), all in $7E.
; (MVN copies forwards; DBR stays $7E for the C code)
CFUNC mem_copy
  php
  phb
  rep #$30
  .a16
  .i16
  ldx _ea+0
  ldy _ea+2
  lda _ea+4
  dec a
  mvn #$7e, #$7e
  plb
  plp
  rtl

; void oam_clear(void): every slot off the screen and small, slot 0 next
CFUNC oam_clear
  php
  rep #$30
  .a16
  .i16
  ldx #0
@l:
  lda #240 << 8        ; x 0, y 240
  sta _oam,x
  stz _oam+2,x
  inx
  inx
  inx
  inx
  cpx #512
  bcc @l
  lda #$5555
@h:
  sta _oam,x
  inx
  inx
  cpx #544
  bcc @h
  stz _oam_used
  lda #128
  sta _oam_top
  sep #$20
  .a8
  stz _oam_next
  plp
  rtl
