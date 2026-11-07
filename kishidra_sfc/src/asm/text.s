; text boxes on BG3: 12-dot glyphs rendered into a 2bpp tile canvas ($7F:0000 = BG3 tile 0)
.include "snes.inc"

.import text_table, font_glyphs, font_width, font_glyphs_s, font_width_s, vq_push
.importzp FONT_DIGIT0
.import _ea, _bg3map, _bgmap_dirty
.export _targ_num, _targ_num2, _targ_str, _hero_name, _tb, _text_wrap, _text_small
.import _bg3_vram

CANVAS_BANK = $7f
BG3_TILE_VRAM = $6000
NBOX = 12

.segment "LOWBSS"
t_numbuf: .res 24         ; decimal digits (2 bytes each, up to 10) + end
t_dry:    .res 2          ; 1 = measure only (draw nothing)
t_drawn:  .res 2          ; a glyph was drawn on this line since the last colour change
t_maxx:   .res 2          ; widest x reached (measure)
_text_wrap: .res 2        ; > 0: a glyph that would reach this x starts a new line (C sets it)
_text_small: .res 2       ; text_width measures in the 8-dot font (C sets it)
t_gh:     .res 2          ; glyph rows (12, or 7 in the 8-dot font) ; the shadow adds one
t_lh:     .res 2          ; line step (14 / 9)
t_msg:    .res 2          ; text id being drawn (text check log)
t_minw:   .res 2          ; minimum digits ({n3}: 3, zero padded)
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
t_tmp3:   .res 2
t_pair:   .res 2             ; width of a following glyph that must stay on this line
t_code:   .res 2
t_dst:    .res 3             ; canvas pointer (long)
t_plane:  .res 2             ; 0 = plane 0 (text), 1 = plane 1 (shadow)
t_pal:    .res 2
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
set_font:              ; t_gh / t_lh from t_flags bit 3 (BOX_SMALL: the 8-dot font)
  lda t_flags
  and #8
  bne :+
  lda #12
  sta t_gh
  lda #14
  sta t_lh
  rts
:
  lda #7
  sta t_gh
  lda #9
  sta t_lh
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
.ifdef PROFILE
  jsr tc_clear
.endif
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
  ; fill by DMA through the WRAM port (channel 2: the NMI uses 0, assets 1, HDMA 6-7)
  txa
  sta f:WMADDL         ; + WMADDM
  sep #$20
  .a8
  lda #$7f
  sta f:WMADDH
  lda #^fill_pat
  sta f:DMAP0+$24
  rep #$20
  .a16
  ; in 512-byte pieces: a DMA holds off the NMI, which must not start late (crt0.s nmi_uploads)
  lda #$8008           ; transparent: fixed source (zeros) -> B bus $80 (WMDATA)
  sta t_tmp3
  lda t_flags
  and #1
  beq :+
  lda #$8000           ; opaque: 00 ff 00 ff ... (colour 2), incrementing source
  sta t_tmp3
:
  lda t_tmp2
  asl
  sta t_tmp
@piece:
  lda t_tmp3
  sta f:DMAP0+$20
  lda #.loword(fill_pat)
  sta f:DMAP0+$22
  lda t_tmp
  cmp #512
  bcc :+
  lda #512
:
  sta f:DMAP0+$25
  sep #$20
  .a8
  lda #$04
  sta f:MDMAEN
  rep #$20
  .a16
  lda t_tmp
  sec
  sbc #512
  sta t_tmp
  beq @filled
  bcs @piece
@filled:
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

; void tb_clear_now(u8 box) (C: tb_clear waits for the box's pending uploads first)
CFUNC tb_clear_now
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

; void tb_rect_now(u8 box): ea0 = x, ea1 = y, ea2 = width, ea3 = height | colour << 8 (0-3):
; a filled rectangle in the box canvas (gauges such as the boss HP bar)
CFUNC tb_rect_now
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
  lda _ea+6
  and #$00ff
  sta t_ty1            ; rows left
  bne :+
  jmp @done
:
  lda _ea+2
  sta t_py
@row:
  lda _ea+4
  sta t_tx1            ; columns left
  bne :+
  jmp @done
:
  lda _ea+0
  sta t_px
@px:
  lda t_px             ; the pixel's bit
  and #7
  tax
  sep #$20
  .a8
  lda f:rect_bit,x
  sta t_tmp2
  eor #$ff
  sta t_tmp2+1
  rep #$20
  .a16
  ; canvas byte of (t_px, t_py): ((base + (py >> 3) * w + (px >> 3)) * 16) + (py & 7) * 2
  lda t_py
  lsr
  lsr
  lsr
  ldx t_w
  jsr mul16x8
  sta t_tmp
  lda t_px
  lsr
  lsr
  lsr
  clc
  adc t_tmp
  clc
  adc t_base
  asl
  asl
  asl
  asl
  sta t_tmp
  lda t_py
  and #7
  asl
  clc
  adc t_tmp
  tax
  sep #$20
  .a8
  lda _ea+7            ; colour
  lsr
  lda f:$7f0000,x      ; plane 0 <- colour bit 0
  and t_tmp2+1
  bcc :+
  ora t_tmp2
:
  sta f:$7f0000,x
  lda _ea+7
  lsr
  lsr
  lda f:$7f0001,x      ; plane 1 <- colour bit 1
  and t_tmp2+1
  bcc :+
  ora t_tmp2
:
  sta f:$7f0001,x
  rep #$20
  .a16
  inc t_px
  dec t_tx1
  beq :+
  jmp @px
:
  inc t_py
  dec t_ty1
  beq @done
  jmp @row
@done:
  plb
  plp
  rtl
rect_bit: .byte $80, $40, $20, $10, $08, $04, $02, $01

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
  jsr font_override
  lda _ea+4
  sta t_msg
  jsr text_ptr
  jsr draw_string
  plb
  plp
  rtl

; text_small set by C: this text in the 8-dot font whatever the box's font
font_override:
  .a16
  lda _text_small
  beq :+
  lda t_flags
  ora #8
  sta t_flags
  jsr set_font
:
  rts

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
  lda #$ffff
  sta t_msg
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
  lda _text_small
  beq :+
  lda #8
:
  sta t_flags
  jsr set_font
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

; measure the first line of t_src -> A: the drawing loop without drawing, so {s} / {n}
; expansions and colour runs (which start on a tile) count exactly as when drawn
measure:
  .a16
  .i16
  lda #1
  sta t_dry
  stz t_px
  stz t_x0
  stz t_py
  stz t_pal            ; assume the box colour is white
  stz t_maxx
  jsr draw_run
  stz t_dry
  lda t_maxx
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
  stz t_maxx
draw_run:
  stz t_nest
  stz t_drawn
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
  lda t_dry
  beq :+
  jmp @out             ; measuring: the first line only
:
.ifdef PROFILE
  jsr tc_extent
.endif
  lda t_x0
  sta t_px
  stz t_drawn
  stz t_maxx
  lda t_py
  clc
  adc t_lh
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
  stz t_minw
  jsr num_to_codes
  jmp @loop
@not_num:
  cmp #12
  bne @not_num3
  jsr push_src
  lda #3
  sta t_minw
  jsr num_to_codes
  jmp @loop
@not_num3:
  cmp #11
  bne @not_num2
  jsr push_src
  lda _targ_num2
  sta t_tmp
  lda _targ_num2+2
  sta t_tmp2
  stz t_minw
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
  ; colour code 5..10, 13, 14 -> palette (the same palettes in every box); {c0} = the box's own colour
  cmp #13
  bne :+
  lda #11              ; {purple}: colour_pal[6]
:
  cmp #14
  bne :+
  lda #12              ; {gold}: colour_pal[7]
:
  sec
  sbc #5
  and #$0007
  bne @col
  lda t_box
  asl
  asl
  asl
  tax
  lda _tb+6,x
  and #$0007
  bra @setpal
@col:
  tax
  lda f:colour_pal,x
  and #$00ff
@setpal:
  cmp t_pal
  beq @samepal
  sta t_pal
  lda t_drawn          ; a colour change after text on this line: start on the next tile
  beq @samepal         ; (an 8x8 tile has one palette, so two colours must not share one)
  lda t_px
  and #7
  beq @samepal
  lda t_px
  ora #7
  inc a
  sta t_px
  stz t_drawn
@samepal:
  jmp @loop
@glyph:
  ; kinsoku: when the next glyph must not start a line (、。！？）… small kana), this glyph
  ; wraps if the two do not fit together
  sta t_code
  stz t_pair
  lda _text_wrap
  beq @draw
  lda t_flags
  and #8
  bne @draw
  lda [t_src]
  cmp #16
  bcc @draw
  sec
  sbc #16
  tax
  lda f:font_width,x
  and #$00ff
  bit #$0080
  beq @draw
  and #$007f
  sta t_pair
@draw:
  lda t_code
  jsr draw_glyph
  lda #1
  sta t_drawn
  jmp @loop
@end:
  lda t_nest
  beq @out
  jsr pop_src
  jmp @loop
@out:
.ifdef PROFILE
  lda t_dry
  bne :+
  jsr tc_extent
:
.endif
  rts

colour_pal: .byte 0, 1, 2, 3, 5, 4, 7, 6   ; c0 (box), yellow, red, gray, blue, green, purple, gold (BG3 palettes, see bg3pal in sys.c)

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
@pad:
  cpx t_minw
  bcs @padded
  lda #0
  pha
  inx
  bra @pad
@padded:
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
  lda t_flags
  and #8
  bne @small
  lda f:font_width,x
  and #$007f           ; (bit 7: no line starts with this glyph, see @glyph)
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
  bra @font
@small:                ; 8-dot font: 16 bytes per glyph
  lda f:font_width_s,x
  and #$00ff
  sta t_gw
  lda t_tmp
  asl
  asl
  asl
  asl
  clc
  adc #.loword(font_glyphs_s)
  sta t_glyph
  A8
  lda #^font_glyphs_s
  sta t_glyph+2
  A16
@font:
  lda t_dry
  bne @adv
  lda _text_wrap       ; wrapping: the glyph's last column past the limit -> next line
  beq @nowrap
  lda t_px
  clc
  adc t_gw
  adc t_pair           ; (and a following glyph that must not start a line)
  dec a
  dec a
  cmp _text_wrap
  bcc @nowrap
  lda t_px
  cmp t_x0
  beq @nowrap          ; (a glyph wider than the whole line stays)
.ifdef PROFILE
  jsr tc_extent
.endif
  lda t_x0
  sta t_px
  stz t_maxx
  lda t_py
  clc
  adc t_lh
  sta t_py
@nowrap:
.ifdef PROFILE
  jsr text_check
.endif
  ; colour: set palette of the tiles touched when the colour is not the box default
  jsr glyph_pal
  ; text and its +1,+1 shadow in one pass
  jsr gblit
@adv:
  lda t_px
  clc
  adc t_gw
  sta t_px
  cmp t_maxx
  bcc :+
  sta t_maxx
:
  rts

; one-pass glyph blit at (t_px, t_py): rows of t_glyph plus the shadow (the previous row
; one pixel to the right).  transparent boxes: text plane 0, shadow plane 1 (one 16-bit OR
; per tile row); opaque: shadow = colour 1, text = colour 3 (gput).
; Row bytes are shifted with tables: tile k gets text byte k of (row << 8 >> s), s = x & 7.
; t_tmp / t_tmp2 / g_sl hold (text, shadow) byte pairs of the three tiles a row touches.
G_P0 = t_tmp
G_P1 = t_tmp2
G_P2 = g_sl
gblit:
  .a16
  .i16
  lda t_px
  and #7
  xba
  sta t_ty1            ; shift * 256: index into the shift tables
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
  stz G_P0
  stz G_P1
  stz G_P2
  stz t_row
@row:
  sep #$20
  .a8
  lda G_P0             ; shadow = last row's text >> 1 across the three tiles
  lsr
  sta G_P0+1
  lda G_P1
  ror
  sta G_P1+1
  lda G_P2
  ror
  sta G_P2+1
  rep #$20
  .a16
  lda t_row
  cmp t_gh
  bcs @blank
  asl
  tay
  lda [t_glyph],y      ; low byte = left 8 pixels (H), high byte = the rest (L)
  sta t_bits
  and #$00ff
  ora t_ty1
  tax
  sep #$20
  .a8
  lda f:shr_tab,x
  sta G_P0
  lda f:shl_tab,x
  sta G_P1
  rep #$20
  .a16
  lda t_bits
  xba
  and #$00ff
  ora t_ty1
  tax
  sep #$20
  .a8
  lda f:shr_tab,x
  ora G_P1
  sta G_P1
  lda f:shl_tab,x
  sta G_P2
  rep #$20
  .a16
  bra @have
@blank:
  sep #$20
  .a8
  stz G_P0
  stz G_P1
  stz G_P2
  rep #$20
  .a16
@have:
  lda G_P0
  ora G_P1
  ora G_P2
  beq @adv
  lda g_ty
  cmp t_h
  bcs @ret             ; below the box
  ldx t_off
  jmp @opaque          ; one rule for every box (text colour 3, shadow 1; see bg3pal)
  lda G_P0
  beq :+
  ora f:$7f0000,x
  sta f:$7f0000,x
:
  lda t_tx
  inc a
  cmp t_w
  bcs @adv
  lda G_P1
  beq :+
  ora f:$7f0010,x
  sta f:$7f0010,x
:
  lda t_tx
  inc a
  inc a
  cmp t_w
  bcs @adv
  lda G_P2
  beq @adv
  ora f:$7f0020,x
  sta f:$7f0020,x
@adv:
  inc t_row
  lda t_row
  cmp t_gh
  beq :+
  bcs @ret             ; the glyph rows and one more for the shadow
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
@ret:
  rts
@opaque:               ; p0 |= text | shadow, p1 = (p1 & (text | ~shadow)) | text:
  sep #$20             ; text = colour 3, shadow = colour 1, the rest is left (0 or the fill, 2)
  .a8
  lda G_P0
  ora G_P0+1
  beq @o1
  ora f:$7f0000,x
  sta f:$7f0000,x
  lda G_P0+1
  eor #$ff
  ora G_P0
  and f:$7f0001,x
  ora G_P0
  sta f:$7f0001,x
@o1:
  lda t_tx
  inc a
  cmp t_w
  bcs @op_done
  lda G_P1
  ora G_P1+1
  beq @o2
  ora f:$7f0010,x
  sta f:$7f0010,x
  lda G_P1+1
  eor #$ff
  ora G_P1
  and f:$7f0011,x
  ora G_P1
  sta f:$7f0011,x
@o2:
  lda t_tx
  inc a
  inc a
  cmp t_w
  bcs @op_done
  lda G_P2
  ora G_P2+1
  beq @op_done
  ora f:$7f0020,x
  sta f:$7f0020,x
  lda G_P2+1
  eor #$ff
  ora G_P2
  and f:$7f0021,x
  ora G_P2
  sta f:$7f0021,x
@op_done:
  rep #$20
  .a16
  jmp @adv

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
  adc t_gh             ; the shadow row: 12 (12-dot) or 7 (8-dot)
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


.segment "ENGINE"
; box fill source: byte 0 = 0 (transparent, fixed source); 00 ff pairs for opaque boxes
fill_pat:
  .repeat 512
  .byte $00, $ff
  .endrep

; glyph shift tables (gblit): shr_tab[s][x] = x >> s, shl_tab[s][x] = (x << (8 - s)) & $ff
.segment "BANKAF"
shr_tab:
  .repeat 8, sh
    .repeat 256, xx
      .byte xx >> sh
    .endrep
  .endrep
shl_tab:
  .repeat 8, sh
    .repeat 256, xx
      .byte (xx << (8 - sh)) & $ff
    .endrep
  .endrep

.ifdef PROFILE
; ---------------------------------------------------------------- text check (PROFILE=1 builds)
; Logs glyphs that leave their box and glyphs whose 8x8 tiles already hold text of another colour
; (a tile has one palette).  Entries of 8 bytes at $7F:E000 (512; bytes written at $7F:DBFE):
;   type (1 right, 2 bottom, 3 colour, 4 left, 5 top), box, text id (2), x (2), y, old pal << 4 | new pal
;   type 6: a line drawn (x = its start, extra = width), type 7: the box was cleared
; $7F:DC00 + tile = palette of the text already in that tile ($FF none); box_clear resets it.
.segment "ENGINE"
TC_INK = $7fdc00       ; 1024 tiles
TC_LOG = $7fe000       ; 512 entries
TC_IDX = $7fdbfe       ; bytes written (free running; the entry is at TC_IDX & $0ff8)

tc_log:                ; A8 = type, t_tmp2 = extra byte
  .a16
  .i16
  pha
  lda f:TC_IDX
  and #$0ff8
  tax
  pla
  sep #$20
  .a8
  sta f:TC_LOG,x
  lda t_box
  sta f:TC_LOG+1,x
  lda t_tmp2
  sta f:TC_LOG+7,x
  rep #$20
  .a16
  lda t_msg
  sta f:TC_LOG+2,x
  lda t_px
  sta f:TC_LOG+4,x
  sep #$20
  .a8
  lda t_py
  sta f:TC_LOG+6,x
  rep #$20
  .a16
  lda f:TC_IDX
  clc
  adc #8
  sta f:TC_IDX
  rts

text_check:
  .a16
  .i16
  lda t_tmp2
  pha
  ; bounds: the glyph's pixels (columns px..px+gw-2, rows py..py+11) must stay in the box, and in a
  ; framed box off the frame's tiles (those use the gold palette: text there turns gold)
  lda t_flags
  and #2
  beq :+
  lda #8
:
  sta g_suby           ; margin (temp)
  lda t_px
  cmp g_suby
  bcs :+
  stz t_tmp2
  lda #4               ; 4: left
  jsr tc_log
:
  lda t_py
  cmp g_suby
  bcs :+
  stz t_tmp2
  lda #5               ; 5: top
  jsr tc_log
:
  lda t_w
  asl
  asl
  asl
  sec
  sbc g_suby
  sta t_tmp2           ; first column outside
  lda t_px
  clc
  adc t_gw
  dec a
  dec a                ; last column of the glyph
  cmp t_tmp2
  bcc :+
  stz t_tmp2
  lda #1               ; 1: right
  jsr tc_log
:
  lda t_h
  asl
  asl
  asl
  sec
  sbc g_suby
  sta t_tmp2
  lda t_py
  clc
  adc t_gh
  dec a                ; last row of the glyph
  cmp t_tmp2
  bcc :+
  stz t_tmp2
  lda #2               ; 2: bottom
  jsr tc_log
:
  ; colour: every tile under the glyph (rows py..py+11, columns px..px+gw-2)
  lda t_gw
  cmp #2
  bcs :+
  jmp @done
:
  lda t_py
  clc
  adc t_gh
  dec a                ; last row of the glyph
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
  sta g_ty
@ty:
  lda g_ty
  cmp t_ty1
  beq :+
  bcc :+
  jmp @done
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
  lda g_ty             ; tile = base + ty * w + tx
  tax
  lda t_w
  jsr mul16x8
  clc
  adc t_tx
  clc
  adc t_base
  tax
  sep #$20
  .a8
  lda f:TC_INK,x
  cmp #$ff
  beq @set
  cmp t_pal
  beq @set
  asl
  asl
  asl
  asl
  ora t_pal
  sta t_tmp2
  rep #$20
  .a16
  phx
  lda #3
  jsr tc_log
  plx
  sep #$20
  .a8
@set:
  lda t_pal
  sta f:TC_INK,x
  rep #$20
  .a16
  inc t_tx
  bra @tx
@nrow:
  inc g_ty
  bra @ty
@done:
  pla
  sta t_tmp2
  rts

; the line just drawn: type 6, x = line start, y, extra byte = width (for overlap checks)
tc_extent:
  .a16
  .i16
  lda t_maxx
  sec
  sbc t_x0
  beq @r
  bcc @r
  cmp #256
  bcc :+
  lda #255
:
  sta t_tmp2
  lda t_px
  pha
  lda t_x0
  sta t_px
  lda t_flags
  and #8
  beq :+
  lda #3               ; 9: a line in the 8-dot font
:
  clc
  adc #6
  jsr tc_log
  pla
  sta t_px
@r:
  rts

; box_clear: no text in the box's tiles any more (logged as type 7)
tc_clear:
  .a16
  .i16
  stz t_tmp2
  lda #7
  jsr tc_log
  lda t_w
  ldx t_h
  jsr mul16x8
  beq @r
  tay
  ldx t_base
  sep #$20
  .a8
  lda #$ff
@l:
  sta f:TC_INK,x
  inx
  dey
  bne @l
  rep #$20
  .a16
@r:
  rts
.endif
