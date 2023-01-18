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

// Precalculate the low and high signal chosen for each 64 base colors
// with their respective attenuated values
// https://www.nesdev.org/wiki/NTSC_video#Brightness_Levels
const int8_t IRE_levels[2][2][0x40]{
   // waveform low
   {
      // normal
      {
         // 0x
         43, -12, -12, -12, -12, -12, -12, -12, -12, -12, -12, -12, -12, -12, 0, 0,
         // 1x
         74, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         // 2x
         110, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 0, 0,
         // 3x
         110, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 0, 0
      },
      // attenuated
      {
         // 0x
         26 , -17, -17, -17, -17, -17, -17, -17, -17, -17, -17, -17, -17, -17, 0, 0,
         // 1x
         51, -8, -8, -8, -8, -8, -8, -8, -8, -8, -8, -8, -8, -8, 0, 0,
         // 2x
         82, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 0, 0,
         // 3x
         82, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 0, 0
      }
   },
   // waveform high
   {
      // normal
      {
         // 0x
         43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, -12, 0, 0,
         // 1x
         74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 0, 0, 0,
         // 2x
         110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 34, 0, 0,
         // 3x
         110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 80, 0, 0
         },
      // attenuated
      {
         // 0x
         26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, -17, 0, 0,
         // 1x
         51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, -8, 0, 0,
         // 2x
         82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 19, 0, 0,
         // 3x
         82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 56, 0, 0
      }
   }
};

/* generate the square wave for a given 9-bit pixel and phase */
static int
square_sample(int p, int phase)
{
    static int active[6] = {
        0300, 0100,
        0500, 0400,
        0600, 0200
    };
    int hue;
    bool e, v;

    hue = (p & 0x0f);

    v = (((hue + phase) % 12) < 6);

    /* red 0100, green 0200, blue 0400 */
    e = ((p & 0700) & active[(phase >> 1) % 6]);

    return IRE_levels[v][e][p & 0x3F];
}

#define NES_OPTIMIZED 0

/* the optimized version is NOT the most optimized version, it just performs
 * some simple refactoring to prevent a few redundant computations
 */
#if NES_OPTIMIZED


/* this function is an optimization
 * basically factoring out the field setup since as long as CRT->analog
 * does not get cleared, all of this should remain the same every update
 */
static void
setup_field(struct CRT *v)
{
    int n;
 
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
            /* prerender/postrender/video scanlines */
            while (t < SYNC_BEG) line[t++] = BLANK_LEVEL; /* FP */
            while (t < BW_BEG) line[t++] = SYNC_LEVEL;  /* SYNC */
            while (t < CRT_HRES) line[t++] = BLANK_LEVEL;
        }
    }
}
 
extern void
crt_modulate(struct CRT *v, struct NTSC_SETTINGS *s)
{
    static int init = 0;
    int x, y, xo, yo;
    int destw = AV_LEN;
    int desth = CRT_LINES;
    int n, phase;
    int po, lo;
    int iccf[4];
    int ccburst[4]; /* color phase for burst */
    int burst_level[4];
    int sn, cs;
    if (!init) {
        setup_field(v);
        init = 1;
    }
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
    phase = po * 3;
 
    for (n = CRT_TOP; n <= (CRT_BOT + 2); n++) {
        int t; /* time */
        signed char *line = &v->analog[n * CRT_HRES];
        
        t = LINE_BEG;
 
        phase += LAV_BEG * 3;
        t = LAV_BEG;
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
    
        phase %= 12;
    
    }
 
    phase = 6;
    /* precalculate to save some CPU */
    for (x = 0; x < 4; x++) {
        burst_level[x] = (BLANK_LEVEL + (ccburst[x] * BURST_LEVEL)) >> 5;
    }
    for (y = (lo - 3); y < desth; y++) {
        signed char *line;  
        int t;
        int sy = (y * s->h) / desth;
        
        if (sy >= s->h) sy = s->h;
        if (sy < 0) sy = 0;
 
        n = (y + yo);
        line = &v->analog[n * CRT_HRES];
        
        /* CB_CYCLES of color burst at 3.579545 Mhz */
        for (t = CB_BEG; t < CB_BEG + (CB_CYCLES * CRT_CB_FREQ); t++) {
            line[t] = burst_level[(t + po + n) & 3];
            iccf[(t + n) & 3] = line[t];
        }
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
#else
/* NOT NES_OPTIMIZED */
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

#endif
