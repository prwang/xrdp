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

Usage: avc444_ltr_wire_audit.py <dump.bin> [label] [max_pictures]
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


def views_of(rec):
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


def main():
    path = sys.argv[1]
    label = sys.argv[2] if len(sys.argv) > 2 else path
    max_pics = int(sys.argv[3]) if len(sys.argv) > 3 else 10 ** 9
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
        sys.exit(1)
    print('VERDICT: both views are inter-coded from their OWN previous '
          'picture (main<-LT0, aux<-LT1); no cross-view prediction.')


if __name__ == '__main__':
    main()
