/* Memory card: one card image, kept in saves/memcard1.mcr. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MC_SIZE (128*1024)
static uint8_t card[MC_SIZE]; static int opened, present, dirty; static long idle; static char path[1024]; long mc_writes, mc_reads;
static void fix_sum(uint8_t*f){ uint8_t x=0; for(int i=0;i<127;i++) x^=f[i]; f[127]=x; }
static void format_card(void){
    memset(card,0,sizeof card);
    card[0]='M'; card[1]='C'; fix_sum(card);
    for(int i=1;i<=15;i++){ uint8_t*f=card+i*128; f[0]=0xa0; f[8]=0xff; f[9]=0xff; fix_sum(f); }
    for(int i=16;i<=35;i++){ uint8_t*f=card+i*128; f[0]=f[1]=f[2]=f[3]=0xff; f[8]=0xff; f[9]=0xff; fix_sum(f); }
    for(int i=36;i<=62;i++) memset(card+i*128,0xff,128);
    memcpy(card+63*128,card,128);
}
void mc_flush(void){ if(!opened||!dirty) return; FILE*f=fopen(path,"wb"); if(!f){ fprintf(stderr,"memory card: cannot write %s\n",path); return; } fwrite(card,1,sizeof card,f); fclose(f); dirty=0; }
int mc_open(const char*p){
    if(opened) return present; opened=1; snprintf(path,sizeof path,"%s",p?p:"memcard1.mcr");
    FILE*f=fopen(path,"rb");
    if(f){ size_t n=fread(card,1,sizeof card,f); fclose(f); if(n==MC_SIZE){ present=1; fprintf(stderr,"[a2pc] memory card: %s\n",path); atexit(mc_flush); return 1; }
           if(n==MC_SIZE+3904 || n==MC_SIZE+128){   f=fopen(path,"rb"); fseek(f,(long)(n-MC_SIZE),SEEK_SET); fread(card,1,sizeof card,f); fclose(f); present=1; atexit(mc_flush); return 1; }
           fprintf(stderr,"[a2pc] memory card file %s has an unexpected size (%zu) - a new card will be created in memory only\n",path,n); format_card(); present=1; return 1; }
    format_card(); present=1; dirty=1; mc_flush(); fprintf(stderr,"[a2pc] memory card: new formatted card %s\n",path); atexit(mc_flush); return 1; }
int mc_present(int slot){ return slot==0 && present; }
int mc_read(int slot,int sector,uint8_t*dst){ if(slot!=0||!present||sector<0||sector>=1024) return 0; memcpy(dst,card+sector*128,128); mc_reads++; return 1; }
int mc_write(int slot,int sector,const uint8_t*src){ if(slot!=0||!present||sector<0||sector>=1024) return 0; memcpy(card+sector*128,src,128); dirty=1; idle=0; mc_writes++; return 1; }
void mc_tick(void){ if(dirty && ++idle>=30) mc_flush(); }

typedef struct{ int used, head, off, nowait, write; } McFd;
static McFd mcfd[16];
static int dir_state(int b){ return card[b*128]; }
static int is_free(int b){ int s=card[b*128]&0xf0; return s==0xa0 || card[b*128]==0; }
static void dir_fix(int b){ fix_sum(card+b*128); dirty=1; idle=0; }
static int path_split(const char*path,int*slot,const char**name){
    if(!(path[0]=='b'&&path[1]=='u'&&path[2]>='0'&&path[2]<='9'&&path[3]>='0'&&path[3]<='9'&&path[4]==':')) return 0;
    *slot=(path[2]-'0'); *name=path+5; return 1; }
static int find_file(const char*name){
    for(int b=1;b<=15;b++) if(card[b*128]==0x51 && strncmp((const char*)card+b*128+10,name,20)==0 && (strlen(name)<=20)) return b;
    return 0; }
static int chain_block(int head,int idx){
    int b=head; for(int i=0;i<idx;i++){ int n=card[b*128+8]|(card[b*128+9]<<8); if(n==0xffff||n>14) return 0; b=n+1; } return b; }
static int file_size(int head){ uint8_t*d=card+head*128; return d[4]|(d[5]<<8)|(d[6]<<16)|(d[7]<<24); }
int mcfs_open(const char*path,uint32_t mode){
    int slot; const char*name; if(!path_split(path,&slot,&name)||!mc_present(slot)) return -1;
    if(strlen(name)>20) return -1;
    int head=find_file(name);
    if(mode&0x200){
        int blocks=(int)(mode>>16); if(blocks<1||blocks>15||head) return -1;
        int fr[16],n=0; for(int b=1;b<=15&&n<blocks;b++) if(is_free(b)) fr[n++]=b;
        if(n<blocks) return -1;
        for(int i=0;i<n;i++){ uint8_t*d=card+fr[i]*128; memset(d,0,128); d[0]=(i==0)?0x51:(i==n-1)?0x53:0x52;
            if(i==0){ int sz=blocks*8192; d[4]=sz; d[5]=sz>>8; d[6]=sz>>16; d[7]=sz>>24; strncpy((char*)d+10,name,20); }
            int nx=(i==n-1)?0xffff:fr[i+1]-1; d[8]=nx; d[9]=nx>>8; dir_fix(fr[i]); }
        head=fr[0];
    }
    if(!head) return -1;
    for(int i=0;i<16;i++) if(!mcfd[i].used){ mcfd[i]=(McFd){1,head,0,(mode&0x8000)!=0,(mode&2)!=0 || (mode&0x200)!=0}; return i+3; }
    return -1; }
int mcfs_lseek(int fd,int off,int whence){ fd-=3; if(fd<0||fd>15||!mcfd[fd].used) return -1; int sz=file_size(mcfd[fd].head);
    int n=whence==0?off:whence==1?mcfd[fd].off+off:sz+off; if(n<0) n=0; mcfd[fd].off=n; return n; }
int mcfs_nowait(int fd){ fd-=3; return fd>=0&&fd<16&&mcfd[fd].used&&mcfd[fd].nowait; }
static int rw(int fd,uint8_t*buf,int n,int wr){
    fd-=3; if(fd<0||fd>15||!mcfd[fd].used) return -1; McFd*f=&mcfd[fd]; int sz=file_size(f->head); int done=0;
    while(done<n && f->off<sz){ int bi=f->off/8192, in=f->off%8192; int b=chain_block(f->head,bi); if(!b) break;
        int cnt=8192-in; if(cnt>n-done) cnt=n-done; if(f->off+cnt>sz) cnt=sz-f->off;
        if(wr){ memcpy(card+b*8192+in,buf+done,cnt); dirty=1; idle=0; mc_writes++; } else { memcpy(buf+done,card+b*8192+in,cnt); mc_reads++; }
        done+=cnt; f->off+=cnt; }
    return done; }
int mcfs_read(int fd,uint8_t*buf,int n){ return rw(fd,buf,n,0); }
int mcfs_write(int fd,const uint8_t*buf,int n){ return rw(fd,(uint8_t*)buf,n,1); }
int mcfs_close(int fd){ fd-=3; if(fd<0||fd>15||!mcfd[fd].used) return -1; mcfd[fd].used=0; return fd+3; }
int mcfs_delete(const char*path){ int slot; const char*name; if(!path_split(path,&slot,&name)||!mc_present(slot)) return 0;
    int head=find_file(name); if(!head) return 0; int b=head;
    for(int i=0;i<16&&b;i++){ uint8_t*d=card+b*128; int nx=d[8]|(d[9]<<8); d[0]=(d[0]&0x0f)|0xa0; dir_fix(b); if(nx==0xffff||nx>14) break; b=nx+1; }
    return 1; }

static char pat[24]; static int scan_pos, scan_ok;
static int match(const char*p,const char*s){ for(;*p;p++,s++){ if(*p=='*') return 1; if(*p=='?'){ if(!*s) return 0; continue; } if(*p!=*s) return 0; } return *s==0; }
static int next_match(uint8_t*out){
    while(scan_pos<=15){ int b=scan_pos++; if(card[b*128]!=0x51) continue; char nm[21]; memcpy(nm,card+b*128+10,20); nm[20]=0;
        if(!match(pat,nm)) continue;
        memset(out,0,40); memcpy(out,nm,20); int sz=file_size(b); out[24]=sz; out[25]=sz>>8; out[26]=sz>>16; out[27]=sz>>24; out[32]=b-1; return 1; }
    return 0; }
int mcfs_first(const char*path,uint8_t*out){ int slot; const char*name; if(!path_split(path,&slot,&name)||!mc_present(slot)) return 0;
    if(strlen(name)>20) return 0; strcpy(pat,name); scan_pos=1; return next_match(out); }
int mcfs_next(uint8_t*out){ return next_match(out); }
