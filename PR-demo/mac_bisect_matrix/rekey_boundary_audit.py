#!/usr/bin/env python3
"""BACKLOG #48 acceptance audit: the aux_ltr_chain re-key BOUNDARY on the wire.

Two independent wire captures of the SAME connection are cross-checked:

  1. the oracle dump (/tmp/oracle_avc_s<id>.bin) -- every AVC444 surface
     command PAYLOAD, u32-length-prefixed, in wire order. Gives the damage
     region (the AVC420 regionRects preamble), the H.264 slice type and the
     shared LTR frame_num.
  2. FreeRDP's transport dump (/dump:record) -- the post-TLS byte stream.
     xrdp writes EGFX as RDP8_BULK_ENCODED_DATA with the COMPRESSED bit
     clear, so the PDUs are literal bytes; egfx_pdu_scan gives their ORDER.

The audit answers, per boundary:
  A. did a re-key happen (main view IDR, shared frame_num back to 0)?
  B. did DELETE_SURFACE -> CREATE_SURFACE -> MAP_SURFACE_TO_OUTPUT arrive
     BEFORE that frame's first WIRE_TO_SURFACE PDU, and does the frame's
     damage region cover the whole surface?
  C. away from the boundary, is every picture still an inter P slice
     retargeted to its own view's long-term slot?

Correlation between the two captures is by payload identity: a WIRE_TO_
SURFACE_1 PDU's bitmapDataLength equals the oracle record length, and the
first bitmap bytes are compared literally.

Usage: rekey_boundary_audit.py <oracle.bin> <transport.dump> [surface_w] [surface_h]
"""
import re
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from egfx_pdu_scan import scan, CMD          # noqa: E402


class Bits:
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
            v = (v << 1) | ((self.b[self.pos >> 3] >>
                             (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def ue(self):
        z = 0
        while self.u(1) == 0:
            z += 1
            if z > 31:
                raise ValueError('bad ue')
        return (1 << z) - 1 + (self.u(z) if z else 0)


def nals(buf):
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


def slice_info(nal, log2_mfn=16):
    """(idr, slice_type, frame_num, ltr_target, ltr_mark) or None."""
    t = nal[0] & 0x1F
    if t not in (1, 5):
        return None
    b = Bits(nal[1:])
    first_mb = b.ue()
    if first_mb != 0:
        return None
    st = b.ue() % 5
    b.ue()                                   # pps id
    fn = b.u(log2_mfn)
    idr = (t == 5)
    idr_pic_id = b.ue() if idr else None
    target = mark = None
    if st == 0:                              # P
        if b.u(1):                           # num_ref_idx_active_override
            b.ue()
        if b.u(1):                           # ref_pic_list_modification_l0
            while True:
                op = b.ue()
                if op == 3:
                    break
                arg = b.ue()
                if target is None and op == 2:
                    target = arg
    if (nal[0] >> 5) & 3:                    # nal_ref_idc != 0
        if idr:
            b.u(1)                           # no_output_of_prior_pics
            mark = ('ltr_flag', b.u(1))
        else:
            if b.u(1):                       # adaptive_ref_pic_marking
                while True:
                    op = b.ue()
                    if op == 0:
                        break
                    if op in (1, 3):
                        b.ue()
                    if op in (2,):
                        b.ue()
                    if op in (3, 6):
                        arg = b.ue()
                        if op == 6 and mark is None:
                            mark = ('mark_lt', arg)
                    if op in (4, 5):
                        if op == 4:
                            b.ue()
    return {'idr': idr, 'type': st, 'frame_num': fn, 'idr_pic_id': idr_pic_id,
            'ltr_target': target, 'mark': mark}


def oracle_records(path):
    """Yield (index, payload, rects, view_lc)."""
    data = open(path, 'rb').read()
    pos = 0
    idx = 0
    while pos + 4 <= len(data):
        (ln,) = struct.unpack_from('<I', data, pos)
        pos += 4
        if pos + ln > len(data) or ln < 8:
            break
        rec = data[pos:pos + ln]
        pos += ln
        (w,) = struct.unpack_from('<I', rec, 0)
        lc = (w >> 30) & 3
        view = rec[4:]
        (nr,) = struct.unpack_from('<I', view, 0)
        rects = []
        for k in range(nr):
            x1, y1, x2, y2 = struct.unpack_from('<HHHH', view, 4 + k * 10)
            rects.append((x1, y1, x2, y2))
        yield idx, rec, view[4 + nr * 10:], rects, lc, ln
        idx += 1


def ordered_oracle_records(paths, dump_pdus):
    """Interleave per-surface oracle dumps back into WIRE order.

    Since the re-key alternates surface ids (BACKLOG #48), the oracle
    harness writes one file per surface -- /tmp/oracle_avc_s0.bin and
    /tmp/oracle_avc_s16.bin. Each file is in order for ITS surface, but
    the relative order across files is only recoverable from the
    transport dump, where every AVC444 WIRE_TO_SURFACE_1 names its
    surface. Walk those PDUs and pop the next record from the matching
    file.
    """
    per_surface = {}
    for path in paths:
        m = re.search(r'_s(\d+)\.bin$', path)
        sid = int(m.group(1)) if m else 0
        per_surface[sid] = list(oracle_records(path))
    cursor = {sid: 0 for sid in per_surface}
    idx = 0
    for _off, _k, c, f in dump_pdus:
        if c != 0x0001 or f.get('codec') not in (14, 15):
            continue
        sid = f.get('surface')
        recs = per_surface.get(sid)
        if recs is None or cursor[sid] >= len(recs):
            continue
        _i, rec, es, rects, lc, ln = recs[cursor[sid]]
        cursor[sid] += 1
        yield idx, rec, es, rects, lc, ln
        idx += 1


def main():
    oracle, dump = sys.argv[1], sys.argv[2]
    sw = int(sys.argv[3]) if len(sys.argv) > 3 else None
    sh_ = int(sys.argv[4]) if len(sys.argv) > 4 else None
    oracle_paths = [p for p in oracle.split(',') if p]

    dump_data = open(dump, 'rb').read()
    dump_pdus = scan(dump_data)

    print('== capture 1: oracle payload dump ==')
    if len(oracle_paths) > 1:
        print('interleaving %d per-surface dumps into wire order'
              % len(oracle_paths))
        source = ordered_oracle_records(oracle_paths, dump_pdus)
    else:
        source = oracle_records(oracle)
    pics = []
    for idx, rec, es, rects, lc, ln in source:
        info = None
        for nal in nals(es):
            info = slice_info(nal) or info
        if info is None:
            continue
        info.update({'rec': idx, 'rects': rects, 'lc': lc, 'reclen': ln,
                     'prefix': es[:32]})
        pics.append(info)
    print('records with a slice: %d' % len(pics))

    idrs = [p for p in pics if p['idr']]
    print('IDR pictures at records: %s' % [p['rec'] for p in idrs])
    boundaries = [p for p in idrs if p['rec'] > 0]

    # C: steady-state shape between boundaries
    bad = [p for p in pics
           if not p['idr'] and (p['type'] != 0 or p['ltr_target'] is None)]
    lt = {}
    for p in pics:
        if not p['idr']:
            lt.setdefault(p['ltr_target'], 0)
            lt[p['ltr_target']] += 1
    print('C: non-IDR pictures: %d, of which not P-with-LTR-retarget: %d'
          % (len(pics) - len(idrs), len(bad)))
    print('C: L0 retarget histogram (long-term slot -> count): %s' % lt)
    par = {}
    for p in pics:
        if not p['idr'] and p['ltr_target'] is not None:
            par.setdefault((p['rec'] % 2, p['ltr_target']), 0)
            par[(p['rec'] % 2, p['ltr_target'])] += 1
    print('C: (record parity, LT slot) -> count: %s' % par)

    print()
    print('== capture 2: post-TLS transport dump (EGFX PDU order) ==')
    pdus = dump_pdus
    counts = {}
    for _o, _k, c, _f in pdus:
        counts[CMD[c]] = counts.get(CMD[c], 0) + 1
    print('%d PDUs: %s' % (len(pdus), counts))

    # surface geometry: take it from the stream itself unless overridden,
    # so "whole surface" is a real test of the damage rect rather than a
    # check that it merely starts at (0, 0)
    for _o, _k, c, f in pdus:
        if c == 0x0009:                      # CREATE_SURFACE
            if sw is None:
                sw = f.get('w')
            if sh_ is None:
                sh_ = f.get('h')
            break
    print('surface geometry used for the whole-surface test: %sx%s'
          % (sw, sh_))

    # ---- D: never map a blank surface (BACKLOG #48 RED, 2026-07-29) ----
    # The defect the owner saw on macOS: the re-key mapped a freshly
    # CREATEd (zero-filled) surface to output BEFORE sending that frame's
    # pixels, so any client that composites when the mapping changes shows
    # black until the IDR decodes. FreeRDP composites at END_FRAME and is
    # structurally blind to it, so no pixel check can gate this -- the
    # wire order is the invariant, and this is it.
    #
    # The FIRST map of the session is exempt: at connect there is no prior
    # content to preserve, so mapping an empty surface is correct.
    painted = {}
    seen_create = set()
    blank_maps = []
    for off, _k, c, f in pdus:
        name = CMD[c]
        sid = f.get('surface')
        if name == 'CREATE_SURFACE':
            painted[sid] = False
            first = sid not in seen_create
            seen_create.add(sid)
            f['_first_create'] = first
        elif name == 'WIRE_TO_SURFACE_1':
            painted[sid] = True
        elif name == 'MAP_SURFACE_TO_OUTPUT':
            if painted.get(sid, True) is False:
                blank_maps.append((off, sid))
    # the connect-time map is the first one in the stream
    connect_map = blank_maps[:1]
    offending = blank_maps[1:]
    print('D: MAP of a surface with no pixels since CREATE: %d '
          '(+%d exempt connect-time map)'
          % (len(offending), len(connect_map)))
    if offending:
        print('D: VIOLATIONS at byte offsets %s'
              % [o for o, _s in offending][:12])
    print('D: NEVER MAP A BLANK SURFACE: %s'
          % ('PASS' if not offending else 'FAIL'))
    print()

    dels = [i for i, (_o, _k, c, _f) in enumerate(pdus) if c == 0x000A]
    print('DELETE_SURFACE occurrences: %d' % len(dels))
    # AVC444 (codec 14 = v1, 15 = v2) surface commands, in wire order
    avc_pdus = [i for i, (_o, _k, c, f) in enumerate(pdus)
                if c == 0x0001 and f.get('codec') in (14, 15)]
    print('AVC444 WIRE_TO_SURFACE_1 PDUs: %d (oracle records: %d)'
          % (len(avc_pdus), len(pics)))

    print()
    print('== boundaries ==')
    for n, p in enumerate(boundaries):
        print('-- boundary %d: oracle record %d --' % (n + 1, p['rec']))
        prev = [q for q in pics if q['rec'] < p['rec']]
        print('   previous picture: rec=%d frame_num=%d %s'
              % (prev[-1]['rec'], prev[-1]['frame_num'],
                 'IDR' if prev[-1]['idr'] else 'P'))
        print('   re-key picture  : rec=%d IDR=%s frame_num=%d idr_pic_id=%s'
              % (p['rec'], p['idr'], p['frame_num'], p['idr_pic_id']))
        print('   damage rects    : %s' % (p['rects'],))
        full = (len(p['rects']) == 1 and p['rects'][0][0] == 0
                and p['rects'][0][1] == 0
                and (sw is None or p['rects'][0][2] == sw)
                and (sh_ is None or p['rects'][0][3] == sh_))
        print('   whole surface   : %s' % ('YES' if full else 'NO'))
        nxt = [q for q in pics if q['rec'] == p['rec'] + 1]
        if nxt:
            print('   paired aux      : rec=%d %s frame_num=%d L0->LT%s'
                  % (nxt[0]['rec'], 'IDR' if nxt[0]['idr'] else 'P',
                     nxt[0]['frame_num'], nxt[0]['ltr_target']))

        # Correlate by ORDER, not by bytes: the Nth AVC444 WIRE_TO_SURFACE_1
        # PDU carries the Nth oracle record, and its bitmapDataLength must
        # equal that record's length. (An IDR's first bytes are the SPS, so
        # a byte-prefix search would match every IDR in the capture.)
        if p['rec'] >= len(avc_pdus):
            print('   TRANSPORT: fewer AVC444 PDUs (%d) than oracle records'
                  % len(avc_pdus))
            continue
        pi = avc_pdus[p['rec']]
        hit = pdus[pi][0]
        blen = pdus[pi][3].get('bitmap_len')
        print('   correlation    : AVC444 W2S1 #%d @%d bitmap_len=%s vs '
              'oracle record len=%d -> %s'
              % (p['rec'], hit, blen, p['reclen'],
                 'MATCH' if blen == p['reclen'] else 'MISMATCH'))
        before = [x for x in pdus if x[0] < hit]
        after = [x for x in pdus if x[0] >= hit]
        ctx = before[-6:]
        print('   PDU order around the re-key frame (byte offsets):')
        for o, k, c, fl in ctx:
            print('      @%-10d %-9s %-24s %s' % (o, k, CMD[c], fl))
        print('      >>> re-key H.264 payload bytes at @%d' % hit)
        for o, k, c, fl in after[:3]:
            print('      @%-10d %-9s %-24s %s' % (o, k, CMD[c], fl))
        # B is judged on the GAP: the PDUs between the PREVIOUS picture's
        # payload and this re-key payload. Judging it on a fixed window of
        # preceding PDUs was wrong -- that window always ends with the
        # previous frame's WIRE_TO_SURFACE_1, which legitimately precedes
        # DELETE_SURFACE, so the ordering test could never pass (it read
        # NO against a capture whose printed order was correct).
        # B: the replacement surface is CREATEd before the pixels and
        # MAPped only after them, with the old surface deleted last.
        # Expected shape, per BACKLOG #48 after the 2026-07-29 RED:
        #   ... CREATE(new) | W2S1(new) W2S1(new) | MAP(new) DELETE(old)
        # The earlier code asserted DELETE < CREATE < MAP < pixels, which
        # is the ORDER THAT CAUSED THE BUG -- it printed YES seven times
        # against a capture that flashed black on macOS.
        prev_hit = pdus[avc_pdus[p['rec'] - 1]][0] if p['rec'] else -1
        gap = [x for x in pdus if prev_hit < x[0] < hit]
        gseq = [CMD[c] for _o, _k, c, _f in gap]
        # window after this frame's two payloads, up to the next picture
        nxt_hit = (pdus[avc_pdus[p['rec'] + 2]][0]
                   if p['rec'] + 2 < len(avc_pdus) else 1 << 62)
        tail = [x for x in pdus if hit < x[0] < nxt_hit]
        tseq = [CMD[c] for _o, _k, c, _f in tail]
        pre_ok = ('CREATE_SURFACE' in gseq
                  and 'MAP_SURFACE_TO_OUTPUT' not in gseq
                  and 'DELETE_SURFACE' not in gseq)
        try:
            m_i = tseq.index('MAP_SURFACE_TO_OUTPUT')
            d_i = tseq.index('DELETE_SURFACE')
            # both views must land before output is handed over
            post_ok = (tseq[:m_i].count('WIRE_TO_SURFACE_1') >= 1
                       and m_i < d_i)
        except ValueError:
            post_ok = False
        ok = pre_ok and post_ok
        print('   B: CREATE before pixels, MAP only after them, old '
              'surface deleted last: %s' % ('YES' if ok else 'NO'))
        print('      before: %s' % ' '.join(gseq))
        print('      after : %s' % ' '.join(tseq))
        print()


if __name__ == '__main__':
    main()
