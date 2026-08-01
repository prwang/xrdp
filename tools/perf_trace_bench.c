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
 * perf_trace_bench -- what does the perf ring cost the thread that
 * writes into it?
 *
 * BACKLOG #61h left one thing unproven. The old per-frame trace was
 * shown to be inside its own measurement (log.c: global mutex,
 * unbuffered write(), ~12 per frame). The replacement -- a per-thread
 * SPSC ring drained by a separate sink thread -- was ARGUED to be off
 * the measured path, and that argument was never measured. An instrument
 * that is merely believed to be transparent is the exact thing #61h is
 * about.
 *
 * The question is a number, not a bit: HOW MANY MILLISECONDS does an
 * armed ring add to one frame's work on a producer thread? A frame
 * period is tens of milliseconds, so the answer only matters against
 * that scale.
 *
 * WHY THIS AND NOT A FLEET A/B. Two 60 s gate runs on two arms is ~20
 * minutes and answers the question with a session, a client, an encoder
 * and a network in the sum -- every one of which has more run-to-run
 * spread (x006 drifted 36.8 -> 41.8 ms in one day, same image) than the
 * effect being looked for. This links the SHIPPED common/perf_trace.c
 * -- not a copy of it, the file itself -- and isolates the one variable.
 *
 * NO LOGGING ARM. The obvious contrast is "ring vs the log.c lines it
 * replaced", and it is deliberately absent: CLAUDE.md rule 5 forbids
 * per-frame LOG(), and a bench is not an exemption from a rule about
 * what may exist in this tree. The sensitivity of the bracket is
 * established instead by the `cal` arm below, which needs no forbidden
 * pattern.
 *
 * THREE ARMS, each its own process (the sink is opened once per process
 * through pthread_once, so arming cannot be toggled inside one run):
 *
 *   off    XRDP_PERF_TRACE unset -- the shipped default. Twelve
 *          PERF_TRACE6 macros that each test a cached flag and return.
 *          This is the baseline the server actually runs.
 *   ring   XRDP_PERF_TRACE set -- twelve records really written: a
 *          vDSO clock read and a store into this thread's own ring,
 *          with the sink thread draining and writing to a file.
 *   cal    off, plus a calibrated busy-spin of a known duration per
 *          frame. This arm exists to answer "what would this bench
 *          show if the cost WERE real?" -- without it, a null result
 *          from `ring` is indistinguishable from a blind bracket.
 *
 * WHAT IS MEASURED, per arm:
 *
 *   1. Added latency per frame. One clock pair around the whole batch
 *      of twelve records -- not around each one, because a per-call
 *      clock read costs more than the call it would be timing. The
 *      bracket therefore reports exactly the quantity the question is
 *      asked in: milliseconds added to one frame.
 *   2. The DISTRIBUTION of that, not just its mean. A mean is the wrong
 *      summary when the shape is unknown, and a tracer's tail (the sink
 *      waking on the same core, a cache miss on ring wrap) is the part
 *      that could matter.
 *   3. Total process CPU, user + sys, over the run. The bracket cannot
 *      see the sink thread -- that is the whole point of the sink -- so
 *      the sink's cost is caught here instead. `ring` minus `off` is
 *      the tracer's ENTIRE CPU footprint, both threads.
 *
 * The batches are PACED at a frame period rather than run flat out. A
 * tight loop keeps the ring head, the clock page and the code all hot in
 * L1 and would understate the real cost; pacing at least lets the
 * caches age between batches the way they do between frames. It does not
 * make this a server: an idle bench thread still has a warmer cache than
 * xrdp's main thread, so the figures below are a FLOOR on the cost. Say
 * so when quoting them -- the conclusion has to survive that.
 *
 * Build (not part of `make`, same as the other benches here):
 *   gcc -O2 -I. -Icommon tools/perf_trace_bench.c \
 *       common/.libs/libcommon.a -lpthread -o /tmp/perf_trace_bench
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

#include "perf_trace.h"

/* Records per frame, and the tags themselves. This is the real mix the
 * xrdp encode/emit path writes -- every tag in
 * PR-demo/mac_bisect_matrix/perf_trace_lines.py, with `enc` twice,
 * because a two-monitor cycle emits it once per child. Twelve is the
 * count #61h found on the hot path. */
#define RECS_PER_FRAME 12

static const char *g_tags[RECS_PER_FRAME] =
{
    "dmg", "batch", "submit", "enc", "enc", "absorb",
    "msgin", "cliack", "ackregion", "ackslot", "send", "egress"
};

/* Default shape of a run: 500 frames at 40 ms is 20 s, which is long
 * enough for a tail to appear and short enough that three arms fit in a
 * minute. Both are overridable so a smoke run costs seconds. */
#define DEF_FRAMES 500
#define DEF_PERIOD_MS 40

/* What the `cal` arm burns per frame. 200 us is deliberately far below
 * anything that would matter against a 25-40 ms frame, and far above
 * what the ring is expected to cost: if the bracket resolves this, it
 * resolves anything worth worrying about. */
#define CAL_SPIN_US 200

/*****************************************************************************/
static long long
now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

/*****************************************************************************/
static int
cmp_ll(const void *a, const void *b)
{
    long long x = *(const long long *)a;
    long long y = *(const long long *)b;

    if (x < y)
    {
        return -1;
    }
    return x > y;
}

/*****************************************************************************/
/* Busy-spin, not sleep: the point is to occupy the bracket the way real
 * work would, so the bracket's ability to see occupied time is what
 * gets tested. */
static void
spin_us(long long us)
{
    long long deadline = now_ns() + us * 1000LL;

    while (now_ns() < deadline)
    {
        ;
    }
}

/*****************************************************************************/
/* One frame's worth of records. Kept in its own function so all three
 * arms run identical surrounding code and differ only in what this
 * does. */
static void
emit_frame(int arm_ring, int seq)
{
    int i;

    if (!arm_ring)
    {
        return;
    }
    for (i = 0; i < RECS_PER_FRAME; i++)
    {
        PERF_TRACE6(g_tags[i], seq, i, 1920, 1080, seq & 0xff, 2);
    }
}

/*****************************************************************************/
static void
stats(const char *what, long long *d, int n)
{
    double sum = 0.0;
    int i;

    if (n < 1)
    {
        printf("  %-22s (none)\n", what);
        return;
    }
    for (i = 0; i < n; i++)
    {
        sum += (double)d[i];
    }
    qsort(d, (size_t)n, sizeof(d[0]), cmp_ll);
    printf("  %-22s n=%-4d mean %8.3f  p50 %8.3f  p90 %8.3f"
           "  p99 %8.3f  max %8.3f\n",
           what, n, sum / n / 1000.0, d[n / 2] / 1000.0,
           d[(int)(n * 0.90)] / 1000.0, d[(int)(n * 0.99)] / 1000.0,
           d[n - 1] / 1000.0);
}

/*****************************************************************************/
/* Split the frames by whether the kernel charged this thread a minor
 * fault during the batch, and report the two groups separately.
 *
 * This is here because the first run of this bench came back with a
 * plainly bimodal batch cost (p50 3.2 us, p90 25 us) and a mean that
 * described neither mode. A mean over two mechanisms is the thing the
 * #61h post-mortem specifically warns about, so the shape gets
 * attributed rather than averaged: a fault is a mechanism the ring can
 * be FIXED to stop causing, and the fault-free group is what the ring
 * costs once it has stopped. */
static void
report(const char *arm, long long *d, long long *flt, int n,
       double cpu_ms, double wall_ms)
{
    long long *cold;
    long long *warm;
    int nc = 0;
    int nw = 0;
    int i;

    cold = (long long *)calloc((size_t)n, sizeof(long long));
    warm = (long long *)calloc((size_t)n, sizeof(long long));
    if (cold == NULL || warm == NULL)
    {
        free(cold);
        free(warm);
        return;
    }
    for (i = 0; i < n; i++)
    {
        if (flt[i] > 0)
        {
            cold[nc++] = d[i];
        }
        else
        {
            warm[nw++] = d[i];
        }
    }
    printf("arm=%-4s frames=%d   per-frame batch cost, microseconds\n",
           arm, n);
    stats("all frames", d, n);
    stats("frames w/ minor fault", cold, nc);
    stats("frames w/o fault", warm, nw);
    printf("  process CPU (user+sys) %.1f ms over %.1f ms wall"
           "  = %.4f ms/frame\n", cpu_ms, wall_ms, cpu_ms / n);

    /* Faults by quarter of the run. First-touch of a lazily-allocated
     * ring is a startup TRANSIENT -- it must decay to nothing once every
     * page is resident. A flat profile would mean something else is
     * faulting every frame, which would be a real per-frame cost and a
     * different conclusion entirely. Cheaper to print than to argue. */
    printf("  faulting frames by quarter of run:");
    for (i = 0; i < 4; i++)
    {
        int lo = n * i / 4;
        int hi = n * (i + 1) / 4;
        int k;
        int c = 0;

        for (k = lo; k < hi; k++)
        {
            if (flt[k] > 0)
            {
                c++;
            }
        }
        printf("  Q%d %d/%d", i + 1, c, hi - lo);
    }
    printf("\n");
    free(cold);
    free(warm);
}

/*****************************************************************************/
int
main(int argc, char **argv)
{
    const char *arm;
    int frames = DEF_FRAMES;
    int period_ms = DEF_PERIOD_MS;
    int arm_ring;
    int arm_cal;
    int i;
    long long *d;
    long long *flt;
    long long prev_flt;
    struct rusage rf;
    long long t0;
    long long t1;
    long long wall0;
    long long next;
    struct timespec ts;
    struct rusage ru;
    double cpu_ms;

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s off|ring|cal [frames] [period_ms]\n",
                argv[0]);
        return 2;
    }
    arm = argv[1];
    if (argc > 2)
    {
        frames = atoi(argv[2]);
    }
    if (argc > 3)
    {
        period_ms = atoi(argv[3]);
    }
    if (frames < 10)
    {
        fprintf(stderr, "frames must be >= 10\n");
        return 2;
    }
    arm_ring = (strcmp(arm, "ring") == 0);
    arm_cal = (strcmp(arm, "cal") == 0);
    if (!arm_ring && !arm_cal && strcmp(arm, "off") != 0)
    {
        fprintf(stderr, "unknown arm '%s'\n", arm);
        return 2;
    }

    /* MECHANISM CHECK, before any number is produced. An arm that
     * silently failed to arm -- or one that armed when it should not
     * have -- would produce a clean-looking null result, which is the
     * failure mode this whole exercise exists to avoid. */
    if (arm_ring && !perf_trace_on())
    {
        fprintf(stderr, "arm=ring but the sink did not arm: is "
                "XRDP_PERF_TRACE set to a writable path prefix?\n");
        return 1;
    }
    if (!arm_ring && perf_trace_on())
    {
        fprintf(stderr, "arm=%s but the sink IS armed: unset "
                "XRDP_PERF_TRACE for this arm\n", arm);
        return 1;
    }

    d = (long long *)calloc((size_t)frames, sizeof(long long));
    flt = (long long *)calloc((size_t)frames, sizeof(long long));
    if (d == NULL || flt == NULL)
    {
        return 1;
    }

    /* Warm the first-touch page faults and this thread's ring claim out
     * of the measurement -- they happen once per process in the server
     * too, and a one-off would otherwise land entirely in frame 0. */
    for (i = 0; i < 20; i++)
    {
        emit_frame(arm_ring, -1);
    }

    getrusage(RUSAGE_SELF, &rf);
    prev_flt = rf.ru_minflt;
    wall0 = now_ns();
    next = wall0;
    for (i = 0; i < frames; i++)
    {
        t0 = now_ns();
        emit_frame(arm_ring, i);
        if (arm_cal)
        {
            spin_us(CAL_SPIN_US);
        }
        t1 = now_ns();
        d[i] = t1 - t0;

        /* Outside the bracket, so this syscall is not in the number it
         * is explaining. */
        getrusage(RUSAGE_SELF, &rf);
        flt[i] = rf.ru_minflt - prev_flt;
        prev_flt = rf.ru_minflt;

        /* Absolute deadline, so a slow frame does not push the whole
         * schedule out and turn the pacing into a drift. */
        next += (long long)period_ms * 1000000LL;
        ts.tv_sec = (time_t)(next / 1000000000LL);
        ts.tv_nsec = (long)(next % 1000000000LL);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
    }
    t1 = now_ns();

    getrusage(RUSAGE_SELF, &ru);
    cpu_ms = ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0
             + ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;

    /* Close the sink INSIDE the measured process and before reporting,
     * so its final drain and flush are inside the CPU figure. A sink
     * whose cost is paid after the accounting stops is not accounted
     * for. */
    perf_trace_close();
    getrusage(RUSAGE_SELF, &ru);
    cpu_ms = ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0
             + ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;

    report(arm, d, flt, frames, cpu_ms, (t1 - wall0) / 1000000.0);
    printf("  expected records in ring file: %d\n",
           arm_ring ? frames * RECS_PER_FRAME + 20 * RECS_PER_FRAME : 0);
    free(d);
    free(flt);
    return 0;
}
