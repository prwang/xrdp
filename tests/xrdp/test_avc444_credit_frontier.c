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
#include "xup_client_info.h"
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
/*
 * THE FROZEN CLIENT -- the end-to-end guard doing its job, and then
 * letting go of it.
 *
 * FR-FLOW-1 clause 2 puts the client's ack frontier at exactly one
 * decision point, capture ADMISSION, and clause 4 states the one meaning
 * of C there: at most C + 2 frames unacknowledged at send, the "+ 2"
 * being xorgxrdp's per-monitor capture budget riding above the credit.
 * Read together with clause 3's credit
 * min(consumed, server + 1, client + C), a client that stops
 * acknowledging at frame F pins every one of those:
 *
 *   the credit halts at F + C exactly -- the third term is the strict
 *   minimum once the children and the transport have drained everything
 *   the producer was allowed to make;
 *
 *   capture halts at F + C + MODEL_SLOTS, because xorgxrdp admits a
 *   capture only while captured + 1 <= its own frontier + slots;
 *
 *   so the wire carries C + MODEL_SLOTS unacknowledged frames and not
 *   one more, and the planner must then emit NOTHING, however many
 *   further absorb or egress events arrive -- there is no ack left to
 *   send that the window permits.
 *
 * Expected values are computed from those clauses by hand above; nothing
 * here is read off the implementation. F is arbitrary (100) and the
 * arithmetic is stated relative to it.
 *
 * The RESUME half is asserted in the same case on purpose. A pipeline
 * that halts correctly and never restarts satisfies every assertion in
 * the first half -- "emit nothing forever" is the trivially safe
 * behaviour and it is a total stall. So: one client ack must move the
 * credit by exactly one, twice, until the SLOT FACT takes over as the
 * binding term and the credit stops again on a fact about the encoder
 * rather than the network.
 */
#define FROZEN_F 100

START_TEST(test_credit_frontier_frozen_client_halts_at_client_plus_c)
{
    int c;

    for (c = 1; c <= 3; ++c)
    {
        struct model m;
        int rounds;
        int i;
        int before;

        /* Everything is drained and current at frame F: the client, the
         * transport and the children all name F, and the producer has
         * been told F. Then the client stops acknowledging, and NO
         * CLIACK event occurs for the rest of the first half. */
        m.captured = FROZEN_F;
        m.consumed = FROZEN_F;
        m.server = FROZEN_F;
        m.client = FROZEN_F;
        m.ack = FROZEN_F;
        m.region = FROZEN_F;

        /* Run capture, the encoder children and the transport as fast as
         * they will go. Each round admits every capture xorgxrdp's own
         * budget allows, drains all of it and sends all of it, planning
         * after every absorb and every egress exactly as the live code
         * does. Two rounds reach the halt; twenty is slack. */
        for (rounds = 0; rounds < 20; ++rounds)
        {
            while (m.captured + 1 <= m.ack + MODEL_SLOTS)
            {
                m.captured += 1;
            }
            while (m.consumed < m.captured)
            {
                m.consumed += 1;
                model_plan(&m, c);
            }
            while (m.server < m.consumed)
            {
                m.server += 1;
                model_plan(&m, c);
                /* clause 4's bound, checked at every send and not only
                 * at the end */
                ck_assert_int_le(m.server - m.client, c + MODEL_SLOTS);
            }
        }

        /* (a) and (c): the halt is EXACT, not merely bounded. */
        ck_assert_int_eq(m.ack, FROZEN_F + c);
        ck_assert_int_eq(m.region, FROZEN_F + c);
        ck_assert_int_eq(m.captured, FROZEN_F + c + MODEL_SLOTS);
        ck_assert_int_eq(m.server, FROZEN_F + c + MODEL_SLOTS);
        ck_assert_int_eq(m.client, FROZEN_F);
        /* the wire bound is ATTAINED here, so the inequality above is a
         * tight statement in this case rather than a comfortable one */
        ck_assert_int_eq(m.server - m.client, c + MODEL_SLOTS);
        /* and capture really is refused: one more frame would sit
         * MODEL_SLOTS + 1 above the frontier */
        ck_assert_int_gt(m.captured + 1, m.ack + MODEL_SLOTS);
        /* the credit ITSELF, not merely the frontier the producer ended
         * up holding. Both acks move the producer's slot frontier, so a
         * credit that is one id too small or too large is masked by the
         * region ack in the state above; asserted here directly against
         * clause 3, min(F + C + MODEL_SLOTS, F + C + MODEL_SLOTS + 1,
         * F + C) = F + C, so neither direction can hide. */
        ck_assert_int_eq(xrdp_gfx_credit_frontier(m.consumed, m.server,
                         m.client, c),
                         FROZEN_F + c);

        /* (b) once halted the planner emits nothing at all, no matter
         * how far the stages downstream of capture are pushed. The
         * pushed states are counterfactual -- the producer cannot reach
         * them while the client is frozen -- which is the point: even
         * handed them, the planner has no ack the window permits. */
        for (i = 1; i <= 5; ++i)
        {
            struct xrdp_gfx_ack_state st;
            struct xrdp_gfx_ack_plan plan;

            st.frame_id_consumed = m.consumed + i;
            st.frame_id_server = m.server + i;
            st.frame_id_client = m.client;
            st.wire_window = c;
            st.frame_id_region_sent = m.region;
            st.frame_id_server_sent = m.ack;
            xrdp_gfx_plan_acks(&st, &plan);
            ck_assert_int_eq(plan.region, -1);
            ck_assert_int_eq(plan.slot, -1);
        }
        /* replanning the halted state itself is also silent */
        before = m.ack;
        model_plan(&m, c);
        ck_assert_int_eq(m.ack, before);
        ck_assert_int_eq(m.region, FROZEN_F + c);

        /* RESUME. The client acknowledges ONE frame. The window moves by
         * one id and both other terms are above it (consumed is
         * F + C + MODEL_SLOTS, server + 1 is one more), so the credit
         * must move by exactly one -- not zero, and not up to the
         * transport frontier. */
        before = m.ack;
        m.client += 1;
        model_plan(&m, c);
        ck_assert_int_eq(m.ack - before, 1);
        ck_assert_int_eq(m.ack, FROZEN_F + c + 1);
        ck_assert_int_eq(xrdp_gfx_credit_frontier(m.consumed, m.server,
                         m.client, c),
                         FROZEN_F + c + 1);
        /* the capture that was refused a moment ago is admitted now:
         * this is the assertion a pipeline that never restarts fails */
        ck_assert_int_le(m.captured + 1, m.ack + MODEL_SLOTS);

        /* a second ack buys exactly one more id, for the same reason */
        before = m.ack;
        m.client += 1;
        model_plan(&m, c);
        ck_assert_int_eq(m.ack - before, 1);
        ck_assert_int_eq(m.ack, FROZEN_F + c + MODEL_SLOTS);

        /* a third buys nothing, and now for a LOCAL reason: no further
         * frame has been captured or absorbed, so the SLOT FACT
         * (frame_id_consumed) is the strict minimum. The credit stopping
         * here is the nearest-neighbour behaviour clause 1 requires,
         * not the network's doing. */
        before = m.ack;
        m.client += 1;
        model_plan(&m, c);
        ck_assert_int_eq(m.ack - before, 0);
        ck_assert_int_eq(m.ack, m.consumed);
        ck_assert_int_lt(m.consumed, m.client + c);
    }
}
END_TEST

/****************************************************************************/
/*
 * BACKLOG #91 -- the per-monitor "would the credit have permitted this
 * screen to capture" predicate that the pump trace record carries.
 *
 * WHERE THESE EXPECTED VALUES COME FROM. Not from the helper. The rule
 * is written down in common/xup_client_info.h, the contract header
 * xrdp and xorgxrdp compile VERBATIM from the same text, and it says
 * two things:
 *
 *   (1) the capture budget is XUP_CAP_AVC444_SLOT_COUNT outstanding
 *       frames PER MONITOR -- "m INDEPENDENT caps ... never a global
 *       pool";
 *   (2) the ack is CUMULATIVE, so retiring is "drop every entry the ack
 *       covers", never a per-id match.
 *
 * Every expected value below is (1) and (2) applied by hand to a
 * scenario. The frame ids are the producer's rect_ids, which count up
 * from 1 GLOBALLY -- one counter shared by all monitors -- so two
 * screens interleave their ids, and that is exactly what makes a single
 * cumulative credit able to permit one screen and refuse the other.
 */
START_TEST(test_credit_permits_capture_from_the_producers_contract)
{
    int ids[MODEL_SLOTS];

    /* the contract this test is written against is a TWO slot budget;
     * if that number ever changes, these hand-derived cases are about a
     * different machine and must be re-derived, not adjusted */
    ck_assert_int_eq(MODEL_SLOTS, 2);

    /* Nothing has ever been sent for this monitor. xup_cap_budget_reset
     * leaves count 0, so the monitor has both its slots. Ids below 1
     * are the "no such frame" sentinel: the producer's rect_id counts
     * up FROM 1, so 0 can never be a real capture. */
    ids[0] = 0;
    ids[1] = 0;
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 0), 1);
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 9), 1);

    /* ONE frame out and unacked. Two slots means one outstanding frame
     * still leaves room -- this is the whole point of FR-CAPTURE-8's
     * two-deep budget, and a predicate that said "no" here would pin
     * the producer to one frame in flight. */
    ids[0] = 7;
    ids[1] = 0;
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 6), 1);

    /* BOTH slots out and unacked: the monitor is at cap. This is the
     * only shape that yields "credit-limited" on the trace. */
    ids[0] = 8;
    ids[1] = 7;
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 6), 0);

    /* the ack is CUMULATIVE: a credit of exactly 7 covers id 7, so one
     * slot comes back and the monitor may capture again. The boundary
     * is "at or below", not "below". */
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 7), 1);

    /* a credit above both retires both */
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 8), 1);

    /* the predicate counts, so the order the ids are stored in cannot
     * matter -- the trace's ring is newest-first, the producer's is
     * oldest-first, and both must answer the same */
    ids[0] = 7;
    ids[1] = 8;
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 6), 0);
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 7), 1);

    /* a monitor with only ONE id on record is one outstanding frame at
     * most, whatever the second slot would have held */
    ids[0] = 9;
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, 1, 0), 1);

    /* TWO SCREENS, ONE CUMULATIVE CREDIT -- the case the whole field
     * exists for. Global rect_ids 3,5 went to the top screen and 4,6 to
     * the bottom one. A credit of 3 has retired the top screen's older
     * frame and NEITHER of the bottom screen's, so the same credit
     * permits the top screen and refuses the bottom one. */
    ids[0] = 5;
    ids[1] = 3;
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 3), 1);
    ids[0] = 6;
    ids[1] = 4;
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 3), 0);
    /* one more ack and the bottom screen is released too */
    ck_assert_int_eq(xrdp_gfx_credit_permits_capture(ids, MODEL_SLOTS, 4), 1);
}
END_TEST

/****************************************************************************/
/*
 * The same predicate, checked against the PRODUCER'S OWN implementation
 * of the rule rather than against a table.
 *
 * xup_cap_budget_has_capacity() is a different algorithm -- a ring that
 * is compacted in place by the ack, then a count against the cap --
 * living in the contract header both processes compile. Agreeing with
 * it over an enumeration is what says the trace's answer is the
 * producer's answer, not a plausible restatement of it.
 */
static void
model_replay_sends(struct xup_cap_budget *budget, int (*ids)[MODEL_SLOTS],
                   int n_mon, int n_sends)
{
    int send;
    int mon;
    int slot;
    int rect_id;

    xup_cap_budget_reset(budget);
    memset(ids, 0, sizeof(ids[0]) * n_mon);
    for (send = 0; send < n_sends; send++)
    {
        /* one GLOBAL rect_id counter, handed out in monitor rotation --
         * rdpDeferredUpdateCallback's scan order */
        mon = send % n_mon;
        rect_id = send + 1;
        /* the producer's ring, driven exactly as the producer drives it;
         * ack 0 during the replay so nothing retires early */
        xup_cap_budget_record_send(budget, mon, rect_id, 0, MODEL_SLOTS);
        /* the trace's ring: newest first */
        for (slot = MODEL_SLOTS - 1; slot > 0; slot--)
        {
            ids[mon][slot] = ids[mon][slot - 1];
        }
        ids[mon][0] = rect_id;
    }
}

START_TEST(test_credit_permits_capture_agrees_with_the_producer_budget)
{
    struct xup_cap_budget budget;
    int ids[4][MODEL_SLOTS];
    int n_mon;
    int n_sends;
    int credit;
    int mon;
    int expected;
    int got;

    for (n_mon = 1; n_mon <= 4; n_mon++)
    {
        for (n_sends = 0; n_sends <= 12; n_sends++)
        {
            for (credit = 0; credit <= 14; credit++)
            {
                /* has_capacity() RETIRES as it answers, so the ring is
                 * rebuilt for every question rather than carried */
                model_replay_sends(&budget, ids, n_mon, n_sends);
                for (mon = 0; mon < n_mon; mon++)
                {
                    expected = xup_cap_budget_has_capacity(&budget, mon,
                                                           credit,
                                                           MODEL_SLOTS);
                    got = xrdp_gfx_credit_permits_capture(ids[mon],
                                                          MODEL_SLOTS,
                                                          credit);
                    ck_assert_int_eq(got != 0, expected != 0);
                }
            }
        }
    }
}
END_TEST

/****************************************************************************/
/*
 * BACKLOG #91 -- the MASK the pump record actually carries.
 *
 * The parent analysis reads bit m of this word as "monitor m", so the
 * bit positions and the unknown-monitor rule are the contract, and they
 * are pinned here rather than by reading a live capture.
 */
#if defined(XRDP_PERF_TRACE)
START_TEST(test_credit_mask_names_the_permitted_monitors)
{
    struct xrdp_encoder enc;
    int mask;

    memset(&enc, 0, sizeof(enc));

    /* No frame has ever arrived for any monitor. A zeroed history is
     * "unknown", NOT "idle and permitted": claiming a bit for a monitor
     * that may not even exist would invent a credit-limited/not
     * decomposition for fifteen screens nobody has. */
    ck_assert_int_eq(gfx_batch_credit_mask(&enc, 0), 0);
    ck_assert_int_eq(gfx_batch_credit_mask(&enc, 1000), 0);

    /* Two screens, the interleaved global rect_ids of the hand-derived
     * case above: top screen (monitor 0) holds 3 and 5, bottom screen
     * (monitor 1) holds 4 and 6, newest first. */
    enc.avc444_mon_frame_id[0][0] = 5;
    enc.avc444_mon_frame_id[0][1] = 3;
    enc.avc444_mon_frame_id[1][0] = 6;
    enc.avc444_mon_frame_id[1][1] = 4;

    /* credit 2 retires nothing: both screens are at cap */
    ck_assert_int_eq(gfx_batch_credit_mask(&enc, 2), 0);
    /* credit 3 retires the top screen's older frame only -> bit 0 */
    mask = gfx_batch_credit_mask(&enc, 3);
    ck_assert_int_eq(mask, 1 << 0);
    /* credit 4 retires the bottom screen's older frame too -> both */
    mask = gfx_batch_credit_mask(&enc, 4);
    ck_assert_int_eq(mask, (1 << 0) | (1 << 1));

    /* a third screen that has never been seen still contributes no bit,
     * even while its neighbours are permitted */
    ck_assert_int_eq(gfx_batch_credit_mask(&enc, 99),
                     (1 << 0) | (1 << 1));

    /* the highest monitor index the array can hold lands on the highest
     * bit the reader will look at -- an off-by-one here would silently
     * relabel every screen in the analysis */
    memset(&enc, 0, sizeof(enc));
    enc.avc444_mon_frame_id[15][0] = 1;
    ck_assert_int_eq(gfx_batch_credit_mask(&enc, 1), 1 << 15);
    ck_assert_int_eq(gfx_batch_credit_mask(&enc, 0), 1 << 15);
    /* ... and with both slots of monitor 15 outstanding, no bit */
    enc.avc444_mon_frame_id[15][0] = 2;
    enc.avc444_mon_frame_id[15][1] = 1;
    ck_assert_int_eq(gfx_batch_credit_mask(&enc, 0), 0);
}
END_TEST
#endif

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
    tcase_add_test(tc,
                   test_credit_frontier_frozen_client_halts_at_client_plus_c);
    tcase_add_test(tc, test_credit_permits_capture_from_the_producers_contract);
    tcase_add_test(tc,
                   test_credit_permits_capture_agrees_with_the_producer_budget);
#if defined(XRDP_PERF_TRACE)
    tcase_add_test(tc, test_credit_mask_names_the_permitted_monitors);
#endif

    return s;
}
