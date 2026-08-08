/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2024
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
 * BACKLOG #92 / PRD FR-H264-9 -- when is the aux (chroma) view due?
 *
 * WHAT THESE ASSERTIONS DEFEND, because a test is only worth its claim.
 *
 * The feature drops the AVC444 aux view while the screen is moving and
 * sends it when the screen settles: the aux view is 44.8 % of the bytes
 * and a whole second pack and encode, and chroma detail is least
 * perceptible in motion. The danger is that "in motion" becomes a
 * global judgement that starves quiet regions: one animating window in
 * a corner keeps the whole pipeline busy, and a static document beside
 * it would never receive 4:4:4 text for as long as the animation runs.
 *
 * So the design has a GUARANTEE, and it is the guarantee -- not the
 * motion signal -- that these tests are mostly about:
 *
 *   chroma detail is restored at least every chroma_refresh_ms,
 *   whatever the screen is doing.
 *
 * EVERY EXPECTED VALUE BELOW IS DERIVED FROM THE OWNER'S STATED
 * REQUIREMENT (2026-08-08), NOT FROM RUNNING THE CODE. The requirement
 * as given: "a bound of 1000 ms and an aux idle threshold of 100 ms --
 * that means we capture the aux 100 ms after the pipeline is idle, so
 * main stays whatever fps it needs and aux is clamped to < 10 fps."
 * The aux rates asserted here are computed from those two sentences by
 * hand; where a rate appears, the arithmetic that produces it is
 * written beside it.
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <check.h>

#include "xrdp_encoder.h"
#include "test_xrdp.h"

/* the owner's configuration, named once so a reader can see that every
 * expectation below is derived from these two numbers */
#define REFRESH_MS 1000
#define IDLE_MS 100

/*****************************************************************************/
/* Drive a frame sequence with a constant inter-frame gap and count how
 * many frames carry the aux view. This mirrors what the encoder does
 * with the return value -- send aux, then remember when -- and nothing
 * else. */
static int
aux_frames_over(int refresh_ms, int idle_ms, int gap_ms, int duration_ms)
{
    long long now = 0;
    long long last_aux = -1;
    long long prev_frame = -1;
    int count = 0;

    while (now <= duration_ms)
    {
        if (xrdp_gfx_chroma_due(refresh_ms, idle_ms, now, last_aux,
                                prev_frame))
        {
            count++;
            last_aux = now;
        }
        prev_frame = now;
        now += gap_ms;
    }
    return count;
}

/*****************************************************************************/
/* The feature is OFF by default and OFF must be byte-for-byte today's
 * behaviour: the aux view accompanies every single frame. */
START_TEST(test_chroma_due_disabled_sends_aux_every_frame)
{
    /* 51 frames at 20 ms over one second, and every one carries aux */
    ck_assert_int_eq(aux_frames_over(0, IDLE_MS, 20, 1000), 51);
    /* the idle threshold is irrelevant when the feature is off */
    ck_assert_int_eq(aux_frames_over(0, 0, 20, 1000), 51);
    ck_assert_int_eq(xrdp_gfx_chroma_due(0, IDLE_MS, 5000, 4999, 4999), 1);
}
END_TEST

/*****************************************************************************/
/* THE GUARANTEE. Under continuous motion the settle never fires, so the
 * refresh bound is the only thing that can send chroma -- and it must,
 * on time, for ever.
 *
 * Derivation: frames every 20 ms (50 per second) is unbroken motion,
 * since 20 < IDLE_MS. Over 10 000 ms the aux view can therefore only be
 * sent by the 1000 ms guarantee: at t = 0 (nothing has carried chroma
 * yet) and then at 1000, 2000, ... 10000. That is 1 + 10 = 11. */
START_TEST(test_chroma_due_guarantee_holds_under_unbroken_motion)
{
    ck_assert_int_eq(aux_frames_over(REFRESH_MS, IDLE_MS, 20, 10000), 11);
    /* the same 1 per second at any motion rate: 100 fps changes nothing */
    ck_assert_int_eq(aux_frames_over(REFRESH_MS, IDLE_MS, 10, 10000), 11);
}
END_TEST

/*****************************************************************************/
/* THE STARVATION CASE, stated as its own test because it is the reason
 * the guarantee exists. However long the animation runs, the gap
 * between chroma frames never exceeds the bound. */
START_TEST(test_chroma_due_gap_never_exceeds_the_bound)
{
    long long now = 0;
    long long last_aux = -1;
    long long prev_frame = -1;
    long long prev_aux = -1;
    long long worst = 0;

    /* 60 seconds of unbroken 50 fps motion -- a spinning cube in the
     * corner of an otherwise still desktop */
    while (now <= 60000)
    {
        if (xrdp_gfx_chroma_due(REFRESH_MS, IDLE_MS, now, last_aux,
                                prev_frame))
        {
            if (prev_aux >= 0 && now - prev_aux > worst)
            {
                worst = now - prev_aux;
            }
            prev_aux = now;
            last_aux = now;
        }
        prev_frame = now;
        now += 20;
    }
    ck_assert_int_eq((int)worst, REFRESH_MS);
    /* and chroma really did keep coming: 60 s / 1000 ms, plus the
     * bootstrap frame at t = 0 */
    ck_assert_int_eq((int)(prev_aux / REFRESH_MS), 60);
}
END_TEST

/*****************************************************************************/
/* THE SAME PROPERTY WITH A FRAME GAP THAT DOES NOT DIVIDE THE BOUND,
 * and it is a different number.
 *
 * ADDED 2026-08-08, announced as its own change, after the first fleet
 * run measured a worst chroma gap of 1022 ms against a configured 1000
 * (capture i92_sparse_aux_ab_20260808_211023_s20). The test above did
 * not and could not catch that: it drives frames exactly 20 ms apart,
 * 20 divides 1000, so a frame lands EXACTLY on the bound and the
 * overshoot is unreachable in that fixture. Every assertion in it is
 * correct and derived from the specification -- the fixture chose a
 * frame gap that hides the effect. A test can be right in every line
 * and still be unable to fail.
 *
 * WHAT THE GUARANTEE ACTUALLY PROMISES, per the owner's ruling of
 * 2026-08-08 (option 1: state the achievable bound rather than predict
 * the next frame's arrival, which would be the first heuristic in a
 * decision whose selling point is that it has none):
 *
 *   the FIRST FRAME AT OR AFTER refresh_ms carries chroma
 *
 * so the gap between chroma frames can reach refresh_ms plus one frame
 * interval, and no further. The expected values below are derived from
 * the two trigger rules by hand, not read off the implementation, and
 * there are TWO regimes because there are two triggers:
 *
 *   frames FASTER than idle_ms   nothing ever settles, so only the
 *                                guarantee sends chroma. Chroma last
 *                                went at time T, a multiple of GAP;
 *                                the next one is the first multiple of
 *                                GAP at or after T + REFRESH_MS. So the
 *                                gap is REFRESH_MS ROUNDED UP to a
 *                                multiple of GAP.
 *   frames SLOWER than idle_ms   every frame is already a settle, so
 *                                every frame carries chroma and the gap
 *                                is simply GAP.
 *
 * The second regime is in this test because the first draft of it left
 * it out and the case at GAP = 300 went red: 1200 expected against 300
 * measured. The rule was incomplete, not the code -- exactly the kind
 * of thing a fixture with only one frame rate cannot show.
 */
START_TEST(test_chroma_due_bound_is_refresh_plus_one_frame)
{
    /* frame gaps chosen so NONE of them divides 1000 -- which is the
     * whole point of this case -- and spanning both regimes: 3..70 are
     * faster than the 100 ms settle threshold, 300 is slower */
    static const int gaps[] = { 3, 7, 24, 30, 70, 300 };
    unsigned int g;

    for (g = 0; g < sizeof(gaps) / sizeof(gaps[0]); g++)
    {
        int gap = gaps[g];
        /* by hand, from the two rules above */
        int expect = (gap >= IDLE_MS)
                     ? gap
                     : ((REFRESH_MS + gap - 1) / gap) * gap;
        long long now = 0;
        long long last_aux = -1;
        long long prev_frame = -1;
        long long prev_aux = -1;
        long long worst = 0;

        while (now <= 60000)
        {
            if (xrdp_gfx_chroma_due(REFRESH_MS, IDLE_MS, now, last_aux,
                                    prev_frame))
            {
                if (prev_aux >= 0 && now - prev_aux > worst)
                {
                    worst = now - prev_aux;
                }
                prev_aux = now;
                last_aux = now;
            }
            prev_frame = now;
            now += gap;
        }
        ck_assert_int_eq((int)worst, expect);
        /* the bound holds in BOTH regimes */
        ck_assert_int_le((int)worst, REFRESH_MS + gap);
        /* and in the motion regime it is strictly ABOVE REFRESH_MS,
         * which is the whole finding: "at least every refresh_ms" was
         * never what the mechanism could deliver */
        if (gap < IDLE_MS && REFRESH_MS % gap != 0)
        {
            ck_assert_int_gt((int)worst, REFRESH_MS);
        }
    }
    /* worked examples, so a reader can check the rule without running
     * anything: at 24 ms frames the worst gap is 1008 (42 frames), and
     * at 70 ms it is 1050 (15 frames) */
    ck_assert_int_eq(((REFRESH_MS + 23) / 24) * 24, 1008);
    ck_assert_int_eq(((REFRESH_MS + 69) / 70) * 70, 1050);
}
END_TEST

/*****************************************************************************/
/* THE SETTLE, and the rate clamp that follows from it.
 *
 * Derivation from the requirement: aux is sent when the pipeline has
 * been quiet for IDLE_MS, so a stream of frames exactly IDLE_MS apart
 * settles on every frame -- 100 ms apart is 10 per second, which is the
 * "< 10 fps" clamp the owner asked for, met exactly at the boundary.
 * Over 1000 ms inclusive that is frames at 0, 100, ... 1000 = 11. */
START_TEST(test_chroma_due_settle_clamps_aux_to_the_idle_interval)
{
    ck_assert_int_eq(aux_frames_over(REFRESH_MS, IDLE_MS, 100, 1000), 11);

    /* one millisecond faster than the threshold is still motion, so
     * only the guarantee fires: t = 0 and t = 1000 -- 99 ms frames put
     * a frame at 990 and the next at 1089, so within 0..1000 the
     * guarantee can only fire once more, at 990 + 0 -> the frame at or
     * after 1000 is outside the window. Count = 1. */
    ck_assert_int_eq(aux_frames_over(REFRESH_MS, IDLE_MS, 99, 1000), 1);

    /* comfortably settled: 200 ms apart is 5 frames per second and all
     * of them settle, 0..1000 inclusive = 6 */
    ck_assert_int_eq(aux_frames_over(REFRESH_MS, IDLE_MS, 200, 1000), 6);
}
END_TEST

/*****************************************************************************/
/* The aux view can never be sent MORE often than the idle interval,
 * which is the other half of the clamp: whatever the frame rate, the
 * number of chroma frames per second is at most 1000/IDLE_MS + 1. */
START_TEST(test_chroma_due_aux_rate_is_bounded_by_the_idle_interval)
{
    int gap;

    for (gap = 1; gap <= 500; gap++)
    {
        int n = aux_frames_over(REFRESH_MS, IDLE_MS, gap, 1000);
        /* 1000/100 = 10 chroma frames in a second, plus the one at
         * t = 0, is the most the design permits */
        ck_assert_int_le(n, 1000 / IDLE_MS + 1);
    }
}
END_TEST

/*****************************************************************************/
/* With no idle threshold the guarantee is the only trigger, which is a
 * legal configuration: chroma exactly on the bound and never between. */
START_TEST(test_chroma_due_without_idle_only_the_guarantee_fires)
{
    /* 20 ms frames over 5000 ms: t = 0 and 1000..5000 = 6 */
    ck_assert_int_eq(aux_frames_over(REFRESH_MS, 0, 20, 5000), 6);
    /* even a long quiet gap does not trigger a settle when idle is off,
     * unless the bound has expired anyway */
    ck_assert_int_eq(xrdp_gfx_chroma_due(REFRESH_MS, 0, 500, 400, 100), 0);
    ck_assert_int_eq(xrdp_gfx_chroma_due(REFRESH_MS, 0, 1500, 400, 100), 1);
}
END_TEST

/*****************************************************************************/
/* Boundary and bootstrap conditions, spelled out so a future change
 * cannot quietly move them: the first frame of a session always carries
 * chroma, and both triggers are ">=", not ">". */
START_TEST(test_chroma_due_bootstrap_and_boundaries)
{
    /* nothing has carried chroma yet */
    ck_assert_int_eq(xrdp_gfx_chroma_due(REFRESH_MS, IDLE_MS, 0, -1, -1), 1);
    /* first frame after a chroma frame, no previous frame recorded */
    ck_assert_int_eq(xrdp_gfx_chroma_due(REFRESH_MS, IDLE_MS, 10, 0, -1), 1);
    /* the guarantee fires AT the bound, not one past it */
    ck_assert_int_eq(xrdp_gfx_chroma_due(REFRESH_MS, IDLE_MS,
                                         1000, 0, 990), 1);
    ck_assert_int_eq(xrdp_gfx_chroma_due(REFRESH_MS, IDLE_MS,
                                         999, 0, 990), 0);
    /* the settle fires AT the threshold, not one past it */
    ck_assert_int_eq(xrdp_gfx_chroma_due(REFRESH_MS, IDLE_MS,
                                         200, 150, 100), 1);
    ck_assert_int_eq(xrdp_gfx_chroma_due(REFRESH_MS, IDLE_MS,
                                         199, 150, 100), 0);
}
END_TEST

/*****************************************************************************/
Suite *
make_suite_avc444_chroma_due(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444ChromaDue");

    tc = tcase_create("chroma_due");
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_chroma_due_disabled_sends_aux_every_frame);
    tcase_add_test(tc, test_chroma_due_guarantee_holds_under_unbroken_motion);
    tcase_add_test(tc, test_chroma_due_gap_never_exceeds_the_bound);
    tcase_add_test(tc, test_chroma_due_bound_is_refresh_plus_one_frame);
    tcase_add_test(tc, test_chroma_due_settle_clamps_aux_to_the_idle_interval);
    tcase_add_test(tc, test_chroma_due_aux_rate_is_bounded_by_the_idle_interval);
    tcase_add_test(tc, test_chroma_due_without_idle_only_the_guarantee_fires);
    tcase_add_test(tc, test_chroma_due_bootstrap_and_boundaries);

    return s;
}
