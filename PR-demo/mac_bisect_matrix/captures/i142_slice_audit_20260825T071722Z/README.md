# Corrected exact-slice audit for #126 through #142

This is the retained commit-by-commit closure audit for the paired clean-room
series. It builds and tests the code at each commit rather than testing only
the assembled final tree.

## Identities

* xrdp branch `cleanroom/avc444_ffmpeg`, pinned ancestor
  `fe850a22c08a624c66bbac07e310251782e6f828`, final commit
  `b38c63473c5297250af325d2770c5219ede69d48`;
* xorgxrdp branch `cleanroom/avc444_ffmpeg`, pinned ancestor
  `49bf2dd3546dc48b9d5bae62022762fde11793d0`, final commit
  `3dc52da1321644bda7678fb246d815dc27bd9bef`;
* final xrdp tree `5ceed6c1fb3a52d7daa1343b0da6029a3aa7f2f1`;
* final xup header SHA-256
  `9c37f12ef823e90f6e75960d11cd1cddc15a4161f9ad48e7855b6773e8fe5d49`.

There are exactly 17 ordered xrdp commits and five paired xorgxrdp commits.
Both repositories have the pinned base as an ancestor and use the same branch
name. Every commit message has a subject plus explanatory body and contains no
escaped newline artifact.

## Valid results

`xrdp-slice-gates.tsv` contains 33 green full-suite rows: default for #126,
and default plus compile-time trace-enabled for every slice from #127 through
#142. Each row's xrdp PASS count equals TOTAL. The corrected #133 rows are
87/87 in both modes; corrected #137 is 114/114; corrected #141 is 187/187;
and final #142 is 203/203.

`xorgxrdp-slice-gates.tsv` contains five green paired rows. #126 and #128 each
have the one suite present at those commits; #129 and #136 each have two; #140
has three total tests. Every paired build used the exact xrdp checkout named in
the same row as its include source.

`xrdp-static-gates.tsv` contains 17 green rows for `git diff --check`, astyle
3.4.14 with no resulting diff, and cppcheck 2.20.0 after bootstrap/configure.
`xorgxrdp-static-gates.tsv` records `git diff --check` and changed-hunk
prose/style review for all five producer commits. xorgxrdp has no repository
xrdp-astyle or cppcheck gate; its compiler build and tests are recorded in the
paired full-suite table.

The individual `gates/*.summary.txt` files retain commands, commit identities
and every emitted Automake suite summary. The disposable checkout paths are
recorded only to make clear that no live branch worktree supplied the results;
they are not durable dependencies.

## Red audit and collector corrections

The preceding sibling capture stopped when old #133 did not compile with
tracing. This capture tests the repaired history. During this audit, pinned
astyle 3.4.14 also found a continuation-indent defect at old #141. The
formatter diff is retained as `gates/141-static.FAILED.log`; #141 was amended
before #142 was replayed, and its corrected full/static summaries and TSV rows
are green.

Two retained `FAILED.log` files are collector setup failures, not source
verdicts:

* `126-static.FAILED.log` ran cppcheck before configure generated
  `config_ac.h`; the corrected CI-order run is green for all 17 commits.
* `xorg-126-static.FAILED.log` applied xrdp's recursive whole-tree formatter to
  xorgxrdp, which has a different pinned-base style and no such repository
  gate. It rewrote untouched base lines. The corrected xorg gate reviews only
  changed hunks and retains the clean compiler/test result.

The first xorg result collector also assumed that every paired slice already
had `tests/yuv444/test-suite.log`. #126 and #128 precede that suite. The final
table instead sums every test-suite log actually emitted by each exact commit
and refuses a commit with no test log. No blank-count row remains.
