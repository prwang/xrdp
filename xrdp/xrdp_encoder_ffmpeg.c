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
 * The runner is SYNCHRONOUS: encode_pair()/encode_single() wait (bounded by
 * pair/picture_timeout_ms) for the picture(s) just submitted and return
 * exactly that frame; flush_next() closes the input and drains the remainder.
 *
 * History: an earlier pipelined design returned the OLDEST completed pair
 * instead. Whenever the encoder's first frame was slow (VAAPI driver warmup)
 * or its pipeline deep (-async_depth > 1, x264 frame-threading), the runner
 * went permanently one-behind: frame N's H.264 content was emitted under
 * frame N+1's damage region. Clients that strictly honour the AVC metablock
 * region rects (mstsc) then blit stale pixels forever and reveal the newest
 * picture only inside later small damage rects (the "stuck last frame /
 * tooltip reveals it" field bug); FreeRDP hid it by presenting the whole
 * decoded surface. The synchronous wait makes content/region desync
 * impossible; an encoder that cannot return the submitted picture in time
 * (pipeline too deep) fails LOUDLY and is restarted -- configure a zero-
 * latency pipeline (h264_vaapi -async_depth 1, libx264 -tune zerolatency;
 * ~3-9ms/picture measured, well inside an interactive frame budget).
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "xrdp_encoder_ffmpeg.h"
#include "xrdp_nut.h"
#include "xrdp_h264_annexb.h"
#include "perf_trace.h"
#include "log.h"
#include "os_calls.h"
#include "string_calls.h"

#define FF_CHILD_RAW_FD 3
/* fixed input+output framing (~40 tokens) plus up to XRDP_AVC444_MAX_ENC_ARGS
 * verbatim encoder tokens, with headroom */
#define FF_MAX_ARGV 128
/* How large an input pipe the feeder ASKS for, and why that number.
 *
 * 1 MiB is the compiled-in default of fs/pipe-max-size, which fcntl(2)
 * defines as "the limit ... an unprivileged process can adjust the pipe
 * capacity to"; a process with CAP_SYS_RESOURCE in the initial user
 * namespace overrides it. So this is NOT an architectural ceiling: a
 * host may raise or lower the sysctl, and a bare-metal xrdp running as
 * real root could ask for more.
 *
 * It asks for 1 MiB anyway, because more buys nothing. Measured over a
 * 16x range of pipe sizes, the handover cost is FLAT from 64 KiB upward
 * (see FF_IN_PIPE_MIN_BYTES): what remains at that point is the reader's
 * copy, which no pipe size removes. Above 1 MiB is untested -- an
 * unprivileged process cannot get there -- but nothing in the shape of
 * the curve suggests a second knee.
 *
 * Asking for MORE than the sysctl allows is actively harmful, which is
 * why this is a wish and not a maximum: F_SETPIPE_SZ does not clamp, it
 * fails and leaves the pipe at its 64 KiB default, so a greedy request
 * gets LESS than a modest one (measured: asking 8 MiB once yields 65536
 * on a box whose limit is 1 MiB, tools/vmsplice_pipe_bench.c). See
 * negotiate_in_pipe_size(). */
#define FF_IN_PIPE_WANT_BYTES (1024 * 1024)
/* How large it must ACTUALLY be, which is the whole of the requirement
 * and is what PIPE_TOO_SMALL is judged against. Measured 2026-08-09
 * with tools/vmsplice_pipe_bench.c, one 13.82 MB picture, handover
 * timed until the reader acknowledges the last byte:
 *
 *      8 KiB  1688 round trips  5.632 ms
 *     16 KiB   844 round trips  2.824 ms
 *     32 KiB   422 round trips  1.952 ms
 *     64 KiB   211 round trips  0.729 ms   <-- flat from here
 *    128 KiB   106 round trips  0.776 ms
 *    512 KiB    27 round trips  0.710 ms
 *      1 MiB    14 round trips  0.601 ms
 *
 * Below 64 KiB the time is proportional to the round trips: the pipe
 * cannot hold enough for the two processes to run at once, so they take
 * turns and every turn costs a pair of context switches. At and above
 * 64 KiB they overlap and the cost is the reader's copy, which no pipe
 * size can remove -- 14 round trips and 211 round trips measure the
 * same. So the syscall count is NOT the thing to minimise, and the
 * requirement is only "enough to overlap".
 *
 * 64 KiB is also the kernel's own default pipe size, so any box that is
 * not in the pathological clamped state already satisfies it. */
#define FF_IN_PIPE_MIN_BYTES (64 * 1024)
#define FF_READ_CHUNK 65536
#define FF_MAX_INFLIGHT_PAIRS 8
/* borrowed input segments queued for the vmsplice feeder */
/* poll-set bound: 2 views x CLIENT_MONITOR_DATA_MAXIMUM_MONITORS
 * children can be armed in one set (#45 step 5) */
#define FF_PUMP_MAX_KIDS 32
#define FF_IN_IOV_MAX 32

struct ff_pkt
{
    unsigned char *data;
    int cap;
    int len;
    int keyframe;
};

struct xrdp_ffmpeg_avc444
{
    struct xrdp_ffmpeg_avc444_config cfg;
    int actual_width;
    int actual_height;
    int coded_width;
    int coded_height;
    int nv12_size;

    int in_fd;
    int out_fd;
    int err_fd;
    int pid;
    int flushing;

    unsigned long long generation;

    struct xrdp_nut_ctx *nut;

    /* pending input segments, fed exclusively via vmsplice
     * (PRD FR-PROC-6): BORROWED pointers (capture shmem / probe
     * fixtures) valid only within the encode call that queued them */
    struct iovec in_iov[FF_IN_IOV_MAX];
    int in_iov_count;
    int in_iov_head;
    size_t in_iov_off;

    /* BACKLOG #78: cleared at submit so drain_stdout can stamp the
     * FIRST output byte of the submitted picture exactly once */
    int trace_out_seen;

    /* completed-packet FIFO (in coded-picture order) */
    struct ff_pkt *pk;
    int pk_cap;
    int pk_head;
    int pk_count;

    /* submitted desktop sequence FIFO, one per pair submitted */
    unsigned long long *seq;
    int seq_cap;
    int seq_head;
    int seq_count;

    unsigned long long pairs_submitted;
    unsigned long long pairs_returned;

    /* result buffers (stable between calls) */
    unsigned char *main_buf;
    int main_cap;
    int main_len;
    int main_key;
    unsigned char *aux_buf;
    int aux_cap;
    int aux_len;

    /* DIAGNOSTIC (fault_aux_delay): previous pair's aux, swapped in */
    unsigned char *fault_aux_buf;
    /* SPS/PPS fields cached across calls for fault_strip_mmco */
    struct xrdp_h264_param_cache mmco_cache;
    int fault_aux_cap;
    int fault_aux_len;

    /* aux_intra_leaf: second child encoding the aux view all-IDR; its
     * packets are rewritten into non-reference I leaves so the main
     * chain never references aux frames (reference partitioning) */
    struct xrdp_ffmpeg_avc444 *leaf;
    struct xrdp_h264_param_cache leaf_main_cache;
    struct xrdp_h264_param_cache leaf_aux_cache;

    /* aux_ltr_chain (EXPERIMENTAL, FR-H264-8): shared-chain rewrite
     * state, including the scheduled-refresh period and per-view
     * picture ordinals */
    struct xrdp_h264_ltr_state ltr;
    int rekey_pending;

    /* BACKLOG #92 / FR-H264-9 (sparse aux): set by submit_pair to say
     * whether THIS cycle handed the aux child a picture. When it is 0
     * the aux child was not fed, is not armed in the poll set, is not
     * waited for, and is not popped at collect -- the pair comes back
     * with aux_len 0 and the caller emits the LC=1 luma PDU alone.
     * Always 1 when chroma_refresh_ms is 0, which is the default. */
    int aux_submitted;

    char errline[512];
    int errline_len;

    struct xrdp_ffmpeg_avc444_metrics metrics;
};

static int
spawn_second_child(struct xrdp_ffmpeg_avc444 *self);

/*****************************************************************************/
void
xrdp_ffmpeg_avc444_default_encoder_args(struct xrdp_avc444_encoder_args *args)
{
    /*
     * Built-in default encoder block (used when gfx.toml supplies no explicit
     * encoder_args). libx264 tuned for interactive RDP:
     *   -tune zerolatency   disables x264 lookahead + B-frame reorder +
     *                       threaded-frame delay, so the child streams one
     *                       encoded picture per input frame instead of
     *                       withholding several until EOF (see tests/xrdp/
     *                       avc444/FINDINGS_ffmpeg_latency.md).
     *   repeat-headers=1    emit SPS/PPS before every IDR so the client can
     *                       always decode.
     *   aud=1               emit an Access Unit Delimiter (NAL unit type 9)
     *                       at the start of every access unit, matching stock
     *                       Microsoft RDP (whose AVC444 stream carries an AUD
     *                       on every frame -- confirmed by wire capture of a
     *                       real Windows Server 2022 + NVIDIA host). AUDs are
     *                       inert to the decoders that already worked, so this
     *                       is a backward-compatible superset kept for wire
     *                       conformance. NOTE: this is NOT the fix for the
     *                       macOS Windows App black screen -- adding the AUD was
     *                       tested live and did not change the black (mstsc/UWP
     *                       rendered without it too). Root cause is the AVC444
     *                       LC framing (the earlier implementation emitted
     *                       same-region LC=0 every frame; real Windows
     *                       bootstraps luma-only LC=1 and defers chroma via
     *                       LC=2). See
     *                       PRD/slices/219-avc-wire-serialization.md.
     * This reproduces the historic hard-coded argv (plus the AUD delimiter);
     * tuning is the administrator's job via gfx.toml [avc444_ffmpeg]
     * encoder_args.
     */
    static const char *const def[] =
    {
        "-c:v", "libx264",
        "-bf", "0",
        "-preset", "ultrafast",
        "-tune", "zerolatency",
        "-crf", "18",
        "-g", "240",
        "-x264-params", "repeat-headers=1:aud=1"
    };
    int i;
    int count = (int)(sizeof(def) / sizeof(def[0]));

    memset(args, 0, sizeof(*args));
    for (i = 0; i < count && i < XRDP_AVC444_MAX_ENC_ARGS; i++)
    {
        g_strncpy(args->arg[i], def[i], XRDP_AVC444_ENC_ARG_LEN - 1);
    }
    args->count = i;
}

/*****************************************************************************/
void
xrdp_ffmpeg_avc444_config_default(struct xrdp_ffmpeg_avc444_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    xrdp_ffmpeg_avc444_default_encoder_args(&cfg->encoder_args);
    cfg->chroma_align = 32;   /* default: match mstsc's 32-aligned U|V split */
    cfg->strip_sei = 0;
    cfg->sanitize_hrd = 0;
    cfg->strip_pic_struct = 0;
    cfg->aux_intra_leaf = 0;
    cfg->aux_ltr_chain = 0;
    cfg->ltr_rekey_frame_num = XRDP_H264_LTR_FRAME_NUM_REKEY;
    cfg->intra_refresh_frames = XRDP_H264_INTRA_REFRESH_FRAMES;
    cfg->intra_refresh_frames_aux = XRDP_H264_INTRA_REFRESH_FRAMES_AUX;
    cfg->fault_strip_mmco = 0;
    cfg->fault_aux_delay = 0;
    cfg->use_dump_extra = 0;  /* static administrator policy (gfx.toml
                               * [avc444_ffmpeg] dump_extra); verified --
                               * never changed -- by the probe
                               * (PRD FR-PROBE-6) */
    /* BACKLOG #91: "not set". Only the owner of the per-monitor handle
     * array knows the real index; a probe or a unit test has none. */
    cfg->monitor_index = -1;
    cfg->desktop_fps = 60;
    cfg->stream_ready_timeout_ms = 2000;
    cfg->picture_timeout_ms = 2000;
    cfg->pair_timeout_ms = 2000;
    cfg->terminate_grace_ms = 250;
    cfg->max_nut_header_bytes = 1024 * 1024;
    cfg->max_encoded_picture_bytes = (size_t)128 * 1024 * 1024;
    cfg->max_encoded_pair_bytes = (size_t)256 * 1024 * 1024;
}

/*****************************************************************************/
static int
round_up_16(int v)
{
    return (v + 15) & ~15;
}

/*****************************************************************************/
static long long
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*****************************************************************************/
static int
grow(unsigned char **buf, int *cap, int need)
{
    if (need > *cap)
    {
        int nc = *cap ? *cap : 65536;
        unsigned char *nb;
        while (nc < need)
        {
            nc *= 2;
        }
        nb = (unsigned char *)realloc(*buf, nc);
        if (nb == NULL)
        {
            return 1;
        }
        *buf = nb;
        *cap = nc;
    }
    return 0;
}

/*****************************************************************************/
/* log a bounded, sanitized line of untrusted ffmpeg stderr (NFR-SEC-8)     */
static void
log_stderr_line(char *line, int len)
{
    int i;

    if (len <= 0)
    {
        return;
    }
    for (i = 0; i < len; i++)
    {
        unsigned char ch = (unsigned char)line[i];
        if (ch < 0x20 || ch == 0x7f)
        {
            line[i] = ' ';
        }
    }
    line[len] = '\0';
    LOG(LOG_LEVEL_WARNING, "ffmpeg: %s", line);
}

/*****************************************************************************/
/* Build the exact structural argv (PRD FR-PROC-5) plus the low-latency    */
/* pipe flags required to stream from a persistent input pipe.             */
static int
build_argv(const struct xrdp_ffmpeg_avc444_config *cfg,
           int cw, int ch, char *argv[FF_MAX_ARGV],
           char *num_store, int num_store_size)
{
    int n = 0;
    char *p = num_store;
    char *end = num_store + num_store_size;
    int coded_rate = cfg->desktop_fps > 0 ? cfg->desktop_fps * 2 : 120;

#define ADD(s) do { if (n >= FF_MAX_ARGV - 1) { return -1; } \
        argv[n++] = (char *)(s); } while (0)
#define ADDNUM(fmt, val) do { \
        int _r = snprintf(p, end - p, fmt, val); \
        if (_r < 0 || _r >= end - p) { return -1; } \
        ADD(p); p += _r + 1; } while (0)

    ADD(cfg->path);
    ADD("-hide_banner");
    ADD("-nostdin");
    ADD("-loglevel");
    ADD("error");
    ADD("-f");
    ADD("rawvideo");
    ADD("-pixel_format");
    ADD("nv12");
    ADD("-video_size");
    {
        int _r = snprintf(p, end - p, "%dx%d", cw, ch);
        if (_r < 0 || _r >= end - p)
        {
            return -1;
        }
        ADD(p);
        p += _r + 1;
    }
    ADD("-framerate");
    ADDNUM("%d", coded_rate);
    ADD("-color_range");
    ADD("pc");
    ADD("-colorspace");
    ADD("bt709");
    ADD("-color_primaries");
    ADD("bt709");
    ADD("-color_trc");
    ADD("bt709");
    /* End stream analysis after exactly one frame. With a declared input
     * rate above ~100 fps, avformat_find_stream_info() distrusts the
     * timebase and buffers frames for rate estimation up to the default
     * 5 MB probesize -- 4+ pictures at 1024x768 -- before emitting
     * anything. The synchronous encode then times out on the first pair
     * at any resolution where a pair is smaller than that window. The
     * rate needs no estimation (-framerate is explicit), so cap the
     * analysis window at one frame. */
    ADD("-probesize");
    ADDNUM("%d", cw * ch + cw * (ch / 2));
    ADD("-i");
    ADD("pipe:3");
    ADD("-map");
    ADD("0:v:0");
    ADD("-an");
    ADD("-sn");
    ADD("-dn");
    ADD("-fps_mode");
    ADD("passthrough");
    /* encoder block: verbatim admin passthrough (-c:v + tuning). xrdp does
     * not interpret these; each token is one execve argv element. */
    {
        int i;
        for (i = 0; i < cfg->encoder_args.count &&
                i < XRDP_AVC444_MAX_ENC_ARGS; i++)
        {
            ADD(cfg->encoder_args.arg[i]);
        }
    }
    /* Scheduled paired intra refresh (PRD FR-H264-6, #45 steps 2 and 4):
     * an identical FRAME-INDEXED schedule on both children, so a cut
     * lands on the same picture ordinal in each view and the rewriter
     * can check observed against requested. -g is pinned to the same
     * value (D7) so every GOP boundary coincides with a scheduled index
     * and an "unscheduled IDR" is unreachable by construction; it comes
     * AFTER the admin encoder block deliberately, because ffmpeg takes
     * the last occurrence and D7 is not negotiable while the chain is
     * on. -forced-idr is NOT set: nvenc's non-IDR I at a forced key
     * frame is exactly the shape this refresh wants. */
    if (cfg->intra_refresh_schedule > 0)
    {
        ADD("-force_key_frames");
        /* NO backslash before the comma: the argv reaches execve
         * directly, and ffmpeg hands everything after "expr:" to
         * av_expr_parse, which rejects "mod(n\,240)" outright
         * (verified 2026-07-29: the escaped form fails with "Missing
         * ')' or too many args", the plain form keys correctly at
         * n = 0, N, 2N). The escape is a SHELL convention. */
        ADDNUM("expr:not(mod(n,%d))", cfg->intra_refresh_schedule);
        ADD("-g");
        ADDNUM("%d", cfg->intra_refresh_schedule);
    }
    /* NUT is a global-header muxer: encoders with no in-band repeat knob
     * (h264_nvenc and others) put SPS/PPS in extradata only, which fails
     * the probe's reset-keyframe check and would ship an undecodable
     * stream. dump_extra reinserts the extradata parameter sets ahead of
     * each keyframe -- but ONLY when the probe proved them missing:
     * chaining it unconditionally DUPLICATED the parameter sets on
     * encoders that already repeat in-band (libx264 repeat-headers,
     * h264_vaapi packed headers), and strict decoders refuse to present
     * such keyframes (macOS Windows App rendered black; bisected live
     * 2026-07-23). Exactly one SPS/PPS copy per keyframe either way. */
    ADD("-bsf:v");
    /* strip_sei removes ALL SEI NALs (NAL unit type 6):
     * the macOS Windows App's RDP H264 path blacks on the HRD SEI
     * class (bisected 2026-07-26 on the dev box; QuickTime plays the
     * same bytes, so this is App-path-specific). */
    if (cfg->strip_sei)
    {
        ADD(cfg->use_dump_extra
            ? "dump_extra,filter_units=remove_types=6,h264_mp4toannexb"
            : "filter_units=remove_types=6,h264_mp4toannexb");
    }
    else
    {
        ADD(cfg->use_dump_extra ? "dump_extra,h264_mp4toannexb"
            : "h264_mp4toannexb");
    }
    ADD("-flush_packets");
    ADD("1");
    ADD("-write_index");
    ADD("0");
    ADD("-f");
    ADD("nut");
    ADD("pipe:1");
    argv[n] = NULL;
    return n;
#undef ADD
#undef ADDNUM
}

/*****************************************************************************/
static void
close_range_from(int low)
{
    int fd;
    int maxfd = (int)sysconf(_SC_OPEN_MAX);

    if (maxfd < 0 || maxfd > 4096)
    {
        maxfd = 4096;
    }
    for (fd = low; fd < maxfd; fd++)
    {
        if (fd != FF_CHILD_RAW_FD)
        {
            close(fd);
        }
    }
}

/*****************************************************************************/
/* Negotiate the input pipe DOWN from what we want to what this kernel
 * will give, and return the size actually in force (or -1).
 *
 * A single F_SETPIPE_SZ is not enough, because the call is all-or-
 * nothing: it does not clamp to the ceiling, it fails and leaves the
 * pipe at its default. On a box whose administrator lowered
 * fs/pipe-max-size to 256 KiB, asking once for 1 MiB therefore yields
 * 64 KiB when 256 KiB was there for the asking. Halving until one
 * request is granted takes at most five fcntl calls, once per child at
 * spawn, and cannot leave us below what a single ask would have got.
 *
 * Stops at FF_IN_PIPE_MIN_BYTES: shrinking below the requirement to
 * make a call succeed would be answering the wrong question. */
static int
negotiate_in_pipe_size(int fd)
{
    int want;

    for (want = FF_IN_PIPE_WANT_BYTES; want >= FF_IN_PIPE_MIN_BYTES;
            want /= 2)
    {
        if (fcntl(fd, F_SETPIPE_SZ, want) >= 0)
        {
            break;
        }
    }
    return fcntl(fd, F_GETPIPE_SZ);
}

/*****************************************************************************/
/* spawn the child; on success sets the in/out/err fds and pid (parent fds  */
/* are nonblocking + close-on-exec). Returns 0 on success.                 */
static int
spawn_child(const struct xrdp_ffmpeg_avc444_config *cfg, int cw, int ch,
            int *in_fd, int *out_fd, int *err_fd, int *out_pid)
{
    int inpipe[2];
    int outpipe[2];
    int errpipe[2];
    int pid;
    int got_pipe;
    int picture_bytes;
    char *argv[FF_MAX_ARGV];
    char num_store[192];

    if (build_argv(cfg, cw, ch, argv, num_store, sizeof(num_store)) < 0)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: argv build failed");
        return 1;
    }
    if (pipe(inpipe) != 0)
    {
        return 1;
    }
    if (pipe(outpipe) != 0)
    {
        close(inpipe[0]);
        close(inpipe[1]);
        return 1;
    }
    if (pipe(errpipe) != 0)
    {
        close(inpipe[0]);
        close(inpipe[1]);
        close(outpipe[0]);
        close(outpipe[1]);
        return 1;
    }
    pid = fork();
    if (pid < 0)
    {
        close(inpipe[0]);
        close(inpipe[1]);
        close(outpipe[0]);
        close(outpipe[1]);
        close(errpipe[0]);
        close(errpipe[1]);
        return 1;
    }
    if (pid == 0)
    {
        char *envp[2];
        int devnull;

        envp[0] = (char *)"PATH=/usr/bin:/bin";
        envp[1] = NULL;
        devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0)
        {
            dup2(devnull, 0);
            if (devnull != 0)
            {
                close(devnull);
            }
        }
        dup2(outpipe[1], 1);
        dup2(errpipe[1], 2);
        if (inpipe[0] != FF_CHILD_RAW_FD)
        {
            dup2(inpipe[0], FF_CHILD_RAW_FD);
        }
        close_range_from(4);
        execve(cfg->path, argv, envp);
        _exit(127);
    }
    close(inpipe[0]);
    close(outpipe[1]);
    close(errpipe[1]);
    fcntl(inpipe[1], F_SETFL, O_NONBLOCK);
    fcntl(inpipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(outpipe[0], F_SETFL, O_NONBLOCK);
    fcntl(outpipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(errpipe[0], F_SETFL, O_NONBLOCK);
    fcntl(errpipe[0], F_SETFD, FD_CLOEXEC);
    /* Size the input pipe, and CHECK WHAT THE KERNEL ACTUALLY GAVE US
     * against the requirement rather than against the wish (FR-PROC-6
     * clause 4). The resize is refused, silently, for a caller over
     * fs/pipe-user-pages-soft that is not CAP_SYS_RESOURCE-capable in
     * the INITIAL user namespace; the pipe then stays at the kernel
     * minimum of two pages. Measured 2026-08-08 (BACKLOG #103) inside
     * an unprivileged container whose root maps to an ordinary host
     * uid: an 8192-byte pipe carried a 13.8 MB picture in 1688 round
     * trips instead of 14 and cost 7.5 ms of a 24.5 ms frame at
     * 3840x2400. xrdp does not change system settings -- not a sysctl,
     * not a capability -- so the only correct response is to be loud
     * about it and let the administrator decide. */
    got_pipe = negotiate_in_pipe_size(inpipe[1]);
    picture_bytes = cw * ch + cw * (ch / 2);
    if (got_pipe < 0)
    {
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: PIPE_SIZE_UNKNOWN monitor "
            "%d: sized the encoder input pipe and could not read back "
            "what is in force (%s)", cfg->monitor_index,
            g_get_strerror());
    }
    else if (got_pipe < FF_IN_PIPE_MIN_BYTES)
    {
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: PIPE_TOO_SMALL monitor %d: "
            "the encoder input pipe is %d bytes; %d is the minimum this "
            "server needs and %d is what it asked for. One %dx%d NV12 "
            "picture is %d bytes, so each one now crosses the pipe in "
            "%d turns instead of %d. Below %d bytes the pipe cannot hold "
            "enough for xrdp and the encoder to run at the same time, so "
            "they take turns and each turn costs a pair of context "
            "switches.", cfg->monitor_index, got_pipe,
            (int)FF_IN_PIPE_MIN_BYTES, (int)FF_IN_PIPE_WANT_BYTES,
            cw, ch, picture_bytes,
            (picture_bytes + got_pipe - 1) / (got_pipe > 0 ? got_pipe : 1),
            (picture_bytes + (int)FF_IN_PIPE_MIN_BYTES - 1)
            / (int)FF_IN_PIPE_MIN_BYTES, (int)FF_IN_PIPE_MIN_BYTES);
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: PIPE_TOO_SMALL monitor %d: "
            "this is a host limit on this uid, not an xrdp setting. The "
            "administrator can raise fs/pipe-user-pages-soft, or give "
            "the server CAP_SYS_RESOURCE in the initial user namespace. "
            "xrdp will not change a system setting on its own. Any "
            "performance measurement taken in this state is not valid.",
            cfg->monitor_index);
    }
    else if (got_pipe < FF_IN_PIPE_WANT_BYTES)
    {
        /* Above the requirement, below the wish -- normal on a box whose
         * fs/pipe-max-size has been lowered. Worth one line so a capture
         * records which it was, but NOT a warning: measured flat from
         * 64 KiB upward (see FF_IN_PIPE_MIN_BYTES). */
        LOG(LOG_LEVEL_INFO, "xrdp_ffmpeg: encoder input pipe is %d bytes "
            "for monitor %d; asked for %d, need at least %d. This is "
            "fine -- the handover cost is flat above the minimum -- and "
            "means fs/pipe-max-size on this host is below %d.",
            got_pipe, cfg->monitor_index, (int)FF_IN_PIPE_WANT_BYTES,
            (int)FF_IN_PIPE_MIN_BYTES, (int)FF_IN_PIPE_WANT_BYTES);
    }
    *in_fd = inpipe[1];
    *out_fd = outpipe[0];
    *err_fd = errpipe[0];
    *out_pid = pid;
    return 0;
}

/*****************************************************************************/
/* returns the child's waitpid status, or -1 when it could not be collected */
static int
reap_child(int pid, int grace_ms)
{
    long long deadline;
    int status = -1;

    if (pid <= 0)
    {
        return -1;
    }
    kill(pid, SIGTERM);
    deadline = now_ms() + (grace_ms > 0 ? grace_ms : 250);
    for (;;)
    {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
        {
            return status;
        }
        if (r < 0 && errno == ECHILD)
        {
            return -1;
        }
        if (now_ms() >= deadline)
        {
            break;
        }
        usleep(5000);
    }
    kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) == pid)
    {
        return status;
    }
    return -1;
}

/*****************************************************************************/
static void
drain_stderr(struct xrdp_ffmpeg_avc444 *self)
{
    char tmp[FF_READ_CHUNK];
    int n;
    int i;

    if (self->err_fd < 0)
    {
        return;
    }
    while ((n = (int)read(self->err_fd, tmp, sizeof(tmp))) > 0)
    {
        for (i = 0; i < n; i++)
        {
            if (tmp[i] == '\n' ||
                    self->errline_len >= (int)sizeof(self->errline) - 1)
            {
                if (self->errline_len > 0)
                {
                    log_stderr_line(self->errline, self->errline_len);
                    self->metrics.stderr_warnings++;
                }
                self->errline_len = 0;
                if (tmp[i] == '\n')
                {
                    continue;
                }
            }
            self->errline[self->errline_len++] = tmp[i];
        }
    }
}

/*****************************************************************************/
static int
pk_push(struct xrdp_ffmpeg_avc444 *self, const unsigned char *data, int len,
        int keyframe)
{
    struct ff_pkt *slot;

    /* compact if the head has advanced far */
    if (self->pk_head > 0 && self->pk_head == self->pk_count)
    {
        self->pk_head = 0;
        self->pk_count = 0;
    }
    if (self->pk_count >= self->pk_cap)
    {
        int nc = self->pk_cap ? self->pk_cap * 2 : 8;
        struct ff_pkt *np = (struct ff_pkt *)realloc(self->pk,
                            nc * sizeof(struct ff_pkt));
        if (np == NULL)
        {
            return 1;
        }
        memset(np + self->pk_cap, 0,
               (nc - self->pk_cap) * sizeof(struct ff_pkt));
        self->pk = np;
        self->pk_cap = nc;
    }
    slot = &self->pk[self->pk_count];
    if (grow(&slot->data, &slot->cap, len) != 0)
    {
        return 1;
    }
    memcpy(slot->data, data, len);
    slot->len = len;
    slot->keyframe = keyframe;
    self->pk_count++;
    return 0;
}

/*****************************************************************************/
static int
pk_available(struct xrdp_ffmpeg_avc444 *self)
{
    return self->pk_count - self->pk_head;
}

/*****************************************************************************/
/* BACKLOG #78: echoed identity for the feedend/outfirst records -- the
 * desktop sequence of the picture this child is currently working on
 * (the seq FIFO front), truncated to the record's int payload. -1 when
 * no picture is in flight (records so tagged are discarded by the
 * reader rather than mis-paired -- 2c gate). */
static int
trace_seq_front(const struct xrdp_ffmpeg_avc444 *self)
{
    if (self->seq_count - self->seq_head < 1)
    {
        return -1;
    }
    return (int)(self->seq[self->seq_head] & 0x3fffffff);
}

/*****************************************************************************/
/* read stdout, feed NUT, append completed packets to the FIFO.            */
/* returns bytes read (>=0), -1 error, -2 child EOF                        */
static int
drain_stdout(struct xrdp_ffmpeg_avc444 *self)
{
    char tmp[FF_READ_CHUNK];
    int n;
    int total = 0;

    if (self->out_fd < 0)
    {
        return -2;
    }
    for (;;)
    {
        n = (int)read(self->out_fd, tmp, sizeof(tmp));
        if (n == 0)
        {
            return -2;
        }
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return total;
            }
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        total += n;
        if (!self->trace_out_seen)
        {
            /* BACKLOG #78: first output byte since submit == the child
             * has finished encoding and started writing the picture
             * (input is consumed strictly before output exists) */
            self->trace_out_seen = 1;
            /* BACKLOG #91: d = the monitor this child belongs to, so a
             * four-child pump can be split by screen. -1 when the
             * creator did not set one (probes, unit tests). */
            PERF_TRACE("event=outfirst sequence=%d main=%d bytes=%d "
                       "monitor=%d", trace_seq_front(self),
                       self->leaf == NULL, n, self->cfg.monitor_index);
        }
        if (xrdp_nut_feed(self->nut, (unsigned char *)tmp, n) != 0)
        {
            self->metrics.parser_errors++;
            return -1;
        }
        for (;;)
        {
            struct xrdp_nut_packet pkt;
            enum xrdp_nut_event_type ev = xrdp_nut_next(self->nut, &pkt);
            if (ev == XRDP_NUT_NEED_MORE)
            {
                break;
            }
            if (ev == XRDP_NUT_STREAM_READY)
            {
                continue;
            }
            if (ev == XRDP_NUT_ERROR)
            {
                self->metrics.parser_errors++;
                return -1;
            }
            if (pkt.stream_id != 0)
            {
                return -1;
            }
            if (pk_push(self, pkt.data, pkt.len, pkt.keyframe) != 0)
            {
                return -1;
            }
        }
    }
}

/*****************************************************************************/
/* Pump the child: write pending input and drain output/stderr. When        */
/* drain_to_eof is 0, returns as soon as the input is written and no more    */
/* output is immediately available (used while streaming). When 1, keeps     */
/* polling until child EOF or the deadline (used to flush after closing      */
/* input). Returns 0 ok, 1 error, 2 child EOF.                             */
static int
in_iov_push(struct xrdp_ffmpeg_avc444 *self, const void *data, size_t len)
{
    if (self->in_iov_count >= FF_IN_IOV_MAX)
    {
        return 1;
    }
    self->in_iov[self->in_iov_count].iov_base = (void *)(uintptr_t)data;
    self->in_iov[self->in_iov_count].iov_len = len;
    self->in_iov_count++;
    return 0;
}

/*****************************************************************************/
static int
in_iov_pending(const struct xrdp_ffmpeg_avc444 *self)
{
    return self->in_iov_head < self->in_iov_count;
}

/*****************************************************************************/
/* move the next chunk of queued input into the child's pipe by page
 * reference -- vmsplice is the ONLY input mechanism (PRD FR-PROC-6).
 * SPLICE_F_GIFT is never used: the pages belong to the capture shmem.
 * returns 0 on progress/would-block, 1 on fatal error */
static int
feed_vmsplice(struct xrdp_ffmpeg_avc444 *self)
{
    struct iovec iov;
    ssize_t n;

    if (self->in_fd < 0 || !in_iov_pending(self))
    {
        return 0;
    }
    iov.iov_base = (char *)self->in_iov[self->in_iov_head].iov_base
                   + self->in_iov_off;
    iov.iov_len = self->in_iov[self->in_iov_head].iov_len
                  - self->in_iov_off;
    n = vmsplice(self->in_fd, &iov, 1, SPLICE_F_NONBLOCK);
    if (n > 0)
    {
        self->metrics.input_bytes += n;
        self->in_iov_off += n;
        if (self->in_iov_off >= self->in_iov[self->in_iov_head].iov_len)
        {
            self->in_iov_head++;
            self->in_iov_off = 0;
            if (self->in_iov_head >= self->in_iov_count)
            {
                self->in_iov_head = 0;
                self->in_iov_count = 0;
                /* BACKLOG #78: the picture is now fully in the pipe.
                 * The child may still hold up to one pipe window
                 * unread; FEED here means "input no longer paces the
                 * worker", not "child copied the last byte". */
                /* BACKLOG #91: d = the monitor, in the SAME field as
                 * outfirst's, so one reader rule covers both */
                PERF_TRACE("event=feedend sequence=%d main=%d monitor=%d",
                           trace_seq_front(self), self->leaf == NULL,
                           self->cfg.monitor_index);
            }
        }
        return 0;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
    {
        return 1;
    }
    return 0;
}

/*****************************************************************************/
/* One child's slots in a poll set (#45 step 5). pump() is the n = 1 case
 * of pump_set(): arm every child, poll ONCE, service every child. One
 * thread, N children, ONE shared deadline (D3) -- a per-child deadline
 * would cost n x pair_timeout_ms when a single child stalls. */
struct ff_pump_slot
{
    int in_slot;        /* index into the shared pollfd array, or -1 */
    int out_slot;
    int err_slot;
    int want_write;
};

/*****************************************************************************/
/* fill pfd[base..] for this child; returns the number of slots used */
static int
pump_arm(struct xrdp_ffmpeg_avc444 *self, struct pollfd *pfd, int base,
         struct ff_pump_slot *sl)
{
    int n = base;

    sl->in_slot = -1;
    sl->out_slot = -1;
    sl->err_slot = -1;
    sl->want_write = (self->in_fd >= 0 && in_iov_pending(self));
    if (sl->want_write)
    {
        pfd[n].fd = self->in_fd;
        pfd[n].events = POLLOUT;
        sl->in_slot = n;
        n++;
    }
    pfd[n].fd = self->out_fd;
    pfd[n].events = POLLIN;
    sl->out_slot = n;
    n++;
    if (self->err_fd >= 0)
    {
        pfd[n].fd = self->err_fd;
        pfd[n].events = POLLIN;
        sl->err_slot = n;
        n++;
    }
    return n - base;
}

/*****************************************************************************/
/* dispatch one child's revents: feed its input, drain its stderr and
 * stdout. Returns 0 ok, 1 error, 2 child EOF. *quiet is set when this
 * child has nothing left to write and produced no output this round --
 * exactly the condition pump() used to return 0 on. */
static int
pump_service(struct xrdp_ffmpeg_avc444 *self, struct pollfd *pfd,
             const struct ff_pump_slot *sl, int *quiet)
{
    int dr;

    *quiet = 0;
    if (sl->in_slot >= 0 &&
            (pfd[sl->in_slot].revents & (POLLOUT | POLLERR | POLLHUP)))
    {
        if (pfd[sl->in_slot].revents & (POLLERR | POLLHUP))
        {
            return 1;
        }
        if (feed_vmsplice(self) != 0)
        {
            return 1;
        }
    }
    drain_stderr(self);
    dr = drain_stdout(self);
    if (dr == -1)
    {
        return 1;
    }
    if (dr == -2)
    {
        return 2;
    }
    if (!sl->want_write && dr == 0)
    {
        *quiet = 1;
    }
    return 0;
}

/*****************************************************************************/
/* arm all n children, poll ONCE, service all n. Returns 0 ok, 1 error,
 * 2 child EOF; on a non-zero return *bad_kid is the index of the child
 * that failed, because the caller has to tear down THAT child's handle
 * and encoding another monitor with a dead child is silent corruption.
 * *all_quiet is set when every child was quiet. The CALLER owns the
 * completion predicate: n = 1 for one child, 2 for a pair, 4 for two
 * monitors (E4). Never pump2(): a pair-shaped helper hard-wires "two
 * issue, two retire" and breaks under FR-PROC-7's variable set shape. */
static int
pump_set(struct xrdp_ffmpeg_avc444 **kids, int n, long long deadline,
         int *all_quiet, int *bad_kid)
{
    struct pollfd pfd[3 * FF_PUMP_MAX_KIDS];
    struct ff_pump_slot sl[FF_PUMP_MAX_KIDS];
    int nfds;
    int want_write_any;
    int quiet_all;
    int timeout;
    int rv;
    int st;
    int quiet;
    int i;

    *all_quiet = 0;
    *bad_kid = -1;
    if (n < 1 || n > FF_PUMP_MAX_KIDS)
    {
        return 1;
    }
    nfds = 0;
    want_write_any = 0;
    for (i = 0; i < n; i++)
    {
        if (kids[i] == NULL)
        {
            *bad_kid = i;
            return 1;
        }
        nfds += pump_arm(kids[i], pfd, nfds, &sl[i]);
        want_write_any = want_write_any || sl[i].want_write;
    }
    timeout = want_write_any ? (int)(deadline - now_ms()) : 10;
    if (timeout < 0)
    {
        timeout = 0;
    }
    rv = poll(pfd, nfds, timeout);
    if (rv < 0)
    {
        if (errno == EINTR)
        {
            return 0;         /* the caller re-checks its deadline */
        }
        return 1;
    }
    if (rv == 0)
    {
        /* nothing ready: quiet only if no child is waiting to write */
        *all_quiet = !want_write_any;
        return 0;
    }
    quiet_all = 1;
    for (i = 0; i < n; i++)
    {
        st = pump_service(kids[i], pfd, &sl[i], &quiet);
        if (st != 0)
        {
            *bad_kid = i;
            return st;
        }
        quiet_all = quiet_all && quiet;
    }
    *all_quiet = quiet_all;
    return 0;
}

/*****************************************************************************/
/* Pump the child: write pending input and drain output/stderr. When
 * drain_to_eof is 0, returns as soon as the input is written and no more
 * output is immediately available (used while streaming). When 1, keeps
 * polling until child EOF or the deadline (used to flush after closing
 * input). Returns 0 ok, 1 error, 2 child EOF.
 * This is pump_set() with n = 1; the semantics are unchanged from the
 * single-child loop it replaces, path for path. */
static int
pump(struct xrdp_ffmpeg_avc444 *self, long long deadline, int drain_to_eof)
{
    struct xrdp_ffmpeg_avc444 *kids[1];
    int bad_kid;
    int quiet;
    int st;

    kids[0] = self;
    for (;;)
    {
        if (now_ms() >= deadline)
        {
            return 0;
        }
        st = pump_set(kids, 1, deadline, &quiet, &bad_kid);
        if (st != 0)
        {
            return st;
        }
        if (quiet && !drain_to_eof)
        {
            return 0;         /* input flushed and output drained */
        }
    }
}

/*****************************************************************************/
/* pop the oldest completed pair into the result buffers and validate it.   */
/* returns 0 ok, 1 validation failure                                      */
static int
pop_pair(struct xrdp_ffmpeg_avc444 *self,
         struct xrdp_avc444_encoded_pair *result)
{
    struct ff_pkt *m = &self->pk[self->pk_head];
    struct ff_pkt *a = &self->pk[self->pk_head + 1];
    unsigned long long seq;

    if (grow(&self->main_buf, &self->main_cap, m->len) != 0 ||
            grow(&self->aux_buf, &self->aux_cap, a->len) != 0)
    {
        return 1;
    }
    memcpy(self->main_buf, m->data, m->len);
    self->main_len = m->len;
    self->main_key = m->keyframe;
    memcpy(self->aux_buf, a->data, a->len);
    self->aux_len = a->len;
    self->pk_head += 2;

    if (self->cfg.fault_aux_delay)
    {
        /* DIAGNOSTIC FAULT INJECTION (bisect arm-K, 2026-07-27): ship
         * the PREVIOUS pair's aux with this pair's main — a deliberate
         * one-frame chroma pairing slip that models a decoder-side
         * main/aux association error (hypothesis A). Never enable
         * outside a bisect arm. */
        unsigned char *tb = self->fault_aux_buf;
        int tl = self->fault_aux_len;
        int tc = self->fault_aux_cap;

        self->fault_aux_buf = self->aux_buf;
        self->fault_aux_len = self->aux_len;
        self->fault_aux_cap = self->aux_cap;
        self->aux_buf = tb;
        self->aux_len = tl;
        self->aux_cap = tc;
        if (self->aux_buf == NULL || self->aux_len == 0)
        {
            /* very first pair has no predecessor: emit its own aux */
            tb = self->fault_aux_buf;
            tl = self->fault_aux_len;
            tc = self->fault_aux_cap;
            self->fault_aux_buf = self->aux_buf;
            self->fault_aux_len = self->aux_len;
            self->fault_aux_cap = self->aux_cap;
            self->aux_buf = tb;
            self->aux_len = tl;
            self->aux_cap = tc;
        }
    }

    if (self->cfg.sanitize_hrd)
    {
        if (xrdp_h264_sanitize_hrd(self->main_buf, &self->main_len) != 0 ||
                xrdp_h264_sanitize_hrd(self->aux_buf, &self->aux_len) != 0)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: sanitize_hrd could not "
                "rewrite an SPS; refusing to ship the packet");
            return 1;
        }
    }

    if (self->cfg.strip_pic_struct)
    {
        if (xrdp_h264_strip_pic_struct(self->main_buf,
                                       &self->main_len) != 0 ||
                xrdp_h264_strip_pic_struct(self->aux_buf,
                                           &self->aux_len) != 0)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: strip_pic_struct could not "
                "rewrite an SPS; refusing to ship the packet");
            return 1;
        }
    }

    if (self->cfg.fault_strip_mmco)
    {
        if (xrdp_h264_strip_mmco(self->main_buf, &self->main_len,
                                 &self->mmco_cache) != 0 ||
                xrdp_h264_strip_mmco(self->aux_buf, &self->aux_len,
                                     &self->mmco_cache) != 0)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: fault_strip_mmco could not "
                "rewrite a slice; refusing to ship the packet");
            return 1;
        }
    }

    if (self->pairs_returned == 0)
    {
        struct xrdp_h264_nal_summary sum;

        if (xrdp_h264_scan_annexb(self->main_buf, self->main_len,
                                  &sum) != 0 ||
                !sum.has_sps || !sum.has_pps || !sum.has_idr)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: reset main packet lacks "
                "SPS/PPS/IDR");
            return 1;
        }
        if (sum.sps_count != 1)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: reset main packet carries "
                "%d SPS (must be exactly 1; strict decoders black out on "
                "duplicated parameter sets)", sum.sps_count);
            return 1;
        }
    }
    else if (!xrdp_h264_aux_ok(self->main_buf, self->main_len))
    {
        return 1;
    }
    if (!xrdp_h264_aux_ok(self->aux_buf, self->aux_len))
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: aux packet lacks a VCL NAL");
        return 1;
    }
    seq = 0;
    if (self->seq_count > self->seq_head)
    {
        seq = self->seq[self->seq_head++];
    }
    result->generation = self->generation;
    result->desktop_sequence = seq;
    result->main_data = self->main_buf;
    result->main_len = self->main_len;
    result->main_keyframe = self->main_key;
    result->aux_data = self->aux_buf;
    result->aux_len = self->aux_len;
    self->pairs_returned++;
    self->metrics.pairs_completed++;
    return 0;
}

/*****************************************************************************/
/* pop the oldest completed single picture (AVC420) into the main result     */
/* buffer and validate it. returns 0 ok, 1 validation failure.              */
static int
pop_single(struct xrdp_ffmpeg_avc444 *self,
           struct xrdp_avc444_encoded_pair *result)
{
    struct ff_pkt *m = &self->pk[self->pk_head];
    unsigned long long seq;

    if (grow(&self->main_buf, &self->main_cap, m->len) != 0)
    {
        return 1;
    }
    memcpy(self->main_buf, m->data, m->len);
    self->main_len = m->len;
    self->main_key = m->keyframe;
    self->pk_head += 1;

    if (self->cfg.sanitize_hrd &&
            xrdp_h264_sanitize_hrd(self->main_buf, &self->main_len) != 0)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: sanitize_hrd could not "
            "rewrite an SPS; refusing to ship the packet");
        return 1;
    }

    if (self->cfg.strip_pic_struct &&
            xrdp_h264_strip_pic_struct(self->main_buf,
                                       &self->main_len) != 0)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: strip_pic_struct could not "
            "rewrite an SPS; refusing to ship the packet");
        return 1;
    }

    if (self->cfg.fault_strip_mmco &&
            xrdp_h264_strip_mmco(self->main_buf, &self->main_len,
                                 &self->mmco_cache) != 0)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: fault_strip_mmco could not "
            "rewrite a slice; refusing to ship the packet");
        return 1;
    }

    if (self->pairs_returned == 0)
    {
        struct xrdp_h264_nal_summary sum;

        if (xrdp_h264_scan_annexb(self->main_buf, self->main_len,
                                  &sum) != 0 ||
                !sum.has_sps || !sum.has_pps || !sum.has_idr)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: reset packet lacks "
                "SPS/PPS/IDR");
            return 1;
        }
        if (sum.sps_count != 1)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: reset packet carries %d SPS "
                "(must be exactly 1; strict decoders black out on "
                "duplicated parameter sets)", sum.sps_count);
            return 1;
        }
    }
    else if (!xrdp_h264_aux_ok(self->main_buf, self->main_len))
    {
        return 1;
    }
    seq = 0;
    if (self->seq_count > self->seq_head)
    {
        seq = self->seq[self->seq_head++];
    }
    result->generation = self->generation;
    result->desktop_sequence = seq;
    result->main_data = self->main_buf;
    result->main_len = self->main_len;
    result->main_keyframe = self->main_key;
    result->aux_data = NULL;
    result->aux_len = 0;
    self->pairs_returned++;
    self->metrics.pairs_completed++;
    return 0;
}

/*****************************************************************************/
static int
seq_push(struct xrdp_ffmpeg_avc444 *self, unsigned long long s)
{
    if (self->seq_head > 0 && self->seq_head == self->seq_count)
    {
        self->seq_head = 0;
        self->seq_count = 0;
    }
    if (self->seq_count >= self->seq_cap)
    {
        int nc = self->seq_cap ? self->seq_cap * 2 : 16;
        unsigned long long *np = (unsigned long long *)realloc(self->seq,
                                 nc * sizeof(unsigned long long));
        if (np == NULL)
        {
            return 1;
        }
        self->seq = np;
        self->seq_cap = nc;
    }
    self->seq[self->seq_count++] = s;
    return 0;
}

/*****************************************************************************/
int
xrdp_ffmpeg_avc444_encode_pair(struct xrdp_ffmpeg_avc444 *self,
                               const unsigned char *main_nv12,
                               const unsigned char *aux_nv12,
                               int nv12_size,
                               unsigned long long desktop_sequence,
                               struct xrdp_avc444_encoded_pair *result)
{
    int st;

    if (self == NULL || main_nv12 == NULL || aux_nv12 == NULL ||
            nv12_size != self->nv12_size || result == NULL || self->flushing)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (self->cfg.aux_ltr_chain)
    {
        /* EXPERIMENTAL FR-H264-8 + #45 step 5: both children encode
         * normal refs=1 chains and are driven as ONE poll set, so the
         * two views of a pair are encoded concurrently by this single
         * thread; both views' slice headers are then rewritten into ONE
         * shared frame_num chain with per-view long-term slots.
         * The synchronous per-pair API is kept for every existing
         * caller: it is submit + pump(set of 1 handle) + collect. */
        struct xrdp_ffmpeg_avc444 *handles[1];
        int bad_handle;
        int kids_armed;

        st = xrdp_ffmpeg_avc444_submit_pair(self, main_nv12, aux_nv12,
                                            nv12_size, desktop_sequence);
        if (st != XRDP_FFMPEG_PAIR_READY)
        {
            return st;
        }
        handles[0] = self;
        st = xrdp_ffmpeg_avc444_pump_pairs(handles, 1, &bad_handle,
                                           &kids_armed);
        if (st != XRDP_FFMPEG_PAIR_READY)
        {
            return st;
        }
        return xrdp_ffmpeg_avc444_collect_pair(self, desktop_sequence,
                                               result);
    }
    if (self->cfg.aux_intra_leaf)
    {
        /* reference partitioning: the main child sees ONLY main frames
         * (its chain self-references), the leaf child encodes the aux
         * view all-IDR, and the aux packet is rewritten into
         * non-reference I leaves before shipping. See
         * PR-demo/mac_bisect_matrix/CROSS_VIEW_REFERENCE_PROOF.md. */
        struct xrdp_avc444_encoded_pair leaf_result;

        st = xrdp_ffmpeg_avc444_encode_single(self, main_nv12, nv12_size,
                                              desktop_sequence, result);
        if (st != XRDP_FFMPEG_PAIR_READY)
        {
            return st;
        }
        st = xrdp_ffmpeg_avc444_encode_single(self->leaf, aux_nv12,
                                              nv12_size, desktop_sequence,
                                              &leaf_result);
        if (st != XRDP_FFMPEG_PAIR_READY)
        {
            /* the main picture is already consumed; a pair without its
             * aux would desync -- fail loudly and restart */
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: aux_intra_leaf child did "
                "not return the aux picture; restarting encoder");
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        if (grow(&self->aux_buf, &self->aux_cap,
                 leaf_result.main_len) != 0)
        {
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        memcpy(self->aux_buf, leaf_result.main_data, leaf_result.main_len);
        self->aux_len = leaf_result.main_len;
        if (xrdp_h264_aux_to_leaf(self->aux_buf, &self->aux_len,
                                  self->main_buf, self->main_len,
                                  &self->leaf_main_cache,
                                  &self->leaf_aux_cache) != 0)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: aux_intra_leaf rewrite "
                "failed; refusing to ship the pair");
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        result->aux_data = self->aux_buf;
        result->aux_len = self->aux_len;
        return XRDP_FFMPEG_PAIR_READY;
    }
    /* queue this pair's two pictures for the vmsplice feeder; the
     * pointers are BORROWED (capture shmem) and are fully consumed by
     * the child before this call returns READY (FR-PROC-6) */
    if (in_iov_push(self, main_nv12, nv12_size) != 0 ||
            in_iov_push(self, aux_nv12, nv12_size) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (seq_push(self, desktop_sequence) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    self->pairs_submitted++;

    /* SYNCHRONOUS: wait (bounded) for THE pair just submitted. Returning any
     * older pair would ship stale pixels under the caller's current damage
     * region -- the content/region desync behind the mstsc "stuck last frame"
     * bug. A too-deep encoder pipeline (e.g. -async_depth > 1) that cannot
     * return the submitted picture without further input times out here and
     * fails LOUDLY (teardown + respawn) instead of silently desyncing; use a
     * zero-latency encoder config (see gfx.toml). */
    {
        long long deadline = now_ms() + self->cfg.pair_timeout_ms;
        for (;;)
        {
            st = pump(self, deadline, 0);
            if (st == 1 || st == 2)
            {
                return XRDP_FFMPEG_PAIR_ERROR;
            }
            if (pk_available(self) >= 2)
            {
                break;
            }
            if (now_ms() >= deadline)
            {
                LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: submitted pair not "
                    "returned within %d ms (encoder pipeline too deep? use "
                    "-async_depth 1 / -tune zerolatency); restarting encoder",
                    self->cfg.pair_timeout_ms);
                self->metrics.timeouts++;
                return XRDP_FFMPEG_PAIR_ERROR;
            }
        }
    }
    if (in_iov_pending(self))
    {
        /* output implies the child consumed its input; borrowed segments
         * must never outlive this call (FR-PROC-6) */
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: input not fully spliced at "
            "pair return; restarting encoder");
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (pop_pair(self, result) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (result->desktop_sequence != desktop_sequence)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: pair sequence mismatch "
            "(got %llu want %llu); restarting encoder",
            (unsigned long long)result->desktop_sequence,
            (unsigned long long)desktop_sequence);
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    return XRDP_FFMPEG_PAIR_READY;
}

/*****************************************************************************/
/* #45 step 5 -- SUBMIT half of the aux_ltr_chain pair: queue this pair's
 * two pictures into the two children's input iovecs. Nothing is written
 * to a pipe here; the write happens inside the shared poll set, which is
 * what lets 2m children be fed concurrently by one thread. The pointers
 * are BORROWED (capture shmem, FR-PROC-6) and must stay valid until the
 * matching collect returns. */
int
xrdp_ffmpeg_avc444_submit_pair(struct xrdp_ffmpeg_avc444 *self,
                               const unsigned char *main_nv12,
                               const unsigned char *aux_nv12,
                               int nv12_size,
                               unsigned long long desktop_sequence)
{
    if (self == NULL || main_nv12 == NULL ||
            nv12_size != self->nv12_size || self->flushing ||
            !self->cfg.aux_ltr_chain || self->leaf == NULL)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (self->ltr.started && self->ltr.frame_num >= (1 << 16) - 8)
    {
        /* backstop only: the caller must have re-keyed on
         * rekey_pending long before the counter can reach the wrap a
         * per-view decoder cannot survive (measured, see
         * xrdp_h264_annexb.h) */
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: aux_ltr_chain frame_num "
            "%d at wrap backstop; forcing encoder restart",
            self->ltr.frame_num);
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (in_iov_push(self, main_nv12, nv12_size) != 0 ||
            seq_push(self, desktop_sequence) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    self->pairs_submitted++;
    self->trace_out_seen = 0;
    /* BACKLOG #92 / FR-H264-9: aux_nv12 NULL means "chroma is not due
     * this frame". The aux child is simply not fed -- no picture, no
     * sequence entry -- so it has nothing outstanding and pump_pairs
     * will not arm it. Its input index therefore does not advance,
     * which is exactly what makes intra_refresh_frames_aux a count of
     * AUX pictures. */
    self->aux_submitted = (aux_nv12 != NULL);
    if (!self->aux_submitted)
    {
        return XRDP_FFMPEG_PAIR_READY;
    }
    if (in_iov_push(self->leaf, aux_nv12, nv12_size) != 0 ||
            seq_push(self->leaf, desktop_sequence) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    self->leaf->pairs_submitted++;
    self->leaf->trace_out_seen = 0;
    return XRDP_FFMPEG_PAIR_READY;
}

/*****************************************************************************/
/* #45 step 5 -- drive every submitted handle's children as ONE poll set
 * until each handle has both of its pictures, or the ONE shared deadline
 * expires (D3). n_handles is 1 for a single monitor and m for a batched
 * set, so the armed child count is 2m -- the E4 quantity, reported in
 * *kids_armed for the caller to assert and log.
 * On failure *bad_handle names the handle whose child failed: tearing
 * down the wrong one would leave a monitor encoding into a dead child. */
int
xrdp_ffmpeg_avc444_pump_pairs(struct xrdp_ffmpeg_avc444 **handles,
                              int n_handles, int *bad_handle,
                              int *kids_armed)
{
    struct xrdp_ffmpeg_avc444 *kids[FF_PUMP_MAX_KIDS];
    int owner[FF_PUMP_MAX_KIDS];
    long long deadline;
    int nkids;
    int ready;
    int quiet;
    int bad_kid;
    int st;
    int i;

    if (handles == NULL || n_handles < 1 || bad_handle == NULL ||
            kids_armed == NULL)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    *bad_handle = -1;
    *kids_armed = 0;
    nkids = 0;
    for (i = 0; i < n_handles; i++)
    {
        if (handles[i] == NULL || handles[i]->leaf == NULL ||
                !handles[i]->cfg.aux_ltr_chain)
        {
            *bad_handle = i;
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        if (nkids + 2 > FF_PUMP_MAX_KIDS)
        {
            *bad_handle = i;
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        owner[nkids] = i;
        kids[nkids++] = handles[i];
        /* #92: a handle whose aux was not fed this cycle contributes
         * ONE child to the poll set, not two. Arming the aux child
         * anyway would make the set wait for a picture nobody
         * submitted, until the shared deadline killed the whole
         * cycle. */
        if (handles[i]->aux_submitted)
        {
            owner[nkids] = i;
            kids[nkids++] = handles[i]->leaf;
        }
    }
    *kids_armed = nkids;
    /* ONE deadline for the whole set (D3): a per-child budget would cost
     * n x pair_timeout_ms whenever one child stalls */
    deadline = now_ms() + handles[0]->cfg.pair_timeout_ms;
    for (;;)
    {
        ready = 1;
        for (i = 0; i < n_handles; i++)
        {
            if (pk_available(handles[i]) < 1 ||
                    (handles[i]->aux_submitted &&
                     pk_available(handles[i]->leaf) < 1))
            {
                ready = 0;
            }
        }
        if (ready)
        {
            return XRDP_FFMPEG_PAIR_READY;
        }
        if (now_ms() >= deadline)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: submitted set of %d "
                "children not returned within %d ms (encoder pipeline "
                "too deep? use -async_depth 1 / -tune zerolatency); "
                "restarting encoder", nkids,
                handles[0]->cfg.pair_timeout_ms);
            handles[0]->metrics.timeouts++;
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        st = pump_set(kids, nkids, deadline, &quiet, &bad_kid);
        if (st != 0)
        {
            if (bad_kid >= 0 && bad_kid < nkids)
            {
                *bad_handle = owner[bad_kid];
            }
            else
            {
                *bad_handle = 0;
            }
            return XRDP_FFMPEG_PAIR_ERROR;
        }
    }
}

/*****************************************************************************/
/* #45 step 5 -- COLLECT half: pop this handle's two pictures, rewrite
 * both views into the shared LTR chain and hand back the pair. The
 * borrowed-pointer contract (FR-PROC-6) is re-checked per child here,
 * which is the point at which the caller may release the capture slot. */
int
xrdp_ffmpeg_avc444_collect_pair(struct xrdp_ffmpeg_avc444 *self,
                                unsigned long long desktop_sequence,
                                struct xrdp_avc444_encoded_pair *result)
{
    struct xrdp_avc444_encoded_pair ltr_result;
    int budget;

    if (self == NULL || result == NULL || self->leaf == NULL ||
            !self->cfg.aux_ltr_chain)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (in_iov_pending(self) ||
            (self->aux_submitted && in_iov_pending(self->leaf)))
    {
        /* output implies the child consumed its input; borrowed
         * segments must never outlive this call (FR-PROC-6) */
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: input not fully spliced at "
            "pair return; restarting encoder");
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (pk_available(self) < 1 ||
            (self->aux_submitted && pk_available(self->leaf) < 1))
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (pop_single(self, result) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (result->desktop_sequence != desktop_sequence)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: pair sequence mismatch "
            "(got %llu want %llu); restarting encoder",
            (unsigned long long)result->desktop_sequence,
            (unsigned long long)desktop_sequence);
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    budget = xrdp_h264_ltr_growth_budget(self->main_buf, self->main_len);
    if (grow(&self->main_buf, &self->main_cap,
             self->main_len + budget) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (xrdp_h264_ltr_rewrite_main(self->main_buf, &self->main_len,
                                   self->main_cap, &self->ltr) != 0)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: aux_ltr_chain main "
            "rewrite failed; refusing to ship the pair");
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    result->main_data = self->main_buf;
    result->main_len = self->main_len;
    if (!self->aux_submitted)
    {
        /* BACKLOG #92 / FR-H264-9: chroma was not due this frame. The
         * aux child was never fed, so there is nothing to pop and
         * nothing to rewrite; the pair ships as a luma-only frame and
         * the caller emits the LC=1 PDU alone. The aux long-term
         * reference LT1 is untouched and still holds the last chroma
         * picture, which is what the NEXT aux P-slice predicts from --
         * skipping a frame shortens no prediction chain and needs no
         * re-seed. The re-key check below still runs: the shared
         * frame_num counter advanced with the main picture, and a wrap
         * is no less fatal on a luma-only frame. */
        result->aux_data = NULL;
        result->aux_len = 0;
    }
    else
    {
        /* #45 step 4: there is no aux respawn here any more. A main-view
         * cut no longer empties the DPB, so LT1 cannot go missing
         * mid-chain; if it ever does, the aux rewrite below refuses the
         * packet ("aux P with LT1 unseeded") and the pair fails loudly.
         * Respawning the aux child instead would restart that child's
         * frame index while the main child keeps counting, de-phasing
         * its schedule -- one fault made permanent. */
        if (pop_single(self->leaf, &ltr_result) != 0)
        {
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        if (ltr_result.desktop_sequence != desktop_sequence)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: aux picture sequence "
                "mismatch (got %llu want %llu); restarting encoder",
                (unsigned long long)ltr_result.desktop_sequence,
                (unsigned long long)desktop_sequence);
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        budget = xrdp_h264_ltr_growth_budget(ltr_result.main_data,
                                             ltr_result.main_len);
        if (grow(&self->aux_buf, &self->aux_cap,
                 ltr_result.main_len + budget) != 0)
        {
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        memcpy(self->aux_buf, ltr_result.main_data, ltr_result.main_len);
        self->aux_len = ltr_result.main_len;
        if (xrdp_h264_ltr_rewrite_aux(self->aux_buf, &self->aux_len,
                                      self->aux_cap, &self->ltr) != 0)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: aux_ltr_chain aux "
                "rewrite failed; refusing to ship the pair");
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        result->aux_data = self->aux_buf;
        result->aux_len = self->aux_len;
    }
    if (self->ltr.frame_num >= self->cfg.ltr_rekey_frame_num &&
            !self->rekey_pending)
    {
        /* re-key BEFORE the shared counter can wrap (a per-view
         * decoder silently stops at a frame_num wrap -- measured,
         * xrdp_h264_annexb.h). The CURRENT pair still ships (its
         * damage must not be lost); the caller polls
         * xrdp_ffmpeg_avc444_rekey_pending() after shipping and
         * rebuilds the encoder, so the NEXT frame is a fresh IDR.
         * Roughly once an hour of continuous encoding. */
        LOG(LOG_LEVEL_INFO, "xrdp_ffmpeg: aux_ltr_chain frame_num "
            "%d reached the re-key threshold %d; re-key requested",
            self->ltr.frame_num, self->cfg.ltr_rekey_frame_num);
        self->rekey_pending = 1;
    }
    return XRDP_FFMPEG_PAIR_READY;
}

/*****************************************************************************/
int
xrdp_ffmpeg_avc444_encode_single(struct xrdp_ffmpeg_avc444 *self,
                                 const unsigned char *nv12,
                                 int nv12_size,
                                 unsigned long long desktop_sequence,
                                 struct xrdp_avc444_encoded_pair *result)
{
    int st;

    if (self == NULL || nv12 == NULL ||
            nv12_size != self->nv12_size || result == NULL || self->flushing)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    /* queue this frame's single picture for writing */
    /* borrowed pointer for the vmsplice feeder (FR-PROC-6) */
    if (in_iov_push(self, nv12, nv12_size) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (seq_push(self, desktop_sequence) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    self->pairs_submitted++;

    /* SYNCHRONOUS: wait (bounded) for THE picture just submitted; see the
     * matching comment in encode_pair for why returning an older one is a
     * correctness bug (content/region desync). */
    {
        long long deadline = now_ms() + self->cfg.picture_timeout_ms;
        for (;;)
        {
            st = pump(self, deadline, 0);
            if (st == 1 || st == 2)
            {
                return XRDP_FFMPEG_PAIR_ERROR;
            }
            if (pk_available(self) >= 1)
            {
                break;
            }
            if (now_ms() >= deadline)
            {
                LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: submitted picture not "
                    "returned within %d ms (encoder pipeline too deep? use "
                    "-async_depth 1 / -tune zerolatency); restarting encoder",
                    self->cfg.picture_timeout_ms);
                self->metrics.timeouts++;
                return XRDP_FFMPEG_PAIR_ERROR;
            }
        }
    }
    if (in_iov_pending(self))
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: input not fully spliced at "
            "picture return; restarting encoder");
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (pop_single(self, result) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (result->desktop_sequence != desktop_sequence)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: picture sequence mismatch "
            "(got %llu want %llu); restarting encoder",
            (unsigned long long)result->desktop_sequence,
            (unsigned long long)desktop_sequence);
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    return XRDP_FFMPEG_PAIR_READY;
}

/*****************************************************************************/
int
xrdp_ffmpeg_avc444_flush_next(struct xrdp_ffmpeg_avc444 *self,
                              struct xrdp_avc444_encoded_pair *result)
{
    int st;

    if (self == NULL || result == NULL)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (!self->flushing)
    {
        /* write any remaining queued input, then close to force EOF flush */
        st = pump(self, now_ms() + self->cfg.pair_timeout_ms, 0);
        if (st == 1)
        {
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        if (self->in_fd >= 0)
        {
            close(self->in_fd);
            self->in_fd = -1;
        }
        self->flushing = 1;
    }
    if (pk_available(self) <
            ((self->cfg.aux_intra_leaf || self->cfg.aux_ltr_chain)
             ? 1 : 2))
    {
        st = pump(self, now_ms() + self->cfg.pair_timeout_ms, 1);
        if (st == 1)
        {
            return XRDP_FFMPEG_PAIR_ERROR;
        }
    }
    if (self->cfg.aux_intra_leaf || self->cfg.aux_ltr_chain)
    {
        /* the main child holds single pictures in leaf/LTR mode; the
         * aux child is synchronous per call and never has a tail */
        if (pk_available(self) >= 1)
        {
            if (pop_single(self, result) != 0)
            {
                return XRDP_FFMPEG_PAIR_ERROR;
            }
            if (self->cfg.aux_ltr_chain)
            {
                /* a tail main picture must still join the shared
                 * chain -- never ship an unrewritten frame_num */
                int budget;

                budget = xrdp_h264_ltr_growth_budget(self->main_buf,
                                                     self->main_len);
                if (grow(&self->main_buf, &self->main_cap,
                         self->main_len + budget) != 0 ||
                        xrdp_h264_ltr_rewrite_main(self->main_buf,
                                                   &self->main_len,
                                                   self->main_cap,
                                                   &self->ltr) != 0)
                {
                    return XRDP_FFMPEG_PAIR_ERROR;
                }
                result->main_data = self->main_buf;
                result->main_len = self->main_len;
            }
            return XRDP_FFMPEG_PAIR_READY;
        }
        return XRDP_FFMPEG_PAIR_DONE;
    }
    if (pk_available(self) >= 2)
    {
        if (pop_pair(self, result) != 0)
        {
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        return XRDP_FFMPEG_PAIR_READY;
    }
    return XRDP_FFMPEG_PAIR_DONE;
}

/*****************************************************************************/
struct xrdp_ffmpeg_avc444 *
xrdp_ffmpeg_avc444_create(const struct xrdp_ffmpeg_avc444_config *cfg,
                          int actual_width, int actual_height)
{
    struct xrdp_ffmpeg_avc444 *self;

    if (cfg == NULL || cfg->path[0] != '/' ||
            actual_width < 1 || actual_height < 1 ||
            actual_width > 16384 || actual_height > 16384)
    {
        return NULL;
    }
    if (cfg->aux_ltr_chain &&
            (cfg->fault_aux_delay || cfg->fault_strip_mmco))
    {
        /* the LTR path never runs pop_pair (fault_aux_delay) and
         * replaces all marking (fault_strip_mmco): the diagnostics
         * would be silently inert, and a bisect arm run with them
         * would report a green result that means nothing */
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: aux_ltr_chain is not "
            "compatible with fault_aux_delay/fault_strip_mmco "
            "(diagnostic would be silently inert); refusing to start");
        return NULL;
    }
    self = (struct xrdp_ffmpeg_avc444 *)g_malloc(sizeof(*self), 1);
    if (self == NULL)
    {
        return NULL;
    }
    self->cfg = *cfg;
    if (self->cfg.ltr_rekey_frame_num < XRDP_H264_LTR_FRAME_NUM_REKEY_MIN ||
            self->cfg.ltr_rekey_frame_num >
            XRDP_H264_LTR_FRAME_NUM_REKEY_MAX)
    {
        /* out of range is CLAMPED, never honoured: above the max a
         * decoder would meet the frame_num wrap the re-key exists to
         * prevent (BACKLOG #48) */
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: ltr_rekey_frame_num %d out "
            "of range [%d,%d]; clamped", self->cfg.ltr_rekey_frame_num,
            XRDP_H264_LTR_FRAME_NUM_REKEY_MIN,
            XRDP_H264_LTR_FRAME_NUM_REKEY_MAX);
        self->cfg.ltr_rekey_frame_num =
            self->cfg.ltr_rekey_frame_num < XRDP_H264_LTR_FRAME_NUM_REKEY_MIN
            ? XRDP_H264_LTR_FRAME_NUM_REKEY_MIN
            : XRDP_H264_LTR_FRAME_NUM_REKEY_MAX;
    }
    if (self->cfg.intra_refresh_frames <
            XRDP_H264_INTRA_REFRESH_FRAMES_MIN ||
            self->cfg.intra_refresh_frames >
            XRDP_H264_INTRA_REFRESH_FRAMES_MAX)
    {
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: intra_refresh_frames %d out "
            "of range [%d,%d]; clamped", self->cfg.intra_refresh_frames,
            XRDP_H264_INTRA_REFRESH_FRAMES_MIN,
            XRDP_H264_INTRA_REFRESH_FRAMES_MAX);
        self->cfg.intra_refresh_frames =
            self->cfg.intra_refresh_frames <
            XRDP_H264_INTRA_REFRESH_FRAMES_MIN
            ? XRDP_H264_INTRA_REFRESH_FRAMES_MIN
            : XRDP_H264_INTRA_REFRESH_FRAMES_MAX;
    }
    if (self->cfg.intra_refresh_frames_aux < 1)
    {
        /* "not declared" -- FOLLOW THE MAIN INTERVAL. Silent and
         * deliberate: it is the value that makes a caller which never
         * heard of this field behave exactly as it did before the
         * field existed, which is every gfx.toml written before #92
         * and every direct-config caller in tests/. */
        self->cfg.intra_refresh_frames_aux = self->cfg.intra_refresh_frames;
    }
    else if (self->cfg.intra_refresh_frames_aux <
             XRDP_H264_INTRA_REFRESH_FRAMES_MIN ||
             self->cfg.intra_refresh_frames_aux >
             XRDP_H264_INTRA_REFRESH_FRAMES_MAX)
    {
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: intra_refresh_frames_aux %d "
            "out of range [%d,%d]; clamped",
            self->cfg.intra_refresh_frames_aux,
            XRDP_H264_INTRA_REFRESH_FRAMES_MIN,
            XRDP_H264_INTRA_REFRESH_FRAMES_MAX);
        self->cfg.intra_refresh_frames_aux =
            self->cfg.intra_refresh_frames_aux <
            XRDP_H264_INTRA_REFRESH_FRAMES_MIN
            ? XRDP_H264_INTRA_REFRESH_FRAMES_MIN
            : XRDP_H264_INTRA_REFRESH_FRAMES_MAX;
    }
    /* the schedule reaches EACH child through this runner-internal
     * field: the aux child's config has aux_ltr_chain cleared, so
     * build_argv cannot key off that flag, and a child with no schedule
     * would never cut. spawn_second_child fills in the AUX interval,
     * which is a separate integer counted in that child's own pictures
     * (owner directive 2026-08-08 -- two frame counters, no time). */
    if (self->cfg.aux_ltr_chain)
    {
        self->cfg.intra_refresh_schedule = self->cfg.intra_refresh_frames;
    }
    /* else: keep whatever the caller set. The aux child is created with
     * aux_ltr_chain cleared and the schedule already filled in by
     * spawn_second_child; recomputing it here would silently unschedule
     * exactly one of the two children. */
    /* the rewriter checks OBSERVED against REQUESTED with the same
     * numbers the children were spawned with -- one source of truth per
     * view. refresh_period_aux stays 0 unless this handle owns an aux
     * child, so a plain single-view handle is unaffected. */
    self->ltr.refresh_period = self->cfg.intra_refresh_schedule;
    self->ltr.refresh_period_aux = 0;
    self->in_fd = -1;
    self->out_fd = -1;
    self->err_fd = -1;
    self->pid = -1;
    self->actual_width = actual_width;
    self->actual_height = actual_height;
    self->coded_width = (cfg->chroma_align == 32)
                        ? ((actual_width + 31) & ~31)
                        : round_up_16(actual_width);
    self->coded_height = round_up_16(actual_height);
    self->nv12_size = self->coded_width * self->coded_height +
                      self->coded_width * (self->coded_height / 2);
    self->generation = 1;
    self->nut = xrdp_nut_create(cfg->max_nut_header_bytes,
                                cfg->max_encoded_picture_bytes,
                                cfg->max_encoded_pair_bytes);
    if (self->nut == NULL)
    {
        g_free(self);
        return NULL;
    }
    /* &self->cfg, not cfg: the clamps above and the schedule field are
     * what must reach the argv */
    if (spawn_child(&self->cfg, self->coded_width, self->coded_height,
                    &self->in_fd, &self->out_fd, &self->err_fd,
                    &self->pid) != 0)
    {
        xrdp_nut_delete(self->nut);
        g_free(self);
        return NULL;
    }
    LOG(LOG_LEVEL_INFO, "xrdp_ffmpeg: spawned ffmpeg pid %d coded %dx%d "
        "generation %llu", self->pid, self->coded_width, self->coded_height,
        (unsigned long long)self->generation);
    if (cfg->aux_intra_leaf || cfg->aux_ltr_chain)
    {
        if (spawn_second_child(self) != 0)
        {
            xrdp_ffmpeg_avc444_delete(self);
            return NULL;
        }
    }
    return self;
}

/*****************************************************************************/
/* spawn the second (aux-view) child: all-IDR for the leaf architecture
 * (FR-H264-7), a normal refs chain with the SAME scheduled paired
 * refresh as the main child for the LTR aux-chain (FR-H264-8). Called
 * exactly once per encoder, at create: there is no aux respawn any more
 * (#45 step 4 -- a scheduled refresh no longer empties the DPB, so LT1
 * never needs re-seeding mid-stream).
 * Diagnostic/rewrite knobs are cleared -- the rewrites drop the aux
 * SPS/PPS/SEI themselves. */
static int
spawn_second_child(struct xrdp_ffmpeg_avc444 *self)
{
    struct xrdp_ffmpeg_avc444_config leaf_cfg = self->cfg;
    static const char *const extra[] =
    {
        "-forced-idr", "1", "-force_key_frames", "expr:gte(t,0)"
    };
    int i;

    leaf_cfg.aux_intra_leaf = 0;
    leaf_cfg.aux_ltr_chain = 0;
    leaf_cfg.fault_aux_delay = 0;
    leaf_cfg.fault_strip_mmco = 0;
    leaf_cfg.sanitize_hrd = 0;
    leaf_cfg.strip_pic_struct = 0;
    leaf_cfg.strip_sei = 0;
    /* the AUX child's own frame-indexed schedule, counted in ITS input
     * pictures. With the sparse-aux cadence off the two intervals are
     * equal, so a cut lands on the same picture ordinal in both views
     * exactly as before (step 2, and what the wire audit's A3 check
     * asserts). With it on the aux child is fed fewer pictures, so the
     * same ordinal is a later wall-clock instant -- which is why this
     * is a separate integer rather than a share of the main one. */
    leaf_cfg.intra_refresh_schedule = self->cfg.aux_ltr_chain
                                      ? self->cfg.intra_refresh_frames_aux
                                      : self->cfg.intra_refresh_schedule;
    /* the rewriter must check the aux view against the number the aux
     * child was actually spawned with */
    self->ltr.refresh_period_aux = leaf_cfg.intra_refresh_schedule;
    if (!self->cfg.aux_ltr_chain)
    {
        if (leaf_cfg.encoder_args.count + 4 > XRDP_AVC444_MAX_ENC_ARGS)
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: no room to append the "
                "aux_intra_leaf forced-IDR args");
            return 1;
        }
        for (i = 0; i < 4; i++)
        {
            g_strncpy(leaf_cfg.encoder_args.arg
                      [leaf_cfg.encoder_args.count + i],
                      extra[i], XRDP_AVC444_ENC_ARG_LEN - 1);
        }
        leaf_cfg.encoder_args.count += 4;
    }
    self->leaf = xrdp_ffmpeg_avc444_create(&leaf_cfg, self->actual_width,
                                           self->actual_height);
    if (self->leaf == NULL)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: aux child failed to start");
        return 1;
    }
    LOG(LOG_LEVEL_INFO, "xrdp_ffmpeg: %s active (aux child pid %d)",
        self->cfg.aux_ltr_chain ? "aux_ltr_chain (EXPERIMENTAL)"
        : "aux_intra_leaf", self->leaf->pid);
    return 0;
}

/*****************************************************************************/
/* aux_ltr_chain: the shared frame_num counter is near its wrap; the
 * caller must delete and recreate the encoder AFTER shipping the
 * current pair (never before -- the triggering frame's damage would
 * be lost, the stuck-last-frame class) */
int
xrdp_ffmpeg_avc444_rekey_pending(struct xrdp_ffmpeg_avc444 *self)
{
    return self != NULL ? self->rekey_pending : 0;
}

/*****************************************************************************/
int
xrdp_ffmpeg_avc444_coded_width(struct xrdp_ffmpeg_avc444 *self)
{
    return self != NULL ? self->coded_width : 0;
}

/*****************************************************************************/
/* number of submitted frames not yet returned = frames held in the encoder  */
/* pipeline (depth = async_depth-1 for VAAPI / frame-thread window for x264;  */
/* zero with the shipped low-latency args). Bounds the tail-flush drain.      */
int
xrdp_ffmpeg_avc444_inflight(struct xrdp_ffmpeg_avc444 *self)
{
    if (self == NULL)
    {
        return 0;
    }
    return (int)(self->pairs_submitted - self->pairs_returned);
}

/*****************************************************************************/
int
xrdp_ffmpeg_avc444_coded_height(struct xrdp_ffmpeg_avc444 *self)
{
    return self != NULL ? self->coded_height : 0;
}

/*****************************************************************************/
void
xrdp_ffmpeg_avc444_get_metrics(struct xrdp_ffmpeg_avc444 *self,
                               struct xrdp_ffmpeg_avc444_metrics *out)
{
    if (self != NULL && out != NULL)
    {
        *out = self->metrics;
    }
}

/*****************************************************************************/
void
xrdp_ffmpeg_avc444_delete(struct xrdp_ffmpeg_avc444 *self)
{
    int i;

    if (self == NULL)
    {
        return;
    }
    xrdp_ffmpeg_avc444_delete(self->leaf);
    self->leaf = NULL;
    xrdp_h264_ltr_state_free(&self->ltr);
    if (self->in_fd >= 0)
    {
        close(self->in_fd);
        self->in_fd = -1;
    }
    reap_child(self->pid, self->cfg.terminate_grace_ms);
    if (self->out_fd >= 0)
    {
        close(self->out_fd);
    }
    if (self->err_fd >= 0)
    {
        close(self->err_fd);
    }
    xrdp_nut_delete(self->nut);
    for (i = 0; i < self->pk_cap; i++)
    {
        g_free(self->pk[i].data);
    }
    g_free(self->pk);
    g_free(self->seq);
    g_free(self->main_buf);
    g_free(self->aux_buf);
    g_free(self->fault_aux_buf);
    g_free(self);
}

/* ------------------------------------------------------------------------ */
/* Bounded behavioral probe (PRD FR-PROBE). The probe closes its input      */
/* after the four pictures so the child flushes on EOF and is disposable.   */

/*****************************************************************************/
static void
make_probe_frame(unsigned char *buf, int nv12_size, int cw, int ch, int idx)
{
    int y;
    int x;

    for (y = 0; y < ch; y++)
    {
        for (x = 0; x < cw; x++)
        {
            buf[y * cw + x] = (unsigned char)((x + y + idx * 37) & 0xff);
        }
    }
    memset(buf + cw * ch, (unsigned char)(0x80 + idx * 8),
           nv12_size - cw * ch);
}

/*****************************************************************************/
const char *
xrdp_ffmpeg_probe_result_str(enum xrdp_ffmpeg_probe_result res)
{
    switch (res)
    {
        case XRDP_FFMPEG_PROBE_OK:
            return "OK";
        case XRDP_FFMPEG_PROBE_BAD_CONFIG:
            return "BAD_CONFIG";
        case XRDP_FFMPEG_PROBE_SPAWN_FAIL:
            return "SPAWN_FAIL";
        case XRDP_FFMPEG_PROBE_TIMEOUT:
            return "TIMEOUT";
        case XRDP_FFMPEG_PROBE_STREAM_ERROR:
            return "STREAM_ERROR";
        case XRDP_FFMPEG_PROBE_CONTENT_REJECT:
            return "CONTENT_REJECT";
    }
    return "UNKNOWN";
}

#define FF_PROBE_ERRLINE 512

/*****************************************************************************/
/* split probe child stderr into bounded lines and log each one (the        */
/* runtime path does the same via drain_stderr; the probe has no handle to  */
/* hold the line buffer, so the caller keeps one on its stack)              */
static void
probe_log_stderr(char *line, int *line_len, const char *tmp, int n)
{
    int i;

    for (i = 0; i < n; i++)
    {
        if (tmp[i] == '\n' || *line_len >= FF_PROBE_ERRLINE - 1)
        {
            if (*line_len > 0)
            {
                log_stderr_line(line, *line_len);
            }
            *line_len = 0;
            if (tmp[i] == '\n')
            {
                continue;
            }
        }
        line[(*line_len)++] = tmp[i];
    }
}

/*****************************************************************************/
/* render a waitpid status for the probe log                                */
static void
describe_child_status(int status, char *out, int out_size)
{
    if (status < 0)
    {
        snprintf(out, out_size, "not reaped");
    }
    else if (WIFEXITED(status))
    {
        snprintf(out, out_size, "exited %d", WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        snprintf(out, out_size, "killed by signal %d", WTERMSIG(status));
    }
    else
    {
        snprintf(out, out_size, "status 0x%x", status);
    }
}

/*****************************************************************************/
enum xrdp_ffmpeg_probe_result
xrdp_ffmpeg_avc444_probe(const struct xrdp_ffmpeg_avc444_config *cfg,
                         int coded_width, int coded_height)
{
    int in_fd = -1;
    int out_fd = -1;
    int err_fd = -1;
    int pid = -1;
    int nv12_size;
    unsigned char *blob = NULL;
    struct xrdp_nut_ctx *nut = NULL;
    int i;
    int off = 0;
    int total;
    int got = 0;
    long long start;
    long long deadline;
    int fail = 0;
    int closed = 0;
    long long last_pts = -1;
    enum xrdp_ffmpeg_probe_result res = XRDP_FFMPEG_PROBE_OK;
    const char *why = "";
    struct xrdp_h264_ltr_state ltr_m;
    struct xrdp_h264_ltr_state ltr_a;
    char errline[FF_PROBE_ERRLINE];
    int errline_len = 0;
    int child_status = -1;
    char status_str[64];

    if (cfg == NULL || cfg->path[0] != '/' || coded_width < 16 ||
            coded_height < 16)
    {
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: probe BAD_CONFIG: path not "
            "absolute or coded size below 16");
        return XRDP_FFMPEG_PROBE_BAD_CONFIG;
    }
    memset(&ltr_m, 0, sizeof(ltr_m));
    memset(&ltr_a, 0, sizeof(ltr_a));
    nv12_size = coded_width * coded_height + coded_width * (coded_height / 2);
    total = 4 * nv12_size;
    blob = (unsigned char *)malloc(total);
    if (blob == NULL)
    {
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: probe SPAWN_FAIL: out of "
            "memory");
        return XRDP_FFMPEG_PROBE_SPAWN_FAIL;
    }
    for (i = 0; i < 4; i++)
    {
        make_probe_frame(blob + i * nv12_size, nv12_size, coded_width,
                         coded_height, i);
    }
    nut = xrdp_nut_create(cfg->max_nut_header_bytes,
                          cfg->max_encoded_picture_bytes,
                          cfg->max_encoded_pair_bytes);
    if (nut == NULL)
    {
        free(blob);
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: probe SPAWN_FAIL: out of "
            "memory");
        return XRDP_FFMPEG_PROBE_SPAWN_FAIL;
    }
    start = now_ms();
    if (spawn_child(cfg, coded_width, coded_height, &in_fd, &out_fd,
                    &err_fd, &pid) != 0)
    {
        xrdp_nut_delete(nut);
        free(blob);
        LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: probe SPAWN_FAIL: cannot "
            "spawn %s", cfg->path);
        return XRDP_FFMPEG_PROBE_SPAWN_FAIL;
    }
    deadline = start + (cfg->stream_ready_timeout_ms > 0 ?
                        cfg->stream_ready_timeout_ms * 2 : 4000);
    while (!fail && got < 4)
    {
        struct pollfd pfd[3];
        int nfds = 0;
        int in_slot = -1;
        int timeout;
        int rv;
        char tmp[FF_READ_CHUNK];
        int n;

        if (now_ms() >= deadline)
        {
            res = XRDP_FFMPEG_PROBE_TIMEOUT;
            why = "no verdict within the deadline (cold encoder/device "
            "init? child still starting?)";
            fail = 1;
            break;
        }
        if (!closed && off < total)
        {
            pfd[nfds].fd = in_fd;
            pfd[nfds].events = POLLOUT;
            in_slot = nfds;
            nfds++;
        }
        pfd[nfds].fd = out_fd;
        pfd[nfds].events = POLLIN;
        nfds++;
        pfd[nfds].fd = err_fd;
        pfd[nfds].events = POLLIN;
        nfds++;
        timeout = (int)(deadline - now_ms());
        rv = poll(pfd, nfds, timeout > 0 ? timeout : 0);
        if (rv < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            res = XRDP_FFMPEG_PROBE_STREAM_ERROR;
            why = "poll failed";
            fail = 1;
            break;
        }
        if (in_slot >= 0 && (pfd[in_slot].revents & POLLOUT))
        {
            struct iovec iov;
            ssize_t w;

            iov.iov_base = blob + off;
            iov.iov_len = total - off;
            w = vmsplice(in_fd, &iov, 1, SPLICE_F_NONBLOCK);
            if (w > 0)
            {
                off += (int)w;
            }
            else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                     errno != EINTR)
            {
                res = XRDP_FFMPEG_PROBE_STREAM_ERROR;
                why = "write to the child's input pipe failed";
                fail = 1;
                break;
            }
        }
        if (!closed && off >= total)
        {
            /* force the child to flush all four pictures via EOF */
            close(in_fd);
            in_fd = -1;
            closed = 1;
        }
        while ((n = (int)read(err_fd, tmp, sizeof(tmp))) > 0)
        {
            probe_log_stderr(errline, &errline_len, tmp, n);
        }
        while ((n = (int)read(out_fd, tmp, sizeof(tmp))) > 0)
        {
            if (xrdp_nut_feed(nut, (unsigned char *)tmp, n) != 0)
            {
                res = XRDP_FFMPEG_PROBE_STREAM_ERROR;
                why = "NUT feed rejected (buffer limit exceeded)";
                fail = 1;
                break;
            }
            for (;;)
            {
                struct xrdp_nut_packet pkt;
                enum xrdp_nut_event_type ev = xrdp_nut_next(nut, &pkt);
                if (ev == XRDP_NUT_NEED_MORE)
                {
                    break;
                }
                if (ev == XRDP_NUT_STREAM_READY)
                {
                    continue;
                }
                if (ev == XRDP_NUT_ERROR)
                {
                    res = XRDP_FFMPEG_PROBE_STREAM_ERROR;
                    why = "NUT parse error";
                    fail = 1;
                    break;
                }
                if (pkt.pts <= last_pts && got > 0)
                {
                    res = XRDP_FFMPEG_PROBE_STREAM_ERROR;
                    why = "non-monotonic pts";
                    fail = 1;
                    break;
                }
                last_pts = pkt.pts;
                if (got == 0)
                {
                    struct xrdp_h264_nal_summary sum;
                    int scan_ok =
                        (xrdp_h264_scan_annexb(pkt.data, pkt.len,
                                               &sum) == 0);
                    if (!pkt.keyframe || !scan_ok || !sum.has_sps ||
                            !sum.has_pps || !sum.has_idr)
                    {
                        res = XRDP_FFMPEG_PROBE_CONTENT_REJECT;
                        why = "first packet lacks keyframe flag or in-band "
                              "SPS/PPS/IDR (extradata-only encoder such as "
                              "h264_nvenc? set [avc444_ffmpeg] "
                              "dump_extra = true in gfx.toml)";
                        fail = 1;
                        break;
                    }
                    if (sum.sps_count != 1)
                    {
                        res = XRDP_FFMPEG_PROBE_CONTENT_REJECT;
                        why = "duplicated in-band SPS in the first packet "
                              "(the encoder already repeats headers; set "
                              "[avc444_ffmpeg] dump_extra = false -- "
                              "strict decoders black out on duplicated "
                              "parameter sets)";
                        fail = 1;
                        break;
                    }
                }
                else if (!xrdp_h264_aux_ok(pkt.data, pkt.len))
                {
                    res = XRDP_FFMPEG_PROBE_CONTENT_REJECT;
                    why = "follow-up packet lacks a VCL NAL";
                    fail = 1;
                    break;
                }
                if (cfg->aux_ltr_chain)
                {
                    /* FR-H264-8: exercise BOTH LTR rewriters on the
                     * probe packets. The LTR guard is strictly harder
                     * than the leaf checks (CABAC, poc_type 2, no
                     * weighted pred, level DPB budget...); a shape it
                     * rejects must fail HERE, at the probe, never as
                     * a per-frame encoder respawn loop live. */
                    int blen = pkt.len;
                    int bcap = pkt.len +
                               xrdp_h264_ltr_growth_budget(pkt.data,
                                                           pkt.len);
                    unsigned char *scratch =
                        (unsigned char *)malloc(bcap);

                    if (scratch == NULL)
                    {
                        res = XRDP_FFMPEG_PROBE_STREAM_ERROR;
                        why = "out of memory";
                        fail = 1;
                        break;
                    }
                    memcpy(scratch, pkt.data, pkt.len);
                    if (xrdp_h264_ltr_rewrite_main(scratch, &blen,
                                                   bcap, &ltr_m) != 0)
                    {
                        free(scratch);
                        res = XRDP_FFMPEG_PROBE_CONTENT_REJECT;
                        why = "aux_ltr_chain guard rejects this "
                              "encoder's stream shape (needs CABAC, "
                              "poc_type 2, refs=1, no weighted pred, "
                              "level with a 3-frame DPB budget)";
                        fail = 1;
                        break;
                    }
                    ltr_a.main_cache = ltr_m.main_cache;
                    blen = pkt.len;
                    memcpy(scratch, pkt.data, pkt.len);
                    if (xrdp_h264_ltr_rewrite_aux(scratch, &blen,
                                                  bcap, &ltr_a) != 0)
                    {
                        free(scratch);
                        res = XRDP_FFMPEG_PROBE_CONTENT_REJECT;
                        why = "aux_ltr_chain guard rejects this "
                              "encoder's stream shape on the aux "
                              "rewrite path";
                        fail = 1;
                        break;
                    }
                    free(scratch);
                }
                got++;
                if (got >= 4)
                {
                    break;
                }
            }
            if (fail || got >= 4)
            {
                break;
            }
        }
        if (n == 0 && got < 4)
        {
            res = XRDP_FFMPEG_PROBE_STREAM_ERROR;
            why = "child closed stdout before four packets";
            fail = 1;
            break;
        }
    }
    if (!fail && got < 4)
    {
        /* defensive: loop left without a verdict */
        res = XRDP_FFMPEG_PROBE_STREAM_ERROR;
        why = "incomplete";
        fail = 1;
    }
    if (in_fd >= 0)
    {
        close(in_fd);
        in_fd = -1;
    }
    child_status = reap_child(pid, cfg->terminate_grace_ms);
    if (out_fd >= 0)
    {
        close(out_fd);
    }
    if (err_fd >= 0)
    {
        close(err_fd);
    }
    if (errline_len > 0)
    {
        log_stderr_line(errline, errline_len);
    }
    xrdp_nut_delete(nut);
    free(blob);
    if (!fail)
    {
        LOG(LOG_LEVEL_INFO, "xrdp_ffmpeg: probe OK (dump_extra=%d) at "
            "%dx%d in %lld ms", cfg->use_dump_extra, coded_width,
            coded_height, now_ms() - start);
        return XRDP_FFMPEG_PROBE_OK;
    }
    describe_child_status(child_status, status_str, sizeof(status_str));
    LOG(LOG_LEVEL_WARNING, "xrdp_ffmpeg: probe %s: %s (dump_extra=%d, "
        "%dx%d, packets=%d, elapsed=%lld ms, child %s)",
        xrdp_ffmpeg_probe_result_str(res), why, cfg->use_dump_extra,
        coded_width, coded_height, got, now_ms() - start, status_str);
    return res;
}
