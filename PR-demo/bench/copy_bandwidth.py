#!/usr/bin/env python3
"""M4: userspace memcpy bandwidth -> per-frame pipe-feed copy cost."""
import numpy as np, time
pic = 1920 * 1088 * 3 // 2          # one AVC444 coded picture, NV12
a = np.zeros(pic, np.uint8); b = np.empty(pic, np.uint8)
N = 3000
t0 = time.perf_counter()
for _ in range(N):
    np.copyto(b, a)
dt = time.perf_counter() - t0
gbps = pic * N / dt / 1e9
ms = 2 * pic / (gbps * 1e9) * 1000  # 444 frame = main+aux
print(f"memcpy ~{gbps:.1f} GB/s; 444 frame (2 pics) 1 copy = {ms:.3f} ms; "
      f"at 60fps = {ms*60/1000*100:.2f}% of one core per copy stage")
