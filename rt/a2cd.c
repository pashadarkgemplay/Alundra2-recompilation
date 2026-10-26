/* CD drive: serves the game data from the game/ folder the way the disc would. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#define backtrace(b,n) 0
#define backtrace_symbols_fd(b,n,fd) ((void)0)
#else
#include <execinfo.h>
#endif
static FILE* disc; static uint32_t disc_sectors;
static int idx; static uint8_t enable=0x1f, flag;
static uint8_t param[16]; static int nparam;
static uint8_t resp[16]; static int rhead, rtail;
static uint8_t sect[2352], dbuf[2352]; static int dpos, dlen, drq;
static uint8_t mode, stat=0x02; static int reading, busy; static uint32_t loc;
typedef struct{ uint64_t t; int irq; uint8_t r[16]; int n; int kind; }Ev;
static Ev q[8]; static int nq;
static uint64_t read_period=80000;
static int cdlog; long cd_cmd_count[256]; static int xa_file, xa_chan;

typedef struct{ uint32_t lba, count; int raw; FILE*f; }GFile;
static GFile gfile[64]; static int ngfile; static uint8_t *gsys; static int ngsys; static int use_dir;
static int dir_open(void){
    const char*d=getenv("A2GAMEDIR"); char p[1024]; const char*cand[3]={d,"game","../game"};
    for(int k=0;k<3;k++){ if(!cand[k]) continue; snprintf(p,sizeof p,"%s/index.txt",cand[k]); FILE*x=fopen(p,"r"); if(!x) continue;
        char line[512]; ngfile=0; unsigned total=0;
        while(fgets(line,sizeof line,x)){ char kind; unsigned lba,cnt; char name[400];
            if(sscanf(line,"total %u",&total)==1) continue;
            if(sscanf(line,"%c %u %u %399s",&kind,&lba,&cnt,name)!=4) continue;
            snprintf(p,sizeof p,"%s/%s",cand[k],name);
            if(kind=='s'){ FILE*sf=fopen(p,"rb"); if(!sf) continue; fseek(sf,0,SEEK_END); long n=ftell(sf); fseek(sf,0,SEEK_SET); gsys=(uint8_t*)malloc(n); if(gsys&&fread(gsys,1,n,sf)==(size_t)n) ngsys=(int)(n/2052); fclose(sf); continue; }
            if(ngfile>=64) continue; GFile*g=&gfile[ngfile]; g->f=fopen(p,"rb"); if(!g->f){ fprintf(stderr,"a2cd: missing game file %s\n",p); continue; }
            g->lba=lba; g->count=cnt; g->raw=(kind=='r'); ngfile++; }
        fclose(x); disc_sectors=total; use_dir=ngfile>0; return use_dir; }
    return 0; }
static void make_cooked(uint32_t lba,const uint8_t*data,uint8_t*out){
    memset(out,0,2352); out[0]=0; memset(out+1,0xff,10); uint32_t a=lba+150; out[12]=((a/75/60/10)<<4)|(a/75/60%10); out[13]=(((a/75%60)/10)<<4)|((a/75%60)%10); out[14]=(((a%75)/10)<<4)|((a%75)%10); out[15]=2;
    out[18]=out[22]=0x08; if(data) memcpy(out+24,data,2048); }
static void read_sector(uint32_t lba,uint8_t*out){
    if(!use_dir){ if(!disc||lba>=disc_sectors){ memset(out,0,2352); return; } fseek(disc,(long)lba*2352,SEEK_SET); if(fread(out,1,2352,disc)!=2352) memset(out,0,2352); return; }
    for(int i=0;i<ngfile;i++){ GFile*g=&gfile[i]; if(lba>=g->lba&&lba<g->lba+g->count){ long off=(long)(lba-g->lba); uint8_t tmp[2048];
        if(g->raw){ fseek(g->f,off*2352,SEEK_SET); if(fread(out,1,2352,g->f)!=2352) memset(out,0,2352); }
        else { fseek(g->f,off*2048,SEEK_SET); if(fread(tmp,1,2048,g->f)!=2048) memset(tmp,0,2048); make_cooked(lba,tmp,out); } return; } }
    for(int i=0;i<ngsys;i++){ const uint8_t*e=gsys+(size_t)i*2052; uint32_t l=e[0]|(e[1]<<8)|(e[2]<<16)|((uint32_t)e[3]<<24); if(l==lba){ make_cooked(lba,e+4,out); return; } }
    make_cooked(lba,0,out); }
int cd_open(const char* path){ cdlog=getenv("A2CDLOG")?atoi(getenv("A2CDLOG")):0; if(path){ disc=fopen(path,"rb"); if(disc){ fseek(disc,0,SEEK_END); disc_sectors=ftell(disc)/2352; fseek(disc,0,SEEK_SET); return 1; } } return dir_open(); }
static void push(uint64_t t,int irq,int n,const uint8_t*r,int kind){ if(nq>=8) return; Ev*e=&q[nq++]; e->t=t; e->irq=irq; e->n=n; e->kind=kind; if(n) memcpy(e->r,r,n); }
static void purge_sectors(void){ int j=0; for(int i=0;i<nq;i++) if(q[i].kind!=1) q[j++]=q[i]; nq=j; }
static uint32_t bcd(uint8_t b){ return (b>>4)*10+(b&15); }
static uint8_t tobcd(uint32_t v){ return ((v/10)<<4)|(v%10); }
static uint64_t now_t;
extern long cd_xa_sectors;
static uint8_t ring_c[32], ring_p[32][4]; static uint32_t ring_loc[32]; static uint8_t ring_mode[32]; static unsigned ring_n;
void cd_dump(FILE*f){ fprintf(f,"CD: loc %u mode %02x reading %d stat %02x queued %d flag %d enable %02x xa_skipped %ld\nlast CD commands (oldest first): ",loc,mode,reading,stat,nq,flag,enable,cd_xa_sectors); for(unsigned i=0;i<32;i++){ unsigned k=(ring_n+i)&31; if(ring_c[k]) fprintf(f," %02x(%02x %02x %02x %02x loc%u m%02x)",ring_c[k],ring_p[k][0],ring_p[k][1],ring_p[k][2],ring_p[k][3],ring_loc[k],ring_mode[k]); } fprintf(f,"\n"); }
static void cmd(uint8_t c){ { unsigned k=ring_n++&31; ring_c[k]=c; memcpy(ring_p[k],param,4); ring_loc[k]=loc; ring_mode[k]=mode; }
    uint8_t r[16]; uint64_t t=now_t; cd_cmd_count[c]++; { extern long gpu_frame; static int lg=-1; if(lg<0) lg=getenv("A2CDCMD")!=0; if(lg) fprintf(stderr,"[F%ld] CD cmd %02x\n",gpu_frame,c); } if(getenv("A2CDBT") && c==0x06 && cd_cmd_count[c]==(long)atoi(getenv("A2CDBT"))){ void*bt[24]; int n=backtrace(bt,24); fprintf(stderr,"C stack at ReadN #%s:\n",getenv("A2CDBT")); backtrace_symbols_fd(bt,n,2); } if(cdlog) fprintf(stderr,"CD cmd %02x params %d: %02x %02x %02x\n",c,nparam,param[0],param[1],param[2]);
    uint8_t s=stat|(reading?0x20:0); r[0]=s;
    switch(c){
    case 0x01: push(t+3000,3,1,r,0); break;
    case 0x02: loc=(bcd(param[0])*60+bcd(param[1]))*75+bcd(param[2])-150; push(t+3000,3,1,r,0); break;
    case 0x06: case 0x1B: purge_sectors(); reading=1; r[0]=stat|0x20; push(t+3000,3,1,r,0); push(t+3000+((mode&0x80)?read_period:2*read_period),1,1,r,1); break;
    case 0x08: case 0x09: purge_sectors(); push(t+3000,3,1,r,0); reading=0; r[0]=stat; push(t+20000,2,1,r,0); break;
    case 0x0A: purge_sectors(); reading=0; mode=0; push(t+3000,3,1,r,0); push(t+25000,2,1,r,0); break;
    case 0x0B: case 0x0C: case 0x0D: if(c==0x0D){ xa_file=param[0]; xa_chan=param[1]; if(getenv("A2XALOG")) fprintf(stderr,"[XA] Setfilter file %d channel %d (mode %02x loc %u)\n",xa_file,xa_chan,mode,loc); } push(t+3000,3,1,r,0); break;
    case 0x0E: mode=param[0]; if(getenv("A2XALOG")) fprintf(stderr,"[XA] Setmode %02x\n",mode); push(t+3000,3,1,r,0); break;
    case 0x0F: r[1]=mode; r[2]=0; r[3]=0; r[4]=0; push(t+3000,3,5,r,0); break;
    case 0x11: if(getenv("A2GETLOCOLD")){ uint32_t l=loc+150; r[0]=tobcd(l/75/60); r[1]=tobcd(l/75%60); r[2]=tobcd(l%75); r[3]=mode&1?0:2; r[4]=r[5]=r[6]=r[7]=0; push(t+3000,3,8,r,0); break; } { uint32_t cur=loc?loc-1:0, ab=cur+150; r[0]=1; r[1]=1; r[2]=tobcd(cur/75/60); r[3]=tobcd(cur/75%60); r[4]=tobcd(cur%75); r[5]=tobcd(ab/75/60); r[6]=tobcd(ab/75%60); r[7]=tobcd(ab%75); push(t+3000,3,8,r,0); } break;
    case 0x10: { uint32_t l=loc+150; r[0]=tobcd(l/75/60); r[1]=tobcd(l/75%60); r[2]=tobcd(l%75); r[3]=mode&1?0:2; r[4]=0; r[5]=0; r[6]=0; r[7]=0; push(t+3000,3,8,r,0); } break;
    case 0x13: r[1]=0x01; r[2]=0x01; push(t+3000,3,3,r,0); break;
    case 0x14: r[1]=0x00; r[2]=0x02; push(t+3000,3,3,r,0); break;
    case 0x15: case 0x16: push(t+3000,3,1,r,0); r[0]=stat; push(t+30000,2,1,r,0); break;
    case 0x19: if(param[0]==0x20){ r[0]=0x94; r[1]=0x09; r[2]=0x19; r[3]=0xC0; push(t+3000,3,4,r,0); } else { r[0]=s|1; r[1]=0x10; push(t+3000,5,2,r,0); } break;
    case 0x1A: push(t+3000,3,1,r,0); { uint8_t g[8]={0x02,0x00,0x20,0x00,'S','C','E','A'}; push(t+30000,2,8,g,0); } break;
    case 0x1E: push(t+3000,3,1,r,0); push(t+30000,2,1,r,0); break;
    default: r[0]=s|1; r[1]=0x40; push(t+3000,5,2,r,0); break;
    }
    nparam=0;
}

extern void spu_xa_push(const int16_t*l,const int16_t*r,int n,int rate);
static int xa_old[2], xa_older[2];
static void xa_decode(const uint8_t*sec){
    static const int K0[4]={0,60,115,98}, K1[4]={0,0,-52,-55};
    int coding=sec[19], stereo=coding&1, rate=(coding&4)?18900:37800, bits8=(coding&16)!=0; if(bits8) return;
    static int16_t L[18*224], R[18*224]; int nl=0, nr=0; const uint8_t*data=sec+24;
    for(int g=0;g<18;g++){ const uint8_t*grp=data+g*128, *body=grp+16;
        for(int u=0;u<8;u++){ uint8_t h=grp[(u/4)*8+(u%4)]; int sh=h&15, fl=(h>>4)&3; if(sh>12) sh=9; int ch=stereo?(u&1):0;
            for(int s=0;s<28;s++){ int n=(body[4*s+u/2]>>(4*(u&1)))&15; int t=(int16_t)(n<<12); t>>=sh; int v=t+((xa_old[ch]*K0[fl]+xa_older[ch]*K1[fl]+32)>>6); if(v>32767) v=32767; if(v<-32768) v=-32768; xa_older[ch]=xa_old[ch]; xa_old[ch]=v;
                if(stereo){ if(ch) R[nr++]=(int16_t)v; else L[nl++]=(int16_t)v; } else L[nl++]=(int16_t)v; } } }
    if(stereo) spu_xa_push(L,R,nl,rate); else spu_xa_push(L,L,nl,rate); }
static int xa_count; long cd_xa_sectors;
static int fire_sector(void){
    if(loc>=disc_sectors){ memset(sect,0,sizeof sect); } else read_sector(loc,sect);
    if(mode&0x20){ memcpy(dbuf,sect+12,2340); dlen=2340; } else { memcpy(dbuf,sect+24,2048); dlen=2048; }
    dpos=0; loc++;
    if((mode&0x40) && (sect[18]&0x04)){ cd_xa_sectors++;
        int pass=!(mode&0x08) || (sect[16]==xa_file && sect[17]==xa_chan);
        if(getenv("A2XALOG")){ static long n; if(n++<60) fprintf(stderr,"[XA] audio sector lba %u file %d ch %d coding %02x submode %02x (mode %02x, filter %d/%d) %s\n",loc-1,sect[16],sect[17],sect[19],sect[18],mode,xa_file,xa_chan,pass?"PLAY":"skip"); }
        if(pass) xa_decode(sect);
        return 1; }
    return 0;
}
static void xa_selftest(uint64_t now){
    static int init, on; static unsigned lba, cnt, ch; static uint64_t t0, nextt; static long step;
    if(!init){ init=1; const char*e=getenv("A2XATEST"); if(e){ unsigned f=0; ch=0; if(sscanf(e,"%u:%u:%u:%u",&lba,&cnt,&f,&ch)>=2){ on=1; t0=(uint64_t)f*200000; nextt=t0; step=(ch&0x100)?8:16; } } }
    if(!on||now<nextt||!cnt||(!disc&&!use_dir)) return; nextt+=1260000;
    unsigned want=ch&0xff; for(int k=0;k<16;k++){ uint8_t sc[2352]; read_sector(lba,sc); lba++; if((sc[18]&4)&&sc[17]==want){ xa_decode(sc); cnt--; break; } } }
void cd_step(uint64_t now){
    now_t=now; xa_selftest(now);
    if(flag==0 && nq>0){
        int best=-1; for(int i=0;i<nq;i++) if(q[i].t<=now && (best<0||q[i].t<q[best].t)) best=i;
        if(best>=0){ Ev e=q[best]; memmove(&q[best],&q[best+1],(nq-best-1)*sizeof(Ev)); nq--;
            if(e.kind==1){ if(!reading) return; int xa=fire_sector(); uint8_t r[1]={stat|0x20}; push(now+((mode&0x80)?read_period:2*read_period),1,1,r,1); if(xa) return; }
            rhead=rtail=0; for(int i=0;i<e.n;i++) resp[rtail++]=e.r[i];
            flag=e.irq; busy=0; if(cdlog){ fprintf(stderr,"CD INT%d resp",e.irq); for(int i=0;i<e.n;i++) fprintf(stderr," %02x",e.r[i]); fprintf(stderr,"\n"); } }
    }
}
int cd_irq(void){ return (flag&enable)!=0; }
static uint32_t cd_read_port_(int p);
uint32_t cd_read_port(int p){ uint32_t v=cd_read_port_(p); if(cdlog>1) fprintf(stderr,"CD rd port%d idx%d -> %02x\n",p,idx,v); return v; }
static uint32_t cd_read_port_(int p){
    switch(p){
    case 0: return idx | (nparam==0?0x08:0) | (nparam<16?0x10:0) | (rhead<rtail?0x20:0) | ((drq&&dpos<dlen)?0x40:0) | (busy?0x80:0);
    case 1: return rhead<rtail? resp[rhead++] : 0;
    case 2: if(drq&&dpos<dlen) return dbuf[dpos++]; return 0;
    default: return idx==0||idx==2 ? (0xE0|enable) : (0xE0|flag); }
}
void cd_write_port(int p,uint32_t v){
    v&=0xff; if(cdlog>1) fprintf(stderr,"CD wr port%d idx%d <- %02x\n",p,idx,v);
    if(p==0){ idx=v&3; return; }
    if(idx==0){ if(p==1){ busy=1; cmd(v); } else if(p==2){ if(nparam<16) param[nparam++]=v; } else { drq=(v&0x80)!=0; if(drq){ if(dpos>=dlen) dpos=0; } else dpos=dlen; } }
    else if(idx==1){ if(p==2) enable=v&0x1f; else if(p==3){ if(cdlog) fprintf(stderr,"CD ack %02x (flag was %d)\n",v,flag); flag&=~(v&0x1f); if(v&0x40) nparam=0; } }
}
int cd_dma(uint8_t*dst,int n){ int c=0; while(c<n && dpos<dlen){ dst[c++]=dbuf[dpos++]; } return c; }
uint32_t cd_cur_loc(void){ return loc; }
