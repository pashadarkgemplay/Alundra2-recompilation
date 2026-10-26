/* Geometry engine: the fixed-point 3D math unit the game uses for rotation and projection. */

#include "a2rt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static int32_t D[32], C[32];
int gte_unknown;
enum { F_MAC1P=30,F_MAC2P=29,F_MAC3P=28,F_MAC1N=27,F_MAC2N=26,F_MAC3N=25,F_IR1=24,F_IR2=23,F_IR3=22,F_CR=21,F_CG=20,F_CB=19,
       F_SZ=18,F_DIV=17,F_MAC0P=16,F_MAC0N=15,F_SX=14,F_SY=13,F_IR0=12 };
int a2_nofog;
#define FLAG C[31]
#define SETF(b) (FLAG|=1u<<(b))
static uint8_t unr[257]; static int unr_init;
static void init_unr(void){ if(unr_init) return; for(int i=0;i<=256;i++){ int v=((0x40000/(i+0x100))+1)/2-0x101; unr[i]=v<0?0:v; } unr_init=1; }
static inline int16_t s16_(int32_t x){ return (int16_t)x; }
#define VX(n) s16_(D[(n)*2])
#define VY(n) s16_(D[(n)*2]>>16)
#define VZ(n) s16_(D[(n)*2+1])
#define IR0 D[8]
#define IR1 D[9]
#define IR2 D[10]
#define IR3 D[11]
#define MAC0 D[24]
#define MAC1 D[25]
#define MAC2 D[26]
#define MAC3 D[27]
#define RGBC D[6]

static inline int mat(int base,int r,int c){ int k=r*3+c; int32_t w=C[base+k/2]; return (k&1)? (int16_t)(w>>16) : (int16_t)w; }

static int64_t chkA(int i,int64_t v){
    if(v>0x7FFFFFFFFFFLL) SETF(F_MAC1P-i); else if(v<-0x80000000000LL) SETF(F_MAC1N-i);
    return (int64_t)((uint64_t)v<<20)>>20; }
static int32_t clampIR(int i,int32_t v,int lm){
    int32_t lo=lm?0:-0x8000; int fl= i==0?F_IR1: i==1?F_IR2:F_IR3;
    if(v<lo){ SETF(fl); return lo; } if(v>0x7FFF){ SETF(fl); return 0x7FFF; } return v; }
static void setMAC0(int64_t v){ if(v>0x7FFFFFFFLL) SETF(F_MAC0P); else if(v<-0x80000000LL) SETF(F_MAC0N); MAC0=(int32_t)v; }
static void setIR(int lm){ IR1=clampIR(0,MAC1,lm); IR2=clampIR(1,MAC2,lm); IR3=clampIR(2,MAC3,lm); }
extern long gpu_frame; static long zc[8]; static long zframe=-1;
static void zstat(int32_t raw){ static int on=-1; if(on<0) on=getenv("A2GTEZ")!=0; if(!on) return; if(gpu_frame!=zframe){ if(zframe>=0&&(zframe%20==0)) fprintf(stderr,"Z F%ld total %ld  <0 %ld  <188 %ld  <384 %ld  >=0x8000 %ld  >0xFFFF %ld\n",zframe,zc[0],zc[1],zc[2],zc[3],zc[4],zc[5]); zframe=gpu_frame; for(int i=0;i<8;i++) zc[i]=0; } zc[0]++; if(raw<0) zc[1]++; if(raw<188) zc[2]++; if(raw<384) zc[3]++; if(raw>=0x8000) zc[4]++; if(raw>0xFFFF) zc[5]++; }
static void pushSZ(int32_t v){ zstat(v); if(v<0){v=0;SETF(F_SZ);} else if(v>0xFFFF){v=0xFFFF;SETF(F_SZ);} D[16]=D[17]; D[17]=D[18]; D[18]=D[19]; D[19]=v; }

uint8_t gte_stamp[1<<22]; extern long a2_flips; int a2_gte_mark;
static void pushSXY(int32_t x,int32_t y){
    if(x<-0x400){x=-0x400;SETF(F_SX);} else if(x>0x3FF){x=0x3FF;SETF(F_SX);}
    if(y<-0x400){y=-0x400;SETF(F_SY);} else if(y>0x3FF){y=0x3FF;SETF(F_SY);}
    if(a2_gte_mark) gte_stamp[((x&0x7ff)<<11)|(y&0x7ff)]=(uint8_t)((a2_flips&0x7f)+1);
    D[12]=D[13]; D[13]=D[14]; D[14]=(uint32_t)(x&0xFFFF)|((uint32_t)y<<16); }
static uint32_t clampC(int32_t v,int fl){ if(v<0){SETF(fl);return 0;} if(v>255){SETF(fl);return 255;} return v; }
static void pushColor(void){ uint32_t r=clampC(MAC1>>4,F_CR), g=clampC(MAC2>>4,F_CG), b=clampC(MAC3>>4,F_CB);
    D[20]=D[21]; D[21]=D[22]; D[22]=r|(g<<8)|(b<<16)|((uint32_t)RGBC&0xFF000000u); }

static uint32_t divide(uint32_t h,uint32_t sz3){
    init_unr(); h&=0xFFFF; sz3&=0xFFFF;
    if(h>=sz3*2u){ SETF(F_DIV); return 0x1FFFF; }
    int z=0; for(uint32_t t=sz3; z<16 && !(t&0x8000); t<<=1) z++;
    uint64_t n=(uint64_t)h<<z; uint32_t d=sz3<<z;
    uint32_t u=unr[(d-0x7FC0)>>7]+0x101;
    int64_t dd=(int64_t)(((int64_t)0x2000080-(int64_t)d*u)>>8); dd=(0x80+dd*(int64_t)u)>>8;
    uint64_t r=(n*(uint64_t)dd+0x8000)>>16; return r>0x1FFFF?0x1FFFF:(uint32_t)r; }

static int wide_kn=-1;
static void wide_init(void){ const char*e=getenv("A2WIDE"); wide_kn=256; if(!e||!*e) return; double a=0; int p,q; if(sscanf(e,"%d:%d",&p,&q)==2&&q>0) a=(double)p/q; else a=atof(e); if(a>1.34&&a<8.0) wide_kn=(int)(256.0*(4.0/3.0)/a+0.5); }
static void rtp(int vi,int sf,int lm,int last){
    int sh=sf?12:0; int vx=VX(vi),vy=VY(vi),vz=VZ(vi);
    int64_t s[3];
    for(int i=0;i<3;i++){ int64_t a=chkA(i,((int64_t)C[5+i]<<12)+(int64_t)mat(0,i,0)*vx); a=chkA(i,a+(int64_t)mat(0,i,1)*vy); s[i]=chkA(i,a+(int64_t)mat(0,i,2)*vz); }
    MAC1=(int32_t)(s[0]>>sh); MAC2=(int32_t)(s[1]>>sh); MAC3=(int32_t)(s[2]>>sh);
    IR1=clampIR(0,MAC1,lm); IR2=clampIR(1,MAC2,lm);
    { int32_t v=(int32_t)(s[2]>>12); if(v<-0x8000||v>0x7FFF) SETF(F_IR3);
      int32_t lo=lm?0:-0x8000; IR3= MAC3<lo?lo: MAC3>0x7FFF?0x7FFF:MAC3; }
    pushSZ((int32_t)(s[2]>>12));
    uint32_t div=divide((uint16_t)C[26],(uint16_t)D[19]);
    if(wide_kn<0) wide_init();
    int64_t sx=(int64_t)C[24]+(((int64_t)IR1*div*wide_kn)>>8), sy=(int64_t)C[25]+(int64_t)IR2*div;
    setMAC0(sx); int32_t X=(int32_t)(sx>>16); setMAC0(sy); int32_t Y=(int32_t)(sy>>16);
    pushSXY(X,Y);
    if(last){ int64_t m0=(int64_t)C[28]+(int64_t)(int16_t)C[27]*div; setMAC0(m0); int32_t v=(int32_t)(m0>>12);
        if(v<0){v=0;SETF(F_IR0);} else if(v>0x1000){v=0x1000;SETF(F_IR0);} IR0=v; }
}

static void mvmva(int mx,int v,int cv,int sf,int lm){
    int sh=sf?12:0; int vv[3];
    if(v<3){ vv[0]=VX(v); vv[1]=VY(v); vv[2]=VZ(v); } else { vv[0]=(int16_t)IR1; vv[1]=(int16_t)IR2; vv[2]=(int16_t)IR3; }
    int M[3][3];
    if(mx<3){ for(int r=0;r<3;r++)for(int c=0;c<3;c++) M[r][c]=mat(mx==0?0:mx==1?8:16,r,c); }
    else { int R=RGBC&0xFF; int a=mat(0,0,2), b=mat(0,1,1);
        M[0][0]=-(R<<4); M[0][1]=R<<4; M[0][2]=(int16_t)IR0; for(int c=0;c<3;c++){ M[1][c]=a; M[2][c]=b; } }
    int64_t t[3]={0,0,0}; if(cv<3){ for(int i=0;i<3;i++) t[i]=(int64_t)C[(cv==0?5:cv==1?13:21)+i]<<12; }
    int64_t s[3];
    for(int i=0;i<3;i++){
        int64_t a=chkA(i,t[i]+(int64_t)M[i][0]*vv[0]);
        if(cv==2){ clampIR(i,(int32_t)(a>>sh),0);
                   a=0; }
        a=chkA(i,a+(int64_t)M[i][1]*vv[1]); s[i]=chkA(i,a+(int64_t)M[i][2]*vv[2]); }
    MAC1=(int32_t)(s[0]>>sh); MAC2=(int32_t)(s[1]>>sh); MAC3=(int32_t)(s[2]>>sh); setIR(lm);
}
static void interp(int64_t in1,int64_t in2,int64_t in3,int sf,int lm){
    int sh=sf?12:0; int64_t in[3]={in1,in2,in3}; int32_t ir[3];
    for(int i=0;i<3;i++){ int64_t d=chkA(i,((int64_t)C[21+i]<<12)-in[i]); ir[i]=clampIR(i,(int32_t)(d>>sh),0); }
    int64_t m[3]; for(int i=0;i<3;i++) m[i]=chkA(i,in[i]+(int64_t)ir[i]*(a2_nofog?0:(int16_t)IR0));
    MAC1=(int32_t)(m[0]>>sh); MAC2=(int32_t)(m[1]>>sh); MAC3=(int32_t)(m[2]>>sh); setIR(lm);
}
static void lightcolor(int vi,int sf,int lm){
    int sh=sf?12:0; int vx=VX(vi),vy=VY(vi),vz=VZ(vi); int64_t s[3];
    for(int i=0;i<3;i++){ int64_t a=chkA(i,(int64_t)mat(8,i,0)*vx); a=chkA(i,a+(int64_t)mat(8,i,1)*vy); s[i]=chkA(i,a+(int64_t)mat(8,i,2)*vz); }
    MAC1=(int32_t)(s[0]>>sh); MAC2=(int32_t)(s[1]>>sh); MAC3=(int32_t)(s[2]>>sh); setIR(1);
    int ir[3]={IR1,IR2,IR3};
    for(int i=0;i<3;i++){ int64_t a=chkA(i,((int64_t)C[13+i]<<12)+(int64_t)mat(16,i,0)*ir[0]); a=chkA(i,a+(int64_t)mat(16,i,1)*ir[1]); s[i]=chkA(i,a+(int64_t)mat(16,i,2)*ir[2]); }
    MAC1=(int32_t)(s[0]>>sh); MAC2=(int32_t)(s[1]>>sh); MAC3=(int32_t)(s[2]>>sh); setIR(1);
}
static long gte_hist[64];
__attribute__((destructor)) static void gte_histp(void){ if(!getenv("A2GTESTAT")) return; for(int i=0;i<64;i++) if(gte_hist[i]) fprintf(stderr,"GTE op %02x: %ld\n",i,gte_hist[i]); }
void gte_cmd(CPU*c,uint32_t cmd){
    if(!gte_hist[cmd&63]++ && getenv("A2GTESTAT")) fprintf(stderr,"GTE first use of op %02x (cmd %08x)\n",cmd&63,cmd);
    int sf=(cmd>>19)&1, lm=(cmd>>10)&1; FLAG=0;
    switch(cmd&0x3F){
    case 0x01: rtp(0,sf,lm,1); break;
    case 0x30: rtp(0,sf,lm,0); rtp(1,sf,lm,0); rtp(2,sf,lm,1); break;
    case 0x06: { int sx0=(int16_t)D[12],sy0=(int16_t)(D[12]>>16),sx1=(int16_t)D[13],sy1=(int16_t)(D[13]>>16),sx2=(int16_t)D[14],sy2=(int16_t)(D[14]>>16);
        setMAC0((int64_t)sx0*sy1+(int64_t)sx1*sy2+(int64_t)sx2*sy0-(int64_t)sx0*sy2-(int64_t)sx1*sy0-(int64_t)sx2*sy1); } break;
    case 0x0C: { int sh=sf?12:0; int d1=mat(0,0,0),d2=mat(0,1,1),d3=mat(0,2,2); int64_t i1=(int16_t)IR1,i2=(int16_t)IR2,i3=(int16_t)IR3;
        MAC1=(int32_t)(chkA(0,i3*d2-i2*d3)>>sh); MAC2=(int32_t)(chkA(1,i1*d3-i3*d1)>>sh); MAC3=(int32_t)(chkA(2,i2*d1-i1*d2)>>sh); setIR(lm); } break;
    case 0x10: { uint32_t r=RGBC&255,g=(RGBC>>8)&255,b=(RGBC>>16)&255; interp((int64_t)r<<16,(int64_t)g<<16,(int64_t)b<<16,sf,lm); pushColor(); } break;
    case 0x2A: for(int k=0;k<3;k++){ uint32_t w=D[20]; interp((int64_t)(w&255)<<16,(int64_t)((w>>8)&255)<<16,(int64_t)((w>>16)&255)<<16,sf,lm); pushColor(); } break;
    case 0x12: mvmva((cmd>>17)&3,(cmd>>15)&3,(cmd>>13)&3,sf,lm); break;
    case 0x13: { lightcolor(0,sf,lm); uint32_t r=RGBC&255,g=(RGBC>>8)&255,b=(RGBC>>16)&255;
        interp(((int64_t)r*(int16_t)IR1)<<4,((int64_t)g*(int16_t)IR2)<<4,((int64_t)b*(int16_t)IR3)<<4,sf,lm); pushColor(); } break;
    case 0x1B: { lightcolor(0,sf,lm); uint32_t r=RGBC&255,g=(RGBC>>8)&255,b=(RGBC>>16)&255; int sh=sf?12:0;
        MAC1=(int32_t)(chkA(0,((int64_t)r*(int16_t)IR1)<<4)>>sh); MAC2=(int32_t)(chkA(1,((int64_t)g*(int16_t)IR2)<<4)>>sh); MAC3=(int32_t)(chkA(2,((int64_t)b*(int16_t)IR3)<<4)>>sh);
        setIR(lm); pushColor(); } break;
    case 0x28: { int sh=sf?12:0; int64_t a=(int16_t)IR1,b=(int16_t)IR2,d=(int16_t)IR3;
        MAC1=(int32_t)(chkA(0,a*a)>>sh); MAC2=(int32_t)(chkA(1,b*b)>>sh); MAC3=(int32_t)(chkA(2,d*d)>>sh); setIR(lm); } break;
    case 0x3D: { int sh=sf?12:0; int64_t k=(int16_t)IR0;
        MAC1=(int32_t)(chkA(0,(int16_t)IR1*k)>>sh); MAC2=(int32_t)(chkA(1,(int16_t)IR2*k)>>sh); MAC3=(int32_t)(chkA(2,(int16_t)IR3*k)>>sh); setIR(lm); pushColor(); } break;
    case 0x2D: { int64_t m=(int64_t)(int16_t)C[29]*((int64_t)(uint16_t)D[17]+(uint16_t)D[18]+(uint16_t)D[19]); setMAC0(m); int32_t v=(int32_t)(m>>12); if(v<0){v=0;SETF(F_SZ);} else if(v>0xFFFF){v=0xFFFF;SETF(F_SZ);} D[7]=v; } break;
    case 0x2E: { int64_t m=(int64_t)(int16_t)C[30]*((int64_t)(uint16_t)D[16]+(uint16_t)D[17]+(uint16_t)D[18]+(uint16_t)D[19]); setMAC0(m); int32_t v=(int32_t)(m>>12); if(v<0){v=0;SETF(F_SZ);} else if(v>0xFFFF){v=0xFFFF;SETF(F_SZ);} D[7]=v; } break;
    case 0x1E: lightcolor(0,sf,lm); pushColor(); break;
    case 0x20: for(int k=0;k<3;k++){ lightcolor(k,sf,lm); pushColor(); } break;
    case 0x16: for(int k=0;k<3;k++){ lightcolor(k,sf,lm); uint32_t r=RGBC&255,g=(RGBC>>8)&255,b=(RGBC>>16)&255;
        interp(((int64_t)r*(int16_t)IR1)<<4,((int64_t)g*(int16_t)IR2)<<4,((int64_t)b*(int16_t)IR3)<<4,sf,lm); pushColor(); } break;
    case 0x3F: for(int k=0;k<3;k++){ lightcolor(k,sf,lm); uint32_t r=RGBC&255,g=(RGBC>>8)&255,b=(RGBC>>16)&255; int sh=sf?12:0;
        MAC1=(int32_t)(chkA(0,((int64_t)r*(int16_t)IR1)<<4)>>sh); MAC2=(int32_t)(chkA(1,((int64_t)g*(int16_t)IR2)<<4)>>sh); MAC3=(int32_t)(chkA(2,((int64_t)b*(int16_t)IR3)<<4)>>sh);
        setIR(lm); pushColor(); } break;
    case 0x1C: { int sh=sf?12:0; int ir[3]={(int16_t)IR1,(int16_t)IR2,(int16_t)IR3}; int64_t s[3];
        for(int i=0;i<3;i++){ int64_t a=chkA(i,((int64_t)C[13+i]<<12)+(int64_t)mat(16,i,0)*ir[0]); a=chkA(i,a+(int64_t)mat(16,i,1)*ir[1]); s[i]=chkA(i,a+(int64_t)mat(16,i,2)*ir[2]); }
        MAC1=(int32_t)(s[0]>>sh); MAC2=(int32_t)(s[1]>>sh); MAC3=(int32_t)(s[2]>>sh); setIR(1);
        uint32_t r=RGBC&255,g=(RGBC>>8)&255,b=(RGBC>>16)&255;
        MAC1=(int32_t)(chkA(0,((int64_t)r*(int16_t)IR1)<<4)>>sh); MAC2=(int32_t)(chkA(1,((int64_t)g*(int16_t)IR2)<<4)>>sh); MAC3=(int32_t)(chkA(2,((int64_t)b*(int16_t)IR3)<<4)>>sh);
        setIR(lm); pushColor(); } break;
    case 0x14: { int sh=sf?12:0; int ir[3]={(int16_t)IR1,(int16_t)IR2,(int16_t)IR3}; int64_t s[3];
        for(int i=0;i<3;i++){ int64_t a=chkA(i,((int64_t)C[13+i]<<12)+(int64_t)mat(16,i,0)*ir[0]); a=chkA(i,a+(int64_t)mat(16,i,1)*ir[1]); s[i]=chkA(i,a+(int64_t)mat(16,i,2)*ir[2]); }
        MAC1=(int32_t)(s[0]>>sh); MAC2=(int32_t)(s[1]>>sh); MAC3=(int32_t)(s[2]>>sh); setIR(1);
        uint32_t r=RGBC&255,g=(RGBC>>8)&255,b=(RGBC>>16)&255;
        interp(((int64_t)r*(int16_t)IR1)<<4,((int64_t)g*(int16_t)IR2)<<4,((int64_t)b*(int16_t)IR3)<<4,sf,lm); pushColor(); } break;
    case 0x29: { uint32_t r=RGBC&255,g=(RGBC>>8)&255,b=(RGBC>>16)&255;
        interp(((int64_t)r*(int16_t)IR1)<<4,((int64_t)g*(int16_t)IR2)<<4,((int64_t)b*(int16_t)IR3)<<4,sf,lm); pushColor(); } break;
    case 0x11: interp((int64_t)(int16_t)IR1<<12,(int64_t)(int16_t)IR2<<12,(int64_t)(int16_t)IR3<<12,sf,lm); pushColor(); break;
    default: gte_unknown++; break;
    }
    if(FLAG&0x7F87E000u) FLAG|=0x80000000u;
}
uint32_t gte_mfc2(CPU*c,int r){
    switch(r){
    case 1: case 3: case 5: case 8: case 9: case 10: case 11: return (uint32_t)(int32_t)(int16_t)D[r];
    case 7: case 16: case 17: case 18: case 19: return (uint16_t)D[r];
    case 15: return D[14];
    case 28: case 29: { int a=IR1>>7,b=IR2>>7,d=IR3>>7; a=a<0?0:a>31?31:a; b=b<0?0:b>31?31:b; d=d<0?0:d>31?31:d; return a|(b<<5)|(d<<10); }
    default: return D[r]; }
}
void gte_mtc2(CPU*c,int r,uint32_t v){
    switch(r){
    case 15: D[12]=D[13]; D[13]=D[14]; D[14]=v; return;
    case 28: D[28]=v&0x7FFF; IR1=(v&31)<<7; IR2=((v>>5)&31)<<7; IR3=((v>>10)&31)<<7; return;
    case 29: return;
    case 30: { D[30]=v; uint32_t x=(int32_t)v<0?~v:v; int n=0; for(int i=31;i>=0&&((x>>i)&1)==0;i--) n++; D[31]=n; return; }
    case 31: return;
    default: D[r]=v; } }
uint32_t gte_cfc2(CPU*c,int r){ switch(r){ case 4: case 12: case 20: case 26: case 27: case 29: case 30: return (uint32_t)(int32_t)(int16_t)C[r]; default: return C[r]; } }
void gte_ctc2(CPU*c,int r,uint32_t v){ if(r==31){ C[31]=(v&0x7FFFF000u); if(C[31]&0x7F87E000u) C[31]|=0x80000000u; } else C[r]=v; }

int32_t* gte_D(void){return D;} int32_t* gte_C(void){return C;}
