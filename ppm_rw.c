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
#include "ppm_rw.h"

#include <stdlib.h>
#include <stdio.h>

extern int
ppm_read24(char *file,
           int **out_color, int *out_w, int *out_h,
           void *(*calloc_func)(int, int))
{
	FILE *f;
	long beg;
	int *out;
    int i, npix, header = 0;

	if (!(f = fopen(file, "rb"))) {
	    printf("[ppm_rw] unable to open ppm: %s\n", file);
		return 0;
	}
	while (header < 3) {
		char buf[64];
		if (!fgets(buf, sizeof(buf), f)) {
		    printf("[ppm_rw] invalid ppm [no data]: %s\n", file);
			fclose(f);
			return 0;
		}
		if (buf[0] == '#') {
			continue;
		}
		switch (header) {
			case 0:
				if (buf[0] != 'P' || buf[1] != '6') {
				    printf("[ppm_rw] invalid ppm [not P6]: %s\n", file);
					fclose(f);
					return 0;
				}
				break;
			case 1:
				if (sscanf(buf, "%d %d", out_w, out_h) != 2) {
				    printf("[ppm_rw] invalid ppm [no dim]: %s\n", file);
					fclose(f);
					return 0;
				}
				break;
			case 2:
				if (atoi(buf) > 0xff) {
				    printf("[ppm_rw] invalid ppm [>255]: %s\n", file);
					fclose(f);
					return 0;
				}
				break;
			default:
				break;
		}
		header++;
	}
	beg = ftell(f);
	npix = *out_w * *out_h;
	*out_color = calloc_func(npix, sizeof(int));
	if (*out_color == NULL) {
	    printf("[ppm_rw] out of memory loading ppm: %s\n", file);
		fclose(f);
		return 0;
	}
	out = *out_color;
	/*printf("ppm 24-bit w: %d, h: %d, s: %d\n", *out_w, *out_h, npix);*/
	for (i = 0; i < npix; i++) {
		int r = fgetc(f) & 0xff;
		int g = fgetc(f) & 0xff;
		int b = fgetc(f) & 0xff;
		if (feof(f)) {
			printf("[ppm_rw] early eof: %s\n", file);
			fclose(f);
			return 0;
		}
		out[i] = (r << 16 | g << 8 | b);
	}
	fseek(f, beg, SEEK_SET);
	return 1;
}

extern int
ppm_write24(char *name, int *color, int w, int h)
{
    FILE *fp;
    int i, npix, c;

    if (!(fp = fopen(name, "wb"))) {
        printf("[ppm_rw] failed to write file: %s\n", name);
        return 0;
    }

    fprintf(fp, "P6\n%d %d\n255\n", w, h);

    npix = w * h;
    for (i = 0; i < npix; i++) {
        c = *color++;
        fputc((c >> 16 & 0xff), fp);
        fputc((c >> 8  & 0xff), fp);
        fputc((c >> 0  & 0xff), fp);
    }
    fclose(fp);
    return 1;
}
