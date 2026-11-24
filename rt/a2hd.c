/* HD texture packs: recognises the pictures the game sends to video memory by their rows,
   decodes their palette-index HD maps and tells the renderer where to put them. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a2hd.h"
#include "a2hdcodec.h"

#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION mx; static CONDITION_VARIABLE cv;
static void lk(void){ EnterCriticalSection(&mx); } static void ulk(void){ LeaveCriticalSection(&mx); }
static void wt(void){ SleepConditionVariableCS(&cv,&mx,100); } static void wk(void){ WakeAllConditionVariable(&cv); }
static void minit(void){ InitializeCriticalSection(&mx); InitializeConditionVariable(&cv); }
static DWORD WINAPI tmain_(LPVOID p);
static void spawn(int i){ HANDLE h=CreateThread(0,0,tmain_,(LPVOID)(intptr_t)i,0,0); if(h) CloseHandle(h); }
#else
#include <pthread.h>
static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static void lk(void){ pthread_mutex_lock(&mx); } static void ulk(void){ pthread_mutex_unlock(&mx); }
static void wt(void){ pthread_cond_wait(&cv,&mx); } static void wk(void){ pthread_cond_broadcast(&cv); }
static void minit(void){}
static void *tmain_(void*p);
static void spawn(int i){ pthread_t t; if(!pthread_create(&t,0,tmain_,(void*)(intptr_t)i)) pthread_detach(t); }
#endif

typedef struct { char name[40]; int bpp, W, H, part, pack, local, ncol, embed, haslr; uint32_t off, size; uint64_t *rows;
    uint8_t *lr, *got; int ngot, state, partial, dirty; long lastfr; uint8_t *hd; long used; } Ent;          /* state: 0 idle, 1 queued, 2 decoding, 3 ready */
typedef struct { int e, vx, vy, key, u0, v0, cw, ch, hw, c0, live; uint8_t got[512]; } Place;
typedef struct { uint64_t h; uint32_t e, r; } RowRef;
typedef struct { uint64_t h; uint32_t e; uint16_t r, off; } SegRef;
static SegRef *segs; static int nsegs;

static char*usef; static void use_write(void);
int hd_on; uint8_t hd_blk[32][64]; long hd_nplace, hd_ndec, hd_ncopy, hd_nglyph;
static int K, KR, npack; static char dir[512]; static FILE *parts[2][16][4];
static Ent *ent; static int nent; static RowRef *tab; static int ntab;
#define NPL 1024
static Place pl[NPL]; static RowRef *cells; static int ncells; static uint16_t own[512][1024];
static HdOp *ops; static int nops, capops;
static int jobs[4096], njobs, nthr; static long tick; static size_t cache_bytes, cache_max;
static int *ready; static int nready, capready;

static void qop(int type,int key,int u0,int v0,int w,int h,const uint8_t*src,int stride){
    if(nops>=capops){ int nc=capops?capops*2:1024; HdOp*n=(HdOp*)realloc(ops,sizeof(HdOp)*nc); if(!n) return; ops=n; capops=nc; } HdOp*o=&ops[nops++]; o->type=type; o->key=key; o->u0=u0; o->v0=v0; o->w=w; o->h=h; o->src=src; o->stride=stride; }

static int load_pack(const char*kind,int pk,int show){
    char fn[600]; snprintf(fn,sizeof fn,"%s/%s.a2hd",dir,kind); FILE*f=fopen(fn,"rb"); if(!f) return 0;
    uint8_t h[24]; if(fread(h,1,24,f)!=24||memcmp(h,"A2HD",4)){ fclose(f); return 0; }
    uint32_t v[5]; memcpy(v,h+4,20); int kk=(int)v[1], ne=(int)v[2], nr=(int)v[3], np=(int)v[4];
    if(v[0]<1||v[0]>2||(K&&kk!=K)||np>16){ fclose(f); return 0; } K=kk;
    long here=ftell(f); fseek(f,0,SEEK_END); size_t bl=(size_t)(ftell(f)-here); fseek(f,here,SEEK_SET);
    if(bl<(size_t)ne*60+(size_t)nr*8){ fclose(f); return 0; }
    uint8_t*b=(uint8_t*)malloc(bl); if(!b||fread(b,1,bl,f)!=bl){ free(b); fclose(f); return 0; } fclose(f);
    hc_crypt(b,bl,HC_KEY,0x5EED);
    uint64_t*rows=(uint64_t*)malloc((size_t)nr*8); memcpy(rows,b+(size_t)ne*60,(size_t)nr*8);
    ent=(Ent*)realloc(ent,sizeof(Ent)*(nent+ne));
    for(int i=0;i<ne;i++){ const uint8_t*q=b+(size_t)i*60; Ent*e=&ent[nent+i]; memset(e,0,sizeof *e);
        memcpy(e->name,q,39); e->bpp=q[40]; e->part=q[41]; e->W=q[42]|(q[43]<<8); e->H=q[44]|(q[45]<<8);
        { int fl=q[46]|(q[47]<<8); e->embed=(fl>>8)&1; e->haslr=(fl>>9)&1; e->ncol=fl&255; }
        uint32_t rs,off,sz; memcpy(&rs,q+48,4); memcpy(&off,q+52,4); memcpy(&sz,q+56,4);
        e->rows=rows+rs; e->off=off; e->size=show?sz:0; e->pack=pk; e->local=i; }
    for(int p=0;p<np;p++) for(int t=0;t<4;t++){ snprintf(fn,sizeof fn,"%s/%s.a2hd.%03d",dir,kind,p); parts[pk][p][t]=fopen(fn,"rb"); }
    { size_t o=(size_t)ne*60+(size_t)nr*8; uint32_t ns=0; if(v[0]>=2&&o+4<=bl){ memcpy(&ns,b+o,4); o+=4; }
      if(ns&&o+(size_t)ns*16<=bl){ segs=(SegRef*)realloc(segs,sizeof(SegRef)*(nsegs+ns));
        for(uint32_t i=0;i<ns;i++){ const uint8_t*s=b+o+(size_t)i*16; SegRef*g=&segs[nsegs++]; memcpy(&g->h,s,8); uint32_t e; memcpy(&e,s+8,4); g->e=e+(uint32_t)nent; memcpy(&g->r,s+12,2); memcpy(&g->off,s+14,2); } } }
    free(b);
    int base=nent; nent+=ne; tab=(RowRef*)realloc(tab,sizeof(RowRef)*(ntab+nr));
    for(int i=0;i<ne;i++){ Ent*e=&ent[base+i]; if(e->embed) continue; for(int r=0;r<e->H;r++) if(e->rows[r]){ tab[ntab].h=e->rows[r]; tab[ntab].e=(uint32_t)(base+i); tab[ntab].r=(uint32_t)r; ntab++; } }
    fprintf(stderr,"[a2pc] HD %s textures: %d pictures, %dx\n",kind,ne,K); return 1; }

static int cmp_seg(const void*a,const void*b){ uint64_t x=((const SegRef*)a)->h, y=((const SegRef*)b)->h; return x<y?-1:x>y; }
static int cmp_row(const void*a,const void*b){ uint64_t x=((const RowRef*)a)->h, y=((const RowRef*)b)->h; return x<y?-1:x>y; }
static uint64_t cellhash(const uint64_t*rows,int n){ uint64_t h=(uint64_t)n; for(int r=0;r<n;r++) h=hc_mix(h^rows[r]); return h; }
static int is_cell(const Ent*e){ return e->embed&&e->ncol>=1&&e->ncol<=3; }

static uint8_t *grave[256]; static int ngrave; extern long gpu_frame; extern uint16_t vram[];
#define QUIET 8
static void decode(int i,int t){
    Ent*e=&ent[i]; uint8_t*blob=(uint8_t*)malloc(e->size?e->size:1); FILE*f=parts[e->pack][e->part][t]; int ok=0;
    if(blob&&f&&fseek(f,(long)e->off,SEEK_SET)==0&&fread(blob,1,e->size,f)==e->size) ok=1;
    uint8_t*hd=0;
    if(ok){ hc_crypt(blob,e->size,HC_KEY,0x100000000ull+(uint64_t)e->local);
        size_t n=(size_t)e->W*K*e->H*K*4; hd=(uint8_t*)malloc(n);
        size_t lrn=(e->embed||e->haslr)?(size_t)e->W*e->bpp/8*e->H:0;
        if(hd&&e->size>lrn){ hc_decode(lrn?blob:e->lr,e->bpp,e->W,e->H,K,blob+lrn,e->size-lrn,hd);
            if(KR<K){ int st=K/KR, o=st/2, WW=e->W*KR, HH=e->H*KR; for(int y=0;y<HH;y++) for(int x=0;x<WW;x++) memcpy(hd+((size_t)y*WW+x)*4,hd+((size_t)(y*st+o)*e->W*K+(x*st+o))*4,4);
                hd=(uint8_t*)realloc(hd,(size_t)WW*HH*4); } }
        else { free(hd); hd=0; } }
    free(blob);
    lk(); hd_ndec++; if(hd&&e->hd){ if(ngrave<256) grave[ngrave++]=e->hd; else hd=e->hd; cache_bytes-=(size_t)e->W*KR*e->H*KR*4; } e->hd=hd; e->state=hd?3:0; if(hd){ cache_bytes+=(size_t)e->W*KR*e->H*KR*4; if(nready>=capready){ capready=capready?capready*2:64; ready=(int*)realloc(ready,sizeof(int)*capready); } ready[nready++]=i; } ulk(); }

#ifdef _WIN32
static DWORD WINAPI tmain_(LPVOID p){
#else
static void *tmain_(void*p){
#endif
    int t=(int)(intptr_t)p;
    for(;;){ lk(); while(!njobs) wt(); int i=jobs[--njobs]; ent[i].state=2; ulk(); decode(i,t); }
    return 0; }

void hd_init(int scale_cap){
    const char*a=getenv("A2HDTEX"), *b=getenv("A2HDUI"), *d=getenv("A2HDDIR");
    int want_w=a&&atoi(a), want_u=b&&atoi(b); if(!want_w&&!want_u&&!getenv("A2HDUSAGE")) return;
    snprintf(dir,sizeof dir,"%s",d&&*d?d:"../hd"); minit();
    { const char*u=getenv("A2HDUSAGE"); if(u&&*u) usef=strdup(u); }
    if(want_u||usef) npack+=load_pack("ui",0,want_u);
    if(want_w||usef) npack+=load_pack("world",1,want_w);
    if(!npack||!ntab){ fprintf(stderr,"[a2pc] HD textures: no pack found in %s\n",dir); return; }
    qsort(tab,(size_t)ntab,sizeof(RowRef),cmp_row); if(nsegs) qsort(segs,(size_t)nsegs,sizeof(SegRef),cmp_seg);
    for(int i=0;i<nent;i++) if(is_cell(&ent[i])) ncells++;
    cells=(RowRef*)malloc(sizeof(RowRef)*(ncells+1)); ncells=0;
    for(int i=0;i<nent;i++) if(is_cell(&ent[i])){ cells[ncells].h=cellhash(ent[i].rows,ent[i].H); cells[ncells].e=(uint32_t)i; cells[ncells].r=(uint32_t)ent[i].ncol; ncells++; }
    qsort(cells,(size_t)ncells,sizeof(RowRef),cmp_row);
    { const char*e=getenv("A2HDSCALE"); KR=e?atoi(e):K; if(KR<1||KR>K) KR=K; while(K%KR) KR--; }
    if(scale_cap>0) while(KR>1&&256*KR>scale_cap) KR/=2;
    { const char*e=getenv("A2HDCACHE"); cache_max=(size_t)(e?atoi(e):768)<<20; }
    nthr=2; for(int i=0;i<nthr;i++) spawn(i);
    hd_on=1; }
int hd_scale(void){ return KR; }

static void kill_place(int p){
    Place*q=&pl[p]; if(!q->live) return; q->live=0; Ent*e=&ent[q->e]; int hw=q->hw;
    for(int r=0;r<e->H;r++){ if(!q->got[r]) continue; int y=q->vy+r; if(y<0||y>511) continue;
        for(int c=q->vx;c<=q->vx+hw-1&&c<1024;c++) if(own[y][c]==p+1) own[y][c]=0; }
    for(int r=0;r<q->ch;){ if(!q->got[r]){ r++; continue; } int r1=r; while(r1<q->ch&&q->got[r1]) r1++; qop(HD_CLEAR,q->key,q->u0,q->v0+r,q->cw,r1-r,0,0); r=r1; } }

static void kill_rows(int p,int ra,int rb){
    Place*q=&pl[p]; if(!q->live) return; Ent*e=&ent[q->e]; if(ra<0) ra=0; if(rb>=e->H) rb=e->H-1;
    for(int r=ra;r<=rb;r++){ if(!q->got[r]) continue; q->got[r]=0; int y=q->vy+r;
        if(y>=0&&y<512) for(int c=q->vx;c<=q->vx+q->hw-1&&c<1024;c++) if(own[y][c]==p+1) own[y][c]=0; }
    int a=ra, b=rb+1; if(b>q->ch) b=q->ch; if(a<b) qop(HD_CLEAR,q->key,q->u0,q->v0+a,q->cw,b-a,0,0);
    for(int r=0;r<e->H;r++) if(q->got[r]) return; q->live=0; }

void hd_overwrite(int x0,int y0,int x1,int y1){
    if(!hd_on) return; if(x0<0)x0=0; if(y0<0)y0=0; if(x1>1023)x1=1023; if(y1>511)y1=511;
    for(int y=y0;y<=y1;y++) for(int c=x0;c<=x1;c++){ int p=own[y][c]; if(!p) continue; Place*q=&pl[p-1];
        int ra=y-q->vy, rb=ra; while(rb+1<=y1-q->vy&&rb+1<ent[q->e].H) rb++; kill_rows(p-1,ra,rb); } }

static void put_rows(int p,int r0,int r1){
    Place*q=&pl[p]; Ent*e=&ent[q->e]; if(e->state!=3||!e->hd) return; e->used=tick;
    if(r1>q->ch) r1=q->ch; if(r0>=r1) return;
    qop(HD_PUT,q->key,q->u0,q->v0+r0,q->cw,r1-r0,e->hd+((size_t)r0*KR*e->W*KR+(size_t)q->c0*KR)*4,e->W*KR); }

static int find_place(int e,int vx,int vy,int c0){ for(int i=0;i<NPL;i++) if(pl[i].live&&pl[i].e==e&&pl[i].vx==vx&&pl[i].vy==vy&&pl[i].c0==c0) return i; return -1; }

static void run_sub(int ei,int r0,int n,int x,int y,int c0,int hw);
static void run(int ei,int r0,int n,int x,int y){ Ent*e=&ent[ei]; run_sub(ei,r0,n,x,y,0,e->embed?e->ncol:e->W*e->bpp/16); }
static void run_sub(int ei,int r0,int n,int x,int y,int c0,int hw){
    Ent*e=&ent[ei]; int vy=y-r0, full=!c0&&hw==e->W*e->bpp/16;
    int p=find_place(ei,x,vy,c0);
    for(int r=r0;r<r0+n;r++){ int yy=vy+r; for(int c=x;c<=x+hw-1&&c<1024;c++){ int o=own[yy][c]; if(o&&o-1!=p) kill_rows(o-1,yy-pl[o-1].vy,yy-pl[o-1].vy); } }
    if(p<0){ for(int i=0;i<NPL;i++) if(!pl[i].live){ p=i; break; } if(p<0) return;
        hd_nplace++; Place*q=&pl[p]; memset(q,0,sizeof *q); q->live=1; q->e=ei; q->vx=x; q->vy=vy; q->c0=c0;
        int tpx=x>>6, tpy=(vy>=256)?1:0; q->key=((tpy*16+tpx)<<1)|(e->bpp==8);
        q->hw=hw; q->u0=(x&63)*(16/e->bpp); q->v0=vy-tpy*256; q->cw=full?e->W:hw*(16/e->bpp); if(q->u0+q->cw>256) q->cw=256-q->u0;
        q->ch=e->H; if(q->v0<0){ q->live=0; return; } if(q->v0+q->ch>256) q->ch=256-q->v0; }
    Place*q=&pl[p];
    for(int r=r0;r<r0+n;r++){ int yy=vy+r; q->got[r]=1; for(int c=x;c<=x+hw-1&&c<1024;c++){ own[yy][c]=(uint16_t)(p+1); hd_blk[yy>>4][c>>4]=1; } }
    put_rows(p,r0,r0+n); }

static void collect(int ei,int r0,int n,const uint16_t*vram,int x,int y){
    Ent*e=&ent[ei]; if(!e->size) return; int R=e->W*e->bpp/8;
    if(e->embed||e->haslr){ if(!e->state){ lk(); if(njobs<4096){ jobs[njobs++]=ei; e->state=1; } wk(); ulk(); } return; }
    if(e->state==1||e->state==2){ e->dirty=1; e->lastfr=gpu_frame; return; }
    if(e->state==3&&!e->partial) return;
    if(!e->lr){ e->lr=(uint8_t*)malloc((size_t)R*e->H); e->got=(uint8_t*)calloc((size_t)e->H,1); e->ngot=0; if(!e->lr||!e->got) return; }
    int fresh=0;
    for(int r=r0;r<r0+n;r++){ if(!e->got[r]){ memcpy(e->lr+(size_t)r*R,&vram[(y+r-r0)*1024+x],(size_t)R); e->got[r]=1; e->ngot++; fresh=1; } }
    e->lastfr=gpu_frame; if(fresh&&e->state==3) e->dirty=1;
    if(e->ngot==e->H&&!e->state){ e->partial=0; lk(); if(njobs<4096){ jobs[njobs++]=ei; e->state=1; } wk(); ulk(); } }

static void fill_missing(Ent*e){
    int R=e->W*e->bpp/8, last=-1;
    for(int r=0;r<e->H;r++){ if(e->got[r]){ last=r; continue; } int nx=r; while(nx<e->H&&!e->got[nx]) nx++;
        int src=last>=0&&(nx>=e->H||r-last<=nx-r)?last:nx; if(src>=0&&src<e->H) memcpy(e->lr+(size_t)r*R,e->lr+(size_t)src*R,(size_t)R); } }

static int lookup(uint64_t h){ int lo=0,hi=ntab; while(lo<hi){ int m=(lo+hi)/2; if(tab[m].h<h) lo=m+1; else hi=m; } return lo; }
static int cell_lookup(uint64_t h,int n,int H){ int lo=0,hi=ncells; while(lo<hi){ int m=(lo+hi)/2; if(cells[m].h<h) lo=m+1; else hi=m; }
    for(;lo<ncells&&cells[lo].h==h;lo++) if((int)cells[lo].r==n&&ent[cells[lo].e].H==H) return (int)cells[lo].e; return -1; }

static void text_cells(const uint16_t*vram,int x,int y,int w,int h){
    uint64_t rh[24];
    int best=0, bn=0;
    for(int pass=0;pass<2;pass++){
        for(int ch=24;ch>=16;ch-=8){ if(h<ch||(pass&&ch!=best)) continue; int cnt=0;
            for(int y0=0;y0+ch<=h;y0+=ch) for(int i=0;i<w;){ int e=-1;
                for(int n=3;n>=1&&e<0;n--){ if(i+n>w) continue; int ink=0; for(int r=0;r<ch;r++){ const uint8_t*rp=(const uint8_t*)&vram[(y+y0+r)*1024+x+i]; rh[r]=hc_cellrow(rp,(size_t)n*2); for(int k=0;k<n*2;k++) ink|=rp[k]; } if(!ink) break; e=cell_lookup(cellhash(rh,ch),n,ch); }
                if(e<0){ i++; continue; } cnt++;
                if(pass){ static int tr=-1; if(tr<0) tr=getenv("A2HDTRACE")!=0; if(tr) fprintf(stderr,"glyph %s at %d,%d (upload %d,%d %dx%d)\n",ent[e].name,x+i,y+y0,x,y,w,h); hd_nglyph++; run(e,0,ch,x+i,y+y0); collect(e,0,ch,vram+(y+y0)*1024,x+i,0); }
                i+=ent[e].ncol; }
            if(!pass&&cnt>bn){ bn=cnt; best=ch; } }
        if(!bn) return; } }

static void seg_rows(const uint16_t*vram,int x,int y,int h){
    static int pe=-1, pr, po, px, py;
    for(int j=0;j<h;j++){ const uint8_t*rp=(const uint8_t*)&vram[(y+j)*1024+x]; int ink=0; for(int k=0;k<16;k++) ink|=rp[k]; if(!ink) continue;
        uint64_t hh=hc_cellrow(rp,16); int lo=0,hi=nsegs; while(lo<hi){ int m=(lo+hi)/2; if(segs[m].h<hh) lo=m+1; else hi=m; }
        int best=-1;
        for(int k=lo;k<nsegs&&segs[k].h==hh;k++){ if(best<0) best=k; if((int)segs[k].e==pe&&segs[k].r==pr+1&&segs[k].off==po&&px==x&&py==y+j-1){ best=k; break; } }
        if(best<0){ pe=-1; continue; }
        SegRef*g=&segs[best]; Ent*e=&ent[g->e]; run_sub((int)g->e,g->r,1,x,y+j,g->off*(16/e->bpp),8); collect((int)g->e,0,0,vram,x,y+j);
        pe=(int)g->e; pr=g->r; po=g->off; px=x; py=y+j; } }

void hd_upload(const uint16_t*vram,int x,int y,int w,int h){
    if(!hd_on) return;
    if(x+w>1024||y+h>512){ hd_overwrite(x,y,x+w-1,y+h-1); return; }
    int R=w*2, um=0, matched=0;
    uint64_t*hs=(uint64_t*)malloc(sizeof(uint64_t)*h); if(!hs) return;
    for(int j=0;j<h;j++) hs[j]=hc_rowhash((const uint8_t*)&vram[(y+j)*1024+x],(size_t)R);
    for(int j=0;j<h;){
        int best=-1,bl=0,br=0;
        if(hs[j]) for(int k=lookup(hs[j]);k<ntab&&tab[k].h==hs[j];k++){ Ent*e=&ent[tab[k].e]; if(e->W*e->bpp/8!=R) continue;
            int r=(int)tab[k].r, l=1; while(j+l<h&&r+l<e->H&&e->rows[r+l]==hs[j+l]) l++; if(l>bl){ bl=l; best=(int)tab[k].e; br=r; } }
        if(best<0){ j++; continue; }
        int b=j, r=br; while(b>um&&r>0&&!hs[b-1]&&!ent[best].rows[r-1]){ b--; r--; }
        if(um<b) hd_overwrite(x,y+um,x+w-1,y+b-1);
        int n=j-b+bl; run(best,r,n,x,y+b); collect(best,r,n,vram+(y+b)*1024,x,0); matched=1;
        j=b+n; um=j; }
    if(um<h) hd_overwrite(x,y+um,x+w-1,y+h-1);
    free(hs); tick++;
    if(!matched&&ncells&&h>=16) text_cells(vram,x,y,w,h);
    if(!matched&&nsegs&&w==8&&h<=64) seg_rows(vram,x,y,h); }

static void evict(void){
    while(cache_bytes>cache_max){ int v=-1; for(int i=0;i<nent;i++){ if(ent[i].state!=3||!ent[i].hd) continue; int inuse=0; for(int p=0;p<NPL;p++) if(pl[p].live&&pl[p].e==i){ inuse=1; break; } if(inuse) continue; if(v<0||ent[i].used<ent[v].used) v=i; }
        if(v<0) break; Ent*e=&ent[v]; free(e->hd); e->hd=0; e->state=0; cache_bytes-=(size_t)e->W*KR*e->H*KR*4; free(e->lr); e->lr=0; free(e->got); e->got=0; e->ngot=0; } }

int hd_ops(HdOp**out){
    if(!hd_on){ *out=ops; return 0; }
    lk(); int nr=nready; int*rd=0; if(nr){ rd=(int*)malloc(sizeof(int)*nr); memcpy(rd,ready,sizeof(int)*nr); nready=0; } ulk();
    for(int k=0;k<nr;k++){ int i=rd[k]; if(!ent[i].partial){ free(ent[i].lr); ent[i].lr=0; free(ent[i].got); ent[i].got=0; }
        for(int p=0;p<NPL;p++) if(pl[p].live&&pl[p].e==i){ for(int r=0;r<pl[p].ch;){ if(!pl[p].got[r]){ r++; continue; } int r1=r; while(r1<pl[p].ch&&pl[p].got[r1]) r1++; put_rows(p,r,r1); r=r1; } } }
    free(rd); *out=ops; int n=nops; nops=0; return n; }
void hd_frame(void){
    if(!hd_on) return;
    lk(); for(int i=0;i<ngrave;i++) free(grave[i]); ngrave=0; ulk();
    for(int i=0;i<nent;i++){ Ent*e=&ent[i]; if(!e->lr||e->embed||e->haslr||gpu_frame-e->lastfr<QUIET) continue;
        int go=(!e->state&&e->ngot>0&&e->ngot<e->H)||(e->state==3&&e->dirty&&e->partial);
        if(!go){ if(e->state==3) e->dirty=0; continue; }
        int R=e->W*e->bpp/8;
        for(int p=0;p<NPL;p++){ Place*q=&pl[p]; if(!q->live||q->e!=i) continue;
            for(int r=0;r<e->H;r++){ int y=q->vy+r; if(!q->got[r]||y<0||y>511||own[y][q->vx]!=p+1) continue;
                memcpy(e->lr+(size_t)r*R,&vram[y*1024+q->vx],(size_t)R); if(!e->got[r]){ e->got[r]=1; e->ngot++; } } }
        e->dirty=0; fill_missing(e); e->partial=e->ngot<e->H;
        lk(); if(njobs<4096){ jobs[njobs++]=i; e->state=1; } wk(); ulk(); }
    if(cache_bytes>cache_max) evict(); }
int hd_owned(int x0,int y0,int x1,int y1){
    if(!hd_on) return 0; if(x0<0)x0=0; if(y0<0)y0=0; if(x1>1023)x1=1023; if(y1>511)y1=511;
    for(int y=y0;y<=y1;y++) for(int c=x0;c<=x1;c++) if(own[y][c]) return 1; return 0; }
void hd_why_print(FILE*f);
void hd_stats(FILE*f){ use_write(); hd_why_print(f); if(hd_on) fprintf(f,"HD textures: %ld placed, %ld decoded, %ld glyphs, %ld copies from HD areas, %zu MB cached\n",hd_nplace,hd_ndec,hd_nglyph,hd_ncopy,cache_bytes>>20); }

typedef struct { int e; uint32_t ph; int n; uint16_t pal[256]; uint8_t cells[128]; } Use;
static Use*uses; static int capuses, nuses;
void hd_use(int tpx,int tpy,int mode,int clx,int cly,int umin,int vmin,int umax,int vmax,const uint16_t*vram){
    if(!usef||mode>1) return;
    int hx=tpx*64+(mode?umin/2:umin/4), hy=tpy*256+vmin; if(hx>1023||hy>511) return;
    int p=own[hy][hx]; if(!p) return; Place*q=&pl[p-1]; Ent*e=&ent[q->e];
    if(q->key!=(((tpy*16+tpx)<<1)|mode)) return;
    int n=mode?256:16; uint32_t ph=2166136261u; const uint16_t*cp=&vram[(cly&511)*1024]; uint16_t pal[256];
    for(int i=0;i<n;i++){ pal[i]=cp[(clx+i)&1023]; ph=(ph^pal[i])*16777619u; }
    if(!capuses){ capuses=1<<15; uses=(Use*)calloc((size_t)capuses,sizeof(Use)); if(!uses){ capuses=0; return; } }
    unsigned h=((unsigned)q->e*2654435761u^ph)&(unsigned)(capuses-1); Use*u=0;
    for(int k=0;k<capuses;k++,h=(h+1)&(unsigned)(capuses-1)){ Use*c=&uses[h]; if(!c->n){ c->n=n; c->e=q->e; c->ph=ph; memcpy(c->pal,pal,sizeof(uint16_t)*n); nuses++; u=c; break; } if(c->e==q->e&&c->ph==ph){ u=c; break; } }
    if(!u||nuses>capuses*3/4) return;
    int a0=umin-q->u0, a1=umax-q->u0, b0=vmin-q->v0, b1=vmax-q->v0; if(a0<0)a0=0; if(b0<0)b0=0; if(a1>=e->W)a1=e->W-1; if(b1>=e->H)b1=e->H-1;
    for(int y=b0>>3;y<=b1>>3;y++) for(int x=a0>>3;x<=a1>>3;x++) u->cells[(y*32+x)>>3]|=(uint8_t)(1<<((y*32+x)&7)); }
static void use_write(void){
    if(!usef||!nuses) return; FILE*f=fopen(usef,"a"); if(!f) return;
    for(int i=0;i<capuses;i++){ Use*u=&uses[i]; if(!u->n) continue; fprintf(f,"%s %d ",ent[u->e].name,u->n);
        for(int k=0;k<u->n;k++) fprintf(f,"%04x",u->pal[k]); fputc(' ',f); for(int k=0;k<128;k++) fprintf(f,"%02x",u->cells[k]); fputc('\n',f); }
    fclose(f); fprintf(stderr,"[a2pc] HD usage: %d picture/palette pairs written to %s\n",nuses,usef); nuses=0; memset(uses,0,sizeof(Use)*capuses); }

static long why_n[4]; static int why_on=-1;
typedef struct { int k, n; } WhyLoc; static WhyLoc why_loc[64];
void hd_why(int tpx,int tpy,int mode,int umin,int vmin,int umax,int vmax){
    if(why_on<0) why_on=getenv("A2HDWHY")!=0; if(!why_on||!hd_on) return;
    if(mode>1){ static long n15; static int lx=-1,ly=-1; n15++; if(n15%500==1) fprintf(stderr,"HD why: 15-bit texture at page %d,%d uv %d,%d-%d,%d (%ld)\n",tpx,tpy,umin,vmin,umax,vmax,n15); return; }
    int hx=tpx*64+(mode?umin/2:umin/4), hy=tpy*256+vmin; if(hx>1023||hy>511) return;
    int p=own[hy][hx], r;
    if(!p) r=0; else { Place*q=&pl[p-1]; Ent*e=&ent[q->e]; r=q->key!=(((tpy*16+tpx)<<1)|mode)?1:e->state!=3?2:3; }
    why_n[r]++;
    if(r<3){ int k=(r<<24)|(hy<<12)|(hx&~3)|(mode<<11); for(int i=0;i<64;i++){ if(why_loc[i].n&&why_loc[i].k==k){ why_loc[i].n++; break; } if(!why_loc[i].n){ why_loc[i].k=k; why_loc[i].n=1; break; } } } }
const char*hd_owner_name(int tpx,int tpy,int mode,int u,int v){
    if(!hd_on||mode>1) return "-"; int hx=tpx*64+(mode?u/2:u/4), hy=tpy*256+v; if(hx>1023||hy>511) return "-"; int p=own[hy][hx]; return p?ent[pl[p-1].e].name:"-"; }
void hd_why_print(FILE*f){
    if(why_on<=0) return; fprintf(f,"HD why: unowned %ld, other page/mode %ld, not decoded %ld, ok %ld\n",why_n[0],why_n[1],why_n[2],why_n[3]);
    for(int i=0;i<64&&why_loc[i].n;i++){ int k=why_loc[i].k, hx=k&0x7fc, hy=(k>>12)&511, own_=0; int p=own[hy][hx]; if(p) own_=p;
        fprintf(f,"  reason %d at vram %d,%d mode %d: %d draws, owner %s key %d\n",k>>24,hx,hy,(k>>11)&1,why_loc[i].n,p?ent[pl[p-1].e].name:"-",p?pl[p-1].key:-1); } }

void hd_copy(int sx,int sy,int dx,int dy,int w,int h){
    if(!hd_on) return;
    typedef struct { int e, r0, n, x, y, c0, hw; } Mv; Mv mv[64]; int nm=0;
    if(sx+w<=1024&&sy+h<=512&&dx+w<=1024&&dy+h<=512&&hd_owned(sx,sy,sx+w-1,sy+h-1)){
        for(int i=0;i<NPL&&nm<64;i++){ Place*q=&pl[i]; if(!q->live) continue;
            int hx0=q->vx>sx?q->vx:sx, hx1=q->vx+q->hw<sx+w?q->vx+q->hw:sx+w; if(hx0>=hx1) continue;
            int tpc=16/ent[q->e].bpp; if(ent[q->e].embed&&(hx0!=q->vx||hx1!=q->vx+q->hw)) continue;
            int a=sy-q->vy, b=sy+h-q->vy; if(a<0) a=0; if(b>ent[q->e].H) b=ent[q->e].H;
            for(int r=a;r<b;){ if(!q->got[r]){ r++; continue; } int r1=r; while(r1<b&&q->got[r1]) r1++;
                if(nm<64){ mv[nm].e=q->e; mv[nm].r0=r; mv[nm].n=r1-r; mv[nm].x=hx0+dx-sx; mv[nm].y=q->vy+r+dy-sy; mv[nm].c0=q->c0+(hx0-q->vx)*tpc; mv[nm].hw=hx1-hx0; nm++; } r=r1; } } }
    if(nm) hd_ncopy++;
    hd_overwrite(dx,dy,dx+w-1,dy+h-1);
    for(int i=0;i<nm;i++) run_sub(mv[i].e,mv[i].r0,mv[i].n,mv[i].x,mv[i].y,mv[i].c0,mv[i].hw); }
