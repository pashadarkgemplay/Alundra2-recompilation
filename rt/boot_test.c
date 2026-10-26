/* Start-up, the main loop, and replacements for the console's built-in (BIOS) routines. */
extern long gpu_frame;

#include <time.h>
#include "a2rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#define backtrace(b,n) 0
#define backtrace_symbols_fd(b,n,fd) ((void)0)
#define strtok_r strtok_s
#define alarm(x) 0
#ifndef SIGALRM
#define SIGALRM 14
#endif
#else
#include <unistd.h>
#include <execinfo.h>
#endif
extern int cd_open(const char*); extern uint32_t cd_read_port(int); extern void cd_write_port(int,uint32_t); extern void cd_step(uint64_t); extern int cd_irq(void); extern int cd_dma(uint8_t*,int); extern long cd_cmd_count[256];
static uint8_t ram[0x200000], sp_[0x400];
static uint32_t istat, imask, dicr;

extern void spu_write(uint32_t,uint32_t,int); extern uint32_t spu_read(uint32_t); extern void spu_dma(uint8_t*,uint32_t,int); extern void spu_frame(void); extern void spu_close(void); extern void spu_stats(FILE*);
static uint32_t tickhist[8]; static int tickn; static uint32_t io[0x1000/4]; static int irq_on=1, pend_dma4=0, pend_vbl=0; static uint64_t ticks, last_vbl; static CPU*maincpu;
static uint32_t watch_lo=0,watch_hi=0; static int watch_n;
extern uint32_t sio_read(uint32_t,int); extern void sio_write(uint32_t,uint32_t,int); extern void sio_step(uint64_t); extern int sio_irq_take(void); extern void sio_set_buttons(uint16_t); extern long sio_bytes, sio_transactions;
typedef struct{char k;uint32_t a,b,c;}EV; static EV ev[1<<22]; static long spu_writes; static int nev; static int stopped;
static void logev(char k,uint32_t a,uint32_t b,uint32_t c){ if(k=='R' && ((a&0x1fffffff)==0x1f801110||(a&0x1fffffff)==0x1f801814)){ spu_writes+=0; return; }
  if(k=='W' && (a&0x1fffffff)>=0x1f801c00 && (a&0x1fffffff)<0x1f801e00){ spu_writes++; return; }
  if(nev<(1<<22)){ev[nev].k=k;ev[nev].a=a;ev[nev].b=b;ev[nev].c=c;nev++;} }
extern void gpu_init(void); extern void gpu_gp0(uint32_t); extern void gpu_gp1(uint32_t); extern uint32_t gpu_read_data(void); extern uint32_t gpu_status(void);
extern int gpu_dma_list(uint8_t*,uint32_t); extern void gpu_dma_to(uint8_t*,uint32_t,uint32_t); extern void gpu_dma_from(uint8_t*,uint32_t,uint32_t); extern void gpu_stats(void); extern int gpu_dump_ppm(const char*,int,int,int,int); extern int gpu_disp_x(void); extern int gpu_disp_y(void); extern uint32_t gpu_vram_hash(void);
extern uint32_t cd_cur_loc(void); extern long cd_xa_sectors;
static long n_mdec_w, n_dma_start[7];
static void dump_bios(void);
static uint32_t site_a[512]; static long site_n[512]; static int nsite;
long a2_t1reads, a2_t1eps; static uint64_t vtime;
static void dump(const char*why){ dump_bios(); { int ord[512]; for(int i=0;i<nsite;i++) ord[i]=i; for(int i=0;i<nsite;i++) for(int j=i+1;j<nsite;j++) if(site_n[ord[j]]>site_n[ord[i]]){ int t=ord[i]; ord[i]=ord[j]; ord[j]=t; } fprintf(stderr,"tick sites in overlay code (addr:count):"); for(int i=0;i<nsite&&i<12;i++) fprintf(stderr," %08x:%ld",site_a[ord[i]],site_n[ord[i]]); fprintf(stderr,"\n"); } fprintf(stderr,"STATE frame %llu module %u arg %u padraw1 %08x padraw2 %08x overlayA %d p1rec %d padflag %u buf1 %08x %08x\n",(unsigned long long)(vtime/200000),rd32(0x80060888),rd32(0x800608CC),rd32(0x8006AA88),rd32(0x8006AA8C),a2_get_overlay(),a2_p1_record(a2_get_p1()),rd32(0x80056C04),rd32(0x8006AA98),rd32(0x8006AA9C)); fprintf(stderr,"SIO bytes %ld transactions %ld\n",sio_bytes,sio_transactions); fprintf(stderr,"MDEC cmd writes %ld, DMA starts ch0 %ld ch1 %ld ch2 %ld ch6 %ld\n",n_mdec_w,n_dma_start[0],n_dma_start[1],n_dma_start[2],n_dma_start[6]); fprintf(stderr,"CD position LBA %u (A2STR starts 248942), XA audio sectors skipped %ld\n",cd_cur_loc(),cd_xa_sectors); gpu_stats(); fprintf(stderr,"GPU display start %d,%d  vram hash %08x\n",gpu_disp_x(),gpu_disp_y(),gpu_vram_hash()); if(getenv("A2PPM")) gpu_dump_ppm(getenv("A2PPM"),gpu_disp_x(),gpu_disp_y(),320,240); for(int i=0;i<256;i++) if(cd_cmd_count[i]) printf("  CD command %02x x%ld\n",i,cd_cmd_count[i]);
  printf("  IRQ callback table @8005f7c4:"); for(int i=0;i<11;i++) printf(" %08x",rd32(0x8005f7c4+4*i)); printf("\n  DMA callback table @8005f7f8:"); for(int i=0;i<7;i++) printf(" %08x",rd32(0x8005f7f8+4*i)); printf("\n  I_STAT %08x I_MASK %08x DICR %08x irq_on=%d cd_irq=%d\n",istat,imask,dicr,irq_on,cd_irq());
  printf("  CD callbacks @8005e31c..e32c:"); for(int i=0;i<5;i++) printf(" %08x",rd32(0x8005e31c+4*i)); printf("\n");
  printf("  table @8005e71c:"); for(int i=0;i<13;i++) printf(" %08x",rd32(0x8005e71c+4*i)); printf("\n");
  printf("STOP: %s after %d events (+%ld SPU register writes not listed)\n",why,nev,spu_writes);
  for(int i=(nev>60?nev-60:0);i<nev;i++){ EV*e=&ev[i]; switch(e->k){
    case 'B': printf("  BIOS %02x/%02x args %08x %08x\n",e->a,e->b,e->c,0); break;
    case 'R': printf("  HW read  %08x\n",e->a); break; case 'W': printf("  HW write %08x = %08x\n",e->a,e->b); break;
    case 'J': printf("  indirect jump to %08x\n",e->a); break; case 'C': printf("  indirect call to %08x\n",e->a); break;
    case 'D': printf("  DMA ch%d start MADR %08x BCR %08x (completed instantly)\n",e->a,e->b,e->c); break; case 'I': printf("  interrupt callback %08x delivered\n",e->a); break; case 'M': printf("  missing function %08x\n",e->a); break; case 'G': printf("  GTE %c %08x\n",'c',e->a); break; } }
  fflush(stdout); { extern void a2_alert(const char*,...); a2_alert("[a2pc] STOP: %s (after %d events)\n",why,nev); } exit(0); }
static void onalarm(int s){ fprintf(stderr,"last backward branches (MIPS addr):"); for(int i=0;i<8;i++) fprintf(stderr," %08x",tickhist[i]); fprintf(stderr,"\n"); void*bt[40]; int n=backtrace(bt,40); fprintf(stderr,"C call stack at timeout (innermost first; function f_XXXXXXXX = MIPS address):\n"); backtrace_symbols_fd(bt,n,2); dump("timeout (probably waiting for an interrupt / vblank)"); }
static uint8_t* P(uint32_t a){ uint32_t p=a&0x1fffffff; if(p<0x200000) return ram+p; if(p>=0x1f800000&&p<0x1f800400) return sp_+(p-0x1f800000); return NULL;}
static int isio(uint32_t a){ uint32_t p=a&0x1fffffff; return p>=0x1f801000&&p<0x1f802000; }

static long sio_r[32], sio_w[32];

extern long gpu_cycles; static uint64_t gpu_busy_until; static long gpu_cyc_seen; int a2_gpu_model=1; extern double (*a2_vbl_clock)(void); static double vbl_clock_fn(void); void a2_install_clock(void);
static void gpu_fold(void){ if(gpu_cycles==gpu_cyc_seen) return; uint64_t d=(uint64_t)((gpu_cycles-gpu_cyc_seen)*100/282); gpu_cyc_seen=gpu_cycles; if(gpu_busy_until<vtime) gpu_busy_until=vtime; gpu_busy_until+=d; }
static int a2_gpu_model_on(void){ static int on=-1; if(on<0) on=getenv("A2GPUMODEL")?atoi(getenv("A2GPUMODEL")):0; return on; }
static int gpu_busy(void){ if(a2_gpu_model_on()) gpu_fold(); else gpu_cyc_seen=gpu_cycles; return vtime<gpu_busy_until; }
static uint32_t ioread(uint32_t a){ uint32_t p=a&0x1fffffff; vtime+=7; if(p>=0x1f801040&&p<0x1f801050){ sio_step(vtime); return sio_read(p-0x1f801040,4); } if(p>=0x1f801040&&p<0x1f801060) sio_r[(p-0x1f801040)]++; if(p>=0x1f801800&&p<=0x1f801803) return cd_read_port(p-0x1f801800); if(p==0x1f801070){ return istat|(cd_irq()?4:0);} if(p==0x1f801074) return imask; if(p==0x1f8010f4) return (dicr&0x00ffffff)|(dicr&0x7f000000)|((((dicr>>23)&1)&&(((dicr>>24)&0x7f)&((dicr>>16)&0x7f)))?0x80000000u:0); if(p==0x1f801824) return 0x80040000u;
    if(p==0x1f801810) return gpu_read_data();
    if(p==0x1f801814) return ((gpu_status()&0x7fffffffu)&~(gpu_busy()?0x14000000u:0))|((uint32_t)((vtime/400)&1)<<31);
    if(p==0x1f8010a8&&gpu_busy()) return io[(p&0xfff)/4]|0x01000000u; if(p==0x1f801120) return (uint32_t)((vtime*3/8)&0xffff);
    if(p==0x1f801110||p==0x1f801100){ extern long a2_t1reads; if(p==0x1f801110){ static uint64_t lv; extern long a2_t1eps; if(vtime-lv>1500) a2_t1eps++; lv=vtime; } } if(p==0x1f801110) return (uint32_t)((vtime*263/200000)&0xffff);
    if(p==0x1f801100) return (uint32_t)((vtime/16)&0xffff); if(p>=0x1f801c00&&p<0x1f801e00) return spu_read(p); return io[(p&0xfff)/4]; }
static int pend_dma3, pend_dmax;

static unsigned a2_vs_n; static uint64_t a2_vs_frame; static int a2_vs_seen;
void a2_vsync_hook(unsigned n){ a2_vs_seen=1; if((int)n>=2&&n<=4){ a2_vs_n=n; a2_vs_frame=vtime/200000; } }
static int a2_auto_minflip(void){ if(!a2_vs_seen) return 2;
    return (a2_vs_n>=2&&vtime/200000-a2_vs_frame<=4)?(int)a2_vs_n:1; }
static void iowrite(uint32_t a,uint32_t v,int w){ uint32_t p=a&0x1fffffff; if(p>=0x1f801c00&&p<0x1f801e00) spu_write(p,v,w); if(p>=0x1f801040&&p<0x1f801060) sio_w[(p-0x1f801040)]++; if(p>=0x1f801040&&p<0x1f801050){ sio_step(vtime); sio_write(p-0x1f801040,v,w); return; } if(p==0x1f801820) n_mdec_w++; if(w==4&&p==0x1f801810) gpu_gp0(v); if(w==4&&p==0x1f801814){ gpu_gp1(v); if((v>>24)==5){ static uint32_t lastpos=~0u; static int minflip=-1; if(minflip<0) minflip=getenv("A2MINFLIP")?atoi(getenv("A2MINFLIP")):0; if((v&0xfffff)!=lastpos){ lastpos=v&0xfffff; int mf=minflip>0?minflip:a2_auto_minflip(); if(mf>1){ uint64_t t=(vtime/200000+(uint64_t)mf)*200000-30000; if(gpu_busy_until<t) gpu_busy_until=t; } } } } if(p==0x1f801070){ istat&=(w==4?v:(w==2?(v|0xffff0000u):(v|0xffffff00u))); return; } if(p==0x1f801074){ imask=(w==4?v:(w==2?((imask&0xffff0000u)|(v&0xffff)):((imask&0xffffff00u)|(v&0xff)))); return; } if(w==4&&p==0x1f8010f4){ dicr=(dicr&0x00ffffff&~0x00ffffffu)|(v&0x00ff803fu)|(dicr&0x7f000000&~(v&0x7f000000)); io[0xf4/4]=dicr; return; } if(p>=0x1f801800&&p<=0x1f801803){ cd_write_port(p-0x1f801800,v); return; } if(w==4) io[(p&0xfff)/4]=v; else if(w==2){ uint32_t o=io[(p&0xfff)/4]; int sh=(p&2)*8; io[(p&0xfff)/4]=(o&~(0xffffu<<sh))|(v&0xffffu)<<sh; } else { uint32_t o=io[(p&0xfff)/4]; int sh=(p&3)*8; io[(p&0xfff)/4]=(o&~(0xffu<<sh))|(v&0xffu)<<sh; }
  if(p==0x1f8010b8 && (v&0x01000000)){ io[(p&0xfff)/4]&=~0x01000000u; uint32_t madr=io[(0x1f8010b0&0xfff)/4]&0x1fffff, bcr=io[(0x1f8010b4&0xfff)/4]; uint32_t words=(bcr&0xffff)*((bcr>>16)?(bcr>>16):1); int n=cd_dma(ram+madr,words*4); if(getenv("A2CDLOG")){ fprintf(stderr,"CD DMA3 madr %08x words %u got %d bytes:",madr,words,n); for(int i=0;i<(n<16?n:16);i++) fprintf(stderr," %02x",ram[madr+i]); fprintf(stderr,"\n"); } pend_dma3=1; logev('D',3,madr,bcr); }
  if((p==0x1f8010a8||p==0x1f8010e8||p==0x1f8010a8-0x20||p==0x1f8010a8-0x10) && w==4 && (v&0x01000000)){
      int ch=(p-0x1f801080)/16; n_dma_start[ch]++; io[(p&0xfff)/4]&=~0x01000000u&~0x10000000u;
      if(ch==6){ uint32_t madr=io[(0x1f8010e0&0xfff)/4]&0x1ffffc, n=io[(0x1f8010e4&0xfff)/4]&0xffff; if(!n) n=0x10000; for(uint32_t i=0;i<n;i++){ uint32_t nx=(i==n-1)?0x00ffffffu:((madr-4)&0xffffff); ram[madr]=nx; ram[madr+1]=nx>>8; ram[madr+2]=nx>>16; ram[madr+3]=nx>>24; madr=(madr-4)&0x1ffffc; } }
      if(ch==2){ uint32_t madr=io[(0x1f8010a0&0xfff)/4]&0x1ffffc, bcr=io[(0x1f8010a4&0xfff)/4]; int mode=(v>>9)&3; uint32_t words=(bcr&0xffff)*((bcr>>16)?(bcr>>16):1);
          if(mode==2) gpu_dma_list(ram,madr); else if(v&1) gpu_dma_to(ram,madr,words); else gpu_dma_from(ram,madr,words); }
      logev('D',ch,io[((p&0xfff)-8)/4],io[((p&0xfff)-4)/4]); pend_dmax|=1<<ch; }
  if(p==0x1f8010c8 && (v&0x01000000)){ io[(p&0xfff)/4]&=~0x01000000u; { uint32_t madr=io[(0x1f8010c0&0xfff)/4]&0x1ffffc, bcr=io[(0x1f8010c4&0xfff)/4]; uint32_t words=(bcr&0xffff)*(((v>>9)&3)==1&&(bcr>>16)?(bcr>>16):1); if(madr+words*4<=0x200000) spu_dma(ram+madr,words*4,v&1); } pend_dma4=1; logev('D',4,io[(0x1f8010c0&0xfff)/4],io[(0x1f8010c4&0xfff)/4]); } }
static uint8_t rd8_slow(uint32_t a){ if(isio(a)){ uint32_t pp=a&0x1fffffff; logev('R',a,0,0); if(pp>=0x1f801040&&pp<0x1f801050){ vtime+=7; sio_step(vtime); return sio_read(pp-0x1f801040,1)&0xff; } if(pp>=0x1f801800&&pp<=0x1f801803) return ioread(a); return ioread(a)>>((a&3)*8); } uint8_t*p=P(a); if(!p){logev('R',a,0,0); return 0;} return *p;}
static uint16_t rd16_slow(uint32_t a){ if(isio(a)){ logev('R',a,0,0); { uint32_t pp=a&0x1fffffff; if(pp>=0x1f801040&&pp<0x1f801050){ vtime+=7; sio_step(vtime); return sio_read(pp-0x1f801040,2)&0xffff; } } return ioread(a)>>((a&2)*8); } uint8_t*p=P(a); if(!p){logev('R',a,0,0);return 0;} return p[0]|(p[1]<<8);} static uint32_t rd32_slow(uint32_t a){ if(isio(a)){ logev('R',a,0,0); return ioread(a); } uint8_t*p=P(a); if(!p){logev('R',a,0,0);return 0;} return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static void wcheck(uint32_t a,uint32_t v,int w){ static long wf=-1; if(wf<0) wf=getenv("A2WATCHF")?atol(getenv("A2WATCHF")):0; static uint32_t wv=0,wm=0; static int wvi; if(!wvi){ wvi=1; const char*e=getenv("A2WATCHV"); if(e){ wv=strtoul(e,0,16); const char*sl=strchr(e,'/'); wm=sl?strtoul(sl+1,0,16):0xffffffffu; } } if(watch_hi&&gpu_frame>=wf&&a>=watch_lo&&a<watch_hi&&(!wm||(v&wm)==wv)&&watch_n<(getenv("A2WATCHN")?atoi(getenv("A2WATCHN")):8)){ watch_n++; if(getenv("A2WATCHQ")){ fprintf(stderr,"W %08x=%x t=%llu F%ld\n",a,v,(unsigned long long)vtime,gpu_frame); return; } void*bt[40]; int n=backtrace(bt,40); fprintf(stderr,"WRITE %08x = %x (width %d) from:\n",a,v,w); backtrace_symbols_fd(bt,n,2); } }

static long pg_frame=-1, pg_hits, pg_drops;
static long cb_hits;
static int pg_drop(uint32_t a,uint32_t v,int w){ uint32_t q=a&0x1fffffffu;
    if(q>=0x5f7c4u&&q<0x5f7e4u&&v!=0&&!(w==4&&v>=0x80010000u&&v<0x80200000u&&!(v&3))){
        if(cb_hits++<5){ fprintf(stderr,"[a2pc] bad write into the VSync callback list %08x = %08x (width %d); recent loops:",a,v,w); for(int i=0;i<8;i++) fprintf(stderr," %08x",tickhist[(tickn+i)&7]); fprintf(stderr," - write dropped\n"); } return 1; } if(q<0x5e2f8u||q>=0x5e780u) return 0; long fr=(long)(vtime/200000);
    if(w==4&&((v>>24)==9||(v>>24)==10)&&(v&0xffffff)!=0){ if(pg_frame!=fr){ pg_frame=fr; if(pg_hits++<5){ fprintf(stderr,"[a2pc] primitive overflow into %08x (tag %08x) at frame %ld; recent loops:",a,v,fr); for(int i=0;i<8;i++) fprintf(stderr," %08x",tickhist[(tickn+i)&7]); fprintf(stderr," - writes there are dropped for this frame\n"); } } }
    if(pg_frame==fr){ pg_drops++; return 1; } return 0; }
static void wr8_slow(uint32_t a,uint32_t v){ if(pg_drop(a,v,1)) return; wcheck(a,v,1); if(isio(a)){logev('W',a,v,0); iowrite(a,v,1); return;} uint8_t*p=P(a); if(!p){logev('W',a,v,0);return;} *p=v;}
static void wr16_slow(uint32_t a,uint32_t v){ if(pg_drop(a,v,2)) return; wcheck(a,v,2); if(isio(a)){logev('W',a,v,0); iowrite(a,v,2); return;} uint8_t*p=P(a); if(!p){logev('W',a,v,0);return;} p[0]=v;p[1]=v>>8;}
static int skip_intro=-1;
static void wr32_slow(uint32_t a,uint32_t v){ if(a==0x80060888u && v==7){ if(skip_intro<0) skip_intro=getenv("A2SKIPINTRO")?1:0; if(skip_intro){   ram[0x521A8+4-0]=0x88; ram[0x521A8+5]=0xad; ram[0x521A8+6]=0x01; ram[0x521A8+7]=0x80; } }
if(pg_drop(a,v,4)) return; wcheck(a,v,4); if(isio(a)){logev('W',a,v,0); iowrite(a,v,4); return;} uint8_t*p=P(a); if(!p){logev('W',a,v,0);return;} p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}

#define A2_FASTW(q) ((q)<0x5e2f8u||((q)>=0x5e780u&&(q)<0x5f7c4u)||((q)>=0x5f7e4u&&(q)<0x200000u))
uint8_t rd8(uint32_t a){ uint32_t q=a&0x1fffffffu; if(q<0x200000u) return ram[q]; return rd8_slow(a); }
uint16_t rd16(uint32_t a){ uint32_t q=a&0x1fffffffu; if(q<0x200000u){ uint16_t x; memcpy(&x,ram+q,2); return x; } return rd16_slow(a); }
uint32_t rd32(uint32_t a){ uint32_t q=a&0x1fffffffu; if(q<0x200000u){ uint32_t x; memcpy(&x,ram+q,4); return x; } return rd32_slow(a); }
void wr8(uint32_t a,uint32_t v){ uint32_t q=a&0x1fffffffu; if(A2_FASTW(q)&&!watch_hi){ ram[q]=(uint8_t)v; return; } wr8_slow(a,v); }
void wr16(uint32_t a,uint32_t v){ uint32_t q=a&0x1fffffffu; if(A2_FASTW(q)&&!watch_hi){ uint16_t x=(uint16_t)v; memcpy(ram+q,&x,2); return; } wr16_slow(a,v); }
void wr32(uint32_t a,uint32_t v){ uint32_t q=a&0x1fffffffu; if(A2_FASTW(q)&&q!=0x60888u&&!watch_hi){ memcpy(ram+q,&v,4); return; } wr32_slow(a,v); }
#define find a2_find

static uint8_t* ovimg[5]; static long ovlen[5]; static const char* ovname[5]={"game00.bin","event00.bin","game01.bin","game02.bin","game03.bin"};
static uint8_t* p1img[32]; static long p1len[32];
void a2_ov_check(uint32_t a){
  static int inited; if(!inited){ inited=1; for(int i=0;i<5;i++){ FILE*f=fopen(ovname[i],"rb"); if(!f) continue; fseek(f,0,SEEK_END); ovlen[i]=ftell(f); fseek(f,0,SEEK_SET); ovimg[i]=malloc(ovlen[i]); if(fread(ovimg[i],1,ovlen[i],f)!=(size_t)ovlen[i]) ovlen[i]=0; fclose(f);}
    for(int i=0;i<a2_p1_count();i++){ char nm[32]; sprintf(nm,"p1_%d.bin",a2_p1_record(i)); FILE*f=fopen(nm,"rb"); if(!f) continue; fseek(f,0,SEEK_END); p1len[i]=ftell(f); fseek(f,0,SEEK_SET); p1img[i]=malloc(p1len[i]); if(fread(p1img[i],1,p1len[i],f)!=(size_t)p1len[i]) p1len[i]=0; fclose(f);} }
  if(a>=0x801493B0u){
    int cur=a2_get_p1(); const uint8_t*r=ram+0x1493B0;
    if(cur>=0 && p1img[cur] && p1len[cur]>=64 && !memcmp(r,p1img[cur],64)) return;
    for(int i=0;i<a2_p1_count();i++) if(p1img[i] && p1len[i]>=64 && !memcmp(r,p1img[i],64)){ if(i!=cur){ a2_set_p1(i); if(getenv("A2CDLOG")) fprintf(stderr,"overlay detected: p1_%d\n",a2_p1_record(i)); } return; }
    return; }
  int cur=a2_get_overlay(); const uint8_t*r=ram+0x84A98;
  if(cur>=0 && ovimg[cur] && !memcmp(r,ovimg[cur],64)) return;
  for(int i=0;i<5;i++) if(ovimg[i] && ovlen[i]>=64 && !memcmp(r,ovimg[i],64)){ if(i!=cur){ a2_set_overlay(i); if(getenv("A2CDLOG")) fprintf(stderr,"overlay detected: %s\n",ovname[i]); } return; }
}
void c_jump(CPU*c,uint32_t t){ void(*f)(CPU*)=a2_find(t); if(f){ f(c); return; } if(getenv("A2SOFT")){ static uint32_t seen[64]; static int ns; int k; for(k=0;k<ns;k++) if(seen[k]==t) break; if(k==ns && ns<64){ seen[ns++]=t; { extern void a2_alert(const char*,...); a2_alert("[a2pc] unknown indirect jump to %08x - treated as return; please report this address [module %u overlay %d p1 %d sp %08x ra %08x]\n",t,rd32(0x80060888),a2_get_overlay(),a2_p1_record(a2_get_p1()),c->r[29],c->r[31]); } } return; } logev('J',t,0,0); fprintf(stderr,"  indirect jump to %08x\n",t); dump("indirect jump to an address that is not a function start");}
extern int mcfs_open(const char*,uint32_t); extern int mcfs_lseek(int,int,int); extern int mcfs_nowait(int); extern int mcfs_read(int,uint8_t*,int); extern int mcfs_write(int,const uint8_t*,int); extern int mcfs_close(int); extern int mcfs_delete(const char*); extern int mcfs_first(const char*,uint8_t*); extern int mcfs_next(uint8_t*);
static void ram_str(uint32_t a,char*o,int n){ int i=0; for(;i<n-1;i++){ o[i]=(char)rd8(a+i); if(!o[i]) break; } o[i]=0; }
static int cardlog(void){ static int v=-1; if(v<0) v=getenv("A2CARDLOG")!=0; return v; }
static void card_event(uint32_t spec);
static void mc_driver(CPU*c){
    char p[64]; uint8_t e[40]; ram_str(c->r[5],p,64); int ok=mcfs_first(p,e); if(ok) for(int i=0;i<40;i++) wr8(c->r[6]+i,e[i]);
    if(cardlog()) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card firstfile \"%s\" -> %s\n",p,ok?(char*)e:"none"); c->r[2]=ok?c->r[6]:0; if(!ok) card_event(0x0004);   }
void c_call(CPU*c,uint32_t t){ if(t==0x80000c00u){ mc_driver(c); return; } { static int on=-1; if(on<0) on=getenv("A2OPLOG")!=0; if(on){ extern long gpu_frame; static uint32_t tb[236]; if(!tb[1]||(!tb[0]&&0)) for(int i=0;i<236;i++) tb[i]=rd32(0x800ee6f4+4*i); uint32_t ra=c->r[31]; if(ra==0x80098c90u||ra==0x8009ac90u||ra==0x8009acfcu||ra==0x8009e2a0u||(ra>=0x8009ac00u&&ra<0x8009b000u)||(ra>=0x8009e200u&&ra<0x8009e600u)){ for(int i=0;i<236;i++) if(tb[i]==t){ fprintf(stderr,"OP F%ld %d ra=%08x a1=%08x\n",gpu_frame,i,ra,c->r[5]); break; } } } }  if(t==0xa0||t==0xb0||t==0xc0){ c_bios(c,t,c->r[9]); return; } void(*f)(CPU*)=find(t); if(!f && getenv("A2SOFT")){ static uint32_t seen[64]; static int ns; int k; for(k=0;k<ns;k++) if(seen[k]==t) break; if(k==ns && ns<64){ seen[ns++]=t; { extern void a2_alert(const char*,...); a2_alert("[a2pc] unknown indirect call to %08x (from %08x) - skipped; please report this address [module %u overlay %d p1 %d sp %08x ra %08x]\n",t,c->r[31],rd32(0x80060888),a2_get_overlay(),a2_p1_record(a2_get_p1()),c->r[29],c->r[31]); } } c->r[2]=0; return; } if(!f){logev('C',t,0,0); fprintf(stderr,"UNKNOWN CALL from ra %08x module %u overlay %d\n  indirect call to %08x\n",c->r[31],rd32(0x80060888),a2_get_overlay(),t); dump("indirect call to unknown address");} f(c);}
void c_bios(CPU*c,uint32_t t,uint32_t n);
static long bioscnt[3][256]; static uint32_t intrp[16]; static int nintrp;

typedef struct{ uint32_t cls, spec, mode, fn; int used, en, pend; }BEv; static BEv bev[64];
static void run_cb(CPU*c,uint32_t cb,const char*what);
static CPU*bev_cpu;
static void bev_deliver(uint32_t cls,uint32_t spec){ for(int i=0;i<64;i++) if(bev[i].used&&bev[i].en&&bev[i].cls==cls&&bev[i].spec==spec){ if(bev[i].mode==0x1000&&bev[i].fn&&bev_cpu){ if(getenv("A2BIOSLOG")) fprintf(stderr,"event callback %08x (cls %08x spec %04x)\n",bev[i].fn,cls,spec); run_cb(bev_cpu,bev[i].fn,"event"); } else bev[i].pend=1; } }
extern int mc_open(const char*); extern int mc_present(int); extern int mc_read(int,int,uint8_t*); extern int mc_write(int,int,const uint8_t*); extern void mc_tick(void);
static void card_event(uint32_t spec){ bev_deliver(0xf0000011u,spec); bev_deliver(0xf4000001u,spec); }
static int bios_hle(CPU*c,uint32_t t,uint32_t n){ bev_cpu=c;
    if(t==0xb0){ switch(n){
        case 0x08: { for(int i=0;i<64;i++) if(!bev[i].used){ bev[i]=(BEv){c->r[4],c->r[5],c->r[6],c->r[7],1,0,0}; c->r[2]=0xf1000000u+i; return 1; } c->r[2]=0xffffffffu; return 1; }
        case 0x09: { uint32_t i=c->r[4]-0xf1000000u; if(i<64) bev[i].used=0; c->r[2]=1; return 1; }
        case 0x0a: { uint32_t i=c->r[4]-0xf1000000u; if(i<64&&bev[i].pend){ bev[i].pend=0; c->r[2]=1; } else c->r[2]=1; return 1; }
        case 0x0b: { uint32_t i=c->r[4]-0xf1000000u; if(i<64&&bev[i].used&&bev[i].pend){ bev[i].pend=0; c->r[2]=1; } else c->r[2]=0; return 1; }
        case 0x0c: { uint32_t i=c->r[4]-0xf1000000u; if(i<64) bev[i].en=1; c->r[2]=1; return 1; }
        case 0x0d: { uint32_t i=c->r[4]-0xf1000000u; if(i<64) bev[i].en=0; c->r[2]=1; return 1; }
        case 0x07: bev_deliver(c->r[4],c->r[5]); c->r[2]=0; return 1;
        case 0x32: { char p[64]; ram_str(c->r[4],p,64); int fd=mcfs_open(p,c->r[5]); if(cardlog()) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card open \"%s\" mode %x -> %d\n",p,c->r[5],fd); c->r[2]=(uint32_t)fd; if(fd>=0&&(c->r[5]&0x8000)) card_event(0x0004); return 1; }
        case 0x33: { int r=mcfs_lseek(c->r[4],(int)c->r[5],c->r[6]); if(cardlog()) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card lseek fd %d %d -> %d\n",c->r[4],(int)c->r[5],r); c->r[2]=(uint32_t)r; return 1; }
        case 0x34: case 0x35: { int fd=(int)c->r[4]; uint32_t buf=c->r[5]; int len=(int)c->r[6];
            if(n==0x35 && fd<3){ c->r[2]=len; return 1; }
            if(fd<3){ c->r[2]=(uint32_t)-1; return 1; }
            uint8_t*tmp=malloc(len>0?len:1); int r; if(n==0x34){ r=mcfs_read(fd,tmp,len); if(r>0) for(int i=0;i<r;i++) wr8(buf+i,tmp[i]); } else { for(int i=0;i<len;i++) tmp[i]=rd8(buf+i); r=mcfs_write(fd,tmp,len); }
            free(tmp); if(cardlog()) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card %s fd %d len %d -> %d\n",n==0x34?"read":"write",fd,len,r);
            if(r<0) c->r[2]=(uint32_t)-1; else if(mcfs_nowait(fd)){ c->r[2]=0; card_event(0x0004); } else c->r[2]=(uint32_t)r; return 1; }
        case 0x36: { int r=mcfs_close(c->r[4]); if(cardlog()) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card close fd %d\n",c->r[4]); c->r[2]=(uint32_t)r; return 1; }
        case 0x42: case 0x43: { uint8_t e[40]; int ok; char p[64]=""; uint32_t d;
            if(n==0x42){ uint32_t name=c->r[4]; d=c->r[5]; ram_str(name,p,64); uint32_t fn=0;
                for(int i=0;i<2;i++){ uint32_t ent=0xA00+i*0x50; char nm[8]; ram_str(rd32(ent),nm,8); if(!strcmp(nm,"bu")&&!strncmp(p,"bu",2)){ fn=rd32(ent+0x34); break; } }
                if(fn && fn!=0x80000c00u){ c->r[4]=0x80000B80u; c->r[5]=name; c->r[6]=d; c_call(c,fn); return 1; }
                ok=mcfs_first(p,e); }
            else { d=c->r[4]; ok=mcfs_next(e); }
            if(ok) for(int i=0;i<40;i++) wr8(d+i,e[i]);
            if(cardlog()) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card %s \"%s\" -> %s\n",n==0x42?"firstfile":"nextfile",p,ok?(char*)e:"none"); c->r[2]=ok?d:0; return 1; }
        case 0x45: { char p[64]; ram_str(c->r[4],p,64); int r=mcfs_delete(p); if(cardlog()) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card delete \"%s\" -> %d\n",p,r); c->r[2]=(uint32_t)r; return 1; }
        case 0x4a: case 0x4b: case 0x4c: c->r[2]=1; return 1;
        case 0x4e: case 0x4f: {
            int slot=(c->r[4]>>4)&15; int ok; uint8_t tmp[128];
            if(n==0x4f){ ok=mc_read(slot,c->r[5],tmp); if(ok) for(int i=0;i<128;i++) wr8(c->r[6]+i,tmp[i]); }
            else { for(int i=0;i<128;i++) tmp[i]=rd8(c->r[6]+i); ok=mc_write(slot,c->r[5],tmp); }
            if(getenv("A2CARDLOG")) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card %s port %02x sector %u -> %s\n",n==0x4f?"read":"write",c->r[4],c->r[5],ok?"ok":"no card");
            card_event(ok?0x0004:0x0100); c->r[2]=1; return 1; }
        case 0x50: c->r[2]=1; return 1; } }
    if(t==0xa0 && n==0xab && getenv("A2CARDINFO")){ static int ci; char*e=strdup(getenv("A2CARDINFO")),*sv=0,*tk; int idx=0,hw=4,sw=4; for(tk=strtok_r(e,",",&sv);tk;tk=strtok_r(0,",",&sv),idx++){ if(idx>ci) break; sscanf(tk,"%x:%x",&hw,&sw); } free(e); ci++; if(getenv("A2CARDLOG")) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card info #%d hw %x sw %x\n",ci,hw,sw); if(hw) bev_deliver(0xf0000011u,hw); if(sw) bev_deliver(0xf4000001u,sw); c->r[2]=1; return 1; }
    if(t==0xa0 && (n==0xab||n==0xac)){ int slot=(c->r[4]>>4)&15; if(getenv("A2CARDLOG")) (fprintf(stderr,"[F%ld] ",gpu_frame),0)?0:fprintf(stderr,"card %s port %02x -> %s\n",n==0xab?"info":"load",c->r[4],mc_present(slot)?"ok":"no card"); card_event(mc_present(slot)?0x0004:0x0100); c->r[2]=1; return 1; }
    if(t==0xa0 && n>=0xa6 && n<=0xad){ c->r[2]=1; return 1; }
    return 0; }
void c_bios(CPU*c,uint32_t t,uint32_t n){ if(getenv("A2BIOSLOG") && !((t==0xc0&&n==0x0a)||(t==0xb0&&(n==0x35||n==0x5b||n==0x0b)))){ static int bl; if(bl++<(getenv("A2BIOSLOGN")?atoi(getenv("A2BIOSLOGN")):400)) fprintf(stderr,"BIOSCALL %02x:%02x a0=%08x a1=%08x a2=%08x a3=%08x t=%llu\n",t,n,c->r[4],c->r[5],c->r[6],c->r[7],(unsigned long long)(vtime/200000)); } if(bios_hle(c,t,n)){ bioscnt[(t>>4)&3?((t==0xb0)?1:2):0][n&255]++; return; } if(t==0xc0&&n==2&&nintrp<16){ intrp[nintrp++]=c->r[5]; if(getenv("A2CDLOG")) fprintf(stderr,"SysEnqIntRP prio %u struct %08x f4 %08x f8 %08x\n",c->r[4],c->r[5],rd32(c->r[5]+4),rd32(c->r[5]+8)); } if(t==0xc0&&n==3){ for(int i=0;i<nintrp;i++) if(intrp[i]==c->r[5]){ intrp[i]=intrp[--nintrp]; break; } } bioscnt[(t>>4)&3?((t==0xb0)?1:2):0][n&255]++; logev('B',t,n,c->r[4]); if(nev>(getenv("A2MAXEV")?atoi(getenv("A2MAXEV")):60000)) dump("too many events"); c->r[2]=0; }
void c_missing(CPU*c,uint32_t a){logev('M',a,0,0); dump("call to function that is not in the main exe");}
void c_syscall(CPU*c,uint32_t x){ logev('B',0xffff,c->r[4],0); if(c->r[4]==1){ c->r[2]=irq_on; irq_on=0; } else if(c->r[4]==2){ irq_on=1; c->r[2]=0; } }
static void run_cb(CPU*c,uint32_t cb,const char*what){ if(!cb) return; void(*f)(CPU*)=a2_find(cb); if(!f){ logev('C',cb,0,0); return; } CPU t=*c; t.r[29]=0x8000fff0u; t.r[31]=0; irq_on=0; f(&t); irq_on=1; logev('I',cb,0,0); (void)what; }
static void dma_done(int ch){ if((dicr>>(16+ch))&1) dicr|=1u<<(24+ch); if(((dicr>>23)&1) && ((dicr>>24)&0x7f)&((dicr>>16)&0x7f)) istat|=8; }

typedef struct{ long f0, f1; uint16_t m; }PadEv; static PadEv padev[256]; static int npadev;
static uint16_t padname(const char*s){ static const struct{const char*n; int b;} t[]={{"SELECT",0},{"START",3},{"UP",4},{"RIGHT",5},{"DOWN",6},{"LEFT",7},{"L2",8},{"R2",9},{"L1",10},{"R1",11},{"TRI",12},{"O",13},{"X",14},{"CROSS",14},{"CIRCLE",13},{"TRIANGLE",12},{"SQUARE",15},{"SQ",15}}; for(unsigned i=0;i<sizeof t/sizeof*t;i++) if(!strcmp(s,t[i].n)) return 1u<<t[i].b; return 0; }
static void pad_parse(const char*e){ char*cp=strdup(e); char*sp1; for(char*tok=strtok_r(cp,",",&sp1);tok&&npadev<256;tok=strtok_r(0,",",&sp1)){ long f=0,d=0; char nm[64]=""; if(sscanf(tok,"%ld:%ld:%63s",&f,&d,nm)<3) continue; uint16_t m=0; { char*sp2; for(char*n=strtok_r(nm,"+",&sp2);n;n=strtok_r(0,"+",&sp2)) m|=padname(n); } padev[npadev].f0=f; padev[npadev].f1=f+d; padev[npadev].m=m; npadev++; } free(cp); }
static uint16_t pad_mask_at(long fr){ uint16_t m=0; for(int i=0;i<npadev;i++) if(fr>=padev[i].f0&&fr<padev[i].f1) m|=padev[i].m; return m; }
extern int a2_win_init(void); extern uint16_t a2_win_frame(int,int); extern int a2_win_quit(void); static int win_on; static uint16_t win_pad;

static void mctest_run(CPU*c0){
    CPU c=*c0; c.r[29]=0x801d8000u; uint32_t buf=0x801d0000u, buf2=0x801d4000u, nm=0x801d7000u, cm=0x801d7100u, rs=0x801d7104u;
    const char*name="bu00:BASLUS-01017TEST"; for(int i=0;i<=(int)strlen(name);i++) wr8(nm+i,name[i]);
    for(int i=0;i<3584;i++){ wr8(buf+i,(uint8_t)(i*7+3)); wr8(buf2+i,0); }
    struct { const char*n; uint32_t fn; } st[4]={{"create",0x8003f06c},{"write",0x8003ea88},{"read",0x8003e830},{"dummy",0}};
    for(int k=0;k<3;k++){
        c.r[29]=0x801d8000u; c.r[4]=0; c.r[5]=nm+5; c.r[31]=0;
        if(k==0){ c.r[6]=1; } else { c.r[6]=(k==1)?buf:buf2; c.r[7]=0; wr32(c.r[29]+16,3584); }
        void(*f)(CPU*)=find(st[k].fn); f(&c);
        uint32_t first=c.r[2]; int ret=-99, i;
        for(i=0;i<20000;i++){ CPU t=*c0; t.r[29]=0x801d9000u; t.r[4]=0; t.r[5]=cm; t.r[6]=rs; t.r[31]=0; void(*sy)(CPU*)=find(0x8003ef50); sy(&t); ret=(int)t.r[2]; if(ret!=1) break; a2_tick(&t,0); }
        fprintf(stderr,"MCTEST %s: start=%d sync=%d after %d ticks cmd=%u res=%u\n",st[k].n,(int)first,ret,i,rd32(cm),rd32(rs));
    }
    int bad=0; for(int i=0;i<3584;i++) if(rd8(buf+i)!=rd8(buf2+i)) bad++;
    fprintf(stderr,"MCTEST readback mismatches: %d\n",bad);
}

extern void cd_dump(FILE*f);
static void stall_report(void){ FILE*fs=fopen("a2pc_stall.txt","a"); FILE*o[1]={fs}; fprintf(stderr,"[a2pc] long wait logged in a2pc_stall.txt (normal while idle on a menu; if the game is really stuck, send that file)\n");
    for(int k=0;k<1;k++){ FILE*f=o[k]; if(!f) continue; fprintf(f,"--- the game has waited in the same loop for ~30 s of game time\nframe %llu module %u arg %u overlay %d\nrecent wait loops:",(unsigned long long)last_vbl,rd32(0x80060888),rd32(0x800608CC),a2_get_overlay()); for(int i=0;i<8;i++) fprintf(f," %08x",tickhist[(tickn+i)&7]); fprintf(f,"\nloader queue @800610E8:"); for(int i=0;i<24;i++) fprintf(f," %08x",rd32(0x800610E8+4*i)); fprintf(f,"\nI_STAT %08x I_MASK %08x irq table:",istat,imask); for(int i=0;i<12;i++) fprintf(f," %08x",rd32(0x8005e71c+4*i)); fprintf(f,"\n"); cd_dump(f); fprintf(f,"primitive-overflow hits %ld drops %ld\n",pg_hits,pg_drops); }
    if(fs) fclose(fs); }
int a2_force_l2; static uint32_t minsp=0x80200000u;

static void voice_test(CPU*c){ static int st=-1; static char nm[32]; static long fr; static int fn; if(st<0){ st=0; const char*e=getenv("A2VOICETEST"); if(e){ char b[64]; fn=0; if(sscanf(e,"%31[^:]:%ld:%d",b,&fr,&fn)>=2){ strcpy(nm,b); st=1; } } }
    if(st==2){ static long lf; if(gpu_frame>=lf){ lf=gpu_frame+20; static int n; if(n++<12) fprintf(stderr,"[voicetest] F%ld state56eec=%x d728=%u d6f4=%u d714=%u d718=%u d72c=%u d70c=%u\n",gpu_frame,rd32(0x80056eec),rd32(0x8007d728),rd8(0x8007d6f4),rd32(0x8007d714),rd32(0x8007d718),rd32(0x8007d72c),rd32(0x8007d70c)); } }
    if(st!=1||gpu_frame<fr) return; st=2; extern void f_80030784(CPU*), f_80030658(CPU*), f_80030430(CPU*); CPU t=*c; uint32_t sa=0x801d0000u; memcpy(ram+(sa&0x1fffff),nm,strlen(nm)+1); t.r[4]=sa; t.r[29]=c->r[29]-0x600;
    fprintf(stderr,"[voicetest] flag80056ef4=%u busy80056ee8=%u 80056eec=%u d700=%08x d704=%08x\n",rd32(0x80056ef4),rd32(0x80056ee8),rd32(0x80056eec),rd32(0x8007d700),rd32(0x8007d704)); { extern void f_80030254(CPU*), f_80031468(CPU*); CPU u=*c; u.r[4]=sa; u.r[5]=sa+0x100; u.r[6]=sa+0x104; u.r[29]=c->r[29]-0x800; f_80030254(&u); fprintf(stderr,"[voicetest] lookup idx=%d mono=%d (v0=%d)\n",(int)rd32(sa+0x100),(int)rd32(sa+0x104),(int)u.r[2]); } fprintf(stderr,"[voicetest] F%ld calling %s with \"%s\"\n",gpu_frame,fn==1?"f_80030658":fn==2?"f_80030430":"f_80030784",nm); if(fn==1) f_80030658(&t); else if(fn==2) f_80030430(&t); else f_80030784(&t); fprintf(stderr,"[voicetest] returned\n"); }
void a2_tick(CPU*c,uint32_t at){ voice_test(c); if(c->r[29]>0x80100000u&&c->r[29]<minsp) minsp=c->r[29]; { static unsigned long sn; static int sfr, nrep; static uint32_t sigs[6]; if(nrep<6&&!(++sn&0xffff)){ int d=0; uint32_t u[8]; for(int i=0;i<8;i++){ int f=0; for(int j=0;j<d;j++) if(u[j]==tickhist[i]) f=1; if(!f) u[d++]=tickhist[i]; } if(d<=2) sfr++; else sfr=0; if(sfr>=1800){ uint32_t sg=u[0]^(d>1?u[1]*31:0); int seen=0; for(int j=0;j<nrep;j++) if(sigs[j]==sg) seen=1; if(!seen){ sigs[nrep++]=sg; stall_report(); } sfr=0; } } } tickhist[tickn++&7]=at; if(at>=0x80084A98u){ static short sh[1024]; unsigned h=((at>>2)*2654435761u)>>22; int k; for(;;h=(h+1)&1023){ k=sh[h]-1; if(k<0){ if(nsite<512){ k=nsite++; site_a[k]=at; sh[h]=(short)(k+1); } break; } if(site_a[k]==at) break; } if(k>=0) site_n[k]++; } ticks++; vtime+=3; cd_step(vtime); sio_step(vtime); { static int pinit; if(!pinit){ pinit=1; if(getenv("A2PAD")) pad_parse(getenv("A2PAD")); } { static uint64_t pm_next; static uint16_t pm; if(vtime>=pm_next){ long fr_=vtime/200000; pm=pad_mask_at(fr_); pm_next=(uint64_t)(fr_+1)*200000; } sio_set_buttons(pm|win_pad); } { static int sk=-1; static long s0,s1; static int slx,sly; extern void sio_set_sticks(uint8_t,uint8_t,uint8_t,uint8_t); extern void sio_set_digital(void); if(sk<0){ sk=0; if(getenv("A2PADMODE")&&getenv("A2PADMODE")[0]=='d') sio_set_digital(); if(getenv("A2STICK")&&sscanf(getenv("A2STICK"),"%ld:%ld:%d:%d",&s0,&s1,&slx,&sly)==4) sk=1; } if(sk==1){ long fr=vtime/200000; if(fr>=s0&&fr<s1) sio_set_sticks(0x80,0x80,slx,sly); else sio_set_sticks(0x80,0x80,0x80,0x80); } } if(sio_irq_take()){ istat|=0x80; if(getenv("A2SIOLOG")) fprintf(stderr,"IRQ7 raised t=%llu\n",(unsigned long long)vtime);} }
    for(int ch=0;ch<7;ch++) if(pend_dmax&(1<<ch)){ pend_dmax&=~(1<<ch); dma_done(ch); } if(pend_dma4){ pend_dma4=0; dma_done(4); } if(pend_dma3){ pend_dma3=0; dma_done(3); }
    if((vtime/200000)!=last_vbl){ last_vbl=vtime/200000; { extern long a2_vbl_now; a2_vbl_now=(long)last_vbl; } istat|=1; mc_tick(); spu_frame(); if(getenv("A2HICHK")&&last_vbl%120==0){ extern void gpu_hi_check(void); gpu_hi_check(); } if(getenv("A2FPS")&&last_vbl%120==0){ extern long a2_gp05, gpu_cycles; static long lc; fprintf(stderr,"DISPSTART F%ld flips %ld gpu/vbl %.2f\n",(long)last_vbl,a2_gp05,(gpu_cycles-lc)/120.0/564480.0); a2_gp05=0; lc=gpu_cycles; { extern long a2_flip_hist[8]; fprintf(stderr,"FLIPHIST F%ld",(long)last_vbl); for(int q=0;q<8;q++){ fprintf(stderr," %ld",a2_flip_hist[q]); a2_flip_hist[q]=0; } fprintf(stderr,"\n"); } } if(getenv("A2FPS")){ static int ld=-1; static long flips; int dd=gpu_disp_y()*1024+gpu_disp_x(); if(dd!=ld) flips++; ld=dd; if((long)last_vbl%120==0){  } } if(getenv("A2FPS")){ static struct timespec tp; static double mx; struct timespec tn; clock_gettime(CLOCK_MONOTONIC,&tn); if(tp.tv_sec){ double d=(tn.tv_sec-tp.tv_sec)*1e3+(tn.tv_nsec-tp.tv_nsec)/1e6; if(d>mx) mx=d; } tp=tn; if(last_vbl%120==0){ fprintf(stderr,"WORST vblank ms %.1f (frame %ld)\n",mx,(long)last_vbl); mx=0; } } if(getenv("A2FPS")){ static struct timespec t0; static long n0; if((long)last_vbl%120==0){ struct timespec t1; clock_gettime(CLOCK_MONOTONIC,&t1); double dt=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9; if(n0&&dt>0) fprintf(stderr,"FPS vblank/s %.1f (frame %ld, gpu_frame %ld)\n",120/dt,(long)last_vbl,(long)gpu_frame); t0=t1; n0=1; } } { static int pgt; if(getenv("A2PGTEST")&&!pgt&&(long)last_vbl>=atol(getenv("A2PGTEST"))){ pgt=1; wr32(0x8005e400,0x9050000); wr32(0x8005e404,0x2c808080); fprintf(stderr,"PGTEST after tag: %08x %08x\n",rd32(0x8005e400),rd32(0x8005e404)); } if(getenv("A2PGTEST")&&pgt==1&&(long)last_vbl>=atol(getenv("A2PGTEST"))+2){ pgt=2; wr32(0x8005e404,0x1234); fprintf(stderr,"PGTEST next frames: %08x (want 1234)\n",rd32(0x8005e404)); } } { static int mcdone; const char*e=getenv("A2MCTEST"); if(e&&!mcdone&&(long)last_vbl>=atol(e)){ mcdone=1; mctest_run(c); } } { extern long gpu_frame; gpu_frame=(long)last_vbl; } { extern int a2_force_l2; if(a2_force_l2){ uint32_t p88=rd32(0x80134e90u+88),p96=rd32(0x80134e90u+96),n=rd32(0x80134e90u+100); if(p88>=0x80100000u&&p88<0x80200000u&&p96>=0x80100000u&&p96<0x80200000u&&n>=60&&n<=0x4000u&&rd32(0x8012b7bcu)==0) wr32(0x8012b7bcu,1); } } { static int pk; const char*e=getenv("A2POKE"); if(e&&!pk){ long f=atol(e); const char*a=strchr(e,':'); if(a&&(long)last_vbl>=f){ pk=1; uint32_t ad=strtoul(a+1,0,16); const char*v=strchr(a+1,':'); uint32_t val=v?strtoul(v+1,0,16):0; wr32(ad,val); fprintf(stderr,"[a2pc] A2POKE F%ld %08x=%08x\n",(long)last_vbl,ad,val); } } } if(win_on){ { extern int a2_hold_present; static int lastd=-1, still, held; int dd=gpu_disp_y()*1024+gpu_disp_x(); if(dd==lastd) still++; else still=0; lastd=dd;   if(getenv("A2HOLD")&&a2_gpu_model_on()&&still>=3&&gpu_busy()&&held<2){ a2_hold_present=1; held++; } else { a2_hold_present=0; held=0; } } { extern int a2_interp_on, a2_gte_mark, a2_hold_present; extern double a2_interp_t, a2_flip_tb, a2_flip_ta, a2_pace_rate; static int R=-1;
              if(R<0){ const char*e=getenv("A2UNLOCK"); R=e?atoi(e):0; extern int a2_nat_interp, a2_hi; if(R>=60&&(a2_nat_interp||a2_hi>1)){ if(R>240) R=240; a2_interp_on=1; a2_gte_mark=1; a2_pace_rate=R; } else R=0; }
              if(R<=0){ a2_hold_present=0; win_pad=a2_win_frame(gpu_disp_x(),gpu_disp_y()); }
              else {
                  { static long uo=-1; if(uo<0) uo=getenv("A2UNLOCKOFF")?atol(getenv("A2UNLOCKOFF")):0; if(uo&&(long)last_vbl>=uo&&a2_interp_on){ a2_interp_on=0; a2_pace_rate=0; R=0; } }
                  if(R<=0){ a2_hold_present=0; win_pad=a2_win_frame(gpu_disp_x(),gpu_disp_y()); } else {
                  long m0=((long)last_vbl*R+59)/60, m1=((long)(last_vbl+1)*R+59)/60; double dt=60.0/R;
                  for(long m=m0;m<m1;m++){ { extern double a2_win_lag(void); extern void a2_win_skip(void); if(m<m1-1&&a2_win_lag()>dt/60.0*0.9){ a2_win_skip(); continue; } }
                      double tau=(double)m*dt, iv=a2_flip_tb-a2_flip_ta; double tt=1.0; if(a2_flip_ta>=0&&iv>=0.5&&iv<=4.0&&(double)last_vbl-a2_flip_tb<8.0){ tt=(tau-a2_flip_tb+dt)/iv; if(tt<0) tt=0; if(tt>1) tt=1; } a2_interp_t=tt; a2_hold_present=0; { static int ul=-1,nl; if(ul<0) ul=getenv("A2UNLOCKLOG")!=0; if(ul&&last_vbl>4300&&(double)last_vbl-a2_flip_tb<6&&nl++<48) fprintf(stderr,"UNLOCK vbl %ld slot %ld tau %.2f flipB %.2f flipA %.2f t %.3f\n",(long)last_vbl,m,tau,a2_flip_tb,a2_flip_ta,tt); } win_pad=a2_win_frame(gpu_disp_x(),gpu_disp_y()); } } } } if(a2_win_quit()){ fprintf(stderr,"window closed\n"); exit(0);} } if(getenv("A2VRAMDUMP")){ extern int gpu_vram_save(const char*); char*e=strdup(getenv("A2VRAMDUMP")),*sv=0; for(char*tk=strtok_r(e,",",&sv);tk;tk=strtok_r(0,",",&sv)){ char*cl=strchr(tk,':'); if(cl&&(uint64_t)atoi(tk)==last_vbl) gpu_vram_save(cl+1); } free(e); } if(getenv("A2RAMDUMP")){   char*e=strdup(getenv("A2RAMDUMP")),*sv=0; for(char*tk=strtok_r(e,",",&sv);tk;tk=strtok_r(0,",",&sv)){ char*cl=strchr(tk,':'); if(cl&&(uint64_t)atoi(tk)==last_vbl){ FILE*f=fopen(cl+1,"wb"); if(f){ fwrite(ram,1,sizeof ram,f); fclose(f); fprintf(stderr,"RAMDUMP frame %llu -> %s\n",(unsigned long long)last_vbl,cl+1);} } } free(e); } if(getenv("A2SITERESET")&&last_vbl==(uint64_t)atoi(getenv("A2SITERESET"))){ nsite=0; memset(site_n,0,sizeof site_n); } if(getenv("A2GPULOG")&&last_vbl%25==0&&last_vbl<3000) fprintf(stderr,"--- frame %llu\n",(unsigned long long)last_vbl); if(getenv("A2GPUHIST")&&last_vbl%(atoi(getenv("A2GPUHIST")))==0){ extern void gpu_hist_print(void); fprintf(stderr,"frame %llu ",(unsigned long long)last_vbl); gpu_hist_print(); } if(getenv("A2PADLOG")&&last_vbl%(getenv("A2TICK")?atoi(getenv("A2TICK")):600)==0) { extern void gpu_screen_stats(int,int,char*,int); extern void gpu_rect_summary(char*,int); char ss[160],rs[120]; gpu_screen_stats(gpu_disp_x(),gpu_disp_y(),ss,160); gpu_rect_summary(rs,120); { extern long gpu_prims[8]; static long lp; extern long gpu_rejected,gpu_rej_px; static long lr,lrp; fprintf(stderr,"rejpx+%ld ",gpu_rej_px-lrp); lrp=gpu_rej_px; fprintf(stderr,"polys+%ld rej+%ld ",gpu_prims[1]-lp,gpu_rejected-lr); lp=gpu_prims[1]; lr=gpu_rejected; } { fprintf(stderr,"fade:"); for(int k=0;k<44;k+=4) fprintf(stderr," %08x",rd32(0x80067880+k)); fprintf(stderr," sel/state %08x startmap %u map8012B748 %u 8012B74C %u ",rd32(0x8014B2AC),rd16(0x800676DC),rd32(0x8012B748),rd32(0x8012B74C)); } { uint32_t sp=rd32(0x80147898); fprintf(stderr,"menu@%08x: w0 %08x w4 %08x w12 %08x w16 %08x w1052 %08x slot-status",sp,rd32(sp),rd32(sp+4),rd32(sp+12),rd32(sp+16),rd32(sp+1052)); if(sp) for(int q=0;q<3;q++) fprintf(stderr," %04x",rd16(sp+q*480+40)); fprintf(stderr," "); } { extern void gpu_render_stats(char*,int); char rs2[100]; gpu_render_stats(rs2,100); fprintf(stderr,"TV(%s) ",rs2); } fprintf(stderr,"minsp %08x ",minsp); fprintf(stderr,"tick frame %llu [%s] rects[%s] module %u arg %u lba %u vramhash %08x ovA %d p1 %d\n",(unsigned long long)last_vbl,ss,rs,rd32(0x80060888),rd32(0x800608CC),cd_cur_loc(),gpu_vram_hash(),a2_get_overlay(),a2_p1_record(a2_get_p1())); } if(getenv("A2PADLOG")){ static uint32_t lastraw=1; uint32_t w=rd32(0x8006AA88)^(rd32(0x8006AA98)<<1); if(w!=lastraw){ lastraw=w; w=rd32(0x8006AA88); fprintf(stderr,"frame %llu padraw %08x struct %08x %08x module %u flag56c04 %u buf %08x\n",(unsigned long long)last_vbl,w,rd32(0x80061EF8),rd32(0x80061EFC),rd32(0x80060888),rd32(0x80056C04),rd32(0x8006AA98),rd32(0x8006AA9C)); } } }
    if(cd_irq()) istat|=4; else istat&=~4u;
    if(!irq_on) return;
    uint32_t pend=istat&imask&0x7ff; if((pend&0x80)&&!rd32(0x8005e720+4*7)) pend&=~0x80u; if(!pend) return;
    int irq=__builtin_ctz(pend);
    if(irq==0 && !getenv("A2NOCHAIN")){ irq_on=0; uint64_t vt0=vtime; for(int i=0;i<nintrp;i++){ uint32_t vf=rd32(intrp[i]+8), hf=rd32(intrp[i]+4); void(*fv)(CPU*)=vf?a2_find(vf):0; void(*fh)(CPU*)=hf?a2_find(hf):0; if(!fv||!fh) continue; CPU t=*c; t.r[29]=0x8000fff0u; t.r[31]=0; fv(&t); if(t.r[2]){ CPU u=*c; u.r[29]=0x801fe000; u.r[31]=0; fh(&u); } } irq_on=1; { static int pr; if(getenv("A2CHAINLOG") && pr++<12) fprintf(stderr,"chain run used %llu vtime units\n",(unsigned long long)(vtime-vt0)); } }
    istat&=~(1u<<irq);
    uint32_t h=rd32(0x8005e720+4*irq); if(!h) return;
    void(*f)(CPU*)=a2_find(h); if(!f){ logev('C',h,0,0); dump("interrupt handler not translated"); }
    CPU t=*c; t.r[29]=0x8000fff0u; t.r[31]=0; t.r[4]=irq; irq_on=0; f(&t); irq_on=1; logev('I',h,irq,0);
}
void c_break(CPU*c,uint32_t x){dump("break instruction");}
uint32_t cop0_mfc0(CPU*c,int r){return 0;} void cop0_mtc0(CPU*c,int r,uint32_t v){} void cop0_rfe(CPU*c){}
static void crash_ctx(char*b,int n){ snprintf(b,n,"frame %ld module %u overlay %d p1 %d",(long)(vtime/200000),rd32(0x80060888),a2_get_overlay(),a2_p1_record(a2_get_p1())); }
#ifdef A2_PROFILER
#include <sys/time.h>
#include <ucontext.h>
static unsigned long long prof_pc[200000]; static int prof_n;
#include <execinfo.h>
static unsigned long long prof_c[200000];
static void prof_h(int sg,siginfo_t*si,void*ctx){ (void)sg;(void)si; if(prof_n<200000){ unsigned long long pc=((ucontext_t*)ctx)->uc_mcontext.gregs[16]; unsigned long long ca=0; if(pc<0x400000||pc>0x2000000){ void*bt[8]; int n=backtrace(bt,8); for(int i=0;i<n;i++){ unsigned long long a=(unsigned long long)bt[i]; if(a>=0x400000&&a<0x2000000){ ca=a; break; } } } prof_c[prof_n]=ca; prof_pc[prof_n++]=pc; } }
static void prof_dump(void){ system("cp /proc/$PPID/maps /tmp/claude-0/maps.txt 2>/dev/null"); { FILE*m=fopen("/proc/self/maps","r"),*o=fopen("/tmp/claude-0/maps.txt","w"); char b[512]; while(m&&o&&fgets(b,512,m)) fputs(b,o); if(m) fclose(m); if(o) fclose(o); } FILE*f=fopen("/tmp/claude-0/samples.txt","w"); if(f){ for(int i=0;i<prof_n;i++) fprintf(f,"%llx %llx\n",prof_pc[i],prof_c[i]); fclose(f); } }
__attribute__((constructor)) static void prof_init(void){ if(!getenv("A2SAMPLE")) return; struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=prof_h; sa.sa_flags=SA_SIGINFO|SA_RESTART; sigaction(SIGPROF,&sa,0); struct itimerval it={{0,1000},{0,1000}}; setitimer(ITIMER_PROF,&it,0); atexit(prof_dump); }
#endif
int main(int argc,char**argv){ { extern void a2_install_crash(void), a2_set_ctx(void(*)(char*,int)); a2_install_crash(); a2_set_ctx(crash_ctx); } gpu_init(); a2_install_clock(); if(getenv("A2WINDOW")) win_on=a2_win_init(); FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); fread(ram+0x17000,1,n,f); fclose(f);
  if(getenv("A2WATCH")){ watch_lo=strtoul(getenv("A2WATCH"),0,16); const char*cm=strchr(getenv("A2WATCH"),','); watch_hi=watch_lo+(cm?strtoul(cm+1,0,16):4); }
  { const char*img=getenv("A2DISC"); if(!cd_open(img)) fprintf(stderr,"warning: no game data found (folder \"game\" or disc image)\n"); }
  mc_open(getenv("A2MEMCARD")); {
    wr32(0x150,0xA00); wr32(0x154,0xA0); for(int i=0;i<2;i++){ uint32_t ent=0xA00+i*0x50; wr32(ent,0xB00+i*8); wr32(ent+4,1); wr32(ent+0x34,0x80000c00u); } memcpy(ram+0xB00,"tty",4); memcpy(ram+0xB08,"bu",3); } signal(SIGALRM,onalarm); alarm(argc>2?atoi(argv[2]):(getenv("A2WINDOW")?0:3));
  CPU c; memset(&c,0,sizeof c); c.r[29]=0x801ffff0; c.r[31]=0; void(*fp)(CPU*)=find(0x8003cf4c); if(!fp){puts("no entry");return 1;} fp(&c); dump("entry function returned"); return 0; }

extern void sio_cmd_report(void);
static void dump_bios(void){ spu_stats(stderr); spu_close(); sio_cmd_report(); for(int i=0;i<32;i++) if(sio_r[i]||sio_w[i]) fprintf(stderr,"SIO %08x reads %ld writes %ld\n",0x1f801040+i,sio_r[i],sio_w[i]); for(int k=0;k<3;k++) for(int i=0;i<256;i++) if(bioscnt[k][i]) fprintf(stderr,"BIOS %cx/%02x called %ld times\n",k==0?'a':k==1?'b':'c',i,bioscnt[k][i]); }

#ifndef A2NOPADHLE
void f_80045a04(CPU*c){ c->r[2]=((c->r[4]&0xff)==0)?6:0; }
#endif

void f_80017ec4(CPU*c){ dump("call into a harvested non-code address 80017ec4"); }

static double vbl_clock_fn(void){ return (double)vtime/200000.0; }
void a2_install_clock(void){ a2_vbl_clock=vbl_clock_fn; }
