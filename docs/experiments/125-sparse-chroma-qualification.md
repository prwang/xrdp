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
