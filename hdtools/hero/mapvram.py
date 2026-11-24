"""The part of the PS1 video memory that a map's own texture slots (0..3 of its GRP.GP entry) provide, built the way the game's map loader does it
(GAME00 0x800AF1F0..0x800AF840, CONFIRMED by reading the code; the placement was also compared word for word with a VRAM dump of map 213):
  slot 0  palettes: rect (768,488) 256 wide, one row per palette (10 rows)      images: stream k at x = 320+64*k, y = 0 (4-bit = 64 words wide, 8-bit = 128 words)
  slot 1  palette:  row (768,503)                                                image : 128x256 8-bit at (960,0)
  slot 2  palettes: rows (768,498) and (768,499)                                 image : 256x128 4-bit at (512,384)
  slot 3  palettes: ONE rect (896,464) 64 wide x 16 tall: palette p (256 colours) fills rows 464+4p .. 467+4p     images: stream k at (320+64*k, 256), k = 0..2
Anything outside these cells is NOT provided by the map (other loaders write there)."""
import sys,struct,numpy as np
sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from recipes import slot,table,decode1,decode2
_T=None
def _t():
    global _T
    if _T is None: _T=table('GRP.GP')[0]
    return _T
def streams(d):
    """(palettes as uint16[256] arrays, [(index, uint16 image words | None, is8bit, width, height)])"""
    h=struct.unpack('<13I',d[:52]); ty,npal=h[0],h[1]; W,Hh=h[2]&0xffff,h[2]>>16; sizes=list(h[3:3+ty]); off=52+512*npal
    pals=[np.frombuffer(d[52+512*i:52+512*(i+1)],dtype='<u2') for i in range(npal)]; out=[]
    for k,sz in enumerate(sizes):
        s=d[off:off+sz]; off+=sz
        if not sz: out.append((k,None,False,W,Hh)); continue
        deep=s[1]!=0; tgt=W*Hh if deep else W*Hh//2
        o,p=decode2(s,tgt) if s[0]==2 else (decode1(s,tgt) if s[0]==1 else (s[4:],len(s)))
        out.append((k,np.frombuffer(bytes(o),dtype='<u2'),deep,W,Hh))
    return pals,out
def build(mp):
    """returns (vram uint16[512,1024], provided bool[512,1024])"""
    v=np.zeros((512,1024),np.uint16); ok=np.zeros((512,1024),bool)
    def put(x,y,a):
        h,w=a.shape; v[y:y+h,x:x+w]=a; ok[y:y+h,x:x+w]=True
    t=_t()
    for k in (0,1,2,3):
        d=slot(t,mp,k)
        if not d: continue
        pals,ss=streams(d)
        for j,p in enumerate(pals):
            if k==0: put(768,488+j,p.reshape(1,256))
            elif k==1: put(768,503+j,p.reshape(1,256))
            elif k==2: put(768,498+j,p.reshape(1,256))
            elif k==3: put(896,464+4*j,p.reshape(4,64))
        for (i,a,deep,W,Hh) in ss:
            if a is None: continue
            wd=(W if deep else W//2)//2
            if len(a)!=Hh*wd: continue
            a=a.reshape(Hh,wd)
            if k==0: put(320+64*i,0,a)
            elif k==1: put(960,0,a)
            elif k==2: put(512,384,a)
            elif k==3: put(320+64*i,256,a)
    return v,ok
def render(v,ok,tpage,clut,du=0,dv=0):
    """RGBA uint8 [256,256,4] of a texture page seen through a clut, exactly like the GPU fetches texels (4-bit / 8-bit / 15-bit); texel value 0 = transparent.
    Also returns the fraction of used texels/palette entries that the map really provides (1.0 = fully known)."""
    px=(tpage&15)*64; py=((tpage>>4)&1)*256; mode=(tpage>>7)&3; cx=(clut&63)*16; cy=clut>>6
    ys=np.arange(256)[:,None]; xs=np.arange(256)[None,:]
    if mode==0:
        hw=v[py+ys,px+(xs>>2)]; idx=(hw>>((xs&3)*4))&15; pal=v[cy,cx:cx+16]; prov=ok[py+ys,px+(xs>>2)]&ok[cy,cx:cx+16][idx]
    elif mode==1:
        hw=v[py+ys,px+(xs>>1)]; idx=(hw>>((xs&1)*8))&255; pal=v[cy,cx:cx+256]; prov=ok[py+ys,px+(xs>>1)]&ok[cy,cx:cx+256][idx]
    else:
        c=v[py+ys,px+xs+0*ys]; r=((c&31)<<3); g=(((c>>5)&31)<<3); b=(((c>>10)&31)<<3); return np.dstack([r,g,b,np.where(c==0,0,255)]).astype('uint8'),float(ok[py+ys,px+xs].mean())
    c=pal[idx]
    r=((c&31)<<3); g=(((c>>5)&31)<<3); b=(((c>>10)&31)<<3); a=np.where(c==0,0,255)
    return np.dstack([r|(r>>5),g|(g>>5),b|(b>>5),a]).astype('uint8'),float(prov.mean())
if __name__=='__main__':
    mp=int(sys.argv[1]); dump=np.fromfile(sys.argv[2],dtype='<u2').reshape(512,1024)
    v,ok=build(mp); eq=(v[ok]==dump[ok])
    print('map',mp,'cells provided by the map',int(ok.sum()),'equal to the VRAM dump',int(eq.sum()),'(%.4f)'%eq.mean())
