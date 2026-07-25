#!/bin/bash
# Dual-monitor AVC444 "drag burr" reproduction (end-to-end, self-driven),
# faithful to the owner's real layout:
#
#   canvas 3840x3840
#   monitor "primary" 2560x1440 at +594+0        (small screen ON TOP)
#   monitor "sub4k"   3840x2400 at +0+1440       (4K screen BELOW)
#
# (owner's negotiated layout: 4K left=-594 top=1440 -> normalized (0,1440);
# primary (594,0).  Non-monitor area stays black on the client:
# 3840*3840 - 3840*2400 - 2560*1440 = 1843200 pure-black px = layout check.)
#
# A real qterminal window is dragged BY ITSELF (xdotool windowmove steps):
# a horizontal sweep on the 4K screen, then a vertical sweep crossing the
# monitor seam, so trailing edges damage both surfaces at the same client x.
# Oracle: client framebuffer vs session framebuffer (truth) after a settle
# pause; any persistent structured mismatch is residual, classified by line
# width (1px/2px) and dashing, and checked for seam strike-through.
#
# MODE=dual (default) | single (4K only; owner-reported clean control)
# GFX=AVC444 (default)
set -u
MODE=${MODE:-dual}
GFX=${GFX:-AVC444}
SU=${KEYTEST_USER:-tester}
SX=/var/run/xrdp/1000/Xauthority
HOST=${KEYTEST_HOST:-127.0.0.1:3389}
CD=:97
OUT=/tmp/mmburr-$MODE-$GFX
D=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"; chmod 1777 "$OUT"; rm -f "$OUT"/*.png "$OUT"/*.txt 2>/dev/null

P_W=2560; P_H=1440; P_X=594;  P_Y=0
S_W=3840; S_H=2400; S_X=0;    S_Y=1440
if [ "$MODE" = single ]; then
    CANVAS_W=$S_W; CANVAS_H=$S_H; S_Y=0
else
    CANVAS_W=3840; CANVAS_H=3840
fi

cleanup(){
    pkill -9 -f xfreerdp3 2>/dev/null
    pkill -9 -f "Xorg $CD" 2>/dev/null
}
trap cleanup EXIT

# ---- 1. client Xorg (dummy driver honours setmonitor, Xvfb does not) ------
pkill -9 -f "Xorg $CD" 2>/dev/null; sleep 1
ML=$(cvt "$CANVAS_W" "$CANVAS_H" 60 2>/dev/null | grep Modeline | sed 's/Modeline //' | tr -d '"')
MODENAME=$(echo "$ML" | awk '{print $1}')
MLREST=$(echo "$ML" | cut -d' ' -f2-)
cat > "$OUT/xorg.conf" <<EOF
Section "ServerFlags"
    Option "AutoAddDevices" "false"
    Option "DontVTSwitch" "true"
EndSection
Section "Device"
    Identifier "dummy"
    Driver "dummy"
    VideoRam 262144
EndSection
Section "Monitor"
    Identifier "mon"
    HorizSync 5.0-1000.0
    VertRefresh 5.0-200.0
    Modeline "$MODENAME" $MLREST
EndSection
Section "Screen"
    Identifier "screen"
    Device "dummy"
    Monitor "mon"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Modes "$MODENAME"
        Virtual $CANVAS_W $CANVAS_H
    EndSubSection
EndSection
EOF
setsid Xorg $CD -config "$OUT/xorg.conf" -noreset -logfile "$OUT/xorg.log" \
    </dev/null >/dev/null 2>&1 &
for i in $(seq 1 20); do DISPLAY=$CD xrandr >/dev/null 2>&1 && break; sleep 0.5; done
DISPLAY=$CD xrandr --newmode "$MODENAME" $MLREST 2>/dev/null
DISPLAY=$CD xrandr --addmode DUMMY0 "$MODENAME" 2>/dev/null
DISPLAY=$CD xrandr --output DUMMY0 --mode "$MODENAME" 2>/dev/null
if [ "$MODE" = single ]; then
    DISPLAY=$CD xrandr --setmonitor sub4k ${S_W}/1016x${S_H}/635+0+0 DUMMY0 2>/dev/null
else
    DISPLAY=$CD xrandr --setmonitor primary ${P_W}/677x${P_H}/381+${P_X}+${P_Y} DUMMY0 2>/dev/null
    DISPLAY=$CD xrandr --setmonitor sub4k ${S_W}/1016x${S_H}/635+${S_X}+${S_Y} none 2>/dev/null
fi
echo "== client monitors ($MODE) =="; DISPLAY=$CD xrandr --listmonitors 2>&1

# ---- 2. cold tester login over multimon AVC444 ----------------------------
pkill -9 -f xfreerdp3 2>/dev/null
sudo -u $SU pkill -u $SU -TERM xfce4-session 2>/dev/null; sleep 2
sudo -u $SU pkill -u $SU -KILL -f 'xfce4-session|Xorg :' 2>/dev/null
for i in $(seq 1 25); do pgrep -f 'Xorg :1[0-9]' >/dev/null || break; sleep 1; done
sleep 3
MM=/multimon
[ "$MODE" = single ] && MM="/size:${S_W}x${S_H}"
setsid env DISPLAY=$CD xfreerdp3 /v:"$HOST" /u:$SU /p: $MM \
    /gfx:$GFX /cert:ignore /log-level:WARN </dev/null >"$OUT/rdp.log" 2>&1 &
sleep 8
fw=""
for r in 1 2 3; do
    fw=$(DISPLAY=$CD xdotool search --name FreeRDP 2>/dev/null | head -1)
    [ -n "$fw" ] && { DISPLAY=$CD xdotool windowactivate "$fw" 2>/dev/null; sleep 0.4;
                      DISPLAY=$CD xdotool key --window "$fw" Return 2>/dev/null; }
    sleep 5
done
sleep 6
SD=$(pgrep -a Xorg | grep -oE ':1[0-9]+' | head -1)
[ -z "$SD" ] && { echo "FAIL: no fresh Xorg session"; exit 1; }
echo "== session display=$SD client=$CD =="
sess(){ sudo -u $SU env DISPLAY=$SD XAUTHORITY=$SX "$@"; }

grab_client(){ ffmpeg -hide_banner -loglevel error -f x11grab \
    -video_size ${CANVAS_W}x${CANVAS_H} -i "$CD.0" -frames:v 1 -y "$1" 2>/dev/null; }
grab_sess(){ sess ffmpeg -hide_banner -loglevel error -f x11grab \
    -video_size ${CANVAS_W}x${CANVAS_H} -i "$SD.0" -frames:v 1 -y "$1" 2>/dev/null; }

# ---- 3. layout check: count pure-black px on the settled client fb --------
sleep 3
grab_client "$OUT/layout.png"
python3 - "$OUT/layout.png" "$MODE" <<'EOF'
import sys
import numpy as np
from PIL import Image
im = np.asarray(Image.open(sys.argv[1]).convert('RGB'))
black = int((im.sum(axis=2) == 0).sum())
print(f"layout check: pure-black px = {black}"
      + (" (expected ~1843200 for owner layout)" if sys.argv[2] == "dual" else ""))
EOF

# ---- 3b. baseline truth/client pair BEFORE any drag: every hot px here is
# static codec noise (icons, panel, fine detail) and is masked from the
# residual analysis, so only drag-caused mismatches count.
grab_sess   "$OUT/truth_0.png"
grab_client "$OUT/client_0.png"

# ---- 4. drag a real qterminal window ---------------------------------------
sess pkill -u $SU qterminal 2>/dev/null; sleep 1
sess setsid qterminal </dev/null >/dev/null 2>&1 &
qw=""
for i in $(seq 1 15); do
    sleep 1
    qw=$(sess xdotool search --class qterminal 2>/dev/null | tail -1)
    [ -n "$qw" ] && break
done
[ -z "$qw" ] && { echo "FAIL: no qterminal"; exit 1; }
QW=900; QH=700
sess xdotool windowsize "$qw" $QW $QH 2>/dev/null
STEP=24

# pass A: horizontal sweep on the 4K screen (like the owner's drag)
HY=$((S_Y + 500))
x=$((S_X + 40))
sess xdotool windowmove "$qw" $x $HY 2>/dev/null; sleep 2
while [ $x -lt $((S_X + S_W - QW - 60)) ]; do
    sess xdotool windowmove "$qw" $x $HY 2>/dev/null
    x=$((x + STEP))
done
sleep 2.5
grab_sess   "$OUT/truth_A.png"
grab_client "$OUT/client_A.png"

if [ "$MODE" = dual ]; then
    # pass B: vertical sweep crossing the monitor seam (window inside the
    # primary's x-range so it is visible on both screens while crossing),
    # then drag AWAY horizontally on the primary so the crossing columns are
    # fully vacated on BOTH screens (strike-through condition)
    VX=1500
    y=$((S_Y + 700))
    sess xdotool windowmove "$qw" $VX $y 2>/dev/null; sleep 2
    while [ $y -gt 300 ]; do
        sess xdotool windowmove "$qw" $VX $y 2>/dev/null
        y=$((y - STEP))
    done
    x=$VX
    while [ $x -lt 2200 ]; do
        sess xdotool windowmove "$qw" $x 300 2>/dev/null
        x=$((x + STEP))
    done
    sleep 2.5
    grab_sess   "$OUT/truth_B.png"
    grab_client "$OUT/client_B.png"

    # pass C: cross the seam up-and-down at several x positions (ghost
    # formation at a given edge is stochastic; more crossings, more chances),
    # then park the window in the 4K bottom-right corner
    for cx in 800 1300 1800 2300; do
        y=$((S_Y + 560))
        sess xdotool windowmove "$qw" $cx $y 2>/dev/null; sleep 1
        while [ $y -gt 200 ]; do
            y=$((y - STEP)); sess xdotool windowmove "$qw" $cx $y 2>/dev/null
        done
        while [ $y -lt $((S_Y + 560)) ]; do
            y=$((y + STEP)); sess xdotool windowmove "$qw" $cx $y 2>/dev/null
        done
    done
    sess xdotool windowmove "$qw" 2700 3000 2>/dev/null
    sleep 2.5
    grab_sess   "$OUT/truth_C.png"
    grab_client "$OUT/client_C.png"
fi

# ---- 5. classify residuals (baseline-masked) --------------------------------
python3 - "$OUT" "$MODE" <<'EOF'
import sys, os
import numpy as np
from PIL import Image, ImageDraw
out, mode = sys.argv[1], sys.argv[2]
P = (594, 0, 594+2560, 1440)      # primary rect on canvas
S = (0, 1440, 3840, 3840)         # 4K rect on canvas
if mode == "single":
    S = (0, 0, 3840, 2400); P = None
TOL = 48
fail = 0
def load(p): return np.asarray(Image.open(p).convert('RGB')).astype(int)
def hotmap(tag):
    t = load(os.path.join(out, f"truth_{tag}.png"))
    c = load(os.path.join(out, f"client_{tag}.png"))
    return np.abs(t - c).max(axis=2) > TOL
# static codec noise mask: hot before any drag, dilated by 2px
base = hotmap("0")
k = 2
mask = base.copy()
for dy in range(-k, k+1):
    for dx in range(-k, k+1):
        mask |= np.roll(np.roll(base, dy, 0), dx, 1)
def lines(hot, axis):
    score = hot.sum(axis=(0 if axis==0 else 1))
    idx = np.nonzero(score > 120)[0]
    if len(idx) == 0:
        return []
    groups, s, p = [], idx[0], idx[0]
    for i in idx[1:]:
        if i <= p + 1: p = i; continue
        groups.append((s, p)); s = p = i
    groups.append((s, p))
    res = []
    for a, b in groups:
        lane = hot[:, a:b+1] if axis == 0 else hot[a:b+1, :]
        prof = lane.any(axis=1 if axis == 0 else 0)
        nz = np.nonzero(prof)[0]
        span = nz[-1] - nz[0] + 1
        cover = prof.sum() / span
        res.append(dict(pos=(a, b), width=b-a+1, span=int(span),
                        dashed=bool(cover < 0.7), cover=round(float(cover), 2)))
    return res
found = {}
for tag in (["A", "B", "C"] if mode == "dual" else ["A"]):
    if not os.path.exists(os.path.join(out, f"truth_{tag}.png")):
        continue
    hot_all = hotmap(tag) & ~mask          # drag-caused only
    found[tag] = hot_all
    for name, R in (("4K", S), ("primary", P)) if P else (("4K", S),):
        x1, y1, x2, y2 = R
        hot = hot_all[y1:y2, x1:x2]
        for L in lines(hot, 0):
            print(f"pass {tag} {name}: VERTICAL ghost cols {L['pos'][0]+x1}-{L['pos'][1]+x1}"
                  f" width={L['width']}px span={L['span']} "
                  f"{'DASHED' if L['dashed'] else 'solid'} (cover {L['cover']})")
            fail = 1
        for L in lines(hot, 1):
            print(f"pass {tag} {name}: HORIZONTAL ghost rows {L['pos'][0]+y1}-{L['pos'][1]+y1}"
                  f" width={L['width']}px span={L['span']} "
                  f"{'DASHED' if L['dashed'] else 'solid'} (cover {L['cover']})")
            fail = 1
    # seam strike-through: same client x hot in BOTH screens, ONLY within
    # the x-range where the two screens overlap vertically (594..3154)
    if P and tag in ("B", "C"):
        ov1, ov2 = P[0], P[2]
        top = hot_all[P[1]:P[3], ov1:ov2].sum(axis=0)
        bot = hot_all[S[1]:S[3], ov1:ov2].sum(axis=0)
        both = np.nonzero((top > 60) & (bot > 60))[0] + ov1
        if len(both):
            print(f"pass {tag}: STRIKE-THROUGH vertical ghost at client x"
                  f" {both.tolist()[:10]} (crosses both screens)")
print("RESULT:", "RESIDUAL DETECTED" if fail else "NO residual")
# annotated artifact: full stitched client fb with red boxes on ghost lines
if found:
    tag = "C" if "C" in found else ("B" if "B" in found else "A")
    im = Image.open(os.path.join(out, f"client_{tag}.png")).convert('RGB')
    dr = ImageDraw.Draw(im)
    hot_all = found[tag]
    for name, R in (("4K", S), ("primary", P)) if P else (("4K", S),):
        x1, y1, x2, y2 = R
        hot = hot_all[y1:y2, x1:x2]
        for L in lines(hot, 0):
            a, b = L['pos']
            dr.rectangle([x1+a-5, y1, x1+b+5, y2-1], outline=(255,0,0), width=3)
        for L in lines(hot, 1):
            a, b = L['pos']
            dr.rectangle([x1, y1+a-5, x2-1, y1+b+5], outline=(255,0,0), width=3)
    im.save(os.path.join(out, "annotated_client.png"))
    print(f"annotated artifact: {out}/annotated_client.png (pass {tag})")
sys.exit(fail)
EOF
rc=$?
echo "== grabs =="; ls -1 "$OUT"/*.png 2>/dev/null
exit $rc
