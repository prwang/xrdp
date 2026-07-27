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
 * External stock-ffmpeg AVC444 process runner (PRD sections 8.4, 8.6, 8.13).
 *
 * Spawns one persistent stock ffmpeg child that encodes the main and
 * auxiliary AVC444 views through one libx264 H.264 stream, demuxes its NUT
 * stdout in-tree, validates the H.264, and returns encoded pairs. No shell
 * is invoked and no FFmpeg library is linked. All structural argv is owned
 * by xrdp; the child is driven with a nonblocking poll() loop.
 *
 * Latency note: the runner is SYNCHRONOUS -- encode_pair()/encode_single()
 * wait (bounded) for the just-submitted picture(s) and return exactly that
 * frame, so the H.264 content always matches the caller's damage region.
 * Returning an older picture instead (the previous pipelined design) shipped
 * stale pixels under the current region; region-strict clients (mstsc) then
 * displayed a permanently stale screen (see the header of the .c file).
 * Requires a shallow encoder pipeline (h264_vaapi -async_depth 1, libx264
 * -tune zerolatency; ~3-9ms/picture): a pipeline that cannot return the
 * submitted picture without more input times out and errors. flush_next()
 * closes the input and drains the remainder (reset/teardown). See PRD s25.
 */

#ifndef _XRDP_ENCODER_FFMPEG_H
#define _XRDP_ENCODER_FFMPEG_H

#include <stddef.h>

/*
 * Verbatim encoder-argument passthrough.
 *
 * xrdp owns only two hard contracts in the ffmpeg command line: the INPUT
 * (raw NV12 on pipe:3 at the 16-aligned coded size, fed by the AVC444
 * converter) and the OUTPUT (Annex-B H.264 wrapped in NUT on pipe:1, which
 * the in-tree NUT demux and h264 parser require). Everything in between --
 * the codec choice (-c:v) and all of its tuning -- is supplied verbatim by
 * the administrator via gfx.toml [avc444_ffmpeg] encoder_args and is not
 * enumerated or interpreted by xrdp. Each array element becomes exactly one
 * execve() argv token (there is no shell, so no tokenization or quoting),
 * which is what makes hardware encoders (nvenc/qsv/vaapi) expressible without
 * changing xrdp. gfx.toml is root-owned admin config, trusted like sshd_config.
 *
 * The chosen encoder MUST emit decodable Annex-B H.264 (for libx264 include
 * repeat-headers=1 so every IDR carries SPS/PPS); otherwise the client cannot
 * decode. See the gfx.toml man page for the contract and examples.
 */
#define XRDP_AVC444_MAX_ENC_ARGS 64
#define XRDP_AVC444_ENC_ARG_LEN  256

struct xrdp_avc444_encoder_args
{
    char arg[XRDP_AVC444_MAX_ENC_ARGS][XRDP_AVC444_ENC_ARG_LEN];
    int count;
};

struct xrdp_ffmpeg_avc444_config
{
    char path[256];                 /* absolute ffmpeg path                */
    struct xrdp_avc444_encoder_args encoder_args; /* verbatim -c:v + tuning */
    int chroma_align;               /* coded WIDTH alignment 16 or 32; must  */
    /* match the converter's width_align     */
    int fault_aux_delay;            /* DIAGNOSTIC: ship previous pair's aux */
    /* (one-frame chroma slip, bisect arm-K)*/
    int sanitize_hrd;               /* rewrite SPS NALs to drop VUI HRD     */
    int strip_pic_struct;           /* clear VUI pic_struct_present_flag    */
    /* (xrdp_h264_sanitize_hrd); the 2026-  */
    /* 07-27 matrix convicted SPS HRD alone */
    /* of blacking the macOS Windows App    */
    int strip_sei;                  /* chain filter_units to remove         */
    /* buffering_period(0)/pic_timing(1)     */
    /* SEI NALs (macOS Windows App interop)  */
    int use_dump_extra;             /* chain the dump_extra bsf: ONLY for    */
    /* encoders with no in-band SPS/PPS      */
    /* (extradata only, e.g. h264_nvenc).    */
    /* Unconditional use duplicates the      */
    /* parameter sets on encoders that DO    */
    /* repeat in-band (libx264/h264_vaapi)   */
    /* and strict decoders then refuse to    */
    /* present (macOS Windows App: black).   */
    /* STATIC administrator policy (gfx.toml */
    /* [avc444_ffmpeg] dump_extra), verified */
    /* -- never changed -- by the probe      */
    /* (PRD FR-PROBE-6).                     */
    int desktop_fps;                /* coded rate is 2x this               */
    int stream_ready_timeout_ms;
    int picture_timeout_ms;
    int pair_timeout_ms;
    int terminate_grace_ms;
    size_t max_nut_header_bytes;
    size_t max_encoded_picture_bytes;
    size_t max_encoded_pair_bytes;
};

/** Populate cfg with the MVP defaults (path left empty). */
void
xrdp_ffmpeg_avc444_config_default(struct xrdp_ffmpeg_avc444_config *cfg);

/**
 * Fill args with the built-in default encoder block used when gfx.toml does
 * not supply an explicit [avc444_ffmpeg] encoder_args list. Shared by the
 * config default and the tconfig loader so both stay identical.
 */
void
xrdp_ffmpeg_avc444_default_encoder_args(struct xrdp_avc444_encoder_args *args);

struct xrdp_ffmpeg_avc444_metrics
{
    unsigned long long input_bytes;
    unsigned long long restarts;
    unsigned long long timeouts;
    unsigned long long parser_errors;
    unsigned long long pairs_completed;
    unsigned long long stderr_warnings;
};

struct xrdp_avc444_encoded_pair
{
    unsigned long long generation;
    unsigned long long desktop_sequence;
    const unsigned char *main_data;  /* into handle buffers; valid until    */
    int main_len;                    /* the next encode_pair / delete       */
    int main_keyframe;
    const unsigned char *aux_data;
    int aux_len;
};

struct xrdp_ffmpeg_avc444;

/* Probe outcome classes (PRD FR-PROBE-6). Only CONTENT_REJECT is
 * deterministic evidence about the encoder's bitstream; every other
 * failure is environmental (cold GPU init, missing binary, broken driver)
 * and MUST NOT be used to change the header policy. */
enum xrdp_ffmpeg_probe_result
{
    XRDP_FFMPEG_PROBE_OK = 0,
    XRDP_FFMPEG_PROBE_BAD_CONFIG,     /* invalid cfg/dims (caller error)   */
    XRDP_FFMPEG_PROBE_SPAWN_FAIL,     /* fork/pipe failure                 */
    XRDP_FFMPEG_PROBE_TIMEOUT,        /* no verdict within the deadline    */
    XRDP_FFMPEG_PROBE_STREAM_ERROR,   /* early EOF, NUT parse error,       */
    /*                                   non-monotonic pts, write failure  */
    XRDP_FFMPEG_PROBE_CONTENT_REJECT  /* bitstream violates the declared   */
    /*                                   header policy (missing or         */
    /*                                   duplicated in-band SPS/PPS)       */
};

/** Short stable name for a probe result, for logging. */
const char *
xrdp_ffmpeg_probe_result_str(enum xrdp_ffmpeg_probe_result res);

/**
 * Bounded behavioral verification (PRD FR-PROBE): spawn the exact command
 * at the given coded dimensions, submit four distinguishable NV12 pictures
 * in main/aux order, verify one ordered Annex-B packet per picture with
 * exactly one in-band SPS (plus PPS and IDR) in the first, then terminate
 * and reap. Verifies the cfg->use_dump_extra the CALLER declared; never
 * retries with a different policy. Logs the outcome class, packet count,
 * elapsed time, child stderr and child exit status.
 */
enum xrdp_ffmpeg_probe_result
xrdp_ffmpeg_avc444_probe(const struct xrdp_ffmpeg_avc444_config *cfg,
                         int coded_width, int coded_height);

/**
 * Create a runner and spawn the child for the given visible dimensions.
 * Waits (bounded) for the NUT stream to become ready. Returns NULL on
 * failure.
 */
struct xrdp_ffmpeg_avc444 *
xrdp_ffmpeg_avc444_create(const struct xrdp_ffmpeg_avc444_config *cfg,
                          int actual_width, int actual_height);

/* encode_pair / flush_next return codes */
#define XRDP_FFMPEG_PAIR_READY   0  /* *result filled with a completed pair */
#define XRDP_FFMPEG_PAIR_PENDING 2  /* submitted; no completed pair yet     */
#define XRDP_FFMPEG_PAIR_ERROR   1  /* child killed/reaped; recreate handle */
#define XRDP_FFMPEG_PAIR_DONE     3 /* flush drained; no more pairs         */

/**
 * Submit one pair and return the oldest completed pair. main_nv12/aux_nv12
 * are each coded_width*coded_height*3/2 bytes of NV12 at the handle's coded
 * dimensions. Returns XRDP_FFMPEG_PAIR_READY (result filled; result->
 * desktop_sequence is the sequence originally submitted for that pair),
 * XRDP_FFMPEG_PAIR_PENDING (nothing ready yet due to pipeline latency), or
 * XRDP_FFMPEG_PAIR_ERROR. Result pointers are valid until the next call.
 */
int
xrdp_ffmpeg_avc444_encode_pair(struct xrdp_ffmpeg_avc444 *self,
                               const unsigned char *main_nv12,
                               const unsigned char *aux_nv12,
                               int nv12_size,
                               unsigned long long desktop_sequence,
                               struct xrdp_avc444_encoded_pair *result);

/**
 * Single-view variant for plain AVC420 (codec id 0x000B): submit one NV12
 * picture and return the oldest completed encoded picture in result->main_*
 * (result->aux_* are cleared). Same pipeline latency and return codes as
 * encode_pair. A handle must be driven either by encode_pair (AVC444) or by
 * encode_single (AVC420), not both.
 */
int
xrdp_ffmpeg_avc444_encode_single(struct xrdp_ffmpeg_avc444 *self,
                                 const unsigned char *nv12,
                                 int nv12_size,
                                 unsigned long long desktop_sequence,
                                 struct xrdp_avc444_encoded_pair *result);

/**
 * Close the input (first call) and drain remaining completed pairs, one per
 * call. Returns XRDP_FFMPEG_PAIR_READY (result filled), XRDP_FFMPEG_PAIR_DONE
 * (no more pairs), or XRDP_FFMPEG_PAIR_ERROR. After flushing, the child is
 * spent; the handle must be recreated to encode again.
 */
int
xrdp_ffmpeg_avc444_flush_next(struct xrdp_ffmpeg_avc444 *self,
                              struct xrdp_avc444_encoded_pair *result);

int
xrdp_ffmpeg_avc444_coded_width(struct xrdp_ffmpeg_avc444 *self);

/**
 * Frames submitted but not yet returned = frames still held in the encoder's
 * pipeline (depth = async_depth-1 for VAAPI, the frame-thread window for x264;
 * zero with the shipped low-latency args). The opt-in tail-flush drains at most
 * this many duplicate frames to push a withheld tail out.
 */
int
xrdp_ffmpeg_avc444_inflight(struct xrdp_ffmpeg_avc444 *self);
int
xrdp_ffmpeg_avc444_coded_height(struct xrdp_ffmpeg_avc444 *self);

void
xrdp_ffmpeg_avc444_get_metrics(struct xrdp_ffmpeg_avc444 *self,
                               struct xrdp_ffmpeg_avc444_metrics *out);

/** Terminate/reap the child and free the handle. */
void
xrdp_ffmpeg_avc444_delete(struct xrdp_ffmpeg_avc444 *self);

#endif /* _XRDP_ENCODER_FFMPEG_H */
