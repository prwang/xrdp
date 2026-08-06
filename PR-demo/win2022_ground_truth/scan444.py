#!/usr/bin/env python3
"""Scan a dump dir: per-frame LC and NAL-type summary for stream1/stream2."""
import sys, os, glob, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse444 import rects_and_h264, nal_list

def summ(nals):
    return "+".join(str(t) for t,_ in nals)

def scan(d, limit=None):
    files = sorted(glob.glob(os.path.join(d,"f*_c*.bin")))
    for fp in files:
        code = fp.split('_c')[-1].split('.')[0]
        with open(fp,'rb') as f: data=f.read()
        seq = int(os.path.basename(fp)[1:7])
        if code != '000F' and code != '000E':
            # non-AVC (PLANAR etc.) -> skip detail
            print(f"seq={seq:05d} codec=0x{code} (non-AVC)")
            continue
        hdr = struct.unpack_from('<I', data, 0)[0]
        cb1 = hdr & 0x3fffffff; lc = (hdr>>30)&0x3
        body = data[4:]
        try:
            if lc == 2:
                n,r,h = rects_and_h264(body)
                print(f"seq={seq:05d} LC=2 aux[{summ(nal_list(body[h:]))}] rect={r}")
            else:
                s1 = body[:cb1] if lc==0 else body
                n1,r1,h1 = rects_and_h264(s1)
                line=f"seq={seq:05d} LC={lc} s1[{summ(nal_list(s1[h1:]))}]{r1}"
                if lc==0:
                    s2=body[cb1:]; n2,r2,h2=rects_and_h264(s2)
                    line+=f" s2[{summ(nal_list(s2[h2:]))}]{r2}"
                print(line)
        except Exception as e:
            print(f"seq={seq:05d} LC={lc} PARSE-ERR {e}")
        if limit and seq>=limit: break

if __name__=='__main__':
    scan(sys.argv[1], int(sys.argv[2]) if len(sys.argv)>2 else None)
