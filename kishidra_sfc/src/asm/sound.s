; sound: SPC700 boot (IPL upload of driver + sound effects) and BGM streaming
; (protocol: tools/audio/driver.py)
.include "snes.inc"

.import bgm_table, bgm_banks, spc_image, se_dir, se_vol, _ea
.import SPC_IMAGE_LEN_LO, SPC_IMAGE_LEN_HI
.export spc_boot_far

APU0 = $2140
APU1 = $2141
APU2 = $2142
APU3 = $2143
BLOCKS_PER_BANK = 3640
RING_BLOCKS = 1813
PREFILL = 640
PER_FRAME = 24
SPC_IMAGE_LEN = SPC_IMAGE_LEN_LO + SPC_IMAGE_LEN_HI * 65536

.segment "ENGZP": zeropage
s_ptr:   .res 3

.segment "LOWBSS"
s_on:    .res 1        ; 0 = off, 1 = streaming, 2 = silence tail
s_seq:   .res 1
s_bi:    .res 1        ; index into bgm_banks
s_left:  .res 2        ; blocks left in this bank
s_rem:   .res 3        ; blocks left in the track
s_trk:   .res 2        ; bgm_table offset of the track
s_tail:  .res 2
s_p2:    .res 1
s_p3:    .res 1
s_cnt:   .res 2
s_ready: .res 1

.segment "ENGINE"

; ---------------------------------------------------------------- IPL upload (reset)
spc_boot_far:
  php
  phb
  A8
  I16
  lda #$80
  pha
  plb
  stz s_on
  stz s_ready
  ldx #0
@wait:                 ; IPL ready: $BBAA (give up after a while: no APU)
  dex
  bne :+
  jmp @none
:
  lda APU0
  cmp #$AA
  bne @wait
  lda APU1
  cmp #$BB
  bne @wait
  ldx #$0200
  stx APU2
  lda #$01
  sta APU1
  lda #$CC
  sta APU0
:
  cmp APU0
  bne :-
  lda #^spc_image
  sta s_ptr+2
  ldx #.loword(spc_image)
  stx s_ptr
  ldx #.loword(SPC_IMAGE_LEN)
  stx s_cnt
  ldy #0
  A16
  lda #0
  A8
@byte:
  lda [s_ptr]
  sta APU1
  tya                  ; transfer index (low byte)
  sta APU0
:
  cmp APU0
  bne :-
  iny
  A16
  inc s_ptr
  bne :+
  A8                   ; crossed into the next bank's $0000: continue at $8000
  inc s_ptr+2
  A16
  lda #$8000
  sta s_ptr
:
  dec s_cnt
  A8
  bne @byte
  ; execute the driver at $0200
  ldx #$0200
  stx APU2
  stz APU1
  tya
  inc a
  bne :+
  inc a
:
  sta APU0
  and #$7f
  sta s_seq
  ; wait until the driver says it is ready
  ldx #0
@rdy:
  dex
  beq @none
  lda APU3
  cmp #$a5
  bne @rdy
  lda #1
  sta s_ready
@none:
  plb
  plp
  rtl

; ---------------------------------------------------------------- transactions (A8 I16, DBR $80)
; command A, parameters s_p2 / s_p3
cmd:
  .a8
  .i16
  sta APU1
  lda s_p2
  sta APU2
  lda s_p3
  sta APU3
  lda s_seq
  inc a
  and #$7f
  sta s_seq
  sta APU0
:
  cmp APU0
  bne :-
  rts

; one stream block (9 bytes at [s_ptr]) = three transactions
send_block:
  .a8
  .i16
  ldy #0
  .repeat 3
  lda [s_ptr],y
  sta APU1
  iny
  lda [s_ptr],y
  sta APU2
  iny
  lda [s_ptr],y
  sta APU3
  iny
  lda s_seq
  inc a
  and #$7f
  sta s_seq
  ora #$80
  sta APU0
:
  cmp APU0
  bne :-
  .endrep
  rts

send_silence:
  .a8
  .i16
  .repeat 3
  stz APU1
  stz APU2
  stz APU3
  lda s_seq
  inc a
  and #$7f
  sta s_seq
  ora #$80
  sta APU0
:
  cmp APU0
  bne :-
  .endrep
  rts

; load the stream position from bgm_table + X (+0 = start, +8 = loop)
load_pos:
  .a8
  .i16
  lda f:bgm_table+0,x
  sta s_bi
  lda f:bgm_table+1,x
  sta s_ptr
  lda f:bgm_table+2,x
  sta s_ptr+1
  lda f:bgm_table+3,x
  sta s_left
  lda f:bgm_table+4,x
  sta s_left+1
  lda f:bgm_table+5,x
  sta s_rem
  lda f:bgm_table+6,x
  sta s_rem+1
  lda f:bgm_table+7,x
  sta s_rem+2
  phx
  ldx #0
  lda s_bi
  A16
  and #$00ff
  tax
  A8
  lda f:bgm_banks,x
  sta s_ptr+2
  plx
  rts

; send the next block of the track (or silence), advance
stream_one:
  .a8
  .i16
  lda s_on
  cmp #2
  bne @music
  jsr send_silence
  A16
  dec s_tail
  A8
  bne @r
  lda #1               ; tail done: stop the voice
  jsr cmd
  stz s_on
@r:
  rts
@music:
  jsr send_block
  A16
  lda s_ptr
  clc
  adc #9
  sta s_ptr
  dec s_left
  bne :+
  A8
  inc s_bi
  lda s_bi
  A16
  and #$00ff
  tax
  A8
  lda f:bgm_banks,x
  sta s_ptr+2
  A16
  lda #$8000
  sta s_ptr
  lda #BLOCKS_PER_BANK
  sta s_left
:
  ; blocks left in the track (24-bit)
  lda s_rem
  sec
  sbc #1
  sta s_rem
  A8
  lda s_rem+2
  sbc #0
  sta s_rem+2
  ora s_rem
  ora s_rem+1
  bne @r2
  ; end of track: loop or fade to silence
  ldx s_trk
  lda f:bgm_table+13,x
  ora f:bgm_table+14,x
  ora f:bgm_table+15,x
  beq @once
  A16
  txa
  clc
  adc #8
  tax
  A8
  jsr load_pos
  rts
@once:
  lda #2
  sta s_on
  ldx #RING_BLOCKS + 16
  stx s_tail
@r2:
  rts

; ---------------------------------------------------------------- C-callable
; void bgm_start(void): ea0 = track (0..), ea1 = pitch
CFUNC bgm_start
  php
  phb
  A8
  I16
  lda #$80
  pha
  plb
  lda s_ready
  beq @out
  lda #1               ; STOP: empty the ring
  jsr cmd
  lda _ea+0
  A16
  and #$00ff
  asl
  asl
  asl
  asl
  sta s_trk
  tax
  A8
  jsr load_pos
  lda #1
  sta s_on
  ldx #PREFILL
  stx s_cnt
@pre:
  jsr stream_one
  lda s_on
  cmp #1
  bne @go              ; a short track ended during the prefill
  ldx s_cnt
  dex
  stx s_cnt
  bne @pre
@go:
  lda _ea+2
  sta s_p2
  lda _ea+3
  sta s_p3
  lda #2               ; START
  jsr cmd
@out:
  plb
  plp
  rtl

; void bgm_halt(void)
CFUNC bgm_halt
  php
  phb
  A8
  I16
  lda #$80
  pha
  plb
  lda s_ready
  beq :+
  stz s_on
  lda #1
  jsr cmd
:
  plb
  plp
  rtl

; void sound_tick(void): feed the ring (call every frame)
CFUNC sound_tick
  php
  phb
  A8
  I16
  lda #$80
  pha
  plb
  lda s_on
  beq @out
  lda APU2             ; free blocks reported by the driver
  cmp #PER_FRAME
  bcc :+
  lda #PER_FRAME
:
  sta s_cnt
@l:
  lda s_cnt
  beq @out
  dec s_cnt
  jsr stream_one
  lda s_on
  bne @l
@out:
  plb
  plp
  rtl

; void se_start(void): ea0 = SE id, ea1 = volume scale (0-8)
CFUNC se_start
  php
  phb
  A8
  I16
  lda #$80
  pha
  plb
  lda s_ready
  beq @out
  A16
  lda _ea+0
  and #$00ff
  tax
  A8
  lda f:se_dir,x
  beq @out
  sta s_p2
  lda f:se_vol,x
  sta s_p3
  lda #3
  jsr cmd
@out:
  plb
  plp
  rtl

; void bgm_volume(void): ea0 = volume (0-127)
CFUNC bgm_volume
  php
  phb
  A8
  I16
  lda #$80
  pha
  plb
  lda s_ready
  beq :+
  lda _ea+0
  sta s_p2
  lda #4
  jsr cmd
:
  plb
  plp
  rtl
