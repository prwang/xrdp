#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <string.h>

#include "test_xrdp.h"

/*
 * FR-H264-8 syntax ratchet (PRD "Unit-test specification", group 3):
 * a small pure-C DPB simulator implementing the H.264 8.2.5 marking
 * subset our AVC444 streams exercise -- sliding window, mmco 6
 * (mark CURRENT picture long-term), mmco 4 (max_long_term_frame_idx),
 * IDR long_term_reference_flag -- fed the FR-H264-8 recipe's access
 * unit sequences in BOTH client decode modes:
 *   1-context: full interleave through one decoder (mstsc shape);
 *   2-context: each view through its own decoder (macOS shape).
 * The tests machine-check the no-ambiguity claim: every P slice's
 * ref-list position 0 resolves to the intended SAME-VIEW picture in
 * both modes, long-term slots are never touched by the sliding window
 * across >= 512 frames including the frame_num wrap, mmco6
 * reassignment REPLACES the slot occupant (measured Win2022
 * semantics), and the DPB reference count stays within the SPS
 * max_num_ref_frames bound at every step.
 *
 * SCOPE: the simulator models 8.2.5 MARKING only -- it does not model
 * decoder gap-frame synthesis (ffmpeg fills frame_num gaps with dummy
 * short-term frames), and real per-view decoders measurably STOP at a
 * frame_num wrap (see xrdp_h264_annexb.h): the 2-context wrap
 * crossings below validate the model's counter/slot arithmetic, while
 * the SHIPPED wire prevents decoder-visible wraps structurally via
 * the XRDP_H264_LTR_FRAME_NUM_REKEY restart. The sliding-window
 * eviction order uses insertion order as a FrameNumWrap proxy, valid
 * because no test (and no recipe stream) holds short-terms across a
 * larger-than-half-range frame_num jump.
 *
 * Reference resolution mirrors deployed-decoder semantics (ffmpeg
 * h264_refs.c): long_term_pic_num lookups are NOT bounded by
 * MaxLongTermFrameIdx. The recipe's deliberate 7.4.3.3 deviation
 * (mmco6 long_term_frame_idx=1 with no mmco4 anywhere -- exactly what
 * the measured Win2022 server ships, see
 * PR-demo/win2022_ground_truth/GROUND_TRUTH_win2022_avc444.md) is
 * RECORDED by the simulator and asserted to occur only where known,
 * so the nonconformance stays deliberate and visible, never silent.
 */

/* Win2022 ground-truth SPS values (gfxwin_anim capture):
 * log2_max_frame_num_minus4 = 4, max_num_ref_frames = 3 */
#define LTR_LOG2_MAX_FRAME_NUM 8
#define LTR_MAX_FRAME_NUM (1 << LTR_LOG2_MAX_FRAME_NUM)
#define LTR_MAX_NUM_REF_FRAMES 3

#define LTR_DPB_MAX 16

#define LTR_VIEW_MAIN 0
#define LTR_VIEW_AUX 1

struct ltr_pic
{
    int used;
    int view;
    int seq;        /* source picture identity (generator index)     */
    int frame_num;
    int is_long;
    int lt_idx;     /* LongTermFrameIdx, valid when is_long          */
    int order;      /* insertion order, stands in for FrameNumWrap   */
};

struct ltr_dpb
{
    struct ltr_pic pics[LTR_DPB_MAX];
    int max_num_ref_frames;
    int max_frame_num;
    int max_lt_idx;         /* MaxLongTermFrameIdx, -1 = none        */
    int prev_frame_num;
    int started;
    int order;
    /* recorded events the tests assert on */
    int gaps;               /* frame_num jumps (gaps_allowed = 0)    */
    int idr_fn_violations;  /* IDR with frame_num != 0 (7.4.3)       */
    int range_violations;   /* mmco6 ltfi > MaxLongTermFrameIdx      */
    int window_evictions;   /* sliding-window short-term removals    */
    int missing_ref;        /* P resolution failed                   */
    int overflow;           /* ref count exceeded the SPS bound      */
};

/* one coded picture as the FR-H264-8 splicer emits it */
struct ltr_au
{
    int view;
    int seq;
    int is_idr;
    int ltr_flag;       /* IDR long_term_reference_flag              */
    int frame_num;
    int slice_p;        /* 1 = P slice with LTR list-mod, 0 = I      */
    int ltpn;           /* long_term_pic_num selected when slice_p   */
    int mmco6_ltfi;     /* -1 = sliding window, else self-mark index */
    int mmco4_plus1;    /* 0 = absent (Windows shape)                */
};

/*****************************************************************************/
static void
ltr_dpb_init(struct ltr_dpb *d)
{
    memset(d, 0, sizeof(*d));
    d->max_num_ref_frames = LTR_MAX_NUM_REF_FRAMES;
    d->max_frame_num = LTR_MAX_FRAME_NUM;
    d->max_lt_idx = -1;
}

/*****************************************************************************/
static int
ltr_dpb_ref_count(const struct ltr_dpb *d)
{
    int i;
    int n;

    n = 0;
    for (i = 0; i < LTR_DPB_MAX; i++)
    {
        if (d->pics[i].used)
        {
            n++;
        }
    }
    return n;
}

/*****************************************************************************/
static struct ltr_pic *
ltr_dpb_free_slot(struct ltr_dpb *d)
{
    int i;

    for (i = 0; i < LTR_DPB_MAX; i++)
    {
        if (!d->pics[i].used)
        {
            return &d->pics[i];
        }
    }
    return NULL;
}

/*****************************************************************************/
/* 8.2.5.3 sliding window: remove the short-term reference with the      */
/* smallest FrameNumWrap. Long-term pictures are EXEMPT -- the pinning   */
/* property under test. A long-term eviction here would be a simulator   */
/* bug and is counted separately so a test can prove it never happens.   */
static void
ltr_dpb_sliding_window(struct ltr_dpb *d)
{
    int i;
    int oldest;

    while (ltr_dpb_ref_count(d) > d->max_num_ref_frames)
    {
        oldest = -1;
        for (i = 0; i < LTR_DPB_MAX; i++)
        {
            if (d->pics[i].used && !d->pics[i].is_long &&
                    (oldest < 0 ||
                     d->pics[i].order < d->pics[oldest].order))
            {
                oldest = i;
            }
        }
        if (oldest < 0)
        {
            /* only long-term pictures left: the window may not evict
             * them; the stream is over its reference bound */
            d->overflow++;
            return;
        }
        d->pics[oldest].used = 0;
        d->window_evictions++;
    }
}

/*****************************************************************************/
/* decode one picture: resolve its ref-list position 0 (when P), then    */
/* apply 8.2.5 marking. Returns 0 and fills *ref_view / *ref_seq on a    */
/* resolved P; returns 0 with *ref_seq = -1 for I; returns 1 when a P    */
/* cannot resolve its reference (recorded in missing_ref).               */
static int
ltr_dpb_decode(struct ltr_dpb *d, const struct ltr_au *au,
               int *ref_view, int *ref_seq)
{
    struct ltr_pic *p;
    int i;

    *ref_view = -1;
    *ref_seq = -1;

    /* frame_num gap detection (gaps_in_frame_num_allowed = 0) */
    if (au->is_idr)
    {
        if (au->frame_num != 0)
        {
            d->idr_fn_violations++;
            return 1;
        }
    }
    else if (d->started &&
             au->frame_num != (d->prev_frame_num + 1) % d->max_frame_num)
    {
        d->gaps++;
    }

    /* reference resolution BEFORE marking (8.2.4): for frame coding,
     * LongTermPicNum == LongTermFrameIdx */
    if (au->slice_p)
    {
        p = NULL;
        for (i = 0; i < LTR_DPB_MAX; i++)
        {
            if (d->pics[i].used && d->pics[i].is_long &&
                    d->pics[i].lt_idx == au->ltpn)
            {
                p = &d->pics[i];
                break;
            }
        }
        if (p == NULL)
        {
            d->missing_ref++;
            return 1;
        }
        *ref_view = p->view;
        *ref_seq = p->seq;
    }

    /* 8.2.5 marking */
    if (au->is_idr)
    {
        /* 8.2.5.1: IDR empties the DPB */
        memset(d->pics, 0, sizeof(d->pics));
        p = ltr_dpb_free_slot(d);
        p->used = 1;
        p->view = au->view;
        p->seq = au->seq;
        p->frame_num = au->frame_num;
        p->order = d->order++;
        if (au->ltr_flag)
        {
            p->is_long = 1;
            p->lt_idx = 0;
            d->max_lt_idx = 0;
        }
        else
        {
            p->is_long = 0;
            d->max_lt_idx = -1;
        }
    }
    else
    {
        if (au->mmco4_plus1 > 0)
        {
            /* 8.2.5.4.4: set MaxLongTermFrameIdx, prune above it */
            d->max_lt_idx = au->mmco4_plus1 - 1;
            for (i = 0; i < LTR_DPB_MAX; i++)
            {
                if (d->pics[i].used && d->pics[i].is_long &&
                        d->pics[i].lt_idx > d->max_lt_idx)
                {
                    d->pics[i].used = 0;
                }
            }
        }
        if (au->mmco6_ltfi >= 0)
        {
            /* 7.4.3.3 range clause: deliberate Windows-shape
             * deviation is recorded, then executed leniently like
             * deployed decoders (ffmpeg h264_refs.c) */
            if (au->mmco6_ltfi > d->max_lt_idx)
            {
                d->range_violations++;
            }
            /* 8.2.5.4.6: reassignment REPLACES the slot occupant */
            for (i = 0; i < LTR_DPB_MAX; i++)
            {
                if (d->pics[i].used && d->pics[i].is_long &&
                        d->pics[i].lt_idx == au->mmco6_ltfi)
                {
                    d->pics[i].used = 0;
                }
            }
            p = ltr_dpb_free_slot(d);
            if (p == NULL)
            {
                d->overflow++;
                return 1;
            }
            p->used = 1;
            p->view = au->view;
            p->seq = au->seq;
            p->frame_num = au->frame_num;
            p->is_long = 1;
            p->lt_idx = au->mmco6_ltfi;
            p->order = d->order++;
        }
        else
        {
            /* sliding window: current enters short-term */
            p = ltr_dpb_free_slot(d);
            if (p == NULL)
            {
                d->overflow++;
                return 1;
            }
            p->used = 1;
            p->view = au->view;
            p->seq = au->seq;
            p->frame_num = au->frame_num;
            p->is_long = 0;
            p->order = d->order++;
            ltr_dpb_sliding_window(d);
        }
    }
    if (ltr_dpb_ref_count(d) > d->max_num_ref_frames)
    {
        d->overflow++;
    }
    d->prev_frame_num = au->frame_num;
    d->started = 1;
    return 0;
}

/*
 * FR-H264-8 recipe generator -- the same constants-per-view emission
 * the splicer performs (PRD FR-H264-8 "Measured recipe" with our
 * first-aux variant: a self-contained I that self-marks LT1 and
 * references nothing, instead of the Windows first-aux-refs-LT0
 * quirk, so the 2-context aux feed resolves entirely inside the aux
 * chain).
 */

struct ltr_seq_ctx
{
    int frame_num;      /* shared counter across BOTH views          */
    int aux_seeded;     /* LT1 occupied                              */
};

/*****************************************************************************/
static void
ltr_emit_main_idr(struct ltr_seq_ctx *c, int seq, struct ltr_au *au)
{
    memset(au, 0, sizeof(*au));
    au->view = LTR_VIEW_MAIN;
    au->seq = seq;
    au->is_idr = 1;
    au->ltr_flag = 1;       /* seeds LT0 */
    au->frame_num = 0;
    au->mmco6_ltfi = -1;
    c->frame_num = 1;
    c->aux_seeded = 0;      /* IDR emptied the DPB: LT1 is gone */
}

/*****************************************************************************/
static void
ltr_emit_main_p(struct ltr_seq_ctx *c, int seq, struct ltr_au *au)
{
    memset(au, 0, sizeof(*au));
    au->view = LTR_VIEW_MAIN;
    au->seq = seq;
    au->frame_num = c->frame_num;
    au->slice_p = 1;
    au->ltpn = 0;           /* constant per view: main selects LT0 */
    au->mmco6_ltfi = 0;     /* constant per view: main marks LT0 */
    c->frame_num = (c->frame_num + 1) % LTR_MAX_FRAME_NUM;
}

/*****************************************************************************/
static void
ltr_emit_aux(struct ltr_seq_ctx *c, int seq, struct ltr_au *au)
{
    memset(au, 0, sizeof(*au));
    au->view = LTR_VIEW_AUX;
    au->seq = seq;
    au->frame_num = c->frame_num;
    au->mmco6_ltfi = 1;     /* constant per view: aux marks LT1 */
    if (c->aux_seeded)
    {
        au->slice_p = 1;
        au->ltpn = 1;       /* constant per view: aux selects LT1 */
    }
    else
    {
        /* first aux after an IDR: self-contained I, references
         * nothing (the 2-context enabler) */
        au->slice_p = 0;
        c->aux_seeded = 1;
    }
    c->frame_num = (c->frame_num + 1) % LTR_MAX_FRAME_NUM;
}

/*
 * Both-modes runner: feed the AU list to one 1-context DPB and to two
 * per-view 2-context DPBs, computing the INTENDED reference for every
 * P from the AU list itself (last same-view picture), and assert
 * bit-identical resolution everywhere.
 */

struct ltr_run_stats
{
    struct ltr_dpb one_ctx;
    struct ltr_dpb main_only;
    struct ltr_dpb aux_only;
    int p_slices_checked;
};

/*****************************************************************************/
static void
ltr_run_both_modes(const struct ltr_au *aus, int n,
                   struct ltr_run_stats *st)
{
    int last_seq[2];
    int rv1;
    int rv2;
    int rs1;
    int rs2;
    int expect_view;
    int expect_seq;
    int i;

    memset(st, 0, sizeof(*st));
    ltr_dpb_init(&st->one_ctx);
    ltr_dpb_init(&st->main_only);
    ltr_dpb_init(&st->aux_only);
    last_seq[0] = -1;
    last_seq[1] = -1;

    for (i = 0; i < n; i++)
    {
        const struct ltr_au *au = aus + i;
        struct ltr_dpb *own = (au->view == LTR_VIEW_MAIN)
                              ? &st->main_only : &st->aux_only;

        if (au->slice_p)
        {
            expect_view = au->view;
            expect_seq = last_seq[au->view];
            ck_assert_int_ge(expect_seq, 0);
        }
        else
        {
            expect_view = -1;
            expect_seq = -1;
        }
        ck_assert_int_eq(ltr_dpb_decode(&st->one_ctx, au, &rv1, &rs1), 0);
        ck_assert_int_eq(ltr_dpb_decode(own, au, &rv2, &rs2), 0);
        if (au->slice_p)
        {
            /* the no-ambiguity claim: same-view resolution, identical
             * in the interleaved and the per-view decode */
            ck_assert_int_eq(rv1, expect_view);
            ck_assert_int_eq(rs1, expect_seq);
            ck_assert_int_eq(rv2, expect_view);
            ck_assert_int_eq(rs2, expect_seq);
            st->p_slices_checked++;
        }
        /* the SPS reference bound holds at every step in every mode */
        ck_assert_int_eq(st->one_ctx.overflow, 0);
        ck_assert_int_eq(st->main_only.overflow, 0);
        ck_assert_int_eq(st->aux_only.overflow, 0);
        last_seq[au->view] = au->seq;
    }
    /* no P slice ever failed to resolve, in any mode */
    ck_assert_int_eq(st->one_ctx.missing_ref, 0);
    ck_assert_int_eq(st->main_only.missing_ref, 0);
    ck_assert_int_eq(st->aux_only.missing_ref, 0);
    /* the sliding window never ran at all on recipe streams (every
     * picture self-marks long-term): pinning depends on nothing the
     * window could take away */
    ck_assert_int_eq(st->one_ctx.window_evictions, 0);
    ck_assert_int_eq(st->main_only.window_evictions, 0);
    ck_assert_int_eq(st->aux_only.window_evictions, 0);
}

#define LTR_MAX_TEST_AUS 1600

static struct ltr_au g_aus[LTR_MAX_TEST_AUS];

/*****************************************************************************/
/* 1:1 cadence M A M A ... over n_pairs; returns the AU count */
static int
ltr_gen_pairs(int n_pairs)
{
    struct ltr_seq_ctx c;
    int k;
    int n;

    memset(&c, 0, sizeof(c));
    n = 0;
    ltr_emit_main_idr(&c, n, &g_aus[n]);
    n++;
    ltr_emit_aux(&c, n, &g_aus[n]);
    n++;
    for (k = 1; k < n_pairs; k++)
    {
        ltr_emit_main_p(&c, n, &g_aus[n]);
        n++;
        ltr_emit_aux(&c, n, &g_aus[n]);
        n++;
    }
    return n;
}

/*****************************************************************************/
START_TEST(test_ltr_dpb_resolution_identical_both_modes)
{
    struct ltr_run_stats st;
    int n;

    n = ltr_gen_pairs(20);
    ltr_run_both_modes(g_aus, n, &st);
    /* 19 main P + 19 aux P */
    ck_assert_int_eq(st.p_slices_checked, 38);
    /* the interleaved feed is gap-free; each per-view feed observes
     * the other view's frame_num increments as gaps (tolerated by the
     * invariance contract, recorded here) */
    ck_assert_int_eq(st.one_ctx.gaps, 0);
    ck_assert_int_gt(st.main_only.gaps, 0);
    ck_assert_int_gt(st.aux_only.gaps, 0);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_dpb_pinning_512_frames_across_wrap)
{
    struct ltr_run_stats st;
    int n;

    /* 700 pairs = 1400 pictures: the shared frame_num counter
     * (log2_max_frame_num = 8) wraps 255 -> 0 five times; long-term
     * pinning and resolution must hold across every wrap */
    n = ltr_gen_pairs(700);
    ck_assert_int_ge(n, 512);
    ltr_run_both_modes(g_aus, n, &st);
    ck_assert_int_eq(st.p_slices_checked, 1398);
    ck_assert_int_eq(st.one_ctx.gaps, 0);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_dpb_sparse_aux_cadence)
{
    struct ltr_run_stats st;
    struct ltr_seq_ctx c;
    int n;
    int k;

    /* aux cadences != 1:1 must not desynchronize the shared counter
     * or the slot resolution (Lever-2 sparse-aux shape): 1 aux every
     * 5 mains, then a burst of 3 aux in a row */
    memset(&c, 0, sizeof(c));
    n = 0;
    ltr_emit_main_idr(&c, n, &g_aus[n]);
    n++;
    ltr_emit_aux(&c, n, &g_aus[n]);
    n++;
    for (k = 0; k < 200; k++)
    {
        ltr_emit_main_p(&c, n, &g_aus[n]);
        n++;
        if (k % 5 == 4)
        {
            ltr_emit_aux(&c, n, &g_aus[n]);
            n++;
        }
        if (k == 100)
        {
            ltr_emit_aux(&c, n, &g_aus[n]);
            n++;
            ltr_emit_aux(&c, n, &g_aus[n]);
            n++;
            ltr_emit_aux(&c, n, &g_aus[n]);
            n++;
        }
    }
    ltr_run_both_modes(g_aus, n, &st);
    /* 200 main P + (40 cadence-5 aux + 3 burst aux) P = 243: exact,
     * so a generator regression cannot silently drop slices */
    ck_assert_int_eq(st.p_slices_checked, 243);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_dpb_slot_reassignment_replaces)
{
    struct ltr_dpb d;
    struct ltr_seq_ctx c;
    struct ltr_au au;
    int rv;
    int rs;
    int i;
    int longs;
    int lt1_seq;

    /* after every aux self-mark there is EXACTLY ONE picture in LT1,
     * and it is the newest aux (measured Windows semantics: mmco6
     * reassignment replaces the occupant, no accumulation) */
    ltr_dpb_init(&d);
    memset(&c, 0, sizeof(c));
    ltr_emit_main_idr(&c, 0, &au);
    ck_assert_int_eq(ltr_dpb_decode(&d, &au, &rv, &rs), 0);
    for (i = 1; i <= 40; i++)
    {
        if (i % 2 == 1)
        {
            ltr_emit_aux(&c, i, &au);
        }
        else
        {
            ltr_emit_main_p(&c, i, &au);
        }
        ck_assert_int_eq(ltr_dpb_decode(&d, &au, &rv, &rs), 0);
        longs = 0;
        lt1_seq = -1;
        {
            int j;

            for (j = 0; j < LTR_DPB_MAX; j++)
            {
                if (d.pics[j].used)
                {
                    ck_assert_int_eq(d.pics[j].is_long, 1);
                    if (d.pics[j].lt_idx == 1)
                    {
                        longs++;
                        lt1_seq = d.pics[j].seq;
                    }
                }
            }
        }
        ck_assert_int_eq(longs, 1);
        if (i % 2 == 1)
        {
            ck_assert_int_eq(lt1_seq, i);
        }
        /* never more than LT0 + LT1 resident */
        ck_assert_int_le(ltr_dpb_ref_count(&d), 2);
    }
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_dpb_idr_restart_reseeds)
{
    struct ltr_run_stats st;
    struct ltr_seq_ctx c;
    int n;
    int k;

    /* mid-stream IDR: the DPB empties, LT1 dies with it, and the
     * recipe re-seeds via a fresh self-contained first aux -- the
     * whole sequence must resolve cleanly in both modes */
    memset(&c, 0, sizeof(c));
    n = 0;
    ltr_emit_main_idr(&c, n, &g_aus[n]);
    n++;
    ltr_emit_aux(&c, n, &g_aus[n]);
    n++;
    for (k = 0; k < 20; k++)
    {
        ltr_emit_main_p(&c, n, &g_aus[n]);
        n++;
        ltr_emit_aux(&c, n, &g_aus[n]);
        n++;
    }
    ltr_emit_main_idr(&c, n, &g_aus[n]);
    n++;
    ltr_emit_aux(&c, n, &g_aus[n]);   /* re-seed: I again, not P */
    ck_assert_int_eq(g_aus[n].slice_p, 0);
    n++;
    for (k = 0; k < 20; k++)
    {
        ltr_emit_main_p(&c, n, &g_aus[n]);
        n++;
        ltr_emit_aux(&c, n, &g_aus[n]);
        n++;
    }
    ltr_run_both_modes(g_aus, n, &st);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_dpb_unseeded_aux_p_is_missing_ref)
{
    struct ltr_dpb d;
    struct ltr_seq_ctx c;
    struct ltr_au au;
    int rv;
    int rs;

    /* the failure the emitter guard must make impossible: a P-shaped
     * aux while LT1 is unseeded (e.g. right after a main IDR) has no
     * resolvable reference -- the simulator must detect it, proving
     * the check can fail */
    ltr_dpb_init(&d);
    memset(&c, 0, sizeof(c));
    ltr_emit_main_idr(&c, 0, &au);
    ck_assert_int_eq(ltr_dpb_decode(&d, &au, &rv, &rs), 0);
    /* forge the broken shape: aux P selecting LT1 with LT1 empty */
    memset(&au, 0, sizeof(au));
    au.view = LTR_VIEW_AUX;
    au.seq = 1;
    au.frame_num = 1;
    au.slice_p = 1;
    au.ltpn = 1;
    au.mmco6_ltfi = 1;
    ck_assert_int_eq(ltr_dpb_decode(&d, &au, &rv, &rs), 1);
    ck_assert_int_eq(d.missing_ref, 1);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_dpb_sliding_window_exempts_long_term)
{
    struct ltr_dpb d;
    struct ltr_au au;
    int rv;
    int rs;
    int i;
    int have_lt0;
    int have_lt1;
    int j;

    /* adversarial: unmarked short-term reference pictures flood the
     * DPB past max_num_ref_frames; the sliding window must evict ONLY
     * short-terms while LT0/LT1 survive (8.2.5.3 long-term exemption
     * -- the property "our streams rely on nothing else") */
    ltr_dpb_init(&d);
    memset(&au, 0, sizeof(au));
    au.view = LTR_VIEW_MAIN;
    au.is_idr = 1;
    au.ltr_flag = 1;
    au.mmco6_ltfi = -1;
    ck_assert_int_eq(ltr_dpb_decode(&d, &au, &rv, &rs), 0);
    memset(&au, 0, sizeof(au));
    au.view = LTR_VIEW_AUX;
    au.seq = 1;
    au.frame_num = 1;
    au.mmco6_ltfi = 1;      /* seed LT1 */
    ck_assert_int_eq(ltr_dpb_decode(&d, &au, &rv, &rs), 0);
    for (i = 2; i < 12; i++)
    {
        memset(&au, 0, sizeof(au));
        au.view = LTR_VIEW_MAIN;
        au.seq = i;
        au.frame_num = i;
        au.mmco6_ltfi = -1; /* sliding window short-term */
        ck_assert_int_eq(ltr_dpb_decode(&d, &au, &rv, &rs), 0);
    }
    ck_assert_int_gt(d.window_evictions, 0);
    ck_assert_int_eq(d.overflow, 0);
    have_lt0 = 0;
    have_lt1 = 0;
    for (j = 0; j < LTR_DPB_MAX; j++)
    {
        if (d.pics[j].used && d.pics[j].is_long)
        {
            if (d.pics[j].lt_idx == 0)
            {
                have_lt0 = 1;
            }
            if (d.pics[j].lt_idx == 1)
            {
                have_lt1 = 1;
            }
        }
    }
    ck_assert_int_eq(have_lt0, 1);
    ck_assert_int_eq(have_lt1, 1);
    /* and the pinned slots still resolve */
    memset(&au, 0, sizeof(au));
    au.view = LTR_VIEW_AUX;
    au.seq = 99;
    au.frame_num = 12;
    au.slice_p = 1;
    au.ltpn = 1;
    au.mmco6_ltfi = 1;
    ck_assert_int_eq(ltr_dpb_decode(&d, &au, &rv, &rs), 0);
    ck_assert_int_eq(rv, LTR_VIEW_AUX);
    ck_assert_int_eq(rs, 1);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_dpb_range_violation_recorded_not_silent)
{
    struct ltr_run_stats st;
    struct ltr_seq_ctx c;
    int n;
    int k;

    /* the deliberate 7.4.3.3 deviation (no mmco4, Windows shape) is
     * visible exactly where known -- the aux self-marks with ltfi=1
     * while MaxLongTermFrameIdx stays 0 -- and nowhere else. If the
     * recipe ever changes to emit mmco4, this count going to zero
     * must be a conscious test update, not an accident. */
    n = ltr_gen_pairs(10);
    ltr_run_both_modes(g_aus, n, &st);
    /* every aux picture (10 of them) trips the recorded deviation */
    ck_assert_int_eq(st.one_ctx.range_violations, 10);
    ck_assert_int_eq(st.aux_only.range_violations, 10);
    ck_assert_int_eq(st.main_only.range_violations, 0);

    /* contingency documented in the PRD: mmco4 with
     * max_long_term_frame_idx_plus1 = 2 on the first aux makes the
     * same stream fully conforming and changes nothing else */
    memset(&c, 0, sizeof(c));
    n = 0;
    ltr_emit_main_idr(&c, n, &g_aus[n]);
    n++;
    ltr_emit_aux(&c, n, &g_aus[n]);
    g_aus[n].mmco4_plus1 = 2;
    n++;
    for (k = 1; k < 10; k++)
    {
        ltr_emit_main_p(&c, n, &g_aus[n]);
        n++;
        ltr_emit_aux(&c, n, &g_aus[n]);
        n++;
    }
    ltr_run_both_modes(g_aus, n, &st);
    ck_assert_int_eq(st.one_ctx.range_violations, 0);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_frame_num_slots_golden)
{
    struct ltr_seq_ctx c;
    struct ltr_au au;
    int k;

    /* PRD unit-test group 2: golden shared-counter sequence. 1:1
     * cadence must produce frame_num == emission ordinal mod 256 --
     * exactly the measured Win2022 progression (frame_num == AU
     * decode index mod 256, all 357 AUs verified) */
    memset(&c, 0, sizeof(c));
    ltr_emit_main_idr(&c, 0, &au);
    ck_assert_int_eq(au.frame_num, 0);
    for (k = 1; k < 600; k++)
    {
        if (k % 2 == 1)
        {
            ltr_emit_aux(&c, k, &au);
        }
        else
        {
            ltr_emit_main_p(&c, k, &au);
        }
        ck_assert_int_eq(au.frame_num, k % LTR_MAX_FRAME_NUM);
    }
    /* wrap edge vectors: emission 255 -> frame_num 255, emission 256
     * -> frame_num 0 (log2_max_frame_num = 8) */
    memset(&c, 0, sizeof(c));
    ltr_emit_main_idr(&c, 0, &au);
    for (k = 1; k <= 256; k++)
    {
        if (k % 2 == 1)
        {
            ltr_emit_aux(&c, k, &au);
        }
        else
        {
            ltr_emit_main_p(&c, k, &au);
        }
    }
    ck_assert_int_eq(au.frame_num, 0);
}
END_TEST

/*****************************************************************************/
Suite *
make_suite_avc444_ltr(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444Ltr");
    tc = tcase_create("avc444_ltr_dpb");
    tcase_add_test(tc, test_ltr_dpb_resolution_identical_both_modes);
    tcase_add_test(tc, test_ltr_dpb_pinning_512_frames_across_wrap);
    tcase_add_test(tc, test_ltr_dpb_sparse_aux_cadence);
    tcase_add_test(tc, test_ltr_dpb_slot_reassignment_replaces);
    tcase_add_test(tc, test_ltr_dpb_idr_restart_reseeds);
    tcase_add_test(tc, test_ltr_dpb_unseeded_aux_p_is_missing_ref);
    tcase_add_test(tc, test_ltr_dpb_sliding_window_exempts_long_term);
    tcase_add_test(tc, test_ltr_dpb_range_violation_recorded_not_silent);
    tcase_add_test(tc, test_ltr_frame_num_slots_golden);
    suite_add_tcase(s, tc);
    return s;
}
