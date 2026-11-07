; SNES hardware registers and helper macros (ca65, 65816)
.p816
.smart -

INIDISP   = $2100
OBSEL     = $2101
OAMADDL   = $2102
OAMADDH   = $2103
OAMDATA   = $2104
BGMODE    = $2105
MOSAIC    = $2106
BG1SC     = $2107
BG2SC     = $2108
BG3SC     = $2109
BG4SC     = $210a
BG12NBA   = $210b
BG34NBA   = $210c
BG1HOFS   = $210d
BG1VOFS   = $210e
BG2HOFS   = $210f
BG2VOFS   = $2110
BG3HOFS   = $2111
BG3VOFS   = $2112
BG4HOFS   = $2113
BG4VOFS   = $2114
VMAIN     = $2115
VMADDL    = $2116
VMADDH    = $2117
VMDATAL   = $2118
VMDATAH   = $2119
M7SEL     = $211a
CGADD     = $2121
CGDATA    = $2122
W12SEL    = $2123
W34SEL    = $2124
WOBJSEL   = $2125
WH0       = $2126
WH1       = $2127
WH2       = $2128
WH3       = $2129
WBGLOG    = $212a
WOBJLOG   = $212b
TM        = $212c
TS        = $212d
TMW       = $212e
TSW       = $212f
CGWSEL    = $2130
CGADSUB   = $2131
COLDATA   = $2132
SETINI    = $2133
MPYL      = $2134
MPYM      = $2135
MPYH      = $2136
RDNMI     = $4210
APUIO0    = $2140
APUIO1    = $2141
APUIO2    = $2142
APUIO3    = $2143
WMDATA    = $2180
WMADDL    = $2181
WMADDM    = $2182
WMADDH    = $2183
NMITIMEN  = $4200
WRIO      = $4201
WRMPYA    = $4202
WRMPYB    = $4203
WRDIVL    = $4204
WRDIVH    = $4205
WRDIVB    = $4206
HTIMEL    = $4207
VTIMEL    = $4209
MDMAEN    = $420b
HDMAEN    = $420c
MEMSEL    = $420d
TIMEUP    = $4211
HVBJOY    = $4212
RDDIVL    = $4214
RDDIVH    = $4215
RDMPYL    = $4216
RDMPYH    = $4217
JOY1L     = $4218
JOY1H     = $4219

DMAP0     = $4300
BBAD0     = $4301
A1T0L     = $4302
A1B0      = $4304
DAS0L     = $4305
DASB0     = $4307

.macro A8
  sep #$20
  .a8
.endmacro
.macro A16
  rep #$20
  .a16
.endmacro
.macro I8
  sep #$10
  .i8
.endmacro
.macro I16
  rep #$10
  .i16
.endmacro
.macro AXY8
  sep #$30
  .a8
  .i8
.endmacro
.macro AXY16
  rep #$30
  .a16
  .i16
.endmacro

; Export an engine routine (ends with RTL) to C code: emits a stub
;   _name: jsl name_far / rts
; in the STUBS segment (copied to low WRAM, reachable from every C bank).
; The routine is entered with A/X/Y 8-bit (cc65 convention) and DBR=$7E.
.macro CFUNC name
  .export .ident(.concat("_", .string(name)))
  .pushseg
  .segment "STUBS"
  .ident(.concat("_", .string(name))):
    jsl .ident(.concat(.string(name), "_far"))
    rts
  .popseg
  .ident(.concat(.string(name), "_far")):
.endmacro
