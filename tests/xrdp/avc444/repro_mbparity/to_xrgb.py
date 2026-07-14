import numpy as np
from PIL import Image
im=np.asarray(Image.open('/tmp/conv44_src.png').convert('RGB')).astype(np.uint8)
H,W,_=im.shape
out=np.zeros((H,W,4),np.uint8)
out[...,0]=im[...,2]; out[...,1]=im[...,1]; out[...,2]=im[...,0]  # B,G,R,X
out.tofile('/tmp/conv44.xrgb')
print('W',W,'H',H)
