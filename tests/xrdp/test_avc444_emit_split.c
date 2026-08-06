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
 * BACKLOG #70B / PRD FR-ACK-2 -- the EGFX assembly split.
 *
 * The expected values below come from the WRITTEN CONTRACT of each
 * helper (its doc comment in xrdp_encoder.h, which restates FR-ACK-2),
 * not from running the implementation.
 *
 * Both mutations were checked against this suite rather than asserted
 * to be caught:
 *
 *   - "was this monitor armed" written as (seq != 0) instead of the
 *     tri-state. REAL BUG, and it is caught twice: sequence 0 is the
 *     first submit of a session, so that frame silently un-arms, and
 *     an un-armed monitor inherits a stale sequence.
 *   - "may I encode inline" written as (have <= 0) instead of
 *     (have == 0). NOT a behaviour change, because the have == -1 case
 *     returns on the failed-pair branch before this predicate is
 *     reached -- the mutation is caught only by the test that pins
 *     WHICH code owns that decision. Recorded here because the first
 *     draft of this file claimed the <= 0 form would blank every
 *     successfully collected frame, and it would not.
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <string.h>
#include "xrdp.h"
#include "xrdp_encoder.h"
#include "test_xrdp.h"

/*
 * gfx_emit_may_encode_inline
 *
 * Contract (xrdp_encoder.h): with the split ON, a monitor the submit
 * pass did not arm may NOT be encoded inline, because that would mean a
 * second thread creating and driving an ffmpeg child the worker owns.
 * Nothing else changes.
 */

/* Split off is the shipped configuration: the predicate must be
 * transparent for every arm state, or the knob is not a knob. */
START_TEST(test_emit_inline_allowed_when_split_off)
{
    ck_assert_int_ne(gfx_emit_may_encode_inline(0, 0), 0);
    ck_assert_int_ne(gfx_emit_may_encode_inline(0, 1), 0);
    ck_assert_int_ne(gfx_emit_may_encode_inline(0, -1), 0);
}
END_TEST

/* have == 0 with the split on is THE case the predicate exists for */
START_TEST(test_emit_inline_refused_for_unarmed_monitor)
{
    ck_assert_int_eq(gfx_emit_may_encode_inline(1, 0), 0);
}
END_TEST

/* have == 1 means the pair is already collected and the emit pass has
 * no child to touch. Refusing it would ship nothing for every frame the
 * pipeline successfully encoded -- the loudest possible failure, and
 * the one worth pinning even though no plausible mutation reaches it. */
START_TEST(test_emit_ready_pair_is_not_refused)
{
    ck_assert_int_ne(gfx_emit_may_encode_inline(1, 1), 0);
}
END_TEST

/* have == -1 already ships nothing on its own path (the failed-pair
 * branch), so this predicate must not be the thing that decides it --
 * two owners of one decision is how the two disagree later. */
START_TEST(test_emit_failed_pair_is_not_this_predicates_business)
{
    ck_assert_int_ne(gfx_emit_may_encode_inline(1, -1), 0);
}
END_TEST

/*
 * gfx_emit_slot_fill
 *
 * Contract: returns set_n on success and copies set/set_mon in order;
 * returns 0 and leaves an EMPTY slot on any unusable argument. A set
 * larger than the slot is REFUSED, never truncated -- a truncated set
 * leaves items with no owner and no ack.
 */

START_TEST(test_slot_fill_copies_in_order)
{
    struct xrdp_encoder_emit_slot slot;
    struct xrdp_enc_data *set[3];
    int set_mon[3] = { 1, 0, -1 };
    int rv;

    memset(&slot, 0, sizeof(slot));
    /* the slot only stores the pointers; it never dereferences them */
    set[0] = (struct xrdp_enc_data *)0x1000;
    set[1] = (struct xrdp_enc_data *)0x2000;
    set[2] = (struct xrdp_enc_data *)0x3000;

    rv = gfx_emit_slot_fill(&slot, set, set_mon, 3);

    ck_assert_int_eq(rv, 3);
    ck_assert_int_eq(slot.set_n, 3);
    ck_assert_ptr_eq(slot.set[0], set[0]);
    ck_assert_ptr_eq(slot.set[1], set[1]);
    ck_assert_ptr_eq(slot.set[2], set[2]);
    ck_assert_int_eq(slot.set_mon[0], 1);
    ck_assert_int_eq(slot.set_mon[1], 0);
    /* -1 ("not the batchable shape") must survive the handoff: the emit
     * pass distinguishes it from monitor 0 */
    ck_assert_int_eq(slot.set_mon[2], -1);
}
END_TEST

/* Refused, not truncated. If this ever returns a positive number the
 * dispatch will hand over a partial frame and the items past the bound
 * are leaked with no terminal ack. */
START_TEST(test_slot_fill_refuses_oversized_set)
{
    struct xrdp_encoder_emit_slot slot;
    struct xrdp_enc_data *set[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS + 1];
    int set_mon[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS + 1];
    int index;

    memset(&slot, 0, sizeof(slot));
    for (index = 0; index < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS + 1;
            index++)
    {
        set[index] = (struct xrdp_enc_data *)(long)(0x1000 + index);
        set_mon[index] = index;
    }

    ck_assert_int_eq(gfx_emit_slot_fill(&slot, set, set_mon,
                                        CLIENT_MONITOR_DATA_MAXIMUM_MONITORS
                                        + 1), 0);
    ck_assert_int_eq(slot.set_n, 0);
}
END_TEST

START_TEST(test_slot_fill_rejects_bad_args)
{
    struct xrdp_encoder_emit_slot slot;
    struct xrdp_enc_data *set[2];
    int set_mon[2] = { 0, 1 };

    memset(&slot, 0, sizeof(slot));
    set[0] = (struct xrdp_enc_data *)0x1000;
    set[1] = NULL;

    ck_assert_int_eq(gfx_emit_slot_fill(NULL, set, set_mon, 2), 0);
    ck_assert_int_eq(gfx_emit_slot_fill(&slot, NULL, set_mon, 2), 0);
    ck_assert_int_eq(gfx_emit_slot_fill(&slot, set, NULL, 2), 0);
    ck_assert_int_eq(gfx_emit_slot_fill(&slot, set, set_mon, 0), 0);
    /* a NULL item mid-set: refuse the whole set rather than hand over a
     * prefix the assembler would walk off the end of */
    ck_assert_int_eq(gfx_emit_slot_fill(&slot, set, set_mon, 2), 0);
    ck_assert_int_eq(slot.set_n, 0);
}
END_TEST

/*
 * gfx_batch_publish
 *
 * Contract: runs after the join and is the ONLY writer of the arm state
 * the emit pass reads. sub_state is a tri-state precisely so that
 * sequence 0 is a real sequence.
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
 * an arm from the previous one. Clearing it is what USED to happen
 * after the emit pass on the worker; with the split it happens here,
 * after the join, and that move is the whole reason this function
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
 * this monitor"), not as 0 ("encode it inline") -- with the split on,
 * 0 is refused and the frame is dropped with a warning instead of
 * being acked as a known failure. */
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
 * cycle, but the flag the EMIT pass reads may only be raised after the
 * join, which is here. Publishing it is what makes the next frame for
 * that monitor repaint the whole surface from the fresh IDR (#48). */
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

    tc = tcase_create("emit_inline_policy");
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_emit_inline_allowed_when_split_off);
    tcase_add_test(tc, test_emit_inline_refused_for_unarmed_monitor);
    tcase_add_test(tc, test_emit_ready_pair_is_not_refused);
    tcase_add_test(tc, test_emit_failed_pair_is_not_this_predicates_business);

    tc = tcase_create("emit_slot");
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_slot_fill_copies_in_order);
    tcase_add_test(tc, test_slot_fill_refuses_oversized_set);
    tcase_add_test(tc, test_slot_fill_rejects_bad_args);

    tc = tcase_create("batch_publish");
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_publish_arms_sequence_zero);
    tcase_add_test(tc, test_publish_leaves_untouched_monitor_alone);
    tcase_add_test(tc, test_publish_clears_stale_arm_state);
    tcase_add_test(tc, test_publish_marks_submit_failure);
    tcase_add_test(tc, test_publish_arms_surface_reset_after_teardown);

    return s;
}
