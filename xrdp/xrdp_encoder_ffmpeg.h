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
 * Pipeline note: a stock ffmpeg reading a *persistent* pipe emits output
 * one picture behind its input and never flushes the final picture without
 * EOF (empirically verified; the PRD's synchronous model held only for
 * EOF-terminated input). The runner is therefore pipelined: encode_pair()
 * submits a pair and returns the oldest *completed* pair, so a submitted
 * pair becomes available one desktop update later. flush_next() closes the
 * input and drains the remaining pairs (used at reset/teardown).
 */

#ifndef _XRDP_ENCODER_FFMPEG_H
#define _XRDP_ENCODER_FFMPEG_H

#include <stddef.h>

struct xrdp_ffmpeg_avc444_config
{
    char path[256];                 /* absolute ffmpeg path                */
    int desktop_fps;                /* coded rate is 2x this               */
    int stream_ready_timeout_ms;
    int picture_timeout_ms;
    int pair_timeout_ms;
    int terminate_grace_ms;
    int quality_crf;
    int gop_pictures;
    size_t max_nut_header_bytes;
    size_t max_encoded_picture_bytes;
    size_t max_encoded_pair_bytes;
};

/** Populate cfg with the MVP defaults (path left empty). */
void
xrdp_ffmpeg_avc444_config_default(struct xrdp_ffmpeg_avc444_config *cfg);

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

/**
 * Bounded behavioral probe (PRD FR-PROBE): spawn the exact command at the
 * given coded dimensions, submit four distinguishable NV12 pictures in
 * main/aux order, verify one ordered Annex-B packet per picture with
 * SPS/PPS/IDR in the first, then terminate and reap. Returns 0 on success.
 */
int
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
int
xrdp_ffmpeg_avc444_coded_height(struct xrdp_ffmpeg_avc444 *self);

void
xrdp_ffmpeg_avc444_get_metrics(struct xrdp_ffmpeg_avc444 *self,
                               struct xrdp_ffmpeg_avc444_metrics *out);

/** Terminate/reap the child and free the handle. */
void
xrdp_ffmpeg_avc444_delete(struct xrdp_ffmpeg_avc444 *self);

#endif /* _XRDP_ENCODER_FFMPEG_H */
