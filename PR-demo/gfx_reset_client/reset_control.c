/* Test-only control, included beside the pinned FreeRDP channel callbacks. */
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/channels/channels.h>
#include <winpr/handle.h>

static UINT
controlled_reset(RDPGFX_PLUGIN *gfx)
{
    UINT status;
    rdpGdi *gdi;
    RdpgfxClientContext *context;
    UINT32 flags;

    context = gfx->context;
    gdi = gfx->rdpcontext->gdi;
    if (gfx->reset_waiting || gfx->TotalDecodedFrames == 0 ||
            gfx->ConnectionCaps.version < RDPGFX_CAPVERSION_103 ||
            gfx->ConnectionCaps.version > RDPGFX_CAPVERSION_107 ||
            gdi == NULL || context->custom != gdi)
    {
        return ERROR_NOT_READY;
    }
    /* The receive path shares control_lock. No PDU can observe half-reset
     * state or be acknowledged after the reset request but before clearing. */
    gfx->reset_waiting = TRUE;
    status = rdpgfx_send_supported_caps(
                 gfx->base.listener_callback->channel_callback);
    if (status != CHANNEL_RC_OK)
    {
        return status;
    }
    EnterCriticalSection(&context->mux);
    free_surfaces(context, gfx->SurfaceTable);
    evict_cache_slots(context, gfx->MaxCacheSlots, gfx->CacheSlots);
    gfx->UnacknowledgedFrames = 0;
    gfx->TotalDecodedFrames = 0;
    gfx->StartDecodingTime = 0;
    gdi->inGfxFrame = FALSE;
    gdi->frameId = 0;
    flags = freerdp_settings_get_codecs_flags(gfx->rdpcontext->settings);
    if (!freerdp_client_codecs_reset(context->codecs, flags,
                                     gdi->width, gdi->height) ||
            !freerdp_client_codecs_reset(gfx->rdpcontext->codecs, flags,
                                         gdi->width, gdi->height))
    {
        status = ERROR_INTERNAL_ERROR;
    }
    LeaveCriticalSection(&context->mux);
    gfx->reset_requests++;
    WLog_INFO(TAG, "CONTROL reset sent: request=%u status=%u",
              gfx->reset_requests, status);
    return status;
}

/* Runs on the FreeRDP protocol loop; DVC receive processing is serialized
 * with this callback by control_lock. The socket is private and nonblocking. */
static BOOL
control_dispatch(rdpContext *rdp, void *arg)
{
    RDPGFX_PLUGIN *gfx;
    struct sockaddr_un peer;
    socklen_t peer_size;
    char command[16];
    char reply[256];
    ssize_t length;
    UINT status;

    gfx = arg;
    peer_size = sizeof(peer);
    length = recvfrom(gfx->control_fd, command, sizeof(command), 0,
                      (struct sockaddr *)&peer, &peer_size);
    if (length < 0)
    {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
    pthread_mutex_lock(&gfx->control_lock);
    status = ERROR_BAD_ARGUMENTS;
    if (length == 6 && memcmp(command, "reset\n", 6) == 0)
    {
        status = controlled_reset(gfx);
    }
    else if (length == 7 && memcmp(command, "status\n", 7) == 0)
    {
        status = CHANNEL_RC_OK;
    }
    snprintf(reply, sizeof(reply),
             "status=%u requests=%u confirms=%u waiting=%d "
             "decoded=%u ignored=%u version=0x%08x\n",
             status, gfx->reset_requests, gfx->reset_confirms,
             gfx->reset_waiting, gfx->TotalDecodedFrames,
             gfx->reset_ignored, gfx->ConnectionCaps.version);
    pthread_mutex_unlock(&gfx->control_lock);
    sendto(gfx->control_fd, reply, strlen(reply), MSG_NOSIGNAL,
           (struct sockaddr *)&peer, peer_size);
    return status == CHANNEL_RC_OK || status == ERROR_NOT_READY ||
           status == ERROR_BAD_ARGUMENTS;
}

static UINT
controlled_open(IWTSVirtualChannelCallback *channel)
{
    GENERIC_CHANNEL_CALLBACK *callback;
    RDPGFX_PLUGIN *gfx;
    struct sockaddr_un address;
    struct stat st;
    const char *directory;
    UINT status;

    callback = (GENERIC_CHANNEL_CALLBACK *)channel;
    gfx = (RDPGFX_PLUGIN *)callback->plugin;
    directory = getenv("XRDP_GFX_CONTROL_DIR");
    if (directory == NULL || lstat(directory, &st) != 0 ||
            !S_ISDIR(st.st_mode) || st.st_uid != geteuid() ||
            (st.st_mode & 077) != 0 ||
            strlen(directory) + 13 >= sizeof(address.sun_path))
    {
        return ERROR_BAD_ARGUMENTS;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path),
             "%s/control.sock", directory);
    gfx->control_fd = socket(AF_UNIX,
                             SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (gfx->control_fd < 0)
    {
        return ERROR_INTERNAL_ERROR;
    }
    if (bind(gfx->control_fd, (struct sockaddr *)&address,
             sizeof(address)) != 0)
    {
        close(gfx->control_fd);
        return ERROR_INTERNAL_ERROR;
    }
    snprintf(gfx->control_path, sizeof(gfx->control_path),
             "%s", address.sun_path);
    gfx->control_event = CreateFileDescriptorEvent(NULL, TRUE, FALSE,
                         gfx->control_fd,
                         WINPR_FD_READ);
    if (gfx->control_event == NULL ||
            pthread_mutex_init(&gfx->control_lock, NULL) != 0)
    {
        if (gfx->control_event != NULL)
        {
            CloseHandle(gfx->control_event);
        }
        close(gfx->control_fd);
        unlink(gfx->control_path);
        return ERROR_INTERNAL_ERROR;
    }
    gfx->control_started = TRUE;
    status = rdpgfx_on_open(channel);
    if (status == CHANNEL_RC_OK &&
            !freerdp_client_channel_register(gfx->rdpcontext->channels,
                    gfx->control_event,
                    control_dispatch, gfx))
    {
        status = ERROR_INTERNAL_ERROR;
    }
    return status;
}

static UINT
controlled_receive(IWTSVirtualChannelCallback *channel, wStream *data)
{
    GENERIC_CHANNEL_CALLBACK *callback;
    RDPGFX_PLUGIN *gfx;
    UINT status;

    callback = (GENERIC_CHANNEL_CALLBACK *)channel;
    gfx = (RDPGFX_PLUGIN *)callback->plugin;
    pthread_mutex_lock(&gfx->control_lock);
    status = rdpgfx_on_data_received(channel, data);
    pthread_mutex_unlock(&gfx->control_lock);
    return status;
}

static UINT
controlled_close(IWTSVirtualChannelCallback *channel)
{
    GENERIC_CHANNEL_CALLBACK *callback;
    RDPGFX_PLUGIN *gfx;

    callback = (GENERIC_CHANNEL_CALLBACK *)channel;
    gfx = (RDPGFX_PLUGIN *)callback->plugin;
    if (gfx->control_started)
    {
        freerdp_client_channel_unregister(gfx->rdpcontext->channels,
                                          gfx->control_event);
        CloseHandle(gfx->control_event);
        close(gfx->control_fd);
        pthread_mutex_destroy(&gfx->control_lock);
        unlink(gfx->control_path);
        gfx->control_started = FALSE;
    }
    return rdpgfx_on_close(channel);
}
