#!/usr/bin/env python3
"""Rewrite the [avc444_ffmpeg] block of /etc/xrdp/gfx.toml with a chosen VAAPI
async_depth and tail_flush; preserve every other section verbatim."""
import sys, re
async_depth = sys.argv[1]
tail_flush = sys.argv[2]          # "true" / "false"
path = "/etc/xrdp/gfx.toml"
txt = open(path).read()
lines = txt.splitlines()
out = []
i = 0
skipping = False
while i < len(lines):
    ln = lines[i]
    if ln.strip() == "[avc444_ffmpeg]":
        skipping = True
        i += 1
        continue
    if skipping:
        # stop skipping at the next top-level [section] header
        if re.match(r"^\s*\[[^\]]+\]\s*$", ln) and ln.strip() != "[avc444_ffmpeg]":
            skipping = False
        else:
            i += 1
            continue
    out.append(ln)
    i += 1

block = f"""[avc444_ffmpeg]
path = "/usr/bin/ffmpeg"
avc_mode = "auto"
tail_flush = {tail_flush}
encoder_args = [
  "-vaapi_device", "/dev/dri/renderD128",
  "-vf", "format=nv12,hwupload",
  "-c:v", "h264_vaapi",
  "-rc_mode", "CQP", "-qp", "20",
  "-bf", "0",
  "-async_depth", "{async_depth}",
]
"""
# insert the fresh block right after the [codec] table (before first [x264]/[Open)
res = []
inserted = False
for ln in out:
    if not inserted and re.match(r"^\s*\[(x264|OpenH264)", ln):
        res.append(block)
        inserted = True
    res.append(ln)
if not inserted:
    res.append(block)
open(path, "w").write("\n".join(res) + "\n")
print(f"wrote async_depth={async_depth} tail_flush={tail_flush}")
