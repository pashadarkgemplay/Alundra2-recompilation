"""Alundra 2 (USA) skeletal-animation bank tool (session 54).
Confirmed by SLUS code (loader 0x800253A8 / per-anim 0x80025F00 / channel init 0x80025D5C / frame step 0x80026394 /
output 0x800260F0 / part matrix 0x80027C6C) and by byte-exact tests over every bank on the disc.

BANK RECORD (PACK0 records 69,71,73,75,478 Flint/Alexia, 412/430 PLAYER, TA*/TO*/O*EFF*... ; 51 in PACK0 + 4 in PAKOMAKE):
  0x00 name (ASCII, same name as its model record, which is the record just before it)
  0x1E u8   bone count nb (== number of model parts; model record has 0 here)
  0x20 u32[256] animation block offset (0xFFFFFFFF = empty; several entries may share one block)
  0x420 u32 offset of the "hit/effect data" pointer table (u32 ptr[N]; ptr[0] gives N*4 = table size; used via u16 'extra' channel)
  0x424 34 bytes + 0x446 u16[256]: per-animation default flags (0x8000 active,0x4000 one-shot,0x1000 ...), read by 0x80026098/0x800260AC
  ANIMATION BLOCK (offset o, nfr = u16@o):
    u16 nfr  (last frame index; length = nfr+1 frames), u16 0
    u16 bone_off[nb]   (relative to o; 0xFFFF = bone not animated)
    4 x (u16 frame, u16 event)  (event code fired when the frame counter == frame; 0,0 = unused)   [0x80026198..]
    extra channel: pairs (u8 count, u8 value) until sum(count)=nfr+1; value indexes the pointer table @0x420   [0x80026624]
    bone tracks: u16 chan_off[10] (relative to o) then the channel streams
  CHANNELS per bone: 0-2 translation (units = model units), 3-5 rotation rx,ry,rz (4096 = 360 deg),
    6-8 scale sx,sy,sz (4096 = 1.0), 9 visibility (byte stream: bit7 = flag, bits0-6 = hold count; padded to even).
  STREAM WORD (channels 0-8): w=u16; value = int16(w & 0xFFE0); n = w & 0x1F; segment lasts max(n,1) frames;
    if n >= 3 the next int16 is the slope (added every frame inside the segment, in 1/8 output units).
    translation output = value >> 5 (value/4 in register, >>3 on output); rot/scale output = value (register = value*8, >>3).
  NO PARENT/CHILD LINKS: every part is  v_obj = R * S * v_local + t  with R = RotMatrix(rx,ry,rz) = Rz*Ry*Rx (CONFIRMED in session 57 by running the game's own routine 0x80010A2C, see rotcheck.py),
    S = diag(sx,sy,sz)/4096, and then the object base matrix. Translations are ABSOLUTE in object space (feet end at y = 0, y points down).
MODEL RECORD (PC00 rec 68, PC02 rec 72...): u32[16] part offsets @0x20; part header s16: 0, bbox min xyz, bbox max xyz, npoly, nvert;
  the LAST nvert*16 bytes of a part are vertices: s16 x,y,z, nx,ny,nz (4096 = 1.0), pad.
python3 animtool.py verify | info <PACK> <rec> | pose <model_rec> <bank_rec> [anim] [frame] | shapes <PACK> <rec>
"""
import sys,struct,math
sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from lib import pack,rec

def s16(x): return x-65536 if x&0x8000 else x

# ---------------------------------------------------------------- streams
def decode_stream(d,p,nframes,kind):
    """channels 0..8. kind 0 = translation (>>5), 1 = rotation/scale. returns (values[nframes], end_ptr)"""
    out=[];reg=0;slope=0;cnt=0
    def load():
        nonlocal p,reg,slope,cnt
        w=struct.unpack_from('<H',d,p)[0]; p+=2
        v=s16(w&0xFFE0)
        reg=(v>>2) if kind==0 else (v<<3)      # sra 18 / sra 13 of (v<<16) in the game code
        cnt=w&0x1f
        if cnt>=3:
            slope=s16(struct.unpack_from('<H',d,p)[0]); p+=2
    load(); out.append(reg>>3)
    for f in range(1,nframes):
        if cnt: cnt-=1
        reg+=slope
        if cnt==0: load()
        out.append(reg>>3)
    return out,p

def decode_vis(d,p,nframes):
    out=[];cnt=0;val=0
    for f in range(nframes):
        if cnt==0:
            b=d[p]; p+=1; val=b>>7; cnt=b&0x7f
        out.append(val); cnt-=1
    return out,p

def walk_stream(d,p,nsteps):
    """byte-exact length check without evaluating values: returns (end_ptr, covered==nsteps)"""
    cov=0
    while cov<nsteps:
        if p+2>len(d): return p,False
        w=struct.unpack_from('<H',d,p)[0]; p+=2; n=w&0x1f
        if n>=3: p+=2
        cov+=max(n,1)
    return p,cov==nsteps

def walk_vis(d,p,nsteps):
    cov=0
    while cov<nsteps:
        c=d[p]&0x7f; p+=1
        if c==0: return p,False
        cov+=c
    return p,cov==nsteps

# ---------------------------------------------------------------- bank
def is_bank(r):
    if len(r)<0x700 or not (1<=r[0x1e]<=32): return False
    t=struct.unpack_from('<256I',r,0x20)
    nz=[x for x in t if x!=0xffffffff]
    return bool(nz) and all(0x646<=x<len(r) for x in nz) and min(nz)==0x646

def parse_bank(r):
    nb=r[0x1e]; tab=struct.unpack_from('<256I',r,0x20)
    return nb,tab

def parse_anim(r,o,nb):
    nfr=struct.unpack_from('<H',r,o)[0]
    bo=struct.unpack_from('<%dH'%nb,r,o+4)
    e=o+4+2*nb
    ev=[struct.unpack_from('<2H',r,e+4*k) for k in range(4)]
    p=e+16; extra=[]; cov=0
    while cov<nfr+1 and p+1<len(r):
        c,v=r[p],r[p+1]; p+=2; extra.append((c,v)); cov+=c
        if c==0: break
    return dict(nfr=nfr,bone_off=bo,events=ev,extra=extra,extra_ok=(cov==nfr+1),tracks_start=p)

def decode_anim(r,o,nb):
    """returns dict(nframes, bones=[None | (frames x 9 list, vis list)], events, extra)"""
    a=parse_anim(r,o,nb); n=a['nfr']+1; bones=[]
    for b in a['bone_off']:
        if b==0xffff: bones.append(None); continue
        co=struct.unpack_from('<10H',r,o+b)
        ch=[decode_stream(r,o+co[k],n,0 if k<3 else 1)[0] for k in range(9)]
        vis=decode_vis(r,o+co[9],n)[0]
        bones.append(([[ch[k][f] for k in range(9)] for f in range(n)],vis))
    a['nframes']=n; a['bones']=bones
    return a

def verify_bank(r):
    nb,tab=parse_bank(r)
    offs=sorted(set(x for x in tab if x!=0xffffffff))
    ptr=struct.unpack_from('<I',r,0x420)[0]
    ends=offs[1:]+[ptr if ptr>offs[-1] else len(r)]
    st=dict(table_entries=sum(1 for x in tab if x!=0xffffffff),blocks=len(offs),tracks=0,channels=0,ok=0,bad=0,gap0=0,gap1=0,gap3=0,gapother=0,extra_ok=0,extra_bad=0)
    for o,e in zip(offs,ends):
        a=parse_anim(r,o,nb); n=a['nfr']+1
        st['extra_ok' if a['extra_ok'] else 'extra_bad']+=1
        used=[b for b in a['bone_off'] if b!=0xffff]
        allst=sorted(set([o+c for b in used for c in struct.unpack_from('<10H',r,o+b)]+[o+b for b in used]+[e]))
        for b in used:
            st['tracks']+=1
            co=struct.unpack_from('<10H',r,o+b)
            for k,c in enumerate(co):
                st['channels']+=1
                p1,ok=(walk_stream(r,o+c,n) if k<9 else walk_vis(r,o+c,n))
                st['ok' if ok else 'bad']+=1
                nx=[s for s in allst if s>o+c]
                g=min(nx)-p1 if nx else 0
                st['gap%s'%(g if g in (0,1,3) else 'other')]+=1
    return st

def verify_all():
    tot={}
    for pk in ('PACK0.PAK','PACK1.PAK','PAKOMAKE.PAK','PAKVCAST.PAK'):
        t,_=pack(pk); nbk=0
        for i,x in enumerate(t):
            if not x: continue
            r=rec(pk,i)
            if is_bank(r):
                nbk+=1
                for k,v in verify_bank(r).items(): tot[k]=tot.get(k,0)+v
        print(pk,'banks:',nbk)
    # shapes + extra-channel indices
    nshape=0;bad=0;over=0;tags={}
    for pk in ('PACK0.PAK','PAKOMAKE.PAK'):
        t,_=pack(pk)
        for i,x in enumerate(t):
            if not x: continue
            r=rec(pk,i)
            if not is_bank(r): continue
            nb,tab=parse_bank(r)
            try: d,lists=parse_shapes(r)
            except Exception as ex: bad+=1; continue
            for l in lists:
                nshape+=1
                for tg,_ in l: tags[tg]=tags.get(tg,0)+1
            for o in set(x for x in tab if x!=0xffffffff):
                for c,v in parse_anim(r,o,nb)['extra']:
                    if v>=len(lists): over+=1
    print('shape lists',nshape,'parse errors',bad,'tag counts',tags,'extra-channel indices beyond the table',over)
    print(tot)
    return tot


# ---------------------------------------------------------------- collision/attack shapes (table @0x420)
SHAPE_SIZE={1:14,2:8,3:10}   # tag 1 = box (cx,cy,cz,w,h,d), 2 = point (x,y,z), 3 = sphere (cx,cy,cz,r); 0 = end of list
def parse_shapes(r):
    """returns (default_size(w,h,d), [list of [(tag,values)] per table index]); index 0 is the untagged default body size"""
    ptr=struct.unpack_from('<I',r,0x420)[0]; first=struct.unpack_from('<I',r,ptr)[0]; n=(first-ptr)//4
    ents=struct.unpack_from('<%dI'%n,r,ptr)
    out=[[]]
    for k in range(1,n):
        p=ents[k]; lst=[]
        while True:
            tg=struct.unpack_from('<H',r,p)[0]
            if tg==0: break
            if tg not in SHAPE_SIZE: raise ValueError('bad shape tag %d at %x'%(tg,p))
            lst.append((tg,struct.unpack_from('<%dh'%((SHAPE_SIZE[tg]-2)//2),r,p+2))); p+=SHAPE_SIZE[tg]
        out.append(lst)
    return struct.unpack_from('<3h',r,ents[0]),out

# ---------------------------------------------------------------- model + pose
def load_parts(model_rec_bytes):
    d=model_rec_bytes; t=struct.unpack_from('<16I',d,0x20); ends=list(t[1:])+[len(d)]
    parts=[]
    for k in range(16):
        seg=d[t[k]:ends[k]]; h=struct.unpack_from('<9h',seg,0); n=h[8]
        vs=len(seg)-n*16
        parts.append([struct.unpack_from('<3h',seg,vs+j*16) for j in range(n)])
    return parts

def rot_zyx(rx,ry,rz):
    a=2*math.pi/4096
    cx,sx,cy,sy,cz,sz=math.cos(rx*a),math.sin(rx*a),math.cos(ry*a),math.sin(ry*a),math.cos(rz*a),math.sin(rz*a)
    X=[[1,0,0],[0,cx,-sx],[0,sx,cx]];Y=[[cy,0,sy],[0,1,0],[-sy,0,cy]];Z=[[cz,-sz,0],[sz,cz,0],[0,0,1]]
    mm=lambda A,B:[[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mm(Z,mm(Y,X))   # libgte RotMatrix = Rz*Ry*Rx

def pose_part(verts,row):
    tx,ty,tz,rx,ry,rz,sx,sy,sz=row
    R=rot_zyx(rx,ry,rz); S=(sx/4096.0,sy/4096.0,sz/4096.0)
    out=[]
    for v in verts:
        u=[v[0]*S[0],v[1]*S[1],v[2]*S[2]]
        out.append([R[i][0]*u[0]+R[i][1]*u[1]+R[i][2]*u[2]+(tx,ty,tz)[i] for i in range(3)])
    return out

if __name__=='__main__':
    c=sys.argv[1] if len(sys.argv)>1 else 'verify'
    if c=='verify': verify_all()
    elif c=='info':
        r=rec(sys.argv[2],int(sys.argv[3])); nb,tab=parse_bank(r)
        print('bank',r[:12].split(b'\0')[0],'bones',nb,'anims',sum(1 for x in tab if x!=0xffffffff))
        print(' '.join('%d:%d'%(i,struct.unpack_from('<H',r,o)[0]+1) for i,o in enumerate(tab) if o!=0xffffffff))
    elif c=='shapes':
        r=rec(sys.argv[2],int(sys.argv[3])); d,l=parse_shapes(r); print('default size (w,h,d)',d)
        for k,x in enumerate(l): print(k,x)
    elif c=='pose':
        parts=load_parts(rec('PACK0.PAK',int(sys.argv[2]))); r=rec('PACK0.PAK',int(sys.argv[3])); nb,tab=parse_bank(r)
        ai=int(sys.argv[4]) if len(sys.argv)>4 else [i for i,o in enumerate(tab) if o!=0xffffffff][0]
        fr=int(sys.argv[5]) if len(sys.argv)>5 else 0
        a=decode_anim(r,tab[ai],nb); ys=[]
        for k,b in enumerate(a['bones']):
            if b is None: continue
            P=pose_part(parts[k],b[0][fr]); ys.append((k,min(p[1] for p in P),max(p[1] for p in P)))
        for k,lo,hi in ys: print('part %2d world y %.0f..%.0f'%(k,lo,hi))
