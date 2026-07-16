import sys, numpy as np
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
W,H = im.size
cx,cy = W//2, H//2
a = np.asarray(im.crop((cx-150,cy-150,cx+150,cy+150))).reshape(-1,3).mean(0)
r,g,b = a
names = {'red':(200,40,40),'green':(40,180,40),'blue':(40,40,200),
         'cyan':(40,180,200),'black':(20,20,20)}
best=min(names, key=lambda n: sum((a[i]-names[n][i])**2 for i in range(3)))
verdict = "DELIVERED" if best=='red' else ("WITHHELD" if best in ('green','cyan','blue') else "UNCLEAR")
print(f"center avg rgb=({r:.0f},{g:.0f},{b:.0f}) nearest={best} -> {verdict}")
