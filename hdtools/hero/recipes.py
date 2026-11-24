import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import a2img
def table(name): return a2img.table(name), len(a2img.data(name))
def slot(t, mp, k):
    s = a2img.grp_slots(mp)
    return s[k] if s else b''
def decode2(s, target): return a2img.lz(s, target), len(s)
decode1 = decode2
