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
#include <string.h>

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

START_TEST(test_perf_trace_disarmed_by_default)
{
    ck_assert_int_eq(perf_trace_on(), 0);
    perf_trace_ev("event=emit_beg frame_id=%d monitor=%d", 1, 2);
    perf_trace_close();
    ck_assert_int_eq(perf_trace_on(), 0);
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
    tcase_add_test(tc, test_perf_trace_disarmed_by_default);
    tcase_add_test(tc, test_perf_trace_ring_fifo_bytes);
    tcase_add_test(tc, test_perf_trace_ring_wraps_without_copy);
    tcase_add_test(tc, test_perf_trace_ring_counts_whole_record_drops);
    tcase_add_test(tc, test_perf_trace_ring_rejects_null);

    return s;
}
