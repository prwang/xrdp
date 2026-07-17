#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "xrdp_encoder_ffmpeg.h"
#include "xrdp_avc444_convert.h"
#include "os_calls.h"
#include "test_xrdp.h"

/*
 * Executable-behavior test (PRD 15.2). Gated on the XRDP_TEST_FFMPEG_PATH
 * environment variable so the default "make check" (and CI without a usable
 * ffmpeg) skips it. Point the variable at an absolute stock ffmpeg with
 * libx264 to exercise the full converter -> ffmpeg -> NUT -> H.264 path.
 */

static int
have_ffmpeg(struct xrdp_ffmpeg_avc444_config *cfg)
{
    const char *path = g_getenv("XRDP_TEST_FFMPEG_PATH");

    if (path == NULL || path[0] != '/' || !g_file_exist(path))
    {
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

    /* submit several pairs; the pipeline returns each one an update later */
    for (i = 0; i < nsub; i++)
    {
        int j;
        for (j = 0; j < w * h; j++)
        {
            unsigned int r = (j * 7 + i * 20) & 0xff;
            unsigned int g = (j * 13 + i) & 0xff;
            unsigned int b = (j * 5) & 0xff;
            unsigned int px = (r << 16) | (g << 8) | b;
            memcpy(xrgb + j * 4, &px, 4);
        }
        ck_assert_int_eq(xrdp_avc444_conv_update(conv, xrgb, stride, w, h), 0);
        rc = xrdp_ffmpeg_avc444_encode_pair(enc, conv->main_nv12,
                                            conv->aux_nv12, conv->nv12_size,
                                            (unsigned long long)i, &pair);
        ck_assert_int_ne(rc, XRDP_FFMPEG_PAIR_ERROR);
        if (rc == XRDP_FFMPEG_PAIR_READY)
        {
            ck_assert_int_gt(pair.main_len, 0);
            ck_assert_int_gt(pair.aux_len, 0);
            got_seq[ngot] = pair.desktop_sequence;
            got_key[ngot] = pair.main_keyframe;
            ngot++;
        }
    }
    /* flush the remaining pipelined pairs */
    for (;;)
    {
        rc = xrdp_ffmpeg_avc444_flush_next(enc, &pair);
        ck_assert_int_ne(rc, XRDP_FFMPEG_PAIR_ERROR);
        if (rc == XRDP_FFMPEG_PAIR_DONE)
        {
            break;
        }
        got_seq[ngot] = pair.desktop_sequence;
        got_key[ngot] = pair.main_keyframe;
        ngot++;
    }
    /* every submitted pair comes back exactly once, in submit order, and the
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
        int j;
        for (j = 0; j < w * h; j++)
        {
            unsigned int r = (j * 7 + i * 20) & 0xff;
            unsigned int g = (j * 13 + i) & 0xff;
            unsigned int b = (j * 5) & 0xff;
            unsigned int px = (r << 16) | (g << 8) | b;
            memcpy(xrgb + j * 4, &px, 4);
        }
        ck_assert_int_eq(xrdp_avc444_conv_update(conv, xrgb, stride, w, h), 0);
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
 * create converter + child, submit nsub distinguishable pairs, flush, and
 * check every submitted pair returns exactly once in order with the first
 * being the reset keyframe. Asserts the child adopts the 16-aligned coded
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
        int j;
        for (j = 0; j < w * h; j++)
        {
            unsigned int r = (unsigned int)((j * 7 + i * 20) & 0xff);
            unsigned int g = (unsigned int)((j * 13 + i) & 0xff);
            unsigned int b = (unsigned int)((j * 5) & 0xff);
            unsigned int px = (r << 16) | (g << 8) | b;
            memcpy(xrgb + j * 4, &px, 4);
        }
        ck_assert_int_eq(xrdp_avc444_conv_update(conv, xrgb, stride, w, h), 0);
        rc = xrdp_ffmpeg_avc444_encode_pair(enc, conv->main_nv12,
                                            conv->aux_nv12, conv->nv12_size,
                                            (unsigned long long)i, &pair);
        ck_assert_int_ne(rc, XRDP_FFMPEG_PAIR_ERROR);
        if (rc == XRDP_FFMPEG_PAIR_READY)
        {
            ck_assert_int_gt(pair.main_len, 0);
            ck_assert_int_gt(pair.aux_len, 0);
            got_seq[ngot++] = pair.desktop_sequence;
        }
    }
    for (;;)
    {
        rc = xrdp_ffmpeg_avc444_flush_next(enc, &pair);
        ck_assert_int_ne(rc, XRDP_FFMPEG_PAIR_ERROR);
        if (rc == XRDP_FFMPEG_PAIR_DONE)
        {
            break;
        }
        got_seq[ngot++] = pair.desktop_sequence;
    }
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
    tcase_add_test(tc, test_ffmpeg_encode_pair);
    tcase_add_test(tc, test_ffmpeg_encode_single);
    tcase_add_test(tc, test_ffmpeg_resize_recycle);
    suite_add_tcase(s, tc);
    return s;
}
