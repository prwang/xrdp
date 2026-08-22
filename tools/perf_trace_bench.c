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
 * Source-cost smoke bench for the selected named-text perf tracer. The
 * fixed-object comparison used to choose this representation is retired;
 * its surviving raw runs and distributions are recorded under BACKLOG #107.
 *
 * Build (not part of make):
 *   gcc -O2 -I. -Icommon tools/perf_trace_bench.c \
 *       common/.libs/libcommon.a -lpthread -o /tmp/perf_trace_bench
 *
 * Run with XRDP_PERF_TRACE pointing at a private output prefix. The workload
 * is #61h's twelve real event families per frame, paced at 40 ms by default.
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "perf_trace.h"

#define DEFAULT_FRAMES 500
#define DEFAULT_PERIOD_MS 40
#define EVENTS_PER_FRAME 12

/*****************************************************************************/
static long long
now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/*****************************************************************************/
static int
compare_long_long(const void *left, const void *right)
{
    long long a;
    long long b;

    a = *(const long long *)left;
    b = *(const long long *)right;
    return a < b ? -1 : a > b;
}

/*****************************************************************************/
static void
emit_frame(int sequence)
{
    PERF_TRACE("event=dmg class=GFX_TRACE surface=%d num_rects=%d "
               "x1=%d y1=%d x2=%d y2=%d",
               1, 37, 0, 0, 3840, 2400);
    PERF_TRACE("event=batch class=GFX_TRACE cycle=%d set_n=%d "
               "monitors_armed=%d kids_armed=%d max_kids=%d rv=%d",
               sequence, 2, 2, 4, 4, 1);
    PERF_TRACE("event=submit class=ACK_TRACE id=%d monitor=%d",
               sequence, 0);
    PERF_TRACE("event=enc class=GFX_TRACE submitted_seq=%d "
               "returned_seq=%d ready=%d inflight=%d center_y=%d",
               sequence, sequence, 1, 0, 128);
    PERF_TRACE("event=enc class=GFX_TRACE submitted_seq=%d "
               "returned_seq=%d ready=%d inflight=%d center_y=%d",
               sequence, sequence, 1, 0, 128);
    PERF_TRACE("event=absorb class=ACK_TRACE id=%d monitor=%d",
               sequence, 0);
    PERF_TRACE("event=msgin class=ACK_TRACE id=%d bytes=%d",
               sequence, 13824000);
    PERF_TRACE("event=ack class=GFX_TRACE frame_id=%d queue_depth=%d "
               "decoded=%d id_server=%d ack_off=%d",
               sequence, 0, sequence, sequence, 0);
    PERF_TRACE("event=ack class=ACK_TRACE id=%d kind=region egress=%d "
               "absorbed=%d client=%d window=%d",
               sequence, sequence, sequence, sequence, 1);
    PERF_TRACE("event=ack class=ACK_TRACE id=%d kind=slot egress=%d "
               "absorbed=%d client=%d window=%d",
               sequence, sequence, sequence, sequence, 1);
    PERF_TRACE("event=send class=GFX_TRACE bytes=%d last=%d frame_id=%d "
               "id_server=%d id_client=%d fif=%d",
               1930000, 0, sequence, sequence, sequence - 1, 2);
    PERF_TRACE("event=egress class=ACK_TRACE id=%d shown=%d "
               "pending_kib=%d client=%d",
               sequence, 1, 1885, sequence - 1);
}

/*****************************************************************************/
static double
percentile(const long long *samples, int count, double fraction)
{
    int index;

    index = (int)(fraction * count);
    if (index >= count)
    {
        index = count - 1;
    }
    return samples[index] / 1000.0;
}

/*****************************************************************************/
int
main(int argc, char **argv)
{
    struct timespec target;
    long long *samples;
    long long start;
    long long end;
    double sum;
    int frames;
    int period_ms;
    int index;

    frames = argc > 1 ? atoi(argv[1]) : DEFAULT_FRAMES;
    period_ms = argc > 2 ? atoi(argv[2]) : DEFAULT_PERIOD_MS;
    if (frames < 1 || period_ms < 1 ||
            perf_trace_init() != PERF_TRACE_INIT_ARMED)
    {
        fprintf(stderr, "usage: XRDP_PERF_TRACE=/path/prefix %s "
                "[frames] [period_ms]\n", argv[0]);
        return 1;
    }
    samples = (long long *)calloc((size_t)frames, sizeof(*samples));
    if (samples == NULL)
    {
        perf_trace_close();
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &target);
    for (index = 0; index < frames; index++)
    {
        target.tv_nsec += period_ms * 1000000L;
        while (target.tv_nsec >= 1000000000L)
        {
            target.tv_sec++;
            target.tv_nsec -= 1000000000L;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL);
        start = now_ns();
        emit_frame(index);
        end = now_ns();
        samples[index] = end - start;
    }
    perf_trace_close();
    sum = 0.0;
    for (index = 0; index < frames; index++)
    {
        sum += samples[index];
    }
    qsort(samples, (size_t)frames, sizeof(*samples), compare_long_long);
    printf("named text source, %d events/frame: mean %.3f us p50 %.3f us "
           "p90 %.3f us p99 %.3f us max %.3f us\n",
           EVENTS_PER_FRAME, sum / frames / 1000.0,
           percentile(samples, frames, 0.50),
           percentile(samples, frames, 0.90),
           percentile(samples, frames, 0.99),
           samples[frames - 1] / 1000.0);
    free(samples);
    return 0;
}
