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
 * Bounded, security-hardened NUT container demuxer (PRD section 8.7).
 *
 * This extracts H.264 access-unit packet boundaries and metadata from the
 * standard-syncpoint NUT stream produced by "ffmpeg -f nut -write_index 0".
 * It is NOT an H.264 decoder. All size/offset arithmetic derived from NUT
 * variable-length integers is overflow-checked and validated against
 * configured safety ceilings before any allocation or copy (PRD FR-NUT-7).
 * The stream is treated as untrusted even though the process is local.
 *
 * It implements the standard-syncpoint subset only; experimental
 * PIPE/no-syncpoint streams are unsupported and rejected.
 */

#ifndef _XRDP_NUT_H
#define _XRDP_NUT_H

#include <stddef.h>

enum xrdp_nut_event_type
{
    XRDP_NUT_NEED_MORE = 0,
    XRDP_NUT_STREAM_READY,
    XRDP_NUT_PACKET,
    XRDP_NUT_ERROR
};

struct xrdp_nut_packet
{
    int stream_id;
    long long pts;
    int keyframe;
    const unsigned char *data;  /* into the demuxer buffer; valid until the
                                 * next xrdp_nut_feed / xrdp_nut_next call    */
    int len;
};

struct xrdp_nut_ctx;

/**
 * Create a demuxer. The three ceilings bound header/metadata bytes, one
 * encoded picture packet, and the total buffered bytes respectively
 * (PRD FR-NUT-3). Pass 0 for a limit to use the MVP default.
 */
struct xrdp_nut_ctx *
xrdp_nut_create(size_t max_header_bytes, size_t max_picture_bytes,
                size_t max_total_bytes);

void
xrdp_nut_delete(struct xrdp_nut_ctx *self);

/**
 * Append raw child-stdout bytes. Returns 0 on success, -1 if the buffered
 * total would exceed the total ceiling (caller must reset the child).
 */
int
xrdp_nut_feed(struct xrdp_nut_ctx *self, const unsigned char *data, int len);

/**
 * Advance the parser. Returns one event:
 *   XRDP_NUT_NEED_MORE    - insufficient buffered bytes; feed more
 *   XRDP_NUT_STREAM_READY - main+stream headers parsed and validated (once)
 *   XRDP_NUT_PACKET       - *out filled with a video access unit
 *   XRDP_NUT_ERROR        - sticky structural/validation/CRC/bounds failure
 */
enum xrdp_nut_event_type
xrdp_nut_next(struct xrdp_nut_ctx *self, struct xrdp_nut_packet *out);

/** Diagnostic string for the last error (constant, never external data). */
const char *
xrdp_nut_error(struct xrdp_nut_ctx *self);

/** H.264 codec-specific extradata captured from the stream header, or NULL. */
const unsigned char *
xrdp_nut_extradata(struct xrdp_nut_ctx *self, int *len);

#endif /* _XRDP_NUT_H */
