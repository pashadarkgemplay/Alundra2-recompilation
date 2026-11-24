"""Alundra 2 model record parser (characters/monsters/weapons).  Verified on ALL 555 parts of every model record in PACK0/PAKOMAKE (session 57: 555/555 consumed,
polygon count = header count).  Record: name[0x20], u32 part_offsets[..] @0x20 (0xFFFFFFFF/0 ends), u32 @0x128 = number of 'external' vertices, part count u32 @0x12C.
Part: s16 hdr[9] = (0, bbox min xyz, bbox max xyz, npoly, nvert); u32 @+0x14 = absolute (in record) offset of the vertex block; elements from +0x18;
vertices = the last nvert*16 bytes: s16 x,y,z, nx,ny,nz (4096=1.0), then byte 12 = EXPORT SLOT (0 or 60+k: the game copies this transformed vertex into external slot k, session 58), byte 13 = 0/1 (meaning unknown), 2 zero bytes.
ELEMENT (11 words): flags (low 16 bits 0..0x20D, high 16 bits sometimes a signed number, meaning unknown) | idx = 4 bytes v0 v1 v2 v3 (v2==v3 -> triangle; vertices are in CONTOUR
order) | 4 colour words 0x00BBGGRR, the first one carries the GPU command (0x30/0x38 Gouraud tri/quad, 0x34/0x3C textured; +2 = semi-transparent) |
3 words: untextured -> 0, 0x01000000, 0; textured -> (u0 v0 clut) (u1 v1 tpage) (u2 v2 u3 v3) | 0xE1xxxxxx/0xE2xxxxxx (GPU texpage word) | 0xFFFFFF0t (t = 0..3, unknown).
DECAL packet (9 words, follows an element and REUSES its idx): 4 colour words (cmd 0x34/0x36/0x3C/0x3E) + 3 UV words + E1/E2 word + 0xFFFFFF0t.
polygon index >= nvert = 'external' vertex = the vertex of ANY part of the same model whose export byte equals that index (CONFIRMED by the game code, session 58)."""
import struct,sys
sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
def part_table(m):
    t=struct.unpack_from('<64I',m,0x20); out=[]
    for x in t:
        if x in (0,0xFFFFFFFF): break
        out.append(x)
    return out
def decode_uv(w,cmd):
    """GP0 textured packet tail: (u0,v0,clut) (u1,v1,tpage) (u2,v2,u3,v3)"""
    a,b,c=w
    return dict(uv=[(a&255,(a>>8)&255),(b&255,(b>>8)&255),(c&255,(c>>8)&255),((c>>16)&255,(c>>24)&255)],clut=a>>16,tpage=b>>16)
def parse_part(m,off,end):
    seg=m[off:end]; h=struct.unpack_from('<9h',seg,0); npoly,nvert=h[7],h[8]
    vo=len(seg)-nvert*16
    verts=[struct.unpack_from('<6h',seg,vo+16*i) for i in range(nvert)]
    export=[seg[vo+16*i+12] for i in range(nvert)]      # byte 12 of a vertex: 0 or 60+k = the game copies this transformed vertex to external slot k (AL22 0x800133A4)
    ws=struct.unpack_from('<%dI'%((vo-0x18)//4),seg,0x18); p=0; polys=[]; cur=None; bad=0
    isc=lambda c: (c>>24)&0xE0==0x20            # GPU polygon command byte 0x20..0x3F (bit2 textured, bit1 semi-transparent, bit0 raw texture)
    term=lambda t: (t&0xFFFFFF00)==0xFFFFFF00   # terminator 0xFFFFFF0x (low byte differs: 00..03, meaning unknown)
    while p<len(ws):
        w=ws[p]
        if p+11<=len(ws) and isc(ws[p+2]) and (ws[p+9]>>24) in (0xE1,0xE2) and term(ws[p+10]):      # element: flags idx c0..c3 w6..w8 E1/E2 TERM
            idx=ws[p+1]; v=[(idx>>(8*k))&255 for k in range(4)]; cmd=ws[p+2]>>24
            q=dict(v=v[:3] if v[2]==v[3] else v,cmd=cmd,col=[ws[p+2+i]&0xffffff for i in range(4)],flags=w,tex=[],term=ws[p+10]&255)
            if cmd&4: q['uv']=decode_uv(ws[p+6:p+9],cmd)
            polys.append(q); cur=q; p+=11
        elif isc(w) and (w>>24)&4 and cur is not None and p+9<=len(ws) and (ws[p+7]>>24) in (0xE1,0xE2) and term(ws[p+8]):  # decal on the previous idx
            cur['tex'].append(dict(cmd=w>>24,col=[ws[p+i]&0xffffff for i in range(4)],uv=decode_uv(ws[p+4:p+7],w>>24),term=ws[p+8]&255)); p+=9
        else:
            bad+=1; break
    return dict(hdr=h,verts=verts,export=export,polys=polys,consumed=(p==len(ws)),n_ok=len(polys)==npoly,bad=bad)
def parse_model(m):
    """list of parsed parts; a malformed part becomes an empty dict with 'error' (never raises)"""
    t=part_table(m); ends=t[1:]+[len(m)]; out=[]
    for k in range(len(t)):
        try: out.append(parse_part(m,t[k],ends[k]))
        except Exception as e:
            out.append(dict(hdr=None,verts=[],polys=[],consumed=False,n_ok=False,bad=1,error=repr(e)))
    return out
def is_model(r):
    """character/monster model record: name, part table, part count @0x12C"""
    if len(r)<0x130: return False
    n=struct.unpack_from('<I',r,0x12C)[0]
    t=part_table(r)
    return 1<=n<=64 and len(t)==n and all(0x130<=x<len(r) for x in t) and t==sorted(t)
def selftest():
    import pack_tool as P, iso_tool as I
    tot=ok=ext=0; bads=[]
    for pk in ('PACK0.PAK','PAKOMAKE.PAK'):
        d=I.read_file('/home/claude/work/alundra2.bin','DATA/'+pk); t,nz,recs=P.parse_pak(d)
        for i in range(768):
            if not t[i]: continue
            r=recs[t[i]]
            if not is_model(r): continue
            for k,p in enumerate(parse_model(r)):
                tot+=1
                if p['consumed'] and p['n_ok']:
                    ok+=1
                    if any(max(q['v'])>=len(p['verts']) for q in p['polys']): ext+=1
                else: bads.append((pk[:5],i,r[:10].split(b'\0')[0],k))
    print('parts',tot,'parsed exactly',ok,'(of them with external-vertex indices: %d)'%ext,'failed',len(bads)); print(bads[:20])
if __name__=='__main__': selftest()
