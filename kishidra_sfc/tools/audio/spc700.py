"""Tiny SPC700 assembler: just the instructions the sound driver uses.

    a = Asm(org=0x0200)
    a.label('loop'); a.mov_a_dp(0xf4); a.cbne(0x06, 'loop') ...
    image = a.build()     # bytes, labels resolved
"""


class Asm:
    def __init__(self, org):
        self.org = org
        self.code = bytearray()
        self.labels = {}
        self.fix = []          # (pos, label, kind) kind: 'rel' (1 byte, from pos+1), 'abs' (2 bytes)

    @property
    def pc(self):
        return self.org + len(self.code)

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = self.pc

    def b(self, *v):
        for x in v:
            self.code.append(x & 0xff)

    def w(self, v):
        self.b(v & 0xff, v >> 8)

    def _rel(self, target):
        self.fix.append((len(self.code), target, 'rel'))
        self.b(0)

    def _abs(self, target):
        if isinstance(target, int):
            self.w(target)
        else:
            self.fix.append((len(self.code), target, 'abs'))
            self.w(0)

    def build(self):
        for pos, t, kind in self.fix:
            if kind == 'rel':
                d = self.labels[t] - (self.org + pos + 1)
                assert -128 <= d <= 127, 'branch to %s out of range (%d)' % (t, d)
                self.code[pos] = d & 0xff
            else:
                v = self.labels[t]
                self.code[pos] = v & 0xff
                self.code[pos + 1] = v >> 8
        return bytes(self.code)

    # ---------------------------------------------------------------- moves
    def mov_a_imm(self, v): self.b(0xE8, v)
    def mov_x_imm(self, v): self.b(0xCD, v)
    def mov_y_imm(self, v): self.b(0x8D, v)
    def mov_a_dp(self, d): self.b(0xE4, d)
    def mov_dp_a(self, d): self.b(0xC4, d)
    def mov_x_dp(self, d): self.b(0xF8, d)
    def mov_dp_x(self, d): self.b(0xD8, d)
    def mov_y_dp(self, d): self.b(0xEB, d)
    def mov_dp_y(self, d): self.b(0xCB, d)
    def mov_dp_imm(self, d, v): self.b(0x8F, v, d)
    def mov_dp_dp(self, dst, src): self.b(0xFA, src, dst)
    def mov_a_x(self): self.b(0x7D)
    def mov_x_a(self): self.b(0x5D)
    def mov_a_y(self): self.b(0xDD)
    def mov_y_a(self): self.b(0xFD)
    def mov_a_abs(self, t): self.b(0xE5); self._abs(t)
    def mov_abs_a(self, t): self.b(0xC5); self._abs(t)
    def mov_a_absx(self, t): self.b(0xF5); self._abs(t)
    def mov_a_absy(self, t): self.b(0xF6); self._abs(t)
    def mov_absx_a(self, t): self.b(0xD5); self._abs(t)
    def mov_absy_a(self, t): self.b(0xD6); self._abs(t)
    def mov_a_dpx(self, d): self.b(0xF4, d)
    def mov_dpx_a(self, d): self.b(0xD4, d)
    def mov_a_ind_y(self, d): self.b(0xF7, d)       # mov a,[dp]+y
    def mov_ind_y_a(self, d): self.b(0xD7, d)       # mov [dp]+y,a
    def mov_sp_x(self): self.b(0xBD)
    def movw_ya_dp(self, d): self.b(0xBA, d)
    def movw_dp_ya(self, d): self.b(0xDA, d)
    def incw(self, d): self.b(0x3A, d)
    def decw(self, d): self.b(0x1A, d)
    def addw_ya(self, d): self.b(0x7A, d)
    def subw_ya(self, d): self.b(0x9A, d)
    def cmpw_ya(self, d): self.b(0x5A, d)

    # ---------------------------------------------------------------- alu
    def cmp_a_imm(self, v): self.b(0x68, v)
    def cmp_a_dp(self, d): self.b(0x64, d)
    def cmp_x_imm(self, v): self.b(0xC8, v)
    def cmp_y_imm(self, v): self.b(0xAD, v)
    def cmp_dp_imm(self, d, v): self.b(0x78, v, d)
    def adc_a_imm(self, v): self.b(0x88, v)
    def adc_a_dp(self, d): self.b(0x84, d)
    def sbc_a_imm(self, v): self.b(0xA8, v)
    def sbc_a_dp(self, d): self.b(0xA4, d)
    def and_a_imm(self, v): self.b(0x28, v)
    def or_a_imm(self, v): self.b(0x08, v)
    def eor_a_imm(self, v): self.b(0x48, v)
    def and_a_dp(self, d): self.b(0x24, d)
    def or_a_dp(self, d): self.b(0x04, d)
    def clrc(self): self.b(0x60)
    def setc(self): self.b(0x80)
    def inc_a(self): self.b(0xBC)
    def dec_a(self): self.b(0x9C)
    def inc_x(self): self.b(0x3D)
    def dec_x(self): self.b(0x1D)
    def inc_y(self): self.b(0xFC)
    def dec_y(self): self.b(0xDC)
    def inc_dp(self, d): self.b(0xAB, d)
    def dec_dp(self, d): self.b(0x8B, d)
    def asl_a(self): self.b(0x1C)
    def lsr_a(self): self.b(0x5C)
    def mul_ya(self): self.b(0xCF)
    def nop(self): self.b(0x00)

    # ---------------------------------------------------------------- flow
    def bra(self, t): self.b(0x2F); self._rel(t)
    def beq(self, t): self.b(0xF0); self._rel(t)
    def bne(self, t): self.b(0xD0); self._rel(t)
    def bcc(self, t): self.b(0x90); self._rel(t)
    def bcs(self, t): self.b(0xB0); self._rel(t)
    def bmi(self, t): self.b(0x30); self._rel(t)
    def bpl(self, t): self.b(0x10); self._rel(t)
    def cbne(self, d, t): self.b(0x2E, d); self._rel(t)
    def dbnz_y(self, t): self.b(0xFE); self._rel(t)
    def jmp(self, t): self.b(0x5F); self._abs(t)
    def call(self, t): self.b(0x3F); self._abs(t)
    def ret(self): self.b(0x6F)
    def push_a(self): self.b(0x2D)
    def pop_a(self): self.b(0xAE)
    def push_x(self): self.b(0x4D)
    def pop_x(self): self.b(0xCE)
    def push_y(self): self.b(0x6D)
    def pop_y(self): self.b(0xEE)

    # ---------------------------------------------------------------- DSP helpers
    def dsp_imm(self, reg, v):
        """$F2 = reg, $F3 = v"""
        self.mov_dp_imm(0xF2, reg)
        self.mov_dp_imm(0xF3, v)

    def dsp_a(self, reg):
        self.mov_dp_imm(0xF2, reg)
        self.mov_dp_a(0xF3)
