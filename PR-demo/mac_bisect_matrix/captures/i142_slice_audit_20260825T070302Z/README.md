# Red exact-slice audit: trace-enabled #133 did not compile

This audit stopped at the first source failure. Slices #126 through #132 passed
their available default and trace-enabled full suites. #133 passed its default
suite (87/87) but its trace-enabled build failed to compile:

```
xrdp_encoder_ffmpeg.c:1046:32: error: ‘force_idr’ undeclared
```

At #133 there is one runner role, so `force_idr` does not exist yet. The later
#137 slice introduces separate ordinary-main and forced-IDR auxiliary roles.
The clean-room history was corrected at both ownership boundaries: #133 marks
its only role as main, and #137 changes the assignment to `!force_idr` when the
second role first exists. No downstream result from this red run is claimed.
The complete corrected audit is in sibling capture
`i142_slice_audit_20260825T071722Z`.

`gates/133-trace.FAILED.log` is the unabridged failure. The TSV is intentionally
partial and records the red row.
