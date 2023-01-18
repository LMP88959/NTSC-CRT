/*****************************************************************************/
/*
 * NTSC/CRT - integer-only NTSC video signal encoding / decoding emulation
 *
 *   by EMMIR 2018-2023
 *   modifications for Mesen by Persune
 *   https://github.com/LMP88959/NTSC-CRT
 *
 *   YouTube: https://www.youtube.com/@EMMIR_KC/videos
 *   Discord: https://discord.com/invite/hdYctSmyQJ
 */
/*****************************************************************************/

#include "crt_core.h"

#if (CRT_SYSTEM == CRT_SYSTEM_NES)
#include <stdlib.h>
#include <string.h>

/* generate the square wave for a given 9-bit pixel and phase */
static int
square_sample(int p, int phase)
{
    static int active[6] = {
        0300, 0100,
        0500, 0400,
        0600, 0200
    };
    int bri, hue, v;

    hue = (p & 0x0f);
    
    /* last two columns are black */
    if (hue >= 0x0e) {
        return 0;
    }

    bri = ((p & 0x30) >> 4) * 300;
    
    switch (hue) {
        case 0:
            v = bri + 410;
            break;
        case 0x0d:
            v = bri - 300;
            break;
        default:
            v = (((hue + phase) % 12) < 6) ? (bri + 410) : (bri - 300);
            break;
    }

    if (v > 1024) {
        v = 1024;
    }
    /* red 0100, green 0200, blue 0400 */
    if ((p & 0700) & active[(phase >> 1) % 6]) {
        return (v >> 1) + (v >> 2);
    }

    return v;
}

extern void
crt_modulate(struct CRT *v, struct NTSC_SETTINGS *s)
{
    int x, y, xo, yo;
    int destw = AV_LEN;
    int desth = CRT_LINES;
    int n, phase;
    int po, lo;
    int iccf[4];
    int ccburst[4]; /* color phase for burst */
    int sn, cs;
    
    for (x = 0; x < 4; x++) {
        n = s->hue + x * 90;
        crt_sincos14(&sn, &cs, (n + 33) * 8192 / 180);
        ccburst[x] = sn >> 10;
    }
    xo = AV_BEG;
    yo = CRT_TOP;
         
    /* align signal */
    xo = (xo & ~3);
    
    /* this mess of offsetting logic was reached through trial and error */
    lo = (s->dot_crawl_offset % 3); /* line offset to match color burst */
    po = lo + 1;
    if (lo == 1) {
        lo = 3;
    }
    phase = 3 + po * 3;

    for (n = 0; n < CRT_VRES; n++) {
        int t; /* time */
        signed char *line = &v->analog[n * CRT_HRES];
        
        t = LINE_BEG;

        /* vertical sync scanlines */
        if (n >= 259 && n <= CRT_VRES) {
           while (t < SYNC_BEG) line[t++] = BLANK_LEVEL; /* FP */
           while (t < PPUpx2pos(327)) line[t++] = SYNC_LEVEL; /* sync separator */
           while (t < CRT_HRES) line[t++] = BLANK_LEVEL; /* blank */
        } else {
            int cb;
            /* prerender/postrender/video scanlines */
            while (t < SYNC_BEG) line[t++] = BLANK_LEVEL; /* FP */
            while (t < BW_BEG) line[t++] = SYNC_LEVEL;  /* SYNC */
            while (t < CB_BEG) line[t++] = BLANK_LEVEL; /* BW + CB + BP */
            /* CB_CYCLES of color burst at 3.579545 Mhz */
            for (t = CB_BEG; t < CB_BEG + (CB_CYCLES * CRT_CB_FREQ); t++) {
                cb = ccburst[(t + po + n) & 3];
                line[t] = (BLANK_LEVEL + (cb * BURST_LEVEL)) >> 5;
                iccf[(t + n) & 3] = line[t];
            }
            while (t < LAV_BEG) line[t++] = BLANK_LEVEL;
            phase += t * 3;
            if (n >= CRT_TOP && n <= (CRT_BOT + 2)) {
                while (t < CRT_HRES) {
                    int ire, p;
                    p = s->border_color;
                    if (t == LAV_BEG) p = 0xf0;
                    ire = BLACK_LEVEL + v->black_point;
                    ire += square_sample(p, phase + 0);
                    ire += square_sample(p, phase + 1);
                    ire += square_sample(p, phase + 2);
                    ire += square_sample(p, phase + 3);
                    ire = (ire * (WHITE_LEVEL * v->white_point / 100)) >> 12;
                    line[t++] = ire;
                    phase += 3;
                }
            } else {
                while (t < CRT_HRES) line[t++] = BLANK_LEVEL;
                phase += (CRT_HRES - LAV_BEG) * 3;
            }
            phase %= 12;
        }
    }

    phase = 6;

    for (y = (lo - 3); y < desth; y++) {
        int sy = (y * s->h) / desth;
        if (sy >= s->h) sy = s->h;
        if (sy < 0) sy = 0;
        
        sy *= s->w;
        phase += (xo * 3);
        for (x = 0; x < destw; x++) {
            if (y >= 0) {
                int ire, p;
                
                p = s->data[((x * s->w) / destw) + sy];
                ire = BLACK_LEVEL + v->black_point;
                ire += square_sample(p, phase + 0);
                ire += square_sample(p, phase + 1);
                ire += square_sample(p, phase + 2);
                ire += square_sample(p, phase + 3);
                ire = (ire * (WHITE_LEVEL * v->white_point / 100)) >> 12;
                v->analog[(x + xo) + (y + yo) * CRT_HRES] = ire;
            } else {
                v->analog[(x + xo) + (y + yo) * CRT_HRES] = BLACK_LEVEL;
            }
            phase += 3;
        }
        /* mod here so we don't overflow down the line */
        phase = (phase + ((CRT_HRES - destw) * 3)) % 12;
    }
    
    for (x = 0; x < 4; x++) {
        for (n = 0; n < 4; n++) {
            /* don't know why, but it works */
            v->ccf[n][x] = iccf[(x + n + 1) & 3] << 7;
        }
    }
}
#endif
