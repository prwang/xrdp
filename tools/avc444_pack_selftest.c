/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2026
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Standalone byte-exactness driver for the AVC444 v1 fill loops
 * (PRD FR-H264-8 semantic harness, packer validation). Reads a planar
 * YUV444 file (Y then U then V, each w*h bytes), runs the REAL in-tree
 * fill_main/fill_aux (v1 mode, width_align 32) via
 * xrdp_avc444_conv_update(), and writes the two NV12 views so
 * tools/avc444_roundtrip_psnr.py selftest can byte-compare its numpy
 * packer against them.
 *
 * Build (ad hoc, against the built object like tests/xrdp does):
 *   gcc -O2 -I. -Icommon -o /tmp/pack_selftest \
 *       tools/avc444_pack_selftest.c xrdp/xrdp_avc444_convert.o \
 *       common/.libs/libcommon.a
 *
 * Usage: pack_selftest width height in.yuv444 out_main.nv12 out_aux.nv12
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xrdp/xrdp_avc444_convert.h"

int
main(int argc, char **argv)
{
    struct xrdp_avc444_conv *conv;
    unsigned char *in;
    unsigned char *buf;
    size_t area;
    size_t plane;
    FILE *fp;
    int w;
    int h;
    int i;
    int row;

    if (argc != 6)
    {
        fprintf(stderr, "usage: %s w h in.yuv444 out_main out_aux\n",
                argv[0]);
        return 1;
    }
    w = atoi(argv[1]);
    h = atoi(argv[2]);
    if (w < 1 || h < 1)
    {
        fprintf(stderr, "bad dimensions %dx%d\n", w, h);
        return 1;
    }
    conv = xrdp_avc444_conv_create(w, h, 32);
    if (conv == NULL)
    {
        fprintf(stderr, "conv_create failed\n");
        return 1;
    }
    plane = (size_t)w * h;
    in = (unsigned char *)malloc(plane * 3);
    fp = fopen(argv[3], "rb");
    if (fp == NULL || fread(in, 1, plane * 3, fp) != plane * 3)
    {
        fprintf(stderr, "cannot read %zu bytes from %s\n",
                plane * 3, argv[3]);
        return 1;
    }
    fclose(fp);
    /* conv_update wants each plane at pstride * coded_height; rows
     * past h are never read (sample_yuv clamps) but must exist */
    area = (size_t)w * conv->coded_height;
    buf = (unsigned char *)calloc(area, 3);
    for (i = 0; i < 3; i++)
    {
        for (row = 0; row < h; row++)
        {
            memcpy(buf + i * area + (size_t)row * w,
                   in + i * plane + (size_t)row * w, w);
        }
    }
    if (xrdp_avc444_conv_update(conv, buf, w, w, h) != 0)
    {
        fprintf(stderr, "conv_update failed\n");
        return 1;
    }
    fp = fopen(argv[4], "wb");
    fwrite(conv->main_nv12, 1, conv->nv12_size, fp);
    fclose(fp);
    fp = fopen(argv[5], "wb");
    fwrite(conv->aux_nv12, 1, conv->nv12_size, fp);
    fclose(fp);
    printf("%dx%d -> coded %dx%d, %d bytes per view\n",
           w, h, conv->coded_width, conv->coded_height, conv->nv12_size);
    free(buf);
    free(in);
    xrdp_avc444_conv_delete(conv);
    return 0;
}
