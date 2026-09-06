# Gate #142C — Development and clean-room behavioral equivalence

## Purpose and boundary

The development frontier and the clean-room series are independent
implementations of one PRD. This gate finds and removes semantic drift between
them before the clean-room history is frozen. It does not require identical
source structure or commit history, and it shall not turn a textual diff into
an implementation transplant.

The first observed red case is dynamic growth resize: the development producer
recomputes the AVC444 capture allocation from the new monitor description,
while the clean-room producer retains the layout received at login. The
clean-room consumer independently derives the new layout, rejects the stale
snapshot and closes the connection. This known discrepancy is the starting
point, not the permitted scope limit.

The current audit starts from development xrdp
`83bcb274bd294f8479422e51e00867f953d46e3e` and xorgxrdp
`985bc42d335ad5828ac76dd3999d1a8266cac953`, compared with clean-room xrdp
`f8d8d06d2ffda21cdf49477d3f62518c1323df94` and xorgxrdp
`aca3c774cb8b828371c555dbed5886f176ddf5ef`. The clean-room xrdp base is pinned
at `fe850a22c08a624c66bbac07e310251782e6f828`; development diverged from that
base at `3af31df3fc18cb4f910524bbccfc90ff3189a757`. If reconciliation or an owning
correction advances one of these branches, the matrix records both the
starting identity and the correction which supersedes it.

## Complete divergence audit

Construct the file inventory from three complete ranges before classifying
requirements: the clean-room pinned base against the development merge base,
all clean-room xrdp slices through the candidate, and all paired clean-room
xorgxrdp slices through its candidate. Audit every changed production, test,
build, configuration and documentation file as well as every normative
requirement and required test in slices #126 through #142 against both paired
trees. For each requirement, record the responsible xrdp and xorgxrdp seams,
observable behavior, defaults/failure policy and the independent test or
retained evidence which proves it. Classify every apparent difference as
exactly one of:

1. equivalent behavior implemented differently — retain both implementations;
2. normative behavior present only in clean-room — implement and validate it
   on development before constructing the comparison arms;
3. normative behavior present only in development — reopen the owning
   clean-room slice; do not hide it by weakening development;
4. non-normative development residue — exclude it from clean-room and remove
   it from development only when an active item explicitly owns that cleanup;
5. unrelated pinned-base behavior — preserve it in the tree which owns it and
   do not count source-shape differences as product divergence.

The audit is semantic. Matching function names, test names, branches or line
counts is not evidence of equivalent behavior. Conversely, independently
authored data structures and control flow are allowed when they implement the
same protocol bytes, ownership/lifetime, bounds, failure outcome and public
contract. All wire structures shared by one deployed xrdp/xorgxrdp pair must
still be version-compatible and interpreted identically.

The audit result is a committed requirement matrix plus its reproducible Git
range and changed-file inventory. It shall list every slice and may group
requirements only when one named test genuinely covers the same invariant.
Blank, assumed, unclassified files or “covered by the final gate” cells are
red. A test whose expected result came from either implementation does not
qualify the other one.

## Development reconciliation

Port every category-2 behavior to the sole development branches in the
canonical checkouts. Category-3 findings update their owning PRD slice and are
proven on development before clean-room history changes. Reconciliation is a
sequence of ordinary, reviewed edits, not a bulk history transplant. Each
functional correction is small and reviewable, with its own deterministic test
where the invariant is amenable to CI. Both paired repositories build against
the same development capture-contract header and pass their complete test
suites.

The reconciled development branch deliberately retains the currently observed
stale-resize behavior for the first live arm. This is not a shipped fallback or
fault-injection mode: it is the clean-room behavior being reproduced on the
otherwise fully reconciled development implementation. The repair is a second
commit on top, so the comparison changes only the owning fix.

## Two-arm dynamic-resize proof

Build two immutable Ubuntu 24.04/FFmpeg 6 server arms from clean,
commit-identified development packages:

* host port `40059` runs the fully reconciled development pair before the
  resize repair. It must reproduce the real-client failure: connect at
  2196-by-1250, resize to 2198-by-1250, then grow to 2412-by-1344; retain the
  old 16,760,832-byte mapping, reject the first stale snapshot and disconnect
  the client while preserving the X session.
* host port `40060` differs only by the proposed resize repair. The same client,
  credentials, session image, `gfx.toml`, codec, resize sequence and observation
  window must allocate the 19,611,648-byte layout before capture resumes,
  remain connected and deliver a post-resize frame. No retry, fallback or codec
  substitution is permitted.

Use a versioned client-side dynamic-resolution script. It shall record the
actual monitor-update dimensions and server allocation/reuse decisions rather
than infer them from requested window size. The binary outcome needs only a
short run; performance tracing and per-frame normal logging remain disabled.
Capture complete xrdp, sesman and Xorg logs, deployed package/image/config
identities, client command and exit state, connection liveness and the
post-resize rendered assertion for both arms.

The comparison is red unless the pre-fix arm reproduces the same mechanism and
the repaired arm changes that mechanism. A pre-fix arm which stays connected
does not validate the fix; it means the clean-room discrepancy was not fully
ported or the reproduction did not reach the failing geometry. A repaired arm
which survives by changing codec, mode, payload, client or session state is
also red.

## Repeated-resize wire-transition proof

Allocation equivalence alone is insufficient. With the same interactive
client, capsets, codec profile and sequence of rapid and spaced resizes, the
development and clean-room pairs shall agree on every client-visible graphics
transition. Retain explicit identities for the last acknowledged frame, the
first unacknowledged frame, any subsequent capability advertisement, surface
reset and replacement encoder. If either pair elicits a new capability
advertisement while a direct producer is live, the other pair must first
reproduce the same precursor after all normative behavior is reconciled.

The first comparison is observational, not a behavior port. Compile both arms
with the repository's trace sink and record the same bounded schema at the
logical RDPGFX send/receive boundary. Each outbound record identifies command
order, surface and frame when present, coded and visible geometry, logical
payload and segmented transport lengths, a bounded sample from the payload
edges and the send result. Each inbound acknowledgement records its frame and
decode frontier; each capability advertisement records every advertised
version and flags together with the immediately preceding outbound sequence.
The measured path shall not synchronously write, allocate a second trace ring,
or scan an encoded payload solely to fingerprint it. Exact identities, rather
than nearest timestamps, pair the events.

The working hypothesis is that the first clean-room frame which Windows does
not acknowledge differs from development in one of those client-observable
properties, and that the difference precedes the new capability advertisement.
The hypothesis is falsified if the two transaction sequences are equivalent at
this boundary. No implementation branch may be ported merely because its
source shape differs; a port must name the differing observation it predicts
and the trace result which would falsify that mechanism.

A repeated capability advertisement is a state replacement, not initial
setup. Before publishing a replacement surface or encoder, xrdp shall
completely retire the current encoder and its children, queues, borrowed
captures and acknowledgement ownership. After the successor is ready, it
shall request exactly one current full-screen update from the direct producer.
The replacement must render a complete current desktop; connection liveness
alone does not satisfy this gate.
Removing a known advertisement trigger does not discharge the replacement
lifecycle requirement. A deterministic test shall drive the real callback with
a live encoder, pending work/completions and borrowed captures while serialized
regions remain within visible bounds. It shall prove old-worker termination
before freeing shared state, exactly-once capture/acknowledgement disposition,
no old completion entering the successor, and failure-safe handling of a
shutdown or replacement-creation failure. Repeat the existing rendered gate
after the successor is ready.

Do not correct only a source-reachable handler on a development build which
does not exhibit the client-visible precursor. That would prove a unit branch,
not the observed failure. First reproduce and identify the behavioral
divergence, then demonstrate that the correction changes the same mechanism.
If a later peer EOF remains after the precursor and replacement transition are
correct, keep it red as a separate defect rather than masking it with retry,
fallback, codec selection or timeout changes.

## Clean-room correction and closure

Only after the development audit, paired CI and two-arm red/green proof pass may
the repair be re-authored into the clean-room slice which owns the violated
invariant. Amend that slice, then replay and independently gate every descendant
commit through #142. Do not append a post-#142 fix commit and do not transcribe
the development patch.

The final clean-room candidate repeats the deterministic resize test and its
ordinary profile/smoke gates. #142C closes only when the requirement matrix has
no unresolved category-2 or category-3 rows, the corrected dev and clean-room
pairs agree behaviorally, and all evidence names the exact paired commits,
packages, configuration, client and trace-disabled state.
