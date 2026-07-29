
#ifndef _XRDP_ENCODER_H
#define _XRDP_ENCODER_H

#include "arch.h"
#include "fifo.h"
#include "xrdp_client_info.h"
#include "xrdp_encoder_ffmpeg.h"

/* The aux_ltr_chain re-key (BACKLOG #48) builds its replacement surface
 * under base_id + this offset, alternating base <-> base+16 across
 * boundaries. 16 is the per-monitor array bound, so an alternate id can
 * never collide with another monitor's base id (ids are 0..monitorCount-1,
 * xrdp_mm.c:1171). */
#define XRDP_AVC444_SURFACE_ALT 16

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
    /* items currently in fifo_to_proc (mutex-guarded); the xorgxrdp
     * producer gate bounds it to the outstanding-rect budget for the
     * AVC444 two-slot capture, which is PER MONITOR (#45 D13), so the
     * legal bound is 2 * monitorCount (PRD FR-CAPTURE-8 assertion) */
    int fifo_to_proc_depth;
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
    int avc444_dump_extra; /* static gfx.toml policy, probe-verified    */
    int avc444_strip_sei;  /* strip BP/PT SEI NALs (macOS interop)      */
    int avc444_sanitize_hrd; /* drop SPS VUI HRD (macOS interop)        */
    int avc444_strip_pic_struct; /* clear VUI pic_struct flag (macOS)   */
    int avc444_fault_aux_delay; /* DIAGNOSTIC one-frame aux slip        */
    int avc444_fault_strip_mmco; /* DIAGNOSTIC MMCO -> sliding window   */
    int avc444_aux_ltr_chain;   /* EXPERIMENTAL FR-H264-8 LTR aux-chain  */
    int avc444_ltr_rekey_frame_num; /* re-key threshold (BACKLOG #48)    */
    int avc444_intra_refresh_frames; /* scheduled refresh (FR-H264-6)    */
    int avc444_ltr_rekey_surface_reset; /* 0 = mask the churn (#48)     */
    /* aux_ltr_chain re-key (BACKLOG #48): when the shared frame_num
     * counter hits the threshold the encoder pair is destroyed, and the
     * NEXT frame for that monitor rebuilds the client's decoder by
     * deleting and recreating its EGFX surface before repainting the
     * whole surface from the fresh IDR. Set in the encoder thread,
     * consumed by the encoder thread on the following frame. */
    int avc444_surface_reset_pending[16];
    /* The re-key builds the replacement surface under a DIFFERENT id and
     * only maps it once it holds pixels, so output is never mapped to a
     * blank surface (BACKLOG #48 RED 2026-07-29: mapping the freshly
     * created surface BEFORE this frame's pixels made macOS flash black
     * at every boundary. FreeRDP composites at END_FRAME so it never
     * showed it -- the fault is visible only to clients that present
     * when the mapping changes).
     *
     * -1 means "the id in the command is live"; otherwise this is the id
     * the client currently has for that monitor, and it REPLACES the id
     * xorgxrdp sends. Written by the encoder thread; read by the main
     * thread in xrdp_mm_egfx_delete_surfaces() under self->mutex, so a
     * resize tears down the surface that actually exists rather than the
     * base id it assumed. */
    int avc444_surface_id_live[16];
    /* EGFX surface layout cached at encoder-create time (main thread)
     * so the encoder thread can rebuild a surface without touching
     * wm/client_info concurrently. A resize deletes the encoder
     * (WMRZ_ENCODER_DELETE), so this cache cannot go stale. */
    int avc444_surface_x[16];
    int avc444_surface_y[16];
    int avc444_chroma_align; /* coded WIDTH alignment 16 or 32        */
    char avc444_path[256];
    struct xrdp_avc444_encoder_args avc444_encoder_args;
    unsigned long long avc444_seq;
    /* #45 step 7 -- per-CYCLE batch state, keyed by monitor index. The
     * worker submits every damaged monitor's pair, drives the whole set
     * through ONE pump, and collects every pair BEFORE the first PDU is
     * emitted; the emit pass then finds this monitor's pair already in
     * hand instead of encoding synchronously. have[] is 0 for "encode as
     * before", 1 for "pair[] holds this monitor's pair" and -1 for "this
     * monitor's pair failed in this cycle, ship nothing". Written and
     * read by the encoder thread only, and cleared at both ends of a
     * cycle so nothing can survive into the next one. */
    int avc444_batch_have[16];
    unsigned long long avc444_batch_seq[16];
    struct xrdp_avc444_encoded_pair avc444_batch_pair[16];
    /* E4 counters: E4 must be assertable from a deployed log, never
     * inferred from a wall-clock improvement */
    unsigned long long avc444_batch_cycles;
    unsigned long long avc444_batch_items;
    int avc444_batch_max_kids;
    int avc444_batch_e4_logged;
    void *avc444_ffmpeg_handle[16];  /* struct xrdp_ffmpeg_avc444 * */
    int avc444_actual_w[16];         /* per-surface visible dims for  */
    int avc444_actual_h[16];         /* resize detection (FR-RESIZE)  */
    /* tail-flush (OPT-IN last resort, gfx.toml tail_flush; default off): a deep
     * encoder pipeline (e.g. -async_depth > 1) withholds the last frame of an
     * idle-bounded burst until the next input. The root-cause fix is a shallow
     * pipeline (-async_depth 1 / -tune zerolatency); when that is impossible,
     * arm a short idle timer after a real frame and drain a bounded number of
     * duplicate frames to push the withheld frame out. */
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

struct xrdp_ffmpeg_avc444_config;
/* last hop of the gfx.toml plumbing: session policy -> runner config.
 * Exposed for the unit test that pins every field (BACKLOG #48/#50). */
void
xrdp_avc444_cfg_from_encoder(const struct xrdp_encoder *self,
                             struct xrdp_ffmpeg_avc444_config *cfg);

struct xrdp_encoder *
xrdp_encoder_create(struct xrdp_mm *mm);
void
xrdp_encoder_delete(struct xrdp_encoder *self);
THREAD_RV THREAD_CC
proc_enc_msg(void *arg);

/* #45 step 7 -- the two PURE halves of the multimon batching rule,
 * exposed for unit testing (nothing else in step 7 can be tested without
 * a live capture and two ffmpeg children).
 *
 * gfx_egfx_batch_peek_mon(): returns the monitor index 0..15 if and only
 * if the blob is EXACTLY the xorgxrdp AVC444 shape -- STARTFRAME (0x000B)
 * + WIRETOSURFACE_1 (0x0001, codec 0x000E/0x000F) + ENDFRAME (0x000C),
 * every length accounting for itself byte for byte -- and -1 otherwise.
 * The blob is client-influenced, so every field is length-checked before
 * it is read: an out-of-bounds read here would be client-triggerable.
 *
 * gfx_egfx_batch_group(): groups the head of a drained FIFO run into ONE
 * set holding AT MOST ONE item per monitor index. A second item for a
 * monitor already in the set belongs to that monitor's next frame and
 * ENDS the batch; a non-batchable item ENDS the batch, and if it is the
 * first item the set is that one item alone (processed exactly as before
 * this step). Returns the number of input items consumed, so the caller
 * knows what is left over. set[] and set_mon[] must hold at least
 * CLIENT_MONITOR_DATA_MAXIMUM_MONITORS (16) entries; set_mon[i] is -1 for
 * the single non-batchable item and the monitor index otherwise. */
int
gfx_egfx_batch_peek_mon(const char *cmd, int cmd_bytes);
int
gfx_egfx_batch_group(XRDP_ENC_DATA **in, int n_in,
                     XRDP_ENC_DATA **set, int *set_mon, int *set_n);

struct xrdp_egfx_rect;
struct stream;
/* Emit an RFX_AVC420_METABLOCK for the AVC420/AVC444 GFX paths. Exposed for
 * unit testing the even-alignment of the emitted region-rect origins. */
int
out_RFX_AVC420_METABLOCK(struct xrdp_egfx_rect *dst_rect,
                         struct stream *s,
                         struct xrdp_egfx_rect *rects,
                         int num_rects);

/* Serialize ONE view of an RFX_AVC444_BITMAP_STREAM as its own PDU body: the
 * avc420EncodedBitstreamInfo word (cbAvc420EncodedBitstream1 in bits 0..29, LC
 * in bits 30..31) followed by a single RFX_AVC420_BITMAP_STREAM (metablock +
 * Annex-B bitstream) for that view.
 *   lc == 1 (luma):   cb = len(metablock + bitstream); bitstream1 (main) present.
 *   lc == 2 (chroma): cb = 0; only bitstream2 (aux) present.
 * The AVC444 path emits an LC=1 luma PDU immediately followed by an LC=2 chroma
 * PDU within one GFX frame, matching a real Windows AVC444v2 server (luma-first
 * bootstrap, chroma deferred as an LC=2 P-slice). Exposed for unit testing. */
int
out_RFX_AVC444_BITMAP_STREAM_view(struct xrdp_egfx_rect *dst_rect,
                                  struct stream *s,
                                  struct xrdp_egfx_rect *d_rects, int num_rects,
                                  const unsigned char *view_data, int view_len,
                                  int lc);

#endif
