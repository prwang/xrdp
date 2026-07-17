#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xrdp_nut.h"
#include "xrdp_h264_annexb.h"
#include "test_xrdp.h"

#define FIXTURE XRDP_TOP_SRCDIR "/tests/xrdp/avc444/fixture_4frame.nut"

static unsigned char *
load_fixture(int *len)
{
    FILE *f = fopen(FIXTURE, "rb");
    unsigned char *buf;
    long sz;

    if (f == NULL)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(sz);
    if (buf != NULL && fread(buf, 1, sz, f) != (size_t)sz)
    {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (int)sz;
    return buf;
}

/* feed the whole fixture at once, expect stream-ready + 4 ordered packets */
START_TEST(test_nut_four_packets)
{
    int len;
    unsigned char *fx = load_fixture(&len);
    struct xrdp_nut_ctx *c;
    struct xrdp_nut_packet pkt;
    int expected_size[4] = { 1356, 63, 64, 41 };
    long long expected_pts[4] = { 0, 512, 1024, 1536 };
    int got = 0;
    int saw_ready = 0;
    enum xrdp_nut_event_type ev;

    ck_assert_ptr_ne(fx, NULL);
    ck_assert_int_gt(len, 0);
    c = xrdp_nut_create(0, 0, 0);
    ck_assert_ptr_ne(c, NULL);
    ck_assert_int_eq(xrdp_nut_feed(c, fx, len), 0);

    for (;;)
    {
        ev = xrdp_nut_next(c, &pkt);
        if (ev == XRDP_NUT_STREAM_READY)
        {
            saw_ready = 1;
            continue;
        }
        if (ev == XRDP_NUT_PACKET)
        {
            ck_assert_int_lt(got, 4);
            ck_assert_int_eq(pkt.stream_id, 0);
            ck_assert_int_eq(pkt.len, expected_size[got]);
            ck_assert_int_eq((int)pkt.pts, (int)expected_pts[got]);
            if (got == 0)
            {
                /* first packet is a keyframe carrying SPS/PPS/IDR */
                ck_assert_int_eq(pkt.keyframe, 1);
                ck_assert_int_eq(xrdp_h264_main_reset_ok(pkt.data, pkt.len), 1);
            }
            else
            {
                ck_assert_int_eq(xrdp_h264_aux_ok(pkt.data, pkt.len), 1);
            }
            got++;
            continue;
        }
        break; /* NEED_MORE (end of buffered data) or ERROR */
    }
    ck_assert_int_eq(saw_ready, 1);
    ck_assert_int_eq(got, 4);
    ck_assert_int_eq(ev, XRDP_NUT_NEED_MORE);
    xrdp_nut_delete(c);
    free(fx);
}
END_TEST

/* one byte at a time exercises fragmentation / NEED_MORE handling */
START_TEST(test_nut_byte_fragmentation)
{
    int len;
    unsigned char *fx = load_fixture(&len);
    struct xrdp_nut_ctx *c;
    struct xrdp_nut_packet pkt;
    int i;
    int got = 0;
    int expected_size[4] = { 1356, 63, 64, 41 };

    ck_assert_ptr_ne(fx, NULL);
    c = xrdp_nut_create(0, 0, 0);
    for (i = 0; i < len; i++)
    {
        enum xrdp_nut_event_type ev;
        ck_assert_int_eq(xrdp_nut_feed(c, fx + i, 1), 0);
        do
        {
            ev = xrdp_nut_next(c, &pkt);
            if (ev == XRDP_NUT_PACKET)
            {
                ck_assert_int_lt(got, 4);
                ck_assert_int_eq(pkt.len, expected_size[got]);
                got++;
            }
            ck_assert_int_ne(ev, XRDP_NUT_ERROR);
        }
        while (ev == XRDP_NUT_PACKET || ev == XRDP_NUT_STREAM_READY);
    }
    ck_assert_int_eq(got, 4);
    xrdp_nut_delete(c);
    free(fx);
}
END_TEST

/* a truncated stream must report NEED_MORE, never a false packet or error */
START_TEST(test_nut_truncation)
{
    int len;
    unsigned char *fx = load_fixture(&len);
    int cut;

    ck_assert_ptr_ne(fx, NULL);
    for (cut = 1; cut < len; cut += 7)
    {
        struct xrdp_nut_ctx *c = xrdp_nut_create(0, 0, 0);
        struct xrdp_nut_packet pkt;
        enum xrdp_nut_event_type ev;
        ck_assert_int_eq(xrdp_nut_feed(c, fx, cut), 0);
        do
        {
            ev = xrdp_nut_next(c, &pkt);
            ck_assert_int_ne(ev, XRDP_NUT_ERROR);
        }
        while (ev == XRDP_NUT_PACKET || ev == XRDP_NUT_STREAM_READY);
        ck_assert_int_eq(ev, XRDP_NUT_NEED_MORE);
        xrdp_nut_delete(c);
    }
    free(fx);
}
END_TEST

/* corrupting a header CRC byte must be detected */
START_TEST(test_nut_bad_crc)
{
    int len;
    unsigned char *fx = load_fixture(&len);
    struct xrdp_nut_ctx *c;
    struct xrdp_nut_packet pkt;
    enum xrdp_nut_event_type ev;
    int saw_error = 0;

    ck_assert_ptr_ne(fx, NULL);
    /* flip a byte inside the main header body (offset 0x30) */
    fx[0x30] ^= 0xff;
    c = xrdp_nut_create(0, 0, 0);
    ck_assert_int_eq(xrdp_nut_feed(c, fx, len), 0);
    for (;;)
    {
        ev = xrdp_nut_next(c, &pkt);
        if (ev == XRDP_NUT_ERROR)
        {
            saw_error = 1;
            break;
        }
        if (ev == XRDP_NUT_NEED_MORE)
        {
            break;
        }
    }
    ck_assert_int_eq(saw_error, 1);
    xrdp_nut_delete(c);
    free(fx);
}
END_TEST

/* a bad file identifier is rejected */
START_TEST(test_nut_bad_file_id)
{
    struct xrdp_nut_ctx *c = xrdp_nut_create(0, 0, 0);
    struct xrdp_nut_packet pkt;
    unsigned char junk[64];

    memset(junk, 'x', sizeof(junk));
    ck_assert_int_eq(xrdp_nut_feed(c, junk, sizeof(junk)), 0);
    ck_assert_int_eq(xrdp_nut_next(c, &pkt), XRDP_NUT_ERROR);
    xrdp_nut_delete(c);
}
END_TEST

/* the total ceiling bounds buffered bytes */
START_TEST(test_nut_total_ceiling)
{
    struct xrdp_nut_ctx *c = xrdp_nut_create(0, 0, 1024);
    unsigned char blob[2048];

    memset(blob, 0, sizeof(blob));
    ck_assert_int_eq(xrdp_nut_feed(c, blob, sizeof(blob)), -1);
    xrdp_nut_delete(c);
}
END_TEST

/******************************************************************************/
Suite *
make_suite_avc444_nut(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444Nut");
    tc = tcase_create("avc444_nut");
    tcase_add_test(tc, test_nut_four_packets);
    tcase_add_test(tc, test_nut_byte_fragmentation);
    tcase_add_test(tc, test_nut_truncation);
    tcase_add_test(tc, test_nut_bad_crc);
    tcase_add_test(tc, test_nut_bad_file_id);
    tcase_add_test(tc, test_nut_total_ceiling);
    suite_add_tcase(s, tc);
    return s;
}
