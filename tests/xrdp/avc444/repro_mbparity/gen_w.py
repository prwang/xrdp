import sys, numpy as np
W=int(sys.argv[1]); H=256
im=np.zeros((H,W,4),np.uint8)   # B,G,R,X
for x0 in range(100, W-40, 96):
    im[40:H-40, x0:x0+48, 1]=255   # green bars (high chroma contrast)
im.tofile('/tmp/w.xrgb')
