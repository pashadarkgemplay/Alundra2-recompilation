"""Builds a replacement hero model (Flint's PACK0 record 68) from another character's model, using only the user's own game files.

The foreign model is laid over Flint's rest pose, every polygon is attached to the nearest Flint bone, and its textures are baked into
polygon colours (faces become a grid of small coloured quads), so the game needs no extra texture page or palette.
Result: hd/hero_<name>.bin (encrypted) = new model record (same layout as record 68, parts 14 and 15 kept)."""
import os, sys, math, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.dirname(HERE))
import numpy as np
import modelparse as M, animtool as A, mapvram as MV, matres as MR, mapmodel as MM
from lib import rec

# name -> (map, object id) of the model that is used
CHARACTERS = {'diaz': (340, 207), 'mephisto': (266, 208)}
MAX_BYTES = 31216
MAX_VERTS = int(os.environ.get('A2MAXV', '46'))
HERO_ANIM = 3

def rotmat(rx, ry, rz): return np.array(A.rot_zyx(rx, ry, rz), np.float64)

def hero_rest(bank, nparts):
    nb, tab = A.parse_bank(bank); a = A.decode_anim(bank, tab[HERO_ANIM], nb); T = []
    for k in range(nparts):
        bone = a['bones'][k] if k < len(a['bones']) else None
        row = [0, 0, 0, 0, 0, 0, 4096, 4096, 4096] if bone is None else bone[0][0]
        hid = False if bone is None else bool(bone[1][0])
        tx, ty, tz, rx, ry, rz, sx, sy, sz = row
        T.append(dict(R=rotmat(rx, ry, rz), S=np.array([sx, sy, sz], np.float64) / 4096.0, t=np.array([tx, ty, tz], np.float64), hidden=hid))
    return T

def to_world(T, v): return T['R'] @ (np.asarray(v, np.float64) * T['S']) + T['t']
def to_local(T, w): return (T['R'].T @ (np.asarray(w, np.float64) - T['t'])) / T['S']

def npc_posed(model, bank):
    parts = M.parse_model(model); nb, tab = A.parse_bank(bank); ids = [i for i, o in enumerate(tab) if o != 0xffffffff]
    a = A.decode_anim(bank, tab[ids[0]], nb); out = []
    for k, p in enumerate(parts):
        bone = a['bones'][k] if k < len(a['bones']) else None
        row = [0, 0, 0, 0, 0, 0, 4096, 4096, 4096] if bone is None else bone[0][0]
        if bone is not None and bone[1][0]: out.append(None); continue
        T = dict(zip(('tx', 'ty', 'tz', 'rx', 'ry', 'rz', 'sx', 'sy', 'sz'), row))
        R = rotmat(row[3], row[4], row[5]); S = np.array(row[6:9], np.float64) / 4096.0; t = np.array(row[0:3], np.float64)
        V = [R @ (np.array(v[:3], np.float64) * S) + t for v in p['verts']]
        N = [R @ np.array(v[3:6], np.float64) for v in p['verts']]
        out.append((p, V, N))
    return parts, out

class Sampler:
    def __init__(self, vr, f): self.vr = vr; self.f = f; self.c = {}
    def image(self, tpw, clw):
        rr = MR.real(self.f, tpw, clw)
        if not rr: return None
        k = (rr[0], rr[1])
        if k not in self.c: self.c[k] = MV.render(self.vr[0], self.vr[1], rr[0], rr[1])[0]
        return self.c[k], rr[2], rr[3]
    def cell(self, uvd, uv4, cu, cv, du, dv, n=3):
        """average colour of a texture cell; None if mostly transparent. cu/cv = (u0,u1),(v0,v1) in 0..1 of the quad"""
        img, ddu, ddv = uvd
        acc = np.zeros(3); cnt = 0; tot = 0
        for a in range(n):
            for b in range(n):
                u = cu[0] + (cu[1] - cu[0]) * (a + 0.5) / n; v = cv[0] + (cv[1] - cv[0]) * (b + 0.5) / n
                P0, P1, P2, P3 = uv4
                x = (1 - u) * (1 - v) * P0[0] + u * (1 - v) * P1[0] + u * v * P2[0] + (1 - u) * v * P3[0]
                y = (1 - u) * (1 - v) * P0[1] + u * (1 - v) * P1[1] + u * v * P2[1] + (1 - u) * v * P3[1]
                px = img[int(y + ddv) & 255, int(x + ddu) & 255]; tot += 1
                if px[3] > 0: acc += px[:3]; cnt += 1
        return (acc / cnt, cnt / tot) if cnt else (None, 0.0)

def bilerp(P, u, v): return (1 - u) * (1 - v) * P[0] + u * (1 - v) * P[1] + u * v * P[2] + (1 - u) * v * P[3]

def bake_element(q, V, N, samp, kmax, esub=3):
    """-> list of cells [(corner positions[4], corner normals[4], corner colours[4] (0..255), is_tri)]"""
    idx = q['v']; tri = len(idx) == 3
    P = [np.array(V[i]) for i in idx]; Nn = [np.array(N[i]) for i in idx]; C = [np.array([c & 255, (c >> 8) & 255, (c >> 16) & 255], np.float64) for c in q['col'][:len(idx)]]
    if tri: P.append(P[2]); Nn.append(Nn[2]); C.append(C[2])
    layers = []
    if 'uv' in q and q['cmd'] & 4: layers.append(('base', q['uv']))
    for d in q['tex']: layers.append(('decal', d['uv']))
    images = []
    for kind, uvd in layers:
        im = samp.image(uvd['tpage'], uvd['clut'])
        if im:
            uv = uvd['uv']; uv4 = list(uv[:len(idx)]) + ([uv[2]] if tri else [])
            area = abs(sum(uv4[i][0] * uv4[(i + 1) % 4][1] - uv4[(i + 1) % 4][0] * uv4[i][1] for i in range(4))) / 2
            images.append((kind, im, uv4, area))
    edge = max(np.linalg.norm(P[i] - P[(i + 1) % 4]) for i in range(4)); ksub = max(1, min(esub, int(math.ceil(edge / 14.0))))
    if not images and ksub == 1:
        if tri: return [([p for p in P[:3]], Nn[:3], C[:3], True)]
        return [(P, Nn, C, False)]
    k = max(ksub, 1)
    if images: k = max(k, min(kmax, int(round(math.sqrt(max(a for *_, a in images)) / 3.5))))
    cells = []
    for i in range(k):
        for j in range(k):
            cu = (i / k, (i + 1) / k); cv = (j / k, (j + 1) / k); col = None
            for kind, im, uv4, area in images:
                c, frac = samp.cell(im, uv4, cu, cv, 0, 0)
                if c is None or frac < 0.4: continue
                mod = bilerp(C, (cu[0] + cu[1]) / 2, (cv[0] + cv[1]) / 2) / 128.0 if True else 1
                col = np.clip(c * mod, 0, 255)
            corners = [(cu[0], cv[0]), (cu[1], cv[0]), (cu[1], cv[1]), (cu[0], cv[1])]
            if col is None:
                if any(kind == 'base' for kind, *_ in images): continue
                cols = [bilerp(C, u, v) for u, v in corners]
            else: cols = [col] * 4
            cells.append(([bilerp(P, u, v) for u, v in corners], [bilerp(Nn, u, v) for u, v in corners], cols, False))
    return cells

def encode_part_count(polys):
    seen = set()
    for P, Nn, C, tri in polys:
        for i in range(3 if tri else 4): seen.add((tuple(int(round(x)) for x in P[i]), tuple(int(round(x)) for x in Nn[i])))
    return len(seen)

def encode_part(polys):
    """polys: [(corner positions in part-local int, normals int, colours, is_tri)] -> raw part bytes (vertex block offset still to be fixed by caller)"""
    verts = []; vmap = {}; els = []
    def vid(p, n):
        key = (tuple(int(round(x)) for x in p), tuple(int(round(x)) for x in n))
        if key not in vmap: vmap[key] = len(verts); verts.append(key)
        return vmap[key]
    for P, Nn, C, tri in polys:
        ids = [vid(P[i], Nn[i]) for i in range(3 if tri else 4)]
        if len(set(ids)) < 3: continue
        els.append((ids, C, tri))
    if not verts: return None
    if len(verts) > MAX_VERTS: return (b'', b'', b'', False)
    xs = [v[0][0] for v in verts]; ys = [v[0][1] for v in verts]; zs = [v[0][2] for v in verts]
    body = bytearray()
    for ids, C, tri in els:
        n = 3 if tri else 4; cmd = 0x30 if tri else 0x38
        idx = ids + [ids[2]] if tri else ids
        words = [0x200 if tri else 0, idx[0] | (idx[1] << 8) | (idx[2] << 16) | (idx[3] << 24)]
        for k in range(4):
            c = C[k if k < n else n - 1]; r, g, b = [int(max(0, min(255, round(x)))) for x in c]
            words.append(r | (g << 8) | (b << 16) | ((cmd << 24) if k == 0 else 0))
        words += [0, 0x01000000, 0, 0xE1000220, 0xFFFFFF00]
        body += struct.pack('<11I', *words)
    vb = bytearray()
    for (p, n) in verts: vb += struct.pack('<6hBBH', p[0], p[1], p[2], n[0], n[1], n[2], 0, 0, 0)
    hdr = struct.pack('<9h', 0, min(xs), min(ys), min(zs), max(xs), max(ys), max(zs), len(els), len(verts)) + b'\0\0'
    return hdr, bytes(body), bytes(vb), len(verts) <= 255

def build(name, out_dir, userscale=1.0, kmax=6, log=print, esub=3):
    mp, oid = CHARACTERS[name]
    flint = rec('PACK0.PAK', 68); fbank = rec('PACK0.PAK', 69)
    fparts = M.parse_model(flint); ftab = M.part_table(flint); T = hero_rest(fbank, 16)
    # Flint rest pose vertices (bones 0..13 take the foreign polygons)
    HV = []; HB = []
    for k in range(14):
        for v in fparts[k]['verts']: HV.append(to_world(T[k], v[:3])); HB.append(k)
    HV = np.array(HV); HB = np.array(HB); hero_h = HV[:, 1].max() - HV[:, 1].min()
    obj = [x for x in MM.map_objects(mp) if x[1]['id'] == oid and x[2] is not None and x[3] is not None]
    if not obj: raise SystemExit('model of %s not found in map %d' % (name, mp))
    _, f, nm, nbk = obj[0]; vr = MV.build(mp); samp = Sampler(vr, f)
    nparts, posed = npc_posed(nm, nbk)
    allv = np.array([v for it in posed if it for v in it[1]]);
    npc_h = allv[:, 1].max() - allv[:, 1].min(); s = hero_h / npc_h * userscale
    cen = allv.mean(0); hcen = HV.mean(0); off = np.array([hcen[0], 0, hcen[2]]) - np.array([cen[0], 0, cen[2]]) * s
    off[1] = HV[:, 1].max() - allv[:, 1].max() * s
    xf = lambda w: np.asarray(w) * s + off
    log('%s: model %d parts, height %.0f -> scale %.3f (hero height %.0f)' % (name, len(parts := nparts), npc_h, s, hero_h))
    pre = []
    for it in posed:
        if not it: continue
        p, V, N = it; V = [xf(v) for v in V]
        for q in p['polys']:
            if max(q['v']) >= len(V): continue
            pre.append((q, V, N))
    def bone_polys_at(k, km, es):
        out = []
        for q, V, N in bone_elems[k]:
            for P, Nn, C, tri in bake_element(q, V, N, samp, km, es): out.append(([to_local(T[k], x) for x in P], [_nrm(T[k], n) for n in Nn], C, tri))
        return out
    bone_elems = [[] for _ in range(14)]; ranks = {}
    for q, V, N in pre:
        cells0 = bake_element(q, V, N, samp, 1, 1)
        if not cells0: continue
        c = np.mean([x for x in cells0[0][0]], axis=0); d = np.linalg.norm(HV - c, axis=1)
        dist = [float(d[HB == k].min()) for k in range(14)]; order = sorted(range(14), key=lambda k: dist[k]); ranks[id(q)] = (order, dist)
        bone_elems[order[0]].append((q, V, N))
    def base_verts(k):
        e = encode_part_count(bone_polys_at(k, 1, 1)); return e
    # a bone can hold only MAX_VERTS vertices: move its farthest elements to the next nearest bone that has room
    for _round in range(400):
        over = [k for k in range(14) if base_verts(k) > MAX_VERTS]
        if not over: break
        k = max(over, key=base_verts); elems = bone_elems[k]
        q, V, N = max(elems, key=lambda e: (0 if (e[0].get('uv') or e[0]['tex']) else 1, ranks[id(e[0])][1][k]))   # untextured and far from the bone first: faces keep their bone
        elems.remove((q, V, N)); order = ranks[id(q)][0]
        for nk in order:
            if nk == k: continue
            bone_elems[nk].append((q, V, N))
            if base_verts(nk) <= MAX_VERTS: break
            bone_elems[nk].remove((q, V, N))
        else: bone_elems[k].append((q, V, N)); break
    for _k in [int(x) for x in os.environ.get('A2DROPBONES', '').split(',') if x]: bone_elems[_k] = []
    SETTINGS = ((6, 2), (6, 1), (5, 2), (5, 1), (4, 2), (4, 1), (3, 2), (3, 1), (2, 2), (2, 1), (1, 2), (1, 1))
    stub = [([np.zeros(3)] * 3, [np.zeros(3), np.array([1.0, 0, 0]), np.array([0, 1.0, 0])], [np.zeros(3)] * 3, True)]   # zero-area triangle: the game scales some bones, a real stub would turn into a long sliver
    def fit_bone(k, cap):
        """best detail (not above cap) whose part has at most 60 vertices; the smallest polygons are dropped only at the lowest detail"""
        for km, es in SETTINGS:
            if (km, es) > cap and not (km <= cap[0] and es <= cap[1]): continue
            polys = bone_polys_at(k, km, es) or stub; enc = encode_part(polys)
            if enc is not None and enc[3]: return enc, (km, es)
        polys = sorted(bone_polys_at(k, 1, 1) or stub, key=lambda pl: -_area(pl[0])); lo, hi = 1, len(polys)
        while lo < hi:
            mid = (lo + hi + 1) // 2; e2 = encode_part(polys[:mid])
            if e2 is not None and e2[3]: lo = mid
            else: hi = mid - 1
        return encode_part(polys[:lo]), ('thinned', lo)
    res = None
    for cap in ((6, 2), (5, 2), (4, 2), (4, 1), (3, 1), (2, 1), (1, 1)):
        raw = []; sizes = []; used = []
        for k in range(16):
            if k >= 14:
                raw.append(('keep', k)); sizes.append(ftab[k + 1] - ftab[k] if k < 15 else len(flint) - ftab[k]); continue
            enc, how = fit_bone(k, cap); raw.append(('new', enc)); sizes.append(0x18 + len(enc[1]) + len(enc[2])); used.append(how)
        total = 0x130 + sum(sizes); log('  detail cap %s: %d bytes %s' % (cap, total, 'fits' if total <= MAX_BYTES else 'too big'))
        if total <= MAX_BYTES: res = (raw, sizes); log('  per bone: %s' % used); break
    if not res: raise SystemExit('could not fit the model')
    parts_raw, sizes = res
    out = bytearray(flint[:0x130]); offs = []; pos = 0x130
    chunks = []
    for (kind, x), sz in zip(parts_raw, sizes):
        offs.append(pos if sz else 0)
        if kind == 'keep':
            seg = bytearray(flint[ftab[x]:ftab[x] + sz]); old_vo = struct.unpack_from('<I', seg, 0x14)[0]; struct.pack_into('<I', seg, 0x14, old_vo - ftab[x] + pos); chunks.append(bytes(seg))
        elif x is None: chunks.append(b'')
        else:
            hdr, body, vb, ok = x; vo = pos + 0x18 + len(body); chunks.append(hdr + struct.pack('<I', vo) + body + vb)
        pos += sz
    # empty parts: a one-triangle invisible stub keeps the part table valid
    for k in range(16):
        if not offs[k]: raise SystemExit('bone %d got no polygons from the model' % k)
    struct.pack_into('<64I', out, 0x20, *(offs + [0xFFFFFFFF] * (64 - 16)))
    struct.pack_into('<I', out, 0x128, 0); struct.pack_into('<I', out, 0x12C, 16)
    data = bytes(out) + b''.join(chunks)
    P = M.parse_model(data); bad = [i for i, p in enumerate(P) if not (p['consumed'] and p['n_ok'])]
    log('  built %d bytes, %d polygons, parts ok: %s' % (len(data), sum(len(p['polys']) for p in P), 'yes' if not bad else 'NO %s' % bad))
    if bad: raise SystemExit('encoder produced an unparsable part')
    os.makedirs(out_dir, exist_ok=True); import hdc; path = os.path.join(out_dir, 'hero_%s.bin' % name); open(path, 'wb').write(b'A2HM' + struct.pack('<I', len(data)) + hdc.crypt(data, 0xA2480001))
    return path, data

def _area(P):
    a = np.asarray(P[0], float); b = np.asarray(P[1], float); c = np.asarray(P[2], float)
    return float(np.linalg.norm(np.cross(b - a, c - a)))

def _nrm(T, n):
    v = (T['R'].T @ np.asarray(n, np.float64)) * T['S']; l = np.linalg.norm(v)
    return v / l * 4096 if l > 1e-6 else np.zeros(3)

if __name__ == '__main__':
    for nm in (sys.argv[1:] or list(CHARACTERS)): build(nm, os.environ.get('A2MODOUT', os.path.join(os.path.dirname(os.path.dirname(HERE)), 'hd')))
