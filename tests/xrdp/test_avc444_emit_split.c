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
 * BACKLOG #70B -- the EGFX assembly split: assembly separated from the
 * encode path, so that it reads only what collect snapshotted.
 *
 * The expected values below come from the WRITTEN CONTRACT of
 * gfx_batch_publish (its doc comment in xrdp_encoder.h), not from
 * running the implementation.
 *
 * This mutation was checked against this suite rather than asserted to
 * be caught:
 *
 *   - "was this monitor armed" written as (seq != 0) instead of the
 *     tri-state. REAL BUG, and it is caught twice: sequence 0 is the
 *     first submit of a session, so that frame silently un-arms, and
 *     an un-armed monitor inherits a stale sequence.
 *
 * BACKLOG #100 (2026-08-07) removed the assembly THREAD, which this
 * file also used to cover; the seven assertions that named the thread
 * or its hand-off slot went with it. What remains is the part that is
 * independent of which thread the work runs on.
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <string.h>
#include "xrdp.h"
#include "xrdp_encoder.h"
#include "test_xrdp.h"

/*
 * gfx_batch_publish
 *
 * Contract: the ONLY writer of the arm state the emit pass reads,
 * applied after the pump and before the collect. sub_state is a
 * tri-state precisely so that sequence 0 is a real sequence.
 */

/* The regression this exists for: sequence 0 is the first submit of a
 * session. A publish that tested the SEQUENCE for truthiness rather
 * than the state would leave that monitor un-armed, and the very first
 * frame of every session would ship nothing. */
START_TEST(test_publish_arms_sequence_zero)
{
    struct xrdp_encoder enc;
    unsigned long long sub_seq[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_state[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_reset[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];

    memset(&enc, 0, sizeof(enc));
    memset(sub_seq, 0, sizeof(sub_seq));
    memset(sub_state, 0, sizeof(sub_state));
    memset(sub_reset, 0, sizeof(sub_reset));
    /* monitor 0 was armed, and it drew sequence 0 */
    enc.avc444_batch_seq[0] = 9999;
    sub_seq[0] = 0;
    sub_state[0] = 1;

    gfx_batch_publish(&enc, sub_seq, sub_state, sub_reset);

    ck_assert_uint_eq((unsigned int)enc.avc444_batch_seq[0], 0);
}
END_TEST

/* An un-armed monitor must not inherit the previous cycle's sequence:
 * a stale seq would make collect wait for a pair that was never
 * submitted. */
START_TEST(test_publish_leaves_untouched_monitor_alone)
{
    struct xrdp_encoder enc;
    unsigned long long sub_seq[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_state[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_reset[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];

    memset(&enc, 0, sizeof(enc));
    memset(sub_seq, 0, sizeof(sub_seq));
    memset(sub_state, 0, sizeof(sub_state));
    memset(sub_reset, 0, sizeof(sub_reset));
    enc.avc444_batch_seq[1] = 42;
    sub_seq[1] = 7;          /* stale value in the caller's local */
    sub_state[1] = 0;        /* ... but the monitor was not armed */

    gfx_batch_publish(&enc, sub_seq, sub_state, sub_reset);

    ck_assert_uint_eq((unsigned int)enc.avc444_batch_seq[1], 42);
}
END_TEST

/* have[] carries no state across a cycle: the emit pass must never see
 * an arm from the previous one. The publish is one of the two places
 * it is cleared (the other is the end of the emit pass), and being the
 * single writer of that state is the whole reason this function
 * exists. */
START_TEST(test_publish_clears_stale_arm_state)
{
    struct xrdp_encoder enc;
    unsigned long long sub_seq[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_state[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_reset[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];

    memset(&enc, 0, sizeof(enc));
    memset(sub_seq, 0, sizeof(sub_seq));
    memset(sub_state, 0, sizeof(sub_state));
    memset(sub_reset, 0, sizeof(sub_reset));
    enc.avc444_batch_have[0] = 1;    /* last cycle's collected pair */
    enc.avc444_batch_have[2] = -1;   /* last cycle's failure        */

    gfx_batch_publish(&enc, sub_seq, sub_state, sub_reset);

    ck_assert_int_eq(enc.avc444_batch_have[0], 0);
    ck_assert_int_eq(enc.avc444_batch_have[2], 0);
}
END_TEST

/* A submit failure must reach the emit pass as -1 ("ship nothing for
 * this monitor"), not as 0 ("encode it synchronously here"). Losing the
 * -1 would make the emit pass re-encode, on the spot, a monitor whose
 * submit has just failed in this very cycle. */
START_TEST(test_publish_marks_submit_failure)
{
    struct xrdp_encoder enc;
    unsigned long long sub_seq[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_state[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_reset[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];

    memset(&enc, 0, sizeof(enc));
    memset(sub_seq, 0, sizeof(sub_seq));
    memset(sub_state, 0, sizeof(sub_state));
    memset(sub_reset, 0, sizeof(sub_reset));
    sub_state[3] = -1;

    gfx_batch_publish(&enc, sub_seq, sub_state, sub_reset);

    ck_assert_int_eq(enc.avc444_batch_have[3], -1);
}
END_TEST

/* The deferred re-key: the worker tears the child down at the top of a
 * cycle, and the flag the EMIT pass reads is raised by the publish.
 * Publishing it is what makes the next frame for that monitor repaint
 * the whole surface from the fresh IDR (#48). */
START_TEST(test_publish_arms_surface_reset_after_teardown)
{
    struct xrdp_encoder enc;
    unsigned long long sub_seq[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_state[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int sub_reset[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];

    memset(&enc, 0, sizeof(enc));
    memset(sub_seq, 0, sizeof(sub_seq));
    memset(sub_state, 0, sizeof(sub_state));
    memset(sub_reset, 0, sizeof(sub_reset));
    sub_reset[1] = 1;

    gfx_batch_publish(&enc, sub_seq, sub_state, sub_reset);

    ck_assert_int_eq(enc.avc444_surface_reset_pending[1], 1);
    /* and only for the monitor that was torn down */
    ck_assert_int_eq(enc.avc444_surface_reset_pending[0], 0);
}
END_TEST

Suite *
make_suite_avc444_emit_split(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444EmitSplit");

    tc = tcase_create("batch_publish");
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_publish_arms_sequence_zero);
    tcase_add_test(tc, test_publish_leaves_untouched_monitor_alone);
    tcase_add_test(tc, test_publish_clears_stale_arm_state);
    tcase_add_test(tc, test_publish_marks_submit_failure);
    tcase_add_test(tc, test_publish_arms_surface_reset_after_teardown);

    return s;
}
