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
 * H.264 Annex-B byte-stream validation for the AVC444 external backend.
 *
 * This is deliberately NOT a full H.264 parser (PRD FR-H264-5): it splits
 * bounded Annex-B start codes and inspects only the one-byte NAL header's
 * nal_unit_type. It never parses slice syntax or decodes pixels. All
 * scanning is bounded; the input is treated as untrusted (PRD FR-NUT-7).
 */

#ifndef _XRDP_H264_ANNEXB_H
#define _XRDP_H264_ANNEXB_H

struct xrdp_h264_nal_summary
{
    int valid;      /* well-formed Annex-B with at least one start code    */
    int nal_count;
    int has_sps;    /* nal_unit_type 7                                     */
    int has_pps;    /* nal_unit_type 8                                     */
    int has_idr;    /* nal_unit_type 5 (IDR VCL)                           */
    int has_vcl;    /* nal_unit_type 1 or 5                                */
    int forbidden_bit_set; /* any NAL with forbidden_zero_bit == 1         */
    int sps_count;  /* reset packets must carry EXACTLY ONE SPS: strict    */
    int pps_count;  /* decoders (macOS VideoToolbox) black out on          */
    /*                 duplicated parameter sets (PRD FR-PROBE-6)          */
};

/**
 * Scan an Annex-B access unit. Returns 0 and fills *out on a structurally
 * valid byte stream; returns non-zero (out->valid == 0) on malformed input
 * (no start code, truncation). Bounded: at most 65536 NAL units inspected.
 */
int
xrdp_h264_scan_annexb(const unsigned char *data, int len,
                      struct xrdp_h264_nal_summary *out);

/**
 * First-packet reset requirement (PRD FR-H264-5): at least one SPS, one
 * PPS and one IDR, valid framing, no forbidden bit. Returns 1 if satisfied.
 */
int
xrdp_h264_main_reset_ok(const unsigned char *data, int len);

/**
 * Auxiliary packet requirement (PRD FR-H264-5): at least one VCL NAL unit
 * (type 1 or 5), valid framing. Returns 1 if satisfied.
 */
int
xrdp_h264_aux_ok(const unsigned char *data, int len);

/**
 * macOS interop (BACKLOG 2026-07-27): remove HRD parameters from every
 * SPS in an Annex-B access unit, in place. Clears
 * nal_hrd_parameters_present_flag and vcl_hrd_parameters_present_flag,
 * drops their hrd_parameters() structures and low_delay_hrd_flag, and
 * preserves every other SPS field bit-exactly (the stream can only
 * shrink; *len is updated). SPS NALs without HRD pass through untouched.
 * Returns 0 on success, non-zero on any parse or bounds failure — the
 * caller must treat that as a validation failure and never ship a
 * half-rewritten stream.
 */
int
xrdp_h264_sanitize_hrd(unsigned char *data, int *len);

/*
 * Clear pic_struct_present_flag in the VUI of every SPS in an Annex-B
 * access unit, in place (macOS interop: the flag declares per-frame
 * picture-timing SEI which xrdp strips, and a declared-but-absent
 * timing feed changes VideoToolbox output pacing). Every other SPS
 * field is preserved bit-exactly; SPS NALs without VUI or with the
 * flag already 0 pass through untouched. Same failure contract as
 * xrdp_h264_sanitize_hrd().
 */
int
xrdp_h264_strip_pic_struct(unsigned char *data, int *len);


/* bound on a raw parameter-set copy: real SPS/PPS NALs on this path
 * are tens of bytes (measured: SPS 29, PPS 4 on the shipped VAAPI
 * stream) */
#define XRDP_H264_PS_RAW_MAX 256

/* cached SPS/PPS fields needed to parse slice headers (strip_mmco,
 * aux_to_leaf) */
struct xrdp_h264_param_cache
{
    int have_sps;
    int log2_max_frame_num;
    int log2_max_poc_lsb;
    int poc_type;
    int frame_mbs_only;
    int scaling_present;    /* SPS seq_scaling_matrix_present_flag       */
    int have_pps;
    int entropy_cabac;
    int slice_groups;
    int num_ref_idx_l0_default; /* num_ref_idx_l0_default_active_minus1 */
    int weighted_pred;
    int deblock_present;
    int redundant_present;
    int pic_init_qp;        /* pic_init_qp_minus26 + 26                  */
    int chroma_qp_offset;
    int second_chroma_qp_offset;
    int transform_8x8;
    int pps_scaling_present;
    /* the parameter sets as the child emitted them, byte for byte.
     * Kept only so a scheduled refresh can DROP the child's repeat of
     * them (BACKLOG #45 D18: a converted cut is not a decoder entry
     * point, so parameter sets there cost bytes and buy nothing) while
     * proving they are the same sets already on the wire -- swallowing
     * a CHANGED SPS would be silent whole-picture corruption. A set
     * too large to copy leaves the length 0, which means "cannot
     * prove identical" and keeps the pass-through. */
    unsigned char sps_raw[XRDP_H264_PS_RAW_MAX];
    int sps_raw_len;
    unsigned char pps_raw[XRDP_H264_PS_RAW_MAX];
    int pps_raw_len;
};

/*
 * DIAGNOSTIC (Mac k=1 bisect): rewrite every non-IDR reference slice so
 * dec_ref_pic_marking uses sliding-window instead of an explicit MMCO
 * op list, re-padding the CABAC alignment (payload copied verbatim).
 * SPS/PPS NALs encountered in the stream update *cache. Fails loudly on
 * any slice shape our encoders do not emit.
 */
int
xrdp_h264_strip_mmco(unsigned char *data, int *len,
                     struct xrdp_h264_param_cache *cache);

/*
 * AVC444 reference partitioning (BACKLOG 2026-07-27): rewrite an
 * all-IDR auxiliary packet into non-reference, non-IDR I "leaf"
 * frames so the aux view never enters the shared DPB and the main
 * chain self-references only main frames at any aux cadence.
 * Walks the MAIN packet first (read-only) to cache its SPS/PPS in
 * *main_cache and read the frame_num of its last reference VCL NAL;
 * then rewrites the AUX packet in place: SPS/PPS are cached into
 * *aux_cache and dropped, SEI/AUD are dropped, and every IDR slice
 * becomes a type-1 slice with nal_ref_idc 0, idr_pic_id and
 * dec_ref_pic_marking removed, and frame_num = main frame_num + 1
 * (the non-reference-picture rule). The CABAC payload is copied
 * byte-verbatim after re-padding the alignment. Fails loudly (and
 * leaves the caller to drop the packet) on any stream shape outside
 * what our encoders emit, or if any parse-relevant SPS/PPS field
 * differs between the two encoder children.
 */
int
xrdp_h264_aux_to_leaf(unsigned char *aux, int *aux_len,
                      const unsigned char *main_data, int main_len,
                      struct xrdp_h264_param_cache *main_cache,
                      struct xrdp_h264_param_cache *aux_cache);

/*
 * FR-H264-8 (EXPERIMENTAL): aux-refs-aux via Windows-style long-term
 * reference slots. The two child streams (main: self-referencing P
 * chain; aux: refs=1 P chain, IDR first) are rewritten into ONE
 * frame_num chain where every picture is a reference (nri=3) that
 * mmco6-self-marks into its view's long-term slot (LT0 = main,
 * LT1 = aux) and every P slice list-modifies ref 0 to its OWN view's
 * slot -- the reference topology measured byte-exactly from a real
 * Win2022 server (PR-demo/win2022_ground_truth, LTR addendum).
 * Deliberate deviation from Windows: the first aux after an IDR is
 * converted to a self-contained non-IDR I slice that self-marks LT1
 * referencing NOTHING (Windows references the main IDR instead), so
 * a two-decoder client can decode the aux view standalone.
 * Deliberate H.264 7.4.3.3 nonconformance, copied from Windows: no
 * mmco4 is ever emitted, so mmco6 long_term_frame_idx=1 exceeds
 * MaxLongTermFrameIdx (= 0 from the IDR long_term_reference_flag).
 * Every deployed decoder accepts this shape daily from real Windows
 * servers; ffmpeg does not track MaxLongTermFrameIdx at all.
 * Contingency if a future decoder enforces the range: emit mmco4
 * max_long_term_frame_idx_plus1=2 on the first non-IDR slice.
 */
/* the LTR chain's OUTPUT frame_num width: child fields are widened to
 * 16 bits (log2_max_frame_num_minus4 = 12, the legal maximum).
 * MEASURED (2026-07-28, ffmpeg 7.1.5): a per-view feed with +2
 * frame_num gaps SILENTLY STOPS DECODING at the frame_num wrap (300
 * aux pictures, 8-bit field: 129/300 frames output, zero warnings) --
 * so the wire must never let a decoder see a wrap in ANY topology.
 * The runner re-keys (encoder pair restart -> fresh IDR, counter
 * reset) before the counter reaches the wrap. */
#define XRDP_H264_LTR_LOG2_MAX_FRAME_NUM 16
#define XRDP_H264_LTR_FRAME_NUM_REKEY \
    ((1 << XRDP_H264_LTR_LOG2_MAX_FRAME_NUM) - 512)
/* The re-key threshold is settable (gfx.toml ltr_rekey_frame_num) so the
 * boundary can be exercised in minutes instead of the ~18 min of
 * continuous 30 fps animation the shipped value implies (32512 pairs).
 * The MAX is the shipped default and is a CEILING, not a suggestion:
 * a larger value would let a decoder see the frame_num wrap this whole
 * mechanism exists to prevent. The MIN keeps a test value from turning
 * every frame into a re-key. */
#define XRDP_H264_LTR_FRAME_NUM_REKEY_MAX XRDP_H264_LTR_FRAME_NUM_REKEY
#define XRDP_H264_LTR_FRAME_NUM_REKEY_MIN 64

/* Scheduled intra refresh (PRD FR-H264-6, gfx.toml
 * intra_refresh_frames / intra_refresh_frames_aux): the number of
 * pictures between scheduled cuts, counted SEPARATELY IN EACH VIEW off
 * that view's own child input index. Two plain integers and no time
 * anywhere (owner directive 2026-08-08). When the aux view is sent with
 * every frame -- the shipped default -- the two intervals are equal and
 * a cut therefore lands on the same picture ordinal in both views, which
 * is what the wire audit's A3 check asserts. Under the sparse-aux
 * cadence (FR-H264-9) the aux child sees fewer pictures, so its cuts are
 * spaced further apart in wall time and the ordinals no longer coincide;
 * A3 does not apply there, by definition.
 *
 * WHY 250, and why the same number for both. xrdp's linked-library
 * H.264 path (xrdp_encoder_x264.c) never sets i_keyint_max, so it takes
 * libx264's default of 250 pictures -- measured, not looked up, with
 * PR-demo/mac_bisect_matrix/x264_keyint_probe.c under every preset
 * gfx.toml ships. That path has emitted a real IDR (a full DPB flush)
 * every 250 pictures for years. Matching the number keeps this backend's
 * cadence identical to the one xrdp already had, and ours is the gentler
 * of the two: a converted non-IDR I that flushes nothing and keeps both
 * long-term chains alive. Measured cost on the code-scroll corpus:
 * +1.84 % of bytes (PRD FR-H264-6). The previous default was 240, which
 * was picked to be convenient to test and was never derived.
 *
 * There is deliberately NO 0/off value (#45 D6): an off switch would
 * keep the deleted aux-respawn path alive as a shadow fallback. The
 * loader REFUSES an out-of-range value (the default stands) and the
 * runner CLAMPS independently. */
#define XRDP_H264_INTRA_REFRESH_FRAMES 250
#define XRDP_H264_INTRA_REFRESH_FRAMES_MIN 24
#define XRDP_H264_INTRA_REFRESH_FRAMES_MAX 4096
/* The aux view's own interval, in AUX pictures. Equal to the main
 * default, so with the sparse-aux feature off nothing changes. */
#define XRDP_H264_INTRA_REFRESH_FRAMES_AUX XRDP_H264_INTRA_REFRESH_FRAMES

struct xrdp_h264_ltr_state
{
    struct xrdp_h264_param_cache main_cache;
    struct xrdp_h264_param_cache aux_cache;
    int frame_num;      /* shared counter: value for the NEXT picture */
    int started;        /* a main IDR has been rewritten              */
    int aux_seeded;     /* LT1 occupied (first aux converted)         */
    /* scheduled refresh (FR-H264-6). refresh_period is the interval the
     * MAIN child was spawned with, set by the runner from
     * intra_refresh_frames; pic_index counts pictures per view since the
     * epoch entry. The rewriter compares OBSERVED against REQUESTED at
     * every picture and fails the pair on a mismatch -- a silently
     * skipped cut must never ship.
     * refresh_period 0 means "no schedule declared", which only the
     * unit tests and the non-scheduled diagnostic arms use; the runner
     * always sets it when aux_ltr_chain is on. */
    int refresh_period;
    /* the interval the AUX child was spawned with, from
     * intra_refresh_frames_aux. The two children count their own input
     * indices, so under the sparse-aux cadence (FR-H264-9) the aux view
     * needs its own number rather than a share of the main one.
     * 0 means "not declared separately" and the aux view is then checked
     * against refresh_period, which is exactly the 1:1 behaviour that
     * shipped before this field existed. */
    int refresh_period_aux;
    int pic_index[2];
    /* #75: reusable output buffer for the rewrite, grown on demand and
     * released by xrdp_h264_ltr_state_free(). A picture-sized malloc
     * per view per frame was an mmap+munmap pair whose every page
     * faulted on first touch. Zero-initialised state means "not yet
     * allocated", so an existing caller that memsets its state needs no
     * change. */
    unsigned char *scratch;
    int scratch_cap;
};

/*
 * Release the rewrite scratch buffer. Safe on a zeroed state and safe to
 * call twice; the state stays usable afterwards (the buffer simply
 * re-grows on the next packet).
 */
void
xrdp_h264_ltr_state_free(struct xrdp_h264_ltr_state *st);

/*
 * Worst-case output growth of the LTR rewrite for an Annex-B packet
 * (per-slice list-mod + marking insertion, SPS ue growth, escape
 * bytes). The caller must provide a buffer of at least
 * len + xrdp_h264_ltr_growth_budget(data, len) bytes.
 */
int
xrdp_h264_ltr_growth_budget(const unsigned char *data, int len);

/*
 * Rewrite one MAIN-view packet in place. Three input shapes, all of
 * which a shipped child encoder produces:
 *   - the EPOCH ENTRY IDR (the chain has not started): stays an IDR,
 *     long_term_reference_flag=1 seeds LT0, the shared counter starts
 *     at 0 and LT1 is marked unseeded; the SPS is rewritten
 *     (max_num_ref_frames and VUI max_dec_frame_buffering raised to 3,
 *     level DPB budget checked) and PPS/SEI/AUD pass through;
 *   - a SCHEDULED REFRESH once the chain has started, arriving either
 *     as a mid-stream IDR (h264_vaapi) or as a non-IDR I (h264_nvenc
 *     at a forced key frame without -forced-idr): both become the
 *     self-contained non-IDR I whose mmco6 REPLACES the occupant of
 *     LT0, so the shared counter keeps running and the aux view's LT1
 *     is untouched (FR-H264-6). The child's repeated SPS/PPS/SEI are
 *     dropped there (#45 D18) and a CHANGED parameter set fails the
 *     packet rather than being swallowed;
 *   - P: shared frame_num, LTR list-modification and mmco6/LT0
 *     marking.
 * All VCL nri=3, CABAC payload byte-verbatim. cap is the allocated
 * size of data (see growth_budget). Fails loudly on any stream shape
 * outside the compat guard; on failure the caller must drop the packet
 * (never ship a half-rewrite).
 */
int
xrdp_h264_ltr_rewrite_main(unsigned char *data, int *len, int cap,
                           struct xrdp_h264_ltr_state *st);

/*
 * Rewrite one AUX-view packet in place (SPS/PPS cached + dropped,
 * SEI/AUD dropped; ANY intra picture -- IDR or non-IDR I -- converted
 * to the self-contained LT1-seeding non-IDR I slice; P: shared
 * frame_num, LTR list-modification and mmco6/LT1 marking; nri=3). An
 * aux P while LT1 is unseeded is a loud failure: only an aux INTRA
 * picture can seed LT1, and with the scheduled refresh (FR-H264-6) a
 * main-view cut no longer takes LT1 away, so this state means the
 * schedule was not honoured and the pair must not ship.
 * Same cap and failure contract as the main rewrite.
 */
int
xrdp_h264_ltr_rewrite_aux(unsigned char *data, int *len, int cap,
                          struct xrdp_h264_ltr_state *st);

#endif /* _XRDP_H264_ANNEXB_H */

