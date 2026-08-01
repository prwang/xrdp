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
 * Dedicated performance trace sink -- see perf_trace.h for why the
 * source and the sink are on different threads (PRD FR-TRACE-1).
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "perf_trace.h"

/* The SINK's own stdio buffer. Only the sink thread ever touches the
 * FILE*, so this buffering is invisible to every measured thread. */
#define PERF_TRACE_BUF_SIZE (1024 * 1024)

/* How long the sink sleeps between drain passes. A ring holds
 * PERF_TRACE_RING_SLOTS - 1 = 8191 records; at the rate this is used
 * for (single digits of events per frame, tens of frames per second) a
 * 10 ms pass drains a handful of records and the ring is never close to
 * full. It is deliberately NOT event-driven: a condition variable would
 * put a shared lock back on the source path, which is the entire defect
 * this design exists to remove. */
#define PERF_TRACE_DRAIN_MS 10

static FILE *g_perf_file = NULL;
static char *g_perf_buf = NULL;
static int g_perf_armed = 0;
static pthread_once_t g_perf_once = PTHREAD_ONCE_INIT;

/* The ring pool. Allocated ONCE when the sink is armed -- never on the
 * source path, so perf_trace_ev() cannot allocate. */
static struct perf_trace_ring *g_perf_rings = NULL;
static int g_perf_ring_count = 0;
static pthread_t g_perf_sink;
static int g_perf_sink_live = 0;
static volatile int g_perf_quit = 0;

/* Threads that wanted a ring and found the pool exhausted. Written by
 * any thread, read by the sink; it is a diagnostic counter, so a lost
 * update is acceptable where a lock on the source path is not. */
static volatile unsigned int g_perf_no_ring = 0;

/* This thread's ring, resolved once per thread. NULL means "not yet
 * looked up"; the claim below sets it to a ring or leaves it NULL and
 * sets g_perf_no_ring. */
static __thread struct perf_trace_ring *g_perf_my_ring = NULL;
static __thread int g_perf_my_ring_done = 0;

/*****************************************************************************/
static long long
perf_trace_now_ns(void)
{
    struct timespec ts;

    /* vDSO on Linux: a memory read and arithmetic, not a syscall. This
     * is the ONE thing FR-TRACE-1 clause 1 permits on the source path. */
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

/*****************************************************************************/
/* SPSC push. Called ONLY by the thread that owns this ring, so head is
 * read and written by this thread alone and needs no lock. Returns 1 if
 * the record was stored, 0 if the ring was full. */
int
perf_trace_ring_push(struct perf_trace_ring *r, long long ns, long long tid,
                     const char *tag, int a, int b, int c, int d, int e,
                     int f)
{
    unsigned int head;
    unsigned int next;

    if (r == NULL)
    {
        return 0;
    }
    head = r->head;
    next = (head + 1) & PERF_TRACE_RING_MASK;
    /* One slot is left empty so that full and empty are distinguishable
     * without a shared count. */
    if (next == r->tail)
    {
        r->dropped++;
        return 0;
    }
    r->slots[head].ns = ns;
    r->slots[head].tid = tid;
    r->slots[head].tag = tag;
    r->slots[head].a = a;
    r->slots[head].b = b;
    r->slots[head].c = c;
    r->slots[head].d = d;
    r->slots[head].e = e;
    r->slots[head].f = f;
    /* The slot must be visible to the sink BEFORE the index that
     * publishes it, or the sink can read a half-written record. */
    __sync_synchronize();
    r->head = next;
    return 1;
}

/*****************************************************************************/
/* SPSC pop. Called ONLY by the sink thread. Returns 1 and fills *out if
 * a record was available, 0 if the ring was empty. */
int
perf_trace_ring_pop(struct perf_trace_ring *r, struct perf_trace_rec *out)
{
    unsigned int tail;

    if (r == NULL || out == NULL)
    {
        return 0;
    }
    tail = r->tail;
    if (tail == r->head)
    {
        return 0;
    }
    /* Read the published index before the slot it publishes. */
    __sync_synchronize();
    *out = r->slots[tail];
    __sync_synchronize();
    r->tail = (tail + 1) & PERF_TRACE_RING_MASK;
    return 1;
}

/*****************************************************************************/
int
perf_trace_format(char *buf, int len, long long ns, long long tid,
                  const char *tag, int a, int b, int c, int d, int e, int f)
{
    if (buf == NULL || len < 1 || tag == NULL)
    {
        return -1;
    }
    return snprintf(buf, (size_t)len, "%lld %lld %s %d %d %d %d %d %d\n",
                    ns, tid, tag, a, b, c, d, e, f);
}

/*****************************************************************************/
/* One drain pass over every ring. Sink thread only. */
static void
perf_trace_drain(void)
{
    struct perf_trace_rec rec;
    char line[256];
    int index;
    unsigned int dropped;

    for (index = 0; index < g_perf_ring_count; index++)
    {
        struct perf_trace_ring *r = &g_perf_rings[index];

        while (perf_trace_ring_pop(r, &rec))
        {
            /* through perf_trace_format(), so the schema has ONE
               definition and the unit test that pins it pins what the
               sink actually writes */
            if (perf_trace_format(line, (int)sizeof(line), rec.ns, rec.tid,
                                  rec.tag, rec.a, rec.b, rec.c, rec.d,
                                  rec.e, rec.f) > 0)
            {
                fputs(line, g_perf_file);
            }
        }
        /* A truncated trace must announce itself. */
        dropped = r->dropped;
        if (dropped != r->drop_seen)
        {
            if (perf_trace_format(line, (int)sizeof(line),
                                  perf_trace_now_ns(), index, "perfdrop",
                                  (int)dropped, index, 0, 0, 0, 0) > 0)
            {
                fputs(line, g_perf_file);
            }
            r->drop_seen = dropped;
        }
    }
}

/*****************************************************************************/
static void *
perf_trace_sink_thread(void *arg)
{
    struct timespec ts;

    (void)arg;
    ts.tv_sec = 0;
    ts.tv_nsec = PERF_TRACE_DRAIN_MS * 1000000L;
    while (!g_perf_quit)
    {
        perf_trace_drain();
        nanosleep(&ts, NULL);
    }
    /* Final pass: whatever the producers wrote before they stopped. */
    perf_trace_drain();
    if (g_perf_no_ring != 0)
    {
        {
            char line[256];
            if (perf_trace_format(line, (int)sizeof(line),
                                  perf_trace_now_ns(), 0, "perfnoring",
                                  (int)g_perf_no_ring, 0, 0, 0, 0, 0) > 0)
            {
                fputs(line, g_perf_file);
            }
        }
    }
    fflush(g_perf_file);
    return NULL;
}

/*****************************************************************************/
/* Runs exactly once, from whichever thread calls perf_trace_on() first. */
static void
perf_trace_open(void)
{
    const char *prefix;
    char path[512];

    prefix = getenv("XRDP_PERF_TRACE");
    if (prefix == NULL || prefix[0] == '\0')
    {
        return;
    }
    /* the pid keeps sibling xrdp processes out of each other's file */
    if (snprintf(path, sizeof(path), "%s.%d", prefix, (int)getpid())
            >= (int)sizeof(path))
    {
        return;
    }
    /* "e" is O_CLOEXEC: a forked child must not inherit the sink and
     * append its own records to a file another process is buffering */
    g_perf_file = fopen(path, "we");
    if (g_perf_file == NULL)
    {
        return;
    }
    g_perf_buf = (char *)malloc(PERF_TRACE_BUF_SIZE);
    if (g_perf_buf != NULL)
    {
        setvbuf(g_perf_file, g_perf_buf, _IOFBF, PERF_TRACE_BUF_SIZE);
    }
    /* The whole pool, up front. After this point the source path never
     * allocates -- it only claims an already-built ring. */
    g_perf_rings = (struct perf_trace_ring *)
                   calloc(PERF_TRACE_MAX_RINGS,
                          sizeof(struct perf_trace_ring));
    if (g_perf_rings == NULL)
    {
        fclose(g_perf_file);
        g_perf_file = NULL;
        free(g_perf_buf);
        g_perf_buf = NULL;
        return;
    }
    g_perf_ring_count = PERF_TRACE_MAX_RINGS;
    /* The clock base, once, as the first line. Records carry
     * CLOCK_MONOTONIC because that is what can be intersected across
     * processes; a reader that has to place them beside a wall-clock
     * artefact (an xrdp.log line, a screenshot) needs the pair, and
     * fitting one from the other is exactly the kind of guess this
     * project has been bitten by. Comment-prefixed so a record parser
     * skips it. */
    {
        struct timespec mono;
        struct timespec real;
        if (clock_gettime(CLOCK_MONOTONIC, &mono) == 0 &&
                clock_gettime(CLOCK_REALTIME, &real) == 0)
        {
            fprintf(g_perf_file, "# perfbase mono_ns %lld real_ns %lld\n",
                    (long long)mono.tv_sec * 1000000000LL + mono.tv_nsec,
                    (long long)real.tv_sec * 1000000000LL + real.tv_nsec);
        }
    }
    if (pthread_create(&g_perf_sink, NULL, perf_trace_sink_thread,
                       NULL) != 0)
    {
        /* No sink thread means no way to drain, and a source that fills
         * its ring and drops everything is worse than a disarmed one. */
        free(g_perf_rings);
        g_perf_rings = NULL;
        g_perf_ring_count = 0;
        fclose(g_perf_file);
        g_perf_file = NULL;
        free(g_perf_buf);
        g_perf_buf = NULL;
        return;
    }
    g_perf_sink_live = 1;
    g_perf_armed = 1;
}

/*****************************************************************************/
int
perf_trace_on(void)
{
    pthread_once(&g_perf_once, perf_trace_open);
    return g_perf_armed;
}

/*****************************************************************************/
/* Claim this thread's ring. Runs at most once per thread; the claim is
 * an atomic test-and-set on an already-allocated slot, so it neither
 * allocates nor takes a lock. */
static struct perf_trace_ring *
perf_trace_my_ring(void)
{
    int index;

    if (g_perf_my_ring_done)
    {
        return g_perf_my_ring;
    }
    g_perf_my_ring_done = 1;
    for (index = 0; index < g_perf_ring_count; index++)
    {
        if (__sync_bool_compare_and_swap(&g_perf_rings[index].claimed, 0, 1))
        {
            g_perf_my_ring = &g_perf_rings[index];
            return g_perf_my_ring;
        }
    }
    /* Pool exhausted: this thread drops, loudly in the trace, rather
     * than blocking or allocating on the measured path. */
    __sync_fetch_and_add(&g_perf_no_ring, 1);
    return NULL;
}

/*****************************************************************************/
void
perf_trace_ev6(const char *tag, int a, int b, int c, int d, int e, int f)
{
    struct perf_trace_ring *r;

    if (!g_perf_armed || tag == NULL)
    {
        return;
    }
    r = perf_trace_my_ring();
    if (r == NULL)
    {
        return;
    }
    /* No syscall, no I/O, no allocation, no shared lock -- a clock read
     * through the vDSO and a store into this thread's own ring. */
    perf_trace_ring_push(r, perf_trace_now_ns(), (long long)pthread_self(),
                         tag, a, b, c, d, e, f);
}

/*****************************************************************************/
void
perf_trace_ev(const char *tag, int a, int b)
{
    perf_trace_ev6(tag, a, b, 0, 0, 0, 0);
}

/*****************************************************************************/
void
perf_trace_close(void)
{
    if (g_perf_sink_live)
    {
        /* Stop producing before draining, so the final pass sees a
         * quiescent set of rings. */
        g_perf_armed = 0;
        g_perf_quit = 1;
        pthread_join(g_perf_sink, NULL);
        g_perf_sink_live = 0;
    }
    g_perf_armed = 0;
    if (g_perf_file != NULL)
    {
        fflush(g_perf_file);
        fclose(g_perf_file);
        g_perf_file = NULL;
    }
    if (g_perf_rings != NULL)
    {
        free(g_perf_rings);
        g_perf_rings = NULL;
        g_perf_ring_count = 0;
    }
    if (g_perf_buf != NULL)
    {
        free(g_perf_buf);
        g_perf_buf = NULL;
    }
}
