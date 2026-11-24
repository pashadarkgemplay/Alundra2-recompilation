"""Upscaling of palette textures into palette-index HD maps.

An HD pixel keeps two palette indices and a blend weight, so the game's own palettes
(sub-palettes, palette animation, flashes) still apply to the HD picture."""
import os, subprocess, numpy as np
from PIL import Image

ESRGAN = os.environ.get('A2ESRGAN', r'D:\A2\tools\realesrgan\realesrgan-ncnn-vulkan.exe')
PAD = 16

def unpack(bpp, W, H, idx):
    a = np.frombuffer(idx, np.uint8)
    if bpp == 4: a = np.stack([a & 15, a >> 4], 1).reshape(-1)
    return a.reshape(H, W).astype(np.int32)

def pal_rgb(pal):
    p = np.frombuffer(pal, np.uint16).astype(np.int32)
    rgb = np.stack([(p & 31) << 3, ((p >> 5) & 31) << 3, ((p >> 10) & 31) << 3], 1).astype(np.float32)
    return rgb, p

def candidate_palettes(bpp, pals):
    """list of (rgb[ncolors,3], raw[ncolors]) usable for this picture"""
    out = []
    for pal in pals:
        rgb, raw = pal_rgb(pal)
        if bpp == 8: out.append((rgb, raw))
        else:
            for s in range(16): out.append((rgb[s * 16:s * 16 + 16], raw[s * 16:s * 16 + 16]))
    keep = [c for c in out if np.count_nonzero(c[1]) >= 2]
    return keep or out[:1] or [(np.zeros((256 if bpp == 8 else 16, 3), np.float32), np.zeros(256 if bpp == 8 else 16, np.int32))]

def _score(c, opq):
    if not opq.any(): return 1e9
    dx = np.abs(c[:, 1:] - c[:, :-1]).sum(2)[opq[:, 1:] & opq[:, :-1]]
    dy = np.abs(c[1:] - c[:-1]).sum(2)[opq[1:] & opq[:-1]]
    n = dx.size + dy.size
    return (dx.sum() + dy.sum()) / max(n, 1) - 0.5 * opq.mean() * c[opq].std()

def heuristic_maps(ix, cands, T=16):
    """per T x T tile, the palette under which the tile looks smoothest"""
    H, W = ix.shape; col = np.zeros((H, W, 3), np.float32); raw = np.zeros((H, W), np.int32)
    for ty in range(0, H, T):
        for tx in range(0, W, T):
            sl = (slice(ty, ty + T), slice(tx, tx + T)); t = ix[sl]; best = None
            for rgb, r in cands:
                sc = _score(rgb[t], r[t] != 0)
                if best is None or sc < best[0]: best = (sc, rgb, r)
            col[sl] = best[1][t]; raw[sl] = best[2][t]
    return col, raw

def usage_maps(ix, layers):
    """texel colours from the palettes the game really uses on each texel.
    layers: [(rgb[n,3], raw[n], weight[H,W])]; None if nothing covers the picture"""
    if not layers: return None
    H, W = ix.shape; stack = np.stack([l[2] for l in layers]).astype(np.float32); best = stack.argmax(0); cov = stack.max(0) > 0
    if not cov.any(): return None
    lab = np.where(cov, best, -1)
    for _ in range(64):
        if (lab >= 0).all(): break
        nb = lab.copy()
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            sh = np.roll(np.roll(lab, dy, 0), dx, 1); m = (nb < 0) & (sh >= 0); nb[m] = sh[m]
        lab = nb
    lab[lab < 0] = 0
    col = np.zeros((H, W, 3), np.float32); raw = np.zeros((H, W), np.int32)
    for n, (rgb, r, _) in enumerate(layers):
        m = lab == n; i = np.clip(ix[m], 0, len(r) - 1); col[m] = rgb[i]; raw[m] = r[i]
    return col, raw

def esrgan(paths_in, dir_out, model='realesrgan-x4plus', scale=4, tile=0):
    d_in = os.path.dirname(paths_in[0])
    cmd = [ESRGAN, '-i', d_in, '-o', dir_out, '-n', model, '-s', str(scale), '-f', 'png']
    if tile: cmd += ['-t', str(tile)]
    subprocess.run(cmd, check=True, capture_output=True)

def bleed(rgb, opq, n=12):
    """colour of transparent pixels taken from the opaque ones around them"""
    if opq.all() or not opq.any(): return rgb
    c = rgb * opq[..., None]; m = opq.astype(np.float32); out = rgb.copy(); done = opq.copy()
    for _ in range(n):
        cs = sum(np.roll(np.roll(c, dy, 0), dx, 1) for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        ms = sum(np.roll(np.roll(m, dy, 0), dx, 1) for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        new = (~done) & (ms > 0); out[new] = cs[new] / ms[new][..., None]
        c = np.where(new[..., None], out, c); m = np.where(new, 1.0, m); done |= new
        if done.all(): break
    return out

def prepare(ix, rgb):
    """wrap-padded picture for the network (textures repeat in the game)"""
    return np.pad(rgb, ((PAD, PAD), (PAD, PAD), (0, 0)), mode='wrap')

def project(hd, ix, colmap, opq, K, lam_mix=0.0, lam_far=0.0, wlev=15, alpha=None):
    """HD colours -> (i0, i1, w) restricted to the indices around each low-res pixel"""
    H, W = ix.shape; HH, WW = H * K, W * K
    i0 = np.zeros((HH, WW), np.uint8); i1 = np.zeros((HH, WW), np.uint8); wq = np.zeros((HH, WW), np.uint8)
    ys = (np.arange(HH) + 0.5) / K - 0.5; xs = (np.arange(WW) + 0.5) / K - 0.5
    CH = max(1, 262144 // WW)
    for y0 in range(0, HH, CH):
        y1 = min(HH, y0 + CH); yy = ys[y0:y1]
        fy = np.floor(yy).astype(int); fx = np.floor(xs).astype(int)
        ya = np.clip(fy, 0, H - 1); yb = np.clip(fy + 1, 0, H - 1); xa = np.clip(fx, 0, W - 1); xb = np.clip(fx + 1, 0, W - 1)
        cy = np.clip(np.round(yy).astype(int), 0, H - 1); cx = np.clip(np.round(xs).astype(int), 0, W - 1)
        # candidates: the 2x2 footprint plus the nearest pixel
        pts = [(ya[:, None], xa[None]), (ya[:, None], xb[None]), (yb[:, None], xa[None]), (yb[:, None], xb[None])]
        cand_i = np.stack([np.broadcast_to(ix[p], (y1 - y0, WW)) for p in pts], -1)
        cand_c = np.stack([np.broadcast_to(colmap[p[0], p[1]], (y1 - y0, WW, 3)) for p in pts], -2)
        cand_o = np.stack([np.broadcast_to(opq[p], (y1 - y0, WW)) for p in pts], -1)
        anyo = cand_o.any(-1)
        near = ix[cy[:, None], cx[None]]
        C = hd[y0:y1].astype(np.float32)
        best_e = np.full((y1 - y0, WW), np.inf, np.float32); b0 = np.broadcast_to(near, best_e.shape).copy(); b1 = b0.copy(); bw = np.zeros(best_e.shape, np.float32)
        for a in range(4):
            for b in range(a, 4):
                ca = cand_c[..., a, :]; cb = cand_c[..., b, :]; d = cb - ca; dd = (d * d).sum(-1)
                w = np.where(dd > 1e-3, ((C - ca) * d).sum(-1) / np.maximum(dd, 1e-3), 0.0); w = np.clip(w, 0, 1)
                e = ((ca + d * w[..., None] - C) ** 2).sum(-1)
                if a == b: e = e - 1.0
                else: e = e + lam_mix
                e = e + lam_far * ((cand_i[..., a] != near).astype(np.float32))
                if alpha is not None: e = e + np.where(anyo & ~(cand_o[..., a] & cand_o[..., b]), 1e7, 0).astype(np.float32)
                m = e < best_e
                best_e[m] = e[m]; b0[m] = cand_i[..., a][m]; b1[m] = cand_i[..., b][m]; bw[m] = w[m]
        swap = bw > 0.5
        n0 = np.where(swap, b1, b0); n1 = np.where(swap, b0, b1); w = np.where(swap, 1 - bw, bw)
        wq8 = np.round(w * 2 * wlev).astype(np.int32) * (15 // wlev)
        wq8 = np.where(n1 == n0, 0, wq8); n1 = np.where(wq8 == 0, n0, n1)
        if alpha is not None:
            A = alpha[y0:y1]; hast = ~cand_o.all(-1)
            tsel = np.argmin(cand_o, -1); ti = np.take_along_axis(cand_i, tsel[..., None], -1)[..., 0]
            q = lambda x: np.round(np.clip(x, 0, 0.5) * 2 * wlev).astype(np.int32) * (15 // wlev)
            lo = hast & anyo & (A < 0.5); hi = hast & anyo & (A >= 0.5) & (A < 0.9); none = ~anyo
            n1 = np.where(lo, n0, n1); wq8 = np.where(lo, q(A), wq8); n0 = np.where(lo, ti, n0)
            n1 = np.where(hi, ti, n1); wq8 = np.where(hi, q(1 - A), wq8)
            n0 = np.where(none, ti, n0); n1 = np.where(none, ti, n1); wq8 = np.where(none, 0, wq8)
            wq8 = np.where(n1 == n0, 0, wq8); n1 = np.where(wq8 == 0, n0, n1)
        i0[y0:y1] = n0; i1[y0:y1] = n1; wq[y0:y1] = wq8
    return i0, i1, wq

def preview(i0, i1, wq, rgb_of_index):
    c0 = rgb_of_index[i0]; c1 = rgb_of_index[i1]; w = (wq / 30.0)[..., None]
    return np.clip(c0 * (1 - w) + c1 * w, 0, 255).astype(np.uint8)
