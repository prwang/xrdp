/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2021
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Test driver for XRDP routines
 *
 * If you want to run this driver under valgrind to check for memory leaks,
 * use the following command line:-
 *
 * CK_FORK=no valgrind --leak-check=full --show-leak-kinds=all \
 *     .libs/test_xrdp
 *
 * without the 'CK_FORK=no', memory still allocated by the test driver will
 * be logged
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "log.h"
#include "os_calls.h"
#include <stdlib.h>
#include <string.h>
#include "ms-rdpbcgr.h"
#include "xrdp_egfx.h"
#include "xrdp_encoder.h"
#include "test_xrdp.h"

START_TEST(test_xrdp_egfx_send_create_surface__happy_path)
{
    struct xrdp_egfx_bulk *bulk = g_new0(struct xrdp_egfx_bulk, 1);

    const int surface_id = 0xFF;
    const int width = 640;
    const int height = 480;
    const int pixel_format = 32;

    struct stream *s = xrdp_egfx_create_surface(
                           bulk, surface_id, width, height, pixel_format);
    s->p = s->data;

    unsigned char descriptor;
    in_uint8(s, descriptor);
    ck_assert_int_eq(0xE0, descriptor);

    free_stream(s);
    g_free(bulk);
}
END_TEST

/******************************************************************************/
/* BACKLOG #45 step 7 -- gfx_egfx_batch_peek_mon / gfx_egfx_batch_group.
 *
 * These are the only PURE parts of step 7. What they prove: that exactly
 * the xorgxrdp AVC444 blob shape is recognised, that every other EGFX
 * command shape and every truncation is rejected without an
 * out-of-bounds read, and that the grouping rule is "at most one item per
 * monitor index; a repeat or a non-batchable item ends the batch". What
 * they cannot prove is left to the live E3/E5/E7 runs: that a set really
 * arms 2m children in one pump, that the per-monitor PDU order and acks
 * are unchanged on the wire, and that no monitor starves. */

#define TB_STARTFRAME_BYTES 16
#define TB_ENDFRAME_BYTES   12
#define TB_W2S1_FIXED_BYTES 33

static void
tb_u16(unsigned char *p, int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void
tb_u32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

/* Build the EXACT xorgxrdp AVC444 blob: STARTFRAME + WIRETOSURFACE_1 +
 * ENDFRAME, field for field as rdpClientCon.c's CC_GFX_AVC444 arm emits
 * it. Returns the total byte count. */
static int
tb_build(unsigned char *buf, int mon, int codec_id, int nd, int nc)
{
    unsigned char *p;
    int w2s1_bytes;
    int total;

    w2s1_bytes = TB_W2S1_FIXED_BYTES + nd * 8 + nc * 8;
    total = TB_STARTFRAME_BYTES + w2s1_bytes + TB_ENDFRAME_BYTES;
    memset(buf, 0, total);
    p = buf;
    tb_u16(p, XR_RDPGFX_CMDID_STARTFRAME);
    tb_u16(p + 2, 0);
    tb_u32(p + 4, TB_STARTFRAME_BYTES);
    tb_u32(p + 8, 77);                        /* frame_id   */
    tb_u32(p + 12, 0);                        /* time_stamp */
    p += TB_STARTFRAME_BYTES;
    tb_u16(p, XR_RDPGFX_CMDID_WIRETOSURFACE_1);
    tb_u16(p + 2, 0);
    tb_u32(p + 4, (unsigned int)w2s1_bytes);
    tb_u16(p + 8, mon);                       /* surface_id   */
    tb_u16(p + 10, codec_id);
    p[12] = 0x20;                             /* pixel_format */
    tb_u32(p + 13, (unsigned int)mon << 28);  /* flags        */
    tb_u16(p + 17, nd);
    tb_u16(p + 19 + nd * 8, nc);
    p += 21 + nd * 8 + nc * 8;
    tb_u16(p, 0);                             /* left   */
    tb_u16(p + 2, 0);                         /* top    */
    tb_u16(p + 4, 1024);                      /* width  */
    tb_u16(p + 6, 768);                       /* height */
    tb_u32(p + 8, 0);                         /* shmem_offset */
    p += 12;
    tb_u16(p, XR_RDPGFX_CMDID_ENDFRAME);
    tb_u16(p + 2, 0);
    tb_u32(p + 4, TB_ENDFRAME_BYTES);
    tb_u32(p + 8, 77);
    return total;
}

/* One EGFX command of cmd_id, alone in its own blob */
static int
tb_build_single_cmd(unsigned char *buf, int cmd_id, int body_bytes)
{
    memset(buf, 0, 8 + body_bytes);
    tb_u16(buf, cmd_id);
    tb_u16(buf + 2, 0);
    tb_u32(buf + 4, (unsigned int)(8 + body_bytes));
    return 8 + body_bytes;
}

/* Feed the first n bytes of blob through a heap buffer of EXACTLY n bytes,
 * so an out-of-bounds read is a real heap over-read (valgrind/ASAN see
 * it) and not a silent walk into the rest of a big stack array. */
static int
tb_peek_truncated(const unsigned char *blob, int n)
{
    char *tight;
    int rv;

    tight = (char *)malloc(n > 0 ? n : 1);
    ck_assert_ptr_ne(tight, NULL);
    if (n > 0)
    {
        memcpy(tight, blob, n);
    }
    rv = gfx_egfx_batch_peek_mon(tight, n);
    free(tight);
    return rv;
}

START_TEST(test_batch_peek_accepts_the_exact_avc444_shape)
{
    unsigned char blob[512];
    int total;
    int mon;

    for (mon = 0; mon < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; mon++)
    {
        total = tb_build(blob, mon, XR_RDPGFX_CODECID_AVC444, 1, 1);
        ck_assert_int_eq(TB_STARTFRAME_BYTES + TB_W2S1_FIXED_BYTES + 16 +
                         TB_ENDFRAME_BYTES, total);
        ck_assert_int_eq(mon, gfx_egfx_batch_peek_mon((char *)blob, total));
        /* AVC444 v2 is the same shape with the v2 codec id */
        total = tb_build(blob, mon, XR_RDPGFX_CODECID_AVC444V2, 1, 1);
        ck_assert_int_eq(mon, gfx_egfx_batch_peek_mon((char *)blob, total));
    }
    /* several damage rects and several copy rects are still the shape */
    total = tb_build(blob, 3, XR_RDPGFX_CODECID_AVC444, 9, 4);
    ck_assert_int_eq(3, gfx_egfx_batch_peek_mon((char *)blob, total));
}
END_TEST

START_TEST(test_batch_peek_rejects_bad_command_ids)
{
    unsigned char blob[512];
    int total;

    /* the STARTFRAME slot holding something else */
    total = tb_build(blob, 1, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u16(blob, XR_RDPGFX_CMDID_WIRETOSURFACE_1);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    /* the WIRETOSURFACE_1 slot holding WIRETOSURFACE_2 */
    total = tb_build(blob, 1, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u16(blob + TB_STARTFRAME_BYTES, XR_RDPGFX_CMDID_WIRETOSURFACE_2);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    /* the ENDFRAME slot holding something else */
    total = tb_build(blob, 1, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u16(blob + total - TB_ENDFRAME_BYTES, XR_RDPGFX_CMDID_STARTFRAME);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
}
END_TEST

START_TEST(test_batch_peek_rejects_other_egfx_commands)
{
    unsigned char blob[512];
    int total;
    int index;
    static const int cmd_ids[] =
    {
        XR_RDPGFX_CMDID_WIRETOSURFACE_2,
        XR_RDPGFX_CMDID_SOLIDFILL,
        XR_RDPGFX_CMDID_SURFACETOSURFACE,
        XR_RDPGFX_CMDID_CREATESURFACE,
        XR_RDPGFX_CMDID_DELETESURFACE,
        XR_RDPGFX_CMDID_RESETGRAPHICS,
        XR_RDPGFX_CMDID_MAPSURFACETOOUTPUT,
        XR_RDPGFX_CMDID_STARTFRAME,
        XR_RDPGFX_CMDID_ENDFRAME
    };

    for (index = 0; index < (int)(sizeof(cmd_ids) / sizeof(cmd_ids[0]));
            index++)
    {
        total = tb_build_single_cmd(blob, cmd_ids[index], 24);
        ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    }
}
END_TEST

START_TEST(test_batch_peek_rejects_wrong_codec_ids)
{
    unsigned char blob[512];
    int total;
    int index;
    static const int codec_ids[] = { 0x0000, 0x0009, 0x000B, 0x000C, 0x000D,
                                     0x0010, 0xFFFF
                                   };

    for (index = 0; index < (int)(sizeof(codec_ids) / sizeof(codec_ids[0]));
            index++)
    {
        total = tb_build(blob, 1, codec_ids[index], 1, 1);
        ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    }
}
END_TEST

START_TEST(test_batch_peek_rejects_every_truncation)
{
    unsigned char blob[1024];
    int total;
    int n;

    total = tb_build(blob, 5, XR_RDPGFX_CODECID_AVC444, 6, 3);
    ck_assert_int_eq(5, gfx_egfx_batch_peek_mon((char *)blob, total));
    for (n = 0; n < total; n++)
    {
        ck_assert_int_eq(-1, tb_peek_truncated(blob, n));
    }
    /* and a padded blob is not the shape either */
    blob[total] = 0;
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total + 1));
}
END_TEST

START_TEST(test_batch_peek_rejects_lying_lengths)
{
    unsigned char blob[512];
    int total;

    /* STARTFRAME claiming a size it does not have */
    total = tb_build(blob, 2, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u32(blob + 4, 20);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    /* WIRETOSURFACE_1 claiming more than the blob holds */
    total = tb_build(blob, 2, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u32(blob + TB_STARTFRAME_BYTES + 4, 0x7FFFFFFF);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    /* WIRETOSURFACE_1 claiming less than its own fixed fields */
    total = tb_build(blob, 2, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u32(blob + TB_STARTFRAME_BYTES + 4, 8);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    /* ENDFRAME claiming a size it does not have */
    total = tb_build(blob, 2, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u32(blob + total - TB_ENDFRAME_BYTES + 4, 16);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    /* zero damage rects, zero copy rects, and counts that do not add up */
    total = tb_build(blob, 2, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u16(blob + TB_STARTFRAME_BYTES + 17, 0);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    total = tb_build(blob, 2, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u16(blob + TB_STARTFRAME_BYTES + 19 + 8, 0);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    total = tb_build(blob, 2, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u16(blob + TB_STARTFRAME_BYTES + 17, 4);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    total = tb_build(blob, 2, XR_RDPGFX_CODECID_AVC444, 1, 1);
    tb_u16(blob + TB_STARTFRAME_BYTES + 17, 16 * 1024 + 1);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
}
END_TEST

START_TEST(test_batch_peek_rejects_over_the_emit_envelope)
{
    /* The batch envelope must be a STRICT SUBSET of what the emit pass
     * accepts. process_enc_egfx rejects any command with
     * cmd_bytes > 32 * 1024 BEFORE the WIRETOSURFACE_1 handler runs, so a
     * blob larger than that must not be batched: the pair would be
     * encoded -- advancing the shared LTR frame_num and both long-term
     * slots -- for a picture the client never receives, and every later P
     * would reference a missing reference picture until the next
     * scheduled intra. Found by adversarial review of step 7; not
     * reachable from the shipped xorgxrdp (MAX_CAPTURE_RECTS 15), but
     * cmd_bytes arrives verbatim off the xup socket. */
    unsigned char *blob;
    int total;

    /* nd = 4092 makes w2s1_bytes = 33 + 8*4092 + 8 = 32777 > 32768,
     * while every field is otherwise perfectly formed */
    blob = (unsigned char *)malloc(64 * 1024);
    ck_assert_ptr_ne(blob, NULL);
    total = tb_build(blob, 3, XR_RDPGFX_CODECID_AVC444, 4092, 1);
    ck_assert_int_gt(total, 32 * 1024);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, total));
    /* and the same shape just inside the envelope is still accepted */
    total = tb_build(blob, 3, XR_RDPGFX_CODECID_AVC444, 15, 1);
    ck_assert_int_le(total, 32 * 1024);
    ck_assert_int_eq(3, gfx_egfx_batch_peek_mon((char *)blob, total));
    free(blob);
}
END_TEST

START_TEST(test_batch_peek_rejects_degenerate_input)
{
    unsigned char blob[512];

    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon(NULL, 64));
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon(NULL, 0));
    tb_build(blob, 0, XR_RDPGFX_CODECID_AVC444, 1, 1);
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, 0));
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, -1));
    ck_assert_int_eq(-1, gfx_egfx_batch_peek_mon((char *)blob, -1000000));
}
END_TEST

/******************************************************************************/
/* gfx_egfx_batch_group */

#define TB_MAX_ITEMS 20

struct tb_group_fixture
{
    XRDP_ENC_DATA enc[TB_MAX_ITEMS];
    unsigned char blob[TB_MAX_ITEMS][256];
    XRDP_ENC_DATA *in[TB_MAX_ITEMS];
    XRDP_ENC_DATA *set[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int set_mon[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int set_n;
};

static void
tb_fx_init(struct tb_group_fixture *fx)
{
    int index;

    memset(fx, 0, sizeof(*fx));
    for (index = 0; index < TB_MAX_ITEMS; index++)
    {
        fx->in[index] = &fx->enc[index];
    }
    fx->set_n = -1;
}

/* item i is a batchable AVC444 frame for monitor mon */
static void
tb_fx_avc444(struct tb_group_fixture *fx, int i, int mon)
{
    ENC_SET_BIT(fx->enc[i].flags, ENC_FLAGS_GFX_BIT);
    fx->enc[i].u.gfx.cmd = (char *)fx->blob[i];
    fx->enc[i].u.gfx.cmd_bytes =
        tb_build(fx->blob[i], mon, XR_RDPGFX_CODECID_AVC444, 1, 1);
}

/* item i is a GFX item that is NOT the batchable shape */
static void
tb_fx_other_gfx(struct tb_group_fixture *fx, int i, int cmd_id)
{
    ENC_SET_BIT(fx->enc[i].flags, ENC_FLAGS_GFX_BIT);
    fx->enc[i].u.gfx.cmd = (char *)fx->blob[i];
    fx->enc[i].u.gfx.cmd_bytes = tb_build_single_cmd(fx->blob[i], cmd_id, 24);
}

START_TEST(test_batch_group_single_item)
{
    struct tb_group_fixture fx;

    tb_fx_init(&fx);
    tb_fx_avc444(&fx, 0, 0);
    ck_assert_int_eq(1, gfx_egfx_batch_group(fx.in, 1, fx.set, fx.set_mon,
                     &fx.set_n));
    ck_assert_int_eq(1, fx.set_n);
    ck_assert_ptr_eq(&fx.enc[0], fx.set[0]);
    ck_assert_int_eq(0, fx.set_mon[0]);
}
END_TEST

START_TEST(test_batch_group_two_monitors_batch_together)
{
    struct tb_group_fixture fx;

    tb_fx_init(&fx);
    tb_fx_avc444(&fx, 0, 0);
    tb_fx_avc444(&fx, 1, 1);
    ck_assert_int_eq(2, gfx_egfx_batch_group(fx.in, 2, fx.set, fx.set_mon,
                     &fx.set_n));
    ck_assert_int_eq(2, fx.set_n);
    ck_assert_ptr_eq(&fx.enc[0], fx.set[0]);
    ck_assert_ptr_eq(&fx.enc[1], fx.set[1]);
    ck_assert_int_eq(0, fx.set_mon[0]);
    ck_assert_int_eq(1, fx.set_mon[1]);
}
END_TEST

START_TEST(test_batch_group_same_monitor_ends_the_batch)
{
    struct tb_group_fixture fx;
    int consumed;

    /* m1C1, m1C2: the second frame of monitor 1 belongs to the NEXT
     * cycle -- batching it would reorder that monitor's own frames */
    tb_fx_init(&fx);
    tb_fx_avc444(&fx, 0, 1);
    tb_fx_avc444(&fx, 1, 1);
    consumed = gfx_egfx_batch_group(fx.in, 2, fx.set, fx.set_mon, &fx.set_n);
    ck_assert_int_eq(1, consumed);
    ck_assert_int_eq(1, fx.set_n);
    ck_assert_int_eq(1, fx.set_mon[0]);
    /* leftover: exactly one item, the second frame of monitor 1 */
    ck_assert_int_eq(1, 2 - consumed);

    /* m0C1, m1C1, m0C2, m1C2: the batch is the first two, two left over */
    tb_fx_init(&fx);
    tb_fx_avc444(&fx, 0, 0);
    tb_fx_avc444(&fx, 1, 1);
    tb_fx_avc444(&fx, 2, 0);
    tb_fx_avc444(&fx, 3, 1);
    consumed = gfx_egfx_batch_group(fx.in, 4, fx.set, fx.set_mon, &fx.set_n);
    ck_assert_int_eq(2, consumed);
    ck_assert_int_eq(2, fx.set_n);
    ck_assert_int_eq(0, fx.set_mon[0]);
    ck_assert_int_eq(1, fx.set_mon[1]);
    ck_assert_int_eq(2, 4 - consumed);
}
END_TEST

START_TEST(test_batch_group_non_batchable_first_is_a_set_of_one)
{
    struct tb_group_fixture fx;

    tb_fx_init(&fx);
    tb_fx_other_gfx(&fx, 0, XR_RDPGFX_CMDID_CREATESURFACE);
    tb_fx_avc444(&fx, 1, 0);
    tb_fx_avc444(&fx, 2, 1);
    ck_assert_int_eq(1, gfx_egfx_batch_group(fx.in, 3, fx.set, fx.set_mon,
                     &fx.set_n));
    ck_assert_int_eq(1, fx.set_n);
    ck_assert_ptr_eq(&fx.enc[0], fx.set[0]);
    /* -1 means "handled exactly as before this step" */
    ck_assert_int_eq(-1, fx.set_mon[0]);

    /* a NON-GFX item must not have its u.gfx read at all */
    tb_fx_init(&fx);
    fx.enc[0].u.sc.num_drects = 3;
    ck_assert_int_eq(1, gfx_egfx_batch_group(fx.in, 2, fx.set, fx.set_mon,
                     &fx.set_n));
    ck_assert_int_eq(1, fx.set_n);
    ck_assert_int_eq(-1, fx.set_mon[0]);
}
END_TEST

START_TEST(test_batch_group_non_batchable_second_ends_the_batch)
{
    struct tb_group_fixture fx;
    int consumed;

    tb_fx_init(&fx);
    tb_fx_avc444(&fx, 0, 0);
    tb_fx_other_gfx(&fx, 1, XR_RDPGFX_CMDID_DELETESURFACE);
    tb_fx_avc444(&fx, 2, 1);
    consumed = gfx_egfx_batch_group(fx.in, 3, fx.set, fx.set_mon, &fx.set_n);
    ck_assert_int_eq(1, consumed);
    ck_assert_int_eq(1, fx.set_n);
    ck_assert_int_eq(0, fx.set_mon[0]);
    ck_assert_int_eq(2, 3 - consumed);

    tb_fx_init(&fx);
    tb_fx_avc444(&fx, 0, 0);
    tb_fx_avc444(&fx, 1, 1);
    tb_fx_other_gfx(&fx, 2, XR_RDPGFX_CMDID_SOLIDFILL);
    consumed = gfx_egfx_batch_group(fx.in, 3, fx.set, fx.set_mon, &fx.set_n);
    ck_assert_int_eq(2, consumed);
    ck_assert_int_eq(2, fx.set_n);
    ck_assert_int_eq(1, 3 - consumed);
}
END_TEST

START_TEST(test_batch_group_sixteen_monitors)
{
    struct tb_group_fixture fx;
    int index;

    tb_fx_init(&fx);
    for (index = 0; index < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; index++)
    {
        tb_fx_avc444(&fx, index, index);
    }
    /* a 17th item can only repeat a monitor, so it ends the batch */
    tb_fx_avc444(&fx, CLIENT_MONITOR_DATA_MAXIMUM_MONITORS, 0);
    ck_assert_int_eq(CLIENT_MONITOR_DATA_MAXIMUM_MONITORS,
                     gfx_egfx_batch_group(fx.in,
                                          CLIENT_MONITOR_DATA_MAXIMUM_MONITORS
                                          + 1, fx.set, fx.set_mon,
                                          &fx.set_n));
    ck_assert_int_eq(CLIENT_MONITOR_DATA_MAXIMUM_MONITORS, fx.set_n);
    for (index = 0; index < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; index++)
    {
        ck_assert_int_eq(index, fx.set_mon[index]);
        ck_assert_ptr_eq(&fx.enc[index], fx.set[index]);
    }
}
END_TEST

START_TEST(test_batch_group_degenerate_input)
{
    struct tb_group_fixture fx;

    tb_fx_init(&fx);
    tb_fx_avc444(&fx, 0, 0);
    ck_assert_int_eq(0, gfx_egfx_batch_group(fx.in, 0, fx.set, fx.set_mon,
                     &fx.set_n));
    ck_assert_int_eq(0, fx.set_n);
    ck_assert_int_eq(0, gfx_egfx_batch_group(NULL, 1, fx.set, fx.set_mon,
                     &fx.set_n));
    ck_assert_int_eq(0, fx.set_n);
    ck_assert_int_eq(0, gfx_egfx_batch_group(fx.in, 1, NULL, fx.set_mon,
                     &fx.set_n));
    ck_assert_int_eq(0, gfx_egfx_batch_group(fx.in, 1, fx.set, NULL,
                     &fx.set_n));
    ck_assert_int_eq(0, gfx_egfx_batch_group(fx.in, 1, fx.set, fx.set_mon,
                     NULL));
    fx.in[0] = NULL;
    ck_assert_int_eq(0, gfx_egfx_batch_group(fx.in, 1, fx.set, fx.set_mon,
                     &fx.set_n));
    ck_assert_int_eq(0, fx.set_n);
}
END_TEST

/******************************************************************************/
Suite *
make_suite_egfx_base_functions(void)
{
    Suite *s;
    TCase *tc_process_monitors;
    TCase *tc_batch;

    s = suite_create("test_xrdp_egfx_base_functions");

    tc_process_monitors = tcase_create("xrdp_egfx_base_functions");
    tcase_add_test(tc_process_monitors,
                   test_xrdp_egfx_send_create_surface__happy_path);

    suite_add_tcase(s, tc_process_monitors);

    tc_batch = tcase_create("xrdp_egfx_multimon_batch");
    tcase_add_test(tc_batch, test_batch_peek_accepts_the_exact_avc444_shape);
    tcase_add_test(tc_batch, test_batch_peek_rejects_bad_command_ids);
    tcase_add_test(tc_batch, test_batch_peek_rejects_other_egfx_commands);
    tcase_add_test(tc_batch, test_batch_peek_rejects_wrong_codec_ids);
    tcase_add_test(tc_batch, test_batch_peek_rejects_every_truncation);
    tcase_add_test(tc_batch, test_batch_peek_rejects_lying_lengths);
    tcase_add_test(tc_batch,
                   test_batch_peek_rejects_over_the_emit_envelope);
    tcase_add_test(tc_batch, test_batch_peek_rejects_degenerate_input);
    tcase_add_test(tc_batch, test_batch_group_single_item);
    tcase_add_test(tc_batch, test_batch_group_two_monitors_batch_together);
    tcase_add_test(tc_batch, test_batch_group_same_monitor_ends_the_batch);
    tcase_add_test(tc_batch,
                   test_batch_group_non_batchable_first_is_a_set_of_one);
    tcase_add_test(tc_batch,
                   test_batch_group_non_batchable_second_ends_the_batch);
    tcase_add_test(tc_batch, test_batch_group_sixteen_monitors);
    tcase_add_test(tc_batch, test_batch_group_degenerate_input);
    suite_add_tcase(s, tc_batch);

    return s;
}

