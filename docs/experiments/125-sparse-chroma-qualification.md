# #125 — sparse-chroma qualification log

This is the durable record for an open item. It is not a closure report.

## 2026-08-23 early real-client observation — RED

The pinned T4 instance was running the sparse AVC444v2 profile with
`chroma_refresh_ms=1000` and `chroma_idle_ms=100`. On Windows, the nominally
static one-pixel red/blue stripe region flickered visibly at approximately
one hertz. The effect was strong on Windows and uncertain on macOS.

That cadence matches the configured periodic full-chroma refresh closely
enough to make the refresh boundary the first mechanism to inspect, but it
does not identify whether the fault is server emission, main/aux pairing or
client presentation. No conclusion is assigned before the trace/byte audit
and a corrected, isolated client replay.

There is no useful MP4 artifact. The available recording path re-encoded to
4:2:0, which removes the one-pixel chroma distinction carrying the symptom.
Absence of a video does not turn the owner-observed red result green.

This observation occurred before #124 and its helper repair were complete.
It reopens no dense-mode #123 result, but #125A is red and must be explained,
fixed and independently replayed on the development pair before clean-room
work starts.

## 2026-08-23 static Color-A/Color-B reproduction — RED

After the corrected Solarized Dark code-scroll printed `#include` lines, the
owner stopped it with Control-C and manually scrolled twice. A `#include`
glyph then remained in either of two stable states: bright magenta (Color A)
or faint purple-magenta (Color B). Dense AVC444v2 should retain Color A. The
two source screenshots and exact nearest-neighbour crops are indexed under
`PR-demo/media_evidences/sparse_chroma_stall/`.

The inclusive five-by-seven crop `(151,125)--(155,131)` contains the small
hash sign. Among the same 20 foreground pixels selected by Color A having
red above 100, Color A averages RGB `(203.7, 53.1, 123.5)`, close to Solarized
magenta `#d33682`; Color B averages `(149.8, 55.7, 111.5)`. Red falls by about
54 while green stays nearly constant. Color B is therefore a material chroma
and hue loss, not an overall brightness change or a display-scale ambiguity.

## 2026-08-23 source correction

The earlier record treated the one-second cadence as a question of emission,
pairing or presentation. Source inspection now establishes the missing case.
For a skipped auxiliary picture xrdp emits LC=1 alone over the current damage
region. Microsoft defines LC=1 as a YUV420 stream whose corresponding
Chroma420 stream may follow in later frames; it does not promise that a client
will preserve the older full-chroma samples (MS-RDPEGFX section 3.3.8.3.2,
https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/3b337b87-f478-4786-a63b-97794aa72075).
The screenshots show that the
Windows client presents the LC=1 reconstruction, which is the expected
one-pixel detail loss already proved by `test_avc420_isoluminant_chroma_loss`.

The current scheduler only asks whether auxiliary work is due when a new
damage frame arrives. Its “settle” test measures the gap *before* that new
frame. After the final main-only update there is no timer, no later event and
no retained capture from which to send the corresponding chroma. A future
damaged region may also exclude the static glyph. Consequently Color B can
remain indefinitely; the old claim that a client keeps prior chroma is void.

The local fix must create an actual trailing-edge restoration independent of
future application damage and cover it with a deterministic display-state
regression. It must preserve the capture-page ownership contract and may not
hide the defect by silently changing a requested sparse profile to dense.
Only after that gate is green can #125 return to representative hardware and
real clients. The T4 used for this observation has been imaged and
decommissioned.

This source correction diagnoses the permanent Color-B final state only. It
does not diagnose or close the separately observed approximately one-hertz
red/blue stripe flicker.

## 2026-08-23 local reproduction and repair — deterministic GREEN

`test_sparse_main_only_stalls_at_420_until_restore` independently applies the
AVC444v2 packing to a one-pixel isoluminant chroma pattern. A complete pair has
distinct adjacent chroma, applying main alone makes that chroma flat, and no
further update leaves it flat. Applying auxiliary again restores the original
separation. This is the local display-state reproduction the earlier cadence
tests lacked.

The repair does not retain a capture slot and does not copy a full frame per
moving update. Each main-only submission rearms a per-monitor monotonic
deadline at `now + chroma_idle_ms`; a full-chroma submission cancels it. If
the worker reaches a deadline with no newer frame, it sends one control marker
to the main thread and disarms the deadline. The main thread uses xup's
existing full-invalidate request, so xorgxrdp captures current pixels. The gap
makes that frame auxiliary-due and its full-screen main-plus-auxiliary update
restores even regions with no application damage. Transport I/O remains on
the main thread and borrowed capture pages retain their existing lifetime.

The two new deadline tests derive their expected times from the configured
100 ms interval and prove rearm, cancel, one-shot expiry, disabled behavior
and overflow safety. The full xrdp suite passes 211/211 in the trace-enabled
development build. This is local implementation evidence, not a replacement
for the Windows/macOS replay: #125 remains open until a representative rig
confirms Color B no longer persists and the expected one-shot request appears.

## 2026-08-23 defect-order decision

The permanent dim-text final state and the transient red/blue stripe flicker
are tracked as separate correctness failures. Scope A1 first qualifies the
trailing restoration above. Only after a representative client proves that a
still surface converges to Color A does Scope A2 investigate the flicker.

The original flicker was approximately one hertz under
`chroma_refresh_ms=1000` and `chroma_idle_ms=100`. Its cadence points first at
the one-second refresh boundary, not the 100 ms settle threshold, but that is
only a hypothesis: the observation cannot yet distinguish server emission,
main/aux pairing or client presentation. The new 100 ms one-shot restoration
has not been deployed to any hardware arm and is not credited as a flicker
fix.
