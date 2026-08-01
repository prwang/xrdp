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
 * Dedicated performance trace sink.
 *
 * This is deliberately NOT the logger. LOG() formats a timestamp, takes
 * a global mutex and performs an UNBUFFERED write() syscall per line
 * (log.c internal_log_message) -- measured at ~350-450 ns and, more to
 * the point, it lands in /var/log/xrdp and journald mixed with every
 * other message. Stage timings are a different kind of data: high rate,
 * machine-read, and useless to an administrator reading a log.
 *
 * So: its own file, its own schema, and stdio's own buffering rather
 * than a hand-rolled ring buffer -- measured at ~100 ns per event
 * including the clock read, which is ~4x cheaper than LOG() and small
 * enough that bracketing a stage cannot perturb what it measures. At
 * the rate this is used for (single digits of events per frame) the
 * cost is tens of microseconds per SECOND of session.
 *
 * Armed by the environment variable XRDP_PERF_TRACE, whose value is a
 * path PREFIX; each process appends its own pid, so several xrdp
 * processes never interleave into one file. Unset -- the shipped
 * default -- costs one predictable branch on a cached flag.
 *
 * Record format, one line per event, all fields space separated:
 *
 *   <monotonic_ns> <tid> <tag> <a> <b>
 *
 * CLOCK_MONOTONIC is system-wide, so records from different processes
 * share one timeline and can be intersected. Frames must still be
 * joined by their ECHOED IDENTITY (put it in <a>), never by a time
 * window -- see BACKLOG #64 and the 2c quality gate.
 */

#ifndef _PERF_TRACE_H
#define _PERF_TRACE_H

/**
 * Is the sink armed? Cheap after the first call (a cached flag); safe
 * to call from any thread. Opens the sink on first use.
 */
int
perf_trace_on(void);

/**
 * Record one event. Callers should use the PERF_TRACE macro so the
 * argument expressions are not evaluated when the sink is disarmed.
 * tag must be a short, space-free identifier.
 */
void
perf_trace_ev(const char *tag, int a, int b);

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
                  const char *tag, int a, int b);

#define PERF_TRACE(tag, a, b)             \
    do                                    \
    {                                     \
        if (perf_trace_on())              \
        {                                 \
            perf_trace_ev((tag), (a), (b)); \
        }                                 \
    }                                     \
    while (0)

#endif
