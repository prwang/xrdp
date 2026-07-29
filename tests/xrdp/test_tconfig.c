#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "xrdp_tconfig.h"
#include "xrdp_h264_annexb.h"
#include "test_xrdp.h"
#include "xrdp.h"
#include "string_calls.h"

#define GFXCONF_STUBDIR XRDP_TOP_SRCDIR "/tests/xrdp/gfx/"

START_TEST(test_tconfig_gfx_always_success)
{
    ck_assert_int_eq(1, 1);
}
END_TEST

START_TEST(test_tconfig_gfx_h264_oh264)
{
    struct xrdp_tconfig_gfx gfxconfig;
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_h264_encoder_openh264.toml", &gfxconfig);

    /* H.264 encoder is OpenH264 */
    ck_assert_int_eq(gfxconfig.h264_encoder, XTC_H264_OPENH264);
}

START_TEST(test_tconfig_gfx_h264_x264)
{
    struct xrdp_tconfig_gfx gfxconfig;
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_h264_encoder_x264.toml", &gfxconfig);

    /* H.264 encoder is x264 */
    ck_assert_int_eq(gfxconfig.h264_encoder, XTC_H264_X264);
}

START_TEST(test_tconfig_gfx_h264_undefined)
{
    struct xrdp_tconfig_gfx gfxconfig;
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_h264_encoder_undefined.toml", &gfxconfig);

    /* H.264 encoder is x264 if undefined */
    ck_assert_int_eq(gfxconfig.h264_encoder, XTC_H264_X264);
}

START_TEST(test_tconfig_gfx_h264_invalid)
{
    struct xrdp_tconfig_gfx gfxconfig;
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_h264_encoder_invalid.toml", &gfxconfig);

    /* H.264 encoder is x264 if invalid, unknown encoder specified */
    ck_assert_int_eq(gfxconfig.h264_encoder, XTC_H264_X264);
}

START_TEST(test_tconfig_gfx_oh264_load_basic)
{
    struct xrdp_tconfig_gfx gfxconfig;
    int rv = tconfig_load_gfx(GFXCONF_STUBDIR "/gfx.toml", &gfxconfig);

    ck_assert_int_eq(rv, 0);

    /* default */
    ck_assert_int_eq(gfxconfig.openh264_param[0].EnableFrameSkip, 0);
    ck_assert_int_eq(gfxconfig.openh264_param[0].TargetBitrate, 20000000);
    ck_assert_int_eq(gfxconfig.openh264_param[0].MaxBitrate, 0);
    ck_assert_float_eq(gfxconfig.openh264_param[0].MaxFrameRate, 60.0);
}

START_TEST(test_tconfig_gfx_x264_load_basic)
{
    struct xrdp_tconfig_gfx gfxconfig;
    int rv = tconfig_load_gfx(GFXCONF_STUBDIR "/gfx.toml", &gfxconfig);

    ck_assert_int_eq(rv, 0);

    /* default */
    ck_assert_str_eq(gfxconfig.x264_param[0].preset, "ultrafast");
    ck_assert_str_eq(gfxconfig.x264_param[0].tune, "zerolatency");
    ck_assert_str_eq(gfxconfig.x264_param[0].profile, "main");
    ck_assert_int_eq(gfxconfig.x264_param[0].vbv_max_bitrate, 0);
    ck_assert_int_eq(gfxconfig.x264_param[0].vbv_buffer_size, 0);
    ck_assert_int_eq(gfxconfig.x264_param[0].fps_num, 60);
    ck_assert_int_eq(gfxconfig.x264_param[0].fps_den, 1);

}
END_TEST

START_TEST(test_tconfig_gfx_codec_order)
{
    struct xrdp_tconfig_gfx gfxconfig;

    /* H264 earlier */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_codec_h264_preferred.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.codec.codec_count, 2);
    ck_assert_int_eq(gfxconfig.codec.codecs[0], XTC_H264);
    ck_assert_int_eq(gfxconfig.codec.codecs[1], XTC_RFX);

    /* H264 only */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_codec_h264_only.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.codec.codec_count, 1);
    ck_assert_int_eq(gfxconfig.codec.codecs[0], XTC_H264);

    /* RFX earlier */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_codec_rfx_preferred.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.codec.codec_count, 2);
    ck_assert_int_eq(gfxconfig.codec.codecs[0], XTC_RFX);
    ck_assert_int_eq(gfxconfig.codec.codecs[1], XTC_H264);

    /* RFX appears twice like: RFX, H264, RFX */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_codec_rfx_preferred_odd.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.codec.codec_count, 2);
    ck_assert_int_eq(gfxconfig.codec.codecs[0], XTC_RFX);
    ck_assert_int_eq(gfxconfig.codec.codecs[1], XTC_H264);

    /* RFX only */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_codec_rfx_only.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.codec.codec_count, 1);
    ck_assert_int_eq(gfxconfig.codec.codecs[0], XTC_RFX);

    /* H264 is preferred if order undefined */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_codec_order_undefined.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.codec.codec_count, 2);
    ck_assert_int_eq(gfxconfig.codec.codecs[0], XTC_H264);
    ck_assert_int_eq(gfxconfig.codec.codecs[1], XTC_RFX);
}
END_TEST

START_TEST(test_tconfig_gfx_missing_file)
{
    struct xrdp_tconfig_gfx gfxconfig;

    /* Check RFX config is returned if the file doesn't exist */
    tconfig_load_gfx(GFXCONF_STUBDIR "/no_such_file.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.codec.codec_count, 1);
    ck_assert_int_eq(gfxconfig.codec.codecs[0], XTC_RFX);
}
END_TEST

START_TEST(test_tconfig_gfx_missing_h264)
{
    struct xrdp_tconfig_gfx gfxconfig;

    /* Check RFX config only is returned if H.264 parameters are missing */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_missing_h264.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.codec.codec_count, 1);
    ck_assert_int_eq(gfxconfig.codec.codecs[0], XTC_RFX);
}
END_TEST

/* index of the first encoder_args token equal to needle, or -1 */
static int
find_enc_arg(const struct xrdp_avc444_encoder_args *a, const char *needle)
{
    int i;
    for (i = 0; i < a->count; i++)
    {
        if (g_strcmp(a->arg[i], needle) == 0)
        {
            return i;
        }
    }
    return -1;
}

START_TEST(test_tconfig_gfx_avc444_defaults)
{
    struct xrdp_tconfig_gfx gfxconfig;
    const struct xrdp_avc444_encoder_args *a;

    /* the stub gfx.toml has no [avc444_ffmpeg] table, so the built-in default
     * encoder block applies (reproduces the historic hard-coded argv) */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx.toml", &gfxconfig);
    ck_assert_str_eq(gfxconfig.avc444_ffmpeg_path, "/usr/bin/ffmpeg");
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_avc_mode, XTC_AVC_AUTO);
    /* dump_extra absent -> false: in-band header encoders are the default;
     * the probe verifies (never adapts) this static policy */
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_dump_extra, 0);
    a = &gfxconfig.avc444_ffmpeg_encoder_args;
    ck_assert_int_gt(a->count, 0);
    ck_assert_int_ge(find_enc_arg(a, "libx264"), 0);
    ck_assert_int_ge(find_enc_arg(a, "zerolatency"), 0);
    /* -crf is immediately followed by its value */
    {
        int ci = find_enc_arg(a, "-crf");
        ck_assert_int_ge(ci, 0);
        ck_assert_int_lt(ci + 1, a->count);
        ck_assert_str_eq(a->arg[ci + 1], "18");
    }
    /* the x264-params token carries repeat-headers plus the AUD delimiter
     * (aud=1) that matches a real Windows AVC444 stream; see
     * xrdp_ffmpeg_avc444_default_encoder_args */
    ck_assert_int_ge(find_enc_arg(a, "repeat-headers=1:aud=1"), 0);
}
END_TEST

START_TEST(test_tconfig_gfx_avc444_override)
{
    struct xrdp_tconfig_gfx gfxconfig;
    const struct xrdp_avc444_encoder_args *a;

    /* an explicit encoder_args list replaces the default block verbatim,
     * here selecting a hardware encoder xrdp never enumerates */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_avc444_ffmpeg.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.h264_encoder, XTC_H264_FFMPEG);
    ck_assert_str_eq(gfxconfig.avc444_ffmpeg_path, "/opt/custom/ffmpeg");
    /* "444v1": AVC444 with codec id 0x000E pinned (v2/ChromaV2 suppressed) */
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_avc_mode, XTC_AVC_FORCE_444V1);
    /* the stub declares an extradata-only encoder (h264_nvenc) */
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_dump_extra, 1);
    a = &gfxconfig.avc444_ffmpeg_encoder_args;
    ck_assert_int_eq(a->count, 10);
    ck_assert_str_eq(a->arg[0], "-c:v");
    ck_assert_str_eq(a->arg[1], "h264_nvenc");
    ck_assert_int_ge(find_enc_arg(a, "h264_nvenc"), 0);
    /* built-in libx264 default must NOT leak through */
    ck_assert_int_eq(find_enc_arg(a, "libx264"), -1);
}
END_TEST

START_TEST(test_tconfig_gfx_avc444_empty_args_fallback)
{
    struct xrdp_tconfig_gfx gfxconfig;
    const struct xrdp_avc444_encoder_args *a;

    /* an explicitly empty encoder_args array must fall back to the built-in
     * default rather than leaving the command with no encoder */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_avc444_empty_args.toml", &gfxconfig);
    a = &gfxconfig.avc444_ffmpeg_encoder_args;
    ck_assert_int_gt(a->count, 0);
    ck_assert_int_ge(find_enc_arg(a, "libx264"), 0);
}
END_TEST

START_TEST(test_tconfig_gfx_avc444_rekey_threshold)
{
    struct xrdp_tconfig_gfx gfxconfig;

    /* BACKLOG #48: the aux_ltr_chain re-key threshold is settable so the
     * boundary can be exercised without ~18 min of continuous animation.
     * Absent -> the shipped default. */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_ltr_rekey_frame_num,
                     XRDP_H264_LTR_FRAME_NUM_REKEY);
    /* an in-range value is honoured verbatim */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_avc444_rekey.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_ltr_rekey_frame_num, 536);
    ck_assert_int_ge(gfxconfig.avc444_ffmpeg_ltr_rekey_frame_num,
                     XRDP_H264_LTR_FRAME_NUM_REKEY_MIN);
}
END_TEST

START_TEST(test_tconfig_gfx_avc444_rekey_surface_reset)
{
    struct xrdp_tconfig_gfx gfxconfig;

    /* BACKLOG #48 (2026-07-29): the surface teardown is separable from
     * the re-key. It defaults ON (the shipped mechanism), and can be
     * masked so the client sees only a full-surface repaint from a fresh
     * IDR -- macOS renders the surface swap itself as a black flash. */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_ltr_rekey_surface_reset, 0);
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_avc444_rekey.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_ltr_rekey_surface_reset, 0);
    /* explicitly masked, threshold it travels with still honoured */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_avc444_rekey_nochurn.toml",
                     &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_ltr_rekey_surface_reset, 0);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_ltr_rekey_frame_num, 536);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_aux_ltr_chain, 1);
    /* and the known-bad behaviour is still reachable on purpose */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_avc444_rekey_churn.toml",
                     &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_ltr_rekey_surface_reset, 1);
}
END_TEST

START_TEST(test_tconfig_gfx_avc444_rekey_out_of_range_refused)
{
    struct xrdp_tconfig_gfx gfxconfig;

    /* Above the max the wrap guard would be defeated: the loader must
     * keep the default rather than weaken it silently (BACKLOG #48). */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_avc444_rekey_bad.toml",
                     &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_ltr_rekey_frame_num,
                     XRDP_H264_LTR_FRAME_NUM_REKEY);
    ck_assert_int_le(gfxconfig.avc444_ffmpeg_ltr_rekey_frame_num,
                     XRDP_H264_LTR_FRAME_NUM_REKEY_MAX);
    /* the rest of the table still parsed */
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_aux_ltr_chain, 1);
}
END_TEST

START_TEST(test_tconfig_gfx_avc444_intra_refresh)
{
    struct xrdp_tconfig_gfx gfxconfig;

    /* BACKLOG #45 D6 / PRD FR-H264-6: the scheduled paired refresh
     * interval. Absent -> the shipped default; there is deliberately no
     * 0/off value, because an off switch would keep the deleted aux
     * respawn path alive as a shadow fallback. */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx.toml", &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_intra_refresh_frames,
                     XRDP_H264_INTRA_REFRESH_FRAMES);
    /* an in-range value is honoured verbatim */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_avc444_intra_refresh.toml",
                     &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_intra_refresh_frames, 48);
    ck_assert_int_ge(gfxconfig.avc444_ffmpeg_intra_refresh_frames,
                     XRDP_H264_INTRA_REFRESH_FRAMES_MIN);
    ck_assert_int_le(gfxconfig.avc444_ffmpeg_intra_refresh_frames,
                     XRDP_H264_INTRA_REFRESH_FRAMES_MAX);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_aux_ltr_chain, 1);
}
END_TEST

START_TEST(test_tconfig_gfx_avc444_intra_refresh_out_of_range_refused)
{
    struct xrdp_tconfig_gfx gfxconfig;

    /* Below the minimum every frame would become a cut and the bandwidth
     * gate would silently change meaning: the loader keeps the default
     * rather than honouring it (#45 D6, "loader refuses"). */
    tconfig_load_gfx(GFXCONF_STUBDIR "/gfx_avc444_intra_refresh_bad.toml",
                     &gfxconfig);
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_intra_refresh_frames,
                     XRDP_H264_INTRA_REFRESH_FRAMES);
    /* the rest of the table still parsed */
    ck_assert_int_eq(gfxconfig.avc444_ffmpeg_aux_ltr_chain, 1);
}
END_TEST

/******************************************************************************/
Suite *
make_suite_tconfig_load_gfx(void)
{
    Suite *s;
    TCase *tc_tconfig_load_gfx;

    s = suite_create("GfxLoad");

    tc_tconfig_load_gfx = tcase_create("xrdp_tconfig_load_gfx");
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_always_success);
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_x264_load_basic);
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_codec_order);
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_missing_file);
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_missing_h264);

    /* OpenH264 */
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_oh264_load_basic);

    /* H.264 encoder */
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_h264_oh264);
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_h264_x264);
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_h264_undefined);
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_h264_invalid);
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_avc444_defaults);
    tcase_add_test(tc_tconfig_load_gfx, test_tconfig_gfx_avc444_override);
    tcase_add_test(tc_tconfig_load_gfx,
                   test_tconfig_gfx_avc444_rekey_threshold);
    tcase_add_test(tc_tconfig_load_gfx,
                   test_tconfig_gfx_avc444_rekey_surface_reset);
    tcase_add_test(tc_tconfig_load_gfx,
                   test_tconfig_gfx_avc444_rekey_out_of_range_refused);
    tcase_add_test(tc_tconfig_load_gfx,
                   test_tconfig_gfx_avc444_intra_refresh);
    tcase_add_test(tc_tconfig_load_gfx,
                   test_tconfig_gfx_avc444_intra_refresh_out_of_range_refused);
    tcase_add_test(tc_tconfig_load_gfx,
                   test_tconfig_gfx_avc444_empty_args_fallback);

    suite_add_tcase(s, tc_tconfig_load_gfx);

    return s;
}
