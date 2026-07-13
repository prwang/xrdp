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
 * A stock ffmpeg reading a persistent pipe emits output one picture behind
 * its input and flushes the final picture only on EOF. The runner is
 * therefore pipelined: encode_pair() submits a pair and returns the oldest
 * completed pair; flush_next() closes the input and drains the remainder.
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

#include "xrdp_encoder_ffmpeg.h"
#include "xrdp_nut.h"
#include "xrdp_h264_annexb.h"
#include "log.h"
#include "os_calls.h"

#define FF_CHILD_RAW_FD 3
#define FF_MAX_ARGV 80
#define FF_READ_CHUNK 65536
#define FF_MAX_INFLIGHT_PAIRS 8

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

    /* pending raw input bytes waiting to be written to the child */
    unsigned char *inq;
    int inq_cap;
    int inq_len;
    int inq_off;

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

    char errline[512];
    int errline_len;

    struct xrdp_ffmpeg_avc444_metrics metrics;
};

/*****************************************************************************/
void
xrdp_ffmpeg_avc444_config_default(struct xrdp_ffmpeg_avc444_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->desktop_fps = 60;
    cfg->stream_ready_timeout_ms = 2000;
    cfg->picture_timeout_ms = 2000;
    cfg->pair_timeout_ms = 2000;
    cfg->terminate_grace_ms = 250;
    cfg->quality_crf = 18;
    cfg->gop_pictures = 240;
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
    ADD("-i");
    ADD("pipe:3");
    ADD("-map");
    ADD("0:v:0");
    ADD("-an");
    ADD("-sn");
    ADD("-dn");
    ADD("-fps_mode");
    ADD("passthrough");
    ADD("-c:v");
    ADD("libx264");
    ADD("-bf");
    ADD("0");
    ADD("-preset");
    ADD("ultrafast");
    ADD("-tune");
    ADD("zerolatency");
    ADD("-crf");
    ADDNUM("%d", cfg->quality_crf);
    ADD("-g");
    ADDNUM("%d", cfg->gop_pictures);
    ADD("-x264-params");
    ADD("repeat-headers=1");
    ADD("-bsf:v");
    ADD("h264_mp4toannexb");
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
    char *argv[FF_MAX_ARGV];
    char num_store[128];

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
    *in_fd = inpipe[1];
    *out_fd = outpipe[0];
    *err_fd = errpipe[0];
    *out_pid = pid;
    return 0;
}

/*****************************************************************************/
static void
reap_child(int pid, int grace_ms)
{
    long long deadline;
    int status;

    if (pid <= 0)
    {
        return;
    }
    kill(pid, SIGTERM);
    deadline = now_ms() + (grace_ms > 0 ? grace_ms : 250);
    for (;;)
    {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid || (r < 0 && errno == ECHILD))
        {
            return;
        }
        if (now_ms() >= deadline)
        {
            break;
        }
        usleep(5000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
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
pump(struct xrdp_ffmpeg_avc444 *self, long long deadline, int drain_to_eof)
{
    for (;;)
    {
        struct pollfd pfd[3];
        int nfds = 0;
        int in_slot = -1;
        int want_write;
        int timeout;
        int rv;
        int dr;

        if (now_ms() >= deadline)
        {
            return 0;
        }
        want_write = (self->in_fd >= 0 && self->inq_off < self->inq_len);
        if (want_write)
        {
            pfd[nfds].fd = self->in_fd;
            pfd[nfds].events = POLLOUT;
            in_slot = nfds;
            nfds++;
        }
        pfd[nfds].fd = self->out_fd;
        pfd[nfds].events = POLLIN;
        nfds++;
        if (self->err_fd >= 0)
        {
            pfd[nfds].fd = self->err_fd;
            pfd[nfds].events = POLLIN;
            nfds++;
        }
        timeout = want_write ? (int)(deadline - now_ms()) : 10;
        if (timeout < 0)
        {
            timeout = 0;
        }
        rv = poll(pfd, nfds, timeout);
        if (rv < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return 1;
        }
        if (rv == 0)
        {
            if (want_write || drain_to_eof)
            {
                continue; /* keep waiting (for write slot or flushed output) */
            }
            return 0; /* nothing more to read right now */
        }
        if (in_slot >= 0 && (pfd[in_slot].revents & (POLLOUT | POLLERR | POLLHUP)))
        {
            if (pfd[in_slot].revents & (POLLERR | POLLHUP))
            {
                return 1;
            }
            {
                int remain = self->inq_len - self->inq_off;
                int w = (int)write(self->in_fd, self->inq + self->inq_off,
                                   remain);
                if (w > 0)
                {
                    self->inq_off += w;
                    self->metrics.input_bytes += w;
                    if (self->inq_off >= self->inq_len)
                    {
                        self->inq_len = 0;
                        self->inq_off = 0;
                    }
                }
                else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                         errno != EINTR)
                {
                    return 1;
                }
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
        if (!want_write && dr == 0 && !drain_to_eof)
        {
            return 0; /* input flushed and output drained */
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

    if (self->pairs_returned == 0)
    {
        if (!xrdp_h264_main_reset_ok(self->main_buf, self->main_len))
        {
            LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: reset main packet lacks "
                "SPS/PPS/IDR");
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
    /* queue this pair's two pictures for writing */
    if (grow(&self->inq, &self->inq_cap, self->inq_len + 2 * nv12_size) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    memcpy(self->inq + self->inq_len, main_nv12, nv12_size);
    memcpy(self->inq + self->inq_len + nv12_size, aux_nv12, nv12_size);
    self->inq_len += 2 * nv12_size;
    if (seq_push(self, desktop_sequence) != 0)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    self->pairs_submitted++;

    st = pump(self, now_ms() + self->cfg.picture_timeout_ms, 0);
    if (st == 1 || st == 2)
    {
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    if (pk_available(self) >= 2)
    {
        if (pop_pair(self, result) != 0)
        {
            return XRDP_FFMPEG_PAIR_ERROR;
        }
        return XRDP_FFMPEG_PAIR_READY;
    }
    if (self->pairs_submitted - self->pairs_returned > FF_MAX_INFLIGHT_PAIRS)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_ffmpeg: encoder stalled, %llu pairs in "
            "flight", (unsigned long long)(self->pairs_submitted -
                                           self->pairs_returned));
        self->metrics.timeouts++;
        return XRDP_FFMPEG_PAIR_ERROR;
    }
    return XRDP_FFMPEG_PAIR_PENDING;
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
    if (pk_available(self) < 2)
    {
        st = pump(self, now_ms() + self->cfg.pair_timeout_ms, 1);
        if (st == 1)
        {
            return XRDP_FFMPEG_PAIR_ERROR;
        }
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
    self = (struct xrdp_ffmpeg_avc444 *)g_malloc(sizeof(*self), 1);
    if (self == NULL)
    {
        return NULL;
    }
    self->cfg = *cfg;
    self->in_fd = -1;
    self->out_fd = -1;
    self->err_fd = -1;
    self->pid = -1;
    self->actual_width = actual_width;
    self->actual_height = actual_height;
    self->coded_width = round_up_16(actual_width);
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
    if (spawn_child(cfg, self->coded_width, self->coded_height,
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
    return self;
}

/*****************************************************************************/
int
xrdp_ffmpeg_avc444_coded_width(struct xrdp_ffmpeg_avc444 *self)
{
    return self != NULL ? self->coded_width : 0;
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
    g_free(self->inq);
    g_free(self->main_buf);
    g_free(self->aux_buf);
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
int
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
    long long deadline;
    int ok = 0;
    int fail = 0;
    int closed = 0;
    long long last_pts = -1;

    if (cfg == NULL || cfg->path[0] != '/' || coded_width < 16 ||
            coded_height < 16)
    {
        return 1;
    }
    nv12_size = coded_width * coded_height + coded_width * (coded_height / 2);
    total = 4 * nv12_size;
    blob = (unsigned char *)malloc(total);
    if (blob == NULL)
    {
        return 1;
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
        return 1;
    }
    if (spawn_child(cfg, coded_width, coded_height, &in_fd, &out_fd,
                    &err_fd, &pid) != 0)
    {
        xrdp_nut_delete(nut);
        free(blob);
        return 1;
    }
    deadline = now_ms() + (cfg->stream_ready_timeout_ms > 0 ?
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
            fail = 1;
            break;
        }
        if (in_slot >= 0 && (pfd[in_slot].revents & POLLOUT))
        {
            int w = (int)write(in_fd, blob + off, total - off);
            if (w > 0)
            {
                off += w;
            }
            else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                     errno != EINTR)
            {
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
        }
        while ((n = (int)read(out_fd, tmp, sizeof(tmp))) > 0)
        {
            if (xrdp_nut_feed(nut, (unsigned char *)tmp, n) != 0)
            {
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
                    fail = 1;
                    break;
                }
                if (pkt.pts <= last_pts && got > 0)
                {
                    fail = 1;
                    break;
                }
                last_pts = pkt.pts;
                if (got == 0)
                {
                    if (!pkt.keyframe ||
                            !xrdp_h264_main_reset_ok(pkt.data, pkt.len))
                    {
                        fail = 1;
                        break;
                    }
                }
                else if (!xrdp_h264_aux_ok(pkt.data, pkt.len))
                {
                    fail = 1;
                    break;
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
            fail = 1; /* stdout EOF before four packets */
            break;
        }
    }
    ok = (!fail && got == 4) ? 1 : 0;
    if (in_fd >= 0)
    {
        close(in_fd);
        in_fd = -1;
    }
    reap_child(pid, cfg->terminate_grace_ms);
    if (out_fd >= 0)
    {
        close(out_fd);
    }
    if (err_fd >= 0)
    {
        close(err_fd);
    }
    xrdp_nut_delete(nut);
    free(blob);
    return ok ? 0 : 1;
}
