import sys,struct
sys.path.insert(0,'.')
from mips import dis
def load(which):
    if which=='main': d=open('./extracted/SLUS_010.17','rb').read()[0x800:]; return 0x80017000,d
    d=open('./extracted/%s.BIN'%which.upper(),'rb').read(); return 0x80084a98,d
def run(which,start,n):
    B,d=load(which)
    for i in range(n):
        a=start+4*i; o=a-B
        if o<0 or o+4>len(d): break
        w=struct.unpack('<I',d[o:o+4])[0]; print('%08x: %08x  %s'%(a,w,dis(w,a)))
if __name__=='__main__':
    run(sys.argv[1],int(sys.argv[2],16),int(sys.argv[3]))
