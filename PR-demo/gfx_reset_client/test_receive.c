/* Exercise the patched channel parser with specification-derived packets. */
#include "build/rdpgfx_main.c"

static unsigned int fills;

static UINT
count_fill(RdpgfxClientContext *context,
           const RDPGFX_SOLID_FILL_PDU *fill)
{
    fills++;
    return CHANNEL_RC_OK;
}

static UINT
receive_bytes(GENERIC_CHANNEL_CALLBACK *callback,
              const BYTE *bytes, size_t count)
{
    wStream buffer = {0};
    wStream *stream;

    stream = Stream_StaticConstInit(&buffer, bytes, count);
    return rdpgfx_recv_pdu(callback, stream);
}

int
main(void)
{
    RDPGFX_PLUGIN gfx = {0};
    rdpContext rdp = {0};
    RdpgfxClientContext context = {0};
    GENERIC_CHANNEL_CALLBACK callback = {0};
    /* MS-RDPEGFX: 8-byte header, then surface, colour, rectangle count. */
    const BYTE fill[] = {4, 0, 0, 0, 16, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0
                        };
    /* Capability 10.7, four bytes of capability data. */
    const BYTE confirm[] = {19, 0, 0, 0, 20, 0, 0, 0,
                            1, 7, 10, 0, 4, 0, 0, 0, 2, 0, 0, 0
                           };
    const BYTE short_header[] = {4, 0, 0, 0, 7, 0, 0, 0};
    const BYTE truncated[] = {4, 0, 0, 0, 16, 0, 0, 0};

    gfx.log = WLog_Get(TAG);
    rdp.settings = freerdp_settings_new(0);
    if (rdp.settings == NULL)
    {
        return 5;
    }
    freerdp_settings_set_bool(rdp.settings, FreeRDP_BitmapCachePersistEnabled,
                              FALSE);
    gfx.rdpcontext = &rdp;
    gfx.context = &context;
    callback.plugin = (IWTSPlugin *)&gfx.base;
    context.SolidFill = count_fill;
    gfx.reset_waiting = TRUE;
    if (receive_bytes(&callback, fill, sizeof(fill)) != CHANNEL_RC_OK ||
            fills != 0 || gfx.reset_ignored != 1 || !gfx.reset_waiting)
    {
        return 1;
    }
    if (receive_bytes(&callback, short_header, sizeof(short_header)) ==
            CHANNEL_RC_OK ||
            receive_bytes(&callback, truncated, sizeof(truncated)) ==
            CHANNEL_RC_OK || gfx.reset_ignored != 1)
    {
        return 2;
    }
    if (receive_bytes(&callback, confirm, sizeof(confirm)) != CHANNEL_RC_OK ||
            gfx.reset_waiting || gfx.reset_confirms != 1 ||
            gfx.ConnectionCaps.version != RDPGFX_CAPVERSION_107)
    {
        return 3;
    }
    if (receive_bytes(&callback, fill, sizeof(fill)) != CHANNEL_RC_OK ||
            fills != 1 || gfx.reset_ignored != 1)
    {
        return 4;
    }
    puts("PASS: ignore before confirmation, resume after confirmation, "
         "reject invalid framing");
    freerdp_settings_free(rdp.settings);
    return 0;
}
