# NUT test-fixture provenance

## fixture_4frame.nut
- Generator: stock Debian ffmpeg — `ffmpeg version 7.1.5-0+deb13u1 Copyright (c) 2000-2026 the FFmpeg developers`, configured --enable-gpl --enable-libx264.
- Input: 4 synthetic 32x32 NV12 pictures (main0, aux0, main1, aux1 order),
  raw concatenation, 1536 bytes each.
- Exact generator argv (coded 32x32, 120 fps):
  ffmpeg -hide_banner -nostdin -loglevel error -f rawvideo -pixel_format nv12 \
    -video_size 32x32 -framerate 120 -color_range pc -colorspace bt709 \
    -color_primaries bt709 -color_trc bt709 -i pipe:3-equivalent -map 0:v:0 \
    -an -sn -dn -fps_mode passthrough -c:v libx264 -bf 0 -preset ultrafast \
    -tune zerolatency -crf 18 -g 240 -x264-params repeat-headers=1 \
    -bsf:v h264_mp4toannexb -flush_packets 1 -write_index 0 -f nut pipe:1
- Expected: standard-syncpoint NUT, one H264 video stream, 4 ordered packets
  of data_size 1356, 63, 64, 41; packet 0 is a keyframe carrying Annex-B
  SPS(7)+PPS(8)+IDR(5); PTS 0,512,1024,1536 (monotonic).
- SHA-256: 200fe21a88dccbf4b27543c8d18725dddba829b56b2cf3bd3f4b75831f3d6c78
- Post-generation corruption applied: none.
