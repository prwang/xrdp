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
    /* DIAGNOSTIC/interop knob: rewrite every SPS to clear
     * pic_struct_present_flag (xrdp_h264_strip_pic_struct). 2026-07-27
     * Mac bisect: nvenc declares pic_struct while xrdp strips the pic
     * timing SEI it announces; suspected VideoToolbox output-pacing
     * trigger for the measured one-frame aux/chroma lag. Default 0. */
    int avc444_ffmpeg_strip_pic_struct;
    /* EXPERIMENTAL (PRD FR-H264-8): aux-refs-aux via Windows-style
     * long-term reference slots -- the aux child encodes a normal
     * refs=1 P chain and both views are rewritten into one shared
     * frame_num chain (LT0 = main, LT1 = aux). Default 0: FR-H264-7
     * all-intra leaves remain the shipped architecture until the
     * FR-H264-8 acceptance gate (incl. the bandwidth gate) passes. */
    int avc444_ffmpeg_aux_ltr_chain;
    /* aux_ltr_chain re-key threshold: the shared frame_num value at
     * which the encoder pair is rebuilt and the EGFX surface reset
     * (BACKLOG #48). Default XRDP_H264_LTR_FRAME_NUM_REKEY (2^16-512,
     * ~18 min of continuous 30 fps animation); clamped to
     * [XRDP_H264_LTR_FRAME_NUM_REKEY_MIN, ..._MAX]. Lower it to
     * exercise the re-key boundary in a test arm without an hour of
     * animation; the MAX is a ceiling, since a higher value would let
     * a decoder meet the frame_num wrap. */
    int avc444_ffmpeg_ltr_rekey_frame_num;
    /* Emit the EGFX surface delete/create/map teardown at a re-key.
     * Default 1 (the BACKLOG #48 mechanism). Setting it to 0 MASKS the
     * surface churn from the client: the encoder is still restarted, so
     * the client still gets a real IDR with full-surface damage and the
     * shared frame_num counter still resets -- which is the only thing
     * the re-key is actually FOR. Measured 2026-07-29: macOS flashes
     * black at every surface swap, in both emission orders. */
    int avc444_ffmpeg_ltr_rekey_surface_reset;
    /* NOTE: AVC444 reference partitioning (aux encoded by a second
     * all-IDR child and spliced in as non-reference, non-IDR I leaves)
     * is NOT configurable: it is a structural requirement of the
     * backend (PRD FR-H264-7, decode-topology invariance) and is
     * always on for the AVC444 pair path. A former gfx.toml knob
     * ("aux_intra_leaf", bisect era) is ignored if present. */
    /* DIAGNOSTIC: rewrite non-IDR ref slices from explicit-MMCO
     * reference marking to sliding window (xrdp_h264_strip_mmco).
     * 2026-07-27 Mac k=1 bisect: the Mac-clean VAAPI wire uses MMCO,
     * the Mac-broken nvenc wire uses sliding window; this converts the
     * clean stream for a single-delta arm. Default 0. */
    int avc444_ffmpeg_fault_strip_mmco;
    /* DIAGNOSTIC fault injection: delay the aux stream by one pair to
     * visualize a main/aux pairing slip (2026-07-27 arm-K). Default 0;
     * the runner logs a WARNING whenever it is active. */
    int avc444_ffmpeg_fault_aux_delay;
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
