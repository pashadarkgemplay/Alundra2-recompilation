"""Map object-type records (GRP.GP slot 10): the fields that decide a model's tpage/clut (read from the code at GAME00 0x800AEC04).
 record = 44 bytes: +0 id(u16, 0xFFFF ends the list), +2 class(u8), +4 flags word (bits 14..15 select what gets set up),
 +28 model offset, +32 bank offset, +36 tpage-table start, +37 clut-table start, +38 du, +39 dv, +40 extra offset."""
import sys,struct,collections
sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from lib import gslot,grp
def types(slot10):
    if len(slot10)<16: return []
    o=struct.unpack_from('<I',slot10,0)[0]; out=[]
    while o+44<=len(slot10):
        r=slot10[o:o+44]
        if struct.unpack_from('<H',r,0)[0]==0xFFFF: break
        out.append(r); o+=44
    return out
def fields(r):
    return dict(id=struct.unpack_from('<H',r,0)[0],cls=r[2],flags=struct.unpack_from('<I',r,4)[0],model=struct.unpack_from('<I',r,28)[0],bank=struct.unpack_from('<I',r,32)[0],
                tp0=r[36],cl0=r[37],du=r[38],dv=r[39],extra=struct.unpack_from('<I',r,40)[0],b20=r[20])
if __name__=='__main__':
    t,sz=grp(); n=0; C=collections.Counter(); F=collections.Counter(); V=collections.Counter()
    for i in range(len(t)):
        if not t[i]: continue
        d=gslot(i,10)
        for r in types(d):
            f=fields(r); n+=1; C[f['cls']]+=1; F[f['flags']&0xC000]+=1; V[(f['cls'],f['flags']&0xC000,f['tp0'],f['cl0'],f['du'],f['dv'])]+=1
    print('records',n); print('class',sorted(C.items())); print('flags&0xC000',sorted(F.items()))
    for k,v in sorted(V.items(),key=lambda x:-x[1])[:40]: print(k,v)
