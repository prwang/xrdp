
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

/**
 * Is the GFX ack window open for one more frame?
 *
 * This is the xrdp HALF of the flow control that decides whether capture
 * of frame N+1 may overlap encode of frame N (PRD "Concurrency state of
 * the encode pipeline": capture || encode is YES for m = 1). xorgxrdp
 * owns the other half — the per-monitor xup_cap_budget in
 * common/xup_client_info.h — and a capture happens only when BOTH admit
 * it. xorgxrdp's rect_id_ack advances only when this predicate lets
 * mod_frame_ack through, so a window closed here pins the capture side
 * to one outstanding frame no matter how many slots it has.
 *
 * Extracted from the xrdp_mm_update_module_ack call site so the joint
 * state machine is unit-testable: overlap at m = 1 is a PRD requirement
 * and had no CI assertion until BACKLOG #64 (test_avc444_multimon.c).
 * Behaviour is unchanged — this is the same comparison, by name.
 *
 * @param frame_id_client  last frame id the client has acknowledged
 * @param frame_id_server  last frame id the server has sent
 * @param frames_in_flight window size (DEFAULT_XRDP_GFX_FRAMES_IN_FLIGHT)
 * @return != 0 if another frame may be admitted
 */
static inline int
xrdp_gfx_ack_window_open(int frame_id_client, int frame_id_server,
                         int frames_in_flight)
{
    return frame_id_client + frames_in_flight > frame_id_server;
}

/**
 * How many capture slots xorgxrdp holds per monitor for CC_GFX_AVC444.
 *
 * Mirrors XUP_CAP_AVC444_SLOT_COUNT (common/xup_client_info.h), repeated
 * here only so the wire bound below can be written down; the producer
 * remains the owner of the number.
 */
#define XRDP_GFX_CAPTURE_SLOTS 2

/**
 * BACKLOG #80 / PRD FR-FLOW-1 -- the CREDIT FRONTIER.
 *
 * xorgxrdp captures a frame only when xrdp has told it a frame id it may
 * overwrite; that ack is the single admission token for the whole
 * pipeline. This function is the arithmetic that produces the token's
 * value, and it is the ONLY place the three layers meet. Each term
 * consults exactly its own layer, and the smallest wins:
 *
 *   frame_id_consumed      SLOT FACT. The encoder children have drained
 *                          frame N's vmsplice'd input, so the borrowed
 *                          capture pages are free. Anything above this
 *                          would hand back pages a child is reading.
 *
 *   frame_id_server + 1    NEAREST-NEIGHBOUR BACKPRESSURE (FR-FLOW-1
 *                          clause 1). One frame of pipeline inventory,
 *                          no more: the stage downstream of capture is
 *                          allowed to be one frame behind, and the
 *                          producer waits for it. This is a stall, and
 *                          it consults only the immediately adjacent
 *                          stage -- never the network.
 *
 *   frame_id_client + C    END-TO-END WIRE WINDOW (FR-FLOW-1 clause 2).
 *                          The farthest consumer -- the RDP client --
 *                          throttling the nearest producer, at the one
 *                          place where refusing costs nothing: capture
 *                          admission. When this term binds, the producer
 *                          is NOT stalled holding a finished frame; it
 *                          simply does not capture, and xorgxrdp
 *                          coalesces the damage into its dirty region.
 *                          Fewer frames, each of them fresher.
 *
 * The critical property is that this value is emitted UNCONDITIONALLY.
 * The shipped code instead wraps the whole emission in
 * xrdp_gfx_ack_window_open() -- so when the client falls behind, the
 * SLOT credit is withheld too, and the producer stalls on a fact about
 * the network that has nothing to do with whether its pages are free.
 * That is the cross-layer stall FR-FLOW-1 clause 1 forbids, and it is
 * the defect BACKLOG #79 measured.
 *
 * Wire bound that follows, by induction on capture admission: a capture
 * is admitted only at ids at most XRDP_GFX_CAPTURE_SLOTS above the
 * credit (that is xorgxrdp's own budget), frame_id_client never
 * decreases, and frames reach the transport in id order. So at the
 * instant any frame is handed to the transport,
 *
 *     frame_id - frame_id_client <= C + XRDP_GFX_CAPTURE_SLOTS * M
 *
 * with M the monitor count (the budget is per monitor). Every frame
 * that exists is therefore already inside the window by the time it
 * reaches egress -- which is why there is no gate at egress and nothing
 * anywhere in the pipeline ever holds a completed frame.
 *
 * @param frame_id_consumed contiguous frontier of absorbed input
 * @param frame_id_server   last frame id handed to the transport
 * @param frame_id_client   last frame id the client acknowledged
 * @param wire_window       C, gfx.toml [avc444_ffmpeg] wire_window
 * @return the frame id the producer may be told about now
 */
static inline int
xrdp_gfx_credit_frontier(int frame_id_consumed, int frame_id_server,
                         int frame_id_client, int wire_window)
{
    int credit;

    credit = frame_id_consumed;
    if (credit > frame_id_server + 1)
    {
        credit = frame_id_server + 1;
    }
    if (credit > frame_id_client + wire_window)
    {
        credit = frame_id_client + wire_window;
    }
    return credit;
}

/**
 * BACKLOG #80 -- the ordinary, region-disposing ack's target.
 *
 * That ack says "every region up to N is disposed of; forget the pixels
 * you were holding for it". It is NOT flagged SLOT_ONLY, so on the
 * producer it moves the slot frontier as well
 * (xup_ack_frontier_apply()) -- which makes it a second admission token
 * and means it must obey the same wire window, or the window would be
 * enforced on one ack and bypassed on the other.
 *
 * Clamping it is safe for the producer's region bookkeeping: xorgxrdp
 * sizes cap_sent at XUP_CAP_SENT_SLOTS = capture slots + 1 per monitor,
 * and min(frame_id_server, frame_id_client + C) never lags the credit
 * frontier by more than one id, so the held-region count stays inside
 * that ring. Delaying it costs held pixels in the producer, never lost
 * ones.
 *
 * @param frame_id_server last frame id handed to the transport
 * @param frame_id_client last frame id the client acknowledged
 * @param wire_window     C, gfx.toml [avc444_ffmpeg] wire_window
 * @return the frame id the region-disposing ack may name now
 */
static inline int
xrdp_gfx_region_ack_target(int frame_id_server, int frame_id_client,
                           int wire_window)
{
    if (frame_id_server > frame_id_client + wire_window)
    {
        return frame_id_client + wire_window;
    }
    return frame_id_server;
}

/**
 * BACKLOG #80 -- everything xrdp knows when it decides what to tell the
 * producer. Grouped into one struct so the decision below can be a PURE
 * function of it and driven from CI, rather than a decision buried in a
 * method that needs a live session to reach.
 */
struct xrdp_gfx_ack_state
{
    int frame_id_consumed;    /* children have drained input up to here */
    int frame_id_server;      /* handed to the transport up to here     */
    int frame_id_client;      /* client has acknowledged up to here     */
    int wire_window;          /* C, gfx.toml wire_window                */
    int frame_id_region_sent; /* highest region-disposing ack sent      */
    int frame_id_server_sent; /* highest ack of any kind sent           */
};

/**
 * BACKLOG #80 -- which acks to send now. -1 means "send none".
 */
struct xrdp_gfx_ack_plan
{
    int region; /* value for the region-disposing ack (flags 0)   */
    int slot;   /* value for the SLOT_ONLY ack                    */
};

/**
 * BACKLOG #80 / PRD FR-FLOW-1 -- the whole emission decision, pure.
 *
 * This is the ONLY place that decides whether the producer is told
 * anything, and it has no branch that can decide to tell it nothing
 * while the frontier has advanced. That property is what FR-FLOW-1
 * clause 1 asks for and what BACKLOG #79 measured the absence of, so it
 * is asserted directly in CI (tests/xrdp/test_avc444_credit_frontier.c)
 * over an exhaustive enumeration of event interleavings.
 *
 * Each ack is sent only when its own frontier has advanced -- resending
 * a value the producer already has is harmless (the producer applies
 * acks with max()) but it is wire traffic for nothing.
 *
 * @param st   current state
 * @param plan out; region/slot values, -1 for "do not send"
 */
static inline void
xrdp_gfx_plan_acks(const struct xrdp_gfx_ack_state *st,
                   struct xrdp_gfx_ack_plan *plan)
{
    int region;
    int credit;
    int sent;

    plan->region = -1;
    plan->slot = -1;
    region = xrdp_gfx_region_ack_target(st->frame_id_server,
                                        st->frame_id_client,
                                        st->wire_window);
    credit = xrdp_gfx_credit_frontier(st->frame_id_consumed,
                                      st->frame_id_server,
                                      st->frame_id_client,
                                      st->wire_window);
    sent = st->frame_id_server_sent;
    if (region > st->frame_id_region_sent)
    {
        plan->region = region;
        if (region > sent)
        {
            sent = region;
        }
    }
    if (credit > sent)
    {
        plan->slot = credit;
    }
}

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
     * cycle so nothing can survive into the next one.
     *
     * BACKLOG #70B: the submit pass does NOT write these where it
     * computes them. It records its results in the caller's locals and
     * one publish (gfx_batch_publish) applies them, after the pump and
     * before the collect, so there is exactly one writer and exactly
     * one point in a cycle where the arm state changes. That ordering
     * is load-bearing, not incidental. */
    int avc444_batch_have[16];
    unsigned long long avc444_batch_seq[16];
    struct xrdp_avc444_encoded_pair avc444_batch_pair[16];
    /* #70B: the coded geometry the emit pass used to read straight off
     * the child (xrdp_ffmpeg_avc444_coded_width/height). Snapshotted by
     * the worker at collect so the emit pass never dereferences a
     * handle -- see avc444_teardown_req below for why that matters. */
    int avc444_batch_cw[16];
    int avc444_batch_ch[16];
    /* E4 counters: E4 must be assertable from a deployed log, never
     * inferred from a wall-clock improvement */
    unsigned long long avc444_batch_cycles;
    unsigned long long avc444_batch_items;
    int avc444_batch_max_kids;
    int avc444_batch_e4_logged;
    void *avc444_ffmpeg_handle[16];  /* struct xrdp_ffmpeg_avc444 * */
    int avc444_actual_w[16];         /* per-surface visible dims for  */
    int avc444_actual_h[16];         /* resize detection (FR-RESIZE)  */
    /* BACKLOG #70B: the emit pass USED to tear the child down itself
     * (the post-ship aux_ltr_chain re-key) while submit(N+1) read and
     * created through this same array -- a use-after-free when the two
     * ran on different threads. #100 removed the assembly thread, but
     * the deferral stays: it is the shipped shape, it keeps every
     * create and delete of a child on one code path, and unpicking it
     * would put the teardown back inside the emit pass for no gain.
     * The worker observes
     * xrdp_ffmpeg_avc444_rekey_pending() right after collect, where it
     * already holds the handle, and applies the teardown at the TOP of
     * the next cycle, before submit. Cost: the re-key is deferred by
     * one frame out of the 504 of margin between
     * XRDP_H264_LTR_FRAME_NUM_REKEY (2^16-512) and the 2^16-8 hard
     * stop. Worker-only. */
    int avc444_teardown_req[16];
    /* tail-flush (OPT-IN last resort, gfx.toml tail_flush; default off): a deep
     * encoder pipeline (e.g. -async_depth > 1) withholds the last frame of an
     * idle-bounded burst until the next input. The root-cause fix is a shallow
     * pipeline (-async_depth 1 / -tune zerolatency); when that is impossible,
     * arm a short idle timer after a real frame and drain a bounded number of
     * duplicate frames to push the withheld frame out. */
    int frame_id_client; /* last frame id received from client */
    int frame_id_server; /* last frame id received from Xorg */
    int frame_id_server_sent;
    /* BACKLOG #70: the CONTIGUOUS frontier of frame ids whose input the
     * encoder children have absorbed (their vmsplice'd pages are free).
     * Never advanced past a gap: the module ack is cumulative, so
     * acking N would also release an unconsumed N-1 whose borrowed
     * capture pages a child may still be reading. */
    int frame_id_consumed;
    /* highest id acked with the region-disposing (ordinary) ack. Equal
     * to frame_id_server_sent unless an eager slot-only ack has run
     * ahead of it. */
    int frame_id_region_sent;
    int eager_slot_ack;  /* gfx.toml eager_slot_ack, batch path only */
    /* BACKLOG #80 / PRD FR-FLOW-1 clause 4: C, the end-to-end wire
     * window, in frames. USER configuration (gfx.toml [avc444_ffmpeg]
     * wire_window) because its right value is a property of the
     * deployment's round-trip time, which the server cannot know. Read
     * only on the eager_slot_ack path -- the legacy path keeps
     * frames_in_flight and its behaviour is unchanged. */
    int wire_window;
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

/**
 * Publish one cycle's per-monitor arm state to the emit pass.
 *
 * The SINGLE writer of the arm state, applied after the pump and
 * before the collect. The submit pass that produced its inputs carries
 * them in the caller's locals rather than writing them where they were
 * computed, so that a cycle's arm state changes at exactly one point.
 *
 * Not static so the tri-state can be pinned: sub_state distinguishes
 * "armed" from "never touched" because seq 0 is a real sequence number
 * -- the first submit of a session -- and a truthiness test on the seq
 * would silently un-arm that frame.
 *
 * @param self
 * @param sub_seq   per-monitor sequence handed out by the submit pass
 * @param sub_state per-monitor: 1 armed, 0 untouched, -1 submit failed
 * @param sub_reset per-monitor: the child was torn down this cycle
 */
void
gfx_batch_publish(struct xrdp_encoder *self,
                  const unsigned long long *sub_seq, const int *sub_state,
                  const int *sub_reset);

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
/* PRD FR-ACK-1 terminal state (b): this frame was CONSUMED but produced
 * no output frame (AVC444 warmup PENDING, encoder error, dropped pair).
 * Carries the echoed frame id with comp_bytes 0 so the producer's slot
 * is released and its region returns to the dirty region. Set only
 * together with ENC_DONE_FLAGS_FRAME_ID_BIT. */
#define ENC_DONE_FLAGS_NOT_DISPLAYED_BIT 2
/* BACKLOG #70: this enc_done is not a frame at all -- it reports that
 * the encoder children have ABSORBED the input of the frame whose
 * echoed id it carries. comp_bytes 0, last 0: it sends nothing and it
 * does not release the XRDP_ENC_DATA. Set only together with
 * ENC_DONE_FLAGS_FRAME_ID_BIT. */
#define ENC_DONE_FLAGS_CONSUMED_BIT 3

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

/* BACKLOG #70 measurement (env XRDP_ACK_TRACE=1, off by default and
 * separate from XRDP_GFX_TRACE so a timing run does not also pay for the
 * per-rect damage tracing): one line per pipeline stage boundary,
 * stamped in CLOCK_MONOTONIC microseconds and keyed by the frame's
 * ECHOED id. That clock is system-wide, so the xorgxrdp capture legs and
 * these encode/egress legs live on ONE timeline and can be intersected
 * to answer "how many ms of capture ran concurrently with the tail of
 * the previous frame" -- the unit the concurrency claim is made in. */
long long
xrdp_mono_us(void);
int
xrdp_ack_trace_on(void);

/* BACKLOG #70 -- the ECHOED frame id (the producer's rect_id) carried in
 * a batchable blob's STARTFRAME, or -1 if the blob does not open with a
 * whole STARTFRAME. It is only ever read from a blob
 * gfx_egfx_batch_peek_mon() has already accepted, which is the one shape
 * whose STARTFRAME sits at offset 0; the bounds are re-checked here
 * anyway because the blob arrives verbatim off the xup socket. */
int
gfx_egfx_batch_peek_frame_id(const char *cmd, int cmd_bytes);

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
