; キシドラ！ SFC - startup, NMI, frame sync, DMA queue, input
.include "snes.inc"

.import _main
.importzp sp
.import __CODE_LOAD__, __CODE_RUN__, __CODE_SIZE__
.import __STUBS_LOAD__, __STUBS_RUN__, __STUBS_SIZE__
.import __RODATA_LOAD__, __RODATA_RUN__, __RODATA_SIZE__
.import __DATA_LOAD__, __DATA_RUN__, __DATA_SIZE__
.import __BSS_RUN__, __BSS_SIZE__
.import __LOWBSS_RUN__, __LOWBSS_SIZE__
.import spc_boot_far
.import RODATA_LOAD_BANK

.export __STARTUP__ : absolute = 1
.export _pad_lat, _rep_lat, _bg3_vram, _nmi_pc, _brk_info
.export _ea, _oam, _cgram, _cgram_dirty, _scroll, _tm_main, _tm_sub, _bright, _joy, _joy_new, _joy_rep
.export _frame, _bgmap_dirty, _nmi_ready, _vq_count, _vq_head, _vq, _bgmode, _cgadsub, _cgwsel, _coldata, _hdma_on
.export _bg1map, _bg2map, _bg3map, _oam_hide, _lag
.export vq_push, _hdma_tab1, _hdma_tab2, _mosaic

C_STACK_TOP = $ff00

; copy a segment from ROM to WRAM (16-bit A/X/Y)
.macro COPYSEG load, runa, size, dbank
  .local skip
  lda #.loword(size)
  beq skip
  dec a
  ldx #.loword(load)
  ldy #.loword(runa)
  .byte $54, dbank, ^load
skip:
.endmacro

; clear $7E:0000-$7F:FFFF by DMA (inline: the stack is cleared too)
.macro CLEAR_WRAM
  .a8
  .i16
  stz WMADDL
  stz WMADDM
  stz WMADDH
  ldx #$8008
  stx DMAP0          ; mode 0 fixed source, B=$80 (WMDATA)
  ldx #.loword(zero_byte)
  stx A1T0L
  lda #^zero_byte
  sta A1B0
  ldx #$0000         ; 64KB
  stx DAS0L
  lda #$01
  sta MDMAEN         ; $7E:0000-$FFFF (the stack is rebuilt by caller)
  ldx #$0000
  stx DAS0L
  sta MDMAEN         ; $7F:0000-$FFFF
.endmacro


.segment "LOWBSS"
_ea:          .res 32      ; argument block for engine calls (16 words)
_oam:         .res 544
_cgram:       .res 512
_cgram_dirty: .res 1       ; bit0: colors 0-127, bit1: 128-255
_scroll:      .res 12      ; BG1H BG1V BG2H BG2V BG3H BG3V
_tm_main:     .res 1
_tm_sub:      .res 1
_bright:      .res 1       ; INIDISP value (bit7 = force blank)
_bgmode:      .res 1
_mosaic:      .res 1
_cgadsub:     .res 1
_cgwsel:      .res 1
_coldata:     .res 3       ; r,g,b intensities for fixed color (0-31)
_hdma_on:     .res 1       ; HDMAEN mask for channels 6/7
_joy:         .res 2
_joy_new:     .res 2
_joy_rep:     .res 2
_nmi_pc:      .res 4
_brk_info:    .res 8       ; debug: P, PC, PB, S, A of the last BRK/COP       ; debug: where the last NMI interrupted the main program
_bg3_vram:    .res 2       ; BG3 tile base (word address) used by the text renderer
_pad_lat:     .res 2       ; edges latched by wait_frame, consumed by frame_wait
_rep_lat:     .res 2
joy_held_t:   .res 1
_frame:       .res 2
_lag:         .res 2
_bgmap_dirty: .res 1       ; bit0 BG1, bit1 BG2, bit2 BG3 (whole 2KB map)
_nmi_ready:   .res 1
_oam_hide:    .res 1
_vq_count:    .res 1
_vq_head:     .res 1       ; ring index of the oldest entry
_vq:          .res 8*24    ; DMA queue: vaddr(2) src(2) bank(1) size(2) mode(1)
_hdma_tab1:   .res 32
_hdma_tab2:   .res 32
nmi_tmp:      .res 2
nmi_budget:   .res 2

.segment "BSS"
_bg1map: .res 2048
_bg2map: .res 2048
_bg3map: .res 2048

VQ_MAX = 24

; BG tilemap VRAM word addresses (must match src/c/sfc.h)
BG1MAP_VRAM = $7000
BG2MAP_VRAM = $7400
BG3MAP_VRAM = $7800

.segment "STARTUP"
reset:
  sei
  clc
  xce
  AXY16
  ldx #$01ff
  txs
  lda #$0000
  tcd
  A8
  lda #$80
  pha
  plb
  lda #$8f
  sta INIDISP
  stz NMITIMEN
  stz HDMAEN
  stz MDMAEN
  lda #$01
  sta MEMSEL            ; FastROM
  jml reset_fast
.segment "ENGINE"
reset_fast:
  .a8
  .i16
  ; clear registers $2101-$2133
  ldx #$2101
@clr:
  stz $00,x
  inx
  cpx #$2134
  bne @clr
  stz BG1HOFS
  stz BG1HOFS
  stz BG1VOFS
  stz BG1VOFS
  stz BG2HOFS
  stz BG2HOFS
  stz BG2VOFS
  stz BG2VOFS
  stz BG3HOFS
  stz BG3HOFS
  stz BG3VOFS
  stz BG3VOFS
  lda #$80
  sta VMAIN
  lda #$30
  sta CGWSEL
  lda #$e0
  sta COLDATA
  ; clear all WRAM $7E:0000-$7F:FFFF except stack page (DMA fill)
  CLEAR_WRAM
  ; clear VRAM
  jsr clear_vram
  ; copy runtime + stubs to low WRAM (bank 0 mirror)
  AXY16
  COPYSEG __CODE_LOAD__, __CODE_RUN__, __CODE_SIZE__, $00
  COPYSEG __STUBS_LOAD__, __STUBS_RUN__, __STUBS_SIZE__, $00
  COPYSEG __RODATA_LOAD__, __RODATA_RUN__, __RODATA_SIZE__, $7e
  COPYSEG __DATA_LOAD__, __DATA_RUN__, __DATA_SIZE__, $7e
  A8
  lda #$80
  pha
  plb
  ; defaults
  lda #$8f
  sta _bright
  lda #$01
  sta _bgmode
  lda #$80
  sta _oam_hide
  ; boot the sound CPU
  jsl spc_boot_far
  ; enable NMI + auto joypad
  lda RDNMI
.ifdef PROFILE
  lda #16
  sta VTIMEL
  stz VTIMEL+1
  lda #$a1             ; + V-IRQ: the PC sampler
.else
  lda #$81
.endif
  sta NMITIMEN
  ; C environment: DBR=$7E, 8-bit regs, software stack
  AXY8
  lda #$7e
  pha
  plb
  lda #<C_STACK_TOP
  sta sp
  lda #>C_STACK_TOP
  sta sp+1
  cli
  .byte $22
  .word .loword(c_main_tramp)
  .byte $81            ; jsl $81:c_main_tramp (CODE1 is bank $81)
@hang:
  bra @hang

clear_vram:
  .a8
  .i16
  lda #$80
  sta VMAIN
  ldx #$0000
  stx VMADDL
  ldx #$1809         ; mode 1 fixed, B=$18
  stx DMAP0
  ldx #.loword(zero_byte)
  stx A1T0L
  lda #^zero_byte
  sta A1B0
  ldx #$0000
  stx DAS0L
  lda #$01
  sta MDMAEN
  ; CGRAM
  stz CGADD
  ldx #$2208
  stx DMAP0
  ldx #512
  stx DAS0L
  lda #$01
  sta MDMAEN
  rts

zero_byte: .byte 0

; ---------------------------------------------------------------- NMI
nmi_entry:
  jml nmi_fast
nmi_fast:
  AXY16
  pha
  phx
  phy
  phb
  phd
  lda 10,s             ; debug: P / PC / PB of the interrupted code
  sta f:_nmi_pc
  lda 12,s
  sta f:_nmi_pc+2
  lda #$0000
  tcd
  A8
  lda #$80
  pha
  plb
  lda RDNMI
  lda _nmi_ready
  bne :+
  jmp @lagframe
:
  lda #$8f
  sta INIDISP          ; blank during updates (restored below)
.ifdef PROFILE
  jsr nmi_vline0
.endif
  ; registers and HDMA first: they must be in place before line 0 even if an upload runs late
  ; registers
  lda _bgmode
  sta BGMODE
  lda _mosaic
  sta MOSAIC
  lda _tm_main
  sta TM
  lda _tm_sub
  sta TS
  lda _cgwsel
  sta CGWSEL
  lda _cgadsub
  sta CGADSUB
  lda _coldata
  ora #$20
  sta COLDATA
  lda _coldata+1
  ora #$40
  sta COLDATA
  lda _coldata+2
  ora #$80
  sta COLDATA
  lda _scroll+0
  sta BG1HOFS
  lda _scroll+1
  sta BG1HOFS
  lda _scroll+2
  sta BG1VOFS
  lda _scroll+3
  sta BG1VOFS
  lda _scroll+4
  sta BG2HOFS
  lda _scroll+5
  sta BG2HOFS
  lda _scroll+6
  sta BG2VOFS
  lda _scroll+7
  sta BG2VOFS
  lda _scroll+8
  sta BG3HOFS
  lda _scroll+9
  sta BG3HOFS
  lda _scroll+10
  sta BG3VOFS
  lda _scroll+11
  sta BG3VOFS
  ; HDMA channels 6/7 (set up by C via hdma tables)
  jsr hdma_setup
  jsr nmi_uploads
  stz _nmi_ready
.ifdef PROFILE
  jsr nmi_vline
.endif
  lda _bright
  sta INIDISP
  bra @joy
@lagframe:
  A16
  inc _lag
  A8
@joy:
  ; wait for auto-joypad read
:
  lda HVBJOY
  and #$01
  bne :-
  A16
  lda JOY1L
  tax
  eor _joy
  and JOY1L
  sta nmi_tmp
  ora _joy_new         ; edges accumulate until wait_frame latches them
  sta _joy_new
  stx _joy
  ; auto repeat: first after 16 frames, then every 4
  lda nmi_tmp
  ora _joy_rep
  sta _joy_rep
  txa
  beq @norep
  A8
  inc joy_held_t
  lda joy_held_t
  cmp #16
  bcc @repdone
  lda #12
  sta joy_held_t
  A16
  lda _joy
  and #$0f00          ; d-pad only
  ora _joy_rep
  sta _joy_rep
  A8
  bra @repdone
@norep:
  A8
  stz joy_held_t
@repdone:
  A16
  inc _frame
  A8
  AXY16
  pld
  plb
  ply
  plx
  pla
  rti

; upload OAM, palettes, tilemaps and queued DMA within one byte budget, so that the uploads end
; inside vblank (NTSC: lines 225-261, about 157 bytes of DMA a line; measured with PROFILE=1,
; see nmi_vline): OAM, CGRAM, the BG3 map,
; one of the BG1 / BG2 maps when 2KB are left, then queue entries with what is left.  What does
; not fit stays dirty / queued for the next NMI.
NMI_BUDGET = 4864
nmi_uploads:
  .a8
  .i16
  A16
  lda #NMI_BUDGET - 544
  sta nmi_budget
  A8
  ; OAM
  ldx #$0000
  stx OAMADDL
  ldx #$0400
  stx DMAP0
  ldx #.loword(_oam)
  stx A1T0L
  stz A1B0
  ldx #544
  stx DAS0L
  lda #$01
  sta MDMAEN
  ; palette
  lda _cgram_dirty
  beq @nopal
  stz CGADD
  ldx #$2200
  stx DMAP0
  ldx #.loword(_cgram)
  stx A1T0L
  stz A1B0
  ldx #512
  stx DAS0L
  lda #$01
  sta MDMAEN
  stz _cgram_dirty
  A16
  lda nmi_budget
  sec
  sbc #512
  sta nmi_budget
  A8
@nopal:
  ; tilemaps (whole maps): BG3, then BG1 or BG2
  lda #$80
  sta VMAIN
  ldx #$1801
  stx DMAP0
  lda #$7e
  sta A1B0
  lda _bgmap_dirty
  bit #$04
  beq @nobg3
  ldx #BG3MAP_VRAM
  stx VMADDL
  ldx #.loword(_bg3map)
  stx A1T0L
  ldx #2048
  stx DAS0L
  lda #$01
  sta MDMAEN
  lda _bgmap_dirty
  and #$fb
  sta _bgmap_dirty
  A16
  lda nmi_budget
  sec
  sbc #2048
  sta nmi_budget
  A8
@nobg3:
  A16
  lda nmi_budget
  cmp #2048
  A8
  bcc @maps_done
  lda _bgmap_dirty
  and #$03
  beq @maps_done
  cmp #$03
  bne @one
  lda _frame           ; both dirty: take turns
  and #$01
  inc a
@one:
  bit #$01
  beq @bg2
  ldx #BG1MAP_VRAM
  stx VMADDL
  ldx #.loword(_bg1map)
  stx A1T0L
  ldx #2048
  stx DAS0L
  lda #$01
  sta MDMAEN
  lda _bgmap_dirty
  and #$fe
  sta _bgmap_dirty
  bra @mapped
@bg2:
  ldx #BG2MAP_VRAM
  stx VMADDL
  ldx #.loword(_bg2map)
  stx A1T0L
  ldx #2048
  stx DAS0L
  lda #$01
  sta MDMAEN
  lda _bgmap_dirty
  and #$fd
  sta _bgmap_dirty
@mapped:
  A16
  lda nmi_budget
  sec
  sbc #2048
  sta nmi_budget
  A8
@maps_done:
  jsr vq_run
  rts

.ifdef PROFILE
; the scanline the uploads ended on (262 + line when they ran past the frame's start): the
; largest since power-on at $7F:DBFA (read by tools/harness)
NMI_VMAX = $7fdbfa
; debug ring of 32 frames at $7F:DA00: entry line, end line, budget before the queue, queue
; entries left, map dirty bits left
nmi_vline0:
  .a8
  .i16
  jsr vline
  .a16
  pha
  lda _frame
  and #$1f
  asl
  asl
  asl
  tax
  pla
  sta f:$7fda00,x
  A8
  rts
vline:                 ; A16 = the current scanline (262 + n past the frame's start)
  .a8
  lda STAT78
  lda SLHV
  lda OPVCT
  xba
  lda OPVCT
  and #$01
  xba
  A16
  cmp #225
  bcs :+
  clc
  adc #262
:
  rts
nmi_vline:
  .a8
  .i16
  jsr vline
  .a16
  pha
  lda _frame
  and #$1f
  asl
  asl
  asl
  tax
  pla
  sta f:$7fda02,x
  pha
  lda nmi_budget
  sta f:$7fda04,x
  A8
  lda _vq_count
  sta f:$7fda06,x
  lda _bgmap_dirty
  sta f:$7fda07,x
  lda _bright          ; the maximum only over frames that are shown
  bmi @no
  A16
  pla
  cmp f:NMI_VMAX
  bcc :+
  sta f:NMI_VMAX
:
  A8
  rts
@no:
  A16
  pla
  A8
  rts
.endif

; run DMA queue entries while budget lasts
vq_run:
  .a8
  .i16
  lda _vq_count
  bne :+
  rts
:
  A16
  lda nmi_budget
  sta nmi_tmp
  lda _vq_head        ; read index (byte offset of the oldest entry)
  and #$00ff
  asl
  asl
  asl
  tay
  A8
@loop:
  lda _vq_count
  bne :+
  jmp @done
:
  A16
  lda _vq+5,y        ; size
  cmp nmi_tmp
  bcc @fits
  beq @fits
  ; does not fit: if nothing but OAM went this frame, still do it (large single entry)
  lda nmi_tmp
  cmp #NMI_BUDGET - 544
  bne @stop
@fits:
  lda nmi_tmp
  sec
  sbc _vq+5,y
  bcs :+
  lda #0
:
  sta nmi_tmp
  lda _vq+0,y
  sta VMADDL
  lda _vq+2,y
  sta A1T0L
  lda _vq+5,y
  sta DAS0L
  A8
  lda _vq+4,y
  sta A1B0
  lda _vq+7,y        ; mode: 0 = words to VRAM, 1 = low bytes only, 2 = high bytes only
  beq @w
  cmp #1
  beq @lo
  lda #$80
  sta VMAIN
  ldx #$1900
  stx DMAP0
  bra @go
@lo:
  stz VMAIN
  ldx #$1800
  stx DMAP0
  bra @go
@w:
  lda #$80
  sta VMAIN
  ldx #$1801
  stx DMAP0
@go:
  lda #$01
  sta MDMAEN
  lda #$80
  sta VMAIN
  dec _vq_count
  lda _vq_head        ; the queue is a ring: no entries are moved
  inc a
  cmp #VQ_MAX
  bcc :+
  lda #0
:
  sta _vq_head
  A16
  and #$00ff
  asl
  asl
  asl
  tay
  A8
  jmp @loop
@stop:
  A8
@done:
  rts

; push one entry: ea[0]=vaddr ea[1]=src lo ea[2]=bank ea[3]=size ea[4]=mode
; returns carry set if the queue is full
vq_push:
  .a8
  .i16
  lda _vq_count
  cmp #VQ_MAX
  bcc :+
  sec
  rts
:
  clc                  ; slot (head + count) mod VQ_MAX
  adc _vq_head
  cmp #VQ_MAX
  bcc :+
  sbc #VQ_MAX
:
  A16
  and #$00ff
  asl
  asl
  asl
  tax
  lda _ea+0
  sta _vq+0,x
  lda _ea+2
  sta _vq+2,x
  lda _ea+6
  sta _vq+5,x
  A8
  lda _ea+4
  sta _vq+4,x
  lda _ea+8
  sta _vq+7,x
  inc _vq_count
  clc
  rts

hdma_setup:
  .a8
  .i16
  lda _hdma_on
  beq @off
  ; channel 6: table 1 -> BG1HOFS/BG1VOFS (mode 3: 2 regs write twice)
  bit #$40
  beq @no6
  lda #$03
  sta $4360
  lda #<BG1HOFS
  sta $4361
  ldx #.loword(_hdma_tab1)
  stx $4362
  stz $4364
@no6:
  lda _hdma_on
  bit #$80
  beq @no7
  ; channel 7: table 2 -> BG2HOFS/BG2VOFS
  lda #$03
  sta $4370
  lda #<BG2HOFS
  sta $4371
  ldx #.loword(_hdma_tab2)
  stx $4372
  stz $4374
@no7:
  lda _hdma_on
@off:
  sta HDMAEN
  rts

; ---------------------------------------------------------------- C-callable
; void wait_frame(void): hand the shadows to the NMI and wait for it
CFUNC wait_frame
  php
  AXY16
  phb
  A8
  lda #$80
  pha
  plb
  lda #$01
  sta _nmi_ready
  A16
  lda _frame
@w:
  cmp _frame
  beq @w
  ; latch joypad edges (the next NMI is a frame away)
  lda _joy_new
  ora _pad_lat
  sta _pad_lat
  lda _joy_rep
  ora _rep_lat
  sta _rep_lat
  stz _joy_new
  stz _joy_rep
  A8
  plb
  plp
  rtl

; void vram_now(void): immediate DMA (display must be blanked)
;   ea[0]=vaddr ea[1]=src lo ea[2]=bank ea[3]=size
CFUNC vram_now
  php
  AXY16
  phb
  A8
  lda #$80
  pha
  plb
  lda #$80
  sta VMAIN
  ldx _ea+0
  stx VMADDL
  ldx #$1801
  stx DMAP0
  ldx _ea+2
  stx A1T0L
  lda _ea+4
  sta A1B0
  ldx _ea+6
  stx DAS0L
  lda #$01
  sta MDMAEN
  plb
  plp
  rtl

; unsigned char vram_queue(void): queue ea[0..4]; returns 1 if queued
CFUNC vram_queue
  php
  AXY16
  phb
  A8
  lda #$80
  pha
  plb
  I16
  sei
  jsr vq_push
  cli
  lda #0
  rol
  eor #1
  plb
  plp
  .i8
  ldx #0
  rtl
  .i16

; void force_blank(unsigned char on): immediate INIDISP write
CFUNC force_blank
  php
  A8
  cmp #0
  beq @on
  lda #$8f
  sta f:INIDISP
  sta f:_bright
  plp
  rtl
@on:
  lda f:_bright
  and #$7f
  sta f:_bright
  sta f:INIDISP
  plp
  rtl

; void set_vram_layout(void): BG base registers (called once)
CFUNC set_vram_layout
  php
  A8
  ; BG1 map $7000, BG2 map $7400, BG3 map $7800 (32x32 each)
  lda #(BG1MAP_VRAM >> 8) & $fc
  sta f:BG1SC
  lda #(BG2MAP_VRAM >> 8) & $fc
  sta f:BG2SC
  lda #(BG3MAP_VRAM >> 8) & $fc
  sta f:BG3SC
  ; BG1 tiles $0000, BG2 tiles $2000 (word addr), BG3 tiles $6000
  lda #$20
  sta f:BG12NBA
  lda #$06
  sta f:BG34NBA
  ; OBJ: 8x8 / 32x32, name base $4000 (word), second table at $5000
  lda #$22
  sta f:OBSEL
  A16
  lda #$6000
  sta f:_bg3_vram
  plp
  rtl

; void vram_clear(void): zero all 64KB of VRAM (display must be blanked)
CFUNC vram_clear
  php
  phb
  A8
  I16
  lda #$80
  pha
  plb
  lda #$80
  sta VMAIN
  ldx #$0000
  stx VMADDL
  ldx #$1809
  stx DMAP0
  ldx #.loword(zero_byte)
  stx A1T0L
  lda #^zero_byte
  sta A1B0
  ldx #$0000
  stx DAS0L
  lda #$01
  sta MDMAEN
  plb
  plp
  rtl

; void menu_layout(void): BG3 tiles at $4000 (1024 text tiles), OBJ at $6000 (256 tiles)
CFUNC menu_layout
  php
  A8
  lda #$04
  sta f:BG34NBA
  lda #$23
  sta f:OBSEL
  A16
  lda #$4000
  sta f:_bg3_vram
  plp
  rtl

; cc65 calls function pointers through jmpvec (a patched "jmp abs").  The runtime keeps it in
; DATA ($7E:xxxx), which code banks cannot reach; this copy lives in the low WRAM CODE copy
; that every bank mirrors at $0000-$1FFF.  The jmp stays inside the caller's bank.
.export jmpvec
.segment "CODE"
jmpvec:
  jmp $ffff

.segment "CODE1"
c_main_tramp:
  jsr _main
  rtl

.segment "VECTORS"
  ; native: unused, unused, COP, BRK, ABORT, NMI, unused, IRQ
.ifdef PROFILE
  .word 0, 0, .loword(brk_trap), .loword(brk_trap), .loword(nmi_dummy), .loword(nmi_vec), 0, .loword(irq_prof)
.else
  .word 0, 0, .loword(brk_trap), .loword(brk_trap), .loword(nmi_dummy), .loword(nmi_vec), 0, .loword(irq_dummy)
.endif
  ; emulation: unused, unused, COP, unused, ABORT, NMI, RESET, IRQ/BRK
  .word 0, 0, .loword(nmi_dummy), 0, .loword(nmi_dummy), .loword(nmi_dummy), .loword(reset), .loword(irq_dummy)

.segment "STARTUP"
nmi_vec:
  jml nmi_fast
nmi_dummy:
irq_dummy:
  rti
.ifdef PROFILE
; profiling build (PROFILE=1 python build.py rom): a V-IRQ every 16 lines stores the
; interrupted PC/PB in a ring at $7F:F000 (index at $7F:EFFE) for tools/harness/prof.py
irq_prof:
  rep #$30
  .a16
  .i16
  pha
  phx
  lda f:$7fefff-1
  and #$0ffc
  tax
  lda 6,s
  sta f:$7ff000,x
  lda 8,s
  and #$00ff
  sta f:$7ff002,x
  txa
  clc
  adc #4
  sta f:$7fefff-1
  sep #$20
  .a8
  lda f:TIMEUP
  lda f:prof_line
  clc
  adc #16
  cmp #224
  bcc :+
  sbc #224
:
  sta f:prof_line
  sta f:VTIMEL
  rep #$30
  plx
  pla
  rti
.segment "LOWBSS"
prof_line: .res 1
.segment "STARTUP"
.endif
; BRK/COP: a crash. Red backdrop and stop (the stack keeps the address for debugging)
brk_trap:
  sei
  rep #$30
  .a16
  .i16
  sta f:_brk_info+6    ; A
  lda 1,s              ; P, PCL
  sta f:_brk_info
  lda 3,s              ; PCH, PB
  sta f:_brk_info+2
  tsc
  sta f:_brk_info+4
  sep #$20
  .a8
  lda #$0f
  sta f:INIDISP
  lda #0
  sta f:CGADD
  lda #$1f
  sta f:CGDATA
  lda #0
  sta f:CGDATA
  sta f:NMITIMEN
@hang:
  bra @hang
