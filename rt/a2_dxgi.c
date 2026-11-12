/* Windows: the picture drawn by OpenGL is shown through a Direct3D 11 / DXGI flip swap chain (WGL_NV_DX_interop2),
   so the game window is composed by Windows like any other window: no exclusive full screen, HDR desktops stay correct. */

#ifdef _WIN32
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>

typedef HANDLE (WINAPI *P_Open)(void*);
typedef BOOL (WINAPI *P_Close)(HANDLE);
typedef HANDLE (WINAPI *P_Reg)(HANDLE,void*,unsigned,unsigned,unsigned);
typedef BOOL (WINAPI *P_Unreg)(HANDLE,HANDLE);
typedef BOOL (WINAPI *P_Lock)(HANDLE,int,HANDLE*);
typedef HRESULT (WINAPI *P_Create)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,const DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);
typedef BOOL (WINAPI *P_Rect)(HWND,RECT*);

extern unsigned gpu_gl_new_texture(void); extern unsigned gpu_gl_fbo_for(unsigned tex); extern void gpu_gl_delete(unsigned tex,unsigned fbo);

static const GUID A2_IID_Tex2D   = {0x6f15aaf2,0xd208,0x4e89,{0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c}};
static const GUID A2_IID_Factory = {0x7b7166ec,0x21c7,0x44ae,{0xb2,0x1a,0xc9,0xae,0x32,0x1a,0xe3,0x69}};
#define GL_TEXTURE_2D_ 0x0DE1
#define WGL_ACCESS_WRITE_DISCARD_NV_ 0x0002

static P_Open dxOpen; static P_Close dxClose; static P_Reg dxReg; static P_Unreg dxUnreg; static P_Lock dxLock, dxUnlock; static P_Rect getrect;
static HWND hwnd; static ID3D11Device*dev; static ID3D11DeviceContext*ctx; static IDXGISwapChain*sc; static ID3D11Texture2D*tex;
static HANDLE hdev, hobj; static unsigned gltex, glfbo; static int cw, ch, locked, flipmodel, fails;

static void drop_surface(void){ if(hobj){ dxUnreg(hdev,hobj); hobj=0; } if(glfbo||gltex){ gpu_gl_delete(gltex,glfbo); gltex=glfbo=0; } if(tex){ ID3D11Texture2D_Release(tex); tex=0; } }
static int make_surface(int w,int h){
    D3D11_TEXTURE2D_DESC td; ZeroMemory(&td,sizeof td); td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1;
    td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(ID3D11Device_CreateTexture2D(dev,&td,0,&tex))){ tex=0; return 0; }
    gltex=gpu_gl_new_texture(); hobj=dxReg(hdev,tex,gltex,GL_TEXTURE_2D_,WGL_ACCESS_WRITE_DISCARD_NV_); if(!hobj) return 0;
    if(!dxLock(hdev,1,&hobj)) return 0; glfbo=gpu_gl_fbo_for(gltex); dxUnlock(hdev,1,&hobj); if(!glfbo) return 0;
    cw=w; ch=h; return 1; }
void a2_dx_shutdown(void){ if(locked&&hobj){ dxUnlock(hdev,1,&hobj); locked=0; } drop_surface(); if(hdev){ dxClose(hdev); hdev=0; }
    if(sc){ IDXGISwapChain_Release(sc); sc=0; } if(ctx){ ID3D11DeviceContext_Release(ctx); ctx=0; } if(dev){ ID3D11Device_Release(dev); dev=0; } }
int a2_dx_size(int*w,int*h){ RECT r; if(!getrect||!getrect(hwnd,&r)) return 0; *w=r.right-r.left; *h=r.bottom-r.top; return 1; }

int a2_dx_init(void*window,void*(*getproc)(const char*)){
    if(getenv("A2DXGI")&&!atoi(getenv("A2DXGI"))) return 0;
    hwnd=(HWND)window; if(!hwnd) return 0;
    dxOpen=(P_Open)getproc("wglDXOpenDeviceNV"); dxClose=(P_Close)getproc("wglDXCloseDeviceNV"); dxReg=(P_Reg)getproc("wglDXRegisterObjectNV");
    dxUnreg=(P_Unreg)getproc("wglDXUnregisterObjectNV"); dxLock=(P_Lock)getproc("wglDXLockObjectsNV"); dxUnlock=(P_Lock)getproc("wglDXUnlockObjectsNV");
    if(!dxOpen||!dxClose||!dxReg||!dxUnreg||!dxLock||!dxUnlock){ fprintf(stderr,"[a2pc] DXGI output: the OpenGL driver has no WGL_NV_DX_interop\n"); return 0; }
    HMODULE u=LoadLibraryA("user32.dll"); getrect=u?(P_Rect)GetProcAddress(u,"GetClientRect"):0; HMODULE d=LoadLibraryA("d3d11.dll"); P_Create create=d?(P_Create)GetProcAddress(d,"D3D11CreateDeviceAndSwapChain"):0;
    if(!getrect||!create) return 0;
    int w=640,h=480; a2_dx_size(&w,&h); if(w<1) w=1; if(h<1) h=1;
    static const int effects[3]={4,3,0};      /* flip discard (Windows 10), flip sequential (8), old blit (7) */
    for(int i=0;i<3&&!sc;i++){
        DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd,sizeof sd); sd.BufferDesc.Width=w; sd.BufferDesc.Height=h; sd.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count=1;
        sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=effects[i]?3:1; sd.OutputWindow=hwnd; sd.Windowed=TRUE; sd.SwapEffect=(DXGI_SWAP_EFFECT)effects[i];
        if(FAILED(create(0,D3D_DRIVER_TYPE_HARDWARE,0,0,0,0,D3D11_SDK_VERSION,&sd,&sc,&dev,0,&ctx))){ sc=0; dev=0; ctx=0; } else flipmodel=effects[i]!=0; }
    if(!sc){ fprintf(stderr,"[a2pc] DXGI output: no Direct3D 11 swap chain\n"); return 0; }
    { IDXGIFactory*f=0; if(SUCCEEDED(IDXGISwapChain_GetParent(sc,&A2_IID_Factory,(void**)&f))&&f){ IDXGIFactory_MakeWindowAssociation(f,hwnd,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES); IDXGIFactory_Release(f); } }
    hdev=dxOpen(dev); if(!hdev){ fprintf(stderr,"[a2pc] DXGI output: the OpenGL driver cannot share with Direct3D 11\n"); a2_dx_shutdown(); return 0; }
    if(!make_surface(w,h)){ fprintf(stderr,"[a2pc] DXGI output: could not share a %dx%d picture\n",w,h); a2_dx_shutdown(); return 0; }
    fprintf(stderr,"[a2pc] picture shown through DXGI (%s swap chain)\n",flipmodel?"flip":"blit");
    return 1; }

/* lock the shared picture for OpenGL; *fbo is where to draw it */
int a2_dx_begin(int w,int h,unsigned*fbo){
    if(!sc||w<1||h<1) return 0;
    if(w!=cw||h!=ch){ drop_surface(); ID3D11DeviceContext_ClearState(ctx); ID3D11DeviceContext_Flush(ctx);
        if(FAILED(IDXGISwapChain_ResizeBuffers(sc,0,w,h,DXGI_FORMAT_UNKNOWN,0))){ if(fails++<3) fprintf(stderr,"[a2pc] DXGI output: resize to %dx%d failed\n",w,h); return 0; }
        if(!make_surface(w,h)){ if(fails++<3) fprintf(stderr,"[a2pc] DXGI output: could not share a %dx%d picture\n",w,h); drop_surface(); return 0; } }
    if(!dxLock(hdev,1,&hobj)){ if(fails++<3) fprintf(stderr,"[a2pc] DXGI output: lock failed\n"); return 0; }
    locked=1; *fbo=glfbo; return 1; }
/* hand the picture to Direct3D and show it */
void a2_dx_end(void){
    if(!locked) return; dxUnlock(hdev,1,&hobj); locked=0;
    ID3D11Texture2D*bb=0; if(SUCCEEDED(IDXGISwapChain_GetBuffer(sc,0,&A2_IID_Tex2D,(void**)&bb))&&bb){ ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)bb,(ID3D11Resource*)tex); ID3D11Texture2D_Release(bb); }
    HRESULT r=IDXGISwapChain_Present(sc,0,0); if(FAILED(r)&&fails++<3) fprintf(stderr,"[a2pc] DXGI output: present failed (%08lx)\n",(unsigned long)r); }
#else
int a2_dx_init(void*window,void*(*getproc)(const char*)){ (void)window; (void)getproc; return 0; }
int a2_dx_size(int*w,int*h){ (void)w; (void)h; return 0; }
int a2_dx_begin(int w,int h,unsigned*fbo){ (void)w; (void)h; (void)fbo; return 0; }
void a2_dx_end(void){}
void a2_dx_shutdown(void){}
#endif
