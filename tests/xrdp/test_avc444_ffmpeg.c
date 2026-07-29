#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "xrdp_encoder_ffmpeg.h"
#include "xrdp_h264_annexb.h"
#include "xrdp_avc444_convert.h"
#include "os_calls.h"
#include "log.h"
#include "test_xrdp.h"

/*
 * Executable-behavior test (PRD 15.2). Gated on the XRDP_TEST_FFMPEG_PATH
 * environment variable so the default "make check" (and CI without a usable
 * ffmpeg) skips it. Point the variable at an absolute stock ffmpeg with
 * libx264 to exercise the full converter -> ffmpeg -> NUT -> H.264 path.
 */

/* Fill a planar YUV444 buffer (Y, U, V planes, stride w, w/h 16-aligned) with
 * deterministic per-iteration-varying content - the format xorgxrdp now
 * delivers. Only the frame-to-frame variation matters to these pipeline tests. */
static void
fill_yuv444_seed(unsigned char *yuv, int w, int h, int i)
{
    int area = w * h;
    int j;

    for (j = 0; j < area; j++)
    {
        yuv[j]            = (unsigned char)((j * 7 + i * 20) & 0xff);
        yuv[area + j]     = (unsigned char)((j * 13 + i) & 0xff);
        yuv[2 * area + j] = (unsigned char)((j * 5) & 0xff);
    }
}

static int
have_ffmpeg(struct xrdp_ffmpeg_avc444_config *cfg)
{
    const char *path = g_getenv("XRDP_TEST_FFMPEG_PATH");

    if (path == NULL || path[0] != '/' || !g_file_exist(path))
    {
        /* Check has no skip verdict: a gated test that returns early is
         * reported as "Passed". Say so in the log, every time, so a run
         * where this whole file executed NOTHING cannot be read as
         * evidence that the ffmpeg path works. */
        LOG(LOG_LEVEL_WARNING, "test_avc444_ffmpeg: SKIPPED (reported as "
            "PASS, proves nothing) -- set XRDP_TEST_FFMPEG_PATH to an "
            "absolute stock ffmpeg to actually run this test");
        return 0;
    }
    xrdp_ffmpeg_avc444_config_default(cfg);
    /* pin 16-alignment so the runner's coded width matches the converters this
     * suite creates with align 16 and its round_up_16 expectations (the default
     * is 32; the two must always agree, which the real encoder guarantees) */
    cfg->chroma_align = 16;
    snprintf(cfg->path, sizeof(cfg->path), "%s", path);
    return 1;
}

START_TEST(test_ffmpeg_probe)
{
    struct xrdp_ffmpeg_avc444_config cfg;

    if (!have_ffmpeg(&cfg))
    {
        return; /* skipped: no ffmpeg configured */
    }
    ck_assert_int_eq(xrdp_ffmpeg_avc444_probe(&cfg, 64, 64), 0);
}
END_TEST

/* set up libx264 WITHOUT repeat-headers=1: parameter sets land in
 * extradata only, modelling h264_nvenc (found live on a Tesla T4,
 * 2026-07-22) */
static void
set_global_header_only_args(struct xrdp_ffmpeg_avc444_config *cfg)
{
    static const char *args[] =
    {
        "-c:v", "libx264",
        "-bf", "0",
        "-preset", "ultrafast",
        "-tune", "zerolatency",
        "-crf", "18",
        "-g", "240"
    };
    int nargs = (int)(sizeof(args) / sizeof(args[0]));
    int i;

    memset(&cfg->encoder_args, 0, sizeof(cfg->encoder_args));
    for (i = 0; i < nargs; i++)
    {
        snprintf(cfg->encoder_args.arg[i], sizeof(cfg->encoder_args.arg[i]),
                 "%s", args[i]);
    }
    cfg->encoder_args.count = nargs;
}

/* count SPS NALs (type 7) in an Annex-B buffer */
static int
count_sps(const unsigned char *d, int len)
{
    int i;
    int n = 0;

    for (i = 0; i + 3 < len; i++)
    {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1)
        {
            if ((d[i + 3] & 0x1f) == 7)
            {
                n++;
            }
            i += 3;
        }
    }
    return n;
}

/* REGRESSION trio (all proven live; PRD FR-PROBE-6):
 * - Tesla T4 2026-07-22: extradata-only encoders (h264_nvenc) violate the
 *   in-band contract unless dump_extra reinserts the parameter sets. With
 *   the STATIC gfx.toml policy this misconfiguration must be classified
 *   as CONTENT_REJECT (deterministic evidence), never a generic failure.
 * - macOS Windows App 2026-07-23: duplicated parameter sets (dump_extra
 *   chained onto an in-band encoder) render BLACK on strict decoders.
 *   The declared-policy verification must refuse that config outright.
 * - Tesla T4 2026-07-26: a cold-GPU probe TIMEOUT was conflated with the
 *   content reject by the old adaptive ladder and could flip the policy
 *   bit; timeout is environmental and must classify as TIMEOUT. */
START_TEST(test_ffmpeg_probe_global_header_encoder)
{
    struct xrdp_ffmpeg_avc444_config cfg;

    if (!have_ffmpeg(&cfg))
    {
        return; /* skipped: no ffmpeg configured */
    }
    set_global_header_only_args(&cfg);
    /* dump_extra=false on an extradata-only encoder: refused, and refused
     * for the RIGHT reason (content, not environment) */
    cfg.use_dump_extra = 0;
    ck_assert_int_eq(xrdp_ffmpeg_avc444_probe(&cfg, 64, 64),
                     XRDP_FFMPEG_PROBE_CONTENT_REJECT);
    /* dump_extra=true matches this encoder: verified OK */
    cfg.use_dump_extra = 1;
    ck_assert_int_eq(xrdp_ffmpeg_avc444_probe(&cfg, 64, 64),
                     XRDP_FFMPEG_PROBE_OK);
}
END_TEST

/* the Mac-black misconfiguration: dump_extra=true on an encoder that
 * already repeats headers in-band duplicates the SPS on the wire. The
 * verification must refuse it (exactly-one-SPS bound) instead of letting
 * it reach a strict decoder. */
START_TEST(test_ffmpeg_probe_duplicate_headers_rejected)
{
    struct xrdp_ffmpeg_avc444_config cfg;

    if (!have_ffmpeg(&cfg))
    {
        return; /* skipped: no ffmpeg configured */
    }
    /* default args: libx264 with repeat-headers=1 (in-band) */
    cfg.use_dump_extra = 1;
    ck_assert_int_eq(xrdp_ffmpeg_avc444_probe(&cfg, 64, 64),
                     XRDP_FFMPEG_PROBE_CONTENT_REJECT);
}
END_TEST

/* a hanging "encoder" (cold hardware init, T4 2026-07-26) must classify
 * as TIMEOUT -- environmental, carrying no evidence about header policy.
 * Needs only /bin/sh, so it is not gated on XRDP_TEST_FFMPEG_PATH. */
START_TEST(test_ffmpeg_probe_timeout_classified)
{
    struct xrdp_ffmpeg_avc444_config cfg;
    char *abs_path;

    xrdp_ffmpeg_avc444_config_default(&cfg);
    /* XRDP_TOP_SRCDIR is relative for in-tree builds; the probe requires
     * an absolute path */
    abs_path = realpath(XRDP_TOP_SRCDIR
                        "/tests/xrdp/gfx/fake_encoder_hang.sh", NULL);
    ck_assert_ptr_ne(abs_path, NULL);
    /* cfg.path is zero-filled by config_default, so this stays terminated */
    strncpy(cfg.path, abs_path, sizeof(cfg.path) - 1);
    free(abs_path);
    cfg.stream_ready_timeout_ms = 100; /* deadline = 2x = 200ms */
    ck_assert_int_eq(xrdp_ffmpeg_avc444_probe(&cfg, 64, 64),
                     XRDP_FFMPEG_PROBE_TIMEOUT);
}
END_TEST

/* exactly ONE SPS per keyframe on the wire, in BOTH static configs */
START_TEST(test_ffmpeg_single_sps_per_keyframe)
{
    struct xrdp_ffmpeg_avc444_config cfg;
    struct xrdp_ffmpeg_avc444 *enc;
    struct xrdp_avc444_conv *conv;
    struct xrdp_avc444_encoded_pair pair;
    unsigned char *xrgb;
    int w = 128;
    int h = 96;
    int branch;
    int rc;

    if (!have_ffmpeg(&cfg))
    {
        return;
    }
    /* planar YUV444 source (Y, U, V), the format xorgxrdp now delivers; w,h
     * are 16-aligned so the plane stride equals w */
    xrgb = (unsigned char *)malloc(3 * w * h);
    ck_assert_ptr_ne(xrgb, NULL);
    memset(xrgb, 0x80, 3 * w * h);
    for (branch = 0; branch < 2; branch++)
    {
        if (!have_ffmpeg(&cfg))
        {
            break;
        }
        if (branch == 0)
        {
            /* in-band encoder (default args, repeat-headers), pristine */
            cfg.use_dump_extra = 0;
        }
        else
        {
            /* extradata-only encoder, dump_extra reinsertion */
            set_global_header_only_args(&cfg);
            cfg.use_dump_extra = 1;
        }
        conv = xrdp_avc444_conv_create(w, h, 16);
        ck_assert_ptr_ne(conv, NULL);
        enc = xrdp_ffmpeg_avc444_create(&cfg, w, h);
        ck_assert_ptr_ne(enc, NULL);
        ck_assert_int_eq(xrdp_avc444_conv_update(conv, xrgb, w, w, h), 0);
        rc = xrdp_ffmpeg_avc444_encode_pair(enc, conv->main_nv12,
                                            conv->aux_nv12, conv->nv12_size,
                                            0ULL, &pair);
        ck_assert_int_eq(rc, XRDP_FFMPEG_PAIR_READY);
        ck_assert_int_eq(count_sps(pair.main_data, pair.main_len), 1);
        xrdp_ffmpeg_avc444_delete(enc);
        xrdp_avc444_conv_delete(conv);
    }
    free(xrgb);
}
END_TEST

START_TEST(test_ffmpeg_encode_pair)
{
    struct xrdp_ffmpeg_avc444_config cfg;
    struct xrdp_ffmpeg_avc444 *enc;
    struct xrdp_avc444_conv *conv;
    struct xrdp_avc444_encoded_pair pair;
    unsigned char *xrgb;
    int w = 128;
    int h = 96;
    int stride = w * 4;
    int i;
    int nsub = 4;
    unsigned long long got_seq[8];
    int got_key[8];
    int ngot = 0;
    int rc;

    if (!have_ffmpeg(&cfg))
    {
        return;
    }
    xrgb = (unsigned char *)malloc(stride * h);
    ck_assert_ptr_ne(xrgb, NULL);
    conv = xrdp_avc444_conv_create(w, h, 16);
    ck_assert_ptr_ne(conv, NULL);
    enc = xrdp_ffmpeg_avc444_create(&cfg, w, h);
    ck_assert_ptr_ne(enc, NULL);
    ck_assert_int_eq(xrdp_ffmpeg_avc444_coded_width(enc), conv->coded_width);
    ck_assert_int_eq(xrdp_ffmpeg_avc444_coded_height(enc), conv->coded_height);

    /* REGRESSION GUARDS (both proven in the field, see PRD 25):
     * - content/region desync: the runner must return THE submitted pair
     *   from the same call (READY, matching desktop_sequence), never an
     *   older one -- a pipelined runner shipping pair N-1 under frame N's
     *   damage region froze region-strict clients (mstsc) on stale frames.
     * - low-resolution startup deadlock: at this coded size one pair is far
     *   below ffmpeg's default 5 MB probesize, so any input-side analysis
     *   hold (framerate > ~100 fps without the one-frame -probesize cap)
     *   times these calls out and fails the suite. */
    for (i = 0; i < nsub; i++)
    {
        fill_yuv444_seed(xrgb, w, h, i);
        ck_assert_int_eq(xrdp_avc444_conv_update(conv, xrgb, w, w, h), 0);
        rc = xrdp_ffmpeg_avc444_encode_pair(enc, conv->main_nv12,
                                            conv->aux_nv12, conv->nv12_size,
                                            (unsigned long long)i, &pair);
        ck_assert_int_eq(rc, XRDP_FFMPEG_PAIR_READY);
        ck_assert_int_gt(pair.main_len, 0);
        ck_assert_int_gt(pair.aux_len, 0);
        ck_assert_int_eq((int)pair.desktop_sequence, i);
        got_seq[ngot] = pair.desktop_sequence;
        got_key[ngot] = pair.main_keyframe;
        ngot++;
    }
    /* synchronous runner: nothing left in flight to flush */
    rc = xrdp_ffmpeg_avc444_flush_next(enc, &pair);
    ck_assert_int_eq(rc, XRDP_FFMPEG_PAIR_DONE);
    /* every submitted pair came back exactly once, in submit order, and the
     * first pair of the generation is the reset keyframe */
    ck_assert_int_eq(ngot, nsub);
    for (i = 0; i < nsub; i++)
    {
        ck_assert_int_eq((int)got_seq[i], i);
    }
    ck_assert_int_eq(got_key[0], 1);

    xrdp_ffmpeg_avc444_delete(enc);
    xrdp_avc444_conv_delete(conv);
    free(xrgb);
}
END_TEST

/* Single-view AVC420 path: a main_only converter feeds encode_single. The
 * runner is synchronous: every submitted picture must come back READY from
 * the same call (never an older picture -- that was the content/region
 * desync bug), in submit order, with the first being the reset keyframe. */
/* Mostly-static planar YUV444: a flat field with one small moving block.
 * fill_yuv444_seed() rewrites every pixel every frame, which makes x264
 * code each picture intra -- fine for the pipeline tests, but useless for
 * the LTR chain, whose whole subject is the P-slice reference topology
 * (and whose rewriter does not yet accept a mid-stream non-IDR I -- the
 * gap FR-H264-6/#45 closes). */
static void
fill_yuv444_mostly_static(unsigned char *yuv, int w, int h, int i)
{
    int area = w * h;
    int bx = (i * 4) % (w - 16);
    int y;
    int x;

    memset(yuv, 0x40, area);
    memset(yuv + area, 0x80, area);
    memset(yuv + 2 * area, 0x80, area);
    for (y = 8; y < 24; y++)
    {
        for (x = bx; x < bx + 16; x++)
        {
            yuv[y * w + x] = 0xd0;
        }
    }
}

/* BACKLOG #48: the aux_ltr_chain re-key threshold is settable, and the
 * re-key must fire AT it -- not early, not late, and never by dropping
 * the frame that trips it. The shared frame_num advances by two per pair
 * (one value per view), so a threshold of N is crossed on pair N/2.
 *
 * This is the ratchet behind the lowered-threshold fleet arm: if the
 * counter ever stopped driving the re-key, the wrap guard would go quiet
 * and the failure would only appear ~18 min into a live session. */
START_TEST(test_ffmpeg_ltr_rekey_cycle)
{
    struct xrdp_ffmpeg_avc444_config cfg;
    struct xrdp_ffmpeg_avc444 *enc;
    struct xrdp_avc444_conv *conv;
    struct xrdp_avc444_encoded_pair pair;
    unsigned char *xrgb;
    int w = 128;
    int h = 96;
    int stride = w * 4;
    int i;
    int rc;
    int fired_at = -1;
    /* an encoder whose stream shape the LTR rewriter accepts: CABAC on,
     * one reference, no weighted P (verified against ltr_cache_ok with
     * tools/avc444_ltr_wire_audit.py --annexb) */
    static const char *const ltr_args[] =
    {
        "-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency",
        "-refs", "1", "-bf", "0", "-g", "30000",
        "-x264-params", "repeat-headers=1:aud=1:cabac=1:weightp=0"
    };
    const int nargs = (int)(sizeof(ltr_args) / sizeof(ltr_args[0]));
    const int threshold = XRDP_H264_LTR_FRAME_NUM_REKEY_MIN;

    if (!have_ffmpeg(&cfg))
    {
        return;
    }
    cfg.aux_ltr_chain = 1;
    cfg.ltr_rekey_frame_num = threshold;
    cfg.encoder_args.count = nargs;
    for (i = 0; i < nargs; i++)
    {
        snprintf(cfg.encoder_args.arg[i], sizeof(cfg.encoder_args.arg[i]),
                 "%s", ltr_args[i]);
    }
    xrgb = (unsigned char *)malloc(stride * h);
    ck_assert_ptr_ne(xrgb, NULL);
    conv = xrdp_avc444_conv_create(w, h, 16);
    ck_assert_ptr_ne(conv, NULL);
    enc = xrdp_ffmpeg_avc444_create(&cfg, w, h);
    ck_assert_ptr_ne(enc, NULL);
    /* nothing pending before the first pair */
    ck_assert_int_eq(xrdp_ffmpeg_avc444_rekey_pending(enc), 0);
    for (i = 0; i < threshold; i++)
    {
        fill_yuv444_mostly_static(xrgb, w, h, i);
        ck_assert_int_eq(xrdp_avc444_conv_update(conv, xrgb, w, w, h), 0);
        rc = xrdp_ffmpeg_avc444_encode_pair(enc, conv->main_nv12,
                                            conv->aux_nv12, conv->nv12_size,
                                            (unsigned long long)i, &pair);
        /* the LTR chain must engage: a guard rejection would surface here
         * as ERROR, and this test would be silently proving nothing */
        ck_assert_int_eq(rc, XRDP_FFMPEG_PAIR_READY);
        /* the pair that trips the re-key STILL SHIPS -- dropping it would
         * lose that frame's damage (the stuck-last-frame class) */
        ck_assert_int_gt(pair.main_len, 0);
        ck_assert_int_gt(pair.aux_len, 0);
        if (xrdp_ffmpeg_avc444_rekey_pending(enc) && fired_at < 0)
        {
            fired_at = i;
            break;
        }
    }
    /* two frame_num values per pair: the trip is on pair threshold/2 - 1
     * (zero-based), i.e. the first pair whose SECOND value reaches it */
    ck_assert_int_eq(fired_at, threshold / 2 - 1);

    /* Cross the boundary the way the caller does (xrdp_encoder.c: the
     * pair has shipped, now destroy the encoder so the next damaged frame
     * rebuilds it). This half is what makes #48 testable INDEPENDENTLY of
     * the #45 non-IDR-I gap: a re-key never asks the rewriter to splice a
     * mid-stream non-IDR I -- the child is destroyed, so its replacement's
     * first picture is a real IDR, the one intra shape the rewriter has
     * always accepted (xrdp_h264_annexb.c: ntype == 5 resets the chain). */
    xrdp_ffmpeg_avc444_delete(enc);
    enc = xrdp_ffmpeg_avc444_create(&cfg, w, h);
    ck_assert_ptr_ne(enc, NULL);
    /* the fresh pair starts a new epoch: nothing pending, and the first
     * picture is the reset keyframe */
    ck_assert_int_eq(xrdp_ffmpeg_avc444_rekey_pending(enc), 0);
    for (i = 0; i < 4; i++)
    {
        fill_yuv444_mostly_static(xrgb, w, h, fired_at + 1 + i);
        ck_assert_int_eq(xrdp_avc444_conv_update(conv, xrgb, w, w, h), 0);
        rc = xrdp_ffmpeg_avc444_encode_pair(enc, conv->main_nv12,
                                            conv->aux_nv12, conv->nv12_size,
                                            (unsigned long long)i, &pair);
        /* the post-re-key chain must splice cleanly: main IDR then P, aux
         * seed-I then P. A failure here would mean the epoch restart -- not
         * merely its trigger -- is broken. */
        ck_assert_int_eq(rc, XRDP_FFMPEG_PAIR_READY);
        ck_assert_int_gt(pair.main_len, 0);
        ck_assert_int_gt(pair.aux_len, 0);
        if (i == 0)
        {
            ck_assert_int_eq(pair.main_keyframe, 1);
        }
        /* and the new epoch must be far from the threshold again */
        ck_assert_int_eq(xrdp_ffmpeg_avc444_rekey_pending(enc), 0);
    }
    xrdp_ffmpeg_avc444_delete(enc);
    xrdp_avc444_conv_delete(conv);
    free(xrgb);
}
END_TEST

START_TEST(test_ffmpeg_encode_single)
{
    struct xrdp_ffmpeg_avc444_config cfg;
    struct xrdp_ffmpeg_avc444 *enc;
    struct xrdp_avc444_conv *conv;
    struct xrdp_avc444_encoded_pair pic;
    unsigned char *xrgb;
    int w = 128;
    int h = 96;
    int stride = w * 4;
    int i;
    int nsub = 4;
    unsigned long long got_seq[8];
    int got_key[8];
    int ngot = 0;
    int rc;

    if (!have_ffmpeg(&cfg))
    {
        return;
    }
    xrgb = (unsigned char *)malloc(stride * h);
    ck_assert_ptr_ne(xrgb, NULL);
    conv = xrdp_avc444_conv_create(w, h, 16);
    ck_assert_ptr_ne(conv, NULL);
    conv->main_only = 1;
    enc = xrdp_ffmpeg_avc444_create(&cfg, w, h);
    ck_assert_ptr_ne(enc, NULL);

    for (i = 0; i < nsub; i++)
    {
        fill_yuv444_seed(xrgb, w, h, i);
        ck_assert_int_eq(xrdp_avc444_conv_update(conv, xrgb, w, w, h), 0);
        rc = xrdp_ffmpeg_avc444_encode_single(enc, conv->main_nv12,
                                              conv->nv12_size,
                                              (unsigned long long)i, &pic);
        ck_assert_int_eq(rc, XRDP_FFMPEG_PAIR_READY);
        ck_assert_int_gt(pic.main_len, 0);
        ck_assert_int_eq(pic.aux_len, 0);
        ck_assert_ptr_eq((void *)pic.aux_data, NULL);
        ck_assert_int_eq((int)pic.desktop_sequence, i);
        got_seq[ngot] = pic.desktop_sequence;
        got_key[ngot] = pic.main_keyframe;
        ngot++;
    }

    /* synchronous runner: every submitted picture returned from its call */
    ck_assert_int_eq(ngot, nsub);
    for (i = 0; i < ngot; i++)
    {
        ck_assert_int_eq((int)got_seq[i], i);
    }
    ck_assert_int_eq(got_key[0], 1);

    xrdp_ffmpeg_avc444_delete(enc);
    xrdp_avc444_conv_delete(conv);
    free(xrgb);
}
END_TEST

/*
 * Drive one encode generation at a given visible size through the real child:
 * create converter + child, submit nsub distinguishable pairs, and check
 * every call returns its own pair synchronously, in order, with the first
 * being the reset keyframe (regression guards as in test_ffmpeg_encode_pair,
 * here across resolutions). Asserts the child adopts the 16-aligned coded
 * dimensions (odd visible sizes round up). Returns with everything reaped.
 */
static void
run_one_generation(struct xrdp_ffmpeg_avc444_config *cfg, int w, int h)
{
    struct xrdp_ffmpeg_avc444 *enc;
    struct xrdp_avc444_conv *conv;
    struct xrdp_avc444_encoded_pair pair;
    unsigned char *xrgb;
    int stride = w * 4;
    int nsub = 4;
    int expect_cw = (w + 15) & ~15;
    int expect_ch = (h + 15) & ~15;
    unsigned long long got_seq[8];
    int ngot = 0;
    int i;
    int rc;

    xrgb = (unsigned char *)malloc((size_t)stride * h);
    ck_assert_ptr_ne(xrgb, NULL);
    conv = xrdp_avc444_conv_create(w, h, 16);
    ck_assert_ptr_ne(conv, NULL);
    ck_assert_int_eq(conv->coded_width, expect_cw);
    ck_assert_int_eq(conv->coded_height, expect_ch);

    enc = xrdp_ffmpeg_avc444_create(cfg, w, h);
    ck_assert_ptr_ne(enc, NULL);
    /* the child was spawned with -s <coded_w>x<coded_h> matching the NV12 the
     * converter produces; odd visible sizes must be padded to a multiple of 16
     * (libx264 also requires even dimensions, which /16 guarantees) */
    ck_assert_int_eq(xrdp_ffmpeg_avc444_coded_width(enc), expect_cw);
    ck_assert_int_eq(xrdp_ffmpeg_avc444_coded_height(enc), expect_ch);

    for (i = 0; i < nsub; i++)
    {
        fill_yuv444_seed(xrgb, w, h, i);
        ck_assert_int_eq(xrdp_avc444_conv_update(conv, xrgb, w, w, h), 0);
        rc = xrdp_ffmpeg_avc444_encode_pair(enc, conv->main_nv12,
                                            conv->aux_nv12, conv->nv12_size,
                                            (unsigned long long)i, &pair);
        ck_assert_int_eq(rc, XRDP_FFMPEG_PAIR_READY);
        ck_assert_int_gt(pair.main_len, 0);
        ck_assert_int_gt(pair.aux_len, 0);
        ck_assert_int_eq((int)pair.desktop_sequence, i);
        got_seq[ngot++] = pair.desktop_sequence;
    }
    /* synchronous runner: nothing left in flight to flush */
    rc = xrdp_ffmpeg_avc444_flush_next(enc, &pair);
    ck_assert_int_eq(rc, XRDP_FFMPEG_PAIR_DONE);
    ck_assert_int_eq(ngot, nsub);
    for (i = 0; i < nsub; i++)
    {
        ck_assert_int_eq((int)got_seq[i], i);
    }

    xrdp_ffmpeg_avc444_delete(enc);
    xrdp_avc444_conv_delete(conv);
    free(xrgb);
}

/*
 * Resize lifecycle (PRD FR-RESIZE): a client resize drops the converter and
 * child and starts a fresh generation at the new coded dimensions. Walk a
 * sequence of sizes including odd widths/heights, recreating the child each
 * time exactly as gfx_wiretosurface1_avc444() does on a size change. Each
 * generation must spawn cleanly, adopt the 16-aligned coded dims, encode, and
 * be fully reaped before the next spawns — so N resizes leak no processes or
 * fds and every generation still round-trips its pairs.
 */
START_TEST(test_ffmpeg_resize_recycle)
{
    struct xrdp_ffmpeg_avc444_config cfg;
    /* even -> odd (shrink) -> odd (grow) -> odd-odd -> back to even */
    static const int sizes[][2] =
    {
        {1280, 720},
        {1281, 721},
        {1366, 769},
        { 641, 481},
        {1280, 720}
    };
    int n = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int i;

    if (!have_ffmpeg(&cfg))
    {
        return;
    }
    for (i = 0; i < n; i++)
    {
        run_one_generation(&cfg, sizes[i][0], sizes[i][1]);
    }
}
END_TEST

/******************************************************************************/
Suite *
make_suite_avc444_ffmpeg(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444Ffmpeg");
    tc = tcase_create("avc444_ffmpeg");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ffmpeg_probe);
    tcase_add_test(tc, test_ffmpeg_probe_global_header_encoder);
    tcase_add_test(tc, test_ffmpeg_probe_duplicate_headers_rejected);
    tcase_add_test(tc, test_ffmpeg_probe_timeout_classified);
    tcase_add_test(tc, test_ffmpeg_single_sps_per_keyframe);
    tcase_add_test(tc, test_ffmpeg_encode_pair);
    tcase_add_test(tc, test_ffmpeg_ltr_rekey_cycle);
    tcase_add_test(tc, test_ffmpeg_encode_single);
    tcase_add_test(tc, test_ffmpeg_resize_recycle);
    suite_add_tcase(s, tc);
    return s;
}
