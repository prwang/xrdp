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
 * Schema tests for the dedicated performance trace sink.
 *
 * The expected strings below are written from the SCHEMA DOCUMENTED IN
 * perf_trace.h ("<monotonic_ns> <tid> <tag> <a> <b>"), not read off a
 * run of the implementation. An analyzer parses these records by
 * position, so the field order and separator are a contract: if a
 * change to perf_trace.c makes one of these fail, the analyzer is what
 * broke, and the assertion is what said so.
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <string.h>
#include <stdlib.h>
#include "perf_trace.h"
#include "test_common.h"

START_TEST(test_perf_trace_format_schema)
{
    char buf[128];
    int rv;

    rv = perf_trace_format(buf, (int)sizeof(buf), 1569856240672000LL,
                           140737488355328LL, "emit_beg", 42, 1, 0, 0, 0, 0);
    ck_assert_int_eq(rv, (int)strlen(buf));
    ck_assert_str_eq(buf,
                     "1569856240672000 140737488355328 emit_beg 42 1 "
                     "0 0 0 0\n");
}
END_TEST

/* the identity field must survive being negative: an id is -1 when the
 * batch header could not be peeked, and an analyzer has to be able to
 * tell that apart from frame 1 */
START_TEST(test_perf_trace_format_negative_ids)
{
    char buf[128];

    perf_trace_format(buf, (int)sizeof(buf), 1, 2, "coll_end", -1, -1,
                      -1, -1, -1, -1);
    ck_assert_str_eq(buf, "1 2 coll_end -1 -1 -1 -1 -1 -1\n");
}
END_TEST

/* snprintf semantics: report what WOULD have been written, and never
 * write past the end. A truncated record must not be silently accepted
 * by a caller that only checks for a negative return. */
START_TEST(test_perf_trace_format_truncates)
{
    char buf[8];
    int rv;

    memset(buf, 'x', sizeof(buf));
    rv = perf_trace_format(buf, (int)sizeof(buf), 111111, 222222,
                           "pump_beg", 7, 8, 0, 0, 0, 0);
    ck_assert_int_gt(rv, (int)sizeof(buf));
    ck_assert_int_eq((int)strlen(buf), (int)sizeof(buf) - 1);
}
END_TEST

START_TEST(test_perf_trace_format_rejects_bad_args)
{
    char buf[64];

    ck_assert_int_eq(perf_trace_format(NULL, 64, 1, 2, "t", 0, 0,
                                       0, 0, 0, 0), -1);
    ck_assert_int_eq(perf_trace_format(buf, 0, 1, 2, "t", 0, 0,
                                       0, 0, 0, 0), -1);
    ck_assert_int_eq(perf_trace_format(buf, 64, 1, 2, NULL, 0, 0,
                                       0, 0, 0, 0), -1);
}
END_TEST

/* Disarmed is the SHIPPED state: with XRDP_PERF_TRACE unset the sink
 * must stay closed and recording must be a no-op that cannot crash.
 * (The test binary never sets the variable, so this also pins that no
 * other test in the suite arms it as a side effect.) */
START_TEST(test_perf_trace_disarmed_by_default)
{
    ck_assert_int_eq(perf_trace_on(), 0);
    perf_trace_ev("emit_beg", 1, 2);
    perf_trace_close();
    ck_assert_int_eq(perf_trace_on(), 0);
}
END_TEST

/* --- the SPSC ring (PRD FR-TRACE-1) ---------------------------------
 *
 * Every expected value below comes from the SPECIFICATION of a
 * head/tail ring that leaves one slot empty to tell full from empty:
 *
 *   - a ring of N slots holds N-1 records, never N;
 *   - it is FIFO;
 *   - the push that would make head meet tail stores NOTHING and
 *     increments ->dropped;
 *   - a pop frees exactly one slot, so a full ring accepts exactly one
 *     more push per pop;
 *   - popping an empty ring reports empty and does not invent a record.
 *
 * None of these numbers were obtained by running perf_trace.c. If the
 * ring is reimplemented (a different capacity convention, a count
 * field, a lock-free variant), these are the claims it must still
 * satisfy -- and the one that must NOT be quietly edited to agree with
 * it is the capacity: N-1 is a decision, not an accident. */

START_TEST(test_perf_trace_ring_fifo_order)
{
    struct perf_trace_ring *r;
    struct perf_trace_rec rec;
    int index;

    r = (struct perf_trace_ring *)calloc(1, sizeof(*r));
    ck_assert_ptr_ne(r, NULL);
    for (index = 0; index < 5; index++)
    {
        ck_assert_int_eq(perf_trace_ring_push(r, 100 + index, 7, "subm_beg",
                                              index, 0, 0, 0, 0, 0), 1);
    }
    for (index = 0; index < 5; index++)
    {
        ck_assert_int_eq(perf_trace_ring_pop(r, &rec), 1);
        ck_assert_int_eq((int)rec.ns, 100 + index);
        ck_assert_int_eq(rec.a, index);
        ck_assert_int_eq((int)rec.tid, 7);
        ck_assert_str_eq(rec.tag, "subm_beg");
    }
    ck_assert_int_eq(perf_trace_ring_pop(r, &rec), 0);
    ck_assert_uint_eq(r->dropped, 0);
    free(r);
}
END_TEST

/* Capacity is SLOTS-1, and the overflowing push must not store. */
START_TEST(test_perf_trace_ring_capacity_is_slots_minus_one)
{
    struct perf_trace_ring *r;
    struct perf_trace_rec rec;
    int index;

    r = (struct perf_trace_ring *)calloc(1, sizeof(*r));
    ck_assert_ptr_ne(r, NULL);
    for (index = 0; index < PERF_TRACE_RING_SLOTS - 1; index++)
    {
        ck_assert_int_eq(perf_trace_ring_push(r, index, 1, "pump_beg", index,
                                              0, 0, 0, 0, 0), 1);
    }
    /* the N-th push has nowhere to go */
    ck_assert_int_eq(perf_trace_ring_push(r, 999999, 1, "pump_beg", 999, 0, 0, 0, 0, 0),
                     0);
    ck_assert_uint_eq(r->dropped, 1);
    /* and it stored nothing: the head of the queue is still record 0 */
    ck_assert_int_eq(perf_trace_ring_pop(r, &rec), 1);
    ck_assert_int_eq(rec.a, 0);
    /* one pop freed exactly one slot */
    ck_assert_int_eq(perf_trace_ring_push(r, 12345, 1, "pump_end", 42, 0, 0, 0, 0, 0), 1);
    ck_assert_int_eq(perf_trace_ring_push(r, 12346, 1, "pump_end", 43, 0, 0, 0, 0, 0), 0);
    ck_assert_uint_eq(r->dropped, 2);
    free(r);
}
END_TEST

/* Drops are COUNTED, not silent: a full ring keeps counting every
 * rejected push, which is what lets an analysis tell a complete trace
 * from a truncated one. */
START_TEST(test_perf_trace_ring_counts_every_drop)
{
    struct perf_trace_ring *r;
    int index;

    r = (struct perf_trace_ring *)calloc(1, sizeof(*r));
    ck_assert_ptr_ne(r, NULL);
    for (index = 0; index < PERF_TRACE_RING_SLOTS - 1; index++)
    {
        perf_trace_ring_push(r, index, 1, "coll_beg", 0, 0, 0, 0, 0, 0);
    }
    for (index = 0; index < 100; index++)
    {
        ck_assert_int_eq(perf_trace_ring_push(r, index, 1, "coll_beg", 0, 0, 0, 0, 0, 0),
                         0);
    }
    ck_assert_uint_eq(r->dropped, 100);
    free(r);
}
END_TEST

/* The index wraps rather than growing: pushing and popping far more
 * records than the ring holds must work, and must never drop. */
START_TEST(test_perf_trace_ring_wraps_without_dropping)
{
    struct perf_trace_ring *r;
    struct perf_trace_rec rec;
    int index;

    r = (struct perf_trace_ring *)calloc(1, sizeof(*r));
    ck_assert_ptr_ne(r, NULL);
    for (index = 0; index < PERF_TRACE_RING_SLOTS * 3; index++)
    {
        ck_assert_int_eq(perf_trace_ring_push(r, index, 1, "emit_beg", index,
                                              0, 0, 0, 0, 0), 1);
        ck_assert_int_eq(perf_trace_ring_pop(r, &rec), 1);
        ck_assert_int_eq(rec.a, index);
    }
    ck_assert_uint_eq(r->dropped, 0);
    ck_assert_int_eq(perf_trace_ring_pop(r, &rec), 0);
    free(r);
}
END_TEST

/* A NULL ring or a NULL out is refused, not dereferenced: the source
 * path calls push with whatever perf_trace_my_ring() returned, and that
 * is NULL when the pool is exhausted. */
START_TEST(test_perf_trace_ring_rejects_null)
{
    struct perf_trace_rec rec;
    struct perf_trace_ring *r;

    ck_assert_int_eq(perf_trace_ring_push(NULL, 1, 1, "x", 0, 0, 0, 0, 0, 0), 0);
    ck_assert_int_eq(perf_trace_ring_pop(NULL, &rec), 0);
    r = (struct perf_trace_ring *)calloc(1, sizeof(*r));
    ck_assert_ptr_ne(r, NULL);
    ck_assert_int_eq(perf_trace_ring_pop(r, NULL), 0);
    free(r);
}
END_TEST


/* The payload is SIX fields (BACKLOG #61h). Every one of them must
 * survive the ring and reach the formatted record: the per-frame
 * records that moved off log.c carry up to six -- a GFX send is
 * (bytes, last, frame_id, id_server, id_client, fif) -- and a field
 * silently dropped in the middle of that tuple would mis-key an
 * analysis rather than fail it. */
START_TEST(test_perf_trace_six_payload_fields_round_trip)
{
    struct perf_trace_ring *r = calloc(1, sizeof(*r));
    struct perf_trace_rec got;
    char buf[128];

    ck_assert_ptr_ne(r, NULL);
    ck_assert_int_eq(perf_trace_ring_push(r, 42, 7, "send",
                                          1930000, 0, 288, 288, 286, 2), 1);
    ck_assert_int_eq(perf_trace_ring_pop(r, &got), 1);
    ck_assert_int_eq(got.a, 1930000);
    ck_assert_int_eq(got.b, 0);
    ck_assert_int_eq(got.c, 288);
    ck_assert_int_eq(got.d, 288);
    ck_assert_int_eq(got.e, 286);
    ck_assert_int_eq(got.f, 2);
    perf_trace_format(buf, (int)sizeof(buf), got.ns, got.tid, got.tag,
                      got.a, got.b, got.c, got.d, got.e, got.f);
    ck_assert_str_eq(buf, "42 7 send 1930000 0 288 288 286 2\n");
    free(r);
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
    tcase_add_test(tc, test_perf_trace_format_schema);
    tcase_add_test(tc, test_perf_trace_format_negative_ids);
    tcase_add_test(tc, test_perf_trace_format_truncates);
    tcase_add_test(tc, test_perf_trace_format_rejects_bad_args);
    tcase_add_test(tc, test_perf_trace_disarmed_by_default);
    tcase_add_test(tc, test_perf_trace_ring_fifo_order);
    tcase_add_test(tc, test_perf_trace_ring_capacity_is_slots_minus_one);
    tcase_add_test(tc, test_perf_trace_ring_counts_every_drop);
    tcase_add_test(tc, test_perf_trace_ring_wraps_without_dropping);
    tcase_add_test(tc, test_perf_trace_ring_rejects_null);
    tcase_add_test(tc, test_perf_trace_six_payload_fields_round_trip);

    return s;
}
