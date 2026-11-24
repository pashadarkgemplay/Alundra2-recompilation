/* Fun mods: a different hero. The model, its texture pages and palettes come from hd/hero_<name>.bin (built by hdtools/hero/build2.py). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a2rt.h"
#include "a2hdcodec.h"

extern uint16_t vram[];
extern int a2_gl;
void gpu_gl_upload(const uint16_t *v, int x, int y, int w, int h);

typedef struct { int x, y, w, h; uint16_t *d; } Blob;
static int kind = -2;
static uint8_t *mdata; static uint32_t msz;
static uint8_t tpn, cln; static uint8_t tpi[16], cli[16]; static uint16_t tpv[16], clv[16];
static Blob blobs[16]; static int nblobs, applied;

static void put(void){
    for(int i = 0; i < nblobs; i++){ Blob *b = &blobs[i];
        for(int r = 0; r < b->h; r++) memcpy(&vram[(b->y + r) * 1024 + b->x], b->d + r * b->w, b->w * 2);
        if(a2_gl) gpu_gl_upload(vram, b->x, b->y, b->w, b->h); }
    applied = 1;
}

static void init(void){
    const char *h = getenv("A2HERO"); kind = -1;
    if(!h || !*h || !strcmp(h, "flint")) return;
    const char *d = getenv("A2MODDIR"); char fn[700]; snprintf(fn, sizeof fn, "%s/hero_%s.bin", d && *d ? d : "../hd", h);
    FILE *f = fopen(fn, "rb"); uint8_t hd[8]; uint8_t *p = 0;
    if(!f || fread(hd, 1, 8, f) != 8 || memcmp(hd, "A2HN", 4)){ if(f) fclose(f); fprintf(stderr, "[a2pc] hero mod %s: file %s not found, Flint stays\n", h, fn); return; }
    uint32_t n; memcpy(&n, hd + 4, 4); p = (uint8_t *)malloc(n);
    if(!p || fread(p, 1, n, f) != n){ free(p); fclose(f); return; }
    fclose(f); hc_crypt(p, n, HC_KEY, 0xA2480001ull);
    uint32_t o = 0; memcpy(&msz, p, 4); o = 4; if(msz > 31216 || o + msz > n){ free(p); return; }
    mdata = (uint8_t *)malloc(msz); memcpy(mdata, p + o, msz); o += msz;
    tpn = p[o++]; for(int i = 0; i < tpn && i < 16; i++){ tpi[i] = p[o]; memcpy(&tpv[i], p + o + 1, 2); o += 3; }
    cln = p[o++]; for(int i = 0; i < cln && i < 16; i++){ cli[i] = p[o]; memcpy(&clv[i], p + o + 1, 2); o += 3; }
    nblobs = p[o++]; if(nblobs > 16) nblobs = 16;
    for(int i = 0; i < nblobs; i++){ uint16_t q[4]; memcpy(q, p + o, 8); o += 8; Blob *b = &blobs[i]; b->x = q[0]; b->y = q[1]; b->w = q[2]; b->h = q[3];
        b->d = (uint16_t *)malloc(b->w * b->h * 2); memcpy(b->d, p + o, b->w * b->h * 2); o += b->w * b->h * 2; }
    free(p); kind = 1; fprintf(stderr, "[a2pc] hero: %s (%u bytes)\n", h, msz);
}

void a2_hero_mode_hook(CPU *c){
    (void)c; if(kind == -2) init();
}

void a2_hero_model_hook(CPU *c){
    (void)c; if(kind == -2) init();
    if(kind != 1 || !mdata) return;
    uint32_t p = rd32(0x8012CB9Cu); if((p >> 24) != 0x80) return;
    if(rd16(0x8012CB88u) != 0) return;
    for(uint32_t i = 0; i < msz; i++) wr8(p + i, mdata[i]);
    for(int i = 0; i < tpn; i++) wr16(0x8005244Cu + 2 * tpi[i], tpv[i]);
    for(int i = 0; i < cln; i++) wr16(0x80052460u + 2 * cli[i], clv[i]);
    put();
}

void a2mod_tick(void){
    static int n; if(kind != 1 || !applied || (++n & 15)) return;
    for(int i = 0; i < nblobs; i++){ Blob *b = &blobs[i];
        for(int r = 0; r < b->h; r += (b->h > 1 ? 37 : 1)) if(memcmp(&vram[(b->y + r) * 1024 + b->x], b->d + r * b->w, b->w * 2)){ put(); return; } }
}
