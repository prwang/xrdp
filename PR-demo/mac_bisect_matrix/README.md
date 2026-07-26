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

Verdict table: C renders ⇒ SEI NALs convicted (strip_sei is the nvenc fix
candidate). C black ⇒ HRD VUI in the SPS convicted. D's meaning is fixed by
byte-verification of what Mesa actually emits under plain CBR.

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
