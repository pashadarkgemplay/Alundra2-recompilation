"""Resolve the REAL tpage/clut of a map-embedded model element exactly like the game does (GAME00 0x800AEC04 -> exe 0x80024DC4), and say which map slot supplies it.
No per-model tables: everything comes from the 44-byte type record (bytes 36..39, class) plus the game's own constant tables."""
import struct
_EXE=None
def exe():
    global _EXE
    if _EXE is None: _EXE=open('D:/A2/src/Alundra2-recompilation-main/game/SLUS_010.17','rb').read()[0x800:]
    return _EXE
def hw(a,n=1):
    d=exe(); o=a-0x80017000
    return list(struct.unpack_from('<%dH'%n,d,o))
def tables(f):
    """(tpage table, clut table, du, dv) the game passes to 0x80024DC4 for a type record dict (maptypes.fields)"""
    c=f['cls']
    if c==14: return hw(0x80052588,2),hw(0x8005258C,16),0,128
    if c==4:  return hw(0x800522D0,6),hw(0x800522E8,96),0,0          # boss: pointer 0x800522D0 stored at GAME00 0x800EE57C
    return hw(0x80052480+2*f['tp0'],4),hw(0x80052488+2*f['cl0'],40),f['du'],f['dv']
def real(f,tpw,clw):
    """tpw/clw = the tpage/clut halfwords stored in the model polygon. Returns (tpage word, clut word) or None if the index is outside the game's table"""
    T,C,du,dv=tables(f); i=tpw&0xFF
    if i>=len(T) or clw>=len(C): return None
    return (T[i]&0xFF9F)|((tpw>>3)&0x60), C[clw], du, dv
def where(tp,cl):
    """what VRAM cell the words mean and which map slot put data there.  Placement = the map loader in GAME00 (0x800AF1F0..0x800AF840, read from the code)
    and verified byte-for-byte against a VRAM dump of map 213:
      slot0: palettes rect (768,488,256x10) one row per palette; images from (320,0), stream k at x=320+64*k
      slot1: palette (768,503), 8-bit 128x256 image at (960,0)
      slot2: palettes (768,498,256x2); 256x128 4-bit image at (512,384)  [= lower half of page 0x18]
      slot3: palettes as ONE rect (896,464, 64x16): 256 colours = 4 rows of 64, so palette p covers rows 464+4p..467+4p; 4-bit images (320+64k,256), k=0..2"""
    px=(tp&15)*64; py=((tp>>4)&1)*256; mode=(tp>>7)&3; cx=(cl&63)*16; cy=cl>>6
    src=None
    if py==0 and px>=320: src=('slot0 stream',(px-320)//64)
    elif py==0 and px==960: src=('slot1 stream',0)
    elif py==256 and 320<=px<=448 and mode==0: src=('slot3 stream',(px-320)//64)
    elif py==256 and px==512 and mode==0: src=('page 0x18',0)
    pal=None
    if 896<=cx<960 and 464<=cy<476: pal=('slot3 palette',(cy-464)//4,((cy-464)%4)*4+(cx-896)//16)
    elif cy in (498,499) and cx>=768: pal=('slot2 palette',cy-498,(cx-768)//16)
    elif 488<=cy<=497 and cx>=768: pal=('slot0 palette',cy-488,(cx-768)//16)
    elif cy==503 and cx>=768: pal=('slot1 palette',0,(cx-768)//16)
    return dict(page=(px,py),mode=mode,abr=(tp>>5)&3,clut=(cx,cy),page_src=src,pal_src=pal)
