/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Koichiro Iwao
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
 */

/**
 *
 * @file xrdp_tconfig.c
 * @brief TOML config loader and structures
 * @author Koichiro Iwao
 *
 */
#ifndef _XRDP_TCONFIG_H_
#define _XRDP_TCONFIG_H_

#include "arch.h"
#include "xrdp_encoder_ffmpeg.h"

/* The number of connection types in MS-RDPBCGR 2.2.1.3.2 */
#define NUM_CONNECTION_TYPES 7
#define GFX_CONF XRDP_CFG_PATH "/gfx.toml"

/* nc stands for new config */
struct xrdp_tconfig_gfx_x264_param
{
    char preset[16];
    char tune[16];
    char profile[16];
    int vbv_max_bitrate;
    int vbv_buffer_size;
    int fps_num;
    int fps_den;
    int threads;
};

struct xrdp_tconfig_gfx_openh264_param
{
    bool_t EnableFrameSkip;
    int TargetBitrate;
    int MaxBitrate;
    float MaxFrameRate;
};

enum xrdp_tconfig_codecs
{
    XTC_H264,
    XTC_RFX
};

enum xrdp_tconfig_h264_encoders
{
    XTC_H264_X264,
    XTC_H264_OPENH264,
    XTC_H264_FFMPEG      /* external stock-ffmpeg AVC444 backend */
};

/* AVC444-vs-AVC420 selection for the external ffmpeg backend. AUTO prefers
 * AVC444 and falls back to AVC420; FORCE_420 emits AVC420 to any H.264-capable
 * client (so mstsc, which always offers AVC444, can be tested on the AVC420
 * path); FORCE_444 serves AVC444 only. FORCE_444V1 serves AVC444 but pins
 * codec id 0x000E (v1) even when the client supports v2 (ChromaV2) — a
 * per-client diagnostic/compatibility knob (e.g. the macOS Windows App
 * mis-renders v2; v1 isolates the aux-view packing). */
enum xrdp_tconfig_avc_mode
{
    XTC_AVC_AUTO = 0,
    XTC_AVC_FORCE_444,
    XTC_AVC_FORCE_420,
    XTC_AVC_FORCE_444V1
};

struct xrdp_tconfig_gfx_codec_order
{
    enum xrdp_tconfig_codecs codecs[2];
    unsigned short codec_count;
};

struct xrdp_tconfig_gfx
{
    struct xrdp_tconfig_gfx_codec_order codec;
    enum xrdp_tconfig_h264_encoders h264_encoder;
    /* store x264 parameters for each connection type */
    struct xrdp_tconfig_gfx_x264_param x264_param[NUM_CONNECTION_TYPES];
    struct xrdp_tconfig_gfx_openh264_param
        openh264_param[NUM_CONNECTION_TYPES];
    /* external stock-ffmpeg AVC444 backend (h264_encoder = "ffmpeg") */
    char avc444_ffmpeg_path[256];
    struct xrdp_avc444_encoder_args avc444_ffmpeg_encoder_args;
    int avc444_ffmpeg_chroma_align; /* coded WIDTH alignment: 16 or 32 */
    enum xrdp_tconfig_avc_mode avc444_ffmpeg_avc_mode; /* AVC444/420 select */
    /* Last-resort tail-flush (33ms same-frame drain). Default OFF: the
     * root-cause fix for the withheld tail is a low-latency encoder pipeline
     * (h264_vaapi -async_depth 1, or libx264 -tune zerolatency). Enable this
     * only when stuck on an encoder whose pipeline depth cannot be lowered. */
    int avc444_ffmpeg_tail_flush;
    /* STATIC in-band parameter-set policy (administrator declaration, like
     * encoder_args). 0 (default): the encoder repeats SPS/PPS in-band on
     * keyframes (libx264 repeat-headers=1, h264_vaapi). 1: the encoder emits
     * parameter sets only as global extradata (h264_nvenc); chain the
     * dump_extra bsf to reinsert them. The pre-confirm probe VERIFIES the
     * declared value against the real bitstream and refuses the AVC
     * candidate on mismatch; it never changes the policy at runtime
     * (PRD FR-PROBE-6). */
    int avc444_ffmpeg_dump_extra;
    /* DIAGNOSTIC/interop knob: strip buffering_period (0) and pic_timing
     * (1) SEI NALs from the encoder output (filter_units bsf). Added for
     * the 2026-07-26 macOS Windows App bisect: HRD-class bytes black the
     * Mac's RDP H264 path. Default 0 (off). */
    int avc444_ffmpeg_strip_sei;
    /* DIAGNOSTIC/interop knob: rewrite every SPS to drop
     * nal_hrd/vcl_hrd parameters from the VUI (xrdp_h264_sanitize_hrd).
     * 2026-07-27 matrix verdict: HRD in the SPS VUI alone blacks the
     * macOS Windows App even with all SEI NALs stripped. Default 0. */
    int avc444_ffmpeg_sanitize_hrd;
};

static const char *const rdpbcgr_connection_type_names[] =
{
    "default", /* for xrdp internal use */
    "modem",
    "broadband_low",
    "satellite",
    "broadband_high",
    "wan",
    "lan",
    "autodetect",
    0
};

/**
 * Provide a string representation of a codec order
 *
 * @param codec_order Codec order struct
 * @param buff Buffer for result
 * @param bufflen Length of above
 * @return Convenience copy of buff
 */
const char *
tconfig_codec_order_to_str(
    const struct xrdp_tconfig_gfx_codec_order *codec_order,
    char *buff,
    unsigned int bufflen);

/**
 * Loads the GFX config from the specified file
 *
 * @param filename Name of file to load
 * @param config Struct to receive result
 * @return 0 for success
 *
 * In the event of failure, an error is logged. A minimal
 * useable configuration is always returned
 */
int
tconfig_load_gfx(const char *filename, struct xrdp_tconfig_gfx *config);

#endif
