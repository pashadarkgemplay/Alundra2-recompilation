"""Builds a replacement hero from another character's model, keeping the original polygons and textures.

Every part of the foreign model is put on one bone of Flint's skeleton as it is (nothing is cut or recoloured); its texture pages and palettes come
from the user's own game files and are written into free video memory when the hero is loaded.
Result: hd/hero_<name>.bin (encrypted) = model record in the layout of record 68 + texture pages + palettes + table entries."""
import os, sys, math, struct, itertools
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.dirname(HERE))
import numpy as np
import modelparse as M, animtool as A, mapvram as MV, matres as MR, mapmodel as MM, build as B
from lib import rec

CHARACTERS = {'alexia': (6, 201), 'diaz': (340, 207), 'mephisto': (266, 208)}
MAX_BYTES = 31216
PAGE_DST = [(832, 0), (896, 0)]
PAL_DST = (832, 300)
TIDX0 = 1
FLINT_NEUTRAL = (28, 0)
# NPC part -> (Flint bone, fit mode). Flint: 0 head, 2 torso, 3 pelvis, 4/5 and 6/7 arms (upper/lower), 8/9/10 and 11/12/13 legs; arms 4,5 and legs 8,9,10 lie on -x.
# modes: f = fit to the bone's box, fb = same but bottoms aligned, g = keep the global scale, gb = global scale and feet on the ground. NPC side given for the model facing -z.
SPEC = {
 'alexia': dict(feet=[0, 2], parts=[(10, 0, 'f'), (11, 2, 'f'), (4, 3, 'f'), (7, 6, 'f'), (6, 7, 'f'), (9, 4, 'f'), (8, 5, 'f'), (3, 8, 'f'), (2, 10, 'fb'), (1, 11, 'f'), (0, 13, 'fb')]),
 'diaz': dict(feet=[0, 3], parts=[(7, 0, 'f'), (12, 2, 'f'), (13, 2, 'f'), (6, 3, 'f'), (9, 6, 'f'), (8, 7, 'f'), (11, 4, 'f'), (10, 5, 'f'), (5, 8, 'f'), (4, 9, 'f'), (3, 10, 'f'), (2, 11, 'f'), (1, 12, 'f'), (0, 13, 'f')]),
 'mephisto': dict(feet=[0, 1], parts=[(5, 0, 'f'), (6, 2, 'gb:1,1.1,1'), (2, 3, 'like:2'), (3, 3, 'like:2'), (4, 3, 'like:2'), (0, 10, 'fb'), (1, 13, 'fb')]),
}
SIDE = {4: 6, 5: 7, 6: 4, 7: 5, 8: 11, 9: 12, 10: 13, 11: 8, 12: 9, 13: 10}
FACE = {'mephisto': 0.0}

def isc(c): return (c >> 24) & 0xE0 == 0x20
def term(t): return (t & 0xFFFFFF00) == 0xFFFFFF00

def layout(seg):
    """word indices (from +0x18) of every element/decal: [(kind, base)]"""
    h = struct.unpack_from('<9h', seg, 0); nvert = h[8]; vo = len(seg) - nvert * 16
    ws = list(struct.unpack_from('<%dI' % ((vo - 0x18) // 4), seg, 0x18)); p = 0; out = []
    while p < len(ws):
        w = ws[p]
        if p + 11 <= len(ws) and isc(ws[p + 2]) and (ws[p + 9] >> 24) in (0xE1, 0xE2) and term(ws[p + 10]):
            out.append(('e', p)); p += 11
        elif isc(w) and (w >> 24) & 4 and p + 9 <= len(ws) and (ws[p + 7] >> 24) in (0xE1, 0xE2) and term(ws[p + 8]):
            out.append(('d', p)); p += 9
        else: raise ValueError('bad part')
    return ws, out, vo

def npc_rows(bank):
    nb, tab = A.parse_bank(bank); ids = [i for i, o in enumerate(tab) if o != 0xffffffff]
    return A.decode_anim(bank, tab[ids[0]], nb)

def row_of(a, k):
    b = a['bones'][k] if k < len(a['bones']) else None
    if b is None: return [0, 0, 0, 0, 0, 0, 4096, 4096, 4096], False
    return b[0][0], bool(b[1][0])

def assign(cost):
    """cost[i][j]: parts i -> bones j, every part gets its own bone, minimal total (DP over bone sets)"""
    n, m = len(cost), len(cost[0]); best = {0: (0.0, [])}
    for i in range(n):
        nxt = {}
        for mask, (c, path) in best.items():
            for j in range(m):
                if mask >> j & 1: continue
                nm = mask | 1 << j; nc = c + cost[i][j]
                if nm not in nxt or nc < nxt[nm][0]: nxt[nm] = (nc, path + [j])
        best = nxt
    return min(best.values(), key=lambda t: t[0])

def build(name, out_dir, log=print, yaw=None, override=None):
    mp, oid = CHARACTERS[name]
    flint = rec('PACK0.PAK', 68); fbank = rec('PACK0.PAK', 69)
    ftab = M.part_table(flint); fparts = M.parse_model(flint)
    Trest = B.hero_rest(fbank, 16)
    nbk, ftb = A.parse_bank(fbank); fa = A.decode_anim(fbank, ftb[FLINT_NEUTRAL[0]], nbk)
    fpiv = np.array([row_of(fa, k)[0][:3] for k in range(16)], float)
    HV = np.array([B.to_world(Trest[k], v[:3]) for k in range(14) for v in fparts[k]['verts']]); hero_h = HV[:, 1].max() - HV[:, 1].min()
    obj = [x for x in MM.map_objects(mp) if x[1]['id'] == oid and x[2] is not None and x[3] is not None]
    if not obj: raise SystemExit('model of %s not found' % name)
    _, f, nm, nbank = obj[0]; vr = MV.build(mp)
    parts = M.parse_model(nm); ptab = M.part_table(nm); na = npc_rows(nbank)
    info = []
    for k, p in enumerate(parts):
        row, hid = row_of(na, k)
        R = B.rotmat(*row[3:6]); S = np.array(row[6:9], float) / 4096.0; t = np.array(row[:3], float)
        V = np.array([v[:3] for v in p['verts']], float)
        info.append(dict(k=k, R=R, S=S, t=t, hid=hid, nv=len(V), p=p, V=V))
    allw = np.array([R_ @ (v * i['S']) + i['t'] for i in info if not i['hid'] and i['nv'] for R_ in [i['R']] for v in i['V']])
    npc_h = allw[:, 1].max() - allw[:, 1].min(); s = hero_h / npc_h
    log('%s: %d parts, npc height %.0f -> scale %.3f (hero %.0f)' % (name, len(parts), npc_h, s, hero_h))
    sp = SPEC[name]; byk = {i['k']: i for i in info}
    toe = []
    for k in sp['feet']:
        i = byk[k]; W = np.array([i['R'] @ (v * i['S']) + i['t'] for v in i['V']]); toe.append((W[:, 2].max() + W[:, 2].min()) / 2 - i['t'][2])
    yw = FACE.get(name) if name in FACE else (0.0 if np.mean(toe) <= 0 else math.pi)
    if yaw is not None: yw = yaw
    c, sn = math.cos(yw), math.sin(yw); log('  feet z offsets %s -> yaw %.0f' % (np.round(toe, 1), math.degrees(yw)))
    def yawv(w): return np.array([w[0] * c + w[2] * sn, w[1], -w[0] * sn + w[2] * c])
    use = []; path = []; modes = {}
    for k, bone, mode in sp['parts']:
        if yw != 0 and bone in SIDE: bone = SIDE[bone]
        use.append(byk[k]); path.append(bone); modes[k] = mode
    Fbox = {}
    for bone in set(path):
        vs = [v[:3] for v in fparts[bone]['verts']] + ([v[:3] for v in fparts[1]['verts']] if bone == 0 else [])
        vs = np.array(vs, float); Fbox[bone] = (vs.min(0), vs.max(0))
    ground = HV[:, 1].max(); xfm = {}
    first = {}
    for i, bone in zip(use, path): first.setdefault(bone, i)
    for bone, i in sorted(first.items(), key=lambda kv: modes[kv[1]['k']].startswith('like')):
        if modes[i['k']].startswith('like'):
            src = int(modes[i['k']].split(':')[1]); cN, sc, cF = xfm[src]; xfm[bone] = (cN, sc, cF + fpiv[src] - fpiv[bone]); continue
        W = np.array([yawv(i['R'] @ (v * i['S']) + i['t']) for v in i['V']]); n0, n1 = W.min(0), W.max(0); f0, f1 = Fbox[bone]; mode = modes[i['k']]
        mul = np.array([float(x) for x in mode.split(':')[1].split(',')]) if ':' in mode else np.ones(3); mode = mode.split(':')[0]
        if mode in ('g', 'gb'): sc = np.array([s, s, s]) * mul
        else: sc = np.clip((f1 - f0) / np.maximum(n1 - n0, 1.0), 0.7, 1.45)
        cN = (n0 + n1) / 2; cF = (f0 + f1) / 2
        if mode == 'fb': cF = cF.copy(); cF[1] = f1[1] - (n1[1] - n0[1]) * sc[1] / 2
        if mode == 'gb': cF = cF.copy(); cF[1] = (ground - fpiv[bone][1]) - (n1[1] - n0[1]) * sc[1] / 2
        xfm[bone] = (cN, sc, cF)
    log('  parts -> bones: %s' % {i['k']: b for i, b in zip(use, path)})
    # texture pages and palettes
    pages = {}; pals = {}
    def reg_tex(tpw, clw):
        rr = MR.real(f, tpw, clw)
        if rr is None: raise SystemExit('texture index outside the game tables')
        tp, cl, du, dv = rr; px = (tp & 15) * 64; py = ((tp >> 4) & 1) * 256; md = (tp >> 7) & 3
        cx_ = (cl & 63) * 16; cy_ = cl >> 6
        pages.setdefault((px, py, md), None); pals.setdefault((cx_, cy_, md), None)
        return (px, py, md), (cx_, cy_, md), du, dv, tp
    scan = []
    for i in use:
        seg = nm[ptab[i['k']]:(ptab[i['k'] + 1] if i['k'] + 1 < len(ptab) else len(nm))]
        if i['k'] == len(ptab) - 1: seg = MM.model_blob(nm, 0, len(nm))[ptab[i['k']]:]
        ws, lay, vo = layout(seg)
        for kind, b in lay:
            if kind == 'e':
                if (ws[b + 2] >> 24) & 4: reg_tex(ws[b + 7] >> 16, ws[b + 6] >> 16)
            else: reg_tex(ws[b + 5] >> 16, ws[b + 4] >> 16)
    # destination
    pk = sorted(pages); dst = {}
    free = list(PAGE_DST)
    for key in pk:
        px, py, md = key
        if md == 0:
            if not free: raise SystemExit('too many texture pages')
            dst[key] = free.pop(0)
        else: raise SystemExit('8-bit/16-bit pages are not supported: %s' % (key,))
    pdst = {}; n = 0
    for key in sorted(pals):
        if key[2] != 0: raise SystemExit('only 16-colour palettes')
        pdst[key] = (PAL_DST[0] + 16 * (n % 12), PAL_DST[1] + n // 12); n += 1
    log('  pages %s palettes %d' % ({k: dst[k] for k in pk}, n))
    # tables: page index and clut index inside the hero tables
    tpidx = {key: TIDX0 + j for j, key in enumerate(pk)}
    cluts_used = set()
    for k in (14, 15):
        for q in fparts[k]['polys']:
            if 'uv' in q: cluts_used.add(q['uv']['clut'])
            for d in q['tex']: cluts_used.add(d['uv']['clut'])
    freec = [x for x in range(16) if x not in cluts_used]
    pkeys = sorted(pals)
    if len(freec) < len(pkeys): raise SystemExit('not enough free clut slots (%d needed, %d free)' % (len(pkeys), len(freec)))
    cidx = {key: freec[j] for j, key in enumerate(pkeys)}
    clut_table = {cidx[key]: (pdst[key][0] >> 4) | (pdst[key][1] << 6) for key in pkeys}
    tp_table = {tpidx[key]: (dst[key][0] >> 6) | ((dst[key][1] >> 8) << 4) | (key[2] << 7) for key in pk}
    # parts
    out_parts = [None] * 16; flint_t = ftab; groups = {}; segs = {}
    for i, bone in zip(use, path): groups.setdefault(bone, []).append(i)
    for i, bone in zip(use, path):
        k = i['k']; cN, sc, cF = xfm[bone]
        seg = bytearray(nm[ptab[k]:(ptab[k + 1] if k + 1 < len(ptab) else len(nm))])
        if k == len(ptab) - 1:
            blob = MM.model_blob(nm, 0, len(nm)); seg = bytearray(blob[ptab[k]:])
        ws, lay, vo = layout(bytes(seg)); nvert = i['nv']
        R = i['R']; S = i['S']
        newv = []
        for jv in range(nvert):
            o = vo + 16 * jv; x, y, z, nx, ny, nz = struct.unpack_from('<6h', seg, o)
            w = yawv(R @ (np.array([x, y, z], float) * S) + i['t']); w = (w - cN) * sc + cF
            n_ = yawv(R @ np.array([nx, ny, nz], float)) / sc
            ln = np.linalg.norm(n_); n_ = n_ / ln * 4096 if ln > 1e-6 else n_
            struct.pack_into('<6h', seg, o, *[int(round(v)) for v in (*w, *n_)]); newv.append(w)
        mn = np.min(newv, 0); mx = np.max(newv, 0)
        struct.pack_into('<6h', seg, 2, *[int(math.floor(v)) for v in mn], *[int(math.ceil(v)) for v in mx]) if False else None
        h = list(struct.unpack_from('<9h', seg, 0)); h[1:4] = [int(math.floor(v)) for v in mn]; h[4:7] = [int(math.ceil(v)) for v in mx]; struct.pack_into('<9h', seg, 0, *h)
        # texture words
        for kind, b in lay:
            base = b if kind == 'e' else b
            wi = (b + 6, b + 7, b + 8, b + 9) if kind == 'e' else (b + 4, b + 5, b + 6, b + 7)
            cmdw = ws[b + 2] if kind == 'e' else ws[b]
            if not (cmdw >> 24) & 4: continue
            wa, wb, wc, we = ws[wi[0]], ws[wi[1]], ws[wi[2]], ws[wi[3]]
            pgk, plk, du, dv, tp = reg_tex(wb >> 16, wa >> 16)
            def fix(w): return (((w & 255) + du) & 255) | (((((w >> 8) & 255) + dv) & 255) << 8)
            wa2 = fix(wa & 0xFFFF) | (cidx[plk] << 16); wb2 = fix(wb & 0xFFFF) | (((wb >> 16) & 0xFF00 | tpidx[pgk]) << 16)
            wc2 = fix(wc & 0xFFFF) | (fix(wc >> 16) << 16)
            ws[wi[0]], ws[wi[1]], ws[wi[2]] = wa2, wb2, wc2
            e1 = (we & 0xFFFFFFE0) | ((dst[pgk][0] >> 6) & 15) | (((dst[pgk][1] >> 8) & 1) << 4) if False else we
            ws[wi[3]] = e1
        struct.pack_into('<%dI' % len(ws), seg, 0x18, *ws)
        segs.setdefault(bone, []).append(bytes(seg))
    def merge(segs):
        a = bytearray(segs[0]); 
        for b_ in segs[1:]:
            b_ = bytearray(b_); wa, la, voa = layout(bytes(a)); wb, lb, vob = layout(bytes(b_)); na = (len(a) - voa) // 16; nb_ = (len(b_) - vob) // 16
            for kind, q in lb:
                if kind == 'e':
                    w_ = wb[q + 1]; bs = [(w_ >> (8 * j)) & 255 for j in range(4)]; bs = [x + na if x < nb_ else x for x in bs]; wb[q + 1] = sum(x << (8 * j) for j, x in enumerate(bs))
            ha = list(struct.unpack_from('<9h', a, 0)); hb = struct.unpack_from('<9h', b_, 0)
            ha[1:4] = [min(x, y) for x, y in zip(ha[1:4], hb[1:4])]; ha[4:7] = [max(x, y) for x, y in zip(ha[4:7], hb[4:7])]; ha[7] += hb[7]; ha[8] += hb[8]
            body = struct.pack('<%dI' % len(wa), *wa) + struct.pack('<%dI' % len(wb), *wb)
            a = bytearray(struct.pack('<9h', *ha) + bytes(2) + struct.pack('<I', 0) + body + bytes(a[voa:]) + bytes(b_[vob:]))
        return bytes(a)
    for bone, lst in segs.items():
        sg = merge(lst) if len(lst) > 1 else lst[0]; nvert = struct.unpack_from('<h', sg, 16)[0]; out_parts[bone] = (sg, len(sg) - nvert * 16)
    stub_poly = [([np.zeros(3)] * 3, [np.zeros(3), np.array([1.0, 0, 0]), np.array([0, 1.0, 0])], [np.zeros(3)] * 3, True)]
    for k in range(16):
        if out_parts[k] is None:
            if k >= 14: out_parts[k] = ('keep', None)
            else:
                hd, body, vb, _ = B.encode_part(stub_poly); out_parts[k] = (hd + struct.pack('<I', 0) + body + vb, 0x18 + len(body))
    data = bytearray(flint[:0x130]); pos = 0x130; offs = []; chunks = []
    for k in range(16):
        seg, vo = out_parts[k]
        if seg == 'keep':
            sz = (ftab[k + 1] - ftab[k]) if k < 15 else len(flint) - ftab[k]
            seg = bytearray(flint[ftab[k]:ftab[k] + sz]); old = struct.unpack_from('<I', seg, 0x14)[0]; struct.pack_into('<I', seg, 0x14, old - ftab[k] + pos)
        else:
            seg = bytearray(seg); nvert = struct.unpack_from('<h', seg, 16)[0]; struct.pack_into('<I', seg, 0x14, pos + len(seg) - nvert * 16)
        offs.append(pos); chunks.append(bytes(seg)); pos += len(seg)
    struct.pack_into('<64I', data, 0x20, *(offs + [0xFFFFFFFF] * 48)); struct.pack_into('<I', data, 0x128, 0); struct.pack_into('<I', data, 0x12C, 16)
    model = bytes(data) + b''.join(chunks)
    P = M.parse_model(model); bad = [i for i, p in enumerate(P) if not (p['consumed'] and p['n_ok'])]
    log('  model %d bytes, parts ok: %s, vertices per part %s' % (len(model), 'yes' if not bad else 'NO %s' % bad, [len(p['verts']) for p in P]))
    if bad or len(model) > MAX_BYTES: raise SystemExit('model unusable')
    # texture data
    blobs = []
    for key in pk:
        px, py, md = key; a = np.ascontiguousarray(vr[0][py:py + 256, px:px + 64]).astype('<u2'); blobs.append((dst[key][0], dst[key][1], 64, 256, a.tobytes()))
    for key in pkeys:
        cx_, cy_, md = key; a = np.ascontiguousarray(vr[0][cy_:cy_ + 1, cx_:cx_ + 16]).astype('<u2'); blobs.append((pdst[key][0], pdst[key][1], 16, 1, a.tobytes()))
    payload = struct.pack('<I', len(model)) + model + struct.pack('<B', len(tp_table)) + b''.join(struct.pack('<BH', k, v) for k, v in sorted(tp_table.items()))
    payload += struct.pack('<B', len(clut_table)) + b''.join(struct.pack('<BH', k, v) for k, v in sorted(clut_table.items()))
    payload += struct.pack('<B', len(blobs)) + b''.join(struct.pack('<4H', x, y, w, h) + d for x, y, w, h, d in blobs)
    import hdc
    os.makedirs(out_dir, exist_ok=True); path_ = os.path.join(out_dir, 'hero_%s.bin' % name)
    open(path_, 'wb').write(b'A2HN' + struct.pack('<I', len(payload)) + hdc.crypt(payload, 0xA2480001))
    log('  wrote %s (%d bytes)' % (path_, os.path.getsize(path_)))
    return dict(model=model, tp_table=tp_table, clut_table=clut_table, blobs=blobs, name=name)

if __name__ == '__main__':
    for nm in (sys.argv[1:] or list(CHARACTERS)): build(nm, os.environ.get('A2MODOUT', os.path.join(os.path.dirname(os.path.dirname(HERE)), 'hd')))
