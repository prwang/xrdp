# CLEANUP_PLAN.md

How to extract a clean, upstream-ready PR for **DPI-1** from this development
branch, leaving all process/scaffolding files behind on the dev branch.

This repo's dev branch (`fix/client-dpi-propagation`) deliberately carries
in-tree process artifacts (transparent backlog, rules, runbooks, packaging).
Those are **ours**, not upstream's. The PR to `neutrinolabs/xrdp` must contain
**only the functional change + its tests** — nothing else.

---

## 1. What goes UPSTREAM (the PR scope) — 21 files

These are the only files the PR may touch. They are the union of the two DPI-1
commits (`480c596e`, `40455218`) **minus `BACKLOG.md`**.

```
common/Makefile.am
common/xrdp_client_info.c            (new)
common/xrdp_client_info.h
libipm/eicp.c
libipm/eicp.h
libipm/libipm_private.h
libipm/scp.c
libipm/scp.h
sesman/scp_process.c
sesman/sesexec/eicp_server.c
sesman/sesexec/session.c
sesman/sesexec/session.h
sesman/tools/sesrun.c
tests/common/Makefile.am
tests/common/test_common.h
tests/common/test_common_main.c
tests/common/test_xrdp_client_info.c (new)
tests/libipm/test_libipm.h
tests/libipm/test_libipm_recv_calls.c
xrdp/xrdp_login_wnd.c
xrdp/xrdp_mm.c
```

## 2. What STAYS on the dev branch (NOT in the PR)

Process, planning, and local-tooling files. They remain committed here for our
own traceability and review history; they are never imported into the PR branch.

```
BACKLOG.md            – transparent task backlog (ours)
CLAUDE.md             – agent/contributor rules (ours)
PRD.md                – requirements doc (ours)
dev_config.md         – interactive DPI-1 test runbook (ours)
build_config.md       – build/package/deploy + headless checks (ours)
normal_config.md      – stock-baseline runbook (ours)
CLEANUP_PLAN.md       – this file
PR.md                 – the text we paste into the GitHub PR
FAQ.md                – maintainer-objection prep
scripts/build_dev_deb.sh – local commit-tagged .deb packer
.gitignore            – dev-only additions (/dist, *.deb, connectmon)
dist/ , *.deb         – build artifacts (already gitignored)
```

> Rationale: upstream has its own packaging, its own contribution process, and
> no use for our backlog/PRD/runbooks. Importing them would bloat the diff,
> invite "why is this here?" review noise, and leak our internal scaffolding.

## 3. Procedure — pristine worktree, import only the scope

Work from a **fresh worktree off the upstream base** so nothing from the dev
branch can leak in. The base is the commit this work was branched from
(`21d38d0c`, upstream `devel` HEAD at branch time); rebase onto current
upstream `devel` before opening the PR.

```sh
# 0. From the dev checkout (/work). Confirm the scope builds & tests green first.
make check            # libcommon 172, libipm 39, libxrdp 13, memtest 1, daemon 26

# 1. Create a pristine worktree on a NEW branch off the upstream base.
#    (Add the real upstream remote if not present.)
git remote add upstream https://github.com/neutrinolabs/xrdp.git 2>/dev/null || true
git fetch upstream
git worktree add -b dpi-xorg-from-client ../xrdp-pr upstream/devel

# 2. Import ONLY the 21 scope files from the dev work (tree state, no history).
cd ../xrdp-pr
DEV=fix/client-dpi-propagation          # or the exact SHA 40455218
git checkout "$DEV" -- \
  common/Makefile.am common/xrdp_client_info.c common/xrdp_client_info.h \
  libipm/eicp.c libipm/eicp.h libipm/libipm_private.h libipm/scp.c libipm/scp.h \
  sesman/scp_process.c sesman/sesexec/eicp_server.c \
  sesman/sesexec/session.c sesman/sesexec/session.h sesman/tools/sesrun.c \
  tests/common/Makefile.am tests/common/test_common.h \
  tests/common/test_common_main.c tests/common/test_xrdp_client_info.c \
  tests/libipm/test_libipm.h tests/libipm/test_libipm_recv_calls.c \
  xrdp/xrdp_login_wnd.c xrdp/xrdp_mm.c

# 3. Sanity: the diff must contain NONE of the dev-only files in section 2.
git status --short
git diff --cached --stat | grep -E 'BACKLOG|CLAUDE|PRD|dev_config|build_config|normal_config|CLEANUP|PR\.md|FAQ|build_dev_deb|\.deb' \
  && echo "!! LEAK - remove before committing" || echo "clean scope"

# 4. Build + test from clean in the pristine tree.
git submodule update --init
./bootstrap && ./configure && make -j"$(nproc)" && make check

# 5. Commit as ONE clean, well-described commit (use PR.md body as the message).
git add -A
git commit            # paste PR.md (drop the GitHub-only checkboxes if desired)

# 6. astyle + cppcheck exactly as CI does, then push to YOUR fork and open the PR.
./scripts/run_astyle.sh
git push origin dpi-xorg-from-client     # origin = your fork
# Open PR against neutrinolabs/xrdp:devel, body = PR.md, link issue #3473.
```

## 4. Pre-submit checklist

- [ ] `git diff upstream/devel --stat` lists **only** the 21 files in section 1.
- [ ] No file from section 2 appears in the PR diff.
- [ ] Fresh `./bootstrap && ./configure && make` succeeds (new `common/*.c` →
      `Makefile.am` updated, so bootstrap must regenerate).
- [ ] `make check` green; new tests present (libcommon +15, libipm +4).
- [ ] astyle (pinned 3.4.14) and cppcheck clean, matching `.github/workflows`.
- [ ] One squashed, signed-off-friendly commit; message mirrors `PR.md`.
- [ ] Rebased on current `upstream/devel`; `LIBIPM_VERSION` bump still ends in 3
      (re-bump if upstream changed it meanwhile — see FAQ "version conflict").
- [ ] PR description links #3473 and notes the breaking-wire-change/lockstep.
