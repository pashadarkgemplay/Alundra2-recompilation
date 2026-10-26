/* Small thread helpers (one lock, one wake-up signal) for the multi-threaded 3D drawing. */

#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION mtx; static CONDITION_VARIABLE cnd; static int inited;
static void init_(void){ if(!inited){ inited=1; InitializeCriticalSection(&mtx); InitializeConditionVariable(&cnd); } }
int a2_cpus(void){ SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors; }
typedef struct{ void(*fn)(int); int wk; } Arg;
static DWORD WINAPI tmain(LPVOID p){ Arg a=*(Arg*)p; HeapFree(GetProcessHeap(),0,p); a.fn(a.wk); return 0; }
int a2_thread_start(void(*fn)(int),int wk){ init_(); Arg*a=(Arg*)HeapAlloc(GetProcessHeap(),0,sizeof(Arg)); if(!a) return 0; a->fn=fn; a->wk=wk; HANDLE h=CreateThread(0,0,tmain,a,0,0); if(h){ CloseHandle(h); return 1; } HeapFree(GetProcessHeap(),0,a); return 0; }
void a2_lock(void){ init_(); EnterCriticalSection(&mtx); }
void a2_unlock(void){ LeaveCriticalSection(&mtx); }
void a2_wait(void){ SleepConditionVariableCS(&cnd,&mtx,50); }
void a2_wake(void){ WakeAllConditionVariable(&cnd); }
#else
#include <pthread.h>
#include <unistd.h>
#include <time.h>
static pthread_mutex_t mtx=PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cnd=PTHREAD_COND_INITIALIZER;
int a2_cpus(void){ long n=sysconf(_SC_NPROCESSORS_ONLN); return n<1?1:(int)n; }
typedef struct{ void(*fn)(int); int wk; } Arg;
#include <stdlib.h>
static void* tmain(void*p){ Arg a=*(Arg*)p; free(p); a.fn(a.wk); return 0; }
int a2_thread_start(void(*fn)(int),int wk){ Arg*a=malloc(sizeof(Arg)); if(!a) return 0; a->fn=fn; a->wk=wk; pthread_t t; if(pthread_create(&t,0,tmain,a)==0){ pthread_detach(t); return 1; } free(a); return 0; }
void a2_lock(void){ pthread_mutex_lock(&mtx); }
void a2_unlock(void){ pthread_mutex_unlock(&mtx); }
void a2_wait(void){ struct timespec ts; clock_gettime(CLOCK_REALTIME,&ts); ts.tv_nsec+=50*1000000L; if(ts.tv_nsec>=1000000000L){ ts.tv_sec++; ts.tv_nsec-=1000000000L; } pthread_cond_timedwait(&cnd,&mtx,&ts); }
void a2_wake(void){ pthread_cond_broadcast(&cnd); }
#endif
