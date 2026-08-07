# The interactive arm — looking at the credit frontier with your own clients

An XFCE desktop running the **shipped defaults as of 2026-08-07**, for
judging correctness by eye from a Windows (UWP) or macOS RDP client.

**This is not a benchmark and no number from it means anything about the
server.** A visible client decodes and presents every frame, and the
client side is the known bottleneck in that configuration — measured
repeatedly on this project. If it looks smooth, that tells you the
pipeline is correct, not how fast it is. Rates come from the offscreen
oracle harness, never from here.

---

## What it is running

| | |
|---|---|
| arm | `x027`, host port **40043** (loopback only) |
| image | `localhost/xrdp-bisect:821218e54c24.xx10fa3aa-xfce.pc0097388` |
| xrdp build | commit `821218e54c24` — the credit frontier ON by default at `wire_window = 2`, and **no emit thread in the binary at all** |
| xorgxrdp | `10fa3aa23033` |
| desktop | XFCE (`SESSION_KIND=xfce`) |
| certificate | 7 checks clean, 0 black frames, at deploy |
| smoke gate | **PASS** at 1920×1080 and 1024×768: 8 of 8 keys correct, 0 lagged frames, colour-edge fidelity 0.998, 0 encoder errors |

`aux_ltr_chain` is ON in this arm's `gfx.toml`, and it has to be: the
credit frontier is ANDed with it, so with it off the arm would silently
be running the old acknowledgement path and you would be looking at the
wrong thing.

## Connecting

The port is bound to **127.0.0.1 on this box**, deliberately — the fleet
is never exposed. So from your laptop, forward it first:

```
ssh -N -L 43389:127.0.0.1:40043 <this-box>
```

Then point the client at `127.0.0.1:43389`.

**Log in as `tester`.** Its password inside the pod is the same one the
`tester` account on this box uses — the deploy copies that hash in, so
there is no separate credential to look up and nothing to write down.

**Windows / UWP client.** Add a PC, host `127.0.0.1:43389`. Before
connecting, set the display size explicitly rather than leaving it on
"match this device": a 4K client negotiating a scaled resolution makes
what you see depend on the client's DPI handling as much as on the
server. 1920×1080 and 2560×1440 are the two sizes worth trying.

**macOS client.** Same host and port. Turn OFF any "optimise for
Retina / scaled" option for the same reason. If the window is resized
mid-session the session does not follow — log off and reconnect at the
size you want to judge.

Both clients: connect **once per size**. A reconnect to an existing
session reuses the negotiated geometry, so the size you asked for on the
second connection may not be the size you get.

## The three things to look at

Open a terminal in XFCE (Applications → Terminal Emulator) and run each
in turn. All three are installed in the image at `/usr/local/bin`.

### 1. Colour keys — `colorkey.sh`

Press `r`, `g`, `b`, `w`. Each keypress repaints the whole screen in one
write, so it is exactly one frame, and the screen shows a running count.

* **What is right:** the colour changes on the keypress, and the count
  goes up by exactly one each time.
* **A frame withheld** shows as the screen still holding the PREVIOUS
  colour and the previous count until you press the next key. Nothing is
  drawn between keypresses, so there is no other damage that could flush
  it — a lag here is unambiguous.
* `e` draws fine red/blue vertical stripes. They must look saturated,
  not washed out to grey. Flat colours look identical at 4:2:0 and
  4:4:4; these stripes do not, so this is the chroma check.

### 2. Eight-colour cycle and sliding block — same app, keys `c` and `s`

* **`c`** walks the full screen through black, red, green, blue, yellow,
  magenta, cyan, white, one colour per frame, naming the colour and the
  step on screen. **Read the order.** A colour out of order, or a step
  index jumping by more than one, means frames were reordered or
  dropped. A colour that never appears means one was withheld. A wrong
  hue with the order intact — yellow looking green, magenta looking
  blue — is a chroma-plane problem, not a flow-control one. The eight
  corners were chosen so a chroma fault changes the colour's *name*
  rather than its shade.
* **`s`** slides a block across a contrasting background with the frame
  count on it. Even hops are healthy. A pause then a resume at the next
  position means a frame was late, with the sequence still complete. A
  jump to a position the block was never drawn at means frames were
  dropped or coalesced. The top and bottom of the block at different
  horizontal positions in one screen is a torn frame.
  **It steps whole character cells, not pixels** — a terminal cannot do
  better, and the app says so on screen. Judge it for stutter and
  tearing, not for sub-pixel smoothness.

`q` quits. Both animated modes stop on any key, so you can go straight
from `c` to `s`.

### 3. The fixed 10 Hz code scroll — `codescroll10.sh`

One line of syntax-coloured real code every 0.1 s. The rate is pinned by
the payload, so a healthy pipeline and a struggling one both emit ten
frames a second — **what this tests is smoothness and text fidelity at a
rate a human reads at**, not throughput.

* Scrolling should be even. Clumping means frames are arriving in bursts.
* Glyph edges carry colour fringes (it is deliberately subpixel
  antialiased). If they shimmer or turn grey, chroma is being degraded.
* The window title carries a frame counter; it should climb by one.

Both apps take arguments if you want to push them (`codescroll10.sh 10
0.1` for a fast scroll, `CK_SLIDE_MS=20` for a faster block), but the
defaults are the ones the descriptions above are written against.

## If something looks wrong

Capture what you saw and the count on screen — the count is what turns
"it stuttered" into something reproducible. The server-side trace for
this arm is in the pod at `/var/log/xrdp-perf/`, and the session log at
`/var/log/xrdp/xrdp.log`:

```
kubectl -n bisect-matrix logs $(kubectl -n bisect-matrix get pod \
    -l arm=x027 -o name | head -1)
```

To get the OLD acknowledgement behaviour for comparison without
rebuilding anything, arm **x020** on port 40036 runs the legacy path
with the same everything else. It has the benchmark payload rather than
a desktop, so it is a comparison of mechanism, not of what you can see.

## Ending the session

Log off from inside XFCE (Applications → Log Out). Do not kill processes
in the session from outside — that has produced misleading PID
bookkeeping on this project before, and the next login starts clean
anyway.
