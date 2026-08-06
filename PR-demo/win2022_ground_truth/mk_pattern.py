from PIL import Image, ImageDraw, ImageFont
W, H = 1280, 800

def Y(r,g,b): return 0.2126*r + 0.7152*g + 0.0722*b

# Isoluminant pair: solve two colors with equal Rec.709 luma but opposite chroma.
# A = greenish, B = magenta-ish, both tuned to Y ~ 128.
A = (0, 179, 0)          # Y = 0.7152*179 = 128.0
# magenta R=B=x with Y=128 -> (0.2126+0.0722)*x =128 -> x=449 clip... use R,B mix
B = (222, 0, 222)        # Y = (0.2126+0.0722)*222 = 63.2  -> too low
# retune B so Y matches A (128): (0.2848)*x =128 -> x=449 >255. Cap: use R only side.
# Pick A,B as red/cyan isoluminant instead:
# red (r,0,0): Y=0.2126r ; cyan (0,g,b) g=b: Y=(0.7152+0.0722)g
# set Y=128: r=602(clip). Not feasible at full sat. Lower Y target to 54 (max at sat).
# red 255 -> Y=54.2 ; cyan g=b: (0.7874)g=54.2 -> g=b=68.9
RED = (255, 0, 0)                       # Y=54.2
CYAN = (0, 69, 69)                      # Y=54.3  -> isoluminant with pure red
img = Image.new("RGB",(W,H),(64,64,64))
px = img.load()
bandh = H//4

def stripes(y0,y1,c1,c2,period):
    for y in range(y0,y1):
        for x in range(W):
            px[x,y] = c1 if (x//period)%2==0 else c2

# Band 1: naive red/blue 1px (luma differs)
stripes(0, bandh, (255,0,0),(0,0,255),1)
# Band 2: ISOLUMINANT red/cyan 1px  -> pure chroma edge, the rigorous test
stripes(bandh, 2*bandh, RED, CYAN, 1)
# Band 3: isoluminant red/cyan 2px
stripes(2*bandh, 3*bandh, RED, CYAN, 2)
# Band 4: fine isoluminant checkerboard 1px (worst case for chroma)
for y in range(3*bandh, H):
    for x in range(W):
        px[x,y] = RED if (x+y)%2==0 else CYAN

d = ImageDraw.Draw(img)
# Registration markers in all 4 corners + center crosshair to confirm full coverage
for (cx,cy) in [(0,0),(W-40,0),(0,H-40),(W-40,H-40)]:
    d.rectangle([cx,cy,cx+39,cy+39], outline=(255,255,0), width=3)
d.line([W//2-30,H//2,W//2+30,H//2], fill=(255,255,0), width=1)
d.line([W//2,H//2-30,W//2,H//2+30], fill=(255,255,0), width=1)
# Labels
try:
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
except: f = ImageFont.load_default()
d.text((50,4),  "B1 naive R/B 1px (luma+chroma edge)", fill=(255,255,255), font=f)
d.text((50,bandh+4), "B2 ISOLUMINANT R/cyan 1px  <-- 420 collapses to gray here", fill=(255,255,255), font=f)
d.text((50,2*bandh+4), "B3 isoluminant R/cyan 2px", fill=(255,255,255), font=f)
d.text((50,3*bandh+4), "B4 isoluminant checkerboard 1px", fill=(255,255,255), font=f)
img.save("chroma_test_1280x800.png")
print("wrote chroma_test_1280x800.png  Y(red)=%.1f Y(cyan)=%.1f delta=%.1f"%(Y(*RED),Y(*CYAN),abs(Y(*RED)-Y(*CYAN))))
