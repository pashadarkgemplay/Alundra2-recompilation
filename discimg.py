"""Reading a disc image: .zip and .7z unpacking, and the file list of the ISO9660 volume inside a raw 2352-byte-sector .bin."""
import os, struct, zipfile, lzma, shutil

SMALL = ['SLUS_010.17', 'SYSTEM.CNF', 'GAME00.BIN', 'GAME01.BIN', 'GAME02.BIN', 'GAME03.BIN', 'EVENT00.BIN', 'PACK1.PAK']

def extract_zip(path,out):
    z=zipfile.ZipFile(path); bins=[i for i in z.infolist() if i.filename.lower().endswith('.bin')]
    if not bins: raise SystemExit('no .bin in zip: '+str([i.filename for i in z.infolist()]))
    info=max(bins,key=lambda i:i.file_size)
    with z.open(info) as src, open(out,'wb') as dst: shutil.copyfileobj(src,dst,1<<22)

def extract_7z(path,out):
    import sevenz_min as S
    f=open(path,'rb'); f.seek(12); nho,nhs,_=struct.unpack('<QQI',f.read(20)); f.seek(32+nho); h=f.read(nhs)
    if h[0]==0x17:                               # encoded header
        r=S.R(h); r.u8(); info=S.parse_streams(r); fo=info['folders'][0]; c=fo['coders'][0]
        f.seek(32+info['pack_pos']); pk=f.read(info['pack_sizes'][0]); h=S.lzma_dec(c[1],pk,fo['unpack'][0])
    r=S.R(h); assert r.u8()==1 and r.u8()==4,'unexpected 7z header'
    info={}
    try: info=S.parse_streams(r)
    except Exception: pass
    fo=info['folders'][0]; c=fo['coders'][0]; total=fo['unpack'][-1]
    first=(info.get('sub_sizes') or [total])[0]
    if c[0]==b'\x03\x01\x01': pb=c[1][0]; lc=pb%9; pb//=9; lp=pb%5; pb//=5; ds=struct.unpack('<I',c[1][1:5])[0]; flt=[{'id':lzma.FILTER_LZMA1,'dict_size':ds,'lc':lc,'lp':lp,'pb':pb}]
    elif c[0]==b'\x21':
        code=c[1][0]; ds=0xFFFFFFFF if code==40 else (2|(code&1))<<(code//2+11); flt=[{'id':lzma.FILTER_LZMA2,'dict_size':ds}]
    else: raise SystemExit('unsupported 7z coder '+c[0].hex())
    d=lzma.LZMADecompressor(format=lzma.FORMAT_RAW,filters=flt); f.seek(32+info['pack_pos']); left=info['pack_sizes'][0]; done=0
    with open(out,'wb') as o:
        while left>0 and done<first:
            ch=f.read(min(1<<20,left)); left-=len(ch); data=d.decompress(ch); data=data[:first-done]; o.write(data); done+=len(data)

def parse_iso(binpath):
    raw=open(binpath,'rb')
    def sec(n): raw.seek(n*2352+24); return raw.read(2048)
    pvd=sec(16); assert pvd[1:6]==b'CD001','not ISO9660 mode2/2352'
    root=pvd[156:190]; lba=struct.unpack('<I',root[2:6])[0]; ln=struct.unpack('<I',root[10:14])[0]; out=[]
    def walk(lba,ln,path):
        data=b''.join(sec(lba+i) for i in range((ln+2047)//2048)); p=0
        while p<ln:
            l=data[p]
            if l==0: p=(p//2048+1)*2048; continue
            rec=data[p:p+l]; p+=l; fl=rec[25]; nl=rec[32]; name=rec[33:33+nl].decode('latin1')
            if name in('\x00','\x01'): continue
            el=struct.unpack('<I',rec[2:6])[0]; es=struct.unpack('<I',rec[10:14])[0]
            if fl&2: walk(el,es,path+name+'/')
            else: out.append((path+name,el,es))
    walk(lba,ln,'/'); return out
