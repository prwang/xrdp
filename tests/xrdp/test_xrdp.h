#ifndef TEST_XRDP_H
#define TEST_XRDP_H

#include <check.h>

Suite *make_suite_test_bitmap_load(void);
Suite *make_suite_test_keymap_load(void);
Suite *make_suite_egfx_base_functions(void);
Suite *make_suite_region(void);
Suite *make_suite_tconfig_load_gfx(void);
Suite *make_suite_avc444_convert(void);
Suite *make_suite_avc444_caps(void);
Suite *make_suite_avc444_metablock(void);
Suite *make_suite_avc444_h264(void);
Suite *make_suite_avc444_ltr(void);
Suite *make_suite_avc444_nut(void);
Suite *make_suite_avc444_ffmpeg(void);
Suite *make_suite_avc444_multimon(void);
Suite *make_suite_avc444_emit_split(void);
Suite *make_suite_avc444_credit_frontier(void);
Suite *make_suite_avc444_chroma_due(void);

#endif /* TEST_XRDP_H */
