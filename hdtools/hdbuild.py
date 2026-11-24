#!/usr/bin/env python3
"""Builds the HD texture packs (hd/ui.a2hd, hd/world.a2hd + data parts) from the game files in game/.

    python hdtools/hdbuild.py catalog            list every picture of the game (names, sizes, ui/world)
    python hdtools/hdbuild.py prep               pictures for the upscaler -> work/in
    python hdtools/hdbuild.py ai                 run Real-ESRGAN on work/in -> work/out
    python hdtools/hdbuild.py pack [--scale 4]   palette-index HD maps -> hd/*.a2hd
    python hdtools/hdbuild.py all

Replacing a picture by hand: put <name>.png (W*K x H*K, alpha = transparency) into hdtools/override/ and run `pack` again."""
import os, sys, struct, pickle, hashlib, re, subprocess, time
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import a2img, hdupscale as U, hdc

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.environ.get('A2HDWORK') or os.path.join(ROOT, 'hdwork')
OUT = os.path.join(ROOT, 'hd')
OVR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'override')
PART = 95 * 1024 * 1024
MODEL = os.environ.get('A2HDMODEL', 'realesrgan-x4plus')
LAM = dict(lam_mix=300, lam_far=200, wlev=5)

UI = [r'^font', r'^slus_', r'^game0\d_', r'^pack0_00(0\d|1[0-3])_', r'^pack0_002[3-9]', r'^pack0_0080_', r'^pack0_0248_', r'^pack0_0258_',
      r'^pack0_0298_', r'^pack0_0302_', r'^pack0_0333_', r'^pack0_034[078]_', r'^pack0_036[12]_', r'^pack0_037[57]_', r'^pack0_040[347]_',
      r'^pack0_0428_', r'^pack0_0476_', r'^pack0_051[45]_']
def is_ui(name): return any(re.match(p, name) for p in UI)

def say(*a): print('[hd]', *a, flush=True)

def catalog():
    os.makedirs(WORK, exist_ok=True); seen = {}; cat = []; use = a2img.map_usage(); hint = a2img.palette_hints(); say('map geometry: palettes known for %d pictures' % len(use))
    for name, g, bpp, W, H, idx, pals in a2img.assets():
        key = hashlib.sha1(bytes([bpp]) + struct.pack('<HH', W, H) + idx).digest()
        if key in seen:
            e = cat[seen[key]]; e['alias'].append(name)
            if not e['usage'] and name in use: e['usage'] = (pals, use[name])
            if e['hint'] is None and name in hint: e['hint'] = (pals, hint[name])
            continue
        seen[key] = len(cat); cat.append(dict(name=name, bpp=bpp, W=W, H=H, idx=idx, pals=pals, alias=[], ui=is_ui(name), usage=(pals, use[name]) if name in use else None, hint=(pals, hint[name]) if name in hint else None))
    pickle.dump(cat, open(os.path.join(WORK, 'catalog.pkl'), 'wb'))
    say('catalog: %d pictures (%d interface, %d world), %d texels' % (len(cat), sum(c['ui'] for c in cat), sum(not c['ui'] for c in cat), sum(c['W'] * c['H'] for c in cat)))
    with open(os.path.join(WORK, 'catalog.txt'), 'w') as f:
        for c in cat: f.write('%s %s %dbpp %dx%d %s\n' % (c['name'], 'ui' if c['ui'] else 'world', c['bpp'], c['W'], c['H'], ' '.join(c['alias'])))
    return cat

def load_cat(): return pickle.load(open(os.path.join(WORK, 'catalog.pkl'), 'rb'))

_REC = None
def recorded():
    """palettes seen while playing with A2HDUSAGE (python a2pc.py play --hdrecord): name -> [(palette, cells)]"""
    global _REC
    if _REC is None:
        _REC = {}; f = os.path.join(WORK, 'usage.txt')
        if os.path.exists(f):
            for line in open(f):
                p = line.split()
                if len(p) != 4: continue
                n = int(p[1]); pal = np.frombuffer(bytes.fromhex(p[2]), '>u2').astype(np.int32)[:n]
                cells = np.unpackbits(np.frombuffer(bytes.fromhex(p[3]), np.uint8), bitorder='little').reshape(32, 32)
                _REC.setdefault(p[0], []).append((pal, cells))
    return _REC

def layers_of(c):
    L = []; H, W = c['H'], c['W']
    if c.get('usage'):
        pals, votes = c['usage']
        for (pal, sub), m in votes.items():
            if pal >= len(pals): continue
            rgb, raw = U.pal_rgb(pals[pal]); base = 0 if c['bpp'] == 8 else sub * 16; n = 256 if c['bpp'] == 8 else 16
            L.append((rgb[base:base + n], raw[base:base + n], m[:H, :W].astype(np.float32)))
    for nm in [c['name']] + c['alias']:
        for pal, cells in recorded().get(nm, []):
            m = np.kron(cells, np.ones((8, 8), np.uint8))[:H, :W].astype(np.float32) * 4
            rgb = np.stack([(pal & 31) << 3, ((pal >> 5) & 31) << 3, ((pal >> 10) & 31) << 3], 1).astype(np.float32)
            L.append((rgb, pal, m))
    return L

def prep_one(c):
    ix = U.unpack(c['bpp'], c['W'], c['H'], c['idx']); m = U.usage_maps(ix, layers_of(c))
    if m is None:
        m = U.heuristic_maps(ix, U.candidate_palettes(c['bpp'], c['pals']))
    col, raw = m; opq = raw != 0
    alpha = 0.002 < (~opq).mean() < 0.998
    rgb = U.bleed(col, opq) if alpha else col
    return ix, col, opq, rgb, alpha

def save_input(path, a):
    """writes a network input; a changed input drops the old network output"""
    if os.path.exists(path) and np.array_equal(np.asarray(Image.open(path).convert('RGB')), a): return 0
    Image.fromarray(a).save(path); o = os.path.join(WORK, 'out', os.path.basename(path))
    if os.path.exists(o): os.remove(o)
    return 1

def prep_job(c):
    d = os.path.join(WORK, 'in'); ix, col, opq, rgb, alpha = prep_one(c)
    n = save_input(os.path.join(d, c['name'] + '.png'), np.clip(U.prepare(ix, rgb), 0, 255).astype(np.uint8))
    if alpha:
        m = np.repeat((opq * 255).astype(np.uint8)[..., None], 3, 2)
        n += save_input(os.path.join(d, c['name'] + '__m.png'), np.pad(m, ((U.PAD, U.PAD), (U.PAD, U.PAD), (0, 0)), mode='wrap'))
    return n

def prep():
    from concurrent.futures import ProcessPoolExecutor
    cat = load_cat(); os.makedirs(os.path.join(WORK, 'in'), exist_ok=True); changed = 0
    with ProcessPoolExecutor(max(1, (os.cpu_count() or 2) - 1)) as ex:
        for i, n in enumerate(ex.map(prep_job, cat, chunksize=8)):
            changed += n
            if i % 200 == 199: say('prep %d/%d' % (i + 1, len(cat)))
    say('prep done, %d network inputs changed' % changed)

def ai():
    d_in = os.path.join(WORK, 'in'); d_out = os.path.join(WORK, 'out'); os.makedirs(d_out, exist_ok=True)
    todo = [f for f in os.listdir(d_in) if not os.path.exists(os.path.join(d_out, f))]
    say('upscaling %d pictures with %s' % (len(todo), MODEL))
    if not todo: return
    tmp = os.path.join(WORK, 'in_todo'); os.makedirs(tmp, exist_ok=True)
    for f in os.listdir(tmp): os.remove(os.path.join(tmp, f))
    for f in todo: os.link(os.path.join(d_in, f), os.path.join(tmp, f))
    subprocess.run([U.ESRGAN, '-i', tmp, '-o', d_out, '-n', MODEL, '-s', '4', '-f', 'png', '-j', '2:2:2'], check=True)
    say('upscaling done')

PIXEL_ART = {n: [(0, 240, 96, 256)] for n in ('pack0_0032_0', 'pack0_0033_0', 'pack0_0034_0', 'pack0_0035_0')}

def epx(a):
    """Scale2x on palette indices"""
    P = np.pad(a, 1, mode='edge'); B = P[:-2, 1:-1]; D = P[1:-1, :-2]; F = P[1:-1, 2:]; H = P[2:, 1:-1]; E = a
    o = np.zeros((a.shape[0] * 2, a.shape[1] * 2), a.dtype)
    o[0::2, 0::2] = np.where((B == D) & (B != F) & (D != H), D, E); o[0::2, 1::2] = np.where((B == F) & (B != D) & (F != H), F, E)
    o[1::2, 0::2] = np.where((D == H) & (D != B) & (H != F), D, E); o[1::2, 1::2] = np.where((H == F) & (D != H) & (B != F), F, E)
    return o

def pixel_art(c, ix, K, i0, i1, wq):
    for x0, y0, x1, y1 in PIXEL_ART.get(c['name'], []):
        r = ix[y0:y1, x0:x1]; k = 1
        while k < K: r = epx(r); k *= 2
        if k != K: r = np.kron(r[::k // K or 1, ::k // K or 1], np.ones((1, 1), r.dtype))
        i0[y0 * K:y1 * K, x0 * K:x1 * K] = r; i1[y0 * K:y1 * K, x0 * K:x1 * K] = r; wq[y0 * K:y1 * K, x0 * K:x1 * K] = 0
    return i0, i1, wq

def hd_of(c, K, meta):
    """(i0, i1, w) maps of one picture at scale K"""
    ix, col, opq, rgb, alpha = prep_one(c)
    ovr = os.path.join(OVR, c['name'] + '.png'); A = None
    if os.path.exists(ovr):
        im = Image.open(ovr).convert('RGBA').resize((c['W'] * K, c['H'] * K), Image.LANCZOS); a = np.asarray(im).astype(np.float32)
        hd = a[..., :3]; A = a[..., 3] / 255.0
    elif c['name'].startswith('font'):
        size = (c['W'] * K, c['H'] * K)
        hd = np.asarray(Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).resize(size, Image.BICUBIC)).astype(np.float32)
        A = np.clip(np.asarray(Image.fromarray((opq * 255).astype(np.uint8)).resize(size, Image.BICUBIC)).astype(np.float32) / 255.0, 0, 1)
    else:
        P = U.PAD * 4; src = os.path.join(WORK, 'out', c['name'] + '.png')
        im = Image.open(src).convert('RGB')
        im = im.crop((P, P, P + c['W'] * 4, P + c['H'] * 4))
        if K != 4: im = im.resize((c['W'] * K, c['H'] * K), Image.LANCZOS)
        hd = np.asarray(im).astype(np.float32)
        if alpha and os.path.exists(os.path.join(WORK, 'out', c['name'] + '__m.png')):
            m = Image.open(os.path.join(WORK, 'out', c['name'] + '__m.png')).convert('L').crop((P, P, P + c['W'] * 4, P + c['H'] * 4))
            if K != 4: m = m.resize((c['W'] * K, c['H'] * K), Image.LANCZOS)
            A = np.asarray(m).astype(np.float32) / 255.0
    i0, i1, wq = U.project(hd, ix, col, opq, K, alpha=A, **LAM)
    return pixel_art(c, ix, K, i0, i1, wq)

def write_pack(kind, items, K):
    """index file <kind>.a2hd + data parts <kind>.a2hd.000 ..."""
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.startswith(kind + '.a2hd'): os.remove(os.path.join(OUT, f))
    parts = []; cur = bytearray(); ents = []; rows = []; segs = []
    for n, (c, blob) in enumerate(items):
        if len(cur) + len(blob) > PART: parts.append(cur); cur = bytearray()
        R = c['W'] * c['bpp'] // 8; flags = 0
        if c['name'].startswith('font'):
            cols = 1; ink = 0
            for r in range(c['H']):
                for k in range(R // 2):
                    if c['idx'][r * R + 2 * k] or c['idx'][r * R + 2 * k + 1]: cols = max(cols, k + 1)
            for b in c['idx']: ink += (b & 15 != 0) + (b >> 4 != 0)
            flags = 0x100 | (cols if ink >= 10 else 0)
            rows += [hdc.cellrow(c['idx'][r * R:r * R + 2 * cols]) for r in range(c['H'])]
            if blob: blob = c['idx'] + blob
        else:
            rows += [hdc.rowhash(c['idx'][r * R:(r + 1) * R]) for r in range(c['H'])]
            if c['ui'] and c['bpp'] == 4 and R >= 32:
                for r in range(c['H']):
                    for k in range(0, R, 16):
                        sg = c['idx'][r * R + k:r * R + k + 16]
                        if any(sg): segs.append((hdc.cellrow(sg), n, r, k // 2))
                flags = 0x200
                if blob: blob = c['idx'] + blob
        enc = hdc.crypt(blob, 0x100000000 + n)
        ents.append((c['name'].encode()[:39], c['bpp'], len(parts), c['W'], c['H'], flags, len(rows) - c['H'], len(cur), len(blob)))
        cur += enc
    parts.append(cur)
    hdr = struct.pack('<4sIIIII', b'A2HD', 2, K, len(ents), len(rows), len(parts))
    body = b''.join(struct.pack('<40sBBHHHIII', nm, b, p, W, H, fl, rs, off, sz) for nm, b, p, W, H, fl, rs, off, sz in ents)
    body += struct.pack('<%dQ' % len(rows), *rows)
    body += struct.pack('<I', len(segs)) + b''.join(struct.pack('<QIHH', h, e, r, o) for h, e, r, o in segs)
    open(os.path.join(OUT, kind + '.a2hd'), 'wb').write(hdr + hdc.crypt(body, 0x5EED))
    for i, p in enumerate(parts): open(os.path.join(OUT, '%s.a2hd.%03d' % (kind, i)), 'wb').write(bytes(p))
    total = sum(len(p) for p in parts)
    say('%s pack: %d pictures, %.1f MB in %d part(s)' % (kind, len(ents), total / 1048576, len(parts)))

def blob_job(args):
    c, K, cf = args
    i0, i1, wq = hd_of(c, K, None); blob = hdc.encode(c['idx'], c['bpp'], c['W'], c['H'], K, i0, i1, wq)
    open(cf, 'wb').write(blob); return cf

def pack(K=4):
    from concurrent.futures import ProcessPoolExecutor
    cat = load_cat(); out = {'ui': [], 'world': []}; t = time.time(); skipped = 0; todo = []; ready = []
    cache = os.path.join(WORK, 'blobs_%d' % K); os.makedirs(cache, exist_ok=True)
    for c in cat:
        cf = os.path.join(cache, c['name'] + '.bin'); ovr = os.path.join(OVR, c['name'] + '.png')
        if (not os.path.exists(ovr) and not os.path.exists(os.path.join(WORK, 'out', c['name'] + '.png'))) or            (not os.path.exists(ovr) and os.path.exists(os.path.join(WORK, 'in', c['name'] + '__m.png')) and not os.path.exists(os.path.join(WORK, 'out', c['name'] + '__m.png'))):
            skipped += 1; ready.append((c, None)); continue
        srcs = [ovr] if os.path.exists(ovr) else [os.path.join(WORK, 'out', c['name'] + x) for x in ('.png', '__m.png')]
        newest = max(os.path.getmtime(f) for f in srcs if os.path.exists(f))
        if not (os.path.exists(cf) and os.path.getmtime(cf) >= newest): todo.append((c, K, cf))
        ready.append((c, cf))
    say('%d pictures to encode, %d already encoded' % (len(todo), len(ready) - len(todo)))
    with ProcessPoolExecutor(max(1, (os.cpu_count() or 2) - 1)) as ex:
        for n, _ in enumerate(ex.map(blob_job, todo, chunksize=4)):
            if n % 100 == 99: say('encoded %d/%d  %.0f s' % (n + 1, len(todo), time.time() - t))
    for c, cf in ready: out['ui' if c['ui'] else 'world'].append((c, open(cf, 'rb').read() if cf else b''))
    if skipped: say('%d pictures have no upscaled version yet (recognised, not replaced)' % skipped)
    for kind in ('ui', 'world'): write_pack(kind, out[kind], K)

if __name__ == '__main__':
    a = sys.argv[1:] or ['all']; K = int(a[a.index('--scale') + 1]) if '--scale' in a else 4
    if a[0] in ('catalog', 'all'): catalog()
    if a[0] in ('prep', 'all'): prep()
    if a[0] in ('ai', 'all'): ai()
    if a[0] in ('pack', 'all'): pack(K)
