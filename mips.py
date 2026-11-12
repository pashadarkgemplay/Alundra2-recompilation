import struct
R=['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7','s0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
def s16(x): return x-65536 if x&0x8000 else x
def dis(w,pc):
    op=w>>26; rs=(w>>21)&31; rt=(w>>16)&31; rd=(w>>11)&31; sh=(w>>6)&31; fn=w&63; im=w&0xffff; si=s16(im)
    if w==0: return 'nop'
    if op==0:
        m={0:'sll',2:'srl',3:'sra'}
        if fn in m: return f'{m[fn]} {R[rd]},{R[rt]},{sh}'
        if fn==8: return f'jr {R[rs]}'
        if fn==9: return f'jalr {R[rd]},{R[rs]}'
        if fn==12: return 'syscall'
        if fn==13: return 'break'
        if fn in(16,18): return f'{"mfhi" if fn==16 else "mflo"} {R[rd]}'
        if fn in(17,19): return f'{"mthi" if fn==17 else "mtlo"} {R[rs]}'
        if fn in(24,25,26,27): return f'{["mult","multu","div","divu"][fn-24]} {R[rs]},{R[rt]}'
        m={4:'sllv',6:'srlv',7:'srav'}
        if fn in m: return f'{m[fn]} {R[rd]},{R[rt]},{R[rs]}'
        m={32:'add',33:'addu',34:'sub',35:'subu',36:'and',37:'or',38:'xor',39:'nor',42:'slt',43:'sltu'}
        if fn in m: return f'{m[fn]} {R[rd]},{R[rs]},{R[rt]}'
    if op==1:
        n={0:'bltz',1:'bgez',16:'bltzal',17:'bgezal'}.get(rt)
        if n: return f'{n} {R[rs]},{pc+4+si*4:08x}'
    if op==2: return f'j {((pc+4)&0xf0000000)|((w&0x3ffffff)<<2):08x}'
    if op==3: return f'jal {((pc+4)&0xf0000000)|((w&0x3ffffff)<<2):08x}'
    if op in(4,5): return f'{"beq" if op==4 else "bne"} {R[rs]},{R[rt]},{pc+4+si*4:08x}'
    if op in(6,7): return f'{"blez" if op==6 else "bgtz"} {R[rs]},{pc+4+si*4:08x}'
    m={8:'addi',9:'addiu',10:'slti',11:'sltiu'}
    if op in m: return f'{m[op]} {R[rt]},{R[rs]},{si}'
    m={12:'andi',13:'ori',14:'xori'}
    if op in m: return f'{m[op]} {R[rt]},{R[rs]},0x{im:x}'
    if op==15: return f'lui {R[rt]},0x{im:x}'
    m={32:'lb',33:'lh',34:'lwl',35:'lw',36:'lbu',37:'lhu',38:'lwr',40:'sb',41:'sh',42:'swl',43:'sw',46:'swr'}
    if op in m: return f'{m[op]} {R[rt]},{si}({R[rs]})'
    if op==16: return f'cop0 {w&0x3ffffff:07x}'
    if op==18: return f'cop2 {w&0x1ffffff:07x}'
    if op in(50,58): return f'{"lwc2" if op==50 else "swc2"} c2r{rt},{si}({R[rs]})'
    return f'.word {w:08x}'
class Img:
    def __init__(s,path,hdr=0x800):
        d=open(path,'rb').read(); s.d=d
        s.pc,s.gp,s.base,s.size=struct.unpack('<IIII',d[0x10:0x20]); s.hdr=hdr
        s.mem=d[hdr:hdr+s.size]
    def w(s,a): o=a-s.base; return struct.unpack('<I',s.mem[o:o+4])[0]
    def off(s,a): return a-s.base
    def addr(s,o): return o+s.base
