#!/usr/bin/env python3
"""Feasibility spike: merge a main P-chain and an all-IDR aux stream into
ONE H.264 chain where aux frames are non-reference, non-IDR I leaves.

Rewrite per aux IDR slice (validated preconditions: poc_type=2, CABAC,
single slice, no fields, pic_init_qp equal across children):
  nal header  : type 5 -> 1, nal_ref_idc 3 -> 0
  slice header: drop idr_pic_id (ue) and dec_ref_pic_marking (2 bits),
                set frame_num, keep everything else bit-exact,
                re-pad cabac_alignment_one_bit, copy payload verbatim.

Usage: aux_leaf_spike.py main.h264 aux_allidr.h264 out_merged.h264 fnrule
       fnrule: 'prevref' or 'prevref+1'
"""
import sys


def unescape(b):
    out = bytearray()
    i = 0
    while i < len(b):
        if i + 2 < len(b) and b[i] == 0 and b[i+1] == 0 and b[i+2] == 3:
            out += b[i:i+2]
            i += 3
        else:
            out.append(b[i])
            i += 1
    return bytes(out)


def escape(b):
    out = bytearray()
    zeros = 0
    for byte in b:
        if zeros >= 2 and byte <= 3:
            out.append(3)
            zeros = 0
        out.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return bytes(out)


class R:
    def __init__(self, buf):
        self.b = buf
        self.p = 0

    def u(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.b[self.p >> 3] >> (7 - (self.p & 7))) & 1)
            self.p += 1
        return v

    def ue(self):
        z = 0
        while self.u(1) == 0:
            z += 1
            assert z < 32
        return (1 << z) - 1 + (self.u(z) if z else 0)

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k % 2 else -(k // 2)


class W:
    def __init__(self):
        self.bits = []

    def u(self, v, n):
        for i in range(n - 1, -1, -1):
            self.bits.append((v >> i) & 1)

    def ue(self, v):
        v += 1
        n = v.bit_length()
        self.u(0, n - 1)
        self.u(v, n)

    def se(self, v):
        self.ue(2 * v - 1 if v > 0 else -2 * v)

    def copy_bits(self, r, n):
        for _ in range(n):
            self.bits.append(r.u(1))

    def tobytes(self):
        by = bytearray()
        for i in range(0, len(self.bits), 8):
            chunk = self.bits[i:i+8]
            chunk += [0] * (8 - len(chunk))
            v = 0
            for bit in chunk:
                v = (v << 1) | bit
            by.append(v)
        return bytes(by)


def nal_units(path):
    buf = open(path, 'rb').read()
    out = []
    i = buf.find(b'\x00\x00\x01')
    while i >= 0:
        j = buf.find(b'\x00\x00\x01', i + 3)
        e = j if j >= 0 else len(buf)
        raw = buf[i+3:e]
        if raw.endswith(b'\x00'):
            raw = raw[:-1]
        if raw:
            out.append(raw)
        i = j
    return out


def access_units(nals):
    """Group NALs into AUs: prefix non-VCL NALs attach to next VCL."""
    aus = []
    cur = []
    for n in nals:
        cur.append(n)
        if (n[0] & 0x1F) in (1, 5):
            aus.append(cur)
            cur = []
    assert not cur, 'trailing non-VCL NALs'
    return aus


LOG2_MAX_FRAME_NUM = 8  # both children: log2_max_frame_num_minus4 = 4


def idr_to_leaf(nal, new_frame_num):
    """nal: raw IDR NAL (escaped, with header byte). Returns leaf NAL."""
    assert (nal[0] & 0x1F) == 5, 'not IDR'
    rbsp = unescape(nal[1:])
    r = R(rbsp)
    w = W()
    first_mb = r.ue()
    stype = r.ue()
    pps_id = r.ue()
    fn = r.u(LOG2_MAX_FRAME_NUM)
    assert stype % 5 == 2, 'not an I slice: %d' % stype
    idr_pic_id = r.ue()
    r.u(2)   # no_output_of_prior_pics_flag, long_term_reference_flag
    w.ue(first_mb)
    w.ue(stype)
    w.ue(pps_id)
    w.u(new_frame_num, LOG2_MAX_FRAME_NUM)
    # remaining header: slice_qp_delta se, deblock idc ue (+2 se if idc != 1)
    hdr_start = r.p
    r.se()          # slice_qp_delta
    idc = r.ue()    # disable_deblocking_filter_idc
    if idc != 1:
        r.se()
        r.se()
    hdr_end = r.p
    r.p = hdr_start
    w.copy_bits(r, hdr_end - hdr_start)
    # cabac_alignment_one_bit padding in the SOURCE up to byte boundary
    while r.p & 7:
        assert r.u(1) == 1, 'bad cabac alignment bit'
    # re-pad in the destination
    while len(w.bits) & 7:
        w.u(1, 1)
    out = bytes([0x01]) + escape(w.tobytes() + rbsp[r.p >> 3:])
    return out, fn, idr_pic_id


def main():
    main_p, aux_p, out_p, fnrule = sys.argv[1:5]
    main_aus = access_units(nal_units(main_p))
    aux_aus = access_units(nal_units(aux_p))
    n = min(len(main_aus), len(aux_aus))
    merged = bytearray()
    prev_ref_fn = None
    for k in range(n):
        for nal in main_aus[k]:
            t = nal[0] & 0x1F
            if t in (1, 5):
                r = R(unescape(nal[1:]))
                r.ue(); r.ue(); r.ue()
                prev_ref_fn = r.u(LOG2_MAX_FRAME_NUM)
            merged += b'\x00\x00\x00\x01' + nal
        leaf_fn = prev_ref_fn if fnrule == 'prevref' \
            else (prev_ref_fn + 1) % (1 << LOG2_MAX_FRAME_NUM)
        idr = [x for x in aux_aus[k] if (x[0] & 0x1F) == 5]
        assert len(idr) == 1, 'aux AU %d: %d IDR nals' % (k, len(idr))
        leaf, _, _ = idr_to_leaf(idr[0], leaf_fn)
        merged += b'\x00\x00\x00\x01' + leaf
    open(out_p, 'wb').write(bytes(merged))
    print('merged %d main+%d leaf AUs -> %s (%d bytes), fnrule=%s'
          % (n, n, out_p, len(merged), fnrule))


if __name__ == '__main__':
    main()
