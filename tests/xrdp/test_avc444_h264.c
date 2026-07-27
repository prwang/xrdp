#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <string.h>

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


/* Real SPS NALs captured from the 2026-07-27 bisect matrix (Mesa VAAPI,
 * High 4.2, 1280x720). ARM_A: CQP baseline, no HRD -- renders on the
 * macOS Windows App. ARM_C: CBR, nal_hrd_parameters present -- black on
 * the Mac even with every SEI NAL stripped. Field-level diff of the two
 * is EXACTLY the HRD region, so sanitizing ARM_C must yield ARM_A
 * byte-for-byte. Both carry a 00 00 03 emulation-prevention byte. */
static const unsigned char sps_arm_a[] =
{
    0x67, 0x64, 0x0c, 0x2a, 0xac, 0x2b, 0x40, 0x28,
    0x02, 0xdd, 0x37, 0x01, 0x01, 0x01, 0x40, 0x00,
    0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x3c, 0x23,
    0xc2, 0x21, 0x1a, 0x80
};
static const unsigned char sps_arm_c[] =
{
    0x67, 0x64, 0x0c, 0x2a, 0xac, 0x2b, 0x40, 0x28,
    0x02, 0xdd, 0x37, 0x01, 0x01, 0x01, 0x40, 0x00,
    0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x3c, 0x39,
    0xa8, 0x00, 0x09, 0x89, 0x60, 0x00, 0x13, 0x12,
    0xcb, 0xdc, 0xe0, 0x1e, 0x11, 0x08, 0xd4
};

/* Real nvenc SPS captured from the T4 (2026-07-27, h264_nvenc profile
 * high, refs=1/dpb_size=1, post sanitize_hrd): Main-profile-free VUI
 * with pic_struct_present_flag = 1 and no HRD. Clearing pic_struct is a
 * one-bit in-place change (byte 22: 0x13 -> 0x11); every other field must stay bit-exact (ffmpeg trace_headers
 * verified). */
static const unsigned char sps_nvenc_ps1[] =
{
    0x67, 0x4d, 0x40, 0x33, 0x95, 0xa0, 0x19, 0x01,
    0xce, 0xc0, 0x5b, 0x80, 0x80, 0x80, 0xa0, 0x00,
    0x00, 0x7d, 0x00, 0x00, 0x75, 0x30, 0x13, 0xe3,
    0x85, 0x54
};
static const unsigned char sps_nvenc_ps0[] =
{
    0x67, 0x4d, 0x40, 0x33, 0x95, 0xa0, 0x19, 0x01,
    0xce, 0xc0, 0x5b, 0x80, 0x80, 0x80, 0xa0, 0x00,
    0x00, 0x7d, 0x00, 0x00, 0x75, 0x30, 0x11, 0xe3,
    0x85, 0x54
};

START_TEST(test_h264_strip_pic_struct_clears_flag)
{
    unsigned char au[4 + sizeof(sps_nvenc_ps1)];
    unsigned char want[4 + sizeof(sps_nvenc_ps0)];
    int len;

    memset(au, 0, sizeof(au));
    au[3] = 1;
    memcpy(au + 4, sps_nvenc_ps1, sizeof(sps_nvenc_ps1));
    memset(want, 0, sizeof(want));
    want[3] = 1;
    memcpy(want + 4, sps_nvenc_ps0, sizeof(sps_nvenc_ps0));
    len = sizeof(au);
    ck_assert_int_eq(xrdp_h264_strip_pic_struct(au, &len), 0);
    ck_assert_int_eq(len, (int)sizeof(want));
    ck_assert_mem_eq(au, want, sizeof(want));
    /* idempotent: a second pass changes nothing */
    ck_assert_int_eq(xrdp_h264_strip_pic_struct(au, &len), 0);
    ck_assert_int_eq(len, (int)sizeof(want));
    ck_assert_mem_eq(au, want, sizeof(want));
}
END_TEST

START_TEST(test_h264_strip_pic_struct_zero_flag_untouched)
{
    /* the VAAPI arm-A SPS declares pic_struct = 0 already: must pass
     * through byte-identical (and sanitize_hrd's golden output shape
     * stays valid input for the pic_struct pass) */
    unsigned char au[4 + sizeof(sps_arm_a)];
    unsigned char want[4 + sizeof(sps_arm_a)];
    int len;

    memset(au, 0, sizeof(au));
    au[3] = 1;
    memcpy(au + 4, sps_arm_a, sizeof(sps_arm_a));
    memcpy(want, au, sizeof(au));
    len = sizeof(au);
    ck_assert_int_eq(xrdp_h264_strip_pic_struct(au, &len), 0);
    ck_assert_int_eq(len, (int)sizeof(want));
    ck_assert_mem_eq(au, want, sizeof(want));
}
END_TEST

START_TEST(test_h264_strip_pic_struct_truncated_sps_fails)
{
    unsigned char au[4 + 8];
    int len;

    memset(au, 0, sizeof(au));
    au[3] = 1;
    memcpy(au + 4, sps_nvenc_ps1, 8); /* cut mid-SPS */
    len = sizeof(au);
    ck_assert_int_ne(xrdp_h264_strip_pic_struct(au, &len), 0);
}
END_TEST

START_TEST(test_h264_sanitize_hrd_rewrites_to_golden)
{
    /* AU: [SPS-with-HRD][PPS][IDR] -> sanitize -> the SPS must become the
     * captured no-HRD SPS bit-exactly and the tail must stay intact */
    unsigned char au[4 + sizeof(sps_arm_c) + 5 + 7 + 64];
    unsigned char want[4 + sizeof(sps_arm_a) + 5 + 7 + 64];
    static const unsigned char pps[] = { 0, 0, 1, NAL_PPS, 0xce };
    static const unsigned char idr[] =
    { 0, 0, 0, 1, NAL_IDR, 0x88, 0x99 };
    static const unsigned char sc4[] = { 0, 0, 0, 1 };
    int len;
    int want_len;

    len = 0;
    memcpy(au + len, sc4, 4);
    len += 4;
    memcpy(au + len, sps_arm_c, sizeof(sps_arm_c));
    len += sizeof(sps_arm_c);
    memcpy(au + len, pps, sizeof(pps));
    len += sizeof(pps);
    memcpy(au + len, idr, sizeof(idr));
    len += sizeof(idr);

    want_len = 0;
    memcpy(want + want_len, sc4, 4);
    want_len += 4;
    memcpy(want + want_len, sps_arm_a, sizeof(sps_arm_a));
    want_len += sizeof(sps_arm_a);
    memcpy(want + want_len, pps, sizeof(pps));
    want_len += sizeof(pps);
    memcpy(want + want_len, idr, sizeof(idr));
    want_len += sizeof(idr);

    ck_assert_int_eq(xrdp_h264_sanitize_hrd(au, &len), 0);
    ck_assert_int_eq(len, want_len);
    ck_assert_int_eq(memcmp(au, want, len), 0);
    ck_assert_int_eq(xrdp_h264_main_reset_ok(au, len), 1);
}
END_TEST

START_TEST(test_h264_sanitize_hrd_no_hrd_untouched)
{
    /* an SPS already without HRD must pass through bit-exactly */
    unsigned char au[4 + sizeof(sps_arm_a) + 7];
    unsigned char orig[sizeof(au)];
    static const unsigned char sc4[] = { 0, 0, 0, 1 };
    static const unsigned char idr[] =
    { 0, 0, 0, 1, NAL_IDR, 0x88, 0x99 };
    int len;

    memcpy(au, sc4, 4);
    memcpy(au + 4, sps_arm_a, sizeof(sps_arm_a));
    memcpy(au + 4 + sizeof(sps_arm_a), idr, sizeof(idr));
    len = sizeof(au);
    memcpy(orig, au, sizeof(au));

    ck_assert_int_eq(xrdp_h264_sanitize_hrd(au, &len), 0);
    ck_assert_int_eq(len, (int)sizeof(au));
    ck_assert_int_eq(memcmp(au, orig, len), 0);
}
END_TEST

START_TEST(test_h264_sanitize_hrd_idempotent)
{
    /* mid-stream SPS repeats (-g refresh) hit the rewrite again: the
     * second pass must be a no-op on the already-sanitized bytes */
    unsigned char au[4 + sizeof(sps_arm_c)];
    unsigned char once[sizeof(au)];
    static const unsigned char sc4[] = { 0, 0, 0, 1 };
    int len;
    int len_once;

    memcpy(au, sc4, 4);
    memcpy(au + 4, sps_arm_c, sizeof(sps_arm_c));
    len = sizeof(au);
    ck_assert_int_eq(xrdp_h264_sanitize_hrd(au, &len), 0);
    len_once = len;
    memcpy(once, au, len);
    ck_assert_int_eq(xrdp_h264_sanitize_hrd(au, &len), 0);
    ck_assert_int_eq(len, len_once);
    ck_assert_int_eq(memcmp(au, once, len), 0);
}
END_TEST

START_TEST(test_h264_sanitize_hrd_truncated_sps_fails)
{
    /* a truncated SPS with the HRD flag set must fail loudly, never
     * ship a half-rewritten stream (strict honesty: no silent pass) */
    unsigned char au[4 + 20];
    int len;

    memcpy(au, "\x00\x00\x00\x01", 4);
    memcpy(au + 4, sps_arm_c, 20);
    len = sizeof(au);
    ck_assert_int_ne(xrdp_h264_sanitize_hrd(au, &len), 0);
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
    tcase_add_test(tc, test_h264_sanitize_hrd_rewrites_to_golden);
    tcase_add_test(tc, test_h264_sanitize_hrd_no_hrd_untouched);
    tcase_add_test(tc, test_h264_sanitize_hrd_idempotent);
    tcase_add_test(tc, test_h264_sanitize_hrd_truncated_sps_fails);
    tcase_add_test(tc, test_h264_strip_pic_struct_clears_flag);
    tcase_add_test(tc, test_h264_strip_pic_struct_zero_flag_untouched);
    tcase_add_test(tc, test_h264_strip_pic_struct_truncated_sps_fails);
    suite_add_tcase(s, tc);
    return s;
}
