# #142 local numerical replay — invalid LTR replacement

No timing leg ran and no performance number from this directory is usable.

This shakedown changed only `aux_ltr_chain` from the preceding default-leaf
profile. AVC444 capability preflight then rejected the live libx264 output as
`content-rejection`, so codec selection chose RFX and the certification client
never created an AVC dump. The retained `client.nodump.log` is the exact
client-side evidence; the server log identified the typed preflight rejection.

The rejection is explained by the produced access unit, not by a generic
ffmpeg failure. At 1024 by 768, `-tune zerolatency` lets libx264 use sliced
threads and the first picture contained twelve VCL slices. The optional LTR
rewriter intentionally accepts one slice per picture and rejected that shape.
The same built-in arguments happen to emit one slice at the old 64 by 64 unit
geometry, which is why the smaller direct probe passed. The pre-confirm probe
therefore did its job: it refused a topology the configured encoder could not
provide and did not enter a retry loop.

This does not invalidate the default auxiliary-leaf profile or authorize an
encoder-argument change. The numerical replay is intended to test the shipped
software default, so it returns to `aux_ltr_chain=false`. The deployment
certifier now selects a topology-specific auxiliary-leaf audit rather than
requiring LTR syntax from every AVC444 configuration.
