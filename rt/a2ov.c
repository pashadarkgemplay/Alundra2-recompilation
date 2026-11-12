/* Address lookup for the translated code: which translated function sits at a game address, for the overlay that is loaded. */

#include "a2rt.h"
#include <stdio.h>
#include <stdlib.h>
extern const FuncEntry main_functions[], al22_functions[], al22b_functions[], al22c_functions[], al22d_functions[], game00_functions[], event00_functions[], game01_functions[], game02_functions[], game03_functions[], p1_1_functions[], p1_2_functions[], p1_11_functions[], p1_13_functions[], p1_17_functions[], p1_18_functions[], p1_21_functions[], p1_23_functions[], p1_24_functions[], p1_25_functions[], p1_26_functions[], p1_30_functions[], p1_31_functions[], p1_32_functions[], p1_33_functions[], p1_34_functions[], p1_35_functions[];
static const FuncEntry* const OV[5]={game00_functions,event00_functions,game01_functions,game02_functions,game03_functions};
static const FuncEntry* const P1[17]={p1_1_functions,p1_2_functions,p1_11_functions,p1_13_functions,p1_17_functions,p1_18_functions,p1_21_functions,p1_23_functions,p1_24_functions,p1_25_functions,p1_26_functions,p1_30_functions,p1_31_functions,p1_32_functions,p1_33_functions,p1_34_functions,p1_35_functions};
static const int P1ID[17]={1,2,11,13,17,18,21,23,24,25,26,30,31,32,33,34,35};
static const FuncEntry* const AL[4]={al22_functions,al22b_functions,al22c_functions,al22d_functions};
int a2_al_variant(void){ if(rd32(0x80010000u)!=0x32324c41u) return 0; switch(rd32(0x80010004u)){ case 0: return 1; case 1: return 2; case 2: return 3; default: return 0; } }
static int cur_ov=-1, cur_p1=-1;
void a2_set_overlay(int id){ cur_ov=id; }
int a2_get_overlay(void){ return cur_ov; }
void a2_set_p1(int idx){ cur_p1=idx; }
int a2_get_p1(void){ return cur_p1; }
int a2_p1_count(void){ return 17; }
int a2_p1_record(int i){ return P1ID[i]; }
static void (*look(const FuncEntry*t,uint32_t a))(CPU*){ for(const FuncEntry*f=t;f->fn;f++){ if(f->addr==a) return f->fn; if(f->addr>a) break; } return 0; }
void a2_ov_check(uint32_t a) __attribute__((weak));
void (*a2_find(uint32_t a))(CPU*){
    a=0x80000000u|(a&0x1fffffffu);
    if(a>=0x80084A98u && a<0x801CA000u && a2_ov_check) a2_ov_check(a);
    void(*f)(CPU*)=look(main_functions,a); if(f) return f;
    if(a<0x80017000u){ int v=a2_al_variant(); static int lv=-1; if(v!=lv){ lv=v; if(getenv("A2ALLOG")) fprintf(stderr,"[a2pc] AL22 variant %d (PACK0 record %d)\n",v,76+v); } return look(AL[v],a); }
    if(a>=0x801493B0u){ if(cur_p1>=0) return look(P1[cur_p1],a); return 0; }
    if(cur_ov>=0) return look(OV[cur_ov],a);
    return 0; }
void ov_call(CPU*c,uint32_t a){ void(*f)(CPU*)=a2_find(a); if(f) f(c); else c_missing(c,a); }
