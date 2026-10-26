/* What the translated code needs from the runtime: CPU state, memory access and dispatch. */

#ifndef A2RT_H
#define A2RT_H
#include <stdint.h>
typedef struct CPU { uint32_t r[32], hi, lo, pc; uint32_t gte_d[32], gte_c[32]; uint32_t cop0[32]; } CPU;
typedef struct { uint32_t addr; void (*fn)(CPU*); } FuncEntry;
#ifdef A2_STANDALONE
#define a2_tick(c,at) ((void)0)
#else
void a2_tick(CPU*c,uint32_t at);
#endif
void ov_call(CPU*c,uint32_t addr);
void (*a2_find(uint32_t addr))(CPU*);
int a2_get_overlay(void);
void a2_set_p1(int idx); int a2_get_p1(void); int a2_p1_count(void); int a2_p1_record(int i);
void a2_set_overlay(int id);
extern uint8_t  rd8(uint32_t a);  extern uint16_t rd16(uint32_t a); extern uint32_t rd32(uint32_t a);
extern void wr8(uint32_t a,uint32_t v); extern void wr16(uint32_t a,uint32_t v); extern void wr32(uint32_t a,uint32_t v);

static inline uint32_t lwl(uint32_t old,uint32_t a){uint32_t w=rd32(a&~3u);switch(a&3){case 0:return (old&0x00FFFFFF)|(w<<24);case 1:return (old&0x0000FFFF)|(w<<16);case 2:return (old&0x000000FF)|(w<<8);default:return w;}}
static inline uint32_t lwr(uint32_t old,uint32_t a){uint32_t w=rd32(a&~3u);switch(a&3){case 0:return w;case 1:return (old&0xFF000000)|(w>>8);case 2:return (old&0xFFFF0000)|(w>>16);default:return (old&0xFFFFFF00)|(w>>24);}}
static inline void swl(uint32_t a,uint32_t v){uint32_t b=a&~3u;switch(a&3){case 0:wr8(b,v>>24);break;case 1:wr8(b,v>>16);wr8(b+1,v>>24);break;case 2:wr8(b,v>>8);wr8(b+1,v>>16);wr8(b+2,v>>24);break;default:wr32(b,v);}}
static inline void swr(uint32_t a,uint32_t v){uint32_t b=a&~3u;switch(a&3){case 0:wr32(b,v);break;case 1:wr8(b+1,v);wr8(b+2,v>>8);wr8(b+3,v>>16);break;case 2:wr8(b+2,v);wr8(b+3,v>>8);break;default:wr8(b+3,v);}}
extern void c_jump(CPU*c,uint32_t target);
extern void c_call(CPU*c,uint32_t target);
extern void c_bios(CPU*c,uint32_t table,uint32_t number);
extern void c_missing(CPU*c,uint32_t addr);
extern void c_syscall(CPU*c,uint32_t code);  extern void c_break(CPU*c,uint32_t code);
extern void gte_cmd(CPU*c,uint32_t cmd);
extern uint32_t gte_mfc2(CPU*c,int r); extern uint32_t gte_cfc2(CPU*c,int r); extern void gte_mtc2(CPU*c,int r,uint32_t v); extern void gte_ctc2(CPU*c,int r,uint32_t v);
extern uint32_t cop0_mfc0(CPU*c,int r); extern void cop0_mtc0(CPU*c,int r,uint32_t v); extern void cop0_rfe(CPU*c);
#endif
