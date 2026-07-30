# E5-2 on the T4 — measurement protocol

Running the BACKLOG **#52 (E5-2)** frame-interval A/B on the T4, where
there is exactly **one xrdp instance** and the two arms must therefore be
installed one after the other. Written before the launch so the cloud time
goes into measuring, not discovering. Everything here is scripted or a
copy-paste command; nothing needs improvising on the box.

**Reference result to compare against (dev box, VAAPI, 2026-07-30):**

| arm | xrdp | mean per send | sends/s | per-monitor period | worker busy | `kids_armed=4` |
|---|---|---|---|---|---|---|
| baseline | steps 0–4 | 63.6 ms | 15.72 | 127.4 ms | 16 % | n/a |
| batched | steps 0–7 | **29.9 ms** | 33.40 | 59.9 ms | 32 % | 52 % |
| | | **2.13×** | | | | |

Evidence: `../mac_bisect_matrix/captures/e52_flood2_arm-s_20260730/`.
The T4 is a different encoder (NVENC) and a much weaker CPU, so the
absolute numbers will differ; **the ratio is the result**, and the T4's is
the one that goes in the PR.

---

## 0. Time and cost budget

| phase | wall clock | why it costs that |
|---|---|---|
| instance up + credential restore + harness install | ~10 min | `t4_restore_cred.sh`, `e52_t4_payload.sh install` |
| install baseline pair + verify | ~5 min | two debs, `dpkg -l` gate |
| baseline measurement | ~5 min | 180 s run + collection |
| install batched xrdp + verify | ~4 min | one deb, `dpkg -l` gate |
| batched measurement | ~5 min | 180 s run + collection |
| pack bench (both builds) | ~4 min | `avc444_pack_bench` per CLAUDE.md |
| smoke gate + teardown for onscreen | ~6 min | last step before a human looks |
| owner onscreen (UWP + macOS) | as long as you want | §6 checklist |

**≈40 min of instance time** for the full protocol excluding the onscreen
session. Every artifact lands on the **dev box**, not on the T4 — the
client side runs here (CLAUDE.md T4 methodology), so nothing of value is
lost when the instance goes away.

---

## 1. Pre-flight (before the instance is needed)

Build both debs on the dev box and keep them in `/work/dist`. For the
2026-07-30 measurement these were:

| role | deb | contents |
|---|---|---|
| **baseline** | `xrdp-dev_0.10.80+git20260730013437.5dae11f63adb_amd64.deb` | #45 steps 0–4 + the log clock fix (branch `bench/e52-arm-t-baseline`) |
| **batched** | `xrdp-dev_0.10.80+git20260730013346.52b8798839ad_amd64.deb` | #45 steps 0–7 + the log clock fix |
| **xorgxrdp (both)** | `xorgxrdp-dev_1%3a0.10.80+git20260729225933.d77d05463e52_amd64.deb` | step 6: per-monitor capture budget, coverage intersect, per-monitor slot |

Rebuild them from a clean tree with `scripts/build_dev_deb.sh` if the
branch has moved; a `+dirty` version string means the tree was not clean
and the deb is not traceable — rebuild rather than deploy it.

> ### The single-instance trap: version order does NOT follow code order
> The deb version is `base+git<COMMIT TIMESTAMP>.<hash>`. The baseline deb
> above was **built later** than the batched one, so it sorts **newer**
> (`20260730013437` > `20260730013346`) even though its code is older.
> Consequences, both real:
> * going baseline → batched is a **downgrade** to dpkg and needs
>   `--allow-downgrades`;
> * `apt` will happily "upgrade" you back to the baseline if anything
>   re-resolves.
>
> So: **never trust the install order — verify the hash before every
> measurement** (§3, gate G2). The hash is in the version string, which
> `e_gate_run.sh` records into `deployed_packages.txt` on its own.

Also on the dev box:

* `/root/.t4_host` — `ubuntu@<ip>` for the new instance (one place to
  update; every wrapper reads it).
* `/root/.ssh/tmp_access_T4` — the key.
* an H.264-capable client rig, already installed here: the oracle client at
  `/opt/freerdp-vaapi/bin/xfreerdp` and `xfreerdp3` for the smoke gate.
  **Install nothing client-side on the T4.**

---

## 2. Bring the instance up

```sh
# 1. credential invariant (cloud-init re-locks `ubuntu` on a fresh AMI)
bash PR-demo/t4_profile/t4_restore_cred.sh

# 2. server packages (DEPLOY_RUNBOOK §2 for a bare AMI; then:)
scp -i /root/.ssh/tmp_access_T4 \
    /work/dist/xrdp-dev_0.10.80+git20260730013437.5dae11f63adb_amd64.deb \
    /work/dist/xorgxrdp-dev_1%3a0.10.80+git20260729225933.d77d05463e52_amd64.deb \
    "$(cat /root/.t4_host):/tmp/"
ssh -i /root/.ssh/tmp_access_T4 "$(cat /root/.t4_host)" \
    "sudo apt-get install -y --allow-downgrades /tmp/xrdp-dev_*.deb /tmp/xorgxrdp-dev_*.deb"

# 3. encoder config: the #45 profile — scheduled paired refresh, NOT -g 30000
scp -i /root/.ssh/tmp_access_T4 \
    PR-demo/t4_profile/gfx-t4-nvenc-ltr-g240-gate.toml \
    "$(cat /root/.t4_host):/tmp/gfx.toml"
ssh -i /root/.ssh/tmp_access_T4 "$(cat /root/.t4_host)" \
    "sudo install -m 644 /tmp/gfx.toml /etc/xrdp/gfx.toml && sudo systemctl restart xrdp"

# 4. the benchmark payload (autostart entry + corpus), then arm it
sh PR-demo/t4_profile/e52_t4_payload.sh install
sh PR-demo/t4_profile/e52_t4_payload.sh arm codeflood
sh PR-demo/t4_profile/e52_t4_payload.sh status
```

`e52_t4_payload.sh` is checksum-gated: re-running `install` after editing
the payload re-installs only what changed. Arming writes
`/etc/xrdp-e52-payload` and takes effect **at the next login**, which is
what `e_gate_run.sh` produces as its first step. This is deliberate: the
only sanctioned operations on a live remote GUI session are logging it off
and autostarting at login (CLAUDE.md, owner directive 2026-07-27).

**The credential for the client.** ubuntu's RDP password lives only in
root-owned `/root/.ubuntu_cred` **on the T4** and must not be stored on the
dev box. Stage it in tmpfs for the run and shred it afterwards:

```sh
install -m 600 /dev/null /dev/shm/.t4_cred
ssh -n -i /root/.ssh/tmp_access_T4 "$(cat /root/.t4_host)" \
    'sudo cat /root/.ubuntu_cred' > /dev/shm/.t4_cred   # never echoed
...   # runs
shred -u /dev/shm/.t4_cred
```

---

## 3. Measure — the same harness as the fleet

`e_gate_run.sh` grew an `E_TARGET=ssh` mode on 2026-07-30 for exactly this:
identical client side, identical analysis, only the server-side identity,
log collection and cold-session reset go over ssh instead of `kubectl`.

```sh
# one port-forward for the whole campaign
ssh -f -N -i /root/.ssh/tmp_access_T4 -L 33389:127.0.0.1:3389 \
    "$(cat /root/.t4_host)"

cd /work/PR-demo/mac_bisect_matrix

# --- BASELINE (steps 0-4 installed) ---
E_TARGET=ssh E_PORT=33389 E_USER=ubuntu E_CRED_FILE=/dev/shm/.t4_cred \
  E_MODE=oracle E_REFRESH=240 \
  E_OUT=$PWD/captures/e52_t4_baseline_$(date +%Y%m%d) \
  ./e_gate_run.sh 180
```

Then install the batched deb and repeat, passing the baseline's own mean:

```sh
ssh -i /root/.ssh/tmp_access_T4 "$(cat /root/.t4_host)" \
    "sudo apt-get install -y --allow-downgrades /tmp/xrdp-dev_*52b8798839ad*.deb"
# G1..G4 below, THEN:
E_TARGET=ssh E_PORT=33389 E_USER=ubuntu E_CRED_FILE=/dev/shm/.t4_cred \
  E_MODE=oracle E_REFRESH=240 E5_BASE_MS=<baseline mean from its VERDICT.txt> \
  E_OUT=$PWD/captures/e52_t4_batched_$(date +%Y%m%d) \
  ./e_gate_run.sh 180

# side-by-side decomposition (who waits on whom)
python3 e52_flood_analyze.py \
  "T4 baseline=captures/e52_t4_baseline_*/gfx_trace.txt" \
  "T4 batched=captures/e52_t4_batched_*/gfx_trace.txt"
```

### Gates — a number only counts if all four pass

The harness checks G1–G3 itself and aborts or warns; G4 is on you.

* **G1 `xorgxrdp-dev` still installed.** An xrdp-dev deb carries
  `Breaks: xorgxrdp (<< 1:0.10.80~)` and has silently removed it before
  (DEPLOY_RUNBOOK §2b). The harness now **aborts** if `dpkg -l` does not
  show it.
* **G2 the deployed commit hash is the arm you meant.** Read it in
  `deployed_packages.txt`; see the version-sort trap in §1.
* **G3 the payload is declared.** `deployed_session_kind.txt` must say
  `codeflood`. If it says `UNKNOWN` the marker is not armed and the run is
  measuring an idle desktop.
* **G4 the session is fresh.** `E_COLD=1` (the default) logs the session
  off first, so the new xorgxrdp module is actually loaded. A surviving
  session keeps the PREVIOUS module: with a matching xup contract it pairs
  silently and you measure the old code (CLAUDE.md, T4 deployments).

### Per-build extras the T4 owes (CLAUDE.md)

```sh
# pack bench on the reference CPU, after every deb touching the encoder path
gcc -O2 -o /tmp/pack_bench tools/avc444_pack_bench.c && \
  scp -i /root/.ssh/tmp_access_T4 /tmp/pack_bench "$(cat /root/.t4_host):/tmp/" && \
  ssh -i /root/.ssh/tmp_access_T4 "$(cat /root/.t4_host)" /tmp/pack_bench
# record the ms/frame in BACKLOG.md beside the deployed hashes
```

---

## 4. Raw data: what is collected, where it lands, what to keep

Each `e_gate_run.sh` run writes one directory under
`PR-demo/mac_bisect_matrix/captures/` **on the dev box**:

| file | what it is | keep? |
|---|---|---|
| `VERDICT.txt` | the generated report: interval mean/p50/p90/p99, sends/s, per-monitor period, `kids_armed` histogram, worker busy %, E2 zero-counts, wire audit, black-frame check | **yes, commit** |
| `gfx_trace.txt` | every `GFX_TRACE` line — the source for every number, re-analysable offline | **yes, commit gzipped** (~0.4 MB/180 s) |
| `xrdp.log` | the server log windowed to this run | **yes, commit gzipped** |
| `session-xorg.log` | the session Xorg/xorgxrdp log, windowed | yes (small) |
| `deployed_packages.txt`, `deployed_image.txt`, `gfx.toml`, `deployed_session_kind.txt` | what was actually deployed and which payload ran | **yes** — a gate result without these is not a gate result |
| `client.log`, `client-monitors.txt` | client side, incl. the geometry actually presented | yes |
| `oracle/oracle_avc_s*.bin` | the raw AVC444 dump per surface | **NO — do not commit.** Audit it, then delete |
| `E5-2_decomposition.txt`, `E2_prefix_audit.txt` | analysis outputs you generate | **yes** |

**Dump sizes are the one thing that will surprise you.** The oracle client
saves every picture. Measured at 2560×1440 + 3840×2400 under `codeflood`:
**5 GB (baseline) and 11 GB (batched) per 180 s run** — the batched arm
pushes twice the pictures, so its dump is twice the size. At the T4's owner
geometry expect the same order of magnitude. They land in `/tmp` on the dev
box and are moved into the capture dir, so:

* check `df -h /work /tmp` before starting (want ≥ 40 GB free);
* the wire audit reads a capture whole — a 7 GB dump OOMs it. Audit a
  prefix and say so in the README:
  `head -c 1200000000 oracle_avc_s0.bin > /tmp/prefix.bin` then
  `tools/avc444_ltr_wire_audit.py --assert --intra-refresh 240 /tmp/prefix.bin`
  and `oracle_black_frame_check.py /tmp/prefix.bin`;
* delete `oracle/` once the audit has run. The README records how to
  reproduce it.

Set `E_MODE=render` for a second, short run if you also want the end-to-end
rate as context — it is not the gate (the session is client-bound by ~3.3×).

---

## 5. Cleanup — both boxes

```sh
sh PR-demo/t4_profile/e52_t4_payload.sh disarm      # T4: stop the flood at next login
ssh -i /root/.ssh/tmp_access_T4 "$(cat /root/.t4_host)" \
    "pkill -TERM -u ubuntu -x xterm; pkill -TERM -u ubuntu Xorg"   # log the session off
shred -u /dev/shm/.t4_cred                          # dev box: credential
bash PR-demo/mac_bisect_matrix/sessions_off.sh      # dev box: fleet sessions + client rig
rm -rf PR-demo/mac_bisect_matrix/captures/e52_t4_*/oracle
```

**Why the cleanup section is not optional** (owner report 2026-07-30): a
session keeps running its payload after the client disconnects. Eight fleet
arms had accumulated sessions burning ~4 cores — two flood sessions at 97 %
and 89 % of a core, and 10 Hz sessions that had been scrolling for 23 h.
`sessions_off.sh` exists so this is one command. The same applies on the
T4, where it is billed by the hour.

---

## 6. What changed since the last T4 test, and what to watch onscreen

### Last T4 test

**2026-07-28**, commit `26b002b4` "T4 nvenc gate: FR-H264-8 LTR chain holds
on real NVENC hardware", against xrdp `2a0279ef3aa1` (2026-07-28 18:47) +
xorgxrdp `5b9650cafbc3`, config `gfx-t4-nvenc-ltr.toml` with **`-g 30000`**.
Owner verdict then: renders correctly onscreen on Windows (including
multimon) and macOS.

### Since then: 41 xrdp commits, 1 xorgxrdp commit, and a config change

Client-visible items are marked **[WATCH]** and expanded below.

1. **#45 — scheduled paired intra refresh** (`9653bd1f`, `a0d9e773`,
   `bce0b77e`): every 240 pairs BOTH views emit a mid-stream **non-IDR I**
   picture that self-marks into its own LTR slot; no IDR, nothing flushed.
   This replaces `-g 30000`, so the config changes too: use
   `gfx-t4-nvenc-ltr-g240-gate.toml` (`intra_refresh_frames = 240`, `-g`
   pinned to the same value by the runner). **[WATCH]** — the mid-stream
   non-IDR I slice is the one genuinely new wire element since the last T4
   test.
2. **#45 step 5 `pump_set`** (`3ceed31d`) and **step 7 two-monitor batch**
   (`f7acb597`): one worker thread drives up to four encoder children under
   one shared deadline. **[WATCH]** — multimon timing coupling.
3. **#45 step 6, xorgxrdp `d77d05463e52`**: per-monitor outstanding capture
   budget, coverage intersect, per-monitor slot. Also carries `5b9650ca`'s
   up-front shmem reservation, which **refuses the client at connect time**
   if `/dev/shm` is too small instead of SIGBUSing mid-session — check
   `/dev/shm` sizing on the T4 (the arena is `w*h*1.5 * 2 views * 2 slots`
   summed over monitors: 73.8 MiB at 2560×1440 + 3840×2400).
4. **#48 frame_num-wrap re-key** (`e928914a` → `b3521842`): at ~65 000
   pairs the encoder pair is re-keyed. It is **bitstream-only** now — the
   EGFX surface teardown was falsified onscreen on macOS (black flash at
   every boundary) and `ltr_rekey_surface_reset` defaults to **false**.
   **[WATCH]** — one full-frame update plus ~1 s pause per boundary
   (~18 min of continuous 30 fps animation), and it must NOT black.
5. **#52 log clock fix** (`16f8f46f`): `common/log.c` printed microseconds
   as the millisecond field. Log-only; no wire or timing effect, but it is
   why timestamps in these captures can be trusted.
6. Harness/bench only: `codeflood`/`grayflood` payloads, `E5_BASE_MS`,
   `e52_flood_analyze.py`, the smoke gate's pod port fix, arms r/s/t.

### Onscreen watch list — Windows App (UWP) and macOS

The structural invariants are checked offline (wire audit, DPB simulator,
black-frame check), and they pass. What a human adds is the behaviour no
bitstream check can see: how a **real decoder implementation** reacts. Watch
for these, in this order of likelihood.

| # | what to do | what a fault looks like | mechanism, if it happens |
|---|---|---|---|
| 1 | Leave a scrolling/animated window running **4–5 minutes** and watch for a rhythmic glitch | corruption, a freeze, or a flash arriving at a **regular cadence** (every 240 encoded pairs — a few seconds at 4K, so it will look periodic) | the new mid-stream non-IDR I refresh. Everything else in that picture (nri=3, mmco6 self-mark, explicit rplm, per-view frame_num stride) already shipped and renders on all three clients; the I-at-a-cut is new. A decoder that mishandles it corrupts AT THE CUT CADENCE — that periodicity is the signature |
| 2 | **Dual monitor**, drag a window across the boundary, then leave one monitor busy and the **other completely static** | one monitor lags or stutters while the other is fine; or the busy monitor gets slower when the second monitor is merely present | the shared deadline couples the set. Measured on the dev box: with one active and one idle monitor the batch is **~9 % slower** than serialized (BACKLOG #53). It should be invisible onscreen — if it is not, that is the report |
| 3 | Keep an animation running **~18 minutes** without disconnecting | a **black flash**, a blank surface, or a wedge at the boundary | the frame_num-wrap re-key. Surface churn is masked by default now (macOS blacked on churn before); the expected appearance is a single full-frame repaint and a ~1 s pause |
| 4 | Resize the session / reconnect at a different size; look along **vertical colour edges** | a comb/"burr" split on chroma edges | `chroma_align` (16 FreeRDP / 32 mstsc). Alignment differs per client — this is a per-client contract, so it must be checked on **each** client, not once |
| 5 | Look at **saturated colour text and fine red/blue stripes** on both clients | washed-out or bleeding chroma = the stream fell back to 4:2:0 behaviour; wrong colours = a packing/parity fault | the smoke gate's edge-fidelity number covers the automated half (0.993 on the dev box, floor 0.50); the eye covers "the bar that says RED is blue" |
| 6 | macOS specifically: connect, disconnect, reconnect twice | a black screen on reconnect, or near-black with top-edge noise | the macOS Windows App has blacked on parameter-set duplication and on surface churn before; `sanitize_hrd` and the churn masking are the fixes. This is the client with the most history |
| 7 | UWP/Windows App specifically: **multimon** connect at the owner layout | one monitor never paints, or the two monitors show each other's content | monitor→surface mapping and the per-monitor capture slot (step 6) are new since the last T4 test |

If any of these fire, capture rather than iterate: note the wall-clock time,
run the oracle capture again at the same geometry, and audit the wire. A
failure that reproduces in a dump is a fixable bug; one that only exists in
a memory is not.

---

## 7. If a number comes back short

The stop rule from #52 carries over verbatim: **≥ 2.0× GREEN, ≥ 1.5×
AMBER, below that RED**, and a RED is attributed — capture pacing, vmsplice
feed, NUT demux, LTR rewrite, EGFX assembly — before anything ships.
Nothing is re-tuned to make the number look better, no arm is swapped for a
different encoder to get a green, and if the instance dies mid-campaign the
honest report is "blocked on real capture" rather than a dev-box number
presented as a T4 result (CLAUDE.md strict-honesty rule).

`e52_flood_analyze.py` is the attribution tool: worker busy %, per-pair
service split into encode-collected and rewrite/emit, and the
`last=1 → next own dmg` wait that says whether the pipeline was full at all.
On the dev box at 2.13× the worker was still only 32 % busy, so a T4 result
below 2× is at least as likely to be capture-side as encoder-side — and the
decomposition will say which.
