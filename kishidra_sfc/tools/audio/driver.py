"""SPC700 resident sound driver for the SFC port.

ARAM:
  $0000-$00EF  driver variables        $0100-$01FF  stack
  $0200-       driver code + tables    $0800-$08FF  sample directory (DIR = $08)
  $0900-$BFFF  sound effect samples    $C000-$FFBC  BGM ring buffer (RING_BLOCKS x 9 bytes)

BGM is streamed: voice 0 plays the ring as one looping sample (the last ring block carries
END+LOOP).  The CPU sends whole BRR blocks, three bytes per handshake.  The play position is
estimated with timer 2 (one tick = one BRR block) and re-synchronised every time the voice
wraps (ENDX bit 0).  Port 2 reports how many blocks may be sent, divided by 8.

CPU -> SPC protocol (port 0 changes on every transaction, the SPC echoes it back):
  port 0 = sequence (bits 2-7, +4 per transaction) | type (bits 0-1)
  type 1: stream block, type 2: raw load block.  A block is 9 bytes in three transactions
      (ports 1-3 each time); the SPC takes the three in straight-line code and echoes each one
      as soon as it has read the ports, so the CPU prepares the next bytes while they are stored.
  type 0: command in port 1, parameters in ports 2-3 (echoed when done)
      1 STOP              key off the BGM voice, empty the ring
      2 START lo hi       key on the BGM voice with PITCH = hi:lo
      3 SE sample vol     play sample (DIR entry) on the next SE voice
      4 VOL bgm           BGM voice volume
      5 LOAD lo hi        following raw load blocks go to ARAM hi:lo
  The boot upload (IPL) carries only the driver and the directory; the sound effect samples
  follow as raw load blocks while the title is already on screen.
"""
from spc700 import Asm

DRIVER = 0x0200
DIR_PAGE = 0x08
SE_START = 0x0900
SE_END = 0xC000
RING = 0xC000
RING_BLOCKS = (0xFFC0 - RING) // 9          # 1813
TIMER_DIV = 93                               # 64 kHz / 93 = 688.2 Hz = one BRR block at 11008 Hz
ROOM = 8                                     # blocks kept free between writer and player

# zero page
WR, WR_BLK, FILL, TMP, RINGM8, RINGM1, RINGN = 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C
LAST, _UNUSED, PLAYING, WRAPPED, LASTBLK, SE_NEXT, BGM_VOL = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16
RAW = 0x18                                   # load pointer (command 5)
T1, T2, NEXT, TICKS = 0x1A, 0x1B, 0x1C, 0x1D
BURST = 96                                   # port 0 polls after a block before the housekeeping loop


def build(n_se):
    a = Asm(DRIVER)
    # ---------------------------------------------------------------- init
    a.mov_x_imm(0xEF)
    a.mov_sp_x()
    a.mov_a_imm(0)
    a.mov_x_imm(0)
    a.label('clr')
    a.mov_dpx_a(0x00)
    a.inc_x()
    a.cmp_x_imm(0xE0)
    a.bne('clr')
    for reg, v in ((0x6C, 0x20), (0x0C, 0x60), (0x1C, 0x60), (0x2C, 0), (0x3C, 0), (0x0D, 0), (0x2D, 0), (0x3D, 0),
                   (0x4D, 0), (0x5D, DIR_PAGE), (0x6D, 0xFF), (0x7D, 0), (0x5C, 0xFF)):
        a.dsp_imm(reg, v)
    for v in range(8):   # all voices: GAIN mode, full level, silent until keyed on
        a.dsp_imm(v * 16 + 5, 0)
        a.dsp_imm(v * 16 + 7, 0x7F)
        a.dsp_imm(v * 16 + 0, 0x7F)
        a.dsp_imm(v * 16 + 1, 0x7F)
    a.dsp_imm(0x5C, 0x00)
    a.mov_dp_imm(BGM_VOL, 0x7F)
    a.mov_dp_imm(SE_NEXT, 1)
    for dp, v in ((RINGM8, RING_BLOCKS - ROOM), (RINGM1, RING_BLOCKS - 1), (RINGN, RING_BLOCKS)):
        a.mov_dp_imm(dp, v & 0xFF)
        a.mov_dp_imm(dp + 1, v >> 8)
    a.call('reset')
    a.mov_dp_imm(0xFC, TIMER_DIV)    # T2DIV
    a.mov_dp_imm(0xF1, 0x04)         # timer 2 on, IPL ROM off
    a.mov_a_dp(0xFF)                 # clear T2OUT
    a.mov_a_dp(0xF4)
    a.mov_dp_a(LAST)
    a.mov_dp_imm(0xF7, 0xA5)         # ready
    # ---------------------------------------------------------------- main loop
    a.label('loop')
    a.mov_a_dp(0xFF)                 # timer ticks since last read (+ those counted during bursts)
    a.clrc()
    a.adc_a_dp(TICKS)
    a.mov_dp_imm(TICKS, 0)           # (mov dp,#imm keeps the flags)
    a.beq('no_tick')
    a.mov_dp_a(TMP)
    a.mov_dp_imm(TMP + 1, 0)
    a.mov_a_dp(PLAYING)
    a.beq('no_tick')
    a.movw_ya_dp(FILL)
    a.subw_ya(TMP)
    a.bcs('tick_ok')
    a.mov_a_imm(0)
    a.mov_y_a()
    a.label('tick_ok')
    a.movw_dp_ya(FILL)
    a.label('no_tick')
    # ENDX: the BGM voice wrapped to the ring start
    a.mov_dp_imm(0xF2, 0x7C)
    a.mov_a_dp(0xF3)
    a.and_a_imm(1)
    a.beq('no_wrap')
    a.mov_dp_imm(0xF3, 0)
    a.mov_a_dp(WRAPPED)
    a.beq('under')
    a.movw_ya_dp(WR_BLK)
    a.movw_dp_ya(FILL)
    a.mov_dp_imm(WRAPPED, 0)
    a.bra('no_wrap')
    a.label('under')
    a.mov_a_imm(0)
    a.mov_y_a()
    a.movw_dp_ya(FILL)
    a.label('no_wrap')
    # free blocks / 8 -> port 2 (0..255; the CPU sends more while the ring is low)
    a.movw_ya_dp(RINGM8)
    a.subw_ya(FILL)
    a.bcs('free_ok')
    a.mov_a_imm(0)
    a.mov_y_a()
    a.label('free_ok')
    a.movw_dp_ya(TMP)
    a.lsr_dp(TMP + 1)
    a.ror_dp(TMP)
    a.lsr_dp(TMP + 1)
    a.ror_dp(TMP)
    a.lsr_dp(TMP + 1)
    a.ror_dp(TMP)
    a.mov_a_dp(TMP)
    a.mov_y_dp(TMP + 1)
    a.beq('free_lt')
    a.mov_a_imm(0xFF)
    a.label('free_lt')
    a.mov_dp_a(0xF6)
    # port 0
    a.mov_a_dp(0xF4)
    a.cbne(LAST, 'got')
    a.jmp('loop')
    a.label('got')                   # A = new port 0 value
    a.mov_dp_a(LAST)
    a.bbs(LAST, 1, 'to_raw')         # type 2: raw load
    a.bbs(LAST, 0, 'g_stream')       # type 1: stream block
    a.call('command')
    a.mov_a_dp(LAST)
    a.mov_dp_a(0xF4)
    a.jmp('loop')
    # after a block the next one usually follows within microseconds: poll port 0 tightly
    # for a while before the housekeeping loop (the timer is read so it cannot overflow)
    a.label('burst')
    a.mov_a_dp(0xFF)
    a.clrc()
    a.adc_a_dp(TICKS)
    a.mov_dp_a(TICKS)
    a.mov_x_imm(BURST)
    a.label('burst_l')
    a.mov_a_dp(0xF4)
    a.cbne(LAST, 'got')
    a.dec_x()
    a.bne('burst_l')
    a.jmp('loop')

    # ---------------------------------------------------------------- one 9-byte block
    # Three transactions in straight-line code.  Each one: read ports 1-3, echo port 0 at once
    # (the CPU then prepares the next three bytes while these are stored), store, wait for
    # the next value (port 0 + 4).  Entered with A = port 0 of the first transaction.
    def block(p, ptr, hdr):
        for t in range(3):
            if t:
                a.label('%s_w%d' % (p, t))
                a.cbne(0xF4, '%s_w%d' % (p, t))
            a.mov_x_dp(0xF5)
            a.mov_dp_dp(T1, 0xF6)
            a.mov_dp_dp(T2, 0xF7)
            a.mov_dp_a(0xF4)
            if t < 2:
                a.clrc()
                a.adc_a_imm(4)
                a.mov_dp_a(NEXT)
            else:
                a.mov_dp_a(LAST)
            a.mov_a_x()
            if t == 0:
                if hdr:
                    a.or_a_dp(LASTBLK)   # END+LOOP only on the last ring block (the data has none)
                a.mov_y_imm(0)
            a.mov_ind_y_a(ptr)
            a.inc_y()
            a.mov_a_dp(T1)
            a.mov_ind_y_a(ptr)
            a.inc_y()
            a.mov_a_dp(T2)
            a.mov_ind_y_a(ptr)
            if t < 2:
                a.inc_y()
                a.mov_a_dp(NEXT)
        a.mov_a_dp(ptr)
        a.clrc()
        a.adc_a_imm(9)
        a.mov_dp_a(ptr)
        a.bcc(p + '_nc')
        a.inc_dp(ptr + 1)
        a.label(p + '_nc')

    a.label('to_raw')
    a.jmp('g_raw')
    a.label('g_stream')
    block('str', WR, True)
    a.incw(FILL)
    a.incw(WR_BLK)
    a.mov_dp_imm(LASTBLK, 0)
    a.movw_ya_dp(WR_BLK)
    a.cmpw_ya(RINGM1)
    a.bne('d_notlast')
    a.mov_dp_imm(LASTBLK, 3)
    a.jmp('burst')
    a.label('d_notlast')
    a.cmpw_ya(RINGN)
    a.bne('d_done')
    a.mov_dp_imm(WR_BLK, 0)
    a.mov_dp_imm(WR_BLK + 1, 0)
    a.mov_dp_imm(WR, RING & 0xFF)
    a.mov_dp_imm(WR + 1, RING >> 8)
    a.mov_dp_imm(WRAPPED, 1)
    a.label('d_done')
    a.jmp('burst')
    a.label('g_raw')
    block('raw', RAW, False)
    a.jmp('burst')
    # ---------------------------------------------------------------- commands
    a.label('command')
    a.mov_a_dp(0xF5)
    a.cmp_a_imm(1)
    a.bne('c_start')
    a.dsp_imm(0x5C, 0x01)            # KOFF voice 0
    a.label('reset')
    a.mov_dp_imm(PLAYING, 0)
    a.mov_dp_imm(WR, RING & 0xFF)
    a.mov_dp_imm(WR + 1, RING >> 8)
    a.mov_dp_imm(WR_BLK, 0)
    a.mov_dp_imm(WR_BLK + 1, 0)
    a.mov_dp_imm(FILL, 0)
    a.mov_dp_imm(FILL + 1, 0)
    a.mov_dp_imm(WRAPPED, 0)
    a.mov_dp_imm(LASTBLK, 0)
    a.ret()
    a.label('c_start')
    a.cmp_a_imm(2)
    a.bne('c_se')
    a.dsp_imm(0x5C, 0x00)            # KOFF off
    a.mov_dp_imm(0xF2, 0x02)
    a.mov_dp_dp(0xF3, 0xF6)          # pitch lo
    a.mov_dp_imm(0xF2, 0x03)
    a.mov_dp_dp(0xF3, 0xF7)          # pitch hi
    a.dsp_imm(0x04, 0)               # SRCN 0 = ring
    a.mov_dp_imm(0xF2, 0x00)
    a.mov_dp_dp(0xF3, BGM_VOL)
    a.mov_dp_imm(0xF2, 0x01)
    a.mov_dp_dp(0xF3, BGM_VOL)
    a.dsp_imm(0x7C, 0x00)            # clear ENDX
    a.mov_a_dp(0xFF)                 # restart the block clock
    a.mov_dp_imm(TICKS, 0)           # (and drop the ticks counted during the prefill's burst)
    a.dsp_imm(0x4C, 0x01)            # KON voice 0
    a.mov_dp_imm(PLAYING, 1)
    a.ret()
    a.label('c_se')
    a.cmp_a_imm(3)
    a.bne('c_vol')
    a.mov_a_dp(SE_NEXT)              # next voice 1..7
    a.inc_a()
    a.cmp_a_imm(8)
    a.bne('se_v')
    a.mov_a_imm(1)
    a.label('se_v')
    a.mov_dp_a(SE_NEXT)
    a.mov_y_a()
    a.mov_a_absy('voice_base')
    a.mov_x_a()
    a.mov_dp_x(0xF2)
    a.mov_dp_dp(0xF3, 0xF7)          # VOL L
    a.inc_x()
    a.mov_dp_x(0xF2)
    a.mov_dp_dp(0xF3, 0xF7)          # VOL R
    a.mov_y_dp(0xF6)
    a.inc_x()
    a.mov_dp_x(0xF2)
    a.mov_a_absy('pitch_lo')
    a.mov_dp_a(0xF3)
    a.inc_x()
    a.mov_dp_x(0xF2)
    a.mov_a_absy('pitch_hi')
    a.mov_dp_a(0xF3)
    a.inc_x()
    a.mov_dp_x(0xF2)
    a.mov_dp_dp(0xF3, 0xF6)          # SRCN
    a.mov_y_dp(SE_NEXT)
    a.mov_a_absy('voice_bit')
    a.dsp_a(0x4C)                    # KON
    a.ret()
    a.label('c_vol')
    a.cmp_a_imm(5)
    a.bne('c_vol4')
    a.mov_dp_dp(RAW, 0xF6)           # LOAD lo hi: where raw data goes
    a.mov_dp_dp(RAW + 1, 0xF7)
    a.ret()
    a.label('c_vol4')
    a.cmp_a_imm(4)
    a.bne('c_end')
    a.mov_dp_dp(BGM_VOL, 0xF6)
    a.mov_dp_imm(0xF2, 0x00)
    a.mov_dp_dp(0xF3, BGM_VOL)
    a.mov_dp_imm(0xF2, 0x01)
    a.mov_dp_dp(0xF3, BGM_VOL)
    a.label('c_end')
    a.ret()
    # ---------------------------------------------------------------- tables
    a.label('voice_base')
    for v in range(8):
        a.b(v * 16)
    a.label('voice_bit')
    for v in range(8):
        a.b(1 << v)
    a.label('pitch_lo')
    pitch_at = a.pc
    a.b(*([0] * (n_se + 1)))
    a.label('pitch_hi')
    a.b(*([0] * (n_se + 1)))
    code = a.build()
    assert DRIVER + len(code) <= DIR_PAGE * 256, 'driver too big'
    return code, pitch_at - DRIVER


def image(se_list):
    """se_list: [(brr bytes, pitch)] -> ARAM image from $0200 to the end of the samples"""
    code, pitch_off = build(len(se_list))
    img = bytearray(SE_START - DRIVER)
    img[:len(code)] = code
    n = len(se_list)
    # directory entry 0 = ring
    d = (DIR_PAGE << 8) - DRIVER
    img[d:d + 4] = bytes([RING & 0xFF, RING >> 8, RING & 0xFF, RING >> 8])
    at = SE_START
    data = bytearray()
    for i, (brr, pitch) in enumerate(se_list):
        e = d + 4 * (i + 1)
        img[e:e + 4] = bytes([at & 0xFF, at >> 8, at & 0xFF, at >> 8])
        img[pitch_off + 1 + i] = pitch & 0xFF
        img[pitch_off + 1 + n + 1 + i] = pitch >> 8
        data += brr
        at += len(brr)
    data += bytes(-len(data) % 9)              # raw load blocks are 9 bytes
    assert SE_START + len(data) <= SE_END, 'sound effects do not fit in ARAM (%d bytes over)' % (SE_START + len(data) - SE_END)
    return bytes(img + data), at
