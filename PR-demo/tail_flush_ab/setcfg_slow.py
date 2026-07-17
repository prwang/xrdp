#!/usr/bin/env python3
# Write a deliberately SLOW libx264 encoder (>10ms/1080p frame) to trigger the
# runner pump() 10ms race, plus tail_flush. Preserves other gfx.toml sections.
import sys, re
tail_flush = sys.argv[1]
path="/etc/xrdp/gfx.toml"; lines=open(path).read().splitlines()
out=[]; skip=False
for ln in lines:
    if ln.strip()=="[avc444_ffmpeg]": skip=True; continue
    if skip:
        if re.match(r"^\s*\[[^\]]+\]\s*$", ln): skip=False
        else: continue
    out.append(ln)
block=f'''[avc444_ffmpeg]
path = "/usr/bin/ffmpeg"
avc_mode = "auto"
tail_flush = {tail_flush}
encoder_args = [
  "-c:v", "libx264",
  "-preset", "veryslow",
  "-tune", "zerolatency",
  "-x264-params", "sliced-threads=0:threads=1",
  "-crf", "20",
]
'''
res=[]; ins=False
for ln in out:
    if not ins and re.match(r"^\s*\[(x264|OpenH264)", ln): res.append(block); ins=True
    res.append(ln)
if not ins: res.append(block)
open(path,"w").write("\n".join(res)+"\n")
print(f"wrote SLOW libx264 veryslow, tail_flush={tail_flush}")
