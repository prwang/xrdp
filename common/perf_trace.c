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
 * Dedicated performance trace sink -- see perf_trace.h for why this is
 * not the logger.
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

/* one page short of a MiB is pointless precision; a round buffer keeps
 * the amortised write() cost per event in the single-digit ns */
#define PERF_TRACE_BUF_SIZE (1024 * 1024)

static FILE *g_perf_file = NULL;
static char *g_perf_buf = NULL;
static int g_perf_armed = 0;
static pthread_once_t g_perf_once = PTHREAD_ONCE_INIT;

/*****************************************************************************/
static long long
perf_trace_now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
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
int
perf_trace_format(char *buf, int len, long long ns, long long tid,
                  const char *tag, int a, int b)
{
    if (buf == NULL || len < 1 || tag == NULL)
    {
        return -1;
    }
    return snprintf(buf, (size_t)len, "%lld %lld %s %d %d\n", ns, tid,
                    tag, a, b);
}

/*****************************************************************************/
void
perf_trace_ev(const char *tag, int a, int b)
{
    if (!g_perf_armed || tag == NULL)
    {
        return;
    }
    /* stdio takes its own lock, so concurrent worker/main-thread events
     * cannot tear a record; the tid field is what separates them */
    fprintf(g_perf_file, "%lld %lld %s %d %d\n", perf_trace_now_ns(),
            (long long)pthread_self(), tag, a, b);
}

/*****************************************************************************/
void
perf_trace_close(void)
{
    if (g_perf_file != NULL)
    {
        g_perf_armed = 0;
        fflush(g_perf_file);
        fclose(g_perf_file);
        g_perf_file = NULL;
    }
    if (g_perf_buf != NULL)
    {
        free(g_perf_buf);
        g_perf_buf = NULL;
    }
}
