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
#ifndef _CRT_SINCOS_H_
#define _CRT_SINCOS_H_

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************/
/*************************** FIXED POINT SIN/COS *****************************/
/*****************************************************************************/

#define T14_2PI           16384
#define T14_MASK          (T14_2PI - 1)
#define T14_PI            (T14_2PI / 2)

extern void crt_sincos14(int *s, int *c, int n);

#ifdef __cplusplus
}
#endif

#endif /* _CRT_SINCOS_H_ */
