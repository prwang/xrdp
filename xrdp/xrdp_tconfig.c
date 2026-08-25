/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Koichiro Iwao
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
 */

/**
 *
 * @file xrdp_tconfig.c
 * @brief TOML config loader
 * @author Koichiro Iwao
 *
 */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch.h"
#include "os_calls.h"
#include "parse.h"
#include "toml.h"
#include "ms-rdpbcgr.h"
#include "xrdp_tconfig.h"
#include "xrdp_h264_annexb.h"
#include "string_calls.h"

#define TCLOG(log_level, args...) LOG(log_level, "TConfig: " args)

#define X264_DEFAULT_PRESET  "ultrafast"
#define X264_DEFAULT_TUNE    "zerolatency"
#define X264_DEFAULT_PROFILE "main"
#define X264_DEFAULT_FPS_NUM 24
#define X264_DEFAULT_FPS_DEN 1
#define X264_DEFAULT_THREADS 1 /* not to exhaust CPU threads for 1 user */

const char *
tconfig_codec_order_to_str(
    const struct xrdp_tconfig_gfx_codec_order *codec_order,
    char *buff,
    unsigned int bufflen)
{
    if (bufflen < (8 * codec_order->codec_count))
    {
        snprintf(buff, bufflen, "???");
    }
    else
    {
        unsigned int p = 0;
        int i;
        for (i = 0 ; i < codec_order->codec_count; ++i)
        {
            if (p > 0)
            {
                buff[p++] = ',';
                buff[p++] = ' ';
            }

            switch (codec_order->codecs[i])
            {
                case XTC_H264:
                    buff[p++] = 'H';
                    buff[p++] = '2';
                    buff[p++] = '6';
                    buff[p++] = '4';
                    break;

                case XTC_RFX:
                    buff[p++] = 'R';
                    buff[p++] = 'F';
                    buff[p++] = 'X';
                    break;

                default:
                    buff[p++] = '?';
                    buff[p++] = '?';
                    buff[p++] = '?';
            }
        }
        buff[p++] = '\0';
    }

    return buff;
}

static int
tconfig_load_gfx_openh264_ct(toml_table_t *tfile, const int connection_type,
                             struct xrdp_tconfig_gfx_openh264_param *param)
{
    TCLOG(LOG_LEVEL_TRACE, "[OpenH264]");

    if (connection_type > NUM_CONNECTION_TYPES)
    {
        TCLOG(LOG_LEVEL_ERROR, "[OpenH264] Invalid connection type is given");
        return 1;
    }

    toml_table_t *oh264 = toml_table_in(tfile, "OpenH264");
    if (!oh264)
    {
        TCLOG(LOG_LEVEL_WARNING, "[OpenH264] OpenH264 params are not defined");
        return 1;
    }

    toml_table_t *oh264_ct =
        toml_table_in(oh264, rdpbcgr_connection_type_names[connection_type]);
    toml_datum_t datum;

    if (!oh264_ct)
    {
        TCLOG(LOG_LEVEL_WARNING, "OpenH264 params for connection type [%s] is not defined",
              rdpbcgr_connection_type_names[connection_type]);
        return 1;
    }

    /* EnableFrameSkip */
    datum = toml_bool_in(oh264_ct, "EnableFrameSkip");
    if (datum.ok)
    {
        param[connection_type].EnableFrameSkip = datum.u.b;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[OpenH264.%s] EnableFrameSkip is not set, adopting the default value [0]",
              rdpbcgr_connection_type_names[connection_type]);
        param[connection_type].EnableFrameSkip = 0;
    }

    /* TargetBitrate */
    datum = toml_int_in(oh264_ct, "TargetBitrate");
    if (datum.ok)
    {
        param[connection_type].TargetBitrate = datum.u.i;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[OpenH264.%s] TargetBitrate is not set, adopting the default value [0]",
              rdpbcgr_connection_type_names[connection_type]);
        param[connection_type].TargetBitrate = 0;
    }

    /* MaxBitrate */
    datum = toml_int_in(oh264_ct, "MaxBitrate");
    if (datum.ok)
    {
        param[connection_type].MaxBitrate = datum.u.i;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[OpenH264.%s] MaxBitrate is not set, adopting the default value [0]",
              rdpbcgr_connection_type_names[connection_type]);
        param[connection_type].MaxBitrate = 0;
    }

    /* MaxFrameRate */
    datum = toml_double_in(oh264_ct, "MaxFrameRate");
    if (datum.ok)
    {
        param[connection_type].MaxFrameRate = (float)datum.u.d;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[OpenH264.%s] MaxFrameRate is not set, adopting the default value [0]",
              rdpbcgr_connection_type_names[connection_type]);
        param[connection_type].MaxFrameRate = 0;
    }

    return 0;
}

static int
tconfig_load_gfx_x264_ct(toml_table_t *tfile, const int connection_type,
                         struct xrdp_tconfig_gfx_x264_param *param)
{
    TCLOG(LOG_LEVEL_TRACE, "[x264]");

    if (connection_type > NUM_CONNECTION_TYPES)
    {
        TCLOG(LOG_LEVEL_ERROR, "[x264] Invalid connection type is given");
        return 1;
    }

    toml_table_t *x264 = toml_table_in(tfile, "x264");
    if (!x264)
    {
        TCLOG(LOG_LEVEL_WARNING, "[x264] x264 params are not defined");
        return 1;
    }

    toml_table_t *x264_ct =
        toml_table_in(x264, rdpbcgr_connection_type_names[connection_type]);
    toml_datum_t datum;

    if (!x264_ct)
    {
        TCLOG(LOG_LEVEL_WARNING, "x264 params for connection type [%s] is not defined",
              rdpbcgr_connection_type_names[connection_type]);
        return 1;
    }

    /* preset */
    datum = toml_string_in(x264_ct, "preset");
    if (datum.ok)
    {
        g_strncpy(param[connection_type].preset,
                  datum.u.s,
                  sizeof(param[connection_type].preset) - 1);
        free(datum.u.s);
        datum.u.s = NULL;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[x264.%s] preset is not set, adopting the default value \""
              X264_DEFAULT_PRESET "\"",
              rdpbcgr_connection_type_names[connection_type]);
        g_strncpy(param[connection_type].preset,
                  X264_DEFAULT_PRESET,
                  sizeof(param[connection_type].preset) - 1);
    }

    /* tune */
    datum = toml_string_in(x264_ct, "tune");
    if (datum.ok)
    {
        g_strncpy(param[connection_type].tune,
                  datum.u.s,
                  sizeof(param[connection_type].tune) - 1);
        free(datum.u.s);
        datum.u.s = NULL;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[x264.%s] tune is not set, adopting the default value \""
              X264_DEFAULT_TUNE"\"",
              rdpbcgr_connection_type_names[connection_type]);
        g_strncpy(param[connection_type].tune,
                  X264_DEFAULT_TUNE,
                  sizeof(param[connection_type].tune) - 1);
    }

    /* profile */
    datum = toml_string_in(x264_ct, "profile");
    if (datum.ok)
    {
        g_strncpy(param[connection_type].profile,
                  datum.u.s,
                  sizeof(param[connection_type].profile) - 1);
        free(datum.u.s);
        datum.u.s = NULL; // Prevent double-free warning with cppcheck 2.18.0
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[x264.%s] profile is not set, adopting the default value \""
              X264_DEFAULT_PROFILE"\"",
              rdpbcgr_connection_type_names[connection_type]);
        g_strncpy(param[connection_type].profile,
                  X264_DEFAULT_PROFILE,
                  sizeof(param[connection_type].profile) - 1);
    }

    /* vbv_max_bitrate */
    datum = toml_int_in(x264_ct, "vbv_max_bitrate");
    if (datum.ok)
    {
        param[connection_type].vbv_max_bitrate = datum.u.i;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[x264.%s] vbv_max_bitrate is not set, adopting the default value [0]",
              rdpbcgr_connection_type_names[connection_type]);
        param[connection_type].vbv_max_bitrate = 0;
    }

    /* vbv_buffer_size */
    datum = toml_int_in(x264_ct, "vbv_buffer_size");
    if (datum.ok)
    {
        param[connection_type].vbv_buffer_size = datum.u.i;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[x264.%s] vbv_buffer_size is not set, adopting the default value [0]",
              rdpbcgr_connection_type_names[connection_type]);
        param[connection_type].vbv_buffer_size = 0;
    }

    /* fps_num */
    datum = toml_int_in(x264_ct, "fps_num");
    if (datum.ok)
    {
        param[connection_type].fps_num = datum.u.i;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[x264.%s] fps_num is not set, adopting the default value [%d]",
              rdpbcgr_connection_type_names[connection_type],
              X264_DEFAULT_FPS_NUM);
        param[connection_type].fps_num = X264_DEFAULT_FPS_NUM;
    }

    /* fps_den */
    datum = toml_int_in(x264_ct, "fps_den");
    if (datum.ok)
    {
        param[connection_type].fps_den = datum.u.i;
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[x264.%s] fps_den is not set, adopting the default value [%d]",
              rdpbcgr_connection_type_names[connection_type],
              X264_DEFAULT_FPS_DEN);
        param[connection_type].fps_den = X264_DEFAULT_FPS_DEN;
    }

    /* threads */
    datum = toml_int_in(x264_ct, "threads");
    if (datum.ok)
    {
        if (datum.u.i >= 0)
        {
            param[connection_type].threads = datum.u.i;
        }
        else
        {
            TCLOG(LOG_LEVEL_WARNING,
                  "[x264.%s] an invalid value (< 0) is specified for threads, "
                  "adopting the default value [%d]",
                  rdpbcgr_connection_type_names[connection_type],
                  X264_DEFAULT_THREADS);
            param[connection_type].threads = X264_DEFAULT_THREADS;
        }
    }
    else if (connection_type == 0)
    {
        TCLOG(LOG_LEVEL_WARNING,
              "[x264.%s] threads is not set, adopting the default value [%d]",
              rdpbcgr_connection_type_names[connection_type],
              X264_DEFAULT_THREADS);
        param[connection_type].threads = X264_DEFAULT_THREADS;
    }

    return 0;
}

static void
disable_codec(struct xrdp_tconfig_gfx_codec_order *co,
              enum xrdp_tconfig_codecs code);

enum h264_encoder_load_status
{
    H264_ENCODER_LOAD_OK = 0,
    H264_ENCODER_LOAD_DEFAULTED,
    H264_ENCODER_LOAD_DISABLED
};

static int tconfig_load_gfx_h264_encoder(toml_table_t *tfile, struct xrdp_tconfig_gfx *config)
{
    TCLOG(LOG_LEVEL_TRACE, "[codec]");

    toml_table_t *codec;
    int valid_encoder_found = 0;

    if ((codec = toml_table_in(tfile, "codec")) != NULL)
    {
        toml_datum_t h264_encoder = toml_string_in(codec, "h264_encoder");

        if (h264_encoder.ok)
        {
            if (g_strcasecmp(h264_encoder.u.s, "x264") == 0)
            {
                TCLOG(LOG_LEVEL_DEBUG, "[codec] h264_encoder = x264");
                valid_encoder_found = 1;
                config->h264_encoder = XTC_H264_X264;
            }
            if (g_strcasecmp(h264_encoder.u.s, "OpenH264") == 0)
            {
                TCLOG(LOG_LEVEL_DEBUG, "[codec] h264_encoder = OpenH264");
                valid_encoder_found = 1;
                config->h264_encoder = XTC_H264_OPENH264;
            }
            if (g_strcasecmp(h264_encoder.u.s, "ffmpeg") == 0)
            {
                TCLOG(LOG_LEVEL_DEBUG, "[codec] h264_encoder = ffmpeg");
                valid_encoder_found = 1;
                config->h264_encoder = XTC_H264_FFMPEG;
            }

            free(h264_encoder.u.s);
        }
    }

    /* external stock-ffmpeg AVC444 backend: path plus a verbatim encoder-arg
     * passthrough (-c:v + tuning). xrdp does not enumerate individual flags;
     * see struct xrdp_avc444_encoder_args and the gfx.toml man page. */
    g_strncpy(config->avc444_ffmpeg_path, "/usr/bin/ffmpeg",
              sizeof(config->avc444_ffmpeg_path) - 1);
    xrdp_ffmpeg_avc444_default_encoder_args(
        &config->avc444_ffmpeg_encoder_args);
    config->avc444_ffmpeg_chroma_align = 32;
    config->avc444_ffmpeg_avc_mode = XTC_AVC_AUTO;
    config->avc444_ffmpeg_tail_flush = 0;
    config->avc444_ffmpeg_dump_extra = 0;
    config->avc444_ffmpeg_strip_sei = 0;
    config->avc444_ffmpeg_sanitize_hrd = 0;
    config->avc444_ffmpeg_strip_pic_struct = 0;
    config->avc444_ffmpeg_aux_ltr_chain = 0;
    config->avc444_ffmpeg_ltr_rekey_surface_reset = 0;
    config->avc444_ffmpeg_ltr_rekey_frame_num =
        XRDP_H264_LTR_FRAME_NUM_REKEY;
    config->avc444_ffmpeg_intra_refresh_frames =
        XRDP_H264_INTRA_REFRESH_FRAMES;
    config->avc444_ffmpeg_intra_refresh_frames_aux =
        XRDP_H264_INTRA_REFRESH_FRAMES_AUX;
    config->avc444_ffmpeg_fault_aux_delay = 0;
    config->avc444_ffmpeg_fault_strip_mmco = 0;
    /* Preserve the historical one-frame client window by default. A larger
     * deployment-specific value trades additional queued inventory and
     * display latency for more acknowledgement headroom. */
    config->avc444_ffmpeg_eager_slot_ack = 1;
    config->avc444_ffmpeg_wire_window = XRDP_GFX_WIRE_WINDOW_DEFAULT;
    /* BACKLOG #92: OFF by default -- the aux view is sent on every
     * frame, which is byte-for-byte today's behaviour. */
    config->avc444_ffmpeg_chroma_refresh_ms =
        XRDP_GFX_CHROMA_REFRESH_MS_DEFAULT;
    config->avc444_ffmpeg_chroma_idle_ms =
        XRDP_GFX_CHROMA_IDLE_MS_DEFAULT;
    {
        toml_table_t *avc = toml_table_in(tfile, "avc444_ffmpeg");
        if (avc != NULL)
        {
            if (toml_raw_in(avc, "tail_flush") != NULL ||
                    toml_raw_in(avc, "fault_aux_delay") != NULL ||
                    toml_raw_in(avc, "fault_strip_mmco") != NULL)
            {
                TCLOG(LOG_LEVEL_WARNING, "[avc444_ffmpeg] contains an "
                      "unsupported development-only setting; disabling "
                      "H.264");
                disable_codec(&config->codec, XTC_H264);
                return H264_ENCODER_LOAD_DISABLED;
            }
            toml_datum_t path = toml_string_in(avc, "path");
            toml_array_t *ea = toml_array_in(avc, "encoder_args");
            toml_datum_t ca = toml_int_in(avc, "chroma_align");
            toml_datum_t am = toml_string_in(avc, "avc_mode");
            toml_datum_t de = toml_bool_in(avc, "dump_extra");
            toml_datum_t ss = toml_bool_in(avc, "strip_sei");
            toml_datum_t sh = toml_bool_in(avc, "sanitize_hrd");
            toml_datum_t sp = toml_bool_in(avc, "strip_pic_struct");
            toml_datum_t lc = toml_bool_in(avc, "aux_ltr_chain");
            toml_datum_t rk = toml_int_in(avc, "ltr_rekey_frame_num");
            toml_datum_t ir = toml_int_in(avc, "intra_refresh_frames");
            toml_datum_t ia = toml_int_in(avc,
                                          "intra_refresh_frames_aux");
            toml_datum_t rs = toml_bool_in(avc,
                                           "ltr_rekey_surface_reset");
            toml_datum_t es = toml_bool_in(avc, "eager_slot_ack");
            toml_datum_t et = toml_bool_in(avc, "emit_thread");
            toml_datum_t ww = toml_int_in(avc, "wire_window");
            toml_datum_t cr = toml_int_in(avc, "chroma_refresh_ms");
            toml_datum_t ci = toml_int_in(avc, "chroma_idle_ms");
            if (es.ok)
            {
                config->avc444_ffmpeg_eager_slot_ack = es.u.b ? 1 : 0;
            }
            if (ww.ok)
            {
                /* same contract as the other bounded ints: out of range
                 * is REFUSED and the default stands, never clamped */
                if (ww.u.i < XRDP_GFX_WIRE_WINDOW_MIN ||
                        ww.u.i > XRDP_GFX_WIRE_WINDOW_MAX)
                {
                    TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg wire_window "
                          "%lld out of range [%d,%d]; keeping the default "
                          "%d", (long long)ww.u.i,
                          XRDP_GFX_WIRE_WINDOW_MIN,
                          XRDP_GFX_WIRE_WINDOW_MAX,
                          config->avc444_ffmpeg_wire_window);
                }
                else
                {
                    config->avc444_ffmpeg_wire_window = (int)ww.u.i;
                }
            }
            /* BACKLOG #92 / PRD FR-H264-9. Same contract as
             * wire_window: an out of range value is REFUSED with a log
             * line and the default stands, never silently clamped. The
             * default for both is 0, i.e. the feature is OFF and the
             * aux view is sent on every frame exactly as today. */
            if (cr.ok)
            {
                if (cr.u.i != 0 &&
                        (cr.u.i < XRDP_GFX_CHROMA_REFRESH_MS_MIN ||
                         cr.u.i > XRDP_GFX_CHROMA_REFRESH_MS_MAX))
                {
                    TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg "
                          "chroma_refresh_ms %lld out of range [%d,%d] "
                          "(0 disables); keeping the default %d",
                          (long long)cr.u.i,
                          XRDP_GFX_CHROMA_REFRESH_MS_MIN,
                          XRDP_GFX_CHROMA_REFRESH_MS_MAX,
                          config->avc444_ffmpeg_chroma_refresh_ms);
                }
                else
                {
                    config->avc444_ffmpeg_chroma_refresh_ms = (int)cr.u.i;
                }
            }
            if (ci.ok)
            {
                if (ci.u.i < XRDP_GFX_CHROMA_IDLE_MS_MIN ||
                        ci.u.i > XRDP_GFX_CHROMA_IDLE_MS_MAX)
                {
                    TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg "
                          "chroma_idle_ms %lld out of range [%d,%d]; "
                          "keeping the default %d", (long long)ci.u.i,
                          XRDP_GFX_CHROMA_IDLE_MS_MIN,
                          XRDP_GFX_CHROMA_IDLE_MS_MAX,
                          config->avc444_ffmpeg_chroma_idle_ms);
                }
                else
                {
                    config->avc444_ffmpeg_chroma_idle_ms = (int)ci.u.i;
                }
            }
            if (et.ok)
            {
                /* BACKLOG #100: the key is GONE and the EGFX assembly
                 * always runs inline on the encoder worker. Warned, not
                 * silently dropped: a deployment that set it true would
                 * otherwise keep believing assembly is threaded, and
                 * every other unhonourable value in this file (an out
                 * of range wire_window, a bad avc_mode) says so out
                 * loud. Parsing is unaffected -- an existing gfx.toml
                 * still loads, with this one line in the log. */
                TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg emit_thread was "
                      "removed: the EGFX assembly always "
                      "runs on the encoder worker. The key is ignored; "
                      "delete it from gfx.toml");
            }
            if (de.ok)
            {
                config->avc444_ffmpeg_dump_extra = de.u.b ? 1 : 0;
            }
            if (ss.ok)
            {
                config->avc444_ffmpeg_strip_sei = ss.u.b ? 1 : 0;
            }
            if (sh.ok)
            {
                config->avc444_ffmpeg_sanitize_hrd = sh.u.b ? 1 : 0;
            }
            if (sp.ok)
            {
                config->avc444_ffmpeg_strip_pic_struct = sp.u.b ? 1 : 0;
            }
            if (lc.ok)
            {
                config->avc444_ffmpeg_aux_ltr_chain = lc.u.b ? 1 : 0;
            }
            if (rs.ok)
            {
                config->avc444_ffmpeg_ltr_rekey_surface_reset =
                    rs.u.b ? 1 : 0;
                if (!rs.u.b)
                {
                    LOG(LOG_LEVEL_INFO, "TConfig: avc444_ffmpeg "
                        "ltr_rekey_surface_reset is OFF: the re-key "
                        "restarts the encoder (fresh IDR, counter reset) "
                        "without any EGFX surface lifecycle event");
                }
            }
            if (rk.ok)
            {
                /* out-of-range is REFUSED here (the default stands) so a
                 * typo cannot silently weaken the wrap guard; the runner
                 * clamps independently as a second line of defence */
                if (rk.u.i < XRDP_H264_LTR_FRAME_NUM_REKEY_MIN ||
                        rk.u.i > XRDP_H264_LTR_FRAME_NUM_REKEY_MAX)
                {
                    TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg "
                          "ltr_rekey_frame_num %lld out of range [%d,%d]; "
                          "keeping the default %d", (long long)rk.u.i,
                          XRDP_H264_LTR_FRAME_NUM_REKEY_MIN,
                          XRDP_H264_LTR_FRAME_NUM_REKEY_MAX,
                          config->avc444_ffmpeg_ltr_rekey_frame_num);
                }
                else
                {
                    config->avc444_ffmpeg_ltr_rekey_frame_num = (int)rk.u.i;
                    if (rk.u.i < XRDP_H264_LTR_FRAME_NUM_REKEY_MAX)
                    {
                        TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg "
                              "ltr_rekey_frame_num lowered to %lld: the "
                              "re-key boundary (surface reset + fresh IDR) "
                              "will fire far more often than in production "
                              "-- test arms only", (long long)rk.u.i);
                    }
                }
            }
            if (ir.ok)
            {
                /* same contract as ltr_rekey_frame_num: out-of-range is
                 * REFUSED here (the default stands) and the runner
                 * clamps independently */
                if (ir.u.i < XRDP_H264_INTRA_REFRESH_FRAMES_MIN ||
                        ir.u.i > XRDP_H264_INTRA_REFRESH_FRAMES_MAX)
                {
                    TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg "
                          "intra_refresh_frames %lld out of range "
                          "[%d,%d]; keeping the default %d",
                          (long long)ir.u.i,
                          XRDP_H264_INTRA_REFRESH_FRAMES_MIN,
                          XRDP_H264_INTRA_REFRESH_FRAMES_MAX,
                          config->avc444_ffmpeg_intra_refresh_frames);
                }
                else
                {
                    config->avc444_ffmpeg_intra_refresh_frames =
                        (int)ir.u.i;
                }
                if (!config->avc444_ffmpeg_aux_ltr_chain)
                {
                    TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg "
                          "intra_refresh_frames is set but aux_ltr_chain "
                          "is OFF: the scheduled refresh is inert");
                }
            }
            {
                /* the aux view's own interval, counted in AUX
                 * pictures. Same contract as the main one: out of
                 * range is REFUSED here and the runner clamps
                 * independently.
                 *
                 * ABSENT (or refused) means FOLLOW THE MAIN INTERVAL,
                 * not "take the shipped default". Every gfx.toml
                 * written before this key existed sets only
                 * intra_refresh_frames -- the fleet arms all say 240 --
                 * and pinning the aux view to 250 behind their backs
                 * would de-phase the two views on the 1:1 path, where
                 * a cut is required to land on the same picture
                 * ordinal in both. Following the main number keeps
                 * every existing table meaning exactly what it meant. */
                int aux_set = 0;

                if (ia.ok)
                {
                    if (ia.u.i < XRDP_H264_INTRA_REFRESH_FRAMES_MIN ||
                            ia.u.i > XRDP_H264_INTRA_REFRESH_FRAMES_MAX)
                    {
                        TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg "
                              "intra_refresh_frames_aux %lld out of range "
                              "[%d,%d]; following intra_refresh_frames "
                              "(%d) instead",
                              (long long)ia.u.i,
                              XRDP_H264_INTRA_REFRESH_FRAMES_MIN,
                              XRDP_H264_INTRA_REFRESH_FRAMES_MAX,
                              config->avc444_ffmpeg_intra_refresh_frames);
                    }
                    else
                    {
                        config->avc444_ffmpeg_intra_refresh_frames_aux =
                            (int)ia.u.i;
                        aux_set = 1;
                    }
                    if (!config->avc444_ffmpeg_aux_ltr_chain)
                    {
                        TCLOG(LOG_LEVEL_WARNING, "avc444_ffmpeg "
                              "intra_refresh_frames_aux is set but "
                              "aux_ltr_chain is OFF: the scheduled "
                              "refresh is inert");
                    }
                }
                if (!aux_set)
                {
                    config->avc444_ffmpeg_intra_refresh_frames_aux =
                        config->avc444_ffmpeg_intra_refresh_frames;
                }
            }
            if (am.ok)
            {
                if (g_strcasecmp(am.u.s, "auto") == 0)
                {
                    config->avc444_ffmpeg_avc_mode = XTC_AVC_AUTO;
                }
                else if (g_strcasecmp(am.u.s, "444") == 0)
                {
                    config->avc444_ffmpeg_avc_mode = XTC_AVC_FORCE_444;
                }
                else if (g_strcasecmp(am.u.s, "420") == 0)
                {
                    config->avc444_ffmpeg_avc_mode = XTC_AVC_FORCE_420;
                }
                else if (g_strcasecmp(am.u.s, "444v1") == 0)
                {
                    config->avc444_ffmpeg_avc_mode = XTC_AVC_FORCE_444V1;
                }
                else
                {
                    TCLOG(LOG_LEVEL_WARNING, "[avc444_ffmpeg] avc_mode must "
                          "be \"auto\", \"444\", \"444v1\" or \"420\", got "
                          "\"%s\"; using auto", am.u.s);
                }
                free(am.u.s);
            }
            if (ca.ok)
            {
                if (ca.u.i == 16 || ca.u.i == 32)
                {
                    config->avc444_ffmpeg_chroma_align = (int)ca.u.i;
                }
                else
                {
                    TCLOG(LOG_LEVEL_WARNING, "[avc444_ffmpeg] chroma_align must "
                          "be 16 or 32, got %lld; using 32",
                          (long long)ca.u.i);
                }
            }
            if (path.ok)
            {
                g_strncpy(config->avc444_ffmpeg_path, path.u.s,
                          sizeof(config->avc444_ffmpeg_path) - 1);
                free(path.u.s);
            }
            if (ea != NULL)
            {
                struct xrdp_avc444_encoder_args *dst =
                        &config->avc444_ffmpeg_encoder_args;
                int i;
                int nelem = toml_array_nelem(ea);
                memset(dst, 0, sizeof(*dst));
                for (i = 0; i < nelem &&
                        dst->count < XRDP_AVC444_MAX_ENC_ARGS; i++)
                {
                    toml_datum_t tok = toml_string_at(ea, i);
                    if (tok.ok)
                    {
                        g_strncpy(dst->arg[dst->count], tok.u.s,
                                  XRDP_AVC444_ENC_ARG_LEN - 1);
                        dst->count++;
                        free(tok.u.s);
                    }
                }
                if (i < nelem)
                {
                    TCLOG(LOG_LEVEL_WARNING, "[avc444_ffmpeg] encoder_args "
                          "truncated to %d tokens", XRDP_AVC444_MAX_ENC_ARGS);
                }
                /* an explicitly empty array would leave no encoder at all;
                 * fall back to the built-in default in that case */
                if (dst->count == 0)
                {
                    xrdp_ffmpeg_avc444_default_encoder_args(dst);
                }
            }
        }
    }

    if (valid_encoder_found == 0)
    {
        TCLOG(LOG_LEVEL_WARNING, "[codec] could not get valid H.264 encoder, "
              "using default \"x264\"");

        /* default to x264 */
        config->h264_encoder = XTC_H264_X264;
        return 1;
    }

    return 0;
}

static int tconfig_load_gfx_order(toml_table_t *tfile, struct xrdp_tconfig_gfx *config)
{
    char buff[64];

    /*
     * This config loader is not responsible to check if xrdp is built with
     * H264/RFX support. Just loads configurations as-is.
     */

    TCLOG(LOG_LEVEL_TRACE, "[codec]");

    int h264_found = 0;
    int rfx_found = 0;

    config->codec.codec_count = 0;

    toml_table_t *codec;
    toml_array_t *order;

    if ((codec = toml_table_in(tfile, "codec")) != NULL &&
            (order = toml_array_in(codec, "order")) != NULL)
    {
        for (int i = 0; ; i++)
        {
            toml_datum_t datum = toml_string_at(order, i);

            if (datum.ok)
            {
                if (h264_found == 0 &&
                        (g_strcasecmp(datum.u.s, "h264") == 0 ||
                         g_strcasecmp(datum.u.s, "h.264") == 0))
                {
                    h264_found = 1;
                    config->codec.codecs[config->codec.codec_count] = XTC_H264;
                    ++config->codec.codec_count;
                }
                if (rfx_found == 0 &&
                        g_strcasecmp(datum.u.s, "rfx") == 0)
                {
                    rfx_found = 1;
                    config->codec.codecs[config->codec.codec_count] = XTC_RFX;
                    ++config->codec.codec_count;
                }
                free(datum.u.s);
            }
            else
            {
                break;
            }
        }
    }

    if (h264_found == 0 && rfx_found == 0)
    {
        /* prefer H264 if no priority found */
        config->codec.codecs[0] = XTC_H264;
        config->codec.codecs[1] = XTC_RFX;
        config->codec.codec_count = 2;

        TCLOG(LOG_LEVEL_WARNING, "[codec] could not get GFX codec order, "
              "using default order %s",
              tconfig_codec_order_to_str(&config->codec, buff, sizeof(buff)));

        return 1;
    }

    TCLOG(LOG_LEVEL_DEBUG, "[codec] %s",
          tconfig_codec_order_to_str(&config->codec, buff, sizeof(buff)));
    return 0;
}

/**
 * Determines whether a codec is enabled
 * @param co Ordered codec list
 * @param code Code of codec to look for
 * @return boolean
 */
static int
codec_enabled(const struct xrdp_tconfig_gfx_codec_order *co,
              enum xrdp_tconfig_codecs code)
{
    for (unsigned short i = 0; i < co->codec_count; ++i)
    {
        if (co->codecs[i] == code)
        {
            return 1;
        }
    }

    return 0;
}

/**
 * Disables a Codec by removing it from the codec list
 * @param co Ordered codec list
 * @param code Code of codec to remove from list
 *
 * The order of the passed-in codec list is preserved.
 */
static void
disable_codec(struct xrdp_tconfig_gfx_codec_order *co,
              enum xrdp_tconfig_codecs code)
{
    unsigned short j = 0;
    for (unsigned short i = 0; i < co->codec_count; ++i)
    {
        if (co->codecs[i] != code)
        {
            co->codecs[j++] = co->codecs[i];
        }
    }
    co->codec_count = j;
}

int
tconfig_load_gfx(const char *filename, struct xrdp_tconfig_gfx *config)
{
    FILE *fp;
    char errbuf[200];
    toml_table_t *tfile;
    int rv = 0;

    /* Default to just RFX support. in case we can't load anything */
    config->codec.codec_count = 1;
    config->codec.codecs[0] = XTC_RFX;
    memset(config->x264_param, 0, sizeof(config->x264_param));

    if ((fp = fopen(filename, "r")) == NULL)
    {
        TCLOG(LOG_LEVEL_ERROR, "Error loading GFX config file %s (%s)",
              filename, g_get_strerror());
        return 1;
    }

    if ((tfile = toml_parse_file(fp, errbuf, sizeof(errbuf))) == NULL)
    {
        TCLOG(LOG_LEVEL_ERROR, "Error in GFX config file %s - %s", filename, errbuf);
        fclose(fp);
        return 1;
    }

    TCLOG(LOG_LEVEL_INFO, "Loading GFX config file %s", filename);
    fclose(fp);

    /* Load GFX codec order */
    tconfig_load_gfx_order(tfile, config);
    /* Load H.264 encoder */
    if (tconfig_load_gfx_h264_encoder(tfile, config) ==
            H264_ENCODER_LOAD_DISABLED)
    {
        rv = 1;
    }

    /* H.264 configuration */
    if (codec_enabled(&config->codec, XTC_H264))
    {
        /* First of all, read the default params */
        int x264_loaded;
        int oh264_loaded;

        x264_loaded = tconfig_load_gfx_x264_ct(tfile, 0, config->x264_param);
        oh264_loaded = tconfig_load_gfx_openh264_ct(tfile, 0, config->openh264_param);

        if (x264_loaded == 0)
        {
            /* Copy default params to other connection types, and
             * then override them */
            for (int ct = CONNECTION_TYPE_MODEM; ct < NUM_CONNECTION_TYPES;
                    ct++)
            {
                config->x264_param[ct] = config->x264_param[0];
                tconfig_load_gfx_x264_ct(tfile, ct, config->x264_param);
            }
        }

        if (oh264_loaded == 0)
        {
            /* Copy default params to other connection types, and
             * then override them */
            for (int ct = CONNECTION_TYPE_MODEM; ct < NUM_CONNECTION_TYPES;
                    ct++)
            {
                config->openh264_param[ct] = config->openh264_param[0];
                tconfig_load_gfx_openh264_ct(tfile, ct, config->openh264_param);
            }
        }

        if (x264_loaded != 0 && config->h264_encoder == XTC_H264_X264)
        {
            /* We can't get x264 defaults. Disable H.264. */
            TCLOG(LOG_LEVEL_WARNING, "x264 is selected as H.264 encoder but "
                  "cannot load default config for x264, disabling H.264");
            disable_codec(&config->codec, XTC_H264);
            rv = 1;
        }

        if (oh264_loaded != 0 && config->h264_encoder == XTC_H264_OPENH264)
        {
            /* We can't get OpenH264 defaults. Disable H.264. */
            TCLOG(LOG_LEVEL_WARNING, "OpenH264 is selected as H.264 encoder but "
                  "cannot load default config for OpenH264, disabling H.264");
            disable_codec(&config->codec, XTC_H264);
            rv = 1;
        }
    }
    toml_free(tfile);

    return rv;
}
