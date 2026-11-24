"""Image assets of Alundra 2 (USA): decoding the packs in game/ into what the game sends to video memory."""
import os, struct, hashlib

GAMEDIR = os.environ.get('A2GAMEDIR') or os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'game')

_files = {}
def data(name):
    if name not in _files: _files[name] = open(os.path.join(GAMEDIR, name), 'rb').read()
    return _files[name]

def table(name):
    d = data(name); first = struct.unpack_from('<I', d, 0)[0]
    return struct.unpack_from('<%dI' % (first // 4), d, 0)

def records(name):
    d = data(name); t = table(name); nz = sorted(set(x for x in t if x)); nxt = {}
    for i, x in enumerate(nz): nxt[x] = nz[i + 1] if i + 1 < len(nz) else len(d)
    return [(i, d[x:nxt[x]] if x else b'') for i, x in enumerate(t)]

GRP_H = 260
def grp_slots(i):
    d = data('GRP.GP'); t = table('GRP.GP')
    if not t[i]: return None
    w = struct.unpack_from('<48I', d, t[i]); out = []
    for k in range(24):
        a, b = w[2 * k], w[2 * k + 1]
        out.append(d[t[i] + GRP_H + a:t[i] + GRP_H + a + b] if b else b'')
    return out

def lz(s, target):
    m = s[0]
    if m == 3: return bytes(s[4:4 + target])
    out = bytearray(); p = 4
    if s[2] == 0: out.append(s[4]); p = 5
    while len(out) < target:
        t = s[p]; p += 1
        n = (((t & 0x3f) << 8 | s[p]) + 1) if t & 0x40 else (t & 0x3f) + 1
        if t & 0x40: p += 1
        if t & 0x80:
            if m == 1: out += bytes([s[p]]) * n; p += 1
            else:
                off = s[p] + 1; p += 1
                for _ in range(n): out.append(out[-off])
        else: out += s[p:p + n]; p += n
    return bytes(out[:target])

def container(b):
    """52-byte header image container: returns (palettes, [streams]) or None"""
    if len(b) < 52: return None
    w = struct.unpack_from('<13I', b, 0); n, npal = w[0], w[1]
    if not (1 <= n <= 10) or npal > 16: return None
    W, H = w[2] & 0xffff, w[2] >> 16
    if not (8 <= W <= 1024 and 1 <= H <= 512): return None
    sizes = list(w[3:3 + n]); total = 52 + npal * 512 + sum(sizes)
    if not (total <= len(b) <= total + 3): return None
    pals = [b[52 + 512 * k:52 + 512 * (k + 1)] for k in range(npal)]
    p = 52 + npal * 512; streams = []
    for sz in sizes:
        s = b[p:p + sz]; p += sz
        if not sz: streams.append(None); continue
        if len(s) < 5 or s[0] not in (1, 2, 3): return None
        bpp = 8 if s[1] else 4; nbytes = W * H * bpp // 8
        try: px = lz(s, nbytes)
        except Exception: return None
        if len(px) != nbytes: return None
        streams.append((bpp, W, H, px))
    return pals, streams

def rowhash(row):
    return struct.unpack('<Q', hashlib.blake2b(row, digest_size=8).digest())[0]

def fnv64(b):
    h = 0xcbf29ce484222325
    for x in b: h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h

SCAN_FILES = ('SLUS_010.17', 'GAME00.BIN', 'GAME01.BIN', 'GAME02.BIN', 'GAME03.BIN', 'EVENT00.BIN',
              'PACK0.PAK', 'PACK1.PAK', 'PAKOMAKE.PAK', 'PAKVCAST.PAK', 'GRP.GP')

def scan(fn):
    """every image container inside a file, at any 4-byte offset: [(offset, size)]"""
    import numpy as np
    d = data(fn); n = len(d) // 4 * 4; w = np.frombuffer(d[:n], np.uint32); out = []
    if len(w) < 14: return out
    w0 = w[:-13]; w1 = w[1:-12]; w2 = w[2:-11]; W = w2 & 0xffff; H = w2 >> 16
    for i in np.nonzero((w0 >= 1) & (w0 <= 10) & (w1 <= 16) & (W >= 8) & (W <= 1024) & (H >= 1) & (H <= 512))[0]:
        off = int(i) * 4; hdr = w[i:i + 13]; nn = int(hdr[0]); tot = 52 + int(hdr[1]) * 512 + int(hdr[3:3 + nn].sum())
        if off + tot <= len(d) and tot <= 400000 and container(d[off:off + tot]): out.append((off, tot))
    return out

def owner(fn, off):
    """readable place of a file offset: record of a pack, slot of a GRP.GP group"""
    tag = fn.split('.')[0].lower()
    try: t = table(fn)
    except Exception: return '%s_%x' % (tag, off)
    if len(t) < 2 or t[0] != len(t) * 4 or fn in ('SLUS_010.17',) or fn.startswith(('GAME', 'EVENT')): return '%s_%x' % (tag, off)
    best = None
    for i, x in enumerate(t):
        if x and x <= off and (best is None or x > t[best]): best = i
    if best is None: return '%s_%x' % (tag, off)
    base = t[best]
    if fn == 'GRP.GP':
        w = struct.unpack_from('<48I', data(fn), base)
        for k in range(24):
            if w[2 * k + 1] and base + GRP_H + w[2 * k] == off: return 'grp%03d_s%d' % (best, k)
        return 'grp%03d_%x' % (best, off - base)
    rec = off - base
    return '%s_%04d' % (tag, best) if rec == 0 else '%s_%04d_%x' % (tag, best, rec)

def mod_tex(fn='PAKVCAST.PAK'):
    """'MOD' 256x256 8-bit pictures of PAKVCAST"""
    d = data(fn); p = 0
    while True:
        p = d.find(b'\x01MOD', p)
        if p < 0: return
        try:
            pal = d[p + 4:p + 516]; s = d[p + 516:]; i = 1; out = bytearray(256)
            while len(out) < 65536 + 256:
                f = s[i]; i += 1
                for k in range(8):
                    if len(out) >= 65536 + 256: break
                    if not (f >> (7 - k)) & 1: out.append(s[i]); i += 1
                    else:
                        a, c = s[i], s[i + 1]; i += 2; q = ((len(out) >> 8) - 1) * 256 + a
                        if q < 0 or q >= len(out): raise ValueError
                        for j in range(c + 3): out.append(out[q + j])
            yield p, pal, bytes(out[256:65536 + 256])
        except (ValueError, IndexError): pass
        p += 4

def assets():
    """yields (name, group, bpp, W, H, indices_bytes, palettes); bpp 4/8/16 as sent to video memory"""
    for fn in SCAN_FILES:
        group = 'map' if fn == 'GRP.GP' else fn.split('.')[0].lower()
        for off, tot in scan(fn):
            pals, streams = container(data(fn)[off:off + tot]); nm = owner(fn, off)
            for k, st in enumerate(streams):
                if st: yield ('%s_%d' % (nm, k), group, st[0], st[1], st[2], st[3], pals)
    for off, pal, px in mod_tex():
        yield ('pakvcast_mod_%x' % off, 'pakvcast', 8, 256, 256, px, [pal])
    yield from glyphs()

FONTS = [(r, 73, 24) for r in (537, 541, 545, 549, 552)] + [(r, 72, 24) for r in (538, 542, 546, 550)] + [(r, 49, 16) for r in (536, 540, 544, 548, 551)]
GRAY = struct.pack('<16H', 0, 0x9084, 0xC631, 0xFFFF, *([0] * 12))

def glyph_cell(g, rows):
    """a glyph as the game's blitter (0x8002A230) writes it: 2-bit pixels, MSB first, as 4-bit texels, 3 halfwords a row"""
    out = bytearray()
    for y in range(rows):
        for k in range(3):
            b = g[y * 3 + k]; hw = ((b & 3) << 12) | ((b << 6) & 0x300) | (b & 0x30) | (b >> 6)
            out += struct.pack('<H', hw)
    return bytes(out)

def glyphs():
    recs = dict(records('PACK0.PAK'))
    for r, stride, rows in FONTS:
        d = recs.get(r, b'')
        for i in range(len(d) // stride):
            g = d[i * stride:i * stride + rows * 3]
            if len(g) < rows * 3 or not any(g): continue
            yield ('font%03d_%03d' % (r, i), 'font', 4, 12, rows, glyph_cell(g, rows), [GRAY + bytes(480)])

def map_polys(i):
    """textured polygons of the map geometry (GRP.GP slot 11): (tpage, clut, us, vs)"""
    sl = grp_slots(i); d = sl[11] if sl else b''
    if len(d) < 560: return []
    w = struct.unpack('<140I', d[:560]); out = []
    for a in [x for x in w[1:45] if x]:
        if a + 84 > len(d): continue
        npoly = struct.unpack_from('<H', d, a + 40)[0]; p = a + 84
        for _ in range(npoly):
            if p + 44 > len(d): break
            ln = 80 if d[p] & 4 else 44; op = d[p + 11]
            if op & 4:
                n = 4 if op & 8 else 3
                us = [d[p + 24], d[p + 28], d[p + 32], d[p + 34]][:n]; vs = [d[p + 25], d[p + 29], d[p + 33], d[p + 35]][:n]
                out.append((struct.unpack_from('<H', d, p + 30)[0], struct.unpack_from('<H', d, p + 26)[0], us, vs))
            p += ln
    return out

def map_usage():
    """name of a slot-0 picture -> {(palette, sub-palette): coverage count per texel} from the map's own polygons"""
    from PIL import Image, ImageDraw
    import numpy as np
    res = {}; t = table('GRP.GP')
    for i in range(len(t)):
        sl = grp_slots(i)
        if not sl or not sl[0]: continue
        c = container(sl[0])
        if not c: continue
        act = [k for k, s in enumerate(c[1]) if s]; masks = {}
        for tp, clut, us, vs in map_polys(i):
            j = tp & 15
            if j >= len(act): continue
            k = act[j]; st = c[1][k]; key = (clut >> 4, 0 if st[0] == 8 else clut & 15)
            m = masks.setdefault((k, key), Image.new('L', (st[1], st[2]), 0)); dr = ImageDraw.Draw(m)
            pts = list(zip(us, vs)); dr.polygon(pts, fill=1, outline=1)
        for (k, key), m in masks.items():
            res.setdefault('grp%03d_s0_%d' % (i, k), {})[key] = np.asarray(m, np.uint16)
    return res

def palette_hints():
    """name -> palette number the map polygons would use: slot 0 by active stream order, slot 3 by stream"""
    res = {}; t = table('GRP.GP')
    for i in range(len(t)):
        sl = grp_slots(i)
        if not sl: continue
        for slot in (0,):
            if not sl[slot] or len(sl[slot]) < 52: continue
            w = struct.unpack_from('<13I', sl[slot], 0); n = w[0]
            if not 1 <= n <= 10: continue
            act = [k for k in range(n) if w[3 + k]]
            for j, k in enumerate(act): res['grp%03d_s%d_%d' % (i, slot, k)] = j if slot == 0 else k
    return res
