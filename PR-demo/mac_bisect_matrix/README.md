# mac_bisect_matrix — containerized per-variant xrdp fleet

Owner directive (2026-07-26, after two host-breakage incidents): bisect arms
are **never** deployed by mutating the host's single xrdp instance. Every
variant runs as its own k3s pod with its own pinned xrdp-dev deb and its own
`gfx.toml`, all up simultaneously, so the tester (owner's Mac) validates the
entire matrix in one sitting. **Server side only** — the client harness on
the host is never touched by this rig.

## Current matrix (Mac blackout bisect, pocket = {HRD VUI} + {SEI NALs})

| arm | port (host loopback) | xrdp-dev | encoder config | meaning |
|-----|----|----------|----------------|---------|
| A | 127.0.0.1:40000 | 52099149 | CQP 20 | control-good (Mac-validated baseline) |
| B | 127.0.0.1:40001 | 52099149 | CBR 20M + `-sei +timing` | control-black (M1 repro: HRD VUI + BP/PT SEI) |
| C | 127.0.0.1:40002 | e96e655416dc | CBR 20M + `-sei +timing` + `strip_sei` | SEI NALs removed post-encode, HRD VUI stays |
| D | 127.0.0.1:40003 | 52099149 | CBR 20M | rate control without the SEI flag |
| E | 127.0.0.1:40004 | c693eeab5ec2 | CBR 20M + `strip_sei` + `sanitize_hrd` | fix candidate: SPS rewritten to drop nal_hrd |

2026-07-27 owner verdict on A–D: A renders, B/C/D black. arm-c (zero SEI
NALs, HRD VUI kept) black => nal_hrd_parameters in the SPS VUI convicted;
timing_info exonerated (present in A). arm-d shows Mesa emits HRD + SEI
from CBR alone. arm-e carries the fix candidate: xrdp_h264_sanitize_hrd()
rewrites every SPS post-encode (golden unit test: captured arm-c SPS
rewrites to captured arm-a SPS byte-for-byte).

Original A–D verdict table (resolved 2026-07-27, kept for the record):
C renders ⇒ SEI NALs convicted. C black ⇒ HRD VUI in the SPS convicted
(what happened). Now: E renders ⇒ sanitize_hrd+strip_sei is the proven
macOS fix, portable to T4/nvenc. E black ⇒ conviction wrong, re-open.

Every arm's session is the same full-screen banner (arm name + colour field
stepping 1/s), so a frozen/black screen is a pipeline failure by
construction, and the tester always knows which arm is on screen.

## Files

- `Containerfile` — Debian 13 (= dev box) + distro ffmpeg/Mesa VAAPI +
  pinned debs; parametrized by `XRDP_DEB` build-arg.
- `entrypoint.sh` / `startwm.sh` / `banner.sh` — pod boot + deterministic
  session content.
- `gfx/arm-*.toml` — full per-arm gfx.toml (single source of truth; becomes
  ConfigMap `xrdp-gfx-<arm>`).
- `k8s/*.yaml` — namespace + one Deployment per arm (privileged, `/dev/dri`
  hostPath, `hostPort` pinned to `hostIP 127.0.0.1`).
- `build_and_deploy.sh` — build → import into k3s → apply → roll → wait.

## Host assumptions (this dev box)

- k3s single node in LXC: `/etc/rancher/k3s/config.yaml` carries
  `KubeletInUserNamespace` + `conntrack-max-per-core=0`; `/dev/kmsg` is a
  symlink to `/dev/console` (recreate after reboot if missing).
- Pods reuse the host `tester` password via its shadow hash in root-only
  `/etc/xrdp-matrix/tester.hash` (written by `build_and_deploy.sh`).
- GPU: AMD render node `/dev/dri/renderD128`, shared by all arms and the
  host instance (VAAPI contexts are independent; fine at banner frame
  rates).

## Adding/changing an arm

Edit/add `gfx/arm-X.toml` + `k8s/arm-X.yaml` (next port), map the arm in
`build_and_deploy.sh` if it needs a different deb, rerun the script. Commit
the yaml/toml with the bisect log entry in `BACKLOG.md` — the matrix in git
must always describe what is actually listening.
