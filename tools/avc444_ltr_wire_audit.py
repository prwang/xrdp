#!/usr/bin/env python3
"""Audit the reference STRUCTURE of an AVC444 wire capture (oracle dump).

Answers one question with syntax, not inference: does each view predict
from its OWN previous picture? For the FR-H264-8 LTR aux-chain that means

  * main pictures are P slices whose L0 is retargeted to long-term slot 0
    (the main view's slot) and which re-mark themselves into slot 0;
  * aux  pictures are P slices whose L0 is retargeted to long-term slot 1
    (the aux view's slot) and which re-mark themselves into slot 1;
  * no picture ever references the OTHER view's slot (that would be the
    leaf/cross-view topology, i.e. aux predicted from main).

Every field below is parsed from the bitstream per ITU-T H.264 7.3.2.1
(SPS), 7.3.2.2 (PPS) and 7.3.3 (slice header, incl. 7.3.3.1 ref list
modification and 7.3.3.3 dec_ref_pic_marking). Nothing is assumed about
the encoder; a stream that does not match the guard shape is reported as
such rather than silently mis-parsed.

Usage: avc444_ltr_wire_audit.py [--annexb] [--assert] [--intra-refresh N]
                                [--allow-idr K] <file> [label] [max_pictures]
  --annexb: <file> is a RAW Annex-B elementary stream from a child
  encoder; report its shape and whether ltr_cache_ok() accepts it.
  --assert: turn the report into a GATE (BACKLOG #45 step 0). Every
  check below is evaluated and named, and the tool exits non-zero on
  the first violated one. Without --assert the tool only prints (the
  descriptive mode every earlier capture was read with).
  --intra-refresh N: the scheduled cut period of the MAIN view, in main
  pictures (gfx.toml intra_refresh_frames). Enables the schedule and
  chain-depth assertions, which cannot be checked without it.
  --intra-refresh-aux M: the same for the AUX view, in AUX pictures
  (gfx.toml intra_refresh_frames_aux). Defaults to N, which is what the
  loader does when the key is absent. It is a separate number because
  under the sparse-aux cadence (BACKLOG #92 / PRD FR-H264-9) the aux
  child is fed fewer pictures than the main one, so the same ordinal is
  a different moment in each view.
  --allow-idr K: tolerate K mid-stream IDRs (only a frame_num-wrap
  re-key may legitimately produce one; default 0).

The assertions, and why each one has teeth:
  A1 no mid-stream IDR              FR-H264-6: a refresh is a non-IDR I;
                                    an IDR flushes the DPB and breaks
                                    both long-term chains.
  A2 intra only on scheduled index  every intra picture sits at
                                    ordinal % N == 0 in its own view.
  A3 cuts are PAIRED                main and aux cut at the SAME view
                                    ordinals -- one view refreshing
                                    alone desynchronises the pair.
                                    SKIPPED, not failed, when the
                                    capture shows a SPARSE AUX CADENCE
                                    (fewer aux pictures than main): the
                                    two views then have different
                                    numbers of pictures and "the same
                                    ordinal" is not a comparison that
                                    means anything. Owner ruling,
                                    2026-08-08. The sparseness is read
                                    off the BITSTREAM -- the picture
                                    counts -- not off a flag, so a
                                    config typo cannot buy the
                                    exemption, and A3 keeps full force
                                    at 1:1, which is the shipped
                                    default.
  A4 no scheduled cut is skipped    every scheduled ordinal inside the
                                    captured window carries an intra in
                                    BOTH views (the observed-vs-
                                    requested property, on the wire).
  A5 own-slot refs and self-marks   every P retargets L0 to its own
                                    view's long-term slot and re-marks
                                    itself into it; no picture ever
                                    references the other view's slot.
  A6 one contiguous frame_num chain +1 per picture across the whole
                                    merged decode order, cuts included.
  A7 chain depth <= N               no picture is more than N pictures
                                    from its own view's last intra.
"""
import struct
import sys

P, B, I, SP, SI = 0, 1, 2, 3, 4
SLICE_NAME = {P: 'P', B: 'B', I: 'I', SP: 'SP', SI: 'SI'}


class Bits:
    """RBSP bit reader (emulation-prevention bytes removed)."""

    def __init__(self, buf):
        out = bytearray()
        i = 0
        while i < len(buf):
            if i + 2 < len(buf) and buf[i] == 0 and buf[i + 1] == 0 \
                    and buf[i + 2] == 3:
                out += buf[i:i + 2]
                i += 3
            else:
                out.append(buf[i])
                i += 1
        self.b = bytes(out)
        self.pos = 0

    def u(self, n):
        v = 0
        for _ in range(n):
            byte = self.b[self.pos >> 3]
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def ue(self):
        z = 0
        while self.u(1) == 0:
            z += 1
            if z > 31:
                raise ValueError('bad ue')
        return (1 << z) - 1 + (self.u(z) if z else 0)

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k % 2 else -(k // 2)


HIGH_PROFILES = (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139,
                 134, 135)


def parse_sps(nal):
    b = Bits(nal[1:])
    sps = {}
    sps['profile'] = b.u(8)
    sps['constraints'] = b.u(8)
    sps['level'] = b.u(8)
    b.ue()                                       # sps id
    sps['separate_colour_plane'] = 0
    if sps['profile'] in HIGH_PROFILES:
        if b.ue() == 3:                          # chroma_format_idc
            sps['separate_colour_plane'] = b.u(1)
        b.ue(); b.ue(); b.u(1)                   # bit depths, bypass
        if b.u(1):                               # scaling matrix
            raise ValueError('scaling matrix present (guard rejects)')
    sps['log2_max_frame_num'] = b.ue() + 4
    sps['poc_type'] = b.ue()
    if sps['poc_type'] == 0:
        sps['log2_max_poc_lsb'] = b.ue() + 4
    elif sps['poc_type'] == 1:
        b.u(1); b.se(); b.se()
        for _ in range(b.ue()):
            b.se()
    sps['max_num_ref_frames'] = b.ue()
    sps['gaps_allowed'] = b.u(1)
    sps['width'] = (b.ue() + 1) * 16
    h_map = b.ue() + 1
    sps['frame_mbs_only'] = b.u(1)
    if not sps['frame_mbs_only']:
        b.u(1)
    sps['height'] = h_map * 16 * (2 - sps['frame_mbs_only'])
    return sps


def parse_pps(nal):
    b = Bits(nal[1:])
    pps = {}
    pps['id'] = b.ue()
    b.ue()                                       # sps id
    pps['cabac'] = b.u(1)
    pps['bottom_field_poc'] = b.u(1)
    pps['slice_groups'] = b.ue() + 1
    if pps['slice_groups'] > 1:
        raise ValueError('slice groups present (guard rejects)')
    pps['num_ref_idx_l0_default'] = b.ue() + 1
    b.ue()                                       # l1 default
    pps['weighted_pred'] = b.u(1)
    b.u(2)                                       # weighted_bipred_idc
    b.se(); b.se(); b.se()                       # qp/qs/chroma offsets
    b.u(1); b.u(1)                               # deblock ctrl, cip
    pps['redundant_pic_cnt_present'] = b.u(1)
    return pps


def parse_slice(nal, sps, pps):
    """Parse a slice header through dec_ref_pic_marking()."""
    nal_ref_idc = (nal[0] >> 5) & 3
    nal_type = nal[0] & 0x1F
    idr = (nal_type == 5)
    b = Bits(nal[1:])
    sh = {'nal_type': nal_type, 'nal_ref_idc': nal_ref_idc, 'idr': idr}
    sh['first_mb'] = b.ue()
    raw_type = b.ue()
    sh['slice_type'] = raw_type % 5
    sh['all_slices_same_type'] = raw_type >= 5
    b.ue()                                       # pps id
    if sps['separate_colour_plane']:
        b.u(2)
    sh['frame_num'] = b.u(sps['log2_max_frame_num'])
    if not sps['frame_mbs_only']:
        raise ValueError('field coding (guard rejects)')
    if idr:
        sh['idr_pic_id'] = b.ue()
    if sps['poc_type'] == 0:
        b.u(sps['log2_max_poc_lsb'])
        if pps['bottom_field_poc']:
            b.se()
    elif sps['poc_type'] == 1:
        raise ValueError('poc_type 1 (guard rejects)')
    if pps['redundant_pic_cnt_present']:
        b.ue()
    if sh['slice_type'] == B:
        b.u(1)
    if sh['slice_type'] in (P, SP, B):
        if b.u(1):                               # num_ref_idx override
            b.ue()
            if sh['slice_type'] == B:
                b.ue()
    # 7.3.3.1 ref_pic_list_modification
    sh['rplm'] = []
    if sh['slice_type'] not in (I, SI):
        if b.u(1):
            while True:
                idc = b.ue()
                if idc == 3:
                    break
                val = b.ue()
                sh['rplm'].append((idc, val))
                if len(sh['rplm']) > 32:
                    raise ValueError('runaway rplm')
    if pps['weighted_pred'] and sh['slice_type'] in (P, SP):
        raise ValueError('weighted pred (guard rejects)')
    # 7.3.3.3 dec_ref_pic_marking
    sh['marking'] = []
    sh['long_term_reference_flag'] = None
    if nal_ref_idc != 0:
        if idr:
            b.u(1)                               # no_output_of_prior_pics
            sh['long_term_reference_flag'] = b.u(1)
        else:
            if b.u(1):                           # adaptive marking
                while True:
                    op = b.ue()
                    if op == 0:
                        sh['marking'].append((0, None))
                        break
                    if op in (1, 3):
                        arg = b.ue()             # diff_of_pic_nums_minus1
                    elif op == 2:
                        arg = b.ue()             # long_term_pic_num
                    elif op in (4, 6):
                        arg = b.ue()             # ltfi / max_ltfi_plus1
                    elif op == 5:
                        arg = None
                    else:
                        raise ValueError('invalid mmco %d' % op)
                    if op == 3:
                        arg = (arg, b.ue())
                    sh['marking'].append((op, arg))
                    if len(sh['marking']) > 32:
                        raise ValueError('runaway marking')
            else:
                sh['marking'].append(('sliding_window', None))
    return sh


def nals_of(buf):
    out = []
    i = buf.find(b'\x00\x00\x01')
    while i >= 0:
        j = buf.find(b'\x00\x00\x01', i + 3)
        end = j if j >= 0 else len(buf)
        raw = buf[i + 3:end]
        if raw.endswith(b'\x00'):
            raw = raw[:-1]
        if raw:
            out.append(raw)
        i = j
    return out


# Set by --single-view. A plain AVC420 PDU (codec id 0x000B) carries NO
# avc420EncodedBitstreamInfo word: it is the RFX_AVC420_METABLOCK and the
# bitstream, nothing else. Reading its first four bytes as an LC word --
# which is what this tool does for AVC444 -- lands on numRegionRects and
# produces nonsense: arm x035 was read as 291 "aux" pictures and 0 "main"
# ones on 2026-08-10, and every two-view assertion duly failed on a
# stream that was perfectly correct for its configuration.
SINGLE_VIEW = False


def views_of(rec):
    if SINGLE_VIEW:
        return [('main', rec)]
    (w,) = struct.unpack_from('<I', rec, 0)
    cb1 = w & 0x3FFFFFFF
    lc = (w >> 30) & 0x3
    if lc == 1:
        return [('main', rec[4:])]
    if lc == 2:
        return [('aux', rec[4:])]
    return [('main', rec[4:4 + cb1]), ('aux', rec[4 + cb1:])]


def stream_of(view):
    """Strip the AVC420 region/quality preamble."""
    (n,) = struct.unpack_from('<I', view, 0)
    return view[4 + n * 10:]


def audit(path, label, max_pics):
    data = open(path, 'rb').read()
    sps = None
    pps = None
    pics = []                    # per picture, decode order
    sps_seen = []
    pos = 0
    while pos + 4 <= len(data) and len(pics) < max_pics:
        (ln,) = struct.unpack_from('<I', data, pos)
        pos += 4
        rec = data[pos:pos + ln]
        pos += ln
        if len(rec) != ln or ln < 8:
            break
        for view, payload in views_of(rec):
            if len(payload) < 4:
                continue
            stream = stream_of(payload)
            for nal in nals_of(stream):
                t = nal[0] & 0x1F
                if t == 7:
                    sps = parse_sps(nal)
                    sps_seen.append((view, sps))
                elif t == 8:
                    pps = parse_pps(nal)
                elif t in (1, 5):
                    if sps is None or pps is None:
                        continue
                    sh = parse_slice(nal, sps, pps)
                    if sh['first_mb'] != 0:
                        continue          # same picture, later slice
                    sh['view'] = view
                    sh['bytes'] = len(nal)
                    pics.append(sh)
    return sps_seen, sps, pps, pics


def describe(sh):
    kind = ('IDR' if sh['idr']
            else SLICE_NAME.get(sh['slice_type'], '?'))
    refs = ('->LT%d' % sh['rplm'][0][1]
            if sh['rplm'] and sh['rplm'][0][0] == 2
            else ('rplm=%s' % sh['rplm'] if sh['rplm'] else 'none'))
    if sh['marking'] and sh['marking'][0][0] == 6:
        mark = 'mark LT%d' % sh['marking'][0][1]
    elif sh['long_term_reference_flag'] is not None:
        mark = 'ltr_flag=%d' % sh['long_term_reference_flag']
    else:
        mark = str(sh['marking'])
    return kind, refs, mark



GUARD = """FR-H264-8 LTR guard (xrdp_h264_annexb.c ltr_cache_ok): the aux
chain rewriter accepts a child encoder's stream only if every condition
below holds. A single FAIL means the encoder's stream shape cannot be
spliced and the chain stays off (leaf topology is used instead)."""


def guard_report(sps, pps):
    """Evaluate ltr_cache_ok()'s conditions against a parsed SPS/PPS."""
    checks = [
        ('log2_max_frame_num in 4..16', 4 <= sps['log2_max_frame_num'] <= 16,
         sps['log2_max_frame_num']),
        ('poc_type == 2', sps['poc_type'] == 2, sps['poc_type']),
        ('frame_mbs_only == 1', sps['frame_mbs_only'] == 1,
         sps['frame_mbs_only']),
        ('sps scaling absent', True, 'ok (parse would have raised)'),
        ('entropy CABAC == 1', pps['cabac'] == 1, pps['cabac']),
        ('slice_groups == 0', pps['slice_groups'] == 1,
         pps['slice_groups'] - 1),
        ('weighted_pred == 0', pps['weighted_pred'] == 0,
         pps['weighted_pred']),
        ('num_ref_idx_l0_default_minus1 == 0',
         pps['num_ref_idx_l0_default'] == 1,
         pps['num_ref_idx_l0_default'] - 1),
        ('redundant_pic_cnt absent', pps['redundant_pic_cnt_present'] == 0,
         pps['redundant_pic_cnt_present']),
    ]
    print(GUARD)
    print()
    ok = True
    for name, passed, val in checks:
        print('  %-36s %-4s (value: %s)'
              % (name, 'PASS' if passed else 'FAIL', val))
        ok = ok and passed
    return ok


def audit_annexb(path, label, max_pics):
    """Guard/shape audit of a RAW Annex-B elementary stream (one view)."""
    buf = open(path, 'rb').read()
    sps = pps = None
    pics = []
    for nal in nals_of(buf):
        t = nal[0] & 0x1F
        if t == 7:
            sps = parse_sps(nal)
        elif t == 8:
            pps = parse_pps(nal)
        elif t in (1, 5) and sps and pps and len(pics) < max_pics:
            sh = parse_slice(nal, sps, pps)
            if sh['first_mb'] != 0:
                continue
            sh['view'] = 'stream'
            sh['bytes'] = len(nal)
            pics.append(sh)
    if sps is None or pps is None:
        sys.exit('%s: no SPS/PPS found' % label)
    print('=== %s (raw Annex-B) ===' % label)
    print('SPS: %dx%d profile=%d level=%d max_num_ref_frames=%d '
          'log2_max_frame_num=%d poc_type=%d gaps=%d'
          % (sps['width'], sps['height'], sps['profile'], sps['level'],
             sps['max_num_ref_frames'], sps['log2_max_frame_num'],
             sps['poc_type'], sps['gaps_allowed']))
    print('PPS: cabac=%d num_ref_idx_l0_default=%d weighted_pred=%d '
          'slice_groups=%d redundant=%d'
          % (pps['cabac'], pps['num_ref_idx_l0_default'],
             pps['weighted_pred'], pps['slice_groups'] - 1,
             pps['redundant_pic_cnt_present']))
    print()
    print('first %d pictures:' % min(8, len(pics)))
    for sh in pics[:8]:
        kind, refs, mark = describe(sh)
        print('  fn=%-6d %-3s %-10s %-24s %7dB'
              % (sh['frame_num'], kind, refs, mark, sh['bytes']))
    print()
    ok = guard_report(sps, pps)
    print()
    if ok:
        print('GUARD VERDICT: PASS -- this encoder\'s stream shape is '
              'splicable by the FR-H264-8 LTR rewriter.')
        return 0
    print('GUARD VERDICT: FAIL -- the LTR rewriter would refuse this '
          'encoder (chain stays off; leaf topology used).')
    return 1

def assert_gate(sps, pics, refresh, allow_idr, refresh_aux=None):
    """Evaluate the #45 wire assertions. Returns a list of
    (name, ok, detail) in check order -- nothing is skipped silently:
    a check that cannot run without --intra-refresh says so and
    counts as a FAIL, because the gate was asked for.

    ok is True (pass), False (violated) or None (NOT APPLICABLE to this
    capture, with the reason in detail). None exists for exactly one
    check, A3, and only under a sparse aux cadence -- see below."""
    out = []
    if refresh_aux is None:
        refresh_aux = refresh
    own_slot = {'main': 0, 'aux': 1}
    per_view = {'main': [s for s in pics if s['view'] == 'main'],
                'aux': [s for s in pics if s['view'] == 'aux']}

    # A1 -- mid-stream IDR
    mid_idr = [i for i, s in enumerate(pics) if s['idr'] and i > 0]
    out.append(('A1 no mid-stream IDR', len(mid_idr) <= allow_idr,
                '%d mid-stream IDR(s)%s' %
                (len(mid_idr),
                 '' if not mid_idr
                 else ' at decode indices %s' % mid_idr[:8])))

    # A2/A3/A4/A7 need the schedule
    intra_ord = {}
    for view, sel in per_view.items():
        intra_ord[view] = [i for i, s in enumerate(sel)
                           if s['slice_type'] == I]
    if refresh is None:
        for name in ('A2 intra only on a scheduled index',
                     'A4 no scheduled cut skipped',
                     'A7 chain depth <= intra_refresh_frames'):
            out.append((name, False,
                        'not evaluated: --intra-refresh N was not given'))
        unscheduled = None
    else:
        unscheduled = {}
        period = {'main': refresh, 'aux': refresh_aux}
        for view, ords in intra_ord.items():
            unscheduled[view] = [o for o in ords if o % period[view] != 0]
        bad = sum(len(v) for v in unscheduled.values())
        out.append(('A2 intra only on a scheduled index', bad == 0,
                    'unscheduled intra: main %s aux %s'
                    % (unscheduled['main'][:8], unscheduled['aux'][:8])))

    # A3 -- paired cuts (independent of the period).
    #
    # NOT APPLICABLE under a sparse aux cadence (owner ruling,
    # 2026-08-08, BACKLOG #92 / PRD FR-H264-9). When the aux view is
    # sent on only some frames, the two views hold different numbers of
    # pictures and "main ordinal k" and "aux ordinal k" are different
    # moments -- comparing the two ordinal lists is not a check that can
    # be passed or failed, it is a category error. It is skipped rather
    # than deleted, and skipped LOUDLY, because at 1:1 -- which is what
    # ships -- one view refreshing alone really does desynchronise the
    # pair and A3 is the only thing on the wire that would catch it.
    #
    # The sparseness is decided from the CAPTURE, by counting pictures
    # in each view, never from a config flag: a gfx.toml typo must not
    # be able to buy an exemption from a wire check.
    n_main = len(per_view['main'])
    n_aux = len(per_view['aux'])
    if n_aux < n_main:
        out.append(('A3 cuts are paired across views', None,
                    'NOT APPLICABLE: sparse aux cadence on the wire -- '
                    '%d aux pictures against %d main (%.1f%%), so the '
                    'two views\' ordinals are different moments. '
                    'main intra ordinals %s ... aux %s'
                    % (n_aux, n_main, 100.0 * n_aux / max(n_main, 1),
                       intra_ord['main'][:8], intra_ord['aux'][:8])))
    else:
        out.append(('A3 cuts are paired across views',
                    intra_ord['main'] == intra_ord['aux'],
                    'main intra ordinals %s ... aux %s'
                    % (intra_ord['main'][:8], intra_ord['aux'][:8])))

    if refresh is not None:
        missing = {}
        for view, sel in per_view.items():
            want = range(0, len(sel), period[view])
            missing[view] = [o for o in want
                             if o not in set(intra_ord[view])]
        nmiss = sum(len(v) for v in missing.values())
        out.append(('A4 no scheduled cut skipped', nmiss == 0,
                    'scheduled ordinals with no intra: main %s aux %s'
                    % (missing['main'][:8], missing['aux'][:8])))

    # A5 -- own-slot refs and self-marks
    a5 = []
    for view, sel in per_view.items():
        if not sel:
            continue
        slot = own_slot[view]
        inter = [s for s in sel if s['slice_type'] == P]
        wrong_ref = [s for s in inter
                     if not (s['rplm'] and s['rplm'][0] == (2, slot))]
        wrong_mark = [s for s in sel
                      if not s['idr']
                      and not (s['marking']
                               and s['marking'][0] == (6, slot))]
        if wrong_ref:
            a5.append('%s: %d/%d P not retargeted to LT%d'
                      % (view, len(wrong_ref), len(inter), slot))
        if wrong_mark:
            a5.append('%s: %d/%d non-IDR pictures do not self-mark LT%d'
                      % (view, len(wrong_mark), len(sel), slot))
    out.append(('A5 own-slot refs and self-marks', not a5,
                '; '.join(a5) if a5 else 'all pictures conform'))

    # A6 -- one contiguous shared frame_num chain, cuts included
    mod = 1 << sps['log2_max_frame_num']
    gaps = [i + 1 for i, (a, b_) in enumerate(zip(pics, pics[1:]))
            if (a['frame_num'] + 1) % mod != b_['frame_num']]
    out.append(('A6 one contiguous frame_num chain', not gaps,
                '%d gap(s)%s' % (len(gaps),
                                 '' if not gaps
                                 else ' at decode indices %s' % gaps[:8])))

    # A7 -- transitive chain depth
    if refresh is not None:
        deep = {}
        for view, sel in per_view.items():
            depth = None
            worst = 0
            for s in sel:
                if s['slice_type'] == I:
                    depth = 0
                elif depth is None:
                    depth = None    # window opened mid-chain: unknown
                else:
                    depth += 1
                    worst = max(worst, depth)
            deep[view] = worst
        ok = all(deep[v] <= period[v] for v in deep)
        out.append(('A7 chain depth <= intra_refresh_frames', ok,
                    'worst depth: main %d (bound %d) aux %d (bound %d)'
                    % (deep['main'], refresh, deep['aux'], refresh_aux)))
    return out


def assert_gate_single_view(sps_seen, sps, pics):
    """The assertions that MEAN something for a plain AVC420 stream.

    None of A1-A7 do. They are assertions about a two-view long-term-
    reference chain -- own-slot retargeting, paired cuts across views,
    LT1 self-marking -- and a single-view stream has no second view to
    pair with and no LTR slots to retarget to. Running them against
    AVC420 does not test the arm, it tests whether the arm is AVC444.

    So this is a different, smaller gate. Every check is POSITIVE: it
    asserts something is present, never that something is absent, so a
    capture this tool failed to parse cannot pass it quietly. S1 in
    particular is the guard on the framing assumption above -- if the
    single-view record layout were wrong, no slice header would parse
    and S1 would fail loudly rather than S2-S4 passing on an empty set.
    """
    out = []
    main = [s for s in pics if s['view'] == 'main']
    aux = [s for s in pics if s['view'] == 'aux']

    out.append(('S1 pictures parsed from the capture', len(main) > 0,
                '%d picture(s) parsed' % len(main)
                if main else 'NO slice header parsed -- the record '
                'framing is wrong or the capture is empty'))

    out.append(('S2 single view, no aux sub-stream', not aux,
                'no aux pictures, as an AVC420 arm must emit'
                if not aux else '%d aux picture(s) in a stream that '
                'should have none' % len(aux)))

    n_sps = len(sps_seen)
    out.append(('S3 SPS repeated in band', n_sps >= 2,
                '%d SPS NAL(s) in the capture' % n_sps))

    # S4 -- frame_num is contiguous WITHIN each IDR period. An IDR
    # legitimately resets it, which is why this is not A6: A6 forbids
    # the reset outright because the AVC444 LTR chain never takes one.
    gaps = []
    expect = None
    for i, sh in enumerate(main):
        if sh['idr']:
            expect = (sh['frame_num'] + 1) % (1 << sps['log2_max_frame_num'])
            continue
        if expect is not None and sh['frame_num'] != expect:
            gaps.append((i, expect, sh['frame_num']))
        expect = (sh['frame_num'] + 1) % (1 << sps['log2_max_frame_num'])
    out.append(('S4 frame_num contiguous within each IDR period',
                not gaps,
                'one chain per IDR period'
                if not gaps else '%d break(s), first at decode index '
                '%d: expected %d, got %d'
                % (len(gaps), gaps[0][0], gaps[0][1], gaps[0][2])))
    return out


def run_assert_gate(sps, pics, refresh, allow_idr, refresh_aux=None,
                    sps_seen=None):
    if SINGLE_VIEW:
        checks = assert_gate_single_view(sps_seen or [], sps, pics)
        print('=== ASSERT GATE (single view, AVC420) ===')
        print('  The AVC444 two-view assertions A1-A7 are NOT run: they')
        print('  assert properties of a long-term-reference chain across')
        print('  two views, which this configuration does not have. The')
        print('  checks below are what a single-view stream can be held')
        print('  to. See assert_gate_single_view().')
    else:
        checks = assert_gate(sps, pics, refresh, allow_idr, refresh_aux)
        print('=== ASSERT GATE (BACKLOG #45 step 0) ===')
    failed = 0
    skipped = 0
    for name, ok, detail in checks:
        word = 'SKIP' if ok is None else ('PASS' if ok else 'FAIL')
        print('  %-40s %-4s  %s' % (name, word, detail))
        if ok is None:
            skipped += 1
        elif not ok:
            failed += 1
    print()
    if failed:
        print('ASSERT VERDICT: FAIL -- %d of %d checks violated'
              % (failed, len(checks)))
        return 1
    if skipped:
        # named in the verdict line, so a skipped check can never be
        # read as a clean sweep by someone skimming for "PASS"
        print('ASSERT VERDICT: PASS -- %d checks clean, %d NOT APPLICABLE '
              'to this capture (read the SKIP line above)'
              % (len(checks) - skipped, skipped))
        return 0
    print('ASSERT VERDICT: PASS -- %d checks, all clean' % len(checks))
    return 0


def main():
    global SINGLE_VIEW
    argv = sys.argv[1:]
    annexb = False
    do_assert = False
    refresh = None
    refresh_aux = None
    allow_idr = 0
    rest = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--annexb':
            annexb = True
        elif a == '--assert':
            do_assert = True
        elif a == '--intra-refresh':
            i += 1
            refresh = int(argv[i])
            if refresh < 1:
                sys.exit('--intra-refresh must be >= 1')
        elif a == '--intra-refresh-aux':
            i += 1
            refresh_aux = int(argv[i])
            if refresh_aux < 1:
                sys.exit('--intra-refresh-aux must be >= 1')
        elif a == '--single-view':
            SINGLE_VIEW = True
        elif a == '--allow-idr':
            i += 1
            allow_idr = int(argv[i])
        else:
            rest.append(a)
        i += 1
    argv = rest
    if not argv:
        sys.exit(__doc__)
    path = argv[0]
    label = argv[1] if len(argv) > 1 else path
    max_pics = int(argv[2]) if len(argv) > 2 else 10 ** 9
    if annexb:
        rv = audit_annexb(path, label, max_pics)
        if do_assert and rv != 0:
            print('ASSERT VERDICT: FAIL -- child stream fails the guard')
        sys.exit(rv)
    sps_seen, sps, pps, pics = audit(path, label, max_pics)
    if not pics:
        sys.exit('%s: no pictures parsed' % label)

    print('=== %s ===' % label)
    print('SPS: %dx%d profile=%d level=%d max_num_ref_frames=%d '
          'log2_max_frame_num=%d poc_type=%d gaps=%d  (%d SPS NALs, '
          'views: %s)'
          % (sps['width'], sps['height'], sps['profile'], sps['level'],
             sps['max_num_ref_frames'], sps['log2_max_frame_num'],
             sps['poc_type'], sps['gaps_allowed'], len(sps_seen),
             ','.join(sorted({v for v, _ in sps_seen}))))
    print('PPS: cabac=%d num_ref_idx_l0_default=%d weighted_pred=%d'
          % (pps['cabac'], pps['num_ref_idx_l0_default'],
             pps['weighted_pred']))
    print()
    print('first 12 pictures (decode order):')
    for sh in pics[:12]:
        kind, refs, mark = describe(sh)
        print('  %-4s fn=%-6d %-3s %-10s %-10s %7dB'
              % (sh['view'], sh['frame_num'], kind, refs, mark,
                 sh['bytes']))
    print()

    own_slot = {'main': 0, 'aux': 1}
    problems = []
    # The narrative below reasons about LTR slots, cross-view prediction
    # and a shared frame_num chain. A single-view AVC420 stream has none
    # of those by design, so running it there manufactures "problems"
    # out of correct bytes -- which is exactly what happened to arm x035
    # on 2026-08-10. Hand straight to the single-view gate instead.
    if SINGLE_VIEW:
        print('single view (AVC420): the LTR / cross-view narrative does')
        print('not apply and is not printed. %d picture(s) parsed.'
              % len(pics))
        print()
        if do_assert:
            sys.exit(run_assert_gate(sps, pics, refresh, allow_idr,
                                     refresh_aux, sps_seen))
        sys.exit(0)
    for view in ('main', 'aux'):
        sel = [s for s in pics if s['view'] == view]
        if not sel:
            continue
        inter = [s for s in sel if s['slice_type'] == P]
        intra = [s for s in sel if s['slice_type'] == I]
        idr = [s for s in sel if s['idr']]
        own = [s for s in inter
               if s['rplm'] and s['rplm'][0] == (2, own_slot[view])]
        other = [s for s in inter
                 if s['rplm'] and s['rplm'][0][0] == 2
                 and s['rplm'][0][1] != own_slot[view]]
        nomod = [s for s in inter if not s['rplm']]
        marked = [s for s in sel
                  if s['marking'] and s['marking'][0] == (6, own_slot[view])]
        ibytes = sum(s['bytes'] for s in intra)   # intra includes IDRs
        pbytes = sum(s['bytes'] for s in inter)
        print('%-4s pictures=%-5d  P(inter)=%-5d  I(intra)=%-4d  IDR=%d'
              % (view, len(sel), len(inter), len(intra) - len(idr), len(idr)))
        print('      P slices retargeted to OWN slot LT%d : %d/%d'
              % (own_slot[view], len(own), len(inter)))
        print('      P slices retargeted to OTHER slot    : %d'
              % len(other))
        print('      P slices with NO list modification   : %d' % len(nomod))
        print('      pictures self-marking into LT%d      : %d/%d'
              % (own_slot[view], len(marked), len(sel)))
        print('      avg P size=%.0fB   avg I/IDR size=%.0fB   '
              'inter share of pictures=%.1f%%'
              % (pbytes / len(inter) if inter else 0,
                 ibytes / (len(intra) or 1),
                 100.0 * len(inter) / len(sel)))
        if other:
            problems.append('%s: %d P slices reference the other view'
                            % (view, len(other)))
        if nomod:
            problems.append('%s: %d P slices carry no list modification'
                            % (view, len(nomod)))
        if len(marked) != len(sel) - len(idr):
            problems.append('%s: %d/%d non-IDR pictures self-mark'
                            % (view, len(marked), len(sel) - len(idr)))
    # merged decode-order frame_num chain: one shared, +1 per picture
    gaps = 0
    for a, b_ in zip(pics, pics[1:]):
        if b_['idr']:
            continue
        if (a['frame_num'] + 1) % (1 << sps['log2_max_frame_num']) \
                != b_['frame_num']:
            gaps += 1
    print()
    print('merged decode-order frame_num chain: %d pictures, %d gaps '
          '(0 = one contiguous chain, as a single decoder requires)'
          % (len(pics), gaps))
    if gaps:
        problems.append('frame_num chain has %d gaps' % gaps)
    print()
    if problems:
        print('VERDICT: PROBLEMS -- ' + '; '.join(problems))
        if do_assert:
            print()
            sys.exit(run_assert_gate(sps, pics, refresh, allow_idr,
                                             refresh_aux) or 1)
        sys.exit(1)
    print('VERDICT: both views are inter-coded from their OWN previous '
          'picture (main<-LT0, aux<-LT1); no cross-view prediction.')
    if do_assert:
        print()
        sys.exit(run_assert_gate(sps, pics, refresh, allow_idr,
                                      refresh_aux))


if __name__ == '__main__':
    main()
