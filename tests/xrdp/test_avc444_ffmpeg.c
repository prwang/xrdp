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
    conv = xrdp_avc444_conv_create(w, h);
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

/******************************************************************************/
Suite *
make_suite_avc444_ffmpeg(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444Ffmpeg");
    tc = tcase_create("avc444_ffmpeg");
    tcase_set_timeout(tc, 30);
    tcase_add_test(tc, test_ffmpeg_probe);
    tcase_add_test(tc, test_ffmpeg_encode_pair);
    suite_add_tcase(s, tc);
    return s;
}
