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

## 2026-08-23 local x034/x038 interactive handoff

The first deployment of the repair is a local-container A/B, not a new T4
arm. x034 retains pre-fix xrdp `3ca17beaa84d` on host port 40050. x038 runs
xrdp `386ca6951a3d` on host port 40054; no functional source changed between
the repair commit `23b6235d` and that package's documentation-only HEAD.

The conditions intentionally held equal are xorgxrdp package
`10fa3aa23033`, AVC444v2, aux LTR chain, eager acknowledgement, wire window 2,
`chroma_refresh_ms=1000`, `chroma_idle_ms=100`, and the interactive payload.
The live `gfx.toml` SHA-256 is
`a9256c7ec5d3ea5342691ab3a4cd6a3cf0c763b6b8d3899a1f12a58f23a5a71e` on
both. The mounted current `codescroll10.sh` SHA-256 is
`20c62ab26dc7cd63cfb7e33a4a3ebe51d7ddfe704cb0c97c62013311dbd7612a` on
both, and its corpus hash is
`bc4a13c3f4dbcff9616e3cb5bd92df6286728de420fb8349f8561dff66696ef5`.

Both final deployments passed the repository's 1920x1080 and 1024x768 smoke
connections: eight of eight colour transitions, no lag, settled chroma above
the required edge-fidelity floor, and zero encoder restart/sequence errors.
Both real-byte deployment certificates pass and are keyed to the identical
config hash. This smoke proves the final arms are connectable and encode
conforming bytes; it does not prove the Windows final-state defect fixed.

This is the owner's requested retained-deployment comparison, not a
one-commit causal isolate. x034 predates x038's perf-trace representation and
compile-out cleanup as well as the repair. The explicit `wire_window=2`
neutralizes the intervening default change, and the payload, producer and
encoder config are held equal, but a visual pass can establish only that the
current frontier no longer shows the old baseline symptom. The deterministic
display-state and deadline regressions remain the independent evidence that
attributes final-state restoration to the new timer.

## 2026-08-23 superseding note — interactive terminal was not equivalent

The local handoff above correctly identified server packages and wire
configuration, but its interactive visual condition is withdrawn. It launched
a bare xterm with forced Xft font, size, foreground, background and scrollback
instead of the regular XFCE desktop and LXTerminal used on the T4. It also
handled resizing and session exit differently. Therefore its successful
certificate and smoke checks remain evidence about connectivity and encoded
bytes only; no visual comparison made in that special xterm can qualify A1.

Both arms are rebuilt from fresh images with `SESSION_KIND=xfce` and
LXTerminal installed. The versioned `codescroll10.sh` remains the identical
payload and is run inside LXTerminal, which supplies the GTK/VTE antialiased
text path. No X resources, fontconfig override or xterm appearance arguments
are installed. An xterm opened from XFCE consequently retains its
package-default white background, small bitmap font and non-antialiased text,
matching the control observed on T4. The prior payload fix which paints blank
cells Solarized Dark remains necessary for the payload, but did not by itself
establish equivalence of the terminal renderer or desktop session.

The corrected deployments are:

| condition | endpoint | xrdp package | image |
|---|---|---|---|
| pre-repair baseline | `127.0.0.1:40050` | `3ca17beaa84d` | `3ca17beaa84d.xx10fa3aa-tf-xfce.p0796eea3` |
| trailing-restoration treatment | `127.0.0.1:40054` | `386ca6951a3d` | `386ca6951a3d.xx10fa3aa23033-tf-xfce.p0796eea3` |

Both report `SESSION_KIND=xfce`, XFCE 4.20.1, LXTerminal 0.4.1, xterm 398,
and xorgxrdp `10fa3aa23033`. No `.Xresources`, `.Xdefaults` or user
fontconfig override exists in either fresh tester home. Their live config,
codescroll script and ANSI corpus hashes are respectively
`a9256c7ec5d3ea5342691ab3a4cd6a3cf0c763b6b8d3899a1f12a58f23a5a71e`,
`20c62ab26dc7cd63cfb7e33a4a3ebe51d7ddfe704cb0c97c62013311dbd7612a`
and
`bc4a13c3f4dbcff9616e3cb5bd92df6286728de420fb8349f8561dff66696ef5`.

Fresh real-byte certificates pass for both final image/config keys. The
mandatory post-roll smoke also passes at both sizes on both arms: every leg
delivered eight of eight colour transitions with no lag and no encoder
restart or sequence error. Settled stripe-edge fidelity was 0.643 and 0.642
on the pre-repair arm, and 0.999 and 0.998 on the treatment at 1920x1080 and
1024x768 respectively, against the 0.50 smoke floor. This test uses its own
xterm colour-key stimulus, so the difference is consistent with the repair's
trailing full-chroma update but is not the requested Windows/LXTerminal
visual acceptance. The owner must compare `codescroll10.sh` inside
LXTerminal under otherwise identical client geometry.

## 2026-08-23 corrected Windows A/B — final convergence GREEN

The owner repeated the comparison on the corrected regular XFCE desktops,
running the common payload in LXTerminal. On the pre-repair endpoint at port
40050, the defect reproduced: after motion stopped, the magenta `#` could
remain permanently in faint Color B. On the repaired endpoint at port 40054,
the same `#` could appear faint while windows were being dragged, but returned
to normal Color A once movement stopped.

This is the load-bearing distinction. Sparse mode is permitted to show the
main-only reconstruction while content is moving; Scope A1 requires the final
static surface to regain full chroma. The baseline failed that condition and
the repaired arm passed it. Together with the independent display-state and
deadline tests, the corrected Windows A/B closes the permanent-convergence
defect as fixed by trailing restoration.

This result does not close or explain the separately observed approximately
one-hertz red/blue flicker. The next #125 correctness action is to reproduce
and understand that transient defect with `chroma-probe` on the repaired arm,
without an unrelated payload or sidecar.

## 2026-08-23 Scope A2 withdrawal — sparse-mode limitation

The owner observed a faster flicker after running `chroma-probe` on the
repaired x038 arm at port 40054. The probe advances its motion zone every
125 ms, while the profile uses `chroma_idle_ms=100`. Source inspection gives
the resulting cycle without needing a semantic hypothesis: a main-only update
arms restoration at 100 ms; restoration supplies full chroma; the next probe
update arrives about 25 ms later and is therefore main-only; the cycle repeats.
The server cannot know at the 100 ms deadline that another update will arrive
25 ms later.

The same limitation exists on the periodic refresh boundary. During damage
which remains faster than the idle threshold, `chroma_refresh_ms=1000`
periodically produces a full-chroma frame and a following main-only frame may
replace fine detail with its 4:2:0 reconstruction again. This is consistent
with the earlier approximately one-hertz T4 observation, but that run did not
retain sufficient trace to prove the specific attribution. The observation
is preserved; only its classification changes.

The sparse decision is deliberately based on two clocks per monitor and does
not inspect pixels. Damage geometry can spare content outside an update, but
cannot identify a previously static object inside a coalesced or broad update
caused by other activity. No fixed timeout distinguishes “motion has stopped”
from “the next motion update has not arrived yet.” Eliminating all visible
transition churn would require a different feature: spatial history/ROI,
content or object classification, or remaining dense after restoration. The
first two violate the selected time-only design and materially expand scope;
the last gives up the sparse byte lever for those workloads.

Scope A2 is therefore withdrawn as a fix target. Sparse mode explicitly
permits visible 4:2:0/4:4:4 transitions in fine-chroma content inside an
affected update while recurrent damage continues. Dense mode, selected by
`chroma_refresh_ms=0`, remains the default and quality-preserving mode. The
correctness requirements which remain are bounded refresh, correct main/aux
pairing and full-chroma convergence after actual quiescence. Scope A1 proved
the last of these. Scope B now measures whether the opt-in mode provides enough
byte or throughput value to justify retaining it; insufficient value withdraws
the sparse slice instead of reopening semantic heuristics.

## 2026-08-24 Scope A3 — operator surface and numerical plan

The preceding Scope B plan fixed `wire_window=2` and compared only dense and
sparse chroma. That plan could measure the sparse effect at one nondefault
window, but could not qualify the shipped `wire_window=1`, show what widening
the operator knob changes, or distinguish a sparse effect from an interaction
with the wider window. It is retired before any hardware time is spent.

The replacement is a repeated 2-by-2 comparison:

| condition | wire window | chroma policy |
|---|---:|---|
| A | 1 | dense: refresh 0 ms, idle 0 ms |
| B | 1 | sparse: refresh 1000 ms, idle 100 ms |
| C | 2 | dense: refresh 0 ms, idle 0 ms |
| D | 2 | sparse: refresh 1000 ms, idle 100 ms |

Eight 20-second legs run in `A B C D D C B A` order, giving two repetitions
per cell and balancing simple time drift. The comparisons are sparse versus
dense at each window, window 2 versus 1 under each chroma policy, and their
interaction. A window effect is admissible only if the trace proves that the
wider leg reached a credit or lag state the one-frame window could not admit;
otherwise the result is “wider window not exercised at this RTT/load.” Every
sparse leg must independently show both auxiliary omission and restoration.

The operator-surface audit found no literal internal label in the installed
template, but the template omitted the sparse controls and several other
supported settings, advertised inert `tail_flush`, and mixed current defaults
with historical explanation. The manual used the wrong section number,
omitted supported controls and the external encoder selector, and repeated a
wire-window performance claim whose synchronous producer instrumentation was
voided by #121. Runtime messages and benchmark help also exposed backlog and
internal requirement identifiers.

Scope A3 replaces those surfaces with operator-facing descriptions. The
template now shows the dense default, sparse dependency and quality tradeoff,
and no longer advertises `tail_flush`. The manual is section 5 and documents
the external ffmpeg controls, including sparse intervals and their limitation.
The active T4 profile, runtime messages and benchmark output use behavior names
instead of project archaeology. Historical capture records and source comments
remain unchanged where they are evidence rather than operator output.

`tests/xrdp/check_operator_surface.sh` is a normal TAP test. It rejects
internal work labels on the installed template, manual source, active T4
profile, compiled xrdp runtime strings and textflood help; checks that the
principal paired-reference, credit and sparse keys exist in both template and
manual; and rejects an advertised `tail_flush` assignment. The manual renders
without groff errors, the helper builds and its offline selftest passes, and
the full configured `make check` result is 212/212 PASS.

## 2026-08-24 Scope A3 follow-through — clean-room prose gate

The A3 audit exposed a broader clean-room risk. The development implementation
is an evolutionary record: correct current behavior is interleaved with work
numbers, requirement shorthand, superseded alternatives, measurement claims,
temporary diagnostics and comments explaining how the branch arrived there.
A file-by-file transcription could reproduce the behavior while also carrying
that archaeology into every reviewable commit. It would make the supposedly
clean series depend on the development branch for vocabulary and rationale.

The durable rule now separates three sources. `PRD/` defines behavior and
independent gates. `BACKLOG.md` inventories development files only to prove
coverage. The development code may reveal seams and edge cases, but supplies
no text to copy. Each slice is re-authored from its pinned base and normative
requirements, and is red if a reviewer needs the development branch to explain
a field, condition, comment, test name or commit message.

The per-slice prose review rejects internal work labels, history narratives,
dead or commented-out settings, fault scaffolding, later-slice placeholders,
copied test expectations and unsupported performance claims. A production
comment must instead explain a current non-obvious invariant such as protocol
meaning, ownership, ordering, bounds, security or compatibility. Operator
surfaces must state defaults, ranges, dependencies, tradeoffs, impact and
action entirely in domain language.

The common rule lives in `AGENTS.md` (a symlink to `CLAUDE.md`) and
`PRD/README.md`, so it applies once rather than being copied into all seventeen
slice files. The backlog now labels every development path as inventory, and
the final activation slice explicitly owns a TAP check for internal labels,
required documented controls and removed-key leakage. The obsolete legacy
requirement-name map was removed from the normative PRD because it encouraged
clean-room authors to translate development shorthand instead of implementing
the self-contained specification.
