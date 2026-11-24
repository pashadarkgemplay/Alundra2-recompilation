/* Sound chip: plays the game's ADPCM voices and mixes them to 44.1 kHz stereo. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SRAM 0x80000
#define RING 16384
#define RINGCAP 8820
int a2_audio_ok=1;
static uint8_t sram[SRAM];
static uint16_t reg[0x200];
static uint32_t taddr;
typedef struct { int on, phase, idx, frac, cnt, level, stop; uint32_t addr, rep; int16_t blk[28], prev, cur; int old, older; } V;
enum { OFF, ATT, DEC, SUS, REL };
static V vc[24];
static uint32_t endx;
long spu_feat[16]; long spu_underrun, spu_drops, spu_kon_count, spu_dma_bytes, spu_frames; double spu_energy;
static FILE*dumpf; static int dump_init;
static int16_t ring[2*RING]; static volatile unsigned wr, rd;

static const int K0[5]={0,60,115,98,122}, K1[5]={0,0,-52,-55,-60};
static void load_block(V*v){
  const uint8_t*b=sram+(v->addr&(SRAM-1)); int shift=b[0]&15, filt=(b[0]>>4)&7, fl=b[1]; if(filt>4) filt=4; if(shift>12) shift=9;
  if(fl&4) v->rep=v->addr;
  for(int i=0;i<28;i++){ int n=(b[2+i/2]>>((i&1)*4))&15; int s=(int)(int16_t)(n<<12)>>shift; s+=(v->old*K0[filt]+v->older*K1[filt]+32)>>6; if(s>32767) s=32767; if(s<-32768) s=-32768; v->older=v->old; v->old=s; v->blk[i]=(int16_t)s; }
  v->idx=0;
  if(fl&1){ endx|=1u<<(v-vc); v->addr=v->rep; if(!(fl&2)) v->stop=1; } else v->addr=(v->addr+16)&(SRAM-1);
}
static void advance(V*v){
  v->idx++;
  if(v->idx>=28){ if(v->stop){ v->on=0; v->phase=OFF; v->level=0; v->stop=0; return; } load_block(v); }
  v->prev=v->cur; v->cur=v->blk[v->idx];
}
static void kon(int n){
  V*v=&vc[n]; uint16_t*r=&reg[(n*16)/2]; memset(v,0,sizeof *v);
  v->addr=((uint32_t)r[3]*8)&(SRAM-1); v->rep=v->addr; v->on=1; v->phase=ATT; v->level=0; endx&=~(1u<<n);
  load_block(v); v->cur=v->blk[0]; v->prev=0; spu_kon_count++;
}
static void envstep(V*v,int decr,int exp,int shift,int stepf){
  int step=decr?(-8+stepf):(7-stepf), cycles=1<<(shift>11?shift-11:0); step*=1<<(shift<11?11-shift:0);
  if(exp){ if(!decr&&v->level>0x6000) cycles*=4; if(decr) step=(step*v->level)>>15; }
  if(++v->cnt>=cycles){ v->cnt=0; v->level+=step; if(v->level<0) v->level=0; if(v->level>0x7fff) v->level=0x7fff; }
}
static void adsr(V*v,const uint16_t*r){
  uint16_t lo=r[4], hi=r[5];
  switch(v->phase){
    case ATT: envstep(v,0,lo>>15,(lo>>10)&31,(lo>>8)&3); if(v->level>=0x7fff) v->phase=DEC; break;
    case DEC: envstep(v,1,1,(lo>>4)&15,0); if(v->level<=(((lo&15)+1)*0x800)) v->phase=SUS; break;
    case SUS: envstep(v,(hi>>14)&1,hi>>15,(hi>>8)&31,(hi>>6)&3); break;
    case REL: envstep(v,1,(hi>>5)&1,hi&31,0); if(v->level<=0){ v->on=0; v->phase=OFF; } break;
  }
}
static inline int vol(uint16_t x){ if(x&0x8000) return (x&0x1000)?-0x3fff*2:0x3fff*2; return (int16_t)(x<<1); }

#define CDRING 65536
static int16_t cdl[CDRING], cdr_[CDRING]; static volatile unsigned cdw, cdrd; static double cdph; static int cdlastl, cdlastr, cd_rate; long spu_xa_frames, spu_xa_drops, spu_xa_trims;
/* CD-XA audio: the drive delivers a decoded sector every 3.2 video frames per channel; the samples are resampled from 37.8 / 18.9 kHz
   to 44.1 kHz with an 8-tap Lanczos filter (the real chip uses a similar 7-tap polyphase filter) and queued. The queue is the only
   buffering: nothing is dropped unless it ever grows beyond half a second (safety net, counted in spu_xa_trims). */
long spu_xa_maxq;
static float lz_w[256][8]; static int lz_init;
static double msin(double x){ const double PI=3.14159265358979323846; while(x>PI) x-=2*PI; while(x<-PI) x+=2*PI; double x2=x*x, t=x, r=x; for(int k=1;k<9;k++){ t*=-x2/((2*k)*(2*k+1)); r+=t; } return r; }
static void lz_build(void){ for(int ph=0;ph<256;ph++){ double f=ph/256.0, sum=0, w[8]; for(int k=-3;k<=4;k++){ double x=k-f, v; if(x==0) v=1; else { double px=3.14159265358979*x; v=(msin(px)/px)*(msin(px/4)/(px/4)); } w[k+3]=v; sum+=v; } for(int k=0;k<8;k++) lz_w[ph][k]=(float)(w[k]/sum); } lz_init=1; }
void spu_xa_push(const int16_t*l,const int16_t*r,int n,int rate){
    if(n<=0) return; if(!lz_init) lz_build(); if(rate!=cd_rate) cd_rate=rate;
    static float hl[8], hr[8]; static double pos=4.0; extern double a2_vbl_ratio(void); double step=(double)rate*a2_vbl_ratio()/44100.0;   /* see a2cd.c: XA keeps the PS1 ratio to the frames */
    { unsigned wait=cdw-cdrd; if(wait>22050){ cdrd=cdw-11025; spu_xa_trims++; } }
    float *xl=(float*)malloc((size_t)(n+8)*sizeof(float)), *xr=(float*)malloc((size_t)(n+8)*sizeof(float)); if(!xl||!xr){ free(xl); free(xr); return; }
    for(int i=0;i<8;i++){ xl[i]=hl[i]; xr[i]=hr[i]; } for(int i=0;i<n;i++){ xl[8+i]=l[i]; xr[8+i]=r[i]; }
    while(pos<n+4){ int i=(int)pos; int ph=(int)((pos-i)*256.0); const float*w=lz_w[ph]; float sl=0,sr=0; for(int k=0;k<8;k++){ sl+=xl[i-3+k]*w[k]; sr+=xr[i-3+k]*w[k]; }
        int ol=(int)(sl+(sl>=0?0.5f:-0.5f)), orr=(int)(sr+(sr>=0?0.5f:-0.5f)); if(ol>32767) ol=32767; if(ol<-32768) ol=-32768; if(orr>32767) orr=32767; if(orr<-32768) orr=-32768;
        if(cdw-cdrd<CDRING-1){ cdl[cdw%CDRING]=(int16_t)ol; cdr_[cdw%CDRING]=(int16_t)orr; cdw++; spu_xa_frames++; } else spu_xa_drops++;
        pos+=step; }
    for(int i=0;i<8;i++){ hl[i]=xl[n+i]; hr[i]=xr[n+i]; } pos-=n; free(xl); free(xr);
    { unsigned wait=cdw-cdrd; if((long)wait>spu_xa_maxq) spu_xa_maxq=wait; }
}
static void rev_reset(void);
void spu_write(uint32_t p,uint32_t val,int w){
  uint32_t o=(p-0x1f801c00)&0x3ff;
  if(w==4){ spu_write(p,val&0xffff,2); spu_write(p+2,val>>16,2); return; }
  if(w==1){ uint16_t c=reg[(o&~1)/2]; int sh=(o&1)*8; uint16_t n=(c&~(0xff<<sh))|((val&0xff)<<sh); spu_write(p&~1u,n,2); return; }
  o&=~1u; reg[o/2]=(uint16_t)val;
  if(val){ if(o==0x198||o==0x19a) spu_feat[0]++; else if(o==0x184||o==0x186) spu_feat[1]++; else if(o==0x190||o==0x192) spu_feat[2]++; else if(o==0x194||o==0x196) spu_feat[3]++; else if(o<0x180&&(o&15)<2&&(val&0x8000)) spu_feat[4]++; else if(o==0x180||o==0x182) { if(val&0x8000) spu_feat[5]++; } else if(o==0x1aa&&(val&0x80)) spu_feat[6]++; }
  switch(o){
    case 0x1a2: rev_reset(); break;
    case 0x1a6: taddr=((uint32_t)val*8)&(SRAM-1); break;
    case 0x1a8: sram[taddr&(SRAM-1)]=val; sram[(taddr+1)&(SRAM-1)]=val>>8; taddr=(taddr+2)&(SRAM-1); spu_dma_bytes+=2; break;
    case 0x188: for(int i=0;i<16;i++) if(val>>i&1) kon(i); break;
    case 0x18a: for(int i=0;i<8;i++) if(val>>i&1) kon(16+i); break;
    case 0x18c: for(int i=0;i<16;i++) if(val>>i&1 && vc[i].on) vc[i].phase=REL; break;
    case 0x18e: for(int i=0;i<8;i++) if(val>>i&1 && vc[16+i].on) vc[16+i].phase=REL; break;
  }
}
uint32_t spu_read(uint32_t p){
  uint32_t o=(p-0x1f801c00)&0x3fc; uint32_t lo=reg[o/2], hi=reg[o/2+1];
  switch(o){
    case 0x1a8: lo=0; break;
    case 0x1a6: lo=(taddr/8)&0xffff; break;
    case 0x1ae: lo=reg[0x1aa/2]&0x3f; hi=0; break;
    case 0x19c: lo=endx&0xffff; hi=(endx>>16)&0xff; break;
  }
  if(o<0x180){ int n=o/16; if((o&15)==12) lo=vc[n].level; }
  return lo|(hi<<16);
}
void spu_dma(uint8_t*ram,uint32_t bytes,int to_spu){
  for(uint32_t i=0;i<bytes;i++){ if(to_spu) sram[(taddr+i)&(SRAM-1)]=ram[i]; else ram[i]=sram[(taddr+i)&(SRAM-1)]; }
  taddr=(taddr+bytes)&(SRAM-1); spu_dma_bytes+=bytes;
}

/* Reverb (runs at 22.05 kHz, work area in sound RAM; formulas from the public hardware description).
   Voices flagged in 0x198/0x19A (and CD audio if SPUCNT bit 2) feed it; its output is added before the master volume. */
static int rev_cur, rev_tick; static int rev_inl, rev_inr, rev_ol, rev_or, rev_pl, rev_pr;
static inline int sat16(int v){ return v>32767?32767:v<-32768?-32768:v; }
static inline int rv16(uint32_t w){ w=(w*2)&(SRAM-1); return (int16_t)(sram[w]|(sram[w+1]<<8)); }
static inline void rw16(uint32_t w,int v,int en){ if(!en) return; v=sat16(v); w=(w*2)&(SRAM-1); sram[w]=(uint8_t)v; sram[w+1]=(uint8_t)(v>>8); }
#define RR(i) ((int)(uint16_t)reg[(0x1c0+2*(i))/2])
#define RS(i) ((int)(int16_t)reg[(0x1c0+2*(i))/2])
static uint32_t radd(uint32_t base,uint32_t area,int off){ int64_t x=(int64_t)rev_cur+off; x%=(int64_t)area; if(x<0) x+=area; return base+(uint32_t)x; }
static void rev_reset(void){ rev_cur=0; }
static void reverb_step(int Lin,int Rin,int en){
  uint32_t base=((uint32_t)reg[0x1a2/2]*4)&0x3ffff, area=0x40000-base; if(area<16) return;
  #define A(i) radd(base,area,RR(i)*4)
  #define Ao(i,o) radd(base,area,RR(i)*4+(o))
  int vIIR=RS(2), vWALL=RS(7), vA1=RS(8), vA2=RS(9);
  int dA1=RR(0)*4, dA2=RR(1)*4;
  /* indexes: 0 dAPF1 1 dAPF2 2 vIIR 3-6 vCOMB1-4 7 vWALL 8 vAPF1 9 vAPF2 10 mLSAME 11 mRSAME 12 mLCOMB1 13 mRCOMB1 14 mLCOMB2 15 mRCOMB2
     16 dLSAME 17 dRSAME 18 mLDIFF 19 mRDIFF 20 mLCOMB3 21 mRCOMB3 22 mLCOMB4 23 mRCOMB4 24 dLDIFF 25 dRDIFF 26 mLAPF1 27 mRAPF1 28 mLAPF2 29 mRAPF2 30 vLIN 31 vRIN */
  int lin=(Lin*RS(30))>>15, rin=(Rin*RS(31))>>15;
  { int pv=rv16(Ao(10,-1)); int x=sat16(lin+((rv16(A(16))*vWALL)>>15)-pv); rw16(A(10),((x*vIIR)>>15)+pv,en); }
  { int pv=rv16(Ao(11,-1)); int x=sat16(rin+((rv16(A(17))*vWALL)>>15)-pv); rw16(A(11),((x*vIIR)>>15)+pv,en); }
  { int pv=rv16(Ao(18,-1)); int x=sat16(lin+((rv16(A(25))*vWALL)>>15)-pv); rw16(A(18),((x*vIIR)>>15)+pv,en); }
  { int pv=rv16(Ao(19,-1)); int x=sat16(rin+((rv16(A(24))*vWALL)>>15)-pv); rw16(A(19),((x*vIIR)>>15)+pv,en); }
  int lo=sat16((RS(3)*rv16(A(12))+RS(4)*rv16(A(14))+RS(5)*rv16(A(20))+RS(6)*rv16(A(22)))>>15);
  int ro=sat16((RS(3)*rv16(A(13))+RS(4)*rv16(A(15))+RS(5)*rv16(A(21))+RS(6)*rv16(A(23)))>>15);
  { int x=rv16(Ao(26,-dA1)); lo=sat16(lo-((vA1*x)>>15)); rw16(A(26),lo,en); lo=sat16(((lo*vA1)>>15)+x); }
  { int x=rv16(Ao(27,-dA1)); ro=sat16(ro-((vA1*x)>>15)); rw16(A(27),ro,en); ro=sat16(((ro*vA1)>>15)+x); }
  { int x=rv16(Ao(28,-dA2)); lo=sat16(lo-((vA2*x)>>15)); rw16(A(28),lo,en); lo=sat16(((lo*vA2)>>15)+x); }
  { int x=rv16(Ao(29,-dA2)); ro=sat16(ro-((vA2*x)>>15)); rw16(A(29),ro,en); ro=sat16(((ro*vA2)>>15)+x); }
  rev_pl=rev_ol; rev_pr=rev_or; rev_ol=lo; rev_or=ro;
  if(++rev_cur>=(int)area) rev_cur=0;
}
void spu_frame(void){
  if(!dump_init){ dump_init=1; if(getenv("A2SPUDUMP")) dumpf=fopen(getenv("A2SPUDUMP"),"wb"); }
  int16_t out[735*2]; uint16_t cnt=reg[0x1aa/2]; int en=(cnt&0x8000)&&(cnt&0x4000);
  int mvl=vol(reg[0x180/2]), mvr=vol(reg[0x182/2]);
  for(int s=0;s<735;s++){
    int L=0,R=0,RL=0,RR_=0; uint32_t rvm=reg[0x198/2]|((uint32_t)reg[0x19a/2]<<16);
    for(int n=0;n<24;n++){ V*v=&vc[n]; if(!v->on) continue; uint16_t*r=&reg[(n*16)/2];
      int sm=v->prev+(((v->cur-v->prev)*v->frac)>>12); sm=(sm*v->level)>>15;
      { int vl=(sm*vol(r[0]))>>15, vr=(sm*vol(r[1]))>>15; L+=vl; R+=vr; if(rvm>>n&1){ RL+=vl; RR_+=vr; } }
      adsr(v,r);
      int pit=r[2]; if(pit>0x3fff) pit=0x3fff; v->frac+=pit; while(v->frac>=0x1000&&v->on){ v->frac-=0x1000; advance(v); }
    }
    { int cl=0,cr=0; if(cdw!=cdrd){ cl=cdl[cdrd%CDRING]; cr=cdr_[cdrd%CDRING]; cdrd++; } if((cnt&1)&&(cl||cr)){ int xl=(cl*(int)(int16_t)reg[0x1b0/2])>>15, xr=(cr*(int)(int16_t)reg[0x1b2/2])>>15; L+=xl; R+=xr; if(cnt&4){ RL+=xl; RR_+=xr; } } }
    { static int nr=-1; if(nr<0) nr=getenv("A2NOREVERB")!=0; int ren=(cnt&0x80)!=0&&!nr; rev_inl+=RL; rev_inr+=RR_; if(++rev_tick>=2){ rev_tick=0; reverb_step(sat16(rev_inl/2),sat16(rev_inr/2),ren); rev_inl=rev_inr=0; }
      if(ren){ int vl=(int)(int16_t)reg[0x184/2], vr=(int)(int16_t)reg[0x186/2]; int rl=rev_tick==0?rev_ol:((rev_ol+rev_pl)>>1), rr=rev_tick==0?rev_or:((rev_or+rev_pr)>>1); L+=(rl*vl)>>15; R+=(rr*vr)>>15; } }
    L=(L*mvl)>>15; R=(R*mvr)>>15; if(!en){ L=R=0; }
    if(L>32767) L=32767; if(L<-32768) L=-32768; if(R>32767) R=32767; if(R<-32768) R=-32768;
    out[2*s]=L; out[2*s+1]=R; spu_energy+=(double)L*L+(double)R*R;
  }
  spu_frames++;
  if(dumpf) fwrite(out,4,735,dumpf);
  if(a2_audio_ok&&wr-rd+735>RINGCAP) spu_drops++;
  if(a2_audio_ok&&wr-rd+735<=RINGCAP) { for(int s=0;s<735;s++){ unsigned i=(wr+s)%RING; ring[2*i]=out[2*s]; ring[2*i+1]=out[2*s+1]; } __sync_synchronize(); wr+=735; }
}
int spu_fill(void){ return (int)(wr-rd); }
int spu_started(void){ return spu_frames>30; }
int spu_pull(int16_t*o,int n){
  int got=0; while(got<n && rd!=wr){ unsigned i=rd%RING; o[2*got]=ring[2*i]; o[2*got+1]=ring[2*i+1]; got++; __sync_synchronize(); rd++; }
  if(got<n&&spu_frames>120) spu_underrun++;
  for(int i=got;i<n;i++){ o[2*i]=0; o[2*i+1]=0; }
  return got;
}
void spu_close(void){ if(dumpf){ fclose(dumpf); dumpf=0; } }
void spu_stats(FILE*f){ int on=0; for(int i=0;i<24;i++) on+=vc[i].on; fprintf(f,"SPU: audio underruns %ld, dropped frames %ld; frames %ld, key-ons %ld, transferred %ld bytes, voices on %d, meansq %.0f, ctrl %04x mainvol %04x/%04x\n",spu_underrun,spu_drops,spu_frames,spu_kon_count,spu_dma_bytes,on,spu_frames?spu_energy/(spu_frames*735.0*2):0.0,reg[0x1aa/2],reg[0x180/2],reg[0x182/2]); fprintf(f,"SPU features used (writes): reverb-voices %ld reverb-vol %ld pitchmod %ld noise %ld voice-vol-sweep %ld main-vol-sweep %ld reverb-enable %ld; ctrl %04x\n",spu_feat[0],spu_feat[1],spu_feat[2],spu_feat[3],spu_feat[4],spu_feat[5],spu_feat[6],reg[0x1aa/2]); fprintf(f,"SPU: CD audio volume %04x/%04x, XA samples queued %ld dropped %ld, skipped ahead %ld, peak queue %ld samples\n",reg[0x1b0/2],reg[0x1b2/2],spu_xa_frames,spu_xa_drops,spu_xa_trims,spu_xa_maxq); }
