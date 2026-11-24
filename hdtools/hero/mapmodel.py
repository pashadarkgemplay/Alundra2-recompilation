"""Models and animation banks embedded in map objects (GRP.GP slot 10, 44-byte type records, offsets at +28 model, +32 bank).
The blob of a model has no size field: every part but the last ends where the next part starts, the last one ends at (vertex block offset @+0x14)+16*nvert."""
import sys,struct
sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from lib import gslot,grp
from maptypes import types,fields
import modelparse as M, animtool as A
def slot10(mp): return gslot(mp,10)
def offsets(d,recs):
    return sorted(set([x for f in recs for x in (f['model'],f['bank'],f['extra']) if 0<x<len(d)]+list(struct.unpack_from('<4I',d,0))+[len(d)]))
def model_blob(d,mo,end):
    """cut the model so that its last part ends exactly after its vertex block"""
    m=d[mo:end]; t=M.part_table(m)
    if not t: return m
    last=t[-1]; nvert=struct.unpack_from('<h',m,last+16)[0]; vo=struct.unpack_from('<I',m,last+0x14)[0]
    return m[:vo+16*nvert]
def map_objects(mp):
    """[(type_index, fields, model_bytes|None, bank_bytes|None)] for one map"""
    d=slot10(mp)
    if len(d)<16: return []
    recs=[fields(r) for r in types(d)]; offs=offsets(d,recs); out=[]
    for ti,f in enumerate(recs):
        m=b=None
        if f['cls']==14 or f['flags']&0xC000!=0x8000:
            mo=f['model']
            if mo and mo<len(d):
                end=min([o for o in offs if o>mo]+[len(d)]); cand=d[mo:end]
                if M.is_model(cand): m=model_blob(d,mo,end)
            bo=f['bank']
            if m is not None and bo and bo<len(d):
                bend=min([o for o in offs if o>bo]+[len(d)]); cb=d[bo:bend]
                if A.is_bank(cb): b=cb
        out.append((ti,f,m,b))
    return out
if __name__=='__main__':
    t,sz=grp(); tot=ok=bank=bone=0; bad=[]
    for i in range(len(t)):
        if not t[i]: continue
        for ti,f,m,b in map_objects(i):
            if m is None: continue
            tot+=1; P=M.parse_model(m); g=all(p['consumed'] and p['n_ok'] for p in P); ok+=g
            if not g and len(bad)<5: bad.append((i,ti,f['id']))
            if b is not None:
                bank+=1; nb,tab=A.parse_bank(b); bone+=(nb==len(P))
    print('models',tot,'every part parsed exactly',ok,'with bank',bank,'bank bones == model parts',bone); print(bad)
