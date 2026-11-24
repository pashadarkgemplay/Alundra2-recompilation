import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import a2img
_P = {}
def pack(name):
    if name not in _P: _P[name] = dict(a2img.records(name))
    return _P[name]
def rec(name, i): return pack(name).get(i, b'')
def grp(): return a2img.table('GRP.GP'), len(a2img.data('GRP.GP'))
def gslot(i, k):
    s = a2img.grp_slots(i)
    return s[k] if s else b''
