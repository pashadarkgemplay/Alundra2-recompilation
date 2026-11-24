"""Removes broken upscaler outputs (missing size, black or unrelated pictures) so `hdbuild.py ai` redoes them."""
import os, sys, numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hdbuild import WORK

def check(fix=True):
    d_in, d_out = os.path.join(WORK, 'in'), os.path.join(WORK, 'out'); bad = 0; ok = 0
    for f in sorted(os.listdir(d_in)):
        o = os.path.join(d_out, f)
        if not os.path.exists(o): continue
        try:
            a = np.asarray(Image.open(os.path.join(d_in, f)).convert('RGB')).astype(np.float32)
            im = Image.open(o).convert('RGB')
            good = im.size == (a.shape[1] * 4, a.shape[0] * 4)
            if good:
                b = np.asarray(im.resize((a.shape[1], a.shape[0]), Image.BOX)).astype(np.float32)
                lim = 14 if min(a.shape[:2]) > 96 else 40
                good = np.abs(a - b).mean() < max(lim, 0.5 * a.std()) and not (b.max() < 4 and a.max() > 16)
        except Exception: good = False
        if good: ok += 1
        else:
            bad += 1
            if fix: os.remove(o)
    print('[hd] check: %d good, %d broken%s' % (ok, bad, ' (removed)' if fix else ''))
    return bad

if __name__ == '__main__': check()
