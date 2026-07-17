/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2026
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
 * Bounded NUT container demuxer.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "xrdp_nut.h"
#include "os_calls.h"

/* NUT startcodes (64-bit big-endian), verified against stock ffmpeg output */
#define NUT_MAIN_STARTCODE      0x4E4D7A561F5F04ADULL
#define NUT_STREAM_STARTCODE    0x4E5311405BF2F9DBULL
#define NUT_SYNCPOINT_STARTCODE 0x4E4BE4ADEECA4569ULL
#define NUT_INDEX_STARTCODE     0x4E58DD672F23E64EULL
#define NUT_INFO_STARTCODE      0x4E49AB68B596BA78ULL

/* NUT frame flag bits */
#define NUT_FLAG_KEY        0x0001
#define NUT_FLAG_EOR        0x0002
#define NUT_FLAG_CODED_PTS  0x0008
#define NUT_FLAG_STREAM_ID  0x0010
#define NUT_FLAG_SIZE_MSB   0x0020
#define NUT_FLAG_CHECKSUM   0x0040
#define NUT_FLAG_RESERVED   0x0080
#define NUT_FLAG_SM_DATA    0x0100
#define NUT_FLAG_HEADER_IDX 0x0400
#define NUT_FLAG_MATCH_TIME 0x0800
#define NUT_FLAG_CODED      0x1000
#define NUT_FLAG_INVALID    0x2000

#define NUT_FILE_ID "nut/multimedia container"  /* 24 chars + trailing NUL */
#define NUT_FILE_ID_LEN 25

#define DEF_MAX_HEADER   (1024 * 1024)
#define DEF_MAX_PICTURE  (128 * 1024 * 1024)
#define DEF_MAX_TOTAL    (256 * 1024 * 1024)

#define NUT_HEADER_CHECKSUM_THRESHOLD 4096

struct nut_frame_code
{
    int flags;
    int stream_id;
    int data_size_mul;
    int data_size_lsb;
    long long pts_delta;
};

struct xrdp_nut_ctx
{
    unsigned char *buf;
    size_t buf_size;   /* allocated */
    size_t len;        /* valid bytes in buf */
    size_t pos;        /* consumed offset */

    size_t max_header;
    size_t max_picture;
    size_t max_total;

    int file_id_done;
    int main_done;
    int stream_done;
    int stream_ready_emitted;
    int error;
    const char *error_str;

    struct nut_frame_code fcode[256];
    int stream_count;
    int msb_pts_shift;
    long long last_pts;

    unsigned char *extradata;
    int extradata_len;
};

/* MSB-first CRC-32, polynomial 0x04C11DB7, init 0, no reflection/xorout */
static unsigned int g_crc_tab[256];
static int g_crc_init = 0;

/*****************************************************************************/
static void
nut_crc_build(void)
{
    int i;
    int k;
    unsigned int c;

    for (i = 0; i < 256; i++)
    {
        c = (unsigned int)i << 24;
        for (k = 0; k < 8; k++)
        {
            c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
        }
        g_crc_tab[i] = c;
    }
    g_crc_init = 1;
}

/*****************************************************************************/
static unsigned int
nut_crc32(const unsigned char *buf, size_t len)
{
    unsigned int crc = 0;
    size_t i;

    for (i = 0; i < len; i++)
    {
        crc = (crc << 8) ^ g_crc_tab[((crc >> 24) ^ buf[i]) & 0xff];
    }
    return crc;
}

/* ------------------------------------------------------------------------ */
/* bounded cursor over a byte range; reads past 'end' set underflow         */

struct cur
{
    const unsigned char *b;
    size_t end;     /* one past last readable byte */
    size_t pos;
    int underflow;
    int overflow;   /* a v decoded too many bytes */
};

/*****************************************************************************/
static unsigned int
cur_u8(struct cur *c)
{
    if (c->pos >= c->end)
    {
        c->underflow = 1;
        return 0;
    }
    return c->b[c->pos++];
}

/*****************************************************************************/
static unsigned long long
cur_u64be(struct cur *c)
{
    unsigned long long v = 0;
    int i;

    for (i = 0; i < 8; i++)
    {
        v = (v << 8) | cur_u8(c);
    }
    return v;
}

/*****************************************************************************/
static unsigned int
cur_u32be(struct cur *c)
{
    unsigned int v = 0;
    int i;

    for (i = 0; i < 4; i++)
    {
        v = (v << 8) | cur_u8(c);
    }
    return v;
}

/*****************************************************************************/
static unsigned long long
cur_v(struct cur *c)
{
    unsigned long long val = 0;
    int n = 0;
    unsigned int b;

    do
    {
        if (c->pos >= c->end)
        {
            c->underflow = 1;
            return 0;
        }
        if (++n > 10)
        {
            c->overflow = 1;
            return 0;
        }
        b = c->b[c->pos++];
        val = (val << 7) | (b & 0x7f);
    }
    while (b & 0x80);
    return val;
}

/*****************************************************************************/
static long long
cur_s(struct cur *c)
{
    unsigned long long t = cur_v(c);

    t++;
    if (t & 1)
    {
        return -(long long)(t >> 1);
    }
    return (long long)(t >> 1);
}

/*****************************************************************************/
static void
set_error(struct xrdp_nut_ctx *self, const char *msg)
{
    if (!self->error)
    {
        self->error = 1;
        self->error_str = msg;
    }
}

/*****************************************************************************/
struct xrdp_nut_ctx *
xrdp_nut_create(size_t max_header_bytes, size_t max_picture_bytes,
                size_t max_total_bytes)
{
    struct xrdp_nut_ctx *self;

    if (!g_crc_init)
    {
        nut_crc_build();
    }
    self = (struct xrdp_nut_ctx *)g_malloc(sizeof(*self), 1);
    if (self == NULL)
    {
        return NULL;
    }
    self->max_header = max_header_bytes ? max_header_bytes : DEF_MAX_HEADER;
    self->max_picture = max_picture_bytes ? max_picture_bytes : DEF_MAX_PICTURE;
    self->max_total = max_total_bytes ? max_total_bytes : DEF_MAX_TOTAL;
    self->error_str = "";
    self->msb_pts_shift = 0;
    self->last_pts = 0;
    return self;
}

/*****************************************************************************/
void
xrdp_nut_delete(struct xrdp_nut_ctx *self)
{
    if (self == NULL)
    {
        return;
    }
    g_free(self->buf);
    g_free(self->extradata);
    g_free(self);
}

/*****************************************************************************/
int
xrdp_nut_feed(struct xrdp_nut_ctx *self, const unsigned char *data, int len)
{
    size_t need;

    if (self == NULL || self->error || data == NULL || len <= 0)
    {
        return (self != NULL && self->error) ? -1 : 0;
    }
    need = self->len + (size_t)len;
    if (need > self->max_total)
    {
        set_error(self, "nut buffered bytes exceed total ceiling");
        return -1;
    }
    if (need > self->buf_size)
    {
        size_t ns = self->buf_size ? self->buf_size : 65536;
        unsigned char *nb;
        while (ns < need)
        {
            ns *= 2;
        }
        if (ns > self->max_total)
        {
            ns = self->max_total;
        }
        nb = (unsigned char *)realloc(self->buf, ns);
        if (nb == NULL)
        {
            set_error(self, "nut buffer allocation failed");
            return -1;
        }
        self->buf = nb;
        self->buf_size = ns;
    }
    memcpy(self->buf + self->len, data, (size_t)len);
    self->len += (size_t)len;
    return 0;
}

/*****************************************************************************/
/* compact consumed prefix to the front of the buffer                       */
static void
nut_compact(struct xrdp_nut_ctx *self)
{
    if (self->pos == 0)
    {
        return;
    }
    if (self->pos < self->len)
    {
        memmove(self->buf, self->buf + self->pos, self->len - self->pos);
    }
    self->len -= self->pos;
    self->pos = 0;
}

/*****************************************************************************/
/* parse the main header body [c->pos, c->end): build the frame_code table  */
static int
parse_main_header(struct xrdp_nut_ctx *self, struct cur *c)
{
    unsigned long long version;
    unsigned long long stream_count;
    unsigned long long time_base_count;
    unsigned long long i;
    int idx = 0;
    int group = 0;
    long long tmp_pts = 0;
    unsigned long long tmp_mul = 1;
    unsigned long long tmp_stream = 0;

    version = cur_v(c);
    if (version > 3)
    {
        cur_v(c); /* minor_version */
    }
    if (version < 2 || version > 4)
    {
        set_error(self, "nut unsupported version");
        return 1;
    }
    stream_count = cur_v(c);
    if (stream_count != 1)
    {
        set_error(self, "nut stream_count must be 1");
        return 1;
    }
    self->stream_count = (int)stream_count;
    cur_v(c); /* max_distance */
    time_base_count = cur_v(c);
    if (time_base_count > 256)
    {
        set_error(self, "nut time_base_count too large");
        return 1;
    }
    for (i = 0; i < time_base_count; i++)
    {
        cur_v(c); /* num */
        cur_v(c); /* den */
    }
    /* frame code table */
    while (idx < 256)
    {
        unsigned long long tmp_flag;
        unsigned long long tmp_fields;
        unsigned long long tmp_size = 0;
        unsigned long long count;
        int j;

        if (++group > 256)
        {
            set_error(self, "nut frame-code table too many groups");
            return 1;
        }
        tmp_flag = cur_v(c);
        tmp_fields = cur_v(c);
        if (tmp_fields > 0)
        {
            tmp_pts = cur_s(c);
        }
        if (tmp_fields > 1)
        {
            tmp_mul = cur_v(c);
        }
        if (tmp_fields > 2)
        {
            tmp_stream = cur_v(c);
        }
        if (tmp_fields > 3)
        {
            tmp_size = cur_v(c);
        }
        if (tmp_fields > 4)
        {
            cur_v(c); /* tmp_res */
        }
        if (tmp_fields > 5)
        {
            count = cur_v(c);
        }
        else
        {
            count = tmp_mul - tmp_size;
        }
        if (tmp_fields > 6)
        {
            cur_s(c); /* tmp_match */
        }
        if (tmp_fields > 7)
        {
            cur_v(c); /* tmp_head_idx */
        }
        for (j = 8; (unsigned long long)j < tmp_fields; j++)
        {
            cur_v(c); /* reserved */
        }
        if (c->underflow || c->overflow)
        {
            return 1;
        }
        if (tmp_mul > 16384 || tmp_stream >= 250 || tmp_size > 16384)
        {
            set_error(self, "nut frame-code field out of range");
            return 1;
        }
        if (count == 0 || count > 256)
        {
            set_error(self, "nut frame-code count out of range");
            return 1;
        }
        for (j = 0; (unsigned long long)j < count && idx < 256; j++, idx++)
        {
            if (idx == 'N')
            {
                self->fcode[idx].flags = NUT_FLAG_INVALID;
                j--;
                continue;
            }
            self->fcode[idx].flags = (int)tmp_flag;
            self->fcode[idx].stream_id = (int)tmp_stream;
            self->fcode[idx].data_size_mul = (int)tmp_mul;
            self->fcode[idx].data_size_lsb = (int)(tmp_size + j);
            self->fcode[idx].pts_delta = tmp_pts;
        }
    }
    /* header_count_minus1, elision headers, main_flags: skipped via the
     * caller seeking to next packet using forward_ptr */
    if (c->underflow || c->overflow)
    {
        return 1;
    }
    self->main_done = 1;
    return 0;
}

/*****************************************************************************/
/* parse stream header body [c->pos, c->end)                                */
static int
parse_stream_header(struct xrdp_nut_ctx *self, struct cur *c)
{
    unsigned long long stream_id;
    unsigned long long stream_class;
    unsigned long long fourcc_len;
    unsigned char fourcc[4];
    unsigned long long extra_len;
    unsigned int k;

    stream_id = cur_v(c);
    stream_class = cur_v(c);
    if (c->underflow || c->overflow)
    {
        return 1;
    }
    if (stream_id != 0)
    {
        set_error(self, "nut unexpected stream_id");
        return 1;
    }
    if (stream_class != 0)
    {
        set_error(self, "nut stream is not video");
        return 1;
    }
    fourcc_len = cur_v(c);
    if (fourcc_len != 4)
    {
        set_error(self, "nut unexpected fourcc length");
        return 1;
    }
    for (k = 0; k < 4; k++)
    {
        fourcc[k] = (unsigned char)cur_u8(c);
    }
    if (c->underflow || memcmp(fourcc, "H264", 4) != 0)
    {
        set_error(self, "nut codec is not H264");
        return 1;
    }
    cur_v(c);                       /* time_base_id */
    self->msb_pts_shift = (int)cur_v(c);
    cur_v(c);                       /* max_pts_distance */
    cur_v(c);                       /* decode_delay */
    cur_v(c);                       /* stream_flags */
    extra_len = cur_v(c);
    if (c->underflow || c->overflow)
    {
        return 1;
    }
    if (extra_len > self->max_header)
    {
        set_error(self, "nut extradata exceeds header ceiling");
        return 1;
    }
    if (extra_len > 0)
    {
        if (c->pos + extra_len > c->end)
        {
            set_error(self, "nut extradata truncated");
            return 1;
        }
        g_free(self->extradata);
        self->extradata = (unsigned char *)g_malloc((int)extra_len, 0);
        if (self->extradata == NULL)
        {
            set_error(self, "nut extradata allocation failed");
            return 1;
        }
        memcpy(self->extradata, c->b + c->pos, (size_t)extra_len);
        self->extradata_len = (int)extra_len;
        c->pos += extra_len;
    }
    if (self->msb_pts_shift < 0 || self->msb_pts_shift > 30)
    {
        set_error(self, "nut msb_pts_shift out of range");
        return 1;
    }
    self->stream_done = 1;
    return 0;
}

/*****************************************************************************/
/* handle a startcode-delimited packet at self->pos. returns:               */
/*   1  consumed a packet (advanced self->pos)                              */
/*   0  need more bytes                                                     */
/*  -1  error (self->error set)                                            */
static int
handle_startcode_packet(struct xrdp_nut_ctx *self)
{
    struct cur c;
    unsigned long long startcode;
    unsigned long long forward_ptr;
    size_t body_start;
    size_t next_packet;
    unsigned int stored_crc;
    unsigned int calc_crc;
    struct cur body;
    int is_syncpoint = 0;

    c.b = self->buf;
    c.end = self->len;
    c.pos = self->pos;
    c.underflow = 0;
    c.overflow = 0;

    startcode = cur_u64be(&c);
    forward_ptr = cur_v(&c);
    if (c.underflow)
    {
        return 0; /* need more to know packet length */
    }
    if (c.overflow)
    {
        set_error(self, "nut forward_ptr overflow");
        return -1;
    }
    if (forward_ptr > self->max_header && startcode != NUT_INDEX_STARTCODE)
    {
        set_error(self, "nut header forward_ptr exceeds ceiling");
        return -1;
    }
    if (forward_ptr > NUT_HEADER_CHECKSUM_THRESHOLD)
    {
        cur_u32be(&c); /* header_checksum, present for large packets */
        if (c.underflow)
        {
            return 0;
        }
    }
    body_start = c.pos;
    if (forward_ptr < 4)
    {
        set_error(self, "nut packet too small for checksum");
        return -1;
    }
    next_packet = body_start + (size_t)forward_ptr;
    if (next_packet > self->len)
    {
        return 0; /* whole packet not buffered yet */
    }
    /* footer CRC over [body_start, next_packet-4) */
    stored_crc = (unsigned int)self->buf[next_packet - 4] << 24 |
                 (unsigned int)self->buf[next_packet - 3] << 16 |
                 (unsigned int)self->buf[next_packet - 2] << 8 |
                 (unsigned int)self->buf[next_packet - 1];
    calc_crc = nut_crc32(self->buf + body_start,
                         (next_packet - 4) - body_start);
    if (calc_crc != stored_crc)
    {
        set_error(self, "nut header CRC mismatch");
        return -1;
    }
    body.b = self->buf;
    body.end = next_packet - 4;
    body.pos = body_start;
    body.underflow = 0;
    body.overflow = 0;

    switch (startcode)
    {
        case NUT_MAIN_STARTCODE:
            if (parse_main_header(self, &body) != 0)
            {
                if (!self->error)
                {
                    set_error(self, "nut main header parse error");
                }
                return -1;
            }
            break;
        case NUT_STREAM_STARTCODE:
            if (!self->main_done)
            {
                set_error(self, "nut stream header before main header");
                return -1;
            }
            if (parse_stream_header(self, &body) != 0)
            {
                if (!self->error)
                {
                    set_error(self, "nut stream header parse error");
                }
                return -1;
            }
            break;
        case NUT_SYNCPOINT_STARTCODE:
            is_syncpoint = 1;
            /* global_key_pts resets the timestamp reference */
            self->last_pts = 0;
            break;
        case NUT_INFO_STARTCODE:
        case NUT_INDEX_STARTCODE:
            break; /* ignored */
        default:
            set_error(self, "nut unknown startcode");
            return -1;
    }
    (void)is_syncpoint;
    self->pos = next_packet;
    return 1;
}

/*****************************************************************************/
/* handle a frame at self->pos. returns 1 packet ready / 0 need more /      */
/* -1 error. On success fills *out.                                         */
static int
handle_frame(struct xrdp_nut_ctx *self, struct xrdp_nut_packet *out)
{
    struct cur c;
    size_t frame_start;
    unsigned int frame_code;
    struct nut_frame_code *fc;
    int flags;
    int stream_id;
    long long pts;
    unsigned long long data_size_msb = 0;
    unsigned long long data_size;
    size_t payload_start;
    size_t payload_end;
    int has_coded_pts = 0;
    unsigned long long coded_pts = 0;

    c.b = self->buf;
    c.end = self->len;
    c.pos = self->pos;
    c.underflow = 0;
    c.overflow = 0;
    frame_start = c.pos;

    frame_code = cur_u8(&c);
    if (c.underflow)
    {
        return 0;
    }
    fc = &self->fcode[frame_code & 0xff];
    flags = fc->flags;
    if (flags & NUT_FLAG_INVALID)
    {
        set_error(self, "nut invalid frame code");
        return -1;
    }
    if (flags & NUT_FLAG_CODED)
    {
        unsigned long long coded_flags = cur_v(&c);
        flags = (int)((unsigned int)flags ^ (unsigned int)coded_flags);
    }
    if (flags & NUT_FLAG_STREAM_ID)
    {
        stream_id = (int)cur_v(&c);
    }
    else
    {
        stream_id = fc->stream_id;
    }
    if (flags & NUT_FLAG_CODED_PTS)
    {
        coded_pts = cur_v(&c);
        has_coded_pts = 1;
    }
    if (flags & NUT_FLAG_SIZE_MSB)
    {
        data_size_msb = cur_v(&c);
    }
    if (flags & NUT_FLAG_MATCH_TIME)
    {
        cur_s(&c);
    }
    if (flags & NUT_FLAG_HEADER_IDX)
    {
        cur_v(&c);
    }
    if (flags & NUT_FLAG_RESERVED)
    {
        unsigned long long rc = cur_v(&c);
        unsigned long long ri;
        if (rc > 256)
        {
            set_error(self, "nut frame reserved count too large");
            return -1;
        }
        for (ri = 0; ri < rc; ri++)
        {
            cur_v(&c);
        }
    }
    if (flags & NUT_FLAG_CHECKSUM)
    {
        cur_u32be(&c); /* validated below if fully present */
    }
    if (c.overflow)
    {
        set_error(self, "nut frame varint overflow");
        return -1;
    }
    if (c.underflow)
    {
        return 0;
    }
    if (stream_id != 0)
    {
        set_error(self, "nut frame wrong stream id");
        return -1;
    }
    /* data_size = lsb + msb * mul, overflow-checked before use */
    if (data_size_msb != 0 && (unsigned long long)fc->data_size_mul != 0 &&
            data_size_msb > (0xFFFFFFFFULL / (unsigned long long)fc->data_size_mul))
    {
        set_error(self, "nut frame data_size overflow");
        return -1;
    }
    data_size = (unsigned long long)fc->data_size_lsb +
                data_size_msb * (unsigned long long)fc->data_size_mul;
    if (data_size == 0 || data_size > self->max_picture)
    {
        set_error(self, "nut frame data_size out of range");
        return -1;
    }
    payload_start = c.pos;
    if (payload_start > self->len ||
            data_size > (unsigned long long)(self->len - payload_start))
    {
        return 0; /* payload not fully buffered */
    }
    payload_end = payload_start + (size_t)data_size;

    /* pts */
    if (has_coded_pts)
    {
        long long mask = ((long long)1 << self->msb_pts_shift);
        if ((long long)coded_pts < mask)
        {
            long long delta = self->last_pts - mask / 2;
            pts = (((long long)coded_pts - delta) & (mask - 1)) + delta;
        }
        else
        {
            pts = (long long)coded_pts - mask;
        }
    }
    else
    {
        pts = self->last_pts + fc->pts_delta;
    }
    self->last_pts = pts;

    out->stream_id = stream_id;
    out->pts = pts;
    out->keyframe = (flags & NUT_FLAG_KEY) ? 1 : 0;
    out->data = self->buf + payload_start;
    out->len = (int)data_size;

    (void)frame_start;
    self->pos = payload_end;
    return 1;
}

/*****************************************************************************/
enum xrdp_nut_event_type
xrdp_nut_next(struct xrdp_nut_ctx *self, struct xrdp_nut_packet *out)
{
    if (self == NULL || out == NULL)
    {
        return XRDP_NUT_ERROR;
    }
    if (self->error)
    {
        return XRDP_NUT_ERROR;
    }
    nut_compact(self);

    for (;;)
    {
        /* file identifier at the very start */
        if (!self->file_id_done)
        {
            if (self->len - self->pos < NUT_FILE_ID_LEN)
            {
                return XRDP_NUT_NEED_MORE;
            }
            if (memcmp(self->buf + self->pos, NUT_FILE_ID, NUT_FILE_ID_LEN)
                    != 0)
            {
                set_error(self, "nut bad file identifier");
                return XRDP_NUT_ERROR;
            }
            self->pos += NUT_FILE_ID_LEN;
            self->file_id_done = 1;
            continue;
        }
        /* emit STREAM_READY once, after headers validated */
        if (self->main_done && self->stream_done &&
                !self->stream_ready_emitted)
        {
            self->stream_ready_emitted = 1;
            return XRDP_NUT_STREAM_READY;
        }
        if (self->len - self->pos < 1)
        {
            return XRDP_NUT_NEED_MORE;
        }
        if (self->buf[self->pos] == 0x4E)
        {
            int rv;
            if (self->len - self->pos < 8)
            {
                return XRDP_NUT_NEED_MORE;
            }
            rv = handle_startcode_packet(self);
            if (rv < 0)
            {
                return XRDP_NUT_ERROR;
            }
            if (rv == 0)
            {
                return XRDP_NUT_NEED_MORE;
            }
            continue;
        }
        else
        {
            int rv;
            if (!self->stream_done)
            {
                set_error(self, "nut frame before stream header");
                return XRDP_NUT_ERROR;
            }
            rv = handle_frame(self, out);
            if (rv < 0)
            {
                return XRDP_NUT_ERROR;
            }
            if (rv == 0)
            {
                return XRDP_NUT_NEED_MORE;
            }
            return XRDP_NUT_PACKET;
        }
    }
}

/*****************************************************************************/
const char *
xrdp_nut_error(struct xrdp_nut_ctx *self)
{
    if (self == NULL)
    {
        return "null";
    }
    return self->error_str;
}

/*****************************************************************************/
const unsigned char *
xrdp_nut_extradata(struct xrdp_nut_ctx *self, int *len)
{
    if (self == NULL || self->extradata == NULL)
    {
        if (len != NULL)
        {
            *len = 0;
        }
        return NULL;
    }
    if (len != NULL)
    {
        *len = self->extradata_len;
    }
    return self->extradata;
}
