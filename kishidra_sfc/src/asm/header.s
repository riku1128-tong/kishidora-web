; ROM header ($FFB0-$FFDF): LoROM + FastROM, 4MB, SRAM 32KB, Japan
; ROM_MB=6 / 8 (ExLoROM): build.py finish_rom sets $FFD7 to $0D (8 MB class, also for 6 MB), copies
; bank $80 (with this header and the vectors) to bank $00 at file 4 MB, and writes the checksum
; (mirror sum) into both copies.  Keep the map mode $30: $22 / $32 read as S-DD1 on bsnes / snes9x.
.include "snes.inc"
.segment "HEADER"
  .byte "LP"                 ; $FFB0 maker code
  .byte "KSDR"               ; $FFB2 game code
  .res 7, 0                  ; $FFB6 reserved
  .byte 0                    ; $FFBD expansion RAM
  .byte 0                    ; $FFBE special version
  .byte 0                    ; $FFBF cartridge sub-type
  ;      123456789012345678901
  .byte "KISHIDORA SFC        "  ; $FFC0 title (21 bytes)
  .byte $30                  ; $FFD5 LoROM + FastROM
  .byte $02                  ; $FFD6 ROM + RAM + battery
  .byte $0c                  ; $FFD7 ROM size 4MB ($0D for ROM_MB=6 / 8: build.py)
  .byte $05                  ; $FFD8 SRAM 32KB
  .byte $00                  ; $FFD9 Japan
  .byte $33                  ; $FFDA extended header
  .byte $00                  ; $FFDB version
  .word $ffff, $0000         ; $FFDC checksum (fixed by build.py)
