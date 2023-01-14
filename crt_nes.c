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

#include "crt.h"

#include <stdlib.h>
#include <string.h>

/* NES composite signal is measured in terms of PPU pixels, or cycles
 * https://www.nesdev.org/wiki/NTSC_video#Scanline_Timing
 *
 *                         FULL HORIZONTAL LINE SIGNAL
 *             (341 PPU px; one cycle skipped on odd rendered frames)
 * |---------------------------------------------------------------------------|
 *   HBLANK (58 PPU px)               ACTIVE VIDEO (283 PPU px)
 * |-------------------||------------------------------------------------------|
 *
 *
 *   WITHIN HBLANK PERIOD:
 *
 *   FP (9 PPU px)  SYNC (25 PPU px) BW (4 PPU px) CB (15 PPU px) BP (5 PPU px)
 * |--------------||---------------||------------||-------------||-------------|
 *      BLANK            SYNC           BLANK          BLANK          BLANK
 *
 *
 *   WITHIN ACTIVE VIDEO PERIOD:
 *
 *   LB (15 PPU px)                 AV (256 PPU px)               RB (11 PPU px)
 * |--------------||--------------------------------------------||-------------|
 *      BORDER                           VIDEO                        BORDER
 *
 */
#define LINE_BEG         0
#define FP_PPUpx         9         /* front porch */
#define SYNC_PPUpx       25        /* sync tip */
#define BW_PPUpx         4         /* breezeway */
#define CB_PPUpx         15        /* color burst */
#define BP_PPUpx         5         /* back porch */
#define PS_PPUpx         1         /* pulse */
#define LB_PPUpx         15        /* left border */
#define AV_PPUpx         256       /* active video */
#define RB_PPUpx         11        /* right border */
#define HB_PPUpx         (FP_PPUpx + SYNC_PPUpx + BW_PPUpx + CB_PPUpx + BP_PPUpx) /* h blank */
/* line duration should be ~63500 ns */
#define LINE_PPUpx          (FP_PPUpx + SYNC_PPUpx + BW_PPUpx + CB_PPUpx + BP_PPUpx + PS_PPUpx + LB_PPUpx + AV_PPUpx + RB_PPUpx)

/* convert pixel offset to its corresponding point on the sampled line */
#define PPUpx2pos(PPUpx)       ((PPUpx) * CRT_HRES / LINE_PPUpx)
/* starting points for all the different pulses */
#define FP_BEG           PPUpx2pos(0)                                           /* front porch point */
#define SYNC_BEG         PPUpx2pos(FP_PPUpx)                                    /* sync tip point */
#define BW_BEG           PPUpx2pos(FP_PPUpx + SYNC_PPUpx)                       /* breezeway point */
#define CB_BEG           PPUpx2pos(FP_PPUpx + SYNC_PPUpx + BW_PPUpx)            /* color burst point */
#define BP_BEG           PPUpx2pos(FP_PPUpx + SYNC_PPUpx + BW_PPUpx + CB_PPUpx) /* back porch point */
#define AV_BEG           PPUpx2pos(HB_PPUpx)                                    /* active video point */
#define PPUAV_BEG        PPUpx2pos(HB_PPUpx + PS_PPUpx + LB_PPUpx)              /* PPU active video point */
#define AV_LEN           PPUpx2pos(AV_PPUpx)                                    /* active video length */

/* somewhere between 7 and 12 cycles */
#define CB_CYCLES   10

/* frequencies for bandlimiting */
#define L_FREQ           1431818 /* full line */
#define Y_FREQ           420000  /* Luma   (Y) 4.2  MHz of the 14.31818 MHz */
#define I_FREQ           150000  /* Chroma (I) 1.5  MHz of the 14.31818 MHz */
#define Q_FREQ           55000   /* Chroma (Q) 0.55 MHz of the 14.31818 MHz */

/* IRE units (100 = 1.0V, -40 = 0.0V) */
/* https://www.nesdev.org/wiki/NTSC_video#Terminated_measurement */
#define WHITE_LEVEL      110
#define BURST_LEVEL      30
#define BLACK_LEVEL      0
#define BLANK_LEVEL      0
#define SYNC_LEVEL      -37

#if (CRT_CHROMA_PATTERN == 1)
/* 227.5 subcarrier cycles per line means every other line has reversed phase */
#define CC_PHASE(ln)     (((ln) & 1) ? -1 : 1)
#else
#define CC_PHASE(ln)     (1)
#endif

/* ensure negative values for x get properly modulo'd */
#define POSMOD(x, n)     (((x) % (n) + (n)) % (n))

/*****************************************************************************/
/***************************** FIXED POINT MATH ******************************/
/*****************************************************************************/

#define T14_2PI           16384
#define T14_MASK          (T14_2PI - 1)
#define T14_PI            (T14_2PI / 2)

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

#define EXP_P         11
#define EXP_ONE       (1 << EXP_P)
#define EXP_MASK      (EXP_ONE - 1)
#define EXP_PI        6434
#define EXP_MUL(x, y) (((x) * (y)) >> EXP_P)
#define EXP_DIV(x, y) (((x) << EXP_P) / (y))

static int e11[] = {
    EXP_ONE,
    5567,  /* e   */
    15133, /* e^2 */
    41135, /* e^3 */
    111817 /* e^4 */
}; 

/* fixed point e^x */
static int
expx(int n)
{
    int neg, idx, res;
    int nxt, acc, del;
    int i;

    if (n == 0) {
        return EXP_ONE;
    }
    neg = n < 0;
    if (neg) {
        n = -n;
    }
    idx = n >> EXP_P;
    res = EXP_ONE;
    for (i = 0; i < idx / 4; i++) {
        res = EXP_MUL(res, e11[4]);
    }
    idx &= 3;
    if (idx > 0) {
        res = EXP_MUL(res, e11[idx]);
    }
    
    n &= EXP_MASK;
    nxt = EXP_ONE;
    acc = 0;
    del = 1;
    for (i = 1; i < 17; i++) {
        acc += nxt / del;
        nxt = EXP_MUL(nxt, n);
        del *= i;
        if (del > nxt || nxt <= 0 || del <= 0) {
            break;
        }
    }
    res = EXP_MUL(res, acc);

    if (neg) {
        res = EXP_DIV(EXP_ONE, res);
    }
    return res;
}

/*****************************************************************************/
/********************************* FILTERS ***********************************/
/*****************************************************************************/

#define HISTLEN     3
#define HISTOLD     (HISTLEN - 1) /* oldest entry */
#define HISTNEW     0             /* newest entry */

#define EQ_P        16 /* if changed, the gains will need to be adjusted */
#define EQ_R        (1 << (EQ_P - 1)) /* rounding */
/* three band equalizer */
static struct EQF {
    int lf, hf; /* fractions */
    int g[3]; /* gains */
    int fL[4];
    int fH[4];
    int h[HISTLEN]; /* history */
} eqY, eqI, eqQ;

/* f_lo - low cutoff frequency
 * f_hi - high cutoff frequency
 * rate - sampling rate
 * g_lo, g_mid, g_hi - gains
 */
static void
init_eq(struct EQF *f,
        int f_lo, int f_hi, int rate,
        int g_lo, int g_mid, int g_hi)
{
    int sn, cs;
    
    memset(f, 0, sizeof(struct EQF));
        
    f->g[0] = g_lo;
    f->g[1] = g_mid;
    f->g[2] = g_hi;
    
    crt_sincos14(&sn, &cs, T14_PI * f_lo / rate);
    if (EQ_P >= 15) {
        f->lf = 2 * (sn << (EQ_P - 15));
    } else {
        f->lf = 2 * (sn >> (15 - EQ_P));
    }
    crt_sincos14(&sn, &cs, T14_PI * f_hi / rate);
    if (EQ_P >= 15) {
        f->hf = 2 * (sn << (EQ_P - 15));
    } else {
        f->hf = 2 * (sn >> (15 - EQ_P));
    }
}

static void
reset_eq(struct EQF *f)
{
    memset(f->fL, 0, sizeof(f->fL));
    memset(f->fH, 0, sizeof(f->fH));
    memset(f->h, 0, sizeof(f->h));
}

static int
eqf(struct EQF *f, int s)
{    
    int i, r[3];

    f->fL[0] += (f->lf * (s - f->fL[0]) + EQ_R) >> EQ_P;
    f->fH[0] += (f->hf * (s - f->fH[0]) + EQ_R) >> EQ_P;
    
    for (i = 1; i < 4; i++) {
        f->fL[i] += (f->lf * (f->fL[i - 1] - f->fL[i]) + EQ_R) >> EQ_P;
        f->fH[i] += (f->hf * (f->fH[i - 1] - f->fH[i]) + EQ_R) >> EQ_P;
    }
    
    r[0] = f->fL[3];
    r[1] = f->fH[3] - f->fL[3];
    r[2] = f->h[HISTOLD] - f->fH[3];

    for (i = 0; i < 3; i++) {
        r[i] = (r[i] * f->g[i]) >> EQ_P;
    }
  
    for (i = HISTOLD; i > 0; i--) {
        f->h[i] = f->h[i - 1];
    }
    f->h[HISTNEW] = s;
    
    return (r[0] + r[1] + r[2]);
}

/* infinite impulse response low pass filter for bandlimiting YIQ */
static struct IIRLP {
    int c;
    int h; /* history */
} iirY, iirI, iirQ;

/* freq  - total bandwidth
 * limit - max frequency
 */
static void
init_iir(struct IIRLP *f, int freq, int limit)
{
    int rate; /* cycles/pixel rate */
    
    memset(f, 0, sizeof(struct IIRLP));
    rate = (freq << 9) / limit;
    f->c = EXP_ONE - expx(-((EXP_PI << 9) / rate));
}

static void
reset_iir(struct IIRLP *f)
{
    f->h = 0;
}

/* hi-pass for debugging */
#define HIPASS 0

static int
iirf(struct IIRLP *f, int s)
{
    f->h += EXP_MUL(s - f->h, f->c);
#if HIPASS
    return s - f->h;
#else
    return f->h;
#endif
}

//Precalculate the low and high signal chosen for each 64 base colors
//with their respective attenuated values
// https://www.nesdev.org/wiki/NTSC_video#Brightness_Levels
const int8_t IRE_levels[2][2][0x40] {
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

/*****************************************************************************/
/***************************** PUBLIC FUNCTIONS ******************************/
/*****************************************************************************/

extern void
crt_resize(struct CRT *v, int w, int h, int *out)
{    
    v->outw = w;
    v->outh = h;
    v->out = out;
}

extern void
crt_reset(struct CRT *v)
{
    v->hue = 0;
    v->saturation = 18;
    v->brightness = 0;
    v->contrast = 180;
    //v->black_point = 0;
    //v->white_point = 100;
    v->noise = 0;
    v->hsync = 0;
    v->vsync = 0;
}

extern void
crt_init(struct CRT *v, int w, int h, int *out)
{
    memset(v, 0, sizeof(struct CRT));
    crt_resize(v, w, h, out);
    crt_reset(v);
            
    /* kilohertz to line sample conversion */
#define kHz2L(kHz) (CRT_HRES * (kHz * 100) / L_FREQ)
    
    /* band gains are pre-scaled as 16-bit fixed point
     * if you change the EQ_P define, you'll need to update these gains too
     */
    init_eq(&eqY, kHz2L(1500), kHz2L(3000), CRT_HRES, 65536, 8192, 9175);
    init_eq(&eqI, kHz2L(80),   kHz2L(1150), CRT_HRES, 65536, 65536, 1311);
    init_eq(&eqQ, kHz2L(80),   kHz2L(1000), CRT_HRES, 65536, 65536, 0);
    
    init_iir(&iirY, L_FREQ, Y_FREQ);
    init_iir(&iirI, L_FREQ, I_FREQ);
    init_iir(&iirQ, L_FREQ, Q_FREQ);
}

/* generate the square wave for a given 9-bit pixel and phase */
static int
square_sample(int pixel_color, int phase)
{
    static int active[6] = {
        0x0C0, 0x040,
        0x140, 0x100,
        0x180, 0x080
    };
    int pixel_index, hue, level, emphasis = 0;
    pixel_index = pixel_color & 0x3F;
    hue = (pixel_index & 0x0f);

    if (hue >= 0x0e) return 0;

    switch (hue) {
    case 0:
       level = 1;
       break;
    case 0x0d:
       level = 0;
       break;
    default:
       level = (((hue + phase) % 12) < 6);
       break;
    }

    /* red 0100, green 0200, blue 0400 */
    if (((pixel_color & 0x1C0) & active[(phase >> 1) % 6]) && hue < 0x0e) {
       emphasis = 1;
    }

    return IRE_levels[level][emphasis][(pixel_index)];
}

extern void
crt_nes2ntsc(struct CRT *v, struct NES_NTSC_SETTINGS *s)
{
    int x, y, xo, yo;
    int destw = AV_LEN;
    int desth = CRT_LINES;
    int n, phase;
    int po, lo;

#if CRT_DO_BLOOM
    if (s->raw) {
        destw = s->w;
        desth = s->h;
        if (destw > ((AV_LEN * 55500) >> 16)) {
            destw = ((AV_LEN * 55500) >> 16);
        }
        if (desth > ((CRT_LINES * 63500) >> 16)) {
            desth = ((CRT_LINES * 63500) >> 16);
        }
    } else {
        destw = (AV_LEN * 55500) >> 16;
        desth = (CRT_LINES * 63500) >> 16;
    }
#else
    if (s->raw) {
        destw = s->w;
        desth = s->h;
        if (destw > AV_LEN) {
            destw = AV_LEN;
        }
        if (desth > ((CRT_LINES * 64500) >> 16)) {
            desth = ((CRT_LINES * 64500) >> 16);
        }
    }
#endif

    xo = PPUAV_BEG;
    yo = CRT_TOP;
         
    /* align signal */
    xo = (xo & ~3);
#if CRT_NES_HIRES
    switch (s->dot_crawl_offset % 3) {
        case 0:
            lo = 1;
            po = 3;
            break;
        case 1:
            lo = 3;
            po = 1;
            break;
        case 2:
            lo = 2;
            po = 0;
            break;
    }
#else
    lo = (s->dot_crawl_offset % 3); /* line offset to match color burst */
    po = lo;
    if (lo == 1) {
        lo = 3;
    }
#endif

    phase = (1 + po) * 3;

    for (n = 0; n < CRT_VRES; n++) {
        int t; /* time */
        signed char *line = &v->analog[n * CRT_HRES];
        
        t = LINE_BEG;

        // vertical sync scanlines
        if (n >= 259 && n <= CRT_VRES) {
           while (t < SYNC_BEG) line[t++] = BLANK_LEVEL; /* FP */
           while (t < PPUpx2pos(327)) line[t++] = SYNC_LEVEL; /* sync separator */
           while (t < CRT_HRES) line[t++] = BLANK_LEVEL; /* blank */
        } else {
            /* prerender/postrender/video scanlines */
            while (t < SYNC_BEG) line[t++] = BLANK_LEVEL; /* FP */
            while (t < BW_BEG) line[t++] = SYNC_LEVEL;  /* SYNC */
            while (t < CB_BEG) line[t++] = BLANK_LEVEL; /* BW + CB + BP */
            int cb;
            /* CB_CYCLES of color burst at 3.579545 Mhz */
            int skipdot = PPUpx2pos(((n == 14 && s->dot_skipped) ? 1 : 0));
            for (t = CB_BEG; t < CB_BEG + (CB_CYCLES * CRT_CB_FREQ) - skipdot; t++) {
                cb = s->cc[(t + po) & 3];
                line[t] = BLANK_LEVEL + (cb * BURST_LEVEL) / s->ccs;
                v->ccf[t & 3] = line[t];
            }
            while (t < AV_BEG) line[t++] = BLANK_LEVEL;
            phase += t * 3;
            if (n >= CRT_TOP && n <= (CRT_BOT + 2)) {
                while (t < CRT_HRES) {
                    int ire, p;
                    p = s->borderdata;
                    if (t == AV_BEG) p = 0xF0;
                    ire = BLACK_LEVEL;
                    ire += square_sample(p, phase + 0);
                    ire += square_sample(p, phase + 1);
                    ire += square_sample(p, phase + 2);
                    ire += square_sample(p, phase + 3);
                    ire >>= 2;
                    line[t++] = ire;
                    phase += 3;
                }
            }
            else {
                while (t < CRT_HRES) line[t++] = BLANK_LEVEL;
                phase += (CRT_HRES - AV_BEG) * 3;
            }
            phase %= 12;
        }
    }

    phase = 3;

    for (y = (lo - 3); y < desth; y++) {
        int sy = (y * s->h) / desth;
        if (sy >= s->h) sy = s->h;
        if (sy < 0) sy = 0;
        
        sy *= s->w;
        phase += (xo * 3);
        for (x = 0; x < destw; x++) {
            int ire, p;
            
            p = s->data[((x * s->w) / destw) + sy];
            ire = BLANK_LEVEL;
            ire += square_sample(p, phase + 0);
            ire += square_sample(p, phase + 1);
            ire += square_sample(p, phase + 2);
            ire += square_sample(p, phase + 3);
            ire >>= 2;
            v->analog[(x + xo) + (y + yo) * CRT_HRES] = ire;
            phase += 3;
        }
        /* mod here so we don't overflow down the line */
        phase = (phase + ((CRT_HRES - destw) * 3)) % 12;
    }
}

/* search windows, in samples */
#define HSYNC_WINDOW 6
#define VSYNC_WINDOW 6

extern void
crt_draw(struct CRT *v)
{
    struct {
        int y, i, q;
    } out[AV_LEN + 1], *yiqA, *yiqB;
    int i, j, line;
#if CRT_DO_BLOOM
    int prev_e; /* filtered beam energy per scan line */
    int max_e; /* approx maximum energy in a scan line */
#endif
    int bright = v->brightness - (BLACK_LEVEL); //+ v->black_point);
    signed char *sig;
    int s = 0;
    int field, ratio;
    static int ccref[4]; /* color carrier signal */
    int huesn, huecs;
    int xnudge = -3, ynudge = 3;
    
    crt_sincos14(&huesn, &huecs, ((v->hue % 360) + 90) * 8192 / 180);
    huesn >>= 11; /* make 4-bit */
    huecs >>= 11;

    ccref[0] = v->ccf[0] << 7;
    ccref[1] = v->ccf[1] << 7;
    ccref[2] = v->ccf[2] << 7;
    ccref[3] = v->ccf[3] << 7;

    for (i = 0; i < CRT_INPUT_SIZE; i++) {
        static int rn = 194; /* 'random' noise */

        rn = (214019 * rn + 140327895);

        /* signal + noise */
        s = v->analog[i] + (((((rn >> 16) & 0xff) - 0x7f) * v->noise) >> 8);
        if (s >  127) { s =  127; }
        if (s < -127) { s = -127; }
        v->inp[i] = s;
    }

    /* Look for vertical sync.
     * 
     * This is done by integrating the signal and
     * seeing if it exceeds a threshold. The threshold of
     * the vertical sync pulse is much higher because the
     * vsync pulse is a lot longer than the hsync pulse.
     * The signal needs to be integrated to lessen
     * the noise in the signal.
     */
    for (i = -VSYNC_WINDOW; i < VSYNC_WINDOW; i++) {
        line = POSMOD(v->vsync + i, CRT_VRES);
        sig = v->inp + line * CRT_HRES;
        s = 0;
        for (j = 0; j < CRT_HRES; j++) {
            s += sig[j];
            /* increase the multiplier to make the vsync
             * more stable when there is a lot of noise
             */
#if CRT_NES_HIRES
            if (s <= (150 * SYNC_LEVEL)) {
                goto vsync_found;
            }
#else
            if (s <= (100 * SYNC_LEVEL)) {
                goto vsync_found;
            }
#endif
        }
    }
vsync_found:
#if CRT_DO_VSYNC
    v->vsync = line; /* vsync found (or gave up) at this line */
#else
    v->vsync = -3;
#endif
    /* if vsync signal was in second half of line, odd field */
    field = (j > (CRT_HRES / 2));
#if CRT_DO_BLOOM
    max_e = (128 + (v->noise / 2)) * AV_LEN;
    prev_e = (16384 / 8);
#endif
    /* ratio of output height to active video lines in the signal */
    ratio = (v->outh << 16) / CRT_LINES;
    ratio = (ratio + 32768) >> 16;
    
    field = (field * (ratio / 2));

    for (line = CRT_TOP; line < CRT_BOT; line++) {
        unsigned pos, ln;
        int scanL, scanR, dx;
        int L, R;
#if CRT_DO_BLOOM
        int line_w;
#endif
        int *cL, *cR;
        int wave[4];
        int dci, dcq; /* decoded I, Q */
        int xpos, ypos;
        int beg, end;
        int phasealign;
  
        beg = (line - CRT_TOP + 0) * v->outh / CRT_LINES + field;
        end = (line - CRT_TOP + 1) * v->outh / CRT_LINES + field;

        if (beg >= v->outh) { continue; }
        if (end > v->outh) { end = v->outh; }

        /* Look for horizontal sync.
         * See comment above regarding vertical sync.
         */
        ln = (POSMOD(line + v->vsync, CRT_VRES)) * CRT_HRES;
        sig = v->inp + ln + v->hsync;
        s = 0;
        for (i = -HSYNC_WINDOW; i < HSYNC_WINDOW; i++) {
            s += sig[SYNC_BEG + i];
            if (s <= (4 * SYNC_LEVEL)) {
                break;
            }
        }
#if CRT_DO_HSYNC
        v->hsync = POSMOD(i + v->hsync, CRT_HRES);
#else
        v->hsync = 3;
#endif
       
        sig = v->inp + ln + (v->hsync & ~3); /* burst @ 1/CB_FREQ sample rate */
        for (i = CB_BEG; i < CB_BEG + (CB_CYCLES * CRT_CB_FREQ); i++) {
            int p = ccref[i & 3] * 127 / 128; /* fraction of the previous */
            int n = sig[i];                   /* mixed with the new sample */
            ccref[i & 3] = p + n;
        }
        xpos = POSMOD(PPUAV_BEG + v->hsync + xnudge, CRT_HRES);
        ypos = POSMOD(line + v->vsync + ynudge, CRT_VRES);
        pos = xpos + ypos * CRT_HRES;
        phasealign = pos & 3;
        
        /* amplitude of carrier = saturation, phase difference = hue */
        dci = ccref[(phasealign + 1) & 3] - ccref[(phasealign + 3) & 3];
        dcq = ccref[(phasealign + 2) & 3] - ccref[(phasealign + 0) & 3];

        /* rotate them by the hue adjustment angle */
        wave[0] = ((dci * huecs - dcq * huesn) >> 4) * v->saturation;
        wave[1] = ((dcq * huecs + dci * huesn) >> 4) * v->saturation;
        wave[2] = -wave[0];
        wave[3] = -wave[1];
        
        sig = v->inp + pos;
#if CRT_DO_BLOOM
        s = 0;
        for (i = 0; i < AV_LEN; i++) {
            s += sig[i]; /* sum up the scan line */
        }
        /* bloom emulation */
        prev_e = (prev_e * 123 / 128) + ((((max_e >> 1) - s) << 10) / max_e);
        line_w = (AV_LEN * 112 / 128) + (prev_e >> 9);

        dx = (line_w << 12) / v->outw;
        scanL = ((AV_LEN / 2) - (line_w >> 1) + 8) << 12;
        scanR = (AV_LEN - 1) << 12;
        
        L = (scanL >> 12);
        R = (scanR >> 12);
#else
        dx = ((AV_LEN - 1) << 12) / v->outw;
        scanL = 0;
        scanR = (AV_LEN - 1) << 12;
        L = 0;
        R = AV_LEN;
#endif
        reset_eq(&eqY);
        reset_eq(&eqI);
        reset_eq(&eqQ);
        
        for (i = L; i < R; i++) {
            out[i].y = eqf(&eqY, sig[i] + bright) << 4;
            out[i].i = eqf(&eqI, sig[i] * wave[(i + 0) & 3] >> 9) >> 3;
            out[i].q = eqf(&eqQ, sig[i] * wave[(i + 3) & 3] >> 9) >> 3;
        }

        cL = v->out + beg * v->outw;
        cR = cL + v->outw;

        for (pos = scanL; pos < scanR && cL < cR; pos += dx) {
            int y, i, q;
            int r, g, b;
            int aa, bb;

            R = pos & 0xfff;
            L = 0xfff - R;
            s = pos >> 12;
            
            yiqA = out + s;
            yiqB = out + s + 1;
            
            /* interpolate between samples if needed */
            y = ((yiqA->y * L) >>  2) + ((yiqB->y * R) >>  2);
            i = ((yiqA->i * L) >> 14) + ((yiqB->i * R) >> 14);
            q = ((yiqA->q * L) >> 14) + ((yiqB->q * R) >> 14);
            
            /* YIQ to RGB */
            r = (((y + 3879 * i + 2556 * q) >> 12) * v->contrast) >> 8;
            g = (((y - 1126 * i - 2605 * q) >> 12) * v->contrast) >> 8;
            b = (((y - 4530 * i + 7021 * q) >> 12) * v->contrast) >> 8;
          
            if (r < 0) r = 0;
            if (g < 0) g = 0;
            if (b < 0) b = 0;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            
            aa = (r << 16 | g << 8 | b);
            bb = *cL;
            /* blend with previous color there */
            *cL++ = (((aa & 0xfefeff) >> 1) + ((bb & 0xfefeff) >> 1));
        }
        
        /* duplicate extra lines */
        ln = v->outw * sizeof(int);
        for (s = beg + 1; s < end; s++) {
            memcpy(v->out + s * v->outw, v->out + (s - 1) * v->outw, ln);
        }
    }
}
