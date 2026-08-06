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
 * Dedicated performance trace sink -- SOURCE and SINK on different
 * threads, joined by a per-thread ring. PRD FR-TRACE-1.
 *
 * This is deliberately NOT the logger. LOG() formats a timestamp, takes
 * a global mutex and performs an UNBUFFERED write() syscall per line
 * (log.c internal_log_message), and it lands in /var/log/xrdp and
 * journald mixed with every other message. Stage timings are a
 * different kind of data: high rate, machine-read, and useless to an
 * administrator reading a log.
 *
 * WHY THE SHAPE IS WHAT IT IS (BACKLOG #61e, 2026-08-01). The first
 * version was an fprintf onto ONE shared FILE*. fprintf takes
 * flockfile, so every event serialized against every other event in
 * whatever thread issued it, and the wait landed inside whichever stage
 * bracket happened to be open. While only ONE thread wrote to it, this
 * was invisible. Adding a single event per frame on a SECOND thread --
 * ~140 events/second in total -- moved the measured frame period from
 * 40.4 ms to 135.3 ms. The instrument was the bug, and it produced a
 * plausible-looking result while being it. Hence:
 *
 *   SOURCE (perf_trace_ev, any thread)   no syscall, no I/O, no
 *                                        allocation, no shared lock;
 *                                        appends to ITS OWN ring and
 *                                        returns.
 *   SINK   (one dedicated thread)        drains every ring, formats,
 *                                        writes. Allowed to be slow:
 *                                        nothing measured waits on it.
 *
 * Overflow DROPS and COUNTS -- it never blocks, never spins, never
 * grows the ring in the hot path. The drop count is emitted into the
 * trace as a "perfdrop" record so an analysis can tell a complete
 * trace from a truncated one; a silently truncated trace is worse than
 * no trace.
 *
 * PRECONDITION ON tag: it is stored as a POINTER and formatted later,
 * on the sink thread. Every caller must pass a string LITERAL (or other
 * storage that outlives the process). This is what keeps the source
 * path free of any copying or formatting.
 *
 * Armed by the environment variable XRDP_PERF_TRACE, whose value is a
 * path PREFIX; each process appends its own pid, so several xrdp
 * processes never interleave into one file. Unset -- the shipped
 * default -- costs one predictable branch on a cached flag.
 *
 * Record format, one line per event, all fields space separated:
 *
 *   <monotonic_ns> <tid> <tag> <a> <b> <c> <d> <e> <f>
 *
 * SIX payload fields, not two (BACKLOG #61h, 2026-08-01). Two was
 * enough while the ring carried only stage brackets, but the per-frame
 * records it now has to carry -- which used to be LOG() lines on the
 * hot path -- have up to six: a GFX send is
 * (bytes, last, frame_id, id_server, id_client, fif) and a damage
 * record is (surface, num_rects, x0, y0, x1, y1). Splitting one record
 * across two events would have to be re-joined by the reader, and
 * joining by anything other than an echoed identity is the exact
 * mistake the 2c quality gate exists to stop.
 *
 * CLOCK_MONOTONIC is system-wide, so records from different processes
 * share one timeline and can be intersected. Frames must still be
 * joined by their ECHOED IDENTITY (put it in <a>), never by a time
 * window -- see BACKLOG #64 and the 2c quality gate.
 */

#ifndef _PERF_TRACE_H
#define _PERF_TRACE_H

/* Slots per producer ring. Power of two: the index wrap is a mask, so
 * the source path has no division and no branch on wrap. One slot is
 * always left empty to distinguish full from empty without a separate
 * count that both threads would have to write, so the usable capacity
 * is PERF_TRACE_RING_SLOTS - 1. */
#define PERF_TRACE_RING_SLOTS 8192
#define PERF_TRACE_RING_MASK  (PERF_TRACE_RING_SLOTS - 1)

/* Producer threads a process may trace. The encoder worker, the EGFX
 * assembler and the main thread are three; the rest is headroom, and a
 * thread that finds the pool exhausted drops and counts rather than
 * blocking or allocating. */
#define PERF_TRACE_MAX_RINGS 8

struct perf_trace_rec
{
    long long ns;
    long long tid;
    const char *tag;  /* NOT owned -- must be a literal, see above */
    int a;
    int b;
    int c;
    int d;
    int e;
    int f;
};

/* Single producer, single consumer. head is written ONLY by the
 * producer that owns this ring; tail ONLY by the sink thread. Neither
 * needs a lock because neither writes the other's index. */
struct perf_trace_ring
{
    struct perf_trace_rec slots[PERF_TRACE_RING_SLOTS];
    unsigned int head;
    unsigned int tail;
    unsigned int dropped;      /* producer-only */
    unsigned int drop_seen;    /* sink-only: last count it reported */
    int claimed;
};

/**
 * Is the sink armed? Cheap after the first call (a cached flag); safe
 * to call from any thread. Opens the sink on first use.
 */
int
perf_trace_on(void);

/**
 * Record one event. Callers should use the PERF_TRACE macro so the
 * argument expressions are not evaluated when the sink is disarmed.
 * tag must be a short, space-free identifier AND must be a string
 * literal -- it is stored by pointer and formatted on the sink thread.
 */
void
perf_trace_ev(const char *tag, int a, int b);

/**
 * As perf_trace_ev(), for a record with more than two payload fields.
 * perf_trace_ev(tag, a, b) is exactly perf_trace_ev6(tag, a, b, 0,0,0,0).
 */
void
perf_trace_ev6(const char *tag, int a, int b, int c, int d, int e, int f);

/**
 * The ring's push and pop, exposed ONLY so the SPSC behaviour can be
 * unit tested without a filesystem or a second thread.
 *
 * perf_trace_ring_push() returns 1 when the record was stored, and 0
 * when the ring was full -- in which case it increments ->dropped and
 * stores nothing. perf_trace_ring_pop() returns 1 and fills *out when a
 * record was available, 0 when the ring was empty.
 */
int
perf_trace_ring_push(struct perf_trace_ring *r, long long ns, long long tid,
                     const char *tag, int a, int b, int c, int d, int e,
                     int f);
int
perf_trace_ring_pop(struct perf_trace_ring *r, struct perf_trace_rec *out);

/**
 * Flush and close the sink. Safe to call when never armed.
 */
void
perf_trace_close(void);

/**
 * Build the record text for one event into buf. Exposed only so the
 * unit tests can assert the schema without a filesystem; returns the
 * number of characters that would be written (snprintf semantics).
 */
int
perf_trace_format(char *buf, int len, long long ns, long long tid,
                  const char *tag, int a, int b, int c, int d, int e, int f);

#define PERF_TRACE(tag, a, b)             \
    do                                    \
    {                                     \
        if (perf_trace_on())              \
        {                                 \
            perf_trace_ev((tag), (a), (b)); \
        }                                 \
    }                                     \
    while (0)

#define PERF_TRACE6(tag, a, b, c, d, e, f)                     \
    do                                                         \
    {                                                          \
        if (perf_trace_on())                                   \
        {                                                      \
            perf_trace_ev6((tag), (a), (b), (c), (d), (e), (f)); \
        }                                                      \
    }                                                          \
    while (0)

#endif
