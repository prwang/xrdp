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
 * MS-RDPEGFX AVC444 view reconstruction and full-range BT.709 color.
 *
 * Reconstructs the two YUV420 (NV12) views MS-RDPEGFX AVC444 requires from a
 * full-chroma XRGB8888 source: a main YUV420 view and an auxiliary chroma
 * view. Both views share the same 16-aligned coded dimensions because a
 * single ffmpeg rawvideo input reads fixed-size frames (PRD FR-IN-1). This
 * module contains no external dependency; it is pure integer logic and is
 * unit tested against specification vectors.
 *
 * Two auxiliary packings are supported, selected by chroma_v2:
 *   v1 (ChromaV1, codec id 0x000E) - the (even,even) main chroma is a point
 *     sample and the aux view bands odd-row chroma; the FreeRDP decoder
 *     extrapolates the (even,even) chroma and overshoots at sharp saturated
 *     edges (the magenta "burr").
 *   v2 (ChromaV2, codec id 0x000F) - the main chroma is the 2x2 block average
 *     and the aux view carries odd-column chroma for every row plus the
 *     even-column/odd-row chroma, so the same decoder filter reconstructs the
 *     true chroma instead of overshooting (MS-RDPEGFX 3.3.8.3.3).
 */

#ifndef _XRDP_AVC444_CONVERT_H
#define _XRDP_AVC444_CONVERT_H

/**
 * MS-RDPEGFX full-range BT.709 forward transform for one pixel.
 *
 * Y = ( 54*R + 183*G +  18*B) >> 8
 * U = ((-29*R -  99*G + 128*B) >> 8) + 128
 * V = ((128*R - 116*G -  12*B) >> 8) + 128
 *
 * The >> 8 is an arithmetic floor (not C truncation-toward-zero) and each
 * result is clamped to [0,255]. Inputs are 8-bit [0,255].
 */
void
xrdp_avc444_rgb_to_yuv709fr(int r, int g, int b, int *y, int *u, int *v);

struct xrdp_avc444_conv
{
    int actual_width;
    int actual_height;
    int coded_width;   /* actual_width  rounded up to a multiple of 16 */
    int coded_height;  /* actual_height rounded up to a multiple of 16 */
    int nv12_size;     /* bytes of one NV12 picture at coded dimensions  */
    int chroma_v2;     /* 0 = AVC444 v1 (ChromaV1); 1 = AVC444 v2         */
    unsigned char *main_nv12; /* persistent main YUV420 (NV12) view      */
    unsigned char *aux_nv12;  /* persistent auxiliary chroma (NV12) view */
};

/**
 * Create a converter for a surface of the given visible dimensions.
 * Returns NULL on invalid dimensions or allocation failure.
 */
struct xrdp_avc444_conv *
xrdp_avc444_conv_create(int actual_width, int actual_height);

void
xrdp_avc444_conv_delete(struct xrdp_avc444_conv *self);

/**
 * Reconstruct both complete views from the full XRGB surface.
 *
 * xrgb    - host-order a8r8g8b8 pixels; each pixel read as a native 32-bit
 *           value, R=(p>>16)&0xff, G=(p>>8)&0xff, B=p&0xff (alpha ignored).
 * stride  - bytes per source row (>= actual_width * 4).
 * width/height - must equal the converter's actual dimensions.
 *
 * Returns 0 on success, non-zero on argument mismatch.
 */
int
xrdp_avc444_conv_update(struct xrdp_avc444_conv *self,
                        const unsigned char *xrgb, int stride,
                        int width, int height);

#endif /* _XRDP_AVC444_CONVERT_H */
