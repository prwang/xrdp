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

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"
#include "perf_trace.h"

#define PERF_TRACE_BUFFER_BYTES (1024 * 1024)
#define PERF_TRACE_DRAIN_MS 10

enum perf_trace_state
{
    PERF_TRACE_UNINITIALIZED = 0,
    PERF_TRACE_DISABLED,
    PERF_TRACE_ARMED,
    PERF_TRACE_FAILED,
    PERF_TRACE_CLOSING,
    PERF_TRACE_CLOSED
};

struct perf_trace_ring
{
    char *base;
    unsigned long long head;
    unsigned long long tail;
    unsigned int dropped;
    unsigned int format_failed;
    int claimed;
};

static int g_perf_fd = -1;
static FILE *g_perf_file = NULL;
static char *g_perf_buffer = NULL;
static int g_perf_state = PERF_TRACE_UNINITIALIZED;
static int g_perf_failure = 0;
static int g_perf_error_reported = 0;
static struct perf_trace_ring *g_perf_rings[PERF_TRACE_MAX_RINGS];
static int g_perf_ring_count = 0;
static pthread_t g_perf_sink;
static int g_perf_sink_live = 0;
static int g_perf_quit = 0;
static unsigned int g_perf_no_ring = 0;
static long long g_perf_pid = 0;
static pthread_mutex_t g_perf_lifecycle_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_perf_wake_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_perf_wake = PTHREAD_COND_INITIALIZER;
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
static long long
perf_trace_tid(void)
{
    return (long long)(intptr_t)pthread_self();
}

/*****************************************************************************/
static void
perf_trace_fail(const char *operation)
{
    int saved_errno;

    saved_errno = errno;
    __atomic_store_n(&g_perf_failure, 1, __ATOMIC_RELEASE);
    if (__sync_bool_compare_and_swap(&g_perf_state, PERF_TRACE_ARMED,
                                     PERF_TRACE_FAILED))
    {
        /* Stop producers before reporting the sink-side failure. */
    }
    if (__sync_bool_compare_and_swap(&g_perf_error_reported, 0, 1))
    {
        LOG(LOG_LEVEL_ERROR, "performance trace %s failed: %s", operation,
            strerror(saved_errno));
    }
}

/*****************************************************************************/
static int
perf_trace_write_all(const char *data, size_t length)
{
    if (fwrite(data, 1, length, g_perf_file) != length)
    {
        perf_trace_fail("write");
        return 0;
    }
    return 1;
}

/*****************************************************************************/
static int
perf_trace_static_char(int character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') ||
           character == '_' || character == '-' || character == '.' ||
           character == ':';
}

/*****************************************************************************/
static int
perf_trace_format_valid(const char *format)
{
    const char *p;
    int event_field;
    int length;

    if (format == NULL || strncmp(format, "event=", 6) != 0)
    {
        return 0;
    }
    p = format;
    event_field = 1;
    while (*p != '\0')
    {
        if (!((*p >= 'a' && *p <= 'z') ||
                (*p >= 'A' && *p <= 'Z') || *p == '_'))
        {
            return 0;
        }
        for (p++; *p != '='; p++)
        {
            if (!(perf_trace_static_char((unsigned char) * p) && *p != ':') ||
                    *p == '\0' || *p == ' ')
            {
                return 0;
            }
        }
        p++;
        if (*p == '%')
        {
            if (event_field)
            {
                return 0;
            }
            p++;
            length = 0;
            while (*p == 'l' && length < 2)
            {
                p++;
                length++;
            }
            if (!(*p == 'd' || *p == 'i' || *p == 'u' || *p == 'o' ||
                    *p == 'x' || *p == 'X'))
            {
                return 0;
            }
            p++;
        }
        else
        {
            if (!perf_trace_static_char((unsigned char) * p))
            {
                return 0;
            }
            while (perf_trace_static_char((unsigned char) * p))
            {
                p++;
            }
        }
        if (*p == '\0')
        {
            return 1;
        }
        if (*p != ' ' || p[1] == '\0' || p[1] == ' ')
        {
            return 0;
        }
        p++;
        event_field = 0;
    }
    return 0;
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
        *(volatile char *)(ring->base + offset) = 0;
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
                       va_list ap, int validate)
{
    char *dest;
    int prefix_length;
    int payload_length;
    int length;
    unsigned long long head;

    if (ring == NULL || (validate && !perf_trace_format_valid(format)))
    {
        if (ring != NULL)
        {
            __atomic_add_fetch(&ring->format_failed, 1, __ATOMIC_RELAXED);
        }
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
    rv = perf_trace_ring_vwrite(ring, ns, pid, tid, format, ap, 1);
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
static int
perf_trace_drain(void)
{
    const char *data;
    size_t length;
    int index;

    for (index = 0; index < g_perf_ring_count; index++)
    {
        while (perf_trace_ring_peek(g_perf_rings[index], &data, &length))
        {
            if (!perf_trace_write_all(data, length))
            {
                return 0;
            }
            perf_trace_ring_consume(g_perf_rings[index], length);
        }
    }
    return 1;
}

/*****************************************************************************/
static void
perf_trace_write_diagnostics(void)
{
    char line[PERF_TRACE_RECORD_BYTES];
    unsigned int count;
    int index;
    int length;

    for (index = 0; index < g_perf_ring_count; index++)
    {
        count = perf_trace_ring_dropped(g_perf_rings[index]);
        if (count != 0)
        {
            length = snprintf(line, sizeof(line), "schema=1 mono_ns=%lld "
                              "pid=%lld tid=0 event=perfdrop dropped=%u "
                              "ring=%d\n", perf_trace_now_ns(), g_perf_pid,
                              count, index);
            if (length > 0 && length < (int)sizeof(line))
            {
                perf_trace_write_all(line, (size_t)length);
            }
        }
        count = perf_trace_ring_format_failed(g_perf_rings[index]);
        if (count != 0)
        {
            length = snprintf(line, sizeof(line), "schema=1 mono_ns=%lld "
                              "pid=%lld tid=0 event=perfformat failed=%u "
                              "ring=%d\n", perf_trace_now_ns(), g_perf_pid,
                              count, index);
            if (length > 0 && length < (int)sizeof(line))
            {
                perf_trace_write_all(line, (size_t)length);
            }
        }
    }
    count = __atomic_load_n(&g_perf_no_ring, __ATOMIC_RELAXED);
    if (count != 0)
    {
        length = snprintf(line, sizeof(line), "schema=1 mono_ns=%lld "
                          "pid=%lld tid=0 event=perfnoring count=%u\n",
                          perf_trace_now_ns(), g_perf_pid, count);
        if (length > 0 && length < (int)sizeof(line))
        {
            perf_trace_write_all(line, (size_t)length);
        }
    }
}

/*****************************************************************************/
static void *
perf_trace_sink_thread(void *arg)
{
    struct timespec until;

    (void)arg;
    while (!__atomic_load_n(&g_perf_quit, __ATOMIC_ACQUIRE))
    {
        if (!__atomic_load_n(&g_perf_failure, __ATOMIC_ACQUIRE))
        {
            perf_trace_drain();
        }
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += PERF_TRACE_DRAIN_MS * 1000000L;
        if (until.tv_nsec >= 1000000000L)
        {
            until.tv_sec++;
            until.tv_nsec -= 1000000000L;
        }
        pthread_mutex_lock(&g_perf_wake_lock);
        if (!__atomic_load_n(&g_perf_quit, __ATOMIC_ACQUIRE))
        {
            pthread_cond_timedwait(&g_perf_wake, &g_perf_wake_lock, &until);
        }
        pthread_mutex_unlock(&g_perf_wake_lock);
    }
    if (!__atomic_load_n(&g_perf_failure, __ATOMIC_ACQUIRE) &&
            perf_trace_drain())
    {
        perf_trace_write_diagnostics();
    }
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
static int
perf_trace_write_clock_base(void)
{
    struct timespec mono;
    struct timespec real;
    char line[PERF_TRACE_RECORD_BYTES];
    int length;

    if (clock_gettime(CLOCK_MONOTONIC, &mono) != 0 ||
            clock_gettime(CLOCK_REALTIME, &real) != 0)
    {
        perf_trace_fail("clock-base timestamp");
        return 0;
    }
    length = snprintf(line, sizeof(line), "schema=1 mono_ns=%lld pid=%lld "
                      "tid=0 event=clock_base real_ns=%lld\n",
                      (long long)mono.tv_sec * 1000000000LL + mono.tv_nsec,
                      g_perf_pid,
                      (long long)real.tv_sec * 1000000000LL + real.tv_nsec);
    return length > 0 && length < (int)sizeof(line) &&
           perf_trace_write_all(line, (size_t)length);
}

/*****************************************************************************/
int
perf_trace_init(void)
{
    const char *prefix;
    char path[512];
    int index;
    int rv;

    pthread_mutex_lock(&g_perf_lifecycle_lock);
    rv = __atomic_load_n(&g_perf_state, __ATOMIC_ACQUIRE);
    if (rv != PERF_TRACE_UNINITIALIZED)
    {
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return rv == PERF_TRACE_ARMED ? PERF_TRACE_INIT_ARMED :
               (rv == PERF_TRACE_DISABLED ? PERF_TRACE_INIT_DISABLED :
                PERF_TRACE_INIT_ERROR);
    }
    prefix = getenv("XRDP_PERF_TRACE");
    if (prefix == NULL || prefix[0] == '\0')
    {
        __atomic_store_n(&g_perf_state, PERF_TRACE_DISABLED,
                         __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return PERF_TRACE_INIT_DISABLED;
    }
    g_perf_pid = (long long)getpid();
    rv = snprintf(path, sizeof(path), "%s.%d", prefix, (int)g_perf_pid);
    if (rv < 0 || rv >= (int)sizeof(path))
    {
        errno = ENAMETOOLONG;
        perf_trace_fail("path construction");
        __atomic_store_n(&g_perf_state, PERF_TRACE_FAILED, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return PERF_TRACE_INIT_ERROR;
    }
    g_perf_fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                     O_NOFOLLOW, 0600);
    if (g_perf_fd < 0)
    {
        perf_trace_fail("create");
        __atomic_store_n(&g_perf_state, PERF_TRACE_FAILED, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return PERF_TRACE_INIT_ERROR;
    }
    g_perf_file = fdopen(g_perf_fd, "w");
    if (g_perf_file == NULL)
    {
        perf_trace_fail("stream setup");
        close(g_perf_fd);
        g_perf_fd = -1;
        __atomic_store_n(&g_perf_state, PERF_TRACE_FAILED, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return PERF_TRACE_INIT_ERROR;
    }
    g_perf_buffer = (char *)malloc(PERF_TRACE_BUFFER_BYTES);
    if (g_perf_buffer == NULL ||
            setvbuf(g_perf_file, g_perf_buffer, _IOFBF,
                    PERF_TRACE_BUFFER_BYTES) != 0)
    {
        perf_trace_fail("buffer setup");
        fclose(g_perf_file);
        g_perf_file = NULL;
        g_perf_fd = -1;
        free(g_perf_buffer);
        g_perf_buffer = NULL;
        __atomic_store_n(&g_perf_state, PERF_TRACE_FAILED, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return PERF_TRACE_INIT_ERROR;
    }
    if (fchmod(g_perf_fd, 0600) != 0)
    {
        perf_trace_fail("mode setup");
        fclose(g_perf_file);
        g_perf_file = NULL;
        g_perf_fd = -1;
        free(g_perf_buffer);
        g_perf_buffer = NULL;
        __atomic_store_n(&g_perf_state, PERF_TRACE_FAILED, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return PERF_TRACE_INIT_ERROR;
    }
    for (index = 0; index < PERF_TRACE_MAX_RINGS; index++)
    {
        g_perf_rings[index] = perf_trace_ring_create();
        if (g_perf_rings[index] == NULL)
        {
            perf_trace_fail("ring allocation");
            perf_trace_delete_rings();
            fclose(g_perf_file);
            g_perf_file = NULL;
            g_perf_fd = -1;
            free(g_perf_buffer);
            g_perf_buffer = NULL;
            __atomic_store_n(&g_perf_state, PERF_TRACE_FAILED,
                             __ATOMIC_RELEASE);
            pthread_mutex_unlock(&g_perf_lifecycle_lock);
            return PERF_TRACE_INIT_ERROR;
        }
        g_perf_ring_count++;
    }
    if (!perf_trace_write_clock_base())
    {
        perf_trace_delete_rings();
        fclose(g_perf_file);
        g_perf_file = NULL;
        g_perf_fd = -1;
        free(g_perf_buffer);
        g_perf_buffer = NULL;
        __atomic_store_n(&g_perf_state, PERF_TRACE_FAILED, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return PERF_TRACE_INIT_ERROR;
    }
    __atomic_store_n(&g_perf_quit, 0, __ATOMIC_RELEASE);
    if (pthread_create(&g_perf_sink, NULL, perf_trace_sink_thread, NULL) != 0)
    {
        errno = EAGAIN;
        perf_trace_fail("sink thread start");
        perf_trace_delete_rings();
        fclose(g_perf_file);
        g_perf_file = NULL;
        g_perf_fd = -1;
        free(g_perf_buffer);
        g_perf_buffer = NULL;
        __atomic_store_n(&g_perf_state, PERF_TRACE_FAILED, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return PERF_TRACE_INIT_ERROR;
    }
    g_perf_sink_live = 1;
    __atomic_store_n(&g_perf_state, PERF_TRACE_ARMED, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_perf_lifecycle_lock);
    return PERF_TRACE_INIT_ARMED;
}

/*****************************************************************************/
int
perf_trace_on(void)
{
    return __atomic_load_n(&g_perf_state, __ATOMIC_ACQUIRE) ==
           PERF_TRACE_ARMED;
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
    __atomic_add_fetch(&g_perf_no_ring, 1, __ATOMIC_RELAXED);
    return NULL;
}

/*****************************************************************************/
void
perf_trace_ev(const char *format, ...)
{
    struct perf_trace_ring *ring;
    va_list ap;

    if (!perf_trace_on() || format == NULL)
    {
        return;
    }
    ring = perf_trace_my_ring();
    if (ring == NULL)
    {
        return;
    }
    va_start(ap, format);
    /* Shipped call sites are compile-time literals checked in review and by
     * the printf attribute. The public ring primitive validates the complete
     * restricted grammar; repeating that scan on every frame would measure
     * the schema validator rather than publication. */
    perf_trace_ring_vwrite(ring, perf_trace_now_ns(), g_perf_pid,
                           perf_trace_tid(), format, ap, 0);
    va_end(ap);
}

/*****************************************************************************/
int
perf_trace_close(void)
{
    int state;
    int rv;

    pthread_mutex_lock(&g_perf_lifecycle_lock);
    state = __atomic_load_n(&g_perf_state, __ATOMIC_ACQUIRE);
    if (state == PERF_TRACE_CLOSED || state == PERF_TRACE_DISABLED)
    {
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return 0;
    }
    if (state == PERF_TRACE_UNINITIALIZED)
    {
        __atomic_store_n(&g_perf_state, PERF_TRACE_CLOSED, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_perf_lifecycle_lock);
        return 0;
    }
    __atomic_store_n(&g_perf_state, PERF_TRACE_CLOSING, __ATOMIC_RELEASE);
    __atomic_store_n(&g_perf_quit, 1, __ATOMIC_RELEASE);
    pthread_mutex_lock(&g_perf_wake_lock);
    pthread_cond_signal(&g_perf_wake);
    pthread_mutex_unlock(&g_perf_wake_lock);
    if (g_perf_sink_live)
    {
        pthread_join(g_perf_sink, NULL);
        g_perf_sink_live = 0;
    }
    rv = __atomic_load_n(&g_perf_failure, __ATOMIC_ACQUIRE) ? -1 : 0;
    if (g_perf_fd >= 0)
    {
        if (fflush(g_perf_file) != 0)
        {
            perf_trace_fail("flush");
            rv = -1;
        }
        if (fsync(g_perf_fd) != 0)
        {
            perf_trace_fail("flush");
            rv = -1;
        }
        if (fclose(g_perf_file) != 0)
        {
            perf_trace_fail("close");
            rv = -1;
        }
        g_perf_file = NULL;
        g_perf_fd = -1;
    }
    free(g_perf_buffer);
    g_perf_buffer = NULL;
    perf_trace_delete_rings();
    __atomic_store_n(&g_perf_state, PERF_TRACE_CLOSED, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_perf_lifecycle_lock);
    return rv;
}
