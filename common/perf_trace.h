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
 * Dedicated performance trace sink. A measured thread formats one bounded,
 * named text record into its own SPSC byte ring. A separate sink thread drains
 * the rings and performs all file I/O. See PRD FR-TRACE-1 and BACKLOG #107.
 *
 * Records use a restricted key/value grammar. Keys and static token values
 * are supplied by a literal, non-positional format string; dynamic values are
 * integers. The common fields are added here:
 *
 *   schema=1 mono_ns=42 pid=7 tid=9 event=send bytes=1930000 last=0
 *
 * Unknown keys and events are additive. Every record is complete and newline
 * terminated. A source which cannot reserve the maximum record size drops the
 * whole event and counts it; it never publishes a partial line.
 *
 * The Linux ring backing is mapped into two adjacent virtual ranges. A record
 * which crosses the physical end is consequently one contiguous snprintf()
 * destination and one contiguous sink span, with no wrap copy.
 */

#ifndef _PERF_TRACE_H
#define _PERF_TRACE_H

#include <stddef.h>

#define PERF_TRACE_RING_BYTES (512 * 1024)
#define PERF_TRACE_RECORD_BYTES 512
#define PERF_TRACE_MAX_RINGS 8

#if defined(__GNUC__)
#define PERF_TRACE_PRINTF(format_arg, first_arg) \
    __attribute__((__format__(__printf__, format_arg, first_arg)))
#else
#define PERF_TRACE_PRINTF(format_arg, first_arg)
#endif

struct perf_trace_ring;

/**
 * Is the sink armed? Cheap after the first call (a cached flag); safe to call
 * from any thread. Opens the sink on first use.
 */
int
perf_trace_on(void);

/**
 * Record one event. Callers use PERF_TRACE() so argument expressions are not
 * evaluated while disarmed. format must be a literal, non-positional format
 * containing static key names and integer conversions only. It must start
 * with event=<static_token>; a newline is added by the tracer.
 */
void
perf_trace_ev(const char *format, ...) PERF_TRACE_PRINTF(1, 2);

/**
 * Flush and close the sink. Safe to call when never armed.
 */
void
perf_trace_close(void);

/**
 * Byte-ring primitives exposed so the double mapping, SPSC publication,
 * complete-record drop rule and text schema can be tested without a sink
 * thread or filesystem. perf_trace_ring_peek() returns the whole currently
 * readable contiguous span; consume advances over bytes already written.
 */
struct perf_trace_ring *
perf_trace_ring_create(void);

void
perf_trace_ring_delete(struct perf_trace_ring *ring);

int
perf_trace_ring_write(struct perf_trace_ring *ring, long long ns,
                      long long pid, long long tid, const char *format, ...)
PERF_TRACE_PRINTF(5, 6);

int
perf_trace_ring_peek(struct perf_trace_ring *ring, const char **data,
                     size_t *length);

void
perf_trace_ring_consume(struct perf_trace_ring *ring, size_t length);

unsigned int
perf_trace_ring_dropped(const struct perf_trace_ring *ring);

unsigned int
perf_trace_ring_format_failed(const struct perf_trace_ring *ring);

#define PERF_TRACE(...)              \
    do                               \
    {                                \
        if (perf_trace_on())         \
        {                            \
            perf_trace_ev(__VA_ARGS__); \
        }                            \
    }                                \
    while (0)

#endif
