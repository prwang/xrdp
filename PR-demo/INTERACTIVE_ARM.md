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

## The acceptance criterion has two halves, and only one is yours

Written 2026-08-08, after the first walk, because the doc previously said
what to look at and never what a pass is.

**Your half — the outcome.** The six checks below, on BOTH clients
(Windows UWP and macOS), at BOTH sizes (1920×1080 and 2560×1440),
connecting fresh per size.

**The server's half — the premise, which no client-side observation can
see.** Every fleet measurement of this mechanism used the oracle client,
which acknowledges a frame BEFORE decoding it, so the frontier's
`frame_id_client + C` term — how far ahead of the client we may run —
has never been exercised. A real client acknowledges after decoding and
presenting. Whether that actually happened during your walk is read off
the server's ring, not the screen:

* acknowledgement latency realistically late (the oracle client's is
  ~9 ms; a real client's should be several times that);
* the window actually reached — frames at distance `wire_window + 2×M`
  and just below it must be a real fraction, not a handful;
* zero encoder restarts, sequence mismatches, parser errors or pair
  timeouts for the whole walk.

Run it with `PR-demo/mac_bisect_matrix/i80_ack_latency.py` over the arm's
perf ring, passing the monitor count from the session log.

**The rule that ties them: a clean visual walk is evidence ONLY if the
trace shows the window was exercised.** If nothing looked wrong and the
distance histogram sits at 1–2, the walk proved the client was fast, not
that the frontier is safe, and it has to be repeated under something
slower.

First result, 2026-08-08: premise half PASSED (ack p50 68 ms and 53 ms;
the bound reached 173 times at one monitor and 73 times at two, held
exactly in both; zero faults). Visual half PARTIAL — the owner reported
no lag on the payload they ran, not the full six across both clients.
Record: `captures/i80_onscreen_walk_x027_20260808/`.

## What to look at

Open a terminal in XFCE (Applications → Terminal Emulator) and run each
in turn. All are at `/usr/local/bin` in the running pod.

All of them ship **in the image** as of 2026-08-08, including
`chroma-probe`'s `python3-tk`. They were briefly copied into a running
pod by hand, which meant they died with it; `Containerfile` and
`build_and_deploy.sh` now build and install all three, and the build
aborts if any probe source is missing. An arm built before that date
does not have `chroma-probe` or `colorkey_x11` — check with
`ls /usr/local/bin/` before handing it over.

### 0. Main/aux pairing — `chroma-probe`

The instrument for the thing `aux_ltr_chain` actually changes, and the
one to run first. Luma and chroma carry independent clocks: the numerals
and rulers are white on black (pure luma), the timed patches are
equiluminant (pure chroma). Top left, a numeral N counting 1..8 beside a
patch of palette hue N, painted by the same repaint; the eight-swatch
reference palette beside it; a second clock stepping once per 8 s; named
colour bars with 1 px red/blue stripe pairs; a never-repainted static
zone bottom left; a bouncing block bottom right.

* **Pass:** the numeral inside the patch and the palette index agree,
  continuously, for two minutes; the slow clock agrees; the static zone
  never changes; the block's hue stays inside its luma outline.
* A persistent disagreement of k means chroma is k frames behind luma —
  a pairing fault, and a hard fail.
* The static zone changing means the fault is not confined to where
  damage flows.

Escape quits.

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

### 2. Eight-colour cycle and sliding block — `colorkey_x11`, keys `c` and `s`

**Use `colorkey_x11`, not the shell version, for anything about motion.**
The shell app draws through a terminal and can only address character
cells, so its block hops ~24 px; `colorkey_x11` owns the pixels and steps
8 px per frame at 25 fps. Same keys.

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
