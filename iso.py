import json,struct,os
_W=os.environ.get('A2WORK','.')
files={p.split('/')[-1].split(';')[0]:(l,s) for p,l,s in json.load(open(_W+'/files.json'))}
_raw=open(os.environ.get('A2DISC',_W+'/alundra2.bin'),'rb')
def rd(name,off,n):
    lba,sz=files[name]; n=min(n,sz-off); out=bytearray(); a=off
    while len(out)<n:
        s=a//2048; o=a%2048
        _raw.seek((lba+s)*2352+24); out+=_raw.read(2048)[o:]; a=(s+1)*2048
    return bytes(out[:n])
def table(name):
    lba,sz=files[name]
    first=struct.unpack('<I',rd(name,0,4))[0]
    t=struct.unpack('<%dI'%(first//4),rd(name,0,first))
    return t,sz
