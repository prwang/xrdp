# bench — encoder/conversion performance measurements

Standalone measurement helpers used to size the AVC444 work (no live session
needed unless noted):

- `bench_encoders.sh`  — wall-clock encode throughput of the candidate H.264
  backends (ffmpeg VAAPI / libx264 flavours) at the session resolutions.
- `conv_measure.c`     — micro-benchmark for the RGB->YUV conversion loops
  (the 167ms->16ms offload evidence); compile standalone with -O2.
- `measure_traffic.sh` — on-the-wire byte counts per GFX mode from a live
  session (needs the box's xrdp + a client).
- `copy_bandwidth.py`  — memcpy/memory bandwidth sanity numbers for the
  shmem plane copies.
