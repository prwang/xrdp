#!/usr/bin/env python3
"""Structural inventory of an oracle AVC444 dump, beyond the SPS.

Reconstructs the TRUE decode-order stream (records in wire order, LC1 main
/ LC2 aux payloads concatenated exactly as the client feeds its single
decoder), then reports per-NAL: record index, LC, nal type, and for slices
frame_num / first_mb_in_slice / slice_type / idr_pic_id via a minimal
slice-header bit parse (enough for pic_order_cnt_type == 2 streams).

Usage: wire_inventory.py /path/dump.bin [max_records]
"""
import struct
import sys


class Bits:
    def __init__(self, buf):
        # unescape emulation-prevention bytes
        out = bytearray()
        i = 0
        while i < len(buf):
            if i + 2 < len(buf) and buf[i] == 0 and buf[i+1] == 0 \
                    and buf[i+2] == 3:
                out += buf[i:i+2]
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


def nals_of(buf):
    """Split Annex-B buffer into NAL payloads (without start codes)."""
    out = []
    i = buf.find(b'\x00\x00\x01')
    while i >= 0:
        j = buf.find(b'\x00\x00\x01', i + 3)
        end = j if j >= 0 else len(buf)
        # trim a trailing zero byte of a 4-byte start code of the NEXT nal
        raw = buf[i + 3:end]
        if raw.endswith(b'\x00'):
            raw = raw[:-1]
        out.append(raw)
        i = j
    return out


def parse(path, max_records):
    data = open(path, 'rb').read()
    pos = 0
    rec_i = 0
    log2_max_frame_num = 8  # updated from SPS when seen
    rows = []
    while pos + 4 <= len(data) and rec_i < max_records:
        (ln,) = struct.unpack_from('<I', data, pos)
        pos += 4
        rec = data[pos:pos + ln]
        pos += ln
        if len(rec) != ln:
            break
        (w,) = struct.unpack_from('<I', rec, 0)
        avc1len = w & 0x3FFFFFFF
        lc = (w >> 30) & 0x3
        (nrects,) = struct.unpack_from('<I', rec, 4)
        # LC=2: avc1len == 0 and the aux AVC420_BITMAP_STREAM occupies the
        # rest of the record (observed on the wire, 2026-07-27)
        end = 4 + avc1len if lc != 2 else len(rec)
        stream = rec[8 + nrects * 10:end]
        rects = [struct.unpack_from('<4H', rec, 8 + k * 8)
                 for k in range(nrects)]
        for nal in nals_of(stream):
            t = nal[0] & 0x1F
            info = ''
            if t == 7:
                b = Bits(nal[1:])
                b.u(24)                      # profile+constraints+level
                b.ue()                       # sps id
                prof = nal[1]
                if prof in (100, 110, 122, 244, 44, 83, 86, 118, 128):
                    if b.ue() == 3:          # chroma_format_idc
                        b.u(1)
                    b.ue(); b.ue(); b.u(1)   # bit depths, transform bypass
                    if b.u(1):               # scaling matrix
                        info = ' scaling!'
                log2_max_frame_num = b.ue() + 4
                info += ' log2_max_frame_num=%d' % log2_max_frame_num
            elif t in (1, 5):
                b = Bits(nal[1:])
                first_mb = b.ue()
                stype = b.ue()
                b.ue()                       # pps id
                fnum = b.u(log2_max_frame_num)
                idr = ''
                if t == 5:
                    idr = ' idr_pic_id=%d' % b.ue()
                info = (' first_mb=%d type=%d frame_num=%d%s'
                        % (first_mb, stype % 5, fnum, idr))
            rows.append('rec=%03d lc=%d nal=%d len=%d rects=%d%s%s'
                        % (rec_i, lc, t, len(nal), nrects,
                           ' %s' % rects[:2] if t in (1, 5) and rec_i < 6
                           else '', info))
        rec_i += 1
    return rows


if __name__ == '__main__':
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 24
    for r in parse(sys.argv[1], limit):
        print(r)
