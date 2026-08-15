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
 * Dedicated performance trace sink -- see perf_trace.h.
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "perf_trace.h"

#define PERF_TRACE_BUF_SIZE (1024 * 1024)
#define PERF_TRACE_DRAIN_MS 10

struct perf_trace_ring
{
    char *base;
    unsigned long long head;
    unsigned long long tail;
    unsigned int dropped;
    unsigned int drop_seen;
    unsigned int format_failed;
    unsigned int format_failed_seen;
    int claimed;
};

static FILE *g_perf_file = NULL;
static char *g_perf_buf = NULL;
static int g_perf_armed = 0;
static pthread_once_t g_perf_once = PTHREAD_ONCE_INIT;
static struct perf_trace_ring *g_perf_rings[PERF_TRACE_MAX_RINGS];
static int g_perf_ring_count = 0;
static pthread_t g_perf_sink;
static int g_perf_sink_live = 0;
static volatile int g_perf_quit = 0;
static volatile unsigned int g_perf_no_ring = 0;
static long long g_perf_pid = 0;
static __thread struct perf_trace_ring *g_perf_my_ring = NULL;
static __thread int g_perf_my_ring_done = 0;

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
struct perf_trace_ring *
perf_trace_ring_create(void)
{
    struct perf_trace_ring *ring;
    void *reserved;
    void *first;
    void *second;
    int fd;
    size_t offset;
    long page_size;

    ring = (struct perf_trace_ring *)calloc(1, sizeof(*ring));
    if (ring == NULL)
    {
        return NULL;
    }
    fd = memfd_create("xrdp-perf-trace", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, PERF_TRACE_RING_BYTES) != 0)
    {
        if (fd >= 0)
        {
            close(fd);
        }
        free(ring);
        return NULL;
    }
    reserved = mmap(NULL, PERF_TRACE_RING_BYTES * 2, PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reserved == MAP_FAILED)
    {
        close(fd);
        free(ring);
        return NULL;
    }
    first = mmap(reserved, PERF_TRACE_RING_BYTES, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED, fd, 0);
    second = first == MAP_FAILED ? MAP_FAILED :
             mmap((char *)reserved + PERF_TRACE_RING_BYTES,
                  PERF_TRACE_RING_BYTES, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_FIXED, fd, 0);
    close(fd);
    if (first == MAP_FAILED || second == MAP_FAILED)
    {
        munmap(reserved, PERF_TRACE_RING_BYTES * 2);
        free(ring);
        return NULL;
    }
    ring->base = (char *)reserved;

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 1)
    {
        page_size = 4096;
    }
    for (offset = 0; offset < PERF_TRACE_RING_BYTES;
            offset += (size_t)page_size)
    {
        ring->base[offset] = 0;
    }
    return ring;
}

/*****************************************************************************/
void
perf_trace_ring_delete(struct perf_trace_ring *ring)
{
    if (ring != NULL)
    {
        if (ring->base != NULL)
        {
            munmap(ring->base, PERF_TRACE_RING_BYTES * 2);
        }
        free(ring);
    }
}

/*****************************************************************************/
static char *
perf_trace_ring_reserve(struct perf_trace_ring *ring)
{
    unsigned long long head;
    unsigned long long tail;

    if (ring == NULL)
    {
        return NULL;
    }
    head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);
    tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
    if (head - tail > PERF_TRACE_RING_BYTES - PERF_TRACE_RECORD_BYTES)
    {
        __atomic_add_fetch(&ring->dropped, 1, __ATOMIC_RELAXED);
        return NULL;
    }
    return ring->base + (head & (PERF_TRACE_RING_BYTES - 1));
}

/*****************************************************************************/
static int
perf_trace_ring_vwrite(struct perf_trace_ring *ring, long long ns,
                       long long pid, long long tid, const char *format,
                       va_list ap)
{
    char *dest;
    int prefix_length;
    int payload_length;
    int length;
    unsigned long long head;

    if (format == NULL)
    {
        return 0;
    }
    dest = perf_trace_ring_reserve(ring);
    if (dest == NULL)
    {
        return 0;
    }
    prefix_length = snprintf(dest, PERF_TRACE_RECORD_BYTES,
                             "schema=1 mono_ns=%lld pid=%lld tid=%lld ",
                             ns, pid, tid);
    if (prefix_length < 0 || prefix_length >= PERF_TRACE_RECORD_BYTES)
    {
        __atomic_add_fetch(&ring->format_failed, 1, __ATOMIC_RELAXED);
        return 0;
    }
    payload_length = vsnprintf(dest + prefix_length,
                               PERF_TRACE_RECORD_BYTES - prefix_length,
                               format, ap);
    length = prefix_length + payload_length;
    if (payload_length < 0 || length >= PERF_TRACE_RECORD_BYTES - 1)
    {
        __atomic_add_fetch(&ring->format_failed, 1, __ATOMIC_RELAXED);
        return 0;
    }
    dest[length++] = '\n';
    head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);
    __atomic_store_n(&ring->head, head + (unsigned int)length,
                     __ATOMIC_RELEASE);
    return 1;
}

/*****************************************************************************/
int
perf_trace_ring_write(struct perf_trace_ring *ring, long long ns,
                      long long pid, long long tid, const char *format, ...)
{
    va_list ap;
    int rv;

    va_start(ap, format);
    rv = perf_trace_ring_vwrite(ring, ns, pid, tid, format, ap);
    va_end(ap);
    return rv;
}

/*****************************************************************************/
int
perf_trace_ring_peek(struct perf_trace_ring *ring, const char **data,
                     size_t *length)
{
    unsigned long long head;
    unsigned long long tail;

    if (ring == NULL || data == NULL || length == NULL)
    {
        return 0;
    }
    tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
    head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
    if (head == tail)
    {
        *data = NULL;
        *length = 0;
        return 0;
    }
    *data = ring->base + (tail & (PERF_TRACE_RING_BYTES - 1));
    *length = (size_t)(head - tail);
    return 1;
}

/*****************************************************************************/
void
perf_trace_ring_consume(struct perf_trace_ring *ring, size_t length)
{
    unsigned long long head;
    unsigned long long tail;

    if (ring == NULL)
    {
        return;
    }
    tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
    head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
    if (length > head - tail)
    {
        length = (size_t)(head - tail);
    }
    __atomic_store_n(&ring->tail, tail + length, __ATOMIC_RELEASE);
}

/*****************************************************************************/
unsigned int
perf_trace_ring_dropped(const struct perf_trace_ring *ring)
{
    return ring == NULL ? 0 :
           __atomic_load_n(&ring->dropped, __ATOMIC_RELAXED);
}

/*****************************************************************************/
unsigned int
perf_trace_ring_format_failed(const struct perf_trace_ring *ring)
{
    return ring == NULL ? 0 :
           __atomic_load_n(&ring->format_failed, __ATOMIC_RELAXED);
}

/*****************************************************************************/
static void
perf_trace_drain(void)
{
    const char *data;
    size_t length;
    int index;
    unsigned int dropped;
    unsigned int format_failed;

    for (index = 0; index < g_perf_ring_count; index++)
    {
        struct perf_trace_ring *ring = g_perf_rings[index];

        while (perf_trace_ring_peek(ring, &data, &length))
        {
            if (fwrite(data, 1, length, g_perf_file) != length)
            {
                return;
            }
            perf_trace_ring_consume(ring, length);
        }
        dropped = perf_trace_ring_dropped(ring);
        if (dropped != ring->drop_seen)
        {
            fprintf(g_perf_file, "schema=1 mono_ns=%lld pid=%lld tid=0 "
                    "event=perfdrop dropped=%u ring=%d\n",
                    perf_trace_now_ns(), g_perf_pid, dropped, index);
            ring->drop_seen = dropped;
        }
        format_failed = perf_trace_ring_format_failed(ring);
        if (format_failed != ring->format_failed_seen)
        {
            fprintf(g_perf_file, "schema=1 mono_ns=%lld pid=%lld tid=0 "
                    "event=perfformat failed=%u ring=%d\n",
                    perf_trace_now_ns(), g_perf_pid, format_failed, index);
            ring->format_failed_seen = format_failed;
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
    perf_trace_drain();
    if (g_perf_no_ring != 0)
    {
        fprintf(g_perf_file, "schema=1 mono_ns=%lld pid=%lld tid=0 "
                "event=perfnoring count=%u\n", perf_trace_now_ns(),
                g_perf_pid, g_perf_no_ring);
    }
    fflush(g_perf_file);
    return NULL;
}

/*****************************************************************************/
static void
perf_trace_delete_rings(void)
{
    int index;

    for (index = 0; index < g_perf_ring_count; index++)
    {
        perf_trace_ring_delete(g_perf_rings[index]);
        g_perf_rings[index] = NULL;
    }
    g_perf_ring_count = 0;
}

/*****************************************************************************/
static void
perf_trace_open(void)
{
    const char *prefix;
    char path[512];
    int fd;
    int index;

    prefix = getenv("XRDP_PERF_TRACE");
    if (prefix == NULL || prefix[0] == '\0')
    {
        return;
    }
    g_perf_pid = (long long)getpid();
    if (snprintf(path, sizeof(path), "%s.%d", prefix, (int)g_perf_pid)
            >= (int)sizeof(path))
    {
        return;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
              0600);
    if (fd < 0)
    {
        return;
    }
    g_perf_file = fdopen(fd, "w");
    if (g_perf_file == NULL)
    {
        close(fd);
        return;
    }
    g_perf_buf = (char *)malloc(PERF_TRACE_BUF_SIZE);
    if (g_perf_buf != NULL)
    {
        setvbuf(g_perf_file, g_perf_buf, _IOFBF, PERF_TRACE_BUF_SIZE);
    }
    for (index = 0; index < PERF_TRACE_MAX_RINGS; index++)
    {
        g_perf_rings[index] = perf_trace_ring_create();
        if (g_perf_rings[index] == NULL)
        {
            perf_trace_delete_rings();
            fclose(g_perf_file);
            g_perf_file = NULL;
            free(g_perf_buf);
            g_perf_buf = NULL;
            return;
        }
        g_perf_ring_count++;
    }
    {
        struct timespec mono;
        struct timespec real;

        if (clock_gettime(CLOCK_MONOTONIC, &mono) == 0 &&
                clock_gettime(CLOCK_REALTIME, &real) == 0)
        {
            fprintf(g_perf_file, "schema=1 mono_ns=%lld pid=%lld tid=0 "
                    "event=clock_base real_ns=%lld\n",
                    (long long)mono.tv_sec * 1000000000LL + mono.tv_nsec,
                    g_perf_pid,
                    (long long)real.tv_sec * 1000000000LL + real.tv_nsec);
        }
    }
    if (pthread_create(&g_perf_sink, NULL, perf_trace_sink_thread,
                       NULL) != 0)
    {
        perf_trace_delete_rings();
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
        if (__sync_bool_compare_and_swap(&g_perf_rings[index]->claimed, 0, 1))
        {
            g_perf_my_ring = g_perf_rings[index];
            return g_perf_my_ring;
        }
    }
    __sync_fetch_and_add(&g_perf_no_ring, 1);
    return NULL;
}

/*****************************************************************************/
void
perf_trace_ev(const char *format, ...)
{
    struct perf_trace_ring *ring;
    va_list ap;

    if (!g_perf_armed || format == NULL)
    {
        return;
    }
    ring = perf_trace_my_ring();
    if (ring == NULL)
    {
        return;
    }
    va_start(ap, format);
    perf_trace_ring_vwrite(ring, perf_trace_now_ns(), g_perf_pid,
                           (long long)pthread_self(), format, ap);
    va_end(ap);
}

/*****************************************************************************/
void
perf_trace_close(void)
{
    if (g_perf_sink_live)
    {
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
    perf_trace_delete_rings();
    if (g_perf_buf != NULL)
    {
        free(g_perf_buf);
        g_perf_buf = NULL;
    }
}
