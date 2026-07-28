#!/usr/bin/env python3
"""Parse RFX_AVC444_BITMAP_STREAM dumps and list H.264 NAL structure of each
sub-bitstream. Layout (MS-RDPEGFX 2.2.4.5 / 2.2.4.4):
  UINT32 header: bits0-29 = cbAvc420EncodedBitstream1, bits30-31 = LC
  stream1: RFX_AVC420_BITMAP_STREAM (len = cbAvc420EncodedBitstream1, unless LC=1
           then stream1 spans the rest; LC=2 => cb1=0 and only stream2 aux)
  stream2: RFX_AVC420_BITMAP_STREAM (the remainder) when LC==0
  RFX_AVC420_BITMAP_STREAM:
    UINT32 numRegionRects
    RDPGFX_RECT16 rects[n]   (4x UINT16: left,top,right,bottom)
    RFX_AVC420_METABLOCK quant[n] (1 byte qpVal(6b)+flags, 1 byte qualityVal)
    BYTE  avc420[]           (H.264 Annex B)
"""
import sys, struct

def rects_and_h264(buf):
    """Given a full RFX_AVC420_BITMAP_STREAM buffer, return (n, rects, h264off)."""
    n = struct.unpack_from('<I', buf, 0)[0]
    off = 4
    rects = []
    for i in range(n):
        l,t,r,b = struct.unpack_from('<HHHH', buf, off); off += 8
        rects.append((l,t,r,b))
    off += n * 2  # quantQualityVals
    return n, rects, off

def nal_list(h264):
    """Annex B: walk start codes, return list of (nal_type, size)."""
    out = []
    i = 0; L = len(h264)
    starts = []
    while i < L-3:
        if h264[i]==0 and h264[i+1]==0 and h264[i+2]==1:
            starts.append(i+3); i+=3
        elif i<L-4 and h264[i]==0 and h264[i+1]==0 and h264[i+2]==0 and h264[i+3]==1:
            starts.append(i+4); i+=4
        else:
            i+=1
    for k,s in enumerate(starts):
        e = starts[k+1]-4 if k+1 < len(starts) else L
        if e < s: e = L
        ntype = h264[s] & 0x1f
        out.append((ntype, e-s))
    return out

NALN = {1:'P/slice',5:'IDR',6:'SEI',7:'SPS',8:'PPS',9:'AUD',
        19:'aux-slice?',20:'ext'}

def parse(path):
    with open(path,'rb') as f: data=f.read()
    hdr = struct.unpack_from('<I', data, 0)[0]
    cb1 = hdr & 0x3fffffff
    lc  = (hdr >> 30) & 0x3
    print(f"{path}: total={len(data)} LC={lc} cbStream1={cb1}")
    body = data[4:]
    if lc == 2:
        # only aux (stream2), whole body
        n,rects,h = rects_and_h264(body)
        print(f"  AUX(LC2): nRects={n} rects={rects}")
        print(f"    NALs={nal_list(body[h:])}")
        return
    s1 = body[:cb1] if lc==0 else body
    n1,r1,h1 = rects_and_h264(s1)
    print(f"  stream1(main): nRects={n1} rects={r1}")
    print(f"    NALs={nal_list(s1[h1:])}")
    if lc == 0:
        s2 = body[cb1:]
        n2,r2,h2 = rects_and_h264(s2)
        print(f"  stream2(aux):  nRects={n2} rects={r2}")
        print(f"    NALs={nal_list(s2[h2:])}")

if __name__=='__main__':
    for p in sys.argv[1:]:
        parse(p); print()
