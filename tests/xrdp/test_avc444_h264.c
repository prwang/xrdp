#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "xrdp_h264_annexb.h"
#include "test_xrdp.h"

/* NAL header bytes: forbidden(1)|nal_ref_idc(2)|type(5) */
#define NAL_SPS 0x67   /* type 7 */
#define NAL_PPS 0x68   /* type 8 */
#define NAL_IDR 0x65   /* type 5 */
#define NAL_SLICE 0x41 /* type 1 */
#define NAL_SEI 0x06   /* type 6 */

START_TEST(test_h264_main_reset_sps_pps_idr)
{
    /* SEI, SPS, PPS, IDR — a valid first main packet */
    unsigned char au[] =
    {
        0, 0, 0, 1, NAL_SEI, 0x11,
        0, 0, 0, 1, NAL_SPS, 0x42, 0xc0, 0x0a,
        0, 0, 1, NAL_PPS, 0xce,
        0, 0, 0, 1, NAL_IDR, 0x88, 0x99, 0xaa
    };
    struct xrdp_h264_nal_summary s;

    ck_assert_int_eq(xrdp_h264_scan_annexb(au, sizeof(au), &s), 0);
    ck_assert_int_eq(s.valid, 1);
    ck_assert_int_eq(s.has_sps, 1);
    ck_assert_int_eq(s.has_pps, 1);
    ck_assert_int_eq(s.has_idr, 1);
    ck_assert_int_eq(s.has_vcl, 1);
    ck_assert_int_eq(s.nal_count, 4);
    ck_assert_int_eq(s.sps_count, 1);
    ck_assert_int_eq(s.pps_count, 1);
    ck_assert_int_eq(xrdp_h264_main_reset_ok(au, sizeof(au)), 1);
}
END_TEST

START_TEST(test_h264_duplicate_sps_counted)
{
    /* the Mac-black wire shape: dump_extra chained onto an in-band
     * encoder duplicates the parameter sets (2 SPS / 2 PPS observed live,
     * 2026-07-23). The counts let probe and runtime enforce the
     * exactly-one-SPS reset bound (PRD FR-PROBE-6). */
    unsigned char au[] =
    {
        0, 0, 0, 1, NAL_SPS, 0x42, 0xc0, 0x0a,
        0, 0, 1, NAL_PPS, 0xce,
        0, 0, 0, 1, NAL_SPS, 0x42, 0xc0, 0x0a,
        0, 0, 1, NAL_PPS, 0xce,
        0, 0, 0, 1, NAL_IDR, 0x88, 0x99
    };
    struct xrdp_h264_nal_summary s;

    ck_assert_int_eq(xrdp_h264_scan_annexb(au, sizeof(au), &s), 0);
    ck_assert_int_eq(s.valid, 1);
    ck_assert_int_eq(s.sps_count, 2);
    ck_assert_int_eq(s.pps_count, 2);
    /* presence-only check still passes -- the duplicate bound is the
     * caller's (probe/pop_pair) responsibility via sps_count */
    ck_assert_int_eq(xrdp_h264_main_reset_ok(au, sizeof(au)), 1);
}
END_TEST

START_TEST(test_h264_main_reset_missing_pps)
{
    unsigned char au[] =
    {
        0, 0, 0, 1, NAL_SPS, 0x42,
        0, 0, 0, 1, NAL_IDR, 0x88
    };
    ck_assert_int_eq(xrdp_h264_main_reset_ok(au, sizeof(au)), 0);
}
END_TEST

START_TEST(test_h264_aux_vcl)
{
    unsigned char au[] = { 0, 0, 0, 1, NAL_SLICE, 0x9a, 0xbc };
    struct xrdp_h264_nal_summary s;

    ck_assert_int_eq(xrdp_h264_scan_annexb(au, sizeof(au), &s), 0);
    ck_assert_int_eq(s.has_vcl, 1);
    ck_assert_int_eq(s.has_idr, 0);
    ck_assert_int_eq(xrdp_h264_aux_ok(au, sizeof(au)), 1);
    /* a stream with no VCL fails the aux requirement */
    {
        unsigned char sps_only[] = { 0, 0, 0, 1, NAL_SPS, 0x42 };
        ck_assert_int_eq(xrdp_h264_aux_ok(sps_only, sizeof(sps_only)), 0);
    }
}
END_TEST

START_TEST(test_h264_malformed)
{
    struct xrdp_h264_nal_summary s;
    unsigned char no_sc[] = { 0x12, 0x34, 0x56, 0x78, 0x9a };
    unsigned char forbidden[] = { 0, 0, 0, 1, 0xE5, 0x11 }; /* forbidden bit */
    unsigned char tiny[] = { 0, 0 };

    ck_assert_int_eq(xrdp_h264_scan_annexb(no_sc, sizeof(no_sc), &s), 1);
    ck_assert_int_eq(s.valid, 0);

    ck_assert_int_eq(xrdp_h264_scan_annexb(tiny, sizeof(tiny), &s), 1);

    ck_assert_int_eq(xrdp_h264_scan_annexb(forbidden, sizeof(forbidden), &s), 1);
    ck_assert_int_eq(s.forbidden_bit_set, 1);
    ck_assert_int_eq(s.valid, 0);

    ck_assert_int_eq(xrdp_h264_scan_annexb(NULL, 0, &s), 1);
}
END_TEST

/******************************************************************************/
Suite *
make_suite_avc444_h264(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444H264");
    tc = tcase_create("avc444_h264");
    tcase_add_test(tc, test_h264_main_reset_sps_pps_idr);
    tcase_add_test(tc, test_h264_duplicate_sps_counted);
    tcase_add_test(tc, test_h264_main_reset_missing_pps);
    tcase_add_test(tc, test_h264_aux_vcl);
    tcase_add_test(tc, test_h264_malformed);
    suite_add_tcase(s, tc);
    return s;
}
