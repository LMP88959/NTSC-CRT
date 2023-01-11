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
#include "crt.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "ppm_rw.h"

#ifndef CMD_LINE_VERSION
#define CMD_LINE_VERSION 1
#endif

#if CMD_LINE_VERSION

#define DRV_HEADER "NTSC/CRT by EMMIR 2018-2023\n"

static int dooverwrite = 1;
static int docolor = 1;
static int field = 0;
static int progressive = 0;
static int raw = 0;
static int phase_offset = 0;

static int
stoint(char *s, int *err)
{
    char *tail;
    long val;

    errno = 0;
    *err = 0;
    val = strtol(s, &tail, 10);
    if (errno == ERANGE) {
        printf("integer out of integer range\n");
        *err = 1;
    } else if (errno != 0) {
        printf("bad string: %s\n", strerror(errno));
        *err = 1;
    } else if (*tail != '\0') {
        printf("integer contained non-numeric characters\n");
        *err = 1;
    }
    return val;
}

static void
usage(char *p)
{
    printf(DRV_HEADER);
    printf("usage: %s -m|o|f|p|r|h outwidth outheight noise phase_offset infile outfile\n", p);
    printf("sample usage: %s -op 640 480 24 3 in.ppm out.ppm\n", p);
    printf("sample usage: %s - 832 624 0 2 in.ppm out.ppm\n", p);
    printf("-- NOTE: the - after the program name is required\n");
    printf("\tphase_offset is [0, 1, 2, or 3] +1 means a color phase change of 90 degrees\n");
    printf("------------------------------------------------------------\n");
    printf("\tm : monochrome\n");
    printf("\to : do not prompt when overwriting files\n");
    printf("\tf : odd field (only meaningful in progressive mode)\n");
    printf("\tp : progressive scan (rather than interlaced)\n");
    printf("\tr : raw image (needed for images that use artifact colors)\n");
    printf("\th : print help\n");
    printf("\n");
    printf("by default, the image will be full color, interlaced, and scaled to the output dimensions\n");
}

static int
process_args(int argc, char **argv)
{
    char *flags;

    flags = argv[1];
    if (*flags == '-') {
        flags++;
    }
    for (; *flags != '\0'; flags++) {
        switch (*flags) {
            case 'm': docolor = 0;     break;
            case 'o': dooverwrite = 0; break;
            case 'f': field = 1;       break;
            case 'p': progressive = 1; break;
            case 'r': raw = 1;         break;
            case 'h': usage(argv[0]); return 0;
            default:
                fprintf(stderr, "Unrecognized flag '%c'\n", *flags);
                return 0;
        }
    }
    return 1;
}

static int
fileexist(char *n)
{
    FILE *fp = fopen(n, "r");
    if (fp) {
        fclose(fp);
        return 1;
    }
    return 0;
}

static int
promptoverwrite(char *fn)
{
    if (dooverwrite && fileexist(fn)) {
        do {
            char c = 0;
            printf("\n--- file (%s) already exists, overwrite? (y/n)\n", fn);
            scanf(" %c", &c);
            if (c == 'y' || c == 'Y') {
                return 1;
            }
            if (c == 'n' || c == 'N') {
                return 0;
            }
        } while (1);
    }
    return 1;
}

int
main(int argc, char **argv)
{
    struct NTSC_SETTINGS ntsc;
    struct CRT crt;
    int *img;
    int imgw, imgh;
    int *output = NULL;
    int outw = 832;
    int outh = 624;
    int noise = 24;
    char *input_file;
    char *output_file;
    int err = 0;
    int phase_ref[4] = { 0, 1, 0, -1 };

    if (argc < 8) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!process_args(argc, argv)) {
        return EXIT_FAILURE;
    }

    printf(DRV_HEADER);

    outw = stoint(argv[2], &err);
    if (err) {
        return EXIT_FAILURE;
    }

    outh = stoint(argv[3], &err);
    if (err) {
        return EXIT_FAILURE;
    }

    noise = stoint(argv[4], &err);
    if (err) {
        return EXIT_FAILURE;
    }

    if (noise < 0) noise = 0;

    phase_offset = stoint(argv[5], &err);
    if (err) {
        return EXIT_FAILURE;
    }
    phase_offset &= 3;

    output = calloc(outw * outh, sizeof(int));
    if (output == NULL) {
        printf("out of memory\n");
        return EXIT_FAILURE;
    }

    input_file = argv[6];
    output_file = argv[7];

    if (!ppm_read24(input_file, &img, &imgw, &imgh, calloc)) {
        printf("unable to read image\n");
        return EXIT_FAILURE;
    }
    printf("loaded %d %d\n", imgw, imgh);

    if (!promptoverwrite(output_file)) {
        return EXIT_FAILURE;
    }

    crt_init(&crt, outw, outh, output);

    ntsc.rgb = img;
    ntsc.w = imgw;
    ntsc.h = imgh;
    ntsc.as_color = docolor;
    ntsc.field = field & 1;
    ntsc.raw = raw;
    ntsc.cc[0] = phase_ref[(phase_offset + 0) & 3];
    ntsc.cc[1] = phase_ref[(phase_offset + 1) & 3];
    ntsc.cc[2] = phase_ref[(phase_offset + 2) & 3];
    ntsc.cc[3] = phase_ref[(phase_offset + 3) & 3];
    ntsc.ccs = 1;

    printf("converting to %dx%d...\n", outw, outh);
    err = 0;
    /* accumulate 4 frames */
    while (err < 4) {
        crt_2ntsc(&crt, &ntsc);
        crt_draw(&crt, noise);
        if (!progressive) {
            ntsc.field ^= 1;
            crt_2ntsc(&crt, &ntsc);
            crt_draw(&crt, noise);
        }
        err++;
    }
    if (!ppm_write24(output_file, output, outw, outh)) {
        printf("unable to write image\n");
        return EXIT_FAILURE;
    }
    printf("done\n");
    return EXIT_SUCCESS;
}
#else
#include "fw.h"
#if 0
#define XMAX 624
#define YMAX 832
#else
#define XMAX 832
#define YMAX 624
#endif
static int *video = NULL;
static VIDINFO *info;

static struct CRT crt;

static int *img;
static int imgw;
static int imgh;

static int color = 1;
static int noise = 24;
static int field = 0;
static int progressive = 0;
static int raw = 0;
static int phase_offset = 0;
static int unlocked_phase = 0; /* color phase changes every field */
static int hue = 0;

static void
updatecb(void)
{
    if (pkb_key_pressed(FW_KEY_ESCAPE)) {
        sys_shutdown();
    }

    if (pkb_key_held('q')) {
        crt.black_point += 1;
        printf("crt.black_point   %d\n", crt.black_point);
    }
    if (pkb_key_held('a')) {
        crt.black_point -= 1;
        printf("crt.black_point   %d\n", crt.black_point);
    }

    if (pkb_key_held('w')) {
        crt.white_point += 1;
        printf("crt.white_point   %d\n", crt.white_point);
    }
    if (pkb_key_held('s')) {
        crt.white_point -= 1;
        printf("crt.white_point   %d\n", crt.white_point);
    }

    if (pkb_key_held(FW_KEY_ARROW_UP)) {
        crt.brightness += 1;
        printf("%d\n", crt.brightness);
    }
    if (pkb_key_held(FW_KEY_ARROW_DOWN)) {
        crt.brightness -= 1;
        printf("%d\n", crt.brightness);
    }
    if (pkb_key_held(FW_KEY_ARROW_LEFT)) {
        crt.contrast -= 1;
        printf("%d\n", crt.contrast);
    }
    if (pkb_key_held(FW_KEY_ARROW_RIGHT)) {
        crt.contrast += 1;
        printf("%d\n", crt.contrast);
    }
    if (pkb_key_held('1')) {
        crt.saturation -= 1;
        printf("%d\n", crt.saturation);
    }
    if (pkb_key_held('2')) {
        crt.saturation += 1;
        printf("%d\n", crt.saturation);
    }
    if (pkb_key_held('3')) {
        noise -= 1;
        if (noise < 0) {
            noise = 0;
        }
        printf("%d\n", noise);
    }
    if (pkb_key_held('4')) {
        noise += 1;
        printf("%d\n", noise);
    }
    if (pkb_key_held('5')) {
        hue--;
        if (hue < 0) {
            hue = 359;
        }
        printf("%d\n", hue);
    }
    if (pkb_key_held('6')) {
        hue++;
        if (hue > 359) {
            hue = 0;
        }
        printf("%d\n", hue);
    }
    if (pkb_key_held('7')) {
        crt.hue -= 1;
        printf("%d\n", crt.hue);
    }
    if (pkb_key_held('8')) {
        crt.hue += 1;
        printf("%d\n", crt.hue);
    }
  
    if (pkb_key_pressed(FW_KEY_SPACE)) {
        color ^= 1;
    }
    if (pkb_key_pressed('r')) {
        crt_reset(&crt);
    }
    if (pkb_key_pressed('f')) {
        field ^= 1;
        printf("field: %d\n", field);
    }
    if (pkb_key_pressed('e')) {
        progressive ^= 1;
        printf("progressive: %d\n", progressive);
    }
    if (pkb_key_pressed('t')) {
        /* Analog array must be cleared since it normally doesn't get zeroed each frame
         * so active video portions that were written to in non-raw mode will not lose
         * their values resulting in the previous image being
         * displayed where the new, smaller image is not
         */
        memset(crt.analog, 0, sizeof(crt.analog));
        raw ^= 1;
        printf("raw: %d\n", raw);
    }
    if (pkb_key_pressed('p')) {
        phase_offset++;
        phase_offset &= 3;
    }
    if (pkb_key_pressed('o')) {
        unlocked_phase ^= 1;
    }
    if (unlocked_phase) {
        phase_offset++;
        phase_offset &= 3;
    }
    if (!progressive) {
        field ^= 1;
    }
}

static void
fade_phosphors(void)
{
    int i, *v;
    unsigned int c;

    v = video;

    for (i = 0; i < info->width * info->height; i++) {
        c = v[i] & 0xffffff;
        v[i] = (c >> 1 & 0x7f7f7f) +
               (c >> 2 & 0x3f3f3f) +
               (c >> 3 & 0x1f1f1f) +
               (c >> 4 & 0x0f0f0f);
    }
}

static void
displaycb(void)
{
    static struct NTSC_SETTINGS ntsc;
    static struct NES_NTSC_SETTINGS nes;
    static int fno = 0;
    int phase_ref[4] = { 0, 1, 0, -1 };
    int sn, cs;
    int i;
    
    for (i = 0; i < 4; i++) {
        crt_sincos14(&sn, &cs, (hue + i * 90) * 8192 / 180);
        phase_ref[i] = sn >> 11;
    }
    
    fade_phosphors();
    /* not necessary to clear if you're rendering on a constant region of the display */
    /* memset(crt.analog, 0, sizeof(crt.analog)); */
#if CRT_NES_MODE
    nes.data = ppu_output_256x240;
    nes.w = 256;
    nes.h = 240;
    nes.dot_crawl_offset = fno++ % 3;
    nes.as_color = cmod;
    nes.cc[0] = phase_ref[(phase_offset + 0) & 3];
    nes.cc[1] = phase_ref[(phase_offset + 1) & 3];
    nes.cc[2] = phase_ref[(phase_offset + 2) & 3];
    nes.cc[3] = phase_ref[(phase_offset + 3) & 3];
    nes.ccs = 16;
    crt_nes2ntsc(&crt, &nes);
#else
    ntsc.rgb = img;
    ntsc.w = imgw;
    ntsc.h = imgh;
    ntsc.as_color = color;
    ntsc.field = field & 1;
    ntsc.raw = raw;
    ntsc.cc[0] = phase_ref[(phase_offset + 0) & 3];
    ntsc.cc[1] = phase_ref[(phase_offset + 1) & 3];
    ntsc.cc[2] = phase_ref[(phase_offset + 2) & 3];
    ntsc.cc[3] = phase_ref[(phase_offset + 3) & 3];
    ntsc.ccs = 16;
    crt_2ntsc(&crt, &ntsc);
#endif
    
    crt_draw(&crt, noise);

    vid_blit();
    vid_sync();
}

int
main(int argc, char **argv)
{
    int werr;

    sys_init();
    sys_updatefunc(updatecb);
    sys_displayfunc(displaycb);
    sys_keybfunc(pkb_keyboard);
    sys_keybupfunc(pkb_keyboardup);

    clk_mode(FW_CLK_MODE_HIRES);
    pkb_reset();
    sys_sethz(60);
    sys_capfps(1);

    werr = vid_open("crt", XMAX, YMAX, 1, FW_VFLAG_VIDFAST);
    if (werr != FW_VERR_OK) {
        FW_error("unable to create window\n");
        return EXIT_FAILURE;
    }

    info = vid_getinfo();
    video = info->video;

    crt_init(&crt, info->width, info->height, video);

    char *input_file;
    if (argc == 1) {
        fprintf(stderr, "Please specify PPM image input file.\n");
        return EXIT_FAILURE;
    }
    input_file = argv[1];
    if (!ppm_read24(input_file, &img, &imgw, &imgh, calloc)) {
        fprintf(stderr, "unable to read image\n");
        return EXIT_FAILURE;
    }
    printf("loaded %d %d\n", imgw, imgh);

    sys_start();

    sys_shutdown();
    return EXIT_SUCCESS;
}

#endif
