#ifndef A2HD_H
#define A2HD_H
#include <stdint.h>
enum { HD_PUT = 1, HD_CLEAR = 2 };
/* key = ((tpy*16 + tpx) << 1) | (8-bit mode); u0/v0/w/h in texels of that texture page */
typedef struct { int type, key, u0, v0, w, h, stride; const uint8_t *src; } HdOp;
extern int hd_on;
extern uint8_t hd_blk[32][64];
void hd_init(int max_canvas);
int hd_scale(void);
void hd_upload(const uint16_t *vram, int x, int y, int w, int h);
void hd_overwrite(int x0, int y0, int x1, int y1);
void hd_copy(int sx, int sy, int dx, int dy, int w, int h);
int hd_ops(HdOp **out);
void hd_frame(void);
int hd_owned(int x0, int y0, int x1, int y1);
extern long hd_ncopy;
#include <stdio.h>
void hd_stats(FILE *f);
void hd_why(int tpx, int tpy, int mode, int umin, int vmin, int umax, int vmax);
void hd_use(int tpx, int tpy, int mode, int clx, int cly, int umin, int vmin, int umax, int vmax, const uint16_t *vram);
#endif
