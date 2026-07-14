#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "xrdp_avc444_caps.h"
#include "xrdp_egfx.h"
#include "test_xrdp.h"

#define AVC420_EN XR_RDPGFX_CAPS_FLAG_AVC420_ENABLED
#define AVC_DIS   XR_RDPGFX_CAPS_FLAG_AVC_DISABLED
#define THIN      XR_RDPGFX_CAPS_FLAG_AVC_THINCLIENT

START_TEST(test_caps_v8_none)
{
    ck_assert_int_eq(xrdp_avc444_classify_caps(XR_RDPGFX_CAPVERSION_8, 0),
                     XRDP_GFX_AVC_NONE);
    ck_assert_int_eq(xrdp_avc444_classify_caps(XR_RDPGFX_CAPVERSION_8, AVC420_EN),
                     XRDP_GFX_AVC_NONE);
}
END_TEST

START_TEST(test_caps_v81)
{
    /* v8.1 with AVC420_ENABLED -> AVC420 only */
    ck_assert_int_eq(xrdp_avc444_classify_caps(XR_RDPGFX_CAPVERSION_81, AVC420_EN),
                     XRDP_GFX_AVC420);
    /* v8.1 without it -> none */
    ck_assert_int_eq(xrdp_avc444_classify_caps(XR_RDPGFX_CAPVERSION_81, 0),
                     XRDP_GFX_AVC_NONE);
}
END_TEST

START_TEST(test_caps_v10_avc444)
{
    ck_assert_int_eq(xrdp_avc444_classify_caps(XR_RDPGFX_CAPVERSION_10, 0),
                     XRDP_GFX_AVC444);
    /* AVC_DISABLED set -> none */
    ck_assert_int_eq(xrdp_avc444_classify_caps(XR_RDPGFX_CAPVERSION_10, AVC_DIS),
                     XRDP_GFX_AVC_NONE);
    /* THINCLIENT is only a preference, not a prerequisite */
    ck_assert_int_eq(xrdp_avc444_classify_caps(XR_RDPGFX_CAPVERSION_10, THIN),
                     XRDP_GFX_AVC444);
}
END_TEST

START_TEST(test_caps_v101_excluded)
{
    /* v10.1 reserved-only capset is never AVC444 v1 in this MVP */
    ck_assert_int_eq(xrdp_avc444_classify_caps(XR_RDPGFX_CAPVERSION_101, 0),
                     XRDP_GFX_AVC_NONE);
    ck_assert_int_eq(xrdp_avc444_classify_caps(XR_RDPGFX_CAPVERSION_101, AVC420_EN),
                     XRDP_GFX_AVC_NONE);
}
END_TEST

START_TEST(test_caps_v102_to_107)
{
    int versions[6];
    int i;

    versions[0] = XR_RDPGFX_CAPVERSION_102;
    versions[1] = XR_RDPGFX_CAPVERSION_103;
    versions[2] = XR_RDPGFX_CAPVERSION_104;
    versions[3] = XR_RDPGFX_CAPVERSION_105;
    versions[4] = XR_RDPGFX_CAPVERSION_106;
    versions[5] = XR_RDPGFX_CAPVERSION_107;
    for (i = 0; i < 6; i++)
    {
        ck_assert_int_eq(xrdp_avc444_classify_caps(versions[i], 0),
                         XRDP_GFX_AVC444);
        ck_assert_int_eq(xrdp_avc444_classify_caps(versions[i], AVC_DIS),
                         XRDP_GFX_AVC_NONE);
        ck_assert_int_eq(xrdp_avc444_classify_caps(versions[i], THIN),
                         XRDP_GFX_AVC444);
    }
}
END_TEST

START_TEST(test_caps_unknown_none)
{
    ck_assert_int_eq(xrdp_avc444_classify_caps(0, 0), XRDP_GFX_AVC_NONE);
    ck_assert_int_eq(xrdp_avc444_classify_caps(0x12345678, 0), XRDP_GFX_AVC_NONE);
}
END_TEST

START_TEST(test_caps_v2_support)
{
    int i;
    int versions[6];

    /* v10.1 signals v2 regardless of flags (reserved-only capset) */
    ck_assert_int_eq(xrdp_avc444_caps_supports_v2(XR_RDPGFX_CAPVERSION_101, 0),
                     1);
    ck_assert_int_eq(
        xrdp_avc444_caps_supports_v2(XR_RDPGFX_CAPVERSION_101, AVC420_EN), 1);

    /* v10.2..10.7 support v2 unless AVC is disabled on that capset */
    versions[0] = XR_RDPGFX_CAPVERSION_102;
    versions[1] = XR_RDPGFX_CAPVERSION_103;
    versions[2] = XR_RDPGFX_CAPVERSION_104;
    versions[3] = XR_RDPGFX_CAPVERSION_105;
    versions[4] = XR_RDPGFX_CAPVERSION_106;
    versions[5] = XR_RDPGFX_CAPVERSION_107;
    for (i = 0; i < 6; i++)
    {
        ck_assert_int_eq(xrdp_avc444_caps_supports_v2(versions[i], 0), 1);
        ck_assert_int_eq(xrdp_avc444_caps_supports_v2(versions[i], THIN), 1);
        ck_assert_int_eq(xrdp_avc444_caps_supports_v2(versions[i], AVC_DIS), 0);
    }

    /* pre-v2 capsets and unknowns never signal v2 */
    ck_assert_int_eq(xrdp_avc444_caps_supports_v2(XR_RDPGFX_CAPVERSION_8, 0),
                     0);
    ck_assert_int_eq(xrdp_avc444_caps_supports_v2(XR_RDPGFX_CAPVERSION_81,
                     AVC420_EN), 0);
    ck_assert_int_eq(xrdp_avc444_caps_supports_v2(XR_RDPGFX_CAPVERSION_10, 0),
                     0);
    ck_assert_int_eq(xrdp_avc444_caps_supports_v2(0x12345678, 0), 0);
}
END_TEST

/******************************************************************************/
Suite *
make_suite_avc444_caps(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444Caps");
    tc = tcase_create("avc444_caps");
    tcase_add_test(tc, test_caps_v8_none);
    tcase_add_test(tc, test_caps_v81);
    tcase_add_test(tc, test_caps_v10_avc444);
    tcase_add_test(tc, test_caps_v101_excluded);
    tcase_add_test(tc, test_caps_v102_to_107);
    tcase_add_test(tc, test_caps_unknown_none);
    tcase_add_test(tc, test_caps_v2_support);
    suite_add_tcase(s, tc);
    return s;
}
