"""Offline textured picture of a built hero in a Flint pose: python shot.py <name>... -> A2SHOT (default hero_shot.png)"""
import os, sys, math
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.dirname(HERE))
import numpy as np, modelparse as M, animtool as A, build as B, mapvram as MV
from lib import rec
from PIL import Image, ImageDraw

def vram_of(blobs):
    v = np.zeros((512, 1024), np.uint16); ok = np.zeros((512, 1024), bool)
    for x, y, w, h, d in blobs: v[y:y + h, x:x + w] = np.frombuffer(d, '<u2').reshape(h, w); ok[y:y + h, x:x + w] = True
    return v, ok

def render(res, anim, frame, yaw, size=360, pitch=0.15):
    model = res['model']; parts = M.parse_model(model); fb = rec('PACK0.PAK', 69); nb, tab = A.parse_bank(fb); a = A.decode_anim(fb, tab[anim], nb)
    v, ok = vram_of(res['blobs']); cache = {}
    tpt = res['tp_table']; clt = res['clut_table']
    ca, sa = math.cos(yaw), math.sin(yaw); cp, sp = math.cos(pitch), math.sin(pitch)
    tris = []; allp = []
    for k, p in enumerate(parts):
        b = a['bones'][k] if k < len(a['bones']) else None
        row = [0, 0, 0, 0, 0, 0, 4096, 4096, 4096] if b is None else b[0][min(frame, a['nframes'] - 1)]
        if b is not None and b[1][min(frame, a['nframes'] - 1)]: continue
        P = A.pose_part([q[:3] for q in p['verts']], row); P = [(x * ca + z * sa, y * cp - (-x * sa + z * ca) * sp, y * sp + (-x * sa + z * ca) * cp) for x, y, z in P]; allp += P
        for q in p['polys']:
            if max(q['v']) >= len(P): continue
            layers = [(q['col'], q.get('uv'), q['cmd'])] + [(d['col'], d['uv'], d['cmd']) for d in q['tex']]
            for li, (col, uv, cmd) in enumerate(layers):
                n = len(q['v']); V = [P[i] for i in q['v']]; tex = None
                if cmd & 4 and uv and k < 14 and uv['tpage'] & 0xFF in tpt and uv['clut'] in clt:
                    tw = tpt[uv['tpage'] & 0xFF]; cw = clt[uv['clut']]; key = (tw, cw)
                    if key not in cache: cache[key] = MV.render(v, ok, tw, cw)[0]
                    tex = (cache[key], uv['uv'])
                tris.append((sum(x[2] for x in V) / n - (0.3 if li else 0), V, col, tex, n))
    xs = [p[0] for p in allp]; ys = [p[1] for p in allp]; x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys); sc = (size - 16) / max(x1 - x0, y1 - y0, 1); cx = (x0 + x1) / 2
    img = np.zeros((size, size, 3), np.float32); img[:] = (30, 30, 50); zb = np.full((size, size), 1e9, np.float32)
    for z, V, col, tex, n in sorted(tris, key=lambda t: -t[0]):
        S = [((vv[0] - cx) * sc + size / 2, (vv[1] - y0) * sc + 8) for vv in V]
        for a_, b_, c_ in ([(0, 1, 2)] if n == 3 else [(0, 1, 2), (0, 2, 3)]):
            Pp = [S[a_], S[b_], S[c_]]; mnx = int(max(0, min(p[0] for p in Pp))); mxx = int(min(size - 1, max(p[0] for p in Pp))); mny = int(max(0, min(p[1] for p in Pp))); mxy = int(min(size - 1, max(p[1] for p in Pp)))
            if mxx < mnx or mxy < mny: continue
            X, Y = np.meshgrid(np.arange(mnx, mxx + 1) + 0.5, np.arange(mny, mxy + 1) + 0.5)
            (x1_, y1_), (x2_, y2_), (x3_, y3_) = Pp; den = (y2_ - y3_) * (x1_ - x3_) + (x3_ - x2_) * (y1_ - y3_)
            if abs(den) < 1e-6: continue
            l1 = ((y2_ - y3_) * (X - x3_) + (x3_ - x2_) * (Y - y3_)) / den; l2 = ((y3_ - y1_) * (X - x3_) + (x1_ - x3_) * (Y - y3_)) / den; l3 = 1 - l1 - l2
            m = (l1 >= -0.01) & (l2 >= -0.01) & (l3 >= -0.01)
            if not m.any(): continue
            c1 = np.array([(col[k_] & 255, (col[k_] >> 8) & 255, (col[k_] >> 16) & 255) for k_ in (a_, b_, c_)], np.float32)
            rgb = l1[..., None] * c1[0] + l2[..., None] * c1[1] + l3[..., None] * c1[2]
            if tex:
                im, uv = tex; U = [uv[k_][0] for k_ in (a_, b_, c_)]; Vv = [uv[k_][1] for k_ in (a_, b_, c_)]
                u = np.clip((l1 * U[0] + l2 * U[1] + l3 * U[2]).astype(int), 0, 255); vv_ = np.clip((l1 * Vv[0] + l2 * Vv[1] + l3 * Vv[2]).astype(int), 0, 255)
                t = im[vv_, u]; okp = t[..., 3] > 0; rgb = np.clip(t[..., :3].astype(np.float32) * rgb / 128.0, 0, 255); m = m & okp
            sub = img[mny:mxy + 1, mnx:mxx + 1]; sub[m] = rgb[m]
    return Image.fromarray(img.clip(0, 255).astype(np.uint8))

if __name__ == '__main__':
    import build2
    names = sys.argv[1:] or list(build2.CHARACTERS); rows = []
    for nm in names:
        res = build2.build(nm, os.environ.get('A2MODOUT', 'D:/A2/work/modout3'), log=lambda *a: None)
        ims = [render(res, an, 0, yw) for an, yw in ((28, 0.0), (28, math.pi), (28, math.pi / 2), (3, 0.0), (3, math.pi))]
        row = Image.new('RGB', (360 * len(ims), 376)); [row.paste(im, (i * 360, 16)) for i, im in enumerate(ims)]; ImageDraw.Draw(row).text((4, 2), nm, fill=(255, 255, 0)); rows.append(row)
    out = Image.new('RGB', (rows[0].width, 376 * len(rows))); [out.paste(r, (0, i * 376)) for i, r in enumerate(rows)]; out.save(os.environ.get('A2SHOT', 'D:/A2/work/hero_shot.png'))
