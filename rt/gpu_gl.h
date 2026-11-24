/* Interface between the graphics chip model (gpu.c) and the video card renderer (gpu_gl.c). */
#ifndef GPU_GL_H
#define GPU_GL_H
#include <stdint.h>
#include <stdio.h>
typedef struct { int x,y,r,g,b,u,v; } GLV;                 /* same layout as the vertex in gpu.c */
typedef struct { int dx0,dy0,dx1,dy1, tpx,tpy,tmode,semi, tww,twh,twx,twy, setmask,chkmask, filt; } GLSt;
typedef struct { int off, alt; double ox,oy,ow,oh; double sx,sy,sw,sh; } GLDisp;  /* output rectangle in 640x480 units, video memory rectangle */
extern int a2_gl;
int gpu_gl_init(void*(*getproc)(const char*),int scale);
int gpu_gl_scale(void);
void gpu_gl_hd_key(int dbg);
void gpu_gl_flush(void);
void gpu_gl_tri(const GLV*a,const GLV*b,const GLV*c,int textured,int gour,int raw,int tr,int clx,int cly,int mode,int sub,const GLSt*s);
int gpu_gl_target(int t);
void gpu_gl_rect(int x,int y,int w,int h,int r,int g,int b,int textured,int raw,int tr,int u0,int v0,int clx,int cly,const GLSt*s);
void gpu_gl_line(const GLV*a,const GLV*b,int gour,int tr,const GLSt*s);
void gpu_gl_fill(int x,int y,int w,int h,int r,int g,int b);
void gpu_gl_upload(const uint16_t*vram,int x,int y,int w,int h);
void gpu_gl_copy(int sx,int sy,int dx,int dy,int w,int h);
void gpu_gl_readback(uint16_t*vram,int x,int y,int w,int h);
void gpu_gl_output(unsigned fbo,int flip);
unsigned gpu_gl_new_texture(void);
unsigned gpu_gl_fbo_for(unsigned tex);
void gpu_gl_delete(unsigned tex,unsigned fbo);
void gpu_gl_overlay(const uint32_t*px,int w,int h,int x,int y,int dw,int dh);
void gpu_gl_present(const GLDisp*d,int winw,int winh,int logw,int smooth,const uint32_t*cpu_pic);
int gpu_gl_shot(const char*path,int w,int h);
void gpu_gl_stats(FILE*f);
#endif
