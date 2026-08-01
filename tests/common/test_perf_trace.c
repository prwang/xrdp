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
#include "perf_trace.h"
#include "test_common.h"

START_TEST(test_perf_trace_format_schema)
{
    char buf[128];
    int rv;

    rv = perf_trace_format(buf, (int)sizeof(buf), 1569856240672000LL,
                           140737488355328LL, "emit_beg", 42, 1);
    ck_assert_int_eq(rv, (int)strlen(buf));
    ck_assert_str_eq(buf, "1569856240672000 140737488355328 emit_beg 42 1\n");
}
END_TEST

/* the identity field must survive being negative: an id is -1 when the
 * batch header could not be peeked, and an analyzer has to be able to
 * tell that apart from frame 1 */
START_TEST(test_perf_trace_format_negative_ids)
{
    char buf[128];

    perf_trace_format(buf, (int)sizeof(buf), 1, 2, "coll_end", -1, -1);
    ck_assert_str_eq(buf, "1 2 coll_end -1 -1\n");
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
                           "pump_beg", 7, 8);
    ck_assert_int_gt(rv, (int)sizeof(buf));
    ck_assert_int_eq((int)strlen(buf), (int)sizeof(buf) - 1);
}
END_TEST

START_TEST(test_perf_trace_format_rejects_bad_args)
{
    char buf[64];

    ck_assert_int_eq(perf_trace_format(NULL, 64, 1, 2, "t", 0, 0), -1);
    ck_assert_int_eq(perf_trace_format(buf, 0, 1, 2, "t", 0, 0), -1);
    ck_assert_int_eq(perf_trace_format(buf, 64, 1, 2, NULL, 0, 0), -1);
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

    return s;
}
