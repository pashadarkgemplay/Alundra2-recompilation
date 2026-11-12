import sys,struct
sys.path.insert(0,'.')
from iso import files,rd,table
_P={}
def pack(name):
    if name not in _P:
        t,sz=table(name); nz=sorted(set(x for x in t if x))
        nxt={nz[i]:(nz[i+1] if i+1<len(nz) else sz) for i in range(len(nz))}
        _P[name]=(t,nxt)
    return _P[name]
def rec(name,i):
    t,nxt=pack(name); x=t[i]
    return rd(name,x,nxt[x]-x) if x else b''
_G=None
def grp():
    global _G
    if _G is None: _G=table('GRP.GP')
    return _G
H=260
def gslot(i,k):
    t,sz=grp(); w=struct.unpack('<48I',rd('GRP.GP',t[i],192)); a,b=w[2*k],w[2*k+1]
    return rd('GRP.GP',t[i]+H+a,b) if b else b''
