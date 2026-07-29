#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <string.h>

#include "xrdp_h264_annexb.h"
#include "test_xrdp.h"
#include "test_avc444_ltr_vectors.h"
#include "test_avc444_ltr_cut_vectors.h"

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

/*
 * FR-H264-8 emitter golden-byte vectors (PRD unit-test group 1): the
 * C rewriter's output must be BYTE-IDENTICAL to the independently
 * implemented, ffmpeg-validated python reference splice
 * (PR-demo/mac_bisect_matrix/ltr_splice_ref.py -- validated by
 * 1-context / drop-aux / aux-only framemd5 bit-identity against the
 * children's own decodes, explode-clean strict decode, and the
 * Win2022 trace-histogram shape). Two independent implementations
 * agreeing bit-for-bit is the cross-validation.
 */

/*****************************************************************************/
static int
ltr_run_vector(struct xrdp_h264_ltr_state *st, int view,
               const unsigned char *in, int in_len,
               const unsigned char *golden, int golden_len)
{
    static unsigned char buf[4096];
    int len;
    int cap;
    int rv;

    memcpy(buf, in, in_len);
    len = in_len;
    cap = in_len + xrdp_h264_ltr_growth_budget(buf, len);
    ck_assert_int_le(cap, (int)sizeof(buf));
    rv = (view == 0)
         ? xrdp_h264_ltr_rewrite_main(buf, &len, cap, st)
         : xrdp_h264_ltr_rewrite_aux(buf, &len, cap, st);
    if (rv != 0)
    {
        return rv;
    }
    ck_assert_int_eq(len, golden_len);
    ck_assert_mem_eq(buf, golden, golden_len);
    return 0;
}

/*****************************************************************************/
START_TEST(test_ltr_emitter_golden_sequence)
{
    struct xrdp_h264_ltr_state st;

    memset(&st, 0, sizeof(st));
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_0,
                                    LTR_MAIN_IN_0_LEN, ltr_main_golden_0,
                                    LTR_MAIN_GOLDEN_0_LEN), 0);
    ck_assert_int_eq(st.started, 1);
    ck_assert_int_eq(st.aux_seeded, 0);
    ck_assert_int_eq(st.frame_num, 1);
    ck_assert_int_eq(ltr_run_vector(&st, 1, ltr_aux_in_0,
                                    LTR_AUX_IN_0_LEN, ltr_aux_golden_0,
                                    LTR_AUX_GOLDEN_0_LEN), 0);
    ck_assert_int_eq(st.aux_seeded, 1);
    ck_assert_int_eq(st.frame_num, 2);
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_1,
                                    LTR_MAIN_IN_1_LEN, ltr_main_golden_1,
                                    LTR_MAIN_GOLDEN_1_LEN), 0);
    ck_assert_int_eq(ltr_run_vector(&st, 1, ltr_aux_in_1,
                                    LTR_AUX_IN_1_LEN, ltr_aux_golden_1,
                                    LTR_AUX_GOLDEN_1_LEN), 0);
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_2,
                                    LTR_MAIN_IN_2_LEN, ltr_main_golden_2,
                                    LTR_MAIN_GOLDEN_2_LEN), 0);
    ck_assert_int_eq(ltr_run_vector(&st, 1, ltr_aux_in_2,
                                    LTR_AUX_IN_2_LEN, ltr_aux_golden_2,
                                    LTR_AUX_GOLDEN_2_LEN), 0);
    ck_assert_int_eq(st.frame_num, 6);
    /* the caches were populated from both children and pass the
     * guard the rewrites enforced */
    ck_assert_int_eq(st.main_cache.have_sps && st.main_cache.have_pps,
                     1);
    ck_assert_int_eq(st.aux_cache.have_sps && st.aux_cache.have_pps, 1);
    ck_assert_int_eq(st.main_cache.poc_type, 2);
}
END_TEST

/*
 * Win2022 field-sequence cross-check (PRD unit-test group 1e): parse
 * the marking/modification syntax elements of a REAL Win2022 aux P
 * slice header from the committed capture and of our emitted aux P
 * golden; the element-value sequence must be identical -- the
 * measured recipe, not our reconstruction of it, is the reference.
 */

struct ltr_hdr_bits
{
    const unsigned char *buf;
    int nbits;
    int pos;
    int err;
};

/*****************************************************************************/
static unsigned int
hdr_u(struct ltr_hdr_bits *b, int n)
{
    unsigned int v;

    v = 0;
    while (n-- > 0)
    {
        if (b->pos >= b->nbits)
        {
            b->err = 1;
            return 0;
        }
        v = (v << 1) | ((b->buf[b->pos >> 3] >> (7 - (b->pos & 7))) & 1);
        b->pos++;
    }
    return v;
}

/*****************************************************************************/
static unsigned int
hdr_ue(struct ltr_hdr_bits *b)
{
    int zeros;

    zeros = 0;
    while (hdr_u(b, 1) == 0 && !b->err && zeros < 31)
    {
        zeros++;
    }
    if (b->err)
    {
        return 0;
    }
    return (1u << zeros) - 1 + hdr_u(b, zeros);
}

struct ltr_p_hdr
{
    unsigned int frame_num;
    unsigned int rplm_flag;
    unsigned int mod_idc;
    unsigned int ltpn;
    unsigned int mod_end_idc;
    unsigned int adaptive;
    unsigned int mmco_a;
    unsigned int ltfi;
    unsigned int mmco_end;
};

/*****************************************************************************/
/* parse a first_mb==0 P slice header of the LTR shape (poc_type 2, no
 * emulation bytes in the header region of these vectors) */
static void
ltr_parse_p_hdr(const unsigned char *nal, int len, int log2_mfn,
                struct ltr_p_hdr *h)
{
    struct ltr_hdr_bits b;

    b.buf = nal + 1;
    b.nbits = (len - 1) * 8;
    b.pos = 0;
    b.err = 0;
    ck_assert_int_eq(hdr_ue(&b), 0);        /* first_mb_in_slice */
    ck_assert_int_eq(hdr_ue(&b) % 5, 0);    /* slice_type P */
    hdr_ue(&b);                             /* pps id */
    h->frame_num = hdr_u(&b, log2_mfn);
    ck_assert_int_eq(hdr_u(&b, 1), 0);      /* num_ref_idx override */
    h->rplm_flag = hdr_u(&b, 1);
    h->mod_idc = hdr_ue(&b);
    h->ltpn = hdr_ue(&b);
    h->mod_end_idc = hdr_ue(&b);
    h->adaptive = hdr_u(&b, 1);
    h->mmco_a = hdr_ue(&b);
    h->ltfi = hdr_ue(&b);
    h->mmco_end = hdr_ue(&b);
    ck_assert_int_eq(b.err, 0);
}

/*****************************************************************************/
START_TEST(test_ltr_win2022_field_sequence_cross_check)
{
    struct ltr_p_hdr win;
    struct ltr_p_hdr ours;
    const unsigned char *nal;

    /* Win2022 later-aux P slice: log2_max_frame_num = 8 */
    ck_assert_int_eq(win2022_aux_p_hdr[0], 0x61);  /* nri=3 type=1 */
    ltr_parse_p_hdr(win2022_aux_p_hdr, WIN2022_AUX_P_HDR_LEN, 8, &win);
    ck_assert_int_eq((int)win.frame_num, 13);
    /* our aux P golden packet: skip the 4-byte start code; our chain
     * carries the widened 16-bit frame_num */
    nal = ltr_aux_golden_1 + 4;
    ck_assert_int_eq(nal[0], 0x61);
    ltr_parse_p_hdr(nal, LTR_AUX_GOLDEN_1_LEN - 4, 16, &ours);
    /* the element-value sequence of the measured recipe */
    ck_assert_int_eq((int)win.rplm_flag, 1);
    ck_assert_int_eq((int)win.mod_idc, 2);
    ck_assert_int_eq((int)win.ltpn, 1);
    ck_assert_int_eq((int)win.mod_end_idc, 3);
    ck_assert_int_eq((int)win.adaptive, 1);
    ck_assert_int_eq((int)win.mmco_a, 6);
    ck_assert_int_eq((int)win.ltfi, 1);
    ck_assert_int_eq((int)win.mmco_end, 0);
    ck_assert_int_eq((int)ours.rplm_flag, (int)win.rplm_flag);
    ck_assert_int_eq((int)ours.mod_idc, (int)win.mod_idc);
    ck_assert_int_eq((int)ours.ltpn, (int)win.ltpn);
    ck_assert_int_eq((int)ours.mod_end_idc, (int)win.mod_end_idc);
    ck_assert_int_eq((int)ours.adaptive, (int)win.adaptive);
    ck_assert_int_eq((int)ours.mmco_a, (int)win.mmco_a);
    ck_assert_int_eq((int)ours.ltfi, (int)win.ltfi);
    ck_assert_int_eq((int)ours.mmco_end, (int)win.mmco_end);
    /* the Windows first-aux quirk we deliberately do NOT copy: their
     * first aux selects ltpn=0 (the main IDR); ours is a
     * self-contained I (type 1 slice_type I, no list modification) */
    ltr_parse_p_hdr(win2022_first_aux_p_hdr,
                    WIN2022_FIRST_AUX_P_HDR_LEN, 8, &win);
    ck_assert_int_eq((int)win.ltpn, 0);
    ck_assert_int_eq((int)win.ltfi, 1);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_rejects_existing_list_modification)
{
    /* PRD group 4: a child P slice that already carries a ref-pic-list
     * modification must hard-reject, never silently emit. Crafted by
     * setting the rplm_l0 bit of a real child P slice at its true bit
     * offset (parsed with the child SPS width). */
    struct xrdp_h264_ltr_state st;
    struct ltr_hdr_bits b;
    static unsigned char buf[4096];
    int len;
    int rv;
    int pos;

    memset(&st, 0, sizeof(st));
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_0,
                                    LTR_MAIN_IN_0_LEN, ltr_main_golden_0,
                                    LTR_MAIN_GOLDEN_0_LEN), 0);
    memcpy(buf, ltr_main_in_1, LTR_MAIN_IN_1_LEN);
    len = LTR_MAIN_IN_1_LEN;
    /* locate the rplm_l0 flag: first_mb ue, slice_type ue, pps ue,
     * frame_num u(child log2), override u(1) -> next bit */
    b.buf = buf + 5;               /* 4-byte start code + NAL header */
    b.nbits = (len - 5) * 8;
    b.pos = 0;
    b.err = 0;
    hdr_ue(&b);
    hdr_ue(&b);
    hdr_ue(&b);
    hdr_u(&b, st.main_cache.log2_max_frame_num);
    ck_assert_int_eq((int)hdr_u(&b, 1), 0);    /* override flag */
    ck_assert_int_eq(b.err, 0);
    pos = b.pos;
    ck_assert_int_eq((buf[5 + (pos >> 3)] >> (7 - (pos & 7))) & 1, 0);
    buf[5 + (pos >> 3)] |= (unsigned char)(0x80 >> (pos & 7));
    rv = xrdp_h264_ltr_rewrite_main(buf, &len, (int)sizeof(buf),
                                    &st);
    ck_assert_int_ne(rv, 0);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_rejects_unseeded_aux_p)
{
    /* an aux P while LT1 is unseeded (right after a main IDR) must
     * fail loudly: the runner restarts the aux child instead */
    struct xrdp_h264_ltr_state st;
    static unsigned char buf[4096];
    int len;
    int rv;

    memset(&st, 0, sizeof(st));
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_0,
                                    LTR_MAIN_IN_0_LEN, ltr_main_golden_0,
                                    LTR_MAIN_GOLDEN_0_LEN), 0);
    ck_assert_int_eq(ltr_run_vector(&st, 1, ltr_aux_in_0,
                                    LTR_AUX_IN_0_LEN, ltr_aux_golden_0,
                                    LTR_AUX_GOLDEN_0_LEN), 0);
    /* simulate the mid-stream main IDR having just reset the chain */
    st.aux_seeded = 0;
    memcpy(buf, ltr_aux_in_1, LTR_AUX_IN_1_LEN);
    len = LTR_AUX_IN_1_LEN;
    rv = xrdp_h264_ltr_rewrite_aux(buf, &len, (int)sizeof(buf),
                                   &st);
    ck_assert_int_ne(rv, 0);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_rejects_main_start_without_idr)
{
    struct xrdp_h264_ltr_state st;
    static unsigned char buf[4096];
    int len;
    int rv;

    memset(&st, 0, sizeof(st));
    memcpy(buf, ltr_main_in_1, LTR_MAIN_IN_1_LEN);
    len = LTR_MAIN_IN_1_LEN;
    rv = xrdp_h264_ltr_rewrite_main(buf, &len, (int)sizeof(buf),
                                    &st);
    ck_assert_int_ne(rv, 0);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_rejects_truncated_and_small_cap)
{
    struct xrdp_h264_ltr_state st;
    static unsigned char buf[4096];
    int len;
    int rv;

    /* truncated mid-slice */
    memset(&st, 0, sizeof(st));
    memcpy(buf, ltr_main_in_0, 200);
    len = 200;
    rv = xrdp_h264_ltr_rewrite_main(buf, &len, (int)sizeof(buf),
                                    &st);
    ck_assert_int_ne(rv, 0);
    /* an undersized caller buffer must fail, never overflow: the aux
     * P rewrite GROWS (in 13 -> golden 17 bytes) */
    memset(&st, 0, sizeof(st));
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_0,
                                    LTR_MAIN_IN_0_LEN, ltr_main_golden_0,
                                    LTR_MAIN_GOLDEN_0_LEN), 0);
    ck_assert_int_eq(ltr_run_vector(&st, 1, ltr_aux_in_0,
                                    LTR_AUX_IN_0_LEN, ltr_aux_golden_0,
                                    LTR_AUX_GOLDEN_0_LEN), 0);
    memcpy(buf, ltr_aux_in_1, LTR_AUX_IN_1_LEN);
    len = LTR_AUX_IN_1_LEN;
    rv = xrdp_h264_ltr_rewrite_aux(buf, &len, LTR_AUX_IN_1_LEN,
                                   &st);
    ck_assert_int_ne(rv, 0);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_emitter_high_counter_and_cadence)
{
    /* the real rewriter past the golden range: high shared-counter
     * values (65000; and 256, whose 16-bit field is zero-run heavy --
     * the emulation-prevention interplay case), and a sparse M,M,A
     * cadence through the REAL code, verified by parsing the emitted
     * headers (log2 = 16) rather than byte goldens */
    struct xrdp_h264_ltr_state st;
    struct ltr_p_hdr h;
    static unsigned char buf[4096];
    int len;

    memset(&st, 0, sizeof(st));
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_0,
                                    LTR_MAIN_IN_0_LEN, ltr_main_golden_0,
                                    LTR_MAIN_GOLDEN_0_LEN), 0);
    ck_assert_int_eq(ltr_run_vector(&st, 1, ltr_aux_in_0,
                                    LTR_AUX_IN_0_LEN, ltr_aux_golden_0,
                                    LTR_AUX_GOLDEN_0_LEN), 0);
    st.frame_num = 65000;
    memcpy(buf, ltr_main_in_1, LTR_MAIN_IN_1_LEN);
    len = LTR_MAIN_IN_1_LEN;
    ck_assert_int_eq(xrdp_h264_ltr_rewrite_main(buf, &len,
                     (int)sizeof(buf), &st),
                     0);
    ltr_parse_p_hdr(buf + 4, len - 4, 16, &h);
    ck_assert_int_eq((int)h.frame_num, 65000);
    ck_assert_int_eq((int)h.ltpn, 0);
    ck_assert_int_eq((int)h.ltfi, 0);
    ck_assert_int_eq(st.frame_num, 65001);
    /* sparse cadence: a second main picture with NO aux between */
    memcpy(buf, ltr_main_in_2, LTR_MAIN_IN_2_LEN);
    len = LTR_MAIN_IN_2_LEN;
    ck_assert_int_eq(xrdp_h264_ltr_rewrite_main(buf, &len,
                     (int)sizeof(buf), &st),
                     0);
    ltr_parse_p_hdr(buf + 4, len - 4, 16, &h);
    ck_assert_int_eq((int)h.frame_num, 65001);
    /* then the aux, still resolving LT1 with the shared counter */
    memcpy(buf, ltr_aux_in_2, LTR_AUX_IN_2_LEN);
    len = LTR_AUX_IN_2_LEN;
    ck_assert_int_eq(xrdp_h264_ltr_rewrite_aux(buf, &len,
                     (int)sizeof(buf), &st),
                     0);
    ltr_parse_p_hdr(buf + 4, len - 4, 16, &h);
    ck_assert_int_eq((int)h.frame_num, 65002);
    ck_assert_int_eq((int)h.ltpn, 1);
    ck_assert_int_eq((int)h.ltfi, 1);
    /* the zero-run frame_num (0x0100): 16-bit field 00000001 00000000
     * feeds the re-escape with long zero runs */
    st.frame_num = 256;
    memcpy(buf, ltr_main_in_1, LTR_MAIN_IN_1_LEN);
    len = LTR_MAIN_IN_1_LEN;
    ck_assert_int_eq(xrdp_h264_ltr_rewrite_main(buf, &len,
                     (int)sizeof(buf), &st),
                     0);
    ltr_parse_p_hdr(buf + 4, len - 4, 16, &h);
    ck_assert_int_eq((int)h.frame_num, 256);
}
END_TEST

/*****************************************************************************/
/* the first VCL NAL of an annex-b packet (4-byte start codes) */
static int
ltr_first_vcl(const unsigned char *p, int len)
{
    int i;

    for (i = 0; i + 4 < len; i++)
    {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1)
        {
            if ((p[i + 4] & 0x1f) == 1 || (p[i + 4] & 0x1f) == 5)
            {
                return i + 4;
            }
        }
    }
    return -1;
}

/*****************************************************************************/
START_TEST(test_ltr_emitter_epoch_restart_byte_exact)
{
    /* An EPOCH restart is the frame_num-wrap re-key: the runner
     * destroys and recreates the encoder, so the rewriter starts from a
     * zeroed state and the same child packets must reproduce the same
     * golden bytes, byte for byte, including the real IDR and the LT1
     * re-seed.
     *
     * RE-SCOPED for BACKLOG #45 step 2 (recorded as C7). This test used
     * to feed the IDR-carrying packet MID-STREAM and assert
     * st.aux_seeded == 0 with st.frame_num == 1 -- i.e. it asserted the
     * DPB flush that FR-H264-6 abolishes. That case is now a scheduled
     * refresh and is covered by test_ltr_cut_midstream_idr_keeps_chain;
     * the second half below pins the difference explicitly so the two
     * paths can never be confused again. */
    struct xrdp_h264_ltr_state st;
    static unsigned char buf[4096];
    int len;
    int cap;
    int rv;
    int vcl;

    memset(&st, 0, sizeof(st));
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_0,
                                    LTR_MAIN_IN_0_LEN, ltr_main_golden_0,
                                    LTR_MAIN_GOLDEN_0_LEN), 0);
    ck_assert_int_eq(ltr_run_vector(&st, 1, ltr_aux_in_0,
                                    LTR_AUX_IN_0_LEN, ltr_aux_golden_0,
                                    LTR_AUX_GOLDEN_0_LEN), 0);
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_1,
                                    LTR_MAIN_IN_1_LEN, ltr_main_golden_1,
                                    LTR_MAIN_GOLDEN_1_LEN), 0);
    /* the re-key: the encoder object is gone, so the state is fresh */
    memset(&st, 0, sizeof(st));
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_0,
                                    LTR_MAIN_IN_0_LEN, ltr_main_golden_0,
                                    LTR_MAIN_GOLDEN_0_LEN), 0);
    ck_assert_int_eq(st.aux_seeded, 0);
    ck_assert_int_eq(st.frame_num, 1);
    /* aux P before the fresh epoch's LT1 seed: loud failure */
    memcpy(buf, ltr_aux_in_1, LTR_AUX_IN_1_LEN);
    len = LTR_AUX_IN_1_LEN;
    rv = xrdp_h264_ltr_rewrite_aux(buf, &len, (int)sizeof(buf),
                                   &st);
    ck_assert_int_ne(rv, 0);
    /* the fresh-epoch aux IDR re-seeds and replays byte-exact */
    ck_assert_int_eq(ltr_run_vector(&st, 1, ltr_aux_in_0,
                                    LTR_AUX_IN_0_LEN, ltr_aux_golden_0,
                                    LTR_AUX_GOLDEN_0_LEN), 0);
    ck_assert_int_eq(st.aux_seeded, 1);
    /* and the contrast that step 2 introduced: the SAME packet fed
     * MID-STREAM is a refresh, not an epoch restart -- the counter
     * keeps running, LT1 survives, the picture ships as a non-IDR I,
     * and the child's repeated parameter sets are dropped (D18), so it
     * is strictly shorter than the epoch-entry golden */
    memcpy(buf, ltr_main_in_0, LTR_MAIN_IN_0_LEN);
    len = LTR_MAIN_IN_0_LEN;
    cap = len + xrdp_h264_ltr_growth_budget(buf, len);
    ck_assert_int_le(cap, (int)sizeof(buf));
    ck_assert_int_eq(xrdp_h264_ltr_rewrite_main(buf, &len, cap, &st), 0);
    ck_assert_int_eq(st.frame_num, 3);
    ck_assert_int_eq(st.aux_seeded, 1);
    ck_assert_int_lt(len, LTR_MAIN_GOLDEN_0_LEN);
    vcl = ltr_first_vcl(buf, len);
    ck_assert_int_ge(vcl, 0);
    ck_assert_int_eq(buf[vcl] & 0x1f, 1);
    /* the first NAL of the packet IS the picture: no SPS, no PPS, no
     * SEI ahead of it */
    ck_assert_int_eq(vcl, 4);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_seed_i_pins_leaf_diff)
{
    /* PRD group 1(d): the seed I is the FR-H264-7 leaf conversion of
     * the same aux IDR with only the NAL header (nri 0 -> 3) and the
     * marking syntax as deltas; the CABAC payload tail must be
     * byte-identical between the two conversions */
    struct xrdp_h264_param_cache mc;
    struct xrdp_h264_param_cache ac;
    static unsigned char leaf[4096];
    int leaf_len;
    int tail;

    memset(&mc, 0, sizeof(mc));
    memset(&ac, 0, sizeof(ac));
    memcpy(leaf, ltr_aux_in_0, LTR_AUX_IN_0_LEN);
    leaf_len = LTR_AUX_IN_0_LEN;
    ck_assert_int_eq(xrdp_h264_aux_to_leaf(leaf, &leaf_len,
                                           ltr_main_in_0,
                                           LTR_MAIN_IN_0_LEN,
                                           &mc, &ac), 0);
    /* leaf: non-reference type 1; seed: reference (nri 3) type 1 */
    ck_assert_int_eq(leaf[4] & 0x1f, 1);
    ck_assert_int_eq((leaf[4] >> 5) & 3, 0);
    ck_assert_int_eq(ltr_aux_golden_0[4] & 0x1f, 1);
    ck_assert_int_eq((ltr_aux_golden_0[4] >> 5) & 3, 3);
    /* identical CABAC payload tail (both conversions copy the child
     * payload byte-verbatim after their differing headers) */
    tail = (leaf_len < LTR_AUX_GOLDEN_0_LEN
            ? leaf_len : LTR_AUX_GOLDEN_0_LEN) - 64;
    ck_assert_int_gt(tail, 100);
    ck_assert_mem_eq(leaf + leaf_len - tail,
                     ltr_aux_golden_0 + LTR_AUX_GOLDEN_0_LEN - tail,
                     tail);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_main_golden_recipe_fields)
{
    /* the main view's constant recipe syntax, parsed from the golden
     * (the aux view gets the independent Win2022 cross-check; this
     * pins the main-view constants semantically, not just by python
     * golden bytes) */
    struct ltr_p_hdr h;

    ck_assert_int_eq(ltr_main_golden_1[4], 0x61);
    ltr_parse_p_hdr(ltr_main_golden_1 + 4, LTR_MAIN_GOLDEN_1_LEN - 4,
                    16, &h);
    ck_assert_int_eq((int)h.frame_num, 2);
    ck_assert_int_eq((int)h.rplm_flag, 1);
    ck_assert_int_eq((int)h.mod_idc, 2);
    ck_assert_int_eq((int)h.ltpn, 0);
    ck_assert_int_eq((int)h.mod_end_idc, 3);
    ck_assert_int_eq((int)h.adaptive, 1);
    ck_assert_int_eq((int)h.mmco_a, 6);
    ck_assert_int_eq((int)h.ltfi, 0);
    ck_assert_int_eq((int)h.mmco_end, 0);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_win2022_sps_fields)
{
    /* turn the vector-header comments into assertions: the Win2022
     * SPS the cross-check parse widths depend on (Main profile, no
     * chroma block) */
    struct ltr_hdr_bits b;

    ck_assert_int_eq(win2022_sps[0], 0x67);
    b.buf = win2022_sps + 1;
    b.nbits = (WIN2022_SPS_LEN - 1) * 8;
    b.pos = 0;
    b.err = 0;
    ck_assert_int_eq((int)hdr_u(&b, 8), 77);   /* profile Main */
    hdr_u(&b, 8);                              /* constraints */
    ck_assert_int_eq((int)hdr_u(&b, 8), 32);   /* level 3.2 */
    ck_assert_int_eq((int)hdr_ue(&b), 0);      /* sps id */
    ck_assert_int_eq((int)hdr_ue(&b) + 4, 8);  /* log2_max_frame_num */
    ck_assert_int_eq((int)hdr_ue(&b), 2);      /* poc_type */
    ck_assert_int_eq((int)hdr_ue(&b), 3);      /* max_num_ref_frames */
    ck_assert_int_eq((int)hdr_u(&b, 1), 0);    /* gaps allowed */
    ck_assert_int_eq(b.err, 0);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_rejects_b_slice_and_unknown_level)
{
    /* PRD group 4: unexpected slice_type and an SPS the level-DPB
     * check cannot price must hard-reject -- and reject WITHOUT
     * touching the caller's buffer or state (no half-rewrite) */
    struct xrdp_h264_ltr_state st;
    struct xrdp_h264_ltr_state st_snap;
    static unsigned char buf[4096];
    static unsigned char snap[4096];
    int len;
    int rv;

    /* synthetic B slice: hdr 0x41 then first_mb ue(0)=1,
     * slice_type ue(1)=010 (B), pps ue(0)=1, frame_num u(4)=0001,
     * padding; bits: 1 010 1 0001 ... -> 0xA8, 0x80 */
    memset(&st, 0, sizeof(st));
    ck_assert_int_eq(ltr_run_vector(&st, 0, ltr_main_in_0,
                                    LTR_MAIN_IN_0_LEN, ltr_main_golden_0,
                                    LTR_MAIN_GOLDEN_0_LEN), 0);
    st_snap = st;
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 1;
    buf[4] = 0x41;
    buf[5] = 0xa8;
    buf[6] = 0x80;
    buf[7] = 0xff;
    len = 8;
    memcpy(snap, buf, len);
    rv = xrdp_h264_ltr_rewrite_main(buf, &len, (int)sizeof(buf),
                                    &st);
    ck_assert_int_ne(rv, 0);
    ck_assert_int_eq(len, 8);
    ck_assert_mem_eq(buf, snap, 8);
    ck_assert_int_eq(memcmp(&st, &st_snap, sizeof(st)), 0);
    /* unknown level_idc: patch the SPS level byte of the IDR packet */
    memset(&st, 0, sizeof(st));
    memcpy(buf, ltr_main_in_0, LTR_MAIN_IN_0_LEN);
    ck_assert_int_eq(buf[4] & 0x1f, 7);   /* first NAL is the SPS */
    buf[7] = 99;                          /* level_idc: unknown */
    len = LTR_MAIN_IN_0_LEN;
    memcpy(snap, buf, len);
    st_snap = st;
    rv = xrdp_h264_ltr_rewrite_main(buf, &len, (int)sizeof(buf),
                                    &st);
    ck_assert_int_ne(rv, 0);
    ck_assert_int_eq(len, LTR_MAIN_IN_0_LEN);
    ck_assert_mem_eq(buf, snap, len);
    /* the state must not have advanced (caches may have been read,
     * but the chain counters/flags are untouched) */
    ck_assert_int_eq(st.frame_num, st_snap.frame_num);
    ck_assert_int_eq(st.started, st_snap.started);
    ck_assert_int_eq(st.aux_seeded, st_snap.aux_seeded);
}
END_TEST

/*
 * BACKLOG #45 step 0 -- scheduled paired-cut ratchets.
 *
 * A scheduled intra refresh (PRD FR-H264-6) replaces the IDR/respawn
 * pair: at each scheduled index BOTH views emit an intra picture that
 * self-marks its OWN long-term slot, the shared frame_num counter keeps
 * running, and neither view's slot is disturbed. The two shapes a real
 * child encoder produces at a forced key frame are BOTH covered:
 *   non-IDR I     h264_nvenc without -forced-idr;
 *   mid-stream IDR h264_vaapi, always.
 * Vectors: tests/xrdp/test_avc444_ltr_cut_vectors.h, generated by the
 * independent python reference splicer and validated by decoding both
 * the 1-context and the 2-context feed with ffmpeg (bit-identical, and
 * strict -err_detect explode).
 */

/*****************************************************************************/
/* parse a first_mb==0 non-IDR I slice header of the cut shape: no
 * ref_pic_list_modification (an I slice has none), then the constant
 * self-mark [adaptive=1, mmco6 ltfi=view, mmco0] */
static void
ltr_parse_i_hdr(const unsigned char *nal, int len, int log2_mfn,
                struct ltr_p_hdr *h)
{
    struct ltr_hdr_bits b;

    b.buf = nal + 1;
    b.nbits = (len - 1) * 8;
    b.pos = 0;
    b.err = 0;
    ck_assert_int_eq(hdr_ue(&b), 0);        /* first_mb_in_slice */
    ck_assert_int_eq(hdr_ue(&b) % 5, 2);    /* slice_type I */
    hdr_ue(&b);                             /* pps id */
    h->frame_num = hdr_u(&b, log2_mfn);
    h->rplm_flag = 0;
    h->mod_idc = 0;
    h->ltpn = 0;
    h->mod_end_idc = 0;
    h->adaptive = hdr_u(&b, 1);
    h->mmco_a = hdr_ue(&b);
    h->ltfi = hdr_ue(&b);
    h->mmco_end = hdr_ue(&b);
    ck_assert_int_eq(b.err, 0);
}

/*****************************************************************************/
START_TEST(test_ltr_cut_sequence_byte_exact)
{
    /* the whole 12-picture scheduled-cut chain, byte-exact against the
     * python reference. RED against a rewriter that rejects a non-IDR
     * I input or that restarts the counter at a mid-stream main IDR. */
    struct xrdp_h264_ltr_state st;
    static unsigned char buf[8192];
    int k;
    int len;
    int cap;
    int rv;
    int vcl;

    memset(&st, 0, sizeof(st));
    for (k = 0; k < LTR_CUT_SEQ_COUNT; k++)
    {
        const struct ltr_cut_vec *v = &ltr_cut_seq[k];

        ck_assert_int_le(v->in_len, (int)sizeof(buf) - 64);
        memcpy(buf, v->in, v->in_len);
        len = v->in_len;
        cap = v->in_len + xrdp_h264_ltr_growth_budget(buf, len);
        ck_assert_int_le(cap, (int)sizeof(buf));
        rv = (v->view == LTR_CUT_VIEW_MAIN)
             ? xrdp_h264_ltr_rewrite_main(buf, &len, cap, &st)
             : xrdp_h264_ltr_rewrite_aux(buf, &len, cap, &st);
        ck_assert_int_eq(rv, 0);
        ck_assert_int_eq(len, v->golden_len);
        ck_assert_mem_eq(buf, v->golden, v->golden_len);
        /* the shared counter advanced by exactly one picture */
        ck_assert_int_eq(st.frame_num, v->fn + 1);
        /* the chain is running and the aux slot survives every cut
         * after the first aux seed */
        ck_assert_int_eq(st.started, 1);
        if (k >= 1)
        {
            ck_assert_int_eq(st.aux_seeded, 1);
        }
        vcl = ltr_first_vcl(buf, len);
        ck_assert_int_ge(vcl, 0);
        if (k == 0)
        {
            /* only the stream-start picture is a real IDR */
            ck_assert_int_eq(buf[vcl] & 0x1f, 5);
        }
        else
        {
            ck_assert_int_eq(buf[vcl] & 0x1f, 1);
        }
    }
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_cut_nonidr_i_accepted_both_views)
{
    /* the h264_nvenc shape: a non-IDR I input, in BOTH views. Today's
     * rewriter rejects it outright (xrdp_h264_annexb.c "B/SP/SI or
     * non-IDR I"), so this is RED before step 1. */
    struct xrdp_h264_ltr_state st;
    static unsigned char buf[8192];
    struct ltr_p_hdr h;
    int k;
    int len;
    int cap;
    int vcl;

    memset(&st, 0, sizeof(st));
    for (k = 0; k < 6; k++)
    {
        const struct ltr_cut_vec *v = &ltr_cut_seq[k];

        memcpy(buf, v->in, v->in_len);
        len = v->in_len;
        cap = v->in_len + xrdp_h264_ltr_growth_budget(buf, len);
        ck_assert_int_eq((v->view == LTR_CUT_VIEW_MAIN)
                         ? xrdp_h264_ltr_rewrite_main(buf, &len, cap, &st)
                         : xrdp_h264_ltr_rewrite_aux(buf, &len, cap, &st),
                         0);
        if (k < 4)
        {
            continue;
        }
        /* pictures 4 and 5 are the paired non-IDR I cut */
        ck_assert_int_eq(v->kind, LTR_CUT_KIND_NONIDR_I);
        vcl = ltr_first_vcl(buf, len);
        ck_assert_int_ge(vcl, 0);
        ck_assert_int_eq(buf[vcl] & 0x1f, 1);      /* not an IDR */
        ck_assert_int_eq((buf[vcl] >> 5) & 3, 3);  /* nri = 3 */
        ltr_parse_i_hdr(buf + vcl, len - vcl, 16, &h);
        ck_assert_int_eq((int)h.frame_num, v->fn);
        ck_assert_int_eq((int)h.adaptive, 1);
        ck_assert_int_eq((int)h.mmco_a, 6);
        /* each view self-marks its OWN slot */
        ck_assert_int_eq((int)h.ltfi,
                         v->view == LTR_CUT_VIEW_MAIN ? 0 : 1);
        ck_assert_int_eq((int)h.mmco_end, 0);
    }
    /* the cut seeded both slots without emptying either */
    ck_assert_int_eq(st.started, 1);
    ck_assert_int_eq(st.aux_seeded, 1);
    ck_assert_int_eq(st.frame_num, 6);
}
END_TEST

/*****************************************************************************/
START_TEST(test_ltr_cut_midstream_idr_keeps_chain)
{
    /* the h264_vaapi shape: a mid-stream main IDR is a scheduled
     * refresh, not a stream restart. It must become the same
     * self-contained I, keep the shared counter running and leave the
     * aux view's LT1 alone -- today it resets the counter to 0 and
     * clears aux_seeded, so this is RED before step 1. */
    struct xrdp_h264_ltr_state st;
    static unsigned char buf[8192];
    struct ltr_p_hdr h;
    int k;
    int len;
    int cap;
    int vcl;

    memset(&st, 0, sizeof(st));
    for (k = 0; k <= 10; k++)
    {
        const struct ltr_cut_vec *v = &ltr_cut_seq[k];

        memcpy(buf, v->in, v->in_len);
        len = v->in_len;
        cap = v->in_len + xrdp_h264_ltr_growth_budget(buf, len);
        ck_assert_int_eq((v->view == LTR_CUT_VIEW_MAIN)
                         ? xrdp_h264_ltr_rewrite_main(buf, &len, cap, &st)
                         : xrdp_h264_ltr_rewrite_aux(buf, &len, cap, &st),
                         0);
    }
    ck_assert_int_eq(ltr_cut_seq[10].kind, LTR_CUT_KIND_MIDSTREAM_IDR);
    vcl = ltr_first_vcl(buf, len);
    ck_assert_int_ge(vcl, 0);
    ck_assert_int_eq(buf[vcl] & 0x1f, 1);     /* converted, not an IDR */
    ltr_parse_i_hdr(buf + vcl, len - vcl, 16, &h);
    ck_assert_int_eq((int)h.frame_num, 10);   /* counter continued */
    ck_assert_int_eq((int)h.mmco_a, 6);
    ck_assert_int_eq((int)h.ltfi, 0);         /* main re-marks LT0 */
    ck_assert_int_eq(st.frame_num, 11);
    ck_assert_int_eq(st.aux_seeded, 1);       /* LT1 survived */
    /* and the aux P that follows still resolves LT1: the property the
     * abolished aux respawn existed to restore */
    memcpy(buf, ltr_cut_seq[11].in, ltr_cut_seq[11].in_len);
    len = ltr_cut_seq[11].in_len;
    cap = len + xrdp_h264_ltr_growth_budget(buf, len);
    ck_assert_int_eq(xrdp_h264_ltr_rewrite_aux(buf, &len, cap, &st), 0);
    ck_assert_int_eq(len, ltr_cut_seq[11].golden_len);
    ck_assert_mem_eq(buf, ltr_cut_seq[11].golden,
                     ltr_cut_seq[11].golden_len);
}
END_TEST

/*****************************************************************************/
/* feed one cut vector, return the rewriter's verdict */
static int
ltr_cut_feed(struct xrdp_h264_ltr_state *st, int k, unsigned char *buf,
             int buf_size, int *len_out)
{
    const struct ltr_cut_vec *v = &ltr_cut_seq[k];
    int len;
    int cap;
    int rv;

    memcpy(buf, v->in, v->in_len);
    len = v->in_len;
    cap = v->in_len + xrdp_h264_ltr_growth_budget(buf, len);
    ck_assert_int_le(cap, buf_size);
    rv = (v->view == LTR_CUT_VIEW_MAIN)
         ? xrdp_h264_ltr_rewrite_main(buf, &len, cap, st)
         : xrdp_h264_ltr_rewrite_aux(buf, &len, cap, st);
    if (len_out != NULL)
    {
        *len_out = len;
    }
    return rv;
}

/*****************************************************************************/
START_TEST(test_ltr_schedule_observed_vs_requested)
{
    /* FR-H264-6 layer 3: the refresh is OBSERVED, never assumed. Both
     * children are spawned with the same frame-indexed schedule, so at
     * a scheduled ordinal an intra picture is DUE in that view; a P
     * there means the encoder silently skipped the cut and the pair
     * must fail instead of shipping a stream whose prediction chain is
     * longer than the wire claims. An intra picture OFF schedule is
     * equally a mismatch (an unscheduled IDR, or a de-phased child).
     *
     * The cut vector sequence has its intra pictures at view ordinals
     * 0, 2 and 4, so a declared period of 2 matches it up to picture 8
     * and then diverges: picture 9 is an aux P where the schedule says
     * a cut is due. */
    struct xrdp_h264_ltr_state st;
    static unsigned char buf[8192];
    int k;
    int fn_before;

    memset(&st, 0, sizeof(st));
    st.refresh_period = 2;
    for (k = 0; k <= 8; k++)
    {
        ck_assert_int_eq(ltr_cut_feed(&st, k, buf, (int)sizeof(buf),
                                      NULL), 0);
    }
    /* the aux picture where a cut was scheduled but a P arrived */
    fn_before = st.frame_num;
    ck_assert_int_ne(ltr_cut_feed(&st, 9, buf, (int)sizeof(buf), NULL), 0);
    /* a rejected packet leaves the chain state untouched */
    ck_assert_int_eq(st.frame_num, fn_before);
    ck_assert_int_eq(st.pic_index[1], 4);

    /* the other direction: an intra picture arriving OFF schedule.
     * With a declared period of 3 the cut at view ordinal 2
     * (picture 4) is unscheduled. */
    memset(&st, 0, sizeof(st));
    st.refresh_period = 3;
    for (k = 0; k <= 3; k++)
    {
        ck_assert_int_eq(ltr_cut_feed(&st, k, buf, (int)sizeof(buf),
                                      NULL), 0);
    }
    ck_assert_int_ne(ltr_cut_feed(&st, 4, buf, (int)sizeof(buf), NULL), 0);

    /* and with NO period declared the check is inert: the same
     * sequence that failed above is accepted, which is what keeps the
     * unscheduled diagnostic arms and the byte-golden vectors working */
    memset(&st, 0, sizeof(st));
    for (k = 0; k <= 9; k++)
    {
        ck_assert_int_eq(ltr_cut_feed(&st, k, buf, (int)sizeof(buf),
                                      NULL), 0);
    }
}
END_TEST

/*
 * The same schedule at the DPB level, in BOTH client decode modes.
 * This is a model check over the simulator above (it cannot fail
 * against today's C rewriter -- the shapes it feeds are the ones the
 * rewriter is being taught to emit), so it is documented as the
 * INVARIANT half of the ratchet: what a scheduled paired cut must mean
 * in a decoder. The RED-first half is the three vector tests above.
 */

/*****************************************************************************/
static void
ltr_emit_main_cut_i(struct ltr_seq_ctx *c, int seq, struct ltr_au *au)
{
    memset(au, 0, sizeof(*au));
    au->view = LTR_VIEW_MAIN;
    au->seq = seq;
    au->frame_num = c->frame_num;
    au->slice_p = 0;        /* intra: references nothing */
    au->mmco6_ltfi = 0;     /* re-marks LT0, replacing the occupant */
    c->frame_num = (c->frame_num + 1) % LTR_MAX_FRAME_NUM;
}

/*****************************************************************************/
static void
ltr_emit_aux_cut_i(struct ltr_seq_ctx *c, int seq, struct ltr_au *au)
{
    memset(au, 0, sizeof(*au));
    au->view = LTR_VIEW_AUX;
    au->seq = seq;
    au->frame_num = c->frame_num;
    au->slice_p = 0;
    au->mmco6_ltfi = 1;     /* re-marks LT1 */
    c->frame_num = (c->frame_num + 1) % LTR_MAX_FRAME_NUM;
    c->aux_seeded = 1;
}

/*****************************************************************************/
START_TEST(test_ltr_dpb_scheduled_paired_cut_both_modes)
{
    struct ltr_run_stats st;
    struct ltr_seq_ctx c;
    int cut_period;
    int n_pairs;
    int depth[2];
    int worst[2];
    int cuts;
    int n;
    int k;
    int i;

    /* 400 pairs at a 24-pair schedule: 16 scheduled paired cuts, no
     * IDR after the stream start */
    cut_period = 24;
    n_pairs = 400;
    memset(&c, 0, sizeof(c));
    n = 0;
    ltr_emit_main_idr(&c, n, &g_aus[n]);
    n++;
    ltr_emit_aux(&c, n, &g_aus[n]);
    n++;
    cuts = 0;
    for (k = 1; k < n_pairs; k++)
    {
        if (k % cut_period == 0)
        {
            ltr_emit_main_cut_i(&c, n, &g_aus[n]);
            n++;
            ltr_emit_aux_cut_i(&c, n, &g_aus[n]);
            n++;
            cuts++;
        }
        else
        {
            ltr_emit_main_p(&c, n, &g_aus[n]);
            n++;
            ltr_emit_aux(&c, n, &g_aus[n]);
            n++;
        }
    }
    ck_assert_int_eq(cuts, (n_pairs - 1) / cut_period);
    ltr_run_both_modes(g_aus, n, &st);
    /* every P still resolves to its own view's previous picture, in
     * both modes (ltr_run_both_modes asserts it); the cuts cost 2 P
     * slices each */
    ck_assert_int_eq(st.p_slices_checked, 2 * (n_pairs - 1 - cuts));
    /* exactly one IDR in the whole stream, at the start */
    for (i = 1; i < n; i++)
    {
        ck_assert_int_eq(g_aus[i].is_idr, 0);
    }
    /* no long-term slot was ever evicted or unresolved, and the
     * transitive prediction depth of every picture is bounded by the
     * schedule (the property a refresh exists to provide) */
    ck_assert_int_eq(st.one_ctx.window_evictions, 0);
    ck_assert_int_eq(st.one_ctx.missing_ref, 0);
    depth[0] = -1;
    depth[1] = -1;
    worst[0] = 0;
    worst[1] = 0;
    for (i = 0; i < n; i++)
    {
        int v = g_aus[i].view;

        if (!g_aus[i].slice_p)
        {
            depth[v] = 0;
        }
        else
        {
            ck_assert_int_ge(depth[v], 0);
            depth[v]++;
            if (depth[v] > worst[v])
            {
                worst[v] = depth[v];
            }
        }
    }
    ck_assert_int_le(worst[0], cut_period);
    ck_assert_int_le(worst[1], cut_period);
    /* the schedule is what bounds it: without a cut the depth would
     * reach n_pairs - 1 */
    ck_assert_int_eq(worst[0], cut_period - 1);
    ck_assert_int_eq(worst[1], cut_period - 1);
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
    tcase_add_test(tc, test_ltr_emitter_golden_sequence);
    tcase_add_test(tc, test_ltr_win2022_field_sequence_cross_check);
    tcase_add_test(tc, test_ltr_rejects_existing_list_modification);
    tcase_add_test(tc, test_ltr_rejects_unseeded_aux_p);
    tcase_add_test(tc, test_ltr_rejects_main_start_without_idr);
    tcase_add_test(tc, test_ltr_rejects_truncated_and_small_cap);
    tcase_add_test(tc, test_ltr_emitter_high_counter_and_cadence);
    tcase_add_test(tc, test_ltr_emitter_epoch_restart_byte_exact);
    tcase_add_test(tc, test_ltr_seed_i_pins_leaf_diff);
    tcase_add_test(tc, test_ltr_main_golden_recipe_fields);
    tcase_add_test(tc, test_ltr_win2022_sps_fields);
    tcase_add_test(tc, test_ltr_rejects_b_slice_and_unknown_level);
    /* BACKLOG #45 step 0: scheduled paired-cut ratchets */
    tcase_add_test(tc, test_ltr_cut_sequence_byte_exact);
    tcase_add_test(tc, test_ltr_cut_nonidr_i_accepted_both_views);
    tcase_add_test(tc, test_ltr_cut_midstream_idr_keeps_chain);
    tcase_add_test(tc, test_ltr_schedule_observed_vs_requested);
    tcase_add_test(tc, test_ltr_dpb_scheduled_paired_cut_both_modes);
    suite_add_tcase(s, tc);
    return s;
}
