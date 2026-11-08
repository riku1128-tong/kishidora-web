; one weapon alone can fire a special (src/c/loadout.c solo_ok_sk, the INV_SOLO bits of the bag): the
; classes of the list's hit skills index a bit table in the ROM blob solo_data (tools/make_data.py
; build_solo, checked there against the web BFS on every list of 1-3 skills).  Blob layout:
SOLO_KICK = 0          ; the class of SK_KICK
SOLO_CLS  = 64         ; class of each skill: 255 buff, 1..K the skills of the specials, K+1 any other
SOLO_MUL1 = 128        ; u16 C * c
SOLO_MUL2 = 192        ; u16 C * C * c
SOLO_WPN  = 256        ; per item id: 1 = a weapon (web solo_ok: weapons only)
SOLO_TAB  = 512        ; bit c0 + C c1 + C^2 c2
INV_SOLO  = $40        ; Inv.slots bit 6 (save.h)
.include "snes.inc"

.import solo_data
.importzp sp

.segment "LOWBSS"
so_p:  .res 2          ; the skill list
so_ix: .res 2          ; the bit index
so_m:  .res 2          ; hit skills so far
so_k:  .res 2          ; 2 while every hit skill is the kick
so_q:  .res 2          ; solo_bag: the item
so_n:  .res 2          ; solo_bag: items left

.segment "ENGINE"

; u8 __fastcall__ solo_eval(const u8 *sk): sk[0..2] (255 = none) -> bit 0 = this list alone can fire a
; special (web solo_ok), bit 1 = it is kick-only (web kick_only: the kick and buffs only).  DB = $7E.
CFUNC solo_eval
  php
  sta so_p
  stx so_p+1
  AXY16
  jsr so_eval
  plp
  .a8
  .i8
  ldx #0
  rtl

; void __fastcall__ solo_bag(Inv *it, u16 n): the INV_SOLO bit of n bag items from it on (Inv = 8 bytes:
; id, sk[3], slots, enh[3]): set when the item is a weapon whose skills alone fire a special, else
; clear.  C stack: it; A / X = n.  DB = $7E.
CFUNC solo_bag
  php
  sta so_n
  stx so_n+1
  AXY16
  lda (sp)
  sta so_q
  lda sp
  clc
  adc #2
  sta sp
@item:
  lda so_n
  beq @done
  ldx so_q
  lda a:0,x
  and #$00ff
  tax
  A8
  lda f:solo_data+SOLO_WPN,x
  A16
  beq @off             ; a shield (or not an item)
  lda so_q
  inc
  sta so_p             ; the skills
  jsr so_eval
  lsr
  bcc @off
  ldx so_q
  A8
  lda a:4,x
  ora #INV_SOLO
  sta a:4,x
  A16
  bra @next
@off:
  ldx so_q
  A8
  lda a:4,x
  and #<~INV_SOLO
  sta a:4,x
  A16
@next:
  lda so_q
  clc
  adc #8
  sta so_q
  dec so_n
  bra @item
@done:
  plp
  rtl

; the flags of the list at $7E:so_p (A16 I16 in and out): A = bit 0 fires, bit 1 kick-only
so_eval:
  .a16
  .i16
  stz so_ix
  stz so_m
  lda #2
  sta so_k
  ldx so_p
  jsr so_one
  ldx so_p
  inx
  jsr so_one
  ldx so_p
  inx
  inx
  jsr so_one
  lda so_m
  beq @none            ; no hit skill: no special, not kick-only
  lda so_ix
  lsr
  lsr
  lsr
  tax                  ; byte
  lda so_ix
  and #7
  tay                  ; bit
  lda f:solo_data+SOLO_TAB,x
  cpy #0
  beq @b
@sh:
  lsr
  dey
  bne @sh
@b:
  and #1
  ora so_k
  rts
@none:
  lda #0
  rts

; the skill at $7E:X: its class into the index (A16 I16 in and out)
so_one:
  .a16
  .i16
  lda a:0,x
  and #$00ff
  cmp #$ff
  beq @out             ; no skill
  tax
  lda f:solo_data+SOLO_CLS,x
  and #$00ff
  cmp #$ff
  beq @out             ; a buff: no history place
  tay
  A8
  cmp f:solo_data+SOLO_KICK
  A16
  beq :+
  stz so_k             ; a hit skill that is not the kick
:
  tya
  ldy so_m
  beq @add             ; first: c0
  asl
  tax
  cpy #1
  bne @m2
  lda f:solo_data+SOLO_MUL1,x
  bra @add
@m2:
  lda f:solo_data+SOLO_MUL2,x
@add:
  clc
  adc so_ix
  sta so_ix
  inc so_m
@out:
  rts
