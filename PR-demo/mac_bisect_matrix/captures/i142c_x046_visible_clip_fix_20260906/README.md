# Forward visible-edge correction on canonical development

Owner direction, 2026-09-06: commit a manual development correction and its
specification, name the clean-room slice to repair later, and replace port
40062 with the fixed commit. Keep the explicit red commit on the same linear
ancestry. Do not start clean-room: a separate development clipboard file-transfer
bug precedes that work. The two other comparison ports are unchanged.

## Red evidence and independent regression

The intentionally overflowing source commit c729a50889a2 remains an ancestor.
Its real Windows failure is retained in the sibling capture
../i142c_x046_windows_20260906T152500Z. The red image remains locally retained
as localhost/xrdp-bisect:dev-odd-edge-40062 with image ID
78abda7880067cf8816104077c28512c3dc504e549acf39131a480d1ec45b31d.

The prior diagnostic test explicitly expected the out-of-surface edge. Its
retirement was conditional on reproduction; the owner has now authorized the
forward correction. Retire that test in a separate evidence/specification/test
commit before editing production behavior. Existing shipping assertions are
unchanged. New independently derived cases assert that full damage ends at
the visible width/height for both LC views, even dimensions, odd width, odd
height, both odd dimensions, and a nonzero outer origin. The sizes include
both failed Windows resize surfaces. Expected edges follow containment and
full-damage coverage, not outputs of the implementation.

With production unchanged, the targeted suite has 16 checks: eight pass and
eight fail, all new odd-size cases, at exactly the outside edge. red-metablock.txt
retains the failing assertions; red-build.txt retains the build. This deliberate
red checkpoint separates the changed test contract from the subsequent code
correction. The six existing shipping checks and two new even-size cases pass.

The first formatter invocation rejected a literal filename because the repo
options request recursion. Re-running with a quoted single-file C wildcard
succeeded; no generated object was matched or changed. The installed formatter
is astyle 3.1; this is not a claim to have run CI's pinned 3.4.14.

## Specification and deferred clean-room ownership

PRD/README.md and slice 135 now require even outward alignment followed by
visible-bound clipping. Only the clipped right/bottom edge may have an odd
extent. Coded padding cannot enlarge the visible destination or authorize an
outside region. Slice 135 owns the future clean-room region builder, visible-
bounds argument seam, serializer header and independently authored tests.
Subsequent consumers must be replayed after development Windows acceptance
and explicit resumption by the owner. Neither clean-room checkout is touched.

Build, deploy, source gates and final handoff will be recorded as the forward
correction proceeds. A unit-test pass does not establish Windows acceptance.

The first evidence commit attempt stopped at diff whitespace checking because
three verbatim archived sesman command lines end with spaces. The capture is
preserved unchanged. Source, test and specification whitespace checks pass;
the raw-data warnings are not production formatting errors.
