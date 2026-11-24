import sys,os,struct,math; HERE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,HERE); sys.path.insert(0,os.path.dirname(HERE))
import numpy as np, modelparse as M, animtool as A
from lib import rec
from PIL import Image, ImageDraw
def render(model,bank,anim,frame,size=340,yaw=0.0):
    parts=M.parse_model(model); nb,tab=A.parse_bank(bank); a=A.decode_anim(bank,tab[anim],nb); polys=[]; allp=[]
    ca,sa=math.cos(yaw),math.sin(yaw)
    for k,p in enumerate(parts):
        bone=a['bones'][k] if k<len(a['bones']) else None
        row=[0,0,0,0,0,0,4096,4096,4096] if bone is None else bone[0][min(frame,a['nframes']-1)]
        if bone is not None and bone[1][min(frame,a['nframes']-1)]: continue
        P=A.pose_part([v[:3] for v in p['verts']],row); P=[(x*ca+z*sa,y,-x*sa+z*ca) for x,y,z in P]; allp+=P
        for q in p['polys']:
            if max(q['v'])>=len(P): continue
            col=q['col'][0]; polys.append((sum(P[i][2] for i in q['v'])/len(q['v']),[P[i] for i in q['v']],(col&255,(col>>8)&255,(col>>16)&255)))
    return polys,allp
def draw(polys,allp,size,box=None):
    xs=[p[0] for p in allp]; ys=[p[1] for p in allp]
    if box is None: box=(min(xs),max(xs),min(ys),max(ys))
    sc=(size-16)/max(box[1]-box[0],box[3]-box[2],1); cx=(box[0]+box[1])/2
    im=Image.new('RGB',(size,size),(30,30,50)); d=ImageDraw.Draw(im)
    for z,V,c in sorted(polys,key=lambda t:-t[0]): d.polygon([((v[0]-cx)*sc+size/2,(v[1]-box[2])*sc+8) for v in V],fill=c)
    return im
if __name__=='__main__':
    names=sys.argv[1].split(','); anims=[int(x) for x in sys.argv[2].split(',')] if len(sys.argv)>2 else [3]
    bank=rec('PACK0.PAK',69); tiles=[]
    flint=rec('PACK0.PAK',68)
    models=[('flint',flint)]+[(n,__import__('hdc').crypt(open(os.environ.get('A2MODOUT','D:/A2/work/modout')+'/hero_%s.bin'%n,'rb').read()[8:],0xA2480001)) for n in names]
    for ai in anims:
        for nm,m in models:
            polys,allp=render(m,bank,ai,0)
            ref=render(flint,bank,ai,0)[1]; xs=[p[0] for p in ref]; ys=[p[1] for p in ref]; box=(min(xs)-30,max(xs)+30,min(ys)-30,max(ys)+30)
            im=draw(polys,allp,340,box); ImageDraw.Draw(im).text((3,3),'%s anim %d'%(nm,ai),fill=(255,255,0)); tiles.append(im)
    W=len(models); c=Image.new('RGB',(340*W,340*len(anims))); [c.paste(t,((i%W)*340,(i//W)*340)) for i,t in enumerate(tiles)]; c.save(os.environ.get('A2PREV','D:/A2/work/hero_prev.png'))
