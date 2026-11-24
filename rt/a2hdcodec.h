/* HD palette-index maps: context-modelled range coder, shared by the game and the pack builder.
   An HD pixel = (i0, i1, w): two palette indices from the 2x2 low-res footprint and the weight of i1 (0..15, 15 = one half). */
#ifndef A2HDCODEC_H
#define A2HDCODEC_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

typedef struct { uint64_t low; uint32_t range; uint8_t cache; uint64_t csize; uint8_t *out; size_t n, cap; } HcEnc;
typedef struct { uint32_t range, code; const uint8_t *in; size_t n, pos; } HcDec;

static void hce_shift(HcEnc *e){
    if((uint32_t)e->low<0xFF000000u||(e->low>>32)){ uint8_t t=e->cache; do{ if(e->n<e->cap) e->out[e->n]=(uint8_t)(t+(e->low>>32)); e->n++; t=0xFF; }while(--e->csize); e->cache=(uint8_t)(e->low>>24); }
    e->csize++; e->low=(e->low&0x00FFFFFFu)<<8; }
static void hce_bit(HcEnc *e,uint16_t *p,int b){ uint32_t bound=(e->range>>11)*(*p);
    if(!b){ e->range=bound; *p+=(2048-*p)>>5; } else { e->low+=bound; e->range-=bound; *p-=*p>>5; }
    while(e->range<(1u<<24)){ e->range<<=8; hce_shift(e); } }
static int hcd_bit(HcDec *d,uint16_t *p){ uint32_t bound=(d->range>>11)*(*p); int b;
    if(d->code<bound){ d->range=bound; *p+=(2048-*p)>>5; b=0; } else { d->code-=bound; d->range-=bound; *p-=*p>>5; b=1; }
    while(d->range<(1u<<24)){ d->range<<=8; d->code=(d->code<<8)|(d->pos<d->n?d->in[d->pos]:0); d->pos++; } return b; }

typedef struct { uint16_t s0[3][4][3], w[8][16], s1[3][4][3]; } HcCtx;
static void hc_init(HcCtx *c){ uint16_t *p=(uint16_t*)c; for(size_t i=0;i<sizeof *c/2;i++) p[i]=1024; }

static int hc_idx(const uint8_t *lr,int bpp,int W,int x,int y){ int i=y*W+x; return bpp==4 ? ((lr[i>>1]>>((i&1)*4))&15) : lr[i]; }

/* distinct indices of the footprint of HD pixel (X,Y), nearest low-res pixel first */
static int hc_cands(const uint8_t *lr,int bpp,int W,int H,int K,int X,int Y,int *c){
    int nx=X/K, ny=Y/K, ox=((2*X+1-K)<0?-1:(2*X+1-K)/(2*K)), oy=((2*Y+1-K)<0?-1:(2*Y+1-K)/(2*K));
    ox = ox==nx ? nx+1 : nx-1; oy = oy==ny ? ny+1 : ny-1;
    if(ox<0) ox=0; if(ox>=W) ox=W-1; if(oy<0) oy=0; if(oy>=H) oy=H-1;
    int v[4]={hc_idx(lr,bpp,W,nx,ny),hc_idx(lr,bpp,W,ox,ny),hc_idx(lr,bpp,W,nx,oy),hc_idx(lr,bpp,W,ox,oy)}, n=0;
    for(int i=0;i<4;i++){ int d=0; for(int j=0;j<n;j++) if(c[j]==v[i]) d=1; if(!d) c[n++]=v[i]; } return n; }

static void hce_sym(HcEnc *e,uint16_t *t,int n,int s){ for(int k=0;k<n-1;k++){ int b=s>k; hce_bit(e,&t[k],b); if(!b) return; } }
static int hcd_sym(HcDec *d,uint16_t *t,int n){ int k=0; while(k<n-1&&hcd_bit(d,&t[k])) k++; return k; }

/* hd: W*K x H*K pixels of 4 bytes (i0, i1, w, any). returns the coded size (may exceed cap: then the output is cut) */
static size_t hc_encode(const uint8_t *lr,int bpp,int W,int H,int K,const uint8_t *hd,uint8_t *out,size_t cap){
    HcEnc e={0,0xFFFFFFFFu,0,1,out,0,cap}; HcCtx *c=(HcCtx*)malloc(sizeof(HcCtx)); hc_init(c);
    int WW=W*K, HH=H*K; uint8_t *ps=(uint8_t*)calloc((size_t)WW*2,1), *pw=ps+WW;
    for(int Y=0;Y<HH;Y++){ int ls=0,lw=0;
        for(int X=0;X<WW;X++){ const uint8_t *q=&hd[((size_t)Y*WW+X)*4]; int cd[4], n=hc_cands(lr,bpp,W,H,K,X,Y,cd), s0=0, s1=0, w=q[1]==q[0]?0:q[2];
            if(n>1){ for(int i=0;i<n;i++) if(cd[i]==q[0]){ s0=i; break; }
                hce_sym(&e,c->s0[n-2][(ls==0)|((ps[X]==0)<<1)],n,s0);
                int wc=(lw>0)|((pw[X]>0)<<1)|((s0==0)<<2); hce_sym(&e,c->w[wc],16,w);
                if(w){ for(int i=0,k=0;i<n;i++){ if(i==s0) continue; if(cd[i]==q[1]){ s1=k; break; } k++; } hce_sym(&e,c->s1[n-2][s0<3?s0:3],n-1,s1); }
            } else w=0;
            ls=s0; lw=w; ps[X]=(uint8_t)s0; pw[X]=(uint8_t)w; } }
    for(int i=0;i<5;i++) hce_shift(&e);
    free(ps); free(c); return e.n; }

static void hc_decode(const uint8_t *lr,int bpp,int W,int H,int K,const uint8_t *in,size_t n,uint8_t *hd){
    HcDec d={0xFFFFFFFFu,0,in,n,0}; for(int i=0;i<5;i++) d.code=(d.code<<8)|(d.pos<n?in[d.pos]:0), d.pos++;
    HcCtx *c=(HcCtx*)malloc(sizeof(HcCtx)); hc_init(c);
    int WW=W*K, HH=H*K; uint8_t *ps=(uint8_t*)calloc((size_t)WW*2,1), *pw=ps+WW;
    for(int Y=0;Y<HH;Y++){ int ls=0,lw=0;
        for(int X=0;X<WW;X++){ uint8_t *q=&hd[((size_t)Y*WW+X)*4]; int cd[4], m=hc_cands(lr,bpp,W,H,K,X,Y,cd), s0=0, w=0, i1;
            if(m>1){ s0=hcd_sym(&d,c->s0[m-2][(ls==0)|((ps[X]==0)<<1)],m);
                w=hcd_sym(&d,c->w[(lw>0)|((pw[X]>0)<<1)|((s0==0)<<2)],16); i1=cd[s0];
                if(w){ int s1=hcd_sym(&d,c->s1[m-2][s0<3?s0:3],m-1); for(int i=0,k=0;i<m;i++){ if(i==s0) continue; if(k==s1){ i1=cd[i]; break; } k++; } } }
            else i1=cd[0];
            q[0]=(uint8_t)cd[s0]; q[1]=(uint8_t)i1; q[2]=(uint8_t)w; q[3]=255;
            ls=s0; lw=w; ps[X]=(uint8_t)s0; pw[X]=(uint8_t)w; } }
    free(ps); free(c); }

static uint64_t hc_mix(uint64_t z){ z+=0x9E3779B97F4A7C15ull; z=(z^(z>>30))*0xBF58476D1CE4E5B9ull; z=(z^(z>>27))*0x94D049BB133111EBull; return z^(z>>31); }
static void hc_crypt(uint8_t *p,size_t n,uint64_t key,uint64_t nonce){
    for(size_t i=0;i<n;i+=8){ uint64_t k=hc_mix(key^hc_mix(nonce+(i>>3))); for(size_t j=0;j<8&&i+j<n;j++) p[i+j]^=(uint8_t)(k>>(8*j)); } }
static uint64_t hc_rowhash(const uint8_t *p,size_t n){ uint64_t h=0xcbf29ce484222325ull; int same=1;
    for(size_t i=0;i<n;i++){ h=(h^p[i])*0x100000001b3ull; if(p[i]!=p[0]) same=0; } return same?0:(h?h:1); }
static uint64_t hc_cellrow(const uint8_t *p,size_t n){ uint64_t h=0x84222325cbf29ce4ull; for(size_t i=0;i<n;i++) h=(h^p[i])*0x100000001b3ull; return h; }
#define HC_KEY 0xA2D3C0DE5EC2E7A2ull
#endif
