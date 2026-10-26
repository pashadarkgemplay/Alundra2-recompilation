/* Graphics chip: reads the game's drawing commands and draws into the 1024x512 video memory, optionally at a higher resolution. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
uint16_t vram[512*1024];
static uint32_t stat=0x14802000;
static int dx0,dy0,dx1,dy1, offx,offy, twx,twy,tww,twh, tpx,tpy,tmode,semi, dither,dtd, setmask,chkmask, dispx,dispy, dispmode;
static int dmadir; static uint32_t hrange=0xC60260, vrange=0x040010; static int dispoff;

static uint32_t cmd[16]; static int ncmd, need; static int polyline, polygour;

static int xfer_mode; static int xx,xy,xw,xh,xcx,xcy;
static uint32_t rdlatch;
static long ghist[256], gfeat[16]; void gpu_hist_print(void){ fprintf(stderr,"GPUHIST ops:"); for(int i=0;i<256;i++) if(ghist[i]) fprintf(stderr," %02x:%ld",i,ghist[i]); fprintf(stderr,"\nGPUFEAT texwin %ld setmask %ld chkmask %ld dither %ld semi0-3 %ld %ld %ld %ld tmode0-2 %ld %ld %ld smallarea %ld\n",gfeat[0],gfeat[1],gfeat[2],gfeat[3],gfeat[4],gfeat[5],gfeat[6],gfeat[7],gfeat[8],gfeat[9],gfeat[10],gfeat[12]); memset(ghist,0,sizeof ghist); memset(gfeat,0,sizeof gfeat); }
long gpu_frame; static long lg0=-1,lg1=-1; static void lg_init(void){ if(lg0!=-1) return; const char*e=getenv("A2GPULOG2"); lg0=lg1=0; if(e){ lg0=atol(e); const char*c=strchr(e,','); lg1=c?atol(c+1):lg0+5; } }
int a2_norej; static FILE*plog; static long plog_end; static int plog_n;
void gpu_plog_start(void){ if(plog) return; char nm[64]; snprintf(nm,sizeof nm,"a2pc_prims_%d.txt",++plog_n); plog=fopen(nm,"w"); if(!plog) return; plog_end=gpu_frame+3; fprintf(stderr,"[a2pc] primitive dump (3 frames) -> %s\n",nm); fprintf(plog,"# frame %ld; lines: GP0 op words... | LIST start | NODE addr words\n",gpu_frame); }
long gpu_cycles; long gpu_prims[8]; long gpu_cmds; long gpu_rejected, gpu_rej_px; static uint32_t rring[4096]; static unsigned rpos;
static inline int cl(int v,int lo,int hi){ return v<lo?lo:v>hi?hi:v; }
static inline uint16_t pack(int r,int g,int b){ return (uint16_t)((r>>3)|((g>>3)<<5)|((b>>3)<<10)); }
static void put(int x,int y,int r,int g,int b,int tr,int stp){
    x+=0; if(x<dx0||x>dx1||y<dy0||y>dy1) return; x&=1023; y&=511; uint16_t o=vram[y*1024+x]; if(chkmask&&(o&0x8000)) return;
    if(tr){ int br=(o&31)<<3,bg=((o>>5)&31)<<3,bb=((o>>10)&31)<<3;
        switch(semi){ case 0: r=(br+r)/2; g=(bg+g)/2; b=(bb+b)/2; break; case 1: r+=br; g+=bg; b+=bb; break; case 2: r=br-r; g=bg-g; b=bb-b; break; default: r=br+r/4; g=bg+g/4; b=bb+b/4; } }
    r=cl(r,0,255); g=cl(g,0,255); b=cl(b,0,255); vram[y*1024+x]=pack(r,g,b)|((stp||setmask)?0x8000:0); }
static uint16_t tex(int u,int v,int clx,int cly,int mode){
    u&=255; v&=255;
    if(tww||twh||twx||twy){ u=(u&~(tww*8))|((twx&tww)*8); v=(v&~(twh*8))|((twy&twh)*8); }
    int bx=tpx*64, by=tpy*256;
    if(mode==0){ uint16_t w=vram[((by+v)&511)*1024+((bx+u/4)&1023)]; int i=(w>>((u&3)*4))&15; return vram[(cly&511)*1024+((clx+i)&1023)]; }
    if(mode==1){ uint16_t w=vram[((by+v)&511)*1024+((bx+u/2)&1023)]; int i=(w>>((u&1)*8))&255; return vram[(cly&511)*1024+((clx+i)&1023)]; }
    return vram[((by+v)&511)*1024+((bx+u)&1023)];
}
int a2_texfilter;
static uint16_t texf(long long fu,long long fv,int clx,int cly,int mode,int umin,int umax,int vmin,int vmax){
    fu-=128; fv-=128; int u0=(int)(fu>>8), v0=(int)(fv>>8), a=(int)(fu&255), b=(int)(fv&255); int u1=u0+1, v1=v0+1;
    if(u0<umin) u0=umin; if(u0>umax) u0=umax; if(u1<umin) u1=umin; if(u1>umax) u1=umax; if(v0<vmin) v0=vmin; if(v0>vmax) v0=vmax; if(v1<vmin) v1=vmin; if(v1>vmax) v1=vmax;
    uint16_t t00=tex(u0,v0,clx,cly,mode), t10=tex(u1,v0,clx,cly,mode), t01=tex(u0,v1,clx,cly,mode), t11=tex(u1,v1,clx,cly,mode);
    if(!t00||!t10||!t01||!t11) return tex((int)((fu+128)>>8),(int)((fv+128)>>8),clx,cly,mode);
    uint16_t o=t00&0x8000;
    for(int sh=0;sh<15;sh+=5){ int c00=(t00>>sh)&31,c10=(t10>>sh)&31,c01=(t01>>sh)&31,c11=(t11>>sh)&31; int top=c00*(256-a)+c10*a, bot=c01*(256-a)+c11*a; o|=(uint16_t)((((top*(256-b)+bot*b)+32768)>>16)<<sh); }
    return o; }
typedef struct{ int x,y,r,g,b,u,v; }V;
static int edge(const V*a,const V*b,int px,int py){ return (b->x-a->x)*(py-a->y)-(b->y-a->y)*(px-a->x); }
static long hole_f=-1; static int hole_b[4]; static void hole_init(void){ if(hole_f!=-1) return; hole_f=0; const char*e=getenv("A2HOLE"); if(e) sscanf(e,"%ld,%d,%d,%d,%d",&hole_f,&hole_b[0],&hole_b[1],&hole_b[2],&hole_b[3]); }

int a2_old_spr; int a2_hi=1; static uint16_t *hv; static int replaying;
int a2_nat_interp, a2_out_k=1; static unsigned long vgen;
void gpu_set_out_scale(int k){ a2_out_k=k<1?1:k>4?4:k; }
static inline void hput(int X,int Y,int r,int g,int b,int tr,int stp){
    const int S=a2_hi; if(X<dx0*S||X>=(dx1+1)*S||Y<dy0*S||Y>=(dy1+1)*S) return;
    uint16_t*p=&hv[(size_t)Y*1024*S+X]; uint16_t o=*p; if(chkmask&&(o&0x8000)) return;
    if(tr){ int br=(o&31)<<3,bg=((o>>5)&31)<<3,bb=((o>>10)&31)<<3;
        switch(semi){ case 0: r=(br+r)/2; g=(bg+g)/2; b=(bb+b)/2; break; case 1: r+=br; g+=bg; b+=bb; break; case 2: r=br-r; g=bg-g; b=bb-b; break; default: r=br+r/4; g=bg+g/4; b=bb+b/4; } }
    r=cl(r,0,255); g=cl(g,0,255); b=cl(b,0,255); *p=pack(r,g,b)|((stp||setmask)?0x8000:0); }
static unsigned long hv_gen;
static void hi_sync(void);
static void hi_rect_sync(int x,int y,int w,int h,int haz);
static void hi_resync(int x0,int y0,int w,int h){
    const int S=a2_hi; if(!hv) return; hi_rect_sync(x0,y0,w,h,0); hv_gen++;
    if(w>1024) w=1024; if(h>512) h=512;
    for(int j=0;j<h;j++){ int y=(y0+j)&511; uint16_t*r0=&hv[((size_t)(y*S))*1024*S];
        for(int i=0;i<w;i++){ int x=(x0+i)&1023; uint16_t c=vram[y*1024+x]; uint16_t*d=r0+(size_t)x*S; for(int q=0;q<S;q++) d[q]=c; }
        for(int seg=0,i=0;i<w;seg++){ int xs=(x0+i)&1023, n=w-i; if(xs+n>1024) n=1024-xs; for(int sy=1;sy<S;sy++) memcpy(&hv[((size_t)(y*S+sy))*1024*S+(size_t)xs*S],r0+(size_t)xs*S,(size_t)n*S*2); i+=n; (void)seg; } } }
static inline void hputp(uint16_t*p,int r,int g,int b,int tr){
    uint16_t o=*p; if(chkmask&&(o&0x8000)) return;
    if(tr){ int br=(o&31)<<3,bg=((o>>5)&31)<<3,bb=((o>>10)&31)<<3;
        switch(semi){ case 0: r=(br+r)/2; g=(bg+g)/2; b=(bb+b)/2; break; case 1: r+=br; g+=bg; b+=bb; break; case 2: r=br-r; g=bg-g; b=bb-b; break; default: r=br+r/4; g=bg+g/4; b=bb+b/4; } }
    r=cl(r,0,255); g=cl(g,0,255); b=cl(b,0,255); *p=pack(r,g,b)|(setmask?0x8000:0); }
#define SPREAD(c) ((uint64_t)((c)&31)|((uint64_t)(((c)>>5)&31)<<21)|((uint64_t)(((c)>>10)&31)<<42))

extern int a2_cpus(void); extern int a2_thread_start(void(*)(int),int); extern void a2_lock(void), a2_unlock(void), a2_wait(void), a2_wake(void);
typedef struct{ int kind; V v[3]; int textured,gour,rawtex,tr,clx,cly,mode, dx0,dy0,dx1,dy1,tpx,tpy,tww,twh,twx,twy,semi,chkmask,setmask,filt,S;
    int x,y,w,h,r,g,b,u0,v0; uint16_t*fb; int sub; } HJ;
static int cur_sub; static uint16_t*cur_fb;
#define HQN 8192
static HJ *hq; static long hq_head, hq_pub, hq_done[8]; static int hK, hq_stop;
static struct{ int x0,y0,x1,y1; } hz[48]; static int nhz; static int hdb_x0,hdb_y0,hdb_x1,hdb_y1, hdb_on;
static void hj_exec(const HJ*J,int wk,int K);
static long hq_mindone(void){ long m=__atomic_load_n(&hq_done[0],__ATOMIC_ACQUIRE); for(int i=1;i<hK;i++){ long d=__atomic_load_n(&hq_done[i],__ATOMIC_ACQUIRE); if(d<m) m=d; } return m; }
#if defined(__x86_64__)||defined(__i386__)
#define CPU_RELAX() __builtin_ia32_pause()
#else
#define CPU_RELAX() ((void)0)
#endif

static int hq_sleepers, hq_waiting;
static void hq_publish(void){ if(hq_pub==hq_head) return; __atomic_store_n(&hq_pub,hq_head,__ATOMIC_SEQ_CST); if(__atomic_load_n(&hq_sleepers,__ATOMIC_SEQ_CST)>0){ a2_lock(); a2_wake(); a2_unlock(); } }
static void hq_done_wake(void){ if(__atomic_load_n(&hq_waiting,__ATOMIC_SEQ_CST)){ a2_lock(); a2_wake(); a2_unlock(); } }
static void hworker(int wk){ long mine=0; for(;;){ long pub=__atomic_load_n(&hq_pub,__ATOMIC_ACQUIRE);
        for(int sp=0;pub==mine&&sp<40000;sp++){ CPU_RELAX(); pub=__atomic_load_n(&hq_pub,__ATOMIC_ACQUIRE); }
        if(pub==mine){ a2_lock(); __atomic_add_fetch(&hq_sleepers,1,__ATOMIC_SEQ_CST); while((pub=__atomic_load_n(&hq_pub,__ATOMIC_SEQ_CST))==mine) a2_wait(); __atomic_sub_fetch(&hq_sleepers,1,__ATOMIC_SEQ_CST); a2_unlock(); }
        while(mine<pub){ hj_exec(&hq[mine%HQN],wk,hK); mine++; if((mine&63)==0){ __atomic_store_n(&hq_done[wk],mine,__ATOMIC_SEQ_CST); hq_done_wake(); } }
        __atomic_store_n(&hq_done[wk],mine,__ATOMIC_SEQ_CST); hq_done_wake(); } }
static void hq_reset_lists(void){ nhz=0; hdb_on=0; }
#include <time.h>
double hs_ns; long hs_n, hs_w;
static void hi_sync(void){ if(!hK) return; hs_n++; if(hq_pub!=hq_head) hq_publish(); if(hq_mindone()<hq_head){ struct timespec a,b; clock_gettime(CLOCK_MONOTONIC,&a); hs_w++; for(int sp=0;sp<60000&&hq_mindone()<hq_head;sp++) CPU_RELAX(); if(hq_mindone()<hq_head){ a2_lock(); __atomic_store_n(&hq_waiting,1,__ATOMIC_SEQ_CST); while(hq_mindone()<hq_head) a2_wait(); __atomic_store_n(&hq_waiting,0,__ATOMIC_SEQ_CST); a2_unlock(); } clock_gettime(CLOCK_MONOTONIC,&b); hs_ns+=(b.tv_sec-a.tv_sec)*1e9+(b.tv_nsec-a.tv_nsec); } hq_reset_lists(); }
static void hi_hazard(int x0,int y0,int x1,int y1){ if(!hK||!nhz) return; if(hq_mindone()>=hq_head){ hq_reset_lists(); return; }
    for(int i=0;i<nhz;i++) if(x0<=hz[i].x1&&x1>=hz[i].x0&&y0<=hz[i].y1&&y1>=hz[i].y0){ hi_sync(); return; } }
int gpu_perf_info(int*S,int*K,int*F){ *S=a2_hi; *K=hK; *F=a2_texfilter; return 0; }
extern int cv_drop_pending(void);
static void hi_sync_region(int x0,int y0,int x1,int y1){ if(!hK||!hdb_on) return; if(hq_mindone()>=hq_head){ hq_reset_lists(); return; }
    if(x0<=hdb_x1&&x1>=hdb_x0&&y0<=hdb_y1&&y1>=hdb_y0) hi_sync(); }

static void hi_rect_sync(int x,int y,int w,int h,int haz){ if(!hK) return; if(w<=0||h<=0) return; if(w>1024) w=1024; if(h>512) h=512; x&=1023; y&=511;
    if(x+w>1024||y+h>512){ hi_sync(); return; } if(haz) hi_hazard(x,y,x+w-1,y+h-1); hi_sync_region(x,y,x+w-1,y+h-1); }
static void hs_report(void){ extern double hs_ns; extern long hs_n,hs_w; fprintf(stderr,"[a2pc] hi_sync calls %ld, waited %ld, wait %.0f ms\n",hs_n,hs_w,hs_ns/1e6); { extern double bm_ns; extern long bm_n; if(bm_n) fprintf(stderr,"[a2pc] polygon matching: %ld runs, %.2f ms each\n",bm_n,bm_ns/1e6/bm_n); } }
static void hi_init_threads(void){ if(getenv("A2HSREP")) atexit(hs_report); int cpus=a2_cpus(), k=cpus-1; const char*e=getenv("A2THREADS"); if(e) k=atoi(e); if(k>4) k=4; if(k<0) k=0; if(!e&&cpus<2) k=0;
    if(k>0){ hq=(HJ*)malloc(sizeof(HJ)*HQN); if(!hq) k=0; } hK=k; for(int i=0;i<k;i++) if(!a2_thread_start(hworker,i)){ hK=0; k=0; break; } if(getenv("A2FPS")||getenv("A2THREADS")) fprintf(stderr,"[a2pc] hi-res rasteriser: %d worker thread(s) (%d cpus)\n",k,cpus); }
static HJ* hq_slot(HJ*loc){ if(!hK) return loc; while(hq_head-hq_mindone()>=HQN){ hq_publish(); a2_lock(); __atomic_store_n(&hq_waiting,1,__ATOMIC_SEQ_CST); while(hq_head-hq_mindone()>=HQN) a2_wait(); __atomic_store_n(&hq_waiting,0,__ATOMIC_SEQ_CST); a2_unlock(); } return &hq[hq_head%HQN]; }
static void hq_note(const HJ*J,int bx0,int by0,int bx1,int by1,int usestex){
    if(!hdb_on){ hdb_on=1; hdb_x0=bx0; hdb_y0=by0; hdb_x1=bx1; hdb_y1=by1; } else { if(bx0<hdb_x0) hdb_x0=bx0; if(by0<hdb_y0) hdb_y0=by0; if(bx1>hdb_x1) hdb_x1=bx1; if(by1>hdb_y1) hdb_y1=by1; }
    if(usestex){ int px=J->tpx*64, py=J->tpy*256, pw=J->mode==0?64:J->mode==1?128:256; if(J->kind==1) pw=J->mode==0?64:J->mode==1?128:256;
        int f=0; for(int i=0;i<nhz;i++) if(hz[i].x0==px&&hz[i].y0==py&&hz[i].x1>=px+pw-1){ f=1; break; }
        if(!f){ if(nhz<48){ hz[nhz].x0=px; hz[nhz].y0=py; hz[nhz].x1=px+pw-1; hz[nhz].y1=py+255; nhz++; } } } }

static void hv_touch(int bx0,int by0,int bx1,int by1){ extern int gpu_disp_x(void), gpu_disp_y(void); int ddx=gpu_disp_x(), ddy=gpu_disp_y(); if(bx0<=ddx+639&&bx1>=ddx&&by0<=ddy+479&&by1>=ddy) hv_gen++; }
static void hq_commit(HJ*slot,const HJ*loc,int bx0,int by0,int bx1,int by1,int usestex){
    hv_touch(bx0,by0,bx1,by1);
    if(!hK){ hj_exec(loc,0,1); return; }
    hq_note(slot,bx0,by0,bx1,by1,usestex); hq_head++; if(hq_head-hq_pub>=24) hq_publish(); if(nhz>=48) hi_sync(); }
static void hj_state(HJ*J){ J->dx0=dx0; J->dy0=dy0; J->dx1=dx1; J->dy1=dy1; J->tpx=tpx; J->tpy=tpy; J->tww=tww; J->twh=twh; J->twx=twx; J->twy=twy; J->semi=semi; J->chkmask=chkmask; J->setmask=setmask; J->filt=a2_texfilter; J->S=a2_hi; J->fb=cur_fb?cur_fb:hv; J->sub=cur_sub; }
static void hq_tri(const V*v0,const V*v1,const V*v2,int textured,int gour,int rawtex,int tr,int clx,int cly,int mode,int bx0,int by0,int bx1,int by1){
    HJ loc; HJ*J=hq_slot(&loc); J->kind=0; J->v[0]=*v0; J->v[1]=*v1; J->v[2]=*v2; J->textured=textured; J->gour=gour; J->rawtex=rawtex; J->tr=tr; J->clx=clx; J->cly=cly; J->mode=mode; hj_state(J);
    hq_commit(J,J,bx0,by0,bx1,by1,textured); }
static void hq_rect(int x,int y,int w,int h,int r,int g,int b,int textured,int rawtex,int tr,int u0,int v0,int clx,int cly){
    HJ loc; HJ*J=hq_slot(&loc); J->kind=1; J->x=x; J->y=y; J->w=w; J->h=h; J->r=r; J->g=g; J->b=b; J->textured=textured; J->rawtex=rawtex; J->tr=tr; J->u0=u0; J->v0=v0; J->clx=clx; J->cly=cly; J->mode=tmode; hj_state(J);
    hq_commit(J,J,x,y,x+w-1,y+h-1,textured); }
static inline uint16_t tex_j(const HJ*J,int u,int v,int clx,int cly,int mode){
    u&=255; v&=255; if(J->tww||J->twh||J->twx||J->twy){ u=(u&~(J->tww*8))|((J->twx&J->tww)*8); v=(v&~(J->twh*8))|((J->twy&J->twh)*8); }
    int bx=J->tpx*64, by=J->tpy*256;
    if(mode==0){ uint16_t w=vram[((by+v)&511)*1024+((bx+u/4)&1023)]; int i=(w>>((u&3)*4))&15; return vram[(cly&511)*1024+((clx+i)&1023)]; }
    if(mode==1){ uint16_t w=vram[((by+v)&511)*1024+((bx+u/2)&1023)]; int i=(w>>((u&1)*8))&255; return vram[(cly&511)*1024+((clx+i)&1023)]; }
    return vram[((by+v)&511)*1024+((bx+u)&1023)]; }
static inline uint16_t semi_px(uint16_t o,int r,int g,int b,int semi,int setm){
    int br=(o&31)<<3,bg=((o>>5)&31)<<3,bb=((o>>10)&31)<<3;
    switch(semi){ case 0: r=(br+r)/2; g=(bg+g)/2; b=(bb+b)/2; break; case 1: r+=br; g+=bg; b+=bb; break; case 2: r=br-r; g=bg-g; b=bb-b; break; default: r=br+r/4; g=bg+g/4; b=bb+b/4; }
    r=cl(r,0,255); g=cl(g,0,255); b=cl(b,0,255); return (uint16_t)(pack(r,g,b)|setm); }

static inline __attribute__((always_inline)) void hj_rect_t(const HJ*J,int wk,int K,const int S){
    const int setm=J->setmask?0x8000:0, chk=J->chkmask, semi=J->semi, textured=J->textured, neutral=(J->r==128&&J->g==128&&J->b==128);
    int ix0=J->dx0-J->x, ix1=J->dx1-J->x, jy0=J->dy0-J->y, jy1=J->dy1-J->y; if(ix0<0) ix0=0; if(jy0<0) jy0=0; if(ix1>J->w-1) ix1=J->w-1; if(jy1>J->h-1) jy1=J->h-1; if(ix0>ix1||jy0>jy1) return;
    uint16_t pix[1024]; uint8_t kind[1024];   uint8_t cr[1024],cg[1024],cb[1024];
    uint16_t *tbl=0; static __thread uint16_t tblbuf[32768]; static __thread long tblkey=-1;
    long share=(long)(ix1-ix0+1)*(jy1-jy0+1)*S*S/(K>1?K:1);
    const int flatsemi=!textured&&J->tr;
    if(flatsemi){ long key=((long)J->r<<24)|((long)J->g<<16)|((long)J->b<<8)|((long)semi<<1)|(setm?1:0);
        if(key==tblkey) tbl=tblbuf; else if(share>12000){ tbl=tblbuf; tblkey=key; for(int o=0;o<32768;o++) tbl[o]=semi_px((uint16_t)o,J->r,J->g,J->b,semi,setm); } }
    const uint16_t flatpix=(uint16_t)(pack(J->r,J->g,J->b)|setm);
    const int usemask=(J->tww||J->twh||J->twx||J->twy), mku=~(J->tww*8), oru=(J->twx&J->tww)*8, mkv=~(J->twh*8), orv=(J->twy&J->twh)*8, tbx=J->tpx*64, tby=J->tpy*256, clx_=J->clx;
    const uint16_t*clutr=&vram[(J->cly&511)*1024]; uint16_t LRt[32],LGt[32],LBt[32];
    if(textured&&!J->tr&&!J->rawtex&&!neutral) for(int i=0;i<32;i++){ LRt[i]=(uint16_t)(cl(((i<<3)*J->r)/128,0,255)>>3); LGt[i]=(uint16_t)((cl(((i<<3)*J->g)/128,0,255)>>3)<<5); LBt[i]=(uint16_t)((cl(((i<<3)*J->b)/128,0,255)>>3)<<10); }
    for(int j=jy0;j<=jy1;j++){
        int rowsmine=0; for(int sy=0;sy<S;sy++){ int Y=(J->y+j)*S+sy; if(!(K>1&&(((Y>>4)%K)!=wk))) rowsmine=1; } if(!rowsmine) continue;
        if(textured&&!J->tr&&!a2_old_spr){
            int v_=(J->v0+j)&255; if(usemask){ v_=(v_&mkv)|orv; } const uint16_t*r_=&vram[((tby+v_)&511)*1024]; const int uu0=J->u0+ix0, n=ix1-ix0+1;
            if(S==1&&!chk){
                const int Y_=J->y+j; if(K>1&&(((Y_>>4)%K)!=wk)) continue; uint16_t*dst=&J->fb[(size_t)Y_*1024+J->x+ix0]; const int plain=(J->rawtex||neutral);
                #define SPRDIRECT(FETCH) if(plain){ for(int i=0;i<n;i++){ int u_=(uu0+i)&255; if(usemask) u_=(u_&mku)|oru; uint16_t t; FETCH; if(t) dst[i]=(uint16_t)((t&0x7fff)|setm); } } \
                    else { for(int i=0;i<n;i++){ int u_=(uu0+i)&255; if(usemask) u_=(u_&mku)|oru; uint16_t t; FETCH; if(t) dst[i]=(uint16_t)(LRt[t&31]|LGt[(t>>5)&31]|LBt[(t>>10)&31]|setm); } }
                if(J->mode==0){ SPRDIRECT(uint16_t w_=r_[(tbx+(u_>>2))&1023]; t=clutr[(clx_+((w_>>((u_&3)*4))&15))&1023]) }
                else if(J->mode==1){ SPRDIRECT(uint16_t w_=r_[(tbx+(u_>>1))&1023]; t=clutr[(clx_+((w_>>((u_&1)*8))&255))&1023]) }
                else { SPRDIRECT(t=r_[(tbx+u_)&1023]) }
                #undef SPRDIRECT
                continue; }
            #define SPRFETCH(EXPR) for(int i=0;i<n;i++){ int u_=(uu0+i)&255; if(usemask) u_=(u_&mku)|oru; uint16_t t; EXPR; if(!t){ kind[ix0+i]=0; continue; } kind[ix0+i]=1; pix[ix0+i]=(uint16_t)(((J->rawtex||neutral)?(t&0x7fff):(LRt[t&31]|LGt[(t>>5)&31]|LBt[(t>>10)&31]))|setm); }
            if(J->mode==0){ SPRFETCH(uint16_t w_=r_[(tbx+(u_>>2))&1023]; t=clutr[(clx_+((w_>>((u_&3)*4))&15))&1023]) }
            else if(J->mode==1){ SPRFETCH(uint16_t w_=r_[(tbx+(u_>>1))&1023]; t=clutr[(clx_+((w_>>((u_&1)*8))&255))&1023]) }
            else { SPRFETCH(t=r_[(tbx+u_)&1023]) }
            #undef SPRFETCH
        }
        else if(textured){ for(int i=ix0;i<=ix1;i++){ uint16_t t=tex_j(J,J->u0+i,J->v0+j,J->clx,J->cly,J->mode); if(!t){ kind[i]=0; continue; }
                int sr=(t&31)<<3, sg=((t>>5)&31)<<3, sb=((t>>10)&31)<<3; if(!J->rawtex&&!neutral){ sr=cl(sr*J->r/128,0,255); sg=cl(sg*J->g/128,0,255); sb=cl(sb*J->b/128,0,255); }
                if(J->tr&&(t&0x8000)){ kind[i]=2; cr[i]=sr; cg[i]=sg; cb[i]=sb; } else { kind[i]=1; pix[i]=(uint16_t)(pack(sr,sg,sb)|setm); } } }
        for(int sy=0;sy<S;sy++){ int Y=(J->y+j)*S+sy; if(K>1&&(((Y>>4)%K)!=wk)) continue;
            uint16_t*row=&J->fb[(size_t)Y*1024*S+(size_t)(J->x)*S];
            if(!textured){ uint16_t*p=row+(size_t)ix0*S; long n=(long)(ix1-ix0+1)*S;
                if(!J->tr){ if(!chk){ for(long q=0;q<n;q++) p[q]=flatpix; } else { for(long q=0;q<n;q++) if(!(p[q]&0x8000)) p[q]=flatpix; } }
                else if(tbl){ if(!chk){ for(long q=0;q<n;q++) p[q]=tbl[p[q]&0x7fff]; } else { for(long q=0;q<n;q++){ uint16_t o=p[q]; if(!(o&0x8000)) p[q]=tbl[o]; } } }
                else { for(long q=0;q<n;q++){ uint16_t o=p[q]; if(chk&&(o&0x8000)) continue; p[q]=semi_px(o,J->r,J->g,J->b,semi,setm); } }
                continue; }
            for(int i=ix0;i<=ix1;i++){ int kd=kind[i]; if(!kd) continue; uint16_t*p=row+(size_t)i*S;
                if(kd==1){ if(!chk){ for(int q=0;q<S;q++) p[q]=pix[i]; } else { for(int q=0;q<S;q++) if(!(p[q]&0x8000)) p[q]=pix[i]; } }
                else { for(int q=0;q<S;q++){ uint16_t o=p[q]; if(chk&&(o&0x8000)) continue; p[q]=semi_px(o,cr[i],cg[i],cb[i],semi,setm); } } } } } }
static double rs_px[8]; static long rs_n[8]; static void rs_dump(void){ for(int i=0;i<8;i++) if(rs_n[i]) fprintf(stderr,"RECTSTAT tex%d tr%d semi: %ld rects, %.0f px (avg %.0f)\n",(i>>1)&1,i&1,rs_n[i],rs_px[i],rs_px[i]/rs_n[i]); }
static void hj_rect(const HJ*J,int wk,int K){ { static int on=-1; if(on<0){ on=getenv("A2RECTSTAT")!=0; if(on) atexit(rs_dump); } if(on){ int k=(J->textured?2:0)|(J->tr?1:0); rs_n[k]++; rs_px[k]+=(double)J->w*J->h; } } switch(J->S){ case 2: hj_rect_t(J,wk,K,2); break; case 3: hj_rect_t(J,wk,K,3); break; case 4: hj_rect_t(J,wk,K,4); break; default: hj_rect_t(J,wk,K,J->S); } }
static void hconv(int idx,int wk,int K);
static void hj_tri(const HJ*J,int wk,int K);
static void hj_exec(const HJ*J,int wk,int K){ if(J->kind==0) hj_tri(J,wk,K); else if(J->kind==1) hj_rect(J,wk,K); else hconv(J->mode,wk,K); }
static inline __attribute__((always_inline)) void hj_tri_t(const HJ*J,int wk,int K,const int textured,const int gour,const int rawtex,const int tr,const int a2_texfilter){
    const V*v0=&J->v[0],*v1=&J->v[1],*v2=&J->v[2]; const int clx=J->clx, cly=J->cly, mode=J->mode;
    const int dx0=J->dx0, dy0=J->dy0, dx1=J->dx1, dy1=J->dy1, tpx=J->tpx, tpy=J->tpy, tww=J->tww, twh=J->twh, twx=J->twx, twy=J->twy, semi=J->semi, chkmask=J->chkmask, setmask=J->setmask;
    (void)dy0;
    const int S=J->S, M=2*S, SB=J->sub; V a=*v0,b=*v1,c=*v2; a.x=(a.x*M)>>SB; a.y=(a.y*M)>>SB; b.x=(b.x*M)>>SB; b.y=(b.y*M)>>SB; c.x=(c.x*M)>>SB; c.y=(c.y*M)>>SB;
    long long area=(long long)(b.x-a.x)*(c.y-a.y)-(long long)(b.y-a.y)*(c.x-a.x); if(!area) return;
    const V*p0=&a,*p1=&b,*p2=&c; V tmp; (void)tmp; if(area<0){ p1=&c; p2=&b; area=-area; }
    int b0=(p2->y>p1->y||(p2->y==p1->y&&p2->x<p1->x))?0:1, b1=(p0->y>p2->y||(p0->y==p2->y&&p0->x<p2->x))?0:1, b2=(p1->y>p0->y||(p1->y==p0->y&&p1->x<p0->x))?0:1;
    int mnx=p0->x<p1->x?(p0->x<p2->x?p0->x:p2->x):(p1->x<p2->x?p1->x:p2->x), mxx=p0->x>p1->x?(p0->x>p2->x?p0->x:p2->x):(p1->x>p2->x?p1->x:p2->x);
    int mny=p0->y<p1->y?(p0->y<p2->y?p0->y:p2->y):(p1->y<p2->y?p1->y:p2->y), mxy=p0->y>p1->y?(p0->y>p2->y?p0->y:p2->y):(p1->y>p2->y?p1->y:p2->y);
    int X0=(mnx-1+S)/2-1, X1=(mxx-1+S)/2+1, Y0=(mny-1+S)/2-1, Y1=(mxy-1+S)/2+1;
    if(X0<dx0*S) X0=dx0*S; if(X1>(dx1+1)*S-1) X1=(dx1+1)*S-1; if(Y0<dy0*S) Y0=dy0*S; if(Y1>(dy1+1)*S-1) Y1=(dy1+1)*S-1; if(X0>X1||Y0>Y1) return;
    int uminp=v0->u,umaxp=v0->u,vminp=v0->v,vmaxp=v0->v; if(textured){ const V*qq[2]={v1,v2}; for(int i=0;i<2;i++){ if(qq[i]->u<uminp) uminp=qq[i]->u; if(qq[i]->u>umaxp) umaxp=qq[i]->u; if(qq[i]->v<vminp) vminp=qq[i]->v; if(qq[i]->v>vmaxp) vmaxp=qq[i]->v; } }

    const int d0=-(p2->y-p1->y)*2, d1=-(p0->y-p2->y)*2, d2=-(p1->y-p0->y)*2;
    const V*src0=p0,*src1=p1,*src2=p2; const V*orig[3]={v0,v1,v2}; (void)orig;

    const V*o0=v0,*o1=(p1==&b)?v1:v2,*o2=(p2==&c)?v2:v1; (void)src0;(void)src1;(void)src2;

    const double inv=1.0/(double)area;
    const long long dR=d0*(long long)o0->r+d1*(long long)o1->r+d2*(long long)o2->r, dG=d0*(long long)o0->g+d1*(long long)o1->g+d2*(long long)o2->g, dB=d0*(long long)o0->b+d1*(long long)o1->b+d2*(long long)o2->b;
    const long long dU=d0*(long long)o0->u+d1*(long long)o1->u+d2*(long long)o2->u, dV=d0*(long long)o0->v+d1*(long long)o1->v+d2*(long long)o2->v;
    const double sR=dR*inv, sG=dG*inv, sB=dB*inv, sU=dU*256.0*inv, sV=dV*256.0*inv;
    const int filt=a2_texfilter, usemask=(tww||twh||twx||twy), mku=~(tww*8), oru=(twx&tww)*8, mkv=~(twh*8), orv=(twy&twh)*8, tbx=tpx*64, tby=tpy*256;
    const int lsemi=semi, lchk=chkmask, lset=setmask?0x8000:0; const uint16_t*const vr=vram; const uint16_t*const clut=&vram[(cly&511)*1024]; const int lclx=clx;
#define TEXFETCH(U,V,OUT) { int u_=(U)&255, v_=(V)&255; if(usemask){ u_=(u_&mku)|oru; v_=(v_&mkv)|orv; } const uint16_t*r_=&vr[((tby+v_)&511)*1024]; \
        if(mode==0){ uint16_t w_=r_[(tbx+(u_>>2))&1023]; OUT=clut[(lclx+((w_>>((u_&3)*4))&15))&1023]; } else if(mode==1){ uint16_t w_=r_[(tbx+(u_>>1))&1023]; OUT=clut[(lclx+((w_>>((u_&1)*8))&255))&1023]; } else OUT=r_[(tbx+u_)&1023]; }

    uint16_t LR[32],LG[32],LB[32]; int neutral=0;
    if(textured&&!gour&&!rawtex){ const int fr=o0->r, fg=o0->g, fb=o0->b; neutral=(fr==128&&fg==128&&fb==128);
        for(int i=0;i<32;i++){ LR[i]=(uint16_t)(cl(((i<<3)*fr)>>7,0,255)>>3); LG[i]=(uint16_t)((cl(((i<<3)*fg)>>7,0,255)>>3)<<5); LB[i]=(uint16_t)((cl(((i<<3)*fb)>>7,0,255)>>3)<<10); } }
    for(int Y=Y0;Y<=Y1;Y++){ if(K>1&&(((Y>>4)%K)!=wk)) continue; int Q=2*Y+1-S; int P=2*X0+1-S;
        int w0=(p2->x-p1->x)*(Q-p1->y)-(p2->y-p1->y)*(P-p1->x), w1=(p0->x-p2->x)*(Q-p2->y)-(p0->y-p2->y)*(P-p2->x), w2=(p1->x-p0->x)*(Q-p0->y)-(p1->y-p0->y)*(P-p0->x);
        uint16_t*hrow=&J->fb[(size_t)Y*1024*S];

        int ka=0, kb=X1-X0; { const int wv[3]={w0,w1,w2}, dv[3]={d0,d1,d2}, bv[3]={b0,b1,b2};
            for(int e=0;e<3&&ka<=kb;e++){ int w=wv[e], d=dv[e], bb=bv[e];
                if(d==0){ if(w<bb) ka=kb+1; }
                else if(d>0){ if(w<bb){ int need=(bb-w+d-1)/d; if(need>ka) ka=need; } }
                else { int dd=-d; if(w<bb){ ka=kb+1; } else { int mx=(w-bb)/dd; if(mx<kb) kb=mx; } } } }
        if(ka>kb) continue;
        w0+=d0*ka; w1+=d1*ka; w2+=d2*ka;
        long long nR=(long long)w0*o0->r+(long long)w1*o1->r+(long long)w2*o2->r, nG=(long long)w0*o0->g+(long long)w1*o1->g+(long long)w2*o2->g, nB=(long long)w0*o0->b+(long long)w1*o1->b+(long long)w2*o2->b;
        long long nU=(long long)w0*o0->u+(long long)w1*o1->u+(long long)w2*o2->u, nV=(long long)w0*o0->v+(long long)w1*o1->v+(long long)w2*o2->v;
        double fR=nR*inv+1e-7, fG=nG*inv+1e-7, fB=nB*inv+1e-7, fU=nU*256.0*inv+1e-6, fV=nV*256.0*inv+1e-6;
        int cu=-100000, cv=-100000, tok=0; uint16_t T00=0,T10=0,T01=0,T11=0; uint64_t X00=0,X10=0,X01=0,X11=0;
        uint16_t*px=hrow+X0+ka, *pend=hrow+X0+kb+1;
        const int fr0=gour?0:o0->r, fg0=gour?0:o0->g, fb0=gour?0:o0->b;
        for(;px<pend;px++,fR+=sR,fG+=sG,fB+=sB,fU+=sU,fV+=sV){
            int r,g,bl; if(gour){ r=(int)fR; g=(int)fG; bl=(int)fB; } else { r=fr0; g=fg0; bl=fb0; }
            int sr,sg,sb, semit=tr;
            if(textured){ uint16_t t;
                if(filt){ int fu=(int)fU-128, fv=(int)fV-128; int u0=fu>>8, v0=fv>>8, fa=fu&255, fb=fv&255;
                    if(u0!=cu||v0!=cv){ int u1=u0+1, v1=v0+1, uu0=u0, vv0=v0;
                        if(uu0<uminp) uu0=uminp; if(uu0>umaxp) uu0=umaxp; if(u1<uminp) u1=uminp; if(u1>umaxp) u1=umaxp; if(vv0<vminp) vv0=vminp; if(vv0>vmaxp) vv0=vmaxp; if(v1<vminp) v1=vminp; if(v1>vmaxp) v1=vmaxp;
                        if(v0==cv&&u0==cu+1&&u0>uminp&&u0<=umaxp&&u1<=umaxp){ T00=T10; T01=T11; TEXFETCH(u1,vv0,T10); TEXFETCH(u1,v1,T11); }
                        else { TEXFETCH(uu0,vv0,T00); TEXFETCH(u1,vv0,T10); TEXFETCH(uu0,v1,T01); TEXFETCH(u1,v1,T11); }
                        cu=u0; cv=v0; tok=T00&&T10&&T01&&T11;
                        if(tok){ X00=SPREAD(T00); X10=SPREAD(T10); X01=SPREAD(T01); X11=SPREAD(T11); } }
                    if(!tok){ TEXFETCH((fu+128)>>8,(fv+128)>>8,t); }
                    else { uint64_t top=X00*(uint64_t)(256-fa)+X10*(uint64_t)fa, bot=X01*(uint64_t)(256-fa)+X11*(uint64_t)fa, res=top*(uint64_t)(256-fb)+bot*(uint64_t)fb+((1ULL<<15)|(1ULL<<36)|(1ULL<<57));
                        t=(uint16_t)((T00&0x8000)|((res>>16)&31)|(((res>>37)&31)<<5)|(((res>>58)&31)<<10)); } }
                else { TEXFETCH((int)(fU*(1.0/256.0)),(int)(fV*(1.0/256.0)),t); }
                if(!t) continue;
                if(!tr&&!gour){ uint16_t pv=(rawtex||neutral)?(uint16_t)(t&0x7fff):(uint16_t)(LR[t&31]|LG[(t>>5)&31]|LB[(t>>10)&31]); if(lchk&&(*px&0x8000)) continue; *px=pv|lset; continue; }
                sr=(t&31)<<3; sg=((t>>5)&31)<<3; sb=((t>>10)&31)<<3;
                if(!rawtex){ sr=cl((sr*r)>>7,0,255); sg=cl((sg*g)>>7,0,255); sb=cl((sb*bl)>>7,0,255); }
                semit=tr&&(t&0x8000); }
            else { sr=r; sg=g; sb=bl; }
            { uint16_t o=*px; if(lchk&&(o&0x8000)) continue;
              if(semit){ int br=(o&31)<<3,bg=((o>>5)&31)<<3,bb=((o>>10)&31)<<3;
                switch(lsemi){ case 0: sr=(br+sr)/2; sg=(bg+sg)/2; sb=(bb+sb)/2; break; case 1: sr+=br; sg+=bg; sb+=bb; break; case 2: sr=br-sr; sg=bg-sg; sb=bb-sb; break; default: sr=br+sr/4; sg=bg+sg/4; sb=bb+sb/4; } }
              sr=cl(sr,0,255); sg=cl(sg,0,255); sb=cl(sb,0,255); *px=pack(sr,sg,sb)|lset; } } } }
#define HV1(t,g,r,tr,f) case ((t)|((g)<<1)|((r)<<2)|((tr)<<3)|((f)<<4)): hj_tri_t(J,wk,K,t,g,r,tr,f); break;
#define HVF(t,g,r,tr) HV1(t,g,r,tr,0) HV1(t,g,r,tr,1)
#define HVT(t,g,r) HVF(t,g,r,0) HVF(t,g,r,1)
#define HVR(t,g) HVT(t,g,0) HVT(t,g,1)
#define HVG(t) HVR(t,0) HVR(t,1)

static void hj_tri(const HJ*J,int wk,int K){ int t=J->textured?1:0, g=J->gour?1:0, r=J->rawtex?1:0, tr=J->tr?1:0, f=(J->filt&&J->textured)?1:0;
    switch(t|(g<<1)|(r<<2)|(tr<<3)|(f<<4)){ HVG(0) HVG(1) } }
static int old_raster=-1;
static void tri(V*v0,V*v1,V*v2,int textured,int gour,int rawtex,int tr,int clx,int cly,int mode){
    long hc=0,ht=0,hcm=0; hole_init();
    { int mnx=v0->x<v1->x?v0->x:v1->x, mxx=v0->x>v1->x?v0->x:v1->x, mny=v0->y<v1->y?v0->y:v1->y, mxy=v0->y>v1->y?v0->y:v1->y;
      if(v2->x<mnx) mnx=v2->x; if(v2->x>mxx) mxx=v2->x; if(v2->y<mny) mny=v2->y; if(v2->y>mxy) mxy=v2->y;
      if((mxx-mnx>=1024||mxy-mny>=512)&&!a2_norej&&!getenv("A2NOREJECT")){ gpu_rejected++; if(getenv("A2REJLOG")&&gpu_frame>=atol(getenv("A2REJLOG"))){ static int nr; if(nr++<40) fprintf(stderr,"REJ F%ld v0 %d,%d v1 %d,%d v2 %d,%d tex %d gour %d offs %d,%d area %d,%d-%d,%d\n",gpu_frame,v0->x,v0->y,v1->x,v1->y,v2->x,v2->y,textured,gour,offx,offy,dx0,dy0,dx1,dy1); } if(getenv("A2REJSTAT")){ for(int y=(mny>dy0?mny:dy0);y<=(mxy<dy1?mxy:dy1);y++) for(int x=(mnx>dx0?mnx:dx0);x<=(mxx<dx1?mxx:dx1);x++){ int a_=edge(v0,v1,x,y),b_=edge(v1,v2,x,y),c_=edge(v2,v0,x,y); if((a_>=0&&b_>=0&&c_>=0)||(a_<=0&&b_<=0&&c_<=0)) gpu_rej_px++; } } return; } }
    if(textured){ static int on=-1; static long cnt_t,cnt_e,lastf=-1; if(on<0) on=getenv("A2EMPTY")!=0; if(on){ int z=1; const V*q[3]={v0,v1,v2}; for(int i=0;i<3;i++) if(tex(q[i]->u,q[i]->v,clx,cly,mode)) z=0; if(z&&tex((v0->u+v1->u+v2->u)/3,(v0->v+v1->v+v2->v)/3,clx,cly,mode)) z=0; cnt_t++; if(z){ cnt_e++; if(cnt_e<=6) fprintf(stderr,"EMPTYTEX F%ld tp %d,%d mode %d clut %d,%d uv %d,%d %d,%d %d,%d xy %d,%d %d,%d %d,%d\n",gpu_frame,tpx,tpy,mode,clx,cly,v0->u,v0->v,v1->u,v1->v,v2->u,v2->v,v0->x,v0->y,v1->x,v1->y,v2->x,v2->y); } if(gpu_frame!=lastf){ if(lastf>=0&&lastf%100==0) fprintf(stderr,"EMPTYSTAT F%ld textured tris %ld empty %ld\n",lastf,cnt_t,cnt_e); lastf=gpu_frame; cnt_t=cnt_e=0; } } }
    int area=edge(v0,v1,v2->x,v2->y); if(!area) return;
    if(area<0){ V*t=v1; v1=v2; v2=t; area=-area; }
    gpu_cycles+=40+(long)(area/2)*(textured?65:50)/100;
    int minx=cl(v0->x<v1->x?(v0->x<v2->x?v0->x:v2->x):(v1->x<v2->x?v1->x:v2->x),dx0,dx1), maxx=cl(v0->x>v1->x?(v0->x>v2->x?v0->x:v2->x):(v1->x>v2->x?v1->x:v2->x),dx0,dx1);
    int miny=cl(v0->y<v1->y?(v0->y<v2->y?v0->y:v2->y):(v1->y<v2->y?v1->y:v2->y),dy0,dy1), maxy=cl(v0->y>v1->y?(v0->y>v2->y?v0->y:v2->y):(v1->y>v2->y?v1->y:v2->y),dy0,dy1);
    if(maxx-minx>1023||maxy-miny>511) return;
    if(a2_hi>1&&hv&&!replaying){ hi_hazard(minx,miny,maxx,maxy); hq_tri(v0,v1,v2,textured,gour,rawtex,tr,clx,cly,mode,minx,miny,maxx,maxy); }
    int uminp=v0->u,umaxp=v0->u,vminp=v0->v,vmaxp=v0->v; if(textured){ const V*qq[2]={v1,v2}; for(int i=0;i<2;i++){ if(qq[i]->u<uminp) uminp=qq[i]->u; if(qq[i]->u>umaxp) umaxp=qq[i]->u; if(qq[i]->v<vminp) vminp=qq[i]->v; if(qq[i]->v>vmaxp) vmaxp=qq[i]->v; } }
    if(old_raster<0) old_raster=getenv("A2OLDRASTER")!=0;
    if(!old_raster){ HJ J; J.kind=0; J.v[0]=*v0; J.v[1]=*v1; J.v[2]=*v2; J.textured=textured; J.gour=gour; J.rawtex=rawtex; J.tr=tr; J.clx=clx; J.cly=cly; J.mode=mode; hj_state(&J); J.S=1; J.fb=vram; J.sub=0; hj_tri(&J,0,1); return; }
    for(int y=miny;y<=maxy;y++) for(int x=minx;x<=maxx;x++){
        int w0=edge(v1,v2,x,y), w1=edge(v2,v0,x,y), w2=edge(v0,v1,x,y);

        int b0=(v2->y>v1->y||(v2->y==v1->y&&v2->x<v1->x))?0:1, b1=(v0->y>v2->y||(v0->y==v2->y&&v0->x<v2->x))?0:1, b2=(v1->y>v0->y||(v1->y==v0->y&&v1->x<v0->x))?0:1;
        if(w0<b0||w1<b1||w2<b2) continue;
        int r,g,b;
        if(gour){ r=(w0*v0->r+w1*v1->r+w2*v2->r)/area; g=(w0*v0->g+w1*v1->g+w2*v2->g)/area; b=(w0*v0->b+w1*v1->b+w2*v2->b)/area; } else { r=v0->r; g=v0->g; b=v0->b; }
        if(textured){ int u=(w0*v0->u+w1*v1->u+w2*v2->u)/area, vv=(w0*v0->v+w1*v1->v+w2*v2->v)/area; uint16_t t; if(a2_texfilter){ long long su=(long long)w0*v0->u+(long long)w1*v1->u+(long long)w2*v2->u, sv=(long long)w0*v0->v+(long long)w1*v1->v+(long long)w2*v2->v; t=texf(su*256/area,sv*256/area,clx,cly,mode,uminp,umaxp,vminp,vmaxp); } else t=tex(u,vv,clx,cly,mode); if(hole_f&&(gpu_frame>=hole_f&&gpu_frame<=hole_f+1)&&x>=hole_b[0]&&x<=hole_b[2]&&y>=hole_b[1]&&y<=hole_b[3]){ hc++; if(!t) ht++; if(t&&((t&31)+((t>>5)&31)+((t>>10)&31))<3) hcm++; } if(!t) continue;
            int sr=(t&31)<<3, sg=((t>>5)&31)<<3, sb=((t>>10)&31)<<3;
            if(!rawtex){ sr=cl(sr*r/128,0,255); sg=cl(sg*g/128,0,255); sb=cl(sb*b/128,0,255); }
            put(x,y,sr,sg,sb,tr&&(t&0x8000),t&0x8000&&0); }
        else { if(hole_f&&(gpu_frame>=hole_f&&gpu_frame<=hole_f+1)&&x>=hole_b[0]&&x<=hole_b[2]&&y>=hole_b[1]&&y<=hole_b[3]) hc++; put(x,y,r,g,b,tr,0); }
    }
    if(hc) fprintf(stderr,"HOLE tri tex %d gour %d semi %d(tr %d) mode %d tp %d,%d cov %ld transparent-texel %ld darktexel %ld v %d,%d %d,%d %d,%d uv %d,%d %d,%d %d,%d rgb %d,%d,%d\n",textured,gour,semi,tr,mode,tpx,tpy,hc,ht,hcm,v0->x,v0->y,v1->x,v1->y,v2->x,v2->y,v0->u,v0->v,v1->u,v1->v,v2->u,v2->v,v0->r,v0->g,v0->b);
}
static void rect(int x,int y,int w,int h,int r,int g,int b,int textured,int rawtex,int tr,int u0,int v0,int clx,int cly){
    if(a2_hi>1&&hv&&!replaying) hi_hazard(x,y,x+w-1,y+h-1);
    gpu_cycles+=30+(long)w*h*(textured?65:30)/100;
    if(old_raster<0) old_raster=getenv("A2OLDRASTER")!=0;
    if(!old_raster){ HJ J; J.kind=1; J.x=x; J.y=y; J.w=w; J.h=h; J.r=r; J.g=g; J.b=b; J.textured=textured; J.rawtex=rawtex; J.tr=tr; J.u0=u0; J.v0=v0; J.clx=clx; J.cly=cly; J.mode=tmode; hj_state(&J); J.S=1; J.fb=vram; J.sub=0; hj_rect(&J,0,1);
        if(a2_hi>1&&hv&&!replaying) hq_rect(x,y,w,h,r,g,b,textured,rawtex,tr,u0,v0,clx,cly); return; }
    for(int j=0;j<h;j++) for(int i=0;i<w;i++){
        if(textured){ uint16_t t=tex(u0+i,v0+j,clx,cly,tmode); if(!t) continue; int sr=(t&31)<<3, sg=((t>>5)&31)<<3, sb=((t>>10)&31)<<3;
            if(!rawtex){ sr=cl(sr*r/128,0,255); sg=cl(sg*g/128,0,255); sb=cl(sb*b/128,0,255); } put(x+i,y+j,sr,sg,sb,tr&&(t&0x8000),0); }
        else put(x+i,y+j,r,g,b,tr,0); }
    if(a2_hi>1&&hv&&!replaying) hq_rect(x,y,w,h,r,g,b,textured,rawtex,tr,u0,v0,clx,cly);
}
static void line(V*a,V*b,int gour,int tr){
    int n=abs(b->x-a->x)>abs(b->y-a->y)?abs(b->x-a->x):abs(b->y-a->y); if(!n){ put(a->x,a->y,a->r,a->g,a->b,tr,0); return; }
    for(int i=0;i<=n;i++){ int x=a->x+(b->x-a->x)*i/n, y=a->y+(b->y-a->y)*i/n; int r=a->r,g=a->g,bb=a->b; if(gour){ r+=(b->r-a->r)*i/n; g+=(b->g-a->g)*i/n; bb+=(b->b-a->b)*i/n; } put(x,y,r,g,bb,tr,0); } }
static int sx(uint32_t w){ int v=w&0x7ff; if(v&0x400) v-=0x800; return v; }
static void settp(uint32_t t){ tpx=t&15; tpy=(t>>4)&1; semi=(t>>5)&3; tmode=(t>>7)&3; if(tmode==3) tmode=2; stat=(stat&~0x7ffu)|(t&0x7ff); }
static void exec(void){
    uint32_t c0=cmd[0]; int op=c0>>24; int r=c0&255, g=(c0>>8)&255, b=(c0>>16)&255; gpu_cmds++; if(plog){ if(gpu_frame>=plog_end){ fclose(plog); plog=0; } else if(op!=0xa0&&op!=0x80&&(ncmd<=24||op<0x80)){ fprintf(plog,"GP0 %02x",op); for(int q=0;q<ncmd&&q<16;q++) fprintf(plog," %08x",cmd[q]); fputc(10,plog); } } lg_init(); if(lg1&&gpu_frame>=lg0&&gpu_frame<=lg1&&(op==2||op==0x80||op==0xa0||op==0xc0||(op>=0x60&&op<=0x7f&&0))){ static int nl; if(nl++<3000) fprintf(stderr,"F%ld GP0 %02x %08x %08x %08x %08x\n",gpu_frame,op,cmd[0],ncmd>1?cmd[1]:0,ncmd>2?cmd[2]:0,ncmd>3?cmd[3]:0); } ghist[op]++; if(op>=0x20&&op<=0x7f){ if(tww||twh||twx||twy) gfeat[0]++; if(setmask) gfeat[1]++; if(chkmask) gfeat[2]++; if(dither) gfeat[3]++; if(op&2) gfeat[4+semi]++; if(op&4) gfeat[8+tmode]++; if(dx1-dx0<300||dy1-dy0<200) gfeat[12]++; }
    if(a2_hi>1&&op>=0x40&&op<=0x5f) hi_sync();
    if(op==0x02){ int x=cmd[1]&0x3f0, y=(cmd[1]>>16)&0x1ff, w=((cmd[2]&0x3ff)+15)&~15, h=(cmd[2]>>16)&0x1ff; uint16_t c=pack(r,g,b); if(a2_hi>1) hi_rect_sync(x,y,w,h,1); for(int j=0;j<h;j++) for(int i=0;i<w;i++) vram[((y+j)&511)*1024+((x+i)&1023)]=c; if(a2_hi>1&&!replaying) hi_resync(x,y,w,h); gpu_prims[0]++; return; }
    if(op>=0x20&&op<=0x3f){
        int quad=(op>>3)&1, tx=(op>>2)&1, gr=(op>>4)&1, tr=(op>>1)&1, raw=op&1; int n=quad?4:3; V v[4]; int k=1; int clx=0,cly=0;
        for(int i=0;i<n;i++){ if(i==0||!gr){ if(i==0){ v[i].r=r; v[i].g=g; v[i].b=b; } else { v[i].r=r; v[i].g=g; v[i].b=b; } } if(gr&&i>0){ uint32_t c=cmd[k++]; v[i].r=c&255; v[i].g=(c>>8)&255; v[i].b=(c>>16)&255; }
            uint32_t p=cmd[k++]; v[i].x=sx(p)+offx; v[i].y=sx(p>>16)+offy; if(tx){ uint32_t t=cmd[k++]; v[i].u=t&255; v[i].v=(t>>8)&255; if(i==0){ clx=((t>>16)&63)*16; cly=(t>>22)&511; } if(i==1) settp(t>>16); } }
        if(tx&&n>=1&&0){}
        gpu_prims[1]++;
        tri(&v[0],&v[1],&v[2],tx,gr,raw,tr,clx,cly,tmode); if(quad) tri(&v[1],&v[2],&v[3],tx,gr,raw,tr,clx,cly,tmode); return; }
    if(op>=0x40&&op<=0x5f){ int gr=(op>>4)&1, tr=(op>>1)&1; V pts[64]; int np=0, k=1; uint32_t cc=c0;
        pts[0].r=r; pts[0].g=g; pts[0].b=b; uint32_t p=cmd[k++]; pts[0].x=sx(p)+offx; pts[0].y=sx(p>>16)+offy; np=1;
        while(k<ncmd){ if(gr){ cc=cmd[k++]; } uint32_t q=cmd[k++]; pts[np].r=cc&255; pts[np].g=(cc>>8)&255; pts[np].b=(cc>>16)&255; if(!gr){ pts[np].r=r; pts[np].g=g; pts[np].b=b; } pts[np].x=sx(q)+offx; pts[np].y=sx(q>>16)+offy; np++; if(np>=63) break; }
        for(int i=0;i+1<np;i++) line(&pts[i],&pts[i+1],gr,tr); if(a2_hi>1&&!replaying){ int lx0=pts[0].x,lx1=lx0,ly0=pts[0].y,ly1=ly0; for(int i=1;i<np;i++){ if(pts[i].x<lx0)lx0=pts[i].x; if(pts[i].x>lx1)lx1=pts[i].x; if(pts[i].y<ly0)ly0=pts[i].y; if(pts[i].y>ly1)ly1=pts[i].y; } if(lx0<dx0)lx0=dx0; if(lx1>dx1)lx1=dx1; if(ly0<dy0)ly0=dy0; if(ly1>dy1)ly1=dy1; if(lx1>=lx0&&ly1>=ly0) hi_resync(lx0,ly0,lx1-lx0+1,ly1-ly0+1); } gpu_prims[2]++; return; }
    if(op>=0x60&&op<=0x7f){ int sz=(op>>3)&3, tx=(op>>2)&1, tr=(op>>1)&1, raw=op&1; int k=1; uint32_t p=cmd[k++]; int x=sx(p)+offx, y=sx(p>>16)+offy; int u=0,vv=0,clx=0,cly=0;
        if(tx){ uint32_t t=cmd[k++]; u=t&255; vv=(t>>8)&255; clx=((t>>16)&63)*16; cly=(t>>22)&511; }
        int w=1,h=1; if(sz==0){ uint32_t s=cmd[k++]; w=s&0x3ff; h=(s>>16)&0x1ff; } else if(sz==2){ w=h=8; } else if(sz==3){ w=h=16; }
        rect(x,y,w,h,r,g,b,tx,raw,tr,u,vv,clx,cly); gpu_prims[3]++; rring[rpos++&4095]=(tx<<30)|((w&0x3ff)<<16)|(h&0x3ff); return; }
    if(op==0x80){ int sx_=cmd[1]&0x3ff, sy=(cmd[1]>>16)&0x1ff, dx=cmd[2]&0x3ff, dy=(cmd[2]>>16)&0x1ff, w=cmd[3]&0x3ff, h=(cmd[3]>>16)&0x1ff; if(!w) w=1024; if(!h) h=512;
        if(sx_==dx&&sy==dy&&!chkmask&&!setmask){ gpu_prims[4]++; return; }
        if(a2_hi>1){ hi_rect_sync(sx_,sy,w,h,0); hi_rect_sync(dx,dy,w,h,1); }
        if(a2_hi>1&&hv&&!replaying){ hv_gen++; const int S=a2_hi; size_t bn=(size_t)w*h*S*S; static uint16_t*tb; static size_t tbn; if(bn>tbn){ tb=realloc(tb,bn*2); tbn=bn; }
            for(int j=0;j<h;j++) for(int q=0;q<S;q++){ const uint16_t*srow=&hv[((size_t)(((sy+j)&511)*S+q))*1024*S]; for(int i=0;i<w;){ int xs=(sx_+i)&1023, n=w-i; if(xs+n>1024) n=1024-xs; memcpy(&tb[(((size_t)j*S+q)*w+i)*S],srow+(size_t)xs*S,(size_t)n*S*2); i+=n; } }
            for(int j=0;j<h;j++){ if(!chkmask){ for(int q=0;q<S;q++){ uint16_t*drow=&hv[((size_t)(((dy+j)&511)*S+q))*1024*S]; for(int i=0;i<w;){ int xd=(dx+i)&1023, n=w-i; if(xd+n>1024) n=1024-xd; const uint16_t*sp=&tb[(((size_t)j*S+q)*w+i)*S]; uint16_t*d=drow+(size_t)xd*S; if(!setmask) memcpy(d,sp,(size_t)n*S*2); else for(int k=0;k<n*S;k++) d[k]=sp[k]|0x8000; i+=n; } } }
                else for(int i=0;i<w;i++){ uint16_t*d0=&vram[((dy+j)&511)*1024+((dx+i)&1023)]; if(*d0&0x8000) continue;
                    for(int q=0;q<S;q++){ uint16_t*d=&hv[((size_t)(((dy+j)&511)*S+q))*1024*S+(size_t)((dx+i)&1023)*S]; const uint16_t*sp=&tb[(((size_t)j*S+q)*w+i)*S]; for(int k=0;k<S;k++) d[k]=sp[k]|(setmask?0x8000:0); } } } }
        for(int j=0;j<h;j++) for(int i=0;i<w;i++){ uint16_t t=vram[((sy+j)&511)*1024+((sx_+i)&1023)]; uint16_t*d=&vram[((dy+j)&511)*1024+((dx+i)&1023)]; if(chkmask&&(*d&0x8000)) continue; *d=t|(setmask?0x8000:0);} gpu_prims[4]++; return; }
}
static int cmd_len(uint32_t c0){ int op=c0>>24;
    if(op==0x02) return 3; if(op>=0x20&&op<=0x3f){ int quad=(op>>3)&1, tx=(op>>2)&1, gr=(op>>4)&1; int n=quad?4:3; return 1+n*(1+tx)+(gr?n-1:0); }
    if(op>=0x40&&op<=0x5f){ if(op&8) return -1; return ((op>>4)&1)?4:3; }
    if(op>=0x60&&op<=0x7f){ int sz=(op>>3)&3, tx=(op>>2)&1; return 2+tx+(sz==0); }
    if(op==0x80||op==0xa0||op==0xc0) return op==0x80?4:3; return 1; }

typedef struct{ uint32_t w[16]; uint8_t n, mk, g3, pad; int16_t ox,oy,ax,ay,bx,by; uint16_t tp; uint32_t tw, sig; } Rc;
typedef struct{ Rc*a; int n,cap; int bad; int nfill,ncopy,nline,npoly,ng3,nrect,nup; } RL;
static RL rlc, rlb, rla;
int a2_interp_on; double a2_interp_t=1.0; double a2_flip_tb=-1, a2_flip_ta=-2;
extern uint8_t gte_stamp[]; extern long a2_flips;
static uint32_t fnv(uint32_t h,uint32_t x){ return (h^x)*16777619u; }
static int poly_pos(uint32_t op,int*pi){ int quad=(op>>3)&1, tx=(op>>2)&1, gr=(op>>4)&1, nv=quad?4:3, k=1, np=0; for(int i=0;i<nv;i++){ if(gr&&i>0) k++; pi[np++]=k++; if(tx) k++; } return np; }
static uint32_t calc_sig(const uint32_t*w,int n){ uint32_t op=w[0]>>24, h=fnv(2166136261u,op); int k=1;
    if(op>=0x20&&op<=0x3f){ int quad=(op>>3)&1, tx=(op>>2)&1, gr=(op>>4)&1, nv=quad?4:3; for(int i=0;i<nv;i++){ if(gr&&i>0) k++; k++; if(tx){ uint32_t t=w[k++]; h=fnv(h,i<2?t:(t&0xffff)); } } return h; }
    return h; }
static int rec_g3(const uint32_t*w,uint32_t op){ int pi[4]; int np=poly_pos(op,pi); const uint8_t cur=(uint8_t)((a2_flips&0x7f)+1), prv=(uint8_t)(((a2_flips-1)&0x7f)+1);
    for(int i=0;i<np;i++){ uint32_t p=w[pi[i]]; uint8_t s=gte_stamp[((p&0x7ff)<<11)|((p>>16)&0x7ff)]; if(s==cur||s==prv) return 1; } return 0; }
static void rec(const uint32_t*w,int n){ if(!a2_interp_on) return; if(rlc.n>=20000){ rlc.bad=1; return; }
    uint32_t op=w[0]>>24; if(op==0x80){ int sx_=w[1]&0x3ff, sy=(w[1]>>16)&0x1ff, dx=w[2]&0x3ff, dy=(w[2]>>16)&0x1ff; if(sx_==dx&&sy==dy&&!chkmask&&!setmask) return; rlc.ncopy++; }
    if(rlc.n>=rlc.cap){ rlc.cap=rlc.cap?rlc.cap*2:1024; rlc.a=realloc(rlc.a,rlc.cap*sizeof(Rc)); }
    Rc*r=&rlc.a[rlc.n++]; if(n>16) n=16; memcpy(r->w,w,n*4); r->n=n; r->ox=offx; r->oy=offy; r->ax=dx0; r->ay=dy0; r->bx=dx1; r->by=dy1; r->tp=stat&0x7ff; r->tw=tww|(twh<<5)|(twx<<10)|(twy<<15); r->mk=setmask|(chkmask<<1); r->sig=calc_sig(w,n); r->g3=0;
    if(op>=0x20&&op<=0x3f){ rlc.npoly++; if(rec_g3(w,op)){ r->g3=1; rlc.ng3++; } } else if(op>=0x60&&op<=0x7f) rlc.nrect++; else if(op>=0x40&&op<=0x5f) rlc.nline++; else if(op==0x02) rlc.nfill++; }
static void exec_rec(void){ rec(cmd,ncmd); exec(); }
void gpu_gp0(uint32_t w){
    vgen++;
    static int lineflag;
    if(xfer_mode==1){ for(int h=0;h<2;h++){ uint16_t px=h?w>>16:w&0xffff; if(xcy<xh){ uint16_t*d=&vram[((xy+xcy)&511)*1024+((xx+xcx)&1023)]; if(!(chkmask&&(*d&0x8000))) *d=px|(setmask?0x8000:0); if(++xcx>=xw){ xcx=0; xcy++; } } } if(xcy>=xh){ xfer_mode=0; if(a2_hi>1&&!replaying){ int ix0=xx,iy0=xy,ix1=xx+xw-1,iy1=xy+xh-1; int hit=(ix0<=dx1&&ix1>=dx0&&iy0<=dy1&&iy1>=dy0)||(ix0<dispx+((dispmode&0x40)?640:((int[]){256,320,512,640})[dispmode&3])&&ix1>=dispx&&iy0<dispy+((dispmode&4)?480:240)&&iy1>=dispy); if(hit) hi_resync(xx,xy,xw,xh); } } return; }
    if(ncmd==0){ int op=w>>24;
        if(op==0x00||op==0x03||(op>=0x04&&op<=0x1e)) return;
        if(op==0x01){ return; }
        if(op>=0xe1&&op<=0xe6){
            switch(op){ case 0xe1: settp(w); dither=(w>>9)&1; dtd=(w>>10)&1; break;
                case 0xe2: tww=w&31; twh=(w>>5)&31; twx=(w>>10)&31; twy=(w>>15)&31; break;
                case 0xe3: lg_init(); if(lg1&&gpu_frame>=lg0&&gpu_frame<=lg1) fprintf(stderr,"F%ld E3 %d,%d\n",gpu_frame,w&1023,(w>>10)&511); dx0=w&1023; dy0=(w>>10)&511; break; case 0xe4: lg_init(); if(lg1&&gpu_frame>=lg0&&gpu_frame<=lg1) fprintf(stderr,"F%ld E4 %d,%d\n",gpu_frame,w&1023,(w>>10)&511); dx1=w&1023; dy1=(w>>10)&511; break;
                case 0xe5: offx=w&0x7ff; if(offx&0x400) offx-=0x800; offy=(w>>11)&0x7ff; if(offy&0x400) offy-=0x800; break;
                case 0xe6: setmask=w&1; chkmask=(w>>1)&1; break; } return; }
        need=cmd_len(w); lineflag=((op>>4)==4||(op>>4)==5); }
    cmd[ncmd++]=w;
    if(need<0){ int op=cmd[0]>>24; int gr=(op>>4)&1;
        if(ncmd>=3 && ((w&0xf000f000)==0x50005000) && (!gr || ((ncmd-1)%2==1 || 1))){ ncmd--; exec_rec(); ncmd=0; return; }
        if(ncmd>=14) { exec_rec(); ncmd=0; } return; }
    if(ncmd>=need){ int op=cmd[0]>>24;
        if(op==0xa0||op==0x80){ static int on=-1,r[4]; if(on<0){ on=0; const char*e=getenv("A2VW"); if(e&&sscanf(e,"%d,%d,%d,%d",r,r+1,r+2,r+3)==4) on=1; } if(on){ int x=cmd[op==0x80?3:1]&0x3ff,y=(cmd[op==0x80?3:1]>>16)&0x1ff,w=cmd[op==0x80?4:2]&0x3ff,h=(cmd[op==0x80?4:2]>>16)&0x1ff; if(!w)w=1024; if(!h)h=512; if(x<=r[2]&&x+w>r[0]&&y<=r[3]&&y+h>r[1]) fprintf(stderr,"VW F%ld op %02x dest %d,%d size %d x %d\n",gpu_frame,op,x,y,w,h); } }
        if(op==0xa0){ { int ux=cmd[1]&0x3ff, uy=(cmd[1]>>16)&0x1ff, uw=cmd[2]&0x3ff, uh=(cmd[2]>>16)&0x1ff; if(!uw) uw=1024; if(!uh) uh=512; if(a2_hi>1) hi_rect_sync(ux,uy,uw,uh,1); if(!replaying&&a2_interp_on&&ux<=dx1&&ux+uw>dx0&&uy<=dy1&&uy+uh>dy0) rlc.bad=1; } xx=cmd[1]&0x3ff; xy=(cmd[1]>>16)&0x1ff; xw=cmd[2]&0x3ff; xh=(cmd[2]>>16)&0x1ff; if(!xw) xw=1024; if(!xh) xh=512; xcx=xcy=0; xfer_mode=1; gpu_prims[5]++; ncmd=0; return; }
        if(op==0xc0){ xx=cmd[1]&0x3ff; xy=(cmd[1]>>16)&0x1ff; xw=cmd[2]&0x3ff; xh=(cmd[2]>>16)&0x1ff; if(!xw) xw=1024; if(!xh) xh=512; xcx=xcy=0; xfer_mode=2; ncmd=0; return; }
        exec_rec(); ncmd=0; }
}

double (*a2_vbl_clock)(void);
void gpu_render_display(uint32_t*dst,int W,int H);
static void interp_eval(int,int);
static int pdx=-1,pdy=-1;
static void flip_frame(void){
    { static long vd=-1,nfl; if(vd<0){ const char*e=getenv("A2VDUMP"); vd=e?atol(e):0; } if(vd&&++nfl==vd){ hi_sync(); const char*f=getenv("A2VDUMPF"); FILE*o=fopen(f?f:"/tmp/vram.bin","wb"); if(o){ fwrite(vram,2,512*1024,o); if(hv&&a2_hi>1&&getenv("A2HVDUMP")) fwrite(hv,2,(size_t)512*1024*a2_hi*a2_hi,o); fclose(o); } if(getenv("A2VDUMPX")){ if(atoi(getenv("A2VDUMPX"))==2) exit(0); _exit(0); } } }
    if(!a2_interp_on){ rlc.n=0; rlc.bad=0; pdx=dispx; pdy=dispy; return; }
    { static int st=-1; if(st<0) st=getenv("A2FRAMESTAT")?atoi(getenv("A2FRAMESTAT")):0; static long nst; if(st&&!(nst++%st)) fprintf(stderr,"FRAMESTAT F%ld flip %ld recs %d polys %d (3D %d) rects %d lines %d fills %d copies %d bad %d  draw area %d,%d-%d,%d\n",gpu_frame,a2_flips,rlc.n,rlc.npoly,rlc.ng3,rlc.nrect,rlc.nline,rlc.nfill,rlc.ncopy,rlc.bad,dx0,dy0,dx1,dy1); }
    if(getenv("A2INTERPEVAL")) interp_eval(pdx,pdy);
    RL t=rla; rla=rlb; rlb=rlc; rlc=t; rlc.n=0; rlc.bad=0; rlc.nfill=rlc.ncopy=rlc.nline=rlc.npoly=rlc.ng3=rlc.nrect=rlc.nup=0;
    a2_flip_ta=a2_flip_tb; a2_flip_tb=a2_vbl_clock?a2_vbl_clock():0; pdx=dispx; pdy=dispy; }
void gpu_render_display_i(uint32_t*dst,int W,int H){ gpu_render_display(dst,W,H); }
void gpu_gp1(uint32_t w){ int op=w>>24; lg_init(); if(lg1&&gpu_frame>=lg0&&gpu_frame<=lg1&&op!=5) fprintf(stderr,"F%ld GP1(%02x) %06x\n",gpu_frame,op,w&0xffffff); if(getenv("A2GPULOG")&&(op==3||op==6||op==7||op==2||op==0)){ static int ng; if(ng++<0) fprintf(stderr,"GP1(%02x) %06x\n",op,w&0xffffff); }
    switch(op){ case 0: hrange=0xC60260; vrange=0x040010; dispoff=1; stat=0x14802000; ncmd=0; xfer_mode=0; dx0=dy0=0; dx1=1023; dy1=511; offx=offy=0; break;
        case 1: ncmd=0; break; case 2: stat&=~(1u<<24); break;
        case 3: dispoff=w&1; stat=(stat&~(1u<<23))|((w&1)<<23); break;
        case 4: dmadir=w&3; stat=(stat&~(3u<<29))|(dmadir<<29); break;
        case 6: hrange=w&0xffffff; break;
        case 7: vrange=w&0xfffff; break;
        case 5: { extern long a2_gp05, a2_flips; extern long a2_flip_hist[8]; static long lastf=-1; a2_gp05++; if((w&0x3ff)!=(uint32_t)dispx||((w>>10)&0x1ff)!=(uint32_t)dispy){ long d=gpu_frame-lastf; lastf=gpu_frame; if(d<0)d=0; if(d>7)d=7; a2_flip_hist[d]++; a2_flips++; } } lg_init(); if(lg1&&gpu_frame>=lg0&&gpu_frame<=lg1) fprintf(stderr,"F%ld GP1(05) %d,%d\n",gpu_frame,w&0x3ff,(w>>10)&0x1ff); if(getenv("A2GPULOG")&&((w&0x3ff)!=(uint32_t)dispx||((w>>10)&0x1ff)!=(uint32_t)dispy)){ static int n5; if(n5++<0) fprintf(stderr,"GP1(05) display start %d,%d\n",w&0x3ff,(w>>10)&0x1ff);} { int ch=((w&0x3ff)!=(uint32_t)dispx||((w>>10)&0x1ff)!=(uint32_t)dispy); dispx=w&0x3ff; dispy=(w>>10)&0x1ff; if(ch) flip_frame(); } break;
        case 8: if(getenv("A2GPULOG")&&(w&0xff)!=(uint32_t)dispmode) fprintf(stderr,"GP1(08) display mode %08x\n",w); dispmode=w&0xff; stat=(stat&~0x7f4000u)|(((w&3)<<17)|(((w>>2)&1)<<19)|(((w>>3)&1)<<20)|(((w>>4)&1)<<21)|(((w>>5)&1)<<22)|(((w>>6)&1)<<16)|(((w>>7)&1)<<14)); break; }
}
uint32_t gpu_status(void){ uint32_t s=stat|0x1c000000u; if(xfer_mode==2) s|=1u<<27; return s; }
uint32_t gpu_read_data(void){ if(xfer_mode==2){ uint32_t v=0; for(int h=0;h<2;h++){ uint16_t px=0; if(xcy<xh){ px=vram[((xy+xcy)&511)*1024+((xx+xcx)&1023)]; if(++xcx>=xw){ xcx=0; xcy++; } } v|=(uint32_t)px<<(16*h); } if(xcy>=xh) xfer_mode=0; rdlatch=v; return v; } return rdlatch; }
void gpu_init(void){ { const char*e=getenv("A2HIRES"); a2_hi=e?atoi(e):1; if(a2_hi<1) a2_hi=1; if(a2_hi>4) a2_hi=4; if(a2_hi>1){ hv=calloc((size_t)1024*a2_hi*512*a2_hi,2); if(!hv) a2_hi=1; else hi_init_threads(); } if(a2_hi==1){ const char*u=getenv("A2UNLOCK"); if(u&&atoi(u)>=60){ a2_nat_interp=1; hi_init_threads(); } } } a2_old_spr=getenv("A2OLDSPR")!=0; a2_texfilter=getenv("A2TEXFILTER")?atoi(getenv("A2TEXFILTER")):0; memset(vram,0,sizeof vram); gpu_gp1(0); }

void gpu_dma_to(uint8_t*ram,uint32_t madr,uint32_t words){ for(uint32_t i=0;i<words;i++){ uint32_t a=(madr+4*i)&0x1ffffc; gpu_gp0(ram[a]|(ram[a+1]<<8)|(ram[a+2]<<16)|((uint32_t)ram[a+3]<<24)); } }
void gpu_dma_from(uint8_t*ram,uint32_t madr,uint32_t words){ for(uint32_t i=0;i<words;i++){ uint32_t v=gpu_read_data(); uint32_t a=(madr+4*i)&0x1ffffc; ram[a]=v; ram[a+1]=v>>8; ram[a+2]=v>>16; ram[a+3]=v>>24; } }
int gpu_dma_list(uint8_t*ram,uint32_t madr){ int n=0, guard=0; uint32_t a=madr&0x1ffffc; static uint32_t lo=~0u,hi=0; static long cnt; static uint32_t bmin[32],bmax[32]; static long bn[32]; if(getenv("A2PRIMLOG")){ if(!(cnt++%400)){ fprintf(stderr,"PRIMLOG start %08x:",madr); for(int b=0;b<32;b++) if(bn[b]) fprintf(stderr," [%05x-%05x n%ld]",bmin[b],bmax[b],bn[b]); fprintf(stderr,"\n"); } }
    { static long ats[64]; static int nat=-1,iat; if(nat<0){ nat=0; const char*e=getenv("A2PLOGAT"); while(e&&*e&&nat<64){ ats[nat++]=atol(e); e=strchr(e,','); if(e) e++; } } if(!plog&&iat<nat&&gpu_frame>=ats[iat]){ iat++; gpu_plog_start(); } }
    if(plog) fprintf(plog,"LIST start %08x frame %ld\n",madr,gpu_frame);
    for(;;){ uint32_t h=ram[a]|(ram[a+1]<<8)|(ram[a+2]<<16)|((uint32_t)ram[a+3]<<24); int cnt=h>>24; for(int i=1;i<=cnt;i++){ uint32_t p=(a+4*i)&0x1ffffc; gpu_gp0(ram[p]|(ram[p+1]<<8)|(ram[p+2]<<16)|((uint32_t)ram[p+3]<<24)); } n++; { int b=a>>16; if(!bn[b]||a<bmin[b]) bmin[b]=a; if(a>bmax[b]) bmax[b]=a; bn[b]++; }
        if((h&0xffffff)==0xffffff||(h&0x800000)||++guard>200000) break; a=h&0x1ffffc; } return n; }
void gpu_stats(void){ fprintf(stderr,"GPU: cmds %ld fill %ld poly %ld line %ld rect %ld vram2vram %ld cpu2vram %ld\n",gpu_cmds,gpu_prims[0],gpu_prims[1],gpu_prims[2],gpu_prims[3],gpu_prims[4],gpu_prims[5]); }
int gpu_disp_x(void){ return dispx; } int gpu_disp_y(void){ return dispy; }
int gpu_dump_ppm(const char*path,int x,int y,int w,int h){ FILE*f=fopen(path,"wb"); if(!f) return 0; fprintf(f,"P6\n%d %d\n255\n",w,h); for(int j=0;j<h;j++) for(int i=0;i<w;i++){ uint16_t p=vram[((y+j)&511)*1024+((x+i)&1023)]; fputc((p&31)<<3,f); fputc(((p>>5)&31)<<3,f); fputc(((p>>10)&31)<<3,f);} fclose(f); return 1; }
int gpu_vram_save(const char*p){ FILE*f=fopen(p,"wb"); if(!f) return 0; fwrite(vram,2,512*1024,f); fclose(f); return 1; }
uint32_t gpu_vram_hash(void){ uint32_t h=2166136261u; for(int i=0;i<512*1024;i++){ h^=vram[i]; h*=16777619u; } return h; }

void gpu_screen_stats(int x0,int y0,char*out,int n){
    int nb=0,minx=9999,miny=9999,maxx=-1,maxy=-1; static uint8_t seen[32768]; memset(seen,0,sizeof seen); int dc=0;
    for(int y=0;y<240;y++) for(int x=0;x<320;x++){ uint16_t p=vram[((y0+y)&511)*1024+((x0+x)&1023)]&0x7fff; if(!seen[p]){ seen[p]=1; dc++; } if(p){ nb++; if(x<minx)minx=x; if(x>maxx)maxx=x; if(y<miny)miny=y; if(y>maxy)maxy=y; } }
    snprintf(out,n,"nonblack %d/%d bbox %d,%d-%d,%d colours %d",nb,320*240,minx,miny,maxx,maxy,dc); }

void gpu_rect_summary(char*out,int n){
    uint32_t k[4096]; int c[4096]; int m=0; for(int i=0;i<4096;i++){ uint32_t v=rring[i]; if(!v) continue; int j; for(j=0;j<m;j++) if(k[j]==v){ c[j]++; break; } if(j==m){ k[m]=v; c[m]=1; m++; } }
    out[0]=0; for(int t=0;t<3;t++){ int b=-1; for(int j=0;j<m;j++) if(c[j]>0&&(b<0||c[j]>c[b])) b=j; if(b<0) break; char tmp[48]; snprintf(tmp,sizeof tmp,"%s%ux%u:%d ",(k[b]>>30)?"t":"f",(k[b]>>16)&0x3ff,k[b]&0x3ff,c[b]); strncat(out,tmp,n-strlen(out)-1); c[b]=-c[b]; } }

void gpu_blit_argb(uint32_t*dst,int x,int y,int w,int h){ for(int j=0;j<h;j++) for(int i=0;i<w;i++){ uint16_t p=vram[((y+j)&511)*1024+((x+i)&1023)]; uint32_t r=(p&31)<<3, g=((p>>5)&31)<<3, b=((p>>10)&31)<<3; dst[j*w+i]=0xff000000u|(r<<16)|(g<<8)|b; } }

void gpu_render_display(uint32_t*dst,int W,int H){
    for(int i=0;i<W*H;i++) dst[i]=0xff000000u;
    if(dispoff) return;
    int div=(dispmode&0x40)?7:((int[]){10,8,5,4})[dispmode&3];
    int x1=hrange&0xfff, x2=(hrange>>12)&0xfff, y1=vrange&0x3ff, y2=(vrange>>10)&0x3ff;
    int i480=(dispmode&4)&&(dispmode&0x20), c24=dispmode&0x10;

    static int xmap[4096], xcap=4096; static uint32_t lut[32768]; static int lut_ok;
    if(!lut_ok){ for(int p=0;p<32768;p++){ uint32_t r=(p&31)<<3, g=((p>>5)&31)<<3, b=((p>>10)&31)<<3; lut[p]=0xff000000u|(r<<16)|(g<<8)|b; } lut_ok=1; }
    if(W>xcap) return;
    int ox0=W,ox1=-1; for(int ox=0;ox<W;ox++){ int cyc=0x260+ox*4*2560/(W*4/1); if(W==640) cyc=0x260+ox*4;
        if(cyc<x1||cyc>=x2){ xmap[ox]=-1; continue; } int px=(cyc-x1)/div; xmap[ox]=px; if(ox<ox0) ox0=ox; ox1=ox; }
    for(int oy=0;oy<H;oy++){
        int vy;
        if(i480){ int r=oy-2*(y1-16); if(r<0||r>=2*(y2-y1)) continue; vy=dispy+r; }
        else { int L=16+oy/2; if(L<y1||L>=y2) continue; vy=dispy+(L-y1); }
        const uint16_t*row=&vram[(vy&511)*1024]; uint32_t*d=dst+(size_t)oy*W;
        if(c24){ for(int ox=ox0;ox<=ox1;ox++){ int px=xmap[ox]; if(px<0) continue; uint32_t r,g,b; int bo=px*3; int vx=dispx+bo/2; uint16_t a=row[vx&1023], c=row[(vx+1)&1023];
                uint8_t by[4]={a&255,a>>8,c&255,c>>8}; int k=bo&1; r=by[k]; g=by[k+1]; b=by[k+2]; d[ox]=0xff000000u|(r<<16)|(g<<8)|b; } }
        else { int ob=dispx; for(int ox=ox0;ox<=ox1;ox++){ int px=xmap[ox]; if(px<0) continue; d[ox]=lut[row[(ob+px)&1023]&0x7fff]; } } } }

static int hd_valid; static unsigned long hd_gen; static uint32_t*hd_dst; static int hd_k[8];

typedef struct{ uint32_t*buf; size_t cap; int*xm; int xmcap; int W,H,S,y1,y2,dispy; uint16_t*fb; int OS,R; } CvSlot;
static CvSlot cvs[4]; static int cv_last=-1; static int cv_pend=-1; static long cv_pend_seq; static int cv_next;
static uint32_t cv_lut[32768]; static int cv_lut_ok;
static int cv_alloc(CvSlot*c,int W,int H){ size_t n=(size_t)W*H; if(c->cap<n){ free(c->buf); c->buf=(uint32_t*)malloc(n*4); c->cap=c->buf?n:0; if(!c->buf) return 0; } if(c->xmcap<W){ free(c->xm); c->xm=(int*)malloc(sizeof(int)*W); c->xmcap=c->xm?W:0; if(!c->xm) return 0; } return 1; }
static void hconv(int idx,int wk,int K){ CvSlot*c=&cvs[idx]; const int W=c->W,H=c->H,S=c->S,OS=c->OS,RR=c->R,y1=c->y1,y2=c->y2; const int*xm=c->xm; const uint32_t*lut=cv_lut;

    for(int r=0;r<H;r++){ uint32_t*d=c->buf+(size_t)r*W; int Ln=16*OS+r;
        if(Ln<y1*OS||Ln>=y2*OS){ if(K>1&&(((r>>4)%K)!=wk)) continue; for(int i=0;i<W;i++) d[i]=0xff000000u; continue; }
        int Y=((c->dispy*OS+(Ln-y1*OS))%(512*OS))/RR; if(K>1&&(((Y>>4)%K)!=wk)) continue; const uint16_t*row=&c->fb[(size_t)Y*1024*S];
        for(int ox=0;ox<W;ox++){ int X=xm[ox]; d[ox]=X<0?0xff000000u:lut[row[X]&0x7fff]; } } }
static void hi_wait_seq(long seq){ if(hq_mindone()>=seq) return; hq_publish(); for(int sp=0;sp<60000&&hq_mindone()<seq;sp++) CPU_RELAX();
    if(hq_mindone()<seq){ a2_lock(); __atomic_store_n(&hq_waiting,1,__ATOMIC_SEQ_CST); while(hq_mindone()<seq) a2_wait(); __atomic_store_n(&hq_waiting,0,__ATOMIC_SEQ_CST); a2_unlock(); } }
int cv_drop_pending(void){ cv_pend=-1; hd_valid=0; return 0; }
static void cv_flush(void){ if(cv_pend>=0){ hi_wait_seq(cv_pend_seq); cv_pend=-1; } }
int gpu_render_display_hi(uint32_t**out,int*Wp,int*Hp){
    const int S=a2_hi; if(S<2||!hv){ cv_flush(); hd_valid=0; return 0; } const int W=320*S, H=240*S; *Wp=W; *Hp=H;
    static int async=-1; if(async<0){ const char*e=getenv("A2SYNCDISP"); async=!(e&&atoi(e)); }
    const int useasync=async&&hK>0&&H>=480;
    int ready=-1; if(useasync&&cv_pend>=0){ hi_wait_seq(cv_pend_seq); ready=cv_pend; cv_pend=-1; } else cv_flush();
    if(!cv_lut_ok){ cv_lut_ok=1; for(int p=0;p<32768;p++) cv_lut[p]=0xff000000u|(((p&31)<<3)<<16)|((((p>>5)&31)<<3)<<8)|(((p>>10)&31)<<3); }
    int slot=ready>=0?1-ready:cv_next; CvSlot*c=&cvs[slot]; if(!cv_alloc(c,W,H)||!cv_alloc(&cvs[1-slot],W,H)){ hd_valid=0; return 0; }
    if(!useasync) hi_sync_region(dispx,dispy,dispx+639,dispy+479);
    if(dispoff){ hd_valid=0; for(int i=0;i<W*H;i++) c->buf[i]=0xff000000u; *out=c->buf; cv_last=slot; cv_next=1-slot; return 1; }
    if(((dispmode&4)&&(dispmode&0x20))||(dispmode&0x10)){ hd_valid=0; return 0; }
    int div=(dispmode&0x40)?7:((int[]){10,8,5,4})[dispmode&3];
    int x1=hrange&0xfff, x2=(hrange>>12)&0xfff, y1=vrange&0x3ff, y2=(vrange>>10)&0x3ff;
    int show=-1;
    { int key[8]={dispx,dispy,dispmode,(int)hrange,(int)vrange,dispoff,S,0}; int same=(hd_valid&&hd_gen==hv_gen&&!memcmp(key,hd_k,sizeof key));
      if(same){ if(ready>=0){ cv_last=ready; *out=cvs[ready].buf; return 1; } if(cv_last>=0) *out=cvs[cv_last].buf; return 2; }
      memcpy(hd_k,key,sizeof key); hd_gen=hv_gen; hd_valid=0; }
    c->W=W; c->H=H; c->S=S; c->OS=S; c->R=1; c->y1=y1; c->y2=y2; c->dispy=dispy; c->fb=hv;
    for(int ox=0;ox<W;ox++){ int cn=(0x260-x1)*S+ox*8, cm=(0x260-x2)*S+ox*8; c->xm[ox]=(cn<0||cm>=0)?-1:(dispx*S+cn/div)%(1024*S); }
    if(useasync){ HJ loc; HJ*J=hq_slot(&loc); J->kind=2; J->mode=slot; J->tpx=J->tpy=0; hq_note(J,dispx,dispy,dispx+639,dispy+479,0); hq_head++; cv_pend=slot; cv_pend_seq=hq_head; cv_next=1-slot; hq_publish();
        hd_valid=1; if(ready>=0){ cv_last=ready; *out=cvs[ready].buf; return 1; } if(cv_last>=0) *out=cvs[cv_last].buf; return 2; }
    hconv(slot,0,1); cv_next=1-slot; cv_last=slot; *out=c->buf; hd_valid=1; (void)show; return 1; }

void gpu_hi_check(void){ const int S=a2_hi; hi_sync(); if(S<2||!hv||dispoff||(dispmode&0x10)||((dispmode&4)&&(dispmode&0x20))) return;
    int dw=(dispmode&0x40)?640:((int[]){256,320,512,640})[dispmode&3], dh=(dispmode&4)?480:240; double tot=0; long n=0, bad=0;
    for(int y=0;y<dh;y+=2) for(int x=0;x<dw;x+=2){ int Y=(dispy+y)&511, X=(dispx+x)&1023; uint16_t lo=vram[Y*1024+X]; int sum[3]={0,0,0};
        for(int sy=0;sy<S;sy++) for(int sx_=0;sx_<S;sx_++){ uint16_t p=hv[((size_t)(Y*S+sy))*1024*S+(size_t)X*S+sx_]; sum[0]+=p&31; sum[1]+=(p>>5)&31; sum[2]+=(p>>10)&31; }
        int d=abs(sum[0]/(S*S)-(lo&31))+abs(sum[1]/(S*S)-((lo>>5)&31))+abs(sum[2]/(S*S)-((lo>>10)&31)); tot+=d; n++; if(d>12) bad++; }
    fprintf(stderr,"HICHK mean abs diff (5-bit units, 3 channels summed) %.3f, pixels with diff>12: %ld of %ld\n",n?tot/n:0.0,bad,n); }

void gpu_render_stats(char*out,int n){ static uint32_t buf[640*480]; gpu_render_display(buf,640,480); int nb=0,minx=9999,miny=9999,maxx=-1,maxy=-1;
    for(int y=0;y<480;y++) for(int x=0;x<640;x++) if(buf[y*640+x]&0xffffff){ nb++; if(x<minx)minx=x; if(x>maxx)maxx=x; if(y<miny)miny=y; if(y>maxy)maxy=y; }
    snprintf(out,n,"shown %d px bbox %d,%d-%d,%d",nb,minx,miny,maxx,maxy); }
int a2_nocull; __attribute__((constructor)) static void nc_init(void){ a2_nocull=getenv("A2NOCULL")?atoi(getenv("A2NOCULL")):0; }

__attribute__((constructor)) static void l2_init(void){ extern int a2_force_l2; a2_force_l2=getenv("A2L2")?atoi(getenv("A2L2")):0;     }
long a2_gp05, a2_flips; long a2_flip_hist[8];

static uint16_t *hv_alt;
typedef struct{ uint32_t sig; int idx; } SI;
static int cmp_si(const void*x,const void*y){ const SI*p=(const SI*)x,*q=(const SI*)y; if(p->sig!=q->sig) return p->sig<q->sig?-1:1; return p->idx-q->idx; }
static int *mtch; static int mtch_cap, mtch_ok; static long mtch_key=-1; static int m_dr, m_mt; static unsigned char*m_used; static int m_used_cap; static SI *m_sa,*m_sb; static int m_sacap,m_sbcap;
static int a2_idmax=64; static int m_nocls, m_nocand, m_farcand, m_moved;
static void poly_rel(const Rc*r,uint32_t op,int*x,int*y,int*np){ int pi[4]; *np=poly_pos(op,pi); for(int i=0;i<*np;i++){ uint32_t p=r->w[pi[i]]; x[i]=sx(p)+r->ox-r->ax; y[i]=sx(p>>16)+r->oy-r->ay; } }
static void disp_rect(int*x0,int*y0,int*x1,int*y1){
    int div=(dispmode&0x40)?7:((int[]){10,8,5,4})[dispmode&3]; int hx1=hrange&0xfff, hx2=(hrange>>12)&0xfff, vy1=vrange&0x3ff, vy2=(vrange>>10)&0x3ff;
    int dw=(hx2-hx1)/div, dh=(dispmode&4)?2*(vy2-vy1):(vy2-vy1); int mw=(dispmode&0x40)?640:((int[]){256,320,512,640})[dispmode&3]; if(dw<1||dw>mw) dw=mw; if(dh<1||dh>((dispmode&4)?480:240)) dh=(dispmode&4)?480:240;
    *x0=dispx; *y0=dispy; *x1=dispx+dw-1; *y1=dispy+dh-1; }
static int interp_mode_ok(void){ if(dispoff) return 0; if(a2_hi>1){ if(!hv) return 0; } else if(!a2_nat_interp) return 0; if(((dispmode&4)&&(dispmode&0x20))||(dispmode&0x10)) return 0; if(a2_hi<2&&((dispmode&3)==3||(dispmode&0x40))) return 0; return 1; }
static int build_match_(void);
double bm_ns; long bm_n;
static int build_match(void){ if(mtch_key==a2_flips) return mtch_ok; struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0); int r=build_match_(); clock_gettime(CLOCK_MONOTONIC,&t1); bm_ns+=(t1.tv_sec-t0.tv_sec)*1e9+(t1.tv_nsec-t0.tv_nsec); bm_n++; return r; }
static int build_match_(void){
    if(mtch_key==a2_flips) return mtch_ok; mtch_key=a2_flips; mtch_ok=0; m_dr=m_mt=0;
    if(rla.bad||rlb.bad||rla.n<4||rlb.n<4) return 0;
    { int X0,Y0,X1,Y1,fill=0; disp_rect(&X0,&Y0,&X1,&Y1);
      for(int i=0;i<rlb.n&&!fill;i++){ const Rc*b=&rlb.a[i]; if((b->w[0]>>24)!=0x02) continue; int x=b->w[1]&0x3f0, y=(b->w[1]>>16)&0x1ff, w=((b->w[2]&0x3ff)+15)&~15, h=(b->w[2]>>16)&0x1ff; if(x<=X0&&y<=Y0&&x+w>X1&&y+h>Y1) fill=1; }
      if(!fill) return 0; }
    if(rlb.n>mtch_cap){ mtch_cap=rlb.n+256; mtch=(int*)realloc(mtch,mtch_cap*sizeof(int)); }
    if(rla.n>m_used_cap){ m_used_cap=rla.n+256; m_used=(unsigned char*)realloc(m_used,m_used_cap); } memset(m_used,0,rla.n);
    if(rla.n>m_sacap){ m_sacap=rla.n+256; m_sa=(SI*)realloc(m_sa,m_sacap*sizeof(SI)); } if(rlb.n>m_sbcap){ m_sbcap=rlb.n+256; m_sb=(SI*)realloc(m_sb,m_sbcap*sizeof(SI)); }
    for(int i=0;i<rlb.n;i++) mtch[i]=-1;

    static int *pa_x,*pa_y; static unsigned char*pa_n; static int pa_cap; typedef struct{ int cost,b,a; } PC; static PC*pc; static int pc_cap;
    if(rla.n>pa_cap){ pa_cap=rla.n+256; pa_x=(int*)realloc(pa_x,pa_cap*4*sizeof(int)); pa_y=(int*)realloc(pa_y,pa_cap*4*sizeof(int)); pa_n=(unsigned char*)realloc(pa_n,pa_cap); }
    for(int j=0;j<rla.n;j++){ const Rc*a=&rla.a[j]; uint32_t op=a->w[0]>>24; pa_n[j]=0; if(op>=0x20&&op<=0x3f){ int np; poly_rel(a,op,&pa_x[j*4],&pa_y[j*4],&np); pa_n[j]=(unsigned char)np; } }
    int npc=0, nb=0;
    for(int i=0;i<rlb.n;i++){ const Rc*b=&rlb.a[i]; uint32_t op=b->w[0]>>24; if(!(op>=0x20&&op<=0x3f)||!b->g3) continue; nb++;
        int bx[4],by[4],bn; poly_rel(b,op,bx,by,&bn); int cnt=0; int cc[3]={1<<30,1<<30,1<<30}, cj[3]={-1,-1,-1};
        for(int j=0;j<rla.n;j++){ if(pa_n[j]!=bn) continue; const Rc*a=&rla.a[j]; if(a->sig!=b->sig||(a->w[0]>>24)!=op) continue;
            const int*ax=&pa_x[j*4],*ay=&pa_y[j*4]; int cost=0, ok=1; for(int q=0;q<bn;q++){ int dx=bx[q]-ax[q], dy=by[q]-ay[q]; if(dx<0) dx=-dx; if(dy<0) dy=-dy; if(dx>a2_idmax||dy>a2_idmax){ ok=0; break; } cost+=dx+dy; }
            if(!ok) continue; uint32_t ca=a->w[0], cb=b->w[0]; cost=cost*2+(abs((int)(ca&255)-(int)(cb&255))+abs((int)((ca>>8)&255)-(int)((cb>>8)&255))+abs((int)((ca>>16)&255)-(int)((cb>>16)&255)))/2;
            int k=2; if(cost<cc[2]){ cc[2]=cost; cj[2]=j; while(k>0&&cc[k]<cc[k-1]){ int t=cc[k]; cc[k]=cc[k-1]; cc[k-1]=t; t=cj[k]; cj[k]=cj[k-1]; cj[k-1]=t; k--; } } (void)cnt; }
        for(int k=0;k<3;k++) if(cj[k]>=0){ if(npc>=pc_cap){ pc_cap=pc_cap?pc_cap*2:4096; pc=(PC*)realloc(pc,pc_cap*sizeof(PC)); } pc[npc].cost=cc[k]; pc[npc].b=i; pc[npc].a=cj[k]; npc++; } }
    {
      int maxc=0; for(int i=0;i<npc;i++) if(pc[i].cost>maxc) maxc=pc[i].cost; if(maxc>20000) maxc=20000; int*cntc=(int*)calloc(maxc+2,sizeof(int)); PC*sp=(PC*)malloc((npc+1)*sizeof(PC));
      for(int i=0;i<npc;i++){ int c=pc[i].cost>maxc?maxc:pc[i].cost; cntc[c+1]++; } for(int c=0;c<=maxc;c++) cntc[c+1]+=cntc[c]; for(int i=0;i<npc;i++){ int c=pc[i].cost>maxc?maxc:pc[i].cost; sp[cntc[c]++]=pc[i]; }
      int mt0=0; for(int i=0;i<npc;i++){ int bi=sp[i].b, ai=sp[i].a; if(mtch[bi]>=0||m_used[ai]) continue; mtch[bi]=ai; m_used[ai]=1; mt0++; int bx[4],by[4],bn; poly_rel(&rlb.a[bi],rlb.a[bi].w[0]>>24,bx,by,&bn); int d=0; for(int q=0;q<bn;q++) d+=abs(bx[q]-pa_x[ai*4+q])+abs(by[q]-pa_y[ai*4+q]); if(d>0) m_moved++; }
      free(cntc); free(sp); m_nocand=nb-mt0; m_dr=nb; int mt=mt0;
      m_mt=mt; mtch_ok=(m_dr>=4&&mt*2>=m_dr&&(m_moved>0||mt<m_dr)); return mtch_ok; } }

static void rep_tri(V*v0,V*v1,V*v2,int textured,int gour,int rawtex,int tr,int clx,int cly,int mode){
    long long area=(long long)(v1->x-v0->x)*(v2->y-v0->y)-(long long)(v1->y-v0->y)*(v2->x-v0->x); if(!area) return;
    int mnx=v0->x,mxx=v0->x,mny=v0->y,mxy=v0->y; const V*q[2]={v1,v2}; for(int i=0;i<2;i++){ if(q[i]->x<mnx) mnx=q[i]->x; if(q[i]->x>mxx) mxx=q[i]->x; if(q[i]->y<mny) mny=q[i]->y; if(q[i]->y>mxy) mxy=q[i]->y; }
    if(mxx-mnx>=1024*16||mxy-mny>=512*16) return;
    int bx0=(mnx>>4)-1, bx1=(mxx>>4)+1, by0=(mny>>4)-1, by1=(mxy>>4)+1; if(bx0<dx0) bx0=dx0; if(bx1>dx1) bx1=dx1; if(by0<dy0) by0=dy0; if(by1>dy1) by1=dy1; if(bx0>bx1||by0>by1) return;
    cur_sub=4; cur_fb=hv_alt; hq_tri(v0,v1,v2,textured,gour,rawtex,tr,clx,cly,mode,bx0,by0,bx1,by1); cur_sub=0; cur_fb=0; }
static inline int rnd16(double v){ double x=v*16.0+0.5; int i=(int)x; if(x<0&&(double)i!=x) i--; return i; }
static void rep_poly(const Rc*b,const Rc*a,double t){
    uint32_t op=b->w[0]>>24; int quad=(op>>3)&1, tx=(op>>2)&1, gr=(op>>4)&1, tr=(op>>1)&1, raw=op&1, n=quad?4:3; uint32_t c0=b->w[0]; int r=c0&255, g=(c0>>8)&255, bl=(c0>>16)&255;
    int axx[4]={0},ayy[4]={0},an=0; if(a) poly_rel(a,op,axx,ayy,&an);
    V v[4]; int k=1, clx=0, cly=0;
    for(int i=0;i<n;i++){ v[i].r=r; v[i].g=g; v[i].b=bl; v[i].u=v[i].v=0; if(gr&&i>0){ uint32_t c=b->w[k++]; v[i].r=c&255; v[i].g=(c>>8)&255; v[i].b=(c>>16)&255; }
        uint32_t p=b->w[k++]; int xr=sx(p)+offx-dx0, yr=sx(p>>16)+offy-dy0; int X16=xr*16, Y16=yr*16;
        if(a&&an==n){ X16=rnd16(axx[i]+(xr-axx[i])*t); Y16=rnd16(ayy[i]+(yr-ayy[i])*t); }
        v[i].x=X16+dx0*16; v[i].y=Y16+dy0*16;
        if(tx){ uint32_t tt=b->w[k++]; v[i].u=tt&255; v[i].v=(tt>>8)&255; if(i==0){ clx=((tt>>16)&63)*16; cly=(tt>>22)&511; } if(i==1) settp(tt>>16); } }
    rep_tri(&v[0],&v[1],&v[2],tx,gr,raw,tr,clx,cly,tmode); if(quad) rep_tri(&v[1],&v[2],&v[3],tx,gr,raw,tr,clx,cly,tmode); }
static void rep_rect(const Rc*b){
    uint32_t c0=b->w[0]; int op=c0>>24, r=c0&255, g=(c0>>8)&255, bl=(c0>>16)&255; int sz=(op>>3)&3, tx=(op>>2)&1, tr=(op>>1)&1, raw=op&1; int k=1; uint32_t p=b->w[k++]; int x=sx(p)+offx, y=sx(p>>16)+offy; int u=0,vv=0,clx=0,cly=0;
    if(tx){ uint32_t t=b->w[k++]; u=t&255; vv=(t>>8)&255; clx=((t>>16)&63)*16; cly=(t>>22)&511; }
    int w=1,h=1; if(sz==0){ uint32_t s=b->w[k++]; w=s&0x3ff; h=(s>>16)&0x1ff; } else if(sz==2){ w=h=8; } else if(sz==3){ w=h=16; }
    if(w<=0||h<=0) return; cur_fb=hv_alt; hq_rect(x,y,w,h,r,g,bl,tx,raw,tr,u,vv,clx,cly); cur_fb=0; }
static void rep_fill(const Rc*b,int*queued){
    int x=b->w[1]&0x3f0, y=(b->w[1]>>16)&0x1ff, w=((b->w[2]&0x3ff)+15)&~15, h=(b->w[2]>>16)&0x1ff; int X0,Y0,X1,Y1; disp_rect(&X0,&Y0,&X1,&Y1);
    if(x>X1||x+w<=X0||y>Y1||y+h<=Y0) return;
    if(*queued){ hi_sync(); *queued=0; }
    const int S=a2_hi; uint16_t c=pack(b->w[0]&255,(b->w[0]>>8)&255,(b->w[0]>>16)&255);
    for(int j=0;j<h;j++){ int yy=(y+j)&511; for(int sy=0;sy<S;sy++){ uint16_t*row=&hv_alt[(size_t)(yy*S+sy)*1024*S]; for(int i=0;i<w;i++){ uint16_t*d=row+(size_t)((x+i)&1023)*S; for(int q=0;q<S;q++) d[q]=c; } } } }
static void replay_enqueue(double t){
    int s_dx0=dx0,s_dy0=dy0,s_dx1=dx1,s_dy1=dy1,s_ox=offx,s_oy=offy,s_twx=twx,s_twy=twy,s_tww=tww,s_twh=twh,s_tpx=tpx,s_tpy=tpy,s_tm=tmode,s_se=semi,s_sm=setmask,s_cm=chkmask; uint32_t s_stat=stat; unsigned long g0=hv_gen;
    int X0,Y0,X1,Y1; disp_rect(&X0,&Y0,&X1,&Y1); int queued=0;
    for(int i=0;i<rlb.n;i++){ const Rc*b=&rlb.a[i]; uint32_t op=b->w[0]>>24;
        offx=b->ox; offy=b->oy; dx0=b->ax; dy0=b->ay; dx1=b->bx; dy1=b->by; settp(b->tp); tww=b->tw&31; twh=(b->tw>>5)&31; twx=(b->tw>>10)&31; twy=(b->tw>>15)&31; setmask=b->mk&1; chkmask=(b->mk>>1)&1;
        if(op==0x02){ rep_fill(b,&queued); continue; }
        if(dx0>X1||dx1<X0||dy0>Y1||dy1<Y0) continue;
        if(op>=0x20&&op<=0x3f){ const Rc*a=(b->g3&&mtch[i]>=0)?&rla.a[mtch[i]]:0; rep_poly(b,a,t); queued=1; }
        else if(op>=0x60&&op<=0x7f){ rep_rect(b); queued=1; } }
    dx0=s_dx0;dy0=s_dy0;dx1=s_dx1;dy1=s_dy1;offx=s_ox;offy=s_oy;twx=s_twx;twy=s_twy;tww=s_tww;twh=s_twh;tpx=s_tpx;tpy=s_tpy;tmode=s_tm;semi=s_se;setmask=s_sm;chkmask=s_cm; stat=s_stat; hv_gen=g0; }

static int out_S(void){ return a2_hi>1?a2_hi:2*a2_out_k; }
static uint16_t* live_fb(void){ return a2_hi>1?hv:vram; }
static int conv_prepare(CvSlot*c,uint16_t*fb,int px,int py){
    const int S=a2_hi, OS=out_S(), RR=OS/S, W=320*OS, H=240*OS; if(!cv_alloc(c,W,H)) return 0; if(!cv_lut_ok){ cv_lut_ok=1; for(int p=0;p<32768;p++) cv_lut[p]=0xff000000u|(((p&31)<<3)<<16)|((((p>>5)&31)<<3)<<8)|(((p>>10)&31)<<3); }
    int div=(dispmode&0x40)?7:((int[]){10,8,5,4})[dispmode&3]; int x1=hrange&0xfff, x2=(hrange>>12)&0xfff, y1=vrange&0x3ff, y2=(vrange>>10)&0x3ff;
    c->W=W; c->H=H; c->S=S; c->OS=OS; c->R=RR; c->y1=y1; c->y2=y2; c->dispy=py; c->fb=fb;
    if(S==1){ const int k=OS/2; for(int ox=0;ox<W;ox++){ int cyc=0x260+(ox/k)*4; c->xm[ox]=(cyc<x1||cyc>=x2)?-1:(px+(cyc-x1)/div)&1023; } return 1; }
    for(int ox=0;ox<W;ox++){ int cn=(0x260-x1)*OS+ox*8, cm=(0x260-x2)*OS+ox*8; c->xm[ox]=(cn<0||cm>=0)?-1:((px*OS+cn/div)/RR)%(1024*S); } return 1; }
static void conv_run(int slot){ if(hK){ HJ loc; HJ*J=hq_slot(&loc); J->kind=2; J->mode=slot; J->tpx=J->tpy=0; hq_note(J,dispx,dispy,dispx+639,dispy+479,0); hq_head++; long seq=hq_head; hq_publish(); hi_wait_seq(seq); } else hconv(slot,0,1); }

int gpu_render_interp(double t,uint32_t**out,int*Wp,int*Hp){
    if(!a2_interp_on||!interp_mode_ok()) return 0; const int S=a2_hi, OS=out_S(); *Wp=320*OS; *Hp=240*OS;
    if(t<1.0&&build_match()){
        if(!hv_alt){ hv_alt=(uint16_t*)calloc((size_t)1024*S*512*S,2); if(!hv_alt) return 0; }
        CvSlot*c=&cvs[2]; if(!conv_prepare(c,hv_alt,dispx,dispy)) return 0;
        replay_enqueue(t); conv_run(2); *out=c->buf; return 1; }
    { static int key[8]; static unsigned long gen; static int valid; const unsigned long cg=S>1?hv_gen:vgen; int nk[8]={dispx,dispy,dispmode,(int)hrange,(int)vrange,S*16+OS,0,0};
      CvSlot*c=&cvs[3]; if(valid&&gen==cg&&!memcmp(key,nk,sizeof key)){ *out=c->buf; return 2; }
      if(S>1) hi_sync_region(dispx,dispy,dispx+639,dispy+479); if(!conv_prepare(c,live_fb(),dispx,dispy)) return 0; conv_run(3); memcpy(key,nk,sizeof key); gen=cg; valid=1; *out=c->buf;
      if(S==1&&getenv("A2NATCHK")){ static uint32_t ref[640*480]; static long nchk,nbad; gpu_render_display(ref,640,480); const int k=OS/2; long bad=0; for(int y=0;y<480*k;y++) for(int x=0;x<640*k;x++) if(c->buf[(size_t)y*c->W+x]!=ref[(y/k)*640+x/k]) bad++; nchk++; if(bad) nbad++; if(bad&&nbad<4){ int x0=9999,x1=-1,y0=9999,y1=-1; for(int y=0;y<480*k;y++) for(int x=0;x<640*k;x++) if(c->buf[(size_t)y*c->W+x]!=ref[(y/k)*640+x/k]){ if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y; } fprintf(stderr,"NATBAD box x %d..%d y %d..%d dispmode %x hr %x vr %x dispx %d dispy %d\n",x0,x1,y0,y1,dispmode,(unsigned)hrange,(unsigned)vrange,dispx,dispy); } if(!(nchk%200)||bad) fprintf(stderr,"NATCHK %ld pictures checked, %ld differ (this one %ld px, k=%d)\n",nchk,nbad,bad,k); }
      return 1; } }

static void interp_eval(int opx,int opy){
    static uint32_t *tr_,*lv_,*md_,*pv_; static size_t cap; static double e_mid,e_next,e_prev; static long ne, nskip;
    if(!interp_mode_ok()||opx<0||rla.bad||rlb.bad||rlc.bad||rla.n<4||rlc.n<4){ nskip++; return; }
    const int S=a2_hi, W=320*out_S(), H=240*out_S(); size_t n=(size_t)W*H; if(cap<n){ cap=n; tr_=(uint32_t*)realloc(tr_,n*4); lv_=(uint32_t*)realloc(lv_,n*4); md_=(uint32_t*)realloc(md_,n*4); pv_=(uint32_t*)realloc(pv_,n*4); }
    hi_sync(); RL sb=rlb; rlb=rlc; mtch_key=-1;
    int ok=build_match(); if(!ok){ rlb=sb; mtch_key=-1; nskip++; return; }
    if(!hv_alt) hv_alt=(uint16_t*)calloc((size_t)1024*S*512*S,2); if(!hv_alt){ rlb=sb; mtch_key=-1; return; }
    CvSlot*c=&cvs[3]; conv_prepare(c,live_fb(),opx,opy); hconv(3,0,1); memcpy(tr_,c->buf,n*4);
    conv_prepare(c,live_fb(),dispx,dispy); hconv(3,0,1); memcpy(lv_,c->buf,n*4);
    c=&cvs[2]; conv_prepare(c,hv_alt,dispx,dispy); replay_enqueue(0.5); conv_run(2); memcpy(md_,c->buf,n*4);
    replay_enqueue(0.0); conv_run(2); memcpy(pv_,c->buf,n*4);
    static uint32_t*r1_; static size_t r1cap; if(r1cap<n){ r1cap=n; r1_=(uint32_t*)realloc(r1_,n*4); } replay_enqueue(1.0); conv_run(2); memcpy(r1_,c->buf,n*4); double e1=0; for(size_t i=0;i<n;i+=3) for(int q=0;q<24;q+=8) e1+=abs(((r1_[i]>>q)&255)-((lv_[i]>>q)&255)); e1/=(double)((n+2)/3)*3; static double e1s; static long e1n; e1s+=e1; e1n++;
    double a=0,b=0,d=0; for(size_t i=0;i<n;i+=3) for(int q=0;q<24;q+=8){ int T=(tr_[i]>>q)&255; a+=abs(((md_[i]>>q)&255)-T); b+=abs(((lv_[i]>>q)&255)-T); d+=abs(((pv_[i]>>q)&255)-T); }
    { const char*de=getenv("A2INTERPDUMP"); static long dumped=-1; if(de&&gpu_frame>=atol(de)&&dumped<0){ dumped=gpu_frame; const uint32_t*im[4]={tr_,md_,lv_,pv_}; const char*nm[4]={"truth","mid","newer","older"}; for(int k=0;k<4;k++){ char fn[128]; snprintf(fn,sizeof fn,"/tmp/claude-0/idump_%s.ppm",nm[k]); FILE*f=fopen(fn,"wb"); if(f){ fprintf(f,"P6\n%d %d\n255\n",W,H); for(size_t i=0;i<n;i++){ uint32_t p=im[k][i]; fputc((p>>16)&255,f); fputc((p>>8)&255,f); fputc(p&255,f); } fclose(f); } } { RL sa=rla, sb2=rlb; rla=sb; rlb=rlc; mtch_key=-1; if(build_match()){ for(int i=0;i<rlb.n;i++){ if(mtch[i]<0) continue; const Rc*b=&rlb.a[i]; uint32_t op=b->w[0]>>24; int bx[4],by[4],bn; poly_rel(b,op,bx,by,&bn); const Rc*a=&rla.a[mtch[i]]; int ax_[4],ay_[4],an; poly_rel(a,op,ax_,ay_,&an); int mx=0; for(int q=0;q<bn;q++){ int d=abs(bx[q]-ax_[q])+abs(by[q]-ay_[q]); if(d>mx) mx=d; } if(mx>=6&&bx[0]>140&&bx[0]<185&&by[0]>85&&by[0]<125){ fprintf(stderr,"MV b#%d a#%d op %02x maxdisp %d  B:",i,mtch[i],op,mx); for(int q=0;q<bn;q++) fprintf(stderr," (%d,%d)",bx[q],by[q]); fprintf(stderr,"  A:"); for(int q=0;q<an;q++) fprintf(stderr," (%d,%d)",ax_[q],ay_[q]); fprintf(stderr,"\n"); } }
            CvSlot*c2=&cvs[2]; conv_prepare(c2,hv_alt,dispx,dispy); replay_enqueue(0.5); conv_run(2); FILE*f=fopen("/tmp/claude-0/idump_mid1.ppm","wb"); if(f){ fprintf(f,"P6\n%d %d\n255\n",W,H); for(size_t i=0;i<n;i++){ uint32_t p=c2->buf[i]; fputc((p>>16)&255,f); fputc((p>>8)&255,f); fputc(p&255,f); } fclose(f); } } rla=sa; rlb=sb2; mtch_key=-1; }
            fprintf(stderr,"[interp] dumped frames at F%ld\n",gpu_frame); } }
    long art=0, arttot=0; for(size_t i=0;i<n;i+=3){ int dm=0,dn=0,dp=0; for(int q=0;q<24;q+=8){ int T=(tr_[i]>>q)&255; int x=abs(((md_[i]>>q)&255)-T), y=abs(((lv_[i]>>q)&255)-T), z=abs(((pv_[i]>>q)&255)-T); if(x>dm) dm=x; if(y>dn) dn=y; if(z>dp) dp=z; } arttot++; if(dm>48&&dn<=24&&dp<=24) art++; }
    static long art_s, art_t; art_s+=art; art_t+=arttot; if(atoi(getenv("A2INTERPEVAL"))>1&&art*1000/arttot>=2) fprintf(stderr,"ART F%ld  %ld of %ld sampled pixels wrong only in the interpolated frame (%.2f per mille)\n",gpu_frame,art,arttot,art*1000.0/arttot);
    double nn=(double)((n+2)/3)*3; e_mid+=a/nn; e_next+=b/nn; e_prev+=d/nn; ne++;
    if(atoi(getenv("A2INTERPEVAL"))>1) fprintf(stderr,"EV F%ld polys3D %d matched %d (no same-texture polygon in older frame %d, none close enough %d)  mid %.3f next %.3f prev %.3f  replay(t=1)-vs-live %.4f\n",gpu_frame,m_dr,m_mt,m_nocls,m_nocand,a/nn,b/nn,d/nn,e1);
    if(ne%50==0) fprintf(stderr,"EVAL over %ld triples (skipped %ld): error of interpolated middle %.3f | of showing the newer frame %.3f | of showing the older frame %.3f | interpolation-only wrong pixels %.3f per mille\n",ne,nskip,e_mid/ne,e_next/ne,e_prev/ne,art_t?art_s*1000.0/art_t:0.0);
    rlb=sb; mtch_key=-1; }
