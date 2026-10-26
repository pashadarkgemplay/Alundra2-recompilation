/* Controller port: answers the game's pad polling with the current keyboard or controller state. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t buttons;
static uint32_t ctrl, mode_, baud; static uint8_t rx[8]; static int nrx;
static int sel_dev, step_;
static uint64_t ack_at; static int ack_pending, irq_flag, dsr;
static uint64_t now_;
long sio_bytes, sio_transactions;
void sio_set_buttons(uint16_t m){ buttons=m; }

static uint8_t stick[4]={0x80,0x80,0x80,0x80};
static int pad_analog=1, pad_cfg, pad_lock, pad_led=1;
static uint8_t vmap[6]={0x00,0x01,0xff,0xff,0xff,0xff}, motor_tx[8], cmd_, t0_;
static uint8_t pad_pending_mode;
static long cmdhist[256];
void sio_set_digital(void){ pad_analog=0; pad_led=0; }
void sio_set_sticks(uint8_t rx,uint8_t ry,uint8_t lx,uint8_t ly){ stick[0]=rx; stick[1]=ry; stick[2]=lx; stick[3]=ly; }
int sio_pad_analog(void){ return pad_analog; }
void sio_get_motors(int*small,int*large){ int s=0,l=0; for(int k=0;k<6;k++){ if(vmap[k]==0) s=(motor_tx[k]&1); else if(vmap[k]==1) l=motor_tx[k]; } *small=s; *large=l; }
static int pad_id(void){ return pad_cfg?0xf3:(pad_analog?0x73:0x41); }
static int data_len(int cmd){ if(pad_cfg) return 6; if(cmd==0x42||cmd==0x43) return pad_analog?6:2; return 6; }
static uint8_t pad_data(int cmd,int k){
    uint16_t b=(uint16_t)~buttons;
    if(cmd==0x42 || (cmd==0x43 && !pad_cfg)){ if(k==0) return b&0xff; if(k==1) return b>>8; return stick[k-2]; }
    switch(cmd){
    case 0x43: { if(k<2) return k==0?(b&0xff):(b>>8); return stick[k-2]; }
    case 0x45: { static const uint8_t r[6]={0x01,0x02,0,0x02,0x01,0x00}; return k==2?(uint8_t)pad_led:r[k]; }
    case 0x46: { static const uint8_t a[6]={0,0,1,2,0,0x0a}, c[6]={0,0,1,1,1,0x14}; return t0_==0?a[k]:c[k]; }
    case 0x47: { static const uint8_t r[6]={0,0,2,0,1,0}; return r[k]; }
    case 0x4c: { static const uint8_t a[6]={0,0,0,4,0,0}, c[6]={0,0,0,6,0,0}; return t0_==0?a[k]:c[k]; }
    case 0x4d: return vmap[k];
    case 0x4f: { static const uint8_t r[6]={0,0,0,0,0,0x5a}; return r[k]; }
    default: return 0; } }
static int pad_byte(uint8_t tx,uint8_t*out,int*acks){
    int n;
    switch(step_){
    case 1: cmd_=tx; cmdhist[tx]++; *out=(uint8_t)pad_id(); *acks=1; break;
    case 2: *out=0x5a; *acks=1; break;
    default: { int k=step_-3; n=data_len(cmd_);
        if(k==0) t0_=tx;
        if(k<8) motor_tx[k]=tx;
        if(cmd_==0x4d && k<6){ static uint8_t nv[6]; nv[k]=tx; *out=pad_data(cmd_,k); vmap[k]=tx; (void)nv; *acks=(k<n-1); break; }
        if(k>=n){ *out=0xff; *acks=0; break; }
        *out=pad_data(cmd_,k); *acks=(k<n-1);
        if(k==n-1){
            if(cmd_==0x43) pad_cfg=(t0_==1);
            else if(cmd_==0x44 && pad_cfg){ pad_analog=(t0_==1); pad_led=pad_analog; }
        }
        break; } }
    return 1; }
static int device_byte(uint8_t tx, uint8_t*out, int*acks){
    if(step_==0){ if(tx==0x01){ sel_dev=1; *out=0xff; *acks=1; step_=1; sio_transactions++; return 1; } sel_dev=0; *out=0xff; *acks=0; return 0; }
    if(!sel_dev){ *out=0xff; *acks=0; return 0; }
    pad_byte(tx,out,acks); step_++; return 1; }
void sio_cmd_report(void){ fprintf(stderr,"pad commands:"); for(int i=0;i<256;i++) if(cmdhist[i]) fprintf(stderr," %02x:%ld",i,cmdhist[i]); fprintf(stderr," analog %d cfg %d\n",pad_analog,pad_cfg); }
static int siolog=-1; static int sl=0;
uint32_t sio_read_(uint32_t off,int w);
uint32_t sio_read(uint32_t off,int w){ uint32_t r=sio_read_(off,w); if(siolog<0) siolog=getenv("A2SIOLOG")!=0; if(siolog&&sl++<150) fprintf(stderr,"SIO rd %x -> %x (t=%llu)\n",off,r,(unsigned long long)now_); return r; }
uint32_t sio_read_(uint32_t off,int w){
    switch(off){
    case 0x0: { uint8_t v=nrx?rx[0]:0xff; if(nrx){ memmove(rx,rx+1,--nrx); } return w==4?(v|0xffffff00u):v; }
    case 0x4: return 1|(nrx?2:0)|4|(dsr?0x80:0)|(irq_flag?0x200:0);
    case 0x8: return mode_;
    case 0xa: return ctrl;
    case 0xe: return baud; }
    return 0; }
void sio_write(uint32_t off,uint32_t v,int w){ if(siolog<0) siolog=getenv("A2SIOLOG")!=0; if(siolog&&sl++<150) fprintf(stderr,"SIO wr %x <- %x (t=%llu)\n",off,v,(unsigned long long)now_);
    switch(off){
    case 0x0: if((ctrl&1)&&(ctrl&2)){ uint8_t o; int acks=0; if(ctrl&0x2000){ o=0xff; acks=0; step_=0; } else device_byte((uint8_t)v,&o,&acks);     if(nrx<8) rx[nrx++]=o; sio_bytes++; if(acks){ ack_pending=1; ack_at=now_+100; } else ack_pending=0; } break;
    case 0x8: mode_=v&0xffff; break;
    case 0xa: if(v&0x40){ ctrl=0; nrx=0; step_=0; irq_flag=0; ack_pending=0; dsr=0; break; }
              if(v&0x10){ irq_flag=0; }
              if(!(v&2)){ step_=0; sel_dev=0; }
              ctrl=v&~0x10u; break;
    case 0xe: baud=v&0xffff; break; } }
void sio_step(uint64_t now){ now_=now; if(siolog>0&&ack_pending&&now>=ack_at) fprintf(stderr,"ACK fires now=%llu ctrl=%x\n",(unsigned long long)now,ctrl); if(ack_pending && now>=ack_at){ ack_pending=0; dsr=1; irq_flag=1; } if(dsr && now>=ack_at+200) dsr=0; }
int sio_irq_take(void){ if(irq_flag==1 && (ctrl&0x1000)){ irq_flag=2; return 1; } return 0; }
