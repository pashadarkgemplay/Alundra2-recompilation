/* Game window, keyboard and controller input, and sound output (SDL2, loaded at run time). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#define DLOPEN(n) ((void*)LoadLibraryA(n))
#define DLSYM(l,n) ((void*)GetProcAddress((HMODULE)(l),n))
#else
#include <dlfcn.h>
#define DLOPEN(n) dlopen(n,RTLD_NOW)
#define DLSYM(l,n) dlsym(l,n)
#endif
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define nanosleep(a,b) Sleep((DWORD)((a)->tv_nsec/1000000L))
#define CLOCK_MONOTONIC 1
#endif
extern void gpu_render_display(uint32_t*,int,int); extern int spu_pull(int16_t*,int);
typedef struct { int freq; uint16_t format; uint8_t channels, silence; uint16_t samples, padding; uint32_t size; void(*cb)(void*,uint8_t*,int); void*ud; } A2AudioSpec;
static void audio_cb(void*u,uint8_t*s,int len){ spu_pull((int16_t*)s,len/4); }
static void *lib, *win, *ren, *tex; static const uint8_t*(*getkeys)(int*); static int(*pollev)(void*); static int(*updtex)(void*,const void*,const void*,int);
static void*pad_; static int(*numjoy)(void); static int(*isgc)(int); static void*(*gcopen)(int); static void(*gcclose)(void*); static uint8_t(*gcbtn)(void*,int); static int16_t(*gcaxis)(void*,int);
static int(*gcrumble)(void*,uint16_t,uint16_t,uint32_t); static int(*initsub)(uint32_t); static int(*gcattached)(void*); static const char*(*gcname)(void*);
extern void sio_set_sticks(uint8_t,uint8_t,uint8_t,uint8_t); extern int sio_pad_analog(void); extern void sio_get_motors(int*,int*);
static int sharpk=1, hiS=1; static uint32_t*bigpix, *hibuf; static int(*rclear)(void*); static int(*rcopy)(void*,void*,const void*,const void*); static void(*rpresent)(void*); static uint32_t pix[640*480]; static int quit; static struct timespec last;
static void*tgt; static int(*settgt)(void*,void*);
static int fps_show; static void*ovtex; static int fs_on, fps_limit=60; static int(*setfs)(void*,uint32_t); static uint64_t(*perfc)(void), (*perff)(void); static void(*sdldelay)(uint32_t); static double next_t;

static int a2_have_audio;
static void pace(void){
  if(fps_limit<=0||!perfc||!perff){ if(!perfc||!perff){ struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now); long us=(now.tv_sec-last.tv_sec)*1000000L+(now.tv_nsec-last.tv_nsec)/1000; long per=1000000L/(fps_limit>0?fps_limit:60); if(fps_limit>0&&us<per){ struct timespec s={0,(per-us)*1000L}; nanosleep(&s,0); } clock_gettime(CLOCK_MONOTONIC,&last); } next_t=0; return; }
  double f=(double)perff(), now=(double)perfc()/f; extern int a2_pace_div; extern double a2_pace_rate; double per=a2_pace_rate>0?1.0/a2_pace_rate:1.0/(fps_limit*(a2_pace_div>0?a2_pace_div:1));
  {
    extern int spu_fill(void), spu_started(void); static double fe=-1; if(a2_have_audio&&spu_started()){ double fl=spu_fill(); if(fe<0) fe=fl; fe+=(fl-fe)*0.05; double ex=(fe-2000.0)/44100.0; double adj=ex*0.5; if(adj>0.03) adj=0.03; if(adj<-0.03) adj=-0.03; per*=1.0+adj; } }
  if(next_t==0||now>next_t+0.25) next_t=now;
  next_t+=per;
  for(;;){ now=(double)perfc()/f; double rem=next_t-now; if(rem<=0) break; if(rem>0.0022&&sdldelay) sdldelay((uint32_t)((rem-0.0012)*1000.0)); }
}

long a2_vbl_now;
extern double a2_pace_rate;
double a2_win_lag(void){ if(!perfc||!perff||next_t==0||a2_pace_rate<=0) return 0; double f=(double)perff(), now=(double)perfc()/f; double lag=now-(next_t+1.0/a2_pace_rate); return lag>0?lag:0; }
void a2_win_skip(void){ if(next_t>0&&a2_pace_rate>0) next_t+=1.0/a2_pace_rate; }
int a2_win_init(void){
  lib=DLOPEN("SDL2.dll"); if(!lib) lib=DLOPEN("libSDL2-2.0.so.0"); if(!lib) lib=DLOPEN("libSDL2.so"); if(!lib) lib=DLOPEN("libSDL2-2.0.0.dylib"); if(!lib){ fprintf(stderr,"a2_win: libSDL2 not found\n"); return 0; }
#define L(n) *(void**)&n##_=DLSYM(lib,"SDL_" #n)
  int(*Init_)(uint32_t); void*(*CreateWindow_)(const char*,int,int,int,int,uint32_t); void*(*CreateRenderer_)(void*,int,uint32_t); void*(*CreateTexture_)(void*,uint32_t,int,int,int);
  L(Init); L(CreateWindow); L(CreateRenderer); L(CreateTexture); *(void**)&updtex=DLSYM(lib,"SDL_UpdateTexture"); *(void**)&rclear=DLSYM(lib,"SDL_RenderClear"); *(void**)&rcopy=DLSYM(lib,"SDL_RenderCopy");
  *(void**)&rpresent=DLSYM(lib,"SDL_RenderPresent"); *(void**)&pollev=DLSYM(lib,"SDL_PollEvent"); *(void**)&getkeys=DLSYM(lib,"SDL_GetKeyboardState");
  if(!Init_||!CreateWindow_||!CreateRenderer_||!CreateTexture_||!updtex||!rcopy||!rpresent||!pollev||!getkeys){ fprintf(stderr,"a2_win: missing SDL symbols\n"); return 0; }
  *(void**)&numjoy=DLSYM(lib,"SDL_NumJoysticks"); *(void**)&isgc=DLSYM(lib,"SDL_IsGameController"); *(void**)&gcopen=DLSYM(lib,"SDL_GameControllerOpen"); *(void**)&gcclose=DLSYM(lib,"SDL_GameControllerClose");
  *(void**)&gcbtn=DLSYM(lib,"SDL_GameControllerGetButton"); *(void**)&gcaxis=DLSYM(lib,"SDL_GameControllerGetAxis"); *(void**)&gcrumble=DLSYM(lib,"SDL_GameControllerRumble");
  *(void**)&initsub=DLSYM(lib,"SDL_InitSubSystem"); *(void**)&gcattached=DLSYM(lib,"SDL_GameControllerGetAttached"); *(void**)&gcname=DLSYM(lib,"SDL_GameControllerName");
#ifdef _WIN32
  { void*mm=DLOPEN("winmm.dll"); if(mm){ unsigned(__stdcall*tbp)(unsigned)=(unsigned(__stdcall*)(unsigned))DLSYM(mm,"timeBeginPeriod"); if(tbp) tbp(1); } }
#endif
  if(Init_(0x20)!=0){ fprintf(stderr,"a2_win: SDL_Init failed\n"); return 0; }
  { const char*e; int ww=960,wh=720; if((e=getenv("A2WINW"))&&atoi(e)>=320) ww=atoi(e); if((e=getenv("A2WINH"))&&atoi(e)>=240) wh=atoi(e);
    int(*sethint)(const char*,const char*)=(void*)DLSYM(lib,"SDL_SetHint"); if(sethint) sethint("SDL_RENDER_SCALE_QUALITY",(getenv("A2LINEAR")&&atoi(getenv("A2LINEAR")))?"1":"0");
    fs_on=(e=getenv("A2FULLSCREEN"))&&atoi(e); *(void**)&setfs=DLSYM(lib,"SDL_SetWindowFullscreen");
    win=CreateWindow_("Alundra 2 recomp",0x2FFF0000,0x2FFF0000,ww,wh,0x24|(fs_on?0x1001:0)|((getenv("A2BORDERLESS")&&atoi(getenv("A2BORDERLESS")))?0x10:0)); if(!win) return 0; ren=CreateRenderer_(win,-1,0); if(!ren) return 0;
    int(*logsz)(void*,int,int)=(void*)DLSYM(lib,"SDL_RenderSetLogicalSize"); if(logsz){ int lw=640; const char*we=getenv("A2WIDE"); if(we&&*we){ int p,q; double ar=(sscanf(we,"%d:%d",&p,&q)==2&&q>0)?(double)p/q:atof(we); if(ar>1.34&&ar<8.0) lw=(int)(480*ar+0.5); } logsz(ren,lw,480); }
    int(*showc)(int)=(void*)DLSYM(lib,"SDL_ShowCursor"); if(showc) showc(fs_on?0:1);
    *(void**)&perfc=DLSYM(lib,"SDL_GetPerformanceCounter"); *(void**)&perff=DLSYM(lib,"SDL_GetPerformanceFrequency"); *(void**)&sdldelay=DLSYM(lib,"SDL_Delay");
    if((e=getenv("A2FPSLIMIT"))) fps_limit=atoi(e); fprintf(stderr,"a2_win: window %dx%d%s, %s scaling, frame limit %d\n",ww,wh,fs_on?" fullscreen":"",(getenv("A2LINEAR")&&atoi(getenv("A2LINEAR")))?"smooth":"sharp",fps_limit); }
  if(initsub) initsub(0x2000);
  if(!getenv("A2MUTE")&&initsub&&initsub(0x10)==0){ uint32_t(*openad)(const char*,int,const A2AudioSpec*,A2AudioSpec*,int)=(void*)DLSYM(lib,"SDL_OpenAudioDevice"); void(*pausead)(uint32_t,int)=(void*)DLSYM(lib,"SDL_PauseAudioDevice");
    if(openad&&pausead){ A2AudioSpec want,have; memset(&want,0,sizeof want); want.freq=44100; want.format=0x8010; want.channels=2; want.samples=1024; want.cb=audio_cb; uint32_t dev=openad(0,0,&want,&have,0); if(dev){ a2_have_audio=1; pausead(dev,0); fprintf(stderr,"a2_win: audio on (%d Hz, %d ch)\n",have.freq,have.channels); } else fprintf(stderr,"a2_win: no audio device\n"); } }
  { const char*e=getenv("A2SHARP"); sharpk=e?atoi(e):1; if(sharpk<1) sharpk=1; if(sharpk>4) sharpk=4; if(sharpk>1) bigpix=malloc((size_t)640*480*sharpk*sharpk*4); if(!bigpix) sharpk=1; }
  { const char*e=getenv("A2HIRES"); hiS=e?atoi(e):1; if(hiS<1) hiS=1; if(hiS>4) hiS=4; if(hiS>1){ hibuf=malloc((size_t)320*hiS*240*hiS*4); if(!hibuf) hiS=1; else sharpk=1; } }
  if(hiS==1&&sharpk>1&&!getenv("A2CPUSHARP")){
    int(*sethint)(const char*,const char*)=(void*)DLSYM(lib,"SDL_SetHint"); *(void**)&settgt=DLSYM(lib,"SDL_SetRenderTarget"); void(*dtex)(void*)=(void*)DLSYM(lib,"SDL_DestroyTexture");
    if(sethint&&settgt&&dtex){ sethint("SDL_RENDER_SCALE_QUALITY","0"); void*t0=CreateTexture_(ren,0x16362004u,1,640,480); sethint("SDL_RENDER_SCALE_QUALITY","1"); void*t1=t0?CreateTexture_(ren,0x16362004u,2,640*sharpk,480*sharpk):0;
      sethint("SDL_RENDER_SCALE_QUALITY",(getenv("A2LINEAR")&&atoi(getenv("A2LINEAR")))?"1":"0");
      if(t0&&t1&&settgt(ren,t1)==0&&settgt(ren,0)==0){ tex=t0; tgt=t1; free(bigpix); bigpix=0; fprintf(stderr,"a2_win: sharp scaling x%d on the video card\n",sharpk); }
      else { if(t1) dtex(t1); if(t0) dtex(t0); settgt=0; } } else settgt=0; }
  { extern void gpu_set_out_scale(int); gpu_set_out_scale(tgt?1:sharpk); }
  if(!tex) tex=hiS>1?CreateTexture_(ren,0x16362004u,1,320*hiS,240*hiS):CreateTexture_(ren,0x16362004u,1,640*sharpk,480*sharpk); if(!tex) return 0; clock_gettime(CLOCK_MONOTONIC,&last); if((fps_show=getenv("A2FPSSHOW")&&atoi(getenv("A2FPSSHOW")))) ovtex=CreateTexture_(ren,0x16362004u,1,44,10); return 1; }

static int ov_g=-1, ov_e=-1; static uint32_t ovbuf[44*10]; extern long a2_flips;
static const unsigned char GL[][7]={ {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,2,12},
  {31,16,16,30,16,16,16},{30,17,17,30,16,16,16},{15,16,16,14,1,1,30},{31,16,16,30,16,16,31},{17,27,21,21,17,17,17},{17,17,17,17,17,17,14} };
static void ov_char(int x,int y,int g){ for(int r=0;r<7;r++) for(int c=0;c<5;c++) if(GL[g][r]&(16>>c)) ovbuf[(y+r)*44+x+c]=0xff40ff60u; }
static void ov_line(int y,const int*gl,int n,int val){ int x=2; for(int i=0;i<n;i++){ ov_char(x,y,gl[i]); x+=6; } x+=3; char b[8]; int l=snprintf(b,8,"%d",val>999?999:val); for(int i=0;i<l;i++){ ov_char(x,y,b[i]-'0'); x+=6; } }
static void ov_update(int g){ if(!ovtex||g==ov_g) return; ov_g=g; for(int i=0;i<44*10;i++) ovbuf[i]=0xff000000u;
  static const int F[]={10,11,12}; ov_line(1,F,3,g); updtex(ovtex,0,ovbuf,44*4); }
extern long a2_pres; extern int a2_interp_on;
static void ov_tick(void){ static struct timespec t0; static long f0; static int init; struct timespec nw; clock_gettime(CLOCK_MONOTONIC,&nw); long cnt=a2_interp_on?a2_pres:a2_flips; if(!init){ init=1; t0=nw; f0=cnt; }
  double dt=(nw.tv_sec-t0.tv_sec)+(nw.tv_nsec-t0.tv_nsec)/1e9; if(dt>=0.5){ ov_update((int)((cnt-f0)/dt+0.5)); t0=nw; f0=cnt; } }
int a2_hold_present, a2_pace_div=1; double a2_pace_rate; long a2_pres; static int a2_hold_present_ext(void){ return a2_hold_present; }
static double ph[5]; static double nowsec(void){ struct timespec q; clock_gettime(CLOCK_MONOTONIC,&q); return q.tv_sec+q.tv_nsec/1e9; }
uint16_t a2_win_frame(int dx,int dy){
  static double tprev; double ta=nowsec(); if(tprev>0) ph[0]+=ta-tprev;
  uint8_t ev[64]; while(pollev(ev)) if(*(uint32_t*)ev==0x100) quit=1;
  int natdone=0;
  if(hiS==1){ extern int a2_nat_interp, a2_interp_on; extern double a2_interp_t; extern int gpu_render_interp(double,uint32_t**,int*,int*); if(a2_nat_interp&&a2_interp_on){ uint32_t*ip=0; int hw,hh; int ir=gpu_render_interp(a2_interp_t,&ip,&hw,&hh); a2_pres++; if(ir){ if(ir==1) updtex(tex,0,ip,hw*4); natdone=1; } } }
  if(natdone){ }
  else if(hiS>1){ extern int gpu_render_display_hi(uint32_t**,int*,int*); extern int gpu_render_interp(double,uint32_t**,int*,int*); extern int a2_interp_on; extern double a2_interp_t; int hw,hh; int ir=0; if(a2_interp_on){ uint32_t*ip=0; ir=gpu_render_interp(a2_interp_t,&ip,&hw,&hh); if(ir==1) updtex(tex,0,ip,hw*4); } a2_pres++; if(!ir&&!a2_hold_present_ext()){ uint32_t*hp=hibuf; int rr=gpu_render_display_hi(&hp,&hw,&hh); if(rr){ if(rr==1) updtex(tex,0,hp,hw*4); } else { gpu_render_display(pix,640,480); const int TW=320*hiS, TH=240*hiS; for(int y=0;y<TH;y++){ const uint32_t*sr=pix+(size_t)(y*480/TH)*640; uint32_t*d=hibuf+(size_t)y*TW; for(int x=0;x<TW;x++) d[x]=sr[x*640/TW]; } updtex(tex,0,hibuf,TW*4); } } }
  else  { extern int a2_hold_present; if(!a2_hold_present){ { extern void gpu_render_display_i(uint32_t*,int,int); gpu_render_display_i(pix,640,480); }
    if(sharpk>1&&!tgt){ const int K=sharpk, BW=640*K; for(int y=0;y<480;y++){ uint32_t*d=bigpix+(size_t)y*K*BW; for(int x=0;x<640;x++){ uint32_t c=pix[y*640+x]; for(int q=0;q<K;q++) d[x*K+q]=c; } for(int r=1;r<K;r++) memcpy(d+(size_t)r*BW,d,BW*4); } updtex(tex,0,bigpix,BW*4); } else updtex(tex,0,pix,640*4); } } double tb=nowsec(); ph[1]+=tb-ta; if(tgt){ settgt(ren,tgt); rcopy(ren,tex,0,0); settgt(ren,0); } if(rclear) rclear(ren); rcopy(ren,tgt?tgt:tex,0,0); if(fps_show&&ovtex){ ov_tick(); int rc[4]={6,6,88,20}; rcopy(ren,ovtex,0,rc); } { static int shot=-1, nsh; if(shot<0){ const char*e=getenv("A2SHOT"); shot=e?atoi(e):0; } if(shot&&++nsh==shot){ int(*rrp)(void*,const void*,uint32_t,void*,int)=(void*)DLSYM(lib,"SDL_RenderReadPixels"); int(*gos)(void*,int*,int*)=(void*)DLSYM(lib,"SDL_GetRendererOutputSize"); int ow=0,oh=0; if(rrp&&gos&&gos(ren,&ow,&oh)==0){ uint32_t*px=malloc((size_t)ow*oh*4); if(px&&rrp(ren,0,0x16362004u,px,ow*4)==0){ FILE*f=fopen(getenv("A2SHOTF")?getenv("A2SHOTF"):"/tmp/claude-0/shot.ppm","wb"); if(f){ fprintf(f,"P6\n%d %d\n255\n",ow,oh); for(int i=0;i<ow*oh;i++){ fputc((px[i]>>16)&255,f); fputc((px[i]>>8)&255,f); fputc(px[i]&255,f); } fclose(f); } } free(px); } } }
  rpresent(ren); double tc=nowsec(); ph[2]+=tc-tb;
  { extern int a2_audio_ok; a2_audio_ok=1; }
  pace(); tprev=nowsec(); ph[3]+=tprev-tc;
  {
    extern int gpu_perf_info(int*,int*,int*);
    static struct timespec pt; static int pn, nlog; static double worst, tw0, tstart; struct timespec nw; clock_gettime(CLOCK_MONOTONIC,&nw); double t=nw.tv_sec+nw.tv_nsec/1e9;
    if(!tstart) tstart=t;
    if(pt.tv_sec||pt.tv_nsec){ double d=t-(pt.tv_sec+pt.tv_nsec/1e9); if(d>worst) worst=d; } pt=nw; if(tw0==0) tw0=t; pn++;
    if(t-tw0>=2.0){ double rate=pn/(t-tw0); int S,K,F; gpu_perf_info(&S,&K,&F);
      if(nlog<3000){ extern void a2_note(const char*,...); char lfb[700]; { extern double hs_ns; static double hs0; double n=pn>0?pn:1; static long pres0; double prate=(a2_pres-pres0)/(t-tw0); pres0=a2_pres; extern long a2_vbl_now; static long vb0; double grate=(a2_vbl_now-vb0)/(t-tw0); vb0=a2_vbl_now; snprintf(lfb,sizeof lfb,"t=%.0fs  %.1f pictures/s, %.1f game ticks/s (limit %d)  worst frame %.0f ms  internal %dx  threads %d  texfilter %d  | ms per frame: game %.1f, picture %.1f, present %.1f, sleep %.1f, gpu-wait %.1f\n",t-tstart,prate,grate,fps_limit,worst*1000.0,S,K,F,ph[0]*1000/n,ph[1]*1000/n,ph[2]*1000/n,ph[3]*1000/n,(hs_ns-hs0)/1e6/n); hs0=hs_ns; for(int i=0;i<5;i++) ph[i]=0; } a2_note("%s",lfb); nlog++; }
      pn=0; tw0=t; worst=0; } }
  const uint8_t*k=getkeys(0); uint16_t m=0;

  static const struct{int sc,b;} t[]={{82,4},{26,4},{79,5},{7,5},{81,6},{22,6},{80,7},{4,7},
      {14,14},{44,14},{13,15},{15,13},{12,12},{20,10},{8,11},{43,8},{225,9},{229,9},{40,3},{41,3},{42,0}};
  for(unsigned i=0;i<sizeof t/sizeof*t;i++) if(k[t[i].sc]) m|=1u<<t[i].b;
  if(k[67]) quit=1;
  { static int p11,p12; if(k[68]&&!p11&&setfs){ fs_on^=1; setfs(win,fs_on?0x1001:0); } p11=k[68]; (void)p12; }
  { extern void gpu_plog_start(void); extern int a2_norej,a2_nofog; static int p8,p9,p7; if(k[65]&&!p8) gpu_plog_start(); p8=k[65]; if(k[66]&&!p9){ a2_norej^=1; fprintf(stderr,"[a2pc] F9: big-triangle rejection %s\n",a2_norej?"OFF":"ON"); } p9=k[66]; if(k[64]&&!p7){ a2_nofog^=1; fprintf(stderr,"[a2pc] F7: depth fog %s\n",a2_nofog?"OFF":"ON"); } p7=k[64]; { extern int a2_force_l2; static int p5; if(k[62]&&!p5){ a2_force_l2^=1; fprintf(stderr,"[a2pc] F5: force the 2nd map layer (far background) %s\n",a2_force_l2?"ON":"OFF"); } p5=k[62]; } { extern int a2_nocull; static int p6; if(k[63]&&!p6){ a2_nocull=(a2_nocull+1)&3; fprintf(stderr,"[a2pc] F6: culling test level %d (0=normal, 1=no off-screen polygon reject, 2=+no back-face reject, 3=+no object box cull)\n",a2_nocull); } p6=k[63]; } }

  static int jtry; uint8_t lx=0x80,ly=0x80,rx=0x80,ry=0x80;
  if(!pad_&&numjoy&&gcopen&&(jtry++%60)==0){ int n=numjoy(); for(int q=0;q<n;q++) if(isgc&&isgc(q)){ pad_=gcopen(q); if(pad_){ fprintf(stderr,"a2_win: controller connected: %s\n",gcname?gcname(pad_):"?"); break; } } }
  if(pad_&&gcattached&&!gcattached(pad_)){ if(gcclose) gcclose(pad_); pad_=0; fprintf(stderr,"a2_win: controller disconnected\n"); }
  if(pad_){
    static const struct{int sb,b;} g[]={{0,14},{1,13},{2,15},{3,12},{4,0},{6,3},{7,1},{8,2},{9,10},{10,11},{11,4},{12,6},{13,7},{14,5}};
    for(unsigned i=0;i<sizeof g/sizeof*g;i++) if(gcbtn(pad_,g[i].sb)) m|=1u<<g[i].b;
    int a0=gcaxis(pad_,0),a1=gcaxis(pad_,1),a2=gcaxis(pad_,2),a3=gcaxis(pad_,3),a4=gcaxis(pad_,4),a5=gcaxis(pad_,5);
#define DZ(v) (((v)>-7000&&(v)<7000)?0:(v))
    a0=DZ(a0); a1=DZ(a1); a2=DZ(a2); a3=DZ(a3);
    lx=(uint8_t)((a0+32768)>>8); ly=(uint8_t)((a1+32768)>>8); rx=(uint8_t)((a2+32768)>>8); ry=(uint8_t)((a3+32768)>>8);
    if(a4>12000) m|=1u<<8; if(a5>12000) m|=1u<<9;
    if(a2<-16000) m|=1u<<10; if(a2>16000) m|=1u<<11;
    if(!sio_pad_analog()){ if(a0<-16000) m|=1u<<7; if(a0>16000) m|=1u<<5; if(a1<-16000) m|=1u<<4; if(a1>16000) m|=1u<<6; }

    { int sm=0,lg=0; sio_get_motors(&sm,&lg); static int ls=-1,ll=-1,rt; if(gcrumble&&(sm!=ls||lg!=ll||(++rt%20)==0)){ gcrumble(pad_,(uint16_t)(lg*257),(uint16_t)(sm?0xFFFF:0),200); ls=sm; ll=lg; } }
  }
  sio_set_sticks(rx,ry,lx,ly);
  return m; }
int a2_win_quit(void){ return quit; }
