"""Text and font for the SFC build.

All game text is collected here, the characters it uses become a 12-dot font
(東雲フォント 12 ドット = public domain; half-width glyphs from 東雲 6x12), and every
string is stored as 16-bit glyph codes:

    0 = end, 1 = new line, 2 = hero name, 3 = number argument, 4 = text argument,
    5..15 = colour changes (5 = normal, 6 = yellow, 7 = red, 8 = gray, 9 = blue, 10 = green)
    16.. = glyphs

The font binary (data/gen/font.bin) is committed, so building the ROM does not need
the font files; they are only read when the font has to grow.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'font'))

FONT_DIRS = ['/usr/share/fonts/X11/misc', os.path.join(HERE, '..', 'font')]
FULL = 'shnmk12.pcf.gz'
HALF = 'shnm6x12a.pcf.gz'
GLYPH0 = 16
CTRL = {'\n': 1, '{name}': 2, '{n}': 3, '{s}': 4, '{m}': 11, '{c0}': 5, '{yellow}': 6, '{red}': 7, '{gray}': 8, '{blue}': 9, '{green}': 10, '{n3}': 12}

# characters the source text uses that the 12-dot fonts lack
SUBST = {'～': '〜', '〜': '〜', '－': 'ー', '−': '－', '♡': '♥', '–': '-', '—': 'ー', '‥': '…', '･': '・', '　': ' ', '❤': '♥', '♪': '♪',
         '①': '1', '②': '2', '③': '3', '④': '4', 'Ⅱ': 'II', '!': '！', '?': '？'}


# hand-made glyphs for symbols missing from JIS X 0208 (rows of '#', 12 high)
CUSTOM = {
    '▶': (8, ['', '', '#', '##', '###', '####', '###', '##', '#', '', '', '']),
    '▷': (8, ['', '', '#', '##', '#.#', '#..#', '#.#', '##', '#', '', '', '']),
    '♥': (11, ['', '', '.##...##', '####.####', '#########', '#########', '.#######', '..#####', '...###', '....#', '', '']),
}


class Glyphs:
    def __init__(self):
        self.full = None
        self.half = None

    def _load(self):
        if self.full is not None:
            return
        import pcf
        for d in FONT_DIRS:
            if os.path.exists(os.path.join(d, FULL)):
                self.full = pcf.read_pcf(os.path.join(d, FULL))[0]
                self.half = pcf.read_pcf(os.path.join(d, HALF))[0]
                return
        raise SystemExit('東雲フォント (shnmk12.pcf.gz) が見つからない。xfonts-shinonome を入れるか tools/font に置いて。')

    def glyph(self, ch):
        """(width, 12 rows of 16-bit MSB-left bits)"""
        self._load()
        if ch == ' ':
            return 4, [0] * 12
        if ch in CUSTOM:
            w, rows = CUSTOM[ch]
            return w, [sum(0x8000 >> i for i, c in enumerate(r) if c == '#') for r in rows]
        if ch in self.half and ord(ch) < 0x100:
            w, h, cell = self.half[ch]
            return w, [(r << (16 - w)) & 0xffff for r in cell[:12]]
        if ch in self.full:
            w, h, cell = self.full[ch]
            return w, [(r << (16 - w)) & 0xffff for r in cell[:12]]
        if ch in self.half:
            w, h, cell = self.half[ch]
            return w, [(r << (16 - w)) & 0xffff for r in cell[:12]]
        return None


class TextDB:
    def __init__(self, font_path):
        self.font_path = font_path
        self.chars = []          # glyph order (code = GLYPH0 + index)
        self.index = {}
        self.glyphs = Glyphs()
        self.strings = []        # (label, codes)
        self.labels = {}
        self._load_existing()

    def _load_existing(self):
        """Keep glyph codes stable: start from the committed font table."""
        p = self.font_path + '.chars'
        if os.path.exists(p):
            for ch in open(p, encoding='utf-8').read().split('\n'):
                if ch:
                    ch = ch.encode('utf-8').decode('unicode_escape') if ch.startswith('\\') else ch
                    self.index[ch] = len(self.chars)
                    self.chars.append(ch)

    def code(self, ch):
        ch = SUBST.get(ch, ch)
        if len(ch) > 1:
            return [self.code(c)[0] for c in ch]
        if ch not in self.index:
            if self.glyphs.glyph(ch) is None:
                import unicodedata
                alt = unicodedata.normalize('NFKC', ch)
                if alt != ch and len(alt) == 1 and self.glyphs.glyph(alt) is not None:
                    return self.code(alt)
                print('warning: フォントにない文字 %r (U+%04X) -> ？' % (ch, ord(ch)))
                return self.code('？')
            self.index[ch] = len(self.chars)
            self.chars.append(ch)
        return [GLYPH0 + self.index[ch]]

    def encode(self, s):
        out = []
        i = 0
        while i < len(s):
            m = re.match(r'\{(\w+)\}', s[i:])
            if m and m.group(0) in CTRL:
                out.append(CTRL[m.group(0)])
                i += len(m.group(0))
                continue
            ch = s[i]
            if ch == '\n':
                out.append(1)
            else:
                out += self.code(ch)
            i += 1
        return out

    def add(self, label, s):
        if label in self.labels:
            return label
        codes = self.encode(s)
        self.labels[label] = len(self.strings)
        self.strings.append((label, codes, s))
        return label

    def width(self, s):
        w = 0
        for c in self.encode(s):
            if c >= GLYPH0:
                w += self.glyphs.glyph(self.chars[c - GLYPH0])[0]
        return w

    def write_font(self):
        """font.bin: per glyph 24 bytes (12 rows, big-endian 16-bit) ; font.wid: widths."""
        rows = bytearray()
        wid = bytearray()
        for ch in self.chars:
            g = self.glyphs.glyph(ch)
            w, r = g
            for v in r:
                rows += struct.pack('>H', v)
            wid.append(w)
        os.makedirs(os.path.dirname(self.font_path), exist_ok=True)
        open(self.font_path, 'wb').write(rows)
        open(self.font_path + '.wid', 'wb').write(wid)
        with open(self.font_path + '.chars', 'w', encoding='utf-8') as f:
            for ch in self.chars:
                f.write((ch.encode('unicode_escape').decode('ascii') if ch in '\\\n' or not ch.isprintable() else ch) + '\n')
        return len(self.chars)
