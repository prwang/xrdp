/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Laxmikant Rashinkar 2004-2014
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
 * Encoder
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <limits.h>
#include <time.h>

#include "xrdp_encoder.h"
#include "xup_client_info.h"
#include "xrdp_encoder_ffmpeg.h"
#include "xrdp.h"
#include "ms-rdpbcgr.h"
#include "thread_calls.h"
#include "fifo.h"
#include "xrdp_egfx.h"
#include "string_calls.h"
#include "perf_trace.h"

#ifdef XRDP_RFXCODEC
#include "rfxcodec_encode.h"
#endif

#ifdef XRDP_X264
#include "xrdp_encoder_x264.h"
#endif

#ifdef XRDP_OPENH264
#include "xrdp_encoder_openh264.h"
#endif

#define DEFAULT_XRDP_GFX_FRAMES_IN_FLIGHT 2
/* limits used for validate env var XRDP_GFX_FRAMES_IN_FLIGHT */
#define MIN_XRDP_GFX_FRAMES_IN_FLIGHT 1
#define MAX_XRDP_GFX_FRAMES_IN_FLIGHT 16

#define DEFAULT_XRDP_GFX_MAX_COMPRESSED_BYTES (3 * 1024 * 1024)
/* limits used for validate env var XRDP_GFX_MAX_COMPRESSED_BYTES */
#define MIN_XRDP_GFX_MAX_COMPRESSED_BYTES (64 * 1024)
#define MAX_XRDP_GFX_MAX_COMPRESSED_BYTES (256 * 1024 * 1024)

#define XRDP_SURCMD_PREFIX_BYTES 256
#define OUT_DATA_BYTES_DEFAULT_SIZE (16 * 1024 * 1024)

#ifdef XRDP_RFXCODEC
/*
 * LH3 LL3, HH3 HL3, HL2 LH2, LH1 HH2, HH1 HL1
 * https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdprfx/3e9c8af4-7539-4c9d-95de-14b1558b902c
 */

/* standard quality */
static const unsigned char g_rfx_quantization_values_std[] =
{
    0x66, 0x66, 0x77, 0x87, 0x98,
    0x76, 0x77, 0x88, 0x98, 0x99
};

/* low quality */
static const unsigned char g_rfx_quantization_values_lq[] =
{
    0x66, 0x66, 0x77, 0x87, 0x98,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA /* TODO: tentative value */
};

/* ultra low quality */
static const unsigned char g_rfx_quantization_values_ulq[] =
{
    0x66, 0x66, 0x77, 0x87, 0x98,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB /* TODO: tentative value */
};
#endif

struct enc_rect
{
    short x1;
    short y1;
    short x2;
    short y2;
};

/*****************************************************************************/
static int
process_enc_jpg(struct xrdp_encoder *self, XRDP_ENC_DATA *enc);
#ifdef XRDP_RFXCODEC
static int
process_enc_rfx(struct xrdp_encoder *self, XRDP_ENC_DATA *enc);
#endif
#if defined(XRDP_X264) || defined(XRDP_OPENH264)
static int
process_enc_h264(struct xrdp_encoder *self, XRDP_ENC_DATA *enc);
#endif
static int
process_enc_egfx(struct xrdp_encoder *self, XRDP_ENC_DATA *enc);

/*****************************************************************************/
/* Item destructor for self->fifo_to_proc */
static void
xrdp_enc_data_destructor(void *item, void *closure)
{
    XRDP_ENC_DATA *enc = (XRDP_ENC_DATA *)item;
    if (ENC_IS_BIT_SET(enc->flags, ENC_FLAGS_GFX_BIT))
    {
        g_free(enc->u.gfx.cmd);
    }
    else
    {
        g_free(enc->u.sc.drects);
        g_free(enc->u.sc.crects);
    }
    g_free(enc);
}

/* Item destructor for self->fifo_processed */
static void
xrdp_enc_data_done_destructor(void *item, void *closure)
{
    XRDP_ENC_DATA_DONE *enc_done = (XRDP_ENC_DATA_DONE *)item;
    g_free(enc_done->comp_pad_data);
    g_free(enc_done);
}

/*****************************************************************************/
/**
 * Sets the methods used by the software H.264 module
 */
static void
set_h264_encoder_methods(struct xrdp_encoder *self)
{
    const char *encoder_name = NULL;
#if defined(XRDP_X264) && defined(XRDP_OPENH264)
    struct xrdp_tconfig_gfx gfxconfig;
    tconfig_load_gfx(GFX_CONF, &gfxconfig);

    switch (gfxconfig.h264_encoder)
    {
        case XTC_H264_OPENH264:
            encoder_name = "OpenH264";
            self->xrdp_encoder_h264_create = xrdp_encoder_openh264_create;
            self->xrdp_encoder_h264_delete = xrdp_encoder_openh264_delete;
            self->xrdp_encoder_h264_encode = xrdp_encoder_openh264_encode;
            break;
        case XTC_H264_X264:
        default:
            /* x264 is the default H.264 software encoder */
            encoder_name = "x264";
            self->xrdp_encoder_h264_create = xrdp_encoder_x264_create;
            self->xrdp_encoder_h264_delete = xrdp_encoder_x264_delete;
            self->xrdp_encoder_h264_encode = xrdp_encoder_x264_encode;
            break;
    }
#elif defined(XRDP_OPENH264)
    encoder_name = "OpenH264";
    self->xrdp_encoder_h264_create = xrdp_encoder_openh264_create;
    self->xrdp_encoder_h264_delete = xrdp_encoder_openh264_delete;
    self->xrdp_encoder_h264_encode = xrdp_encoder_openh264_encode;
#elif defined(XRDP_X264)
    encoder_name = "x264";
    self->xrdp_encoder_h264_create = xrdp_encoder_x264_create;
    self->xrdp_encoder_h264_delete = xrdp_encoder_x264_delete;
    self->xrdp_encoder_h264_encode = xrdp_encoder_x264_encode;
#endif

    // Don't log the library we're going to use if we
    // couldn't load it.
    if (encoder_name != NULL && self->mm->libh264_loaded)
    {
        LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: using %s for "
            "software encoder", encoder_name);
    }
}

/*****************************************************************************/
struct xrdp_encoder *
xrdp_encoder_create(struct xrdp_mm *mm)
{
    LOG_DEVEL(LOG_LEVEL_TRACE, "xrdp_encoder_create:");

    struct xrdp_encoder *self;
    struct xrdp_client_info *client_info;
    char buf[1024];
    int pid;

    client_info = mm->wm->client_info;

    /* RemoteFX 7.1 requires LAN but GFX does not */
    if (client_info->mcs_connection_type != CONNECTION_TYPE_LAN)
    {
        if ((mm->egfx_flags & (XRDP_EGFX_H264 | XRDP_EGFX_RFX_PRO)) == 0)
        {
            return 0;
        }
    }
    if (client_info->bpp < 24)
    {
        return 0;
    }

    self = g_new0(struct xrdp_encoder, 1);
    if (self == NULL)
    {
        return NULL;
    }
    self->mm = mm;
    self->process_enc = process_enc_egfx;
    if (client_info->jpeg_codec_id != 0)
    {
        LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: starting jpeg codec session");
        self->codec_id = client_info->jpeg_codec_id;
        self->in_codec_mode = 1;
        self->codec_quality = client_info->jpeg_prop[0];
        client_info->capture_code = CC_SIMPLE;
        client_info->capture_format = XRDP_a8b8g8r8;
        self->process_enc = process_enc_jpg;
    }
    else if (mm->avc444_ffmpeg)
    {
        /* external stock-ffmpeg AVC444 backend: full-chroma XRGB capture,
         * no linked H.264 library (PRD FR-CAP-0) */
        LOG(LOG_LEVEL_INFO,
            "xrdp_encoder_create: starting ffmpeg AVC444 gfx session");
        self->in_codec_mode = 1;
        client_info->capture_code = CC_GFX_AVC444;
        /* the capture packs the FINAL wire views (FR-CAPTURE-6); the
         * aux-view variant rides capture_format */
        client_info->capture_format = mm->avc444_v2
                                      ? XRDP_yuv444_v2_stream_709fr
                                      : XRDP_yuv444_v1_stream_709fr;
        client_info->avc444_chroma_align =
            mm->wm->gfx_config->avc444_ffmpeg_chroma_align;
        self->gfx = 1;
        self->avc444_ffmpeg = 1;
        self->avc444_v2 = mm->avc444_v2;
        self->avc444_dump_extra = mm->avc444_dump_extra;
        self->avc444_strip_sei = mm->avc444_strip_sei;
        self->avc444_sanitize_hrd = mm->avc444_sanitize_hrd;
        self->avc444_strip_pic_struct = mm->avc444_strip_pic_struct;
        self->avc444_fault_aux_delay = mm->avc444_fault_aux_delay;
        self->avc444_fault_strip_mmco = mm->avc444_fault_strip_mmco;
        self->avc444_aux_ltr_chain = mm->avc444_aux_ltr_chain;
        self->eager_slot_ack = mm->avc444_eager_slot_ack;
        self->avc444_ltr_rekey_frame_num = mm->avc444_ltr_rekey_frame_num;
        self->avc444_intra_refresh_frames =
            mm->avc444_intra_refresh_frames;
        /* cache the EGFX surface origins the re-key reset re-maps with;
         * mirrors xrdp_mm_egfx_create_surfaces (BACKLOG #48) */
        {
            int mi_index;
            int mi_count = mm->wm->client_info->display_sizes.monitorCount;
            for (mi_index = 0; mi_index < 16; mi_index++)
            {
                self->avc444_surface_x[mi_index] = 0;
                self->avc444_surface_y[mi_index] = 0;
                /* -1 = no re-key yet, the id in the command is live */
                self->avc444_surface_id_live[mi_index] = -1;
            }
            if (mi_count > 16)
            {
                mi_count = 16;
            }
            for (mi_index = 0; mi_index < mi_count; mi_index++)
            {
                const struct monitor_info *mi =
                        mm->wm->client_info->display_sizes.minfo_wm + mi_index;
                self->avc444_surface_x[mi_index] = mi->left;
                self->avc444_surface_y[mi_index] = mi->top;
            }
        }
        LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: AVC444 %s",
            self->avc444_v2 ? "v2 (ChromaV2, 0x000F)" : "v1 (0x000E)");
        g_strncpy(self->avc444_path, mm->wm->gfx_config->avc444_ffmpeg_path,
                  sizeof(self->avc444_path) - 1);
        self->avc444_encoder_args =
            mm->wm->gfx_config->avc444_ffmpeg_encoder_args;
        self->avc444_chroma_align =
            mm->wm->gfx_config->avc444_ffmpeg_chroma_align;
        if (mm->wm->gfx_config->avc444_ffmpeg_tail_flush)
        {
            LOG(LOG_LEVEL_WARNING, "xrdp_encoder_create: gfx.toml "
                "tail_flush is inert under the splicable-capture "
                "contract (FR-PROC-6) and is ignored");
        }
    }
    else if (mm->avc420_ffmpeg)
    {
        /* external stock-ffmpeg backend, plain AVC420 (single YUV420 view,
         * codec id 0x000B). Same full-chroma XRGB capture and converter as
         * AVC444 — only the main view is produced and encoded. */
        LOG(LOG_LEVEL_INFO,
            "xrdp_encoder_create: starting ffmpeg AVC420 gfx session");
        self->in_codec_mode = 1;
        client_info->capture_code = CC_GFX_AVC444;
        /* main view only at the coded geometry (FR-CAPTURE-6) */
        client_info->capture_format = XRDP_nv12_709fr;
        client_info->avc444_chroma_align =
            mm->wm->gfx_config->avc444_ffmpeg_chroma_align;
        self->gfx = 1;
        self->avc420_ffmpeg = 1;
        self->avc444_dump_extra = mm->avc444_dump_extra;
        self->avc444_strip_sei = mm->avc444_strip_sei;
        self->avc444_sanitize_hrd = mm->avc444_sanitize_hrd;
        self->avc444_strip_pic_struct = mm->avc444_strip_pic_struct;
        self->avc444_fault_aux_delay = mm->avc444_fault_aux_delay;
        self->avc444_fault_strip_mmco = mm->avc444_fault_strip_mmco;
        g_strncpy(self->avc444_path, mm->wm->gfx_config->avc444_ffmpeg_path,
                  sizeof(self->avc444_path) - 1);
        self->avc444_encoder_args =
            mm->wm->gfx_config->avc444_ffmpeg_encoder_args;
        self->avc444_chroma_align =
            mm->wm->gfx_config->avc444_ffmpeg_chroma_align;
        if (mm->wm->gfx_config->avc444_ffmpeg_tail_flush)
        {
            LOG(LOG_LEVEL_WARNING, "xrdp_encoder_create: gfx.toml "
                "tail_flush is inert under the splicable-capture "
                "contract (FR-PROC-6) and is ignored");
        }
    }
#if defined(XRDP_X264) || defined(XRDP_OPENH264)
    else if (mm->libh264_loaded && (mm->egfx_flags & XRDP_EGFX_H264) != 0)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp_encoder_create: starting h264 codec session gfx");
        self->in_codec_mode = 1;
        client_info->capture_code = CC_GFX_A2;
        client_info->capture_format = XRDP_nv12_709fr;
        self->gfx = 1;
    }
    else if (mm->libh264_loaded && client_info->h264_codec_id != 0)
    {
        LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: starting h264 codec session");
        self->codec_id = client_info->h264_codec_id;
        self->in_codec_mode = 1;
        client_info->capture_code = CC_SUF_A2;
        client_info->capture_format = XRDP_nv12;
        self->process_enc = process_enc_h264;
    }
#endif
#ifdef XRDP_RFXCODEC
    else if (mm->egfx_flags & XRDP_EGFX_RFX_PRO)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp_encoder_create: starting gfx rfx pro codec session");
        self->in_codec_mode = 1;
        client_info->capture_code = CC_GFX_PRO;
        self->gfx = 1;
        self->num_quants = 2;
        self->quant_idx_y = 0;
        self->quant_idx_u = 1;
        self->quant_idx_v = 1;

        switch (client_info->mcs_connection_type)
        {
            case CONNECTION_TYPE_MODEM:
            case CONNECTION_TYPE_BROADBAND_LOW:
            case CONNECTION_TYPE_SATELLITE:
                self->quants = (const char *) g_rfx_quantization_values_ulq;
                break;
            case CONNECTION_TYPE_BROADBAND_HIGH:
            case CONNECTION_TYPE_WAN:
                self->quants = (const char *) g_rfx_quantization_values_lq;
                break;
            case CONNECTION_TYPE_LAN:
            case CONNECTION_TYPE_AUTODETECT: /* not implemented yet */
            default:
                self->quants = (const char *) g_rfx_quantization_values_std;

        }
    }
    else if (client_info->rfx_codec_id != 0)
    {
        LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: starting rfx codec session");
        self->codec_id = client_info->rfx_codec_id;
        self->in_codec_mode = 1;
        client_info->capture_code = CC_SUF_RFX;
        self->process_enc = process_enc_rfx;
        self->codec_handle_rfx = rfxcodec_encode_create(mm->wm->screen->width,
                                 mm->wm->screen->height,
                                 RFX_FORMAT_YUV, 0);
    }
#endif
    else
    {
        g_free(self);
        return 0;
    }

    LOG_DEVEL(LOG_LEVEL_INFO,
              "init_xrdp_encoder: initializing encoder codec_id %d",
              self->codec_id);

    /* setup required FIFOs */
    self->fifo_to_proc = fifo_create(xrdp_enc_data_destructor);
    self->fifo_processed = fifo_create(xrdp_enc_data_done_destructor);
    self->mutex = tc_mutex_create();

    pid = g_getpid();
    /* setup wait objects for signalling */
    g_snprintf(buf, 1024, "xrdp_%8.8x_encoder_event_to_proc", pid);
    self->xrdp_encoder_event_to_proc = g_create_wait_obj(buf);
    g_snprintf(buf, 1024, "xrdp_%8.8x_encoder_event_processed", pid);
    self->xrdp_encoder_event_processed = g_create_wait_obj(buf);
    g_snprintf(buf, 1024, "xrdp_%8.8x_encoder_term", pid);
    self->xrdp_encoder_term_request = g_create_wait_obj(buf);
    self->xrdp_encoder_term_done = g_create_wait_obj(buf);
    if (client_info->gfx)
    {
        const char *env_var = g_getenv("XRDP_GFX_FRAMES_IN_FLIGHT");
        self->frames_in_flight = DEFAULT_XRDP_GFX_FRAMES_IN_FLIGHT;
        if (env_var != NULL)
        {
            int fif = g_atoix(env_var);
            if (fif >= MIN_XRDP_GFX_FRAMES_IN_FLIGHT &&
                    fif <= MAX_XRDP_GFX_FRAMES_IN_FLIGHT)
            {
                self->frames_in_flight = fif;
                LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: "
                    "XRDP_GFX_FRAMES_IN_FLIGHT set to %d", fif);
            }
            else
            {
                LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: "
                    "XRDP_GFX_FRAMES_IN_FLIGHT set but invalid %s",
                    env_var);
            }
        }
        env_var = g_getenv("XRDP_GFX_MAX_COMPRESSED_BYTES");
        self->max_compressed_bytes = DEFAULT_XRDP_GFX_MAX_COMPRESSED_BYTES;
        if (env_var != NULL)
        {
            int mcb = g_atoix(env_var);
            if (mcb >= MIN_XRDP_GFX_MAX_COMPRESSED_BYTES &&
                    mcb <= MAX_XRDP_GFX_MAX_COMPRESSED_BYTES)
            {
                self->max_compressed_bytes = mcb;
                LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: "
                    "XRDP_GFX_MAX_COMPRESSED_BYTES set to %d", mcb);
            }
            else
            {
                LOG(LOG_LEVEL_INFO, "xrdp_encoder_create: "
                    "XRDP_GFX_MAX_COMPRESSED_BYTES set but invalid %s",
                    env_var);
            }
        }
        LOG_DEVEL(LOG_LEVEL_INFO, "Using %d max_compressed_bytes for encoder",
                  self->max_compressed_bytes);
    }
    else
    {
        self->frames_in_flight = client_info->max_unacknowledged_frame_count;
        self->max_compressed_bytes = client_info->max_fastpath_frag_bytes & ~15;
    }
    /* make sure frames_in_flight is at least 1 */
    self->frames_in_flight = MAX(self->frames_in_flight, 1);

    set_h264_encoder_methods(self);

    /* create thread to process messages */
    tc_thread_create(proc_enc_msg, self);

    return self;
}

/*****************************************************************************/
void
xrdp_encoder_delete(struct xrdp_encoder *self)
{
    /* used by the always-compiled ffmpeg AVC444 reap loop (and the codec loops
     * when those are enabled) */
    int index;


    LOG_DEVEL(LOG_LEVEL_INFO, "xrdp_encoder_delete:");
    if (self == 0)
    {
        return;
    }
    if (self->in_codec_mode == 0)
    {
        return;
    }
    /* tell worker thread to shut down */
    g_set_wait_obj(self->xrdp_encoder_term_request);
    (void)g_obj_wait(&self->xrdp_encoder_term_done, 1, NULL, 0, 5000);
    if (!g_is_wait_obj_set(self->xrdp_encoder_term_done))
    {
        LOG(LOG_LEVEL_WARNING, "Encoder failed to shut down cleanly");
    }

#ifdef XRDP_RFXCODEC
    for (index = 0; index < 16; index++)
    {
        if (self->codec_handle_prfx_gfx[index] != NULL)
        {
            rfxcodec_encode_destroy(self->codec_handle_prfx_gfx[index]);
        }
    }
    if (self->codec_handle_rfx != NULL)
    {
        rfxcodec_encode_destroy(self->codec_handle_rfx);
    }
#endif

#if defined(XRDP_X264) || defined(XRDP_OPENH264)
    for (index = 0; index < 16; index++)
    {
        if (self->codec_handle_h264_gfx[index] != NULL)
        {
            self->xrdp_encoder_h264_delete(self->codec_handle_h264_gfx[index]);
        }
    }
    if (self->codec_handle_h264 != NULL)
    {
        self->xrdp_encoder_h264_delete(self->codec_handle_h264);
    }
#endif

    /* external ffmpeg AVC444 backend: reap children and free converters */
    for (index = 0; index < 16; index++)
    {
        if (self->avc444_ffmpeg_handle[index] != NULL)
        {
            xrdp_ffmpeg_avc444_delete((struct xrdp_ffmpeg_avc444 *)
                                      self->avc444_ffmpeg_handle[index]);
            self->avc444_ffmpeg_handle[index] = NULL;
        }
    }

    /* destroy wait objects used for signalling */
    g_delete_wait_obj(self->xrdp_encoder_event_to_proc);
    g_delete_wait_obj(self->xrdp_encoder_event_processed);
    g_delete_wait_obj(self->xrdp_encoder_term_request);
    g_delete_wait_obj(self->xrdp_encoder_term_done);

    /* cleanup fifos */
    fifo_delete(self->fifo_to_proc, NULL);
    fifo_delete(self->fifo_processed, NULL);
    tc_mutex_delete(self->mutex);
    g_free(self);
}

/*****************************************************************************/
/* called from encoder thread */
static int
process_enc_jpg(struct xrdp_encoder *self, XRDP_ENC_DATA *enc)
{
    int index;
    int x;
    int y;
    int cx;
    int cy;
    int quality;
    int error;
    int out_data_bytes;
    int count;
    char *out_data;
    XRDP_ENC_DATA_DONE *enc_done;
    struct fifo *fifo_processed;
    tbus mutex;
    tbus event_processed;

    LOG_DEVEL(LOG_LEVEL_DEBUG, "process_enc_jpg:");
    quality = self->codec_quality;
    fifo_processed = self->fifo_processed;
    mutex = self->mutex;
    event_processed = self->xrdp_encoder_event_processed;
    count = enc->u.sc.num_crects;
    for (index = 0; index < count; index++)
    {
        x = enc->u.sc.crects[index * 4 + 0];
        y = enc->u.sc.crects[index * 4 + 1];
        cx = enc->u.sc.crects[index * 4 + 2];
        cy = enc->u.sc.crects[index * 4 + 3];
        if (cx < 1 || cy < 1)
        {
            LOG_DEVEL(LOG_LEVEL_WARNING, "process_enc_jpg: error 1");
            continue;
        }

        LOG_DEVEL(LOG_LEVEL_DEBUG, "process_enc_jpg: x %d y %d cx %d cy %d",
                  x, y, cx, cy);

        out_data_bytes = MAX((cx + 4) * cy * 4, 8192);
        if ((out_data_bytes < 1)
                || (out_data_bytes > OUT_DATA_BYTES_DEFAULT_SIZE))
        {
            LOG_DEVEL(LOG_LEVEL_ERROR, "process_enc_jpg: error 2");
            return 1;
        }
        out_data = (char *) g_malloc(out_data_bytes
                                     + XRDP_SURCMD_PREFIX_BYTES + 2, 0);
        if (out_data == 0)
        {
            LOG_DEVEL(LOG_LEVEL_ERROR, "process_enc_jpg: error 3");
            return 1;
        }

        out_data[256] = 0; /* header bytes */
        out_data[257] = 0;
        error = libxrdp_codec_jpeg_compress(self->mm->wm->session, 0, enc->u.sc.data,
                                            enc->u.sc.width, enc->u.sc.height,
                                            enc->u.sc.width * 4, x, y, cx, cy,
                                            quality,
                                            out_data
                                            + XRDP_SURCMD_PREFIX_BYTES + 2,
                                            &out_data_bytes);
        if (error < 0)
        {
            LOG_DEVEL(LOG_LEVEL_ERROR, "process_enc_jpg: jpeg error %d "
                      "bytes %d", error, out_data_bytes);
            g_free(out_data);
            return 1;
        }
        LOG_DEVEL(LOG_LEVEL_WARNING,
                  "jpeg error %d bytes %d", error, out_data_bytes);
        enc_done = (XRDP_ENC_DATA_DONE *)
                   g_malloc(sizeof(XRDP_ENC_DATA_DONE), 1);
        enc_done->comp_bytes = out_data_bytes + 2;
        enc_done->pad_bytes = 256;
        enc_done->comp_pad_data = out_data;
        enc_done->enc = enc;
        enc_done->last = index == (enc->u.sc.num_crects - 1);
        enc_done->x = x;
        enc_done->y = y;
        enc_done->cx = cx;
        enc_done->cy = cy;
        enc_done->frame_id = enc->u.sc.frame_id;
        /* done with msg */
        /* inform main thread done */
        tc_mutex_lock(mutex);
        fifo_add_item(fifo_processed, enc_done);
        tc_mutex_unlock(mutex);
        /* signal completion for main thread */
        g_set_wait_obj(event_processed);
    }
    return 0;
}

#ifdef XRDP_RFXCODEC
/*****************************************************************************/
/* called from encoder thread */
static int
process_enc_rfx(struct xrdp_encoder *self, XRDP_ENC_DATA *enc)
{
    int index;
    int x;
    int y;
    int cx;
    int cy;
    int out_data_bytes;
    int count;
    int tiles_written;
    int all_tiles_written;
    int tiles_left;
    int finished;
    char *out_data;
    XRDP_ENC_DATA_DONE *enc_done;
    struct fifo *fifo_processed;
    tbus mutex;
    tbus event_processed;
    struct rfx_tile *tiles;
    struct rfx_rect *rfxrects;
    int alloc_bytes;
    int encode_flags;
    int encode_passes;

    LOG_DEVEL(LOG_LEVEL_DEBUG, "process_enc_rfx:");
    LOG_DEVEL(LOG_LEVEL_DEBUG, "process_enc_rfx: num_crects %d num_drects %d",
              enc->u.sc.num_crects, enc->u.sc.num_drects);
    fifo_processed = self->fifo_processed;
    mutex = self->mutex;
    event_processed = self->xrdp_encoder_event_processed;

    all_tiles_written = 0;
    encode_passes = 0;
    do
    {
        tiles_written = 0;
        tiles_left = enc->u.sc.num_crects - all_tiles_written;
        out_data = NULL;
        out_data_bytes = 0;

        if ((tiles_left > 0) && (enc->u.sc.num_drects > 0))
        {
            alloc_bytes = XRDP_SURCMD_PREFIX_BYTES;
            alloc_bytes += self->max_compressed_bytes;
            alloc_bytes += sizeof(struct rfx_tile) * tiles_left +
                           sizeof(struct rfx_rect) * enc->u.sc.num_drects;
            out_data = g_new(char, alloc_bytes);
            if (out_data != NULL)
            {
                tiles = (struct rfx_tile *)
                        (out_data + XRDP_SURCMD_PREFIX_BYTES +
                         self->max_compressed_bytes);
                rfxrects = (struct rfx_rect *) (tiles + tiles_left);

                count = tiles_left;
                for (index = 0; index < count; index++)
                {
                    x = enc->u.sc.crects[(index + all_tiles_written) * 4 + 0];
                    y = enc->u.sc.crects[(index + all_tiles_written) * 4 + 1];
                    cx = enc->u.sc.crects[(index + all_tiles_written) * 4 + 2];
                    cy = enc->u.sc.crects[(index + all_tiles_written) * 4 + 3];
                    tiles[index].x = x;
                    tiles[index].y = y;
                    tiles[index].cx = cx;
                    tiles[index].cy = cy;
                    tiles[index].quant_y = self->quant_idx_y;
                    tiles[index].quant_cb = self->quant_idx_u;
                    tiles[index].quant_cr = self->quant_idx_v;
                }

                count = enc->u.sc.num_drects;
                for (index = 0; index < count; index++)
                {
                    x = enc->u.sc.drects[index * 4 + 0];
                    y = enc->u.sc.drects[index * 4 + 1];
                    cx = enc->u.sc.drects[index * 4 + 2];
                    cy = enc->u.sc.drects[index * 4 + 3];
                    rfxrects[index].x = x;
                    rfxrects[index].y = y;
                    rfxrects[index].cx = cx;
                    rfxrects[index].cy = cy;
                }

                out_data_bytes = self->max_compressed_bytes;

                encode_flags = 0;
                if (((int)enc->flags & KEY_FRAME_REQUESTED) && encode_passes == 0)
                {
                    encode_flags = RFX_FLAGS_PRO_KEY;
                }
                tiles_written = rfxcodec_encode_ex(self->codec_handle_rfx,
                                                   out_data + XRDP_SURCMD_PREFIX_BYTES,
                                                   &out_data_bytes, enc->u.sc.data,
                                                   enc->u.sc.width, enc->u.sc.height,
                                                   ((enc->u.sc.width + 63) & ~63) * 4,
                                                   rfxrects, enc->u.sc.num_drects,
                                                   tiles, enc->u.sc.num_crects,
                                                   self->quants, self->num_quants,
                                                   encode_flags);
            }
            ++encode_passes;
        }

        LOG_DEVEL(LOG_LEVEL_DEBUG,
                  "process_enc_rfx: rfxcodec_encode tiles_written %d",
                  tiles_written);
        /* only if enc_done->comp_bytes is not zero is something sent
           to the client but you must always send something back even
           on error so Xorg can get ack */
        enc_done = g_new0(XRDP_ENC_DATA_DONE, 1);
        if (enc_done == NULL)
        {
            return 1;
        }
        enc_done->comp_bytes = tiles_written > 0 ? out_data_bytes : 0;
        enc_done->pad_bytes = XRDP_SURCMD_PREFIX_BYTES;
        enc_done->comp_pad_data = out_data;
        enc_done->enc = enc;
        enc_done->x = enc->u.sc.left;
        enc_done->y = enc->u.sc.top;
        enc_done->cx = enc->u.sc.width;
        enc_done->cy = enc->u.sc.height;
        enc_done->frame_id = enc->u.sc.frame_id;
        enc_done->continuation = all_tiles_written > 0;
        if (tiles_written > 0)
        {
            all_tiles_written += tiles_written;
        }
        finished =
            (all_tiles_written == enc->u.sc.num_crects) || (tiles_written < 0);
        enc_done->last = finished;

        /* done with msg */
        /* inform main thread done */
        tc_mutex_lock(mutex);
        fifo_add_item(fifo_processed, enc_done);
        tc_mutex_unlock(mutex);
    }
    while (!finished);

    /* signal completion for main thread */
    g_set_wait_obj(event_processed);

    return 0;
}
#endif

/*****************************************************************************/
/* Diagnostic: XRDP_GFX_TRACE=1 logs the per-frame damage region so a stuck
 * on-screen frame can be checked for a wrong/degenerate metablock region. */
static int
gfx_enc_trace_on(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char *e = g_getenv("XRDP_GFX_TRACE");
        cached = (e != NULL && e[0] == '1') ? 1 : 0;
    }
    return cached;
}

/*****************************************************************************/
/* BACKLOG #70 -- see xrdp_encoder.h */
long long
xrdp_mono_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/*****************************************************************************/
int
xrdp_ack_trace_on(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char *e = g_getenv("XRDP_ACK_TRACE");
        cached = (e != NULL && e[0] == '1') ? 1 : 0;
    }
    return cached;
}

/*****************************************************************************/
static void
gfx_trace_rects(const char *tag, int surface_id, int num_rects,
                struct xrdp_egfx_rect *rects)
{
    int i;
    int bx1 = 1 << 30;
    int by1 = 1 << 30;
    int bx2 = 0;
    int by2 = 0;

    if (!gfx_enc_trace_on())
    {
        return;
    }
    for (i = 0; i < num_rects; i++)
    {
        bx1 = MIN(bx1, rects[i].x1);
        by1 = MIN(by1, rects[i].y1);
        bx2 = MAX(bx2, rects[i].x2);
        by2 = MAX(by2, rects[i].y2);
    }
    LOG(LOG_LEVEL_INFO, "GFX_TRACE %s surface=%d num_rects=%d "
        "bbox=(%d,%d)-(%d,%d) first=(%d,%d)-(%d,%d)", tag, surface_id,
        num_rects, bx1, by1, bx2, by2,
        num_rects > 0 ? rects[0].x1 : -1, num_rects > 0 ? rects[0].y1 : -1,
        num_rects > 0 ? rects[0].x2 : -1, num_rects > 0 ? rects[0].y2 : -1);
}

/* #45 step 7 -- the xorgxrdp AVC444 xup blob is EXACTLY three EGFX
 * commands (rdpClientCon.c, the CC_GFX_AVC444 arm):
 *   STARTFRAME       8 header + frame_id 4 + time_stamp 4      = 16
 *   WIRETOSURFACE_1  8 header + surface_id 2 + codec_id 2 +
 *                    pixel_format 1 + flags 4 + num_rects_d 2 +
 *                    8*nd + num_rects_c 2 + 8*nc +
 *                    left/top/width/height 8 + shmem_offset 4
 *                                             = 33 + 8*nd + 8*nc
 *   ENDFRAME         8 header + frame_id 4                     = 12
 * Only that shape is batchable; the monitor index lives in bits 28..31
 * of the WIRETOSURFACE_1 flags dword. */
#define GFX_BATCH_STARTFRAME_BYTES 16
#define GFX_BATCH_ENDFRAME_BYTES   12
#define GFX_BATCH_W2S1_FIXED_BYTES 33
/* xorgxrdp's own damage-rect bound, mirrored by gfx_wiretosurface1_avc444 */
#define GFX_BATCH_MAX_RECTS        (16 * 1024)
/* Items one worker cycle may hold at once. The legal in-flight count is
 * 2 * monitorCount (#45 D13), i.e. at most 32, so this is pure headroom;
 * it MUST stay above CLIENT_MONITOR_DATA_MAXIMUM_MONITORS so that hitting
 * the bound always leaves carried items behind and the cycle therefore
 * cannot block with work still on the fifo. */
/* the emit dispatcher's own command-length gate (process_enc_egfx): the
 * batch envelope may never exceed it */
#define GFX_BATCH_MAX_CMD_BYTES (32 * 1024)
#define GFX_BATCH_MAX_ITEMS        64

/*****************************************************************************/
static int
gfx_batch_u16(const unsigned char *p)
{
    return (int)p[0] | ((int)p[1] << 8);
}

/*****************************************************************************/
static unsigned int
gfx_batch_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/*****************************************************************************/
/* #45 step 7 -- see xrdp_encoder.h. PURE: reads the blob and nothing
 * else, so it is the one part of step 7 a unit test can pin. Every field
 * is length-checked BEFORE it is read (the damage-rect counts come from
 * the X server and the monitor index from the layout the client
 * advertised), and the three commands must account for cmd_bytes byte for
 * byte -- a padded, truncated or reordered blob is simply not batched and
 * takes the unchanged single-item path. */
int
gfx_egfx_batch_peek_mon(const char *cmd, int cmd_bytes)
{
    const unsigned char *p;
    unsigned int w2s1_bytes;
    unsigned int flags;
    int rem;
    int codec_id;
    int num_rects_d;
    int num_rects_c;

    if (cmd == NULL || cmd_bytes < GFX_BATCH_STARTFRAME_BYTES)
    {
        return -1;
    }
    /* The batch envelope must be a STRICT SUBSET of what the emit pass
     * will accept, or a pair gets encoded for a frame that is then never
     * shipped: process_enc_egfx rejects any command with
     * cmd_bytes > 32 * 1024 BEFORE the WIRETOSURFACE_1 handler runs, so
     * batching such a blob would advance the shared LTR frame_num and
     * both long-term slots for a picture the client never receives --
     * every later P references a reference picture that is missing, i.e.
     * decode corruption until the next scheduled intra (up to
     * intra_refresh_frames pairs). Not reachable from the shipped
     * xorgxrdp (MAX_CAPTURE_RECTS 15 bounds the rect counts), but
     * cmd_bytes arrives verbatim off the xup socket, and the rule here
     * is to bound every field read out of it. */
    if (cmd_bytes > GFX_BATCH_MAX_CMD_BYTES)
    {
        return -1;
    }
    p = (const unsigned char *)cmd;
    /* STARTFRAME, whole and nothing but */
    if (gfx_batch_u16(p) != XR_RDPGFX_CMDID_STARTFRAME ||
            gfx_batch_u32(p + 4) != GFX_BATCH_STARTFRAME_BYTES)
    {
        return -1;
    }
    p += GFX_BATCH_STARTFRAME_BYTES;
    rem = cmd_bytes - GFX_BATCH_STARTFRAME_BYTES;
    /* WIRETOSURFACE_1 header */
    if (rem < 8 || gfx_batch_u16(p) != XR_RDPGFX_CMDID_WIRETOSURFACE_1)
    {
        return -1;
    }
    w2s1_bytes = gfx_batch_u32(p + 4);
    if (w2s1_bytes < GFX_BATCH_W2S1_FIXED_BYTES ||
            w2s1_bytes > (unsigned int)rem)
    {
        return -1;
    }
    /* the AVC444 codec ids only: a progressive or AVC420 blob is not
     * driven by the pair encoder and must never be batched */
    codec_id = gfx_batch_u16(p + 10);
    if (codec_id != XR_RDPGFX_CODECID_AVC444 &&
            codec_id != XR_RDPGFX_CODECID_AVC444V2)
    {
        return -1;
    }
    flags = gfx_batch_u32(p + 13);
    num_rects_d = gfx_batch_u16(p + 17);
    if (num_rects_d < 1 || num_rects_d > GFX_BATCH_MAX_RECTS)
    {
        return -1;
    }
    /* num_rects_c sits behind the damage rects; check before reading */
    if (w2s1_bytes < (unsigned int)(19 + num_rects_d * 8 + 2))
    {
        return -1;
    }
    num_rects_c = gfx_batch_u16(p + 19 + num_rects_d * 8);
    if (num_rects_c < 1 || num_rects_c > GFX_BATCH_MAX_RECTS)
    {
        return -1;
    }
    /* the copy rects, the destination rect and the shmem offset must
     * close the command exactly */
    if (w2s1_bytes != (unsigned int)(GFX_BATCH_W2S1_FIXED_BYTES +
                                     num_rects_d * 8 + num_rects_c * 8))
    {
        return -1;
    }
    p += w2s1_bytes;
    rem -= (int)w2s1_bytes;
    /* ENDFRAME must close the blob exactly */
    if (rem != GFX_BATCH_ENDFRAME_BYTES ||
            gfx_batch_u16(p) != XR_RDPGFX_CMDID_ENDFRAME ||
            gfx_batch_u32(p + 4) != GFX_BATCH_ENDFRAME_BYTES)
    {
        return -1;
    }
    return (int)((flags >> 28) & 0xF);
}

/*****************************************************************************/
/* BACKLOG #70 -- see xrdp_encoder.h */
int
gfx_egfx_batch_peek_frame_id(const char *cmd, int cmd_bytes)
{
    const unsigned char *p;

    if (cmd == NULL || cmd_bytes < GFX_BATCH_STARTFRAME_BYTES)
    {
        return -1;
    }
    p = (const unsigned char *)cmd;
    if (gfx_batch_u16(p) != XR_RDPGFX_CMDID_STARTFRAME ||
            gfx_batch_u32(p + 4) != GFX_BATCH_STARTFRAME_BYTES)
    {
        return -1;
    }
    /* STARTFRAME body: cmd_id 2 + flags 2 + cmd_bytes 4, then frame_id.
     * The producer counts rect_id up from 1 in an int; a value that
     * cannot be one is refused rather than wrapped into a negative id
     * that would poison the ack frontier. */
    if (gfx_batch_u32(p + 8) > (unsigned int)INT_MAX)
    {
        return -1;
    }
    return (int)gfx_batch_u32(p + 8);
}

/*****************************************************************************/
/* #45 step 7 -- see xrdp_encoder.h. PURE apart from reading the items'
 * own blobs. A non-GFX item (a surface-command frame on the same fifo)
 * can never be batched: its union holds u.sc, so u.gfx must not even be
 * read for it. */
int
gfx_egfx_batch_group(XRDP_ENC_DATA **in, int n_in,
                     XRDP_ENC_DATA **set, int *set_mon, int *set_n)
{
    int index;
    int mon;
    int seen;

    if (set_n == NULL)
    {
        return 0;
    }
    *set_n = 0;
    if (in == NULL || set == NULL || set_mon == NULL || n_in < 1)
    {
        return 0;
    }
    seen = 0;
    for (index = 0; index < n_in; index++)
    {
        if (index >= CLIENT_MONITOR_DATA_MAXIMUM_MONITORS)
        {
            /* unreachable: a 17th batchable item must repeat a monitor
             * index and end the batch below. Bounds the writes anyway. */
            break;
        }
        if (in[index] == NULL)
        {
            break;
        }
        mon = -1;
        if (ENC_IS_BIT_SET(in[index]->flags, ENC_FLAGS_GFX_BIT))
        {
            mon = gfx_egfx_batch_peek_mon(in[index]->u.gfx.cmd,
                                          in[index]->u.gfx.cmd_bytes);
        }
        if (mon < 0)
        {
            if (index == 0)
            {
                /* the head is not the batchable shape: the set is that
                 * one item, processed exactly as before this step */
                set[0] = in[0];
                set_mon[0] = -1;
                *set_n = 1;
                return 1;
            }
            break; /* ends the batch BEFORE it */
        }
        if ((seen & (1 << mon)) != 0)
        {
            /* a second item for a monitor already in the set is that
             * monitor's NEXT frame; batching it would reorder its own
             * frames, so it ends the batch */
            break;
        }
        seen |= 1 << mon;
        set[index] = in[index];
        set_mon[index] = mon;
        *set_n = index + 1;
    }
    return *set_n;
}

/*****************************************************************************/
/* Emit an RFX_AVC420_METABLOCK. Kept outside the x264/OpenH264 guard so the
 * external ffmpeg AVC444 backend can reuse it without a linked H.264 library
 * (PRD FR-CAP-0). Not static: the origin even-alignment below is unit tested. */
int
out_RFX_AVC420_METABLOCK(struct xrdp_egfx_rect *dst_rect,
                         struct stream *s,
                         struct xrdp_egfx_rect *rects,
                         int num_rects)
{
    struct xrdp_region *reg;
    struct xrdp_rect rect;
    int index;
    int count;

    /* RFX_AVC420_METABLOCK */
    s_push_layer(s, iso_hdr, 4); /* numRegionRects, set later */
    reg = xrdp_region_create(NULL);
    if (reg == NULL)
    {
        return 1;
    }
    for (index = 0; index < num_rects; index++)
    {
        rect.left = MAX(0, rects[index].x1 - dst_rect->x1 - 1);
        rect.top = MAX(0, rects[index].y1 - dst_rect->y1 - 1);
        rect.right = MIN(dst_rect->x2 - dst_rect->x1,
                         rects[index].x2 - dst_rect->x1 + 1);
        rect.bottom = MIN(dst_rect->y2 - dst_rect->y1,
                          rects[index].y2 - dst_rect->y1 + 1);
        xrdp_region_add_rect(reg, &rect);
    }
    index = 0;
    while (xrdp_region_get_rect(reg, index, &rect) == 0)
    {
        /* Even-align the WHOLE rect to the chroma sampling grid. The AVC444
         * decoder reconstructs chroma one region rect at a time, indexing the
         * odd columns/rows relative to the rect origin (MS-RDPEGFX 3.3.8.3.x);
         * an odd left/top flips chroma parity and fringes the rect's left/top
         * edge (magenta/teal burr on high-contrast edges), and an odd width/
         * height leaves the last column/row's chroma pairing ambiguous —
         * lenient decoders cover it via (width + 1) / 2, but strict ones
         * (FreeRDP's SSE 4:4:4 reconstruction) hard-assert even dimensions.
         * Origins round down, extents round up (both content-preserving on
         * the already 1px-expanded rect), clamped to the surface; a rect
         * flush against an odd-sized surface edge stays odd there, which
         * only an odd-sized surface can produce. */
        rect.left &= ~1;
        rect.top &= ~1;
        rect.right = MIN(dst_rect->x2 - dst_rect->x1, (rect.right + 1) & ~1);
        rect.bottom = MIN(dst_rect->y2 - dst_rect->y1,
                          (rect.bottom + 1) & ~1);
        out_uint16_le(s, rect.left);
        out_uint16_le(s, rect.top);
        out_uint16_le(s, rect.right);
        out_uint16_le(s, rect.bottom);
        index++;
    }
    xrdp_region_delete(reg);
    count = index;
    while (index > 0)
    {
        out_uint8(s, 23); /* qp */
        out_uint8(s, 100); /* quality level 0..100 */
        index--;
    }
    s_push_layer(s, mcs_hdr, 0);
    s_pop_layer(s, iso_hdr);
    out_uint32_le(s, count); /* numRegionRects */
    s_pop_layer(s, mcs_hdr);
    return 0;
}

/*****************************************************************************/
/* Serialize ONE view of an RFX_AVC444_BITMAP_STREAM body (MS-RDPEGFX 2.2.4.5)
 * into s, which must be a fresh stream (the info word is backfilled at
 * s->data[0]): one avc420EncodedBitstreamInfo word -- cbAvc420EncodedBitstream1
 * in bits 0..29, LC in bits 30..31 -- followed by a single
 * RFX_AVC420_BITMAP_STREAM (metablock over the region rects + Annex-B bitstream)
 * for that view.
 *   lc == 1 (luma):   cb = len(metablock + bitstream) of the main view; only
 *                     bitstream1 (main YUV420) follows.
 *   lc == 2 (chroma): cb = 0 (bitstream1 absent); only bitstream2 (aux chroma)
 *                     follows.
 * The AVC444 emitter pairs an LC=1 luma PDU with an LC=2 chroma PDU inside one
 * GFX frame, so the wire matches a real Windows AVC444v2 server (luma-first
 * bootstrap; chroma always deferred as an LC=2 P-slice) while staying atomic per
 * frame. v1 vs v2 is selected by the codec id, not by LC. Unit tested. */
int
out_RFX_AVC444_BITMAP_STREAM_view(struct xrdp_egfx_rect *dst_rect,
                                  struct stream *s,
                                  struct xrdp_egfx_rect *d_rects, int num_rects,
                                  const unsigned char *view_data, int view_len,
                                  int lc)
{
    int cb;
    unsigned int info;

    out_uint32_le(s, 0); /* avc420EncodedBitstreamInfo, backfilled below */
    if (out_RFX_AVC420_METABLOCK(dst_rect, s, d_rects, num_rects) != 0)
    {
        return 1;
    }
    out_uint8a(s, view_data, view_len);
    s_mark_end(s);
    /* cbAvc420EncodedBitstream1 (bits 0..29): for LC=1 the length of the luma
     * sub-stream (metablock + bitstream) carried here; for LC=2 the luma
     * sub-stream is absent, so cb = 0 and this payload is bitstream2. */
    cb = (lc == 1) ? (int)(s->p - s->data) - 4 : 0;
    info = ((unsigned int)cb & 0x3FFFFFFF) | ((unsigned int)(lc & 0x3) << 30);
    s->data[0] = (char)(info & 0xff);
    s->data[1] = (char)((info >> 8) & 0xff);
    s->data[2] = (char)((info >> 16) & 0xff);
    s->data[3] = (char)((info >> 24) & 0xff);
    return 0;
}

#if defined(XRDP_X264) || defined(XRDP_OPENH264)

/*****************************************************************************/
/* called from encoder thread */
static int
process_enc_h264(struct xrdp_encoder *self, XRDP_ENC_DATA *enc)
{
    LOG_DEVEL(LOG_LEVEL_INFO, "process_enc_h264: dummy func");
    return 0;
}
#endif

/*****************************************************************************/
static int
gfx_send_done(struct xrdp_encoder *self, XRDP_ENC_DATA *enc,
              int comp_bytes, int pad_bytes, char *comp_pad_data,
              int got_frame_id, int frame_id, int is_last)

{
    XRDP_ENC_DATA_DONE *enc_done;

    enc_done = g_new0(XRDP_ENC_DATA_DONE, 1);
    if (enc_done == NULL)
    {
        return 1;
    }
    ENC_SET_BIT(enc_done->flags, ENC_DONE_FLAGS_GFX_BIT);
    enc_done->enc = enc;
    enc_done->last = is_last;
    enc_done->pad_bytes = pad_bytes;
    enc_done->comp_bytes = comp_bytes;
    enc_done->comp_pad_data = comp_pad_data;
    if (got_frame_id)
    {
        ENC_SET_BIT(enc_done->flags, ENC_DONE_FLAGS_FRAME_ID_BIT);
        enc_done->frame_id = frame_id;
    }
    /* inform main thread done */
    tc_mutex_lock(self->mutex);
    fifo_add_item(self->fifo_processed, enc_done);
    tc_mutex_unlock(self->mutex);
    /* signal completion for main thread */
    g_set_wait_obj(self->xrdp_encoder_event_processed);
    return 0;
}

/*****************************************************************************/
/* PRD FR-ACK-1 rule 2: the TERMINAL ack of one received paint msg.
 *
 * Carries the ECHOED frame id of that msg -- never a counter this side
 * maintains -- and the terminal state it reached:
 *
 *   displayed=1  every PDU of the frame was built and queued;
 *   displayed=0  the frame was consumed but produced no output frame
 *                (AVC444 warmup PENDING, encoder error, dropped pair).
 *
 * It carries no bytes (comp_bytes 0), so the client-facing stream is
 * unchanged, and it is the msg's LAST enc_done, which is what releases
 * the XRDP_ENC_DATA. Emitting it on EVERY path is what makes the ack
 * total: a msg that produced no ack used to pin a producer capture slot
 * until some later frame's cumulative ack happened to cover it, and at
 * cap there is no later frame. */
static int
gfx_send_terminal_ack(struct xrdp_encoder *self, XRDP_ENC_DATA *enc,
                      int frame_id, int displayed)
{
    XRDP_ENC_DATA_DONE *enc_done;

    enc_done = g_new0(XRDP_ENC_DATA_DONE, 1);
    if (enc_done == NULL)
    {
        return 1;
    }
    ENC_SET_BIT(enc_done->flags, ENC_DONE_FLAGS_GFX_BIT);
    ENC_SET_BIT(enc_done->flags, ENC_DONE_FLAGS_FRAME_ID_BIT);
    if (!displayed)
    {
        ENC_SET_BIT(enc_done->flags, ENC_DONE_FLAGS_NOT_DISPLAYED_BIT);
    }
    enc_done->enc = enc;
    enc_done->last = 1;
    enc_done->frame_id = frame_id;
    tc_mutex_lock(self->mutex);
    fifo_add_item(self->fifo_processed, enc_done);
    tc_mutex_unlock(self->mutex);
    g_set_wait_obj(self->xrdp_encoder_event_processed);
    return 0;
}

/*****************************************************************************/
/* BACKLOG #70: report that the encoder children have ABSORBED this
 * frame's input, so the producer's capture slot can be released without
 * waiting for the rest of the pipeline (LTR rewrite, EGFX assembly,
 * transport egress).
 *
 * This is NOT a frame: comp_bytes 0 (nothing reaches the client) and
 * last 0 (the XRDP_ENC_DATA still belongs to the emit pass that follows).
 * It carries the ECHOED frame id, and it is emitted ONLY after a
 * successful collect -- the child cannot have produced the pair without
 * having read the input the pair was made from, which is exactly the
 * proof the borrowed capture pages (FR-PROC-6: vmsplice, never GIFT)
 * are no longer referenced. Emitting it any earlier would let Xorg
 * overwrite pages a child is still reading. */
static int
gfx_send_consumed(struct xrdp_encoder *self, XRDP_ENC_DATA *enc,
                  int frame_id)
{
    XRDP_ENC_DATA_DONE *enc_done;

    enc_done = g_new0(XRDP_ENC_DATA_DONE, 1);
    if (enc_done == NULL)
    {
        /* the frame is still acked by its terminal enc_done, one full
         * pipeline later: slower, never wrong */
        return 1;
    }
    ENC_SET_BIT(enc_done->flags, ENC_DONE_FLAGS_GFX_BIT);
    ENC_SET_BIT(enc_done->flags, ENC_DONE_FLAGS_FRAME_ID_BIT);
    ENC_SET_BIT(enc_done->flags, ENC_DONE_FLAGS_CONSUMED_BIT);
    enc_done->enc = enc;
    enc_done->last = 0;
    enc_done->frame_id = frame_id;
    tc_mutex_lock(self->mutex);
    fifo_add_item(self->fifo_processed, enc_done);
    tc_mutex_unlock(self->mutex);
    g_set_wait_obj(self->xrdp_encoder_event_processed);
    return 0;
}

/*****************************************************************************/
/* Debug-only capture (env XRDP_AVC444_DUMP=<dir>): write the exact main/aux
 * Annex-B H.264 substreams the client receives, plus the surface dimensions and
 * damage rects, one set of files per emitted frame keyed by desktop sequence.
 * Off unless the env var is set; lets a hard-to-reproduce, resize-triggered
 * decode artifact be captured from a live session and decoded/inspected
 * offline. Best-effort; failures are silent so capture never affects the
 * session. */
static void
avc444_debug_dump(unsigned long long seq, int twidth, int theight,
                  int cwidth, int cheight,
                  struct xrdp_egfx_rect *d_rects, int num_rects,
                  const struct xrdp_avc444_encoded_pair *pair,
                  const unsigned char *main_view,
                  const unsigned char *aux_view, int nv12_bytes)
{
    const char *dir;
    char path[512];
    char meta[2048];
    int fd;
    int i;
    int n;

    dir = g_getenv("XRDP_AVC444_DUMP");
    if (dir == NULL || dir[0] == '\0')
    {
        return;
    }
    if (!g_directory_exist(dir))
    {
        g_mkdir(dir);
    }
    /* also dump the packed NV12 views the capture delivered (pre-H.264):
     * losslessly combinable with no reference chain, so the capture packer
     * can be isolated from the H.264 encode/decode. */
    if (main_view != NULL && nv12_bytes > 0)
    {
        g_snprintf(path, sizeof(path), "%s/%06llu_conv_main.nv12", dir, seq);
        fd = g_file_open_ex(path, 0, 1, 1, 1);
        if (fd >= 0)
        {
            g_file_write(fd, (const char *)main_view, nv12_bytes);
            g_file_close(fd);
        }
        if (aux_view != NULL)
        {
            g_snprintf(path, sizeof(path), "%s/%06llu_conv_aux.nv12", dir,
                       seq);
            fd = g_file_open_ex(path, 0, 1, 1, 1);
            if (fd >= 0)
            {
                g_file_write(fd, (const char *)aux_view, nv12_bytes);
                g_file_close(fd);
            }
        }
    }
    g_snprintf(path, sizeof(path), "%s/%06llu_main.264", dir, seq);
    fd = g_file_open_ex(path, 0, 1, 1, 1);
    if (fd >= 0)
    {
        g_file_write(fd, (const char *)pair->main_data, pair->main_len);
        g_file_close(fd);
    }
    g_snprintf(path, sizeof(path), "%s/%06llu_aux.264", dir, seq);
    fd = g_file_open_ex(path, 0, 1, 1, 1);
    if (fd >= 0)
    {
        g_file_write(fd, (const char *)pair->aux_data, pair->aux_len);
        g_file_close(fd);
    }
    n = g_snprintf(meta, sizeof(meta),
                   "seq=%llu surf=%dx%d coded=%dx%d nrects=%d rects=",
                   seq, twidth, theight, cwidth, cheight, num_rects);
    for (i = 0; i < num_rects && n < (int)sizeof(meta) - 48; i++)
    {
        n += g_snprintf(meta + n, sizeof(meta) - n, "%d,%d,%d,%d;",
                        d_rects[i].x1, d_rects[i].y1,
                        d_rects[i].x2, d_rects[i].y2);
    }
    g_snprintf(path, sizeof(path), "%s/%06llu_meta.txt", dir, seq);
    fd = g_file_open_ex(path, 0, 1, 1, 1);
    if (fd >= 0)
    {
        g_file_write(fd, meta, g_strlen(meta));
        g_file_close(fd);
    }
}

/*****************************************************************************/
/* RFX_AVC420_BITMAP_STREAM serializer for the external ffmpeg backend, plain
 * AVC420 (codec id 0x000B): a single RFX_AVC420_METABLOCK followed by one
 * H.264 sub-stream — no avc420EncodedBitstreamInfo/LC word and no second
 * (auxiliary) sub-stream. The converter fills only the main YUV420 view and
 * the child encodes one picture per frame. Like the AVC444 path the encode is
 * synchronous (content always matches this frame's damage region); NULL on
 * encoder error only (defensive PENDING keeps prior client content). */
static struct stream *
gfx_wiretosurface1_avc420(struct xrdp_encoder *self,
                          struct xrdp_egfx_bulk *bulk, struct stream *in_s,
                          XRDP_ENC_DATA *enc)
{
    int index;
    int surface_id;
    int codec_id;
    int pixel_format;
    int num_rects_d;
    int num_rects_c;
    int flags;
    int mon_index;
    short left;
    short top;
    short width;
    short height;
    short twidth;
    short theight;
    struct xrdp_egfx_rect *d_rects;
    struct xrdp_egfx_rect dst_rect;
    struct xrdp_enc_gfx_cmd *enc_gfx_cmd = &(enc->u.gfx);
    const unsigned char *main_view;
    struct xrdp_ffmpeg_avc444 *ff;
    struct xrdp_avc444_encoded_pair pic;
    int nv12_bytes;
    struct stream ls;
    struct stream *s;
    struct stream *rv;
    int enc_rv;
    int bitmap_data_length;
    int need;
    int shmem_offset;

    if (!s_check_rem(in_s, 11))
    {
        return NULL;
    }
    in_uint16_le(in_s, surface_id);
    in_uint16_le(in_s, codec_id);
    in_uint8(in_s, pixel_format);
    in_uint32_le(in_s, flags);
    mon_index = (flags >> 28) & 0xF;
    (void)codec_id; /* the AVC mode is authoritative; override to AVC420 */
    in_uint16_le(in_s, num_rects_d);
    if ((num_rects_d < 1) || (num_rects_d > 16 * 1024) ||
            (!s_check_rem(in_s, num_rects_d * 8)))
    {
        return NULL;
    }
    d_rects = g_new0(struct xrdp_egfx_rect, num_rects_d);
    if (d_rects == NULL)
    {
        return NULL;
    }
    for (index = 0; index < num_rects_d; index++)
    {
        in_uint16_le(in_s, left);
        in_uint16_le(in_s, top);
        in_uint16_le(in_s, width);
        in_uint16_le(in_s, height);
        d_rects[index].x1 = left;
        d_rects[index].y1 = top;
        d_rects[index].x2 = left + width;
        d_rects[index].y2 = top + height;
    }
    if (!s_check_rem(in_s, 2))
    {
        g_free(d_rects);
        return NULL;
    }
    in_uint16_le(in_s, num_rects_c);
    if ((num_rects_c < 1) || (num_rects_c > 16 * 1024) ||
            (!s_check_rem(in_s, num_rects_c * 8 + 8)))
    {
        g_free(d_rects);
        return NULL;
    }
    in_uint8s(in_s, num_rects_c * 8); /* c_rects unused in MVP */
    gfx_trace_rects("avc dmg", surface_id, num_rects_d, d_rects);
    in_uint16_le(in_s, left);
    in_uint16_le(in_s, top);
    in_uint16_le(in_s, width);
    in_uint16_le(in_s, height);
    /* per-monitor capture shmem offset (multimon plane split); the field
     * is bounded by this command's cmd_bytes, absent means base 0 */
    shmem_offset = 0;
    if (s_check_rem(in_s, 4))
    {
        in_uint32_le(in_s, shmem_offset);
    }
    twidth = width;
    theight = height;
    dst_rect.x1 = 0;
    dst_rect.y1 = 0;
    dst_rect.x2 = width;
    dst_rect.y2 = height;

    nv12_bytes = xup_cap_avc444_nv12_bytes(twidth, theight,
                                           self->avc444_chroma_align);
    if (twidth < 1 || theight < 1 || nv12_bytes < 1 ||
            shmem_offset < 0 || shmem_offset > enc_gfx_cmd->data_bytes ||
            nv12_bytes > enc_gfx_cmd->data_bytes - shmem_offset)
    {
        g_free(d_rects);
        return NULL;
    }

    /* lazily (re)create the per-surface ffmpeg child; a visible resize
     * drops it so the new generation starts with a fresh IDR */
    ff = (struct xrdp_ffmpeg_avc444 *)self->avc444_ffmpeg_handle[mon_index];
    if (ff != NULL &&
            (self->avc444_actual_w[mon_index] != twidth ||
             self->avc444_actual_h[mon_index] != theight))
    {
        xrdp_ffmpeg_avc444_delete(ff);
        self->avc444_ffmpeg_handle[mon_index] = NULL;
        ff = NULL;
    }
    if (ff == NULL)
    {
        struct xrdp_ffmpeg_avc444_config cfg;
        xrdp_ffmpeg_avc444_config_default(&cfg);
        cfg.chroma_align = self->avc444_chroma_align;
        cfg.use_dump_extra = self->avc444_dump_extra;
        cfg.strip_sei = self->avc444_strip_sei;
        cfg.sanitize_hrd = self->avc444_sanitize_hrd;
        cfg.strip_pic_struct = self->avc444_strip_pic_struct;
        cfg.fault_aux_delay = self->avc444_fault_aux_delay;
        cfg.fault_strip_mmco = self->avc444_fault_strip_mmco;
        g_strncpy(cfg.path, self->avc444_path, sizeof(cfg.path) - 1);
        cfg.encoder_args = self->avc444_encoder_args;
        ff = xrdp_ffmpeg_avc444_create(&cfg, twidth, theight);
        if (ff == NULL)
        {
            g_free(d_rects);
            return NULL;
        }
        self->avc444_ffmpeg_handle[mon_index] = ff;
        self->avc444_actual_w[mon_index] = twidth;
        self->avc444_actual_h[mon_index] = theight;
    }

    /* the capture shmem already holds the packed main NV12 view at the
     * coded geometry (FR-CAPTURE-6); hand the borrowed pointer straight
     * to the vmsplice feeder -- zero pixel-domain work here (FR-PROC-6) */
    main_view = (const unsigned char *)enc_gfx_cmd->data + shmem_offset;
    enc_rv = xrdp_ffmpeg_avc444_encode_single(ff, main_view,
             nv12_bytes, self->avc444_seq++, &pic);
    if (gfx_enc_trace_on() && enc_rv != XRDP_FFMPEG_PAIR_ERROR)
    {
        int t_cw = xrdp_ffmpeg_avc444_coded_width(ff);
        int cy_off = (xrdp_ffmpeg_avc444_coded_height(ff) / 2) * t_cw
                     + t_cw / 2;
        LOG(LOG_LEVEL_INFO, "GFX_TRACE enc submitted_seq=%llu returned_seq="
            "%lld rv=%s inflight=%d centerY=%d",
            (unsigned long long)(self->avc444_seq - 1),
            enc_rv == XRDP_FFMPEG_PAIR_READY
            ? (long long)pic.desktop_sequence : -1LL,
            enc_rv == XRDP_FFMPEG_PAIR_READY ? "READY" : "PENDING",
            xrdp_ffmpeg_avc444_inflight(ff),
            (int)main_view[cy_off]);
    }
    if (enc_rv == XRDP_FFMPEG_PAIR_ERROR)
    {
        xrdp_ffmpeg_avc444_delete(ff);
        self->avc444_ffmpeg_handle[mon_index] = NULL;
        g_free(d_rects);
        return NULL;
    }
    if (enc_rv != XRDP_FFMPEG_PAIR_READY)
    {
        g_free(d_rects); /* defensive: no pair; client keeps prior content */
        return NULL;
    }

    need = pic.main_len + num_rects_d * 24 + 512;
    s = &ls;
    g_memset(s, 0, sizeof(struct stream));
    s->size = need;
    s->data = g_new(char, s->size);
    if (s->data == NULL)
    {
        g_free(d_rects);
        return NULL;
    }
    s->p = s->data;
    if (out_RFX_AVC420_METABLOCK(&dst_rect, s, d_rects, num_rects_d) != 0)
    {
        g_free(s->data);
        g_free(d_rects);
        return NULL;
    }
    out_uint8a(s, pic.main_data, pic.main_len);
    s_mark_end(s);
    bitmap_data_length = (int)(s->end - s->data);
    rv = xrdp_egfx_wire_to_surface1(bulk, surface_id,
                                    XR_RDPGFX_CODECID_AVC420,
                                    pixel_format, &dst_rect,
                                    s->data, bitmap_data_length);
    g_free(s->data);
    g_free(d_rects);
    return rv;
}

/*****************************************************************************/
/* Build the ffmpeg runner config from the encoder's session-scoped policy.
 * EXTRACTED so it is unit-testable: this is the LAST hop of the gfx.toml
 * plumbing (tconfig -> xrdp_mm -> struct xrdp_encoder -> cfg), and a field
 * silently dropped here is invisible everywhere else -- ltr_rekey_frame_num
 * was write-only for exactly this reason (found on arm-o, 2026-07-29: the
 * knob loaded, logged, and never reached the encoder). Every session-scoped
 * cfg field MUST be assigned here and asserted by
 * test_avc444_cfg_from_encoder_carries_every_field. */
void
xrdp_avc444_cfg_from_encoder(const struct xrdp_encoder *self,
                             struct xrdp_ffmpeg_avc444_config *cfg)
{
    xrdp_ffmpeg_avc444_config_default(cfg);
    cfg->chroma_align = self->avc444_chroma_align;
    cfg->use_dump_extra = self->avc444_dump_extra;
    cfg->strip_sei = self->avc444_strip_sei;
    cfg->sanitize_hrd = self->avc444_sanitize_hrd;
    cfg->strip_pic_struct = self->avc444_strip_pic_struct;
    /* reference partitioning is STRUCTURAL, not configurable (PRD
     * FR-H264-7): the aux view is encoded by a second all-IDR child
     * and shipped as non-reference, non-IDR I leaves, so main frames
     * never reference aux frames under any client decode topology */
    cfg->aux_intra_leaf = 1;
    /* EXPERIMENTAL FR-H264-8 (gfx.toml aux_ltr_chain): takes
     * precedence over the leaf path inside the runner */
    cfg->aux_ltr_chain = self->avc444_aux_ltr_chain;
    cfg->ltr_rekey_frame_num = self->avc444_ltr_rekey_frame_num;
    cfg->intra_refresh_frames = self->avc444_intra_refresh_frames;
    cfg->fault_aux_delay = self->avc444_fault_aux_delay;
    cfg->fault_strip_mmco = self->avc444_fault_strip_mmco;
    g_strncpy(cfg->path, self->avc444_path, sizeof(cfg->path) - 1);
    cfg->encoder_args = self->avc444_encoder_args;
}

/*****************************************************************************/
/* Queue one already-built EGFX PDU, taking ownership of s either way. */
static int
gfx_queue_pdu(struct xrdp_encoder *self, XRDP_ENC_DATA *enc, struct stream *s)
{
    if (s == NULL)
    {
        return 1;
    }
    if (gfx_send_done(self, enc, (int)(s->end - s->data), 0, s->data,
                      0, 0, 0) != 0)
    {
        free_stream(s);
        return 1;
    }
    g_free(s); /* ->data now owned by the queued enc_done */
    return 0;
}

/* aux_ltr_chain re-key (BACKLOG #48), part 1 of 2: create the REPLACEMENT
 * surface, ahead of this frame's pixels.
 *
 * MS-RDPEGFX binds codec/decoder state to the surface, so destroying one
 * is a protocol-defined decoder teardown -- the event class a resize
 * already produces and every client already survives -- rather than an
 * in-band IDR whose handling by a two-context client we would have to
 * assume.
 *
 * The replacement is built under a DIFFERENT id and is NOT mapped here.
 * The first implementation reused the same id and emitted
 * DELETE -> CREATE -> MAP -> pixels, which leaves output mapped to a
 * freshly created (zero-filled) surface for as long as the re-key IDR
 * takes to arrive and decode -- measured at ~200 ms plus decode. macOS
 * flashed black at every boundary (owner, 2026-07-29). FreeRDP never
 * showed it because gdi_MapSurfaceToOutput only sets flags and
 * presentation happens at END_FRAME, so no amount of client-side
 * sampling here could have caught it; the wire order is the invariant. */
static int
gfx_emit_surface_create(struct xrdp_encoder *self, XRDP_ENC_DATA *enc,
                        struct xrdp_egfx_bulk *bulk, int new_surface_id,
                        int width, int height)
{
    return gfx_queue_pdu(self, enc,
                         xrdp_egfx_create_surface(bulk, new_surface_id,
                                 width, height,
                                 XR_PIXEL_FORMAT_XRGB_8888));
}

/* Part 2 of 2: hand output over to the replacement, AFTER its pixels.
 *
 * MAP(new) is queued and DELETE(old) is returned so the caller emits it
 * last. Order matters in both directions: mapping before the pixels
 * shows a blank surface, and deleting the old one before mapping the new
 * leaves output with nothing mapped. Between MAP and DELETE both
 * surfaces are briefly mapped at the same origin, which is the only
 * window in which a client can composite, and by then the new surface
 * already holds the full-surface repaint. */
static struct stream *
gfx_emit_surface_swap(struct xrdp_encoder *self, XRDP_ENC_DATA *enc,
                      struct xrdp_egfx_bulk *bulk, int new_surface_id,
                      int old_surface_id, int mon_index)
{
    if (gfx_queue_pdu(self, enc,
                      xrdp_egfx_map_surface(bulk, new_surface_id,
                                            self->avc444_surface_x[mon_index],
                                            self->avc444_surface_y[mon_index]))
            != 0)
    {
        return NULL;
    }
    LOG(LOG_LEVEL_INFO, "gfx_wiretosurface1_avc444: aux_ltr_chain re-key: "
        "surface %d replaced by %d at %d,%d, mapped only after its full "
        "repaint from the fresh IDR", old_surface_id, new_surface_id,
        self->avc444_surface_x[mon_index],
        self->avc444_surface_y[mon_index]);
    return xrdp_egfx_delete_surface(bulk, old_surface_id);
}

/*****************************************************************************/
/* Lazily (re)create this monitor's ffmpeg child; a visible resize drops it
 * (kill/reap) so the new generation starts with a full LC=0 reset pair
 * (PRD FR-RESIZE).
 *
 * EXTRACTED VERBATIM for #45 step 7 so the batching submit pass and the
 * unchanged emit path share one implementation: the emit path's call is a
 * pure lookup once the submit pass has already created/kept the handle
 * for this cycle. mon_index is always 0..15 (four bits of the flags
 * dword). Returns NULL only when a child could not be spawned. */
static struct xrdp_ffmpeg_avc444 *
gfx_avc444_handle_for(struct xrdp_encoder *self, int mon_index,
                      int twidth, int theight)
{
    struct xrdp_ffmpeg_avc444 *ff;

    ff = (struct xrdp_ffmpeg_avc444 *)self->avc444_ffmpeg_handle[mon_index];
    if (ff != NULL &&
            (self->avc444_actual_w[mon_index] != twidth ||
             self->avc444_actual_h[mon_index] != theight))
    {
        xrdp_ffmpeg_avc444_delete(ff);
        self->avc444_ffmpeg_handle[mon_index] = NULL;
        ff = NULL;
    }
    if (ff == NULL)
    {
        struct xrdp_ffmpeg_avc444_config cfg;
        xrdp_avc444_cfg_from_encoder(self, &cfg);
        ff = xrdp_ffmpeg_avc444_create(&cfg, twidth, theight);
        if (ff == NULL)
        {
            return NULL;
        }
        self->avc444_ffmpeg_handle[mon_index] = ff;
        self->avc444_actual_w[mon_index] = twidth;
        self->avc444_actual_h[mon_index] = theight;
    }
    return ff;
}

/* #45 step 7 -- everything the SUBMIT pass needs out of one already
 * shape-checked xorgxrdp AVC444 blob. Deliberately only the encode
 * inputs: the damage region, the re-key widening, the alternate surface
 * id and every PDU stay in gfx_wiretosurface1_avc444 where they were. */
struct gfx_avc444_submit_info
{
    int mon_index;
    int twidth;
    int theight;
    int nv12_bytes;
    const unsigned char *main_view;
    const unsigned char *aux_view;
};

/*****************************************************************************/
/* Re-derive the encode inputs from the blob, with the SAME arithmetic and
 * the SAME bounds checks the emit path applies (the shmem offset and the
 * coded geometry are client-influenced). Returns 0 on success; on any
 * failure the item is simply not armed and the unchanged emit path
 * re-parses it and rejects it exactly as before this step. */
static int
gfx_avc444_parse_submit(struct xrdp_encoder *self, XRDP_ENC_DATA *enc,
                        struct gfx_avc444_submit_info *out)
{
    const unsigned char *p;
    const unsigned char *rects_end;
    int mon_index;
    int num_rects_d;
    int num_rects_c;
    int shmem_offset;
    int aux_offset;
    short twidth;
    short theight;

    mon_index = gfx_egfx_batch_peek_mon(enc->u.gfx.cmd, enc->u.gfx.cmd_bytes);
    if (mon_index < 0)
    {
        return 1;
    }
    /* peek() has verified every length below */
    p = (const unsigned char *)enc->u.gfx.cmd + GFX_BATCH_STARTFRAME_BYTES;
    num_rects_d = gfx_batch_u16(p + 17);
    num_rects_c = gfx_batch_u16(p + 19 + num_rects_d * 8);
    rects_end = p + 21 + num_rects_d * 8 + num_rects_c * 8;
    /* left and top are not encode inputs; width/height are read as
     * signed shorts exactly as the emit path reads them, so an absurd
     * geometry fails the same way in both places */
    twidth = (short)gfx_batch_u16(rects_end + 4);
    theight = (short)gfx_batch_u16(rects_end + 6);
    shmem_offset = (int)gfx_batch_u32(rects_end + 8);
    out->nv12_bytes = xup_cap_avc444_nv12_bytes(twidth, theight,
                      self->avc444_chroma_align);
    aux_offset = xup_cap_avc444_aux_offset(twidth, theight,
                                           self->avc444_chroma_align);
    if (twidth < 1 || theight < 1 || out->nv12_bytes < 1 ||
            enc->u.gfx.data == NULL ||
            shmem_offset < 0 || shmem_offset > enc->u.gfx.data_bytes ||
            aux_offset + out->nv12_bytes >
            enc->u.gfx.data_bytes - shmem_offset)
    {
        return 1;
    }
    out->mon_index = mon_index;
    out->twidth = twidth;
    out->theight = theight;
    out->main_view = (const unsigned char *)enc->u.gfx.data + shmem_offset;
    out->aux_view = out->main_view + aux_offset;
    return 0;
}

/*****************************************************************************/
/* RFX_AVC444_BITMAP_STREAM (LC=0) serializer for the external ffmpeg AVC444
 * backend (PRD FR-WIRE). Emits AVC444 v1 (codec id 0x000E) or, when the client
 * advertised v2 support, AVC444 v2 (0x000F) with ChromaV2 packing; the LC field
 * stays 0 in both cases. The encode is synchronous: the emitted H.264 pair is
 * exactly this frame's capture, so the metablock damage region always matches
 * the content (a prior pipelined design emitted the previous frame's pair
 * under this frame's region — region-strict clients like mstsc then showed a
 * permanently stale screen). NULL on encoder error only (defensive PENDING
 * keeps prior client content via an empty STARTFRAME/ENDFRAME update). */
static struct stream *
gfx_wiretosurface1_avc444(struct xrdp_encoder *self,
                          struct xrdp_egfx_bulk *bulk, struct stream *in_s,
                          XRDP_ENC_DATA *enc)
{
    int index;
    int surface_id;
    int codec_id;
    int pixel_format;
    int num_rects_d;
    int num_rects_c;
    int flags;
    int mon_index;
    short left;
    short top;
    short width;
    short height;
    short twidth;
    short theight;
    struct xrdp_egfx_rect *d_rects;
    struct xrdp_egfx_rect dst_rect;
    struct xrdp_enc_gfx_cmd *enc_gfx_cmd = &(enc->u.gfx);
    const unsigned char *main_view;
    const unsigned char *aux_view;
    struct xrdp_ffmpeg_avc444 *ff;
    struct xrdp_avc444_encoded_pair pair;
    int base_surface_id;
    int old_surface_id;
    int do_rekey;
    int nv12_bytes;
    int aux_offset;
    struct stream ls;
    struct stream *s;
    struct stream *rv;
    int enc_rv;
    int bitmap_data_length;
    int need;
    int shmem_offset;
    unsigned long long seq;

    if (!s_check_rem(in_s, 11))
    {
        return NULL;
    }
    in_uint16_le(in_s, surface_id);
    in_uint16_le(in_s, codec_id);
    in_uint8(in_s, pixel_format);
    in_uint32_le(in_s, flags);
    mon_index = (flags >> 28) & 0xF;
    (void)codec_id; /* the AVC mode is authoritative; override to AVC444 */
    /* A previous re-key may have moved this monitor's surface to the
     * alternate id (BACKLOG #48). xorgxrdp keeps sending the base id, so
     * translate here, once, before anything is addressed to a surface. */
    base_surface_id = surface_id;
    do_rekey = 0;
    old_surface_id = surface_id;
    if (self->avc444_surface_id_live[mon_index] >= 0)
    {
        surface_id = self->avc444_surface_id_live[mon_index];
        old_surface_id = surface_id;
    }
    in_uint16_le(in_s, num_rects_d);
    if ((num_rects_d < 1) || (num_rects_d > 16 * 1024) ||
            (!s_check_rem(in_s, num_rects_d * 8)))
    {
        return NULL;
    }
    d_rects = g_new0(struct xrdp_egfx_rect, num_rects_d);
    if (d_rects == NULL)
    {
        return NULL;
    }
    for (index = 0; index < num_rects_d; index++)
    {
        in_uint16_le(in_s, left);
        in_uint16_le(in_s, top);
        in_uint16_le(in_s, width);
        in_uint16_le(in_s, height);
        d_rects[index].x1 = left;
        d_rects[index].y1 = top;
        d_rects[index].x2 = left + width;
        d_rects[index].y2 = top + height;
    }
    if (!s_check_rem(in_s, 2))
    {
        g_free(d_rects);
        return NULL;
    }
    in_uint16_le(in_s, num_rects_c);
    if ((num_rects_c < 1) || (num_rects_c > 16 * 1024) ||
            (!s_check_rem(in_s, num_rects_c * 8 + 8)))
    {
        g_free(d_rects);
        return NULL;
    }
    in_uint8s(in_s, num_rects_c * 8); /* c_rects unused in MVP */
    gfx_trace_rects("avc dmg", surface_id, num_rects_d, d_rects);
    in_uint16_le(in_s, left);
    in_uint16_le(in_s, top);
    in_uint16_le(in_s, width);
    in_uint16_le(in_s, height);
    /* per-monitor capture shmem offset (multimon plane split); the field
     * is bounded by this command's cmd_bytes, absent means base 0 */
    shmem_offset = 0;
    if (s_check_rem(in_s, 4))
    {
        in_uint32_le(in_s, shmem_offset);
    }
    twidth = width;
    theight = height;
    dst_rect.x1 = 0;
    dst_rect.y1 = 0;
    dst_rect.x2 = width;
    dst_rect.y2 = height;

    if (self->avc444_surface_reset_pending[mon_index])
    {
        /* re-key (BACKLOG #48): this frame paints a BRAND NEW surface, so
         * the damage region must cover ALL of it. The capture is always a
         * full frame and the encoder was destroyed with the re-key, so
         * this frame's picture is a fresh IDR that genuinely carries
         * every pixel -- declaring the whole surface is accurate, not a
         * widened guess. */
        d_rects[0].x1 = 0;
        d_rects[0].y1 = 0;
        d_rects[0].x2 = twidth;
        d_rects[0].y2 = theight;
        num_rects_d = 1;
        if (self->avc444_ltr_rekey_surface_reset)
        {
            /* alternate base <-> base+16 so the replacement never reuses
             * the id whose decoder state we are discarding, and so this
             * frame's pixels can be addressed to it while the OLD surface
             * is still the one mapped to output */
            do_rekey = 1;
            surface_id = (old_surface_id == base_surface_id)
                         ? base_surface_id + XRDP_AVC444_SURFACE_ALT
                         : base_surface_id;
        }
        else
        {
            /* Churn MASKED from the client (BACKLOG #48, 2026-07-29).
             * The re-key exists for exactly one reason -- to keep the
             * shared frame_num counter away from its wrap -- and the
             * encoder restart alone achieves that: the replacement child
             * opens with a real IDR and the counter resets. The surface
             * lifecycle event was only ever a belt-and-braces decoder
             * teardown, and it is what macOS renders as a black flash
             * (measured in BOTH emission orders). The client sees an
             * ordinary full-surface repaint from a fresh IDR and no
             * surface event at all. */
            self->avc444_surface_reset_pending[mon_index] = 0;
        }
    }

    nv12_bytes = xup_cap_avc444_nv12_bytes(twidth, theight,
                                           self->avc444_chroma_align);
    aux_offset = xup_cap_avc444_aux_offset(twidth, theight,
                                           self->avc444_chroma_align);
    if (twidth < 1 || theight < 1 || nv12_bytes < 1 ||
            shmem_offset < 0 || shmem_offset > enc_gfx_cmd->data_bytes ||
            aux_offset + nv12_bytes >
            enc_gfx_cmd->data_bytes - shmem_offset)
    {
        g_free(d_rects);
        return NULL;
    }

    if (self->avc444_batch_have[mon_index] < 0)
    {
        /* #45 step 7: this monitor's pair failed in THIS cycle's set and
         * was reported there; ship nothing (the client keeps its prior
         * content) exactly as a PENDING pair does below. Checked before
         * the lazy create so a failed monitor does not pay for a fresh
         * child it will not use until its next frame. */
        g_free(d_rects);
        return NULL;
    }

    /* lazily (re)create the per-surface ffmpeg child (see
     * gfx_avc444_handle_for); already done for this cycle when the
     * batching submit pass armed this monitor */
    ff = gfx_avc444_handle_for(self, mon_index, twidth, theight);
    if (ff == NULL)
    {
        g_free(d_rects);
        return NULL;
    }

    /* the capture shmem already holds the packed wire views
     * (FR-CAPTURE-6): main NV12 at shmem_offset, aux NV12 on the next
     * page boundary after it; hand the borrowed pointers straight to
     * the vmsplice feeder -- zero pixel-domain work here (FR-PROC-6) */
    main_view = (const unsigned char *)enc_gfx_cmd->data + shmem_offset;
    aux_view = main_view + aux_offset;
    if (self->avc444_batch_have[mon_index] > 0)
    {
        /* #45 step 7: this monitor's pair was submitted, pumped as part
         * of ONE poll set with every other damaged monitor, and collected
         * before any PDU of this cycle was emitted. Nothing else about
         * this function changes: same framing, same ack, same shmem
         * lifetime, same per-monitor PDU order. */
        pair = self->avc444_batch_pair[mon_index];
        seq = self->avc444_batch_seq[mon_index];
        enc_rv = XRDP_FFMPEG_PAIR_READY;
    }
    else
    {
        seq = self->avc444_seq++;
        enc_rv = xrdp_ffmpeg_avc444_encode_pair(ff, main_view, aux_view,
                                                nv12_bytes, seq, &pair);
    }
    if (gfx_enc_trace_on() && enc_rv != XRDP_FFMPEG_PAIR_ERROR)
    {
        /* centre luma of the CURRENT capture: proves which colour this
         * submission carries vs which sequence the popped pair returns.
         * BACKLOG #70: with the eager ack this frame's capture slot may
         * already have been released and be under the NEXT capture's
         * pen, so the read is skipped rather than reported as this
         * frame's colour -- a diagnostic must not print a value it
         * cannot stand behind. */
        int t_cw = xrdp_ffmpeg_avc444_coded_width(ff);
        int cy_off = (xrdp_ffmpeg_avc444_coded_height(ff) / 2) * t_cw
                     + t_cw / 2;
        int center_y = -1;
        if (!(self->eager_slot_ack && self->avc444_batch_have[mon_index] > 0))
        {
            center_y = (int)main_view[cy_off];
        }
        LOG(LOG_LEVEL_INFO, "GFX_TRACE enc submitted_seq=%llu returned_seq="
            "%lld rv=%s inflight=%d centerY=%d",
            (unsigned long long)seq,
            enc_rv == XRDP_FFMPEG_PAIR_READY
            ? (long long)pair.desktop_sequence : -1LL,
            enc_rv == XRDP_FFMPEG_PAIR_READY ? "READY" : "PENDING",
            xrdp_ffmpeg_avc444_inflight(ff),
            center_y);
    }
    if (enc_rv == XRDP_FFMPEG_PAIR_ERROR)
    {
        xrdp_ffmpeg_avc444_delete(ff);
        self->avc444_ffmpeg_handle[mon_index] = NULL;
        g_free(d_rects);
        return NULL;
    }
    if (enc_rv != XRDP_FFMPEG_PAIR_READY)
    {
        g_free(d_rects); /* defensive: no pair; client keeps prior content */
        return NULL;
    }

    avc444_debug_dump(pair.desktop_sequence, twidth, theight,
                      xrdp_ffmpeg_avc444_coded_width(ff),
                      xrdp_ffmpeg_avc444_coded_height(ff),
                      d_rects, num_rects_d, &pair, main_view, aux_view,
                      nv12_bytes);

    need = 4 + pair.main_len + pair.aux_len + num_rects_d * 24 + 512;
    s = &ls;
    g_memset(s, 0, sizeof(struct stream));
    s->size = need;
    s->data = g_new(char, s->size);
    if (s->data == NULL)
    {
        g_free(d_rects);
        return NULL;
    }
    codec_id = self->avc444_v2 ? XR_RDPGFX_CODECID_AVC444V2
               : XR_RDPGFX_CODECID_AVC444;
    /* Emit the pair as two PDUs within THIS gfx frame: an LC=1 luma view
     * followed by an LC=2 chroma view. On a keyframe the LC=1 PDU carries the
     * IDR (+ SPS/PPS), bootstrapping the client's decoder luma-first exactly as
     * a real Windows AVC444v2 server does (which Apple VideoToolbox behind the
     * macOS Windows App accepts, unlike our former same-region LC=0 pair); on
     * inter frames it is a luma P-slice and the LC=2 PDU the deferred chroma
     * P-slice. Both land between the surrounding STARTFRAME/ENDFRAME, so the
     * update stays atomic and region-strict clients never present a luma-only
     * intermediate. Same H.264 bytes and same total traffic as the old LC=0
     * pair -- only the wire framing changes. The LC=1 PDU is queued inline as a
     * non-last enc_done; the LC=2 PDU is returned for the dispatch loop. */
    s->p = s->data;
    if (out_RFX_AVC444_BITMAP_STREAM_view(&dst_rect, s, d_rects, num_rects_d,
                                          pair.main_data, pair.main_len,
                                          1) != 0)
    {
        g_free(s->data);
        g_free(d_rects);
        return NULL;
    }
    bitmap_data_length = (int)(s->end - s->data);
    {
        struct stream *s_luma =
            xrdp_egfx_wire_to_surface1(bulk, surface_id, codec_id, pixel_format,
                                       &dst_rect, s->data, bitmap_data_length);
        if (s_luma == NULL)
        {
            g_free(s->data);
            g_free(d_rects);
            return NULL;
        }
        /* The replacement surface must EXIST before this frame's pixels
         * can be addressed to it, but it is deliberately not mapped yet;
         * output stays on the old surface until the repaint has landed.
         * Emitted here, with the luma PDU already built, so nothing
         * between the create and the repaint can fail. */
        if (do_rekey)
        {
            if (gfx_emit_surface_create(self, enc, bulk, surface_id,
                                        twidth, theight) != 0)
            {
                free_stream(s_luma);
                g_free(s->data);
                g_free(d_rects);
                return NULL;
            }
            self->avc444_surface_reset_pending[mon_index] = 0;
        }
        if (gfx_send_done(self, enc, (int)(s_luma->end - s_luma->data), 0,
                          s_luma->data, 0, 0, 0) != 0)
        {
            free_stream(s_luma);
            g_free(s->data);
            g_free(d_rects);
            return NULL;
        }
        g_free(s_luma); /* ->data now owned by the queued enc_done */
    }
    s->p = s->data;
    if (out_RFX_AVC444_BITMAP_STREAM_view(&dst_rect, s, d_rects, num_rects_d,
                                          pair.aux_data, pair.aux_len,
                                          2) != 0)
    {
        g_free(s->data);
        g_free(d_rects);
        return NULL;
    }
    bitmap_data_length = (int)(s->end - s->data);
    rv = xrdp_egfx_wire_to_surface1(bulk, surface_id, codec_id, pixel_format,
                                    &dst_rect, s->data, bitmap_data_length);
    g_free(s->data);
    g_free(d_rects);
    if (do_rekey && rv != NULL)
    {
        /* Both views have now landed on the replacement surface, so it is
         * safe to hand output over: queue the aux PDU, then MAP(new), and
         * return DELETE(old) as this command's last PDU. */
        if (gfx_queue_pdu(self, enc, rv) != 0)
        {
            return NULL;
        }
        rv = gfx_emit_surface_swap(self, enc, bulk, surface_id,
                                   old_surface_id, mon_index);
        /* published for the main thread's resize teardown, which would
         * otherwise delete the base id and orphan this one */
        tc_mutex_lock(self->mutex);
        self->avc444_surface_id_live[mon_index] =
            (surface_id == base_surface_id) ? -1 : surface_id;
        tc_mutex_unlock(self->mutex);
        if (rv == NULL)
        {
            return NULL;
        }
    }
    if (xrdp_ffmpeg_avc444_rekey_pending(ff))
    {
        /* aux_ltr_chain: the shared frame_num counter is near its
         * wrap. The current pair HAS shipped (its damage is on the
         * wire above); tearing the encoder down now makes the next
         * damaged frame recreate it -> fresh IDR, counter reset --
         * no frame is ever dropped for the re-key. */
        LOG(LOG_LEVEL_INFO, "gfx_wiretosurface1_avc444: aux_ltr_chain "
            "re-key: recreating the encoder pair after this frame");
        xrdp_ffmpeg_avc444_delete(ff);
        self->avc444_ffmpeg_handle[mon_index] = NULL;
        /* the next frame for this monitor rebuilds the client's decoder
         * with a surface delete/create rather than relying on an in-band
         * IDR (BACKLOG #48) */
        self->avc444_surface_reset_pending[mon_index] = 1;
    }
    return rv;
}

/*****************************************************************************/
/* #45 step 7 -- collect ONE armed monitor. On any failure the handle is
 * torn down (the next frame recreates it -> fresh IDR) and the monitor is
 * marked failed so the emit pass ships nothing for it. */
static void
gfx_batch_collect_one(struct xrdp_encoder *self,
                      struct xrdp_ffmpeg_avc444 *ff, int mon)
{
    if (xrdp_ffmpeg_avc444_collect_pair(ff, self->avc444_batch_seq[mon],
                                        &self->avc444_batch_pair[mon])
            == XRDP_FFMPEG_PAIR_READY)
    {
        self->avc444_batch_have[mon] = 1;
        return;
    }
    LOG(LOG_LEVEL_ERROR, "gfx_batch_run_set: monitor %d pair seq %llu could "
        "not be collected; recreating its encoder pair", mon,
        (unsigned long long)self->avc444_batch_seq[mon]);
    xrdp_ffmpeg_avc444_delete(ff);
    self->avc444_ffmpeg_handle[mon] = NULL;
    self->avc444_batch_have[mon] = -1;
}

/*****************************************************************************/
/* BACKLOG #70: release the capture slot of every item in this set whose
 * pair was collected, i.e. whose input the child has provably absorbed.
 * A monitor whose collect FAILED is deliberately skipped: its child was
 * torn down mid-stream and nothing here proves how much of the input it
 * read, so that frame keeps the shipped egress-paced ack (its terminal
 * enc_done, displayed=0). Off by default (gfx.toml eager_slot_ack). */
static void
gfx_batch_release_slots(struct xrdp_encoder *self, XRDP_ENC_DATA **set,
                        const int *set_mon, int set_n)
{
    int index;
    int frame_id;

    if (!self->eager_slot_ack && !xrdp_ack_trace_on())
    {
        return;
    }
    for (index = 0; index < set_n; index++)
    {
        if (set_mon[index] < 0 || self->avc444_batch_have[set_mon[index]] != 1)
        {
            continue;
        }
        frame_id = gfx_egfx_batch_peek_frame_id(set[index]->u.gfx.cmd,
                                                set[index]->u.gfx.cmd_bytes);
        if (frame_id < 0)
        {
            continue;
        }
        if (xrdp_ack_trace_on())
        {
            /* stamped in BOTH modes: the absorb instant is the axis the
             * A/B is read on, so the control arm has to publish it too */
            LOG(LOG_LEVEL_INFO, "ACK_TRACE absorb id=%d mon=%d us=%lld",
                frame_id, set_mon[index], xrdp_mono_us());
        }
        if (!self->eager_slot_ack)
        {
            continue;
        }
        if (gfx_send_consumed(self, set[index], frame_id) != 0)
        {
            LOG(LOG_LEVEL_ERROR, "gfx_batch_release_slots: consumed report "
                "for frame id %d could not be queued", frame_id);
        }
    }
}

/*****************************************************************************/
/* #45 step 7 -- run ONE grouped set: SUBMIT every batchable item's pair,
 * drive the whole set's children through ONE pump_pairs (E4: 2 children
 * per damaged monitor, so 4 for two monitors), COLLECT each pair, and only
 * then let the caller EMIT. The worker never waits for a monitor that has
 * not queued damage: the set is exactly what the fifo held.
 *
 * Nothing about the wire changes here. Each item still emits its own
 * STARTFRAME/ENDFRAME framing, its own ack and its own shmem lifetime in
 * the emit pass; batching changes only WHEN the children are fed. Because
 * a set holds at most one item per monitor index, the handles in a set are
 * pairwise distinct -- which is what makes the emit pass's per-item
 * teardowns (an ERROR path, or the post-ship aux_ltr_chain re-key) unable
 * to touch a sibling monitor's already-collected pair. */
static void
gfx_batch_run_set(struct xrdp_encoder *self, XRDP_ENC_DATA **set,
                  const int *set_mon, int set_n)
{
    struct xrdp_ffmpeg_avc444 *handles[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int handle_mon[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    struct gfx_avc444_submit_info info;
    struct xrdp_ffmpeg_avc444 *ff;
    unsigned long long seq;
    int n_handles;
    int bad_handle;
    int kids_armed;
    int index;
    int mon;
    int st;

    /* the emit pass must never see state from an earlier cycle */
    for (index = 0; index < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; index++)
    {
        self->avc444_batch_have[index] = 0;
    }
    n_handles = 0;
    kids_armed = 0;
    bad_handle = -1;
    /* SUBMIT pass, in fifo order. The sequence counter is handed out HERE,
     * one value per armed monitor, in fifo (= xorgxrdp rotation) order --
     * the same order and the same single global counter as before this
     * step, so GFX_TRACE lines, XRDP_AVC444_DUMP file names and the wire
     * audit still correlate across monitors. */
    PERF_TRACE("subm_beg", set_n, 0);
    for (index = 0; index < set_n; index++)
    {
        if (set_mon[index] < 0)
        {
            continue; /* not the batchable shape; emitted the old way */
        }
        if (gfx_avc444_parse_submit(self, set[index], &info) != 0)
        {
            /* the unchanged emit path re-parses this item and rejects it
             * exactly as it did before this step */
            continue;
        }
        mon = info.mon_index;
        ff = gfx_avc444_handle_for(self, mon, info.twidth, info.theight);
        if (ff == NULL)
        {
            continue;
        }
        seq = self->avc444_seq++;
        if (xrdp_ffmpeg_avc444_submit_pair(ff, info.main_view, info.aux_view,
                                           info.nv12_bytes, seq)
                != XRDP_FFMPEG_PAIR_READY)
        {
            LOG(LOG_LEVEL_ERROR, "gfx_batch_run_set: submit failed for "
                "monitor %d seq %llu; recreating its encoder pair and "
                "shipping nothing for it this frame", mon,
                (unsigned long long)seq);
            xrdp_ffmpeg_avc444_delete(ff);
            self->avc444_ffmpeg_handle[mon] = NULL;
            self->avc444_batch_have[mon] = -1;
            continue;
        }
        self->avc444_batch_seq[mon] = seq;
        if (xrdp_ack_trace_on())
        {
            LOG(LOG_LEVEL_INFO, "ACK_TRACE submit id=%d mon=%d us=%lld",
                gfx_egfx_batch_peek_frame_id(set[index]->u.gfx.cmd,
                                             set[index]->u.gfx.cmd_bytes),
                mon, xrdp_mono_us());
        }
        handles[n_handles] = ff;
        handle_mon[n_handles] = mon;
        n_handles++;
    }
    PERF_TRACE("subm_end", n_handles, 0);
    if (n_handles < 1)
    {
        return; /* nothing armed; every item takes the unchanged path */
    }
    /* ONE pump over the whole set (D2/D10) */
    PERF_TRACE("pump_beg", n_handles, 0);
    st = xrdp_ffmpeg_avc444_pump_pairs(handles, n_handles, &bad_handle,
                                       &kids_armed);
    PERF_TRACE("pump_end", n_handles, kids_armed);
    self->avc444_batch_cycles++;
    self->avc444_batch_items += n_handles;
    if (kids_armed > self->avc444_batch_max_kids)
    {
        self->avc444_batch_max_kids = kids_armed;
    }
    if (kids_armed >= 4 && !self->avc444_batch_e4_logged)
    {
        /* E4 must be assertable from a deployed log */
        self->avc444_batch_e4_logged = 1;
        LOG(LOG_LEVEL_INFO, "gfx_batch_run_set: E4 -- ONE encoder thread "
            "armed %d children (%d monitors) in one pump set", kids_armed,
            n_handles);
    }
    LOG(LOG_LEVEL_DEBUG, "gfx_batch_run_set: cycle %llu set_n=%d "
        "monitors_armed=%d kids_armed=%d max_kids=%d rv=%d",
        (unsigned long long)self->avc444_batch_cycles, set_n, n_handles,
        kids_armed, self->avc444_batch_max_kids, st);
    if (gfx_enc_trace_on())
    {
        LOG(LOG_LEVEL_INFO, "GFX_TRACE batch cycle=%llu set_n=%d "
            "monitors_armed=%d kids_armed=%d max_kids=%d rv=%d",
            (unsigned long long)self->avc444_batch_cycles, set_n, n_handles,
            kids_armed, self->avc444_batch_max_kids, st);
    }
    if (st != XRDP_FFMPEG_PAIR_READY)
    {
        LOG(LOG_LEVEL_ERROR, "gfx_batch_run_set: pump of %d children failed; "
            "the failing child belongs to monitor %d", kids_armed,
            (bad_handle >= 0 && bad_handle < n_handles)
            ? handle_mon[bad_handle] : -1);
        for (index = 0; index < n_handles; index++)
        {
            mon = handle_mon[index];
            if (index == bad_handle)
            {
                /* ONLY the handle pump_pairs named is torn down here */
                xrdp_ffmpeg_avc444_delete(handles[index]);
                self->avc444_ffmpeg_handle[mon] = NULL;
                self->avc444_batch_have[mon] = -1;
                continue;
            }
            /* the other monitors are not silently encoded with a dead
             * child: each is collected on its own merits, and any whose
             * input the child never consumed is torn down too -- its
             * borrowed capture pointers (FR-PROC-6) are about to be
             * released with this cycle's shmem */
            gfx_batch_collect_one(self, handles[index], mon);
        }
        gfx_batch_release_slots(self, set, set_mon, set_n);
        return;
    }
    /* COLLECT pass -- this is where the NUT pop and the LTR rewrite of
     * BOTH views happen, so the bracket below is the rewrite's real
     * in-situ cost (the offline bench measured 1.75 ms/pair) */
    for (index = 0; index < n_handles; index++)
    {
        PERF_TRACE("coll_beg", handle_mon[index], 0);
        gfx_batch_collect_one(self, handles[index], handle_mon[index]);
        PERF_TRACE("coll_end", handle_mon[index], 0);
    }
    /* #70: the collects above are the absorb proof for this set */
    gfx_batch_release_slots(self, set, set_mon, set_n);
}

/*****************************************************************************/
static struct stream *
gfx_wiretosurface1(struct xrdp_encoder *self,
                   struct xrdp_egfx_bulk *bulk, struct stream *in_s,
                   XRDP_ENC_DATA *enc)
{
    if (self->avc444_ffmpeg)
    {
        return gfx_wiretosurface1_avc444(self, bulk, in_s, enc);
    }
    if (self->avc420_ffmpeg)
    {
        return gfx_wiretosurface1_avc420(self, bulk, in_s, enc);
    }
#if defined(XRDP_X264) || defined(XRDP_OPENH264)
    int index;
    int surface_id;
    int codec_id;
    int pixel_format;
    int num_rects_d;
    int num_rects_c;
    struct stream *rv;
    short left;
    short top;
    short width;
    short height;
    short twidth;
    short theight;
    int bitmap_data_length;
    int flags;
    struct xrdp_egfx_rect *d_rects;
    struct xrdp_egfx_rect *c_rects;
    struct xrdp_egfx_rect dst_rect;
    int error;
    struct stream ls;
    struct stream *s;
    short *crects;
    struct xrdp_enc_gfx_cmd *enc_gfx_cmd = &(enc->u.gfx);
    int mon_index;
    int connection_type;
    int shmem_offset;

    connection_type = self->mm->wm->client_info->mcs_connection_type;

    s = &ls;
    g_memset(s, 0, sizeof(struct stream));
    s->size = self->max_compressed_bytes;
    s->data = g_new(char, s->size);
    if (s->data == NULL)
    {
        return NULL;
    }
    s->p = s->data;
    if (!s_check_rem(in_s, 11))
    {
        g_free(s->data);
        return NULL;
    }
    in_uint16_le(in_s, surface_id);
    in_uint16_le(in_s, codec_id);
    in_uint8(in_s, pixel_format);
    in_uint32_le(in_s, flags);
    mon_index = (flags >> 28) & 0xF;
    in_uint16_le(in_s, num_rects_d);
    if ((num_rects_d < 1) || (num_rects_d > 16 * 1024) ||
            (!s_check_rem(in_s, num_rects_d * 8)))
    {
        g_free(s->data);
        return NULL;
    }
    d_rects = g_new0(struct xrdp_egfx_rect, num_rects_d);
    if (d_rects == NULL)
    {
        g_free(s->data);
        return NULL;
    }
    for (index = 0; index < num_rects_d; index++)
    {
        in_uint16_le(in_s, left);
        in_uint16_le(in_s, top);
        in_uint16_le(in_s, width);
        in_uint16_le(in_s, height);
        d_rects[index].x1 = left;
        d_rects[index].y1 = top;
        d_rects[index].x2 = left + width;
        d_rects[index].y2 = top + height;

    }
    if (!s_check_rem(in_s, 2))
    {
        g_free(s->data);
        g_free(d_rects);
        return NULL;
    }
    in_uint16_le(in_s, num_rects_c);
    if ((num_rects_c < 1) || (num_rects_c > 16 * 1024) ||
            (!s_check_rem(in_s, num_rects_c * 8)))
    {
        g_free(s->data);
        g_free(d_rects);
        return NULL;
    }
    c_rects = g_new0(struct xrdp_egfx_rect, num_rects_c);
    if (c_rects == NULL)
    {
        g_free(s->data);
        g_free(d_rects);
        return NULL;
    }
    crects = g_new(short, num_rects_c * 4);
    if (crects == NULL)
    {
        g_free(s->data);
        g_free(c_rects);
        g_free(d_rects);
        return NULL;
    }
    g_memcpy(crects, in_s->p, num_rects_c * 2 * 4);
    for (index = 0; index < num_rects_c; index++)
    {
        in_uint16_le(in_s, left);
        in_uint16_le(in_s, top);
        in_uint16_le(in_s, width);
        in_uint16_le(in_s, height);
        c_rects[index].x1 = left;
        c_rects[index].y1 = top;
        c_rects[index].x2 = left + width;
        c_rects[index].y2 = top + height;
    }
    if (!s_check_rem(in_s, 8))
    {
        g_free(s->data);
        g_free(c_rects);
        g_free(d_rects);
        g_free(crects);
        return NULL;
    }
    in_uint16_le(in_s, left);
    in_uint16_le(in_s, top);
    in_uint16_le(in_s, width);
    in_uint16_le(in_s, height);
    /* per-monitor capture shmem offset (multimon plane split); the field
     * is bounded by this command's cmd_bytes, absent means base 0 */
    shmem_offset = 0;
    if (s_check_rem(in_s, 4))
    {
        in_uint32_le(in_s, shmem_offset);
    }
    twidth = width;
    theight = height;
    dst_rect.x1 = 0;
    dst_rect.y1 = 0;
    dst_rect.x2 = width;
    dst_rect.y2 = height;
    LOG_DEVEL(LOG_LEVEL_INFO, "gfx_wiretosurface1: left %d top "
              "%d width %d height %d mon_index %d",
              left, top, width, height, mon_index);
    /* RFX_AVC420_METABLOCK */
    if (out_RFX_AVC420_METABLOCK(&dst_rect, s, d_rects, num_rects_d) != 0)
    {
        g_free(s->data);
        g_free(c_rects);
        g_free(d_rects);
        g_free(crects);
        LOG(LOG_LEVEL_INFO, "10");
        return NULL;
    }

    g_free(c_rects);
    g_free(d_rects);

    if (ENC_IS_BIT_SET(flags, 0))
    {
        /* already compressed */
        out_uint8a(s, enc_gfx_cmd->data, enc_gfx_cmd->data_bytes);
    }
    else
    {
        /* assume NV12 format */
        if (shmem_offset < 0 || shmem_offset > enc_gfx_cmd->data_bytes ||
                twidth * theight * 3 / 2 >
                enc_gfx_cmd->data_bytes - shmem_offset)
        {
            g_free(s->data);
            g_free(crects);
            return NULL;
        }
        bitmap_data_length = s_rem_out(s);
        if (self->codec_handle_h264_gfx[mon_index] == NULL)
        {
            self->codec_handle_h264_gfx[mon_index] =
                self->xrdp_encoder_h264_create();
            if (self->codec_handle_h264_gfx[mon_index] == NULL)
            {
                g_free(s->data);
                g_free(crects);
                return NULL;
            }
        }
        error = self->xrdp_encoder_h264_encode(
                    self->codec_handle_h264_gfx[mon_index], 0,
                    0, 0,
                    width, height, twidth, theight, 0,
                    enc_gfx_cmd->data + shmem_offset,
                    crects, num_rects_c,
                    s->p, &bitmap_data_length,
                    connection_type, NULL);
        if (error == 0)
        {
            xstream_seek(s, bitmap_data_length);
        }
        else
        {
            g_free(s->data);
            g_free(crects);
            return NULL;
        }
    }
    s_mark_end(s);
    bitmap_data_length = (int) (s->end - s->data);
    rv = xrdp_egfx_wire_to_surface1(bulk, surface_id,
                                    codec_id,
                                    pixel_format, &dst_rect,
                                    s->data, bitmap_data_length);
    g_free(s->data);
    g_free(crects);
    return rv;
#else
    (void)self;
    (void)bulk;
    (void)in_s;
    (void)enc;
    return NULL;
#endif
}

/*****************************************************************************/
static struct stream *
gfx_wiretosurface2(struct xrdp_encoder *self,
                   struct xrdp_egfx_bulk *bulk, struct stream *in_s,
                   XRDP_ENC_DATA *enc)
{
#ifdef XRDP_RFXCODEC
    int index;
    int surface_id;
    int codec_id;
    int codec_context_id;
    int pixel_format;
    int num_rects_d;
    int num_rects_c;
    struct stream *rv;
    short left;
    short top;
    short width;
    short height;
    char *bitmap_data;
    int bitmap_data_length;
    struct rfx_tile *tiles;
    struct rfx_rect *rfxrects;
    int tiles_compressed;
    int flags;
    int total_tiles;
    int tiles_written;
    int mon_index;

    if (!s_check_rem(in_s, 15))
    {
        return NULL;
    }
    in_uint16_le(in_s, surface_id);
    in_uint16_le(in_s, codec_id);
    in_uint32_le(in_s, codec_context_id);
    in_uint8(in_s, pixel_format);
    in_uint32_le(in_s, flags);
    mon_index = (flags >> 28) & 0xF;
    in_uint16_le(in_s, num_rects_d);
    if ((num_rects_d < 1) || (num_rects_d > 16 * 1024) ||
            (!s_check_rem(in_s, num_rects_d * 8)))
    {
        return NULL;
    }
    rfxrects = g_new0(struct rfx_rect, num_rects_d);
    if (rfxrects == NULL)
    {
        return NULL;
    }
    for (index = 0; index < num_rects_d; index++)
    {
        in_uint16_le(in_s, left);
        in_uint16_le(in_s, top);
        in_uint16_le(in_s, width);
        in_uint16_le(in_s, height);
        rfxrects[index].x = left;
        rfxrects[index].y = top;
        rfxrects[index].cx = width;
        rfxrects[index].cy = height;
    }
    if (!s_check_rem(in_s, 2))
    {
        g_free(rfxrects);
        return NULL;
    }
    in_uint16_le(in_s, num_rects_c);
    if ((num_rects_c < 1) || (num_rects_c > 16 * 1024) ||
            (!s_check_rem(in_s, num_rects_c * 8)))
    {
        g_free(rfxrects);
        return NULL;
    }
    tiles = g_new0(struct rfx_tile, num_rects_c);
    if (tiles == NULL)
    {
        g_free(rfxrects);
        return NULL;
    }
    for (index = 0; index < num_rects_c; index++)
    {
        in_uint16_le(in_s, left);
        in_uint16_le(in_s, top);
        in_uint16_le(in_s, width);
        in_uint16_le(in_s, height);
        tiles[index].x = left;
        tiles[index].y = top;
        tiles[index].cx = width;
        tiles[index].cy = height;
        tiles[index].quant_y = self->quant_idx_y;
        tiles[index].quant_cb = self->quant_idx_u;
        tiles[index].quant_cr = self->quant_idx_v;
    }
    if (!s_check_rem(in_s, 8))
    {
        g_free(tiles);
        g_free(rfxrects);
        return NULL;
    }
    in_uint16_le(in_s, left);
    in_uint16_le(in_s, top);
    in_uint16_le(in_s, width);
    in_uint16_le(in_s, height);
    LOG_DEVEL(LOG_LEVEL_INFO, "gfx_wiretosurface2: left %d top "
              "%d width %d height %d mon_index %d",
              left, top, width, height, mon_index);
    if (self->codec_handle_prfx_gfx[mon_index] == NULL)
    {
        self->codec_handle_prfx_gfx[mon_index] = rfxcodec_encode_create(
                    width,
                    height,
                    RFX_FORMAT_YUV,
                    RFX_FLAGS_RLGR1 | RFX_FLAGS_PRO1);
        if (self->codec_handle_prfx_gfx[mon_index] == NULL)
        {
            g_free(tiles);
            g_free(rfxrects);
            return NULL;
        }
    }
    bitmap_data_length = self->max_compressed_bytes;
    bitmap_data = g_new(char, bitmap_data_length);
    if (bitmap_data == NULL)
    {
        g_free(tiles);
        g_free(rfxrects);
        return NULL;
    }
    rv = NULL;
    tiles_written = 0;
    total_tiles = num_rects_c;
    for (;;)
    {
        tiles_compressed =
            rfxcodec_encode(self->codec_handle_prfx_gfx[mon_index],
                            bitmap_data,
                            &bitmap_data_length,
                            enc->u.gfx.data,
                            width, height,
                            ((width + 63) & ~63) * 4,
                            rfxrects, num_rects_d,
                            tiles + tiles_written, total_tiles - tiles_written,
                            self->quants, self->num_quants);
        if (tiles_compressed < 1)
        {
            break;
        }
        tiles_written += tiles_compressed;
        rv = xrdp_egfx_wire_to_surface2(bulk, surface_id,
                                        codec_id, codec_context_id,
                                        pixel_format,
                                        bitmap_data, bitmap_data_length);
        if (rv == NULL)
        {
            break;
        }
        LOG_DEVEL(LOG_LEVEL_INFO, "gfx_wiretosurface2: "
                  "tiles_compressed %d total_tiles %d tiles_written %d",
                  tiles_compressed, total_tiles,
                  tiles_written);
        if (tiles_written >= total_tiles)
        {
            /* ok, done with last tile set */
            break;
        }
        /* we have another tile set, send this one to main thread */
        if (gfx_send_done(self, enc, (int)(rv->end - rv->data), 0,
                          rv->data, 0, 0, 0) != 0)
        {
            free_stream(rv);
            rv = NULL;
            break;
        }
        g_free(rv); /* don't call free_stream() here so s->data is valid */
        rv = NULL;
        bitmap_data_length = self->max_compressed_bytes;
    }
    g_free(tiles);
    g_free(rfxrects);
    g_free(bitmap_data);
    return rv;
#else
    (void)self;
    (void)bulk;
    (void)in_s;
    (void)enc;
    return NULL;
#endif
}

/*****************************************************************************/
static struct stream *
gfx_solidfill(struct xrdp_encoder *self,
              struct xrdp_egfx_bulk *bulk, struct stream *in_s)
{
    int surface_id;
    int pixel;
    int num_rects;
    char *ptr8;
    struct xrdp_egfx_rect *rects;

    if (!s_check_rem(in_s, 8))
    {
        return NULL;
    }
    in_uint16_le(in_s, surface_id);
    in_uint32_le(in_s, pixel);
    in_uint16_le(in_s, num_rects);
    if (!s_check_rem(in_s, num_rects * 8))
    {
        return NULL;
    }
    in_uint8p(in_s, ptr8, num_rects * 8);
    rects = (struct xrdp_egfx_rect *) ptr8;
    return xrdp_egfx_fill_surface(bulk, surface_id, pixel, num_rects, rects);
}

/*****************************************************************************/
static struct stream *
gfx_surfacetosurface(struct xrdp_encoder *self,
                     struct xrdp_egfx_bulk *bulk, struct stream *in_s)
{
    int surface_id_src;
    int surface_id_dst;
    char *ptr8;
    int num_pts;
    struct xrdp_egfx_rect *rects;
    struct xrdp_egfx_point *pts;

    if (!s_check_rem(in_s, 14))
    {
        return NULL;
    }
    in_uint16_le(in_s, surface_id_src);
    in_uint16_le(in_s, surface_id_dst);
    in_uint8p(in_s, ptr8, 8);
    rects = (struct xrdp_egfx_rect *) ptr8;
    in_uint16_le(in_s, num_pts);
    if (!s_check_rem(in_s, num_pts * 4))
    {
        return NULL;
    }
    in_uint8p(in_s, ptr8, num_pts * 4);
    pts = (struct xrdp_egfx_point *) ptr8;
    return xrdp_egfx_surface_to_surface(bulk, surface_id_src, surface_id_dst,
                                        rects, num_pts, pts);
}

/*****************************************************************************/
static struct stream *
gfx_createsurface(struct xrdp_encoder *self,
                  struct xrdp_egfx_bulk *bulk, struct stream *in_s)
{
    int surface_id;
    int width;
    int height;
    int pixel_format;

    if (!s_check_rem(in_s, 7))
    {
        return NULL;
    }
    in_uint16_le(in_s, surface_id);
    in_uint16_le(in_s, width);
    in_uint16_le(in_s, height);
    in_uint8(in_s, pixel_format);
    return xrdp_egfx_create_surface(bulk, surface_id,
                                    width, height, pixel_format);
}

/*****************************************************************************/
static struct stream *
gfx_deletesurface(struct xrdp_encoder *self,
                  struct xrdp_egfx_bulk *bulk, struct stream *in_s)
{
    int surface_id;

    if (!s_check_rem(in_s, 2))
    {
        return NULL;
    }
    in_uint16_le(in_s, surface_id);
    return xrdp_egfx_delete_surface(bulk, surface_id);
}

/*****************************************************************************/
static struct stream *
gfx_startframe(struct xrdp_encoder *self,
               struct xrdp_egfx_bulk *bulk, struct stream *in_s,
               int *aframe_id)
{
    int frame_id;
    int time_stamp;

    if (!s_check_rem(in_s, 8))
    {
        return NULL;
    }
    in_uint32_le(in_s, frame_id);
    in_uint32_le(in_s, time_stamp);
    /* FR-ACK-1: the id is published to the caller HERE, not only at the
     * ENDFRAME, so a frame that fails before its ENDFRAME can still be
     * acked with its own echoed id (rule 2, totality) */
    *aframe_id = frame_id;
    return xrdp_egfx_frame_start(bulk, frame_id, time_stamp);
}

/*****************************************************************************/
static struct stream *
gfx_endframe(struct xrdp_encoder *self,
             struct xrdp_egfx_bulk *bulk, struct stream *in_s, int *aframe_id)
{
    int frame_id;

    if (!s_check_rem(in_s, 4))
    {
        return NULL;
    }
    in_uint32_le(in_s, frame_id);
    *aframe_id = frame_id;
    return xrdp_egfx_frame_end(bulk, frame_id);
}

/*****************************************************************************/
static struct stream *
gfx_resetgraphics(struct xrdp_encoder *self,
                  struct xrdp_egfx_bulk *bulk, struct stream *in_s)
{
    int width;
    int height;
    int monitor_count;
    int index;
    struct monitor_info *mi;
    struct stream *rv;

    if (!s_check_rem(in_s, 12))
    {
        return NULL;
    }
    in_uint32_le(in_s, width);
    in_uint32_le(in_s, height);
    in_uint32_le(in_s, monitor_count);
    if ((monitor_count < 1) || (monitor_count > 16) ||
            !s_check_rem(in_s, monitor_count * 20))
    {
        return NULL;
    }
    mi = g_new0(struct monitor_info, monitor_count);
    if (mi == NULL)
    {
        return NULL;
    }
    for (index = 0; index < monitor_count; index++)
    {
        in_uint32_le(in_s, mi[index].left);
        in_uint32_le(in_s, mi[index].top);
        in_uint32_le(in_s, mi[index].right);
        in_uint32_le(in_s, mi[index].bottom);
        in_uint32_le(in_s, mi[index].is_primary);
    }
    rv = xrdp_egfx_reset_graphics(bulk, width, height, monitor_count, mi);
    g_free(mi);
    return rv;
}

/*****************************************************************************/
static struct stream *
gfx_mapsurfacetooutput(struct xrdp_encoder *self,
                       struct xrdp_egfx_bulk *bulk, struct stream *in_s)
{
    int surface_id;
    int x;
    int y;

    if (!s_check_rem(in_s, 10))
    {
        return NULL;
    }
    in_uint16_le(in_s, surface_id);
    in_uint32_le(in_s, x);
    in_uint32_le(in_s, y);
    return xrdp_egfx_map_surface(bulk, surface_id, x, y);
}

/*****************************************************************************/
/* FR-ACK-1 rule 2, the single exhaustive exit of one received egfx paint
 * msg: if the msg carried a frame id, exactly one terminal ack is
 * emitted for THAT id, on every path out of process_enc_egfx -- the
 * normal end of the command stream, a malformed cmd_bytes, or a failed
 * enc_done allocation. There is no third terminal state short of
 * teardown, which is what Invariant I's exhaustiveness rests on.
 * A msg with no frame id (surface lifecycle only) owes no ack and its
 * last PDU keeps releasing the XRDP_ENC_DATA as before. */
static int
gfx_close_egfx_msg(struct xrdp_encoder *self, XRDP_ENC_DATA *enc,
                   int frame_id, int owe_ack, int displayed)
{
    if (!owe_ack)
    {
        return 0;
    }
    if (gfx_send_terminal_ack(self, enc, frame_id, displayed) != 0)
    {
        LOG(LOG_LEVEL_ERROR, "gfx_close_egfx_msg: terminal ack for frame "
            "id %d could not be queued", frame_id);
        return 1;
    }
    return 0;
}

/*****************************************************************************/
/* called from encoder thread */
static int
process_enc_egfx(struct xrdp_encoder *self, XRDP_ENC_DATA *enc)
{
    struct stream *s;
    struct stream in_s;
    struct xrdp_egfx_bulk *bulk;
    int cmd_id;
    int cmd_bytes;
    int frame_id;
    int got_frame_id;
    int error;
    char *holdp;
    char *holdend;
    /* FR-ACK-1: the echoed id of the frame this msg carries, and the
     * terminal state it reaches. owe_ack says a terminal ack is owed --
     * once it is set, the terminal enc_done is the msg's LAST one on
     * every exit path, including the error returns. */
    int term_frame_id;
    int owe_ack;
    int displayed;
    int handled;

    term_frame_id = 0;
    owe_ack = 0;
    displayed = 1;
    bulk = self->mm->egfx->bulk;
    g_memset(&in_s, 0, sizeof(in_s));
    in_s.data = enc->u.gfx.cmd;
    in_s.size = enc->u.gfx.cmd_bytes;
    in_s.p = in_s.data;
    in_s.end = in_s.data + in_s.size;
    while (s_check_rem(&in_s, 8))
    {
        s = NULL;
        frame_id = 0;
        got_frame_id = 0;
        handled = 1;
        holdp = in_s.p;
        in_uint16_le(&in_s, cmd_id);
        in_uint8s(&in_s, 2); /* flags */
        in_uint32_le(&in_s, cmd_bytes);
        if ((cmd_bytes < 8) || (cmd_bytes > 32 * 1024))
        {
            gfx_close_egfx_msg(self, enc, term_frame_id, owe_ack, 0);
            return 1;
        }
        holdend = in_s.end;
        in_s.end = holdp + cmd_bytes;
        LOG_DEVEL(LOG_LEVEL_INFO, "process_enc_egfx: cmd_id %d", cmd_id);
        switch (cmd_id)
        {
            case XR_RDPGFX_CMDID_WIRETOSURFACE_1:       /* 0x0001 */
                s = gfx_wiretosurface1(self, bulk, &in_s, enc);
                break;
            case XR_RDPGFX_CMDID_WIRETOSURFACE_2:       /* 0x0002 */
                s = gfx_wiretosurface2(self, bulk, &in_s, enc);
                break;
            case XR_RDPGFX_CMDID_SOLIDFILL:             /* 0x0004 */
                s = gfx_solidfill(self, bulk, &in_s);
                break;
            case XR_RDPGFX_CMDID_SURFACETOSURFACE:      /* 0x0005 */
                s = gfx_surfacetosurface(self, bulk, &in_s);
                break;
            case XR_RDPGFX_CMDID_CREATESURFACE:         /* 0x0009 */
                s = gfx_createsurface(self, bulk, &in_s);
                break;
            case XR_RDPGFX_CMDID_DELETESURFACE:         /* 0x000A */
                s = gfx_deletesurface(self, bulk, &in_s);
                break;
            case XR_RDPGFX_CMDID_STARTFRAME:            /* 0x000B */
                s = gfx_startframe(self, bulk, &in_s, &frame_id);
                got_frame_id = 1;
                break;
            case XR_RDPGFX_CMDID_ENDFRAME:              /* 0x000C */
                s = gfx_endframe(self, bulk, &in_s, &frame_id);
                got_frame_id = 1;
                break;
            case XR_RDPGFX_CMDID_RESETGRAPHICS:         /* 0x000E */
                s = gfx_resetgraphics(self, bulk, &in_s);
                break;
            case XR_RDPGFX_CMDID_MAPSURFACETOOUTPUT:    /* 0x000F */
                s = gfx_mapsurfacetooutput(self, bulk, &in_s);
                break;
            default:
                handled = 0;
                break;
        }
        /* setup for next cmd */
        in_s.p = holdp + cmd_bytes;
        in_s.end = holdend;
        if (got_frame_id)
        {
            /* FR-ACK-1 rule 1: the id this msg is acked with is COPIED
             * from the msg (the producer's rect_id, carried in both the
             * STARTFRAME and the ENDFRAME), never derived from a counter
             * kept on this side. The terminal ack is emitted once, after
             * the whole msg, by gfx_close_egfx_msg below. */
            term_frame_id = frame_id;
            owe_ack = 1;
        }
        if (s != NULL)
        {
            /* send message to main thread. The frame id never rides a
             * PDU any more: it belongs to the terminal ack, which is
             * also the msg's last enc_done once one is owed. */
            error = gfx_send_done(self, enc, (int) (s->end - s->data),
                                  0, s->data, 0, 0,
                                  !owe_ack && !s_check_rem(&in_s, 8));
            if (error != 0)
            {
                LOG(LOG_LEVEL_ERROR, "process_enc_egfx: gfx_send_done failed "
                    "error %d", error);
                free_stream(s);
                gfx_close_egfx_msg(self, enc, term_frame_id, owe_ack, 0);
                return 1;
            }
            g_free(s); /* don't call free_stream() here so s->data is valid */
        }
        else
        {
            LOG_DEVEL(LOG_LEVEL_INFO, "process_enc_egfx: nil");
            if (handled)
            {
                /* FR-ACK-1 rule 2(b): a command of this frame was
                 * consumed and produced no PDU -- the AVC444 warmup
                 * PENDING return, an encoder error, or a dropped pair.
                 * The frame is still acked, with displayed=0, so the
                 * producer frees the slot and takes the region back. */
                displayed = 0;
            }
        }
    }
    return gfx_close_egfx_msg(self, enc, term_frame_id, owe_ack, displayed);
}

/*****************************************************************************/
/* OPT-IN tail-flush for the external-ffmpeg AVC444/AVC420 backend, gated by
 * gfx.toml [avc444_ffmpeg] tail_flush (default off; see PRD s25). A deep
 * encoder pipeline (e.g. -async_depth N>1, or default frame-threading) holds
 * the last N-1 frame(s) of an idle-bounded burst until more input or EOF. The
 * root-cause fix is a shallow pipeline (-async_depth 1 / -tune zerolatency),
 * which is the shipped default and withholds nothing; this flush exists only
 * for encoders whose depth cannot be lowered. When enabled: after a real frame
 * we arm a short idle timer; on expiry we feed a BOUNDED number of duplicate
 * frames (the retained NV12) to push the withheld real frame out and emit it
 * once. Bounded and one-shot per idle burst, so idle never becomes a fixed-fps
 * duplicate stream. */

/**
 * Encoder thread main loop
 *****************************************************************************/
THREAD_RV THREAD_CC
proc_enc_msg(void *arg)
{
    XRDP_ENC_DATA *enc;
    struct fifo *fifo_to_proc;
    tbus mutex;
    tbus event_to_proc;
    tbus term_obj;
    tbus lterm_obj;
    int robjs_count;
    int wobjs_count;
    int cont;
    int timeout;
    tbus robjs[32];
    tbus wobjs[32];
    struct xrdp_encoder *self;
    /* #45 step 7: the items drained from the fifo in THIS cycle, plus
     * whatever an earlier cycle grouped but could not batch. common/fifo
     * has no peek and no push-front, so an item that must not join this
     * set cannot go back on the queue (re-adding at the tail would
     * reorder that monitor's own frames) -- it is CARRIED here instead. */
    XRDP_ENC_DATA *items[GFX_BATCH_MAX_ITEMS];
    XRDP_ENC_DATA *set[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int set_mon[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int n_items;
    int set_n;
    int consumed;
    int index;
    int batching;
    int drain_full;
    int pf_id;

    LOG_DEVEL(LOG_LEVEL_INFO, "proc_enc_msg: thread is running");

    self = (struct xrdp_encoder *) arg;
    if (self == 0)
    {
        LOG_DEVEL(LOG_LEVEL_DEBUG, "proc_enc_msg: self nil");
        return 0;
    }

    fifo_to_proc = self->fifo_to_proc;
    mutex = self->mutex;
    event_to_proc = self->xrdp_encoder_event_to_proc;

    term_obj = g_get_term();
    lterm_obj = self->xrdp_encoder_term_request;

    /* multimon batching is the aux_ltr_chain AVC444 architecture only
     * (that is the one the submit/pump/collect API drives); every other
     * path keeps the item-at-a-time loop it has today */
    batching = self->avc444_ffmpeg && self->avc444_aux_ltr_chain;
    n_items = 0;
    drain_full = 0;
    cont = 1;
    while (cont)
    {
        if (n_items == 0 && !drain_full)
        {
            /* nothing in hand: block for work exactly as before */
            timeout = -1;
            robjs_count = 0;
            wobjs_count = 0;
            robjs[robjs_count++] = term_obj;
            robjs[robjs_count++] = lterm_obj;
            robjs[robjs_count++] = event_to_proc;

            if (g_obj_wait(robjs, robjs_count, wobjs, wobjs_count,
                           timeout) != 0)
            {
                /* error, should not get here */
                g_sleep(100);
            }
        }
        /* THE STARVATION RULE: a cycle that ends holding carried items
         * must NOT block, or the monitor those items belong to would sit
         * unencoded until unrelated new damage arrives. With items in
         * hand we skip the wait entirely and process them immediately;
         * every iteration consumes at least the head item, so the carry
         * always drains. */

        if (g_is_wait_obj_set(term_obj)) /* global term */
        {
            LOG(LOG_LEVEL_DEBUG,
                "Received termination signal, stopping the encoder thread");
            break;
        }

        if (g_is_wait_obj_set(lterm_obj)) /* xrdp_mm term */
        {
            LOG_DEVEL(LOG_LEVEL_DEBUG, "proc_enc_msg: xrdp_mm term");
            break;
        }

        if (g_is_wait_obj_set(event_to_proc))
        {
            /* clear it right away */
            g_reset_wait_obj(event_to_proc);
        }
        /* NON-BLOCKING drain, taking the mutex once, decrementing the
         * depth exactly as before. If the bound is hit there may be
         * items still queued, and the wait object was already reset
         * above, so the next iteration MUST NOT block: drain_full says
         * so. (The batching branch happens to leave carry behind and so
         * never blocks anyway, but the non-batching branch consumes
         * everything it drained and would have blocked with work still
         * on the fifo until unrelated damage re-set the event.) */
        drain_full = 0;
        PERF_TRACE("drain_beg", n_items, 0);
        tc_mutex_lock(mutex);
        while (n_items < GFX_BATCH_MAX_ITEMS)
        {
            enc = (XRDP_ENC_DATA *) fifo_remove_item(fifo_to_proc);
            if (enc == 0)
            {
                break;
            }
            self->fifo_to_proc_depth--;
            items[n_items++] = enc;
        }
        drain_full = (n_items >= GFX_BATCH_MAX_ITEMS);
        tc_mutex_unlock(mutex);
        PERF_TRACE("drain_end", n_items, drain_full);
        if (n_items == 0)
        {
            continue;
        }
        if (!batching)
        {
            for (index = 0; index < n_items; index++)
            {
                self->process_enc(self, items[index]);
            }
            n_items = 0;
            continue;
        }
        consumed = gfx_egfx_batch_group(items, n_items, set, set_mon,
                                        &set_n);
        if (consumed < 1)
        {
            /* defensive: the grouping rule always takes the head item */
            LOG(LOG_LEVEL_ERROR, "proc_enc_msg: grouping consumed nothing "
                "from %d items; processing the head alone", n_items);
            set[0] = items[0];
            set_mon[0] = -1;
            set_n = 1;
            consumed = 1;
        }
        gfx_batch_run_set(self, set, set_mon, set_n);
        /* EMIT pass: the existing per-item processing, unchanged, in fifo
         * order, with this monitor's pair already collected */
        for (index = 0; index < set_n; index++)
        {
            /* the id is read only when the sink is armed: peeking costs
             * a bounds check and a few bytes, but a disarmed build must
             * pay exactly one branch */
            pf_id = perf_trace_on()
                    ? gfx_egfx_batch_peek_frame_id(set[index]->u.gfx.cmd,
                            set[index]->u.gfx.cmd_bytes)
                    : 0;
            PERF_TRACE("emit_beg", pf_id, set_mon[index]);
            self->process_enc(self, set[index]);
            PERF_TRACE("emit_end", pf_id, set_mon[index]);
        }
        for (index = 0; index < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS;
                index++)
        {
            self->avc444_batch_have[index] = 0;
        }
        /* keep the leftovers, IN ORDER, for the next iteration */
        n_items -= consumed;
        for (index = 0; index < n_items; index++)
        {
            items[index] = items[index + consumed];
        }

    } /* end while (cont) */
    /* carried items are indistinguishable from items still on the fifo at
     * teardown, so they are disposed of the same way (fifo_delete runs
     * this destructor over whatever is still queued) */
    for (index = 0; index < n_items; index++)
    {
        xrdp_enc_data_destructor(items[index], NULL);
    }
    g_set_wait_obj(self->xrdp_encoder_term_done);
    LOG_DEVEL(LOG_LEVEL_DEBUG, "proc_enc_msg: thread exit");
    return 0;
}
