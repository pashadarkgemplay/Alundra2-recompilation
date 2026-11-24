#include "a2hdcodec.h"
#ifdef _WIN32
#define EX __declspec(dllexport)
#else
#define EX
#endif
EX size_t hd_enc(const uint8_t *lr,int bpp,int W,int H,int K,const uint8_t *hd,uint8_t *out,size_t cap){ return hc_encode(lr,bpp,W,H,K,hd,out,cap); }
EX void hd_dec(const uint8_t *lr,int bpp,int W,int H,int K,const uint8_t *in,size_t n,uint8_t *hd){ hc_decode(lr,bpp,W,H,K,in,n,hd); }
EX void hd_crypt(uint8_t *p,size_t n,uint64_t nonce){ hc_crypt(p,n,HC_KEY,nonce); }
EX uint64_t hd_rowhash(const uint8_t *p,size_t n){ return hc_rowhash(p,n); }
EX uint64_t hd_cellrow(const uint8_t *p,size_t n){ return hc_cellrow(p,n); }
