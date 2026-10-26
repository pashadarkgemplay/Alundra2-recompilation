import struct, lzma, sys, io

class R:
    def __init__(s,b): s.b=b; s.p=0
    def u8(s): v=s.b[s.p]; s.p+=1; return v
    def num(s):
        f=s.u8(); mask=0x80; v=0
        for i in range(8):
            if not f&mask:
                hi=f&(mask-1); return v|(hi<<(8*i))
            v|=s.u8()<<(8*i); mask>>=1
        return v
    def raw(s,n): v=s.b[s.p:s.p+n]; s.p+=n; return v

def parse_streams(r):
    info={'pack_pos':0,'pack_sizes':[],'folders':[]}
    while True:
        t=r.u8()
        if t==0: break
        if t==6:
            info['pack_pos']=r.num(); n=r.num()
            while True:
                t2=r.u8()
                if t2==0: break
                if t2==9: info['pack_sizes']=[r.num() for _ in range(n)]
                elif t2==10:
                    alldef=r.u8()
                    if not alldef: raise Exception('crc bitvec')
                    for _ in range(n): r.raw(4)
        elif t==7:
            assert r.u8()==0x0b
            nf=r.num(); assert r.u8()==0
            for _ in range(nf):
                nc=r.num(); coders=[]; tin=tout=0
                for _ in range(nc):
                    fl=r.u8(); idl=fl&0xf; cid=r.raw(idl)
                    nin=nout=1
                    if fl&0x10: nin=r.num(); nout=r.num()
                    props=b''
                    if fl&0x20: props=r.raw(r.num())
                    coders.append((cid,props,nin,nout)); tin+=nin; tout+=nout
                nbp=tout-1
                bps=[(r.num(),r.num()) for _ in range(nbp)]
                npk=tin-nbp
                if npk>1: [r.num() for _ in range(npk)]
                info['folders'].append({'coders':coders,'bps':bps})
            assert r.u8()==0x0c
            for f in info['folders']:
                f['unpack']=[r.num() for _ in range(sum(c[3] for c in f['coders']))]
            while True:
                t2=r.u8()
                if t2==0: break
                if t2==0x0a:
                    alldef=r.u8()
                    for _ in info['folders']: r.raw(4)
        elif t==8:
            info['sub']=[]
            while True:
                t2=r.u8()
                if t2==0: break
                if t2==0x0d:
                    info['sub']=[r.num() for _ in info['folders']]
                elif t2==9:
                    info['sub_sizes']=[]
                    for i,f in enumerate(info['folders']):
                        n=info['sub'][i] if info['sub'] else 1
                        tot=0
                        for _ in range(n-1):
                            x=r.num(); info['sub_sizes'].append(x); tot+=x
                        info['sub_sizes'].append(f['unpack'][-1]-tot)
                elif t2==0x0a:
                    r.u8(); 
                    rest=len(r.b)-r.p
                    info['crc_rest']=r.raw(0)
    return info

def lzma_dec(props,data,size):
    pb=props[0]; lc=pb%9; pb//=9; lp=pb%5; pb//=5
    ds=struct.unpack('<I',props[1:5])[0]
    flt=[{'id':lzma.FILTER_LZMA1,'dict_size':ds,'lc':lc,'lp':lp,'pb':pb}]
    d=lzma.LZMADecompressor(format=lzma.FORMAT_RAW,filters=flt)
    return d.decompress(data,max_length=size)
