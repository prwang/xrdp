/* Offline serializer probe. No encoder, client or production state is run. */
#include "config_ac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xrdp.h"
#include "xrdp_encoder.h"
#include "xrdp_egfx.h"

static unsigned int
read_u16(const unsigned char *p)
{
    return p[0] | (unsigned int)p[1] << 8;
}

int
main(int argc, char **argv)
{
    int width;
    int height;
    const unsigned char *rect;
#ifdef CLEANROOM_PROBE
    const unsigned char byte = 0xaa;
    unsigned char output[64];
    struct xrdp_avc_region region;
    struct xrdp_ffmpeg_encoded_pair encoded;
    struct xrdp_avc_wire_result result;
#else
    struct stream *stream;
    struct xrdp_egfx_rect destination;
#endif

    if (argc != 3)
    {
        return 2;
    }
    width = atoi(argv[1]);
    height = atoi(argv[2]);
    if (width < 1 || width > 8192 || height < 1 || height > 8192)
    {
        return 2;
    }
#ifdef CLEANROOM_PROBE
    memset(&encoded, 0, sizeof(encoded));
    encoded.sequence = 1;
    encoded.main_data = &byte;
    encoded.main_bytes = 1;
    region.left = 0;
    region.top = 0;
    region.right = width;
    region.bottom = height;
    if (xrdp_encoder_serialize_avc(XRDP_AVC_444_V2, 1, 0,
                                   (width + 31) & ~31,
                                   (height + 15) & ~15, &region, 1,
                                   &encoded, output, sizeof(output),
                                   &result) != 0)
    {
        return 1;
    }
    rect = output + 8;
#else
    destination.x1 = 0;
    destination.y1 = 0;
    destination.x2 = width;
    destination.y2 = height;
    make_stream(stream);
    init_stream(stream, 64);
    if (out_RFX_AVC420_METABLOCK(&destination, stream, &destination, 1))
    {
        return 1;
    }
    rect = (const unsigned char *)stream->data + 4;
#endif
    printf("visible=%dx%d region=%u,%u-%u,%u exceeds_visible=%d\n",
           width, height, read_u16(rect), read_u16(rect + 2),
           read_u16(rect + 4), read_u16(rect + 6),
           read_u16(rect + 4) > (unsigned int)width ||
           read_u16(rect + 6) > (unsigned int)height);
#ifndef CLEANROOM_PROBE
    free_stream(stream);
#endif
    return 0;
}
