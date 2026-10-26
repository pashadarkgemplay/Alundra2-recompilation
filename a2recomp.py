#!/usr/bin/env python3
"""Translates the game's MIPS machine code to C, one module at a time.
usage: python a2recomp.py <module> [outdir]   (main, game00, event00, game01..game03, al22, al22b..al22d, p1_<N>)
Every function becomes void f_XXXXXXXX(CPU*c). Branch delay slots keep their order, calls and returns become C calls and returns,
and jumps through registers go to the runtime, which looks the target up by address."""
import sys,os,struct
sys.path.insert(0,'.')
from dasm import load
RN=['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7','s0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
def patch_al22(txt):
    """Debug switch (key F6 / env A2NOCULL=1..3): level>=1 skips the per-polygon off-screen rejects, >=2 also the back-face rejects, >=3 the per-object bounding-box cull. Level 0 = original behaviour."""
    import re
    old=("{int c_=c->r[6]==0; if(c_){goto L_80010414;}}\n" if False else None)
    pat=re.compile(r'((?:\{int c_=c->r\[(?:15|24|2|6|7|23)\]==0; if\(c_\)\{goto L_80010414;\}\}\n){6})')
    m=pat.search(txt); assert m, 'al22 object cull block not found'
    txt=txt[:m.start()]+'if(a2_nocull<3){\n'+m.group(1)+'}\n'+txt[m.end():]
    a=txt.index('void f_al22_80010de4(CPU*c){'); a=txt.index('void f_al22_80010de4(CPU*c){',a) if False else a
    b=txt.index('\n__attribute__',a+10) if '\n__attribute__' in txt[a+10:] else len(txt)
    body=txt[a:b]
    n=[0]
    def rep(mm):
        n[0]+=1
        if n[0]==1: return mm.group(0).replace('if(c_){','if(c_&&a2_nocull<2){')
        if n[0]==2: return '{ if(a2_nocull<2){f_al22_80011344(c);return;} }'
        if n[0]<=6: return mm.group(0).replace('if(c_){','if(c_&&a2_nocull<1){')
        return mm.group(0)
    body=re.sub(r'\{int c_=c->r\[1\]!=0; if\(c_\)\{f_al22_80011344\(c\);return;\}\}|\{ f_al22_80011344\(c\);return;\}|\{int c_=c->r\[1\]==(?:0|c->r\[13\]); if\(c_\)\{f_al22_80011344\(c\);return;\}\}',rep,body)
    assert n[0]>=6, n
    return txt[:a]+body+txt[b:]

AL22REC={'al22':76,'al22b':77,'al22c':78,'al22d':79}
def sx(v,b=16): v&=(1<<b)-1; return v-(1<<b) if v>>(b-1) else v
def _cut(which):
    p=os.path.join('recomp',which+'.bin')
    return open(p,'rb').read() if os.path.exists(p) else None
def load_mod(which):
    if which in AL22REC:
        d=_cut(which)
        if d is None:
            from lib import rec
            d=rec('PACK0.PAK',AL22REC[which])
        return 0x80010000,d
    if which.startswith('p1_'):
        d=_cut(which)
        if d is None:
            from lib import rec
            d=rec('PACK1.PAK',int(which[3:]))
        return 0x801493B0,d
    return load(which)
def is_prologue(w): return (w>>16)==0x27BD and (w&0x8000)!=0 and (w&0xFFFF)!=0
def discover(B,d):
    n=len(d)//4; W=struct.unpack('<%dI'%n,d[:n*4]); end=B+4*n; fs=set()
    ok=lambda a: B<=a<end and a%4==0
    for i,w in enumerate(W):
        if (w>>26)==3:
            t=((w&0x3FFFFFF)<<2)|0x80000000
            if ok(t): fs.add(t)
        if (w>>26)==2:
            t=((w&0x3FFFFFF)<<2)|0x80000000
            if ok(t) and (t-B)//4>=2 and W[(t-B)//4-2]==0x03E00008: fs.add(t)
        if ok(w) and is_prologue(W[(w-B)//4]): fs.add(w)
        if ok(w) and (w-B)//4>=2 and W[(w-B)//4-2]==0x03E00008: fs.add(w)
        if (w>>26)==0x0F and i+1<n:
            nx=W[i+1]; rt=(w>>16)&31
            if (nx>>26) in (9,13) and ((nx>>21)&31)==rt:
                a=((w&0xFFFF)<<16)+(sx(nx) if (nx>>26)==9 else (nx&0xFFFF)); a&=0xFFFFFFFF
                if ok(a) and (is_prologue(W[(a-B)//4]) or ((a-B)//4>=2 and W[(a-B)//4-2]==0x03E00008)): fs.add(a)
    if B==0x801493B0 and n>2 and W[0]<64:
        k=1
        while k<n and ok(W[k]): k+=1
        if k<n-1 and is_prologue(W[k]): fs.add(B+4*k)
    if os.path.exists('./seeds.txt'):
        for ln in open('./seeds.txt'):
            p=ln.split()
            if len(p)==2 and p[0]==CUR['which'] and ok(int(p[1],16)) and int(p[1],16)%4==0: fs.add(int(p[1],16))
    fs.add(B)
    if B==0x80017000: fs.add(0x8003cf4c)
    return sorted(fs),W
def R(r): return '0' if r==0 else 'c->r[%d]'%r
def W_(r,e): return '' if r==0 else 'c->r[%d]=%s;'%(r,e)
def ea(w): rs=(w>>21)&31; off=sx(w); return '(%s+(%d))'%(R(rs),off) if rs else '(%d)'%(off&0xFFFFFFFF)
def simple(w,A,st):
    """translate one non-branch instruction to a C statement ('' for nop)"""
    op=w>>26; rs=(w>>21)&31; rt=(w>>16)&31; rd=(w>>11)&31; sh=(w>>6)&31; fn=w&63; imm=w&0xFFFF; s=sx(w)
    if w==0: return ''
    if op==0:
        t={0x00:lambda:W_(rd,'%s<<%d'%(R(rt),sh)),0x02:lambda:W_(rd,'%s>>%d'%(R(rt),sh)),0x03:lambda:W_(rd,'(uint32_t)((int32_t)%s>>%d)'%(R(rt),sh)),
           0x04:lambda:W_(rd,'%s<<(%s&31)'%(R(rt),R(rs))),0x06:lambda:W_(rd,'%s>>(%s&31)'%(R(rt),R(rs))),0x07:lambda:W_(rd,'(uint32_t)((int32_t)%s>>(%s&31))'%(R(rt),R(rs))),
           0x10:lambda:W_(rd,'c->hi'),0x11:lambda:'c->hi=%s;'%R(rs),0x12:lambda:W_(rd,'c->lo'),0x13:lambda:'c->lo=%s;'%R(rs),
           0x18:lambda:'{int64_t p=(int64_t)(int32_t)%s*(int32_t)%s;c->lo=(uint32_t)p;c->hi=(uint32_t)(p>>32);}'%(R(rs),R(rt)),
           0x19:lambda:'{uint64_t p=(uint64_t)%s*%s;c->lo=(uint32_t)p;c->hi=(uint32_t)(p>>32);}'%(R(rs),R(rt)),
           0x1A:lambda:'{int32_t a_=(int32_t)%s,b_=(int32_t)%s;if(b_==0){c->lo=a_<0?1:-1;c->hi=a_;}else if(a_==INT32_MIN&&b_==-1){c->lo=a_;c->hi=0;}else{c->lo=a_/b_;c->hi=a_%%b_;}}'%(R(rs),R(rt)),
           0x1B:lambda:'{uint32_t a_=%s,b_=%s;if(b_==0){c->lo=-1;c->hi=a_;}else{c->lo=a_/b_;c->hi=a_%%b_;}}'%(R(rs),R(rt)),
           0x20:lambda:W_(rd,'%s+%s'%(R(rs),R(rt))),0x21:lambda:W_(rd,'%s+%s'%(R(rs),R(rt))),0x22:lambda:W_(rd,'%s-%s'%(R(rs),R(rt))),0x23:lambda:W_(rd,'%s-%s'%(R(rs),R(rt))),
           0x24:lambda:W_(rd,'%s&%s'%(R(rs),R(rt))),0x25:lambda:W_(rd,'%s|%s'%(R(rs),R(rt))),0x26:lambda:W_(rd,'%s^%s'%(R(rs),R(rt))),0x27:lambda:W_(rd,'~(%s|%s)'%(R(rs),R(rt))),
           0x2A:lambda:W_(rd,'(int32_t)%s<(int32_t)%s'%(R(rs),R(rt))),0x2B:lambda:W_(rd,'%s<%s'%(R(rs),R(rt))),
           0x0C:lambda:'c_syscall(c,%d);'%((w>>6)&0xFFFFF),0x0D:lambda:'c_break(c,%d);'%((w>>6)&0xFFFFF)}
        return t[fn]() if fn in t else None
    if op==9 or op==8: return W_(rt,'%s+(%d)'%(R(rs),s)) if rs else W_(rt,'%d'%(s&0xFFFFFFFF))
    if op==0x0A: return W_(rt,'(int32_t)%s<(%d)'%(R(rs),s))
    if op==0x0B: return W_(rt,'%s<%du'%(R(rs),s&0xFFFFFFFF))
    if op==0x0C: return W_(rt,'%s&0x%x'%(R(rs),imm))
    if op==0x0D: return W_(rt,'%s|0x%x'%(R(rs),imm))
    if op==0x0E: return W_(rt,'%s^0x%x'%(R(rs),imm))
    if op==0x0F: return W_(rt,'0x%08xu'%(imm<<16))
    L={0x20:'(uint32_t)(int32_t)(int8_t)rd8(%s)',0x21:'(uint32_t)(int32_t)(int16_t)rd16(%s)',0x23:'rd32(%s)',0x24:'rd8(%s)',0x25:'rd16(%s)'}
    if op in L: return W_(rt,L[op]%ea(w))
    if op==0x22: return W_(rt,'lwl(%s,%s)'%(R(rt),ea(w)))
    if op==0x26: return W_(rt,'lwr(%s,%s)'%(R(rt),ea(w)))
    if op==0x28: return 'wr8(%s,%s);'%(ea(w),R(rt))
    if op==0x29: return 'wr16(%s,%s);'%(ea(w),R(rt))
    if op==0x2B: return 'wr32(%s,%s);'%(ea(w),R(rt))
    if op==0x2A: return 'swl(%s,%s);'%(ea(w),R(rt))
    if op==0x2E: return 'swr(%s,%s);'%(ea(w),R(rt))
    if op==0x32: return 'gte_mtc2(c,%d,rd32(%s));'%(rt,ea(w))
    if op==0x3A: return 'wr32(%s,gte_mfc2(c,%d));'%(ea(w),rt)
    if op==0x10:
        if rs==0: return W_(rt,'cop0_mfc0(c,%d)'%rd)
        if rs==4: return 'cop0_mtc0(c,%d,%s);'%(rd,R(rt))
        if w&0x3F==0x10: return 'cop0_rfe(c);'
        return None
    if op==0x12:
        if w&0x2000000: return 'gte_cmd(c,0x%x);'%(w&0x1FFFFFF)
        if rs==0: return W_(rt,'gte_mfc2(c,%d)'%rd)
        if rs==2: return W_(rt,'gte_cfc2(c,%d)'%rd)
        if rs==4: return 'gte_mtc2(c,%d,%s);'%(rd,R(rt))
        if rs==6: return 'gte_ctc2(c,%d,%s);'%(rd,R(rt))
        return None
    return None
def is_branch(w):
    op=w>>26
    return op in (1,2,3,4,5,6,7) or (op==0 and (w&63) in (8,9))

def _const_reg(W,idx,k,reg,start):
    """constant held in `reg` just before address k: `lui reg,hi [; addiu/ori reg,reg,lo]` (the pair may be separated by other code).  None if not found."""
    lo=0; seen_lo=False; lo_or=False
    for j in range(k-4,max(start,k-48)-4,-4):
        u=W[idx(j)]; op=u>>26
        if op in (9,0xD) and ((u>>16)&31)==reg and ((u>>21)&31)==reg and not seen_lo:
            lo=sx(u) if op==9 else (u&0xFFFF); seen_lo=True; continue
        if op==0x0F and ((u>>16)&31)==reg:
            return (((u&0xFFFF)<<16)+lo)&0xFFFFFFFF
        wrt=None
        if op in (0x23,0x20,0x24,0x21,0x25,0x09,0x0D,0x0C,0x0A,0x0B,0x08,0x0E) and ((u>>16)&31)==reg: return None
        if op==0 and (u&63) not in (8,9,0x18,0x19,0x1A,0x1B,0x11,0x13) and ((u>>11)&31)==reg: return None
    return None

def jump_table(B,W,idx,a,start,end):
    """switch jump table: `lui rb,hi ; addu rb,rb,idx ; lw rs,lo(rb) ; nop ; jr rs`, or the base built as `lui/addiu rc` and `addu rb,idx,rc ; lw rs,off(rb)`.
    Returns the list of code addresses inside [start,end) or None."""
    w=W[idx(a)]; rs=(w>>21)&31
    for k in range(a-4,max(start,a-40)-4,-4):
        v=W[idx(k)]
        if (v>>26)==0x23 and ((v>>16)&31)==rs:
            rb=(v>>21)&31; off=sx(v)
            bases=[]
            for j in range(k-4,max(start,k-40)-4,-4):
                u=W[idx(j)]
                if (u>>26)==0x0F and ((u>>16)&31)==rb:
                    bases.append((((u&0xFFFF)<<16)+off)&0xFFFFFFFF); break
                if u>>26==0 and (u&63)==0x21 and ((u>>11)&31)==rb:
                    for rr in ((u>>21)&31,(u>>16)&31):
                        c=_const_reg(W,idx,j,rr,start)
                        if c is not None and 0x80000000<=c<0x80200000: bases.append((c+off)&0xFFFFFFFF); break
                    break
            for base in bases[:1]:
                out=[]
                for i in range(256):
                    p=base+4*i
                    if not (B<=p<B+4*len(W)): break
                    t=W[(p-B)//4]
                    if t%4 or not (start<=t<end): break
                    out.append(t)
                return out if len(out)>=2 else None
            return None
    return None

P1IDS=(1,2,11,13,17,18,21,23,24,25,26,30,31,32,33,34,35)
OVERLAYS=('game00','event00','game01','game02','game03')+tuple('p1_%d'%i for i in P1IDS)+tuple(AL22REC)
EXE_RANGE=(0x80017000,0x80017000+303104); AL22_RANGE=(0x80010000,0x80010000+26968)
CUR={'which':'main','lo':0,'hi':0}
def fname(t):
    """C symbol for code address t as seen from the module being translated: overlay-local names are prefixed (the five overlays share 0x80084A98..);
    exe and AL22 names are global; anything else is reached through ov_call() (current overlay table)."""
    if CUR['lo']<=t<CUR['hi']: return 'f_%s_%08x'%(CUR['which'],t) if CUR['which'] in OVERLAYS else 'f_%08x'%t
    if EXE_RANGE[0]<=t<EXE_RANGE[1]: return 'f_%08x'%t
    return None
def call_stmt(t):
    n=fname(t)
    return '%s(c);'%n if n else 'ov_call(c,0x%08x);'%t
def tr_function(B,W,start,limit):
    """translate [start, end) ; returns (C source, end address, stats)"""
    n=len(W); idx=lambda a:(a-B)//4
    if start+12<=limit:
        w0,w1,w2=W[idx(start)],W[idx(start+4)],W[idx(start+8)]
        if (w0>>16)==0x240A and (w0&0xFFFF) in (0xA0,0xB0,0xC0) and w1==0x01400008 and (w2>>16)==0x2409:
            return '__attribute__((weak)) void %s(CPU*c){c_bios(c,0x%x,0x%x);}'%(fname(start),w0&0xFFFF,w2&0xFFFF),start+12,dict(insn=3,unk=0)
    a=start; maxt=start; end=None
    while a<limit:
        w=W[idx(a)]; op=w>>26
        if op in (4,5,6,7) or (op==1 and ((w>>16)&31) in (0,1,16,17)):
            t=a+4+4*sx(w)
            if t>maxt and t<limit: maxt=t
        if op==2:
            t=((w&0x3FFFFFF)<<2)|(a&0xF0000000)
            if t>maxt and t<limit and t>a: maxt=t
        if op==0 and (w&63)==8 and ((w>>21)&31)!=31:
            jt=jump_table(B,W,idx,a,start,B+4*n)
            if jt:
                mt=max(jt)
                if mt>maxt: maxt=mt
                if mt+4>limit and mt-start<0x8000: limit=B+4*n
        if w==0x03E00008 and a>=maxt: end=a+8; break
        if op==2 and a>=maxt:
            t=((w&0x3FFFFFF)<<2)|(a&0xF0000000)
            if not (start<=t<limit) or t<start: end=a+8; break
        a+=4
    if end is None and limit-start>=16:
        w=W[idx(limit)-2]
        if (W[idx(limit)-1]&0xFC00003F)==0x0D: end=limit
        elif (w>>26)==2 or (w>>26)==0 and (w&63)==8 or (w>>26)==4 and ((w>>21)&31)==0 and ((w>>16)&31)==0: end=limit
    fall=False
    if end is None and limit-start>=4 and limit<=B+4*len(W) and limit-start<=0x2000:
        end=limit; fall=True
    if end is None: return None,limit,None
    targets=set()
    for a in range(start,end,4):
        w=W[idx(a)]; op=w>>26
        if op in (4,5,6,7) or (op==1 and ((w>>16)&31) in (0,1,16,17)):
            t=a+4+4*sx(w)
            if start<=t<end: targets.add(t)
        if op==2:
            t=((w&0x3FFFFFF)<<2)|(a&0xF0000000)
            if start<=t<end: targets.add(t)
        if op==0 and (w&63)==8 and ((w>>21)&31)!=31:
            jt=jump_table(B,W,idx,a,start,end)
            if jt: targets.update(jt)
    out=['__attribute__((weak)) void %s(CPU*c){'%fname(start)]; st=dict(insn=0,unk=0); a=start
    if start==0x8003b148: out.append('{extern void a2_vsync_hook(unsigned); a2_vsync_hook(c->r[4]);}')
    def slot(sa):
        if sa>=end: return ''
        sw=W[idx(sa)]
        if is_branch(sw): st['unk']+=1; return '/*BRANCH IN DELAY SLOT*/'
        r=simple(sw,sa,st); 
        if r is None: st['unk']+=1; return '/*UNK %08x*/'%sw
        return r
    while a<end:
        w=W[idx(a)]; op=w>>26; rs=(w>>21)&31; rt=(w>>16)&31; st['insn']+=1
        lab='L_%08x:;'%a if a in targets else ''
        def tgt(t,cond=None):
            if start<=t<end: return ('a2_tick(c,0x%08x);'%a if t<=a else '')+'goto L_%08x;'%t
            return call_stmt(t)+'return;'
        if is_branch(w):
            sl=slot(a+4); body=None
            if op==2: body='{%s %s}'%(sl,tgt(((w&0x3FFFFFF)<<2)|(a&0xF0000000)))
            elif op==3: body='{c->r[31]=0x%x;%s %s}'%(a+8,sl,call_stmt(((w&0x3FFFFFF)<<2)|(a&0xF0000000)))
            elif op==0 and (w&63)==8:
                if rs==31: body='{%s return;}'%sl
                else:
                    jt=jump_table(B,W,idx,a,start,end)
                    if jt: body='{uint32_t t_=%s;%s switch(t_){%s default: c_jump(c,t_);return;}}'%(R(rs),sl,''.join('case 0x%08x: goto L_%08x;'%(x,x) for x in sorted(set(jt))))
                    else: body='{uint32_t t_=%s;%s c_jump(c,t_);return;}'%(R(rs),sl)
            elif op==0 and (w&63)==9: body='{uint32_t t_=%s;%s %s c_call(c,t_);}'%(R(rs),sl,W_((w>>11)&31,'0x%x'%(a+8)) if True else '')
            else:
                t=a+4+4*sx(w)
                if op==4: cnd='%s==%s'%(R(rs),R(rt))
                elif op==5: cnd='%s!=%s'%(R(rs),R(rt))
                elif op==6: cnd='(int32_t)%s<=0'%R(rs)
                elif op==7: cnd='(int32_t)%s>0'%R(rs)
                else:
                    k=rt; cnd=('(int32_t)%s<0'%R(rs)) if k in (0,16) else ('(int32_t)%s>=0'%R(rs))
                link='c->r[31]=0x%x;'%(a+8) if (op==1 and k in (16,17)) else ''
                body='{int c_=%s;%s%s if(c_){%s}}'%(cnd,link,sl,tgt(t))
            out.append(lab+body)
            if (a+4) in targets: out.append('goto N_%08x; L_%08x:;%s N_%08x:;'%(a+8,a+4,slot(a+4),a+8))
            a+=8; st['insn']+=1; continue
        r=simple(w,a,st)
        if r is None: st['unk']+=1; r='/*UNK %08x*/'%w
        out.append(lab+r); a+=4
    if fall: out.append('%s'%call_stmt(end))
    out.append('}'); return '\n'.join(out),end,st
def run(which,outdir):
    B,d=load_mod(which); CUR.update(which=which,lo=B,hi=B+len(d)//4*4); fs,W=discover(B,d); n=len(W); lim=B+4*n
    CUR.update(which=which,lo=B,hi=lim)
    os.makedirs(outdir,exist_ok=True); body=[]; tot=dict(insn=0,unk=0); done=[]; covered=set(); datar=[]
    def translate_all(fs):
        for k,f in enumerate(fs):
            if f in done: continue
            nxt=fs[k+1] if k+1<len(fs) else lim
            try: src,e,st=tr_function(B,W,f,nxt)
            except Exception as ex: print('fail',hex(f),ex); continue
            if src is None or st['unk']>0: datar.append((f,e)); continue
            body.append(src); done.append(f); tot['insn']+=st['insn']; tot['unk']+=st['unk']
            for a in range(f,e,4): covered.add(a)
    translate_all(fs)
    extra=sorted(B+4*i for i in range(2,n) if W[i-2]==0x03E00008 and W[i]!=0 and (B+4*i) not in covered and (B+4*i) not in set(fs))
    if extra:
        fs2=sorted(set(fs)|set(extra)); datar.clear(); translate_all(fs2)
        print('second pass: %d extra entries after `jr ra`'%len(extra))
    import re as _re
    while True:
        have=set(fname(f) for f in done); drop=[]
        for k,src in enumerate(body):
            m=_re.search(r'\n(f_(?:\w+_)?[0-9a-f]{8})\(c\);\n\}$',src)
            if m and m.group(1) not in have and B<=int(m.group(1)[-8:],16)<lim and src.count('\n')<400: drop.append(k)
        if not drop: break
        for k in reversed(drop): del body[k]; del done[k]
    hdr='#include "a2rt.h"\n'+''.join('void %s(CPU*c);\n'%fname(f) for f in done)
    ext=set()
    import re
    for b in body: ext.update(re.findall(r'\b(f_(?:\w+_)?[0-9a-f]{8})\(c\)',b))
    mine=set(fname(f) for f in done)
    decl='#ifdef A2_STANDALONE   /* single-module tests: undefined callees become c_missing stubs; the real build links all modules and leaves them undefined */\n'+''.join('__attribute__((weak)) void %s(CPU*c){c_missing(c,0x%s);}\n'%(x,x[-8:]) for x in sorted(ext-mine))+'#else\n'+''.join('void %s(CPU*c);\n'%x for x in sorted(ext-mine))+'#endif\n'
    done.sort()
    tab='const FuncEntry %s_functions[]={\n'%which+''.join(' {0x%08x,%s},\n'%(f,fname(f)) for f in done)+' {0,0}};\n'
    out_txt=hdr+decl+'\n'+'\n'.join(body)+'\n'+tab
    if which=='al22': out_txt=patch_al22(out_txt).replace('#include "a2rt.h"\n','#include "a2rt.h"\nextern int a2_nocull;\n',1)
    open(os.path.join(outdir,which+'.c'),'w').write(out_txt)
    print('data blobs skipped:',[('%08x'%a,(b-a)) for a,b in datar]); print('%s: base %08x, %d bytes, %d functions, %d instructions translated, %d unhandled, %d external callees (other module / unknown)'%(which,B,len(d),len(done),tot['insn'],tot['unk'],len(ext-mine)))
    return len(done),tot
if __name__=='__main__':
    run(sys.argv[1],sys.argv[2] if len(sys.argv)>2 else 'recomp')
