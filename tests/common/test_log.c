
#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <string.h>

#include "log.h"
#include "os_calls.h"

#include "test_common.h"

/******************************************************************************/
/* The millisecond field of a log timestamp used to be computed as
   (tv.tv_usec + 500 / 1000), i.e. microseconds plus zero, and then
   truncated to three characters -- so a sub-second part below 100 ms
   printed the leading digits of the microsecond count and every such
   timestamp was up to 0.9 s late. These cases pin the conversion. */

START_TEST(test_log_usec_to_msec_zero)
{
    ck_assert_int_eq(log_usec_to_msec(0), 0);
}
END_TEST

START_TEST(test_log_usec_to_msec_rounds_up)
{
    /* the buggy version returned 999 here */
    ck_assert_int_eq(log_usec_to_msec(999), 1);
    ck_assert_int_eq(log_usec_to_msec(500), 1);
    ck_assert_int_eq(log_usec_to_msec(1500), 2);
}
END_TEST

START_TEST(test_log_usec_to_msec_rounds_down)
{
    ck_assert_int_eq(log_usec_to_msec(499), 0);
    ck_assert_int_eq(log_usec_to_msec(1499), 1);
}
END_TEST

START_TEST(test_log_usec_to_msec_small_fraction)
{
    /* the buggy version returned 451 (the leading digits of 45123) */
    ck_assert_int_eq(log_usec_to_msec(45123), 45);
    ck_assert_int_eq(log_usec_to_msec(9876), 10);
}
END_TEST

START_TEST(test_log_usec_to_msec_large_fraction)
{
    ck_assert_int_eq(log_usec_to_msec(999499), 999);
    ck_assert_int_eq(log_usec_to_msec(123456), 123);
}
END_TEST

START_TEST(test_log_usec_to_msec_never_carries)
{
    /* rounding up out of the second would print a fourth digit and
       disagree with the seconds field the value is appended to */
    ck_assert_int_eq(log_usec_to_msec(999500), 999);
    ck_assert_int_eq(log_usec_to_msec(999999), 999);
}
END_TEST

START_TEST(test_log_usec_to_msec_is_three_digits)
{
    /* every microsecond value in range must format as exactly three
       digits in the timestamp's millisecond field */
    int usec;
    char buf[4];

    for (usec = 0; usec < 1000000; usec += 7)
    {
        g_snprintf(buf, sizeof(buf), "%03d", log_usec_to_msec(usec));
        ck_assert_int_eq((int)strlen(buf), 3);
        ck_assert_int_eq(buf[0] >= '0' && buf[0] <= '9', 1);
        ck_assert_int_eq(buf[1] >= '0' && buf[1] <= '9', 1);
        ck_assert_int_eq(buf[2] >= '0' && buf[2] <= '9', 1);
    }
}
END_TEST

START_TEST(test_log_formatted_datetime_shape)
{
    /* [2026-07-30T12:34:56.789+0100] -- the millisecond field is three
       digits, so the whole stamp has a fixed width and fixed layout */
    char buf[64];
    int i;

    getFormattedDateTime(buf, sizeof(buf));

    ck_assert_int_eq((int)strlen(buf), 31);
    ck_assert_int_eq(buf[0], '[');
    ck_assert_int_eq(buf[11], 'T');
    ck_assert_int_eq(buf[20], '.');
    for (i = 21; i < 24; ++i)
    {
        ck_assert_int_eq(buf[i] >= '0' && buf[i] <= '9', 1);
    }
    ck_assert_int_eq(buf[24] == '+' || buf[24] == '-', 1);
    ck_assert_int_eq(buf[29], ']');
    ck_assert_int_eq(buf[30], ' ');
}
END_TEST

/******************************************************************************/

Suite *
make_suite_test_log(void)
{
    Suite *s;
    TCase *tc_log;

    s = suite_create("Log");

    tc_log = tcase_create("log_timestamp");
    suite_add_tcase(s, tc_log);
    tcase_add_test(tc_log, test_log_usec_to_msec_zero);
    tcase_add_test(tc_log, test_log_usec_to_msec_rounds_up);
    tcase_add_test(tc_log, test_log_usec_to_msec_rounds_down);
    tcase_add_test(tc_log, test_log_usec_to_msec_small_fraction);
    tcase_add_test(tc_log, test_log_usec_to_msec_large_fraction);
    tcase_add_test(tc_log, test_log_usec_to_msec_never_carries);
    tcase_add_test(tc_log, test_log_usec_to_msec_is_three_digits);
    tcase_add_test(tc_log, test_log_formatted_datetime_shape);

    return s;
}
