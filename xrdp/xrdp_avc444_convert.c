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
 * MS-RDPEGFX AVC444 v1 view reconstruction and full-range BT.709 color.
 *
 * The two output views are the exact inverse of the reference decoder
 * combination (MS-RDPEGFX 3.3.8.3.2). Planar-to-NV12 note: the encoder
 * feeds a single ffmpeg rawvideo input, so both the main and auxiliary
 * pictures are NV12 at identical 16-aligned coded dimensions.
 *
 * U/V plane order: the main view carries U in the first chroma component
 * and V in the second (H.264 Cb/Cr semantics), matching xrdp's existing
 * AVC420 path and the reference decoder. The reference *encoder* in
 * FreeRDP swaps the main-view U/V, which is inconsistent with its own
 * decoder and the specification figure; that swap is intentionally not
 * reproduced here.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "xrdp_avc444_convert.h"
#include "os_calls.h"

/*****************************************************************************/
/* arithmetic floor of x / 256 (not C truncation toward zero)              */
static int
floor_shr8(int x)
{
    if (x >= 0)
    {
        return x >> 8;
    }
    return -(((-x) + 255) >> 8);
}

/*****************************************************************************/
static int
clamp8(int v)
{
    if (v < 0)
    {
        return 0;
    }
    if (v > 255)
    {
        return 255;
    }
    return v;
}

/*****************************************************************************/
static int
clampi(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/*****************************************************************************/
void
xrdp_avc444_rgb_to_yuv709fr(int r, int g, int b, int *y, int *u, int *v)
{
    *y = clamp8(floor_shr8(54 * r + 183 * g + 18 * b));
    *u = clamp8(floor_shr8(-29 * r - 99 * g + 128 * b) + 128);
    *v = clamp8(floor_shr8(128 * r - 116 * g - 12 * b) + 128);
}

/*****************************************************************************/
/* fetch the source pixel at (sx,sy) with edge replication, decode YUV     */
static void
sample_yuv(const unsigned char *xrgb, int stride, int w, int h,
           int sx, int sy, int *y, int *u, int *v)
{
    unsigned int pixel;
    int r;
    int g;
    int b;

    sx = clampi(sx, 0, w - 1);
    sy = clampi(sy, 0, h - 1);
    memcpy(&pixel, xrgb + (size_t)sy * stride + (size_t)sx * 4, 4);
    r = (int)((pixel >> 16) & 0xff);
    g = (int)((pixel >> 8) & 0xff);
    b = (int)(pixel & 0xff);
    xrdp_avc444_rgb_to_yuv709fr(r, g, b, y, u, v);
}

/*****************************************************************************/
static int
round_up_16(int v)
{
    return (v + 15) & ~15;
}

/*****************************************************************************/
/* round v up to a multiple of align (align must be a power of two); the
 * coded WIDTH uses this so mstsc (which derives the ChromaV2 U|V split from a
 * 32-aligned width) and FreeRDP (16-aligned) can be matched via width_align. */
static int
round_up(int v, int align)
{
    return (v + align - 1) & ~(align - 1);
}

/*****************************************************************************/
struct xrdp_avc444_conv *
xrdp_avc444_conv_create(int actual_width, int actual_height, int width_align)
{
    struct xrdp_avc444_conv *self;

    if (actual_width < 1 || actual_height < 1 ||
            actual_width > 16384 || actual_height > 16384)
    {
        return NULL;
    }
    /* only 16 or 32 are meaningful; anything else falls back to 16 (the H.264
     * macroblock size), which is the historic behavior */
    if (width_align != 32)
    {
        width_align = 16;
    }
    self = (struct xrdp_avc444_conv *)g_malloc(sizeof(*self), 1);
    if (self == NULL)
    {
        return NULL;
    }
    self->actual_width = actual_width;
    self->actual_height = actual_height;
    self->coded_width = round_up(actual_width, width_align);
    self->coded_height = round_up_16(actual_height);
    /* NV12: Y plane (cw*ch) followed by interleaved UV plane (cw*ch/2) */
    self->nv12_size = self->coded_width * self->coded_height +
                      self->coded_width * (self->coded_height / 2);
    self->main_nv12 = (unsigned char *)g_malloc(self->nv12_size, 1);
    self->aux_nv12 = (unsigned char *)g_malloc(self->nv12_size, 1);
    if (self->main_nv12 == NULL || self->aux_nv12 == NULL)
    {
        xrdp_avc444_conv_delete(self);
        return NULL;
    }
    return self;
}

/*****************************************************************************/
void
xrdp_avc444_conv_delete(struct xrdp_avc444_conv *self)
{
    if (self == NULL)
    {
        return;
    }
    g_free(self->main_nv12);
    g_free(self->aux_nv12);
    g_free(self);
}

/*****************************************************************************/
/* average the 2x2 chroma block whose top-left source pixel is (2cx,2cy)    */
static void
sample_chroma_avg(const unsigned char *xrgb, int stride, int w, int h,
                  int cx, int cy, int *u, int *v)
{
    int y0;
    int u0;
    int v0;
    int u1;
    int v1;
    int u2;
    int v2;
    int u3;
    int v3;

    sample_yuv(xrgb, stride, w, h, 2 * cx, 2 * cy, &y0, &u0, &v0);
    sample_yuv(xrgb, stride, w, h, 2 * cx + 1, 2 * cy, &y0, &u1, &v1);
    sample_yuv(xrgb, stride, w, h, 2 * cx, 2 * cy + 1, &y0, &u2, &v2);
    sample_yuv(xrgb, stride, w, h, 2 * cx + 1, 2 * cy + 1, &y0, &u3, &v3);
    *u = (u0 + u1 + u2 + u3 + 2) / 4;
    *v = (v0 + v1 + v2 + v3 + 2) / 4;
}

/*****************************************************************************/
/* main view: B1 luma (identity), B2/B3 chroma at (even-col/even-row).      */
/* v1 stores a point sample; v2 and plain AVC420 (main_only) store the 2x2  */
/* block average (no U/V swap). */
static void
fill_main(struct xrdp_avc444_conv *self,
          const unsigned char *xrgb, int stride, int w, int h)
{
    const int cw = self->coded_width;
    const int ch = self->coded_height;
    unsigned char *yp = self->main_nv12;
    unsigned char *uvp = self->main_nv12 + cw * ch;
    int x;
    int y;
    int cx;
    int cy;
    int yy;
    int uu;
    int vv;

    for (y = 0; y < ch; y++)
    {
        for (x = 0; x < cw; x++)
        {
            sample_yuv(xrgb, stride, w, h, x, y, &yy, &uu, &vv);
            yp[y * cw + x] = (unsigned char)yy;
        }
    }
    for (cy = 0; cy < ch / 2; cy++)
    {
        for (cx = 0; cx < cw / 2; cx++)
        {
            if (self->chroma_v2 || self->main_only)
            {
                sample_chroma_avg(xrgb, stride, w, h, cx, cy, &uu, &vv);
            }
            else
            {
                sample_yuv(xrgb, stride, w, h, 2 * cx, 2 * cy, &yy, &uu, &vv);
            }
            uvp[cy * cw + 2 * cx] = (unsigned char)uu;
            uvp[cy * cw + 2 * cx + 1] = (unsigned char)vv;
        }
    }
}

/*****************************************************************************/
/* auxiliary view: B4/B5 odd-row chroma banded into the aux luma plane,    */
/* B6/B7 odd-col/even-row chroma into the aux chroma plane                 */
static void
fill_aux(struct xrdp_avc444_conv *self,
         const unsigned char *xrgb, int stride, int w, int h)
{
    const int cw = self->coded_width;
    const int ch = self->coded_height;
    unsigned char *yp = self->aux_nv12;
    unsigned char *uvp = self->aux_nv12 + cw * ch;
    int x;
    int y;
    int cx;
    int cy;
    int yy;
    int uu;
    int vv;
    int u_ctr = 0;
    int v_ctr = 0;

    /* B4 and B5: walk aux luma rows in 16-line bands; the first 8 lines of
     * each band carry consecutive odd rows of U, the next 8 carry odd rows
     * of V. Counters advance even for rows that fall off the bottom, so the
     * layout stays aligned to the reference decoder. */
    for (y = 0; y < ch; y++)
    {
        int use_u = (y % 16) < 8;
        int pos = use_u ? (2 * u_ctr + 1) : (2 * v_ctr + 1);
        if (use_u)
        {
            u_ctr++;
        }
        else
        {
            v_ctr++;
        }
        if (pos >= h)
        {
            /* row not decoded by the client; keep deterministic content by
             * replicating the previous row (row 0 is always valid) */
            if (y > 0)
            {
                memcpy(yp + y * cw, yp + (y - 1) * cw, cw);
            }
            else
            {
                memset(yp + y * cw, 0, cw);
            }
            continue;
        }
        for (x = 0; x < cw; x++)
        {
            sample_yuv(xrgb, stride, w, h, x, pos, &yy, &uu, &vv);
            yp[y * cw + x] = (unsigned char)(use_u ? uu : vv);
        }
    }
    /* B6 and B7: odd-column, even-row chroma */
    for (cy = 0; cy < ch / 2; cy++)
    {
        for (cx = 0; cx < cw / 2; cx++)
        {
            sample_yuv(xrgb, stride, w, h, 2 * cx + 1, 2 * cy, &yy, &uu, &vv);
            uvp[cy * cw + 2 * cx] = (unsigned char)uu;
            uvp[cy * cw + 2 * cx + 1] = (unsigned char)vv;
        }
    }
}

/*****************************************************************************/
/* auxiliary view (ChromaV2, codec id 0x000F): the aux luma plane carries   */
/* odd-column chroma for every row (U in the left half, V in the right), and */
/* the aux chroma plane carries the even-column/odd-row chroma. This is the  */
/* exact inverse of FreeRDP general_ChromaV2ToYUV444 (MS-RDPEGFX 3.3.8.3.3). */
static void
fill_aux_v2(struct xrdp_avc444_conv *self,
            const unsigned char *xrgb, int stride, int w, int h)
{
    const int cw = self->coded_width;
    const int ch = self->coded_height;
    unsigned char *yp = self->aux_nv12;
    unsigned char *uvp = self->aux_nv12 + cw * ch;
    int x;
    int y;
    int cx;
    int cy;
    int row;
    int yy;
    int ua;
    int va;
    int ub;
    int vb;

    /* B4/B5: odd-column chroma for every row, U in [0,cw/2), V in [cw/2,cw) */
    for (y = 0; y < ch; y++)
    {
        for (cx = 0; cx < cw / 2; cx++)
        {
            sample_yuv(xrgb, stride, w, h, 2 * cx + 1, y, &yy, &ua, &va);
            yp[y * cw + cx] = (unsigned char)ua;
            yp[y * cw + cw / 2 + cx] = (unsigned char)va;
        }
    }
    /* B6-B9: even-column/odd-row chroma, interleaved into the aux chroma
     * plane. Deinterleaved by the decoder into an aux U plane (columns 4x,
     * left half U / right half V) and an aux V plane (columns 4x+2). */
    for (cy = 0; cy < ch / 2; cy++)
    {
        row = 2 * cy + 1;
        for (x = 0; x < cw / 4; x++)
        {
            sample_yuv(xrgb, stride, w, h, 4 * x, row, &yy, &ua, &va);
            sample_yuv(xrgb, stride, w, h, 4 * x + 2, row, &yy, &ub, &vb);
            uvp[cy * cw + 2 * x] = (unsigned char)ua;
            uvp[cy * cw + 2 * x + 1] = (unsigned char)ub;
            uvp[cy * cw + 2 * (cw / 4 + x)] = (unsigned char)va;
            uvp[cy * cw + 2 * (cw / 4 + x) + 1] = (unsigned char)vb;
        }
    }
}

/*****************************************************************************/
int
xrdp_avc444_conv_update(struct xrdp_avc444_conv *self,
                        const unsigned char *xrgb, int stride,
                        int width, int height)
{
    if (self == NULL || xrgb == NULL)
    {
        return 1;
    }
    if (width != self->actual_width || height != self->actual_height)
    {
        return 1;
    }
    if (stride < width * 4)
    {
        return 1;
    }
    fill_main(self, xrgb, stride, width, height);
    if (self->main_only)
    {
        /* plain AVC420: no auxiliary chroma view */
    }
    else if (self->chroma_v2)
    {
        fill_aux_v2(self, xrgb, stride, width, height);
    }
    else
    {
        fill_aux(self, xrgb, stride, width, height);
    }
    return 0;
}
