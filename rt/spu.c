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
long spu_underrun, spu_drops, spu_kon_count, spu_dma_bytes, spu_frames; double spu_energy;
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
void spu_xa_push(const int16_t*l,const int16_t*r,int n,int rate){
    if(n<=0) return; double step=(double)rate/44100.0; if(rate!=cd_rate){ cd_rate=rate; }

    { unsigned wait=cdw-cdrd; if(wait>1470){ cdrd=cdw-735; spu_xa_trims++; } }
    int i=0; while(i<n){ int pl=i?l[i-1]:cdlastl, pr=i?r[i-1]:cdlastr; double f=cdph; int ol=(int)(pl+(l[i]-pl)*f), orr=(int)(pr+(r[i]-pr)*f);
        if(cdw-cdrd<CDRING-1){ cdl[cdw%CDRING]=(int16_t)ol; cdr_[cdw%CDRING]=(int16_t)orr; cdw++; spu_xa_frames++; } else spu_xa_drops++;
        cdph+=step; while(cdph>=1.0){ cdph-=1.0; i++; } }
    cdlastl=l[n-1]; cdlastr=r[n-1]; }
void spu_write(uint32_t p,uint32_t val,int w){
  uint32_t o=(p-0x1f801c00)&0x3ff;
  if(w==4){ spu_write(p,val&0xffff,2); spu_write(p+2,val>>16,2); return; }
  if(w==1){ uint16_t c=reg[(o&~1)/2]; int sh=(o&1)*8; uint16_t n=(c&~(0xff<<sh))|((val&0xff)<<sh); spu_write(p&~1u,n,2); return; }
  o&=~1u; reg[o/2]=(uint16_t)val;
  switch(o){
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
void spu_frame(void){
  if(!dump_init){ dump_init=1; if(getenv("A2SPUDUMP")) dumpf=fopen(getenv("A2SPUDUMP"),"wb"); }
  int16_t out[735*2]; uint16_t cnt=reg[0x1aa/2]; int en=(cnt&0x8000)&&(cnt&0x4000);
  int mvl=vol(reg[0x180/2]), mvr=vol(reg[0x182/2]);
  for(int s=0;s<735;s++){
    int L=0,R=0;
    for(int n=0;n<24;n++){ V*v=&vc[n]; if(!v->on) continue; uint16_t*r=&reg[(n*16)/2];
      int sm=v->prev+(((v->cur-v->prev)*v->frac)>>12); sm=(sm*v->level)>>15;
      L+=(sm*vol(r[0]))>>15; R+=(sm*vol(r[1]))>>15;
      adsr(v,r);
      int pit=r[2]; if(pit>0x3fff) pit=0x3fff; v->frac+=pit; while(v->frac>=0x1000&&v->on){ v->frac-=0x1000; advance(v); }
    }
    { int cl=0,cr=0; if(cdw!=cdrd){ cl=cdl[cdrd%CDRING]; cr=cdr_[cdrd%CDRING]; cdrd++; } if((cnt&1)&&(cl||cr)){ L+=(cl*(int)(int16_t)reg[0x1b0/2])>>15; R+=(cr*(int)(int16_t)reg[0x1b2/2])>>15;     } }
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
void spu_stats(FILE*f){ int on=0; for(int i=0;i<24;i++) on+=vc[i].on; fprintf(f,"SPU: audio underruns %ld, dropped frames %ld; frames %ld, key-ons %ld, transferred %ld bytes, voices on %d, meansq %.0f, ctrl %04x mainvol %04x/%04x\n",spu_underrun,spu_drops,spu_frames,spu_kon_count,spu_dma_bytes,on,spu_frames?spu_energy/(spu_frames*735.0*2):0.0,reg[0x1aa/2],reg[0x180/2],reg[0x182/2]); fprintf(f,"SPU: CD audio volume %04x/%04x, XA samples queued %ld dropped %ld, skipped ahead %ld\n",reg[0x1b0/2],reg[0x1b2/2],spu_xa_frames,spu_xa_drops,spu_xa_trims); }
