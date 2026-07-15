/* Offline AVC444 traffic measurement: read a raw BGRA frame, run xrdp's real
 * converter to produce the main + aux NV12 views, write them out. ffmpeg then
 * encodes each (same contract as the live path) so we can compare the 444
 * (main+aux) vs 420 (main-only) intra-frame byte cost. */
#include <stdio.h>
#include <stdlib.h>
#include "xrdp_avc444_convert.h"

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        fprintf(stderr, "usage: %s in.bgra W H main.nv12 aux.nv12\n", argv[0]);
        return 2;
    }
    int w = atoi(argv[2]);
    int h = atoi(argv[3]);
    long n = (long)w * h * 4;
    unsigned char *xrgb = malloc(n);
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(xrgb, 1, n, f) != (size_t)n)
    {
        fprintf(stderr, "read fail\n");
        return 1;
    }
    fclose(f);
    struct xrdp_avc444_conv *c = xrdp_avc444_conv_create(w, h, 32);
    if (!c)
    {
        return 1;
    }
    c->chroma_v2 = 1;                 /* AVC444 v2 packing (main = 2x2 avg) */
    if (xrdp_avc444_conv_update(c, xrgb, w * 4, w, h) != 0)
    {
        return 1;
    }
    FILE *m = fopen(argv[4], "wb");
    fwrite(c->main_nv12, 1, c->nv12_size, m);
    fclose(m);
    FILE *a = fopen(argv[5], "wb");
    fwrite(c->aux_nv12, 1, c->nv12_size, a);
    fclose(a);
    printf("coded %dx%d nv12_size=%d\n", c->coded_width, c->coded_height,
           c->nv12_size);
    return 0;
}
