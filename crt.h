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
#ifndef _CRT_H_
#define _CRT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* crt.h
 *
 * An interface to convert a digital image to an analog NTSC signal
 * and decode the NTSC signal back into a digital image.
 * Can easily be integrated into real-time applications
 * or be used as a command-line tool.
 *
 */

#define CRT_NES_MODE 0
#define CRT_NES_HIRES 1

/* do bloom emulation (side effect: makes screen have black borders) */
#define CRT_DO_BLOOM    0
#define CRT_DO_VSYNC    1  /* look for VSYNC */
#define CRT_DO_HSYNC    1  /* look for HSYNC */
/* 0 = vertical  chroma (228 chroma clocks per line) */
/* 1 = checkered chroma (227.5 chroma clocks per line) */
/* 2 = sawtooth  chroma (227.3 chroma clocks per line) */
#define CRT_CHROMA_PATTERN 1

#if CRT_NES_MODE
#undef CRT_CHROMA_PATTERN
#define CRT_CHROMA_PATTERN 2 /* force sawtooth pattern */
#endif

/* chroma clocks (subcarrier cycles) per line */
#if (CRT_CHROMA_PATTERN == 1)
#define CRT_CC_LINE 2275
#elif (CRT_CHROMA_PATTERN == 2)
#define CRT_CC_LINE 2273
#else
/* this will give the 'rainbow' effect in the famous waterfall scene */
#define CRT_CC_LINE 2280
#endif

/* NOTE, in general, increasing CRT_CB_FREQ reduces blur and bleed */
#if CRT_NES_MODE
#if CRT_NES_HIRES
#define CRT_CB_FREQ     6 /* carrier frequency relative to sample rate */
#else
#define CRT_CB_FREQ     3 /* carrier frequency relative to sample rate */
#endif
#else
#define CRT_CB_FREQ     4 /* carrier frequency relative to sample rate */
#endif
#define CRT_HRES        (CRT_CC_LINE * CRT_CB_FREQ / 10) /* horizontal res */
#define CRT_VRES        262                       /* vertical resolution */
#define CRT_INPUT_SIZE  (CRT_HRES * CRT_VRES)

#define CRT_TOP         21     /* first line with active video */
#define CRT_BOT         261    /* final line with active video */
#define CRT_LINES       (CRT_BOT - CRT_TOP) /* number of active video lines */

struct CRT {
    signed char analog[CRT_INPUT_SIZE];
    signed char inp[CRT_INPUT_SIZE]; /* CRT input, can be noisy */
    int hsync, vsync; /* used internally to keep track of sync over frames */
    int hue, brightness, contrast, saturation; /* common monitor settings */
    int black_point, white_point; /* user-adjustable */
    int outw, outh; /* output width/height */
    int *out; /* output image */
};

/* Initializes the library. Sets up filters.
 *   w   - width of the output image
 *   h   - height of the output image
 *   out - pointer to output image data 32-bit RGB packed as 0xXXRRGGBB
 */
extern void crt_init(struct CRT *v, int w, int h, int *out);

/* Updates the output image parameters
 *   w   - width of the output image
 *   h   - height of the output image
 *   out - pointer to output image data 32-bit RGB packed as 0xXXRRGGBB
 */
extern void crt_resize(struct CRT *v, int w, int h, int *out);

/* Resets the CRT settings back to their defaults */
extern void crt_reset(struct CRT *v);

struct NTSC_SETTINGS {
    const int *rgb; /* 32-bit RGB image data (packed as 0xXXRRGGBB) */
    int w, h;       /* width and height of image */
    int raw;        /* 0 = scale image to fit monitor, 1 = don't scale */
    int as_color;   /* 0 = monochrome, 1 = full color */
    int field;      /* 0 = even, 1 = odd */
    /* color carrier sine wave.
     * ex: { 0, 1, 0, -1 }
     * ex: { 1, 0, -1, 0 }
     */
    int cc[4];
    /* scale value for values in cc
     * for example, if using { 0, 1, 0, -1 }, ccs should be 1.
     * however, if using { 0, 16, 0, -16 }, ccs should be 16.
     * For best results, don't scale the cc values more than 16.
     */
    int ccs;
};

/* Convert RGB image to analog NTSC signal
 *   s - struct containing settings to apply to this field
 */
extern void crt_2ntsc(struct CRT *v, struct NTSC_SETTINGS *s);
    
/* Convert RGB image to analog NTSC signal and stretch it to fill
 * the entire active video portion of the NTSC signal.
 * Does not perform the slight horizontal blending which gets done in crt_2ntsc.
 * Good for seeing test patterns.
 *   s - struct containing settings to apply to this field
 *       NOTE: raw is ignored in this 'FS' (fill screen) version of the 2ntsc function
 */
extern void crt_2ntscFS(struct CRT *v, struct NTSC_SETTINGS *s);

struct NES_NTSC_SETTINGS {
    const unsigned short *data; /* 6 or 9-bit NES 'pixels' */
    int w, h;       /* width and height of image */
    int raw;        /* 0 = scale image to fit monitor, 1 = don't scale */
    int as_color;   /* 0 = monochrome, 1 = full color */
    int dot_crawl_offset; /* 0, 1, or 2 */
    /* NOTE: NES mode is always progressive */
    /* color carrier sine wave.
     * ex: { 0, 1, 0, -1 }
     * ex: { 1, 0, -1, 0 }
     */
    int cc[4];      
    /* scale value for values in cc
     * for example, if using { 0, 1, 0, -1 }, ccs should be 1.
     * however, if using { 0, 16, 0, -16 }, ccs should be 16.
     * For best results, don't scale the cc values more than 16.
     */
    int ccs;
};

/* Convert NES pixel data (generally 256x240) to analog NTSC signal
 *   s - struct containing settings to apply to this field
 */
extern void crt_nes2ntsc(struct CRT *v, struct NES_NTSC_SETTINGS *s);

/* Decodes the NTSC signal generated by crt_2ntsc()
 *   noise - the amount of noise added to the signal (0 - inf)
 */
extern void crt_draw(struct CRT *v, int noise);

/* Exposed utility function */
extern void crt_sincos14(int *s, int *c, int n);

#ifdef __cplusplus
}
#endif

#endif
