# The interactive arm — T4 frontier qualification with real clients

An XFCE desktop on the real Tesla T4, running the pinned development frontier
for BACKLOG #123--#125. Use an identified Windows RDP client and an identified
macOS RDP client; record each product name and version/build in the capture.

**The visible cadence is not a throughput benchmark.** A visible client
decodes and presents every frame, and the client side is a known bottleneck
in this configuration. If it looks smooth, that establishes client
correctness, not the server's maximum rate. The perf ring still supplies
agent-owned diagnostics and the separate numerical qualification; it does not
gate a human visual verdict.

---

## What it is running

| | |
|---|---|
| host | `98.93.137.204`, RDP port **3389** (loopback only) |
| GPU | NVIDIA Tesla T4, driver 580.173.02 |
| xrdp build | `00bce44e8fea`, package `0.10.80+git20260822114412.00bce44e8fea` |
| xorgxrdp | `c190343ff28a`, package `1:0.10.80+git20260822114312.c190343ff28a` |
| desktop | XFCE; `chroma-probe` starts through the installed XDG autostart |
| baseline profile | AVC444, NVENC, aux LTR chain, `wire_window=1`, eager slot acknowledgement, sparse chroma off |
| staged #123 profiles | forced AVC444v1 and forced AVC420, otherwise byte-identical to the baseline |
| staged #125 profile | the same profile with `chroma_refresh_ms=1000`, `chroma_idle_ms=100` |
| retained preflight | `mac_bisect_matrix/captures/i123_t4_frontier_preinteractive_20260822T183935Z/` |

`aux_ltr_chain` is ON in this host's `gfx.toml`, and it has to be: the
credit frontier is ANDed with it, so with it off the arm would silently
be running the old acknowledgement path and you would be looking at the
wrong thing.

## Connecting

The port is bound to **127.0.0.1 on the T4**, deliberately. From the client
machine, forward it first:

```
ssh -N -L 43389:127.0.0.1:3389 -i tmp_access_T4 \
    ubuntu@98.93.137.204
```

Then point the RDP client at `127.0.0.1:43389`.

**Log in as `ubuntu` with the existing T4 credential.** The credential is not
written into this repository or this procedure.

**Windows client.** Add a PC, host `127.0.0.1:43389`. Before
connecting, set the display size explicitly rather than leaving it on
"match this device": a 4K client negotiating a scaled resolution makes
what you see depend on the client's DPI handling as much as on the
server. Use 2560×1440 for the remaining single-monitor checks.

**macOS client.** Same host and port. Turn OFF any "optimise for
Retina / scaled" option for the same reason. Use 2560×1440 for the remaining
single-monitor checks.

Do not log off merely to change client or payload when the server profile is
unchanged. Disconnect one client and reconnect the other to the same XFCE
session. Log off only before a profile change, because `gfx.toml` is loaded for
a fresh session.

## Finish #123 before the six-check walk

The server preflight exercised AVC420, AVC444v2, one monitor and the exact
recorded two-monitor modelines. The owner then checked dense AVC444v2 through
Windows host `5Q77` and macOS host `Signals-iMac`: on both, the alternating
one-pixel red/blue stripes remained visibly distinct rather than becoming a
flat colour. The server log confirms AVC444v2 (`0x000F`) for both, Windows
dynamic resize, and a real Windows two-monitor connection at 3840×2400 plus
2560×1440. Those baseline checks are complete.

Only these real-client mode checks remain for #123:

1. Log the current XFCE session off. From a client-side terminal run
   `ssh -t -i tmp_access_T4 ubuntu@98.93.137.204 '~/xrdp-profile 444v1'`.
   Connect Windows at 2560×1440 and confirm coherent colour, motion and
   distinct one-pixel red/blue stripes. Disconnect without logging off, then
   make the same observation from macOS. The server must report AVC444v1
   (`0x000E`), with no fallback.
2. Log the XFCE session off, then run the same command with `420`. Connect
   Windows, disconnect, and connect macOS to the same session. The desktop,
   clocks and motion must remain coherent. Under 4:2:0 the one-pixel red/blue
   detail is expected to merge; wrong colours, displaced chroma, black output
   or fallback are failures. The server must report AVC420 (`0x000B`).
3. Log the session off and run `~/xrdp-profile 444` to restore the dense
   profile before #124.

`~/xrdp-profile status` prints the live fields. The switcher refuses to change
configuration while an `ubuntu` X11 session exists. A mode the client does not
advertise must fail before codec confirmation and be recorded as unsupported;
it must never confirm another mode and fall back afterward.

## Visual acceptance and passive server evidence

Your acceptance is the visible outcome of the six checks below on both
clients. Run one payload at a time at 2560×1440; quit it before starting the
next. Do not run `textflood`, a sampler or another GUI sidecar during this
walk. With the dense profile unchanged, changing client or payload does not
require a logoff.

The agent separately checks the human-rate xrdp log for the intended codec,
fallbacks and faults, and may harvest the compile-time perf trace after a
normal disconnect. A particular acknowledgement latency or credit distance
is not a visual acceptance criterion and does not make you repeat a clean
walk. This separation is deliberate: the current credit analyzer assumes one
encoder epoch, so a dynamic resize which recreates the encoder can make its
reset client frontier look like a large distance even though the stable
segments are bounded. Historical quantitative results remain with their
captures rather than being acceptance thresholds for this walk.

## The six #124 checks

Run all six on Windows at 2560×1440, one payload at a time. Disconnect without
logging off, connect macOS to the same session at 2560×1440, and repeat. Record
pass/fail separately for each client.

1. `chroma-probe`: watch both luma/chroma clocks continuously for two minutes.
2. `colorkey.sh`, keys `r g b w`: every key changes colour immediately and
   increments the displayed count by exactly one.
3. `colorkey.sh`, key `e`: the one-pixel red/blue stripes remain saturated and
   distinct, not grey or blurred together.
4. `colorkey_x11`, key `c`: all eight named colours arrive in order with no
   missing, repeated or miscoloured step.
5. `colorkey_x11`, key `s`: the block advances in even 8-pixel steps, without
   pause-and-catch-up, skipped positions or a torn top/bottom edge.
6. `codescroll10.sh`: the 10 Hz scroll is evenly paced, syntax-colour fringes
   remain stable, and the title counter increments by one.

Checks 1--6 are qualitative client correctness. The server log must confirm
AVC444/NVENC and contain no fallback, encoder restart, sequence mismatch,
parser failure or pair timeout.

## What to look at in detail

`chroma-probe` starts automatically. Open a terminal in XFCE (Applications →
Terminal Emulator) and run the remaining payloads in turn. All are at
`/usr/local/bin` on the T4.

The 2026-08-22 deployment installed all four payloads from the committed
sources. `textflood` is also installed for a later benchmark arm, but is
deliberately disarmed for this visual walk.

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
  The X11 app advances by 8 pixels per frame; judge those even steps, not
  the character-cell motion of the older shell payload.

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

## #125 sparse-chroma follow-on

Do not start #125 until the #124 walk is green on both clients. Log the current
XFCE session off, then activate the staged sparse profile from a client-side
terminal:

```
ssh -t -i tmp_access_T4 ubuntu@98.93.137.204 '~/xrdp-profile sparse'
```

Make one fresh Windows session and run only the autostarted `chroma-probe`.
After the observation, disconnect without logging off and reconnect from
macOS to the same session. Do not run the six #124 payloads or `textflood`
beside it. On each client, watch the moving block, then stop interacting and
judge the one-pixel red/blue stripes on the still screen. Required
observations:

* the fast and slow numeral/patch pairs never disagree;
* the static zone never changes and the moving hue never leaves its outline;
* after motion settles, full-chroma stripe detail is restored no later than
  1000 ms plus the first actual frame interval at or after that deadline;
* no stale chroma, wrong colour, missing update or surface corruption appears.

The visual result does not depend on forcing the trace to drain. After both
clients are done, one normal logoff lets the agent preserve the trace and run
the separate command-count, byte, chroma-gap and numerical throughput/latency
audit specified by BACKLOG #125B. Those machine checks are agent-owned and do
not add payloads to this visual walk.

After #125, restore the dense baseline with `~/xrdp-profile 444` after the
session has logged off.

## If something looks wrong

Capture what you saw and the count on screen — the count is what turns
"it stuttered" into something reproducible. The server-side trace is in
`/var/log/xrdp-perf/`, the xrdp log is `/var/log/xrdp.log`, and the
session-Xorg log is `/home/ubuntu/.xorgxrdp.<display>.log`. Do not change
profile, encoder or client after a red result; preserve that exact session's
artifacts first.

## Ending the session

Log off from inside XFCE (Applications → Log Out). Do not kill processes
in the session from outside — that has produced misleading PID
bookkeeping on this project before, and the next login starts clean
anyway.
