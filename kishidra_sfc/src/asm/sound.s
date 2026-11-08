; sound: SPC700 boot (IPL upload of driver + sound effects) and BGM streaming
; (protocol: tools/audio/driver.py)
.include "snes.inc"

.import bgm_table, bgm_seg_bank, bgm_seg_ptr, bgm_seg_len, spc_image, se_dir, se_vol, _ea, _frame
.import SPC_IMAGE_LEN_LO, SPC_IMAGE_LEN_HI, SPC_BOOT_LEN, SPC_SPLIT_END, se_thr
.export spc_boot_far, stream_idle

APU0 = $2140
APU1 = $2141
APU2 = $2142
APU3 = $2143
RING_BLOCKS = 1813
PREFILL = 640
PER_FRAME = 13          ; blocks per frame while the ring is nearly full (11.5 are played)
PER_FRAME_MAX = 24      ; ... and while it is low (after slow frames)
LOAD_PER = 48           ; sample blocks per count of the ring (stream_idle; 11 ms)
QUIET = 32              ; lines (2 ms) after a block: the driver has left its burst (1.3 ms) and updated port 2
SPC_IMAGE_LEN = SPC_IMAGE_LEN_LO + SPC_IMAGE_LEN_HI * 65536

.segment "ENGZP": zeropage
s_ptr:   .res 3
s_lptr:  .res 3        ; sound effect samples still to upload

.segment "LOWBSS"
s_on:    .res 1        ; 0 = off, 1 = streaming, 2 = silence tail
s_seq:   .res 1
s_bi:    .res 1        ; stream segment (bgm_seg_*)
s_left:  .res 2        ; blocks left in this segment
s_rem:   .res 3        ; blocks left in the track
s_trk:   .res 2        ; bgm_table offset of the track
s_tail:  .res 2
s_p2:    .res 1
s_p3:    .res 1
s_cnt:   .res 2
s_allow: .res 1        ; blocks the ring still takes (sound_tick, stream_idle)
s_rf:    .res 2        ; _frame when s_allow was set
s_sf:    .res 2        ; _frame and scanline of the last block sent
s_sl:    .res 2
s_lall:  .res 1        ; sample blocks that may go before the next count
s_ready: .res 1
s_lleft: .res 2        ; bytes of samples still to upload (0 = sound effects ready)

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
  lda #<.bank(spc_image)
  sta s_ptr+2
  ldx #.loword(spc_image)
  stx s_ptr
  ldx #SPC_BOOT_LEN     ; driver + directory only; the samples follow after boot
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
  sta s_seq            ; the driver starts from this port 0 value
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
  ; the samples: LOAD to $0200 + SPC_BOOT_LEN, streamed by sound_tick
  ldx #.loword(SPC_IMAGE_LEN) - SPC_BOOT_LEN
  stx s_lleft
  ; s_ptr now points just past the driver in the image
  lda s_ptr
  sta s_lptr
  lda s_ptr+1
  sta s_lptr+1
  lda s_ptr+2
  sta s_lptr+2
  ldx #$0200 + SPC_BOOT_LEN
  stx s_p2
  lda #5
  jsr cmd
@none:
  plb
  plp
  rtl

; one transaction: three bytes at [ptr],y (y advances), port 0 = s_seq + 4
.macro XFER ptr
  lda [ptr],y
  sta APU1
  iny
  lda [ptr],y
  sta APU2
  iny
  lda [ptr],y
  sta APU3
  iny
  lda s_seq
  clc
  adc #4
  sta s_seq
  sta APU0
:
  cmp APU0
  bne :-
.endmacro

; one block of the sound effect samples (raw load, type 2; A8 I16, DB $80)
load_one:
  .a8
  .i16
  lda s_seq
  and #$fc
  ora #2
  sta s_seq
  ldy #0
  XFER s_lptr
  XFER s_lptr
  XFER s_lptr
  jsr mark
  A16
  lda s_lptr
  clc
  adc #9
  cmp #SPC_SPLIT_END   ; the image continues at $8000 of the next bank
  bne :+
  A8
  inc s_lptr+2
  A16
  lda #$8000
:
  sta s_lptr
  lda s_lleft
  sec
  sbc #9
  bcs :+
  lda #0               ; (all loaded)
:
  sta s_lleft
  A8
  rts

; s_allow = what the ring takes, from the driver's free count (A8 I16, DB $80).  The driver
; updates port 2 only between bursts of blocks, so the count is taken once no block has gone for
; QUIET lines; until then what is left of the last count stands (the space only grows meanwhile).
; Taken when the last count is used up, and once a frame.  At most 255.
refresh:
  .a8
  .i16
  lda s_allow
  beq :+
  ldx _frame
  cpx s_rf
  beq @r
:
  jsr quiet
  bcc @r
  ldx _frame
  stx s_rf
  lda APU2             ; free blocks / 8 reported by the driver
  cmp #32
  bcc :+
  lda #$ff
  bra :++
:
  asl
  asl
  asl
:
  sta s_allow
  lda #LOAD_PER
  sta s_lall
@r:
  rts

; carry set when no block has gone for QUIET lines (A8 I16)
quiet:
  .a8
  .i16
  A16
  lda _frame
  sec
  sbc s_sf
  beq @same
  cmp #2
  bcs @yes             ; two frames on
  jsr vline
  clc
  adc #262             ; one frame on
  bra @cmp
@same:
  jsr vline
@cmp:
  sec
  sbc s_sl
  cmp #QUIET
  A8
  rts
@yes:
  A8
  sec
  rts

; A16 = lines since the NMI that started this _frame (0..261; A16 I16).  _frame goes up in the
; NMI (line 225), and the game runs only outside it, so line 225 starts a frame here
vline:
  .a16
  A8
  lda SLHV
  lda STAT78
  lda OPVCT
  xba
  lda OPVCT
  and #1
  xba
  A16
  sec
  sbc #225
  bcs :+
  adc #262             ; (carry clear: + 262)
:
  rts

; a block went: when (A8 I16)
mark:
  .a8
  .i16
  ldx _frame
  stx s_sf
  A16
  jsr vline
  sta s_sl
  A8
  rts

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
  and #$fc             ; type 0
  clc
  adc #4
  sta s_seq
  sta APU0
:
  cmp APU0
  bne :-
  rts

; one stream block (9 bytes at [s_ptr]) = three transactions (type 1)
send_block:
  .a8
  .i16
  lda s_seq
  and #$fc
  ora #1
  sta s_seq
  ldy #0
  XFER s_ptr
  XFER s_ptr
  XFER s_ptr
  jmp mark

send_silence:
  .a8
  .i16
  lda s_seq
  and #$fc
  ora #1
  sta s_seq
  .repeat 3
  stz APU1
  stz APU2
  stz APU3
  lda s_seq
  clc
  adc #4
  sta s_seq
  sta APU0
:
  cmp APU0
  bne :-
  .endrep
  jmp mark

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
  lda s_bi
  A16
  and #$00ff
  tax
  A8
  lda f:bgm_seg_bank,x
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
  A8                   ; next segment of the stream: bank, start, length (tools/romplace.py)
  inc s_bi
  lda s_bi
  A16
  and #$00ff
  tax
  A8
  lda f:bgm_seg_bank,x
  sta s_ptr+2
  A16
  txa
  asl
  tax
  lda f:bgm_seg_ptr,x
  sta s_ptr
  lda f:bgm_seg_len,x
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
  lda s_ready
  beq @done            ; (no sound chip)
  lda s_on
  beq @out
  jsr refresh
  ; the stream goes while wait_frame waits (stream_idle): sending it here, before the wait, makes
  ; a pass that nearly fills a frame take two.  Only a ring that runs low gets blocks now.
  lda APU2
  cmp #180
  bcs @max             ; 1440+ free (under 0.53 s left in the ring of 2.6 s): catch up
  cmp #160
  bcs @norm            ; 1280+ free (under 0.76 s left): the frame's share now
  lda #0
  bra @set
@norm:
  lda #PER_FRAME
  bra @set
@max:
  lda #PER_FRAME_MAX
@set:
  cmp s_allow
  bcc :+
  lda s_allow
:
  sta s_cnt
  eor #$ff             ; s_allow -= s_cnt
  sec
  adc s_allow
  sta s_allow
@l:
  lda s_cnt
  beq @out
  dec s_cnt
  jsr stream_one
  lda s_on
  bne @l
@out:
  ldx s_lleft          ; sound effect samples: two blocks a pass even with no time to wait
  beq @done
  jsr load_one
  ldx s_lleft
  beq @done
  jsr load_one
@done:
  plb
  plp
  rtl

; called again and again while wait_frame waits for the next frame (jsl, A8 I16, DB $80): one
; block of the stream while the ring has room, else one of the sound effect samples still to
; upload.  Every wait feeds the music, whoever waits; sound_tick sends before the wait only when
; the ring runs low (a pass that has no time left to wait).
stream_idle:
  .a8
  .i16
  lda s_ready
  beq @no
  jsr refresh
  lda s_on
  beq @load
  lda s_allow
  beq @load
  dec s_allow
  jsr stream_one
  rtl
@load:
  lda s_lall           ; (a few: the driver updates its count only once they stop)
  beq @no
  ldx s_lleft
  beq @no
  dec s_lall
  jsr load_one
@no:
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
  asl
  tax
  lda s_lleft          ; still uploading: only once this sample is in ARAM
  cmp f:se_thr,x
  beq :+
  bcs @out             ; (plp restores the register widths)
:
  txa
  lsr
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
