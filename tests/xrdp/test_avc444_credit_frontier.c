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
 * BACKLOG #80 / PRD FR-FLOW-1 -- the credit frontier.
 *
 * WHAT THIS FILE IS FOR, in one paragraph, because the assertions below
 * are only meaningful against the claim they are defending.
 *
 * xorgxrdp captures a frame only when xrdp has acked a frame id it may
 * overwrite. That ack is the single admission token for the entire
 * pipeline. Until BACKLOG #80 the whole emission of that token was
 * wrapped in one comparison against the CLIENT's acknowledgements
 * (xrdp_gfx_ack_window_open), so a client that fell behind stopped the
 * producer from capturing -- not because any pipeline stage was busy,
 * but because of a fact about the network several layers away. PRD
 * FR-FLOW-1 clause 1 forbids that: a lossless stall may consult only the
 * immediately adjacent stage. The client's window survives as clause 2's
 * end-to-end guard, but applied where refusing a frame is free: at
 * capture admission, where xorgxrdp coalesces the damage instead of
 * queueing it.
 *
 * WHERE THE EXPECTED VALUES COME FROM. Every number below is derived
 * from FR-FLOW-1's text -- the three terms of the credit and the
 * resulting wire bound -- or, for the replay, computed by hand from
 * those terms against an event ORDER measured on the wire. None is read
 * off the implementation. The two are the same arithmetic here, which is
 * exactly why the enumeration matters more than the arithmetic: it tests
 * the WIRING (which state feeds which term, which frontier gates which
 * ack, what the joint machine with xorgxrdp does) and that is where a
 * regression would actually live.
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdlib.h>
#include <string.h>
#include "arch.h"
#include "xrdp_encoder.h"
#include "test_xrdp.h"

/*
 * xorgxrdp's per-monitor capture budget -- how far above the credit a
 * capture may be admitted. XRDP_GFX_CAPTURE_SLOTS mirrors the producer's
 * XUP_CAP_AVC444_SLOT_COUNT; the model uses the same number the wire
 * bound is written in terms of.
 */
#define MODEL_SLOTS XRDP_GFX_CAPTURE_SLOTS

/****************************************************************************/
/*
 * FR-FLOW-1 clause 3 gives the credit three terms and takes the
 * smallest. Each case here puts exactly one of them in the binding
 * position and reads the expected value out of that term's definition.
 */
START_TEST(test_credit_frontier_each_term_binds)
{
    /* SLOT FACT binds: the children have only drained up to 10, so no
     * argument about the pipeline or the network can free page 11. */
    ck_assert_int_eq(xrdp_gfx_credit_frontier(10, 20, 20, 4), 10);

    /* NEAREST-NEIGHBOUR binds: one frame of pipeline inventory is
     * allowed, so with 7 on the transport the credit stops at 8 even
     * though 30 frames have been absorbed and the client is current. */
    ck_assert_int_eq(xrdp_gfx_credit_frontier(30, 7, 7, 4), 8);

    /* WIRE WINDOW binds: the client is at 5 and C is 2, so the producer
     * may be told 7 and no more, whatever the pipeline has done. */
    ck_assert_int_eq(xrdp_gfx_credit_frontier(30, 20, 5, 2), 7);

    /* ties: all three agree */
    ck_assert_int_eq(xrdp_gfx_credit_frontier(9, 8, 8, 1), 9);
}
END_TEST

/****************************************************************************/
/*
 * The three inputs never decrease in a session (absorb, egress and the
 * client ack are all max()-applied at their sources), so the credit must
 * never decrease either. A frontier that walked backwards would tell
 * xorgxrdp to un-free a slot, which the producer would ignore -- and the
 * bug would then be invisible until pixels went missing.
 */
START_TEST(test_credit_frontier_is_monotone)
{
    int consumed;
    int server;
    int client;

    for (consumed = 0; consumed <= 6; ++consumed)
    {
        for (server = 0; server <= consumed; ++server)
        {
            for (client = 0; client <= server; ++client)
            {
                int here = xrdp_gfx_credit_frontier(consumed, server,
                                                    client, 2);
                /* advancing any ONE input by one -- which is exactly
                 * what an absorb, an egress or a client ack does --
                 * may never lower the credit */
                ck_assert_int_ge(xrdp_gfx_credit_frontier(consumed + 1,
                                                          server, client, 2),
                                 here);
                ck_assert_int_ge(xrdp_gfx_credit_frontier(consumed,
                                                          server + 1, client,
                                                          2), here);
                ck_assert_int_ge(xrdp_gfx_credit_frontier(consumed, server,
                                                          client + 1, 2),
                                 here);
                /* and it is never above any of the three terms */
                ck_assert_int_le(here, consumed);
                ck_assert_int_le(here, server + 1);
                ck_assert_int_le(here, client + 2);
            }
        }
    }
}
END_TEST

/****************************************************************************/
/*
 * The region-disposing ack is not flagged SLOT_ONLY, so on the producer
 * it moves the slot frontier as well: it is a second admission token and
 * must obey the same window, or the window is enforced on one ack and
 * bypassed on the other.
 */
START_TEST(test_region_ack_obeys_the_same_window)
{
    /* inside the window: the ack names everything on the transport */
    ck_assert_int_eq(xrdp_gfx_region_ack_target(9, 8, 2), 9);
    /* outside it: clamped to client + C, never to frame_id_server */
    ck_assert_int_eq(xrdp_gfx_region_ack_target(9, 5, 2), 7);
    /* C = 1, client current: the transport frontier itself */
    ck_assert_int_eq(xrdp_gfx_region_ack_target(4, 4, 1), 4);
}
END_TEST

/****************************************************************************/
/*
 * FR-FLOW-1 clause 3's structural claim: the region ack never lags the
 * credit by more than one id. That is what keeps the producer's held
 * regions inside its cap_sent ring, which is sized at capture slots + 1
 * (XUP_CAP_SENT_SLOTS, common/xup_client_info.h). If this ever fails,
 * clamping the region ack starts losing pixels rather than merely
 * delaying their release, and the clamp must be reconsidered.
 */
START_TEST(test_region_ack_never_lags_the_credit_by_more_than_one)
{
    int c;
    int consumed;
    int server;
    int client;

    for (c = 1; c <= 4; ++c)
    {
        for (consumed = 0; consumed <= 8; ++consumed)
        {
            for (server = 0; server <= consumed; ++server)
            {
                for (client = 0; client <= server; ++client)
                {
                    int credit = xrdp_gfx_credit_frontier(consumed, server,
                                                          client, c);
                    int region = xrdp_gfx_region_ack_target(server, client,
                                                            c);
                    ck_assert_int_le(credit - region, 1);
                    ck_assert_int_ge(credit, region);
                }
            }
        }
    }
}
END_TEST

/****************************************************************************/
/*
 * The contrast, stated against the shipped predicate rather than a copy
 * of it. xrdp_gfx_ack_window_open() is still in the tree (the legacy,
 * non-eager path is unchanged by #80), so the two policies can be asked
 * the same question side by side.
 *
 * The state is the one measured on 2026-08-02, arm x017, 40 ms injected
 * ack delay, at +0.000 ms of the wedge replayed further down: the
 * children have absorbed frame 407, frame 406 is on the transport, the
 * client has acknowledged 405, and frames_in_flight was 1.
 *
 * The gate is SHUT -- so the shipped code says nothing at all, and the
 * capture that frame 407's slot would have admitted waits. The frontier,
 * with C = 2, grants 407. The difference is not academic: in that
 * capture the credit the gate withheld here was finally emitted 112.3 ms
 * later, and the producer sat idle for 85.2 ms of it.
 */
START_TEST(test_the_shipped_gate_withholds_what_the_window_permits)
{
    /* frames_in_flight = 1: "the client must have acknowledged
     * everything sent" */
    ck_assert_int_eq(xrdp_gfx_ack_window_open(405, 406, 1), 0);
    /* and the credit the three FR-FLOW-1 terms permit in that state */
    ck_assert_int_eq(xrdp_gfx_credit_frontier(407, 406, 405, 2), 407);
    /* even at C = 1 -- the tightest window that admits anything at all
     * -- the frontier still frees the slot of a frame the client has
     * acknowledged, where the gate freezes everything */
    ck_assert_int_eq(xrdp_gfx_credit_frontier(407, 406, 405, 1), 406);
}
END_TEST

/****************************************************************************/
/*
 * The no-withholding property, as a property of the planner: after the
 * planner has run, the producer must already hold every id the three
 * terms permit. There is no state in which xrdp knows it may free a slot
 * and has not said so.
 *
 * This is the assertion the shipped gate fails, and it is checked here
 * over the whole lattice of states rather than at a chosen point.
 */
START_TEST(test_planner_never_withholds_an_earned_credit)
{
    int c;
    int consumed;
    int server;
    int client;
    int sent;

    for (c = 1; c <= 3; ++c)
    {
        for (consumed = 0; consumed <= 6; ++consumed)
        {
            for (server = 0; server <= consumed; ++server)
            {
                for (client = 0; client <= server; ++client)
                {
                    for (sent = 0; sent <= consumed; ++sent)
                    {
                        struct xrdp_gfx_ack_state st;
                        struct xrdp_gfx_ack_plan plan;
                        int held;
                        int want;

                        st.frame_id_consumed = consumed;
                        st.frame_id_server = server;
                        st.frame_id_client = client;
                        st.wire_window = c;
                        st.frame_id_region_sent = sent;
                        st.frame_id_server_sent = sent;
                        xrdp_gfx_plan_acks(&st, &plan);
                        held = sent;
                        if (plan.region > held)
                        {
                            held = plan.region;
                        }
                        if (plan.slot > held)
                        {
                            held = plan.slot;
                        }
                        want = xrdp_gfx_credit_frontier(consumed, server,
                                                        client, c);
                        /* the producer holds at least the earned credit
                         * -- never less */
                        if (want > sent)
                        {
                            ck_assert_int_ge(held, want);
                        }
                        /* and never MORE than the window allows */
                        ck_assert_int_le(plan.region, client + c);
                        ck_assert_int_le(plan.slot, client + c);
                    }
                }
            }
        }
    }
}
END_TEST

/****************************************************************************/
/*
 * The joint machine with xorgxrdp, enumerated exhaustively.
 *
 * State, all in frame ids:
 *   captured  highest id xorgxrdp has captured and sent
 *   consumed  highest id the encoder children have drained
 *   server    highest id handed to the transport
 *   client    highest id the client has acknowledged
 *   ack       highest ack value the producer has received (its
 *             rect_id_ack -- the admission frontier)
 *   region    highest region-disposing ack value sent
 *
 * Events, each enabled only by its own precondition:
 *   CAP     admit a capture, iff captured + 1 <= ack + MODEL_SLOTS
 *           (xorgxrdp's own budget, rdpClientConMonitorHasCapacity)
 *   ABSORB  the children drain the next captured frame
 *   EGRESS  the next absorbed frame reaches the transport
 *   CLIACK  the client acknowledges the next sent frame
 *
 * xrdp runs the planner after every event, exactly as the live code does
 * (note_frame_consumed, the enc_done path and the client-ack handler all
 * call xrdp_mm_update_module_frame_ack).
 *
 * Not modelled: the frame that produces no output (displayed = 0). Its
 * region-return ack is deliberately NOT clamped -- see the comment at
 * that call site in xrdp_mm.c -- so it lifts the admission ceiling by
 * one frame and the bound below is stated for frames that reached the
 * transport.
 */

#define ENUM_N 6            /* highest frame id in the enumeration */
#define ENUM_BASE (ENUM_N + 2)

struct model
{
    int captured;
    int consumed;
    int server;
    int client;
    int ack;
    int region;
};

static int
model_key(const struct model *m)
{
    return (((((m->captured * ENUM_BASE + m->consumed) * ENUM_BASE +
               m->server) * ENUM_BASE + m->client) * ENUM_BASE +
             m->ack) * ENUM_BASE + m->region);
}

/* run the planner and apply what it says to the producer's frontiers */
static void
model_plan(struct model *m, int c)
{
    struct xrdp_gfx_ack_state st;
    struct xrdp_gfx_ack_plan plan;

    st.frame_id_consumed = m->consumed;
    st.frame_id_server = m->server;
    st.frame_id_client = m->client;
    st.wire_window = c;
    st.frame_id_region_sent = m->region;
    st.frame_id_server_sent = m->ack;
    xrdp_gfx_plan_acks(&st, &plan);
    if (plan.region >= 0)
    {
        m->region = plan.region;
        if (plan.region > m->ack)
        {
            m->ack = plan.region;
        }
    }
    if (plan.slot >= 0)
    {
        m->ack = plan.slot;
    }
}

START_TEST(test_joint_machine_enumeration)
{
    int c;

    for (c = 1; c <= 3; ++c)
    {
        char *seen;
        struct model *stack;
        int top;
        int visited;
        int deepest;
        int tight_wire;
        int window_bound;
        struct model start;

        seen = (char *)calloc((size_t)ENUM_BASE * ENUM_BASE * ENUM_BASE *
                              ENUM_BASE * ENUM_BASE * ENUM_BASE, 1);
        ck_assert_ptr_ne(seen, NULL);
        stack = (struct model *)malloc(sizeof(struct model) * 65536);
        ck_assert_ptr_ne(stack, NULL);
        memset(&start, 0, sizeof(start));
        model_plan(&start, c);
        stack[0] = start;
        top = 1;
        seen[model_key(&start)] = 1;
        visited = 0;
        deepest = 0;
        tight_wire = 0;
        window_bound = 0;
        while (top > 0)
        {
            struct model cur = stack[--top];
            struct model next[4];
            int n = 0;
            int i;

            ++visited;
            if (cur.captured > deepest)
            {
                deepest = cur.captured;
            }
            /* the two conditions that make the assertions below
             * non-vacuous, recorded as they are met */
            if (cur.server - cur.client == c + MODEL_SLOTS)
            {
                tight_wire = 1;
            }
            if (cur.client + c < cur.consumed &&
                    cur.client + c < cur.server + 1)
            {
                window_bound = 1;
            }

            /* ---- the invariants, checked in every reachable state ---- */

            /* INV-SENT: no ack ever grants past the end-to-end window.
             * This is the window actually being enforced; everything
             * else follows from it. */
            ck_assert_int_le(cur.ack, cur.client + c);

            /* INV-WIRE: frames handed to the transport and not yet
             * acknowledged by the client. FR-FLOW-1 clause 4's single
             * documented meaning of C: at most C + slots. */
            ck_assert_int_le(cur.server - cur.client, c + MODEL_SLOTS);

            /* INV-LIVE: the producer already holds every credit the
             * three terms permit. This is the property the shipped
             * cross-layer gate does not have. */
            ck_assert_int_le(xrdp_gfx_credit_frontier(cur.consumed,
                                                      cur.server,
                                                      cur.client, c),
                             cur.ack);

            /* INV-HELD: regions the producer is holding, i.e. captured
             * frames not yet disposed of by a region ack. xorgxrdp sizes
             * that ring at XUP_CAP_SENT_SLOTS = MODEL_SLOTS + 1 and
             * REFUSES rather than overwrites when it is full. */
            ck_assert_int_le(cur.captured - cur.region, MODEL_SLOTS + 1);

            /* ---- successors ---- */
            if (cur.captured < ENUM_N &&
                    cur.captured + 1 <= cur.ack + MODEL_SLOTS)
            {
                next[n] = cur;
                next[n].captured += 1;
                ++n;
            }
            if (cur.consumed < cur.captured)
            {
                next[n] = cur;
                next[n].consumed += 1;
                ++n;
            }
            if (cur.server < cur.consumed)
            {
                next[n] = cur;
                next[n].server += 1;
                ++n;
            }
            if (cur.client < cur.server)
            {
                next[n] = cur;
                next[n].client += 1;
                ++n;
            }

            /* DEADLOCK FREEDOM: the only state with nothing enabled is
             * the one where the enumeration's id ceiling was reached and
             * everything drained. Any other dead end would be a pipeline
             * that has stopped for good. */
            if (n == 0)
            {
                ck_assert_int_eq(cur.captured, ENUM_N);
                ck_assert_int_eq(cur.client, ENUM_N);
            }

            for (i = 0; i < n; ++i)
            {
                int key;

                model_plan(&next[i], c);
                key = model_key(&next[i]);
                if (!seen[key])
                {
                    seen[key] = 1;
                    ck_assert_int_lt(top, 65536);
                    stack[top++] = next[i];
                }
            }
        }
        /* An enumeration that wedges early, or never reaches the states
         * the invariants are about, passes every assertion above in
         * silence. These three say it did not:
         *   - it ran the pipeline to the end,
         *   - the wire bound is ATTAINED, so "<= C + slots" is a tight
         *     statement and not a comfortable inequality,
         *   - the window term is the strict minimum somewhere, so the
         *     end-to-end guard is actually exercised rather than
         *     dominated by the slot fact throughout. */
        ck_assert_int_eq(deepest, ENUM_N);
        ck_assert_int_eq(tight_wire, 1);
        ck_assert_int_eq(window_bound, 1);
        ck_assert_int_gt(visited, 0);
        free(seen);
        free(stack);
    }
}
END_TEST

/****************************************************************************/
/*
 * GOLDEN REPLAY -- the wedge, measured.
 *
 * Source: PR-demo/mac_bisect_matrix/captures/i79_x017_ackdelay_20260802_s20
 *         leg_d40 (arm x017, 40 ms ack delay injected client->server by
 *         PR-demo/ack_delay_proxy, XRDP_GFX_FRAMES_IN_FLIGHT = 1,
 *         xorgxrdp 10fa3aa23033), frames 406-409.
 *
 * The EVENT ORDER below is measured -- it is the perf_trace, in time
 * order, with the timestamps kept in comments. The EXPECTED ACK VALUES
 * are computed by hand from FR-FLOW-1 clause 3, min(consumed, server+1,
 * client+C) with C = 2, and are written out per row.
 *
 * What the shipped build did on this exact sequence: after the slot ack
 * for 406 at -27.9 ms it emitted NOTHING until +87.4 ms (a region ack
 * for 408, once the client had acknowledged everything) and no slot
 * credit until +112.3 ms. The producer's last capture reached egress at
 * +27.0 ms and the next one was absorbed at +112.3 ms -- 85.2 ms with a
 * free slot, an idle encoder and no permission to use either.
 */

enum wedge_event
{
    W_ABSORB,
    W_EGRESS,
    W_CLIACK
};

struct wedge_step
{
    enum wedge_event ev;
    int id;
    int want_region; /* expected region ack, -1 for none */
    int want_slot;   /* expected slot ack, -1 for none   */
};

START_TEST(test_wedge_replay_d40_c2)
{
    /* state at -53.853 ms: the client has just acknowledged 405, which
     * is also what the transport and the children are at, and the
     * producer has been told 405 */
    struct model m;
    /* C = 2 */
    static const struct wedge_step steps[] =
    {
        /* -27.975  absorb 406: credit = min(406, 405+1, 405+2) = 406.
         *          region = min(405, 407) = 405, already sent. */
        { W_ABSORB, 406, -1, 406 },
        /* -17.554  egress 406: region = min(406, 407) = 406, new.
         *          credit = min(406, 407, 407) = 406, already held. */
        { W_EGRESS, 406, 406, -1 },
        /*  +0.000  absorb 407: credit = min(407, 407, 407) = 407.
         *          THIS is the credit the shipped gate withheld for
         *          112.3 ms; it admits the capture of frame 409. */
        { W_ABSORB, 407, -1, 407 },
        /* +10.227  egress 407: region = min(407, 407) = 407, new. */
        { W_EGRESS, 407, 407, -1 },
        /* +17.222  absorb 408: credit = min(408, 408, 405+2=407) = 407,
         *          already held. The WIRE WINDOW binds here, and this is
         *          the design working: 406 and 407 are on the wire
         *          unacknowledged, which is C. */
        { W_ABSORB, 408, -1, -1 },
        /* +27.036  egress 408: region = min(408, 407) = 407, held. */
        { W_EGRESS, 408, -1, -1 },
        /* +34.157  cliack 406: region = min(408, 408) = 408, new.
         *          credit = min(408, 409, 408) = 408 -- carried by the
         *          region ack, which is not SLOT_ONLY, so no separate
         *          slot ack is due. */
        { W_CLIACK, 406, 408, -1 },
        /* +66.294  cliack 407: region = min(408, 409) = 408, held.
         *          credit = min(408, 409, 409) = 408, held. The SLOT
         *          FACT binds now -- nothing new has been absorbed. */
        { W_CLIACK, 407, -1, -1 },
        /* +87.391  cliack 408: credit = min(408, 409, 410) = 408. */
        { W_CLIACK, 408, -1, -1 }
        /* +112.270 absorb 409 follows, outside the replay. */
    };
    unsigned int i;

    memset(&m, 0, sizeof(m));
    m.captured = 408;   /* 406, 407, 408 were captured before/at these */
    m.consumed = 405;
    m.server = 405;
    m.client = 405;
    m.ack = 405;
    m.region = 405;

    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
    {
        struct xrdp_gfx_ack_state st;
        struct xrdp_gfx_ack_plan plan;

        switch (steps[i].ev)
        {
            case W_ABSORB:
                m.consumed = steps[i].id;
                break;
            case W_EGRESS:
                m.server = steps[i].id;
                break;
            case W_CLIACK:
                m.client = steps[i].id;
                break;
        }
        st.frame_id_consumed = m.consumed;
        st.frame_id_server = m.server;
        st.frame_id_client = m.client;
        st.wire_window = 2;
        st.frame_id_region_sent = m.region;
        st.frame_id_server_sent = m.ack;
        xrdp_gfx_plan_acks(&st, &plan);
        ck_assert_int_eq(plan.region, steps[i].want_region);
        ck_assert_int_eq(plan.slot, steps[i].want_slot);
        if (plan.region >= 0)
        {
            m.region = plan.region;
            if (plan.region > m.ack)
            {
                m.ack = plan.region;
            }
        }
        if (plan.slot >= 0)
        {
            m.ack = plan.slot;
        }
        /* the wire bound, on the measured sequence */
        ck_assert_int_le(m.server - m.client, 2 + MODEL_SLOTS);
    }
    /* end state: the producer has been told 408, so the capture of 410
     * is admitted -- against the shipped build, which had told it 406 */
    ck_assert_int_eq(m.ack, 408);
}
END_TEST

/****************************************************************************/
/*
 * The same measured sequence at C = 1 -- the tightest window that
 * admits anything. It must still never withhold a credit the window
 * permits, and it must hold the wire tighter.
 */
START_TEST(test_wedge_replay_d40_c1)
{
    struct model m;
    static const struct wedge_step steps[] =
    {
        /* -27.975 absorb 406: credit = min(406, 406, 405+1=406) = 406 */
        { W_ABSORB, 406, -1, 406 },
        /* -17.554 egress 406: region = min(406, 406) = 406 */
        { W_EGRESS, 406, 406, -1 },
        /*  +0.000 absorb 407: credit = min(407, 407, 406) = 406, held.
         *         The window binds one frame earlier than at C = 2. */
        { W_ABSORB, 407, -1, -1 },
        /* +10.227 egress 407: region = min(407, 406) = 406, held */
        { W_EGRESS, 407, -1, -1 },
        /* +17.222 absorb 408: credit = min(408, 408, 406) = 406, held */
        { W_ABSORB, 408, -1, -1 },
        /* +27.036 egress 408: region = min(408, 406) = 406, held */
        { W_EGRESS, 408, -1, -1 },
        /* +34.157 cliack 406: region = min(408, 407) = 407, new.
         *         credit = min(408, 409, 407) = 407, carried by it. */
        { W_CLIACK, 406, 407, -1 },
        /* +66.294 cliack 407: region = min(408, 408) = 408, new */
        { W_CLIACK, 407, 408, -1 },
        /* +87.391 cliack 408: region = min(408, 409) = 408, held.
         *         credit = min(408, 409, 409) = 408, held. */
        { W_CLIACK, 408, -1, -1 }
    };
    unsigned int i;

    memset(&m, 0, sizeof(m));
    m.captured = 408;
    m.consumed = 405;
    m.server = 405;
    m.client = 405;
    m.ack = 405;
    m.region = 405;

    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
    {
        struct xrdp_gfx_ack_state st;
        struct xrdp_gfx_ack_plan plan;

        switch (steps[i].ev)
        {
            case W_ABSORB:
                m.consumed = steps[i].id;
                break;
            case W_EGRESS:
                m.server = steps[i].id;
                break;
            case W_CLIACK:
                m.client = steps[i].id;
                break;
        }
        st.frame_id_consumed = m.consumed;
        st.frame_id_server = m.server;
        st.frame_id_client = m.client;
        st.wire_window = 1;
        st.frame_id_region_sent = m.region;
        st.frame_id_server_sent = m.ack;
        xrdp_gfx_plan_acks(&st, &plan);
        ck_assert_int_eq(plan.region, steps[i].want_region);
        ck_assert_int_eq(plan.slot, steps[i].want_slot);
        if (plan.region >= 0)
        {
            m.region = plan.region;
            if (plan.region > m.ack)
            {
                m.ack = plan.region;
            }
        }
        if (plan.slot >= 0)
        {
            m.ack = plan.slot;
        }
        ck_assert_int_le(m.ack, m.client + 1);
    }
    ck_assert_int_eq(m.ack, 408);
}
END_TEST

/****************************************************************************/
Suite *
make_suite_avc444_credit_frontier(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444CreditFrontier");

    tc = tcase_create("credit_frontier");
    tcase_set_timeout(tc, 120);
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_credit_frontier_each_term_binds);
    tcase_add_test(tc, test_credit_frontier_is_monotone);
    tcase_add_test(tc, test_region_ack_obeys_the_same_window);
    tcase_add_test(tc, test_region_ack_never_lags_the_credit_by_more_than_one);
    tcase_add_test(tc, test_the_shipped_gate_withholds_what_the_window_permits);
    tcase_add_test(tc, test_planner_never_withholds_an_earned_credit);
    tcase_add_test(tc, test_joint_machine_enumeration);
    tcase_add_test(tc, test_wedge_replay_d40_c2);
    tcase_add_test(tc, test_wedge_replay_d40_c1);

    return s;
}
