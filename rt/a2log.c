/* Problem log: nothing is written during normal play, a report appears on the Desktop only when the game runs into trouble. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#ifndef _WIN32
extern char**environ;
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
static FILE*lf; static int lf_tried; static char lf_path[1024];
#define NRING 24
static char ring[NRING][420]; static int ring_n; static void(*ctx_cb)(char*,int);
void a2_set_ctx(void(*f)(char*,int)){ ctx_cb=f; }
static void open_log(void){
    if(lf_tried) return; lf_tried=1; char dir[900]="", sub[1000];
#ifdef _WIN32
    { HMODULE sh=LoadLibraryA("shell32.dll"); typedef HRESULT(WINAPI*F)(HWND,int,HANDLE,DWORD,char*); F f=sh?(F)GetProcAddress(sh,"SHGetFolderPathA"):0; if(f){ char b[MAX_PATH]; if(f(0,0x10,0,0,b)==0) snprintf(dir,sizeof dir,"%s",b); }
      if(!dir[0]){ const char*u=getenv("USERPROFILE"); if(u) snprintf(dir,sizeof dir,"%s\\Desktop",u); } }
    if(!dir[0]) snprintf(dir,sizeof dir,".");
    snprintf(sub,sizeof sub,"%s\\alundra2logs",dir); CreateDirectoryA(sub,0);
#else
    { const char*h=getenv("HOME"); if(h){ snprintf(dir,sizeof dir,"%s/Desktop",h); struct stat st; if(stat(dir,&st)!=0) snprintf(dir,sizeof dir,"%s",h); } else snprintf(dir,sizeof dir,"."); }
    snprintf(sub,sizeof sub,"%s/alundra2logs",dir); mkdir(sub,0777);
#endif
    time_t t=time(0); struct tm*tm=localtime(&t); char ts[64]; strftime(ts,sizeof ts,"%Y-%m-%d_%H-%M-%S",tm);
    snprintf(lf_path,sizeof lf_path,"%s/alundra2_%s.txt",sub,ts); lf=fopen(lf_path,"w");
    if(lf){ fprintf(lf,"Alundra 2 PC - session log %s\n",ts);
        fprintf(lf,"Settings / mods (environment variables A2*):\n");
#ifdef _WIN32
        { char*blk=GetEnvironmentStringsA(); if(blk){ for(char*p=blk;*p;p+=strlen(p)+1) if(p[0]=='A'&&p[1]=='2') fprintf(lf,"  %s\n",p); FreeEnvironmentStringsA(blk); } }
#else
        for(char**e=environ;e&&*e;e++) if((*e)[0]=='A'&&(*e)[1]=='2') fprintf(lf,"  %s\n",*e);
#endif

        { const char*w=getenv("A2WIDE"),*u=getenv("A2UNLOCK"); fprintf(lf,"Mods: widescreen %s, frame-rate unlock %s\n",(w&&*w)?w:"off",(u&&atoi(u)>1)?u:"off"); }
        if(ctx_cb){ char cb[600]; cb[0]=0; ctx_cb(cb,sizeof cb); fprintf(lf,"Game state: %s\n",cb); }
        fprintf(lf,"\nLast speed measurements:\n"); for(int i=0;i<NRING;i++){ const char*l=ring[(ring_n+i)%NRING]; if(l[0]) fputs(l,lf); }
        fprintf(lf,"\nEvents:\n"); fflush(lf); fprintf(stderr,"[a2pc] log file: %s\n",lf_path); } }

void a2_note(const char*fmt,...){ va_list ap; va_start(ap,fmt); vsnprintf(ring[ring_n%NRING],sizeof ring[0],fmt,ap); va_end(ap); ring_n++; }
void a2_log(const char*fmt,...){ va_list ap; va_start(ap,fmt); vsnprintf(ring[ring_n%NRING],sizeof ring[0],fmt,ap); va_end(ap); ring_n++; }

void a2_alert(const char*fmt,...){ static int busy; if(busy) return; busy=1; open_log(); va_list ap; va_start(ap,fmt); char b[1400]; vsnprintf(b,sizeof b,fmt,ap); va_end(ap); fputs(b,stderr); if(lf){ fputs(b,lf); fflush(lf); }
#ifdef _WIN32
    Beep(880,150);
#else
    fputc('\a',stderr);
#endif
    busy=0; }

#ifdef _WIN32
static LONG WINAPI crash_filter(EXCEPTION_POINTERS*ep){ a2_alert("[a2pc] CRASH: exception %08lx at address %p\n",(unsigned long)ep->ExceptionRecord->ExceptionCode,ep->ExceptionRecord->ExceptionAddress); return EXCEPTION_CONTINUE_SEARCH; }
void a2_install_crash(void){ SetUnhandledExceptionFilter(crash_filter); }
#else
static void crash_sig(int s){ a2_alert("[a2pc] CRASH: signal %d (%s)\n",s,s==SIGSEGV?"segmentation fault":s==SIGFPE?"arithmetic error":s==SIGILL?"illegal instruction":s==SIGABRT?"abort":"fatal"); signal(s,SIG_DFL); raise(s); }
void a2_install_crash(void){ signal(SIGSEGV,crash_sig); signal(SIGFPE,crash_sig); signal(SIGILL,crash_sig); signal(SIGABRT,crash_sig); signal(SIGBUS,crash_sig); }
#endif
