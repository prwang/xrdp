#!/usr/bin/env python3
"""Order-preserving scan of MS-RDPEGFX PDUs in a FreeRDP transport dump.

Why this exists (BACKLOG #48): the oracle-dump harness
(PR-demo/oracle_client/patch_gfx_oracle.py) records only the AVC420/AVC444
surface-command PAYLOADS, so it cannot show the DELETE_SURFACE /
CREATE_SURFACE / MAP_SURFACE_TO_OUTPUT PDUs the re-key emits, nor their
order relative to that frame's first wire-to-surface PDU. FreeRDP's own
`/dump:record,file:<f>` records the POST-TLS transport byte stream, and
xrdp emits every EGFX PDU as RDP8_BULK_ENCODED_DATA with header byte 0x04
-- compression type RDP8 WITHOUT the PACKET_COMPRESSED bit -- i.e. the
RDPGFX_HEADERs and their bodies are literal bytes on that stream.

So this is a byte-pattern scan, not a protocol stack: it finds each
RDP_SEGMENTED_DATA group xrdp wrote (xrdp_egfx.c) and reports the PDUs in
the order their bytes appear. That is exactly the ordering question #48
asks, and it survives DVC chunking (chunking splits bytes, it never
reorders them).

Frame shapes matched (xrdp_egfx.c):
  SINGLE     E0 04 <cmdId u16> <flags u16=0> <pduLength u32> <body>
  MULTIPART  E1 <segCount u16> <uncompSize u32> <segSize u32>
             04 <cmdId u16> <flags u16=0> <pduLength u32> <body...>

A match is accepted only when the length fields agree with the PDU's
fixed body size, so an accidental byte pattern inside H.264 payload is
rejected rather than silently reported.

Usage: egfx_pdu_scan.py <transport-dump> [--json out.json]
"""
import json
import struct
import sys

CMD = {
    0x0001: 'WIRE_TO_SURFACE_1', 0x0002: 'WIRE_TO_SURFACE_2',
    0x0003: 'DELETE_ENCODING_CONTEXT', 0x0004: 'SOLIDFILL',
    0x0005: 'SURFACE_TO_SURFACE', 0x0006: 'SURFACE_TO_CACHE',
    0x0007: 'CACHE_TO_SURFACE', 0x0008: 'EVICT_CACHE_ENTRY',
    0x0009: 'CREATE_SURFACE', 0x000A: 'DELETE_SURFACE',
    0x000B: 'START_FRAME', 0x000C: 'END_FRAME',
    0x000D: 'FRAME_ACKNOWLEDGE', 0x000E: 'RESET_GRAPHICS',
    0x000F: 'MAP_SURFACE_TO_OUTPUT', 0x0010: 'CACHE_IMPORT_OFFER',
    0x0011: 'CACHE_IMPORT_REPLY', 0x0012: 'CAPS_ADVERTISE',
    0x0013: 'CAPS_CONFIRM', 0x0015: 'MAP_SURFACE_TO_WINDOW',
    0x0016: 'QOE_FRAME_ACKNOWLEDGE',
    0x0017: 'MAP_SURFACE_TO_SCALED_OUTPUT',
    0x0018: 'MAP_SURFACE_TO_SCALED_WINDOW',
}

# cmdId -> exact pduLength for the fixed-size PDUs xrdp emits SINGLE
FIXED_LEN = {
    0x0009: 15,     # surfaceId u16, w u16, h u16, pixelFormat u8
    0x000A: 10,     # surfaceId u16
    0x000F: 20,     # surfaceId u16, reserved u16, x u32, y u32
    0x000B: 16,     # timestamp u32, frameId u32
    0x000C: 12,     # frameId u32
}


def decode_body(cmd, body):
    """Decode the fields this audit cares about."""
    try:
        if cmd == 0x0009:
            sid, w, h, pf = struct.unpack_from('<HHHB', body, 0)
            return {'surface': sid, 'w': w, 'h': h, 'pixel_format': pf}
        if cmd == 0x000A:
            return {'surface': struct.unpack_from('<H', body, 0)[0]}
        if cmd == 0x000F:
            sid, _res, x, y = struct.unpack_from('<HHII', body, 0)
            return {'surface': sid, 'x': x, 'y': y}
        if cmd == 0x000B:
            ts, fid = struct.unpack_from('<II', body, 0)
            return {'timestamp': ts, 'frame_id': fid}
        if cmd == 0x000C:
            return {'frame_id': struct.unpack_from('<I', body, 0)[0]}
        if cmd == 0x0001:
            sid, codec, pf, x1, y1, x2, y2, blen = struct.unpack_from(
                '<HHBHHHHI', body, 0)
            return {'surface': sid, 'codec': codec, 'pixel_format': pf,
                    'dest': (x1, y1, x2, y2), 'bitmap_len': blen}
    except struct.error:
        pass
    return {}


def scan(data):
    """Return the EGFX PDUs in byte order: (offset, kind, cmd, fields)."""
    out = []
    n = len(data)
    i = 0
    while i < n - 16:
        b = data[i]
        if b == 0xE0 and data[i + 1] == 0x04:
            cmd, flags, plen = struct.unpack_from('<HHI', data, i + 2)
            want = FIXED_LEN.get(cmd)
            if (flags == 0 and cmd in CMD and want is not None
                    and plen == want and i + 2 + plen <= n):
                out.append((i, 'SINGLE', cmd,
                            decode_body(cmd, data[i + 10:i + 2 + plen])))
                i += 2 + plen
                continue
        elif b == 0xE1:
            try:
                segs, unc, seg0 = struct.unpack_from('<HII', data, i + 1)
                bulk_hdr = data[i + 11]
                cmd, flags, plen = struct.unpack_from('<HHI', data, i + 12)
            except (struct.error, IndexError):
                i += 1
                continue
            # first segment = 1 header byte + the RDPGFX header and the
            # PDU's fixed prefix; uncompressedSize is the whole PDU
            if (bulk_hdr == 0x04 and flags == 0 and cmd in (0x0001, 0x0002)
                    and unc == plen and 1 < seg0 < 64 and segs >= 1
                    and plen >= seg0):
                body_off = i + 20
                out.append((i, 'MULTIPART', cmd,
                            decode_body(cmd, data[body_off:body_off + 32])))
                i += 20
                continue
        i += 1
    return out


def main():
    path = sys.argv[1]
    data = open(path, 'rb').read()
    pdus = scan(data)
    print('%s: %d bytes, %d EGFX PDUs' % (path, len(data), len(pdus)))
    counts = {}
    for _o, _k, c, _f in pdus:
        counts[CMD[c]] = counts.get(CMD[c], 0) + 1
    for k in sorted(counts):
        print('  %-28s %d' % (k, counts[k]))
    if '--json' in sys.argv:
        out = sys.argv[sys.argv.index('--json') + 1]
        with open(out, 'w') as f:
            json.dump([{'off': o, 'frame': k, 'cmd': CMD[c], 'id': c,
                        'fields': fl} for o, k, c, fl in pdus], f)
        print('wrote', out)
    else:
        for o, k, c, fl in pdus[:80]:
            print('  @%-10d %-9s %-24s %s' % (o, k, CMD[c], fl))


if __name__ == '__main__':
    main()
