; text boxes on BG3: 12-dot glyphs rendered into a 2bpp tile canvas ($7F:0000 = BG3 tile 0)
.include "snes.inc"

.import text_table, font_glyphs, font_width, vq_push
.importzp FONT_DIGIT0
.import _ea, _bg3map, _bgmap_dirty
.export _targ_num, _targ_num2, _targ_str, _hero_name, _tb
.import _bg3_vram

CANVAS_BANK = $7f
BG3_TILE_VRAM = $6000
NBOX = 12

.segment "LOWBSS"
_tb:        .res NBOX*8      ; x, y, w, h, tile lo, tile hi, pal, flags (bit0 opaque, bit1 frame)
_targ_num:  .res 4           ; {n}
_targ_num2: .res 4           ; {m}
_targ_str:  .res 2           ; {s} (text id)
_hero_name: .res 16          ; {name}: up to 7 glyph codes + 0

.segment "ENGZP": zeropage
t_box:    .res 2
t_w:      .res 2             ; box width in tiles
t_h:      .res 2
t_base:   .res 2             ; box first tile
t_px:     .res 2
t_py:     .res 2
t_x0:     .res 2             ; start x (for new lines)
t_src:    .res 3             ; string pointer (long)
t_save:   .res 3             ; saved pointer while expanding {s}
t_glyph:  .res 3             ; glyph pointer (long)
t_gw:     .res 2
t_row:    .res 2
t_bits:   .res 2
t_tmp:    .res 2
t_tmp2:   .res 2
t_dst:    .res 3             ; canvas pointer (long)
t_plane:  .res 2             ; 0 = plane 0 (text), 1 = plane 1 (shadow)
t_pal:    .res 2
t_numbuf: .res 12
t_numptr: .res 2
t_nest:   .res 2
t_tx:     .res 2
t_off:    .res 2
t_ty1:    .res 2
t_tx1:    .res 2
t_attr:   .res 2
t_flags:  .res 2
t_fx:     .res 2
t_fy:     .res 2
m_a:      .res 2
m_b:      .res 2
g_w16:    .res 2             ; fast blit: box width * 16
g_sl:     .res 2
g_suby:   .res 2
g_ty:     .res 2
g_b:      .res 1
g_c:      .res 1
g_op:     .res 1

.segment "ENGINE"

; load box A (0..NBOX-1) into DP vars (16-bit A/X/Y on entry/exit)
box_load:
  .a16
  .i16
  and #$00ff
  sta t_box
  asl
  asl
  asl
  tax
  lda _tb+2,x
  and #$00ff
  sta t_w
  lda _tb+3,x
  and #$00ff
  sta t_h
  lda _tb+4,x
  sta t_base
  lda _tb+6,x
  and #$00ff
  sta t_pal
  lda _tb+7,x
  and #$00ff
  sta t_flags
  rts

; void tb_open(u8 box): ea0 = x | y<<8, ea1 = w | h<<8, ea2 = tile, ea3 = pal | flags<<8 (1 opaque, 2 frame)
CFUNC tb_open
  php
  phb
  AXY16
  pha
  A8
  lda #$80
  pha
  plb
  A16
  pla
  and #$00ff
  asl
  asl
  asl
  tax
  lda _ea+0
  sta _tb+0,x
  lda _ea+2
  sta _tb+2,x
  lda _ea+4
  sta _tb+4,x
  lda _ea+6
  sta _tb+6,x
  txa
  lsr
  lsr
  lsr
  jsr box_load
  jsr box_map
  jsr box_clear
  plb
  plp
  rtl

; write the box's tilemap entries into bg3map (in $7E)
box_map:
  .a16
  .i16
  lda t_box
  asl
  asl
  asl
  tax
  lda _tb+1,x          ; y
  and #$00ff
  asl
  asl
  asl
  asl
  asl                  ; y*32
  sta t_tmp
  lda _tb+0,x
  and #$00ff
  clc
  adc t_tmp
  asl
  sta t_tmp            ; byte offset of top-left entry
  lda t_pal
  and #$0007
  xba
  asl
  asl
  sta t_tmp2
  lda t_flags
  and #4               ; BOX_LOW: priority 0
  bne :+
  lda t_tmp2
  ora #$2000           ; priority
  sta t_tmp2           ; attribute bits
:
  lda t_base
  sta t_row            ; running tile number
  ldy #0               ; row counter
@rows:
  cpy t_h
  bcs @done
  ldx t_tmp
  phy
  ldy t_w
@cols:
  lda t_row
  ora t_tmp2
  sta f:$7e0000+_bg3map,x
  inc t_row
  inx
  inx
  dey
  bne @cols
  ply
  jsr ring_pal
  lda t_tmp
  clc
  adc #64
  sta t_tmp
  iny
  bra @rows
@done:
  A8
  lda _bgmap_dirty
  ora #$04
  sta _bgmap_dirty
  A16
  rts

; framed box: recolour the outer ring of the row just written (row Y, map offset t_tmp)
ring_pal:
  .a16
  .i16
  lda t_flags
  and #2
  bne :+
  rts
:
  phy
  ldx t_tmp
  cpy #0
  beq @all
  tya
  inc a
  cmp t_h
  beq @all
  ; side tiles only
  lda f:$7e0000+_bg3map,x
  and #$e3ff
  ora #$1800           ; palette 6
  sta f:$7e0000+_bg3map,x
  lda t_w
  dec a
  asl
  clc
  adc t_tmp
  tax
  lda f:$7e0000+_bg3map,x
  and #$e3ff
  ora #$1800
  sta f:$7e0000+_bg3map,x
  ply
  rts
@all:
  ldy t_w
@a:
  lda f:$7e0000+_bg3map,x
  and #$e3ff
  ora #$1800
  sta f:$7e0000+_bg3map,x
  inx
  inx
  dey
  bne @a
  ply
  rts

; clear the canvas of the loaded box (w*h*16 bytes at $7F:base*16)
;   transparent: all 0 ; opaque: plane 1 set (fill colour 2) ; framed: + border lines
box_clear:
  .a16
  .i16
  lda t_w
  ldx t_h
  jsr mul16x8
  asl
  asl
  asl
  sta t_tmp2           ; words
  lda t_base
  asl
  asl
  asl
  asl
  tax
  lda t_flags
  and #1
  beq :+
  lda #$ff00
:
  sta t_tmp
@f:
  lda t_tmp
  sta f:$7f0000,x
  inx
  inx
  dec t_tmp2
  bne @f
  lda t_flags
  and #2
  bne :+
  rts
:
  ; frame written byte-wise: outer line colour 1, inner line colour 3, round corners (colour 0)
  ; top tile row: rows 0 (c1) and 1 (c3); bottom tile row: rows 7 (c1) and 6 (c3)
  lda t_base
  asl
  asl
  asl
  asl
  tax                  ; canvas offset of tile (0,0)
  lda t_w
  sta t_tmp
@top:
  lda #$00ff           ; plane0 ff, plane1 00 = colour 1
  sta f:$7f0000,x
  lda #$ffff           ; colour 3
  sta f:$7f0002,x
  txa
  clc
  adc #16
  tax
  dec t_tmp
  bne @top
  ; bottom tile row starts at base + (h-1)*w
  lda t_h
  dec a
  tax
  lda t_w
  jsr mul16x8
  clc
  adc t_base
  asl
  asl
  asl
  asl
  tax
  lda t_w
  sta t_tmp
@bot:
  lda #$ffff
  sta f:$7f000c,x
  lda #$00ff
  sta f:$7f000e,x
  txa
  clc
  adc #16
  tax
  dec t_tmp
  bne @bot
  ; side columns, pixel row by pixel row
  lda t_w
  asl
  asl
  asl
  asl
  sta g_w16
  lda t_h
  asl
  asl
  asl
  dec a
  sta t_ty1            ; last pixel row
  lda t_base
  asl
  asl
  asl
  asl
  sta t_off            ; left tile, row 0
  lda t_w
  dec a
  asl
  asl
  asl
  asl
  sta t_tx1            ; offset from the left tile to the right tile
  stz t_fy
  stz g_suby
@side:
  ldx t_off
  lda t_fy
  beq @corner
  cmp t_ty1
  beq @corner
  A8
  lda f:$7f0000,x      ; left: x0 = c1, x1 = c3
  ora #$c0
  sta f:$7f0000,x
  lda f:$7f0001,x
  and #$7f
  ora #$40
  sta f:$7f0001,x
  A16
  txa
  clc
  adc t_tx1
  tax
  A8
  lda f:$7f0000,x      ; right: x = w-1 c1 (bit 0), w-2 c3 (bit 1)
  ora #$03
  sta f:$7f0000,x
  lda f:$7f0001,x
  and #$fe
  ora #$02
  sta f:$7f0001,x
  A16
  bra @next
@corner:
  A8
  lda f:$7f0000,x
  and #$7f
  sta f:$7f0000,x
  lda f:$7f0001,x
  and #$7f
  sta f:$7f0001,x
  A16
  txa
  clc
  adc t_tx1
  tax
  A8
  lda f:$7f0000,x
  and #$fe
  sta f:$7f0000,x
  lda f:$7f0001,x
  and #$fe
  sta f:$7f0001,x
  A16
@next:
  lda t_fy
  cmp t_ty1
  bne :+
  rts
:
  inc t_fy
  lda t_off
  clc
  adc #2
  sta t_off
  inc g_suby
  lda g_suby
  cmp #8
  bcs :+
  jmp @side
:
  stz g_suby
  lda t_off
  clc
  adc g_w16
  sec
  sbc #16
  sta t_off
  jmp @side

; set pixel (t_fx, t_fy) to colour A (0..3)
pset:
  .a16
  .i16
  sta t_attr
  jsr paddr            ; X = byte addr (plane 0), A = bit mask
  sta t_bits
  A8
  lda t_attr
  bit #1
  beq @c0
  lda f:$7f0000,x
  ora t_bits
  bra @s0
@c0:
  lda t_bits
  eor #$ff
  and f:$7f0000,x
@s0:
  sta f:$7f0000,x
  lda t_attr
  bit #2
  beq @c1
  lda f:$7f0001,x
  ora t_bits
  bra @s1
@c1:
  lda t_bits
  eor #$ff
  and f:$7f0001,x
@s1:
  sta f:$7f0001,x
  A16
  rts
pclear:
  .a16
  lda #0
  jmp pset

; X = canvas address of pixel (t_fx, t_fy) plane 0, A = bit mask
paddr:
  .a16
  .i16
  lda t_fy
  lsr
  lsr
  lsr
  tax
  lda t_w
  jsr mul16x8
  sta t_off
  lda t_fx
  lsr
  lsr
  lsr
  clc
  adc t_off
  clc
  adc t_base
  asl
  asl
  asl
  asl
  sta t_off
  lda t_fy
  and #7
  asl
  clc
  adc t_off
  tax
  lda t_fx
  and #7
  tay
  lda #$80
@m:
  cpy #0
  beq @d
  lsr
  dey
  bra @m
@d:
  rts

; horizontal line at t_fy from x=1 to w*8-2 in colour A
hline:
  .a16
  pha
  lda #1
  sta t_fx
  lda t_w
  asl
  asl
  asl
  dec a
  sta t_tx1
@l:
  lda t_fx
  cmp t_tx1
  bcs @d
  lda 1,s
  jsr pset
  inc t_fx
  bra @l
@d:
  pla
  rts
; vertical line at t_fx from y=1 to h*8-2
vline:
  .a16
  pha
  lda #1
  sta t_fy
  lda t_h
  asl
  asl
  asl
  dec a
  sta t_ty1
@l:
  lda t_fy
  cmp t_ty1
  bcs @d
  lda 1,s
  jsr pset
  inc t_fy
  bra @l
@d:
  pla
  rts
zero2: .word 0

; A(16) = A * X (X < 256); hardware multiplier when A < 256
mul16x8:
  .a16
  .i16
  cmp #256
  bcs @slow
  sep #$20
  .a8
  sta f:$004202        ; WRMPYA
  txa
  sta f:$004203        ; WRMPYB: starts the 8-cycle multiply
  nop
  nop
  nop
  nop
  rep #$20
  .a16
  lda f:$004216        ; RDMPYL/H
  rts
@slow:
  sta m_a
  stx m_b
  lda #0
@l:
  ldx m_b
  beq @e
  clc
  adc m_a
  dec m_b
  bra @l
@e:
  rts

; void tb_clear(u8 box)
CFUNC tb_clear
  php
  phb
  AXY16
  pha
  A8
  lda #$80
  pha
  plb
  A16
  pla
  jsr box_load
  jsr box_clear
  ; restore box palette in the map (colour codes may have changed tiles)
  jsr box_map
  plb
  plp
  rtl

; tb_flush is in ui.c (chunked uploads through queue_vram)

; void tb_text(u8 box): ea0 = px, ea1 = py, ea2 = text id
CFUNC tb_text
  php
  phb
  AXY16
  pha
  A8
  lda #$80
  pha
  plb
  A16
  pla
  jsr box_load
  lda _ea+4
  jsr text_ptr
  jsr draw_string
  plb
  plp
  rtl

; void tb_codes(u8 box): ea0 = px, ea1 = py, ea2 = pointer to codes in $7E
CFUNC tb_codes
  php
  phb
  AXY16
  pha
  A8
  lda #$80
  pha
  plb
  A16
  pla
  jsr box_load
  lda _ea+4
  sta t_src
  A8
  lda #$7e
  sta t_src+2
  A16
  jsr draw_string
  plb
  plp
  rtl

; u16 text_width(void): ea2 = text id -> pixel width of the first line
CFUNC text_width
  php
  phb
  AXY16
  A8
  lda #$80
  pha
  plb
  A16
  lda _ea+4
  jsr text_ptr
  jsr measure
  plb
  plp
  xba
  tax                 ; X = high byte (cc65 16-bit return: A lo, X hi)
  xba
  rtl

; A = text id -> t_src (long)
text_ptr:
  .a16
  .i16
  sta t_tmp
  asl
  clc
  adc t_tmp            ; *3
  tax
  lda f:text_table,x
  sta t_src
  A8
  lda f:text_table+2,x
  sta t_src+2
  A16
  rts

; measure the first line of t_src -> A
measure:
  .a16
  .i16
  stz t_tmp2
  ldy #0
@l:
  lda [t_src],y
  beq @done
  cmp #1
  beq @done
  cmp #16
  bcc @next
  sec
  sbc #16
  tax
  lda f:font_width,x
  and #$00ff
  clc
  adc t_tmp2
  sta t_tmp2
@next:
  iny
  iny
  bra @l
@done:
  lda t_tmp2
  rts

; draw the string at t_src into the loaded box at (ea0, ea1)
draw_string:
  .a16
  .i16
  lda _ea+0
  sta t_px
  sta t_x0
  lda _ea+2
  sta t_py
  stz t_nest
@loop:
  lda [t_src]
  inc t_src
  inc t_src
  cmp #0
  bne :+
  jmp @end
:
  cmp #1
  bne @not_nl
  lda t_x0
  sta t_px
  lda t_py
  clc
  adc #14
  sta t_py
  jmp @loop
@not_nl:
  cmp #2
  bne @not_name
  ; hero name: switch to the name buffer (one level)
  jsr push_src
  lda #.loword(_hero_name)
  sta t_src
  A8
  lda #$00
  sta t_src+2
  A16
  jmp @loop
@not_name:
  cmp #3
  bne @not_num
  jsr push_src
  jsr num_to_codes
  jmp @loop
@not_num:
  cmp #11
  bne @not_num2
  jsr push_src
  lda _targ_num2
  sta t_tmp
  lda _targ_num2+2
  sta t_tmp2
  jsr num_codes2
  jmp @loop
@not_num2:
  cmp #4
  bne @not_str
  jsr push_src
  lda _targ_str
  jsr text_ptr
  jmp @loop
@not_str:
  cmp #16
  bcs @glyph
  ; colour code 5..10 -> palette (transparent boxes 0..3, opaque boxes 4,5,7)
  sec
  sbc #5
  and #$0007
  tax
  lda t_flags
  and #1
  beq :+
  txa
  clc
  adc #8
  tax
:
  lda f:colour_pal,x
  and #$00ff
  sta t_pal
  jmp @loop
@glyph:
  jsr draw_glyph
  jmp @loop
@end:
  lda t_nest
  beq @out
  jsr pop_src
  jmp @loop
@out:
  rts

colour_pal: .byte 0, 1, 2, 0, 1, 1, 0, 0,  4, 5, 7, 3, 5, 5, 4, 4   ; window gray = palette 3

push_src:
  .a16
  lda t_src
  sta t_save
  A8
  lda t_src+2
  sta t_save+2
  A16
  inc t_nest
  rts
pop_src:
  .a16
  lda t_save
  sta t_src
  A8
  lda t_save+2
  sta t_src+2
  A16
  dec t_nest
  rts

; _targ_num (u32) -> decimal glyph codes in t_numbuf (codes for '0'.. are found via font index)
num_to_codes:
  .a16
  .i16
  ; divide 32-bit value by 10 repeatedly, digits pushed on stack
  lda _targ_num
  sta t_tmp
  lda _targ_num+2
  sta t_tmp2
num_codes2:
  ldx #0               ; digit count
@div:
  ; (t_tmp2:t_tmp) / 10 -> quotient back, remainder in Y
  ldy #32
  lda #0               ; remainder
@bit:
  asl t_tmp
  rol t_tmp2
  rol a
  cmp #10
  bcc :+
  sbc #10
  inc t_tmp
:
  dey
  bne @bit
  pha                  ; remainder digit
  inx
  lda t_tmp
  ora t_tmp2
  bne @div
  ldy #0
@out:
  pla
  clc
  adc #FONT_DIGIT0
  sta t_numbuf,y
  iny
  iny
  dex
  bne @out
  lda #0
  sta t_numbuf,y
  lda #t_numbuf
  sta t_src
  A8
  lda #$00
  sta t_src+2
  A16
  rts

; glyph code in A: render with shadow at (t_px, t_py), advance t_px
draw_glyph:
  .a16
  .i16
  sec
  sbc #16
  sta t_tmp
  tax
  lda f:font_width,x
  and #$00ff
  sta t_gw
  lda t_tmp
  asl
  asl
  asl                  ; *8
  sta t_tmp2
  asl                  ; *16
  clc
  adc t_tmp2           ; *24
  clc
  adc #.loword(font_glyphs)
  sta t_glyph
  A8
  lda #^font_glyphs
  sta t_glyph+2
  A16
  ; colour: set palette of the tiles touched when the colour is not the box default
  jsr glyph_pal
  ; text and its +1,+1 shadow in one pass
  A8
  lda t_flags
  and #1
  sta g_op
  A16
  jsr gblit
@adv:
  lda t_px
  clc
  adc t_gw
  sta t_px
  rts

; one-pass glyph blit at (t_px, t_py): rows of t_glyph plus the shadow (previous row >> 1).
; transparent boxes: text plane 0, shadow plane 1; opaque: shadow = colour 1, text = colour 3.
gblit:
  .a16
  .i16
  lda t_px
  and #7
  sta t_ty1            ; sub-tile shift
  lda t_px
  lsr
  lsr
  lsr
  sta t_tx
  lda t_w
  asl
  asl
  asl
  asl
  sta g_w16
  lda t_py
  lsr
  lsr
  lsr
  sta g_ty
  tax
  lda t_w
  jsr mul16x8
  clc
  adc t_tx
  clc
  adc t_base
  asl
  asl
  asl
  asl
  sta t_off
  lda t_py
  and #7
  sta g_suby
  asl
  clc
  adc t_off
  sta t_off
  stz t_fy             ; previous row
  stz t_row
@row:
  lda t_row
  cmp #12
  bcs @past
  asl
  tay
  lda [t_glyph],y
  xba                  ; font rows are big-endian
  bra @have
@past:
  lda #0
@have:
  sta t_bits
  lda t_fy
  lsr
  sta t_tmp2           ; shadow
  lda t_bits
  sta t_fy
  ora t_tmp2
  bne :+
  jmp @adv
:
  lda g_ty
  cmp t_h
  bcc :+
  rts                  ; below the box
:
  ; shift text and shadow right by the sub-tile offset into 24 bits
  stz t_tmp
  stz g_sl
  ldx t_ty1
  beq @shifted
  lda t_bits
@s1:
  lsr a
  ror t_tmp
  dex
  bne @s1
  sta t_bits
  ldx t_ty1
  lda t_tmp2
@s2:
  lsr a
  ror g_sl
  dex
  bne @s2
  sta t_tmp2
@shifted:
  ldx t_off
  A8
  lda t_tx
  cmp t_w
  bcs @skip
  lda t_bits+1
  sta g_b
  lda t_tmp2+1
  sta g_c
  jsr gput
  lda t_tx
  inc a
  cmp t_w
  bcs @skip
  A16
  txa
  clc
  adc #16
  tax
  A8
  lda t_bits
  sta g_b
  lda t_tmp2
  sta g_c
  jsr gput
  lda t_tx
  inc a
  inc a
  cmp t_w
  bcs @skip
  A16
  txa
  clc
  adc #16
  tax
  A8
  lda t_tmp+1
  sta g_b
  lda g_sl+1
  sta g_c
  jsr gput
@skip:
  A16
@adv:
  inc t_row
  lda t_row
  cmp #13
  bcc :+
  rts
:
  lda t_off
  clc
  adc #2
  sta t_off
  inc g_suby
  lda g_suby
  cmp #8
  bcs :+
  jmp @row
:
  stz g_suby
  inc g_ty
  lda t_off
  clc
  adc g_w16
  sec
  sbc #16
  sta t_off
  jmp @row

; g_b = text bits, g_c = shadow bits at canvas X (plane 0 byte)
gput:
  .a8
  .i16
  lda g_op
  bne @op
  lda g_b
  ora f:$7f0000,x
  sta f:$7f0000,x
  lda g_c
  ora f:$7f0001,x
  sta f:$7f0001,x
  rts
@op:
  lda g_b
  ora g_c
  ora f:$7f0000,x
  sta f:$7f0000,x
  lda g_b              ; plane 1: clear under shadow-only pixels, set under text
  eor #$ff
  and g_c
  eor #$ff
  and f:$7f0001,x
  ora g_b
  sta f:$7f0001,x
  rts

; OR the glyph rows into the canvas plane t_plane at (t_px, t_py)
blit:
  .a16
  .i16
  lda t_px
  lsr
  lsr
  lsr
  sta t_tx
  stz t_row
@row:
  lda t_row
  cmp #12
  bcc :+
  rts
:
  asl
  tay
  lda [t_glyph],y
  xba                  ; font rows are big-endian
  cmp #0               ; (xba only tests the low byte)
  bne :+
  jmp @next            ; empty row
:
  sta t_bits
  lda t_py
  clc
  adc t_row
  sta t_tmp            ; y
  lsr
  lsr
  lsr
  cmp t_h
  bcc :+
  jmp @next
:
  tax
  lda t_w
  jsr mul16x8          ; ty*w
  clc
  adc t_tx
  clc
  adc t_base           ; tile number
  asl
  asl
  asl
  asl
  sta t_off
  lda t_tmp
  and #$0007
  asl
  sta t_tmp2
  lda t_plane
  cmp #2
  bcs :+
  ora t_tmp2
  bra :++
:
  lda t_tmp2
:
  clc
  adc t_off
  sta t_off            ; canvas byte offset
  lda t_px
  and #$0007
  tax
  lda t_bits
  stz t_tmp
  cpx #0
  beq @noshift
@sh:
  lsr a
  ror t_tmp
  dex
  bne @sh
@noshift:
  sta t_bits           ; t_bits+1 = tile tx, t_bits = tile tx+1, t_tmp+1 = tile tx+2
  ldx t_off
  A8
  lda t_tx
  cmp t_w
  bcs @skip
  lda t_bits+1
  jsr put8
  lda t_tx
  inc a
  cmp t_w
  bcs @skip
  A16
  txa
  clc
  adc #16
  tax
  A8
  lda t_bits
  jsr put8
  lda t_tx
  inc a
  inc a
  cmp t_w
  bcs @skip
  A16
  txa
  clc
  adc #16
  tax
  A8
  lda t_tmp+1
  jsr put8
@skip:
  A16
@next:
  inc t_row
  jmp @row

; write mask A (8-bit) at canvas X according to t_plane
put8:
  .a8
  .i16
  beq @z
  sta t_attr
  lda t_plane
  cmp #2
  bcs @two
  lda t_attr
  ora f:$7f0000,x
  sta f:$7f0000,x
@z:
  rts
@two:
  bne @three
  lda t_attr
  ora f:$7f0000,x
  sta f:$7f0000,x
  lda t_attr
  eor #$ff
  and f:$7f0001,x
  sta f:$7f0001,x
  rts
@three:
  lda t_attr
  ora f:$7f0000,x
  sta f:$7f0000,x
  lda t_attr
  ora f:$7f0001,x
  sta f:$7f0001,x
  rts

; when t_pal differs from the box palette, recolour the tiles under the glyph
glyph_pal:
  .a16
  .i16
  lda t_box
  asl
  asl
  asl
  tax
  lda _tb+6,x
  and #$0007
  cmp t_pal
  bne :+
  rts
:
  lda t_pal
  and #$0007
  xba
  asl
  asl
  sta t_attr
  lda t_flags
  and #4
  bne :+
  lda t_attr
  ora #$2000
  sta t_attr
:
  lda t_py
  clc
  adc #12
  lsr
  lsr
  lsr
  cmp t_h
  bcc :+
  lda t_h
  dec a
:
  sta t_ty1
  lda t_px
  clc
  adc t_gw
  dec a
  lsr
  lsr
  lsr
  cmp t_w
  bcc :+
  lda t_w
  dec a
:
  sta t_tx1
  lda t_py
  lsr
  lsr
  lsr
  sta t_row
@ty:
  lda t_row
  cmp t_ty1
  beq :+
  bcs @done
:
  lda t_px
  lsr
  lsr
  lsr
  sta t_tx
@tx:
  lda t_tx
  cmp t_tx1
  beq :+
  bcs @nrow
:
  lda t_box
  asl
  asl
  asl
  tax
  lda _tb+1,x
  and #$00ff
  clc
  adc t_row
  asl
  asl
  asl
  asl
  asl
  sta t_off
  lda _tb+0,x
  and #$00ff
  clc
  adc t_tx
  clc
  adc t_off
  asl
  tax
  lda f:$7e0000+_bg3map,x
  and #$c3ff
  ora t_attr
  sta f:$7e0000+_bg3map,x
  inc t_tx
  bra @tx
@nrow:
  inc t_row
  bra @ty
@done:
  A8
  lda _bgmap_dirty
  ora #$04
  sta _bgmap_dirty
  A16
  rts

