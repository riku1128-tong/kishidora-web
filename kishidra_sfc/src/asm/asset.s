; asset access: table lookup, LZSS unpack to WRAM, palette / map helpers, far copies
.include "snes.inc"

.import gfx_table, _ea, _cgram, _cgram_dirty, _bgmap_dirty
.import _bg1map, _bg2map, _bg3map, vq_push
.import estat_index, estat_data

.segment "ENGZP": zeropage
lz_src:  .res 3
lz_bp:   .res 3
lz_len:  .res 2
lz_end:  .res 2
lz_ctrl: .res 2
lz_bits: .res 2
lz_cnt:  .res 2
lz_tmp:  .res 2
fc_src:  .res 3
fc_dst:  .res 3

.segment "LOWBSS"
asset_flags: .res 2
asset_size:  .res 2

.segment "ENGINE"

; A(16) = asset id -> lz_src (long), asset_flags, asset_size
asset_find:
  .a16
  .i16
  sta lz_tmp
  asl
  clc
  adc lz_tmp
  asl                  ; *6
  tax
  lda f:gfx_table,x
  sta lz_src
  lda f:gfx_table+2,x
  and #$00ff
  sta lz_src+2         ; bank (+ high byte 0)
  lda f:gfx_table+3,x
  and #$00ff
  sta asset_flags
  lda f:gfx_table+4,x
  sta asset_size
  rts

; unpack (or copy) asset at lz_src to $7F:X ; returns A = unpacked size
unpack7f:
  .a16
  .i16
  lda asset_flags
  and #1
  bne lz
  ; raw copy
  stx lz_tmp
  ldy #0
  A8
@raw:
  cpy asset_size
  beq @rawdone
  lda [lz_src],y
  sta f:$7f0000,x
  inx
  iny
  bra @raw
@rawdone:
  A16
  lda asset_size
  rts
; LZSS: control byte (bit 1 = literal, 0 = match, LSB first), match = 16-bit token
; (distance - 1 in bits 0-11, length - 3 in bits 12-15).  Matches are copied with MVN
; (it copies forwards one byte at a time, so overlapping matches repeat as they should).
lz:
  phb                  ; MVN sets the data bank to $7F
  stx lz_tmp           ; start
  lda [lz_src]
  sta lz_len
  clc
  adc lz_tmp
  sta lz_end
  ldy #2
  A8
@ctrl:
  lda [lz_src],y
  iny
  sta lz_ctrl
  lda #8
  sta lz_bits
@bit:
  cpx lz_end
  bcs @done
  lsr lz_ctrl
  bcc @match
  lda [lz_src],y       ; literal
  iny
  sta f:$7f0000,x
  inx
  dec lz_bits
  bne @bit
  bra @ctrl
@match:
  A16
  lda [lz_src],y
  iny
  iny
  sta lz_bp            ; token
  phy
  txy                  ; Y = destination
  and #$0fff
  inc a
  sta lz_cnt           ; distance
  txa
  sec
  sbc lz_cnt
  tax                  ; X = source
  lda lz_bp+1          ; high byte of the token in the low byte
  lsr
  lsr
  lsr
  lsr
  and #$000f
  inc a
  inc a                ; length - 1
  mvn #$7f, #$7f
  tyx                  ; destination after the match
  ply
  A8
  dec lz_bits
  bne @bit
  bra @ctrl
@done:
  A16
  plb
  lda lz_len
  rts

; u16 asset_unpack(void): ea0 = asset id, ea1 = destination offset in $7F -> unpacked size
CFUNC asset_unpack
  php
  phb
  AXY16
  lda _ea+0
  jsr asset_find
  ldx _ea+2
  jsr unpack7f
  sta _ea+14
  plb
  plp
  lda f:_ea+15
  tax
  lda f:_ea+14
  rtl

; void asset_vram(void): ea0 = asset id, ea1 = VRAM word address. Display must be blanked
; (uses $7F:8000 as the buffer).
CFUNC asset_vram
  php
  phb
  AXY16
  A8
  lda #$80
  pha
  plb
  A16
  lda _ea+0
  jsr asset_find
  lda asset_flags
  and #1
  bne @packed
  ; stored raw: DMA straight from ROM
  A8
  lda #$80
  sta VMAIN
  A16
  lda _ea+2
  sta VMADDL
  lda #$1801
  sta DMAP0+$10
  lda lz_src
  sta A1T0L+$10
  lda asset_size
  sta DAS0L+$10
  A8
  lda lz_src+2
  sta A1B0+$10
  lda #$02
  sta MDMAEN
  plb
  plp
  rtl
@packed:
  .a16
  ldx #$8000
  jsr unpack7f
  tay                  ; size
  A8
  lda #$80
  sta VMAIN
  A16
  lda _ea+2
  sta VMADDL
  lda #$1801
  sta DMAP0+$10
  lda #$8000
  sta A1T0L+$10
  sty DAS0L+$10
  A8
  lda #$7f
  sta A1B0+$10
  lda #$02
  sta MDMAEN           ; channel 1
  plb
  plp
  rtl

; void asset_pal(void): ea0 = asset id, ea1 = first colour (0-255). Copies into the cgram shadow.
CFUNC asset_pal
  php
  phb
  AXY16
  A8
  lda #$80
  pha
  plb
  A16
  lda _ea+0
  jsr asset_find
  ldx #$7000
  jsr unpack7f
  tay                  ; bytes
  lda _ea+2
  asl
  tax                  ; cgram byte offset
  stx lz_tmp
  ldx #$7000
  A8
@c:
  cpy #0
  beq @d
  lda f:$7f0000,x
  phx
  ldx lz_tmp
  sta _cgram,x
  inx
  stx lz_tmp
  plx
  inx
  dey
  bra @c
@d:
  lda #3
  sta _cgram_dirty
  plb
  plp
  rtl

; void asset_map(void): ea0 = asset id, ea1 = bg (0..2), ea2 = add to every entry (tile/pal offset)
;   ea3 = destination row (0..31) ; map assets are 32 entries wide
CFUNC asset_map
  php
  phb
  AXY16
  A8
  lda #$80
  pha
  plb
  A16
  lda _ea+0
  jsr asset_find
  ldx #$7000
  jsr unpack7f
  lsr
  tay                  ; entries
  lda _ea+6
  asl
  asl
  asl
  asl
  asl
  asl                  ; row * 64 bytes
  sta lz_tmp
  lda _ea+2
  beq @b1
  cmp #1
  beq @b2
  lda #.loword(_bg3map)
  bra @set
@b1:
  lda #.loword(_bg1map)
  bra @set
@b2:
  lda #.loword(_bg2map)
@set:
  clc
  adc lz_tmp
  sta lz_bp
  A8
  lda #$7e
  sta lz_bp+2
  A16
  ldx #$7000
@e:
  cpy #0
  beq @done
  lda f:$7f0000,x
  clc
  adc _ea+4
  sta [lz_bp]
  inc lz_bp
  inc lz_bp
  inx
  inx
  dey
  bra @e
@done:
  A8
  lda _ea+2
  tax
  lda bgbits,x
  ora _bgmap_dirty
  sta _bgmap_dirty
  plb
  plp
  rtl
bgbits: .byte 1, 2, 4

; void far_copy(void): ea0 = source address, ea1 = source bank, ea2 = destination (in $7E), ea3 = length
CFUNC far_copy
  php
  phb
  AXY16
  lda _ea+0
  sta fc_src
  lda _ea+2
  sta fc_src+2
  lda _ea+4
  sta fc_dst
  A8
  lda #$7e
  sta fc_dst+2
  A16
  ldy #0
  A8
@l:
  cpy _ea+6
  beq @d
  lda [fc_src],y
  sta [fc_dst],y
  iny
  bra @l
@d:
  plb
  plp
  rtl

; void estat_block(void): ea0 = area, ea1 = men -> ea2 = count; copies the block to ea3 (in $7E)
CFUNC estat_block
  php
  phb
  AXY16
  lda _ea+0
  asl
  asl
  sta lz_tmp           ; area*4
  asl
  asl
  clc
  adc lz_tmp           ; area*20
  clc
  adc _ea+0            ; area*21
  clc
  adc _ea+2            ; + men
  asl
  asl                  ; *4
  tax
  lda f:estat_index,x
  clc
  adc #.loword(estat_data)
  sta fc_src
  lda f:estat_index+2,x
  sta _ea+4
  A8
  lda #^estat_data
  sta fc_src+2
  lda #$7e
  sta fc_dst+2
  A16
  lda _ea+6
  sta fc_dst
  ; bytes = count * 24
  lda _ea+4
  asl
  asl
  asl
  sta lz_tmp
  asl
  clc
  adc lz_tmp
  tax
  ldy #0
  A8
@l:
  cpy #0
  bne :+
  cpx #0
  beq @d
:
  cpx #0
  beq @d
  lda [fc_src],y
  sta [fc_dst],y
  iny
  dex
  bra @l
@d:
  plb
  plp
  rtl

; void asset_copy(void): ea0 = raw asset id, ea1 = destination (in $7E), ea2 = max length -> copies, returns length in ea2
CFUNC asset_copy
  php
  phb
  AXY16
  lda _ea+0
  jsr asset_find
  lda asset_size
  cmp _ea+4
  bcc :+
  lda _ea+4
:
  sta _ea+4
  lda _ea+2
  sta fc_dst
  A8
  lda #$7e
  sta fc_dst+2
  A16
  ldy #0
  A8
@l:
  cpy _ea+4
  beq @d
  lda [lz_src],y
  sta [fc_dst],y
  iny
  bra @l
@d:
  plb
  plp
  rtl

; void asset_where(void): ea0 = raw asset id -> ea1 = its address, ea2 = its bank (for DMA from ROM)
CFUNC asset_where
  php
  phb
  AXY16
  lda _ea+0
  jsr asset_find
  lda lz_src
  sta _ea+2
  lda lz_src+2
  sta _ea+4
  plb
  plp
  rtl
