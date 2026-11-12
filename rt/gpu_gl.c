/* Video card renderer: the game's drawing commands are drawn by OpenGL 3.3 into a video memory kept on the video card. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "gpu_gl.h"

#ifdef _WIN32
#define GLAPI __stdcall
#else
#define GLAPI
#endif
typedef unsigned int GLenum, GLuint, GLbitfield; typedef int GLint, GLsizei; typedef float GLfloat; typedef unsigned char GLubyte, GLboolean; typedef char GLchar; typedef ptrdiff_t GLsizeiptr, GLintptr;

#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_BGRA 0x80E1
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_SCISSOR_TEST 0x0C11
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_ONE 1
#define GL_ZERO 0
#define GL_SRC1_ALPHA 0x8589
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_TRIANGLES 0x0004
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_VENDOR 0x1F00
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_PACK_ROW_LENGTH 0x0D02
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_PIXEL_PACK_BUFFER 0x88EB
#define GL_STREAM_READ 0x88E1
#define GL_MAP_READ_BIT 0x0001

#define GLF(X) \
 X(void,GetIntegerv,(GLenum,GLint*)) X(const GLubyte*,GetString,(GLenum)) X(GLenum,GetError,(void)) \
 X(void,Viewport,(GLint,GLint,GLsizei,GLsizei)) X(void,Scissor,(GLint,GLint,GLsizei,GLsizei)) X(void,Enable,(GLenum)) X(void,Disable,(GLenum)) \
 X(void,ClearColor,(GLfloat,GLfloat,GLfloat,GLfloat)) X(void,Clear,(GLbitfield)) X(void,BlendFuncSeparate,(GLenum,GLenum,GLenum,GLenum)) X(void,BlendEquationSeparate,(GLenum,GLenum)) \
 X(void,GenTextures,(GLsizei,GLuint*)) X(void,BindTexture,(GLenum,GLuint)) X(void,TexImage2D,(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*)) \
 X(void,TexSubImage2D,(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void*)) X(void,TexParameteri,(GLenum,GLenum,GLint)) X(void,ActiveTexture,(GLenum)) \
 X(void,GenFramebuffers,(GLsizei,GLuint*)) X(void,BindFramebuffer,(GLenum,GLuint)) X(void,FramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint)) X(GLenum,CheckFramebufferStatus,(GLenum)) \
 X(void,BlitFramebuffer,(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum)) X(void,ReadPixels,(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*)) X(void,PixelStorei,(GLenum,GLint)) \
 X(GLuint,CreateShader,(GLenum)) X(void,ShaderSource,(GLuint,GLsizei,const GLchar*const*,const GLint*)) X(void,CompileShader,(GLuint)) X(void,GetShaderiv,(GLuint,GLenum,GLint*)) \
 X(void,GetShaderInfoLog,(GLuint,GLsizei,GLsizei*,GLchar*)) X(GLuint,CreateProgram,(void)) X(void,AttachShader,(GLuint,GLuint)) X(void,LinkProgram,(GLuint)) X(void,GetProgramiv,(GLuint,GLenum,GLint*)) \
 X(void,GetProgramInfoLog,(GLuint,GLsizei,GLsizei*,GLchar*)) X(void,UseProgram,(GLuint)) X(GLint,GetUniformLocation,(GLuint,const GLchar*)) X(void,Uniform1i,(GLint,GLint)) X(void,Uniform1f,(GLint,GLfloat)) \
 X(void,Uniform2f,(GLint,GLfloat,GLfloat)) X(void,Uniform4f,(GLint,GLfloat,GLfloat,GLfloat,GLfloat)) X(void,Uniform4i,(GLint,GLint,GLint,GLint,GLint)) X(void,GenVertexArrays,(GLsizei,GLuint*)) X(void,BindVertexArray,(GLuint)) \
 X(void,GenBuffers,(GLsizei,GLuint*)) X(void,BindBuffer,(GLenum,GLuint)) X(void,BufferData,(GLenum,GLsizeiptr,const void*,GLenum)) \
 X(void,VertexAttribPointer,(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*)) X(void,VertexAttribIPointer,(GLuint,GLint,GLenum,GLsizei,const void*)) X(void,EnableVertexAttribArray,(GLuint)) \
 X(void*,MapBufferRange,(GLenum,GLintptr,GLsizeiptr,GLbitfield)) X(GLboolean,UnmapBuffer,(GLenum)) X(void,DeleteTextures,(GLsizei,const GLuint*)) X(void,DeleteFramebuffers,(GLsizei,const GLuint*)) X(void,DrawArrays,(GLenum,GLint,GLsizei)) X(void,Finish,(void)) X(void,Flush,(void)) X(void,ColorMask,(GLboolean,GLboolean,GLboolean,GLboolean))
#define DECL(r,n,a) static r (GLAPI *gl##n)a;
GLF(DECL)

int a2_gl;                 /* 1 = the video card draws */
static int S=1, truecolor=1, glerr_shown;
static GLuint vtex, rtex, stex, dtex, otex, atex, vfbo, rfbo, sfbo, afbo, prog, dprog, vao, dvao, vbo, ptex, pfbo, pbo; static int pw_, ph_;
static GLint u_scale, u_pass, u_tw, u_quant, u_vram, d_tex, d_src, d_size, d_mode, d_flip;
static int cur_fbo=-1, cur_tex=-1, sc_on=-1, sc_box[4]={-1,-1,-1,-1}, bl_eq=-1;

typedef struct { float x,y,u,v; uint8_t r,g,b,a; uint16_t tp, clut; uint8_t box[4]; } GV;   /* 28 bytes */
static GV *vb; static int nv, vcap;
static int b_kind=-1, b_sc[4], b_tw=-1;          /* state of the batch being collected */
static uint8_t dirty[32][64]; static int ndirty;  /* 16x16 blocks of video memory drawn since the read copy was refreshed */
long gl_flushes, gl_syncs, gl_prims, gl_alt;
static int tgt;                                   /* 0 = video memory, 1 = the in-between picture of the frame-rate mod */

static const char *VS =
"#version 330 core\n"
"layout(location=0) in vec2 a_pos; layout(location=1) in vec2 a_uv; layout(location=2) in vec4 a_col; layout(location=3) in uvec2 a_tp; layout(location=4) in uvec4 a_box;\n"
"out vec4 v_col; noperspective out vec2 v_uv; flat out uvec2 v_tp; flat out uvec4 v_box;\n"
"void main(){ gl_Position=vec4(a_pos.x/512.0-1.0, a_pos.y/256.0-1.0, 0.0, 1.0); v_col=a_col; v_uv=a_uv; v_tp=a_tp; v_box=a_box; }\n";

static const char *FS =
"#version 330 core\n"
"in vec4 v_col; noperspective in vec2 v_uv; flat in uvec2 v_tp; flat in uvec4 v_box;\n"
"uniform sampler2D u_vram; uniform int u_scale; uniform int u_pass; uniform ivec4 u_tw; uniform int u_quant;\n"
"layout(location=0, index=0) out vec4 o_col; layout(location=0, index=1) out vec4 o_bl;\n"
"int tpx, tpy, tmode, clx, cly;\n"
"uint word16(vec4 t){ uvec4 c=uvec4(t*255.0+0.5); return (c.r>>3)|((c.g>>3)<<5)|((c.b>>3)<<10)|(c.a>127u?0x8000u:0u); }\n"
"vec4 texel(int u, int v, ivec2 sub){\n"
"  u&=255; v&=255; u=(u & ~(u_tw.x*8)) | ((u_tw.z & u_tw.x)*8); v=(v & ~(u_tw.y*8)) | ((u_tw.w & u_tw.y)*8);\n"
"  if(tmode==2) return texelFetch(u_vram, ivec2(((tpx+u)&1023)*u_scale+sub.x, ((tpy+v)&511)*u_scale+sub.y), 0);\n"
"  int wx = tmode==0 ? (u>>2) : (u>>1);\n"
"  uint w = word16(texelFetch(u_vram, ivec2(((tpx+wx)&1023)*u_scale, ((tpy+v)&511)*u_scale), 0));\n"
"  uint idx = tmode==0 ? ((w>>uint((u&3)*4))&15u) : ((w>>uint((u&1)*8))&255u);\n"
"  return texelFetch(u_vram, ivec2(((clx+int(idx))&1023)*u_scale, cly*u_scale), 0);\n"
"}\n"
"bool zero(vec4 t){ return t.r<7.5/255.0 && t.g<7.5/255.0 && t.b<7.5/255.0 && t.a<0.5; }\n"
"void main(){\n"
"  uint tp=v_tp.x; tpx=int(tp&15u)*64; tpy=int((tp>>4)&1u)*256; tmode=int((tp>>5)&3u); clx=int(v_tp.y&63u)*16; cly=int((v_tp.y>>6)&511u);\n"
"  bool textured=(tp&0x200u)!=0u, raw=(tp&0x400u)!=0u, tr=(tp&0x800u)!=0u, filt=(tp&0x2000u)!=0u;\n"
"  vec3 c=v_col.rgb; bool semi=tr;\n"
"  if(textured){\n"
"    vec2 uv=v_uv+vec2(1.0/1024.0); vec4 t;\n"
"    if(filt){ vec2 f=uv-0.5; ivec2 i0=ivec2(floor(f)); vec2 a=f-vec2(i0); ivec2 i1=i0+1;\n"
"      ivec2 lo=ivec2(v_box.xy), hi=ivec2(v_box.zw); ivec2 j0=clamp(i0,lo,hi), j1=clamp(i1,lo,hi);\n"
"      vec4 t00=texel(j0.x,j0.y,ivec2(0)), t10=texel(j1.x,j0.y,ivec2(0)), t01=texel(j0.x,j1.y,ivec2(0)), t11=texel(j1.x,j1.y,ivec2(0));\n"
"      if(zero(t00)||zero(t10)||zero(t01)||zero(t11)) t=texel(int(floor(uv.x)),int(floor(uv.y)),ivec2(0));\n"
"      else { t=mix(mix(t00,t10,a.x),mix(t01,t11,a.x),a.y); t.a=t00.a; } }\n"
"    else { ivec2 sub = tmode==2 ? ivec2(clamp(fract(uv)*float(u_scale),vec2(0.0),vec2(float(u_scale)-0.5))) : ivec2(0); t=texel(int(floor(uv.x)),int(floor(uv.y)),sub); }\n"
"    if(zero(t)) discard;\n"
"    vec3 tc=t.rgb; if(!raw) tc=min(tc*(floor(c*255.0+0.5)/128.0),vec3(1.0));\n"
"    c=tc; semi=tr && t.a>0.5;\n"
"  }\n"
"  if(u_quant!=0) c=floor(c*255.0/8.0+0.001)*(8.0/255.0);\n"
"  if(u_pass==1 && semi) discard; if(u_pass==2 && !semi) discard;\n"
"  uint sm=(tp>>7)&3u; float fs=1.0, fd=0.0;\n"
"  if(semi){ if(sm==0u){ fs=0.5; fd=0.5; } else if(sm==3u){ fs=0.25; fd=1.0; } else { fs=1.0; fd=1.0; } }\n"
"  o_col=vec4(c*fs, (tp&0x1000u)!=0u ? 1.0 : 0.0); o_bl=vec4(fd);\n"
"}\n";

/* picture shown in the window: a rectangle of the video memory, nearest / smooth with whole-pixel blocks / plain smooth */
static const char *DVS =
"#version 330 core\n"
"layout(location=0) in vec2 a_pos; out vec2 v_t; uniform int u_flip;\n"
"void main(){ gl_Position=vec4(a_pos*2.0-1.0,0.0,1.0); v_t=vec2(a_pos.x, u_flip!=0 ? 1.0-a_pos.y : a_pos.y); }\n";
static const char *DFS =
"#version 330 core\n"
"in vec2 v_t; out vec4 o; uniform sampler2D u_tex; uniform vec4 u_src; uniform vec4 u_size; uniform int u_mode;\n"
"void main(){ vec2 ts=vec2(textureSize(u_tex,0)); vec2 texel=u_src.xy+v_t*u_src.zw;\n"
"  if(u_mode==0){ o=vec4(texelFetch(u_tex, ivec2(floor(texel)), 0).rgb,1.0); return; }\n"
"  if(u_mode==1){ vec2 sc=max(floor(u_size.zw/abs(u_src.zw)),vec2(1.0)); vec2 fl=floor(texel); vec2 s=fract(texel); vec2 rr=0.5-0.5/sc; vec2 cd=s-0.5; vec2 f=(cd-clamp(cd,-rr,rr))*sc+0.5; texel=fl+f; }\n"
"  o=vec4(texture(u_tex, texel/ts).rgb,1.0); }\n";

static GLuint shader(GLenum t,const char*src){ GLuint s=glCreateShader(t); glShaderSource(s,1,&src,0); glCompileShader(s); GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[2048]; glGetShaderInfoLog(s,sizeof log,0,log); fprintf(stderr,"[a2pc] video card: shader error:\n%s\n",log); return 0; } return s; }
static GLuint program(const char*vs,const char*fs){ GLuint a=shader(GL_VERTEX_SHADER,vs), b=shader(GL_FRAGMENT_SHADER,fs); if(!a||!b) return 0; GLuint p=glCreateProgram(); glAttachShader(p,a); glAttachShader(p,b); glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok); if(!ok){ char log[2048]; glGetProgramInfoLog(p,sizeof log,0,log); fprintf(stderr,"[a2pc] video card: program error:\n%s\n",log); return 0; } return p; }
static GLuint mktex(int w,int h,int filter){ GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE); cur_tex=-1; return t; }
static GLuint mkfbo(GLuint t){ GLuint f; glGenFramebuffers(1,&f); glBindFramebuffer(GL_FRAMEBUFFER,f); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return 0; glClearColor(0,0,0,0); glDisable(GL_SCISSOR_TEST); glClear(GL_COLOR_BUFFER_BIT); cur_fbo=-1; sc_on=-1; return f; }
static void bind_fbo(GLuint f){ if(cur_fbo!=(int)f){ glBindFramebuffer(GL_FRAMEBUFFER,f); cur_fbo=(int)f; } }
static void scissor(int on,int x,int y,int w,int h){ if(on!=sc_on){ if(on) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST); sc_on=on; }
    if(on&&(x!=sc_box[0]||y!=sc_box[1]||w!=sc_box[2]||h!=sc_box[3])){ glScissor(x,y,w,h); sc_box[0]=x; sc_box[1]=y; sc_box[2]=w; sc_box[3]=h; } }
static void check_err(const char*where){ GLenum e=glGetError(); if(e&&glerr_shown<8){ glerr_shown++; fprintf(stderr,"[a2pc] video card: GL error 0x%x at %s\n",e,where); } }

int gpu_gl_scale(void){ return S; }
int gpu_gl_init(void*(*getproc)(const char*),int scale){
#define LOAD(r,n,a) *(void**)&gl##n=getproc("gl" #n); if(!gl##n){ fprintf(stderr,"[a2pc] video card: OpenGL function gl%s is missing\n",#n); return 0; }
    GLF(LOAD)
    const char*ver=(const char*)glGetString(GL_VERSION); const char*ren=(const char*)glGetString(GL_RENDERER);
    fprintf(stderr,"[a2pc] video card: %s | OpenGL %s\n",ren?ren:"?",ver?ver:"?");
    GLint mx=0; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&mx); S=scale<1?1:scale>8?8:scale; while(S>1&&1024*S>mx) S--;
    { const char*e=getenv("A2GLTRUECOLOR"); truecolor=e?atoi(e):1; }
    prog=program(VS,FS); dprog=program(DVS,DFS); if(!prog||!dprog) return 0;
    glUseProgram(prog); u_scale=glGetUniformLocation(prog,"u_scale"); u_pass=glGetUniformLocation(prog,"u_pass"); u_tw=glGetUniformLocation(prog,"u_tw"); u_quant=glGetUniformLocation(prog,"u_quant"); u_vram=glGetUniformLocation(prog,"u_vram");
    glUniform1i(u_vram,0); glUniform1i(u_scale,S); glUniform1i(u_quant,!truecolor); glUniform4i(u_tw,0,0,0,0);
    glUseProgram(dprog); d_tex=glGetUniformLocation(dprog,"u_tex"); d_src=glGetUniformLocation(dprog,"u_src"); d_size=glGetUniformLocation(dprog,"u_size"); d_mode=glGetUniformLocation(dprog,"u_mode"); d_flip=glGetUniformLocation(dprog,"u_flip"); glUniform1i(d_tex,0);
    vtex=mktex(1024*S,512*S,GL_LINEAR); rtex=mktex(1024*S,512*S,GL_NEAREST); stex=mktex(1024,512,GL_NEAREST); dtex=mktex(640,480,GL_LINEAR); otex=mktex(64,32,GL_NEAREST);
    vfbo=mkfbo(vtex); rfbo=mkfbo(rtex); sfbo=mkfbo(stex); if(!vfbo||!rfbo||!sfbo){ fprintf(stderr,"[a2pc] video card: could not make a %dx%d drawing surface\n",1024*S,512*S); return 0; }
    glGenVertexArrays(1,&dvao); glGenVertexArrays(1,&vao); glBindVertexArray(vao); glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glPixelStorei(GL_UNPACK_ALIGNMENT,1); glPixelStorei(GL_PACK_ALIGNMENT,1);
    vcap=65536; vb=(GV*)malloc(sizeof(GV)*vcap); if(!vb) return 0;
    check_err("init"); if(glerr_shown) return 0;
    a2_gl=1; fprintf(stderr,"[a2pc] video card draws the game: internal resolution %dx (%dx%d), %s colour\n",S,1024*S,512*S,truecolor?"full":"15-bit");
    return 1; }

static void attribs(void){
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glVertexAttribPointer(0,2,GL_FLOAT,0,sizeof(GV),(void*)offsetof(GV,x)); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,0,sizeof(GV),(void*)offsetof(GV,u)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,1,sizeof(GV),(void*)offsetof(GV,r)); glEnableVertexAttribArray(2);
    glVertexAttribIPointer(3,2,GL_UNSIGNED_SHORT,sizeof(GV),(void*)offsetof(GV,tp)); glEnableVertexAttribArray(3);
    glVertexAttribIPointer(4,4,GL_UNSIGNED_BYTE,sizeof(GV),(void*)offsetof(GV,box)); glEnableVertexAttribArray(4); }

/* draw what has been collected */
void gpu_gl_flush(void){
    if(!a2_gl||!nv) return; gl_flushes++;
    bind_fbo(tgt?afbo:vfbo); glViewport(0,0,1024*S,512*S); glUseProgram(prog); glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0); if(cur_tex!=(int)rtex){ glBindTexture(GL_TEXTURE_2D,rtex); cur_tex=(int)rtex; }
    scissor(1,b_sc[0]*S,b_sc[1]*S,(b_sc[2]-b_sc[0]+1)*S,(b_sc[3]-b_sc[1]+1)*S);
    glUniform4i(u_tw,b_tw&31,(b_tw>>5)&31,(b_tw>>10)&31,(b_tw>>15)&31);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)nv*sizeof(GV),vb,GL_STREAM_DRAW); attribs();
    glEnable(GL_BLEND); glBlendFuncSeparate(GL_ONE,GL_SRC1_ALPHA,GL_ONE,GL_ZERO);
    if(b_kind==0){ if(bl_eq!=0){ glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD); bl_eq=0; } glUniform1i(u_pass,0); glDrawArrays(GL_TRIANGLES,0,nv); }
    else { if(bl_eq!=0){ glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD); bl_eq=0; } glUniform1i(u_pass,1); glDrawArrays(GL_TRIANGLES,0,nv);
           glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT,GL_FUNC_ADD); bl_eq=1; glUniform1i(u_pass,2); glDrawArrays(GL_TRIANGLES,0,nv); }
    glDisable(GL_BLEND); nv=0; check_err("draw"); }

/* bring the read copy up to date where the game has drawn */
static void sync_read(void){
    if(!ndirty) return; gpu_gl_flush(); int x0=64,y0=32,x1=-1,y1=-1;
    for(int y=0;y<32;y++) for(int x=0;x<64;x++) if(dirty[y][x]){ if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y; }
    memset(dirty,0,sizeof dirty); ndirty=0; if(x1<0) return; gl_syncs++;
    scissor(0,0,0,0,0); glBindFramebuffer(GL_READ_FRAMEBUFFER,vfbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,rfbo);
    int X0=x0*16*S, Y0=y0*16*S, X1=(x1+1)*16*S, Y1=(y1+1)*16*S; glBlitFramebuffer(X0,Y0,X1,Y1,X0,Y0,X1,Y1,GL_COLOR_BUFFER_BIT,GL_NEAREST); cur_fbo=-1; }
static void mark(int x0,int y0,int x1,int y1){ if(tgt) return; if(x0<0)x0=0; if(y0<0)y0=0; if(x1>1023)x1=1023; if(y1>511)y1=511; if(x0>x1||y0>y1) return;
    for(int y=y0>>4;y<=y1>>4;y++) for(int x=x0>>4;x<=x1>>4;x++) if(!dirty[y][x]){ dirty[y][x]=1; ndirty++; } }
static int hit(int x0,int y0,int w,int h){ if(!ndirty) return 0; for(int j=0;j<h;j+=16-((y0+j)&15)){ int y=((y0+j)&511)>>4; for(int i=0;i<w;i+=16-((x0+i)&15)){ if(dirty[y][((x0+i)&1023)>>4]) return 1; } }
    { int y=((y0+h-1)&511)>>4; if(dirty[y][((x0+w-1)&1023)>>4]) return 1; } return 0; }
/* the texture page and palette this primitive reads */
static void tex_hazard(const GLSt*s,int mode,int clx,int cly,int umin,int vmin,int umax,int vmax){
    if(!ndirty) return; int px=s->tpx*64, py=s->tpy*256; int x0,w;
    if(umax>255||s->tww||s->twh){ umin=0; umax=255; } if(vmax>255||s->tww||s->twh){ vmin=0; vmax=255; }
    if(mode==0){ x0=px+umin/4; w=umax/4-umin/4+1; } else if(mode==1){ x0=px+umin/2; w=umax/2-umin/2+1; } else { x0=px+umin; w=umax-umin+1; }
    int need=hit(x0,py+vmin,w,vmax-vmin+1); if(!need&&mode<2) need=hit(clx,cly,mode==0?16:256,1);
    if(need) sync_read(); }
static void batch_state(const GLSt*s,int kind){
    int tw=s->tww|(s->twh<<5)|(s->twx<<10)|(s->twy<<15);
    if(kind!=b_kind||s->dx0!=b_sc[0]||s->dy0!=b_sc[1]||s->dx1!=b_sc[2]||s->dy1!=b_sc[3]||tw!=b_tw){ gpu_gl_flush(); b_kind=kind; b_sc[0]=s->dx0; b_sc[1]=s->dy0; b_sc[2]=s->dx1; b_sc[3]=s->dy1; b_tw=tw; }
    if(nv+6>vcap) gpu_gl_flush(); }
static uint16_t tpbits(const GLSt*s,int mode,int textured,int raw,int tr,int filt){ return (uint16_t)(s->tpx|(s->tpy<<4)|(mode<<5)|(s->semi<<7)|(textured<<9)|(raw<<10)|(tr<<11)|(s->setmask<<12)|(filt<<13)); }
static void put(float x,float y,float u,float v,int r,int g,int b,uint16_t tp,uint16_t clut,const uint8_t*box){ GV*q=&vb[nv++]; q->x=x; q->y=y; q->u=u; q->v=v; q->r=(uint8_t)r; q->g=(uint8_t)g; q->b=(uint8_t)b; q->a=255; q->tp=tp; q->clut=clut; memcpy(q->box,box,4); }

void gpu_gl_tri(const GLV*a,const GLV*b,const GLV*c,int textured,int gour,int raw,int tr,int clx,int cly,int mode,int sub,const GLSt*s){
    if(s->dx1<s->dx0||s->dy1<s->dy0) return; gl_prims++;
    int umin=a->u,umax=a->u,vmin=a->v,vmax=a->v; const GLV*q[2]={b,c}; for(int i=0;i<2;i++){ if(q[i]->u<umin)umin=q[i]->u; if(q[i]->u>umax)umax=q[i]->u; if(q[i]->v<vmin)vmin=q[i]->v; if(q[i]->v>vmax)vmax=q[i]->v; }
    if(textured) tex_hazard(s,mode,clx,cly,umin,vmin,umax,vmax);
    batch_state(s,(tr&&s->semi==2)?1:0);
    uint16_t tp=tpbits(s,mode,textured,raw,tr,textured&&s->filt), cl=(uint16_t)((clx>>4)|(cly<<6)); uint8_t box[4]={(uint8_t)umin,(uint8_t)vmin,(uint8_t)umax,(uint8_t)vmax};
    const GLV*v[3]={a,b,c};
    const float k=1.0f/(float)(1<<sub);
    for(int i=0;i<3;i++){ const GLV*p=gour?v[i]:a; put(v[i]->x*k+0.5f,v[i]->y*k+0.5f,(float)v[i]->u,(float)v[i]->v,p->r,p->g,p->b,tp,cl,box); }
    int mnx=a->x,mxx=a->x,mny=a->y,mxy=a->y; for(int i=0;i<2;i++){ if(q[i]->x<mnx)mnx=q[i]->x; if(q[i]->x>mxx)mxx=q[i]->x; if(q[i]->y<mny)mny=q[i]->y; if(q[i]->y>mxy)mxy=q[i]->y; }
    mnx>>=sub; mxx=(mxx>>sub)+1; mny>>=sub; mxy=(mxy>>sub)+1;
    if(mnx<s->dx0)mnx=s->dx0; if(mxx>s->dx1)mxx=s->dx1; if(mny<s->dy0)mny=s->dy0; if(mxy>s->dy1)mxy=s->dy1; mark(mnx,mny,mxx,mxy); }

void gpu_gl_rect(int x,int y,int w,int h,int r,int g,int b,int textured,int raw,int tr,int u0,int v0,int clx,int cly,const GLSt*s){
    if(s->dx1<s->dx0||s->dy1<s->dy0||w<=0||h<=0) return; gl_prims++;
    if(textured) tex_hazard(s,s->tmode,clx,cly,u0,v0,u0+w-1,v0+h-1);
    batch_state(s,(tr&&s->semi==2)?1:0);
    uint16_t tp=tpbits(s,s->tmode,textured,raw,tr,0), cl=(uint16_t)((clx>>4)|(cly<<6)); uint8_t box[4]={0,0,255,255};
    float X0=(float)x, Y0=(float)y, X1=(float)(x+w), Y1=(float)(y+h), U0=(float)u0, V0=(float)v0, U1=(float)(u0+w), V1=(float)(v0+h);
    put(X0,Y0,U0,V0,r,g,b,tp,cl,box); put(X1,Y0,U1,V0,r,g,b,tp,cl,box); put(X0,Y1,U0,V1,r,g,b,tp,cl,box);
    put(X1,Y0,U1,V0,r,g,b,tp,cl,box); put(X1,Y1,U1,V1,r,g,b,tp,cl,box); put(X0,Y1,U0,V1,r,g,b,tp,cl,box);
    int x1=x+w-1,y1=y+h-1,x0=x,y0=y; if(x0<s->dx0)x0=s->dx0; if(x1>s->dx1)x1=s->dx1; if(y0<s->dy0)y0=s->dy0; if(y1>s->dy1)y1=s->dy1; mark(x0,y0,x1,y1); }

/* a line one game pixel wide, drawn as a thin quad */
void gpu_gl_line(const GLV*a,const GLV*b,int gour,int tr,const GLSt*s){
    if(s->dx1<s->dx0||s->dy1<s->dy0) return; gl_prims++;
    batch_state(s,(tr&&s->semi==2)?1:0);
    uint16_t tp=tpbits(s,0,0,0,tr,0); uint8_t box[4]={0,0,0,0}; const GLV*cb=gour?b:a;
    float ax=a->x+0.5f, ay=a->y+0.5f, bx=b->x+0.5f, by=b->y+0.5f; int dx=b->x-a->x, dy=b->y-a->y;
    float ox,oy; if(abs(dx)>=abs(dy)){ float sg=dx>=0?0.5f:-0.5f; ax-=sg; bx+=sg; ox=0; oy=0.5f; } else { float sg=dy>=0?0.5f:-0.5f; ay-=sg; by+=sg; ox=0.5f; oy=0; }
    put(ax-ox,ay-oy,0,0,a->r,a->g,a->b,tp,0,box); put(bx-ox,by-oy,0,0,cb->r,cb->g,cb->b,tp,0,box); put(ax+ox,ay+oy,0,0,a->r,a->g,a->b,tp,0,box);
    put(bx-ox,by-oy,0,0,cb->r,cb->g,cb->b,tp,0,box); put(bx+ox,by+oy,0,0,cb->r,cb->g,cb->b,tp,0,box); put(ax+ox,ay+oy,0,0,a->r,a->g,a->b,tp,0,box);
    int x0=a->x<b->x?a->x:b->x, x1=a->x>b->x?a->x:b->x, y0=a->y<b->y?a->y:b->y, y1=a->y>b->y?a->y:b->y;
    if(x0<s->dx0)x0=s->dx0; if(x1>s->dx1)x1=s->dx1; if(y0<s->dy0)y0=s->dy0; if(y1>s->dy1)y1=s->dy1; mark(x0,y0,x1,y1); }

/* GP0(02): fill a rectangle (not limited by the drawing area) */
void gpu_gl_fill(int x,int y,int w,int h,int r,int g,int b){
    gpu_gl_flush(); if(w<=0||h<=0) return; float R=(r>>3)*8/255.0f, G=(g>>3)*8/255.0f, B=(b>>3)*8/255.0f; glClearColor(R,G,B,0);
    for(int j=0;j<h;){ int yy=(y+j)&511, hh=h-j; if(yy+hh>512) hh=512-yy; for(int i=0;i<w;){ int xx=(x+i)&1023, ww=w-i; if(xx+ww>1024) ww=1024-xx;
            if(tgt){ bind_fbo(afbo); scissor(1,xx*S,yy*S,ww*S,hh*S); glClear(GL_COLOR_BUFFER_BIT); i+=ww; continue; }
            bind_fbo(vfbo); scissor(1,xx*S,yy*S,ww*S,hh*S); glClear(GL_COLOR_BUFFER_BIT); bind_fbo(rfbo); glClear(GL_COLOR_BUFFER_BIT); i+=ww; } j+=hh; } }

/* GP0(A0): the processor sent pixels; they are already in the processor copy of video memory */
void gpu_gl_upload(const uint16_t*vram,int x,int y,int w,int h){
    gpu_gl_flush(); static uint32_t buf[1024*512]; scissor(0,0,0,0,0);
    for(int j=0;j<h;){ int yy=(y+j)&511, hh=h-j; if(yy+hh>512) hh=512-yy; for(int i=0;i<w;){ int xx=(x+i)&1023, ww=w-i; if(xx+ww>1024) ww=1024-xx;
            for(int q=0;q<hh;q++){ const uint16_t*sr=&vram[(yy+q)*1024+xx]; uint32_t*d=&buf[q*ww]; for(int k=0;k<ww;k++){ uint16_t p=sr[k]; d[k]=((p&31)<<3)|(((p>>5)&31)<<11)|(((p>>10)&31)<<19)|((p&0x8000)?0xff000000u:0); } }
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,stex); cur_tex=(int)stex; glTexSubImage2D(GL_TEXTURE_2D,0,xx,yy,ww,hh,GL_RGBA,GL_UNSIGNED_BYTE,buf);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,sfbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,vfbo); glBlitFramebuffer(xx,yy,xx+ww,yy+hh,xx*S,yy*S,(xx+ww)*S,(yy+hh)*S,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,rfbo); glBlitFramebuffer(xx,yy,xx+ww,yy+hh,xx*S,yy*S,(xx+ww)*S,(yy+hh)*S,GL_COLOR_BUFFER_BIT,GL_NEAREST); cur_fbo=-1;
            i+=ww; } j+=hh; } check_err("upload"); }

/* GP0(80): copy a rectangle inside video memory */
void gpu_gl_copy(int sx,int sy,int dx,int dy,int w,int h){
    gpu_gl_flush(); if(hit(sx,sy,w,h)) sync_read(); scissor(0,0,0,0,0); glBindFramebuffer(GL_READ_FRAMEBUFFER,rfbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,vfbo); cur_fbo=-1;
    for(int j=0;j<h;){ int ys=(sy+j)&511, yd=(dy+j)&511, hh=h-j; if(ys+hh>512) hh=512-ys; if(yd+hh>512) hh=512-yd;
        for(int i=0;i<w;){ int xs=(sx+i)&1023, xd=(dx+i)&1023, ww=w-i; if(xs+ww>1024) ww=1024-xs; if(xd+ww>1024) ww=1024-xd;
            glBlitFramebuffer(xs*S,ys*S,(xs+ww)*S,(ys+hh)*S,xd*S,yd*S,(xd+ww)*S,(yd+hh)*S,GL_COLOR_BUFFER_BIT,GL_NEAREST); mark(xd,yd,xd+ww-1,yd+hh-1); i+=ww; } j+=hh; } }

/* video memory from the video card into the processor copy (GP0(C0) reads, dumps) */
void gpu_gl_readback(uint16_t*vram,int x,int y,int w,int h){
    if(!a2_gl) return; gpu_gl_flush(); scissor(0,0,0,0,0); glBindBuffer(GL_PIXEL_PACK_BUFFER,0); static uint32_t buf[1024*512];
    for(int j=0;j<h;){ int yy=(y+j)&511, hh=h-j; if(yy+hh>512) hh=512-yy; for(int i=0;i<w;){ int xx=(x+i)&1023, ww=w-i; if(xx+ww>1024) ww=1024-xx;
            glBindFramebuffer(GL_READ_FRAMEBUFFER,vfbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,sfbo); glBlitFramebuffer(xx*S,yy*S,(xx+ww)*S,(yy+hh)*S,xx,yy,xx+ww,yy+hh,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,sfbo); glReadPixels(xx,yy,ww,hh,GL_RGBA,GL_UNSIGNED_BYTE,buf); cur_fbo=-1;
            for(int q=0;q<hh;q++){ uint16_t*d=&vram[(yy+q)*1024+xx]; const uint32_t*sr=&buf[q*ww]; for(int k=0;k<ww;k++){ uint32_t p=sr[k]; d[k]=(uint16_t)(((p>>3)&31)|(((p>>11)&31)<<5)|(((p>>19)&31)<<10)|((p>>31)?0x8000:0)); } }
            i+=ww; } j+=hh; } check_err("readback"); }

/* the frame-rate mod draws its in-between pictures into a second surface the size of video memory */
int gpu_gl_target(int t){ if(!a2_gl) return 0; gpu_gl_flush(); if(t&&!afbo){ atex=mktex(1024*S,512*S,GL_LINEAR); afbo=mkfbo(atex); if(!afbo) return 0; } if(t){ sync_read(); gl_alt++; } tgt=t; b_kind=-1; return 1; }

/* the window picture. It goes to the window's own surface, or (Windows, DXGI) into a surface shared with Direct3D, which keeps rows top-down */
static GLuint out_fbo, wtex, wfbo; static int out_flip, ww_, wh_, ov_on, ov_x, ov_y, ov_w, ov_h, ov_tw, ov_th;
void gpu_gl_output(unsigned fbo,int flip){ out_fbo=fbo; out_flip=flip; }
unsigned gpu_gl_new_texture(void){ GLuint t; glGenTextures(1,&t); cur_tex=-1; return t; }
unsigned gpu_gl_fbo_for(unsigned tex){ GLuint f; glGenFramebuffers(1,&f); glBindFramebuffer(GL_FRAMEBUFFER,f); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0); cur_fbo=-1;
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){ glBindFramebuffer(GL_FRAMEBUFFER,0); return 0; } glBindFramebuffer(GL_FRAMEBUFFER,0); return f; }
void gpu_gl_delete(unsigned tex,unsigned fbo){ if(fbo) glDeleteFramebuffers(1,&fbo); if(tex) glDeleteTextures(1,&tex); cur_fbo=-1; cur_tex=-1; }
void gpu_gl_overlay(const uint32_t*px,int w,int h,int x,int y,int dw,int dh){ if(!a2_gl) return; glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,otex); cur_tex=(int)otex; glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_BGRA,GL_UNSIGNED_BYTE,px); ov_on=1; ov_x=x; ov_y=y; ov_w=dw; ov_h=dh; ov_tw=w; ov_th=h; }
static void dquad(GLuint t,float sx,float sy,float sw,float sh,int mode,int flip,int vx,int vy,int vw,int vh){
    static const float q[12]={0,0,1,0,0,1, 1,0,1,1,0,1}; glBindVertexArray(dvao); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof q,q,GL_STREAM_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,0,8,0); glEnableVertexAttribArray(0);
    glViewport(vx,vy,vw,vh); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,t); cur_tex=(int)t;
    glUniform4f(d_src,sx,sy,sw,sh); glUniform4f(d_size,0,0,(float)vw,(float)vh); glUniform1i(d_mode,mode); glUniform1i(d_flip,flip); glDrawArrays(GL_TRIANGLES,0,6); }
void gpu_gl_present(const GLDisp*d,int winw,int winh,int logw,int smooth,const uint32_t*cpu_pic){
    if(!a2_gl||winw<1||winh<1) return; gpu_gl_flush(); scissor(0,0,0,0,0); glDisable(GL_BLEND);
    GLuint dst=out_fbo; if(out_flip){ if(winw!=ww_||winh!=wh_){ if(wfbo) gpu_gl_delete(wtex,wfbo); wtex=mktex(winw,winh,GL_NEAREST); wfbo=mkfbo(wtex); ww_=winw; wh_=winh; } dst=wfbo; }
    bind_fbo(dst); glViewport(0,0,winw,winh); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    double sc=(double)winw/logw; if((double)winh/480<sc) sc=(double)winh/480; int vw=(int)(logw*sc+0.5), vh=(int)(480*sc+0.5), vx=(winw-vw)/2, vy=(winh-vh)/2;
    glUseProgram(dprog);
    if(cpu_pic){ glBindTexture(GL_TEXTURE_2D,dtex); cur_tex=(int)dtex; glTexSubImage2D(GL_TEXTURE_2D,0,0,0,640,480,GL_BGRA,GL_UNSIGNED_BYTE,cpu_pic); dquad(dtex,0,0,640,480,smooth?1:0,1,vx,vy,vw,vh); }
    else if(!d->off&&d->ow>0&&d->oh>0){
        double kx=(double)vw/640, ky=(double)vh/480; int px=vx+(int)(d->ox*kx+0.5), pw=(int)((d->ox+d->ow)*kx+0.5)-(int)(d->ox*kx+0.5), ph=(int)((d->oy+d->oh)*ky+0.5)-(int)(d->oy*ky+0.5), py=vy+vh-(int)((d->oy+d->oh)*ky+0.5);
        int mode=smooth?1:0; if(pw<d->sw*S*0.999) mode=2;     /* 6x/8x shrunk to the window: plain smooth sampling */
        dquad(d->alt?atex:vtex,(float)(d->sx*S),(float)((d->sy+d->sh)*S),(float)(d->sw*S),(float)(-d->sh*S),mode,0,px,py,pw,ph); }
    if(ov_on){ int px=vx+(int)(ov_x*(double)vw/640), py=vy+vh-(int)((ov_y+ov_h)*(double)vh/480); dquad(otex,0,(float)ov_th,(float)ov_tw,(float)-ov_th,0,0,px,py,(int)(ov_w*(double)vw/640),(int)(ov_h*(double)vh/480)); ov_on=0; }
    if(out_flip){ glBindFramebuffer(GL_READ_FRAMEBUFFER,wfbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,out_fbo); glBlitFramebuffer(0,0,winw,winh,0,winh,winw,0,GL_COLOR_BUFFER_BIT,GL_NEAREST); cur_fbo=-1; }
    check_err("present"); }
int gpu_gl_shot(const char*path,int w,int h){ uint32_t*px=(uint32_t*)malloc((size_t)w*h*4); if(!px) return 0; bind_fbo(out_flip?wfbo:out_fbo); glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,px);
    FILE*f=fopen(path,"wb"); if(f){ fprintf(f,"P6\n%d %d\n255\n",w,h); for(int y=h-1;y>=0;y--) for(int x=0;x<w;x++){ uint32_t p=px[(size_t)y*w+x]; fputc(p&255,f); fputc((p>>8)&255,f); fputc((p>>16)&255,f); } fclose(f); } free(px); return 1; }
void gpu_gl_stats(FILE*f){ if(a2_gl) fprintf(f,"video card: %ld primitives, %ld draw calls, %ld read-copy refreshes, %ld in-between pictures\n",gl_prims,gl_flushes,gl_syncs,gl_alt); }
