/*****************************************************************************/
/*
 * NTSC/CRT - integer-only NTSC video signal encoding / decoding emulation
 * 
 *   by EMMIR 2018-2023
 *   
 *   YouTube: https://www.youtube.com/@EMMIR_KC/videos
 *   Discord: https://discord.com/invite/hdYctSmyQJ
 */
/*****************************************************************************/
#include "crt_sincos.h"


static int sigpsin15[18] = { /* significant points on sine wave (15-bit) */
    0x0000,
    0x0c88,0x18f8,0x2528,0x30f8,0x3c50,0x4718,0x5130,0x5a80,
    0x62f0,0x6a68,0x70e0,0x7640,0x7a78,0x7d88,0x7f60,0x8000,
    0x7f60
};

static int
sintabil8(int n)
{
    int f, i, a, b;
    
    /* looks scary but if you don't change T14_2PI
     * it won't cause out of bounds memory reads
     */
    f = n >> 0 & 0xff;
    i = n >> 8 & 0xff;
    a = sigpsin15[i];
    b = sigpsin15[i + 1];
    return (a + ((b - a) * f >> 8));
}

/* 14-bit interpolated sine/cosine */
extern void
crt_sincos14(int *s, int *c, int n)
{
    int h;
    
    n &= T14_MASK;
    h = n & ((T14_2PI >> 1) - 1);
    
    if (h > ((T14_2PI >> 2) - 1)) {
        *c = -sintabil8(h - (T14_2PI >> 2));
        *s = sintabil8((T14_2PI >> 1) - h);
    } else {
        *c = sintabil8((T14_2PI >> 2) - h);
        *s = sintabil8(h);
    }
    if (n > ((T14_2PI >> 1) - 1)) {
        *c = -*c;
        *s = -*s;
    }
}
