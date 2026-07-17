
#ifndef _XRDP_ENCODER_H
#define _XRDP_ENCODER_H

#include "arch.h"
#include "fifo.h"
#include "xrdp_client_info.h"
#include "xrdp_encoder_ffmpeg.h"

#define ENC_IS_BIT_SET(_flags, _bit) (((_flags) & (1 << (_bit))) != 0)
#define ENC_SET_BIT(_flags, _bit) do { _flags |= (1 << (_bit)); } while (0)
#define ENC_CLR_BIT(_flags, _bit) do { _flags &= ~(1 << (_bit)); } while (0)
#define ENC_SET_BITS(_flags, _mask, _bits) \
    do { _flags &= ~(_mask); _flags |= (_bits) & (_mask); } while (0)

struct xrdp_enc_data;

typedef void *(*xrdp_encoder_h264_create_proc)(void);
typedef int (*xrdp_encoder_h264_delete_proc)(void *handle);
typedef int (*xrdp_encoder_h264_encode_proc)(
    void *handle, int session, int left, int top,
    int width, int height, int twidth, int theight,
    int format, const char *data,
    short *crects, int num_crects,
    char *cdata, int *cdata_bytes,
    int connection_type, int *flags_ptr);

/* for codec mode operations */
struct xrdp_encoder
{
    struct xrdp_mm *mm;
    int in_codec_mode;
    int codec_id;
    int codec_quality;
    int max_compressed_bytes;
    tbus xrdp_encoder_event_to_proc;
    tbus xrdp_encoder_event_processed;
    tbus xrdp_encoder_term_request;
    tbus xrdp_encoder_term_done;
    struct fifo *fifo_to_proc;
    struct fifo *fifo_processed;
    tbus mutex;
    int (*process_enc)(struct xrdp_encoder *self, struct xrdp_enc_data *enc);
    void *codec_handle_rfx;
    void *codec_handle_jpg;
    void *codec_handle_h264;
    void *codec_handle_prfx_gfx[16];
    void *codec_handle_h264_gfx[16];
    /* external stock-ffmpeg AVC444 backend (opaque handles) */
    int avc444_ffmpeg;
    int avc420_ffmpeg; /* emit plain AVC420 (codec id 0x000B), single view */
    int avc444_v2;   /* emit AVC444 v2 (ChromaV2, codec id 0x000F)    */
    int avc444_chroma_align; /* coded WIDTH alignment 16 or 32        */
    char avc444_path[256];
    struct xrdp_avc444_encoder_args avc444_encoder_args;
    unsigned long long avc444_seq;
    void *avc444_ffmpeg_handle[16];  /* struct xrdp_ffmpeg_avc444 * */
    void *avc444_conv[16];           /* struct xrdp_avc444_conv *    */
    int frame_id_client; /* last frame id received from client */
    int frame_id_server; /* last frame id received from Xorg */
    int frame_id_server_sent;
    int frames_in_flight;
    int gfx;
    int gfx_ack_off;
    const char *quants;
    int num_quants;
    int quant_idx_y;
    int quant_idx_u;
    int quant_idx_v;
    int pad0;
    xrdp_encoder_h264_create_proc xrdp_encoder_h264_create;
    xrdp_encoder_h264_delete_proc xrdp_encoder_h264_delete;
    xrdp_encoder_h264_encode_proc xrdp_encoder_h264_encode;
};

/* cmd_id = 0 */
struct xrdp_enc_surface_command
{
    struct xrdp_mod *mod;
    int num_drects;
    int pad0;
    short *drects;  /* 4 * num_drects */
    int num_crects;
    int pad1;
    short *crects;  /* 4 * num_crects */
    char *data;
    int left;
    int top;
    int width;
    int height;
    int flags;
    int frame_id;
};

struct xrdp_enc_gfx_cmd
{
    char *cmd;
    char *data;
    int cmd_bytes;
    int data_bytes;
};

typedef struct xrdp_enc_data XRDP_ENC_DATA;

#define ENC_DONE_FLAGS_GFX_BIT      0
#define ENC_DONE_FLAGS_FRAME_ID_BIT 1

/* used when scheduling tasks from xrdp_encoder.c */
struct xrdp_enc_data_done
{
    int comp_bytes;
    int pad_bytes;
    char *comp_pad_data;
    struct xrdp_enc_data *enc;
    int last; /* true is this is last message for enc */
    int continuation; /* true if this isn't the start of a frame */
    int x;
    int y;
    int cx;
    int cy;
    int flags; /* ENC_DONE_FLAGS_* */
    int frame_id;
};

#define ENC_FLAGS_GFX_BIT   0

/* used when scheduling tasks in xrdp_encoder.c */
struct xrdp_enc_data
{
    struct xrdp_mod *mod;
    int flags; /* ENC_FLAGS_* */
    int pad0;
    void *shmem_ptr;
    int shmem_bytes;
    int pad1;
    union _u
    {
        struct xrdp_enc_surface_command sc;
        struct xrdp_enc_gfx_cmd gfx;
    } u;
};

typedef struct xrdp_enc_data_done XRDP_ENC_DATA_DONE;

struct xrdp_encoder *
xrdp_encoder_create(struct xrdp_mm *mm);
void
xrdp_encoder_delete(struct xrdp_encoder *self);
THREAD_RV THREAD_CC
proc_enc_msg(void *arg);

struct xrdp_egfx_rect;
struct stream;
/* Emit an RFX_AVC420_METABLOCK for the AVC420/AVC444 GFX paths. Exposed for
 * reuse by the external ffmpeg backend and for unit testing. */
int
out_RFX_AVC420_METABLOCK(struct xrdp_egfx_rect *dst_rect,
                         struct stream *s,
                         struct xrdp_egfx_rect *rects,
                         int num_rects);

#endif
