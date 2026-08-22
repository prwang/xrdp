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
 * Contract tests for the named text performance trace and its Linux
 * double-mapped SPSC byte ring (BACKLOG #107).
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "perf_trace.h"
#include "test_common.h"

static void
assert_one_record(struct perf_trace_ring *ring, const char *expected)
{
    const char *data;
    size_t length;

    ck_assert_int_eq(perf_trace_ring_peek(ring, &data, &length), 1);
    ck_assert_uint_eq(length, strlen(expected));
    ck_assert_int_eq(memcmp(data, expected, length), 0);
    perf_trace_ring_consume(ring, length);
    ck_assert_int_eq(perf_trace_ring_peek(ring, &data, &length), 0);
}

static void
make_trace_path(char *directory, size_t directory_bytes,
                char *prefix, size_t prefix_bytes,
                char *path, size_t path_bytes, long long pid)
{
    char template[] = "/tmp/xrdp-perf-trace-test-XXXXXX";

    ck_assert_ptr_ne(mkdtemp(template), NULL);
    ck_assert_int_lt(snprintf(directory, directory_bytes, "%s", template),
                     (int)directory_bytes);
    ck_assert_int_lt(snprintf(prefix, prefix_bytes, "%s/trace", directory),
                     (int)prefix_bytes);
    ck_assert_int_lt(snprintf(path, path_bytes, "%s.%lld", prefix, pid),
                     (int)path_bytes);
}

static char *
read_whole_file(const char *path)
{
    FILE *file;
    char *data;
    long length;

    file = fopen(path, "rb");
    ck_assert_ptr_ne(file, NULL);
    ck_assert_int_eq(fseek(file, 0, SEEK_END), 0);
    length = ftell(file);
    ck_assert_int_ge(length, 0);
    ck_assert_int_eq(fseek(file, 0, SEEK_SET), 0);
    data = (char *)malloc((size_t)length + 1);
    ck_assert_ptr_ne(data, NULL);
    ck_assert_uint_eq(fread(data, 1, (size_t)length, file), (size_t)length);
    data[length] = '\0';
    ck_assert_int_eq(fclose(file), 0);
    return data;
}

static int
count_text(const char *text, const char *needle)
{
    int count;
    size_t needle_length;

    count = 0;
    needle_length = strlen(needle);
    while ((text = strstr(text, needle)) != NULL)
    {
        count++;
        text += needle_length;
    }
    return count;
}

START_TEST(test_perf_trace_named_schema)
{
    struct perf_trace_ring *ring;

    ring = perf_trace_ring_create();
    ck_assert_ptr_ne(ring, NULL);
    ck_assert_int_eq(
        perf_trace_ring_write(
            ring, 1569856240672000LL, 27, 140737488355328LL,
            "event=send bytes=%d last=%d frame_id=%d id_server=%d "
            "id_client=%d fif=%d", 1930000, 0, 288, 288, 286, 2),
        1);
    assert_one_record(
        ring,
        "schema=1 mono_ns=1569856240672000 pid=27 tid=140737488355328 "
        "event=send bytes=1930000 last=0 frame_id=288 id_server=288 "
        "id_client=286 fif=2\n");
    perf_trace_ring_delete(ring);
}
END_TEST

START_TEST(test_perf_trace_64_bit_and_negative_values)
{
    struct perf_trace_ring *ring;

    ring = perf_trace_ring_create();
    ck_assert_ptr_ne(ring, NULL);
    ck_assert_int_eq(
        perf_trace_ring_write(
            ring, LLONG_MAX, LLONG_MAX, -1,
            "event=boundary signed=%lld unsigned=%llu ready=%d",
            LLONG_MIN, ULLONG_MAX, 1),
        1);
    assert_one_record(
        ring,
        "schema=1 mono_ns=9223372036854775807 pid=9223372036854775807 "
        "tid=-1 event=boundary signed=-9223372036854775808 "
        "unsigned=18446744073709551615 ready=1\n");
    perf_trace_ring_delete(ring);
}
END_TEST

START_TEST(test_perf_trace_rejects_truncated_record)
{
    struct perf_trace_ring *ring;
    const char *data;
    size_t length;

    ring = perf_trace_ring_create();
    ck_assert_ptr_ne(ring, NULL);
    ck_assert_int_eq(
        perf_trace_ring_write(ring, 1, 2, 3,
                              "event=too_long value=%0500d", 1),
        0);
    ck_assert_uint_eq(perf_trace_ring_format_failed(ring), 1);
    ck_assert_int_eq(perf_trace_ring_peek(ring, &data, &length), 0);
    perf_trace_ring_delete(ring);
}
END_TEST

START_TEST(test_perf_trace_rejects_unsafe_formats)
{
    struct perf_trace_ring *ring;

    ring = perf_trace_ring_create();
    ck_assert_ptr_ne(ring, NULL);
    ck_assert_int_eq(perf_trace_ring_write(
                         ring, 1, 2, 3, "event=bad value=%s", "text"), 0);
    ck_assert_int_eq(perf_trace_ring_write(
                         ring, 1, 2, 3, "event=bad value=%1$d", 1), 0);
    ck_assert_int_eq(perf_trace_ring_write(
                         ring, 1, 2, 3, "event=bad value=%*d", 2, 1), 0);
    ck_assert_int_eq(perf_trace_ring_write(
                         ring, 1, 2, 3, "event=bad value=%f", 1.0), 0);
    ck_assert_uint_eq(perf_trace_ring_format_failed(ring), 4);
    perf_trace_ring_delete(ring);
}
END_TEST

START_TEST(test_perf_trace_disarmed_by_default)
{
    ck_assert_int_eq(perf_trace_init(), PERF_TRACE_INIT_DISABLED);
    ck_assert_int_eq(perf_trace_on(), 0);
    perf_trace_ev("event=emit_beg frame_id=%d monitor=%d", 1, 2);
    perf_trace_close();
    ck_assert_int_eq(perf_trace_on(), 0);
}
END_TEST

START_TEST(test_perf_trace_does_not_initialize_lazily)
{
    char directory[128];
    char prefix[160];
    char path[192];

    make_trace_path(directory, sizeof(directory), prefix, sizeof(prefix),
                    path, sizeof(path), (long long)getpid());
    ck_assert_int_eq(setenv("XRDP_PERF_TRACE", prefix, 1), 0);
    ck_assert_int_eq(perf_trace_on(), 0);
    PERF_TRACE("event=lazy_sentinel value=%d", 1);
    ck_assert_int_eq(access(path, F_OK), -1);
    ck_assert_int_eq(perf_trace_close(), 0);
    ck_assert_int_eq(rmdir(directory), 0);
}
END_TEST

START_TEST(test_perf_trace_explicit_lifecycle_and_final_drain)
{
    struct stat st;
    char directory[128];
    char prefix[160];
    char path[192];
    char *data;

    make_trace_path(directory, sizeof(directory), prefix, sizeof(prefix),
                    path, sizeof(path), (long long)getpid());
    ck_assert_int_eq(setenv("XRDP_PERF_TRACE", prefix, 1), 0);
    ck_assert_int_eq(perf_trace_init(), PERF_TRACE_INIT_ARMED);
    ck_assert_int_eq(perf_trace_on(), 1);
    PERF_TRACE("event=final_drain value=%d", 73);
    ck_assert_int_eq(perf_trace_close(), 0);
    ck_assert_int_eq(perf_trace_on(), 0);
    PERF_TRACE("event=after_close value=%d", 74);
    ck_assert_int_eq(perf_trace_close(), 0);
    ck_assert_int_eq(stat(path, &st), 0);
    ck_assert_int_eq(st.st_mode & 0777, 0600);
    data = read_whole_file(path);
    ck_assert_ptr_ne(strstr(data, "event=clock_base real_ns="), NULL);
    ck_assert_ptr_ne(strstr(data, "event=final_drain value=73\n"), NULL);
    ck_assert_ptr_eq(strstr(data, "event=after_close"), NULL);
    free(data);
    ck_assert_int_eq(unlink(path), 0);
    ck_assert_int_eq(rmdir(directory), 0);
}
END_TEST

START_TEST(test_perf_trace_refuses_existing_symlink)
{
    char directory[128];
    char prefix[160];
    char path[192];
    struct stat st;

    make_trace_path(directory, sizeof(directory), prefix, sizeof(prefix),
                    path, sizeof(path), (long long)getpid());
    ck_assert_int_eq(symlink("/dev/null", path), 0);
    ck_assert_int_eq(setenv("XRDP_PERF_TRACE", prefix, 1), 0);
    ck_assert_int_eq(perf_trace_init(), PERF_TRACE_INIT_ERROR);
    ck_assert_int_eq(lstat(path, &st), 0);
    ck_assert(S_ISLNK(st.st_mode));
    ck_assert_int_eq(perf_trace_close(), -1);
    ck_assert_int_eq(unlink(path), 0);
    ck_assert_int_eq(rmdir(directory), 0);
}
END_TEST

struct writer_args
{
    int writer;
    int records;
};

static void *
write_records(void *arg)
{
    struct writer_args *args;
    int index;

    args = (struct writer_args *)arg;
    for (index = 0; index < args->records; index++)
    {
        PERF_TRACE("event=thread_record writer=%d sequence=%d",
                   args->writer, index);
    }
    return NULL;
}

START_TEST(test_perf_trace_concurrent_thread_rings)
{
    struct writer_args args[4];
    pthread_t threads[4];
    char directory[128];
    char prefix[160];
    char path[192];
    char *data;
    int index;

    make_trace_path(directory, sizeof(directory), prefix, sizeof(prefix),
                    path, sizeof(path), (long long)getpid());
    ck_assert_int_eq(setenv("XRDP_PERF_TRACE", prefix, 1), 0);
    ck_assert_int_eq(perf_trace_init(), PERF_TRACE_INIT_ARMED);
    for (index = 0; index < 4; index++)
    {
        args[index].writer = index;
        args[index].records = 100;
        ck_assert_int_eq(pthread_create(&threads[index], NULL, write_records,
                                        &args[index]), 0);
    }
    for (index = 0; index < 4; index++)
    {
        ck_assert_int_eq(pthread_join(threads[index], NULL), 0);
    }
    ck_assert_int_eq(perf_trace_close(), 0);
    data = read_whole_file(path);
    ck_assert_int_eq(count_text(data, "event=thread_record "), 400);
    ck_assert_ptr_eq(strstr(data, "event=perfdrop"), NULL);
    ck_assert_ptr_eq(strstr(data, "event=perfnoring"), NULL);
    free(data);
    ck_assert_int_eq(unlink(path), 0);
    ck_assert_int_eq(rmdir(directory), 0);
}
END_TEST

START_TEST(test_perf_trace_post_fork_initialization)
{
    char directory[128];
    char prefix[160];
    char path[192];
    char *data;
    pid_t pid;
    int status;

    make_trace_path(directory, sizeof(directory), prefix, sizeof(prefix),
                    path, sizeof(path), 0);
    ck_assert_int_eq(setenv("XRDP_PERF_TRACE", prefix, 1), 0);
    pid = fork();
    ck_assert_int_ne(pid, -1);
    if (pid == 0)
    {
        if (perf_trace_init() != PERF_TRACE_INIT_ARMED)
        {
            _exit(10);
        }
        PERF_TRACE("event=post_fork value=%d", 81);
        _exit(perf_trace_close() == 0 ? 0 : 11);
    }
    ck_assert_int_eq(waitpid(pid, &status, 0), pid);
    ck_assert(WIFEXITED(status));
    ck_assert_int_eq(WEXITSTATUS(status), 0);
    ck_assert_int_lt(snprintf(path, sizeof(path), "%s.%d", prefix, (int)pid),
                     (int)sizeof(path));
    data = read_whole_file(path);
    ck_assert_ptr_ne(strstr(data, "event=post_fork value=81\n"), NULL);
    free(data);
    ck_assert_int_eq(unlink(path), 0);
    ck_assert_int_eq(rmdir(directory), 0);
}
END_TEST

START_TEST(test_perf_trace_sink_write_failure_is_visible)
{
    struct rlimit limit;
    char directory[128];
    char prefix[160];
    char path[192];
    int index;

    make_trace_path(directory, sizeof(directory), prefix, sizeof(prefix),
                    path, sizeof(path), (long long)getpid());
    ck_assert_int_eq(setenv("XRDP_PERF_TRACE", prefix, 1), 0);
    ck_assert_int_eq(perf_trace_init(), PERF_TRACE_INIT_ARMED);
    ck_assert_ptr_ne(signal(SIGXFSZ, SIG_IGN), SIG_ERR);
    limit.rlim_cur = 512;
    limit.rlim_max = 512;
    ck_assert_int_eq(setrlimit(RLIMIT_FSIZE, &limit), 0);
    for (index = 0; index < 1000; index++)
    {
        PERF_TRACE("event=write_failure sequence=%d value=%d", index,
                   index * 2);
    }
    ck_assert_int_eq(perf_trace_close(), -1);
    ck_assert_int_eq(unlink(path), 0);
    ck_assert_int_eq(rmdir(directory), 0);
}
END_TEST

START_TEST(test_perf_trace_ring_fifo_bytes)
{
    struct perf_trace_ring *ring;
    const char *data;
    size_t length;

    ring = perf_trace_ring_create();
    ck_assert_ptr_ne(ring, NULL);
    ck_assert_int_eq(perf_trace_ring_write(
                         ring, 100, 7, 8, "event=first value=%d", 11), 1);
    ck_assert_int_eq(perf_trace_ring_write(
                         ring, 101, 7, 8, "event=second value=%d", 12), 1);
    ck_assert_int_eq(perf_trace_ring_peek(ring, &data, &length), 1);
    ck_assert_uint_eq(
        length,
        strlen("schema=1 mono_ns=100 pid=7 tid=8 event=first value=11\n"
               "schema=1 mono_ns=101 pid=7 tid=8 event=second value=12\n"));
    ck_assert_int_eq(
        memcmp(data,
               "schema=1 mono_ns=100 pid=7 tid=8 event=first value=11\n"
               "schema=1 mono_ns=101 pid=7 tid=8 event=second value=12\n",
               length),
        0);
    perf_trace_ring_consume(ring, length);
    ck_assert_int_eq(perf_trace_ring_peek(ring, &data, &length), 0);
    perf_trace_ring_delete(ring);
}
END_TEST

/* More than one physical ring of bytes passes through without a drop. The
 * record which crosses the physical end is read as part of one contiguous
 * span from the alias mapping; no padding or split record exists. */
START_TEST(test_perf_trace_ring_wraps_without_copy)
{
    struct perf_trace_ring *ring;
    const char *data;
    size_t length;
    int index;

    ring = perf_trace_ring_create();
    ck_assert_ptr_ne(ring, NULL);
    for (index = 0; index < 12000; index++)
    {
        ck_assert_int_eq(
            perf_trace_ring_write(ring, index, 1, 2,
                                  "event=wrap sequence=%d value=%d",
                                  index, index * 2),
            1);
        ck_assert_int_eq(perf_trace_ring_peek(ring, &data, &length), 1);
        ck_assert_int_eq(data[length - 1], '\n');
        perf_trace_ring_consume(ring, length);
    }
    ck_assert_uint_eq(perf_trace_ring_dropped(ring), 0);
    ck_assert_int_eq(perf_trace_ring_peek(ring, &data, &length), 0);
    perf_trace_ring_delete(ring);
}
END_TEST

START_TEST(test_perf_trace_ring_counts_whole_record_drops)
{
    struct perf_trace_ring *ring;
    const char *data;
    size_t length;
    int stored;
    int index;

    ring = perf_trace_ring_create();
    ck_assert_ptr_ne(ring, NULL);
    stored = 0;
    while (perf_trace_ring_write(ring, stored, 1, 2,
                                 "event=fill sequence=%d", stored))
    {
        stored++;
    }
    ck_assert_int_gt(stored, 0);
    ck_assert_uint_eq(perf_trace_ring_dropped(ring), 1);
    for (index = 0; index < 99; index++)
    {
        ck_assert_int_eq(perf_trace_ring_write(
                             ring, index, 1, 2,
                             "event=fill sequence=%d", index), 0);
    }
    ck_assert_uint_eq(perf_trace_ring_dropped(ring), 100);
    ck_assert_int_eq(perf_trace_ring_peek(ring, &data, &length), 1);
    ck_assert_int_eq(data[length - 1], '\n');
    perf_trace_ring_consume(ring, length);
    ck_assert_int_eq(perf_trace_ring_write(
                         ring, 999, 1, 2, "event=after_drop value=%d", 7), 1);
    ck_assert_uint_eq(perf_trace_ring_dropped(ring), 100);
    perf_trace_ring_delete(ring);
}
END_TEST

START_TEST(test_perf_trace_ring_rejects_null)
{
    const char *data;
    size_t length;

    ck_assert_int_eq(perf_trace_ring_write(
                         NULL, 1, 2, 3, "event=null value=%d", 4), 0);
    ck_assert_int_eq(perf_trace_ring_peek(NULL, &data, &length), 0);
    perf_trace_ring_consume(NULL, 10);
    ck_assert_uint_eq(perf_trace_ring_dropped(NULL), 0);
    ck_assert_uint_eq(perf_trace_ring_format_failed(NULL), 0);
    perf_trace_ring_delete(NULL);
}
END_TEST

Suite *
make_suite_test_perf_trace(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("PerfTrace");
    tc = tcase_create("perf_trace");
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_perf_trace_named_schema);
    tcase_add_test(tc, test_perf_trace_64_bit_and_negative_values);
    tcase_add_test(tc, test_perf_trace_rejects_truncated_record);
    tcase_add_test(tc, test_perf_trace_rejects_unsafe_formats);
    tcase_add_test(tc, test_perf_trace_disarmed_by_default);
    tcase_add_test(tc, test_perf_trace_does_not_initialize_lazily);
    tcase_add_test(tc, test_perf_trace_explicit_lifecycle_and_final_drain);
    tcase_add_test(tc, test_perf_trace_refuses_existing_symlink);
    tcase_add_test(tc, test_perf_trace_concurrent_thread_rings);
    tcase_add_test(tc, test_perf_trace_post_fork_initialization);
    tcase_add_test(tc, test_perf_trace_sink_write_failure_is_visible);
    tcase_add_test(tc, test_perf_trace_ring_fifo_bytes);
    tcase_add_test(tc, test_perf_trace_ring_wraps_without_copy);
    tcase_add_test(tc, test_perf_trace_ring_counts_whole_record_drops);
    tcase_add_test(tc, test_perf_trace_ring_rejects_null);

    return s;
}
