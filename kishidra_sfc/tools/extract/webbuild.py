"""Read the Safari (web) build of キシドラ！ that sits in the repository root.

The web build is a Godot 4 export: index.pck plus many small packs that are
concatenated into p/*.bin (listed in data/web_packs.json).  This module gives a
read-only view of every file in those packs, and decoders for the few Godot
resource formats the SFC build needs:

  * .gdc   compiled GDScript (tokenized, zstd)  -> constants of a script
  * .ctex  compressed texture (WebP / PNG inside) -> PIL image
  * .oggvorbisstr  Ogg packet sequence -> a playable .ogg file
  * .mp3str        mp3 stream -> .mp3 bytes
  * .sample        imported wav -> 16-bit PCM

Nothing here is written to the repository: the SFC tools call these functions
while they build the ROM data.
"""
import io
import os
import re
import struct
import json

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.environ.get('KISHIDORA_WEB') or os.path.normpath(os.path.join(HERE, '..', '..', '..'))   # the Safari web build (repository root)


def _read_pack(blob, base=0):
    magic, ver, ma, mi, pa, flags = struct.unpack_from('<4sIIIII', blob, base)
    if magic != b'GDPC':
        raise ValueError('not a Godot pack')
    fb, do = struct.unpack_from('<QQ', blob, base + 24)
    n, = struct.unpack_from('<I', blob, base + do)
    p = base + do + 4
    out = {}
    for _ in range(n):
        ln, = struct.unpack_from('<I', blob, p)
        p += 4
        name = blob[p:p + ln].rstrip(b'\0').decode('utf-8')
        p += ln
        off, size = struct.unpack_from('<QQ', blob, p)
        p += 16 + 16 + 4
        out[name] = (base + fb + off, size)
    return out


class WebBuild:
    def __init__(self, root=WEB):
        self.root = root
        self._blobs = {}
        self.files = {}
        idx = self._blob('index.pck')
        for name, (off, size) in _read_pack(idx).items():
            self.files[name] = ('index.pck', off, size)
        wp = json.loads(self.read('data/web_packs.json'))
        for key, (pname, size, binf, off) in wp['packs'].items():
            blob = self._blob('p/' + binf)
            for name, (o, s) in _read_pack(blob, off).items():
                self.files.setdefault(name, ('p/' + binf, o, s))

    def _blob(self, rel):
        if rel not in self._blobs:
            with open(os.path.join(self.root, rel), 'rb') as f:
                self._blobs[rel] = f.read()
        return self._blobs[rel]

    def has(self, path):
        return path in self.files

    def read(self, path):
        path = path.replace('res://', '')
        src, off, size = self.files[path]
        return self._blob(src)[off:off + size]

    def imported(self, res_path):
        """Path of the imported resource for a source asset (e.g. assets/bg/field_hd.png)."""
        res_path = res_path.replace('res://', '')
        imp = res_path + '.import'
        if not self.has(imp):
            return None
        t = self.read(imp).decode('utf-8', 'replace')
        m = re.search(r'path(?:\.\w+)?="res://([^"]+)"', t)
        return m.group(1) if m else None

    def import_params(self, res_path):
        t = self.read(res_path.replace('res://', '') + '.import').decode('utf-8', 'replace')
        out = {}
        for m in re.finditer(r'^(\w+)=(.*)$', t, re.M):
            out[m.group(1)] = m.group(2)
        return out

    def listdir(self, prefix):
        prefix = prefix.rstrip('/') + '/'
        return sorted(p for p in self.files if p.startswith(prefix))

    # ------------------------------------------------------------ textures
    def image(self, res_path):
        from PIL import Image
        ip = self.imported(res_path)
        if ip is None:
            raise KeyError(res_path)
        d = self.read(ip)
        i = d.find(b'RIFF')
        j = d.find(b'\x89PNG')
        if i >= 0 and (j < 0 or i < j):
            b = d[i:]
        elif j >= 0:
            b = d[j:]
        else:
            raise ValueError('unsupported texture ' + ip)
        im = Image.open(io.BytesIO(b))
        im.load()
        return im.convert('RGBA')

    # ------------------------------------------------------------ audio
    def ogg(self, res_path):
        """Rebuild an .ogg file from an AudioStreamOggVorbis resource."""
        d = self.read(self.imported(res_path))
        i = d.find(b'\x01vorbis')
        j = i - 24
        outer_n, = struct.unpack_from('<I', d, j + 4)
        p = j + 8
        pages = []
        for _ in range(outer_n):
            t, n = struct.unpack_from('<II', d, p)
            p += 8
            pk = []
            for _ in range(n):
                t, ln = struct.unpack_from('<II', d, p)
                p += 8
                pk.append(d[p:p + ln])
                p += (ln + 3) & ~3
            pages.append(pk)
        gran = None
        for off in range(p, min(p + 64, len(d) - 8), 4):
            t, n = struct.unpack_from('<II', d, off)
            if n == outer_n and 40 <= t < 60:
                gran = list(struct.unpack_from('<%dq' % n, d, off + 8))
                break
        return _ogg_mux(pages, gran)

    def mp3(self, res_path):
        d = self.read(self.imported(res_path))
        for sig in (b'ID3', b'\xff\xfb', b'\xff\xf3', b'\xff\xfa'):
            k = d.find(sig)
            if k >= 0:
                return d[k:]
        raise ValueError('no mp3 data')

    def sample(self, res_path):
        """(rate, channels, int16 samples list-bytes) of an imported wav (.sample)."""
        d = self.read(self.imported(res_path))
        # AudioStreamWAV: find the PackedByteArray "data" (variant type 0x1f) – the largest raw array.
        best = None
        p = 0
        while True:
            k = d.find(b'\x1f\x00\x00\x00', p)
            if k < 0:
                break
            if k + 8 <= len(d):
                ln, = struct.unpack_from('<I', d, k + 4)
                if 0 < ln <= len(d) - k - 8 and (best is None or ln > best[1]):
                    best = (k + 8, ln)
            p = k + 4
        if best is None:
            raise ValueError('no wav data')
        pcm = d[best[0]:best[0] + best[1]]
        t = d.decode('latin-1')
        rate = 44100
        stereo = False
        m = re.search(r'mix_rate', t)
        # Godot stores mix_rate/stereo/format as properties; fall back to the .import params.
        try:
            prm = self.import_params(res_path)
        except KeyError:
            prm = {}
        return pcm, prm


def _crc_table():
    t = []
    for i in range(256):
        r = i << 24
        for _ in range(8):
            r = ((r << 1) ^ 0x04c11db7) if r & 0x80000000 else (r << 1)
            r &= 0xffffffff
        t.append(r)
    return t


_CT = _crc_table()


def _crc(b):
    c = 0
    for x in b:
        c = ((c << 8) & 0xffffffff) ^ _CT[((c >> 24) & 0xff) ^ x]
    return c


def _ogg_mux(pages, gran):
    serial = 0x4b495348
    o = bytearray()
    for seq, (pk, g) in enumerate(zip(pages, gran)):
        segs = []
        body = b''
        for x in pk:
            ln = len(x)
            while ln >= 255:
                segs.append(255)
                ln -= 255
            segs.append(ln)
            body += x
        htype = (2 if seq == 0 else 0) | (4 if seq == len(pages) - 1 else 0)
        hdr = struct.pack('<4sBBqIIIB', b'OggS', 0, htype, g, serial, seq, 0, len(segs)) + bytes(segs)
        page = bytearray(hdr + body)
        struct.pack_into('<I', page, 22, _crc(page))
        o += page
    return bytes(o)


# ---------------------------------------------------------------- GDScript constants
TOK = ['EMPTY', 'ANNOTATION', 'IDENTIFIER', 'LITERAL', '<', '<=', '>', '>=', '==', '!=', 'and', 'or', 'not', '&&', '||', '!', '&', '|', '~',
       '^', '<<', '>>', '+', '-', '*', '**', '/', '%', '=', '+=', '-=', '*=', '**=', '/=', '%=', '<<=', '>>=', '&=', '|=', '^=',
       'if', 'elif', 'else', 'for', 'while', 'break', 'continue', 'pass', 'return', 'match', 'when', 'as', 'assert', 'await',
       'breakpoint', 'class', 'class_name', 'const', 'enum', 'extends', 'func', 'in', 'is', 'namespace', 'preload', 'self', 'signal',
       'static', 'super', 'trait', 'var', 'void', 'yield', '[', ']', '{', '}', '(', ')', ',', ';', '.', '..', '...', ':', '$', '->', '_',
       'NEWLINE', 'INDENT', 'DEDENT', 'PI', 'TAU', 'INF', 'NAN', 'VCS', '`', '?', 'ERROR', 'EOF']


def _dec_var(d, p):
    t, = struct.unpack_from('<I', d, p)
    p += 4
    ty = t & 0xffff
    f64 = t & (1 << 16)
    if ty == 0:
        return None, p
    if ty == 1:
        v, = struct.unpack_from('<I', d, p)
        return bool(v), p + 4
    if ty == 2:
        if f64:
            return struct.unpack_from('<q', d, p)[0], p + 8
        return struct.unpack_from('<i', d, p)[0], p + 4
    if ty == 3:
        if f64:
            return struct.unpack_from('<d', d, p)[0], p + 8
        return struct.unpack_from('<f', d, p)[0], p + 4
    if ty in (4, 21, 22):
        ln, = struct.unpack_from('<I', d, p)
        p += 4
        s = d[p:p + ln].decode('utf-8')
        return ('S', s), p + ((ln + 3) & ~3)
    sizes = {5: ('<ff', '<dd'), 6: ('<ii', '<ii'), 7: ('<ffff', '<dddd'), 8: ('<iiii', '<iiii'), 9: ('<fff', '<ddd'),
             10: ('<iii', '<iii'), 20: ('<ffff', '<ffff')}
    if ty in sizes:
        fmt = sizes[ty][1 if f64 else 0]
        return ('V', struct.unpack_from(fmt, d, p)), p + struct.calcsize(fmt)
    raise ValueError('variant type %d' % ty)


def gdscript_source(blob):
    """Approximate source text of a compiled GDScript (tokens only, no comments)."""
    import zstandard
    assert blob[:4] == b'GDSC'
    ver, dsz = struct.unpack_from('<II', blob, 4)
    d = blob[12:]
    if dsz:
        d = zstandard.ZstdDecompressor().decompress(d, max_output_size=dsz)
    ni, nc, nl, nt = struct.unpack_from('<4I', d, 0)
    p = 16
    ids = []
    for _ in range(ni):
        ln, = struct.unpack_from('<I', d, p)
        p += 4
        s = ''.join(chr(struct.unpack('<I', bytes(b ^ 0xb6 for b in d[p + 4 * k:p + 4 * k + 4]))[0]) for k in range(ln))
        p += 4 * ln
        ids.append(s)
    consts = []
    for _ in range(nc):
        v, p = _dec_var(d, p)
        consts.append(v)
    cols = {}
    p += nl * 8
    for _ in range(nl):
        k, v = struct.unpack_from('<II', d, p)
        p += 8
        cols[k] = v
    lines = {}
    indent = {}
    for i in range(nt):
        v, ln = struct.unpack_from('<II', d, p)
        p += 8
        v &= ~0x80
        ty, ix = v & 0x7f, v >> 8
        name = TOK[ty] if ty < len(TOK) else '?'
        if name in ('NEWLINE', 'INDENT', 'DEDENT', 'EOF'):
            continue
        if name in ('IDENTIFIER', 'ANNOTATION'):
            s = ids[ix]
            kind = 'id'
        elif name == 'LITERAL':
            s = _fmt_const(consts[ix])
            kind = 'lit'
        else:
            s = name
            kind = 'op'
        if ln not in lines and i in cols:
            indent[ln] = cols[i] - 1
        lines.setdefault(ln, []).append((kind, s))
    out = []
    for ln in range(1, (max(lines) if lines else 0) + 1):
        items = lines.get(ln)
        if not items:
            out.append('')
            continue
        t = '\t' * indent.get(ln, 0)
        prev = None
        for kind, s in items:
            if prev is not None:
                sp = not (s in (')', ']', '}', ',', ':', '.') or prev in ('(', '[', '.') or
                          (s in ('(', '[') and prev[-1:].isalnum()))
                if sp:
                    t += ' '
            t += s
            prev = s
        out.append(t)
    return '\n'.join(out)


def _fmt_const(v):
    if v is None:
        return 'null'
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, (int, float)):
        return repr(v)
    if v[0] == 'S':
        return json.dumps(v[1], ensure_ascii=False)
    return 'Vector2(%s)' % ', '.join(repr(x) for x in v[1])


def script_constants(wb, path):
    """Evaluate the `const NAME := literal` lines of a compiled script."""
    src = gdscript_source(wb.read(path))
    lines = src.split('\n')
    starts = [k for k, l in enumerate(lines) if re.match(r'^(const|var|func|static|class|enum|signal|extends|class_name)\b', l)]
    starts.append(len(lines))
    raw = {}
    for a, b in zip(starts, starts[1:]):
        m = re.match(r'^const (\w+)\s*(?::\s*[\w\[\]]*\s*)?=\s*(.*)$', lines[a])
        if m:
            raw[m.group(1)] = (m.group(2) + '\n' + '\n'.join(lines[a + 1:b])).strip()
    env = {}
    for _ in range(4):
        for k, v in raw.items():
            if k in env:
                continue
            s = v
            s = re.sub(r'\bColor\(([^)]*)\)', r'("#color",\1)', s)
            s = re.sub(r'\bVector2i?\(([^)]*)\)', r'(\1)', s)
            s = re.sub(r'\btrue\b', 'True', s)
            s = re.sub(r'\bfalse\b', 'False', s)
            s = re.sub(r'\bnull\b', 'None', s)
            s = re.sub(r'(?<!["\w.])\b[A-Z][A-Z0-9_]+\b(?!["\w])', lambda m: repr(env[m.group(0)]) if m.group(0) in env else m.group(0), s)
            try:
                env[k] = eval(s, {'__builtins__': {}}, {})
            except Exception:
                pass
    return env


def font_ttf(wb, out):
    """write the M+ Rounded TTF embedded in the Godot font resource"""
    import zstandard
    d = wb.read('.godot/imported/MPLUSRounded1c-ExtraBold.ttf-9d74343956a030d0b46de2c37d3be9b7.fontdata')
    mode, bs, total = struct.unpack_from('<III', d, 4)
    nb = (total + bs - 1) // bs
    sizes = struct.unpack_from('<%dI' % nb, d, 16)
    p = 16 + 4 * nb
    raw = bytearray()
    dz = zstandard.ZstdDecompressor()
    for sz in sizes:
        raw += dz.decompress(d[p:p + sz], max_output_size=bs)
        p += sz
    for m in re.finditer(rb'\x00\x01\x00\x00\x00[\x08-\x20]', raw):
        st = m.start()
        nt = struct.unpack_from('>H', raw, st + 4)[0]
        tables = [struct.unpack_from('>4sIII', raw, st + 12 + 16 * t) for t in range(nt)]
        if all(tg.isalnum() or tg in (b'OS/2', b'cvt ') for tg, _, _, _ in tables):
            end = max(o + l for _, _, o, l in tables)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, 'wb').write(bytes(raw[st:st + end]))
            return out
    raise ValueError('no TTF in font resource')


_wb = None


def get():
    global _wb
    if _wb is None:
        _wb = WebBuild()
    return _wb
